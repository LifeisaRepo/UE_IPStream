// Copyright (c) 2026 Sanjyot Dahale. Licensed under the MIT License — see LICENSE.md.


#include "IPStreamDecodeWorker.h"
#include "IIPStreamMediaModule.h"
#include "HAL/RunnableThread.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CountersTrace.h"

// Float counters are not atomic hence set on worker thread only
TRACE_DECLARE_FLOAT_COUNTER(IPStream_DecoderDelayMs, TEXT("IPStream/DecoderDelayMs"));
TRACE_DECLARE_FLOAT_COUNTER(IPStream_ConvertMs, TEXT("IPStream/ConvertMs"));

namespace
{
	
	// Backoff will start at 1s (when the connection drops for the first time)
	// and will double every time a reconnect attempt fails - max till 8s
	constexpr double FirstBackoffSeconds = 1.0;
	constexpr double MaxBackoffSeconds = 8.0;

	// Reconnect attempts allowed before a stream has ever played.
	// MaxReconnectAttempts is only considered if the stream plays atleast once.
	constexpr int32 FirstConnectAttempts = 2;

	// Will log stream status and progress after every ~10s @ 25fps
	constexpr uint64 FramesPerProgressLog = 250;

	// Will log the first and every Nth Skipped frame
	// @see SkipFrame()
	constexpr uint64 SkippedFramesPerLog = 100;

	// Will log the first and every Nth skipped audio block
	// @see DropAudio()
	constexpr uint64 AudioBlocksPerLog = 100;

	// Helper to create Track format for blueprints.
	FMediaVideoTrackFormat MakeVideoFormat(const FIPStreamFrameInfo& Info, const FString& CodecName)
	{
		FMediaVideoTrackFormat Format;
		Format.Dim = Info.Dim;
		Format.FrameRate = static_cast<float>(1.0 / Info.Duration.GetTotalSeconds());
		Format.FrameRates = TRange<float>(Format.FrameRate);	// single value range
		Format.TypeName = CodecName;
		return Format;
	}

	// Helper to create the audio track format for blueprints.
	// BitsPerSample = 32: what our samples carry (float). A compressed source like AAC has no bits per sample of its own.
	FMediaAudioTrackFormat MakeAudioFormat(const FIPStreamAudioInfo& Info, const FString& CodecName)
	{
		FMediaAudioTrackFormat Format;
		Format.BitsPerSample = 32;
		Format.NumChannels = static_cast<uint32>(Info.Channels);
		Format.SampleRate = static_cast<uint32>(Info.SampleRate);
		Format.TypeName = CodecName;
		return Format;
	}		
}

/* Ctor & Dtor
**************************************/

FIPStreamDecodeWorker::FIPStreamDecodeWorker(const FString& InUrl, int32 InMaxReconnectAttempts, const FIPStreamSampleReadyFn& InOnSampleReady, const FIPStreamAudioReadyFn& InOnAudioReady)
	: Url(InUrl),
	MaxReconnectAttempts(InMaxReconnectAttempts),
	OnSampleReady(InOnSampleReady),
	OnAudioReady(InOnAudioReady)
{
	// The thread starts here, and Run() might begin even before this constructor returns.
	// So every member Run() uses must be built by this point.
	Thread = FRunnableThread::Create(this, TEXT("IPStreamDecodeWorker"));

	if (!Thread)
	{
		UE_LOG(LogIPStreamMedia, Error, TEXT("FIPStreamDecoder: Failed to create Decode thread."));
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

bool FIPStreamDecodeWorker::PollEvent(FIPStreamWorkerEvent& OutEvent)
{
	return PendingEvents.Dequeue(OutEvent);
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

		// We were playing and lost the stream. Once per outage.
		if (bGotFrameThisConnection)
		{
			PendingEvents.Enqueue({ EMediaEvent::PlaybackSuspended, {} });
		}

		ConsecutiveFailures = bGotFrameThisConnection ? 0 : ConsecutiveFailures + 1;
		bEverGotFrame = bEverGotFrame || bGotFrameThisConnection;

		// When to give up changes based on if its the first connection or not
		const bool bGiveUp = bEverGotFrame
			? (MaxReconnectAttempts > 0 && ConsecutiveFailures >= MaxReconnectAttempts)
			: (ConsecutiveFailures >= FirstConnectAttempts);

		if (bGiveUp)
		{
			UE_LOG(LogIPStreamMedia, Warning, TEXT("DecodeWorker: Run(): Gave up after %d failed attempts in a row."), ConsecutiveFailures);
			PendingEvents.Enqueue({ bEverGotFrame ? EMediaEvent::PlaybackEndReached : EMediaEvent::MediaOpenFailed, {} });
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
	Tracks = FIPStreamTrackList();
	ConnectStartSeconds = FPlatformTime::Seconds();

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

			FIPStreamFrameInfo Info;
			if (!HandOffFrame(Info))
			{
				continue;
			}
			
			if (!bGotFrameThisConnection)
			{
				bGotFrameThisConnection = true;

				// Queued after HandOffFrame() so the first sample is already waiting when the event is processed.
				// TracksChanged queued first so the facade has selected the track before MediaOpened reaches blueprints.
				Tracks.Video = MakeVideoFormat(Info, Session.GetCodecName());
				PendingEvents.Enqueue({ EMediaEvent::TracksChanged, Tracks });
				PendingEvents.Enqueue({ bEverGotFrame ? EMediaEvent::PlaybackResumed : EMediaEvent::MediaOpened, {} });

				UE_LOG(LogIPStreamMedia, Display, TEXT("DecodeWorker: DecodeStream(): First frame on connection %d, %.3f ms after connecting began (%llu frames decoded so far). Decoder: %s"), ConnectionIndex, (FPlatformTime::Seconds() - ConnectStartSeconds) * 1000.0, FramesDecoded, *Session.GetDecoderDelayInfo());
			}
			else if (FramesDecoded % FramesPerProgressLog == 0)
			{
				UE_LOG(LogIPStreamMedia, Display, TEXT("DecodeWorker: DecodeStream(): %llu frames decoded so far. Decoder: %s"), FramesDecoded, *Session.GetDecoderDelayInfo());
			}
			continue;
		}
		if (DecodeResult == EIPStreamDecodeResult::GotAudio)
		{
			FIPStreamAudioInfo AudioInfo;
			if (HandOffAudio(AudioInfo) && !Tracks.Audio.IsSet())
			{
				// First audio of this connection: send the whole list again now with audio in it.
				Tracks.Audio = MakeAudioFormat(AudioInfo, Session.GetAudioCodecName());
				PendingEvents.Enqueue({ EMediaEvent::TracksChanged, Tracks });

				UE_LOG(LogIPStreamMedia, Display, TEXT("DecodeWorker: DecodeStream(): First audio on connection %d: %s, %d channels, %d Hz, %.3f ms after connecting began."), ConnectionIndex, *Session.GetAudioCodecName(), AudioInfo.Channels, AudioInfo.SampleRate, (FPlatformTime::Seconds() - ConnectStartSeconds) * 1000.0);

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
			UE_LOG(LogIPStreamMedia, Display, TEXT("DecodeWorker: DecodeNext(): stream ended."));
		}
		else if (DecodeResult == EIPStreamDecodeResult::Error)
		{
			UE_LOG(LogIPStreamMedia, Warning, TEXT("DecodeWorker: DecodeNext(): connection failed."));
		}
		// This is not necessary as this only happens when a stop is requested,
		// but I'm adding it just because I would like to have every path logged
		// due to my bad experiences with UE media players and their unexpected behaviours. 
		else if (DecodeResult == EIPStreamDecodeResult::Aborted)
		{
			UE_LOG(LogIPStreamMedia, Display, TEXT("DecodeWorker: DecodeNext(): connection closed."));
		}
		

		return;
	}
}

void FIPStreamDecodeWorker::WaitBeforeReconnect()
{
	const double DelaySeconds = FMath::Min(
		FirstBackoffSeconds * FMath::Pow(2.0, static_cast<double>(ConsecutiveFailures)),
		MaxBackoffSeconds);

	UE_LOG(LogIPStreamMedia, Warning, TEXT("DecodeWorker: Trying to reconnect after %d seconds..."), static_cast<int32>(DelaySeconds));
	BackoffEvent->Wait(FTimespan::FromSeconds(DelaySeconds));
}

bool FIPStreamDecodeWorker::HandOffFrame(FIPStreamFrameInfo& OutInfo)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IPStream_HandOff);

	// New decoded frame is just ready
	const uint64 PictureOutCycles = FPlatformTime::Cycles64();

	// Frame's metadata
	if (!Session.GetFrameInfo(OutInfo))
	{
		SkipFrame(TEXT("GetFrameInfo"));
		return false;
	}

	// get an empty sample from the pool
	const TSharedRef<FIPStreamTextureSample, ESPMode::ThreadSafe> Sample = SamplePool.AcquireShared();

	// Create the sample's timestamp from the Info and the Lap number
	// We use secondary sequence index for our lap number and UE generally uses it to represent
	// looping, which reset the time in essence and hence it's a bit more similar to reconnection
	// as compared to seeking, which is usually what the primary sequence index is used for.
	const FMediaTimeStamp Time(OutInfo.Time, FMediaTimeStamp::MakeSequenceIndex(0, ConnectionIndex));
	if (!Sample->Initialize(OutInfo.Dim, Time, OutInfo.Duration))
	{
		SkipFrame(TEXT("Initialize"));
		return false;
	}

	// Pixel conversion
	if (!Session.ConvertFrameToBGRA(Sample->GetMutableBuffer(), static_cast<int32>(Sample->GetStride())))
	{
		SkipFrame(TEXT("ConvertFrameToBGRA"));
		return false;
	}

	// Frame about to be sent into queue
	const uint64 QueuedCycles = FPlatformTime::Cycles64();
	Sample->SetStamps(OutInfo.PacketCycles, QueuedCycles);

	TRACE_COUNTER_SET(IPStream_ConvertMs, FPlatformTime::ToMilliseconds64(QueuedCycles - PictureOutCycles));
	if (OutInfo.PacketCycles != 0)
	{
		TRACE_COUNTER_SET(IPStream_DecoderDelayMs, FPlatformTime::ToMilliseconds64(PictureOutCycles - OutInfo.PacketCycles));
	}

	// Hand-off
	OnSampleReady(Sample);
	return true;
}

void FIPStreamDecodeWorker::SkipFrame(const TCHAR* Reason)
{
	++FramesSkipped;

	if (FramesSkipped == 1 || FramesSkipped % SkippedFramesPerLog == 0)
	{
		UE_LOG(LogIPStreamMedia, Warning, TEXT("DecodeWorker: %s() failed, frame skipped (%llu skipped so far). -_-"), Reason, FramesSkipped);
	}
}

bool FIPStreamDecodeWorker::HandOffAudio(FIPStreamAudioInfo& OutInfo)
{
	// Block's metadata
	if (!Session.GetAudioInfo(OutInfo))
	{
		DropAudio(TEXT("GetAudioInfo() failed"));
		return false;
	}

	// get an empty sample from the pool
	const TSharedRef<FIPStreamAudioSample, ESPMode::ThreadSafe> Sample = AudioSamplePool.AcquireShared();

	// Same lap number as video
	const FMediaTimeStamp Time(OutInfo.Time, FMediaTimeStamp::MakeSequenceIndex(0, ConnectionIndex));

	if (!Sample->Initialize(OutInfo.Channels, OutInfo.SampleRate, OutInfo.Frames, Time))
	{
		DropAudio(TEXT("Initialize() failed"));
		return false;
	}

	const int32 Written = Session.ConvertAudioToFloat(Sample->GetMutableBuffer(), OutInfo.Frames);
	if (!Sample->Finish(Written))
	{
		DropAudio(TEXT("ConvertAudioToFloat() failed"));
		return false;
	}

	if (!OnAudioReady(Sample))
	{
		DropAudio(TEXT("audio sample queue full!"));
	}

	return true;
}

void FIPStreamDecodeWorker::DropAudio(const TCHAR* Reason)
{
	++AudioBlocksDropped;

	if (AudioBlocksDropped == 1 || AudioBlocksDropped % AudioBlocksPerLog == 0)
	{
		UE_LOG(LogIPStreamMedia, Warning, TEXT("DecodeWorker: audio block dropped: %s (%llu dropped so far)."), Reason, AudioBlocksDropped);
	}
}
