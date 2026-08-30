#include "WorldDirectorTownActors.h"
#include "WorldDirectorRuntime.h"

#include "Components/CapsuleComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
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
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
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
#include "NavMesh/RecastNavMesh.h"
#include "SmartObjectComponent.h"
#include "StateTree.h"
#include "Interfaces/IPluginManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/App.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "UnrealClient.h"
#include "WorldDirectorDialogueWidget.h"
#include "WorldDirectorCreateWorldWidget.h"
#include "WorldDirectorLandingWidget.h"
#include "WorldDirectorLoadingWidget.h"
#include "WorldDirectorMapWidget.h"
#include "WorldDirectorGenerationDiagnosticsWidget.h"
#include "WorldDirectorInspectionWidget.h"
#include "WorldDirectorJson.h"
#include "WorldDirectorPhysicalGenerator.h"
#include "WorldDirectorSubsystems.h"
#include "WorldEnvironmentProfile.h"

namespace
{
FString WorldDirectorRunsDirectory()
{
	return FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WorldRuns")));
}

bool IsWorldDirectorRecipePath(const FString& Path)
{
	const FString NormalizedPath = FPaths::ConvertRelativePathToFull(Path);
	FString RunsPrefix = WorldDirectorRunsDirectory();
	if (!RunsPrefix.EndsWith(TEXT("/")))
	{
		RunsPrefix += TEXT("/");
	}
	return FPaths::GetCleanFilename(NormalizedPath) == TEXT("06-resolved-world-v3.json") &&
		NormalizedPath.StartsWith(RunsPrefix, ESearchCase::IgnoreCase);
}

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
	DoorMesh->SetCanEverAffectNavigation(false);
	SetDoorOpen(true);
}

void AWorldDirectorDoorActor::NotifyActorOnClicked(const FKey ButtonPressed)
{
	Super::NotifyActorOnClicked(ButtonPressed);
	if (ButtonPressed == EKeys::LeftMouseButton)
	{
		ToggleDoor();
	}
}

void AWorldDirectorDoorActor::SetDoorOpen(const bool bOpen)
{
	bIsOpen = bOpen;
	if (DoorMesh != nullptr)
	{
		DoorMesh->SetRelativeRotation(FRotator(0.0, bOpen ? 90.0 : 0.0, 0.0));
		if (bOpen)
		{
			// Keep an open door visible to the controller's visibility trace so it
			// remains a usable click target without blocking player movement.
			DoorMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			DoorMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
			DoorMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		}
		else
		{
			DoorMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
			DoorMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		}
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
	HorizonTerrainMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GeneratedHorizonTerrain"));
	HorizonTerrainMesh->SetupAttachment(GetRootComponent());
	HorizonTerrainMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HorizonTerrainMesh->SetCanEverAffectNavigation(false);
	RouteMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GeneratedRoutes"));
	RouteMesh->SetupAttachment(GetRootComponent());
	RouteMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RouteMesh->SetCanEverAffectNavigation(false);
	RouteMesh->SetCastShadow(false);
	PavingMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GeneratedPaving"));
	PavingMesh->SetupAttachment(GetRootComponent());
	PavingMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PavingMesh->SetCanEverAffectNavigation(false);
	PavingMesh->SetCastShadow(false);
	FarmMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GeneratedFarmParcels"));
	FarmMesh->SetupAttachment(GetRootComponent());
	FarmMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FarmMesh->SetCanEverAffectNavigation(false);
	FarmMesh->SetCastShadow(false);
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
	const FSoftObjectPath& NormalPath,
	const float Roughness = 0.88f,
	const float NormalPower = 1.0f)
{
	UMaterialInterface* Parent = Profile ? Cast<UMaterialInterface>(Profile->OpaqueMasterMaterial.TryLoad()) : nullptr;
	UTexture2D* BaseColor = Cast<UTexture2D>(BaseColorPath.TryLoad());
	UTexture2D* Normal = Cast<UTexture2D>(NormalPath.TryLoad());
	if (Parent == nullptr || BaseColor == nullptr || Normal == nullptr)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Parent, Owner);
	// These names are the exposed parameters on Fantastic Village's
	// M_Master_opaque_normal. Binding the normal texture to the older opaque
	// master silently did nothing, which left every generated surface flat.
	Material->SetTextureParameterValue(TEXT("Base Color Texture"), BaseColor);
	Material->SetTextureParameterValue(TEXT("Normal Texture"), Normal);
	Material->SetVectorParameterValue(TEXT("Base Color Tint"), FLinearColor::White);
	Material->SetScalarParameterValue(TEXT("SamplingScale"), 1.0f);
	Material->SetScalarParameterValue(TEXT("Normal Power"), NormalPower);
	Material->SetScalarParameterValue(TEXT("Custom Roughness"), Roughness);
	return Material;
}

float GetTerrainHeight(
	const FWorldDirectorTerrainRecipe& Terrain,
	const int32 X,
	const int32 Y)
{
	const int32 ClampedX = FMath::Clamp(X, 0, Terrain.Resolution - 1);
	const int32 ClampedY = FMath::Clamp(Y, 0, Terrain.Resolution - 1);
	return Terrain.HeightsCentimeters[ClampedY * Terrain.Resolution + ClampedX];
}

float GetRenderedTerrainHeight(
	const FWorldDirectorTerrainRecipe& Terrain,
	const FVector2D& Position)
{
	if (Terrain.Resolution < 2 ||
		Terrain.HeightsCentimeters.Num() != Terrain.Resolution * Terrain.Resolution)
	{
		return 0.0f;
	}
	const float Grid = static_cast<float>(Terrain.Resolution - 1);
	const float GX = FMath::Clamp(
		(Position.X + Terrain.ExtentCentimeters) /
		(2.0f * Terrain.ExtentCentimeters) * Grid, 0.0f, Grid);
	const float GY = FMath::Clamp(
		(Position.Y + Terrain.ExtentCentimeters) /
		(2.0f * Terrain.ExtentCentimeters) * Grid, 0.0f, Grid);
	const int32 X0 = FMath::FloorToInt(GX);
	const int32 Y0 = FMath::FloorToInt(GY);
	const int32 X1 = FMath::Min(X0 + 1, Terrain.Resolution - 1);
	const int32 Y1 = FMath::Min(Y0 + 1, Terrain.Resolution - 1);
	const float TX = GX - X0;
	const float TY = GY - Y0;
	const float A = GetTerrainHeight(Terrain, X0, Y0);
	const float B = GetTerrainHeight(Terrain, X1, Y0);
	const float C = GetTerrainHeight(Terrain, X0, Y1);
	const float D = GetTerrainHeight(Terrain, X1, Y1);
	// The authoritative procedural terrain splits each cell along V10-V01.
	// Match that piecewise plane rather than bilinear sampling so a 2–4 cm
	// render-only overlay never drops under the actual collision triangle.
	return TX + TY <= 1.0f
		? A + TX * (B - A) + TY * (C - A)
		: D + (1.0f - TY) * (B - D) + (1.0f - TX) * (C - D);
}

FVector GetTerrainNormal(
	const FWorldDirectorTerrainRecipe& Terrain,
	const int32 X,
	const int32 Y,
	const float Step)
{
	const int32 PreviousX = FMath::Max(0, X - 1);
	const int32 NextX = FMath::Min(Terrain.Resolution - 1, X + 1);
	const int32 PreviousY = FMath::Max(0, Y - 1);
	const int32 NextY = FMath::Min(Terrain.Resolution - 1, Y + 1);
	const float DeltaX = FMath::Max(Step, (NextX - PreviousX) * Step);
	const float DeltaY = FMath::Max(Step, (NextY - PreviousY) * Step);
	const float GradientX = (GetTerrainHeight(Terrain, NextX, Y) -
		GetTerrainHeight(Terrain, PreviousX, Y)) / DeltaX;
	const float GradientY = (GetTerrainHeight(Terrain, X, NextY) -
		GetTerrainHeight(Terrain, X, PreviousY)) / DeltaY;
	return FVector(-GradientX, -GradientY, 1.0f).GetSafeNormal();
}

float DistanceToRouteSegment2D(
	const FVector2D& Point,
	const FVector2D& A,
	const FVector2D& B)
{
	const FVector2D Delta = B - A;
	const float Denominator = Delta.SizeSquared();
	if (Denominator <= KINDA_SMALL_NUMBER)
	{
		return FVector2D::Distance(Point, A);
	}
	const float T = FMath::Clamp(
		FVector2D::DotProduct(Point - A, Delta) / Denominator, 0.0f, 1.0f);
	return FVector2D::Distance(Point, A + Delta * T);
}

FProcMeshTangent GetTerrainTangent(
	const FWorldDirectorTerrainRecipe& Terrain,
	const int32 X,
	const int32 Y,
	const float Step)
{
	const int32 PreviousX = FMath::Max(0, X - 1);
	const int32 NextX = FMath::Min(Terrain.Resolution - 1, X + 1);
	const float DeltaX = FMath::Max(Step, (NextX - PreviousX) * Step);
	const float GradientX = (GetTerrainHeight(Terrain, NextX, Y) -
		GetTerrainHeight(Terrain, PreviousX, Y)) / DeltaX;
	return FProcMeshTangent(FVector(1.0f, 0.0f, GradientX).GetSafeNormal(), false);
}

FLinearColor GetTerrainBlendColor(
	const FWorldDirectorTerrainRecipe& Terrain,
	const int32 Sample)
{
	const int32 BlendBase = Sample * 4;
	if (Terrain.SurfaceBlendWeights.IsValidIndex(BlendBase + 3))
	{
		return FLinearColor(
			Terrain.SurfaceBlendWeights[BlendBase] / 255.0f,
			Terrain.SurfaceBlendWeights[BlendBase + 1] / 255.0f,
			Terrain.SurfaceBlendWeights[BlendBase + 2] / 255.0f,
			Terrain.SurfaceBlendWeights[BlendBase + 3] / 255.0f);
	}
	return FLinearColor::White;
}

FLinearColor SampleTerrainBlendColor(
	const FWorldDirectorTerrainRecipe& Terrain,
	const FVector2D& Position)
{
	if (Terrain.Resolution < 2 || Terrain.ExtentCentimeters <= 0)
	{
		return FLinearColor(0.72f, 0.0f, 0.0f, 0.28f);
	}
	const float Grid = static_cast<float>(Terrain.Resolution - 1);
	const int32 X = FMath::Clamp(FMath::RoundToInt(
		(Position.X + Terrain.ExtentCentimeters) /
		(2.0f * Terrain.ExtentCentimeters) * Grid), 0, Terrain.Resolution - 1);
	const int32 Y = FMath::Clamp(FMath::RoundToInt(
		(Position.Y + Terrain.ExtentCentimeters) /
		(2.0f * Terrain.ExtentCentimeters) * Grid), 0, Terrain.Resolution - 1);
	return GetTerrainBlendColor(Terrain, Y * Terrain.Resolution + X);
}

void BuildHorizonTerrain(
	UProceduralMeshComponent* HorizonMesh,
	const FWorldDirectorTerrainRecipe& Terrain,
	UMaterialInterface* TerrainMaterial,
	const int32 TerrainSeed)
{
	if (HorizonMesh == nullptr)
	{
		return;
	}
	HorizonMesh->ClearAllMeshSections();
	if (TerrainMaterial == nullptr || Terrain.Resolution < 2 || Terrain.ExtentCentimeters <= 0)
	{
		return;
	}

	// The playable recipe remains the authoritative 1.2 km collision surface.
	// This deliberately coarse continuation only carries the visible landform to
	// the horizon, where broad seed-dependent undulation reads better than a flat
	// skirt and costs very little to render.
	constexpr int32 HorizonResolution = 97;
	const float CoreExtent = static_cast<float>(Terrain.ExtentCentimeters);
	const float HorizonExtent = CoreExtent * 2.75f;
	const float Step = HorizonExtent * 2.0f / (HorizonResolution - 1);
	const float OuterSpan = HorizonExtent - CoreExtent;
	const float Phase = static_cast<float>(static_cast<uint32>(TerrainSeed) % 8192U);
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	TArray<float> Heights;
	const int32 VertexCount = HorizonResolution * HorizonResolution;
	Vertices.Reserve(VertexCount);
	Normals.SetNumUninitialized(VertexCount);
	UVs.Reserve(VertexCount);
	Colors.Reserve(VertexCount);
	Tangents.SetNumUninitialized(VertexCount);
	Heights.Reserve(VertexCount);

	for (int32 Y = 0; Y < HorizonResolution; ++Y)
	{
		for (int32 X = 0; X < HorizonResolution; ++X)
		{
			const float WorldX = -HorizonExtent + X * Step;
			const float WorldY = -HorizonExtent + Y * Step;
			const FVector2D Position(WorldX, WorldY);
			const FVector2D EdgePosition(
				FMath::Clamp(WorldX, -CoreExtent, CoreExtent),
				FMath::Clamp(WorldY, -CoreExtent, CoreExtent));
			const float OutsideDistance = FMath::Max(
				FMath::Max(0.0f, FMath::Abs(WorldX) - CoreExtent),
				FMath::Max(0.0f, FMath::Abs(WorldY) - CoreExtent));
			const float LinearFade = FMath::Clamp(OutsideDistance / OuterSpan, 0.0f, 1.0f);
			const float SmoothFade = LinearFade * LinearFade * (3.0f - 2.0f * LinearFade);
			const float EdgeHeight = static_cast<float>(
				FWorldDirectorPhysicalGenerator::SampleHeightCentimeters(Terrain, EdgePosition));
			const FVector2D InwardDirection = EdgePosition.GetSafeNormal();
			const FVector2D InnerPosition = EdgePosition - InwardDirection * 3200.0f;
			const float InnerHeight = static_cast<float>(
				FWorldDirectorPhysicalGenerator::SampleHeightCentimeters(Terrain, InnerPosition));
			const float EdgeContinuation = (EdgeHeight - InnerHeight) *
				FMath::Min(OutsideDistance / 3200.0f, 2.25f) * FMath::Exp(-LinearFade * 5.0f);
			const float BroadLandform =
				FMath::Sin((WorldX + Phase * 9.7f) / 39000.0f) * 640.0f +
				FMath::Sin((WorldY - Phase * 6.3f) / 57000.0f) * 470.0f +
				FMath::Sin((WorldX + WorldY + Phase * 4.1f) / 91000.0f) * 530.0f;
			const float SeamUnderlay = OutsideDistance <= KINDA_SMALL_NUMBER ? -65.0f : 0.0f;
			const float Height = EdgeHeight + EdgeContinuation + BroadLandform * SmoothFade -
				SmoothFade * 260.0f + SeamUnderlay;
			Heights.Add(Height);
			Vertices.Add(FVector(WorldX, WorldY, Height));
			UVs.Add(FVector2D(WorldX / 500.0f, WorldY / 500.0f));

			const FLinearColor EdgeBlend = SampleTerrainBlendColor(Terrain, EdgePosition);
			const float HeightRange = FMath::Max(1.0f,
				static_cast<float>(Terrain.MaximumHeightCentimeters - Terrain.MinimumHeightCentimeters));
			const float RelativeHeight = FMath::Clamp(
				(Height - Terrain.MinimumHeightCentimeters) / HeightRange, 0.0f, 1.0f);
			const float RockWeight = FMath::Lerp(0.16f, 0.48f, RelativeHeight);
			const FLinearColor DistantBlend(1.0f - RockWeight, 0.0f, 0.0f, RockWeight);
			Colors.Add(FMath::Lerp(EdgeBlend, DistantBlend, SmoothFade * 0.72f));
		}
	}

	for (int32 Y = 0; Y < HorizonResolution; ++Y)
	{
		for (int32 X = 0; X < HorizonResolution; ++X)
		{
			const int32 PreviousX = FMath::Max(0, X - 1);
			const int32 NextX = FMath::Min(HorizonResolution - 1, X + 1);
			const int32 PreviousY = FMath::Max(0, Y - 1);
			const int32 NextY = FMath::Min(HorizonResolution - 1, Y + 1);
			const float GradientX = (Heights[Y * HorizonResolution + NextX] -
				Heights[Y * HorizonResolution + PreviousX]) /
				FMath::Max(Step, (NextX - PreviousX) * Step);
			const float GradientY = (Heights[NextY * HorizonResolution + X] -
				Heights[PreviousY * HorizonResolution + X]) /
				FMath::Max(Step, (NextY - PreviousY) * Step);
			const int32 Index = Y * HorizonResolution + X;
			Normals[Index] = FVector(-GradientX, -GradientY, 1.0f).GetSafeNormal();
			Tangents[Index] = FProcMeshTangent(
				FVector(1.0f, 0.0f, GradientX).GetSafeNormal(), false);
		}
	}

	const float InnerOverlap = CoreExtent - Step * 0.9f;
	for (int32 Y = 0; Y < HorizonResolution - 1; ++Y)
	{
		for (int32 X = 0; X < HorizonResolution - 1; ++X)
		{
			const float CenterX = -HorizonExtent + (X + 0.5f) * Step;
			const float CenterY = -HorizonExtent + (Y + 0.5f) * Step;
			if (FMath::Max(FMath::Abs(CenterX), FMath::Abs(CenterY)) < InnerOverlap)
			{
				continue;
			}
			const int32 V00 = Y * HorizonResolution + X;
			const int32 V10 = V00 + 1;
			const int32 V01 = V00 + HorizonResolution;
			const int32 V11 = V01 + 1;
			Triangles.Append({V00, V01, V10, V10, V01, V11});
		}
	}
	HorizonMesh->CreateMeshSection_LinearColor(
		0, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
	HorizonMesh->SetMaterial(0, TerrainMaterial);
	HorizonMesh->UpdateBounds();
	HorizonMesh->MarkRenderStateDirty();
	UE_LOG(LogWorldDirector, Display,
		TEXT("WORLD_DIRECTOR_HORIZON_TERRAIN vertices=%d triangles=%d playableExtentCm=%.0f visualExtentCm=%.0f collision=0 navigation=0"),
		Vertices.Num(), Triangles.Num() / 3, CoreExtent, HorizonExtent);
}

void AddRibbonPolyline(
	TArray<FVector>& Vertices,
	TArray<int32>& Triangles,
	TArray<FVector>& Normals,
	TArray<FVector2D>& UVs,
	TArray<FLinearColor>& Colors,
	TArray<FProcMeshTangent>& Tangents,
	const TArray<FVector>& SourcePoints,
	const float HalfWidth,
	const float TextureRepeatCentimeters,
	const float VerticalOffset)
{
	TArray<FVector> Points;
	for (const FVector& Point : SourcePoints)
	{
		if (Points.IsEmpty() || !Point.Equals(Points.Last(), 1.0f))
		{
			Points.Add(Point + FVector(0.0f, 0.0f, VerticalOffset));
		}
	}
	if (Points.Num() < 2 || HalfWidth <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const int32 Base = Vertices.Num();
	float DistanceAlong = 0.0f;
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		if (Index > 0)
		{
			DistanceAlong += FVector::Distance(Points[Index - 1], Points[Index]);
		}
		const FVector Incoming = Index > 0
			? (Points[Index] - Points[Index - 1]).GetSafeNormal() : FVector::ZeroVector;
		const FVector Outgoing = Index + 1 < Points.Num()
			? (Points[Index + 1] - Points[Index]).GetSafeNormal() : FVector::ZeroVector;
		FVector PathTangent = (Incoming + Outgoing).GetSafeNormal();
		if (PathTangent.IsNearlyZero())
		{
			PathTangent = Index + 1 < Points.Num() ? Outgoing : Incoming;
		}
		const FVector Incoming2D = Index > 0
			? (Points[Index] - Points[Index - 1]).GetSafeNormal2D() : FVector::ZeroVector;
		const FVector Outgoing2D = Index + 1 < Points.Num()
			? (Points[Index + 1] - Points[Index]).GetSafeNormal2D() : FVector::ZeroVector;
		const FVector SideIn(-Incoming2D.Y, Incoming2D.X, 0.0f);
		const FVector SideOut(-Outgoing2D.Y, Outgoing2D.X, 0.0f);
		FVector Miter = (SideIn + SideOut).GetSafeNormal2D();
		if (Miter.IsNearlyZero())
		{
			Miter = !SideOut.IsNearlyZero() ? SideOut : SideIn;
		}
		const FVector ReferenceSide = !SideOut.IsNearlyZero() ? SideOut : SideIn;
		const float MiterDenominator = FMath::Max(0.55f, FMath::Abs(FVector::DotProduct(Miter, ReferenceSide)));
		const FVector Offset = Miter * FMath::Min(HalfWidth / MiterDenominator, HalfWidth * 1.8f);
		const FVector SurfaceNormal = FVector::CrossProduct(PathTangent, Miter).GetSafeNormal();
		const FVector SafeNormal = SurfaceNormal.Z >= 0.0f ? SurfaceNormal : -SurfaceNormal;
		const float U = DistanceAlong / FMath::Max(1.0f, TextureRepeatCentimeters);

		Vertices.Add(Points[Index] - Offset);
		Vertices.Add(Points[Index] + Offset);
		Normals.Append({SafeNormal, SafeNormal});
		UVs.Append({FVector2D(U, 0.0f), FVector2D(U, 1.0f)});
		Colors.Append({FLinearColor::White, FLinearColor::White});
		Tangents.Append({FProcMeshTangent(PathTangent, false), FProcMeshTangent(PathTangent, false)});
		if (Index > 0)
		{
			const int32 PreviousPair = Base + (Index - 1) * 2;
			const int32 CurrentPair = Base + Index * 2;
			Triangles.Append({PreviousPair, PreviousPair + 1, CurrentPair,
				CurrentPair, PreviousPair + 1, CurrentPair + 1});
		}
	}
}

void AddFeatheredRoutePolyline(
	TArray<FVector>& Vertices,
	TArray<int32>& Triangles,
	TArray<FVector>& Normals,
	TArray<FVector2D>& UVs,
	TArray<FLinearColor>& Colors,
	TArray<FProcMeshTangent>& Tangents,
	const FWorldDirectorTerrainRecipe& Terrain,
	const TArray<FVector>& SourcePoints,
	const float HalfWidth,
	const float VerticalOffset)
{
	TArray<FVector> UniquePoints;
	for (const FVector& Point : SourcePoints)
	{
		if (UniquePoints.IsEmpty() || !Point.Equals(UniquePoints.Last(), 1.0f))
		{
			UniquePoints.Add(Point);
		}
	}
	if (UniquePoints.Num() < 2 || HalfWidth <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	// Physical control points describe the corridor, not render tessellation.
	// Subdivide long chords so every road quad follows the same piecewise terrain
	// triangles used by collision; otherwise low offsets visibly clip into hills.
	constexpr float MaximumRenderSegmentLength = 180.0f;
	TArray<FVector> Points;
	Points.Add(UniquePoints[0] + FVector(0.0f, 0.0f, VerticalOffset));
	for (int32 Index = 1; Index < UniquePoints.Num(); ++Index)
	{
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(
			FVector2D::Distance(FVector2D(UniquePoints[Index - 1]),
				FVector2D(UniquePoints[Index])) / MaximumRenderSegmentLength));
		for (int32 StepIndex = 1; StepIndex <= Steps; ++StepIndex)
		{
			Points.Add(FMath::Lerp(
				UniquePoints[Index - 1], UniquePoints[Index],
				static_cast<float>(StepIndex) / Steps) + FVector(0.0f, 0.0f, VerticalOffset));
		}
	}

	// Seven rows preserve the authored road width while giving its shoulders a
	// narrow grass-to-gravel transition. This remains render-only: collision and
	// navigation continue to come from the authoritative terrain mesh.
	static const float CrossSection[] = {-1.32f, -1.0f, -0.72f, 0.0f, 0.72f, 1.0f, 1.32f};
	static const FLinearColor CrossSectionColor[] = {
		FLinearColor(1.0f, 0.0f, 0.0f, 0.0f),
		FLinearColor(0.42f, 0.58f, 0.0f, 0.0f),
		FLinearColor(0.06f, 0.94f, 0.0f, 0.0f),
		FLinearColor(0.0f, 1.0f, 0.0f, 0.0f),
		FLinearColor(0.06f, 0.94f, 0.0f, 0.0f),
		FLinearColor(0.42f, 0.58f, 0.0f, 0.0f),
		FLinearColor(1.0f, 0.0f, 0.0f, 0.0f)};
	constexpr int32 RowCount = UE_ARRAY_COUNT(CrossSection);
	const int32 Base = Vertices.Num();
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		const FVector Incoming = Index > 0
			? (Points[Index] - Points[Index - 1]).GetSafeNormal2D() : FVector::ZeroVector;
		const FVector Outgoing = Index + 1 < Points.Num()
			? (Points[Index + 1] - Points[Index]).GetSafeNormal2D() : FVector::ZeroVector;
		FVector PathTangent = (Incoming + Outgoing).GetSafeNormal2D();
		if (PathTangent.IsNearlyZero())
		{
			PathTangent = !Outgoing.IsNearlyZero() ? Outgoing : Incoming;
		}
		const FVector SideIn(-Incoming.Y, Incoming.X, 0.0f);
		const FVector SideOut(-Outgoing.Y, Outgoing.X, 0.0f);
		FVector Miter = (SideIn + SideOut).GetSafeNormal2D();
		if (Miter.IsNearlyZero())
		{
			Miter = !SideOut.IsNearlyZero() ? SideOut : SideIn;
		}
		const FVector ReferenceSide = !SideOut.IsNearlyZero() ? SideOut : SideIn;
		const float MiterDenominator = FMath::Max(
			0.68f, FMath::Abs(FVector::DotProduct(Miter, ReferenceSide)));
		const float BoundaryVariation = 1.0f + 0.055f * FMath::Sin(
			Points[Index].X * 0.0047f + Points[Index].Y * 0.0031f + Index * 1.73f);
		for (int32 Row = 0; Row < RowCount; ++Row)
		{
			const float MiterDistance = FMath::Clamp(
				CrossSection[Row] * HalfWidth * BoundaryVariation / MiterDenominator,
				-HalfWidth * 1.45f, HalfWidth * 1.45f);
			FVector Vertex = Points[Index] + Miter * MiterDistance;
			const bool bOuterShoulder = FMath::Abs(CrossSection[Row]) > 1.01f;
			Vertex.Z = GetRenderedTerrainHeight(
				Terrain, FVector2D(Vertex)) + VerticalOffset + (bOuterShoulder ? 0.5f : 1.5f);
			Vertices.Add(Vertex);
			Normals.Add(FVector::UpVector);
			UVs.Add(FVector2D(Vertex.X / 500.0f, Vertex.Y / 500.0f));
			Colors.Add(CrossSectionColor[Row]);
			Tangents.Add(FProcMeshTangent(PathTangent, false));
		}
		if (Index > 0)
		{
			const int32 Previous = Base + (Index - 1) * RowCount;
			const int32 Current = Base + Index * RowCount;
			for (int32 Row = 0; Row < RowCount - 1; ++Row)
			{
				Triangles.Append({Previous + Row, Previous + Row + 1, Current + Row,
					Current + Row, Previous + Row + 1, Current + Row + 1});
			}
		}
	}
}

void AddRouteJunction(
	TArray<FVector>& Vertices,
	TArray<int32>& Triangles,
	TArray<FVector>& Normals,
	TArray<FVector2D>& UVs,
	TArray<FLinearColor>& Colors,
	TArray<FProcMeshTangent>& Tangents,
	const FWorldDirectorTerrainRecipe& Terrain,
	const FVector2D& Center,
	const float Radius)
{
	constexpr int32 SegmentCount = 16;
	const int32 Base = Vertices.Num();
	const float CenterHeight = GetRenderedTerrainHeight(Terrain, Center) + 3.0f;
	Vertices.Add(FVector(Center, CenterHeight));
	Normals.Add(FVector::UpVector);
	UVs.Add(Center / 500.0f);
	Colors.Add(FLinearColor(0.0f, 1.0f, 0.0f, 0.0f));
	Tangents.Add(FProcMeshTangent(FVector::ForwardVector, false));
	for (int32 Ring = 0; Ring < 2; ++Ring)
	{
		const float RingRadius = Radius * (Ring == 0 ? 1.0f : 1.22f);
		const FLinearColor RingColor = Ring == 0
			? FLinearColor(0.0f, 1.0f, 0.0f, 0.0f)
			: FLinearColor(0.34f, 0.66f, 0.0f, 0.0f);
		for (int32 Segment = 0; Segment < SegmentCount; ++Segment)
		{
			const float Angle = 2.0f * PI * Segment / SegmentCount;
			const FVector2D Position = Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * RingRadius;
			const float Height = GetRenderedTerrainHeight(
				Terrain, Position) + (Ring == 0 ? 3.0f : 2.0f);
			Vertices.Add(FVector(Position, Height));
			Normals.Add(FVector::UpVector);
			UVs.Add(Position / 500.0f);
			Colors.Add(RingColor);
			Tangents.Add(FProcMeshTangent(FVector::ForwardVector, false));
		}
	}
	const int32 InnerStart = Base + 1;
	const int32 OuterStart = InnerStart + SegmentCount;
	for (int32 Segment = 0; Segment < SegmentCount; ++Segment)
	{
		const int32 InnerCurrent = InnerStart + Segment;
		const int32 InnerNext = InnerStart + (Segment + 1) % SegmentCount;
		const int32 OuterCurrent = OuterStart + Segment;
		const int32 OuterNext = OuterStart + (Segment + 1) % SegmentCount;
		Triangles.Append({Base, InnerNext, InnerCurrent});
		Triangles.Append({InnerCurrent, InnerNext, OuterCurrent,
			InnerNext, OuterNext, OuterCurrent});
	}
}

void AddTerrainConformingTriangle(
	TArray<FVector>& Vertices,
	TArray<int32>& Triangles,
	TArray<FVector>& Normals,
	TArray<FVector2D>& UVs,
	TArray<FLinearColor>& Colors,
	TArray<FProcMeshTangent>& Tangents,
	const FWorldDirectorTerrainRecipe& Terrain,
	const FVector2D& P0,
	const FVector2D& P1,
	const FVector2D& P2,
	const FVector2D& UV0,
	const FVector2D& UV1,
	const FVector2D& UV2,
	const FLinearColor& C0,
	const FLinearColor& C1,
	const FLinearColor& C2,
	const float Offset0,
	const float Offset1,
	const float Offset2,
	const FVector& Tangent)
{
	constexpr float MaximumEdgeLength = 180.0f;
	const float LongestEdge = FMath::Max3(
		FVector2D::Distance(P0, P1),
		FVector2D::Distance(P1, P2),
		FVector2D::Distance(P2, P0));
	const int32 Steps = FMath::Clamp(
		FMath::CeilToInt(LongestEdge / MaximumEdgeLength), 1, 32);
	TArray<TArray<int32>> GridIndices;
	GridIndices.SetNum(Steps + 1);
	for (int32 Row = 0; Row <= Steps; ++Row)
	{
		GridIndices[Row].SetNum(Steps - Row + 1);
		for (int32 Column = 0; Column <= Steps - Row; ++Column)
		{
			const float Weight1 = static_cast<float>(Row) / Steps;
			const float Weight2 = static_cast<float>(Column) / Steps;
			const float Weight0 = 1.0f - Weight1 - Weight2;
			const FVector2D Position = P0 * Weight0 + P1 * Weight1 + P2 * Weight2;
			const float VerticalOffset =
				Offset0 * Weight0 + Offset1 * Weight1 + Offset2 * Weight2;
			GridIndices[Row][Column] = Vertices.Num();
			Vertices.Add(FVector(
				Position, GetRenderedTerrainHeight(Terrain, Position) + VerticalOffset));
			Normals.Add(FVector::UpVector);
			UVs.Add(UV0 * Weight0 + UV1 * Weight1 + UV2 * Weight2);
			Colors.Add(C0 * Weight0 + C1 * Weight1 + C2 * Weight2);
			Tangents.Add(FProcMeshTangent(Tangent, false));
		}
	}
	for (int32 Row = 0; Row < Steps; ++Row)
	{
		for (int32 Column = 0; Column < Steps - Row; ++Column)
		{
			const int32 A = GridIndices[Row][Column];
			const int32 B = GridIndices[Row + 1][Column];
			const int32 C = GridIndices[Row][Column + 1];
			Triangles.Append({A, B, C});
			if (Column < Steps - Row - 1)
			{
				const int32 D = GridIndices[Row + 1][Column + 1];
				Triangles.Append({B, D, C});
			}
		}
	}
}

void AddCourtyard(
	TArray<FVector>& Vertices,
	TArray<int32>& Triangles,
	TArray<FVector>& Normals,
	TArray<FVector2D>& UVs,
	TArray<FLinearColor>& Colors,
	TArray<FProcMeshTangent>& Tangents,
	const FWorldDirectorTerrainRecipe& Terrain,
	const FResolvedLocationPlan& Location,
	const bool bLandmark)
{
	const FVector2D BuildingCenter(Location.Transform.GetLocation());
	FVector2D Outward = (FVector2D(Location.EntranceTransform.GetLocation()) - BuildingCenter).GetSafeNormal();
	if (Outward.IsNearlyZero())
	{
		const FVector Forward = Location.Transform.GetRotation().RotateVector(FVector(0.0f, -1.0f, 0.0f));
		Outward = FVector2D(Forward).GetSafeNormal();
	}
	// Across and Outward form a right-handed basis so the fan winding faces up.
	const FVector2D Across(Outward.Y, -Outward.X);
	const float HalfAcross = bLandmark
		? FMath::Max(1500.0f, Location.FootprintSize.X * 0.5f + 560.0f)
		: Location.FootprintSize.X * 0.5f + 320.0f;
	const float HalfAlong = bLandmark
		? FMath::Max(1750.0f, Location.FootprintSize.Y * 0.5f + 920.0f)
		: Location.FootprintSize.Y * 0.5f + 420.0f;
	const FVector2D Center = BuildingCenter + Outward * (bLandmark ? 460.0f : 130.0f);
	const float Chamfer = FMath::Min(420.0f, FMath::Min(HalfAcross, HalfAlong) * 0.24f);
	const FVector2D OuterPerimeter[] = {
		FVector2D(-HalfAcross + Chamfer, -HalfAlong),
		FVector2D(HalfAcross - Chamfer, -HalfAlong),
		FVector2D(HalfAcross, -HalfAlong + Chamfer),
		FVector2D(HalfAcross, HalfAlong - Chamfer),
		FVector2D(HalfAcross - Chamfer, HalfAlong),
		FVector2D(-HalfAcross + Chamfer, HalfAlong),
		FVector2D(-HalfAcross, HalfAlong - Chamfer),
		FVector2D(-HalfAcross, -HalfAlong + Chamfer)};
	constexpr int32 PerimeterCount = UE_ARRAY_COUNT(OuterPerimeter);
	const float EdgeFeather = bLandmark ? 220.0f : 150.0f;
	const FVector2D InnerScale(
		FMath::Max(0.1f, (HalfAcross - EdgeFeather) / HalfAcross),
		FMath::Max(0.1f, (HalfAlong - EdgeFeather) / HalfAlong));
	TArray<FVector2D> InnerPositions;
	TArray<FVector2D> OuterPositions;
	InnerPositions.Reserve(PerimeterCount);
	OuterPositions.Reserve(PerimeterCount);
	for (const FVector2D& OuterLocal : OuterPerimeter)
	{
		const FVector2D Local(OuterLocal.X * InnerScale.X, OuterLocal.Y * InnerScale.Y);
		InnerPositions.Add(Center + Across * Local.X + Outward * Local.Y);
	}
	for (const FVector2D& Local : OuterPerimeter)
	{
		OuterPositions.Add(Center + Across * Local.X + Outward * Local.Y);
	}
	const FVector SurfaceTangent(Across, 0.0f);
	// Inner paving sits just above the route overlay so roads visually terminate
	// at the court. Every triangle is independently terrain-conforming; this
	// avoids the long fan chords that clipped into graded plot shoulders.
	for (int32 Index = 0; Index < PerimeterCount; ++Index)
	{
		const int32 Next = (Index + 1) % PerimeterCount;
		AddTerrainConformingTriangle(
			Vertices, Triangles, Normals, UVs, Colors, Tangents, Terrain,
			Center, InnerPositions[Next], InnerPositions[Index],
			Center / 500.0f, InnerPositions[Next] / 500.0f, InnerPositions[Index] / 500.0f,
			FLinearColor::White, FLinearColor::White, FLinearColor::White,
			5.0f, 5.0f, 5.0f, SurfaceTangent);
		AddTerrainConformingTriangle(
			Vertices, Triangles, Normals, UVs, Colors, Tangents, Terrain,
			InnerPositions[Index], InnerPositions[Next], OuterPositions[Index],
			InnerPositions[Index] / 500.0f, InnerPositions[Next] / 500.0f,
			OuterPositions[Index] / 500.0f,
			FLinearColor::White, FLinearColor::White, FLinearColor::Black,
			5.0f, 5.0f, 4.0f, SurfaceTangent);
		AddTerrainConformingTriangle(
			Vertices, Triangles, Normals, UVs, Colors, Tangents, Terrain,
			InnerPositions[Next], OuterPositions[Next], OuterPositions[Index],
			InnerPositions[Next] / 500.0f, OuterPositions[Next] / 500.0f,
			OuterPositions[Index] / 500.0f,
			FLinearColor::White, FLinearColor::Black, FLinearColor::Black,
			5.0f, 4.0f, 4.0f, SurfaceTangent);
	}
}

void AddFarmParcelSurface(
	TArray<FVector>& Vertices,
	TArray<int32>& Triangles,
	TArray<FVector>& Normals,
	TArray<FVector2D>& UVs,
	TArray<FLinearColor>& Colors,
	TArray<FProcMeshTangent>& Tangents,
	const FWorldDirectorTerrainRecipe& Terrain,
	const FWorldDirectorFarmParcel& Parcel)
{
	if (Parcel.BoundaryPoints.Num() < 3)
	{
		return;
	}
	const float YawRadians = FMath::DegreesToRadians(Parcel.YawDegrees);
	const float CosYaw = FMath::Cos(YawRadians);
	const float SinYaw = FMath::Sin(YawRadians);
	const auto ParcelUV = [&](const FVector2D& Position)
	{
		const FVector2D Delta = Position - Parcel.Center;
		return FVector2D(
			Delta.X * CosYaw + Delta.Y * SinYaw,
			-Delta.X * SinYaw + Delta.Y * CosYaw) / 500.0f;
	};
	TArray<FVector2D> InnerBoundary;
	InnerBoundary.Reserve(Parcel.BoundaryPoints.Num());
	for (const FVector2D& BoundaryPoint : Parcel.BoundaryPoints)
	{
		InnerBoundary.Add(FMath::Lerp(Parcel.Center, BoundaryPoint, 0.88f));
	}
	const FLinearColor FarmColor(0.0f, 0.0f, 1.0f, 0.0f);
	const FLinearColor GrassColor(1.0f, 0.0f, 0.0f, 0.0f);
	const FVector SurfaceTangent(FMath::Cos(YawRadians), FMath::Sin(YawRadians), 0.0f);
	for (int32 Index = 0; Index < Parcel.BoundaryPoints.Num(); ++Index)
	{
		const int32 Next = (Index + 1) % Parcel.BoundaryPoints.Num();
		AddTerrainConformingTriangle(
			Vertices, Triangles, Normals, UVs, Colors, Tangents, Terrain,
			Parcel.Center, InnerBoundary[Next], InnerBoundary[Index],
			ParcelUV(Parcel.Center), ParcelUV(InnerBoundary[Next]), ParcelUV(InnerBoundary[Index]),
			FarmColor, FarmColor, FarmColor, 3.5f, 3.5f, 3.5f, SurfaceTangent);
		AddTerrainConformingTriangle(
			Vertices, Triangles, Normals, UVs, Colors, Tangents, Terrain,
			InnerBoundary[Index], InnerBoundary[Next], Parcel.BoundaryPoints[Index],
			ParcelUV(InnerBoundary[Index]), ParcelUV(InnerBoundary[Next]),
			ParcelUV(Parcel.BoundaryPoints[Index]),
			FarmColor, FarmColor, GrassColor, 3.5f, 3.5f, 2.0f, SurfaceTangent);
		AddTerrainConformingTriangle(
			Vertices, Triangles, Normals, UVs, Colors, Tangents, Terrain,
			InnerBoundary[Next], Parcel.BoundaryPoints[Next], Parcel.BoundaryPoints[Index],
			ParcelUV(InnerBoundary[Next]), ParcelUV(Parcel.BoundaryPoints[Next]),
			ParcelUV(Parcel.BoundaryPoints[Index]),
			FarmColor, GrassColor, GrassColor, 3.5f, 2.0f, 2.0f, SurfaceTangent);
	}
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
	TerrainMesh->ClearAllMeshSections();
	if (HorizonTerrainMesh != nullptr)
	{
		HorizonTerrainMesh->ClearAllMeshSections();
	}
	const float Step = 2.0f * Plan.Terrain.ExtentCentimeters / (Resolution - 1);
	TArray<FVector> SampleNormals;
	TArray<FProcMeshTangent> SampleTangents;
	SampleNormals.SetNumUninitialized(Plan.Terrain.HeightsCentimeters.Num());
	SampleTangents.SetNumUninitialized(Plan.Terrain.HeightsCentimeters.Num());
	for (int32 Y = 0; Y < Resolution; ++Y)
	{
		for (int32 X = 0; X < Resolution; ++X)
		{
			const int32 Sample = Y * Resolution + X;
			SampleNormals[Sample] = GetTerrainNormal(Plan.Terrain, X, Y, Step);
			SampleTangents[Sample] = GetTerrainTangent(Plan.Terrain, X, Y, Step);
		}
	}
	const int32 SampleCount = Plan.Terrain.HeightsCentimeters.Num();
	const bool bHasCompleteBlendMask = Plan.Version >= 3 &&
		Plan.Terrain.SurfaceBlendWeights.Num() == SampleCount * 4;
	UMaterialInterface* TerrainBlend = bHasCompleteBlendMask
		? Cast<UMaterialInterface>(Profile->TerrainBlendMaterial.TryLoad()) : nullptr;
	if (bHasCompleteBlendMask && TerrainBlend == nullptr)
	{
		OutReport.AddError(TEXT("generator.terrain_blend_material_missing"),
			TEXT("environmentProfile.terrainBlendMaterial"),
			TEXT("The V3 four-layer terrain material could not be loaded."));
		return false;
	}

	if (TerrainBlend != nullptr)
	{
		// V3 is a single shared-vertex terrain section. Vertex color RGBA carries
		// grass, gravel, farm, and rock weights into the project-owned blend
		// material, eliminating grid-shaped material boundaries while also keeping
		// the lakebed and navigation collision continuous beneath water.
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector2D> UVs;
		TArray<FLinearColor> Colors;
		Vertices.Reserve(SampleCount);
		Triangles.Reserve((Resolution - 1) * (Resolution - 1) * 6);
		UVs.Reserve(SampleCount);
		Colors.Reserve(SampleCount);
		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				const int32 Sample = Y * Resolution + X;
				const float WorldX = -Plan.Terrain.ExtentCentimeters + X * Step;
				const float WorldY = -Plan.Terrain.ExtentCentimeters + Y * Step;
				Vertices.Add(FVector(WorldX, WorldY, Plan.Terrain.HeightsCentimeters[Sample]));
				UVs.Add(FVector2D(WorldX / 500.0f, WorldY / 500.0f));
				Colors.Add(GetTerrainBlendColor(Plan.Terrain, Sample));
			}
		}
		for (int32 Y = 0; Y < Resolution - 1; ++Y)
		{
			for (int32 X = 0; X < Resolution - 1; ++X)
			{
				const int32 V00 = Y * Resolution + X;
				const int32 V10 = V00 + 1;
				const int32 V01 = V00 + Resolution;
				const int32 V11 = V01 + 1;
				// UProceduralMeshComponent's collision cook flips triangle normals.
				// Keep this collision-compatible winding so Recast receives an
				// upward-facing walkable surface; the explicit vertex normals still
				// provide upward-facing render lighting.
				Triangles.Append({V00, V01, V10, V10, V01, V11});
			}
		}
		TerrainMesh->CreateMeshSection_LinearColor(
			0, Vertices, Triangles, SampleNormals, UVs, Colors, SampleTangents, true);
		TerrainMesh->SetMaterial(0, TerrainBlend);
		BuildHorizonTerrain(HorizonTerrainMesh, Plan.Terrain, TerrainBlend,
			Plan.StageSeeds.FindRef(TEXT("terrain")));
	}
	else
	{
		// V2 compatibility path: retain categorical material sections for saved
		// recipes that predate the four-channel blend mask.
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
					// See the V3 section above: ProceduralMesh collision cooking flips
					// this winding before exporting the triangles to navigation.
					Triangles.Append({Base, Base + 2, Base + 1, Base + 1, Base + 2, Base + 3});
					Normals.Append({SampleNormals[Sample], SampleNormals[Sample + 1],
						SampleNormals[Sample + Resolution], SampleNormals[Sample + Resolution + 1]});
					const float U = X0 / 500.0f;
					const float V = Y0 / 500.0f;
					UVs.Append({FVector2D(U, V), FVector2D(U + Step / 500.0f, V),
						FVector2D(U, V + Step / 500.0f), FVector2D(U + Step / 500.0f, V + Step / 500.0f)});
					Colors.Append({GetTerrainBlendColor(Plan.Terrain, Sample),
						GetTerrainBlendColor(Plan.Terrain, Sample + 1),
						GetTerrainBlendColor(Plan.Terrain, Sample + Resolution),
						GetTerrainBlendColor(Plan.Terrain, Sample + Resolution + 1)});
					Tangents.Append({SampleTangents[Sample], SampleTangents[Sample + 1],
						SampleTangents[Sample + Resolution], SampleTangents[Sample + Resolution + 1]});
				}
			}
			if (!Vertices.IsEmpty())
			{
				TerrainMesh->CreateMeshSection_LinearColor(
					SurfaceIndex, Vertices, Triangles, Normals, UVs, Colors, Tangents, true);
			}
		}
		UMaterialInstanceDynamic* Grass = MakeSurfaceMaterial(this, Profile,
			Profile->Surfaces[0].BaseColorTexture, Profile->Surfaces[0].NormalTexture, 0.93f, 1.05f);
		UMaterialInstanceDynamic* Gravel = MakeSurfaceMaterial(this, Profile,
			Profile->Surfaces[1].BaseColorTexture, Profile->Surfaces[1].NormalTexture, 0.9f, 1.15f);
		UMaterialInstanceDynamic* Paving = MakeSurfaceMaterial(this, Profile,
			Profile->Surfaces[2].BaseColorTexture, Profile->Surfaces[2].NormalTexture, 0.82f, 1.1f);
		UMaterialInstanceDynamic* Farm = MakeSurfaceMaterial(this, Profile,
			Profile->Surfaces[3].BaseColorTexture, Profile->Surfaces[3].NormalTexture, 0.95f, 1.0f);
		UMaterialInterface* Rock = Cast<UMaterialInterface>(Profile->RockMaterial.TryLoad());
		for (int32 Index = 0; Index < 5; ++Index)
		{
			UMaterialInterface* Material = Index == 0 ? Grass : Index == 1 ? Gravel : Index == 2 ? Paving : Index == 3 ? Farm : Rock;
			if (Material == nullptr)
			{
				OutReport.AddError(TEXT("generator.surface_material_missing"),
					FString::Printf(TEXT("terrain.materials[%d]"), Index),
					TEXT("StylizedVillage surface material or texture could not load."));
			}
			else
			{
				TerrainMesh->SetMaterial(Index, Material);
			}
		}
	}
	TerrainMesh->UpdateBounds();
	TerrainMesh->MarkRenderStateDirty();
	UNavigationSystemV1::UpdateComponentInNavOctree(*TerrainMesh);
	if (!OutReport.bValid)
	{
		TerrainMesh->ClearAllMeshSections();
		if (HorizonTerrainMesh != nullptr)
		{
			HorizonTerrainMesh->ClearAllMeshSections();
		}
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
	RouteMesh->ClearAllMeshSections();
	PavingMesh->ClearAllMeshSections();
	FarmMesh->ClearAllMeshSections();
	WaterMesh->ClearAllMeshSections();
	const UWorldEnvironmentProfile* Profile = UWorldEnvironmentProfile::ResolveStylizedVillage();
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	for (const FResolvedRoutePlan& Route : Plan.Routes)
	{
		AddFeatheredRoutePolyline(Vertices, Triangles, Normals, UVs, Colors, Tangents,
			Plan.Terrain, Route.ControlPoints, Route.WidthCentimeters * 0.5f, 2.0f);
	}
	TArray<FVector2D> JunctionCenters;
	TArray<float> JunctionRadii;
	auto QueueJunction = [&](const FVector2D& Center, const float Radius)
	{
		for (int32 Index = 0; Index < JunctionCenters.Num(); ++Index)
		{
			if (FVector2D::Distance(JunctionCenters[Index], Center) <=
				FMath::Max(180.0f, FMath::Min(JunctionRadii[Index], Radius) * 0.7f))
			{
				JunctionRadii[Index] = FMath::Max(JunctionRadii[Index], Radius);
				return;
			}
		}
		JunctionCenters.Add(Center);
		JunctionRadii.Add(Radius);
	};
	for (int32 RouteIndex = 0; RouteIndex < Plan.Routes.Num(); ++RouteIndex)
	{
		const FResolvedRoutePlan& Route = Plan.Routes[RouteIndex];
		if (Route.ControlPoints.IsEmpty())
		{
			continue;
		}
		auto TouchesEarlierNetwork = [&](const FVector2D& Endpoint)
		{
			for (int32 EarlierIndex = 0; EarlierIndex < RouteIndex; ++EarlierIndex)
			{
				const FResolvedRoutePlan& EarlierRoute = Plan.Routes[EarlierIndex];
				for (int32 SegmentIndex = 1;
					SegmentIndex < EarlierRoute.ControlPoints.Num(); ++SegmentIndex)
				{
					if (DistanceToRouteSegment2D(
						Endpoint,
						FVector2D(EarlierRoute.ControlPoints[SegmentIndex - 1]),
						FVector2D(EarlierRoute.ControlPoints[SegmentIndex])) <= 120.0f)
					{
						return true;
					}
				}
			}
			return false;
		};
		const float JunctionRadius = Route.WidthCentimeters * 0.62f;
		if (TouchesEarlierNetwork(FVector2D(Route.ControlPoints[0])))
		{
			QueueJunction(FVector2D(Route.ControlPoints[0]), JunctionRadius);
		}
		if (TouchesEarlierNetwork(FVector2D(Route.ControlPoints.Last())))
		{
			QueueJunction(FVector2D(Route.ControlPoints.Last()), JunctionRadius);
		}
	}
	for (int32 Index = 0; Index < JunctionCenters.Num(); ++Index)
	{
		AddRouteJunction(Vertices, Triangles, Normals, UVs, Colors, Tangents,
			Plan.Terrain, JunctionCenters[Index], JunctionRadii[Index]);
	}
	const int32 JunctionCount = JunctionCenters.Num();
	if (!Vertices.IsEmpty())
	{
		RouteMesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
		if (Profile != nullptr)
		{
			if (UMaterialInterface* TerrainBlend =
				Cast<UMaterialInterface>(Profile->TerrainBlendMaterial.TryLoad()))
			{
				RouteMesh->SetMaterial(0, TerrainBlend);
			}
		}
		RouteMesh->UpdateBounds();
		RouteMesh->MarkRenderStateDirty();
		UE_LOG(LogWorldDirector, Display,
			TEXT("WORLD_DIRECTOR_ROUTE_SURFACE routes=%d junctions=%d vertices=%d triangles=%d collision=0 navigation=0"),
			Plan.Routes.Num(), JunctionCount, Vertices.Num(), Triangles.Num() / 3);
	}

	Vertices.Reset(); Triangles.Reset(); Normals.Reset(); UVs.Reset(); Colors.Reset(); Tangents.Reset();
	int32 CourtyardCount = 0;
	for (const FResolvedLocationPlan& Location : Plan.Locations)
	{
		const bool bLandmark = Location.LocationId == Plan.LandmarkLocationId;
		if (!Location.bPavedCourtyard)
		{
			continue;
		}
		AddCourtyard(Vertices, Triangles, Normals, UVs, Colors, Tangents,
			Plan.Terrain, Location, bLandmark);
		++CourtyardCount;
	}
	if (!Vertices.IsEmpty())
	{
		PavingMesh->CreateMeshSection_LinearColor(
			0, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
		if (UMaterialInterface* Paving = Profile
			? Cast<UMaterialInterface>(Profile->PavingMaterial.TryLoad()) : nullptr)
		{
			PavingMesh->SetMaterial(0, Paving);
		}
		PavingMesh->UpdateBounds();
		PavingMesh->MarkRenderStateDirty();
		const FString PavingMaterialName = PavingMesh->GetMaterial(0)
			? PavingMesh->GetMaterial(0)->GetPathName() : TEXT("missing");
		UE_LOG(LogWorldDirector, Display,
			TEXT("WORLD_DIRECTOR_COURTYARD_COMPOSITION courtyards=%d vertices=%d triangles=%d material=%s boundsZ=(%.0f,%.0f) collision=0 navigation=0"),
			CourtyardCount, Vertices.Num(), Triangles.Num() / 3, *PavingMaterialName,
			PavingMesh->Bounds.Origin.Z - PavingMesh->Bounds.BoxExtent.Z,
			PavingMesh->Bounds.Origin.Z + PavingMesh->Bounds.BoxExtent.Z);
	}

	Vertices.Reset(); Triangles.Reset(); Normals.Reset(); UVs.Reset(); Colors.Reset(); Tangents.Reset();
	for (const FWorldDirectorFarmParcel& Parcel : Plan.Terrain.FarmParcels)
	{
		AddFarmParcelSurface(Vertices, Triangles, Normals, UVs, Colors, Tangents,
			Plan.Terrain, Parcel);
	}
	if (!Vertices.IsEmpty())
	{
		FarmMesh->CreateMeshSection_LinearColor(
			0, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
		if (UMaterialInterface* TerrainBlend = Profile
			? Cast<UMaterialInterface>(Profile->TerrainBlendMaterial.TryLoad()) : nullptr)
		{
			FarmMesh->SetMaterial(0, TerrainBlend);
		}
		FarmMesh->UpdateBounds();
		FarmMesh->MarkRenderStateDirty();
		UE_LOG(LogWorldDirector, Display,
			TEXT("WORLD_DIRECTOR_FARM_SURFACE parcels=%d vertices=%d triangles=%d collision=0 navigation=0"),
			Plan.Terrain.FarmParcels.Num(), Vertices.Num(), Triangles.Num() / 3);
	}
	if (Plan.Terrain.WaterLevelCentimeters != INDEX_NONE)
	{
		Vertices.Reset(); Triangles.Reset(); Normals.Reset(); UVs.Reset(); Colors.Reset(); Tangents.Reset();
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
				UVs.Append({FVector2D(X0 / 800.0f, Y0 / 800.0f),
					FVector2D((X0 + Step) / 800.0f, Y0 / 800.0f),
					FVector2D(X0 / 800.0f, (Y0 + Step) / 800.0f),
					FVector2D((X0 + Step) / 800.0f, (Y0 + Step) / 800.0f)});
				Colors.Append({FLinearColor::White, FLinearColor::White, FLinearColor::White, FLinearColor::White});
				Tangents.Append({FProcMeshTangent(1, 0, 0), FProcMeshTangent(1, 0, 0),
					FProcMeshTangent(1, 0, 0), FProcMeshTangent(1, 0, 0)});
			}
		}
		// The classified water cells are the authoritative shoreline. The ribbon is
		// only a fallback for an older recipe without a classified water surface;
		// drawing both would overlap coplanar triangles and shimmer.
		const float HalfWidth = Plan.Terrain.Archetype == EWorldDirectorTerrainArchetype::Coast ? 6200.0f : 520.0f;
		if (Vertices.IsEmpty() && Plan.Terrain.WaterControlPoints.Num() >= 2)
		{
			TArray<FVector> WaterPoints = Plan.Terrain.WaterControlPoints;
			for (FVector& Point : WaterPoints)
			{
				Point.Z = WaterZ;
			}
			AddRibbonPolyline(Vertices, Triangles, Normals, UVs, Colors, Tangents,
				WaterPoints, HalfWidth, 800.0f, 0.0f);
		}
		if (!Vertices.IsEmpty())
		{
			WaterMesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
		}
		if (UMaterialInterface* Water = Profile ? Cast<UMaterialInterface>(Profile->WaterMaterial.TryLoad()) : nullptr)
		{
			WaterMesh->SetMaterial(0, Water);
		}
		WaterMesh->UpdateBounds();
		WaterMesh->MarkRenderStateDirty();
	}
}

void AWorldDirectorTownActor::BuildDressing(
	const FResolvedWorldPlan& Plan,
	FValidationReport& OutReport)
{
	TMap<UStaticMesh*, TArray<FTransform>> TransformsByMesh;
	for (int32 Index = 0; Index < Plan.Dressing.Num(); ++Index)
	{
		const FWorldDirectorDressingInstance& Instance = Plan.Dressing[Index];
		UStaticMesh* Mesh = Cast<UStaticMesh>(Instance.MeshAsset.TryLoad());
		if (Mesh == nullptr)
		{
			OutReport.AddError(TEXT("generator.dressing_asset_missing"), FString::Printf(TEXT("dressing[%d]"), Index), Instance.MeshAsset.ToString());
			continue;
		}
		TransformsByMesh.FindOrAdd(Mesh).Add(Instance.Transform);
	}

	for (TPair<UStaticMesh*, TArray<FTransform>>& Pair : TransformsByMesh)
	{
		UStaticMesh* Mesh = Pair.Key;
		const FString MeshName = Mesh->GetName().ToUpper();
		int32 StartCullDistance = 28000;
		int32 EndCullDistance = 78000;
		if (MeshName.Contains(TEXT("TREE_VILLAGE")))
		{
			StartCullDistance = 52000;
			EndCullDistance = 145000;
		}
		else if (MeshName.Contains(TEXT("PLANT_")))
		{
			StartCullDistance = 9000;
			EndCullDistance = 28000;
		}
		else if (MeshName.Contains(TEXT("STONE_")) ||
			MeshName.Contains(TEXT("TREETRUNK")) || MeshName.Contains(TEXT("HAY_")))
		{
			StartCullDistance = 16000;
			EndCullDistance = 48000;
		}
		else if (MeshName.Contains(TEXT("FENCE_")))
		{
			StartCullDistance = 25000;
			EndCullDistance = 68000;
		}
		TObjectPtr<UHierarchicalInstancedStaticMeshComponent>& ComponentPtr =
			DressingInstanceComponents.FindOrAdd(Mesh);
		if (ComponentPtr == nullptr)
		{
			ComponentPtr = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
			UHierarchicalInstancedStaticMeshComponent* Component = ComponentPtr.Get();
			Component->SetStaticMesh(Mesh);
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->SetCanEverAffectNavigation(false);
			Component->SetCullDistances(StartCullDistance, EndCullDistance);
			Component->SetMobility(GetRootComponent()->Mobility);
			Component->SetCastShadow(!Mesh->GetName().Contains(TEXT("PLANT"), ESearchCase::IgnoreCase));
			Component->SetupAttachment(GetRootComponent());
			AddInstanceComponent(Component);
			Component->RegisterComponent();
		}
		UHierarchicalInstancedStaticMeshComponent* Component = ComponentPtr.Get();
		Component->AddInstances(Pair.Value, false, true, false);
		Component->BuildTreeIfOutdated(false, true);
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
	CompiledPlan = Plan;
	SourceSpecId = Plan.SourceSpecId;
	LandmarkLocationId = Plan.LandmarkLocationId;
	WorldFingerprint = Plan.WorldFingerprint;
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		OutReport.AddError(TEXT("compiler.world_missing"), TEXT("world"), TEXT("Town root is not in a UWorld."));
		return false;
	}
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	int32 NavigationBoundsCount = 0;
	for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
	{
		ANavMeshBoundsVolume* Bounds = *It;
		if (Bounds == nullptr)
		{
			continue;
		}
		++NavigationBoundsCount;
		const FBox CurrentBounds = Bounds->GetComponentsBoundingBox(true);
		const FVector CurrentSize = CurrentBounds.GetSize();
		const float DesiredXY = Plan.Terrain.ExtentCentimeters * 2.0f + 2400.0f;
		const float MaximumAbsoluteTerrainHeight = FMath::Max(
			FMath::Abs(static_cast<float>(Plan.Terrain.MinimumHeightCentimeters)),
			FMath::Abs(static_cast<float>(Plan.Terrain.MaximumHeightCentimeters)));
		// NavMeshBoundsVolume is a static brush in the playable map, so runtime
		// SetActorLocation does not reliably move it. Size Z symmetrically around
		// the map origin to cover both deep basins and high ridges instead.
		const float DesiredZ = FMath::Max(4000.0f,
			MaximumAbsoluteTerrainHeight * 2.0f + 4000.0f);
		const FVector Scale = Bounds->GetActorScale3D();
		Bounds->SetActorScale3D(Scale * FVector(
			CurrentSize.X > KINDA_SMALL_NUMBER ? FMath::Max(1.0f, DesiredXY / CurrentSize.X) : 1.0f,
			CurrentSize.Y > KINDA_SMALL_NUMBER ? FMath::Max(1.0f, DesiredXY / CurrentSize.Y) : 1.0f,
			CurrentSize.Z > KINDA_SMALL_NUMBER ? FMath::Max(1.0f, DesiredZ / CurrentSize.Z) : 1.0f));
		if (Navigation != nullptr)
		{
			Navigation->OnNavigationBoundsUpdated(Bounds);
		}
		const FBox UpdatedBounds = Bounds->GetComponentsBoundingBox(true);
		UE_LOG(LogWorldDirector, Display,
			TEXT("WORLD_DIRECTOR_NAV_BOUNDS actor=%s min=(%.0f,%.0f,%.0f) max=(%.0f,%.0f,%.0f) desiredXY=%.0f desiredZ=%.0f"),
			*Bounds->GetName(), UpdatedBounds.Min.X, UpdatedBounds.Min.Y, UpdatedBounds.Min.Z,
			UpdatedBounds.Max.X, UpdatedBounds.Max.Y, UpdatedBounds.Max.Z, DesiredXY, DesiredZ);
	}
	if (NavigationBoundsCount == 0)
	{
		// A navigation volume is required for the playable map, but rendering and
		// asset-spawn fixtures intentionally use transient worlds without one.
		// Keep construction available there and let the dedicated navigation gate
		// report the missing runtime capability.
		OutReport.AddWarning(TEXT("navigation.bounds_missing"), TEXT("navigation"),
			TEXT("The generated world has no runtime navigation bounds volume; navigation validation will fail."));
	}
	if (!BuildTerrainAndSurfaces(Plan, OutReport))
	{
		return false;
	}
	BuildRouteSurfaces(Plan);
	BuildDressing(Plan, OutReport);
	int32 CollisionTriangleCount = 0;
	for (int32 SectionIndex = 0; SectionIndex < TerrainMesh->GetNumSections(); ++SectionIndex)
	{
		if (const FProcMeshSection* Section = TerrainMesh->GetProcMeshSection(SectionIndex);
			Section != nullptr && Section->bEnableCollision)
		{
			CollisionTriangleCount += Section->ProcIndexBuffer.Num() / 3;
		}
	}
	const ARecastNavMesh* Recast = Navigation != nullptr
		? Cast<ARecastNavMesh>(Navigation->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)) : nullptr;
	const FBox TerrainBounds = TerrainMesh->Bounds.GetBox();
	UE_LOG(LogWorldDirector, Display,
		TEXT("WORLD_DIRECTOR_NAV_TERRAIN relevant=%d canAffect=%d collision=%d triangles=%d boundsMin=(%.0f,%.0f,%.0f) boundsMax=(%.0f,%.0f,%.0f) tileSize=%.0f"),
		static_cast<int32>(TerrainMesh->IsNavigationRelevant()),
		static_cast<int32>(TerrainMesh->CanEverAffectNavigation()),
		static_cast<int32>(TerrainMesh->ContainsPhysicsTriMeshData(false)), CollisionTriangleCount,
		TerrainBounds.Min.X, TerrainBounds.Min.Y, TerrainBounds.Min.Z,
		TerrainBounds.Max.X, TerrainBounds.Max.Y, TerrainBounds.Max.Z,
		Recast != nullptr ? Recast->GetTileSizeUU() : 0.0f);

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
			if (Component != nullptr && Component->GetName().Equals(TEXT("FrontDoorComponent"), ESearchCase::IgnoreCase))
			{
				VendorDoorComponent = Component;
				break;
			}
		}
		if (VendorDoorComponent == nullptr)
		{
			for (UStaticMeshComponent* Component : ShellComponents)
			{
				if (Component != nullptr &&
					Component->GetName().Contains(TEXT("door"), ESearchCase::IgnoreCase) &&
					!Component->GetName().Contains(TEXT("frame"), ESearchCase::IgnoreCase))
				{
					VendorDoorComponent = Component;
					break;
				}
			}
		}
		if (VendorDoorComponent != nullptr)
		{
			VendorDoorComponent->SetVisibility(false, true);
			VendorDoorComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
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

		FTransform DoorTransform = Location.EntranceTransform;
		if (VendorDoorComponent != nullptr)
		{
			// The certified semantic entrance is the playable/nav anchor. Some
			// vendor components place their mesh pivot inside the shell, so using
			// their world location would put the proxy behind the wall. Preserve
			// the authored facing and scale without replacing the certified
			// entrance position.
			const FTransform SourceDoorTransform = VendorDoorComponent->GetComponentTransform();
			DoorTransform.SetRotation(SourceDoorTransform.GetRotation());
			DoorTransform.SetScale3D(SourceDoorTransform.GetScale3D());
		}
		LocationActor->DoorActor = World->SpawnActor<AWorldDirectorDoorActor>(
			AWorldDirectorDoorActor::StaticClass(), DoorTransform);
		if (LocationActor->DoorActor != nullptr)
		{
			if (VendorDoorComponent != nullptr && VendorDoorComponent->GetStaticMesh() != nullptr)
			{
				LocationActor->DoorActor->DoorMesh->SetStaticMesh(VendorDoorComponent->GetStaticMesh());
				// The proxy actor owns the selected entrance transform, so clear the
				// fallback cube offset before applying the source mesh and materials.
				LocationActor->DoorActor->DoorMesh->SetRelativeTransform(FTransform::Identity);
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
	if (Navigation != nullptr)
	{
		// Registration may have occurred before the resized bounds request is
		// consumed. Refresh the final collision payload and dirty the complete
		// terrain once every runtime obstacle has been spawned.
		UNavigationSystemV1::UpdateComponentInNavOctree(*TerrainMesh);
		Navigation->AddDirtyArea(
			TerrainMesh->Bounds.GetBox(), ENavigationDirtyFlag::All,
			FName(TEXT("WorldDirectorTerrainReady")));
		UE_LOG(LogWorldDirector, Display,
			TEXT("WORLD_DIRECTOR_NAV_REBUILD_QUEUED building=%d locations=%d residents=%d"),
			static_cast<int32>(UNavigationSystemV1::IsNavigationBeingBuilt(World)),
			SpawnedLocations.Num(), SpawnedResidents.Num());
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
	UWorld* MutableWorld = const_cast<UWorld*>(World);
	UNavigationSystemV1* Navigation = MutableWorld
		? FNavigationSystem::GetCurrent<UNavigationSystemV1>(MutableWorld) : nullptr;
	if (Navigation == nullptr)
	{
		Report.AddError(TEXT("navigation.system_missing"), TEXT("navigation"), TEXT("No navigation system is available for the generated town."));
		return Report;
	}
	const ARecastNavMesh* Recast = Cast<ARecastNavMesh>(
		Navigation->GetDefaultNavDataInstance(FNavigationSystem::DontCreate));
	const FBox NavBounds = Recast != nullptr ? Recast->GetNavMeshBounds() : FBox(ForceInit);
	const FBox TerrainBounds = TerrainMesh != nullptr ? TerrainMesh->Bounds.GetBox() : FBox(ForceInit);
	UE_LOG(LogWorldDirector, Display,
		TEXT("WORLD_DIRECTOR_NAV_VALIDATE building=%d navData=%s tileSize=%.0f navMin=(%.0f,%.0f,%.0f) navMax=(%.0f,%.0f,%.0f) terrainRelevant=%d terrainCollision=%d terrainMin=(%.0f,%.0f,%.0f) terrainMax=(%.0f,%.0f,%.0f)"),
		static_cast<int32>(UNavigationSystemV1::IsNavigationBeingBuilt(MutableWorld)),
		Recast != nullptr ? *Recast->GetName() : TEXT("none"),
		Recast != nullptr ? Recast->GetTileSizeUU() : 0.0f,
		NavBounds.Min.X, NavBounds.Min.Y, NavBounds.Min.Z,
		NavBounds.Max.X, NavBounds.Max.Y, NavBounds.Max.Z,
		static_cast<int32>(TerrainMesh != nullptr && TerrainMesh->IsNavigationRelevant()),
		static_cast<int32>(TerrainMesh != nullptr && TerrainMesh->ContainsPhysicsTriMeshData(false)),
		TerrainBounds.Min.X, TerrainBounds.Min.Y, TerrainBounds.Min.Z,
		TerrainBounds.Max.X, TerrainBounds.Max.Y, TerrainBounds.Max.Z);

	TMap<FString, FVector> Entrances;
	for (const AWorldDirectorLocationActor* Location : SpawnedLocations)
	{
		if (Location != nullptr && Location->DoorActor != nullptr)
		{
			FNavLocation ProjectedEntrance;
			const bool bEntranceProjected = Navigation->ProjectPointToNavigation(
				Location->NavigationEntranceLocation, ProjectedEntrance, FVector(250.0, 250.0, 500.0));
			UE_LOG(LogWorldDirector, Display,
				TEXT("WORLD_DIRECTOR_ENTRANCE_PROBE location=%s query=(%.0f,%.0f,%.0f) projected=%d result=(%.0f,%.0f,%.0f)"),
				*Location->LocationId,
				Location->NavigationEntranceLocation.X, Location->NavigationEntranceLocation.Y,
				Location->NavigationEntranceLocation.Z, static_cast<int32>(bEntranceProjected),
				ProjectedEntrance.Location.X, ProjectedEntrance.Location.Y, ProjectedEntrance.Location.Z);
			if (bEntranceProjected)
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
						TEXT("WORLD_DIRECTOR_DOOR_PROBE location=%s axis=%d queryA=(%.0f,%.0f,%.0f) queryB=(%.0f,%.0f,%.0f) projectedA=%d projectedB=%d separation=%.1f result=REJECT"),
						*Location->LocationId, DoorAxisIndex,
						(DoorLocation - DoorAxis * 250.0).X, (DoorLocation - DoorAxis * 250.0).Y,
						(DoorLocation - DoorAxis * 250.0).Z, (DoorLocation + DoorAxis * 250.0).X,
						(DoorLocation + DoorAxis * 250.0).Y, (DoorLocation + DoorAxis * 250.0).Z,
						static_cast<int32>(bSideAProjected), static_cast<int32>(bSideBProjected),
						ProjectedSeparation);
					continue;
				}
				UNavigationPath* DoorPath = Navigation->FindPathToLocationSynchronously(
					MutableWorld, SideA.Location, SideB.Location);
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
	GetWorldTimerManager().ClearTimer(SampleWorldCompileHandle);
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
	if (LandingWidget != nullptr)
	{
		LandingWidget->RemoveFromParent();
	}
	if (LoadingWidget != nullptr)
	{
		LoadingWidget->RemoveFromParent();
	}
	if (MapWidget != nullptr)
	{
		MapWidget->RemoveFromParent();
	}
	if (GenerationDiagnosticsWidget != nullptr)
	{
		GenerationDiagnosticsWidget->RemoveFromParent();
	}
	if (AutomatedVisualCaptureCamera.IsValid())
	{
		AutomatedVisualCaptureCamera->Destroy();
		AutomatedVisualCaptureCamera.Reset();
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
			InputComponent->BindKey(EKeys::L, IE_Pressed, this, &AWorldDirectorFixtureBootstrap::OpenLandingPage);
			InputComponent->BindKey(EKeys::N, IE_Pressed, this, &AWorldDirectorFixtureBootstrap::ToggleWorldCreationMenu);
			InputComponent->BindKey(EKeys::F8, IE_Pressed, this, &AWorldDirectorFixtureBootstrap::ToggleGenerationDiagnostics);
			InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &AWorldDirectorFixtureBootstrap::ToggleInteractionCursor);
			InputComponent->BindKey(EKeys::M, IE_Pressed, this, &AWorldDirectorFixtureBootstrap::ToggleMapView);
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
	if (FParse::Param(FCommandLine::Get(), TEXT("WorldDirectorSavedWorldAutoTest")))
	{
		RefreshSavedWorldCatalog();
		if (SavedWorldCatalog.IsEmpty())
		{
			UE_LOG(LogWorldDirector, Error,
				TEXT("WORLD_DIRECTOR_SAVED_WORLD_AUTO_TEST_RESULT=FAIL reason=no_loadable_worlds"));
			FGenericPlatformMisc::RequestExitWithStatus(true, 1);
			return;
		}
		OpenSavedWorld(SavedWorldCatalog[0].RecipePath);
		if (CompiledTown == nullptr)
		{
			UE_LOG(LogWorldDirector, Error,
				TEXT("WORLD_DIRECTOR_SAVED_WORLD_AUTO_TEST_RESULT=FAIL reason=load_failed"));
			FGenericPlatformMisc::RequestExitWithStatus(true, 1);
			return;
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
		FString Prompt = bAutomatedGenerationPrompted
			? TEXT("A river frontier town held together by a mill, old debts, and a guarded civic secret.")
			: FString();
		FParse::Value(FCommandLine::Get(), TEXT("WorldDirectorGenerationPrompt="), Prompt);
		int32 GenerationAutoTestSeed = bAutomatedGenerationPrompted ? 9182 : 4815;
		FParse::Value(FCommandLine::Get(), TEXT("WorldDirectorGenerationSeed="), GenerationAutoTestSeed);
		FString GenerationAutoTestModel = TEXT("gpt-5.6-luna");
		FParse::Value(FCommandLine::Get(), TEXT("WorldDirectorGenerationModel="), GenerationAutoTestModel);
		FString GenerationAutoTestReasoning = TEXT("high");
		FParse::Value(
			FCommandLine::Get(), TEXT("WorldDirectorGenerationReasoning="), GenerationAutoTestReasoning);
		const bool bUseCliProvider = FParse::Param(
			FCommandLine::Get(), TEXT("WorldDirectorUseCliProvider"));
		const float Timeout = bAutomatedGenerationExpectsTimeout ? 1.0f : (bUseCliProvider ? 300.0f : 20.0f);
		if (!Bridge->BeginWorldGeneration(
			Prompt, GenerationAutoTestSeed, Timeout, !bUseCliProvider,
			GenerationAutoTestModel, GenerationAutoTestReasoning))
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
		OpenLandingPage();
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

void AWorldDirectorFixtureBootstrap::OpenLandingPage()
{
	APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (PlayerController == nullptr)
	{
		if (GetWorld() != nullptr && GetNetMode() != NM_DedicatedServer &&
			!GetWorldTimerManager().IsTimerActive(CreationMenuRetryHandle))
		{
			GetWorldTimerManager().SetTimer(CreationMenuRetryHandle, this,
				&AWorldDirectorFixtureBootstrap::OpenLandingPage, 0.1f, false);
		}
		return;
	}
	GetWorldTimerManager().ClearTimer(CreationMenuRetryHandle);
	if (LandingWidget == nullptr)
	{
		LandingWidget = CreateWidget<UWorldDirectorLandingWidget>(
			PlayerController, UWorldDirectorLandingWidget::StaticClass());
		if (LandingWidget != nullptr)
		{
			LandingWidget->InitializeForBootstrap(this);
		}
	}
	if (LandingWidget != nullptr && !LandingWidget->IsInViewport())
	{
		LandingWidget->AddToViewport(200);
	}
	if (LandingWidget != nullptr)
	{
		LandingWidget->RefreshSavedWorlds();
		LandingWidget->ApplyViewportLayout();
	}
	ApplyPlayerInputMode();
	UE_LOG(LogWorldDirector, Display, TEXT("WORLD_DIRECTOR_LANDING state=%s"),
		LandingWidget != nullptr && LandingWidget->IsInViewport() ? TEXT("VISIBLE") : TEXT("FAILED"));
}

void AWorldDirectorFixtureBootstrap::CloseLandingPage()
{
	if (LandingWidget != nullptr)
	{
		LandingWidget->RemoveFromParent();
	}
	ApplyPlayerInputMode();
}

void AWorldDirectorFixtureBootstrap::RefreshSavedWorldCatalog()
{
	SavedWorldCatalog.Reset();

	UWorldGenerationSubsystem* Generation = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UWorldGenerationSubsystem>() : nullptr;
	if (Generation == nullptr)
	{
		return;
	}

	const FString RunsDirectory = WorldDirectorRunsDirectory();
	TArray<FString> RecipePaths;
	IFileManager::Get().FindFilesRecursive(
		RecipePaths, *RunsDirectory, TEXT("06-resolved-world-v3.json"), true, false);
	RecipePaths.Sort([](const FString& Left, const FString& Right)
	{
		return Left > Right;
	});

	for (const FString& RecipePath : RecipePaths)
	{
		if (!IsWorldDirectorRecipePath(RecipePath))
		{
			continue;
		}

		FString RecipeJson;
		FResolvedWorldPlan Plan;
		FValidationReport RecipeReport;
		if (!FFileHelper::LoadFileToString(RecipeJson, *RecipePath) ||
			!FWorldDirectorJson::LoadResolvedWorldPlan(RecipeJson, Plan, RecipeReport))
		{
			continue;
		}

		const FString IntegratedPath = FPaths::Combine(
			FPaths::GetPath(RecipePath), TEXT("05-integrated-world.json"));
		FString IntegratedJson;
		FGeneratedWorldSpec Spec;
		FValidationReport SpecReport;
		if (!FFileHelper::LoadFileToString(IntegratedJson, *IntegratedPath) ||
			!Generation->LoadAndValidateWorldSpec(IntegratedJson, Spec, SpecReport) ||
			!Generation->ValidateFullSliceWorldSpec(Spec).bValid ||
			Plan.SourceSpecId != Spec.Id || Plan.Seed != Spec.Seed)
		{
			continue;
		}

		const FString RunId = FPaths::GetCleanFilename(FPaths::GetPath(RecipePath));
		FString SettlementIdentity = Spec.Brief.SettlementIdentity.TrimStartAndEnd();
		if (SettlementIdentity.IsEmpty())
		{
			SettlementIdentity = Spec.Id;
		}
		FWorldDirectorSavedWorldEntry& Entry = SavedWorldCatalog.AddDefaulted_GetRef();
		Entry.DisplayName = FString::Printf(
			TEXT("%s  |  seed %d  |  %s"), *SettlementIdentity, Plan.Seed, *RunId);
		Entry.RecipePath = FPaths::ConvertRelativePathToFull(RecipePath);
		Entry.RunId = RunId;
		Entry.Seed = Plan.Seed;
	}

	UE_LOG(LogWorldDirector, Display, TEXT("WORLD_DIRECTOR_SAVED_WORLDS count=%d"), SavedWorldCatalog.Num());
}

void AWorldDirectorFixtureBootstrap::OpenSavedWorld(const FString& RecipePath)
{
	if (!IsWorldDirectorRecipePath(RecipePath))
	{
		UE_LOG(LogWorldDirector, Error,
			TEXT("WORLD_DIRECTOR_SAVED_WORLD_LOAD_RESULT=FAIL reason=invalid_recipe_path path=%s"),
			*RecipePath);
		return;
	}

	UWorldGenerationSubsystem* Generation = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UWorldGenerationSubsystem>() : nullptr;
	if (Generation == nullptr)
	{
		UE_LOG(LogWorldDirector, Error,
			TEXT("WORLD_DIRECTOR_SAVED_WORLD_LOAD_RESULT=FAIL reason=generation_subsystem_missing"));
		return;
	}

	const FString NormalizedRecipePath = FPaths::ConvertRelativePathToFull(RecipePath);
	FString RecipeJson;
	FResolvedWorldPlan Plan;
	FValidationReport RecipeReport;
	if (!FFileHelper::LoadFileToString(RecipeJson, *NormalizedRecipePath) ||
		!FWorldDirectorJson::LoadResolvedWorldPlan(RecipeJson, Plan, RecipeReport))
	{
		LastCompilationReport = RecipeReport;
		UE_LOG(LogWorldDirector, Error,
			TEXT("WORLD_DIRECTOR_SAVED_WORLD_LOAD_RESULT=FAIL reason=recipe_invalid path=%s"),
			*NormalizedRecipePath);
		OpenLandingPage();
		return;
	}

	const FString IntegratedPath = FPaths::Combine(
		FPaths::GetPath(NormalizedRecipePath), TEXT("05-integrated-world.json"));
	FString IntegratedJson;
	FGeneratedWorldSpec Spec;
	FValidationReport SpecReport;
	if (!FFileHelper::LoadFileToString(IntegratedJson, *IntegratedPath) ||
		!Generation->LoadAndValidateWorldSpec(IntegratedJson, Spec, SpecReport))
	{
		LastCompilationReport = SpecReport;
		UE_LOG(LogWorldDirector, Error,
			TEXT("WORLD_DIRECTOR_SAVED_WORLD_LOAD_RESULT=FAIL reason=integrated_spec_invalid path=%s"),
			*IntegratedPath);
		OpenLandingPage();
		return;
	}
	const FValidationReport FullSpecReport = Generation->ValidateFullSliceWorldSpec(Spec);
	if (!FullSpecReport.bValid || Plan.SourceSpecId != Spec.Id || Plan.Seed != Spec.Seed)
	{
		LastCompilationReport = FullSpecReport;
		if (Plan.SourceSpecId != Spec.Id || Plan.Seed != Spec.Seed)
		{
			LastCompilationReport.AddError(
				TEXT("saved_world.artifact_mismatch"), TEXT("$"),
				TEXT("The semantic world and physical replay recipe do not belong to the same run."));
		}
		UE_LOG(LogWorldDirector, Error,
			TEXT("WORLD_DIRECTOR_SAVED_WORLD_LOAD_RESULT=FAIL reason=artifact_mismatch_or_incomplete path=%s"),
			*NormalizedRecipePath);
		OpenLandingPage();
		return;
	}

	CloseLandingPage();
	CloseWorldCreationMenu();
	DestroyCurrentTown();
	AWorldDirectorTownActor* SpawnedTown = nullptr;
	LastCompilationReport = FValidationReport();
	if (!Generation->CompileResolvedWorld(this, Plan, SpawnedTown, LastCompilationReport) ||
		SpawnedTown == nullptr)
	{
		UE_LOG(LogWorldDirector, Error,
			TEXT("WORLD_DIRECTOR_SAVED_WORLD_LOAD_RESULT=FAIL reason=runtime_compile_failed path=%s"),
			*NormalizedRecipePath);
		OpenLandingPage();
		return;
	}

	CompiledTown = SpawnedTown;
	if (UWorldStateSubsystem* State = GetWorld()->GetSubsystem<UWorldStateSubsystem>())
	{
		State->SetActiveWorldSpec(Spec);
	}
	UTownSimulationSubsystem* Simulation = GetWorld()->GetSubsystem<UTownSimulationSubsystem>();
	if (Simulation == nullptr || !Simulation->InitializeLivingTown(Spec, SpawnedTown, LastCompilationReport))
	{
		if (Simulation != nullptr)
		{
			Simulation->ShutdownLivingTown();
		}
		if (UChangeProjectSubsystem* Projects = GetWorld()->GetSubsystem<UChangeProjectSubsystem>())
		{
			Projects->ShutdownProjects();
		}
		if (UWorldStateSubsystem* State = GetWorld()->GetSubsystem<UWorldStateSubsystem>())
		{
			State->ClearActiveWorldSpec();
		}
		SpawnedTown->DestroyCompiledContent();
		CompiledTown = nullptr;
		UE_LOG(LogWorldDirector, Error,
			TEXT("WORLD_DIRECTOR_SAVED_WORLD_LOAD_RESULT=FAIL reason=simulation_init_failed path=%s"),
			*NormalizedRecipePath);
		OpenLandingPage();
		return;
	}

	ApplyPlayerInputMode();
	UE_LOG(LogWorldDirector, Display,
		TEXT("WORLD_DIRECTOR_SAVED_WORLD_LOAD_RESULT=PASS run=%s seed=%d"),
		*FPaths::GetCleanFilename(FPaths::GetPath(NormalizedRecipePath)), Plan.Seed);
}

void AWorldDirectorFixtureBootstrap::BeginSampleWorldPreview()
{
	if (LoadingWidget != nullptr && LoadingWidget->IsInViewport())
	{
		return;
	}
	CloseLandingPage();
	OpenLoadingScreen(true);
	GetWorldTimerManager().SetTimer(
		SampleWorldCompileHandle, this, &AWorldDirectorFixtureBootstrap::CompileSampleWorldPreview, 0.05f, false);
}

void AWorldDirectorFixtureBootstrap::CompileSampleWorldPreview()
{
	GetWorldTimerManager().ClearTimer(SampleWorldCompileHandle);
	const bool bCompiled = CompileFixture();
	CloseLoadingScreen();
	if (!bCompiled)
	{
		OpenLandingPage();
		UE_LOG(LogWorldDirector, Error, TEXT("WORLD_DIRECTOR_SAMPLE_WORLD_RESULT=FAIL"));
		return;
	}
	UE_LOG(LogWorldDirector, Display, TEXT("WORLD_DIRECTOR_SAMPLE_WORLD_RESULT=PASS"));
}

void AWorldDirectorFixtureBootstrap::OpenLoadingScreen(const bool bForSampleWorld)
{
	APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (PlayerController == nullptr)
	{
		return;
	}
	if (LoadingWidget == nullptr)
	{
		LoadingWidget = CreateWidget<UWorldDirectorLoadingWidget>(
			PlayerController, UWorldDirectorLoadingWidget::StaticClass());
		if (LoadingWidget != nullptr)
		{
			LoadingWidget->InitializeForBootstrap(this);
		}
	}
	if (LoadingWidget != nullptr)
	{
		LoadingWidget->SetForSampleWorld(bForSampleWorld);
		if (!LoadingWidget->IsInViewport())
		{
			LoadingWidget->AddToViewport(110);
		}
		LoadingWidget->ApplyViewportLayout();
	}
	ApplyPlayerInputMode();
}

void AWorldDirectorFixtureBootstrap::CloseLoadingScreen()
{
	if (LoadingWidget != nullptr)
	{
		LoadingWidget->RemoveFromParent();
	}
	ApplyPlayerInputMode();
}

void AWorldDirectorFixtureBootstrap::OpenMapView()
{
	if (CompiledTown == nullptr ||
		(CreateWorldWidget != nullptr && CreateWorldWidget->IsInViewport()) ||
		(LandingWidget != nullptr && LandingWidget->IsInViewport()) ||
		(LoadingWidget != nullptr && LoadingWidget->IsInViewport()) ||
		(GenerationDiagnosticsWidget != nullptr && GenerationDiagnosticsWidget->IsInViewport()))
	{
		return;
	}
	if (MapWidget == nullptr)
	{
		APlayerController* PlayerController = GetWorld()
			? GetWorld()->GetFirstPlayerController() : nullptr;
		if (PlayerController == nullptr)
		{
			return;
		}
		MapWidget = CreateWidget<UWorldDirectorMapWidget>(
			PlayerController, UWorldDirectorMapWidget::StaticClass());
		if (MapWidget != nullptr)
		{
			MapWidget->InitializeForBootstrap(this);
		}
	}
	if (MapWidget != nullptr)
	{
		MapWidget->InitializeForTown(CompiledTown);
		if (!MapWidget->IsInViewport())
		{
			MapWidget->AddToViewport(110);
		}
		MapWidget->ApplyViewportLayout();
		MapWidget->SetKeyboardFocus();
	}
	ApplyPlayerInputMode();
}

void AWorldDirectorFixtureBootstrap::ToggleMapView()
{
	if (MapWidget != nullptr && MapWidget->IsInViewport())
	{
		CloseMapView();
		return;
	}
	OpenMapView();
}

void AWorldDirectorFixtureBootstrap::CloseMapView()
{
	if (MapWidget != nullptr)
	{
		MapWidget->RemoveFromParent();
	}
	ApplyPlayerInputMode();
}

void AWorldDirectorFixtureBootstrap::OpenWorldCreationMenu()
{
	CloseLandingPage();
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
	const bool bViewportLayoutApplied = CreateWorldWidget != nullptr &&
		CreateWorldWidget->ApplyViewportLayout();
	if (CreateWorldWidget != nullptr && !bViewportLayoutApplied && FApp::CanEverRender() &&
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
	const bool bLandingOpen = LandingWidget != nullptr && LandingWidget->IsInViewport();
	const bool bLoadingOpen = LoadingWidget != nullptr && LoadingWidget->IsInViewport();
	const bool bMapOpen = MapWidget != nullptr && MapWidget->IsInViewport();
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
	const bool bCursorVisible = bCreationOpen || bLandingOpen || bLoadingOpen || bMapOpen || bDiagnosticsOpen || bInspectionOpen ||
		bDialogueOpen || bInteractionCursorMode;
	PlayerController->bShowMouseCursor = bCursorVisible;
	PlayerController->bEnableClickEvents = bCursorVisible;
	PlayerController->bEnableMouseOverEvents = bCursorVisible;
	if (bMapOpen)
	{
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(MapWidget->TakeWidget());
		PlayerController->SetInputMode(InputMode);
	}
	else if (bDiagnosticsOpen)
	{
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(GenerationDiagnosticsWidget->TakeWidget());
		PlayerController->SetInputMode(InputMode);
	}
	else if (bLandingOpen)
	{
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(LandingWidget->TakeWidget());
		PlayerController->SetInputMode(InputMode);
	}
	else if (bLoadingOpen)
	{
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(LoadingWidget->TakeWidget());
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
	const bool bStarted = Bridge->BeginWorldGeneration(PlayerPrompt.TrimStartAndEnd(), FMath::Max(1, Seed),
		bUseFixtureProviderForDebug ? 300.0f : 420.0f,
		bUseFixtureProviderForDebug, Model, ReasoningEffort);
	if (bStarted && !bPlayerFlowAutoTest)
	{
		OpenLoadingScreen(false);
	}
	return bStarted;
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
	CloseLoadingScreen();
	if (bPlayable && !bPlayerFlowAutoTest)
	{
		CloseWorldCreationMenu();
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
	CloseMapView();
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
	if (UWorld* World = GetWorld(); World != nullptr &&
		UNavigationSystemV1::IsNavigationBeingBuilt(World) && PlayerFlowNavigationRetryCount < 20)
	{
		++PlayerFlowNavigationRetryCount;
		UE_LOG(LogWorldDirector, Display,
			TEXT("WORLD_DIRECTOR_PLAYER_FLOW_NAV_WAIT attempt=%d max=20 state=BUILDING"),
			PlayerFlowNavigationRetryCount);
		FTimerHandle TimerHandle;
		GetWorldTimerManager().SetTimer(
			TimerHandle, this, &AWorldDirectorFixtureBootstrap::RunAutomatedPlayerFlowCheck, 1.0f, false);
		return;
	}
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
	if (UWorld* World = GetWorld(); World != nullptr &&
		UNavigationSystemV1::IsNavigationBeingBuilt(World) &&
		AutomatedGenerationNavigationRetryCount < 60)
	{
		++AutomatedGenerationNavigationRetryCount;
		UE_LOG(LogWorldDirector, Display,
			TEXT("WORLD_DIRECTOR_GENERATION_NAV_WAIT attempt=%d max=60 state=BUILDING"),
			AutomatedGenerationNavigationRetryCount);
		FTimerHandle TimerHandle;
		GetWorldTimerManager().SetTimer(
			TimerHandle, this,
			&AWorldDirectorFixtureBootstrap::RunAutomatedGenerationViabilityCheck,
			1.0f, false);
		return;
	}
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
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (CompiledTown == nullptr || CompiledTown->TerrainMesh == nullptr || PlayerController == nullptr)
	{
		UE_LOG(LogWorldDirector, Error,
			TEXT("WORLD_DIRECTOR_VISUAL_CAPTURE_RESULT=FAIL reason=camera_context_missing"));
		FGenericPlatformMisc::RequestExitWithStatus(false, 5);
		return;
	}

	AutomatedVisualCaptureView = TEXT("overview");
	FParse::Value(FCommandLine::Get(), TEXT("WorldDirectorVisualView="), AutomatedVisualCaptureView);
	AutomatedVisualCaptureView.TrimStartAndEndInline();
	AutomatedVisualCaptureView.ToLowerInline();

	const FBox TerrainBounds = CompiledTown->TerrainMesh->Bounds.GetBox();
	if (!TerrainBounds.IsValid)
	{
		UE_LOG(LogWorldDirector, Error,
			TEXT("WORLD_DIRECTOR_VISUAL_CAPTURE_RESULT=FAIL reason=terrain_bounds_invalid"));
		FGenericPlatformMisc::RequestExitWithStatus(false, 5);
		return;
	}
	FVector SettlementFocus = FVector::ZeroVector;
	FBox SettlementBounds(ForceInit);
	int32 FocusLocationCount = 0;
	for (const AWorldDirectorLocationActor* Location : CompiledTown->SpawnedLocations)
	{
		if (Location != nullptr)
		{
			SettlementFocus += Location->GetActorLocation();
			FVector LocationOrigin;
			FVector LocationExtent;
			const AActor* BoundsActor = Location->ShellActor != nullptr
				? Location->ShellActor.Get() : static_cast<const AActor*>(Location);
			BoundsActor->GetActorBounds(true, LocationOrigin, LocationExtent, true);
			SettlementBounds += FBox::BuildAABB(LocationOrigin, LocationExtent);
			++FocusLocationCount;
		}
	}
	if (FocusLocationCount > 0)
	{
		SettlementFocus /= FocusLocationCount;
	}
	else
	{
		SettlementFocus = TerrainBounds.GetCenter();
	}
	SettlementFocus.Z += 700.0f;

	FVector LandmarkFocus = SettlementFocus;
	FVector LandmarkEntrance = SettlementFocus;
	FVector LandmarkExtent(900.0f, 900.0f, 1200.0f);
	for (const AWorldDirectorLocationActor* Location : CompiledTown->SpawnedLocations)
	{
		if (Location != nullptr && Location->LocationId == CompiledTown->LandmarkLocationId)
		{
			FVector LandmarkOrigin;
			const AActor* BoundsActor = Location->ShellActor != nullptr
				? Location->ShellActor.Get() : static_cast<const AActor*>(Location);
			BoundsActor->GetActorBounds(true, LandmarkOrigin, LandmarkExtent, true);
			LandmarkFocus = LandmarkOrigin + FVector(0.0f, 0.0f, LandmarkExtent.Z * 0.18f);
			LandmarkEntrance = Location->DoorActor != nullptr
				? Location->DoorActor->GetActorLocation() : Location->NavigationEntranceLocation;
			break;
		}
	}

	const float WorldSpan = FMath::Max(TerrainBounds.GetSize().X, TerrainBounds.GetSize().Y);
	const float SettlementSpan = SettlementBounds.IsValid
		? FMath::Max(16000.0f, FMath::Max(SettlementBounds.GetSize().X, SettlementBounds.GetSize().Y))
		: WorldSpan * 0.32f;
	FVector CameraTarget = FMath::Lerp(TerrainBounds.GetCenter(), SettlementFocus, 0.7f);
	FVector CameraLocation;
	float FieldOfView = 64.0f;
	if (AutomatedVisualCaptureView == TEXT("approach"))
	{
		CameraTarget = SettlementFocus;
		CameraLocation = CameraTarget + FVector(-SettlementSpan * 0.5f, -SettlementSpan * 0.28f,
			FMath::Max(1200.0f, SettlementSpan * 0.055f));
		FieldOfView = 58.0f;
	}
	else if (AutomatedVisualCaptureView == TEXT("landmark") ||
		AutomatedVisualCaptureView == TEXT("civic"))
	{
		CameraTarget = LandmarkFocus;
		FVector ApproachAxis = (LandmarkEntrance - LandmarkFocus).GetSafeNormal2D();
		if (ApproachAxis.IsNearlyZero())
		{
			ApproachAxis = FVector(-1.0f, -0.7f, 0.0f).GetSafeNormal();
		}
		const FVector ApproachSide(-ApproachAxis.Y, ApproachAxis.X, 0.0f);
		const bool bCivicComposition = AutomatedVisualCaptureView == TEXT("civic");
		const float FrameDistance = bCivicComposition
			? FMath::Max(5600.0f, FMath::Max(LandmarkExtent.X, LandmarkExtent.Y) * 4.8f)
			: FMath::Max(2600.0f, FMath::Max(LandmarkExtent.X, LandmarkExtent.Y) * 2.9f);
		if (bCivicComposition)
		{
			CameraTarget = FMath::Lerp(LandmarkFocus, LandmarkEntrance, 0.55f) +
				ApproachAxis * 520.0f + FVector(0.0f, 0.0f, 160.0f);
		}
		CameraLocation = LandmarkEntrance + ApproachAxis * FrameDistance +
			ApproachSide * LandmarkExtent.X * (bCivicComposition ? 0.72f : 0.32f) +
			FVector(0.0f, 0.0f, bCivicComposition ? 1850.0f : 360.0f);
		FieldOfView = bCivicComposition ? 58.0f : 52.0f;
	}
	else if (AutomatedVisualCaptureView == TEXT("eye") ||
		AutomatedVisualCaptureView == TEXT("street") ||
		AutomatedVisualCaptureView == TEXT("district") ||
		AutomatedVisualCaptureView == TEXT("overlook"))
	{
		// Every pre-existing view sits between 3.6 m and 300 m up, which is exactly
		// the altitude band where a 625 cm terrain cell, top-down planar UVs and
		// one-vertex grading skirts all still look acceptable. These views stand a
		// notional player on the actual collision surface instead, so ground-level
		// defects are visible to review rather than hidden by altitude.
		const float EyeHeightCentimeters = 170.0f;
		auto GroundAt = [this, World](const FVector2D& Where, const float Fallback) -> float
		{
			FHitResult Hit;
			const FVector TraceStart(Where.X, Where.Y, 60000.0f);
			const FVector TraceEnd(Where.X, Where.Y, -30000.0f);
			FCollisionQueryParams Params(SCENE_QUERY_STAT(WorldDirectorVisualGround), false);
			if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, Params))
			{
				return Hit.ImpactPoint.Z;
			}
			return Fallback;
		};

		const FVector2D SettlementCentre(SettlementFocus.X, SettlementFocus.Y);
		const float CentreFallback = SettlementFocus.Z - 700.0f;
		FVector2D StandAt = SettlementCentre;
		FVector LookAt = LandmarkFocus;

		if (AutomatedVisualCaptureView == TEXT("street"))
		{
			// Stand on the civic approach itself, looking back up it at the landmark.
			const FVector2D Entrance(LandmarkEntrance.X, LandmarkEntrance.Y);
			StandAt = FMath::Lerp(Entrance, SettlementCentre, 0.55f);
			LookAt = LandmarkEntrance;
		}
		else if (AutomatedVisualCaptureView == TEXT("district"))
		{
			// The location furthest from the landmark stands in for a secondary
			// district, so the shot is not another view of the civic core.
			float FurthestDistance = -1.0f;
			FVector2D Furthest = SettlementCentre;
			for (const AWorldDirectorLocationActor* Location : CompiledTown->SpawnedLocations)
			{
				if (Location == nullptr || Location->LocationId == CompiledTown->LandmarkLocationId)
				{
					continue;
				}
				const FVector Position = Location->GetActorLocation();
				const float Distance = FVector2D::Distance(
					FVector2D(Position.X, Position.Y), FVector2D(LandmarkFocus.X, LandmarkFocus.Y));
				if (Distance > FurthestDistance)
				{
					FurthestDistance = Distance;
					Furthest = FVector2D(Position.X, Position.Y);
				}
			}
			// Step back off the plot so the building is framed rather than clipped.
			const FVector2D Inward = (SettlementCentre - Furthest).GetSafeNormal();
			StandAt = Furthest + Inward * 2600.0f;
			LookAt = FVector(Furthest.X, Furthest.Y,
				GroundAt(Furthest, CentreFallback) + 400.0f);
		}
		else if (AutomatedVisualCaptureView == TEXT("overlook"))
		{
			// Search a ring around the settlement for genuinely high ground, so the
			// shot reports whatever relief actually exists near the town.
			float BestHeight = -TNumericLimits<float>::Max();
			FVector2D BestPosition = SettlementCentre;
			const float SearchRadius = FMath::Max(18000.0f, SettlementSpan * 0.85f);
			for (int32 RingIndex = 0; RingIndex < 3; ++RingIndex)
			{
				const float Radius = SearchRadius * (0.55f + 0.225f * RingIndex);
				for (int32 Step = 0; Step < 24; ++Step)
				{
					const float Angle = (2.0f * PI * Step) / 24.0f;
					const FVector2D Candidate = SettlementCentre +
						FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
					const float Height = GroundAt(Candidate, -TNumericLimits<float>::Max());
					if (Height > BestHeight)
					{
						BestHeight = Height;
						BestPosition = Candidate;
					}
				}
			}
			StandAt = BestPosition;
			LookAt = FVector(SettlementCentre.X, SettlementCentre.Y, CentreFallback + 600.0f);
		}

		const float StandGround = GroundAt(StandAt, CentreFallback);
		CameraLocation = FVector(StandAt.X, StandAt.Y, StandGround + EyeHeightCentimeters);
		if (AutomatedVisualCaptureView == TEXT("eye"))
		{
			LookAt.Z = FMath::Max(LookAt.Z, CameraLocation.Z);
		}
		CameraTarget = LookAt;
		FieldOfView = 75.0f;
		UE_LOG(LogWorldDirector, Display,
			TEXT("WORLD_DIRECTOR_VISUAL_EYE view=%s groundZ=%.0f eyeZ=%.0f"),
			*AutomatedVisualCaptureView, StandGround, CameraLocation.Z);
	}
	else if (AutomatedVisualCaptureView == TEXT("topdown"))
	{
		CameraTarget = SettlementFocus;
		CameraLocation = CameraTarget + FVector(0.0f, 0.0f,
			FMath::Max(26000.0f, SettlementSpan * 0.92f));
		FieldOfView = 56.0f;
	}
	else
	{
		if (AutomatedVisualCaptureView != TEXT("overview"))
		{
			UE_LOG(LogWorldDirector, Warning,
				TEXT("WORLD_DIRECTOR_VISUAL_CAPTURE unknownView=%s fallback=overview"),
				*AutomatedVisualCaptureView);
			AutomatedVisualCaptureView = TEXT("overview");
		}
		CameraLocation = CameraTarget + FVector(-WorldSpan * 0.62f, -WorldSpan * 0.62f,
			FMath::Max(30000.0f, WorldSpan * 0.52f));
		CameraLocation.Z = FMath::Max(CameraLocation.Z,
			TerrainBounds.Max.Z + FMath::Max(12000.0f, WorldSpan * 0.18f));
	}

	if (AutomatedVisualCaptureCamera.IsValid())
	{
		AutomatedVisualCaptureCamera->Destroy();
	}
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACameraActor* CaptureCamera = World->SpawnActor<ACameraActor>(
		ACameraActor::StaticClass(), CameraLocation, (CameraTarget - CameraLocation).Rotation(), SpawnParameters);
	if (CaptureCamera == nullptr)
	{
		UE_LOG(LogWorldDirector, Error,
			TEXT("WORLD_DIRECTOR_VISUAL_CAPTURE_RESULT=FAIL reason=camera_spawn_failed"));
		FGenericPlatformMisc::RequestExitWithStatus(false, 5);
		return;
	}
	AutomatedVisualCaptureCamera = CaptureCamera;
	CaptureCamera->SetActorEnableCollision(false);
	CaptureCamera->GetCameraComponent()->SetFieldOfView(FieldOfView);
	CaptureCamera->GetCameraComponent()->SetConstraintAspectRatio(false);
	PlayerController->SetViewTargetWithBlend(CaptureCamera, 0.0f);
	PlayerController->bShowMouseCursor = false;

	AutomatedVisualCapturePath = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("Screenshots/Mac/WorldDirectorPhase4.png"));
	FString RequestedOutput;
	if (FParse::Value(FCommandLine::Get(), TEXT("WorldDirectorVisualOutput="), RequestedOutput) &&
		!RequestedOutput.TrimStartAndEnd().IsEmpty())
	{
		RequestedOutput.TrimStartAndEndInline();
		AutomatedVisualCapturePath = FPaths::IsRelative(RequestedOutput)
			? FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots/Mac"), RequestedOutput)
			: RequestedOutput;
	}
	if (FPaths::GetExtension(AutomatedVisualCapturePath).IsEmpty())
	{
		AutomatedVisualCapturePath += TEXT(".png");
	}
	FPaths::NormalizeFilename(AutomatedVisualCapturePath);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(AutomatedVisualCapturePath), true);
	UE_LOG(LogWorldDirector, Display,
		TEXT("WORLD_DIRECTOR_VISUAL_CAMERA view=%s location=%s target=%s fov=%.1f output=%s"),
		*AutomatedVisualCaptureView, *CameraLocation.ToCompactString(), *CameraTarget.ToCompactString(),
		FieldOfView, *AutomatedVisualCapturePath);

	// Let the new view target render and allow HISM/texture streaming to settle
	// before requesting the frame. This avoids capturing the previous pawn view.
	FTimerHandle TimerHandle;
	GetWorldTimerManager().SetTimer(
		TimerHandle, this, &AWorldDirectorFixtureBootstrap::CaptureAutomatedVisualFrame, 1.0f, false);
}

void AWorldDirectorFixtureBootstrap::CaptureAutomatedVisualFrame()
{
	if (AutomatedVisualCapturePath.IsEmpty())
	{
		UE_LOG(LogWorldDirector, Error,
			TEXT("WORLD_DIRECTOR_VISUAL_CAPTURE_RESULT=FAIL reason=output_path_missing"));
		FGenericPlatformMisc::RequestExitWithStatus(false, 5);
		return;
	}
	if (IFileManager::Get().FileExists(*AutomatedVisualCapturePath) &&
		!IFileManager::Get().Delete(*AutomatedVisualCapturePath, false, true, true))
	{
		UE_LOG(LogWorldDirector, Error,
			TEXT("WORLD_DIRECTOR_VISUAL_CAPTURE_RESULT=FAIL reason=stale_output_could_not_be_replaced path=%s"),
			*AutomatedVisualCapturePath);
		FGenericPlatformMisc::RequestExitWithStatus(false, 5);
		return;
	}
	FScreenshotRequest::RequestScreenshot(AutomatedVisualCapturePath, false, false);
	UE_LOG(LogWorldDirector, Display,
		TEXT("WORLD_DIRECTOR_VISUAL_CAPTURE_REQUESTED view=%s path=%s"),
		*AutomatedVisualCaptureView, *AutomatedVisualCapturePath);
	FTimerHandle TimerHandle;
	GetWorldTimerManager().SetTimer(
		TimerHandle, this, &AWorldDirectorFixtureBootstrap::FinishAutomatedVisualCapture, 2.0f, false);
}

void AWorldDirectorFixtureBootstrap::FinishAutomatedVisualCapture()
{
	const int64 ScreenshotBytes = AutomatedVisualCapturePath.IsEmpty()
		? INDEX_NONE : IFileManager::Get().FileSize(*AutomatedVisualCapturePath);

	// A screenshot that exists is not a screenshot that rendered. Offscreen and
	// headless Metal paths can emit a perfectly well-formed, perfectly black PNG,
	// so decode the frame back and require actual tonal variation before passing.
	// Without this the harness reports success for an empty image.
	float FrameMean = 0.0f;
	float FrameStdDev = 0.0f;
	int32 FrameMin = 0;
	int32 FrameMax = 0;
	bool bFrameDecoded = false;
	if (ScreenshotBytes > 1024)
	{
		TArray<uint8> FileBytes;
		if (FFileHelper::LoadFileToArray(FileBytes, *AutomatedVisualCapturePath))
		{
			IImageWrapperModule& ImageWrapperModule =
				FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
			const TSharedPtr<IImageWrapper> ImageWrapper =
				ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
			TArray<uint8> Raw;
			if (ImageWrapper.IsValid() &&
				ImageWrapper->SetCompressed(FileBytes.GetData(), FileBytes.Num()) &&
				ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, Raw) && Raw.Num() >= 4)
			{
				const int64 PixelCount = Raw.Num() / 4;
				// Sample at most ~200k pixels; full decode of a 4K frame is wasteful here.
				const int64 Stride = FMath::Max<int64>(1, PixelCount / 200000);
				double Sum = 0.0;
				double SumSquares = 0.0;
				int64 Samples = 0;
				int32 MinLuma = 255;
				int32 MaxLuma = 0;
				for (int64 Pixel = 0; Pixel < PixelCount; Pixel += Stride)
				{
					const uint8 B = Raw[Pixel * 4 + 0];
					const uint8 G = Raw[Pixel * 4 + 1];
					const uint8 R = Raw[Pixel * 4 + 2];
					const int32 Luma = (R * 299 + G * 587 + B * 114) / 1000;
					Sum += Luma;
					SumSquares += static_cast<double>(Luma) * Luma;
					MinLuma = FMath::Min(MinLuma, Luma);
					MaxLuma = FMath::Max(MaxLuma, Luma);
					++Samples;
				}
				if (Samples > 0)
				{
					const double Mean = Sum / Samples;
					FrameMean = static_cast<float>(Mean);
					FrameStdDev = static_cast<float>(
						FMath::Sqrt(FMath::Max(0.0, SumSquares / Samples - Mean * Mean)));
					FrameMin = MinLuma;
					FrameMax = MaxLuma;
					bFrameDecoded = true;
				}
			}
		}
	}

	const bool bHasContent = bFrameDecoded &&
		(FrameMax - FrameMin) >= 8 && FrameStdDev >= 4.0f;
	const bool bPassed = ScreenshotBytes > 1024 && bHasContent;
	if (bPassed)
	{
		UE_LOG(LogWorldDirector, Display,
			TEXT("WORLD_DIRECTOR_VISUAL_CAPTURE_RESULT=PASS view=%s bytes=%lld ")
			TEXT("mean=%.1f stdev=%.1f min=%d max=%d path=%s"),
			*AutomatedVisualCaptureView, ScreenshotBytes,
			FrameMean, FrameStdDev, FrameMin, FrameMax, *AutomatedVisualCapturePath);
	}
	else
	{
		const TCHAR* Reason = ScreenshotBytes <= 1024
			? TEXT("missing_or_truncated")
			: (!bFrameDecoded ? TEXT("decode_failed") : TEXT("blank_frame"));
		UE_LOG(LogWorldDirector, Error,
			TEXT("WORLD_DIRECTOR_VISUAL_CAPTURE_RESULT=FAIL reason=%s view=%s bytes=%lld ")
			TEXT("mean=%.1f stdev=%.1f min=%d max=%d path=%s"),
			Reason, *AutomatedVisualCaptureView, ScreenshotBytes,
			FrameMean, FrameStdDev, FrameMin, FrameMax, *AutomatedVisualCapturePath);
	}
	FGenericPlatformMisc::RequestExitWithStatus(false, bPassed ? 0 : 5);
}
