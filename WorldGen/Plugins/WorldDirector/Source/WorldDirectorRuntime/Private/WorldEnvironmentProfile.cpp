#include "WorldEnvironmentProfile.h"

UWorldEnvironmentProfile::UWorldEnvironmentProfile()
{
	OpaqueMasterMaterial = FSoftObjectPath(
		TEXT("/Game/Fantastic_Village_Pack/materials/master_materials/M_Master_opaque.M_Master_opaque"));
	RockMaterial = FSoftObjectPath(
		TEXT("/Game/Fantastic_Village_Pack/materials/MI_ENV_stone.MI_ENV_stone"));
	WaterMaterial = FSoftObjectPath(
		TEXT("/Game/Fantastic_Village_Pack/materials/MI_ENV_water.MI_ENV_water"));
	Surfaces = {
		{TEXT("Surface.Grass"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/textures/T_ENV_TERRAIN_grass_01_BC.T_ENV_TERRAIN_grass_01_BC")),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/textures/T_ENV_TERRAIN_grass_01_N.T_ENV_TERRAIN_grass_01_N"))},
		{TEXT("Surface.Gravel"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/textures/T_ENV_TERRAIN_gravel_BC.T_ENV_TERRAIN_gravel_BC")),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/textures/T_ENV_TERRAIN_gravel_N.T_ENV_TERRAIN_gravel_N"))},
		{TEXT("Surface.Paving"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/textures/T_ENV_pavingstone_01_BC.T_ENV_pavingstone_01_BC")),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/textures/T_ENV_pavingstone_01_N.T_ENV_pavingstone_01_N"))},
		{TEXT("Surface.Farmfield"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/textures/T_ENV_farmfield_BC.T_ENV_farmfield_BC")),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/textures/T_ENV_farmfield_N.T_ENV_farmfield_N"))}
	};
	DressingMeshes = {
		FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/environment/SM_ENV_TREE_village_LOD0.SM_ENV_TREE_village_LOD0")),
		FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/natural/SM_PROP_stone_03.SM_PROP_stone_03")),
		FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/natural/SM_PROP_hay_03.SM_PROP_hay_03")),
		FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/deco/SM_PROP_signpost_02.SM_PROP_signpost_02"))
	};
}

const UWorldEnvironmentProfile* UWorldEnvironmentProfile::ResolveStylizedVillage()
{
	if (const UWorldEnvironmentProfile* Asset = LoadObject<UWorldEnvironmentProfile>(
		nullptr, TEXT("/Game/WorldDirector/Profiles/DA_StylizedVillage.DA_StylizedVillage")))
	{
		return Asset;
	}
	return GetDefault<UWorldEnvironmentProfile>();
}

bool UWorldEnvironmentProfile::Validate(FString& OutError) const
{
	if (!bGenerationEnabled || ProfileTag.IsNone() || Surfaces.Num() < 4 || DressingMeshes.IsEmpty() ||
		OpaqueMasterMaterial.IsNull() || RockMaterial.IsNull() || WaterMaterial.IsNull())
	{
		OutError = TEXT("StylizedVillage profile is incomplete or disabled.");
		return false;
	}
	for (const FWorldEnvironmentSurfaceAsset& Surface : Surfaces)
	{
		if (Surface.SurfaceTag.IsNone() || Surface.BaseColorTexture.IsNull() || Surface.NormalTexture.IsNull())
		{
			OutError = TEXT("A certified profile surface is missing its semantic tag or textures.");
			return false;
		}
	}
	return true;
}
