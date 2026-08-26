#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "SmartObjectRuntime.h"
#include "WorldDirectorGeneration.h"
#include "WorldDirectorTypes.h"

#include "WorldDirectorSubsystems.generated.h"

USTRUCT(BlueprintType)
struct WORLDDIRECTORRUNTIME_API FWorldDirectorResidentRuntimeState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FString ResidentId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FString CurrentLocationId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FString IntendedLocationId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FName ActivityTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	EWorldDirectorResidentAvailability Availability = EWorldDirectorResidentAvailability::Available;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 CompletedScheduleTransitions = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 SuccessfulSmartObjectClaims = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 SmartObjectWaits = 0;
};

UCLASS()
class WORLDDIRECTORRUNTIME_API UCapabilityCatalogSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintCallable, Category = "World Director|Capabilities")
	void RegisterCapabilityTag(FName Tag);

	UFUNCTION(BlueprintPure, Category = "World Director|Capabilities")
	bool HasCapabilityTag(FName Tag) const;

	const TSet<FName>& GetCapabilityTags() const { return CapabilityTags; }

private:
	UPROPERTY()
	TSet<FName> CapabilityTags;
};

UCLASS()
class WORLDDIRECTORRUNTIME_API UWorldGenerationSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "World Director|Generation")
	bool LoadAndValidateWorldSpec(
		const FString& Json,
		FGeneratedWorldSpec& OutSpec,
		FValidationReport& OutReport) const;

	UFUNCTION(BlueprintCallable, Category = "World Director|Generation")
	bool SerializeWorldSpec(
		const FGeneratedWorldSpec& Spec,
		FString& OutJson,
		FValidationReport& OutReport) const;

	UFUNCTION(BlueprintCallable, Category = "World Director|Generation")
	FValidationReport ValidateWorldSpec(const FGeneratedWorldSpec& Spec) const;

	UFUNCTION(BlueprintCallable, Category = "World Director|Generation")
	FValidationReport ValidateFullSliceWorldSpec(const FGeneratedWorldSpec& Spec) const;

	UFUNCTION(BlueprintCallable, Category = "World Director|Generation")
	bool ResolveWorldSpec(
		const FGeneratedWorldSpec& Spec,
		FResolvedWorldPlan& OutPlan,
		FValidationReport& OutReport) const;

	UFUNCTION(BlueprintCallable, Category = "World Director|Generation", meta = (WorldContext = "WorldContextObject"))
	bool CompileResolvedWorld(
		UObject* WorldContextObject,
		const FResolvedWorldPlan& Plan,
		class AWorldDirectorTownActor*& OutTown,
		FValidationReport& OutReport) const;
};

UCLASS()
class WORLDDIRECTORRUNTIME_API UWorldStateSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "World Director|State")
	void SetActiveWorldSpec(const FGeneratedWorldSpec& Spec);

	UFUNCTION(BlueprintCallable, Category = "World Director|State")
	void ClearActiveWorldSpec();

	UFUNCTION(BlueprintPure, Category = "World Director|State")
	bool HasActiveWorldSpec() const { return bHasActiveWorldSpec; }

	UFUNCTION(BlueprintPure, Category = "World Director|State")
	const FGeneratedWorldSpec& GetActiveWorldSpec() const { return ActiveWorldSpec; }

private:
	UPROPERTY()
	bool bHasActiveWorldSpec = false;

	UPROPERTY()
	FGeneratedWorldSpec ActiveWorldSpec;
};

UCLASS()
class WORLDDIRECTORRUNTIME_API UTownSimulationSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "World Director|Simulation")
	void ShutdownLivingTown();
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "World Director|Simulation")
	bool InitializeLivingTown(
		const FGeneratedWorldSpec& Spec,
		class AWorldDirectorTownActor* Town,
		FValidationReport& OutReport);

	UFUNCTION(BlueprintCallable, Category = "World Director|Simulation")
	void SetSimulationEnabled(bool bEnabled) { bSimulationEnabled = bEnabled; }

	UFUNCTION(BlueprintPure, Category = "World Director|Simulation")
	bool IsSimulationEnabled() const { return bSimulationEnabled; }

	UFUNCTION(BlueprintCallable, Category = "World Director|Simulation")
	void AdvanceSimulationMinutes(int32 Minutes);

	UFUNCTION(BlueprintCallable, Category = "World Director|Simulation")
	void SetMinutesPerRealSecond(float Minutes) { MinutesPerRealSecond = FMath::Max(0.0f, Minutes); }

	UFUNCTION(BlueprintPure, Category = "World Director|Simulation")
	float GetMinutesPerRealSecond() const { return MinutesPerRealSecond; }

	UFUNCTION(BlueprintPure, Category = "World Director|Simulation")
	int64 GetElapsedSimulationMinutes() const { return ElapsedSimulationMinutes; }

	UFUNCTION(BlueprintPure, Category = "World Director|Simulation")
	int32 GetSimulationDay() const { return static_cast<int32>(ElapsedSimulationMinutes / 1440); }

	UFUNCTION(BlueprintPure, Category = "World Director|Simulation")
	int32 GetMinuteOfDay() const { return static_cast<int32>(ElapsedSimulationMinutes % 1440); }

	UFUNCTION(BlueprintPure, Category = "World Director|Simulation")
	const TArray<FWorldDirectorResidentRuntimeState>& GetResidentStates() const { return ResidentStates; }

	UFUNCTION(BlueprintPure, Category = "World Director|Simulation")
	int32 GetSharedBeliefCount() const { return SharedBeliefCount; }

	UFUNCTION(BlueprintPure, Category = "World Director|Simulation")
	int32 GetRelationshipEventCount() const { return RelationshipEventCount; }

	UFUNCTION(BlueprintPure, Category = "World Director|Simulation")
	int32 GetClosedFactCount() const { return ClosedFactIds.Num(); }

	const TArray<FBelief>* GetRuntimeBeliefsForResident(const FString& ResidentId) const
	{
		return RuntimeBeliefs.Find(ResidentId);
	}

	/** Invoked by the resident's StateTree task; returns false when no living-town state exists. */
	bool TickResidentLife(class AWorldDirectorResidentActor& Resident, float DeltaTime);

	FValidationReport ValidateMultiDayCoherence() const;

	void ApplyRepurposeConsequences(
		const FGeneratedWorldSpec& UpdatedSpec,
		const FChangeProject& Project);

private:
	void ResolveSimulationState(bool bTeleportActors);
	void ResolveResidentIntent(int32 ResidentIndex, bool bTeleportActor);
	void ReleaseResidentClaim(const FString& ResidentId);
	void ProcessBeliefSharing();
	void ProcessRelationshipEvents();
	void UpdateDayNightLighting() const;
	class AWorldDirectorLocationActor* FindLocation(const FString& LocationId) const;
	class AWorldDirectorResidentActor* FindResidentActor(const FString& ResidentId) const;
	const FResident* FindResidentSpec(const FString& ResidentId) const;
	FRelationship* FindMutableRelationship(const FString& SourceId, const FString& TargetId);

	UPROPERTY()
	bool bSimulationEnabled = false;

	UPROPERTY()
	int64 ElapsedSimulationMinutes = 0;

	UPROPERTY(EditAnywhere, Category = "World Director|Simulation")
	float MinutesPerRealSecond = 4.0f;

	UPROPERTY()
	float FractionalMinutes = 0.0f;

	UPROPERTY()
	FGeneratedWorldSpec LivingSpec;

	UPROPERTY()
	TObjectPtr<class AWorldDirectorTownActor> LivingTown;

	UPROPERTY()
	TArray<FWorldDirectorResidentRuntimeState> ResidentStates;

	TMap<FString, FSmartObjectClaimHandle> ResidentClaims;
	TMap<FString, FVector> ResidentClaimLocations;
	TMap<FString, TArray<FBelief>> RuntimeBeliefs;
	TSet<FString> ClosedFactIds;
	int32 LastBeliefSharingMinute = INDEX_NONE;
	int32 LastRelationshipEventDay = INDEX_NONE;
	int32 SharedBeliefCount = 0;
	int32 RelationshipEventCount = 0;
};

USTRUCT(BlueprintType)
struct WORLDDIRECTORRUNTIME_API FWorldDirectorProjectInspectionRecord
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FString ProjectId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FString Proposal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FString SimulationDecision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FString BeforeState;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FString AfterState;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TArray<FString> Lifecycle;
};

UCLASS()
class WORLDDIRECTORRUNTIME_API UChangeProjectSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	bool InitializeProjects(
		const FGeneratedWorldSpec& Spec,
		class AWorldDirectorTownActor* Town,
		FValidationReport& OutReport);
	void ShutdownProjects();

	UFUNCTION(BlueprintCallable, Category = "World Director|Change Projects")
	bool AddProject(const FChangeProject& Project);

	UFUNCTION(BlueprintCallable, Category = "World Director|Change Projects")
	void AdvanceProjects(int64 ElapsedSimulationMinutes);

	UFUNCTION(BlueprintPure, Category = "World Director|Change Projects")
	const TArray<FChangeProject>& GetProjects() const { return Projects; }

	UFUNCTION(BlueprintPure, Category = "World Director|Change Projects")
	const FGeneratedWorldSpec& GetRuntimeSpec() const { return RuntimeSpec; }

	UFUNCTION(BlueprintPure, Category = "World Director|Change Projects")
	const TArray<FWorldDirectorProjectInspectionRecord>& GetInspectionRecords() const
	{
		return InspectionRecords;
	}

private:
	bool ValidateProposal(FChangeProject& Project, FValidationReport& OutReport);
	bool ApplyActiveTransition(FChangeProject& Project, FValidationReport& OutReport);
	void SetProjectState(FChangeProject& Project, EWorldDirectorProjectState State);

	UPROPERTY()
	TArray<FChangeProject> Projects;

	UPROPERTY()
	FGeneratedWorldSpec RuntimeSpec;

	UPROPERTY()
	TObjectPtr<class AWorldDirectorTownActor> RuntimeTown;

	UPROPERTY()
	TArray<FWorldDirectorProjectInspectionRecord> InspectionRecords;

	TMap<FString, int64> PreparationStartMinutes;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FWorldDirectorGenerationFinished,
	bool, bSuccess,
	const FString&, RunId,
	const FString&, Error);

UCLASS()
class WORLDDIRECTORRUNTIME_API UDirectorBridgeSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "World Director|Bridge")
	void SetDirectorConnected(bool bConnected, const FString& InProviderName);

	UFUNCTION(BlueprintCallable, Category = "World Director|Generation")
	bool BeginWorldGeneration(
		const FString& PlayerPrompt,
		int32 Seed,
		float StageTimeoutSeconds = 300.0f,
		bool bUseFixtureProviderForTesting = false,
		const FString& Model = FString(),
		const FString& ReasoningEffort = FString());

	UFUNCTION(BlueprintCallable, Category = "World Director|Generation")
	void CancelWorldGeneration();

	UFUNCTION(BlueprintPure, Category = "World Director|Generation")
	bool IsGenerationRunning() const { return bGenerationRunning; }

	UFUNCTION(BlueprintPure, Category = "World Director|Generation")
	EWorldDirectorGenerationStage GetGenerationStage() const { return GenerationStage; }

	UFUNCTION(BlueprintPure, Category = "World Director|Generation")
	const FString& GetGenerationRunId() const { return GenerationRunId; }

	UFUNCTION(BlueprintPure, Category = "World Director|Generation")
	const FString& GetLastGenerationError() const { return LastGenerationError; }

	UFUNCTION(BlueprintPure, Category = "World Director|Generation")
	const FGeneratedWorldSpec& GetGeneratedWorldSpec() const { return GeneratedWorldSpec; }

	UFUNCTION(BlueprintPure, Category = "World Director|Generation")
	const TArray<FString>& GetGenerationStageHistory() const { return GenerationStageHistory; }

	UFUNCTION(BlueprintPure, Category = "World Director|Generation")
	const TArray<FValidationIssue>& GetGenerationIssueHistory() const { return GenerationIssueHistory; }

	const TArray<FWorldDirectorGenerationStageMetric>& GetGenerationMetrics() const { return GenerationMetrics; }
	const TArray<FWorldDirectorLayoutCandidate>& GetLayoutCandidates() const { return LayoutCandidates; }
	const FResolvedWorldPlan& GetLastResolvedWorldPlan() const { return LastResolvedWorldPlan; }
	const TArray<FString>& GetGenerationLog() const { return GenerationLog; }
	const FString& GetGenerationRunDirectory() const { return GenerationRunDirectory; }
	double GetGenerationElapsedSeconds() const;
	FString BuildGenerationDiagnosticReport() const;
	void RecordRuntimeCompilationMetric(
		bool bSuccess, double DurationSeconds, int32 LocationCount, int32 ResidentCount,
		const FString& Error);

	UFUNCTION(BlueprintPure, Category = "World Director|Generation")
	int32 GetGenerationRepairAttempts() const { return RepairAttempt; }

	UPROPERTY(BlueprintAssignable, Category = "World Director|Generation")
	FWorldDirectorGenerationFinished OnGenerationFinished;

	UFUNCTION(BlueprintPure, Category = "World Director|Bridge")
	bool IsDirectorConnected() const { return bDirectorConnected; }

	UFUNCTION(BlueprintPure, Category = "World Director|Bridge")
	const FString& GetProviderName() const { return ProviderName; }

private:
	void RequestCurrentStage();
	void HandleStageResponse(
		const FString& ExpectedRunId,
		const FString& ExpectedRequestId,
		EWorldDirectorGenerationStage ExpectedStage,
		FWorldDirectorProviderResponse&& Response);
	bool ApplyStagePayload(const TSharedPtr<class FJsonObject>& Payload, FString& OutError);
	bool BuildValidLayoutCandidates(FString& OutError);
	bool IntegrateAndValidate();
	bool ApplyTargetedRepairs(const TSharedPtr<class FJsonObject>& Payload, FString& OutError);
	void FinishGeneration(bool bSuccess, const FString& Error);
	void SaveValidationReport() const;
	void SaveRunSummary() const;
	void RecordGenerationLog(const FString& Message);
	FString StageWireName() const;
	int32 StageFileOrdinal() const;
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);

	UPROPERTY()
	bool bDirectorConnected = false;

	UPROPERTY()
	FString ProviderName;

	UPROPERTY()
	bool bGenerationRunning = false;

	UPROPERTY()
	bool bFixtureProviderForTesting = false;

	UPROPERTY()
	EWorldDirectorGenerationStage GenerationStage = EWorldDirectorGenerationStage::Idle;

	UPROPERTY()
	FString GenerationRunId;

	UPROPERTY()
	FString GenerationRunDirectory;

	UPROPERTY()
	FString GenerationPlayerPrompt;

	UPROPERTY()
	FString GenerationModel;

	UPROPERTY()
	FString GenerationReasoningEffort;

	UPROPERTY()
	int32 GenerationSeed = 0;

	UPROPERTY()
	float GenerationStageTimeoutSeconds = 300.0f;

	UPROPERTY()
	int32 RepairAttempt = 0;

	UPROPERTY()
	int32 ConsecutiveRepairIssueCount = 0;

	UPROPERTY()
	FString LastRepairIssueSignature;

	UPROPERTY()
	FString LastGenerationError;

	UPROPERTY()
	FString RuntimeCompileState = TEXT("NotAttempted");

	UPROPERTY()
	FString RuntimeCompileError;

	UPROPERTY()
	FGeneratedWorldSpec GeneratedWorldSpec;

	UPROPERTY()
	FResolvedWorldPlan LastResolvedWorldPlan;

	UPROPERTY()
	FValidationReport LastGenerationValidation;

	UPROPERTY()
	TArray<FWorldDirectorLayoutCandidate> LayoutCandidates;

	UPROPERTY()
	TArray<FString> GenerationStageHistory;

	UPROPERTY()
	TArray<FValidationIssue> GenerationIssueHistory;

	UPROPERTY()
	TArray<FWorldDirectorGenerationStageMetric> GenerationMetrics;

	UPROPERTY()
	TArray<FString> GenerationLog;

	double GenerationStartedAtSeconds = 0.0;
	double GenerationFinishedElapsedSeconds = 0.0;
	double CurrentStageStartedAtSeconds = 0.0;
	int32 CurrentMetricIndex = INDEX_NONE;

	FString SelectedLayoutCandidateId;
	FString ActiveGenerationRequestId;
	uint64 GenerationRequestSerial = 0;
	TSharedPtr<class FJsonObject> WorkingGenerationDocument;
	TSharedPtr<IWorldDirectorProvider> DirectorProvider;
	FDelegateHandle WorldCleanupHandle;
};
