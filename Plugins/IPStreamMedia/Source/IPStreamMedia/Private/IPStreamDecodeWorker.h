// Copyright (c) 2026 Sanjyot Dahale. Licensed under the MIT License — see LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Event.h"
#include "HAL/Runnable.h"

#include "IPStreamFFmpegSession.h"

class FRunnableThread;

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
	FIPStreamDecodeWorker(const FString& InUrl, int32 InMaxReconnectAttempts = 0);
	virtual ~FIPStreamDecodeWorker();

	// Object of this type cannot be copied or moved.
	UE_NONCOPYABLE(FIPStreamDecodeWorker);


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

	

	/* ~Constructor-only setters.
	**************************************/
	
	const FString Url;						// Url of the stream
	const int32 MaxReconnectAttempts;		// Max times the player should try to reconnect to the stream before giving up. 0 = retry forever.

	/* ~Other members
	**************************************/	

	FIPStreamFFmpegSession Session;
	FEventRef BackoffEvent{ EEventMode::AutoReset };	// Auto-reset is default. Written explicitly for readbility
	FRunnableThread* Thread = nullptr;


	/* ~Worker-thread-only state
	**************************************/

	uint64 FramesDecoded = 0;
	int32 ConsecutiveFailures = 0;			// Will stop reconnect attempts if > MaxReconnectAttempts
	bool bGotFrameThisConnection = false;	// true if atleast one frame was successfully decoded after a connecting to a new stream.

};
