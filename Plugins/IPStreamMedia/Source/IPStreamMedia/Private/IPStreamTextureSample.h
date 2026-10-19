// Copyright (c) 2026 Sanjyot Dahale. Licensed under the MIT License — see LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "IMediaTextureSample.h"
#include "MediaObjectPool.h"

/**
 * Texture sample for IPStreamMedia. 
 * This class is used to hold the decoded video frame data and provide it to the media framework.
 */
class FIPStreamTextureSample : public IMediaTextureSample, public IMediaPoolable
{
public:
	// Returns false and builds nothing if size and duration would make a sample the queue cannot serve
	bool Initialize(FIntPoint InDim, FMediaTimeStamp InTime, FTimespan InDuration);

	// Where the worker writes the converted pixels. Valid between Initialize() and AddVideo() only.
	uint8* GetMutableBuffer();

	//~ IMediaTextureSample interface
	virtual const void* GetBuffer() override;
	virtual FIntPoint GetDim() const override;
	virtual FTimespan GetDuration() const override;
	virtual EMediaTextureSampleFormat GetFormat() const override;
	virtual FIntPoint GetOutputDim() const override;
	virtual uint32 GetStride() const override;
	virtual FRHITexture* GetTexture() const override;
	virtual FMediaTimeStamp GetTime() const override;
	virtual bool IsCacheable() const override;
	// Will be true by default in our case because live SDR video streams are BT.709, whose gamma curve is close to sRGB's.
	virtual bool IsOutputSrgb() const override;

private:

	static constexpr int32 BytesPerPixel = 4;	// CharBGRA

	TArray<uint8> Buffer;
	FIntPoint Dim = FIntPoint(0, 0);
	FMediaTimeStamp Time;
	FTimespan Duration = FTimespan::Zero();
};

class FIPStreamTextureSamplePool : public TMediaObjectPool<FIPStreamTextureSample>
{
};
