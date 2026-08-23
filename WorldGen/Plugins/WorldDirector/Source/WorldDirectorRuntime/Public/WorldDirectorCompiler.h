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
};
