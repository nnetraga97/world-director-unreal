#include "WorldEnvironmentProfile.h"

UWorldEnvironmentProfile::UWorldEnvironmentProfile()
{
	OpaqueMasterMaterial = FSoftObjectPath(
		TEXT("/Game/Fantastic_Village_Pack/materials/master_materials/M_Master_opaque_normal.M_Master_opaque_normal"));
	TerrainBlendMaterial = FSoftObjectPath(
		TEXT("/Game/WorldDirector/Materials/M_WorldDirectorTerrainBlend.M_WorldDirectorTerrainBlend"));
	PavingMaterial = FSoftObjectPath(
		TEXT("/Game/WorldDirector/Materials/M_WorldDirectorPaving.M_WorldDirectorPaving"));
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
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/textures/T_ENV_farmfield_N.T_ENV_farmfield_N"))},
		{TEXT("Surface.Rock"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/textures/T_ENV_TERRAIN_gravel_02_BC.T_ENV_TERRAIN_gravel_02_BC")),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/textures/T_ENV_TERRAIN_gravel_02_N.T_ENV_TERRAIN_gravel_02_N"))}
	};
	DressingMeshes = {
		FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/environment/SM_ENV_TREE_village_LOD0.SM_ENV_TREE_village_LOD0")),
		FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/natural/SM_PROP_stone_03.SM_PROP_stone_03")),
		FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/natural/SM_PROP_hay_03.SM_PROP_hay_03")),
		FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/deco/SM_PROP_signpost_02.SM_PROP_signpost_02"))
	};
	DressingAssets = {
		{TEXT("Dressing.Canopy"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/environment/SM_ENV_TREE_village_LOD0.SM_ENV_TREE_village_LOD0")),
			1.0f, 1.08f, 2.05f},
		{TEXT("Dressing.GroundCover"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/environment/SM_ENV_PLANT_grass_village.SM_ENV_PLANT_grass_village")),
			1.0f, 0.72f, 1.38f},
		{TEXT("Dressing.GroundCover"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/environment/SM_ENV_PLANT_leaf_village.SM_ENV_PLANT_leaf_village")),
			0.45f, 0.75f, 1.25f},
		{TEXT("Dressing.Rock"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/natural/SM_PROP_stone_01.SM_PROP_stone_01")),
			0.85f, 0.68f, 1.75f},
		{TEXT("Dressing.Rock"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/natural/SM_PROP_stone_03.SM_PROP_stone_03")),
			1.0f, 0.72f, 1.85f},
		{TEXT("Dressing.Rock"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/natural/SM_PROP_stone_04.SM_PROP_stone_04")),
			0.75f, 0.66f, 1.55f},
		{TEXT("Dressing.Deadwood"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/natural/SM_PROP_treetrunk_01.SM_PROP_treetrunk_01")),
			0.6f, 0.82f, 1.25f},
		{TEXT("Dressing.Deadwood"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/natural/SM_PROP_treetrunk_03.SM_PROP_treetrunk_03")),
			0.4f, 0.78f, 1.2f},
		{TEXT("Dressing.FarmAccent"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/natural/SM_PROP_hay_03.SM_PROP_hay_03")),
			1.0f, 0.86f, 1.2f},
		{TEXT("Dressing.FarmFence"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/construction/SM_PROP_fence_v01_01.SM_PROP_fence_v01_01")),
			1.0f, 0.92f, 1.08f},
		{TEXT("Dressing.FarmFence"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/construction/SM_PROP_fence_v01_02.SM_PROP_fence_v01_02")),
			0.85f, 0.92f, 1.08f},
		{TEXT("Dressing.FarmFence"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/construction/SM_PROP_fence_v01_03.SM_PROP_fence_v01_03")),
			0.7f, 0.92f, 1.08f},
		{TEXT("Dressing.Wayfinding"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/deco/SM_PROP_signpost_02.SM_PROP_signpost_02")),
			1.0f, 0.9f, 1.08f},
		{TEXT("Dressing.Roadside"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/light/SM_PROP_streetlamp_v02_01.SM_PROP_streetlamp_v02_01")),
			1.0f, 0.92f, 1.08f},
		{TEXT("Dressing.CivicWell"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/deco/SM_PROP_well.SM_PROP_well")),
			1.0f, 0.96f, 1.08f},
		{TEXT("Dressing.CivicSeat"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/furniture/SM_PROP_bench_01.SM_PROP_bench_01")),
			1.0f, 0.96f, 1.06f},
		{TEXT("Dressing.GuildBanner"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/deco/SM_PROP_flag_01.SM_PROP_flag_01")),
			1.0f, 0.9f, 1.04f},
		{TEXT("Dressing.GuildBanner"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/deco/SM_PROP_flag_03.SM_PROP_flag_03")),
			0.75f, 0.9f, 1.04f},
		{TEXT("Dressing.Transport"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/vehicles/SM_PROP_cart_01.SM_PROP_cart_01")),
			1.0f, 0.92f, 1.04f},
		{TEXT("Dressing.Transport"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/vehicles/SM_PROP_cart_02.SM_PROP_cart_02")),
			0.8f, 0.92f, 1.04f},
		{TEXT("Dressing.Transport"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/vehicles/SM_PROP_cart_03.SM_PROP_cart_03")),
			0.65f, 0.92f, 1.04f},
		{TEXT("Dressing.HomeUtility"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/furniture/SM_PROP_outhouse.SM_PROP_outhouse")),
			0.55f, 0.9f, 1.02f},
		{TEXT("Dressing.HomeUtility"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/container/SM_PROP_trough_01.SM_PROP_trough_01")),
			0.75f, 0.9f, 1.08f},
		{TEXT("Dressing.HomeUtility"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/deco/SM_PROP_firepit.SM_PROP_firepit")),
			0.7f, 0.9f, 1.12f},
		{TEXT("Dressing.InnYard"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/deco/SM_PROP_horsestation.SM_PROP_horsestation")),
			1.0f, 0.96f, 1.08f},
		{TEXT("Dressing.InnYard"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/container/SM_PROP_trough_02.SM_PROP_trough_02")),
			0.75f, 0.94f, 1.08f},
		{TEXT("Dressing.CommunalFire"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/deco/SM_PROP_firepit.SM_PROP_firepit")),
			1.0f, 1.05f, 1.25f},
		{TEXT("Dressing.SettlementClutter"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/container/SM_PROP_barrel_02.SM_PROP_barrel_02")),
			1.0f, 0.88f, 1.12f},
		{TEXT("Dressing.SettlementClutter"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/container/SM_PROP_crate_01.SM_PROP_crate_01")),
			0.9f, 0.86f, 1.1f},
		{TEXT("Dressing.SettlementClutter"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/food/SM_PROP_sack_03.SM_PROP_sack_03")),
			0.7f, 0.9f, 1.1f},
		{TEXT("Dressing.SettlementClutter"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/furniture/SM_PROP_bench_01.SM_PROP_bench_01")),
			0.55f, 0.9f, 1.05f},
		{TEXT("Dressing.MarketStall"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/construction/SM_PROP_market_v01_01.SM_PROP_market_v01_01")),
			1.0f, 0.92f, 1.04f},
		{TEXT("Dressing.MarketStall"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/construction/SM_PROP_market_v02_01.SM_PROP_market_v02_01")),
			0.8f, 0.92f, 1.04f},
		{TEXT("Dressing.MarketStall"),
			FSoftObjectPath(TEXT("/Game/Fantastic_Village_Pack/meshes/props/construction/SM_PROP_market_v03_01.SM_PROP_market_v03_01")),
			0.65f, 0.92f, 1.04f}
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
	if (!bGenerationEnabled || ProfileTag.IsNone() || Surfaces.Num() < 4 ||
		(DressingAssets.IsEmpty() && DressingMeshes.IsEmpty()) ||
		OpaqueMasterMaterial.IsNull() || TerrainBlendMaterial.IsNull() || PavingMaterial.IsNull() ||
		RockMaterial.IsNull() || WaterMaterial.IsNull())
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
	for (const FWorldEnvironmentDressingAsset& Dressing : DressingAssets)
	{
		if (Dressing.PlacementTag.IsNone() || Dressing.MeshAsset.IsNull() || Dressing.Weight <= 0.0f ||
			Dressing.MinimumScale <= 0.0f || Dressing.MaximumScale < Dressing.MinimumScale)
		{
			OutError = TEXT("A certified dressing asset has invalid placement or scale metadata.");
			return false;
		}
	}
	return true;
}
