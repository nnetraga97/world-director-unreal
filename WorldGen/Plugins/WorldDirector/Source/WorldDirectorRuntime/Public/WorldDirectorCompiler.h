#pragma once

#include "CoreMinimal.h"
#include "WorldDirectorTypes.h"

class AWorldDirectorTownActor;

class WORLDDIRECTORRUNTIME_API FWorldDirectorCompiler
{
public:
	static bool Resolve(
		const FGeneratedWorldSpec& Spec,
		FResolvedWorldPlan& OutPlan,
		FValidationReport& OutReport,
		bool bLoadAssets = true);

	static bool Spawn(
		UWorld* World,
		const FResolvedWorldPlan& Plan,
		AWorldDirectorTownActor*& OutTown,
		FValidationReport& OutReport);

	/**
	 * A resident's daily routine, shaped by occupation: farmers start early, merchants
	 * keep shop late, some guards stand night watch, and only some residents spend the
	 * evening at the landmark. Stops are sorted by hour; the stop with the latest hour
	 * not after the current hour is active.
	 */
	/** The buildings the model can get, and the purposes and name words that select each. */
	static TArray<TSharedPtr<class FJsonValue>> BuildBuildingManifest();

	static TArray<FWorldDirectorScheduleStop> BuildResidentSchedule(
		const FResident& Resident,
		const FString& LandmarkLocationId,
		int32 WorldSeed);
};
