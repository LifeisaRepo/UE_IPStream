// Copyright (c) 2026 Sanjyot Dahale. Licensed under the MIT License — see LICENSE.md.

#pragma once

#include "IMediaCache.h"
#include "IMediaControls.h"
#include "IMediaTracks.h"
#include "IMediaView.h"
#include "IMediaPlayer.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"

class FIPStreamDecodeWorker;
class FMediaSamples;
class IMediaEventSink;

/**
 * Media Framework player backend for live IP video streams.
 * @note IMediaCache and IMediaView has no pure virtuals so nothing 
 * needs to be overriden here as we are using defaults for them.
 */
class FIPStreamPlayer : public IMediaPlayer, protected IMediaCache, protected IMediaControls, protected IMediaTracks, protected IMediaView
{
public:

	FIPStreamPlayer(IMediaEventSink& InEventSink);
	virtual ~FIPStreamPlayer();

public:

	//~ IMediaPlayer interface

	virtual void Close() override;
	
	virtual IMediaCache& GetCache() override;
	virtual IMediaControls& GetControls() override;
	virtual FString GetInfo() const override;
	virtual FGuid GetPlayerPluginGUID() const override;
	virtual IMediaSamples& GetSamples() override;
	virtual FString GetStats() const override;
	virtual IMediaTracks& GetTracks() override;
	virtual FString GetUrl() const override;
	virtual IMediaView& GetView() override;

	virtual bool Open(const FString& Url, const IMediaOptions* Options) override;
	virtual bool Open(const TSharedRef<FArchive, ESPMode::ThreadSafe>& Archive, const FString& OriginalUrl, const IMediaOptions* Options) override;
	
	virtual bool GetPlayerFeatureFlag(EFeatureFlag Flag) const override;

protected:

	//~ IMediaControls interface

	virtual bool CanControl(EMediaControl Control) const override;
	virtual FTimespan GetDuration() const override;
	virtual float GetRate() const override;
	virtual EMediaState GetState() const override;
	virtual EMediaStatus GetStatus() const override;
	virtual TRangeSet<float> GetSupportedRates(EMediaRateThinning Thinning) const override;
	virtual FTimespan GetTime() const override;
	virtual bool IsLooping() const override;
	virtual bool Seek(const FTimespan& Time) override;
	virtual bool SetLooping(bool Looping) override;
	virtual bool SetRate(float Rate) override;

protected:

	//~ IMediaTracks interface

	virtual bool GetAudioTrackFormat(int32 TrackIndex, int32 FormatIndex, FMediaAudioTrackFormat& OutFormat) const override;
	virtual int32 GetNumTracks(EMediaTrackType TrackType) const override;
	virtual int32 GetNumTrackFormats(EMediaTrackType TrackType, int32 TrackIndex) const override;
	virtual int32 GetSelectedTrack(EMediaTrackType TrackType) const override;
	virtual FText GetTrackDisplayName(EMediaTrackType TrackType, int32 TrackIndex) const override;
	virtual int32 GetTrackFormat(EMediaTrackType TrackType, int32 TrackIndex) const override;
	virtual FString GetTrackLanguage(EMediaTrackType TrackType, int32 TrackIndex) const override;
	virtual FString GetTrackName(EMediaTrackType TrackType, int32 TrackIndex) const override;
	virtual bool GetVideoTrackFormat(int32 TrackIndex, int32 FormatIndex, FMediaVideoTrackFormat& OutFormat) const override;
	virtual bool SelectTrack(EMediaTrackType TrackType, int32 TrackIndex) override;
	virtual bool SetTrackFormat(EMediaTrackType TrackType, int32 TrackIndex, int32 FormatIndex) override;

private:

	// The facade's event sink, handed to us during at construction; we don't create nor own it - hence this is a reference.
	IMediaEventSink& EventSink;

	// URL of the currently open stream. Empty when closed.
	FString CurrentUrl;

	// Playback state, driven only by Open() and Close() in our case
	EMediaState CurrentState;

	// Decoded sample queue, handed to the facade by GetSamples()
	TUniquePtr<FMediaSamples> Samples;

	// Decode thread
	TUniquePtr<FIPStreamDecodeWorker> DecodeWorker;

};
