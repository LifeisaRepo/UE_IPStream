// Copyright (c) 2026 Sanjyot Dahale. Licensed under the MIT License — see LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "HAL/Event.h"
#include "HAL/Runnable.h"
#include "IMediaEventSink.h"

#include "IPStreamFFmpegSession.h"
#include "IPStreamTextureSample.h"

class FRunnableThread;

using FIPStreamSampleReadyFn = TFunction<void(const TSharedRef<IMediaTextureSample, ESPMode::ThreadSafe>&)>;

/**
 * Runs one FIPStreamFFmpegSession on its own thread: connect, decode, and reconnect
 * with backoff, until Stop()
 * 
 * The thread starts in the constructor and is joined in the destructor.
 * 
 * `final` enforces that this class cannot be inherited.
 */
class FIPStreamDecodeWorker final : public FRunnable
{
public:

	// InMaxReconnectAttempts: 0 = retry forever, N = try N times before giving up
	// InOnSampleReady: receives every finished sample, on the worker thread.
	FIPStreamDecodeWorker(const FString& InUrl, int32 InMaxReconnectAttempts, const FIPStreamSampleReadyFn& InOnSampleReady);
	virtual ~FIPStreamDecodeWorker();

	// Object of this type cannot be copied or moved.
	UE_NONCOPYABLE(FIPStreamDecodeWorker);

	/**
	 * @brief Takes the oldest event the worker has queued for the player. Game thread only.
	 * @return false when no event is waiting.
	 */
	bool PollEvent(EMediaEvent& OutEvent);


	/* ~FRunnable Interface
	**************************************/

	virtual uint32 Run() override;
	virtual void Stop() override;

private:

	/**
	 * @brief Open stream and decode until connection ends. Worker thread.
	 */
	void DecodeStream();
	
	/** 
	* @brief Backoff between reconnect attempts.
	* Stop() will end the wait immediately. Worker thread.
	* 
	* NOTE: Wait time will increment after every failed reconnect.
	* 
	* MinWait = 1s | MaxWait = 8s
	*/
	void WaitBeforeReconnect();

	/**
	 * @brief Turns the frame DecodeNext() produced into a sample and hands it over.
	 * Must run before the next DecodeNext() call. Worker thread.
	 */
	void HandOffFrame();

	/**
	 * @brief Counts a frame that could not become a sample. Logs the first and every 100th. Worker thread.
	 * @brief Frame skip might happen due to bad size, duration or conversion failure.
	 */
	void SkipFrame(const TCHAR* Reason);

	

	/* ~Constructor-only setters.
	**************************************/
	
	const FString Url;						// Url of the stream
	const int32 MaxReconnectAttempts;		// Max times the player should try to reconnect to the stream before giving up. 0 = retry forever.
	const FIPStreamSampleReadyFn OnSampleReady;

	/* ~Other members
	**************************************/	

	FIPStreamFFmpegSession Session;
	FIPStreamTextureSamplePool SamplePool;
	FEventRef BackoffEvent{ EEventMode::AutoReset };	// Auto-reset is default. Written explicitly for readbility
	FRunnableThread* Thread = nullptr;

	// Events for the player. Spsc because only worker adds to the queue and player takes from it (PollEvent).
	TQueue<EMediaEvent, EQueueMode::Spsc> PendingEvents;


	/* ~Worker-thread-only state
	**************************************/

	double ConnectStartSeconds = 0.0;		// Connection attempt timer
	uint64 FramesDecoded = 0;
	int32 ConsecutiveFailures = 0;			// Will stop reconnect attempts if > MaxReconnectAttempts
	bool bGotFrameThisConnection = false;	// true if atleast one frame was successfully decoded after a connecting to a new stream.
	bool bEverGotFrame = false;				// true once any connection has delivered a frame.
	int32 ConnectionIndex = -1;				// Connection lap number.
	uint64 FramesSkipped = 0;				// @see SkipFrame() for why a frame might be skipped.

};
