#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WorldEnvironmentProfile.generated.h"

USTRUCT(BlueprintType)
struct WORLDDIRECTORRUNTIME_API FWorldEnvironmentSurfaceAsset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName SurfaceTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FSoftObjectPath BaseColorTexture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FSoftObjectPath NormalTexture;
};

UCLASS(BlueprintType)
class WORLDDIRECTORRUNTIME_API UWorldEnvironmentProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UWorldEnvironmentProfile();

	static const UWorldEnvironmentProfile* ResolveStylizedVillage();
	bool Validate(FString& OutError) const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile")
	FName ProfileTag = TEXT("Profile.StylizedVillage");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile")
	FString ContentVersion = TEXT("stylized-village-1");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile")
	bool bGenerationEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain")
	FSoftObjectPath OpaqueMasterMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain")
	TArray<FWorldEnvironmentSurfaceAsset> Surfaces;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain")
	FSoftObjectPath RockMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain")
	FSoftObjectPath WaterMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dressing")
	TArray<FSoftObjectPath> DressingMeshes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Certification")
	float MaximumPlotSlopeDegrees = 13.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Certification")
	float PlotClearanceCentimeters = 340.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Certification")
	float DressingExclusionCentimeters = 500.0f;
};
