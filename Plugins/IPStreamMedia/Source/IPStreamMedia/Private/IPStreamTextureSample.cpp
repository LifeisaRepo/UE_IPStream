// Copyright (c) 2026 Sanjyot Dahale. Licensed under the MIT License — see LICENSE.md.


#include "IPStreamTextureSample.h"
#include "IIPStreamMediaModule.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CountersTrace.h"

// Float counters are not atomic hence only set on render thread
TRACE_DECLARE_FLOAT_COUNTER(IPStream_QueueWaitMs, TEXT("IPStream/QueueWaitMs"));
TRACE_DECLARE_FLOAT_COUNTER(IPStream_OursMs, TEXT("IPStream/OursMs"));

bool FIPStreamTextureSample::Initialize(FIntPoint InDim, FMediaTimeStamp InTime, FTimespan InDuration)
{
	PacketCycles = 0;
	QueuedCycles = 0;
	bUploadStamped = false;

	if (InDim.X <= 0 || InDim.Y <= 0 || InDuration <= FTimespan::Zero())
	{
		UE_LOG(LogIPStreamMedia, Verbose, TEXT("FIPStreamTextureSample::Initialize - Failed to intialize sample. Either Dim <= 0 or Duration <= 0. "));
		return false;
	}

	Dim = InDim;
	Time = InTime;
	Duration = InDuration;

	Buffer.SetNumUninitialized(Dim.X * Dim.Y * BytesPerPixel);
	return true;
}

uint8* FIPStreamTextureSample::GetMutableBuffer()
{
	return Buffer.GetData();
}

void FIPStreamTextureSample::SetStamps(uint64 InPacketCycles, uint64 InQueuedCycles)
{
	PacketCycles = InPacketCycles;
	QueuedCycles = InQueuedCycles;
}

const void* FIPStreamTextureSample::GetBuffer()
{
	if (!bUploadStamped)
	{
		bUploadStamped = true;
		const uint64 UploadCycles = FPlatformTime::Cycles64();

		if (QueuedCycles != 0)
		{
			TRACE_COUNTER_SET(IPStream_QueueWaitMs, FPlatformTime::ToMilliseconds64(UploadCycles - QueuedCycles));
		}
		if (PacketCycles != 0)
		{
			TRACE_COUNTER_SET(IPStream_OursMs, FPlatformTime::ToMilliseconds64(UploadCycles - PacketCycles));
		}
	}
	return Buffer.GetData();
}

FIntPoint FIPStreamTextureSample::GetDim() const
{
	return Dim;
}

FTimespan FIPStreamTextureSample::GetDuration() const
{
	return Duration;
}

EMediaTextureSampleFormat FIPStreamTextureSample::GetFormat() const
{
	return EMediaTextureSampleFormat::CharBGRA;
}

FIntPoint FIPStreamTextureSample::GetOutputDim() const
{
	// FFmpeg's decoder already crops by default.
	// So the frame's width and height always match the display size (as long as we are not using GPU conversion).
	return Dim;
}

uint32 FIPStreamTextureSample::GetStride() const
{
	return static_cast<uint32>(Dim.X * BytesPerPixel);
}

FRHITexture* FIPStreamTextureSample::GetTexture() const
{
	return nullptr;
}

FMediaTimeStamp FIPStreamTextureSample::GetTime() const
{
	return Time;
}

bool FIPStreamTextureSample::IsCacheable() const
{
	return true;
}

bool FIPStreamTextureSample::IsOutputSrgb() const
{
	return true;
}

