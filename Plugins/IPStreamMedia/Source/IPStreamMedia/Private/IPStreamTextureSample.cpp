// Copyright (c) 2026 Sanjyot Dahale. Licensed under the MIT License — see LICENSE.md.


#include "IPStreamTextureSample.h"
#include "IIPStreamMediaModule.h"

bool FIPStreamTextureSample::Initialize(FIntPoint InDim, FMediaTimeStamp InTime, FTimespan InDuration)
{
	if (InDim.X <= 0 || InDim.Y <= 0 || InDuration <= FTimespan::Zero())
	{
		UE_LOG(LogIPStreamMedia, Display, TEXT("FIPStreamTextureSample::Initialize - Failed to intialize sample. Either Dim <= 0 or Duration <= 0. "));
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

const void* FIPStreamTextureSample::GetBuffer()
{
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

