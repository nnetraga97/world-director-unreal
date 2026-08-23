#pragma once

#include "CoreMinimal.h"
#include "WorldDirectorTypes.h"

class WORLDDIRECTORRUNTIME_API FWorldDirectorJson
{
public:
	static bool LoadGeneratedWorldSpec(
		const FString& Json,
		FGeneratedWorldSpec& OutSpec,
		FValidationReport& OutParseReport);

	static bool SaveGeneratedWorldSpec(
		const FGeneratedWorldSpec& Spec,
		FString& OutJson,
		FValidationReport& OutSerializationReport);

	static bool LoadResolvedWorldPlan(
		const FString& Json,
		FResolvedWorldPlan& OutPlan,
		FValidationReport& OutParseReport);

	static bool SaveResolvedWorldPlan(
		const FResolvedWorldPlan& Plan,
		FString& OutJson,
		FValidationReport& OutSerializationReport);
};
