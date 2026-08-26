#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "SmartObjectDefinition.h"
#include "WorldDirectorTypes.h"

#include "WorldDirectorTownActors.generated.h"

class UInstancedStaticMeshComponent;
class UHierarchicalInstancedStaticMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;

UCLASS()
class WORLDDIRECTORRUNTIME_API UWorldDirectorSmartObjectBehaviorDefinition
	: public USmartObjectBehaviorDefinition
{
	GENERATED_BODY()
};

UCLASS(BlueprintType)
class WORLDDIRECTORRUNTIME_API AWorldDirectorActivityStationActor : public AActor
{
	GENERATED_BODY()

public:
	AWorldDirectorActivityStationActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Activity")
	TObjectPtr<class USmartObjectComponent> SmartObjectComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Activity")
	FString LocationId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Activity")
	FName ActivityTag;
};

UCLASS(BlueprintType)
class WORLDDIRECTORRUNTIME_API AWorldDirectorDoorActor : public AActor
{
	GENERATED_BODY()

public:
	AWorldDirectorDoorActor();

	UFUNCTION(BlueprintCallable, Category = "World Director|Door")
	void SetDoorOpen(bool bOpen);

	UFUNCTION(BlueprintCallable, Category = "World Director|Door")
	void ToggleDoor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Door")
	TObjectPtr<UStaticMeshComponent> DoorMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Door")
	bool bIsOpen = true;
};

UCLASS(BlueprintType)
class WORLDDIRECTORRUNTIME_API AWorldDirectorLocationActor : public AActor
{
	GENERATED_BODY()

public:
	AWorldDirectorLocationActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Location")
	FString LocationId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Location")
	FVector NavigationEntranceLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Location")
	TObjectPtr<AActor> ShellActor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Location")
	TObjectPtr<AActor> InteriorActor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Location")
	TObjectPtr<AWorldDirectorDoorActor> DoorActor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Location")
	TArray<TObjectPtr<AWorldDirectorActivityStationActor>> ActivityStations;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Location")
	FName CurrentPurposeTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Location")
	FString OwnerResidentId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Location")
	FString ControllerResidentId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Location")
	EWorldDirectorAccessPolicy AccessPolicy = EWorldDirectorAccessPolicy::Public;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Location")
	int32 DressingRevision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Location")
	TObjectPtr<class UTextRenderComponent> ProjectSign;
};

UCLASS(BlueprintType)
class WORLDDIRECTORRUNTIME_API AWorldDirectorResidentActor : public ACharacter
{
	GENERATED_BODY()

public:
	AWorldDirectorResidentActor();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void NotifyActorOnClicked(FKey ButtonPressed) override;

	UFUNCTION(BlueprintCallable, Category = "World Director|Resident")
	void OpenDialogueMenu();

	UFUNCTION(BlueprintCallable, Category = "World Director|Resident")
	void CloseDialogueMenu();

	bool TickResidentLife(float DeltaTime);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Resident")
	TObjectPtr<class USkeletalMeshComponent> HeadMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Resident")
	TObjectPtr<class USkeletalMeshComponent> LegsMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Resident")
	TObjectPtr<class USkeletalMeshComponent> FeetMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Resident")
	FString ResidentId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Resident")
	FString HomeLocationId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Resident")
	FString WorkplaceLocationId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Resident")
	TArray<FWorldDirectorScheduleStop> Schedule;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Resident")
	TObjectPtr<class UStateTreeComponent> StateTreeComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Resident")
	EWorldDirectorResidentAvailability Availability = EWorldDirectorResidentAvailability::Available;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Resident")
	FString CurrentLocationId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Resident")
	FString IntendedLocationId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Resident")
	FName CurrentActivityTag;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Resident")
	TObjectPtr<class UWorldDirectorDialogueWidget> ActiveDialogueWidget;
};

UCLASS(BlueprintType)
class WORLDDIRECTORRUNTIME_API AWorldDirectorTownActor : public AActor
{
	GENERATED_BODY()

public:
	AWorldDirectorTownActor();
	virtual void BeginPlay() override;

	bool BuildFromPlan(const FResolvedWorldPlan& Plan, FValidationReport& OutReport);
	bool BuildActivityStations(const FGeneratedWorldSpec& Spec, FValidationReport& OutReport);
	bool ApplyLocationRepurpose(
		const FWorldLocation& UpdatedLocation,
		const FChangeProject& Project,
		FValidationReport& OutReport);
	const FResolvedWorldPlan& GetCompiledPlan() const { return CompiledPlan; }
	void DestroyCompiledContent();

	UFUNCTION(BlueprintCallable, Category = "World Director|Inspection")
	void ToggleInspectionMenu();

	UFUNCTION(BlueprintCallable, Category = "World Director|Inspection")
	void OpenInspectionMenu();

	UFUNCTION(BlueprintCallable, Category = "World Director|Inspection")
	void CloseInspectionMenu();

	UFUNCTION(BlueprintCallable, Category = "World Director|Compilation")
	FValidationReport ValidateNavigationViability() const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Compilation")
	FString SourceSpecId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Compilation")
	FString LandmarkLocationId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Compilation")
	TArray<TObjectPtr<AWorldDirectorLocationActor>> SpawnedLocations;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Compilation")
	TArray<TObjectPtr<AWorldDirectorResidentActor>> SpawnedResidents;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Generation")
	TObjectPtr<class UProceduralMeshComponent> TerrainMesh;

	// A non-colliding continuation beyond the playable terrain. This prevents
	// normal cameras from exposing the square edge of the generated world while
	// keeping navigation and physics bounded to the authored V3 terrain recipe.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Generation")
	TObjectPtr<class UProceduralMeshComponent> HorizonTerrainMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Generation")
	TObjectPtr<class UProceduralMeshComponent> RouteMesh;

	/** Non-colliding public-space paving, kept separate from gravel route rendering. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Generation")
	TObjectPtr<class UProceduralMeshComponent> PavingMesh;

	/** Terrain-conforming, parcel-local farm surfaces with authored boundaries and furrow direction. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Generation")
	TObjectPtr<class UProceduralMeshComponent> FarmMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Generation")
	TObjectPtr<class UProceduralMeshComponent> WaterMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Generation")
	FString WorldFingerprint;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USmartObjectDefinition>> RuntimeSmartObjectDefinitions;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Inspection")
	TObjectPtr<class UWorldDirectorInspectionWidget> ActiveInspectionWidget;

private:
	bool BuildTerrainAndSurfaces(const FResolvedWorldPlan& Plan, FValidationReport& OutReport);
	void BuildRouteSurfaces(const FResolvedWorldPlan& Plan);
	void BuildDressing(const FResolvedWorldPlan& Plan, FValidationReport& OutReport);
	UInstancedStaticMeshComponent* FindOrCreateInstanceComponent(UStaticMeshComponent* Source);
	void CollapseActorToInstances(AActor* SourceActor);

	UPROPERTY(Transient)
	TMap<TObjectPtr<UStaticMesh>, TObjectPtr<UInstancedStaticMeshComponent>> WorldDirectorInstanceComponents;

	UPROPERTY(Transient)
	TMap<TObjectPtr<UStaticMesh>, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> DressingInstanceComponents;

	UPROPERTY(Transient)
	TArray<TObjectPtr<class ALandscapeProxy>> HiddenAuthoredLandscapes;

	UPROPERTY(Transient)
	FResolvedWorldPlan CompiledPlan;
};

USTRUCT(BlueprintType)
struct WORLDDIRECTORRUNTIME_API FWorldDirectorSavedWorldEntry
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Saved World")
	FString DisplayName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Saved World")
	FString RecipePath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Saved World")
	FString RunId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Saved World")
	int32 Seed = 0;
};

UCLASS(BlueprintType)
class WORLDDIRECTORRUNTIME_API AWorldDirectorFixtureBootstrap : public AActor
{
	GENERATED_BODY()

public:
	AWorldDirectorFixtureBootstrap();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "World Director|Fixture")
	bool CompileFixture();

	UFUNCTION(BlueprintCallable, Category = "World Director|Fixture")
	void RunAutomatedViabilityCheck();

	UFUNCTION(BlueprintCallable, Category = "World Director|Fixture")
	void RunAutomatedSimulationCheck();

	UFUNCTION(BlueprintCallable, Category = "World Director|Fixture")
	void BeginAutomatedTravelCheck();

	UFUNCTION(BlueprintCallable, Category = "World Director|Fixture")
	void FinishAutomatedTravelCheck();

	UFUNCTION(BlueprintCallable, Category = "World Director|Fixture")
	void RunAutomatedVisualCapture();

	UFUNCTION()
	void CaptureAutomatedVisualFrame();

	UFUNCTION()
	void FinishAutomatedVisualCapture();

	UFUNCTION()
	void HandleAutomatedGenerationFinished(bool bSuccess, const FString& RunId, const FString& Error);

	UFUNCTION()
	void HandlePlayerGenerationFinished(bool bSuccess, const FString& RunId, const FString& Error);

	UFUNCTION(BlueprintCallable, Category = "World Director|Player Flow")
	bool BeginPlayerWorldGeneration(
		const FString& PlayerPrompt,
		int32 Seed,
		bool bUseFixtureProviderForDebug = false,
		const FString& Model = FString(),
		const FString& ReasoningEffort = FString());

	UFUNCTION(BlueprintCallable, Category = "World Director|Player Flow")
	void CancelPlayerWorldGeneration();

	UFUNCTION(BlueprintCallable, Category = "World Director|Player Flow")
	void OpenWorldCreationMenu();
	UFUNCTION(BlueprintCallable, Category = "World Director|Player Flow")
	void ToggleWorldCreationMenu();
	UFUNCTION(BlueprintCallable, Category = "World Director|Player Flow")
	void CloseWorldCreationMenu();

	UFUNCTION(BlueprintCallable, Category = "World Director|Player Flow")
	void OpenLandingPage();
	UFUNCTION(BlueprintCallable, Category = "World Director|Player Flow")
	void CloseLandingPage();
	UFUNCTION(BlueprintCallable, Category = "World Director|Player Flow")
	void BeginSampleWorldPreview();

	UFUNCTION(BlueprintCallable, Category = "World Director|Player Flow")
	void RefreshSavedWorldCatalog();

	UFUNCTION(BlueprintCallable, Category = "World Director|Player Flow")
	void OpenSavedWorld(const FString& RecipePath);

	const TArray<FWorldDirectorSavedWorldEntry>& GetSavedWorldCatalog() const
	{
		return SavedWorldCatalog;
	}

	void OpenLoadingScreen(bool bForSampleWorld = false);
	void CloseLoadingScreen();

	UFUNCTION(BlueprintCallable, Category = "World Director|Player Flow")
	void OpenMapView();
	UFUNCTION(BlueprintCallable, Category = "World Director|Player Flow")
	void ToggleMapView();
	UFUNCTION(BlueprintCallable, Category = "World Director|Player Flow")
	void CloseMapView();

	UFUNCTION(BlueprintCallable, Category = "World Director|Player Flow")
	void OpenGenerationDiagnostics();
	UFUNCTION(BlueprintCallable, Category = "World Director|Player Flow")
	void ToggleGenerationDiagnostics();
	UFUNCTION(BlueprintCallable, Category = "World Director|Player Flow")
	void CloseGenerationDiagnostics();
	UFUNCTION(BlueprintCallable, Category = "World Director|Player Flow")
	void ToggleInteractionCursor();

	/** Re-evaluates the complete overlay stack after town-owned UI opens or closes. */
	void RefreshPlayerInputMode();

	UFUNCTION()
	void RunAutomatedGenerationViabilityCheck();

	UFUNCTION()
	void RunAutomatedPlayerFlowCheck();

	UFUNCTION()
	void RunAutomatedDialogueCheck();

	UFUNCTION()
	void BeginAutomatedProjectCheck();

	UFUNCTION()
	void FinishAutomatedProjectCheck();

	UFUNCTION()
	void CancelAutomatedGeneration();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Director|Fixture")
	FString FixtureFilename = TEXT("compiler-town.json");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Fixture")
	TObjectPtr<AWorldDirectorTownActor> CompiledTown;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Fixture")
	FValidationReport LastCompilationReport;

	UPROPERTY(Transient)
	TObjectPtr<class UWorldDirectorCreateWorldWidget> CreateWorldWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UWorldDirectorLandingWidget> LandingWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UWorldDirectorLoadingWidget> LoadingWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UWorldDirectorMapWidget> MapWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UWorldDirectorGenerationDiagnosticsWidget> GenerationDiagnosticsWidget;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Director|Saved World")
	TArray<FWorldDirectorSavedWorldEntry> SavedWorldCatalog;

private:
	bool CompileGeneratedWorld();
	void DestroyCurrentTown();
	void ApplyPlayerInputMode();
	bool bInteractionCursorMode = false;
	FTimerHandle CreationMenuRetryHandle;
	FTimerHandle SampleWorldCompileHandle;
	void CompileSampleWorldPreview();
	TMap<FString, FVector> TravelCheckStartLocations;
	bool bAutomatedGenerationPrompted = false;
	bool bAutomatedGenerationExpectsTimeout = false;
	bool bAutomatedGenerationExpectsCancellation = false;
	bool bPlayerFlowAutoTest = false;
	bool bPlayerFlowShouldRegenerate = false;
	int32 PlayerFlowGenerationCount = 0;
	int32 PlayerFlowNavigationRetryCount = 0;
	TWeakObjectPtr<AWorldDirectorTownActor> FirstPlayerFlowTown;
	TWeakObjectPtr<AWorldDirectorLocationActor> ProjectCheckTarget;
	TWeakObjectPtr<AActor> ProjectCheckOriginalInterior;
	TWeakObjectPtr<AWorldDirectorLocationActor> ProjectCheckFailedTarget;
	TWeakObjectPtr<class ACameraActor> AutomatedVisualCaptureCamera;
	FString AutomatedVisualCapturePath;
	FString AutomatedVisualCaptureView;
	FString ProjectCheckProjectId;
	int32 ProjectCheckInitialMemoryCount = 0;
	int32 ProjectCheckInitialBeliefCount = 0;
	bool bProjectCheckHasDecisionCases = false;
};
