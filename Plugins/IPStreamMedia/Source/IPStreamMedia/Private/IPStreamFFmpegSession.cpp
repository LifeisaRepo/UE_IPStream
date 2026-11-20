// Copyright (c) 2026 Sanjyot Dahale. Licensed under the MIT License — see LICENSE.md.


#include "IPStreamFFmpegSession.h"

#include "IIPStreamMediaModule.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

// FFmpeg includes. `extern C` is needed as FFmpeg is written in C
// C++ compilers mangle function names in C code without this
extern "C"
{
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/avutil.h"
#include "libavutil/dict.h"
#include "libavutil/mathematics.h"
#include "libavutil/samplefmt.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
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

	// How many audio frames of each connection to get an AudioInfo log line
	constexpr int32 AudioFramesToLog = 5;

	// How many drops get a warning each, before we switch to one warning every AudioDropsPerLog
	constexpr uint64 AudioDropsWarnedInFull = 3;
	// After the first AudioDropsWarnedInFull warnings, how many audio drops before logging a warning
	constexpr uint64 AudioDropsPerLog = 100;

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
		UE_LOG(LogIPStreamMedia, Error, TEXT("Open: avformat_alloc_context() failed. Failed to create new FormatContext."));
		return false;
	}

	// Setup the interrupt callback
	FormatContext->interrupt_callback.callback = &FIPStreamFFmpegSession::InterruptCallback;
	FormatContext->interrupt_callback.opaque = this;

	// Setup the dictionary
	AVDictionary* Options = nullptr;
	av_dict_set(&Options, "rtsp_transport", "tcp+udp", 0);	// Accepts both TCP and UDP
	av_dict_set(&Options, "rtsp_flags", "prefer_tcp", 0);	// But we prefer TCP because UDP can lose packets
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
			UE_LOG(LogIPStreamMedia, Error, TEXT("Open: avformat_open_input() failed (ReturnCode = %d). Failed to open connection to the stream."), OpenResult);
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
			UE_LOG(LogIPStreamMedia, Error, TEXT("Open: avformat_find_stream_info() failed (ReturnCode = %d). Failed to find stream info."), StreamInfoResult);
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
		UE_LOG(LogIPStreamMedia, Error, TEXT("Open: No video stream found in the connection."));
		Close();
		return false;
	}

	// Get the codec params
	const AVCodecParameters* CodecParams = FormatContext->streams[VideoStreamIndex]->codecpar;
	
	// Find decoder as per the codec id
	const AVCodec* Decoder = avcodec_find_decoder(CodecParams->codec_id);
	if (!Decoder)
	{
		UE_LOG(LogIPStreamMedia, Error, TEXT("Open: No decoder for codec id: %d."), CodecParams->codec_id);
		Close();
		return false;
	}

	// Create CodecContext
	CodecContext = avcodec_alloc_context3(Decoder);
	// Check if CodecContext is valid and fill it up with the CodecParams
	if (!CodecContext || avcodec_parameters_to_context(CodecContext, CodecParams) < 0)
	{
		UE_LOG(LogIPStreamMedia, Error, TEXT("Open: Failed to build codec context."));
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

	// Ensures that the opaque inside a packet is copied into the corresponding frame after decoding
	// Opaque holds our profiling timestamp
	CodecContext->flags |= AV_CODEC_FLAG_COPY_OPAQUE;

	// Setup the decoder in memory
	if (avcodec_open2(CodecContext, Decoder, nullptr) < 0)
	{
		UE_LOG(LogIPStreamMedia, Error, TEXT("Open: avcodec_open2() failed. Failed to setup the decoder."));
		Close();
		return false;
	}


	// Allocate an AVPacket and an AVFrame.
	// Once allocated they will be re-used for the lifetime of this connection
	Packet = av_packet_alloc();
	Frame = av_frame_alloc();

	if (!Packet || !Frame)
	{
		UE_LOG(LogIPStreamMedia, Error, TEXT("Open: Packet/Frame allocation has failed."));
		Close();
		return false;
	}

	// Audio is optional
	OpenAudioDecoder();

	OpenedSeconds = FPlatformTime::Seconds();

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

	// Does the video decoder have a frame for us ?
	int ReceiveResult = 0;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(IPStream_Receive);
		ReceiveResult = avcodec_receive_frame(CodecContext, Frame);
	}

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
		UE_LOG(LogIPStreamMedia, Error, TEXT("DecodeNext: avcodec_receive_frame() has failed. ReturnCode = %d."), ReceiveResult);
		return EIPStreamDecodeResult::Error;
	}

	// Does the audio decoder have a frame for us ?
	if (AudioCodecContext)
	{
		const int AudioReceiveResult = avcodec_receive_frame(AudioCodecContext, AudioFrame);

		if (AudioReceiveResult == 0)
		{
			return EIPStreamDecodeResult::GotAudio;
		}
		if (AudioReceiveResult != AVERROR(EAGAIN) && AudioReceiveResult != AVERROR_EOF)
		{
			DropAudioPacket(TEXT("avcodec_receive_frame"), AudioReceiveResult);
		}
	}
	
	// Now that decoder is empty, read a new AVPacket
	
	// Set deadline for interrupt callback
	SetDeadline(ReadTimeoutSeconds);
	int ReadResult = 0;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(IPStream_Read);
		ReadResult = av_read_frame(FormatContext, Packet);
	}
	
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
		UE_LOG(LogIPStreamMedia, Error, TEXT("DecodeNext: av_read_frame() failed to read a new AVPacket. ReturnCode = %d."), ReadResult);
		return EIPStreamDecodeResult::Error;
	}

	if (av_packet_get_side_data(Packet, AV_PKT_DATA_RTCP_SR, nullptr) != nullptr)
	{
		NoteSenderReport(Packet->stream_index);
	}

	// Send audio packet to audio decoder
	if (AudioCodecContext && Packet->stream_index == AudioStreamIndex)
	{
		const int AudioSendResult = avcodec_send_packet(AudioCodecContext, Packet);
		av_packet_unref(Packet);
		if (AudioSendResult < 0)
		{
			DropAudioPacket(TEXT("avcodec_send_packet"), AudioSendResult);
		}
		return EIPStreamDecodeResult::NoFrameYet;
	}

	// Ignore and wipe Packet unless it contains video stream packets
	if (Packet->stream_index != VideoStreamIndex)
	{
		// Delete the content inside the Packet but keep the container.
		av_packet_unref(Packet);
		return EIPStreamDecodeResult::NoFrameYet;
	}

	// Set the timestamp inside the Packet
	// 
	// NOTE = This will only work on 64-bit platforms because we need 64-bit pointers, 
	// In 32-bit platforms the pointer is 32-bit so it will drop half the tick value.
	// 
	// uintptr_t = a number as big as the width of a pointer //
	Packet->opaque = reinterpret_cast<void*>(static_cast<uintptr_t>(FPlatformTime::Cycles64()));
	
	// Send the new packet to decoder
	int SendResult = 0;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(IPStream_Send);
		SendResult = avcodec_send_packet(CodecContext, Packet);
	}
	
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
		UE_LOG(LogIPStreamMedia, Error, TEXT("DecodeNext: avcodec_send_packet() has failed. ReturnCode = %d."), SendResult);
		return EIPStreamDecodeResult::Error;
	}

	// We have pushed a new packet, but no frame is received - which is expected
	return EIPStreamDecodeResult::NoFrameYet;

}

void FIPStreamFFmpegSession::Close()
{
	// Reverse order of allocation.
	// These functions also ensure that the variable is nullptr

	swr_free(&ResampleContext);
	av_channel_layout_uninit(&ResampleInLayout);

	av_frame_free(&AudioFrame);
	avcodec_free_context(&AudioCodecContext);

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

	AudioStreamIndex = INDEX_NONE;
	AudioTimebaseNum = 0;
	AudioTimebaseDen = 0;
	bHaveLastAudio = false;
	AudioLastTime = FTimespan::Zero();
	AudioLastDuration = FTimespan::Zero();
	AudioFramesLogged = 0;
	ResampleInFormat = -1;
	ResampleInRate = 0;

	bVideoSenderReportLogged = false;
	bAudioSenderReportLogged = false;
	OpenedSeconds = 0.0;

	DeadlineSeconds = 0.0;

	// DO NOT RESET bStopRequested HERE!
	// It's supposed to decide what happens after Close()
}

bool FIPStreamFFmpegSession::IsOpen() const
{
	return FormatContext != nullptr;
}

bool FIPStreamFFmpegSession::HasAudio() const
{
	return AudioCodecContext != nullptr;
}

FString FIPStreamFFmpegSession::GetCodecName() const
{
	return CodecContext ? FString(UTF8_TO_TCHAR(avcodec_get_name(CodecContext->codec_id))) : FString();
}

FString FIPStreamFFmpegSession::GetAudioCodecName() const
{
	return AudioCodecContext ? FString(UTF8_TO_TCHAR(avcodec_get_name(AudioCodecContext->codec_id))) : FString();
}

FString FIPStreamFFmpegSession::GetDecoderDelayInfo()
{
	if (!CodecContext)
	{
		return FString();
	}

	// has_b_frames: size of the decoder's reorder buffer in frames.
	// active_thread_type: 1 = frame threading, which also holds frames back;
	// 2 = slice threading, which doesn't; 0 = no threading
	return FString::Printf(TEXT("has_b_frames = %d, thread_count = %d, active_thread_type = %d"),
		CodecContext->has_b_frames, CodecContext->thread_count, CodecContext->active_thread_type);
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

	// Get timestamp out from the Frame
	OutInfo.PacketCycles = static_cast<uint64>(reinterpret_cast<uintptr_t>(Frame->opaque));

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

	// Currently my test stream returns colorspace as 'undefined', but its a HD video stream. (and usually streams are HD+)
	// So if the colorspace is not explicity set to BT.601 (meant for SD) we will always use BT.709 (meant for HD+)
	const bool bIsBt601 = Frame->colorspace == AVCOL_SPC_BT470BG || Frame->colorspace == AVCOL_SPC_SMPTE170M;
	const int* Coefficients = sws_getCoefficients(bIsBt601 ? SWS_CS_ITU601 : SWS_CS_ITU709);

	// swscale: 1 = full.FFmpeg : limited = 1, full = 2. Map it, never copy it.
	const int SrcFullRange = (Frame->color_range == AVCOL_RANGE_JPEG) ? 1 : 0;

	// sws_setColorspaceDetails uses 16.16 fixed point system for floating point values (bright, cont, sat).
	// 16-bits for whole number . 16-bits for fractional.
	// (1 << 16) = 65536 = 1.0f
	constexpr int NoChange = 1 << 16;
	if (sws_setColorspaceDetails(ScaleContext, Coefficients, SrcFullRange, Coefficients, 1, 0, NoChange, NoChange ) < 0)
	{
		UE_LOG(LogIPStreamMedia, Error, TEXT("ConvertFrameToBGRA: sws_setColorspaceDetails() failed. -_-"));
		return false;
	}

	uint8* DstPlanes[1] = { Dst };
	int32 DstStrides[1] = { DstStride };
	const int OutputHeight = sws_scale(ScaleContext, Frame->data, Frame->linesize, 0, Frame->height, DstPlanes, DstStrides);

	return OutputHeight == Frame->height;
}

bool FIPStreamFFmpegSession::GetAudioInfo(FIPStreamAudioInfo& OutInfo)
{
	if (!AudioFrame || AudioFrame->nb_samples <= 0 || AudioFrame->sample_rate <= 0 || AudioFrame->ch_layout.nb_channels <= 0)
	{
		return false;
	}

	const bool bTimebaseKnown = AudioTimebaseNum > 0 && AudioTimebaseDen > 0;
	const AVRational Timebase{ AudioTimebaseNum, AudioTimebaseDen };

	int64 Pts = AudioFrame->pts;
	if (Pts == AV_NOPTS_VALUE)
	{
		Pts = AudioFrame->best_effort_timestamp;
	}

	FTimespan Time = FTimespan::Zero();
	if (Pts != AV_NOPTS_VALUE && bTimebaseKnown)
	{
		Time = FTimespan(av_rescale_q(Pts, Timebase, UnrealTicks));
	}
	else if (bHaveLastAudio)
	{
		Time = AudioLastTime + AudioLastDuration;
	}

	// This frame's length, based on its own values
	const FTimespan Duration(static_cast<int64>(AudioFrame->nb_samples) * ETimespan::TicksPerSecond / AudioFrame->sample_rate);

	if (AudioFramesLogged < AudioFramesToLog)
	{
		++AudioFramesLogged;
		UE_LOG(LogIPStreamMedia, Display, TEXT("AudioInfo %d: pts = %lld time_base = %d / %d -> Time = %lld ticks, nb_samples = %d, rate = %d, channels = %d, format = %s."),
			AudioFramesLogged, AudioFrame->pts, AudioTimebaseNum, AudioTimebaseDen, Time.GetTicks(), AudioFrame->nb_samples,
			AudioFrame->sample_rate, AudioFrame->ch_layout.nb_channels,
			UTF8_TO_TCHAR(av_get_sample_fmt_name(static_cast<AVSampleFormat>(AudioFrame->format))));
	}

	AudioLastTime = Time;
	AudioLastDuration = Duration;
	bHaveLastAudio = true;

	OutInfo.Channels = AudioFrame->ch_layout.nb_channels;
	OutInfo.Frames = AudioFrame->nb_samples;
	OutInfo.SampleRate = AudioFrame->sample_rate;
	OutInfo.Time = Time;

	return true;
}

int32 FIPStreamFFmpegSession::ConvertAudioToFloat(float* Dst, int32 MaxFrames)
{
	if (!AudioFrame || !Dst || MaxFrames <= 0 || AudioFrame->nb_samples <= 0)
	{
		return 0;
	}

	// swsresample does not have a cached context helper as sws_getCachedContext, so we compare by hand:
	// rebuild if input's format, rate or channel layout differs from before
	const bool bSetupMatches = ResampleContext
		&& AudioFrame->format == ResampleInFormat
		&& AudioFrame->sample_rate == ResampleInRate
		&& av_channel_layout_compare(&AudioFrame->ch_layout, &ResampleInLayout) == 0;

	if (!bSetupMatches)
	{
		swr_free(&ResampleContext);
		av_channel_layout_uninit(&ResampleInLayout);

		const AVSampleFormat InFormat = static_cast<AVSampleFormat>(AudioFrame->format);
		if (swr_alloc_set_opts2(&ResampleContext,
			&AudioFrame->ch_layout, AV_SAMPLE_FMT_FLT ,AudioFrame->sample_rate, 
			&AudioFrame->ch_layout, InFormat, AudioFrame->sample_rate, 0, nullptr) < 0
			|| swr_init(ResampleContext) < 0)
		{
			swr_free(&ResampleContext);
			UE_LOG(LogIPStreamMedia, Error, TEXT("ConvertAudioToFloat: swresample setup failed."));
			return -1;
		}

		av_channel_layout_copy(&ResampleInLayout, &AudioFrame->ch_layout);
		ResampleInFormat = AudioFrame->format;
		ResampleInRate = AudioFrame->sample_rate;

		UE_LOG(LogIPStreamMedia, Display, TEXT("ConvertAudioToFloat: converter built: %d channels, %d Hz, %s -> flt."),
			AudioFrame->ch_layout.nb_channels, AudioFrame->sample_rate, UTF8_TO_TCHAR(av_get_sample_fmt_name(InFormat)));
	}

	// Using extended_data because planar audio with > 8 channels must use extended_data
	uint8_t* OutPlanes[1] = { reinterpret_cast<uint8_t*>(Dst) };
	const int Written = swr_convert(ResampleContext, OutPlanes, MaxFrames, AudioFrame->extended_data, AudioFrame->nb_samples);

	// Frames written per channel
	return static_cast<int32>(Written);
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

void FIPStreamFFmpegSession::OpenAudioDecoder()
{
	for (uint32 i = 0; i < FormatContext->nb_streams; i++)
	{
		if (FormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			AudioStreamIndex = static_cast<int32>(i);
			break;
		}
	}

	if (AudioStreamIndex == INDEX_NONE)
	{
		UE_LOG(LogIPStreamMedia, Display, TEXT("Open: No audio stream in the connection, video only."));
		return;
	}

	const AVStream* AudioStream = FormatContext->streams[AudioStreamIndex];
	const AVCodecParameters* AudioParams = AudioStream->codecpar;
	const AVCodec* AudioDecoder = avcodec_find_decoder(AudioParams->codec_id);

	AudioCodecContext = AudioDecoder ? avcodec_alloc_context3(AudioDecoder) : nullptr;
	bool bOk = AudioCodecContext && avcodec_parameters_to_context(AudioCodecContext, AudioParams) >= 0;

	if (bOk)
	{
		AudioCodecContext->pkt_timebase = AudioStream->time_base;
		bOk = avcodec_open2(AudioCodecContext, AudioDecoder, nullptr) >= 0;
	}
	if (bOk)
	{
		AudioFrame = av_frame_alloc();
		bOk = AudioFrame != nullptr;
	}

	if (!bOk)
	{
		UE_LOG(LogIPStreamMedia, Warning, TEXT("Open: Audio stream %d (codec id %d) could not be opened, video only."), AudioStreamIndex, AudioParams->codec_id);
		av_frame_free(&AudioFrame);
		avcodec_free_context(&AudioCodecContext);
		AudioStreamIndex = INDEX_NONE;
		return;
	}

	AudioTimebaseNum = AudioStream->time_base.num;
	AudioTimebaseDen = AudioStream->time_base.den;

	UE_LOG(LogIPStreamMedia, Display, TEXT("Open: Audio stream %d opened: %s."), AudioStreamIndex, *GetAudioCodecName());
}

void FIPStreamFFmpegSession::NoteSenderReport(int32 StreamIndex)
{
	bool* bLogged = nullptr;
	const TCHAR* Kind = nullptr;

	if (StreamIndex == VideoStreamIndex)
	{
		bLogged = &bVideoSenderReportLogged;
		Kind = TEXT("video");
	}
	else if (StreamIndex == AudioStreamIndex)
	{
		bLogged = &bAudioSenderReportLogged;
		Kind = TEXT("audio");
	}

	if (!bLogged || *bLogged)
	{
		return;
	}
	*bLogged = true;

	UE_LOG(LogIPStreamMedia, Display, TEXT("RTCP: first sender report on the %s stream (%d), %.3f s after Open."),
		Kind, StreamIndex, FPlatformTime::Seconds() - OpenedSeconds);
}

void FIPStreamFFmpegSession::DropAudioPacket(const TCHAR* Where, int ReturnCode)
{
	++AudioPacketsDropped;

	if (AudioPacketsDropped <= AudioDropsWarnedInFull || AudioPacketsDropped % AudioDropsPerLog == 0)
	{
		UE_LOG(LogIPStreamMedia, Warning, TEXT("DecodeNext: audio %s() failed (ReturnCode = %d), packet dropped (%llu dropped so far). -_-"),
			Where, ReturnCode, AudioPacketsDropped);
	}
}



