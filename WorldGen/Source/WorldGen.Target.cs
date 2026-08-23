using UnrealBuildTool;
using System.Collections.Generic;

public class WorldGenTarget : TargetRules
{
	public WorldGenTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("WorldGen");
	}
}
