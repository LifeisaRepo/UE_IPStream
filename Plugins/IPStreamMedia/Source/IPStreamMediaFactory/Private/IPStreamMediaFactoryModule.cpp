// Copyright (c) 2026 Sanjyot Dahale. Licensed under the MIT License — see LICENSE.md.

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogIPStreamMediaFactory, Log, All);

class FIPStreamMediaFactoryModule : public IModuleInterface
{
public:

	virtual void StartupModule() override
	{
		UE_LOG(LogIPStreamMediaFactory, Log, TEXT("IPStreamMediaFactory module has started!"));
	}

	virtual void ShutdownModule() override
	{
		UE_LOG(LogIPStreamMediaFactory, Log, TEXT("IPStreamMediaFactory module has shut down"));
	}
};

IMPLEMENT_MODULE(FIPStreamMediaFactoryModule, IPStreamMediaFactory);
