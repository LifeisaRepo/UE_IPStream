// Copyright (c) 2026 Sanjyot Dahale. Licensed under the MIT License — see LICENSE.md.

using System.IO;
using UnrealBuildTool;

public class FFmpeg : ModuleRules
{
    public FFmpeg(ReadOnlyTargetRules Target) : base(Target)
    {
        // This module exists to purely to describe how to find and link against the FFmpeg third-party libraries.
        Type = ModuleType.External;

        // Actual binaries live one level up in the Plugins/IPStreamMedia/ThirdParty/FFmpeg directory.
        string FFmpegPath = Path.Combine(PluginDirectory, "ThirdParty", "FFmpeg");

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            // PublicSystemIncludePaths marks these files as system headers, which suppresses UE's warnings-as-errors from them.
            PublicSystemIncludePaths.Add(Path.Combine(FFmpegPath, "include"));

            string LibPath = Path.Combine(FFmpegPath, "lib", "Win64");
            string BinPath = Path.Combine(FFmpegPath, "bin", "Win64");

            // Linking only what we need right now,
            // DLL filenames carry FFmpeg's ABI version per library, which is why they cannot be derived just from the plain lib name.
            // They also change independently on every FFmpeg rebuild. Source of truth for the pinned versions is ThirdParty/FFmpeg/NOTICE.md;
            // Recheck both the NOTICE.md and the actual DLLs in ThirdParty/FFmpeg/bin/Win64 when updating the FFmpeg binaries.

            (string LibName, string DllName)[] Libraries =
            {
                ("avutil", "avutil-61.dll"),
                ("avcodec", "avcodec-63.dll"),
                ("avformat", "avformat-63.dll"),
                ("swscale", "swscale-10.dll"),
            };

            foreach((string LibName, string DllName) in Libraries)
            {                
                PublicAdditionalLibraries.Add(Path.Combine(LibPath, LibName + ".lib"));

                // Read more about delay loading in Obsidian docs
                PublicDelayLoadDLLs.Add(DllName);

                RuntimeDependencies.Add(Path.Combine(BinPath, DllName));
            }
            PublicDefinitions.Add("WITH_FFMPEG=1");
        }
        else
        {
            PublicDefinitions.Add("WITH_FFMPEG=0");
        }

    }
}