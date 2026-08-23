using UnrealBuildTool;

public class WorldDirectorEditor : ModuleRules
{
	public WorldDirectorEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"WorldDirectorRuntime"
		});
	}
}
