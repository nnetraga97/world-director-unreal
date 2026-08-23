#include "WorldDirectorTownActors.h"
#include "WorldDirectorRuntime.h"

#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/StateTreeComponent.h"
#include "Components/TextRenderComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimationAsset.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/Texture2D.h"
#include "LandscapeProxy.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "GameplayTagContainer.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "SmartObjectComponent.h"
#include "StateTree.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "UnrealClient.h"
#include "WorldDirectorDialogueWidget.h"
#include "WorldDirectorCreateWorldWidget.h"
#include "WorldDirectorGenerationDiagnosticsWidget.h"
#include "WorldDirectorInspectionWidget.h"
#include "WorldDirectorJson.h"
#include "WorldDirectorPhysicalGenerator.h"
#include "WorldDirectorSubsystems.h"
#include "WorldEnvironmentProfile.h"

namespace
{
void RefreshWorldDirectorPlayerInput(UWorld* World)
{
	if (World == nullptr)
	{
		return;
	}
	TActorIterator<AWorldDirectorFixtureBootstrap> It(World);
	if (It)
	{
		It->RefreshPlayerInputMode();
	}
}
}

AWorldDirectorActivityStationActor::AWorldDirectorActivityStationActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	SmartObjectComponent = CreateDefaultSubobject<USmartObjectComponent>(TEXT("SmartObject"));
	SmartObjectComponent->SetupAttachment(GetRootComponent());
}

AWorldDirectorDoorActor::AWorldDirectorDoorActor()
{
	PrimaryActorTick.bCanEverTick = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	DoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Door"));
	DoorMesh->SetupAttachment(Root);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		DoorMesh->SetStaticMesh(Cube.Object);
	}
	DoorMesh->SetRelativeLocation(FVector(0.0, 0.0, 110.0));
	DoorMesh->SetRelativeScale3D(FVector(0.12, 1.6, 2.2));
	DoorMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	SetDoorOpen(true);
}

void AWorldDirectorDoorActor::SetDoorOpen(const bool bOpen)
{
	bIsOpen = bOpen;
	if (DoorMesh != nullptr)
	{
		DoorMesh->SetRelativeRotation(FRotator(0.0, bOpen ? 90.0 : 0.0, 0.0));
		DoorMesh->SetCollisionEnabled(bOpen ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
	}
}

void AWorldDirectorDoorActor::ToggleDoor()
{
	SetDoorOpen(!bIsOpen);
}

AWorldDirectorLocationActor::AWorldDirectorLocationActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	ProjectSign = CreateDefaultSubobject<UTextRenderComponent>(TEXT("ProjectSign"));
	ProjectSign->SetupAttachment(GetRootComponent());
	ProjectSign->SetHorizontalAlignment(EHTA_Center);
	ProjectSign->SetWorldSize(42.0f);
	ProjectSign->SetTextRenderColor(FColor(238, 198, 92));
	ProjectSign->SetHiddenInGame(true);
}

AWorldDirectorResidentActor::AWorldDirectorResidentActor()
{
	PrimaryActorTick.bCanEverTick = false;
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	GetCapsuleComponent()->InitCapsuleSize(42.0, 90.0);
	GetCharacterMovement()->MaxWalkSpeed = 260.0;
	GetMesh()->SetRelativeLocation(FVector(0.0, 0.0, -90.0));
	GetMesh()->SetRelativeRotation(FRotator(0.0, -90.0, 0.0));
	HeadMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Head"));
	LegsMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Legs"));
	FeetMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Feet"));
	StateTreeComponent = CreateDefaultSubobject<UStateTreeComponent>(TEXT("ResidentLifeStateTree"));
	StateTreeComponent->SetStartLogicAutomatically(false);
	for (USkeletalMeshComponent* Part : {HeadMesh.Get(), LegsMesh.Get(), FeetMesh.Get()})
	{
		Part->SetupAttachment(GetMesh());
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetLeaderPoseComponent(GetMesh());
	}
}

void AWorldDirectorResidentActor::BeginPlay()
{
	Super::BeginPlay();
	if (StateTreeComponent != nullptr)
	{
		UStateTree* ResidentLifeTree = LoadObject<UStateTree>(
			nullptr, TEXT("/Game/WorldDirector/AI/ST_ResidentLife.ST_ResidentLife"));
		if (ResidentLifeTree != nullptr && ResidentLifeTree->IsReadyToRun())
		{
			StateTreeComponent->SetStateTree(ResidentLifeTree);
			StateTreeComponent->StartLogic();
		}
		else
		{
			UE_LOG(LogWorldDirector, Error,
				TEXT("WORLD_DIRECTOR_STATETREE_MISSING resident=%s asset=/Game/WorldDirector/AI/ST_ResidentLife"),
				*ResidentId);
		}
	}
}

void AWorldDirectorResidentActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseDialogueMenu();
	if (StateTreeComponent != nullptr && StateTreeComponent->IsRunning())
	{
		StateTreeComponent->StopLogic(TEXT("Resident end play"));
	}
	Super::EndPlay(EndPlayReason);
}

void AWorldDirectorResidentActor::NotifyActorOnClicked(const FKey ButtonPressed)
{
	Super::NotifyActorOnClicked(ButtonPressed);
	OpenDialogueMenu();
}

void AWorldDirectorResidentActor::OpenDialogueMenu()
{
	APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (PlayerController == nullptr)
	{
		return;
	}
	for (TActorIterator<AWorldDirectorResidentActor> It(GetWorld()); It; ++It)
	{
		if (*It != this)
		{
			It->CloseDialogueMenu();
		}
	}
	CloseDialogueMenu();
	ActiveDialogueWidget = CreateWidget<UWorldDirectorDialogueWidget>(
		PlayerController, UWorldDirectorDialogueWidget::StaticClass());
	if (ActiveDialogueWidget != nullptr)
	{
		ActiveDialogueWidget->InitializeForResident(this);
		ActiveDialogueWidget->AddToViewport(50);
		ActiveDialogueWidget->ApplyViewportLayout();
	}
	RefreshWorldDirectorPlayerInput(GetWorld());
}

void AWorldDirectorResidentActor::CloseDialogueMenu()
{
	if (ActiveDialogueWidget != nullptr)
	{
		ActiveDialogueWidget->RemoveFromParent();
		ActiveDialogueWidget = nullptr;
	}
	RefreshWorldDirectorPlayerInput(GetWorld());
}

bool AWorldDirectorResidentActor::TickResidentLife(const float DeltaTime)
{
	if (UWorld* World = GetWorld())
	{
		if (UTownSimulationSubsystem* Simulation = World->GetSubsystem<UTownSimulationSubsystem>())
		{
			return Simulation->TickResidentLife(*this, DeltaTime);
		}
	}
	return false;
}

AWorldDirectorTownActor::AWorldDirectorTownActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	TerrainMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GeneratedTerrain"));
	TerrainMesh->SetupAttachment(GetRootComponent());
	TerrainMesh->bUseAsyncCooking = false;
	TerrainMesh->SetCollisionProfileName(TEXT("BlockAll"));
	TerrainMesh->SetCanEverAffectNavigation(true);
	RouteMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GeneratedRoutes"));
	RouteMesh->SetupAttachment(GetRootComponent());
	RouteMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RouteMesh->SetCanEverAffectNavigation(false);
	WaterMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GeneratedWater"));
	WaterMesh->SetupAttachment(GetRootComponent());
	WaterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WaterMesh->SetCanEverAffectNavigation(false);
}

void AWorldDirectorTownActor::BeginPlay()
{
	Super::BeginPlay();
	if (APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		EnableInput(PlayerController);
		if (InputComponent != nullptr)
		{
			InputComponent->BindKey(EKeys::I, IE_Pressed, this, &AWorldDirectorTownActor::ToggleInspectionMenu);
		}
	}
}

void AWorldDirectorTownActor::ToggleInspectionMenu()
{
	if (ActiveInspectionWidget != nullptr && ActiveInspectionWidget->IsInViewport())
	{
		CloseInspectionMenu();
		return;
	}
	OpenInspectionMenu();
}

void AWorldDirectorTownActor::OpenInspectionMenu()
{
	APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (PlayerController == nullptr)
	{
		return;
	}
	if (ActiveInspectionWidget == nullptr)
	{
		ActiveInspectionWidget = CreateWidget<UWorldDirectorInspectionWidget>(
			PlayerController, UWorldDirectorInspectionWidget::StaticClass());
		if (ActiveInspectionWidget != nullptr)
		{
			ActiveInspectionWidget->InitializeForTown(this);
		}
	}
	if (ActiveInspectionWidget != nullptr && !ActiveInspectionWidget->IsInViewport())
	{
		ActiveInspectionWidget->AddToViewport(80);
		ActiveInspectionWidget->ApplyViewportLayout();
	}
	RefreshWorldDirectorPlayerInput(GetWorld());
}

void AWorldDirectorTownActor::CloseInspectionMenu()
{
	if (ActiveInspectionWidget != nullptr)
	{
		ActiveInspectionWidget->RemoveFromParent();
	}
	RefreshWorldDirectorPlayerInput(GetWorld());
}

void AWorldDirectorTownActor::DestroyCompiledContent()
{
	if (ActiveInspectionWidget != nullptr)
	{
		ActiveInspectionWidget->RemoveFromParent();
		ActiveInspectionWidget = nullptr;
	}
	for (AWorldDirectorResidentActor* Resident : SpawnedResidents)
	{
		if (Resident != nullptr)
		{
			if (Resident->ActiveDialogueWidget != nullptr)
			{
				Resident->ActiveDialogueWidget->RemoveFromParent();
			}
			Resident->Destroy();
		}
	}
	SpawnedResidents.Reset();
	for (AWorldDirectorLocationActor* Location : SpawnedLocations)
	{
		if (Location == nullptr)
		{
			continue;
		}
		for (AWorldDirectorActivityStationActor* Station : Location->ActivityStations)
		{
			if (Station != nullptr)
			{
				Station->Destroy();
			}
		}
		if (Location->DoorActor != nullptr)
		{
			Location->DoorActor->Destroy();
		}
		if (Location->ShellActor != nullptr)
		{
			Location->ShellActor->Destroy();
		}
		if (Location->InteriorActor != nullptr)
		{
			Location->InteriorActor->Destroy();
		}
		Location->Destroy();
	}
	SpawnedLocations.Reset();
	for (ALandscapeProxy* Landscape : HiddenAuthoredLandscapes)
	{
		if (Landscape != nullptr)
		{
			Landscape->SetActorHiddenInGame(false);
			Landscape->SetActorEnableCollision(true);
		}
	}
	HiddenAuthoredLandscapes.Reset();
	RuntimeSmartObjectDefinitions.Reset();
	Destroy();
}

namespace
{
UMaterialInstanceDynamic* MakeSurfaceMaterial(
	AActor* Owner,
	const UWorldEnvironmentProfile* Profile,
	const FSoftObjectPath& BaseColorPath,
	const FSoftObjectPath& NormalPath)
{
	UMaterialInterface* Parent = Profile ? Cast<UMaterialInterface>(Profile->OpaqueMasterMaterial.TryLoad()) : nullptr;
	UTexture2D* BaseColor = Cast<UTexture2D>(BaseColorPath.TryLoad());
	UTexture2D* Normal = Cast<UTexture2D>(NormalPath.TryLoad());
	if (Parent == nullptr || BaseColor == nullptr || Normal == nullptr)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Parent, Owner);
	Material->SetTextureParameterValue(TEXT("Base Color Texture"), BaseColor);
	Material->SetTextureParameterValue(TEXT("Normal Texture"), Normal);
	return Material;
}

void AddRibbonSegment(
	TArray<FVector>& Vertices,
	TArray<int32>& Triangles,
	TArray<FVector>& Normals,
	TArray<FVector2D>& UVs,
	TArray<FLinearColor>& Colors,
	TArray<FProcMeshTangent>& Tangents,
	const FVector& Start,
	const FVector& End,
	const float HalfWidth,
	const float UStart,
	const float UEnd)
{
	const FVector Direction = (End - Start).GetSafeNormal2D();
	const FVector Side(-Direction.Y, Direction.X, 0.0f);
	const int32 Base = Vertices.Num();
	Vertices.Append({Start - Side * HalfWidth, Start + Side * HalfWidth, End - Side * HalfWidth, End + Side * HalfWidth});
	Triangles.Append({Base, Base + 2, Base + 1, Base + 1, Base + 2, Base + 3});
	Normals.Append({FVector::UpVector, FVector::UpVector, FVector::UpVector, FVector::UpVector});
	UVs.Append({FVector2D(UStart, 0.0f), FVector2D(UStart, 1.0f), FVector2D(UEnd, 0.0f), FVector2D(UEnd, 1.0f)});
	Colors.Append({FLinearColor::White, FLinearColor::White, FLinearColor::White, FLinearColor::White});
	Tangents.Append({FProcMeshTangent(Direction, false), FProcMeshTangent(Direction, false), FProcMeshTangent(Direction, false), FProcMeshTangent(Direction, false)});
}
}

bool AWorldDirectorTownActor::BuildTerrainAndSurfaces(
	const FResolvedWorldPlan& Plan,
	FValidationReport& OutReport)
{
	if (TerrainMesh == nullptr || Plan.Terrain.Resolution < 2 ||
		Plan.Terrain.HeightsCentimeters.Num() != Plan.Terrain.Resolution * Plan.Terrain.Resolution ||
		Plan.Terrain.SurfaceTypes.Num() != Plan.Terrain.HeightsCentimeters.Num())
	{
		OutReport.AddError(TEXT("generator.terrain_recipe_invalid"), TEXT("terrain"),
			TEXT("Generated terrain arrays do not match the declared resolution."));
		return false;
	}
	const int32 Resolution = Plan.Terrain.Resolution;
	const UWorldEnvironmentProfile* Profile = UWorldEnvironmentProfile::ResolveStylizedVillage();
	FString ProfileError;
	if (Profile == nullptr || !Profile->Validate(ProfileError))
	{
		OutReport.AddError(TEXT("generator.profile_invalid"), TEXT("environmentProfile"), ProfileError);
		return false;
	}
	const float Step = 2.0f * Plan.Terrain.ExtentCentimeters / (Resolution - 1);
	for (int32 SurfaceIndex = 0; SurfaceIndex < 5; ++SurfaceIndex)
	{
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FLinearColor> Colors;
		TArray<FProcMeshTangent> Tangents;
		for (int32 Y = 0; Y < Resolution - 1; ++Y)
		{
			for (int32 X = 0; X < Resolution - 1; ++X)
			{
				const int32 Sample = Y * Resolution + X;
				const uint8 Surface = Plan.Terrain.SurfaceTypes[Sample];
				// Water is a visual/semantic mask, not permission to remove the world floor.
				// Emit submerged cells into the rock section so collision and nav always have
				// a continuous lakebed/seabed beneath the non-colliding water surface.
				const uint8 CollisionSurface = Surface == static_cast<uint8>(EWorldDirectorSurfaceType::Water)
					? static_cast<uint8>(EWorldDirectorSurfaceType::Rock) : Surface;
				if (CollisionSurface != static_cast<uint8>(SurfaceIndex))
				{
					continue;
				}
				const float X0 = -Plan.Terrain.ExtentCentimeters + X * Step;
				const float Y0 = -Plan.Terrain.ExtentCentimeters + Y * Step;
				const int32 Base = Vertices.Num();
				const FVector V00(X0, Y0, Plan.Terrain.HeightsCentimeters[Sample]);
				const FVector V10(X0 + Step, Y0, Plan.Terrain.HeightsCentimeters[Sample + 1]);
				const FVector V01(X0, Y0 + Step, Plan.Terrain.HeightsCentimeters[Sample + Resolution]);
				const FVector V11(X0 + Step, Y0 + Step, Plan.Terrain.HeightsCentimeters[Sample + Resolution + 1]);
				Vertices.Append({V00, V10, V01, V11});
				Triangles.Append({Base, Base + 2, Base + 1, Base + 1, Base + 2, Base + 3});
				const FVector NormalA = FVector::CrossProduct(V01 - V00, V10 - V00).GetSafeNormal();
				const FVector NormalB = FVector::CrossProduct(V11 - V10, V01 - V10).GetSafeNormal();
				Normals.Append({NormalA, NormalA, NormalB, NormalB});
				const float U = X0 / 500.0f;
				const float V = Y0 / 500.0f;
				UVs.Append({FVector2D(U, V), FVector2D(U + Step / 500.0f, V), FVector2D(U, V + Step / 500.0f), FVector2D(U + Step / 500.0f, V + Step / 500.0f)});
				Colors.Append({FLinearColor::White, FLinearColor::White, FLinearColor::White, FLinearColor::White});
				Tangents.Append({FProcMeshTangent(1, 0, 0), FProcMeshTangent(1, 0, 0), FProcMeshTangent(1, 0, 0), FProcMeshTangent(1, 0, 0)});
			}
		}
		if (!Vertices.IsEmpty())
		{
			TerrainMesh->CreateMeshSection_LinearColor(SurfaceIndex, Vertices, Triangles, Normals, UVs, Colors, Tangents, true);
		}
	}
	UMaterialInstanceDynamic* Grass = MakeSurfaceMaterial(this, Profile, Profile->Surfaces[0].BaseColorTexture, Profile->Surfaces[0].NormalTexture);
	UMaterialInstanceDynamic* Gravel = MakeSurfaceMaterial(this, Profile, Profile->Surfaces[1].BaseColorTexture, Profile->Surfaces[1].NormalTexture);
	UMaterialInstanceDynamic* Paving = MakeSurfaceMaterial(this, Profile, Profile->Surfaces[2].BaseColorTexture, Profile->Surfaces[2].NormalTexture);
	UMaterialInstanceDynamic* Farm = MakeSurfaceMaterial(this, Profile, Profile->Surfaces[3].BaseColorTexture, Profile->Surfaces[3].NormalTexture);
	UMaterialInterface* Rock = Cast<UMaterialInterface>(Profile->RockMaterial.TryLoad());
	for (int32 Index = 0; Index < 5; ++Index)
	{
		UMaterialInterface* Material = Index == 0 ? Grass : Index == 1 ? Gravel : Index == 2 ? Paving : Index == 3 ? Farm : Rock;
		if (Material == nullptr)
		{
			OutReport.AddError(TEXT("generator.surface_material_missing"), FString::Printf(TEXT("terrain.materials[%d]"), Index),
				TEXT("StylizedVillage surface material or texture could not load."));
		}
		else
		{
			TerrainMesh->SetMaterial(Index, Material);
		}
	}
	TerrainMesh->UpdateBounds();
	TerrainMesh->MarkRenderStateDirty();
	UNavigationSystemV1::UpdateComponentInNavOctree(*TerrainMesh);
	if (!OutReport.bValid)
	{
		TerrainMesh->ClearAllMeshSections();
		return false;
	}
	// Commit the terrain replacement only after its recipe and every material
	// have succeeded, so any failure leaves the authored ground usable.
	for (TActorIterator<ALandscapeProxy> It(GetWorld()); It; ++It)
	{
		ALandscapeProxy* Landscape = *It;
		if (Landscape != nullptr && !Landscape->IsHidden())
		{
			HiddenAuthoredLandscapes.Add(Landscape);
			Landscape->SetActorHiddenInGame(true);
			Landscape->SetActorEnableCollision(false);
		}
	}
	return OutReport.bValid;
}

void AWorldDirectorTownActor::BuildRouteSurfaces(const FResolvedWorldPlan& Plan)
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	float U = 0.0f;
	for (const FResolvedRoutePlan& Route : Plan.Routes)
	{
		for (int32 Index = 1; Index < Route.ControlPoints.Num(); ++Index)
		{
			FVector Start = Route.ControlPoints[Index - 1];
			FVector End = Route.ControlPoints[Index];
			Start.Z += 9.0f;
			End.Z += 9.0f;
			const float NextU = U + FVector::Distance(Start, End) / 500.0f;
			AddRibbonSegment(Vertices, Triangles, Normals, UVs, Colors, Tangents, Start, End, Route.WidthCentimeters * 0.5f, U, NextU);
			U = NextU;
		}
	}
	if (!Vertices.IsEmpty())
	{
		RouteMesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
		const UWorldEnvironmentProfile* Profile = UWorldEnvironmentProfile::ResolveStylizedVillage();
		if (Profile != nullptr && Profile->Surfaces.Num() >= 2)
		{
			if (UMaterialInstanceDynamic* Gravel = MakeSurfaceMaterial(
				this, Profile, Profile->Surfaces[1].BaseColorTexture, Profile->Surfaces[1].NormalTexture))
			{
				RouteMesh->SetMaterial(0, Gravel);
			}
		}
	}
	if (Plan.Terrain.WaterLevelCentimeters != INDEX_NONE)
	{
		Vertices.Reset(); Triangles.Reset(); Normals.Reset(); UVs.Reset(); Colors.Reset(); Tangents.Reset();
		U = 0.0f;
		const int32 Resolution = Plan.Terrain.Resolution;
		const float Step = 2.0f * Plan.Terrain.ExtentCentimeters / FMath::Max(1, Resolution - 1);
		const float WaterZ = Plan.Terrain.WaterLevelCentimeters + 18.0f;
		for (int32 Y = 0; Y < Resolution - 1; ++Y)
		{
			for (int32 X = 0; X < Resolution - 1; ++X)
			{
				const int32 Sample = Y * Resolution + X;
				if (!Plan.Terrain.SurfaceTypes.IsValidIndex(Sample) ||
					Plan.Terrain.SurfaceTypes[Sample] != static_cast<uint8>(EWorldDirectorSurfaceType::Water))
				{
					continue;
				}
				const float X0 = -Plan.Terrain.ExtentCentimeters + X * Step;
				const float Y0 = -Plan.Terrain.ExtentCentimeters + Y * Step;
				const int32 Base = Vertices.Num();
				Vertices.Append({FVector(X0, Y0, WaterZ), FVector(X0 + Step, Y0, WaterZ),
					FVector(X0, Y0 + Step, WaterZ), FVector(X0 + Step, Y0 + Step, WaterZ)});
				Triangles.Append({Base, Base + 2, Base + 1, Base + 1, Base + 2, Base + 3});
				Normals.Append({FVector::UpVector, FVector::UpVector, FVector::UpVector, FVector::UpVector});
				UVs.Append({FVector2D(0, 0), FVector2D(1, 0), FVector2D(0, 1), FVector2D(1, 1)});
				Colors.Append({FLinearColor::White, FLinearColor::White, FLinearColor::White, FLinearColor::White});
				Tangents.Append({FProcMeshTangent(1, 0, 0), FProcMeshTangent(1, 0, 0),
					FProcMeshTangent(1, 0, 0), FProcMeshTangent(1, 0, 0)});
			}
		}
		// The classified water cells are the authoritative shoreline. The ribbon is
		// only a fallback for an older recipe without a classified water surface;
		// drawing both would overlap coplanar triangles and shimmer.
		const float HalfWidth = Plan.Terrain.Archetype == EWorldDirectorTerrainArchetype::Coast ? 6200.0f : 520.0f;
		for (int32 Index = Vertices.IsEmpty() ? 1 : Plan.Terrain.WaterControlPoints.Num();
			Index < Plan.Terrain.WaterControlPoints.Num(); ++Index)
		{
			const FVector Start = Plan.Terrain.WaterControlPoints[Index - 1] + FVector(0, 0, 18);
			const FVector End = Plan.Terrain.WaterControlPoints[Index] + FVector(0, 0, 18);
			const float NextU = U + FVector::Distance(Start, End) / 800.0f;
			AddRibbonSegment(Vertices, Triangles, Normals, UVs, Colors, Tangents, Start, End, HalfWidth, U, NextU);
			U = NextU;
		}
		if (!Vertices.IsEmpty())
		{
			WaterMesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
		}
		const UWorldEnvironmentProfile* Profile = UWorldEnvironmentProfile::ResolveStylizedVillage();
		if (UMaterialInterface* Water = Profile ? Cast<UMaterialInterface>(Profile->WaterMaterial.TryLoad()) : nullptr)
		{
			WaterMesh->SetMaterial(0, Water);
		}
	}
}

void AWorldDirectorTownActor::BuildDressing(
	const FResolvedWorldPlan& Plan,
	FValidationReport& OutReport)
{
	for (int32 Index = 0; Index < Plan.Dressing.Num(); ++Index)
	{
		const FWorldDirectorDressingInstance& Instance = Plan.Dressing[Index];
		UStaticMesh* Mesh = Cast<UStaticMesh>(Instance.MeshAsset.TryLoad());
		if (Mesh == nullptr)
		{
			OutReport.AddError(TEXT("generator.dressing_asset_missing"), FString::Printf(TEXT("dressing[%d]"), Index), Instance.MeshAsset.ToString());
			continue;
		}
		TObjectPtr<UInstancedStaticMeshComponent>& ComponentPtr = DressingInstanceComponents.FindOrAdd(Mesh);
		if (ComponentPtr == nullptr)
		{
			ComponentPtr = NewObject<UInstancedStaticMeshComponent>(this);
			UInstancedStaticMeshComponent* Component = ComponentPtr.Get();
			Component->SetStaticMesh(Mesh);
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->SetCanEverAffectNavigation(false);
			Component->SetupAttachment(GetRootComponent());
			AddInstanceComponent(Component);
			Component->RegisterComponent();
		}
		UInstancedStaticMeshComponent* Component = ComponentPtr.Get();
		Component->AddInstance(Instance.Transform, true);
	}
}

UInstancedStaticMeshComponent* AWorldDirectorTownActor::FindOrCreateInstanceComponent(
	UStaticMeshComponent* Source)
{
	if (Source == nullptr || Source->GetStaticMesh() == nullptr)
	{
		return nullptr;
	}
	if (TObjectPtr<UInstancedStaticMeshComponent>* Existing = WorldDirectorInstanceComponents.Find(Source->GetStaticMesh()))
	{
		return Existing->Get();
	}
	UInstancedStaticMeshComponent* Instances = NewObject<UInstancedStaticMeshComponent>(this);
	Instances->SetStaticMesh(Source->GetStaticMesh());
	for (int32 MaterialIndex = 0; MaterialIndex < Source->GetNumMaterials(); ++MaterialIndex)
	{
		Instances->SetMaterial(MaterialIndex, Source->GetMaterial(MaterialIndex));
	}
	Instances->SetCollisionProfileName(Source->GetCollisionProfileName());
	Instances->SetupAttachment(GetRootComponent());
	AddInstanceComponent(Instances);
	Instances->RegisterComponent();
	WorldDirectorInstanceComponents.Add(Source->GetStaticMesh(), Instances);
	return Instances;
}

void AWorldDirectorTownActor::CollapseActorToInstances(AActor* SourceActor)
{
	if (SourceActor == nullptr)
	{
		return;
	}
	TArray<UStaticMeshComponent*> Components;
	SourceActor->GetComponents(Components);
	for (UStaticMeshComponent* Component : Components)
	{
		if (Component == nullptr || !Component->IsVisible() || Component->bHiddenInGame
			|| Component->GetName().Contains(TEXT("door"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		if (UInstancedStaticMeshComponent* Instances = FindOrCreateInstanceComponent(Component))
		{
			Instances->AddInstance(Component->GetComponentTransform(), true);
		}
	}
	SourceActor->Destroy();
}

bool AWorldDirectorTownActor::BuildFromPlan(
	const FResolvedWorldPlan& Plan,
	FValidationReport& OutReport)
{
	OutReport = FValidationReport();
	SourceSpecId = Plan.SourceSpecId;
	LandmarkLocationId = Plan.LandmarkLocationId;
	WorldFingerprint = Plan.WorldFingerprint;
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		OutReport.AddError(TEXT("compiler.world_missing"), TEXT("world"), TEXT("Town root is not in a UWorld."));
		return false;
	}
	if (!BuildTerrainAndSurfaces(Plan, OutReport))
	{
		return false;
	}
	BuildRouteSurfaces(Plan);
	BuildDressing(Plan, OutReport);
	for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
	{
		ANavMeshBoundsVolume* Bounds = *It;
		if (Bounds != nullptr)
		{
			const FBox CurrentBounds = Bounds->GetComponentsBoundingBox(true);
			const FVector CurrentSize = CurrentBounds.GetSize();
			const float DesiredXY = Plan.Terrain.ExtentCentimeters * 2.0f + 2400.0f;
			const float DesiredZ = FMath::Max(3200.0f,
				static_cast<float>(Plan.Terrain.MaximumHeightCentimeters - Plan.Terrain.MinimumHeightCentimeters) + 2200.0f);
			const FVector Scale = Bounds->GetActorScale3D();
			Bounds->SetActorScale3D(Scale * FVector(
				CurrentSize.X > KINDA_SMALL_NUMBER ? FMath::Max(1.0f, DesiredXY / CurrentSize.X) : 1.0f,
				CurrentSize.Y > KINDA_SMALL_NUMBER ? FMath::Max(1.0f, DesiredXY / CurrentSize.Y) : 1.0f,
				CurrentSize.Z > KINDA_SMALL_NUMBER ? FMath::Max(1.0f, DesiredZ / CurrentSize.Z) : 1.0f));
			if (UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
			{
				Navigation->OnNavigationBoundsUpdated(Bounds);
			}
		}
	}

	for (int32 Index = 0; Index < Plan.Locations.Num(); ++Index)
	{
		const FResolvedLocationPlan& Location = Plan.Locations[Index];
		UClass* ShellClass = Cast<UClass>(Location.ShellAsset.TryLoad());
		UClass* InteriorClass = Cast<UClass>(Location.InteriorAsset.TryLoad());
		if (ShellClass == nullptr || InteriorClass == nullptr)
		{
			OutReport.AddError(
				TEXT("compiler.asset_missing"), FString::Printf(TEXT("locations[%d]"), Index),
				TEXT("Resolved shell or interior class is unavailable at spawn time."));
			continue;
		}

		AWorldDirectorLocationActor* LocationActor = World->SpawnActor<AWorldDirectorLocationActor>(
			AWorldDirectorLocationActor::StaticClass(), Location.Transform);
		if (LocationActor == nullptr)
		{
			OutReport.AddError(TEXT("compiler.spawn_failed"), FString::Printf(TEXT("locations[%d]"), Index), TEXT("Could not spawn location root."));
			continue;
		}
		LocationActor->LocationId = Location.LocationId;
		LocationActor->NavigationEntranceLocation = Location.EntranceTransform.GetLocation();
		LocationActor->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
		LocationActor->ShellActor = World->SpawnActor<AActor>(ShellClass, Location.Transform);
		LocationActor->InteriorActor = World->SpawnActor<AActor>(InteriorClass, Location.Transform);
		if (LocationActor->ShellActor == nullptr || LocationActor->InteriorActor == nullptr)
		{
			OutReport.AddError(TEXT("compiler.spawn_failed"), FString::Printf(TEXT("locations[%d]"), Index), TEXT("Could not spawn the resolved shell and interior pair."));
			continue;
		}

		TArray<UStaticMeshComponent*> ShellComponents;
		LocationActor->ShellActor->GetComponents(ShellComponents);
		UStaticMeshComponent* VendorDoorComponent = nullptr;
		for (UStaticMeshComponent* Component : ShellComponents)
		{
			if (Component != nullptr && Component->GetName().Contains(TEXT("door"), ESearchCase::IgnoreCase))
			{
				VendorDoorComponent = Component;
				Component->SetVisibility(false, true);
				Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
		}
		TArray<UStaticMeshComponent*> InteriorComponents;
		LocationActor->InteriorActor->GetComponents(InteriorComponents);
		for (UStaticMeshComponent* Component : InteriorComponents)
		{
			if (Component != nullptr && Component->GetName().Contains(TEXT("Wall"), ESearchCase::IgnoreCase))
			{
				Component->SetVisibility(false, true);
				Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
		}

		LocationActor->DoorActor = World->SpawnActor<AWorldDirectorDoorActor>(
			AWorldDirectorDoorActor::StaticClass(),
			Location.EntranceTransform);
		if (LocationActor->DoorActor != nullptr)
		{
			if (VendorDoorComponent != nullptr && VendorDoorComponent->GetStaticMesh() != nullptr)
			{
				LocationActor->DoorActor->DoorMesh->SetStaticMesh(VendorDoorComponent->GetStaticMesh());
				LocationActor->DoorActor->DoorMesh->SetRelativeLocation(FVector::ZeroVector);
				LocationActor->DoorActor->DoorMesh->SetRelativeScale3D(FVector::OneVector);
				for (int32 MaterialIndex = 0; MaterialIndex < VendorDoorComponent->GetNumMaterials(); ++MaterialIndex)
				{
					LocationActor->DoorActor->DoorMesh->SetMaterial(MaterialIndex, VendorDoorComponent->GetMaterial(MaterialIndex));
				}
				LocationActor->DoorActor->SetDoorOpen(true);
			}
			LocationActor->DoorActor->AttachToActor(LocationActor, FAttachmentTransformRules::KeepWorldTransform);
		}
		if (!Location.bRepurposable)
		{
			CollapseActorToInstances(LocationActor->ShellActor);
			CollapseActorToInstances(LocationActor->InteriorActor);
			LocationActor->ShellActor = nullptr;
			LocationActor->InteriorActor = nullptr;
		}
		SpawnedLocations.Add(LocationActor);
	}

	for (int32 Index = 0; Index < Plan.Residents.Num(); ++Index)
	{
		const FResolvedResidentPlan& Resident = Plan.Residents[Index];
		USkeletalMesh* Mesh = Cast<USkeletalMesh>(Resident.SkeletalMeshAsset.TryLoad());
		if (Mesh == nullptr || Resident.ModularPartAssets.Num() != 3)
		{
			OutReport.AddError(TEXT("compiler.asset_missing"), FString::Printf(TEXT("residents[%d]"), Index), TEXT("Resolved resident mesh is unavailable at spawn time."));
			continue;
		}
		FActorSpawnParameters ResidentSpawnParameters;
		ResidentSpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		AWorldDirectorResidentActor* ResidentActor = World->SpawnActor<AWorldDirectorResidentActor>(
			AWorldDirectorResidentActor::StaticClass(), Resident.SpawnTransform, ResidentSpawnParameters);
		if (ResidentActor == nullptr)
		{
			OutReport.AddError(TEXT("compiler.spawn_failed"), FString::Printf(TEXT("residents[%d]"), Index), TEXT("Could not spawn resident."));
			continue;
		}
		ResidentActor->ResidentId = Resident.ResidentId;
		ResidentActor->HomeLocationId = Resident.HomeLocationId;
		ResidentActor->WorkplaceLocationId = Resident.WorkplaceLocationId;
		ResidentActor->Schedule = Resident.Schedule;
		ResidentActor->GetMesh()->SetSkeletalMeshAsset(Mesh);
		USkeletalMeshComponent* PartComponents[] = {
			ResidentActor->HeadMesh, ResidentActor->LegsMesh, ResidentActor->FeetMesh
		};
		for (int32 PartIndex = 0; PartIndex < 3; ++PartIndex)
		{
			PartComponents[PartIndex]->SetSkeletalMeshAsset(
				Cast<USkeletalMesh>(Resident.ModularPartAssets[PartIndex].TryLoad()));
		}
		if (UAnimationAsset* IdleAnimation = Cast<UAnimationAsset>(Resident.IdleAnimationAsset.TryLoad()))
		{
			ResidentActor->GetMesh()->PlayAnimation(IdleAnimation, true);
		}
		SpawnedResidents.Add(ResidentActor);
	}
	if (APlayerController* PlayerController = World->GetFirstPlayerController())
	{
		if (APawn* PlayerPawn = PlayerController->GetPawn())
		{
			const FResolvedLocationPlan* ArrivalLocation = Plan.Locations.FindByPredicate(
				[&Plan](const FResolvedLocationPlan& Location)
				{ return Location.LocationId == Plan.LandmarkLocationId; });
			if (ArrivalLocation == nullptr && !Plan.Locations.IsEmpty())
			{
				ArrivalLocation = &Plan.Locations[0];
			}
			if (ArrivalLocation != nullptr)
			{
				FVector BoundsOrigin;
				FVector BoundsExtent;
				PlayerPawn->GetActorBounds(false, BoundsOrigin, BoundsExtent);
				FVector Arrival = ArrivalLocation->EntranceTransform.GetLocation();
				Arrival.Z += FMath::Max(BoundsExtent.Z + 20.0f, 100.0f);
				PlayerPawn->SetActorLocation(Arrival, false, nullptr, ETeleportType::TeleportPhysics);
			}
		}
	}

	OutReport.bValid = !OutReport.Issues.ContainsByPredicate(
		[](const FValidationIssue& Issue)
		{
			return Issue.Severity == EWorldDirectorValidationSeverity::Error;
		});
	return OutReport.bValid;
}

bool AWorldDirectorTownActor::BuildActivityStations(
	const FGeneratedWorldSpec& Spec,
	FValidationReport& OutReport)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		OutReport.AddError(TEXT("simulation.world_missing"), TEXT("activityStations"), TEXT("Town has no world."));
		return false;
	}

	TMap<FName, USmartObjectDefinition*> Definitions;
	auto GetDefinition = [this, &Definitions, &OutReport](const FName ActivityTag)
		-> USmartObjectDefinition*
	{
		if (USmartObjectDefinition** Existing = Definitions.Find(ActivityTag))
		{
			return *Existing;
		}
		const FGameplayTag GameplayActivity = FGameplayTag::RequestGameplayTag(ActivityTag, false);
		if (!GameplayActivity.IsValid())
		{
			OutReport.AddError(TEXT("simulation.activity_tag_missing"), ActivityTag.ToString(),
				TEXT("The activity Gameplay Tag is not registered."));
			return nullptr;
		}
		USmartObjectDefinition* Definition = NewObject<USmartObjectDefinition>(this);
		FGameplayTagContainer Tags;
		Tags.AddTag(GameplayActivity);
		Definition->SetActivityTags(Tags);
		FSmartObjectSlotDefinition& Slot = Definition->DebugAddSlot();
		Slot.ActivityTags = Tags;
		Slot.BehaviorDefinitions.Add(
			NewObject<UWorldDirectorSmartObjectBehaviorDefinition>(Definition));
		if (!Definition->Validate())
		{
			OutReport.AddError(TEXT("simulation.smart_object_definition_invalid"), ActivityTag.ToString(),
				TEXT("Generated Smart Object definition failed validation."));
			return nullptr;
		}
		RuntimeSmartObjectDefinitions.Add(Definition);
		Definitions.Add(ActivityTag, Definition);
		return Definition;
	};

	auto SpawnStations = [this, World, &GetDefinition, &OutReport](
		AWorldDirectorLocationActor& Location,
		const FName ActivityTag,
		const int32 Count)
	{
		USmartObjectDefinition* Definition = GetDefinition(ActivityTag);
		if (Definition == nullptr)
		{
			return;
		}
		// Certified station anchors stay on the connected exterior threshold for this
		// vertical slice. Vendor interiors do not expose stable authored socket names,
		// so arbitrary visual-center placement can create isolated nav islands.
		const FVector Entrance = Location.NavigationEntranceLocation;
		const FVector Outward = (Entrance - Location.GetActorLocation()).GetSafeNormal2D();
		const FVector Lateral(-Outward.Y, Outward.X, 0.0f);
		const bool bMeeting = ActivityTag == TEXT("Activity.Meet");
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const int32 Column = Index % 2;
			const int32 Row = Index / 2;
			const float Side = Column == 0 ? -1.0f : 1.0f;
			const float Depth = 70.0f + Row * (bMeeting ? 70.0f : 35.0f);
			const FVector StationLocation = Entrance + Outward * Depth + Lateral * Side * 55.0f + FVector(0.0f, 0.0f, 10.0f);
			const FTransform Transform((-Outward).Rotation(), StationLocation);
			AWorldDirectorActivityStationActor* Station = World->SpawnActorDeferred<AWorldDirectorActivityStationActor>(
				AWorldDirectorActivityStationActor::StaticClass(), Transform, this, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (Station == nullptr)
			{
				OutReport.AddError(TEXT("simulation.station_spawn_failed"), Location.LocationId,
					TEXT("Could not spawn an activity station."));
				continue;
			}
			Station->LocationId = Location.LocationId;
			Station->ActivityTag = ActivityTag;
			Station->SmartObjectComponent->SetDefinition(Definition);
			Station->FinishSpawning(Transform);
			Station->AttachToActor(&Location, FAttachmentTransformRules::KeepWorldTransform);
			Location.ActivityStations.Add(Station);
		}
	};

	for (const FWorldLocation& LocationSpec : Spec.Locations)
	{
		const TObjectPtr<AWorldDirectorLocationActor>* LocationPtr = SpawnedLocations.FindByPredicate(
			[&LocationSpec](const AWorldDirectorLocationActor* Candidate)
			{
				return Candidate != nullptr && Candidate->LocationId == LocationSpec.Id;
			});
		AWorldDirectorLocationActor* Location = LocationPtr ? LocationPtr->Get() : nullptr;
		if (Location == nullptr)
		{
			OutReport.AddError(TEXT("simulation.location_missing"), LocationSpec.Id,
				TEXT("Compiled location is unavailable for station placement."));
			continue;
		}
		Location->CurrentPurposeTag = LocationSpec.PurposeTag;
		Location->OwnerResidentId = LocationSpec.OwnerResidentId;
		Location->ControllerResidentId = LocationSpec.ControllerResidentId;
		Location->AccessPolicy = LocationSpec.AccessPolicy;
		if (LocationSpec.PurposeTag == TEXT("Purpose.Home"))
		{
			SpawnStations(*Location, TEXT("Activity.Sleep"), FMath::Max(1, LocationSpec.ResidentCapacity));
			SpawnStations(*Location, TEXT("Activity.Sit"), 2);
		}
		if (LocationSpec.PurposeTag == TEXT("Purpose.Workplace") ||
			LocationSpec.PurposeTag == TEXT("Purpose.Landmark") ||
			LocationSpec.PurposeTag == TEXT("Purpose.Clinic") ||
			LocationSpec.PurposeTag == TEXT("Purpose.Headquarters"))
		{
			SpawnStations(*Location, TEXT("Activity.Work"), FMath::Max(1, LocationSpec.ResidentCapacity));
			SpawnStations(*Location, TEXT("Activity.Counter"), 2);
		}
		if (LocationSpec.PurposeTag == TEXT("Purpose.Shelter") ||
			LocationSpec.PurposeTag == TEXT("Purpose.Clinic"))
		{
			SpawnStations(*Location, TEXT("Activity.Sleep"), FMath::Max(1, LocationSpec.ResidentCapacity));
			SpawnStations(*Location, TEXT("Activity.Sit"), 2);
		}
		if (Spec.Topology.LandmarkLocationIds.Contains(LocationSpec.Id))
		{
			SpawnStations(*Location, TEXT("Activity.Meet"), 12);
		}
	}

	OutReport.bValid = !OutReport.Issues.ContainsByPredicate(
		[](const FValidationIssue& Issue)
		{
			return Issue.Severity == EWorldDirectorValidationSeverity::Error;
		});
	return OutReport.bValid;
}

bool AWorldDirectorTownActor::ApplyLocationRepurpose(
	const FWorldLocation& UpdatedLocation,
	const FChangeProject& Project,
	FValidationReport& OutReport)
{
	AWorldDirectorLocationActor* Location = nullptr;
	if (const TObjectPtr<AWorldDirectorLocationActor>* Match = SpawnedLocations.FindByPredicate(
		[&UpdatedLocation](const AWorldDirectorLocationActor* Candidate)
		{
			return Candidate != nullptr && Candidate->LocationId == UpdatedLocation.Id;
		}))
	{
		Location = Match->Get();
	}
	if (Location == nullptr || Location->InteriorActor == nullptr)
	{
		OutReport.AddError(TEXT("project.location_not_live"), UpdatedLocation.Id,
			TEXT("Repurpose target is not a live, individually dressed location."));
		return false;
	}

	const TCHAR* InteriorPath = UpdatedLocation.PurposeTag == TEXT("Purpose.Clinic") ||
		UpdatedLocation.PurposeTag == TEXT("Purpose.Shelter")
		? TEXT("/Game/CapabilityPack/Interiors/BP_Interior_Home_Small_01.BP_Interior_Home_Small_01_C")
		: TEXT("/Game/CapabilityPack/Interiors/BP_Interior_Workplace_Longhouse_01.BP_Interior_Workplace_Longhouse_01_C");
	UClass* InteriorClass = Cast<UClass>(FSoftObjectPath(InteriorPath).TryLoad());
	if (InteriorClass == nullptr)
	{
		OutReport.AddError(TEXT("project.dressing_missing"), UpdatedLocation.Id,
			TEXT("The certified transition interior could not be loaded."));
		return false;
	}
	const FTransform LocationTransform = Location->GetActorTransform();
	Location->InteriorActor->Destroy();
	Location->InteriorActor = GetWorld()->SpawnActor<AActor>(InteriorClass, LocationTransform);
	if (Location->InteriorActor == nullptr)
	{
		OutReport.AddError(TEXT("project.dressing_spawn_failed"), UpdatedLocation.Id,
			TEXT("The certified transition interior could not be spawned."));
		return false;
	}
	Location->InteriorActor->AttachToActor(Location, FAttachmentTransformRules::KeepWorldTransform);
	TArray<UStaticMeshComponent*> InteriorComponents;
	Location->InteriorActor->GetComponents(InteriorComponents);
	for (UStaticMeshComponent* Component : InteriorComponents)
	{
		if (Component != nullptr && Component->GetName().Contains(TEXT("Wall"), ESearchCase::IgnoreCase))
		{
			Component->SetVisibility(false, true);
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}

	for (AWorldDirectorActivityStationActor* Station : Location->ActivityStations)
	{
		if (Station != nullptr)
		{
			Station->Destroy();
		}
	}
	Location->ActivityStations.Reset();
	FGeneratedWorldSpec LocationSpec;
	LocationSpec.Locations.Add(UpdatedLocation);
	if (UpdatedLocation.PurposeTag == TEXT("Purpose.Headquarters"))
	{
		LocationSpec.Topology.LandmarkLocationIds.Add(UpdatedLocation.Id);
	}
	if (!BuildActivityStations(LocationSpec, OutReport))
	{
		return false;
	}

	Location->CurrentPurposeTag = UpdatedLocation.PurposeTag;
	Location->OwnerResidentId = UpdatedLocation.OwnerResidentId;
	Location->ControllerResidentId = UpdatedLocation.ControllerResidentId;
	Location->AccessPolicy = UpdatedLocation.AccessPolicy;
	Location->DressingRevision++;
	Location->ProjectSign->SetText(FText::FromString(
		UpdatedLocation.PurposeTag == TEXT("Purpose.Clinic") ? TEXT("TEMPORARY CLINIC") :
		UpdatedLocation.PurposeTag == TEXT("Purpose.Shelter") ? TEXT("PUBLIC SHELTER") :
		UpdatedLocation.PurposeTag == TEXT("Purpose.Headquarters") ? TEXT("REPAIR HEADQUARTERS") :
		TEXT("ACTIVE WORKSHOP")));
	Location->ProjectSign->SetWorldLocation(
		Location->NavigationEntranceLocation + FVector(0.0f, 0.0f, 260.0f));
	Location->ProjectSign->SetWorldRotation(
		(Location->GetActorLocation() - Location->NavigationEntranceLocation).Rotation());
	Location->ProjectSign->SetHiddenInGame(false);
	UE_LOG(LogWorldDirector, Display,
		TEXT("WORLD_DIRECTOR_PROJECT_VISUAL project=%s location=%s purpose=%s dressingRevision=%d stations=%d sign=VISIBLE"),
		*Project.Id, *UpdatedLocation.Id, *UpdatedLocation.PurposeTag.ToString(),
		Location->DressingRevision, Location->ActivityStations.Num());
	return true;
}

FValidationReport AWorldDirectorTownActor::ValidateNavigationViability() const
{
	FValidationReport Report;
	const UWorld* World = GetWorld();
	const UNavigationSystemV1* Navigation = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (Navigation == nullptr)
	{
		Report.AddError(TEXT("navigation.system_missing"), TEXT("navigation"), TEXT("No navigation system is available for the generated town."));
		return Report;
	}

	TMap<FString, FVector> Entrances;
	for (const AWorldDirectorLocationActor* Location : SpawnedLocations)
	{
		if (Location != nullptr && Location->DoorActor != nullptr)
		{
			FNavLocation ProjectedEntrance;
			if (Navigation->ProjectPointToNavigation(
				Location->NavigationEntranceLocation, ProjectedEntrance, FVector(250.0, 250.0, 500.0)))
			{
				Entrances.Add(Location->LocationId, ProjectedEntrance.Location);
			}
			else
			{
				Report.AddError(
					TEXT("navigation.door_inaccessible"), Location->LocationId,
					TEXT("The location entrance does not project onto the generated navmesh."));
			}

			// The semantic entrance keeps resident routing stable, but the vendor mesh may place its
			// actual door away from that point. Prove that at least one short axis through the real
			// door is traversable so a visually plausible shell cannot mask an inaccessible entrance.
			const FVector DoorLocation = Location->DoorActor->GetActorLocation();
			const FVector DoorAxes[] = {
				Location->DoorActor->GetActorForwardVector(),
				Location->DoorActor->GetActorRightVector()
			};
			bool bActualDoorTraversable = false;
			for (int32 DoorAxisIndex = 0; DoorAxisIndex < UE_ARRAY_COUNT(DoorAxes); ++DoorAxisIndex)
			{
				const FVector& DoorAxis = DoorAxes[DoorAxisIndex];
				FNavLocation SideA;
				FNavLocation SideB;
				const bool bSideAProjected = Navigation->ProjectPointToNavigation(
					DoorLocation - DoorAxis * 250.0, SideA, FVector(150.0, 150.0, 350.0));
				const bool bSideBProjected = Navigation->ProjectPointToNavigation(
					DoorLocation + DoorAxis * 250.0, SideB, FVector(150.0, 150.0, 350.0));
				const double ProjectedSeparation = bSideAProjected && bSideBProjected
					? FVector::Dist2D(SideA.Location, SideB.Location)
					: 0.0;
				if (!bSideAProjected || !bSideBProjected || ProjectedSeparation < 250.0)
				{
					UE_LOG(LogWorldDirector, Display,
						TEXT("WORLD_DIRECTOR_DOOR_PROBE location=%s axis=%d projectedA=%d projectedB=%d separation=%.1f result=REJECT"),
						*Location->LocationId, DoorAxisIndex, bSideAProjected, bSideBProjected, ProjectedSeparation);
					continue;
				}
				UNavigationPath* DoorPath = Navigation->FindPathToLocationSynchronously(
					const_cast<UWorld*>(World), SideA.Location, SideB.Location);
				if (DoorPath == nullptr || !DoorPath->IsValid() || DoorPath->IsPartial())
				{
					UE_LOG(LogWorldDirector, Display,
						TEXT("WORLD_DIRECTOR_DOOR_PROBE location=%s axis=%d separation=%.1f result=NO_PATH"),
						*Location->LocationId, DoorAxisIndex, ProjectedSeparation);
					continue;
				}
				double PathLength = 0.0;
				for (int32 PointIndex = 1; PointIndex < DoorPath->PathPoints.Num(); ++PointIndex)
				{
					PathLength += FVector::Dist2D(
						DoorPath->PathPoints[PointIndex - 1], DoorPath->PathPoints[PointIndex]);
				}
				if (PathLength <= 1200.0)
				{
					UE_LOG(LogWorldDirector, Display,
						TEXT("WORLD_DIRECTOR_DOOR_PROBE location=%s axis=%d separation=%.1f pathLength=%.1f result=PASS"),
						*Location->LocationId, DoorAxisIndex, ProjectedSeparation, PathLength);
					bActualDoorTraversable = true;
					break;
				}
				UE_LOG(LogWorldDirector, Display,
					TEXT("WORLD_DIRECTOR_DOOR_PROBE location=%s axis=%d separation=%.1f pathLength=%.1f result=TOO_LONG"),
					*Location->LocationId, DoorAxisIndex, ProjectedSeparation, PathLength);
			}
			if (!bActualDoorTraversable)
			{
				Report.AddError(
					TEXT("navigation.vendor_door_inaccessible"), Location->LocationId,
					TEXT("The actual vendor-door opening does not have a short traversable path through it."));
			}
		}
	}
	const FVector* Landmark = Entrances.Find(LandmarkLocationId);
	if (Landmark == nullptr)
	{
		Report.AddError(TEXT("navigation.landmark_missing"), TEXT("landmarkLocationId"), TEXT("The landmark entrance was not spawned."));
		return Report;
	}

	for (int32 Index = 0; Index < SpawnedResidents.Num(); ++Index)
	{
		const AWorldDirectorResidentActor* Resident = SpawnedResidents[Index];
		const FVector* Home = Resident ? Entrances.Find(Resident->HomeLocationId) : nullptr;
		const FVector* Work = Resident ? Entrances.Find(Resident->WorkplaceLocationId) : nullptr;
		if (Resident == nullptr || Home == nullptr || Work == nullptr)
		{
			Report.AddError(TEXT("navigation.endpoint_missing"), FString::Printf(TEXT("residents[%d]"), Index), TEXT("Home, workplace, or resident navigation endpoint is missing."));
			continue;
		}
		FNavLocation ProjectedResident;
		if (!Navigation->ProjectPointToNavigation(
			Resident->GetActorLocation(), ProjectedResident, FVector(300.0, 300.0, 500.0)))
		{
			Report.AddError(
				TEXT("navigation.resident_stranded"), FString::Printf(TEXT("residents[%d].spawn"), Index),
				FString::Printf(TEXT("Resident %s does not spawn on accessible navigation."), *Resident->ResidentId));
			continue;
		}
		const FVector Legs[][2] = {
			{ProjectedResident.Location, *Home},
			{*Home, *Work},
			{*Work, *Landmark}
		};
		for (int32 LegIndex = 0; LegIndex < 3; ++LegIndex)
		{
			if (FVector::DistSquared2D(Legs[LegIndex][0], Legs[LegIndex][1]) < FMath::Square(50.0))
			{
				continue;
			}
			UNavigationPath* Path = Navigation->FindPathToLocationSynchronously(
				const_cast<UWorld*>(World), Legs[LegIndex][0], Legs[LegIndex][1], const_cast<AWorldDirectorResidentActor*>(Resident));
			if (Path == nullptr || !Path->IsValid() || Path->IsPartial())
			{
				Report.AddError(
					TEXT("navigation.resident_stranded"),
					FString::Printf(TEXT("residents[%d].path[%d]"), Index, LegIndex),
					FString::Printf(TEXT("Resident %s cannot traverse required route leg %d."), *Resident->ResidentId, LegIndex));
			}
		}
	}
	Report.bValid = !Report.Issues.ContainsByPredicate(
		[](const FValidationIssue& Issue)
		{
			return Issue.Severity == EWorldDirectorValidationSeverity::Error;
		});
	return Report;
}

AWorldDirectorFixtureBootstrap::AWorldDirectorFixtureBootstrap()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AWorldDirectorFixtureBootstrap::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(CreationMenuRetryHandle);
	if (UDirectorBridgeSubsystem* Bridge = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UDirectorBridgeSubsystem>() : nullptr)
	{
		Bridge->OnGenerationFinished.RemoveDynamic(
			this, &AWorldDirectorFixtureBootstrap::HandleAutomatedGenerationFinished);
		Bridge->OnGenerationFinished.RemoveDynamic(
			this, &AWorldDirectorFixtureBootstrap::HandlePlayerGenerationFinished);
	}
	if (CreateWorldWidget != nullptr)
	{
		CreateWorldWidget->RemoveFromParent();
	}
	if (GenerationDiagnosticsWidget != nullptr)
	{
		GenerationDiagnosticsWidget->RemoveFromParent();
	}
	Super::EndPlay(EndPlayReason);
}

void AWorldDirectorFixtureBootstrap::BeginPlay()
{
	Super::BeginPlay();
	if (APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		EnableInput(PlayerController);
		if (InputComponent != nullptr)
		{
			InputComponent->BindKey(EKeys::N, IE_Pressed, this, &AWorldDirectorFixtureBootstrap::ToggleWorldCreationMenu);
			InputComponent->BindKey(EKeys::F8, IE_Pressed, this, &AWorldDirectorFixtureBootstrap::ToggleGenerationDiagnostics);
			InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &AWorldDirectorFixtureBootstrap::ToggleInteractionCursor);
		}
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("WorldDirectorReplayGenerationAutoTest")))
	{
		if (!CompileFixture())
		{
			UE_LOG(LogWorldDirector, Error, TEXT("WORLD_DIRECTOR_GENERATION_REPLAY_RESULT=FAIL reason=compile_failed"));
			FGenericPlatformMisc::RequestExitWithStatus(true, 1);
			return;
		}
		if (const UWorldStateSubsystem* State = GetWorld()
			? GetWorld()->GetSubsystem<UWorldStateSubsystem>() : nullptr;
			State != nullptr && State->HasActiveWorldSpec())
		{
			bAutomatedGenerationPrompted =
				!State->GetActiveWorldSpec().Brief.PlayerPrompt.TrimStartAndEnd().IsEmpty();
		}
		FTimerHandle TimerHandle;
		GetWorldTimerManager().SetTimer(
			TimerHandle, this, &AWorldDirectorFixtureBootstrap::RunAutomatedGenerationViabilityCheck, 5.0f, false);
		return;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("WorldDirectorGenerationAutoTest")))
	{
		UDirectorBridgeSubsystem* Bridge = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UDirectorBridgeSubsystem>() : nullptr;
		if (Bridge == nullptr)
		{
			UE_LOG(LogWorldDirector, Error, TEXT("WORLD_DIRECTOR_GENERATION_PLAYABLE_RESULT=FAIL reason=bridge_missing"));
			FGenericPlatformMisc::RequestExitWithStatus(true, 1);
			return;
		}
		bAutomatedGenerationPrompted = FParse::Param(
			FCommandLine::Get(), TEXT("WorldDirectorPromptedGeneration"));
		bAutomatedGenerationExpectsTimeout = FParse::Param(
			FCommandLine::Get(), TEXT("WorldDirectorExpectGenerationTimeout"));
		bAutomatedGenerationExpectsCancellation = FParse::Param(
			FCommandLine::Get(), TEXT("WorldDirectorExpectGenerationCancellation"));
		Bridge->OnGenerationFinished.AddDynamic(
			this, &AWorldDirectorFixtureBootstrap::HandleAutomatedGenerationFinished);
		const FString Prompt = bAutomatedGenerationPrompted
			? TEXT("A river frontier town held together by a mill, old debts, and a guarded civic secret.")
			: FString();
		const bool bUseCliProvider = FParse::Param(
			FCommandLine::Get(), TEXT("WorldDirectorUseCliProvider"));
		const float Timeout = bAutomatedGenerationExpectsTimeout ? 1.0f : (bUseCliProvider ? 300.0f : 20.0f);
		if (!Bridge->BeginWorldGeneration(
			Prompt, bAutomatedGenerationPrompted ? 9182 : 4815, Timeout, !bUseCliProvider))
		{
			UE_LOG(LogWorldDirector, Error, TEXT("WORLD_DIRECTOR_GENERATION_PLAYABLE_RESULT=FAIL reason=request_rejected"));
			FGenericPlatformMisc::RequestExitWithStatus(true, 1);
		}
		else if (bAutomatedGenerationExpectsCancellation)
		{
			FTimerHandle TimerHandle;
			GetWorldTimerManager().SetTimer(
				TimerHandle, this, &AWorldDirectorFixtureBootstrap::CancelAutomatedGeneration, 0.5f, false);
		}
		return;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("WorldDirectorPlayerFlowAutoTest")))
	{
		bPlayerFlowAutoTest = true;
		bPlayerFlowShouldRegenerate =
			!FParse::Param(FCommandLine::Get(), TEXT("WorldDirectorPlayerFlowUseCliProvider"));
		OpenWorldCreationMenu();
		if (CreateWorldWidget == nullptr || !CreateWorldWidget->IsCreationMenuReady() ||
			!BeginPlayerWorldGeneration(TEXT("A test frontier town shaped by a threatened mill."), 77123,
				!FParse::Param(FCommandLine::Get(), TEXT("WorldDirectorPlayerFlowUseCliProvider"))))
		{
			UE_LOG(LogWorldDirector, Error, TEXT("WORLD_DIRECTOR_PLAYER_FLOW_RESULT=FAIL reason=menu_or_request"));
			FGenericPlatformMisc::RequestExitWithStatus(true, 1);
		}
		return;
	}
	const bool bRuntimeFixtureTest =
		FParse::Param(FCommandLine::Get(), TEXT("WorldDirectorProjectAutoTest")) ||
		FParse::Param(FCommandLine::Get(), TEXT("WorldDirectorInspectionAutoTest")) ||
		FParse::Param(FCommandLine::Get(), TEXT("WorldDirectorDialogueAutoTest")) ||
		FParse::Param(FCommandLine::Get(), TEXT("WorldDirectorAutoTest")) ||
		FParse::Param(FCommandLine::Get(), TEXT("WorldDirectorSimulationAutoTest")) ||
		FParse::Param(FCommandLine::Get(), TEXT("WorldDirectorTravelAutoTest")) ||
		FParse::Param(FCommandLine::Get(), TEXT("WorldDirectorVisualCapture"));
	if (!bRuntimeFixtureTest)
	{
		OpenWorldCreationMenu();
		return;
	}
	if (!CompileFixture())
	{
		UE_LOG(LogWorldDirector, Error, TEXT("WORLD_DIRECTOR_FIXTURE_BOOT_RESULT=FAIL"));
		FGenericPlatformMisc::RequestExitWithStatus(true, 1);
		return;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("WorldDirectorProjectAutoTest")) ||
		FParse::Param(FCommandLine::Get(), TEXT("WorldDirectorInspectionAutoTest")))
	{
		FTimerHandle TimerHandle;
		GetWorldTimerManager().SetTimer(
			TimerHandle, this, &AWorldDirectorFixtureBootstrap::BeginAutomatedProjectCheck, 5.0f, false);
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("WorldDirectorDialogueAutoTest")))
	{
		FTimerHandle TimerHandle;
		GetWorldTimerManager().SetTimer(
			TimerHandle, this, &AWorldDirectorFixtureBootstrap::RunAutomatedDialogueCheck, 2.0f, false);
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("WorldDirectorAutoTest")))
	{
		FTimerHandle TimerHandle;
		GetWorldTimerManager().SetTimer(
			TimerHandle, this, &AWorldDirectorFixtureBootstrap::RunAutomatedViabilityCheck, 5.0f, false);
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("WorldDirectorSimulationAutoTest")))
	{
		FTimerHandle TimerHandle;
		GetWorldTimerManager().SetTimer(
			TimerHandle, this, &AWorldDirectorFixtureBootstrap::RunAutomatedSimulationCheck, 7.0f, false);
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("WorldDirectorTravelAutoTest")))
	{
		FTimerHandle TimerHandle;
		GetWorldTimerManager().SetTimer(
			TimerHandle, this, &AWorldDirectorFixtureBootstrap::BeginAutomatedTravelCheck, 5.0f, false);
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("WorldDirectorVisualCapture")))
	{
		FTimerHandle TimerHandle;
		GetWorldTimerManager().SetTimer(
			TimerHandle, this, &AWorldDirectorFixtureBootstrap::RunAutomatedVisualCapture, 7.0f, false);
	}
}

void AWorldDirectorFixtureBootstrap::OpenWorldCreationMenu()
{
	APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (PlayerController == nullptr)
	{
		if (GetWorld() != nullptr && GetNetMode() != NM_DedicatedServer &&
			!GetWorldTimerManager().IsTimerActive(CreationMenuRetryHandle))
		{
			UE_LOG(LogWorldDirector, Display,
				TEXT("WORLD_DIRECTOR_PLAYER_MENU state=WAITING_FOR_PLAYER_CONTROLLER"));
			GetWorldTimerManager().SetTimer(CreationMenuRetryHandle, this,
				&AWorldDirectorFixtureBootstrap::OpenWorldCreationMenu, 0.1f, false);
		}
		return;
	}
	GetWorldTimerManager().ClearTimer(CreationMenuRetryHandle);
	if (CreateWorldWidget == nullptr)
	{
		CreateWorldWidget = CreateWidget<UWorldDirectorCreateWorldWidget>(
			PlayerController, UWorldDirectorCreateWorldWidget::StaticClass());
		if (CreateWorldWidget != nullptr)
		{
			CreateWorldWidget->InitializeForBootstrap(this);
		}
	}
	if (CreateWorldWidget != nullptr && !CreateWorldWidget->IsInViewport())
	{
		if (const UDirectorBridgeSubsystem* Bridge = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UDirectorBridgeSubsystem>() : nullptr;
			Bridge != nullptr && Bridge->GetGenerationStage() == EWorldDirectorGenerationStage::Completed)
		{
			CreateWorldWidget->PrepareForNewWorld();
		}
		CreateWorldWidget->AddToViewport(100);
	}
	if (CreateWorldWidget != nullptr && !CreateWorldWidget->ApplyViewportLayout() &&
		!GetWorldTimerManager().IsTimerActive(CreationMenuRetryHandle))
	{
		GetWorldTimerManager().SetTimer(CreationMenuRetryHandle, this,
			&AWorldDirectorFixtureBootstrap::OpenWorldCreationMenu, 0.05f, false);
	}
	UE_LOG(LogWorldDirector, Display, TEXT("WORLD_DIRECTOR_PLAYER_MENU state=%s ready=%s"),
		CreateWorldWidget != nullptr && CreateWorldWidget->IsInViewport() ? TEXT("VISIBLE") : TEXT("FAILED"),
		CreateWorldWidget != nullptr && CreateWorldWidget->IsCreationMenuReady() ? TEXT("true") : TEXT("false"));
	ApplyPlayerInputMode();
}

void AWorldDirectorFixtureBootstrap::ToggleWorldCreationMenu()
{
	if (CreateWorldWidget != nullptr && CreateWorldWidget->IsInViewport())
	{
		CloseWorldCreationMenu();
		return;
	}
	OpenWorldCreationMenu();
}

void AWorldDirectorFixtureBootstrap::CloseWorldCreationMenu()
{
	const UDirectorBridgeSubsystem* Bridge = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UDirectorBridgeSubsystem>() : nullptr;
	if (Bridge != nullptr && Bridge->IsGenerationRunning())
	{
		return;
	}
	if (CreateWorldWidget != nullptr)
	{
		CreateWorldWidget->RemoveFromParent();
	}
	ApplyPlayerInputMode();
}

void AWorldDirectorFixtureBootstrap::OpenGenerationDiagnostics()
{
	APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (PlayerController == nullptr)
	{
		return;
	}
	if (GenerationDiagnosticsWidget == nullptr)
	{
		GenerationDiagnosticsWidget = CreateWidget<UWorldDirectorGenerationDiagnosticsWidget>(
			PlayerController, UWorldDirectorGenerationDiagnosticsWidget::StaticClass());
		if (GenerationDiagnosticsWidget != nullptr)
		{
			GenerationDiagnosticsWidget->InitializeForBootstrap(this);
		}
	}
	if (GenerationDiagnosticsWidget != nullptr && !GenerationDiagnosticsWidget->IsInViewport())
	{
		GenerationDiagnosticsWidget->AddToViewport(120);
		GenerationDiagnosticsWidget->ApplyViewportLayout();
	}
	ApplyPlayerInputMode();
}

void AWorldDirectorFixtureBootstrap::ToggleGenerationDiagnostics()
{
	if (GenerationDiagnosticsWidget != nullptr && GenerationDiagnosticsWidget->IsInViewport())
	{
		CloseGenerationDiagnostics();
		return;
	}
	OpenGenerationDiagnostics();
}

void AWorldDirectorFixtureBootstrap::CloseGenerationDiagnostics()
{
	if (GenerationDiagnosticsWidget != nullptr)
	{
		GenerationDiagnosticsWidget->RemoveFromParent();
	}
	ApplyPlayerInputMode();
}

void AWorldDirectorFixtureBootstrap::ToggleInteractionCursor()
{
	if (CreateWorldWidget != nullptr && CreateWorldWidget->IsInViewport())
	{
		return;
	}
	bInteractionCursorMode = !bInteractionCursorMode;
	ApplyPlayerInputMode();
}

void AWorldDirectorFixtureBootstrap::RefreshPlayerInputMode()
{
	ApplyPlayerInputMode();
}

void AWorldDirectorFixtureBootstrap::ApplyPlayerInputMode()
{
	APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (PlayerController == nullptr)
	{
		return;
	}
	const bool bCreationOpen = CreateWorldWidget != nullptr && CreateWorldWidget->IsInViewport();
	const bool bDiagnosticsOpen = GenerationDiagnosticsWidget != nullptr && GenerationDiagnosticsWidget->IsInViewport();
	const bool bInspectionOpen = CompiledTown != nullptr &&
		CompiledTown->ActiveInspectionWidget != nullptr &&
		CompiledTown->ActiveInspectionWidget->IsInViewport();
	UWorldDirectorDialogueWidget* ActiveDialogue = nullptr;
	if (CompiledTown != nullptr)
	{
		for (AWorldDirectorResidentActor* Resident : CompiledTown->SpawnedResidents)
		{
			if (Resident != nullptr && Resident->ActiveDialogueWidget != nullptr &&
				Resident->ActiveDialogueWidget->IsInViewport())
			{
				ActiveDialogue = Resident->ActiveDialogueWidget;
				break;
			}
		}
	}
	const bool bDialogueOpen = ActiveDialogue != nullptr;
	const bool bCursorVisible = bCreationOpen || bDiagnosticsOpen || bInspectionOpen ||
		bDialogueOpen || bInteractionCursorMode;
	PlayerController->bShowMouseCursor = bCursorVisible;
	PlayerController->bEnableClickEvents = bCursorVisible;
	PlayerController->bEnableMouseOverEvents = bCursorVisible;
	if (bDiagnosticsOpen)
	{
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(GenerationDiagnosticsWidget->TakeWidget());
		PlayerController->SetInputMode(InputMode);
	}
	else if (bCreationOpen)
	{
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(CreateWorldWidget->TakeWidget());
		PlayerController->SetInputMode(InputMode);
	}
	else if (bCursorVisible)
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(false);
		if (bInspectionOpen)
		{
			InputMode.SetWidgetToFocus(CompiledTown->ActiveInspectionWidget->TakeWidget());
		}
		else if (bDialogueOpen)
		{
			InputMode.SetWidgetToFocus(ActiveDialogue->TakeWidget());
		}
		PlayerController->SetInputMode(InputMode);
	}
	else
	{
		PlayerController->SetInputMode(FInputModeGameOnly());
	}
}

bool AWorldDirectorFixtureBootstrap::BeginPlayerWorldGeneration(
	const FString& PlayerPrompt, const int32 Seed, const bool bUseFixtureProviderForDebug,
	const FString& Model, const FString& ReasoningEffort)
{
	UDirectorBridgeSubsystem* Bridge = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UDirectorBridgeSubsystem>() : nullptr;
	if (Bridge == nullptr || Bridge->IsGenerationRunning())
	{
		return false;
	}
	Bridge->OnGenerationFinished.RemoveDynamic(
		this, &AWorldDirectorFixtureBootstrap::HandlePlayerGenerationFinished);
	Bridge->OnGenerationFinished.AddDynamic(
		this, &AWorldDirectorFixtureBootstrap::HandlePlayerGenerationFinished);
	return Bridge->BeginWorldGeneration(PlayerPrompt.TrimStartAndEnd(), FMath::Max(1, Seed),
		300.0f, bUseFixtureProviderForDebug, Model, ReasoningEffort);
}

void AWorldDirectorFixtureBootstrap::CancelPlayerWorldGeneration()
{
	if (UDirectorBridgeSubsystem* Bridge = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UDirectorBridgeSubsystem>() : nullptr)
	{
		Bridge->CancelWorldGeneration();
	}
}

void AWorldDirectorFixtureBootstrap::HandlePlayerGenerationFinished(
	const bool bSuccess, const FString& RunId, const FString& Error)
{
	UDirectorBridgeSubsystem* Bridge = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UDirectorBridgeSubsystem>() : nullptr;
	if (Bridge != nullptr)
	{
		Bridge->OnGenerationFinished.RemoveDynamic(
			this, &AWorldDirectorFixtureBootstrap::HandlePlayerGenerationFinished);
	}
	FString ResultError = Error;
	const bool bPlayable = bSuccess && CompileGeneratedWorld();
	if (bSuccess && !bPlayable)
	{
		ResultError = TEXT("The AI specification was accepted, but Unreal could not compile a playable town. ");
		for (const FValidationIssue& Issue : LastCompilationReport.Issues)
		{
			ResultError += FString::Printf(TEXT("[%s %s] %s "),
				*Issue.Code.ToString(), *Issue.Path, *Issue.Message);
		}
	}
	UE_LOG(LogWorldDirector, Display, TEXT("WORLD_DIRECTOR_PLAYER_CREATE_RESULT=%s run=%s error=%s"),
		bPlayable ? TEXT("PASS") : TEXT("FAIL"), *RunId, *ResultError);
	if (CreateWorldWidget != nullptr)
	{
		CreateWorldWidget->SetGenerationResult(bPlayable, ResultError);
	}
	ApplyPlayerInputMode();
	if (bPlayerFlowAutoTest)
	{
		if (!bPlayable)
		{
			UE_LOG(LogWorldDirector, Error, TEXT("WORLD_DIRECTOR_PLAYER_FLOW_RESULT=FAIL reason=compile error=%s"), *ResultError);
			FGenericPlatformMisc::RequestExitWithStatus(true, 1);
			return;
		}
		++PlayerFlowGenerationCount;
		if (bPlayerFlowShouldRegenerate && PlayerFlowGenerationCount == 1)
		{
			FirstPlayerFlowTown = CompiledTown;
			OpenWorldCreationMenu();
			if (!BeginPlayerWorldGeneration(
				TEXT("A second test town generated in place without a level reload."), 88124, true))
			{
				UE_LOG(LogWorldDirector, Error, TEXT("WORLD_DIRECTOR_PLAYER_FLOW_RESULT=FAIL reason=regeneration_request"));
				FGenericPlatformMisc::RequestExitWithStatus(true, 1);
			}
			return;
		}
		FTimerHandle TimerHandle;
		GetWorldTimerManager().SetTimer(
			TimerHandle, this, &AWorldDirectorFixtureBootstrap::RunAutomatedPlayerFlowCheck, 5.0f, false);
	}
}

bool AWorldDirectorFixtureBootstrap::CompileGeneratedWorld()
{
	const double CompileStartedAt = FPlatformTime::Seconds();
	UDirectorBridgeSubsystem* Bridge = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UDirectorBridgeSubsystem>() : nullptr;
	UWorldGenerationSubsystem* Generation = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UWorldGenerationSubsystem>() : nullptr;
	if (Bridge == nullptr || Generation == nullptr)
	{
		if (Bridge != nullptr)
		{
			Bridge->RecordRuntimeCompilationMetric(false,
				FPlatformTime::Seconds() - CompileStartedAt, 0, 0,
				TEXT("Required runtime generation subsystem is unavailable."));
		}
		return false;
	}
	FGeneratedWorldSpec Spec = Bridge->GetGeneratedWorldSpec();
	FString TerrainOverride;
	if (FParse::Value(FCommandLine::Get(), TEXT("WorldDirectorTerrainOverride="), TerrainOverride) &&
		!TerrainOverride.TrimStartAndEnd().IsEmpty())
	{
		Spec.Brief.TerrainPreferences = {TerrainOverride.TrimStartAndEnd()};
		UE_LOG(LogWorldDirector, Display, TEXT("WORLD_DIRECTOR_TERRAIN_OVERRIDE=%s"), *TerrainOverride);
	}
	FResolvedWorldPlan Plan;
	LastCompilationReport = FValidationReport();
	if (!Generation->ResolveWorldSpec(Spec, Plan, LastCompilationReport))
	{
		Bridge->RecordRuntimeCompilationMetric(false,
			FPlatformTime::Seconds() - CompileStartedAt, 0, 0,
			TEXT("Accepted specification could not resolve to certified assets."));
		return false;
	}
	DestroyCurrentTown();
	AWorldDirectorTownActor* SpawnedTown = nullptr;
	if (!Generation->CompileResolvedWorld(this, Plan, SpawnedTown, LastCompilationReport) || SpawnedTown == nullptr)
	{
		Bridge->RecordRuntimeCompilationMetric(false,
			FPlatformTime::Seconds() - CompileStartedAt, 0, 0,
			TEXT("Resolved plan could not spawn a runtime town."));
		return false;
	}
	CompiledTown = SpawnedTown;
	if (UWorldStateSubsystem* State = GetWorld()->GetSubsystem<UWorldStateSubsystem>())
	{
		State->SetActiveWorldSpec(Spec);
	}
	UTownSimulationSubsystem* Simulation = GetWorld()->GetSubsystem<UTownSimulationSubsystem>();
	if (Simulation == nullptr || !Simulation->InitializeLivingTown(Spec, SpawnedTown, LastCompilationReport))
	{
		const int32 LocationCount = SpawnedTown->SpawnedLocations.Num();
		const int32 ResidentCount = SpawnedTown->SpawnedResidents.Num();
		if (Simulation != nullptr)
		{
			Simulation->ShutdownLivingTown();
		}
		if (UChangeProjectSubsystem* Projects = GetWorld()
			? GetWorld()->GetSubsystem<UChangeProjectSubsystem>() : nullptr)
		{
			Projects->ShutdownProjects();
		}
		if (UWorldStateSubsystem* State = GetWorld()->GetSubsystem<UWorldStateSubsystem>())
		{
			State->ClearActiveWorldSpec();
		}
		SpawnedTown->DestroyCompiledContent();
		CompiledTown = nullptr;
		Bridge->RecordRuntimeCompilationMetric(false,
			FPlatformTime::Seconds() - CompileStartedAt, LocationCount, ResidentCount,
			TEXT("Town spawned, but living simulation initialization failed."));
		return false;
	}
	Bridge->RecordRuntimeCompilationMetric(true,
		FPlatformTime::Seconds() - CompileStartedAt,
		SpawnedTown->SpawnedLocations.Num(), SpawnedTown->SpawnedResidents.Num(), FString());
	return true;
}

void AWorldDirectorFixtureBootstrap::DestroyCurrentTown()
{
	if (UTownSimulationSubsystem* Simulation = GetWorld()
		? GetWorld()->GetSubsystem<UTownSimulationSubsystem>() : nullptr)
	{
		Simulation->ShutdownLivingTown();
	}
	if (UWorldStateSubsystem* State = GetWorld()
		? GetWorld()->GetSubsystem<UWorldStateSubsystem>() : nullptr)
	{
		State->ClearActiveWorldSpec();
	}
	if (UChangeProjectSubsystem* Projects = GetWorld()
		? GetWorld()->GetSubsystem<UChangeProjectSubsystem>() : nullptr)
	{
		Projects->ShutdownProjects();
	}
	if (CompiledTown != nullptr)
	{
		CompiledTown->DestroyCompiledContent();
		CompiledTown = nullptr;
	}
}

void AWorldDirectorFixtureBootstrap::RunAutomatedPlayerFlowCheck()
{
	OpenGenerationDiagnostics();
	const bool bDiagnosticsReady = GenerationDiagnosticsWidget != nullptr &&
		GenerationDiagnosticsWidget->IsDiagnosticsReady();
	CloseGenerationDiagnostics();
	OpenWorldCreationMenu();
	const bool bCreationReopened = CreateWorldWidget != nullptr && CreateWorldWidget->IsInViewport();
	CloseWorldCreationMenu();
	const UDirectorBridgeSubsystem* Bridge = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UDirectorBridgeSubsystem>() : nullptr;
	const FValidationReport Navigation = CompiledTown
		? CompiledTown->ValidateNavigationViability() : LastCompilationReport;
	const bool bRunSummaryExists = Bridge != nullptr && IFileManager::Get().FileExists(
		*FPaths::Combine(Bridge->GetGenerationRunDirectory(), TEXT("run-summary.json")));
	const bool bRegenerationPassed = !bPlayerFlowShouldRegenerate ||
		(PlayerFlowGenerationCount == 2 && !FirstPlayerFlowTown.IsValid());
	const APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const bool bGameplayInputRestored = PlayerController != nullptr && CreateWorldWidget != nullptr &&
		GenerationDiagnosticsWidget != nullptr &&
		!PlayerController->bShowMouseCursor && !PlayerController->bEnableClickEvents &&
		!CreateWorldWidget->IsInViewport() && !GenerationDiagnosticsWidget->IsInViewport();
	const bool bPassed = CreateWorldWidget != nullptr && CreateWorldWidget->IsCreationMenuReady() &&
		GenerationDiagnosticsWidget != nullptr && bDiagnosticsReady && bCreationReopened &&
		CompiledTown != nullptr && CompiledTown->SpawnedLocations.Num() >= 12 &&
		CompiledTown->SpawnedResidents.Num() >= 20 && Navigation.bValid && bRunSummaryExists &&
		bRegenerationPassed && bGameplayInputRestored;
	UE_LOG(LogWorldDirector, Display,
		TEXT("WORLD_DIRECTOR_PLAYER_FLOW_EVIDENCE menu=READY diagnostics=READY dismiss=PASS gameplayInput=%s generations=%d regeneration=%s locations=%d residents=%d navigation=%s runSummary=%s"),
		bGameplayInputRestored ? TEXT("PASS") : TEXT("FAIL"),
		PlayerFlowGenerationCount, bRegenerationPassed ? TEXT("PASS") : TEXT("FAIL"),
		CompiledTown ? CompiledTown->SpawnedLocations.Num() : 0,
		CompiledTown ? CompiledTown->SpawnedResidents.Num() : 0,
		Navigation.bValid ? TEXT("PASS") : TEXT("FAIL"), bRunSummaryExists ? TEXT("PRESENT") : TEXT("MISSING"));
	UE_LOG(LogWorldDirector, Display, TEXT("WORLD_DIRECTOR_PLAYER_FLOW_RESULT=%s"),
		bPassed ? TEXT("PASS") : TEXT("FAIL"));
	FGenericPlatformMisc::RequestExitWithStatus(true, bPassed ? 0 : 1);
}

void AWorldDirectorFixtureBootstrap::RunAutomatedDialogueCheck()
{
	AWorldDirectorResidentActor* Resident = CompiledTown && !CompiledTown->SpawnedResidents.IsEmpty()
		? CompiledTown->SpawnedResidents[0].Get() : nullptr;
	if (Resident != nullptr)
	{
		Resident->OpenDialogueMenu();
	}
	const bool bPassed = Resident != nullptr && Resident->ActiveDialogueWidget != nullptr &&
		Resident->ActiveDialogueWidget->IsInViewport() &&
		Resident->ActiveDialogueWidget->IsDialogueMenuReady();
	UE_LOG(LogWorldDirector, Display,
		TEXT("WORLD_DIRECTOR_DIALOGUE_RESULT=%s resident=%s"),
		bPassed ? TEXT("PASS") : TEXT("FAIL"), Resident ? *Resident->ResidentId : TEXT("missing"));
	if (bPassed && FParse::Param(FCommandLine::Get(), TEXT("WorldDirectorDialogueVisualCapture")))
	{
		FScreenshotRequest::RequestScreenshot(
			FPaths::Combine(FPaths::ScreenShotDir(), TEXT("WorldDirectorPhase6Dialogue.png")),
			true, false);
		FTimerHandle TimerHandle;
		GetWorldTimerManager().SetTimer(
			TimerHandle, this, &AWorldDirectorFixtureBootstrap::FinishAutomatedVisualCapture, 1.0f, false);
		return;
	}
	FGenericPlatformMisc::RequestExitWithStatus(true, bPassed ? 0 : 1);
}

void AWorldDirectorFixtureBootstrap::BeginAutomatedProjectCheck()
{
	UTownSimulationSubsystem* Simulation = GetWorld()
		? GetWorld()->GetSubsystem<UTownSimulationSubsystem>() : nullptr;
	UChangeProjectSubsystem* Projects = GetWorld()
		? GetWorld()->GetSubsystem<UChangeProjectSubsystem>() : nullptr;
	const UWorldStateSubsystem* State = GetWorld()
		? GetWorld()->GetSubsystem<UWorldStateSubsystem>() : nullptr;
	if (Simulation == nullptr || Projects == nullptr || State == nullptr ||
		!State->HasActiveWorldSpec() || Projects->GetProjects().Num() != 1 || CompiledTown == nullptr)
	{
		UE_LOG(LogWorldDirector, Error, TEXT("WORLD_DIRECTOR_PROJECT_RESULT=FAIL reason=runtime_missing"));
		FGenericPlatformMisc::RequestExitWithStatus(true, 1);
		return;
	}
	const FChangeProject Project = Projects->GetProjects()[0];
	ProjectCheckProjectId = Project.Id;
	bProjectCheckHasDecisionCases = State->GetActiveWorldSpec().Locations.ContainsByPredicate(
		[](const FWorldLocation& Location) { return Location.Id == TEXT("location.home_ash"); });
	if (bProjectCheckHasDecisionCases)
	{
		FChangeProject RefusedProject = Project;
		RefusedProject.Id = TEXT("project.unsupported_clinic");
		RefusedProject.DesiredPurposeTag = TEXT("Purpose.Clinic");
		RefusedProject.RequiredParticipantResidentIds = {Project.InitiatorResidentId};
		RefusedProject.State = EWorldDirectorProjectState::Proposed;
		Projects->AddProject(RefusedProject);
		FChangeProject DelayedProject = Project;
		DelayedProject.Id = TEXT("project.player_nearby_clinic");
		DelayedProject.TargetLocationId = TEXT("location.home_ash");
		DelayedProject.DesiredPurposeTag = TEXT("Purpose.Clinic");
		DelayedProject.RequiredParticipantResidentIds = {TEXT("resident.mara"), TEXT("resident.anwen")};
		DelayedProject.RequiredCapabilityTags = {
			TEXT("Capability.Bed"), TEXT("Capability.WorkSurface"), TEXT("Capability.Chair"),
			TEXT("Capability.Door"), TEXT("Capability.Interior")
		};
		DelayedProject.State = EWorldDirectorProjectState::Proposed;
		Projects->AddProject(DelayedProject);
		FChangeProject FailedProject = Project;
		FailedProject.Id = TEXT("project.forced_transition_failure");
		FailedProject.TargetLocationId = TEXT("location.pilgrim_shelter");
		FailedProject.DesiredPurposeTag = TEXT("Purpose.Headquarters");
		FailedProject.InitiatorResidentId = TEXT("resident.orla");
		FailedProject.RequiredParticipantResidentIds = {TEXT("resident.orla")};
		FailedProject.RequiredCapabilityTags = {
			TEXT("Capability.Bed"), TEXT("Capability.WorkSurface"), TEXT("Capability.Chair"),
			TEXT("Capability.Door"), TEXT("Capability.Interior")
		};
		FailedProject.RequiredConditionTags = {TEXT("Condition.ThreatActive"), TEXT("Condition.Overnight")};
		FailedProject.State = EWorldDirectorProjectState::Proposed;
		Projects->AddProject(FailedProject);
		if (const TObjectPtr<AWorldDirectorLocationActor>* FailedTarget =
			CompiledTown->SpawnedLocations.FindByPredicate(
				[](const AWorldDirectorLocationActor* Location)
				{ return Location != nullptr && Location->LocationId == TEXT("location.pilgrim_shelter"); }))
		{
			ProjectCheckFailedTarget = FailedTarget->Get();
		}
	}
	if (const TObjectPtr<AWorldDirectorLocationActor>* Target = CompiledTown->SpawnedLocations.FindByPredicate(
		[&Project](const AWorldDirectorLocationActor* Location)
		{ return Location != nullptr && Location->LocationId == Project.TargetLocationId; }))
	{
		ProjectCheckTarget = Target->Get();
		ProjectCheckOriginalInterior = Target->Get()->InteriorActor;
	}
	const FResident* Initiator = State->GetActiveWorldSpec().Residents.FindByPredicate(
		[&Project](const FResident& Resident) { return Resident.Id == Project.InitiatorResidentId; });
	ProjectCheckInitialMemoryCount = Initiator ? Initiator->ImportantMemories.Num() : 0;
	ProjectCheckInitialBeliefCount = Initiator ? Initiator->BeliefIds.Num() : 0;
	const int64 ToPreparation = FMath::Max<int64>(
		1, Project.IntendedStartMinute - Simulation->GetElapsedSimulationMinutes());
	Simulation->AdvanceSimulationMinutes(static_cast<int32>(ToPreparation));
	if (ProjectCheckFailedTarget.IsValid())
	{
		if (AActor* Interior = ProjectCheckFailedTarget->InteriorActor.Get())
		{
			Interior->Destroy();
		}
		ProjectCheckFailedTarget->InteriorActor = nullptr;
	}
	if (bProjectCheckHasDecisionCases)
	{
		if (APlayerController* PlayerController = GetWorld()->GetFirstPlayerController())
		{
			if (APawn* Pawn = PlayerController->GetPawn())
			{
				if (const TObjectPtr<AWorldDirectorLocationActor>* DelayTarget =
					CompiledTown->SpawnedLocations.FindByPredicate(
						[](const AWorldDirectorLocationActor* Location)
						{ return Location != nullptr && Location->LocationId == TEXT("location.home_ash"); }))
				{
					Pawn->SetActorLocation(DelayTarget->Get()->NavigationEntranceLocation);
				}
			}
		}
	}
	Simulation->AdvanceSimulationMinutes(Project.RequiredTransitionMinutes);
	FTimerHandle TimerHandle;
	GetWorldTimerManager().SetTimer(
		TimerHandle, this, &AWorldDirectorFixtureBootstrap::FinishAutomatedProjectCheck, 5.0f, false);
}

void AWorldDirectorFixtureBootstrap::FinishAutomatedProjectCheck()
{
	const UChangeProjectSubsystem* Projects = GetWorld()
		? GetWorld()->GetSubsystem<UChangeProjectSubsystem>() : nullptr;
	const UWorldStateSubsystem* State = GetWorld()
		? GetWorld()->GetSubsystem<UWorldStateSubsystem>() : nullptr;
	const AWorldDirectorLocationActor* Target = ProjectCheckTarget.Get();
	const FChangeProject* Project = Projects ? Projects->GetProjects().FindByPredicate(
		[this](const FChangeProject& Candidate)
		{ return Candidate.Id == ProjectCheckProjectId; }) : nullptr;
	const FChangeProject* RefusedProject = Projects ? Projects->GetProjects().FindByPredicate(
		[](const FChangeProject& Candidate)
		{ return Candidate.Id == TEXT("project.unsupported_clinic"); }) : nullptr;
	const FChangeProject* DelayedProject = Projects ? Projects->GetProjects().FindByPredicate(
		[](const FChangeProject& Candidate)
		{ return Candidate.Id == TEXT("project.player_nearby_clinic"); }) : nullptr;
	const FChangeProject* FailedProject = Projects ? Projects->GetProjects().FindByPredicate(
		[](const FChangeProject& Candidate)
		{ return Candidate.Id == TEXT("project.forced_transition_failure"); }) : nullptr;
	const FGeneratedWorldSpec* Spec = State && State->HasActiveWorldSpec()
		? &State->GetActiveWorldSpec() : nullptr;
	const FWorldLocation* Location = Spec && Project
		? Spec->Locations.FindByPredicate(
			[Project](const FWorldLocation& Candidate)
			{ return Candidate.Id == Project->TargetLocationId; }) : nullptr;
	const FResident* Initiator = Spec && Project
		? Spec->Residents.FindByPredicate(
			[Project](const FResident& Resident)
			{ return Resident.Id == Project->InitiatorResidentId; }) : nullptr;
	bool bHasWorkStation = false;
	bool bHasMeetingStation = false;
	bool bHasSleepStation = false;
	if (Target != nullptr)
	{
		for (const AWorldDirectorActivityStationActor* Station : Target->ActivityStations)
		{
			bHasWorkStation |= Station != nullptr && Station->ActivityTag == TEXT("Activity.Work");
			bHasMeetingStation |= Station != nullptr && Station->ActivityTag == TEXT("Activity.Meet");
			bHasSleepStation |= Station != nullptr && Station->ActivityTag == TEXT("Activity.Sleep");
		}
	}
	const TObjectPtr<AWorldDirectorResidentActor>* InitiatorActorMatch = CompiledTown && Project
		? CompiledTown->SpawnedResidents.FindByPredicate(
			[Project](const AWorldDirectorResidentActor* Resident)
			{ return Resident != nullptr && Resident->ResidentId == Project->InitiatorResidentId; })
		: nullptr;
	const AWorldDirectorResidentActor* InitiatorActor =
		InitiatorActorMatch ? InitiatorActorMatch->Get() : nullptr;
	const bool bScheduleChanged = InitiatorActor && InitiatorActor->Schedule.ContainsByPredicate(
		[Project](const FWorldDirectorScheduleStop& Stop)
		{ return Stop.Hour == 18 && Stop.LocationId == Project->TargetLocationId; });
	const bool bPrimaryPurposeStation = Project &&
		((Project->DesiredPurposeTag == TEXT("Purpose.Clinic") ||
		  Project->DesiredPurposeTag == TEXT("Purpose.Shelter")) ? bHasSleepStation : bHasWorkStation);
	const bool bPurposeStations = bPrimaryPurposeStation &&
		(Project->DesiredPurposeTag != TEXT("Purpose.Headquarters") || bHasMeetingStation);
	const EWorldDirectorAccessPolicy ExpectedAccess = Project &&
		Project->DesiredPurposeTag == TEXT("Purpose.Headquarters")
		? EWorldDirectorAccessPolicy::Restricted
		: Project && Project->DesiredPurposeTag == TEXT("Purpose.Workplace")
			? EWorldDirectorAccessPolicy::Workers : EWorldDirectorAccessPolicy::Public;
	const FValidationReport NavigationReport = CompiledTown
		? CompiledTown->ValidateNavigationViability() : FValidationReport();
	const bool bPassed = Project != nullptr && Project->State == EWorldDirectorProjectState::Active &&
		(!bProjectCheckHasDecisionCases ||
		 (RefusedProject != nullptr && RefusedProject->State == EWorldDirectorProjectState::Refused &&
		  DelayedProject != nullptr && DelayedProject->State == EWorldDirectorProjectState::Delayed &&
		  FailedProject != nullptr && FailedProject->State == EWorldDirectorProjectState::Failed)) &&
		Location != nullptr && Location->PurposeTag == Project->DesiredPurposeTag &&
		Location->ControllerResidentId == Project->InitiatorResidentId &&
		Location->AccessPolicy == ExpectedAccess &&
		Target != nullptr && Target == ProjectCheckTarget.Get() &&
		Target->InteriorActor != ProjectCheckOriginalInterior.Get() && Target->DressingRevision == 1 &&
		Target->ProjectSign != nullptr && !Target->ProjectSign->bHiddenInGame &&
		bPurposeStations && bScheduleChanged && Initiator != nullptr &&
		Initiator->ImportantMemories.Num() > ProjectCheckInitialMemoryCount &&
		Initiator->BeliefIds.Num() > ProjectCheckInitialBeliefCount && NavigationReport.bValid;
	UE_LOG(LogWorldDirector, Display,
		TEXT("WORLD_DIRECTOR_PROJECT_EVIDENCE state=%s refusedState=%s delayedState=%s failedState=%s sameActor=%d purpose=%s controller=%s access=%d dressingRevision=%d stations=%d memoriesAdded=%d beliefsAdded=%d navigation=%s"),
		Project ? *StaticEnum<EWorldDirectorProjectState>()->GetNameStringByValue(
			static_cast<int64>(Project->State)) : TEXT("missing"),
		RefusedProject ? *StaticEnum<EWorldDirectorProjectState>()->GetNameStringByValue(
			static_cast<int64>(RefusedProject->State)) : TEXT("missing"),
		DelayedProject ? *StaticEnum<EWorldDirectorProjectState>()->GetNameStringByValue(
			static_cast<int64>(DelayedProject->State)) : TEXT("missing"),
		FailedProject ? *StaticEnum<EWorldDirectorProjectState>()->GetNameStringByValue(
			static_cast<int64>(FailedProject->State)) : TEXT("missing"),
		Target != nullptr && Target == ProjectCheckTarget.Get(),
		Location ? *Location->PurposeTag.ToString() : TEXT("missing"),
		Location ? *Location->ControllerResidentId : TEXT("missing"),
		Location ? static_cast<int32>(Location->AccessPolicy) : -1,
		Target ? Target->DressingRevision : -1,
		Target ? Target->ActivityStations.Num() : -1,
		Initiator ? Initiator->ImportantMemories.Num() - ProjectCheckInitialMemoryCount : -1,
		Initiator ? Initiator->BeliefIds.Num() - ProjectCheckInitialBeliefCount : -1,
		NavigationReport.bValid ? TEXT("PASS") : TEXT("FAIL"));
	UE_LOG(LogWorldDirector, Display, TEXT("WORLD_DIRECTOR_PROJECT_RESULT=%s"),
		bPassed ? TEXT("PASS") : TEXT("FAIL"));
	if (FParse::Param(FCommandLine::Get(), TEXT("WorldDirectorInspectionAutoTest")))
	{
		if (CompiledTown != nullptr)
		{
			CompiledTown->OpenInspectionMenu();
		}
		const bool bInspectionPassed = bPassed && CompiledTown != nullptr &&
			CompiledTown->ActiveInspectionWidget != nullptr &&
			CompiledTown->ActiveInspectionWidget->IsInViewport() &&
			CompiledTown->ActiveInspectionWidget->IsInspectionReady();
		UE_LOG(LogWorldDirector, Display, TEXT("WORLD_DIRECTOR_INSPECTION_RESULT=%s"),
			bInspectionPassed ? TEXT("PASS") : TEXT("FAIL"));
		FGenericPlatformMisc::RequestExitWithStatus(true, bInspectionPassed ? 0 : 1);
		return;
	}
	FGenericPlatformMisc::RequestExitWithStatus(true, bPassed ? 0 : 1);
}

void AWorldDirectorFixtureBootstrap::HandleAutomatedGenerationFinished(
	const bool bSuccess,
	const FString& RunId,
	const FString& Error)
{
	if (bAutomatedGenerationExpectsCancellation)
	{
		const bool bExpectedFailure = !bSuccess && Error.Contains(TEXT("cancelled"));
		UE_LOG(LogWorldDirector, Display,
			TEXT("WORLD_DIRECTOR_GENERATION_CANCELLATION_RESULT=%s run=%s error=%s"),
			bExpectedFailure ? TEXT("PASS") : TEXT("FAIL"), *RunId, *Error);
		FGenericPlatformMisc::RequestExitWithStatus(true, bExpectedFailure ? 0 : 1);
		return;
	}
	if (bAutomatedGenerationExpectsTimeout)
	{
		const bool bExpectedFailure = !bSuccess && Error.Contains(TEXT("timeout"));
		UE_LOG(LogWorldDirector, Display,
			TEXT("WORLD_DIRECTOR_GENERATION_TIMEOUT_RESULT=%s run=%s error=%s"),
			bExpectedFailure ? TEXT("PASS") : TEXT("FAIL"), *RunId, *Error);
		FGenericPlatformMisc::RequestExitWithStatus(true, bExpectedFailure ? 0 : 1);
		return;
	}
	if (!bSuccess)
	{
		UE_LOG(LogWorldDirector, Error,
			TEXT("WORLD_DIRECTOR_GENERATION_PLAYABLE_RESULT=FAIL run=%s error=%s"), *RunId, *Error);
		FGenericPlatformMisc::RequestExitWithStatus(true, 1);
		return;
	}
	if (!CompileGeneratedWorld())
	{
		UE_LOG(LogWorldDirector, Error, TEXT("WORLD_DIRECTOR_GENERATION_PLAYABLE_RESULT=FAIL reason=runtime_compile_failed"));
		FGenericPlatformMisc::RequestExitWithStatus(true, 1);
		return;
	}
	FTimerHandle TimerHandle;
	GetWorldTimerManager().SetTimer(
		TimerHandle, this, &AWorldDirectorFixtureBootstrap::RunAutomatedGenerationViabilityCheck, 5.0f, false);
}

void AWorldDirectorFixtureBootstrap::CancelAutomatedGeneration()
{
	if (UDirectorBridgeSubsystem* Bridge = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UDirectorBridgeSubsystem>() : nullptr)
	{
		Bridge->CancelWorldGeneration();
	}
}

void AWorldDirectorFixtureBootstrap::RunAutomatedGenerationViabilityCheck()
{
	const FValidationReport Report = CompiledTown
		? CompiledTown->ValidateNavigationViability() : LastCompilationReport;
	const int32 ResidentCount = CompiledTown ? CompiledTown->SpawnedResidents.Num() : 0;
	const int32 LocationCount = CompiledTown ? CompiledTown->SpawnedLocations.Num() : 0;
	const bool bPassed = Report.bValid && ResidentCount >= 20 && ResidentCount <= 30 &&
		LocationCount >= 12 && LocationCount <= 18;
	UE_LOG(LogWorldDirector, Display,
		TEXT("WORLD_DIRECTOR_GENERATION_PLAYABLE_RESULT=%s mode=%s locations=%d residents=%d run=%s"),
		bPassed ? TEXT("PASS") : TEXT("FAIL"),
		bAutomatedGenerationPrompted ? TEXT("prompted") : TEXT("blank"),
		LocationCount,
		ResidentCount,
		GetGameInstance()->GetSubsystem<UDirectorBridgeSubsystem>()
			? *GetGameInstance()->GetSubsystem<UDirectorBridgeSubsystem>()->GetGenerationRunId()
			: TEXT("unknown"));
	FGenericPlatformMisc::RequestExitWithStatus(true, bPassed ? 0 : 1);
}

bool AWorldDirectorFixtureBootstrap::CompileFixture()
{
	LastCompilationReport = FValidationReport();
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("WorldDirector"));
	if (!Plugin.IsValid())
	{
		LastCompilationReport.AddError(TEXT("fixture.plugin_missing"), TEXT("plugin"), TEXT("WorldDirector plugin is unavailable."));
		return false;
	}
	FString Json;
	FString FixturePath;
	if (!FParse::Value(FCommandLine::Get(), TEXT("WorldDirectorReplaySpec="), FixturePath))
	{
		FixturePath = FPaths::Combine(
			Plugin->GetBaseDir(), TEXT("Resources/Fixtures"), FixtureFilename);
	}
	if (!FFileHelper::LoadFileToString(Json, *FixturePath))
	{
		LastCompilationReport.AddError(TEXT("fixture.file_missing"), TEXT("fixtureFilename"), FixturePath);
		return false;
	}
	UGameInstance* GameInstance = GetGameInstance();
	UWorldGenerationSubsystem* Generation = GameInstance
		? GameInstance->GetSubsystem<UWorldGenerationSubsystem>()
		: nullptr;
	if (Generation == nullptr)
	{
		LastCompilationReport.AddError(TEXT("fixture.subsystem_missing"), TEXT("generationSubsystem"), TEXT("World generation subsystem is unavailable."));
		return false;
	}
	FGeneratedWorldSpec Spec;
	if (!Generation->LoadAndValidateWorldSpec(Json, Spec, LastCompilationReport))
	{
		return false;
	}
	int32 CompilerSeedOverride = Spec.Seed;
	if (FParse::Value(
		FCommandLine::Get(), TEXT("WorldDirectorCompilerSeedOverride="), CompilerSeedOverride))
	{
		Spec.Seed = CompilerSeedOverride;
	}
	FString TerrainOverride;
	if (FParse::Value(FCommandLine::Get(), TEXT("WorldDirectorTerrainOverride="), TerrainOverride) &&
		!TerrainOverride.TrimStartAndEnd().IsEmpty())
	{
		Spec.Brief.TerrainPreferences = {TerrainOverride.TrimStartAndEnd()};
		UE_LOG(LogWorldDirector, Display, TEXT("WORLD_DIRECTOR_TERRAIN_OVERRIDE=%s"), *TerrainOverride);
	}
	FResolvedWorldPlan Plan;
	if (!Generation->ResolveWorldSpec(Spec, Plan, LastCompilationReport))
	{
		return false;
	}
	AWorldDirectorTownActor* SpawnedTown = nullptr;
	const bool bCompiled = Generation->CompileResolvedWorld(
		this, Plan, SpawnedTown, LastCompilationReport);
	CompiledTown = SpawnedTown;
	if (!bCompiled || SpawnedTown == nullptr)
	{
		return false;
	}
	if (UWorldStateSubsystem* WorldState = GetWorld()->GetSubsystem<UWorldStateSubsystem>())
	{
		WorldState->SetActiveWorldSpec(Spec);
	}
	if (Spec.Residents.Num() >= 20)
	{
		UTownSimulationSubsystem* Simulation = GetWorld()->GetSubsystem<UTownSimulationSubsystem>();
		if (Simulation == nullptr || !Simulation->InitializeLivingTown(Spec, SpawnedTown, LastCompilationReport))
		{
			return false;
		}
	}
	return true;
}

void AWorldDirectorFixtureBootstrap::RunAutomatedViabilityCheck()
{
	FValidationReport Report;
	if (CompiledTown == nullptr)
	{
		Report = LastCompilationReport;
		if (Report.bValid)
		{
			Report.AddError(TEXT("compiler.town_missing"), TEXT("compiledTown"), TEXT("The fixture did not produce a town actor."));
		}
	}
	else
	{
		Report = CompiledTown->ValidateNavigationViability();
	}
	for (const FValidationIssue& Issue : Report.Issues)
	{
		UE_LOG(
			LogWorldDirector,
			Error,
			TEXT("WORLD_DIRECTOR_VIABILITY_ISSUE code=%s path=%s message=%s"),
			*Issue.Code.ToString(), *Issue.Path, *Issue.Message);
	}
	UE_LOG(
		LogWorldDirector,
		Display,
		TEXT("WORLD_DIRECTOR_NAVIGATION_RESULT=%s locations=%d residents=%d"),
		Report.bValid ? TEXT("PASS") : TEXT("FAIL"),
		CompiledTown ? CompiledTown->SpawnedLocations.Num() : 0,
		CompiledTown ? CompiledTown->SpawnedResidents.Num() : 0);
	FGenericPlatformMisc::RequestExitWithStatus(false, Report.bValid ? 0 : 2);
}

void AWorldDirectorFixtureBootstrap::RunAutomatedSimulationCheck()
{
	FValidationReport Report;
	UTownSimulationSubsystem* Simulation = GetWorld()
		? GetWorld()->GetSubsystem<UTownSimulationSubsystem>() : nullptr;
	if (Simulation == nullptr || !Simulation->IsSimulationEnabled())
	{
		Report.AddError(TEXT("simulation.not_initialized"), TEXT("simulation"),
			TEXT("Living-town simulation did not initialize."));
	}
	else
	{
		auto LogPhase = [Simulation](const TCHAR* Phase, const int32 Day)
		{
			int32 Traveling = 0;
			int32 Working = 0;
			int32 Socializing = 0;
			int32 Sleeping = 0;
			int32 Waiting = 0;
			for (const FWorldDirectorResidentRuntimeState& State : Simulation->GetResidentStates())
			{
				Traveling += State.Availability == EWorldDirectorResidentAvailability::Traveling;
				Working += State.Availability == EWorldDirectorResidentAvailability::Working;
				Socializing += State.Availability == EWorldDirectorResidentAvailability::Socializing;
				Sleeping += State.Availability == EWorldDirectorResidentAvailability::Sleeping;
				Waiting += State.Availability == EWorldDirectorResidentAvailability::Waiting;
			}
			UE_LOG(LogWorldDirector, Display,
				TEXT("WORLD_DIRECTOR_SIMULATION_PHASE day=%d phase=%s sleeping=%d working=%d socializing=%d traveling=%d waiting=%d"),
				Day, Phase, Sleeping, Working, Socializing, Traveling, Waiting);
		};
		for (int32 Day = 0; Day < 3; ++Day)
		{
			Simulation->AdvanceSimulationMinutes(120); // 06:00 -> 08:00 work
			LogPhase(TEXT("work"), Day);
			Simulation->AdvanceSimulationMinutes(600); // 08:00 -> 18:00 meeting
			LogPhase(TEXT("meeting"), Day);
			Simulation->AdvanceSimulationMinutes(180); // 18:00 -> 21:00 home
			LogPhase(TEXT("sleep"), Day);
			Simulation->AdvanceSimulationMinutes(540); // 21:00 -> next 06:00
		}
		Report = Simulation->ValidateMultiDayCoherence();
		UE_LOG(LogWorldDirector, Display,
			TEXT("WORLD_DIRECTOR_SIMULATION_EVIDENCE days=%d minute=%lld residents=%d sharedBeliefs=%d relationshipEvents=%d closedFacts=%d"),
			Simulation->GetSimulationDay(), Simulation->GetElapsedSimulationMinutes(),
			Simulation->GetResidentStates().Num(), Simulation->GetSharedBeliefCount(),
			Simulation->GetRelationshipEventCount(), Simulation->GetClosedFactCount());
	}
	for (const FValidationIssue& Issue : Report.Issues)
	{
		UE_LOG(LogWorldDirector, Error,
			TEXT("WORLD_DIRECTOR_SIMULATION_ISSUE code=%s path=%s message=%s"),
			*Issue.Code.ToString(), *Issue.Path, *Issue.Message);
	}
	UE_LOG(LogWorldDirector, Display, TEXT("WORLD_DIRECTOR_SIMULATION_RESULT=%s"),
		Report.bValid ? TEXT("PASS") : TEXT("FAIL"));
	FGenericPlatformMisc::RequestExitWithStatus(false, Report.bValid ? 0 : 3);
}

void AWorldDirectorFixtureBootstrap::BeginAutomatedTravelCheck()
{
	UTownSimulationSubsystem* Simulation = GetWorld()
		? GetWorld()->GetSubsystem<UTownSimulationSubsystem>() : nullptr;
	if (Simulation == nullptr || CompiledTown == nullptr)
	{
		UE_LOG(LogWorldDirector, Error, TEXT("WORLD_DIRECTOR_TRAVEL_RESULT=FAIL reason=simulation_missing"));
		FGenericPlatformMisc::RequestExitWithStatus(false, 4);
		return;
	}
	Simulation->SetMinutesPerRealSecond(0.0f);
	const int32 TargetMinute = 7 * 60 + 59;
	const int32 MinutesToTarget = (TargetMinute - Simulation->GetMinuteOfDay() + 1440) % 1440;
	Simulation->AdvanceSimulationMinutes(MinutesToTarget); // Resolve offscreen at home at 07:59.
	TravelCheckStartLocations.Reset();
	for (const AWorldDirectorResidentActor* Resident : CompiledTown->SpawnedResidents)
	{
		if (Resident != nullptr)
		{
			TravelCheckStartLocations.Add(Resident->ResidentId, Resident->GetActorLocation());
		}
	}
	Simulation->AdvanceSimulationMinutes(1); // 08:00, issue real AI movement through the StateTree behavior path.
	FTimerHandle TimerHandle;
	GetWorldTimerManager().SetTimer(
		TimerHandle, this, &AWorldDirectorFixtureBootstrap::FinishAutomatedTravelCheck, 10.0f, false);
}

void AWorldDirectorFixtureBootstrap::FinishAutomatedTravelCheck()
{
	int32 MovedResidents = 0;
	int32 RunningStateTrees = 0;
	if (CompiledTown != nullptr)
	{
		for (const AWorldDirectorResidentActor* Resident : CompiledTown->SpawnedResidents)
		{
			if (Resident == nullptr)
			{
				continue;
			}
			if (const FVector* Start = TravelCheckStartLocations.Find(Resident->ResidentId))
			{
				MovedResidents += FVector::Dist2D(*Start, Resident->GetActorLocation()) >= 150.0f;
			}
			RunningStateTrees += Resident->StateTreeComponent != nullptr && Resident->StateTreeComponent->IsRunning();
		}
	}
	const int32 Population = CompiledTown ? CompiledTown->SpawnedResidents.Num() : 0;
	const bool bPassed = Population == 24 && MovedResidents >= 20 && RunningStateTrees == Population;
	UE_LOG(LogWorldDirector, Display,
		TEXT("WORLD_DIRECTOR_TRAVEL_EVIDENCE residents=%d movedAtLeast150cm=%d runningStateTrees=%d"),
		Population, MovedResidents, RunningStateTrees);
	UE_LOG(LogWorldDirector, Display, TEXT("WORLD_DIRECTOR_TRAVEL_RESULT=%s"),
		bPassed ? TEXT("PASS") : TEXT("FAIL"));
	FGenericPlatformMisc::RequestExitWithStatus(false, bPassed ? 0 : 4);
}

void AWorldDirectorFixtureBootstrap::RunAutomatedVisualCapture()
{
	if (UTownSimulationSubsystem* Simulation = GetWorld()
		? GetWorld()->GetSubsystem<UTownSimulationSubsystem>() : nullptr)
	{
		const int32 Noon = 12 * 60;
		Simulation->AdvanceSimulationMinutes((Noon - Simulation->GetMinuteOfDay() + 1440) % 1440);
	}
	const FString ScreenshotPath = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("Screenshots/Mac/WorldDirectorPhase4.png"));
	FScreenshotRequest::RequestScreenshot(ScreenshotPath, false, false);
	UE_LOG(LogWorldDirector, Display, TEXT("WORLD_DIRECTOR_VISUAL_CAPTURE_REQUESTED path=%s"), *ScreenshotPath);
	FTimerHandle TimerHandle;
	GetWorldTimerManager().SetTimer(
		TimerHandle, this, &AWorldDirectorFixtureBootstrap::FinishAutomatedVisualCapture, 2.0f, false);
}

void AWorldDirectorFixtureBootstrap::FinishAutomatedVisualCapture()
{
	FGenericPlatformMisc::RequestExitWithStatus(false, 0);
}
