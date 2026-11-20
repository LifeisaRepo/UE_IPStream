// Copyright (c) 2026 Sanjyot Dahale. Licensed under the MIT License — see LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "IMediaAudioSample.h"
#include "MediaObjectPool.h"

/**
 * Audio sample for IPStreamMedia
 * Holds one decoded block of sound as interleaved float (L R L R...),
 * read by the sound component on the audio thread.
 */
class FIPStreamAudioSample : public IMediaAudioSample, public IMediaPoolable
{
public:

	// Makes room for the the swresample write
	// Returns false and builds nothing if channels, rate or capacity <= 0
	bool Initialize(int32 InChannels, int32 InSampleRate, int32 InCapacity, FMediaTimeStamp InTime);

	// Where swresample writes. Valid between Initialize() and AddAudio() only
	float* GetMutableBuffer();

	// After swresample wrote: the first InWritten frames per channel are real sound.
	// Returns false if InWritten <= 0 or more than the room Initialize() made.
	bool Finish(int32 InWritten);

	//~ IMediaAudioSample interface
	virtual const void* GetBuffer() override;
	virtual uint32 GetChannels() const override;
	virtual FTimespan GetDuration() const override;
	virtual EMediaAudioSampleFormat GetFormat() const override;
	virtual uint32 GetFrames() const override;
	virtual uint32 GetSampleRate() const override;
	virtual FMediaTimeStamp GetTime() const override;

private:

	TArray<float> Buffer;
	int32 Channels = 0;
	int32 SampleRate = 0;
	int32 Capacity = 0;			// room per channel, set by Initialize()
	int32 Frames = 0;			// real sound per channel, set by Finish()
	FMediaTimeStamp Time;
	FTimespan Duration = FTimespan::Zero();
	
};

class FIPStreamAudioSamplePool : public TMediaObjectPool<FIPStreamAudioSample>
{

};
