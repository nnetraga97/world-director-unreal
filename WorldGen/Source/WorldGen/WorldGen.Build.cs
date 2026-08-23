using UnrealBuildTool;
using System.IO;

public class WorldGen : ModuleRules
{
	public WorldGen(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"EnhancedInput",
			"InputCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AIModule",
			"GameplayBehaviorsModule",
			"GameplayStateTreeModule",
			"GameplayTags",
			"PCG",
			"SmartObjectsModule",
			"StateTreeModule"
		});

		// UE 5.8's installed Mac toolchain links these non-weak libraries into the
		// game executable but does not stage them for source projects. Keep the
		// packaged app self-contained instead of relying on the local engine path.
		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			string TbbDirectory = Path.Combine(EngineDirectory, "Source/ThirdParty/Intel/TBB/Deploy/oneTBB-2022.3.0/Mac/lib");
			RuntimeDependencies.Add("$(TargetOutputDir)/libtbb.12.dylib", Path.Combine(TbbDirectory, "libtbb.12.dylib"));
			RuntimeDependencies.Add("$(TargetOutputDir)/libtbbmalloc.2.dylib", Path.Combine(TbbDirectory, "libtbbmalloc.2.dylib"));
			RuntimeDependencies.Add(
				"$(TargetOutputDir)/libmetalirconverter.dylib",
				Path.Combine(EngineDirectory, "Binaries/ThirdParty/Apple/MetalShaderConverter/Mac/libmetalirconverter.dylib"));
		}
	}
}
