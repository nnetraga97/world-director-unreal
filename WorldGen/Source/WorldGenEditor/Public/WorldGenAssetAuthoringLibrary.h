#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "WorldGenAssetAuthoringLibrary.generated.h"

UCLASS()
class WORLDGENEDITOR_API UWorldGenAssetAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "WorldGen|Authoring")
	static bool AuthorCapabilityLandscape(
		const FString& MapPackageName = TEXT("/Game/CapabilityPack/Maps/L_CapabilityLandscape"));

	UFUNCTION(BlueprintCallable, Category = "WorldGen|Authoring")
	static bool AuthorCompilerTownMap(
		const FString& SourceMapPackageName = TEXT("/Game/CapabilityPack/Maps/L_CapabilityLandscape"),
		const FString& DestinationMapPackageName = TEXT("/Game/WorldDirector/Maps/L_WorldDirectorTown"));

	UFUNCTION(BlueprintCallable, Category = "WorldGen|Authoring")
	static bool AuthorResidentLifeStateTree(
		const FString& AssetPackageName = TEXT("/Game/WorldDirector/AI/ST_ResidentLife"));

	UFUNCTION(BlueprintCallable, Category = "WorldGen|Authoring")
	static bool ConfigureLivingTownMap(
		const FString& MapPackageName = TEXT("/Game/WorldDirector/Maps/L_WorldDirectorTown"),
		const FString& FixtureFilename = TEXT("living-town.json"));
};
