// Copyright (c) 2026 Sanjyot Dahale. Licensed under the MIT License — see LICENSE.md.

#include "IIPStreamMediaModule.h"

#include "IMediaModule.h"
#include "IMediaPlayerFactory.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Misc/Guid.h"

DEFINE_LOG_CATEGORY_STATIC(LogIPStreamMediaFactory, Log, All);

#define LOCTEXT_NAMESPACE "FIPStreamMediaFactoryModule"

class FIPStreamMediaFactoryModule : public IMediaPlayerFactory, public IModuleInterface
{
public:

	//~ IMediaPlayerFactory interface

	virtual bool CanPlayUrl(const FString& Url, const IMediaOptions* Options, TArray<FText>* OutWarnings, TArray<FText>* OutErrors) const override
	{
		FString Scheme;
		FString Location;

		if (!Url.Split(TEXT("://"), &Scheme, &Location, ESearchCase::CaseSensitive))
		{
			if (OutErrors != nullptr)
			{
				OutErrors->Add(LOCTEXT("NoSchemeFound", "No URI scheme found"));
			}
			return false;
		}

		if (!SupportedUriSchemes.Contains(Scheme))
		{
			if (OutErrors != nullptr)
			{
				OutErrors->Add(FText::Format(LOCTEXT("SchemeNotSupported", "The URI scheme {0} is not supported"), FText::FromString(Scheme)));
			}
			return false;
		}

		return true;
	}

	virtual TSharedPtr<IMediaPlayer, ESPMode::ThreadSafe> CreatePlayer(IMediaEventSink& EventSink) override
	{
		IIPStreamMediaModule* IPStreamMediaModule = FModuleManager::LoadModulePtr<IIPStreamMediaModule>("IPStreamMedia");
		return (IPStreamMediaModule != nullptr) ? IPStreamMediaModule->CreatePlayer(EventSink) : nullptr;
	}

	virtual FText GetDisplayName() const override
	{
		return LOCTEXT("MediaPlayerDisplayName", "IP Stream Media");
	}

	virtual FName GetPlayerName() const override
	{
		static FName PlayerName(TEXT("IPStreamMedia"));
		return PlayerName;
	}

	virtual FGuid GetPlayerPluginGUID() const override
	{
		//0x6bd90b53, 0xbe5340cd, 0xab90c371, 0x7f1b284a
		static FGuid PlayerPluginGUID(0x6bd90b53, 0xbe5340cd, 0xab90c371, 0x7f1b284a);
		return PlayerPluginGUID;
	}

	virtual const TArray<FString>& GetSupportedPlatforms() const override
	{
		return SupportedPlatforms;
	}

	virtual bool SupportsFeature(EMediaFeature Feature) const override
	{
		// Only supports standard video (no 360 or stereo as well for now)
		return ((Feature == EMediaFeature::VideoSamples) || (Feature == EMediaFeature::VideoTracks));
	}

public:

	//~ IModuleInterface interface

	virtual void StartupModule() override
	{
		SupportedPlatforms.Add(TEXT("Windows"));
		SupportedUriSchemes.Add(TEXT("rtsp"));
		
		// Loads Media module dynamically
		IMediaModule* MediaModule = FModuleManager::LoadModulePtr<IMediaModule>("Media");

		if (MediaModule != nullptr)
		{
			MediaModule->RegisterPlayerFactory(*this);
			UE_LOG(LogIPStreamMediaFactory, Display, TEXT("IPStreamMedia player factory registered"));
		}
		else
		{
			UE_LOG(LogIPStreamMediaFactory, Error, TEXT("'Media' module unavailable - IPStreamMedia player factory NOT registered"));
		}
	}

	virtual void ShutdownModule() override
	{
		IMediaModule* MediaModule = FModuleManager::GetModulePtr<IMediaModule>("Media");

		if (MediaModule != nullptr)
		{
			MediaModule->UnregisterPlayerFactory(*this);
		}
	}

private:
	/**
	* Platform names this player supports.
	* FPlatformProperties::IniPlatformName values, not UnrealTargetPlatform ones
	*/
	TArray<FString> SupportedPlatforms;

	// Uri schemes this player supports
	TArray<FString> SupportedUriSchemes;
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FIPStreamMediaFactoryModule, IPStreamMediaFactory);
