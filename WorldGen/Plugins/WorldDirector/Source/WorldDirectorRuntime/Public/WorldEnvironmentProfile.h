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

USTRUCT(BlueprintType)
struct WORLDDIRECTORRUNTIME_API FWorldEnvironmentDressingAsset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName PlacementTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FSoftObjectPath MeshAsset;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Weight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float MinimumScale = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float MaximumScale = 1.1f;
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
	FString ContentVersion = TEXT("stylized-village-4");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile")
	bool bGenerationEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain")
	FSoftObjectPath OpaqueMasterMaterial;

	/** Project-owned four-layer terrain material driven by procedural-mesh vertex colors. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain")
	FSoftObjectPath TerrainBlendMaterial;

	/** Project-owned paving material used only by civic and public courtyard overlays. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain")
	FSoftObjectPath PavingMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain")
	TArray<FWorldEnvironmentSurfaceAsset> Surfaces;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain")
	FSoftObjectPath RockMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain")
	FSoftObjectPath WaterMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dressing")
	TArray<FSoftObjectPath> DressingMeshes;

	/**
	 * Runtime biome palette. DressingMeshes remains as a compact compatibility
	 * list for older authored profiles, while this table carries placement and
	 * scale semantics for the physical generator.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dressing")
	TArray<FWorldEnvironmentDressingAsset> DressingAssets;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Certification")
	float MaximumPlotSlopeDegrees = 13.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Certification")
	float PlotClearanceCentimeters = 340.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Certification")
	float DressingExclusionCentimeters = 500.0f;
};
