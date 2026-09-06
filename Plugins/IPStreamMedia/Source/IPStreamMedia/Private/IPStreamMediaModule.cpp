// Copyright (c) 2026 Sanjyot Dahale. Licensed under the MIT License — see LICENSE.md.

#include "IIPStreamMediaModule.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogIPStreamMedia);

class FIPStreamMediaModule : public IIPStreamMediaModule
{
public:

	virtual void StartupModule() override
	{
		UE_LOG(LogIPStreamMedia, Log, TEXT("IPStreamMedia module has started"));
	}

	virtual void ShutdownModule() override
	{
		UE_LOG(LogIPStreamMedia, Log, TEXT("IPStreamMedia module has shut down"));
	}
};
	
IMPLEMENT_MODULE(FIPStreamMediaModule, IPStreamMedia)