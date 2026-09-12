// Copyright (c) 2026 Sanjyot Dahale. Licensed under the MIT License — see LICENSE.md.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Templates/SharedPointer.h"

class IMediaEventSink;
class IMediaPlayer;

DECLARE_LOG_CATEGORY_EXTERN(LogIPStreamMedia, Log, All);

/**
 * Public interface for the IPStreamMedia module.
 */
class IIPStreamMediaModule : public IModuleInterface
{
public:
	/**
	* Creates an IP stream media player
	* 
	* @param EventSink The object that will receive the player's events.
	* @return A new media player, or nullptr if a player couldn't be created.
	*/
	virtual TSharedPtr<IMediaPlayer, ESPMode::ThreadSafe> CreatePlayer(IMediaEventSink& EventSink) = 0;
};