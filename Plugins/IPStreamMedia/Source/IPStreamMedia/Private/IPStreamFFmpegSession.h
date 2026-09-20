// Copyright (c) 2026 Sanjyot Dahale. Licensed under the MIT License — see LICENSE.md.

#pragma once

#include "CoreMinimal.h"

#include <atomic>

// Forward declarartions instead of includes
// Apart from the usual reasons, avformat.h pulls a large tree; keeping it out of
// a header keeps it from being re-parsed everywhere
struct AVFormatContext;
struct AVCodecContext;
struct AVPacket;
struct AVFrame;

/**
* Outcome of one FIPStreamFFmpegSession::DecodeNext() call.
* The worker loop branches on this.
*/
enum class EIPStreamDecodeResult : uint8
{
	// A decoded frame is available from GetFrame(), until the next DecodeNext().
	GotFrame,

	// Packets were fed in, no frames came out yet. Normal. Call again.
	NoFrameYet,

	// RequestStop() was seen. Unwind; Do not retry, do not reconnect.
	Aborted,

	// The stream ended or the connection dropped. Close, then reconnect.
	StreamEnded,

	// Read or decode failed in a way retrying won't fix. Close, then reconnect.
	Error
};

/**
 * Owns one FFmpeg connection to one stream: demux, decode, and the
 * interrupt state that makes both of them interruptible.
 * 
 * THREAD AFFINITY: while the worker thread runs, every member is
 * worker-thread only, except RequestStop() and IsStopRequested(), which any
 * thread may call (hence bStopRequested is atomic). Before the thread starts
 * and after it has been joined, the owning thread may use it.
 */
class FIPStreamFFmpegSession
{
public:
	FIPStreamFFmpegSession();
	~FIPStreamFFmpegSession();

	// Ensures that this type of object cannot be copied or moved
	UE_NONCOPYABLE(FIPStreamFFmpegSession)

	// Connects and opens the decoder. Blocking but interruptible
	bool Open(const FString& Url);

	// One step of demux/decode loop. Blocking but interruptible
	EIPStreamDecodeResult DecodeNext();

	// Frees everything that Open() allocates. Safe to call when already closed
	void Close();

	bool IsOpen() const;

	// Valid only after DecodeNext() returned GotFrame, until the next call
	const AVFrame* GetFrame() const;

	
	//~ Thread boundary - Following two functions are callable from any thread

	void RequestStop();
	bool IsStopRequested() const;

private:

	// The AVIOInterruptCB entry point
	static int InterruptCallback(void* Opaque);

	// Sets the time for which the InterruptCB will wait before interrupting a blocking call
	void SetDeadline(double TimeoutSeconds);

	
	//~ Worker-thread-only state

	AVFormatContext* FormatContext = nullptr;
	AVCodecContext* CodecContext = nullptr;
	AVPacket* Packet = nullptr;
	AVFrame* Frame = nullptr;
	int32 VideoStreamIndex = INDEX_NONE;

	// Set using SetDeadline() before a blocking call is called
	double DeadlineSeconds = 0.0;

	
	//~ Cross-thread state

	// UE has suggested using `std::atomic` as `TAtomic` is up for deprecation.
	std::atomic<bool> bStopRequested{ false };

};
