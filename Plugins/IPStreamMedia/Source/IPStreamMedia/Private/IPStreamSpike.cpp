// Copyright (c) 2026 Sanjyot Dahale. Licensed under the MIT License — see LICENSE.md.


#include "IPStreamSpike.h"
#include "IIPStreamMediaModule.h"
#include "Engine/Texture2D.h"
#include "HAL/PlatformTime.h"

// FFmpeg is written in C, so we need to wrap the includes in an extern "C" block to prevent name mangling by C++ compiler.
// Without this the names coming from the header files will not match the names coming from the DLLs.
extern "C"
{
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/avutil.h"
#include "libavutil/dict.h"
#include "libswscale/swscale.h"
}

// Anonymous namespace
namespace
{
	// Deadline state for AVIOInterruptCB
	struct FInterruptState
	{
		double DeadlineSeconds = 0.0;
	};

	// Callback for FFmpeg's AVIOInterruptCB, which is used to interrupt blocking I/O operations if they take too long.
	int CheckInterruptDeadline(void* Opaque)
	{
		const FInterruptState* State = static_cast<FInterruptState*>(Opaque);
		return FPlatformTime::Seconds() > State->DeadlineSeconds ? 1 : 0;
	}

	// Converts one decoded frame into BGRA UTexture2D. Returns nullptr on failure.
	UTexture2D* BuildTextureFromFrame(const AVFrame* Frame)
	{
		SwsContext* ScaleContext = sws_getContext(
			Frame->width, Frame->height, static_cast<AVPixelFormat>(Frame->format),
			Frame->width, Frame->height, AV_PIX_FMT_BGRA,
			SWS_BILINEAR, nullptr, nullptr, nullptr);

		if (!ScaleContext)
		{
			UE_LOG(LogIPStreamMedia, Error, TEXT("BuildTextureFromFrame: sws_getContext() failed"));
			return nullptr;
		}

		const int32 DestStride = Frame->width * 4;
		TArray<uint8> BgraBuffer;
		BgraBuffer.SetNumUninitialized(DestStride * Frame->height);

		uint8* DestPlanes[1] = { BgraBuffer.GetData() };
		int32 DestStrides[1] = { DestStride };
		sws_scale(ScaleContext, Frame->data, Frame->linesize, 0, Frame->height, DestPlanes, DestStrides);
		sws_freeContext(ScaleContext);

		// PF_B8G8R8A8 is CreateTransient's default pixel format - which matches AV_PIX_FMT_BGRA byte-for-byte, so we can use it directly.
		UTexture2D* Texture = UTexture2D::CreateTransient(Frame->width, Frame->height, PF_B8G8R8A8);
		if (!Texture)
		{
			UE_LOG(LogIPStreamMedia, Error, TEXT("BuildTextureFromFrame: CreateTransient() failed"));
			return nullptr;
		}

		// CreateTransient allocated Mip 0 buffer unintialized (never touches the GPU). 
		// We write into it and upload it using UpdateResource().
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		void* MipData = Mip.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(MipData, BgraBuffer.GetData(), BgraBuffer.Num());
		Mip.BulkData.Unlock();

		Texture->UpdateResource();
		return Texture;

	}
}

UTexture2D* UIPStreamSpike::GrabOneFrame(const FString& RtspUrl)
{
	// Converting UTF-16 FString to UTF-8 std::string for FFmpeg
	const FTCHARToUTF8 UrlUtf8(*RtspUrl);

	// Allocating format context ourselves, so we can set the interrupt callback
	// before the blocking "open" call starts.
	AVFormatContext* FormatContext = avformat_alloc_context();

	FInterruptState InterruptState;
	InterruptState.DeadlineSeconds = FPlatformTime::Seconds() + 6.0;	// Intentionally larger timeout for first round of testing.
	FormatContext->interrupt_callback.callback = &CheckInterruptDeadline;
	FormatContext->interrupt_callback.opaque = &InterruptState;

	AVDictionary* Options = nullptr;
	av_dict_set(&Options, "rtsp_transport", "tcp", 0);		// Using TCP for RTSP
	av_dict_set(&Options, "probesize", "32768", 0);			// 32KB probe size
	av_dict_set(&Options, "analyzeduration", "0", 0);		// Useless code - default is already 0, but it doesn't disable analysis - it lets avformat choose what to do.

	if (avformat_open_input(&FormatContext, UrlUtf8.Get(), nullptr, &Options) < 0)
	{
		UE_LOG(LogIPStreamMedia, Error, TEXT("GrabOneFrame: Failed to open: %s"), *RtspUrl);
		av_dict_free(&Options);
		return nullptr;		// avformat_open_input() has already freed FormatContext on failure, so we don't free it here.
	}

	av_dict_free(&Options);

	if (avformat_find_stream_info(FormatContext, nullptr) < 0)
	{
		UE_LOG(LogIPStreamMedia, Error, TEXT("GrabOneFrame: Failed to read stream info."));
		avformat_close_input(&FormatContext);
		return nullptr;
	}

	int32 VideoStreamIndex = -1;
	for (uint32 Index = 0; Index < FormatContext->nb_streams; ++Index)
	{
		if(FormatContext->streams[Index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			VideoStreamIndex = Index;
			break;
		}
	}
	if (VideoStreamIndex == -1)
	{
		UE_LOG(LogIPStreamMedia, Error, TEXT("GrabOneFrame: No video stream in %s"), *RtspUrl);
		avformat_close_input(&FormatContext);
		return nullptr;
	}

	AVCodecParameters* CodecParams = FormatContext->streams[VideoStreamIndex]->codecpar;
	const AVCodec* Decoder = avcodec_find_decoder(CodecParams->codec_id);
	if (!Decoder)
	{
		UE_LOG(LogIPStreamMedia, Error, TEXT("GrabOneFrame: No decoder available for this codec"));
		avformat_close_input(&FormatContext);
		return nullptr;
	}

	AVCodecContext* CodecContext = avcodec_alloc_context3(Decoder);
	avcodec_parameters_to_context(CodecContext, CodecParams);

	if (avcodec_open2(CodecContext, Decoder, nullptr) < 0)
	{
		UE_LOG(LogIPStreamMedia, Error, TEXT("GrabOneFrame: Failed to open decoder"));
		avcodec_free_context(&CodecContext);
		avformat_close_input(&FormatContext);
		return nullptr;
	}

	AVPacket* Packet = av_packet_alloc();
	AVFrame* Frame = av_frame_alloc();
	UTexture2D* ResultTexture = nullptr;

	// Packets and frames aren't 1:1 - keep feeding packets
	// from the video stream until one actually yeilds a frame.
	while (av_read_frame(FormatContext, Packet) >= 0)
	{
		if (Packet->stream_index == VideoStreamIndex &&
			avcodec_send_packet(CodecContext, Packet) == 0 &&
			avcodec_receive_frame(CodecContext, Frame) == 0)
		{
			ResultTexture = BuildTextureFromFrame(Frame);
			av_packet_unref(Packet);
			break;
		}

		av_packet_unref(Packet);
	}

	av_frame_free(&Frame);
	av_packet_free(&Packet);
	avcodec_free_context(&CodecContext);
	avformat_close_input(&FormatContext);

	return ResultTexture;
}
