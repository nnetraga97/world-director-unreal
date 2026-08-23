#pragma once

#include "CoreMinimal.h"
#include "WorldDirectorTypes.h"

class WORLDDIRECTORRUNTIME_API FWorldDirectorValidator
{
public:
	static FValidationReport Validate(
		const FGeneratedWorldSpec& Spec,
		const TSet<FName>& CapabilityTags);

private:
	static bool LooksLikeUnrealAssetPath(const FString& Value);
};
