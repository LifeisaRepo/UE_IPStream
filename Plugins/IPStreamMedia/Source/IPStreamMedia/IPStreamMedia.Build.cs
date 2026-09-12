// Copyright (c) 2026 Sanjyot Dahale. Licensed under the MIT License — see LICENSE.md.

using UnrealBuildTool;

public class IPStreamMedia : ModuleRules
{
	public IPStreamMedia(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
				
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"FFmpeg",		// For FFmpeg integration
				"Projects",		// For IPluginManager.h
				"MediaUtils",	// For FMediaSamples
			}
			);

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
                "Media"		// Headers only, no linkage - Media's public surface is pure-virtual
							// interfaces, so there is nothing to link against.
			}
            );

		PrivateIncludePathModuleNames.AddRange(
			new string[]
			{
				"Media"		// This is to ensure IMediaPlayer.h and friends can be included without linker errors.
			}
			);
		
	}
}
