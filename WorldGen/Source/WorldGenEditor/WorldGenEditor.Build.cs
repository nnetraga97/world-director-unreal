using UnrealBuildTool;

public class WorldGenEditor : ModuleRules
{
	public WorldGenEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Landscape",
			"AssetRegistry",
			"AssetTools",
			"GameplayStateTreeModule",
			"NavigationSystem",
			"PropertyBindingUtils",
			"PropertyBindingUtilsEditor",
			"StateTreeEditorModule",
			"StateTreeModule",
			"UnrealEd",
			"WorldGen",
			"WorldDirectorRuntime"
		});
	}
}
