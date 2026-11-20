// Copyright (c) 2026 Sanjyot Dahale. Licensed under the MIT License — see LICENSE.md.


#include "IPStreamAudioSample.h"
#include "IIPStreamMediaModule.h"

bool FIPStreamAudioSample::Initialize(int32 InChannels, int32 InSampleRate, int32 InCapacity, FMediaTimeStamp InTime)
{
	Capacity = 0;
	Frames = 0;
	Duration = FTimespan::Zero();

	if (InChannels <= 0 || InSampleRate <= 0 || InCapacity <= 0)
	{
		UE_LOG(LogIPStreamMedia, Verbose, TEXT("FIPStreamAudioSample::Initialize - refused: channels, rate or capacity <= 0."));
		return false;
	}

	Channels = InChannels;
	SampleRate = InSampleRate;
	Capacity = InCapacity;
	Time = InTime;

	Buffer.SetNumUninitialized(Capacity * Channels);
	return true;
}

float* FIPStreamAudioSample::GetMutableBuffer()
{
	return Buffer.GetData();
}

bool FIPStreamAudioSample::Finish(int32 InWritten)
{
	if (InWritten <= 0 || InWritten > Capacity)
	{
		return false;
	}

	Frames = InWritten;
	Duration = FTimespan(static_cast<int64>(Frames) * ETimespan::TicksPerSecond / SampleRate);

	return true;
}

const void* FIPStreamAudioSample::GetBuffer()
{
	return Buffer.GetData();
}

uint32 FIPStreamAudioSample::GetChannels() const
{
	return static_cast<uint32>(Channels);
}

FTimespan FIPStreamAudioSample::GetDuration() const
{
	return Duration;
}

EMediaAudioSampleFormat FIPStreamAudioSample::GetFormat() const
{
	return EMediaAudioSampleFormat::Float;
}

uint32 FIPStreamAudioSample::GetFrames() const
{
	return static_cast<uint32>(Frames);
}

uint32 FIPStreamAudioSample::GetSampleRate() const
{
	return static_cast<uint32>(SampleRate);
}

FMediaTimeStamp FIPStreamAudioSample::GetTime() const
{
	return Time;
}
