// Copyright (c) 2026 Sanjyot Dahale. Licensed under the MIT License — see LICENSE.md.

using UnrealBuildTool;
using System.Collections.Generic;

public class IPStreamMediaDemoTarget : TargetRules
{
	public IPStreamMediaDemoTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V4;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_3;
		ExtraModuleNames.Add("IPStreamMediaDemo");
	}
}
