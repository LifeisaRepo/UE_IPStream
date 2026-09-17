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
		UE_LOG(LogIPStreamMedia, Error, TEXT("Open: avformat_open_input() failed (ReturnCode = %d). Failed to open connection to the stream. -_-"), OpenResult);
		Close();
		return false;
	}

	SetDeadline(ConnectTimeoutSeconds);

	if (avformat_find_stream_info(FormatContext, nullptr) < 0)
	{
		UE_LOG(LogIPStreamMedia, Error, TEXT("Open: avformat_find_stream_info() failed. Failed to find stream info. -_-"));
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

	av_frame_free(&Frame);
	av_packet_free(&Packet);
	avcodec_free_context(&CodecContext);
	avformat_close_input(&FormatContext);	// Closes stream, frees FormatContext and sets it to nullptr

	VideoStreamIndex = INDEX_NONE;
	DeadlineSeconds = 0.0;

	// DO NOT RESET bStopRequested HERE!
	// It's supposed to decide what happens after Close()
}

bool FIPStreamFFmpegSession::IsOpen() const
{
	return FormatContext != nullptr;
}

const AVFrame* FIPStreamFFmpegSession::GetFrame() const
{
	return Frame;
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



