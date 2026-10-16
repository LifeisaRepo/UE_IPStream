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

	// Will log the first and every Nth Skipped frame
	// @see SkipFrame()
	constexpr uint64 SkippedFramesPerLog = 100;
}

/* Ctor & Dtor
**************************************/

FIPStreamDecodeWorker::FIPStreamDecodeWorker(const FString& InUrl, int32 InMaxReconnectAttempts, const FIPStreamSampleReadyFn& InOnSampleReady)
	: Url(InUrl),
	MaxReconnectAttempts(InMaxReconnectAttempts),
	OnSampleReady(InOnSampleReady)
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
		bEverGotFrame = bEverGotFrame || bGotFrameThisConnection;

		// If the first open fails, we allow an extra reconnect
		const int32 FailureLimit = MaxReconnectAttempts + (bEverGotFrame ? 0 : 1);

		if (MaxReconnectAttempts > 0 && ConsecutiveFailures >= FailureLimit)
		{
			UE_LOG(LogIPStreamMedia, Warning, TEXT("DecodeWorker: Run(): Gave up after %d attempts to reconnect. -_-"), MaxReconnectAttempts);
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

	++ConnectionIndex;		// Increment lap number on every new connection attempt.

	while (!Session.IsStopRequested())		// one pass = one decode step (inner loop)
	{
		const EIPStreamDecodeResult DecodeResult = Session.DecodeNext();

		if (DecodeResult == EIPStreamDecodeResult::GotFrame)
		{
			++FramesDecoded;

			HandOffFrame();

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

void FIPStreamDecodeWorker::HandOffFrame()
{
	// Frame's metadata
	FIPStreamFrameInfo Info;
	if (!Session.GetFrameInfo(Info))
	{
		SkipFrame(TEXT("GetFrameInfo"));
		return;
	}

	// get an empty sample from the pool
	const TSharedRef<FIPStreamTextureSample, ESPMode::ThreadSafe> Sample = SamplePool.AcquireShared();

	// Create the sample's timestamp from the Info and the Lap number
	// We use secondary sequence index for our lap number and UE generally uses it to represent
	// looping, which reset the time in essence and hence it's a bit more similar to reconnection
	// as compared to seeking, which is usually what the primary sequence index is used for.
	const FMediaTimeStamp Time(Info.Time, FMediaTimeStamp::MakeSequenceIndex(0, ConnectionIndex));
	if (!Sample->Initialize(Info.Dim, Time, Info.Duration))
	{
		SkipFrame(TEXT("Initialize"));
		return;
	}

	// Pixel conversion
	if (!Session.ConvertFrameToBGRA(Sample->GetMutableBuffer(), static_cast<int32>(Sample->GetStride())))
	{
		SkipFrame(TEXT("ConvertFrameToBGRA"));
		return;
	}

	// Hand-off
	OnSampleReady(Sample);
}

void FIPStreamDecodeWorker::SkipFrame(const TCHAR* Reason)
{
	++FramesSkipped;

	if (FramesSkipped == 1 || FramesSkipped % SkippedFramesPerLog == 0)
	{
		UE_LOG(LogIPStreamMedia, Warning, TEXT("DecodeWorker: %s() failed, frame skipped (%llu skipped so far). -_-"), Reason, FramesSkipped);
	}
}
