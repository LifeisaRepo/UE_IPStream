// Copyright (c) 2026 Sanjyot Dahale. Licensed under the MIT License — see LICENSE.md.

using UnrealBuildTool;
/**
* This factory descriptor of the IPStreamMedia Player. Media player factories hold 
* and carry metadata of their media player even where the media player cannot exist.
* (ex. The metadata of an Android-only Media player still needs to available on Win64
*  so that we can actually set it in the UMediaSource settings.)
*/
public class IPStreamMediaFactory : ModuleRules
{
    public IPStreamMediaFactory(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        
        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
            }
        );

        DynamicallyLoadedModuleNames.AddRange(
            new string[]
            {
                "Media",            // Headers only - nothing linkable in it (see IPStreamMedia.Build.cs)
                "IPStreamMedia"     // Deliberately NOT linked: no import-table entry means the player
                                    // module loads only when CreatePlayer asks for it, which is what
                                    // keeps the factory decoupled from the player
            }
            );

        PrivateIncludePathModuleNames.AddRange(
            new string[]
            {
                "Media",            // For IMediaPlayerFactory.h and IMediaModule.h
                "IPStreamMedia"     // For IIPStreamMediaModule.h
            }
            );
    }
}