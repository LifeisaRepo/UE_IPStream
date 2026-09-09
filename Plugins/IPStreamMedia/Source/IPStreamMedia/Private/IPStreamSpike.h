// Copyright (c) 2026 Sanjyot Dahale. Licensed under the MIT License — see LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "IPStreamSpike.generated.h"

/**
 * THROWAWAY SPIKE CLASS: Proves FFmpeg demux/decode/convert works end-to-end.
 */
UCLASS()
class UIPStreamSpike : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category="IPStreamMedia")
	static UTexture2D* GrabOneFrame(const FString& RtspUrl);
	
};
