// Copyright (c) 2026 Sanjyot Dahale. Licensed under the MIT License — see LICENSE.md.


#include "IPStreamDecodeWorker.h"
#include "IIPStreamMediaModule.h"
#include "HAL/RunnableThread.h"

namespace
{
	
	// Backoff will start at 1s (when the connection drops for the first time)
	// and will double every time a reconnect attempt fails - max till 8s
	constexpr double FirstBackoffSeconds = 1.0;
	constexpr double MaxBackoffSeconds = 8.0;

	// Will log stream status and progress after every ~10s @ 25fps
	constexpr uint64 FramesPerProgressLog = 250;
}

/* Ctor & Dtor
**************************************/

FIPStreamDecodeWorker::FIPStreamDecodeWorker(const FString& InUrl, int32 InMaxReconnectAttempts)
	: Url(InUrl),
	MaxReconnectAttempts(InMaxReconnectAttempts)
{
	// The thread starts here, and Run() might begin even before this constructor returns.
	// So every member Run() uses must be built by this point.
	Thread = FRunnableThread::Create(this, TEXT("IPStreamDecodeWorker"));

	if (!Thread)
	{
		UE_LOG(LogIPStreamMedia, Error, TEXT("FIPStreamDecoder: Failed to create Decode thread. -_-"));
	}
}

FIPStreamDecodeWorker::~FIPStreamDecodeWorker()
{
	if (Thread)
	{
		Thread->Kill(true);		// calls Stop(), then waits for Run() to return
		delete Thread;			// Kill() does not free the object
		Thread = nullptr;
	}
}

uint32 FIPStreamDecodeWorker::Run()
{
	while (!Session.IsStopRequested())		// one pass = one connection (outer loop)
	{
		DecodeStream();
		Session.Close();

		if (Session.IsStopRequested())
		{
			break;							// shut down requested
		}

		ConsecutiveFailures = bGotFrameThisConnection ? 0 : ConsecutiveFailures + 1;

		if (MaxReconnectAttempts > 0 && ConsecutiveFailures >= MaxReconnectAttempts)
		{
			UE_LOG(LogIPStreamMedia, Warning, TEXT("DecodeWorker: Run(): Gave up after %d attempts to reconnect. -_-"), ConsecutiveFailures);
			break;
		}

		WaitBeforeReconnect();				// backoff before trying to reconnect
	}

	return 0;
}

void FIPStreamDecodeWorker::Stop()
{
	Session.RequestStop();
	BackoffEvent->Trigger();				// Wake up the worker if it's waiting on backoff, we need to stop immediately.
}

void FIPStreamDecodeWorker::DecodeStream()
{
	bGotFrameThisConnection = false;

	if (!Session.Open(Url))
	{
		return;
	}

	while (!Session.IsStopRequested())		// one pass = one decode step (inner loop)
	{
		const EIPStreamDecodeResult DecodeResult = Session.DecodeNext();

		if (DecodeResult == EIPStreamDecodeResult::GotFrame)
		{
			++FramesDecoded;

			if (!bGotFrameThisConnection)
			{
				bGotFrameThisConnection = true;
				UE_LOG(LogIPStreamMedia, Display, TEXT("DecodeWorker: DecodeStream(): First frame decoded on this connection (%llu frames decoded so far). ^_^"), FramesDecoded);
			}
			else if (FramesDecoded % FramesPerProgressLog == 0)
			{
				UE_LOG(LogIPStreamMedia, Display, TEXT("DecodeWorker: DecodeStream(): %llu frames decoded so far. ^_^"), FramesDecoded);
			}
			continue;
		}
		if (DecodeResult == EIPStreamDecodeResult::NoFrameYet)
		{
			continue;		// just wait for a frame to come...
		}

		// StreamEnded, Error
		if (DecodeResult == EIPStreamDecodeResult::StreamEnded)
		{
			UE_LOG(LogIPStreamMedia, Display, TEXT("DecodeWorker: DecodeNext(): stream ended. -_-"));
		}
		else if (DecodeResult == EIPStreamDecodeResult::Error)
		{
			UE_LOG(LogIPStreamMedia, Warning, TEXT("DecodeWorker: DecodeNext(): connection failed. -_-"));
		}
		// This is not necessary as this only happens when a stop is requested,
		// but I'm adding it just because I would like to have every path logged
		// due to my bad experiences with UE media players and their unexpected behaviours. 
		else if (DecodeResult == EIPStreamDecodeResult::Aborted)
		{
			UE_LOG(LogIPStreamMedia, Display, TEXT("DecodeWorker: DecodeNext(): connection closed. ^_^"));
		}
		

		return;
	}
}

void FIPStreamDecodeWorker::WaitBeforeReconnect()
{
	const double DelaySeconds = FMath::Min(
		FirstBackoffSeconds * FMath::Pow(2.0, static_cast<double>(ConsecutiveFailures)),
		MaxBackoffSeconds);

	UE_LOG(LogIPStreamMedia, Warning, TEXT("DecodeWorker: Trying to reconnect after %d seconds... ~_~"), static_cast<int32>(DelaySeconds));
	BackoffEvent->Wait(FTimespan::FromSeconds(DelaySeconds));
}
