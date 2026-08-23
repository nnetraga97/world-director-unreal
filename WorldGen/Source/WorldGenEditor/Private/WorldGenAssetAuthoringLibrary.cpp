#include "WorldGenAssetAuthoringLibrary.h"

#include "Editor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/DirectionalLight.h"
#include "EngineUtils.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "FileHelpers.h"
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "GameFramework/PlayerStart.h"
#include "WorldDirectorTownActors.h"
#include "WorldDirectorResidentStateTree.h"
#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeFactory.h"
#include "UObject/SavePackage.h"
#include "ActorFactories/ActorFactoryBoxVolume.h"
#include "Builders/CubeBuilder.h"

namespace
{
constexpr int32 ComponentsPerSide = 16;
constexpr int32 SectionsPerComponent = 2;
constexpr int32 QuadsPerSection = 63;
constexpr int32 QuadsPerSide = ComponentsPerSide * SectionsPerComponent * QuadsPerSection;
constexpr int32 VerticesPerSide = QuadsPerSide + 1;

uint16 MakeTerrainHeight(const int32 X, const int32 Y)
{
	const double NX = (static_cast<double>(X) / QuadsPerSide) * 2.0 - 1.0;
	const double NY = (static_cast<double>(Y) / QuadsPerSide) * 2.0 - 1.0;
	const double Radius = FMath::Sqrt(NX * NX + NY * NY);

	// Keep the central settlement basin broad and nearly flat. Outside it,
	// introduce gentle deterministic slopes; add one elevated landmark shelf.
	const double BasinBlend = FMath::Clamp((Radius - 0.38) / 0.42, 0.0, 1.0);
	const double Undulation =
		(260.0 * FMath::Sin(NX * 4.4) + 180.0 * FMath::Cos(NY * 5.1)) * BasinBlend;
	const double HillDX = NX - 0.58;
	const double HillDY = NY - 0.42;
	const double LandmarkHill = 2600.0 * FMath::Exp(-(HillDX * HillDX + HillDY * HillDY) / 0.045);
	const double BasinTerrace = Radius < 0.36 ? 0.0 : 90.0 * BasinBlend;

	return static_cast<uint16>(FMath::Clamp(
		32768.0 + Undulation + LandmarkHill + BasinTerrace,
		0.0,
		65535.0));
}
}

bool UWorldGenAssetAuthoringLibrary::AuthorCapabilityLandscape(const FString& MapPackageName)
{
	if (!GEditor || !FPackageName::IsValidLongPackageName(MapPackageName))
	{
		return false;
	}

	UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
	if (!World)
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = TEXT("CapabilityLandscape_2km");
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FVector LandscapeLocation(-QuadsPerSide * 50.0, -QuadsPerSide * 50.0, 0.0);
	ALandscape* Landscape = World->SpawnActor<ALandscape>(
		LandscapeLocation,
		FRotator::ZeroRotator,
		SpawnParameters);
	if (!Landscape)
	{
		return false;
	}

	// Landscape vertices are one Unreal unit apart after Import. A 100 cm actor
	// scale yields the intended 2.016 km footprint from 2,016 quads.
	Landscape->SetActorScale3D(FVector(100.0));
	Landscape->SetActorLabel(TEXT("Capability Landscape - 2km Basin and Landmark Shelf"));
	Landscape->Tags.Add(TEXT("WorldDirector.TerrainEnvelope"));
	Landscape->Tags.Add(TEXT("WorldDirector.BuildableBasin"));
	Landscape->Tags.Add(TEXT("WorldDirector.LandmarkShelf"));
	Landscape->LandscapeMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Game/Fantastic_Village_Pack/materials/MI_landscape.MI_landscape"));
	if (Landscape->LandscapeMaterial == nullptr)
	{
		Landscape->Destroy();
		return false;
	}

	TArray<uint16> Heights;
	Heights.SetNumUninitialized(VerticesPerSide * VerticesPerSide);
	for (int32 Y = 0; Y < VerticesPerSide; ++Y)
	{
		for (int32 X = 0; X < VerticesPerSide; ++X)
		{
			Heights[Y * VerticesPerSide + X] = MakeTerrainHeight(X, Y);
		}
	}

	const FGuid BaseLayerGuid;
	TMap<FGuid, TArray<uint16>> HeightDataPerLayers;
	HeightDataPerLayers.Add(BaseLayerGuid, MoveTemp(Heights));
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayers;
	TArray<FLandscapeImportLayerInfo> SurfaceLayers;
	const TPair<const TCHAR*, const TCHAR*> LayerDefinitions[] = {
		{TEXT("Grass"), TEXT("/Game/Fantastic_Village_Pack/build/Grass_LayerInfo.Grass_LayerInfo")},
		{TEXT("Gravel"), TEXT("/Game/Fantastic_Village_Pack/build/Gravel_LayerInfo.Gravel_LayerInfo")},
		{TEXT("Gravel2"), TEXT("/Game/Fantastic_Village_Pack/build/Gravel2_LayerInfo.Gravel2_LayerInfo")},
		{TEXT("Pavingstone1"), TEXT("/Game/Fantastic_Village_Pack/build/Pavingstone1_LayerInfo.Pavingstone1_LayerInfo")},
		{TEXT("Pavingstone2"), TEXT("/Game/Fantastic_Village_Pack/build/Pavingstone2_LayerInfo.Pavingstone2_LayerInfo")},
		{TEXT("Farmfield"), TEXT("/Game/Fantastic_Village_Pack/build/Farmfield_LayerInfo.Farmfield_LayerInfo")}
	};
	for (int32 LayerIndex = 0; LayerIndex < UE_ARRAY_COUNT(LayerDefinitions); ++LayerIndex)
	{
		FLandscapeImportLayerInfo& Layer = SurfaceLayers.AddDefaulted_GetRef();
		Layer.LayerName = FName(LayerDefinitions[LayerIndex].Key);
		Layer.LayerInfo = LoadObject<ULandscapeLayerInfoObject>(nullptr, LayerDefinitions[LayerIndex].Value);
		if (Layer.LayerInfo == nullptr)
		{
			Landscape->Destroy();
			return false;
		}
		Layer.LayerData.Init(LayerIndex == 0 ? 255 : 0, VerticesPerSide * VerticesPerSide);
	}
	MaterialLayerDataPerLayers.Add(BaseLayerGuid, MoveTemp(SurfaceLayers));

	Landscape->Import(
		FGuid::NewGuid(),
		0,
		0,
		QuadsPerSide,
		QuadsPerSide,
		SectionsPerComponent,
		QuadsPerSection,
		HeightDataPerLayers,
		nullptr,
		MaterialLayerDataPerLayers,
		ELandscapeImportAlphamapType::Additive,
		TArrayView<const FLandscapeLayer>());
	ULandscapeInfo::RecreateLandscapeInfo(World, true);
	Landscape->MarkPackageDirty();

	FActorSpawnParameters LightingSpawnParameters;
	LightingSpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	LightingSpawnParameters.Name = TEXT("CapabilitySun");
	ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(
		FVector::ZeroVector,
		FRotator(-38.0, -32.0, 0.0),
		LightingSpawnParameters);
	if (Sun)
	{
		Sun->SetActorLabel(TEXT("Capability Sun"));
		UDirectionalLightComponent* SunComponent =
			CastChecked<UDirectionalLightComponent>(Sun->GetLightComponent());
		SunComponent->SetMobility(EComponentMobility::Movable);
		SunComponent->SetIntensity(100000.0f);
		SunComponent->SetLightColor(FLinearColor::White);
		SunComponent->SetAtmosphereSunLight(true);
		SunComponent->SetAtmosphereSunLightIndex(0);
	}

	LightingSpawnParameters.Name = TEXT("CapabilitySkyAtmosphere");
	ASkyAtmosphere* SkyAtmosphere = World->SpawnActor<ASkyAtmosphere>(
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		LightingSpawnParameters);
	if (SkyAtmosphere)
	{
		SkyAtmosphere->SetActorLabel(TEXT("Capability Sky Atmosphere"));
	}

	LightingSpawnParameters.Name = TEXT("CapabilityHeightFog");
	AExponentialHeightFog* HeightFog = World->SpawnActor<AExponentialHeightFog>(
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		LightingSpawnParameters);
	if (HeightFog)
	{
		HeightFog->SetActorLabel(TEXT("Capability Height Fog"));
		HeightFog->GetComponent()->SetFogDensity(0.00001f);
		HeightFog->GetComponent()->SetStartDistance(3000.0f);
	}

	LightingSpawnParameters.Name = TEXT("CapabilityVolumetricCloud");
	AVolumetricCloud* VolumetricCloud = World->SpawnActor<AVolumetricCloud>(
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		LightingSpawnParameters);
	if (VolumetricCloud)
	{
		VolumetricCloud->SetActorLabel(TEXT("Capability Volumetric Cloud"));
		if (UVolumetricCloudComponent* CloudComponent =
			VolumetricCloud->FindComponentByClass<UVolumetricCloudComponent>())
		{
			CloudComponent->SetMaterial(LoadObject<UMaterialInterface>(
				nullptr,
				TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst.m_SimpleVolumetricCloud_Inst")));
		}
	}

	LightingSpawnParameters.Name = TEXT("CapabilitySkyFill");
	ASkyLight* SkyFill = World->SpawnActor<ASkyLight>(
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		LightingSpawnParameters);
	if (SkyFill)
	{
		SkyFill->SetActorLabel(TEXT("Capability Sky Fill"));
		SkyFill->GetLightComponent()->SetMobility(EComponentMobility::Movable);
		SkyFill->GetLightComponent()->SetIntensity(1.0f);
		SkyFill->GetLightComponent()->SetRealTimeCaptureEnabled(true);
	}

	LightingSpawnParameters.Name = TEXT("CapabilityPostProcess");
	APostProcessVolume* PostProcess = World->SpawnActor<APostProcessVolume>(
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		LightingSpawnParameters);
	if (PostProcess)
	{
		PostProcess->SetActorLabel(TEXT("Capability Post Process"));
		PostProcess->bUnbound = true;
		PostProcess->Settings.bOverride_AutoExposureBias = true;
		PostProcess->Settings.AutoExposureBias = 0.0f;
	}

	const FString MapFilename = FPackageName::LongPackageNameToFilename(
		MapPackageName,
		FPackageName::GetMapPackageExtension());
	return FEditorFileUtils::SaveLevel(World->PersistentLevel, MapFilename);
}

bool UWorldGenAssetAuthoringLibrary::AuthorCompilerTownMap(
	const FString& SourceMapPackageName,
	const FString& DestinationMapPackageName)
{
	if (!GEditor || !FPackageName::IsValidLongPackageName(SourceMapPackageName) ||
		!FPackageName::IsValidLongPackageName(DestinationMapPackageName))
	{
		return false;
	}
	UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(SourceMapPackageName);
	if (World == nullptr)
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.Name = TEXT("WorldDirectorNavBounds");
	ANavMeshBoundsVolume* NavigationBounds = World->SpawnActor<ANavMeshBoundsVolume>(
		FVector::ZeroVector, FRotator::ZeroRotator, SpawnParameters);
	if (NavigationBounds)
	{
		UCubeBuilder* NavigationBrush = NewObject<UCubeBuilder>();
		NavigationBrush->X = 16000.0f;
		NavigationBrush->Y = 16000.0f;
		NavigationBrush->Z = 2000.0f;
		UActorFactory::CreateBrushForVolumeActor(NavigationBounds, NavigationBrush);
		NavigationBounds->SetActorLabel(TEXT("World Director - Dynamic Town Nav Bounds"));
		NavigationBounds->Tags.Add(TEXT("WorldDirector.NavigationEnvelope"));
	}

	SpawnParameters.Name = TEXT("WorldDirectorFixtureBootstrap");
	AWorldDirectorFixtureBootstrap* Bootstrap = World->SpawnActor<AWorldDirectorFixtureBootstrap>(
		FVector::ZeroVector, FRotator::ZeroRotator, SpawnParameters);
	if (Bootstrap)
	{
		Bootstrap->SetActorLabel(TEXT("World Director - Compiler Fixture Bootstrap"));
		Bootstrap->Tags.Add(TEXT("WorldDirector.CompilerFixture"));
	}

	SpawnParameters.Name = TEXT("WorldDirectorPlayerStart");
	APlayerStart* PlayerStart = World->SpawnActor<APlayerStart>(
		FVector(0.0, -8500.0, 250.0), FRotator(0.0, 90.0, 0.0), SpawnParameters);
	if (PlayerStart)
	{
		PlayerStart->SetActorLabel(TEXT("World Director - Player Start"));
	}

	if (NavigationBounds == nullptr || Bootstrap == nullptr || PlayerStart == nullptr)
	{
		return false;
	}
	const FString MapFilename = FPackageName::LongPackageNameToFilename(
		DestinationMapPackageName,
		FPackageName::GetMapPackageExtension());
	return FEditorFileUtils::SaveLevel(World->PersistentLevel, MapFilename);
}

bool UWorldGenAssetAuthoringLibrary::AuthorResidentLifeStateTree(
	const FString& AssetPackageName)
{
	if (!FPackageName::IsValidLongPackageName(AssetPackageName))
	{
		return false;
	}
	const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPackageName);
	UPackage* Package = CreatePackage(*AssetPackageName);
	if (Package == nullptr)
	{
		return false;
	}
	if (UStateTree* Existing = FindObject<UStateTree>(Package, *AssetName))
	{
		Existing->ClearFlags(RF_Standalone);
		Existing->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
	}
	UStateTreeFactory* Factory = NewObject<UStateTreeFactory>();
	Factory->SetSchemaClass(UWorldDirectorResidentStateTreeSchema::StaticClass());
	UStateTree* StateTree = Cast<UStateTree>(Factory->FactoryCreateNew(
		UStateTree::StaticClass(), Package, *AssetName,
		RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
	if (StateTree == nullptr)
	{
		return false;
	}
	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData);
	if (EditorData == nullptr || EditorData->SubTrees.IsEmpty() || EditorData->SubTrees[0] == nullptr)
	{
		return false;
	}
	EditorData->SubTrees[0]->AddTask<FWorldDirectorResidentLifeTask>();
	FStateTreeCompilerLog CompilerLog;
	if (!UStateTreeEditingSubsystem::CompileStateTree(StateTree, CompilerLog) || !StateTree->IsReadyToRun())
	{
		return false;
	}
	FAssetRegistryModule::AssetCreated(StateTree);
	Package->MarkPackageDirty();
	const FString Filename = FPackageName::LongPackageNameToFilename(
		AssetPackageName, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	return UPackage::SavePackage(Package, StateTree, *Filename, SaveArgs);
}

bool UWorldGenAssetAuthoringLibrary::ConfigureLivingTownMap(
	const FString& MapPackageName,
	const FString& FixtureFilename)
{
	if (!GEditor || !FPackageName::IsValidLongPackageName(MapPackageName))
	{
		return false;
	}
	UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(MapPackageName);
	if (World == nullptr)
	{
		return false;
	}
	AWorldDirectorFixtureBootstrap* Bootstrap = nullptr;
	TActorIterator<AWorldDirectorFixtureBootstrap> It(World);
	if (It)
	{
		Bootstrap = *It;
	}
	if (Bootstrap == nullptr)
	{
		return false;
	}
	Bootstrap->Modify();
	Bootstrap->FixtureFilename = FixtureFilename;
	Bootstrap->SetActorLabel(TEXT("World Director - Living Town Bootstrap"));
	Bootstrap->Tags.AddUnique(TEXT("WorldDirector.LivingTown"));
	Bootstrap->MarkPackageDirty();
	const FString MapFilename = FPackageName::LongPackageNameToFilename(
		MapPackageName, FPackageName::GetMapPackageExtension());
	return FEditorFileUtils::SaveLevel(World->PersistentLevel, MapFilename);
}
