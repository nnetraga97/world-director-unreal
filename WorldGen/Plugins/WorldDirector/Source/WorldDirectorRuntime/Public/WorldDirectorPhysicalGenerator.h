#pragma once

#include "CoreMinimal.h"
#include "WorldDirectorTypes.h"

class WORLDDIRECTORRUNTIME_API FWorldDirectorPhysicalGenerator
{
public:
	static bool Generate(
		const FGeneratedWorldSpec& Spec,
		FResolvedWorldPlan& InOutPlan,
		FValidationReport& InOutReport);

	// Maps a brief.landform value (basin, valley, ridge, coast, marsh) to its archetype.
	static bool TryParseLandform(const FString& Landform, EWorldDirectorTerrainArchetype* OutArchetype);
	// Uses brief.landform when present; legacy briefs fall back to the earliest
	// unnegated landform keyword in their terrain preferences.
	static EWorldDirectorTerrainArchetype ResolveTerrainArchetype(const FWorldBrief& Brief, int32 Seed);

	static int32 DeriveStageSeed(int32 RootSeed, const FString& StageName, int32 Index = 0);
	static FString FingerprintBytes(const TArray<uint8>& Bytes);
	static FString BuildCandidateSummary(const FResolvedWorldPlan& Plan);
	static int32 SampleHeightCentimeters(
		const FWorldDirectorTerrainRecipe& Terrain,
		const FVector2D& Position);
	static float SampleSlopeDegrees(
		const FWorldDirectorTerrainRecipe& Terrain,
		const FVector2D& Position);
};
