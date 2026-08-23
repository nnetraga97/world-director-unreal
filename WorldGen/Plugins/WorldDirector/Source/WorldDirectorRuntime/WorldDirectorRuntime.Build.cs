using UnrealBuildTool;

public class WorldDirectorRuntime : ModuleRules
{
	public WorldDirectorRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"AIModule",
			"NavigationSystem",
			"GameplayTags",
			"GameplayTasks",
			"SmartObjectsModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"Json",
			"JsonUtilities",
			"Projects",
			"UMG",
			"Slate",
			"SlateCore",
			"ApplicationCore",
			"ImageWrapper"
			,"ProceduralMeshComponent",
			"Landscape"
			,"PlatformCrypto",
			"PlatformCryptoContext"
		});

		RuntimeDependencies.Add("$(PluginDir)/Resources/Companion/world_director_companion.py");
		RuntimeDependencies.Add("$(PluginDir)/Resources/Fixtures/living-town.json");
	}
}
