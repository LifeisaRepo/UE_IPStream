// Copyright (c) 2026 Sanjyot Dahale. Licensed under the MIT License — see LICENSE.md.

#include "IPStreamPlayer.h"

#include "IIPStreamMediaModule.h"
#include "IMediaEventSink.h"
#include "IMediaOptions.h"
#include "IPStreamDecodeWorker.h"
#include "MediaSamples.h"
#include "Misc/Guid.h"
#include "HAL/PlatformTime.h"

namespace
{
	/**
	* Strips credentials from a URL before it reaches a log line or the editor UI
	* RTSP URLs usually have access credentials which can be leaked through logs
	* in-case they exposed without redaction.
	* 
	* SHOULD BE USED ONLY WHEN THE URL NEEDS TO BE DISPLAYED OR PRINTED!
	*/
	FString RedactUrlCredentials(const FString& Url)
	{
		FString Scheme;
		FString Remainder;

		if (!Url.Split(TEXT("://"), &Scheme, &Remainder))
		{
			return Url;
		}

		// Credentials are usually present in the authority section - before the first '/'
		int32 PathStart = INDEX_NONE;
		const FString Authority = Remainder.FindChar(TEXT('/'), PathStart) ? Remainder.Left(PathStart) : Remainder;

		// Get the index of the last '@'
		int32 AtIndex = INDEX_NONE;
		if (!Authority.FindLastChar(TEXT('@'), AtIndex))
		{
			return Url;
		}

		return Scheme + TEXT("://***@") + Remainder.RightChop(AtIndex + 1);
	}
}

/**
* On construct -
* 1. Get EventSink from Facade
* 2. Set MediaState to Closed - obviously -_-
* 3. Create a new FMediaSamples UniquePtr
*/
FIPStreamPlayer::FIPStreamPlayer(IMediaEventSink& InEventSink): EventSink(InEventSink), CurrentState(EMediaState::Closed), Samples(MakeUnique<FMediaSamples>())
{
}

/**
* Joins the decode thread before the members are destroyed.
* Close() is not called here as it sends an event to facade 
* (not a good practice to touch other objects in the destructor) 
*/
FIPStreamPlayer::~FIPStreamPlayer()
{
	DecodeWorker.Reset();
}

/*
**********************************************************
 IMediaPlayer interface
**********************************************************
*/

void FIPStreamPlayer::Close()
{
	if (CurrentState == EMediaState::Closed)
	{
		return;
	}

	// Time before join was requested - debug
	const double JoinStartSeconds = FPlatformTime::Seconds();

	// Join the decode thread first, so samples are not added after the flush below
	DecodeWorker.Reset();

	// Log time delta time for decode thread join in ms - debug
	UE_LOG(LogIPStreamMedia, Verbose, TEXT("Close(): Decode thread joined in %.3f ms. 'o'."), (FPlatformTime::Seconds() - JoinStartSeconds) * 1000.0);

	CurrentUrl.Empty();
	CurrentState = EMediaState::Closed;
	VideoFormat.Reset();
	SelectedVideoTrack = INDEX_NONE;
	Samples->FlushSamples();

	EventSink.ReceiveMediaEvent(EMediaEvent::MediaClosed);

	UE_LOG(LogIPStreamMedia, Display, TEXT("Close(): Player Closed. '_'"));
}

IMediaCache& FIPStreamPlayer::GetCache()
{
	return *this;
}

IMediaControls& FIPStreamPlayer::GetControls()
{
	return *this;
}

FString FIPStreamPlayer::GetInfo() const
{
	return CurrentUrl.IsEmpty() ? FString(TEXT("No media open")) : FString::Printf(TEXT("URL: %s"), *RedactUrlCredentials(CurrentUrl));
}

FGuid FIPStreamPlayer::GetPlayerPluginGUID() const
{
	static FGuid PlayerPluginGUID(0x6bd90b53, 0xbe5340cd, 0xab90c371, 0x7f1b284a);
	return PlayerPluginGUID;
}

IMediaSamples& FIPStreamPlayer::GetSamples()
{
	return *Samples;
}

FString FIPStreamPlayer::GetStats() const
{
	return FString();
}

IMediaTracks& FIPStreamPlayer::GetTracks()
{
	return *this;
}

/**
* The actual raw URL of the stream (which may contain sensitive credentials)
* 
* Use GetInfo() to get a redacted URL (only useful for logging or display).
*/
FString FIPStreamPlayer::GetUrl() const
{
	return CurrentUrl;
}

IMediaView& FIPStreamPlayer::GetView()
{
	return *this;
}

bool FIPStreamPlayer::Open(const FString& Url, const IMediaOptions* Options)
{
	// Open() game-thread cost Timer
	const double OpenStartSeconds = FPlatformTime::Seconds();

	Close();

	if (Url.IsEmpty())
	{
		EventSink.ReceiveMediaEvent(EMediaEvent::MediaOpenFailed);
		return false;
	}

	// 0 = Retry forever. 
	// Use "Set Media Option (int64)" in blueprints to set a max limit before opening the media
	// **NOTE:** MediaOption uses int64 here, but values > MAX_int32 will be clamped.
	int64 MaxReconnectAttempts = 3;
	if (Options)
	{
		MaxReconnectAttempts = Options->GetMediaOption(FName(TEXT("MaxReconnectAttempts")), (int64)3);
	}

	// The worker takes int32. So we clamp the value within the int32 range
	const int32 ReconnectLimit = static_cast<int32>(FMath::Clamp(MaxReconnectAttempts, 0, MAX_int32));

	FMediaSamples* const SampleQueue = Samples.Get();
	DecodeWorker = MakeUnique<FIPStreamDecodeWorker>(Url, ReconnectLimit,
		[SampleQueue](const TSharedRef<IMediaTextureSample, ESPMode::ThreadSafe>& Sample)
		{
			SampleQueue->AddVideo(Sample);
		});

	CurrentUrl = Url;
	CurrentState = EMediaState::Preparing;

	UE_LOG(LogIPStreamMedia, Display, TEXT("Player opened %s"), *RedactUrlCredentials(Url));
	UE_LOG(LogIPStreamMedia, Verbose, TEXT("FIPStreamPlayer::Open(): returned in %.3f ms. 'o'"), (FPlatformTime::Seconds() - OpenStartSeconds) * 1000.0);

	return true;
}

bool FIPStreamPlayer::Open(const TSharedRef<FArchive, ESPMode::ThreadSafe>& Archive, const FString& OriginalUrl, const IMediaOptions* Options)
{
	// A live stream will never have an archive - so this method will never be supported.
	return false;
}

bool FIPStreamPlayer::GetPlayerFeatureFlag(EFeatureFlag Flag) const
{
	switch (Flag)
	{
	case IMediaPlayer::EFeatureFlag::UsePlaybackTimingV2:
	case IMediaPlayer::EFeatureFlag::AlwaysPullNewestVideoFrame:
		return true;
	default:
		break;
	}

	return IMediaPlayer::GetPlayerFeatureFlag(Flag);
}

void FIPStreamPlayer::TickInput(FTimespan DeltaTime, FTimespan Timecode)
{
	if (!DecodeWorker)
	{
		return;
	}

	FIPStreamWorkerEvent Item;
	while (DecodeWorker->PollEvent(Item))
	{
		switch (Item.Event)
		{
		case EMediaEvent::TracksChanged:
			VideoFormat = Item.Format;
			break;
		case EMediaEvent::MediaOpened:
			CurrentState = EMediaState::Playing;
			break;
		case EMediaEvent::MediaOpenFailed:
		case EMediaEvent::PlaybackEndReached:
			CurrentState = EMediaState::Error;
			break;		
		default:
			break;		// CurrentState will stay `Playing` on PlybackSuspended/PlaybackResumed
		}
		EventSink.ReceiveMediaEvent(Item.Event);
	}
}

/*
**********************************************************
 IMediaControls interface
**********************************************************
*/

bool FIPStreamPlayer::CanControl(EMediaControl Control) const
{
	return false;
}

FTimespan FIPStreamPlayer::GetDuration() const
{
	return FTimespan::Zero();
}

float FIPStreamPlayer::GetRate() const
{
	return (CurrentState == EMediaState::Playing) ? 1.0f : 0.0f;
}

EMediaState FIPStreamPlayer::GetState() const
{
	return CurrentState;
}

EMediaStatus FIPStreamPlayer::GetStatus() const
{
	return EMediaStatus::None;
}

TRangeSet<float> FIPStreamPlayer::GetSupportedRates(EMediaRateThinning Thinning) const
{
	TRangeSet<float> Rates;

	// **NOTE** TRange syntax can be deceiving
	// TRange<float>(1.0f); means [1.0 , 1.0] = 1.0 (range with single value)
	// TRange<float>(1.0f, 1.0f); means [1.0 , 1.0) = 0.0 (empty range)
	Rates.Add(TRange<float>(1.0f));	

	return Rates;
}

FTimespan FIPStreamPlayer::GetTime() const
{
	return FTimespan::Zero();
}

bool FIPStreamPlayer::IsLooping() const
{
	return false;
}

bool FIPStreamPlayer::Seek(const FTimespan& Time)
{
	return false;
}

bool FIPStreamPlayer::SetLooping(bool Looping)
{
	return false;
}

bool FIPStreamPlayer::SetRate(float Rate)
{
	// Rate will only be 1.0: Playing once open, no pause
	return Rate == 1.0f && (CurrentState == EMediaState::Preparing || CurrentState == EMediaState::Playing);
}

/*
**********************************************************
 IMediaTracks interface
**********************************************************
*/

bool FIPStreamPlayer::GetAudioTrackFormat(int32 TrackIndex, int32 FormatIndex, FMediaAudioTrackFormat& OutFormat) const
{
	return false;
}

int32 FIPStreamPlayer::GetNumTracks(EMediaTrackType TrackType) const
{
	return (TrackType == EMediaTrackType::Video && VideoFormat.IsSet()) ? 1 : 0;
}

int32 FIPStreamPlayer::GetNumTrackFormats(EMediaTrackType TrackType, int32 TrackIndex) const
{
	return IsVideoTrack(TrackType, TrackIndex) ? 1 : 0;
}

int32 FIPStreamPlayer::GetSelectedTrack(EMediaTrackType TrackType) const
{
	return (TrackType == EMediaTrackType::Video) ? SelectedVideoTrack : INDEX_NONE;
}

FText FIPStreamPlayer::GetTrackDisplayName(EMediaTrackType TrackType, int32 TrackIndex) const
{
	return FText::GetEmpty();
}

int32 FIPStreamPlayer::GetTrackFormat(EMediaTrackType TrackType, int32 TrackIndex) const
{
	return IsVideoTrack(TrackType, TrackIndex) ? 0 : INDEX_NONE;
}

FString FIPStreamPlayer::GetTrackLanguage(EMediaTrackType TrackType, int32 TrackIndex) const
{
	return FString();
}

FString FIPStreamPlayer::GetTrackName(EMediaTrackType TrackType, int32 TrackIndex) const
{
	return FString();
}

bool FIPStreamPlayer::GetVideoTrackFormat(int32 TrackIndex, int32 FormatIndex, FMediaVideoTrackFormat& OutFormat) const
{
	if (!IsVideoTrack(EMediaTrackType::Video, TrackIndex) || FormatIndex != 0)
	{
		return false;
	}
	OutFormat = VideoFormat.GetValue();
	return true;
}

bool FIPStreamPlayer::SelectTrack(EMediaTrackType TrackType, int32 TrackIndex)
{
	if (TrackType != EMediaTrackType::Video || (TrackIndex != INDEX_NONE && !IsVideoTrack(TrackType, TrackIndex)))
	{
		return false;
	}
	SelectedVideoTrack = TrackIndex;
	return true;
}

bool FIPStreamPlayer::SetTrackFormat(EMediaTrackType TrackType, int32 TrackIndex, int32 FormatIndex)
{
	return IsVideoTrack(TrackType, TrackIndex) && FormatIndex == 0 ;
}

bool FIPStreamPlayer::IsVideoTrack(EMediaTrackType TrackType, int32 TrackIndex) const
{
	return TrackType == EMediaTrackType::Video && TrackIndex == 0 && VideoFormat.IsSet();
}
