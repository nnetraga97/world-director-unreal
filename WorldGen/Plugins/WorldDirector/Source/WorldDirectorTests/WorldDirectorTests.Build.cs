using UnrealBuildTool;

public class WorldDirectorTests : ModuleRules
{
	public WorldDirectorTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Json",
			"JsonUtilities",
			"ProceduralMeshComponent",
			"Projects",
			"WorldDirectorRuntime"
		});
	}
}
