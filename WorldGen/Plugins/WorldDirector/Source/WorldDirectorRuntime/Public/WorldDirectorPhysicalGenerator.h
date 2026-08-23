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
