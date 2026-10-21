// Copyright (c) 2026 Sanjyot Dahale. Licensed under the MIT License — see LICENSE.md.


#include "IPStreamFFmpegSession.h"

#include "IIPStreamMediaModule.h"
#include "HAL/PlatformTime.h"

// FFmpeg includes. `extern C` is needed as FFmpeg is written in C
// C++ compilers mangle function names in C code without this
extern "C"
{
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/avutil.h"
#include "libavutil/dict.h"
#include "libavutil/mathematics.h"
#include "libswscale/swscale.h"
}

namespace
{
	/**
	* RTSP handshake budget.
	* Deadline for the interrupt callback while opening the connection.
	*/ 
	constexpr double ConnectTimeoutSeconds = 6.0;

	/**
	* Deadline for the interrupt callback while reading new AVPacket.
	* Assuming GOP = 50 frames @ 25 FPS = 2 sec. (because that's what my camera has)
	* We wait atmost till a new keyframe should potentially arrive. 
	*/
	constexpr double ReadTimeoutSeconds = 2.0;

	// The unit of FTimespan: 100ns per tick. The target unit for av_rescale_q()
	const AVRational UnrealTicks{ 1, static_cast<int>(ETimespan::TicksPerSecond) };

	// A frame's duration only if neither frame not stream provides it. 40ms is 25FPS.
	constexpr int64 FallbackDurationTicks = 40 * ETimespan::TicksPerMillisecond;

	// How many frames of each connection get a FrameInfo log line.
	constexpr int32 FramesToLog = 5;	

}

//~ Ctor & Dtor

// Every member has it's intial value in the header
FIPStreamFFmpegSession::FIPStreamFFmpegSession() = default;


// Must live in C++ as FFmpeg header are included here,
// which are need for the closing/freeing sequence
FIPStreamFFmpegSession::~FIPStreamFFmpegSession()
{
	Close();
}


//~ Open / decode / close

bool FIPStreamFFmpegSession::Open(const FString& Url)
{
	// Ensure previous connection is closed in case of a reconnect
	Close();

	const FTCHARToUTF8 UrlUtf8(*Url);

	// Create FormatContext
	FormatContext = avformat_alloc_context();
	if (!FormatContext)
	{
		UE_LOG(LogIPStreamMedia, Error, TEXT("Open: avformat_alloc_context() failed. Failed to create new FormatContext. -_-"));
		return false;
	}

	// Setup the interrupt callback
	FormatContext->interrupt_callback.callback = &FIPStreamFFmpegSession::InterruptCallback;
	FormatContext->interrupt_callback.opaque = this;

	// Setup the dictionary
	AVDictionary* Options = nullptr;
	av_dict_set(&Options, "rtsp_transport", "tcp", 0);
	av_dict_set(&Options, "probesize", "32768", 0);

	// Setting the interrupt deadline for opening a new connection - Very Important
	SetDeadline(ConnectTimeoutSeconds);
	
	// Try to open the stream
	const int OpenResult = avformat_open_input(&FormatContext, UrlUtf8.Get(), nullptr, &Options);

	// Free the dictionary & nullptr the variable
	av_dict_free(&Options);

	if (OpenResult < 0)
	{
		if (IsStopRequested())
		{
			UE_LOG(LogIPStreamMedia, Display, TEXT("Open: avformat_open_input() Stopped by request while connecting (ReturnCode = %d)."), OpenResult);
		}
		else
		{
			UE_LOG(LogIPStreamMedia, Error, TEXT("Open: avformat_open_input() failed (ReturnCode = %d). Failed to open connection to the stream. -_-"), OpenResult);
		}
		Close();
		return false;
	}

	SetDeadline(ConnectTimeoutSeconds);
	const int StreamInfoResult = avformat_find_stream_info(FormatContext, nullptr);
	if (StreamInfoResult < 0)
	{
		if (IsStopRequested())
		{
			UE_LOG(LogIPStreamMedia, Display, TEXT("Open: avformat_find_stream_info() Stopped by request while connecting (ReturnCode = %d)."), StreamInfoResult);
		}
		else
		{
			UE_LOG(LogIPStreamMedia, Error, TEXT("Open: avformat_find_stream_info() failed (ReturnCode = %d). Failed to find stream info. -_-"), StreamInfoResult);
		}
		Close();
		return false;
	}

	// Loop through every stream present in the connection
	for (uint32 i = 0; i < FormatContext->nb_streams; ++i)
	{
		// Check if there's a video stream present
		if (FormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			// Find the first video stream index, save it and exit loop
			VideoStreamIndex = static_cast<int32>(i);
			break;
		}
	}

	if (VideoStreamIndex == INDEX_NONE)
	{
		UE_LOG(LogIPStreamMedia, Error, TEXT("Open: No video stream found in the connection. -_-"));
		Close();
		return false;
	}

	// Get the codec params
	const AVCodecParameters* CodecParams = FormatContext->streams[VideoStreamIndex]->codecpar;
	
	// Find decoder as per the codec id
	const AVCodec* Decoder = avcodec_find_decoder(CodecParams->codec_id);
	if (!Decoder)
	{
		UE_LOG(LogIPStreamMedia, Error, TEXT("Open: No decoder for codec id: %d. -_-"), CodecParams->codec_id);
		Close();
		return false;
	}

	// Create CodecContext
	CodecContext = avcodec_alloc_context3(Decoder);
	// Check if CodecContext is valid and fill it up with the CodecParams
	if (!CodecContext || avcodec_parameters_to_context(CodecContext, CodecParams) < 0)
	{
		UE_LOG(LogIPStreamMedia, Error, TEXT("Open: Failed to build codec context. -_-"));
		Close();
		return false;
	}

	// Tell the decoder what unit the packet timestamps are in.
	const AVStream* VideoStream = FormatContext->streams[VideoStreamIndex];
	CodecContext->pkt_timebase = VideoStream->time_base;

	// Metadata GetFrameInfo() needs
	TimebaseNum = VideoStream->time_base.num;
	TimebaseDen = VideoStream->time_base.den;

	const AVRational FrameRate = VideoStream->avg_frame_rate;

	FramePeriod = (FrameRate.num > 0 && FrameRate.den > 0) 
		? FTimespan(static_cast<int64>(FrameRate.den) * ETimespan::TicksPerSecond / FrameRate.num) 
		: FTimespan::Zero();

	// Setup the decoder in memory
	if (avcodec_open2(CodecContext, Decoder, nullptr) < 0)
	{
		UE_LOG(LogIPStreamMedia, Error, TEXT("Open: avcodec_open2() failed. Failed to setup the decoder. -_-"));
		Close();
		return false;
	}


	// Allocate an AVPacket and an AVFrame.
	// Once allocated they will be re-used for the lifetime of this connection
	Packet = av_packet_alloc();
	Frame = av_frame_alloc();

	if (!Packet || !Frame)
	{
		UE_LOG(LogIPStreamMedia, Error, TEXT("Open: Packet/Frame allocation has failed. -_-"));
		Close();
		return false;
	}

	// If we have reached here then everything went well,
	return true;
}

EIPStreamDecodeResult FIPStreamFFmpegSession::DecodeNext()
{
	// Decoder called but connection to stream is not open
	if (!IsOpen())
	{
		return EIPStreamDecodeResult::Error;
	}

	// Does the decoder have a frame for us ?
	const int ReceiveResult = avcodec_receive_frame(CodecContext, Frame);

	if (ReceiveResult == 0)
	{
		// Received a new decoded frame
		return EIPStreamDecodeResult::GotFrame;
	}
	if (ReceiveResult == AVERROR_EOF)
	{
		// Stream has already ended
		return EIPStreamDecodeResult::StreamEnded;
	}
	if (ReceiveResult != AVERROR(EAGAIN))
	{
		UE_LOG(LogIPStreamMedia, Error, TEXT("DecodeNext: avcodec_receive_frame() has failed. ReturnCode = %d. -_-"), ReceiveResult);
		return EIPStreamDecodeResult::Error;
	}
	
	// Now that decoder is empty, read a new AVPacket
	
	// Set deadline for interrupt callback
	SetDeadline(ReadTimeoutSeconds);
	const int ReadResult = av_read_frame(FormatContext, Packet);
	
	// Program exited out of av_read_frame() for some reason
	if (ReadResult == AVERROR_EXIT)
	{
		// Either a Stop was requested, or else the callback was fired on timeout.
		return IsStopRequested() ? EIPStreamDecodeResult::Aborted : EIPStreamDecodeResult::Error;
	}
	if (ReadResult == AVERROR_EOF)
	{
		return EIPStreamDecodeResult::StreamEnded;
	}
	if (ReadResult < 0)
	{
		UE_LOG(LogIPStreamMedia, Error, TEXT("DecodeNext: av_read_frame() failed to read a new AVPacket. ReturnCode = %d. -_-"), ReadResult);
		return EIPStreamDecodeResult::Error;
	}

	// Ignore and wipe Packet unless it contains video stream packets
	if (Packet->stream_index != VideoStreamIndex)
	{
		// Delete the content inside the Packet but keep the container.
		av_packet_unref(Packet);
		return EIPStreamDecodeResult::NoFrameYet;
	}
	
	// Send the new packet to decoder
	const int SendResult = avcodec_send_packet(CodecContext, Packet);
	
	// Wipe packet immediately for re-use
	av_packet_unref(Packet);
	if (SendResult < 0)
	{
		/**
		* AVERROR(EAGAIN) is impossible here as we cannot reach this point
		* unless the decoder is empty, i.e. it has no more frames to give.
		* 
		* Hence if we face an error here we need to log it loudly,
		* !becoz it shood not eRoR!
		*/ 

		// AVERROR(EAGAIN) should return code '-11'
		UE_LOG(LogIPStreamMedia, Error, TEXT("DecodeNext: avcodec_send_packet() has failed. ReturnCode = %d. -_-"), SendResult);
		return EIPStreamDecodeResult::Error;
	}

	// We have pushed a new packet, but no frame is received - which is expected
	return EIPStreamDecodeResult::NoFrameYet;

}

void FIPStreamFFmpegSession::Close()
{
	// Reverse order of allocation.
	// These functions also ensure that the variable is nullptr

	sws_freeContext(ScaleContext);
	ScaleContext = nullptr;

	av_frame_free(&Frame);
	av_packet_free(&Packet);
	avcodec_free_context(&CodecContext);
	avformat_close_input(&FormatContext);	// Closes stream, frees FormatContext and sets it to nullptr

	VideoStreamIndex = INDEX_NONE;
	TimebaseNum = 0;
	TimebaseDen = 0;
	FramePeriod = FTimespan::Zero();
	bHaveLastFrame = false;
	LastTime = FTimespan::Zero();
	LastDuration = FTimespan::Zero();
	FramesLogged = 0;
	DeadlineSeconds = 0.0;

	// DO NOT RESET bStopRequested HERE!
	// It's supposed to decide what happens after Close()
}

bool FIPStreamFFmpegSession::IsOpen() const
{
	return FormatContext != nullptr;
}

bool FIPStreamFFmpegSession::GetFrameInfo(FIPStreamFrameInfo& OutInfo)
{
	if (!Frame || Frame->width <= 0 || Frame->height <= 0)
	{
		return false;
	}

	const bool bTimebaseKnown = TimebaseNum > 0 && TimebaseDen > 0;
	const AVRational Timebase{ TimebaseNum, TimebaseDen };

	// Time: PTS(Level 0), then best_effor_timestamp(Level 1), then last frame's time + duration(Level 2), then zero (Level 3)
	int32 TimeLevel = 0;	// Used for logging only
	int64 Pts = Frame->pts;
	if (Pts == AV_NOPTS_VALUE)
	{
		Pts = Frame->best_effort_timestamp;
		TimeLevel = 1;
	}

	FTimespan Time = FTimespan::Zero();
	if (Pts != AV_NOPTS_VALUE && bTimebaseKnown)
	{
		Time = FTimespan(av_rescale_q(Pts, Timebase, UnrealTicks));
	}
	else if (bHaveLastFrame)
	{
		Time = LastTime + LastDuration;
		TimeLevel = 2;
	}
	else
	{
		TimeLevel = 3;
	}

	// Duration: frame->duration, then the stream's frame period, then 40ms. Levels same as time
	// Validity checks happen on FTimespan instead of the raw value as a positive raw value can still convert to zero.
	int32 DurationLevel = 0;	// Used for logging only
	FTimespan Duration = FTimespan::Zero();
	if (Frame->duration > 0 && bTimebaseKnown)
	{
		Duration = FTimespan(av_rescale_q(Frame->duration, Timebase, UnrealTicks));
	}
	if (Duration <= FTimespan::Zero())
	{
		Duration = FramePeriod;
		DurationLevel = 1;
	}
	if (Duration <= FTimespan::Zero())
	{
		Duration = FTimespan(FallbackDurationTicks);
		DurationLevel = 2;
	}

	if (FramesLogged < FramesToLog)
	{
		++FramesLogged;

		//colorspace: 1 = BT.709, 5 or 6 = BT.601, 2 = unspecified.
		//range: 1 = limited("tv"), 2 = full, 0 = unspecified.
		UE_LOG(LogIPStreamMedia, Display, TEXT("FrameInfo %d: pts = %lld best_effort = %lld duration = %lld time_base = %d / %d->Time = %lld ticks(level %d), Duration = %lld ticks(level %d), Colorspace=%d, Range=%d. ^_^ "),
			FramesLogged, Frame->pts, Frame->best_effort_timestamp, Frame->duration, TimebaseNum, TimebaseDen, Time.GetTicks(), TimeLevel, Duration.GetTicks(), DurationLevel, static_cast<int32>(Frame->colorspace), static_cast<int32>(Frame->color_range));
	}

	LastTime = Time;
	LastDuration = Duration;
	bHaveLastFrame = true;

	OutInfo.Dim = FIntPoint(Frame->width, Frame->height);
	OutInfo.Time = Time;
	OutInfo.Duration = Duration;

	return true;
}

bool FIPStreamFFmpegSession::ConvertFrameToBGRA(uint8* Dst, int32 DstStride)
{
	if (!Frame || !Dst || DstStride <= 0 || Frame->width <= 0 || Frame->height <= 0)
	{
		return false;
	}

	// Reuses the ScaleContext while the frame's size and format stay the same.
	// Rebuilds it if they change. Always keep what it returns, on rebuild the old pointer is freed automatically.
	ScaleContext = sws_getCachedContext(ScaleContext,
		Frame->width, Frame->height, static_cast<AVPixelFormat>(Frame->format),
		Frame->width, Frame->height, AV_PIX_FMT_BGRA,
		SWS_BILINEAR, nullptr, nullptr, nullptr);

	if (!ScaleContext)
	{
		UE_LOG(LogIPStreamMedia, Error, TEXT("ConvertFrameToBGRA: sws_getCachedContext() failed. -_-"));
		return false;
	}

	uint8* DstPlanes[1] = { Dst };
	int32 DstStrides[1] = { DstStride };
	const int OutputHeight = sws_scale(ScaleContext, Frame->data, Frame->linesize, 0, Frame->height, DstPlanes, DstStrides);

	return OutputHeight == Frame->height;
}

//~ Thread boundary

void FIPStreamFFmpegSession::RequestStop()
{
	// Set bStopRequested atomic in relaxed mode

	bStopRequested.store(true, std::memory_order_relaxed);
}

bool FIPStreamFFmpegSession::IsStopRequested() const
{
	return bStopRequested.load(std::memory_order_relaxed);
}

//~ Private

// Has to be static to fit into interrupt callbacks signature (which is based in C)
int FIPStreamFFmpegSession::InterruptCallback(void* Opaque)
{
	const FIPStreamFFmpegSession* Session = static_cast<const FIPStreamFFmpegSession*>(Opaque);

	// If a stop is requested...
	if (Session->IsStopRequested())
	{
		// ...Interrupt
		return 1;
	}

	// Else wait for timeout
	return FPlatformTime::Seconds() > Session->DeadlineSeconds ? 1 : 0;
}

void FIPStreamFFmpegSession::SetDeadline(double TimeoutSeconds)
{
	DeadlineSeconds = FPlatformTime::Seconds() + TimeoutSeconds;
}



