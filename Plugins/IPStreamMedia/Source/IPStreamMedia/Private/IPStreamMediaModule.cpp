// Copyright (c) 2026 Sanjyot Dahale. Licensed under the MIT License — see LICENSE.md.

#include "IIPStreamMediaModule.h"
#include "IPStreamPlayer.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY(LogIPStreamMedia);

namespace
{
	// Must match FFmpeg.Build.cs's Libraries array - same four DLLs, same filenames.
	// Order of the DLLs is not important, avutil has been written first just for readability.
	// Windows' loader will automatically resolve avutil dependencies within other dlls even if they are loaded in a different order.

	const TCHAR* GFFmpegDllNames[] = 
	{
		TEXT("avutil-61.dll"),
		TEXT("avcodec-63.dll"),
		TEXT("avformat-63.dll"),
		TEXT("swscale-10.dll"),
	};
}

class FIPStreamMediaModule : public IIPStreamMediaModule
{
public:

	virtual void StartupModule() override
	{
		UE_LOG(LogIPStreamMedia, Log, TEXT("IPStreamMedia module has started. ^_^"));

#if WITH_FFMPEG
		LoadFFmpegLibraries();
#endif
	}

	virtual void ShutdownModule() override
	{
#if WITH_FFMPEG
		UnloadFFmpegLibraries();
#endif
		UE_LOG(LogIPStreamMedia, Log, TEXT("IPStreamMedia module has shut down. ^_^"));
	}

	//~ IIPStreamMediaModule interface

	virtual TSharedPtr<IMediaPlayer, ESPMode::ThreadSafe> CreatePlayer(IMediaEventSink& EventSink) override
	{
		// Editor will crash on worker's first FFmpeg call if all the DLLs are not loaded.
		// Returning nullptr is safer here instead, the facade will turn this nullptr into a failed open.
		if (!bFFmpegLoaded)
		{
			UE_LOG(LogIPStreamMedia, Error, TEXT("CreatePlayer: FFmpeg DLLs not loaded (see startup logs) - cannot create a player. -_-"));
			return nullptr;
		}

		return MakeShared<FIPStreamPlayer, ESPMode::ThreadSafe>(EventSink);
	}

private:

	// True only when all FFmpeg DLLs are loaded.
	bool bFFmpegLoaded = false;

#if WITH_FFMPEG
	void LoadFFmpegLibraries()
	{
		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("IPStreamMedia"));
		checkf(Plugin.IsValid(), TEXT("IPStreamMedia module started without it's own plugin being registered - should be impossible!"));

		const FString FFmpegBinPath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("ThirdParty"), TEXT("FFmpeg"), TEXT("bin"), TEXT("Win64"));
		FPlatformProcess::PushDllDirectory(*FFmpegBinPath);

		for (const TCHAR* DllName : GFFmpegDllNames)
		{
			if (void* Handle = FPlatformProcess::GetDllHandle(DllName))
			{
				FFmpegDllHandles.Add(Handle);
			}
			else
			{
				UE_LOG(LogIPStreamMedia, Error, TEXT("Failed to load %s from %s - FFmpeg decode will not be available. -_-"), DllName, *FFmpegBinPath);
			}
		}

		FPlatformProcess::PopDllDirectory(*FFmpegBinPath);

		bFFmpegLoaded = (FFmpegDllHandles.Num() == UE_ARRAY_COUNT(GFFmpegDllNames));
	}

	void UnloadFFmpegLibraries()
	{
		bFFmpegLoaded = false;

		// Reverse order on the way down is the general convention for unwinding
		// something that is acquired on a stack. We are following this rule here even though
		// Windows' refcounted LoadLibrary doesn't require it here.
		for (int32 Index = FFmpegDllHandles.Num() - 1; Index >= 0; --Index)
		{
			FPlatformProcess::FreeDllHandle(FFmpegDllHandles[Index]);
		}
		FFmpegDllHandles.Empty();
	}

	TArray<void*> FFmpegDllHandles;
#endif
};
	
IMPLEMENT_MODULE(FIPStreamMediaModule, IPStreamMedia)