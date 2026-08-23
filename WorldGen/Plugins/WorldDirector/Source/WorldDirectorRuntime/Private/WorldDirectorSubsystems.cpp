#include "WorldDirectorSubsystems.h"
#include "WorldDirectorRuntime.h"

#include "WorldDirectorJson.h"
#include "WorldDirectorPhysicalGenerator.h"
#include "WorldDirectorCompiler.h"
#include "WorldDirectorValidation.h"
#include "WorldDirectorTownActors.h"
#include "AIController.h"
#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "Serialization/JsonSerializer.h"
#include "Interfaces/IPluginManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StateTreeComponent.h"
#include "Engine/Engine.h"
#include "Engine/DirectionalLight.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "SmartObjectComponent.h"
#include "SmartObjectRequestTypes.h"
#include "SmartObjectSubsystem.h"

void UCapabilityCatalogSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const FName DefaultTags[] = {
		TEXT("Purpose.Home"),
		TEXT("Purpose.Workplace"),
		TEXT("Purpose.Landmark"),
		TEXT("Purpose.Clinic"),
		TEXT("Purpose.Shelter"),
		TEXT("Purpose.Headquarters"),
		TEXT("Occupation.Farmer"),
		TEXT("Occupation.Worker"),
		TEXT("Occupation.Merchant"),
		TEXT("Occupation.Guard"),
		TEXT("Occupation.Reeve"),
		TEXT("Occupation.Herbalist"),
		TEXT("Capability.Bed"),
		TEXT("Capability.WorkSurface"),
		TEXT("Capability.Chair"),
		TEXT("Capability.Counter"),
		TEXT("Capability.Door"),
		TEXT("Capability.Interior"),
		TEXT("Capability.Landmark"),
		TEXT("Capability.Character.Modular")
	};
	for (const FName Tag : DefaultTags)
	{
		CapabilityTags.Add(Tag);
	}
}

void UCapabilityCatalogSubsystem::RegisterCapabilityTag(const FName Tag)
{
	if (!Tag.IsNone())
	{
		CapabilityTags.Add(Tag);
	}
}

bool UCapabilityCatalogSubsystem::HasCapabilityTag(const FName Tag) const
{
	return CapabilityTags.Contains(Tag);
}

bool UWorldGenerationSubsystem::LoadAndValidateWorldSpec(
	const FString& Json,
	FGeneratedWorldSpec& OutSpec,
	FValidationReport& OutReport) const
{
	if (!FWorldDirectorJson::LoadGeneratedWorldSpec(Json, OutSpec, OutReport))
	{
		return false;
	}
	OutReport = ValidateWorldSpec(OutSpec);
	return OutReport.bValid;
}

bool UWorldGenerationSubsystem::SerializeWorldSpec(
	const FGeneratedWorldSpec& Spec,
	FString& OutJson,
	FValidationReport& OutReport) const
{
	OutReport = ValidateWorldSpec(Spec);
	if (!OutReport.bValid)
	{
		return false;
	}
	return FWorldDirectorJson::SaveGeneratedWorldSpec(Spec, OutJson, OutReport);
}

FValidationReport UWorldGenerationSubsystem::ValidateWorldSpec(
	const FGeneratedWorldSpec& Spec) const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UCapabilityCatalogSubsystem* Catalog =
		GameInstance ? GameInstance->GetSubsystem<UCapabilityCatalogSubsystem>() : nullptr;
	const TSet<FName> EmptyTags;
	return FWorldDirectorValidator::Validate(
		Spec,
		Catalog ? Catalog->GetCapabilityTags() : EmptyTags);
}

FValidationReport UWorldGenerationSubsystem::ValidateFullSliceWorldSpec(
	const FGeneratedWorldSpec& Spec) const
{
	FValidationReport Report = ValidateWorldSpec(Spec);
	if (Spec.Residents.Num() < 20 || Spec.Residents.Num() > 30)
	{
		Report.AddError(TEXT("slice.population_count"), TEXT("$.residents"),
			TEXT("The full vertical slice requires 20 to 30 residents."));
	}
	if (Spec.Locations.Num() < 12 || Spec.Locations.Num() > 18)
	{
		Report.AddError(TEXT("slice.location_count"), TEXT("$.locations"),
			TEXT("The full vertical slice requires 12 to 18 buildings."));
	}
	bool bHasHome = false;
	bool bHasWorkplace = false;
	bool bHasPublic = false;
	bool bHasLandmark = false;
	int32 HomeLocationCount = 0;
	for (const FWorldLocation& Location : Spec.Locations)
	{
		bHasHome |= Location.PurposeTag == TEXT("Purpose.Home");
		HomeLocationCount += Location.PurposeTag == TEXT("Purpose.Home");
		bHasWorkplace |= Location.PurposeTag == TEXT("Purpose.Workplace");
		bHasPublic |= Location.AccessPolicy == EWorldDirectorAccessPolicy::Public;
		bHasLandmark |= Location.PurposeTag == TEXT("Purpose.Landmark");
	}
	if (HomeLocationCount > 6)
	{
		Report.AddError(TEXT("slice.home_plot_count"), TEXT("$.locations"),
			TEXT("The certified full-slice layout supports at most six dedicated home plots."));
	}
	if (!bHasHome || !bHasWorkplace || !bHasPublic || !bHasLandmark)
	{
		Report.AddError(TEXT("slice.location_mix"), TEXT("$.locations"),
			TEXT("Full-slice locations require homes, workplaces, public buildings, and a landmark."));
	}
	if (Spec.Threats.IsEmpty())
	{
		Report.AddError(TEXT("slice.central_threat_missing"), TEXT("$.threats"),
			TEXT("The full vertical slice requires one initial central threat."));
	}
	if (Spec.ChangeProjects.Num() != 1 ||
		(!Spec.ChangeProjects.IsEmpty() &&
		 Spec.ChangeProjects[0].State != EWorldDirectorProjectState::Proposed))
	{
		Report.AddError(TEXT("slice.project_proposal_count"), TEXT("$.changeProjects"),
			TEXT("The full vertical slice requires exactly one Proposed change project."));
	}

	TMap<FString, TSet<FString>> Adjacency;
	for (const FResident& Resident : Spec.Residents)
	{
		Adjacency.Add(Resident.Id);
		if (Resident.ImportantMemories.IsEmpty() || Resident.BeliefIds.IsEmpty() ||
			Resident.RelationshipIds.IsEmpty())
		{
			Report.AddError(TEXT("slice.resident_social_state"),
				FString::Printf(TEXT("$.residents[%d]"),
					static_cast<int32>(&Resident - Spec.Residents.GetData())),
				TEXT("Every resident requires memories, beliefs, and at least one relationship."));
		}
	}
	for (const FRelationship& Relationship : Spec.Relationships)
	{
		Adjacency.FindOrAdd(Relationship.SourceResidentId).Add(Relationship.TargetResidentId);
		Adjacency.FindOrAdd(Relationship.TargetResidentId).Add(Relationship.SourceResidentId);
	}
	if (!Spec.Residents.IsEmpty())
	{
		TSet<FString> Visited;
		TArray<FString> Pending = {Spec.Residents[0].Id};
		while (!Pending.IsEmpty())
		{
			const FString Current = Pending.Pop(EAllowShrinking::No);
			if (Visited.Contains(Current))
			{
				continue;
			}
			Visited.Add(Current);
			if (const TSet<FString>* Neighbors = Adjacency.Find(Current))
			{
				for (const FString& Neighbor : *Neighbors)
				{
					if (!Visited.Contains(Neighbor))
					{
						Pending.Add(Neighbor);
					}
				}
			}
		}
		if (Visited.Num() != Spec.Residents.Num())
		{
			Report.AddError(TEXT("slice.relationship_network_disconnected"), TEXT("$.relationships"),
				TEXT("The resident relationship network must form one connected social graph."));
		}
	}
	Report.bValid = !Report.Issues.ContainsByPredicate(
		[](const FValidationIssue& Issue)
		{ return Issue.Severity == EWorldDirectorValidationSeverity::Error; });
	return Report;
}

bool UWorldGenerationSubsystem::ResolveWorldSpec(
	const FGeneratedWorldSpec& Spec,
	FResolvedWorldPlan& OutPlan,
	FValidationReport& OutReport) const
{
	OutReport = ValidateWorldSpec(Spec);
	if (!OutReport.bValid)
	{
		return false;
	}
	return FWorldDirectorCompiler::Resolve(Spec, OutPlan, OutReport);
}

bool UWorldGenerationSubsystem::CompileResolvedWorld(
	UObject* WorldContextObject,
	const FResolvedWorldPlan& Plan,
	AWorldDirectorTownActor*& OutTown,
	FValidationReport& OutReport) const
{
	UWorld* World = WorldContextObject
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	return FWorldDirectorCompiler::Spawn(World, Plan, OutTown, OutReport);
}

void UWorldStateSubsystem::SetActiveWorldSpec(const FGeneratedWorldSpec& Spec)
{
	ActiveWorldSpec = Spec;
	bHasActiveWorldSpec = true;
}

void UWorldStateSubsystem::ClearActiveWorldSpec()
{
	ActiveWorldSpec = FGeneratedWorldSpec();
	bHasActiveWorldSpec = false;
}

void UTownSimulationSubsystem::Tick(const float DeltaTime)
{
	if (!bSimulationEnabled || DeltaTime <= 0.0f)
	{
		return;
	}
	FractionalMinutes += DeltaTime * MinutesPerRealSecond;
	const int32 WholeMinutes = FMath::FloorToInt(FractionalMinutes);
	if (WholeMinutes > 0)
	{
		FractionalMinutes -= WholeMinutes;
		AdvanceSimulationMinutes(WholeMinutes);
	}
}

TStatId UTownSimulationSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTownSimulationSubsystem, STATGROUP_Tickables);
}

bool UTownSimulationSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UTownSimulationSubsystem::Deinitialize()
{
	ShutdownLivingTown();
	Super::Deinitialize();
}

void UTownSimulationSubsystem::ShutdownLivingTown()
{
	for (const TPair<FString, FSmartObjectClaimHandle>& Pair : ResidentClaims)
	{
		if (USmartObjectSubsystem* SmartObjects = GetWorld()
			? GetWorld()->GetSubsystem<USmartObjectSubsystem>() : nullptr)
		{
			SmartObjects->MarkSlotAsFree(Pair.Value);
		}
	}
	ResidentClaims.Reset();
	ResidentClaimLocations.Reset();
	LivingTown = nullptr;
	bSimulationEnabled = false;
	ResidentStates.Reset();
	RuntimeBeliefs.Reset();
	ClosedFactIds.Reset();
}

bool UTownSimulationSubsystem::InitializeLivingTown(
	const FGeneratedWorldSpec& Spec,
	AWorldDirectorTownActor* Town,
	FValidationReport& OutReport)
{
	if (Town == nullptr || Spec.Residents.Num() < 20 || Spec.Residents.Num() > 30)
	{
		OutReport.AddError(TEXT("simulation.population_count"), TEXT("residents"),
			TEXT("Living-town simulation requires 20 to 30 compiled residents."));
		return false;
	}
	for (int32 Index = 0; Index < Spec.Residents.Num(); ++Index)
	{
		const FResident& Resident = Spec.Residents[Index];
		if (Resident.Id.IsEmpty() || Resident.HomeLocationId.IsEmpty() ||
			Resident.WorkplaceLocationId.IsEmpty() || Resident.OccupationTag.IsNone() ||
			Resident.Motivation.IsEmpty() || Resident.Fear.IsEmpty())
		{
			OutReport.AddError(TEXT("simulation.resident_incomplete"),
				FString::Printf(TEXT("residents[%d]"), Index),
				TEXT("A living resident requires identity, home, occupation/dependency, motivation, and fear."));
		}
	}
	if (!OutReport.bValid)
	{
		return false;
	}
	LivingSpec = Spec;
	LivingTown = Town;
	ElapsedSimulationMinutes = 6 * 60;
	FractionalMinutes = 0.0f;
	ResidentStates.Reset();
	RuntimeBeliefs.Reset();
	ClosedFactIds.Reset();
	SharedBeliefCount = 0;
	RelationshipEventCount = 0;
	LastBeliefSharingMinute = INDEX_NONE;
	LastRelationshipEventDay = INDEX_NONE;
	for (const FWorldFact& Fact : LivingSpec.Facts)
	{
		ClosedFactIds.Add(Fact.Id);
	}
	for (const FResident& Resident : LivingSpec.Residents)
	{
		FWorldDirectorResidentRuntimeState& State = ResidentStates.AddDefaulted_GetRef();
		State.ResidentId = Resident.Id;
		State.CurrentLocationId = Resident.CurrentLocationId.IsEmpty()
			? Resident.HomeLocationId : Resident.CurrentLocationId;
		State.IntendedLocationId = State.CurrentLocationId;
		State.Availability = Resident.Availability;
		TArray<FBelief>& Beliefs = RuntimeBeliefs.Add(Resident.Id);
		for (const FString& BeliefId : Resident.BeliefIds)
		{
			if (const FBelief* Belief = LivingSpec.Beliefs.FindByPredicate(
				[&BeliefId](const FBelief& Candidate) { return Candidate.Id == BeliefId; }))
			{
				Beliefs.Add(*Belief);
			}
		}
	}
	if (!Town->BuildActivityStations(Spec, OutReport))
	{
		return false;
	}
	if (UChangeProjectSubsystem* ChangeProjects = GetWorld()->GetSubsystem<UChangeProjectSubsystem>())
	{
		if (!ChangeProjects->InitializeProjects(Spec, Town, OutReport))
		{
			return false;
		}
	}
	bSimulationEnabled = true;
	ResolveSimulationState(true);
	UpdateDayNightLighting();
	UE_LOG(LogWorldDirector, Display,
		TEXT("WORLD_DIRECTOR_SIMULATION_INITIALIZED residents=%d facts=%d stateTreeAsset=/Game/WorldDirector/AI/ST_ResidentLife"),
		ResidentStates.Num(), ClosedFactIds.Num());
	return true;
}

void UTownSimulationSubsystem::AdvanceSimulationMinutes(const int32 Minutes)
{
	if (!bSimulationEnabled || Minutes <= 0)
	{
		return;
	}
	ElapsedSimulationMinutes += Minutes;
	ResolveSimulationState(Minutes >= 60);
	if (UChangeProjectSubsystem* ChangeProjects = GetWorld()->GetSubsystem<UChangeProjectSubsystem>())
	{
		ChangeProjects->AdvanceProjects(ElapsedSimulationMinutes);
	}
	ProcessBeliefSharing();
	ProcessRelationshipEvents();
	UpdateDayNightLighting();
}

void UTownSimulationSubsystem::ResolveSimulationState(const bool bTeleportActors)
{
	for (int32 Index = 0; Index < ResidentStates.Num(); ++Index)
	{
		ResolveResidentIntent(Index, bTeleportActors);
	}
}

void UTownSimulationSubsystem::ResolveResidentIntent(
	const int32 ResidentIndex,
	const bool bTeleportActor)
{
	if (!ResidentStates.IsValidIndex(ResidentIndex) || !LivingSpec.Residents.IsValidIndex(ResidentIndex))
	{
		return;
	}
	FWorldDirectorResidentRuntimeState& State = ResidentStates[ResidentIndex];
	const FResident& ResidentSpec = LivingSpec.Residents[ResidentIndex];
	AWorldDirectorResidentActor* Resident = FindResidentActor(State.ResidentId);
	if (Resident == nullptr)
	{
		return;
	}
	const int32 Hour = GetMinuteOfDay() / 60;
	const FWorldDirectorScheduleStop* SelectedStop = nullptr;
	for (const FWorldDirectorScheduleStop& Stop : Resident->Schedule)
	{
		if (Stop.Hour <= Hour && (SelectedStop == nullptr || Stop.Hour >= SelectedStop->Hour))
		{
			SelectedStop = &Stop;
		}
	}
	if (SelectedStop == nullptr && !Resident->Schedule.IsEmpty())
	{
		SelectedStop = &Resident->Schedule.Last();
	}
	const FString IntendedLocation = SelectedStop ? SelectedStop->LocationId : ResidentSpec.HomeLocationId;
	const FName Activity = Hour < 8 || Hour >= 21 ? FName(TEXT("Activity.Sleep"))
		: Hour < 18 ? FName(TEXT("Activity.Work")) : FName(TEXT("Activity.Meet"));
	if (State.IntendedLocationId != IntendedLocation || State.ActivityTag != Activity)
	{
		ReleaseResidentClaim(State.ResidentId);
		State.IntendedLocationId = IntendedLocation;
		State.ActivityTag = Activity;
		State.CompletedScheduleTransitions++;
	}
	Resident->IntendedLocationId = State.IntendedLocationId;
	Resident->CurrentActivityTag = State.ActivityTag;

	FVector Destination = FVector::ZeroVector;
	bool bHasDestination = false;
	if (const FVector* ExistingClaimLocation = ResidentClaimLocations.Find(State.ResidentId))
	{
		Destination = *ExistingClaimLocation;
		bHasDestination = true;
	}
	else if (USmartObjectSubsystem* SmartObjects = GetWorld()->GetSubsystem<USmartObjectSubsystem>())
	{
		if (AWorldDirectorLocationActor* Location = FindLocation(State.IntendedLocationId))
		{
			FSmartObjectRequestFilter Filter;
			const FGameplayTag GameplayActivity = FGameplayTag::RequestGameplayTag(Activity, false);
			Filter.ActivityRequirements = FGameplayTagQuery::MakeQuery_MatchTag(GameplayActivity);
			Filter.bShouldIncludeClaimedSlots = false;
			const FVector Center = Location->GetActorLocation();
			const FSmartObjectRequest Request(FBox(Center - FVector(1800.0f), Center + FVector(1800.0f)), Filter);
			TArray<FSmartObjectRequestResult> Results;
			SmartObjects->FindSmartObjects(Request, Results, Resident);
			Results.Sort([SmartObjects, Resident](const FSmartObjectRequestResult& A, const FSmartObjectRequestResult& B)
			{
				const TOptional<FVector> ALocation = SmartObjects->GetSlotLocation(A);
				const TOptional<FVector> BLocation = SmartObjects->GetSlotLocation(B);
				return FVector::DistSquared(Resident->GetActorLocation(), ALocation.Get(FVector::ZeroVector)) <
					FVector::DistSquared(Resident->GetActorLocation(), BLocation.Get(FVector::ZeroVector));
			});
			for (const FSmartObjectRequestResult& Result : Results)
			{
				USmartObjectComponent* Component = SmartObjects->GetSmartObjectComponentByRequestResult(Result);
				const AWorldDirectorActivityStationActor* Station = Component
					? Cast<AWorldDirectorActivityStationActor>(Component->GetOwner()) : nullptr;
				if (Station == nullptr || Station->LocationId != State.IntendedLocationId)
				{
					continue;
				}
				const FSmartObjectClaimHandle Claim = SmartObjects->MarkSlotAsClaimed(
					Result.SlotHandle, ESmartObjectClaimPriority::Normal);
				if (!Claim.IsValid())
				{
					continue;
				}
				const TOptional<FVector> SlotLocation = SmartObjects->GetSlotLocation(Result);
				if (!SlotLocation.IsSet())
				{
					SmartObjects->MarkSlotAsFree(Claim);
					continue;
				}
				ResidentClaims.Add(State.ResidentId, Claim);
				ResidentClaimLocations.Add(State.ResidentId, SlotLocation.GetValue());
				State.SuccessfulSmartObjectClaims++;
				Destination = SlotLocation.GetValue();
				bHasDestination = true;
				break;
			}
			if (!bHasDestination)
			{
				State.Availability = EWorldDirectorResidentAvailability::Waiting;
				State.SmartObjectWaits++;
				Resident->Availability = State.Availability;
				return;
			}
		}
	}

	if (!bHasDestination)
	{
		State.Availability = EWorldDirectorResidentAvailability::Waiting;
		Resident->Availability = State.Availability;
		return;
	}
	const float Distance = FVector::Dist2D(Resident->GetActorLocation(), Destination);
	if (bTeleportActor)
	{
		Resident->SetActorLocation(Destination + FVector(0.0f, 0.0f, 95.0f), false, nullptr,
			ETeleportType::TeleportPhysics);
	}
	else if (Distance > 120.0f)
	{
		if (AAIController* Controller = Cast<AAIController>(Resident->GetController()))
		{
			Controller->MoveToLocation(Destination, 90.0f, true, true, true, false);
		}
	}
	const bool bArrived = bTeleportActor || Distance <= 120.0f;
	State.Availability = bArrived
		? (Activity == TEXT("Activity.Sleep") ? EWorldDirectorResidentAvailability::Sleeping
			: Activity == TEXT("Activity.Work") ? EWorldDirectorResidentAvailability::Working
			: EWorldDirectorResidentAvailability::Socializing)
		: EWorldDirectorResidentAvailability::Traveling;
	if (bArrived)
	{
		State.CurrentLocationId = State.IntendedLocationId;
	}
	Resident->Availability = State.Availability;
	Resident->CurrentLocationId = State.CurrentLocationId;
}

bool UTownSimulationSubsystem::TickResidentLife(
	AWorldDirectorResidentActor& Resident,
	const float DeltaTime)
{
	if (!bSimulationEnabled || LivingTown == nullptr)
	{
		return false;
	}
	const int32 Index = ResidentStates.IndexOfByPredicate(
		[&Resident](const FWorldDirectorResidentRuntimeState& State)
		{
			return State.ResidentId == Resident.ResidentId;
		});
	if (Index == INDEX_NONE)
	{
		return false;
	}
	ResolveResidentIntent(Index, false);
	return true;
}

void UTownSimulationSubsystem::ReleaseResidentClaim(const FString& ResidentId)
{
	if (FSmartObjectClaimHandle* Claim = ResidentClaims.Find(ResidentId))
	{
		if (USmartObjectSubsystem* SmartObjects = GetWorld()->GetSubsystem<USmartObjectSubsystem>())
		{
			SmartObjects->MarkSlotAsFree(*Claim);
		}
		ResidentClaims.Remove(ResidentId);
	}
	ResidentClaimLocations.Remove(ResidentId);
}

void UTownSimulationSubsystem::ProcessBeliefSharing()
{
	const int32 CurrentWindow = static_cast<int32>(ElapsedSimulationMinutes / 30);
	if (CurrentWindow == LastBeliefSharingMinute)
	{
		return;
	}
	LastBeliefSharingMinute = CurrentWindow;
	for (const FWorldDirectorResidentRuntimeState& SpeakerState : ResidentStates)
	{
		if (SpeakerState.Availability != EWorldDirectorResidentAvailability::Socializing)
		{
			continue;
		}
		for (const FWorldDirectorResidentRuntimeState& ListenerState : ResidentStates)
		{
			if (SpeakerState.ResidentId == ListenerState.ResidentId ||
				SpeakerState.CurrentLocationId != ListenerState.CurrentLocationId ||
				ListenerState.Availability != EWorldDirectorResidentAvailability::Socializing)
			{
				continue;
			}
			const FRelationship* Relationship = LivingSpec.Relationships.FindByPredicate(
				[&SpeakerState, &ListenerState](const FRelationship& Candidate)
				{
					return Candidate.SourceResidentId == SpeakerState.ResidentId &&
						Candidate.TargetResidentId == ListenerState.ResidentId;
				});
			if (Relationship == nullptr || Relationship->Trust < 0.55f)
			{
				continue;
			}
			const TArray<FBelief>* SpeakerBeliefs = RuntimeBeliefs.Find(SpeakerState.ResidentId);
			TArray<FBelief>* ListenerBeliefs = RuntimeBeliefs.Find(ListenerState.ResidentId);
			if (SpeakerBeliefs == nullptr || ListenerBeliefs == nullptr)
			{
				continue;
			}
			for (const FBelief& Belief : *SpeakerBeliefs)
			{
				if (Belief.WillingnessToShare < 0.55f || Belief.Secrecy > Relationship->Trust ||
					ListenerBeliefs->ContainsByPredicate(
						[&Belief](const FBelief& Candidate) { return Candidate.FactId == Belief.FactId; }))
				{
					continue;
				}
				FBelief Shared = Belief;
				Shared.Id = FString::Printf(TEXT("runtime.%s.%s.%d"), *ListenerState.ResidentId,
					*Belief.FactId, SharedBeliefCount);
				Shared.HolderResidentId = ListenerState.ResidentId;
				Shared.SourceResidentId = SpeakerState.ResidentId;
				Shared.Confidence = FMath::Clamp(Belief.Confidence * 0.8f, 0.0f, 1.0f);
				ListenerBeliefs->Add(Shared);
				SharedBeliefCount++;
				break;
			}
		}
	}
}

void UTownSimulationSubsystem::ProcessRelationshipEvents()
{
	const int32 Day = GetSimulationDay();
	if (Day == LastRelationshipEventDay || GetMinuteOfDay() < 12 * 60)
	{
		return;
	}
	LastRelationshipEventDay = Day;
	for (FRelationship& Relationship : LivingSpec.Relationships)
	{
		const FWorldDirectorResidentRuntimeState* Source = ResidentStates.FindByPredicate(
			[&Relationship](const FWorldDirectorResidentRuntimeState& State)
			{ return State.ResidentId == Relationship.SourceResidentId; });
		const FWorldDirectorResidentRuntimeState* Target = ResidentStates.FindByPredicate(
			[&Relationship](const FWorldDirectorResidentRuntimeState& State)
			{ return State.ResidentId == Relationship.TargetResidentId; });
		if (Source != nullptr && Target != nullptr &&
			Source->CurrentLocationId == Target->CurrentLocationId)
		{
			Relationship.Trust = FMath::Clamp(Relationship.Trust + 0.01f, 0.0f, 1.0f);
			Relationship.Affinity = FMath::Clamp(Relationship.Affinity + 0.02f, 0.0f, 1.0f);
			RelationshipEventCount++;
		}
	}
}

void UTownSimulationSubsystem::UpdateDayNightLighting() const
{
	if (UWorld* World = GetWorld())
	{
		const float DayFraction = static_cast<float>(GetMinuteOfDay()) / 1440.0f;
		const float Pitch = 90.0f - DayFraction * 360.0f;
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			if (UDirectionalLightComponent* Light = Cast<UDirectionalLightComponent>(It->GetLightComponent());
				Light != nullptr && Light->bAtmosphereSunLight)
			{
				It->SetActorRotation(FRotator(Pitch, -32.0f, 0.0f));
				break;
			}
		}
	}
}

AWorldDirectorLocationActor* UTownSimulationSubsystem::FindLocation(const FString& LocationId) const
{
	if (LivingTown == nullptr)
	{
		return nullptr;
	}
	const TObjectPtr<AWorldDirectorLocationActor>* Match = LivingTown->SpawnedLocations.FindByPredicate(
		[&LocationId](const AWorldDirectorLocationActor* Location)
		{ return Location != nullptr && Location->LocationId == LocationId; });
	return Match ? Match->Get() : nullptr;
}

AWorldDirectorResidentActor* UTownSimulationSubsystem::FindResidentActor(const FString& ResidentId) const
{
	if (LivingTown == nullptr)
	{
		return nullptr;
	}
	const TObjectPtr<AWorldDirectorResidentActor>* Match = LivingTown->SpawnedResidents.FindByPredicate(
		[&ResidentId](const AWorldDirectorResidentActor* Resident)
		{ return Resident != nullptr && Resident->ResidentId == ResidentId; });
	return Match ? Match->Get() : nullptr;
}

const FResident* UTownSimulationSubsystem::FindResidentSpec(const FString& ResidentId) const
{
	return LivingSpec.Residents.FindByPredicate(
		[&ResidentId](const FResident& Resident) { return Resident.Id == ResidentId; });
}

FRelationship* UTownSimulationSubsystem::FindMutableRelationship(
	const FString& SourceId,
	const FString& TargetId)
{
	return LivingSpec.Relationships.FindByPredicate(
		[&SourceId, &TargetId](const FRelationship& Relationship)
		{
			return Relationship.SourceResidentId == SourceId && Relationship.TargetResidentId == TargetId;
		});
}

FValidationReport UTownSimulationSubsystem::ValidateMultiDayCoherence() const
{
	FValidationReport Report;
	if (!bSimulationEnabled || LivingTown == nullptr)
	{
		Report.AddError(TEXT("simulation.not_initialized"), TEXT("simulation"),
			TEXT("Living-town simulation is not initialized."));
		return Report;
	}
	if (ResidentStates.Num() < 20 || ResidentStates.Num() > 30)
	{
		Report.AddError(TEXT("simulation.population_count"), TEXT("residentStates"),
			TEXT("Runtime population is outside the 20 to 30 resident contract."));
	}
	for (int32 Index = 0; Index < ResidentStates.Num(); ++Index)
	{
		const FWorldDirectorResidentRuntimeState& State = ResidentStates[Index];
		if (State.CompletedScheduleTransitions < 3)
		{
			Report.AddError(TEXT("simulation.schedule_not_exercised"),
				FString::Printf(TEXT("residentStates[%d]"), Index),
				TEXT("Resident did not resolve enough schedule transitions."));
		}
		if (State.SuccessfulSmartObjectClaims == 0)
		{
			Report.AddError(TEXT("simulation.smart_object_unused"),
				FString::Printf(TEXT("residentStates[%d]"), Index),
				TEXT("Resident never claimed an appropriate Smart Object."));
		}
		const AWorldDirectorResidentActor* ResidentActor = FindResidentActor(State.ResidentId);
		if (ResidentActor == nullptr || ResidentActor->StateTreeComponent == nullptr ||
			!ResidentActor->StateTreeComponent->IsRunning())
		{
			Report.AddError(TEXT("simulation.statetree_not_running"),
				FString::Printf(TEXT("residentStates[%d].stateTree"), Index),
				TEXT("Resident is not running the compiled resident-life StateTree."));
		}
		if (FindLocation(State.CurrentLocationId) == nullptr)
		{
			Report.AddError(TEXT("simulation.location_invalid"),
				FString::Printf(TEXT("residentStates[%d].currentLocationId"), Index),
				TEXT("Resident runtime location is not a compiled location."));
		}
	}
	if (ClosedFactIds.Num() != LivingSpec.Facts.Num())
	{
		Report.AddError(TEXT("simulation.fact_set_mutated"), TEXT("facts"),
			TEXT("The generation-time fact set changed during runtime simulation."));
	}
	if (SharedBeliefCount == 0)
	{
		Report.AddError(TEXT("simulation.belief_rule_unexercised"), TEXT("beliefs"),
			TEXT("No reduced-confidence belief was shared during co-located trusted meetings."));
	}
	Report.bValid = !Report.Issues.ContainsByPredicate(
		[](const FValidationIssue& Issue)
		{ return Issue.Severity == EWorldDirectorValidationSeverity::Error; });
	return Report;
}

void UTownSimulationSubsystem::ApplyRepurposeConsequences(
	const FGeneratedWorldSpec& UpdatedSpec,
	const FChangeProject& Project)
{
	LivingSpec = UpdatedSpec;
	for (const FString& ResidentId : Project.RequiredParticipantResidentIds)
	{
		ReleaseResidentClaim(ResidentId);
	}
	RuntimeBeliefs.Reset();
	for (const FResident& ResidentSpec : LivingSpec.Residents)
	{
		TArray<FBelief>& ResidentBeliefs = RuntimeBeliefs.Add(ResidentSpec.Id);
		for (const FString& BeliefId : ResidentSpec.BeliefIds)
		{
			if (const FBelief* Belief = LivingSpec.Beliefs.FindByPredicate(
				[&BeliefId](const FBelief& Candidate) { return Candidate.Id == BeliefId; }))
			{
				ResidentBeliefs.Add(*Belief);
			}
		}
		if (AWorldDirectorResidentActor* ResidentActor = FindResidentActor(ResidentSpec.Id))
		{
			const bool bProjectParticipant =
				Project.RequiredParticipantResidentIds.Contains(ResidentSpec.Id);
			ResidentActor->HomeLocationId = ResidentSpec.HomeLocationId;
			ResidentActor->WorkplaceLocationId = ResidentSpec.WorkplaceLocationId;
			ResidentActor->Schedule = {
				{0, ResidentSpec.HomeLocationId},
				{8, ResidentSpec.WorkplaceLocationId},
				{18, bProjectParticipant ? Project.TargetLocationId :
					LivingTown ? LivingTown->LandmarkLocationId : ResidentSpec.WorkplaceLocationId},
				{21, ResidentSpec.HomeLocationId}
			};
		}
	}
}

bool UChangeProjectSubsystem::InitializeProjects(
	const FGeneratedWorldSpec& Spec,
	AWorldDirectorTownActor* Town,
	FValidationReport& OutReport)
{
	Projects.Reset();
	PreparationStartMinutes.Reset();
	InspectionRecords.Reset();
	RuntimeSpec = Spec;
	RuntimeTown = Town;
	for (const FChangeProject& Project : Spec.ChangeProjects)
	{
		if (!AddProject(Project))
		{
			OutReport.AddError(TEXT("project.duplicate"), Project.Id,
				TEXT("Change project could not be registered in the running world."));
		}
	}
	OutReport.bValid = !OutReport.Issues.ContainsByPredicate(
		[](const FValidationIssue& Issue)
		{ return Issue.Severity == EWorldDirectorValidationSeverity::Error; });
	return OutReport.bValid;
}

void UChangeProjectSubsystem::ShutdownProjects()
{
	Projects.Reset();
	PreparationStartMinutes.Reset();
	InspectionRecords.Reset();
	RuntimeSpec = FGeneratedWorldSpec();
	RuntimeTown = nullptr;
}

bool UChangeProjectSubsystem::AddProject(const FChangeProject& Project)
{
	if (Project.Id.IsEmpty() || Projects.ContainsByPredicate(
		[&Project](const FChangeProject& Existing) { return Existing.Id == Project.Id; }))
	{
		return false;
	}
	Projects.Add(Project);
	FWorldDirectorProjectInspectionRecord& Record = InspectionRecords.AddDefaulted_GetRef();
	Record.ProjectId = Project.Id;
	Record.Proposal = FString::Printf(
		TEXT("%s proposes %s at %s because %s"),
		*Project.InitiatorResidentId, *Project.DesiredPurposeTag.ToString(),
		*Project.TargetLocationId, *Project.Reason);
	if (const FWorldLocation* Target = RuntimeSpec.Locations.FindByPredicate(
		[&Project](const FWorldLocation& Location) { return Location.Id == Project.TargetLocationId; }))
	{
		Record.BeforeState = FString::Printf(TEXT("purpose=%s owner=%s controller=%s access=%d"),
			*Target->PurposeTag.ToString(), *Target->OwnerResidentId,
			*Target->ControllerResidentId, static_cast<int32>(Target->AccessPolicy));
	}
	Record.Lifecycle.Add(TEXT("Proposed"));
	return true;
}

void UChangeProjectSubsystem::SetProjectState(
	FChangeProject& Project,
	const EWorldDirectorProjectState State)
{
	Project.State = State;
	if (FChangeProject* Canonical = RuntimeSpec.ChangeProjects.FindByPredicate(
		[&Project](const FChangeProject& Candidate) { return Candidate.Id == Project.Id; }))
	{
		Canonical->State = State;
	}
	if (GetWorld() != nullptr)
	{
		if (UWorldStateSubsystem* WorldState = GetWorld()->GetSubsystem<UWorldStateSubsystem>())
		{
			WorldState->SetActiveWorldSpec(RuntimeSpec);
		}
	}
	if (FWorldDirectorProjectInspectionRecord* Record = InspectionRecords.FindByPredicate(
		[&Project](const FWorldDirectorProjectInspectionRecord& Candidate)
		{ return Candidate.ProjectId == Project.Id; }))
	{
		const FString StateName = StaticEnum<EWorldDirectorProjectState>()->GetNameStringByValue(
			static_cast<int64>(State));
		Record->Lifecycle.Add(StateName);
		if (State == EWorldDirectorProjectState::Active ||
			State == EWorldDirectorProjectState::Delayed ||
			State == EWorldDirectorProjectState::Refused ||
			State == EWorldDirectorProjectState::Failed)
		{
			Record->SimulationDecision = StateName;
		}
	}
	UE_LOG(LogWorldDirector, Display, TEXT("WORLD_DIRECTOR_PROJECT_STATE project=%s state=%s"),
		*Project.Id,
		*StaticEnum<EWorldDirectorProjectState>()->GetNameStringByValue(
			static_cast<int64>(State)));
}

bool UChangeProjectSubsystem::ValidateProposal(
	FChangeProject& Project,
	FValidationReport& OutReport)
{
	const FWorldLocation* Target = RuntimeSpec.Locations.FindByPredicate(
		[&Project](const FWorldLocation& Location) { return Location.Id == Project.TargetLocationId; });
	const FResident* Initiator = RuntimeSpec.Residents.FindByPredicate(
		[&Project](const FResident& Resident) { return Resident.Id == Project.InitiatorResidentId; });
	if (Target == nullptr || Initiator == nullptr)
	{
		OutReport.AddError(TEXT("project.reference_missing"), Project.Id,
			TEXT("Initiator or target location is not present in the active world."));
		return false;
	}
	if (!Target->bRepurposable)
	{
		OutReport.AddError(TEXT("project.location_incompatible"), Target->Id,
			TEXT("Target was compiled under the static policy and cannot be redressed live."));
	}
	const bool bSupportedConversion =
		(Target->PurposeTag == TEXT("Purpose.Home") && Project.DesiredPurposeTag == TEXT("Purpose.Clinic")) ||
		(Target->PurposeTag == TEXT("Purpose.Workplace") && Project.DesiredPurposeTag == TEXT("Purpose.Shelter")) ||
		(Target->PurposeTag == TEXT("Purpose.Shelter") && Project.DesiredPurposeTag == TEXT("Purpose.Headquarters")) ||
		(Target->PurposeTag == TEXT("Purpose.Headquarters") && Project.DesiredPurposeTag == TEXT("Purpose.Workplace"));
	if (!bSupportedConversion)
	{
		OutReport.AddError(TEXT("project.conversion_unsupported"), Target->Id,
			TEXT("The requested source-to-purpose conversion is not certified by the capability pack."));
	}
	const FString AuthorityId = !Target->ControllerResidentId.IsEmpty()
		? Target->ControllerResidentId : Target->OwnerResidentId;
	if (!AuthorityId.IsEmpty() && AuthorityId != Project.InitiatorResidentId &&
		!Project.RequiredParticipantResidentIds.Contains(AuthorityId))
	{
		OutReport.AddError(TEXT("project.permission_refused"), Target->Id,
			TEXT("The current owner or controller is not participating in the proposal."));
	}
	if (!Project.RequiredParticipantResidentIds.Contains(Project.InitiatorResidentId))
	{
		OutReport.AddError(TEXT("project.initiator_unavailable"), Project.Id,
			TEXT("The initiator must be one of the required participants."));
	}
	const UTownSimulationSubsystem* Simulation = GetWorld()
		? GetWorld()->GetSubsystem<UTownSimulationSubsystem>() : nullptr;
	for (const FString& ParticipantId : Project.RequiredParticipantResidentIds)
	{
		const FWorldDirectorResidentRuntimeState* State = Simulation
			? Simulation->GetResidentStates().FindByPredicate(
				[&ParticipantId](const FWorldDirectorResidentRuntimeState& Candidate)
				{ return Candidate.ResidentId == ParticipantId; }) : nullptr;
		if (State == nullptr)
		{
			OutReport.AddError(TEXT("project.participant_unavailable"), ParticipantId,
				TEXT("A required participant is absent from the running simulation."));
		}
	}
	const UCapabilityCatalogSubsystem* Catalog = GetWorld() && GetWorld()->GetGameInstance()
		? GetWorld()->GetGameInstance()->GetSubsystem<UCapabilityCatalogSubsystem>() : nullptr;
	for (const FName RequiredTag : Project.RequiredCapabilityTags)
	{
		if (Catalog == nullptr || !Catalog->HasCapabilityTag(RequiredTag))
		{
			OutReport.AddError(TEXT("project.capability_missing"), RequiredTag.ToString(),
				TEXT("The requested broad capability is unavailable."));
		}
	}
	if (Project.RequiredConditionTags.Contains(TEXT("Condition.ThreatActive")) &&
		RuntimeSpec.Threats.IsEmpty())
	{
		OutReport.AddError(TEXT("project.condition_unmet"), TEXT("Condition.ThreatActive"),
			TEXT("No active generation-time threat motivates this proposal."));
	}
	if (Project.RequiredTransitionMinutes < 60 || Project.RequiredTransitionMinutes > 1440)
	{
		OutReport.AddError(TEXT("project.transition_time_invalid"), Project.Id,
			TEXT("Transition time must be from one hour through one day."));
	}
	if (Project.RequiredConditionTags.Contains(TEXT("Condition.Overnight")))
	{
		const int32 StartMinuteOfDay = static_cast<int32>(Project.IntendedStartMinute % 1440);
		const int32 EndMinute = StartMinuteOfDay + Project.RequiredTransitionMinutes;
		if (StartMinuteOfDay < 18 * 60 && EndMinute < 21 * 60)
		{
			OutReport.AddError(TEXT("project.overnight_timing_invalid"), Project.Id,
				TEXT("An overnight transition must begin in the evening or extend into the night."));
		}
	}

	const TObjectPtr<AWorldDirectorLocationActor>* TargetMatch = RuntimeTown
		? RuntimeTown->SpawnedLocations.FindByPredicate(
			[&Project](const AWorldDirectorLocationActor* Location)
			{ return Location != nullptr && Location->LocationId == Project.TargetLocationId; }) : nullptr;
	AWorldDirectorLocationActor* TargetActor = TargetMatch ? TargetMatch->Get() : nullptr;
	if (TargetActor == nullptr || TargetActor->InteriorActor == nullptr)
	{
		OutReport.AddError(TEXT("project.physical_target_missing"), Project.TargetLocationId,
			TEXT("Target is not present as a repurposable actor in the compiled running world."));
	}
	else if (UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		for (const FString& ParticipantId : Project.RequiredParticipantResidentIds)
		{
			const TObjectPtr<AWorldDirectorResidentActor>* ParticipantMatch = RuntimeTown
				? RuntimeTown->SpawnedResidents.FindByPredicate(
					[&ParticipantId](const AWorldDirectorResidentActor* Resident)
					{ return Resident != nullptr && Resident->ResidentId == ParticipantId; }) : nullptr;
			AWorldDirectorResidentActor* ParticipantActor =
				ParticipantMatch ? ParticipantMatch->Get() : nullptr;
			UNavigationPath* Path = ParticipantActor ? Navigation->FindPathToLocationSynchronously(
				GetWorld(), ParticipantActor->GetActorLocation(), TargetActor->NavigationEntranceLocation,
				ParticipantActor) : nullptr;
			if (Path == nullptr || !Path->IsValid() || Path->IsPartial())
			{
				OutReport.AddError(TEXT("project.physical_access_unavailable"), ParticipantId,
					TEXT("A required participant cannot physically reach the target entrance."));
			}
		}
	}
	else
	{
		OutReport.AddError(TEXT("project.navigation_unavailable"), Project.TargetLocationId,
			TEXT("Runtime navigation is not ready to evaluate physical access."));
	}
	OutReport.bValid = !OutReport.Issues.ContainsByPredicate(
		[](const FValidationIssue& Issue)
		{ return Issue.Severity == EWorldDirectorValidationSeverity::Error; });
	return OutReport.bValid;
}

bool UChangeProjectSubsystem::ApplyActiveTransition(
	FChangeProject& Project,
	FValidationReport& OutReport)
{
	FGeneratedWorldSpec UpdatedSpec = RuntimeSpec;
	FWorldLocation* Target = UpdatedSpec.Locations.FindByPredicate(
		[&Project](const FWorldLocation& Location) { return Location.Id == Project.TargetLocationId; });
	if (Target == nullptr || UpdatedSpec.Facts.IsEmpty())
	{
		OutReport.AddError(TEXT("project.apply_state_missing"), Project.Id,
			TEXT("The active world no longer contains the project target or closed fact table."));
		return false;
	}
	const FName PreviousPurpose = Target->PurposeTag;
	Target->PurposeTag = Project.DesiredPurposeTag;
	Target->ControllerResidentId = Project.InitiatorResidentId;
	Target->AccessPolicy = Project.DesiredPurposeTag == TEXT("Purpose.Headquarters")
		? EWorldDirectorAccessPolicy::Restricted
		: Project.DesiredPurposeTag == TEXT("Purpose.Workplace")
			? EWorldDirectorAccessPolicy::Workers
			: EWorldDirectorAccessPolicy::Public;
	Target->RequiredCapabilityTags = Project.RequiredCapabilityTags;

	if (PreviousPurpose == TEXT("Purpose.Home"))
	{
		auto CountResidentsAt = [&UpdatedSpec](const FString& LocationId)
		{
			int32 Count = 0;
			for (const FResident& Resident : UpdatedSpec.Residents)
			{
				Count += Resident.HomeLocationId == LocationId;
			}
			return Count;
		};
		const int32 DisplacedResidents = CountResidentsAt(Target->Id);
		FWorldLocation* Fallback = UpdatedSpec.Locations.FindByPredicate(
			[Target, &CountResidentsAt, DisplacedResidents](const FWorldLocation& Location)
			{
				if (Location.Id == Target->Id ||
					(Location.PurposeTag != TEXT("Purpose.Shelter") && Location.PurposeTag != TEXT("Purpose.Home")))
				{
					return false;
				}
				const int32 ExistingResidents = CountResidentsAt(Location.Id);
				return Location.ResidentCapacity - ExistingResidents >= DisplacedResidents;
			});
		if (Fallback == nullptr)
		{
			OutReport.AddError(TEXT("project.occupants_cannot_relocate"), Target->Id,
				TEXT("No compatible location can receive the displaced occupants."));
			return false;
		}
		for (FHousehold& Household : UpdatedSpec.Households)
		{
			if (Household.HomeLocationId == Target->Id)
			{
				Household.HomeLocationId = Fallback->Id;
				for (FResident& Resident : UpdatedSpec.Residents)
				{
					if (Household.MemberResidentIds.Contains(Resident.Id))
					{
						Resident.HomeLocationId = Fallback->Id;
					}
				}
			}
		}
	}
	if (Project.DesiredPurposeTag != TEXT("Purpose.Shelter"))
	{
		for (FResident& Resident : UpdatedSpec.Residents)
		{
			if (Project.RequiredParticipantResidentIds.Contains(Resident.Id))
			{
				Resident.WorkplaceLocationId = Target->Id;
				Resident.bEmployed = true;
			}
		}
	}

	const FString ConsequenceFactId = UpdatedSpec.Facts.ContainsByPredicate(
		[](const FWorldFact& Fact) { return Fact.Id == TEXT("fact.mill_debt"); })
		? TEXT("fact.mill_debt") : UpdatedSpec.Facts[0].Id;
	for (FResident& Resident : UpdatedSpec.Residents)
	{
		if (!Project.RequiredParticipantResidentIds.Contains(Resident.Id))
		{
			continue;
		}
		const FString BeliefId = FString::Printf(TEXT("belief.%s.%s"), *Project.Id, *Resident.Id);
		if (!Resident.BeliefIds.Contains(BeliefId))
		{
			FBelief& Belief = UpdatedSpec.Beliefs.AddDefaulted_GetRef();
			Belief.Id = BeliefId;
			Belief.HolderResidentId = Resident.Id;
			Belief.FactId = ConsequenceFactId;
			Belief.SourceResidentId = Project.InitiatorResidentId == Resident.Id
				? FString() : Project.InitiatorResidentId;
			Belief.Confidence = 0.9f;
			Belief.EmotionalSignificance = 0.8f;
			Belief.WillingnessToShare = 0.7f;
			Resident.BeliefIds.Add(BeliefId);
		}
		FResidentMemory& Memory = Resident.ImportantMemories.AddDefaulted_GetRef();
		Memory.Id = FString::Printf(TEXT("memory.%s.%s"), *Project.Id, *Resident.Id);
		Memory.FactId = ConsequenceFactId;
		Memory.Summary = FString::Printf(TEXT("Helped enact '%s' at %s."), *Project.Reason, *Target->DisplayName);
		Memory.Day = static_cast<int32>(PreparationStartMinutes.FindRef(Project.Id) / 1440);
		Memory.EmotionalSignificance = 0.85f;
	}

	if (RuntimeTown == nullptr || !RuntimeTown->ApplyLocationRepurpose(*Target, Project, OutReport))
	{
		return false;
	}
	RuntimeSpec = MoveTemp(UpdatedSpec);
	const FWorldLocation* ActiveTarget = RuntimeSpec.Locations.FindByPredicate(
		[&Project](const FWorldLocation& Location) { return Location.Id == Project.TargetLocationId; });
	if (FWorldDirectorProjectInspectionRecord* Record = InspectionRecords.FindByPredicate(
		[&Project](const FWorldDirectorProjectInspectionRecord& Candidate)
		{ return Candidate.ProjectId == Project.Id; }))
	{
		if (ActiveTarget != nullptr)
		{
			Record->AfterState = FString::Printf(TEXT("purpose=%s owner=%s controller=%s access=%d dressingRevision=1"),
				*ActiveTarget->PurposeTag.ToString(), *ActiveTarget->OwnerResidentId,
				*ActiveTarget->ControllerResidentId, static_cast<int32>(ActiveTarget->AccessPolicy));
		}
	}
	if (UTownSimulationSubsystem* Simulation = GetWorld()->GetSubsystem<UTownSimulationSubsystem>())
	{
		Simulation->ApplyRepurposeConsequences(RuntimeSpec, Project);
	}
	if (UWorldStateSubsystem* State = GetWorld()->GetSubsystem<UWorldStateSubsystem>())
	{
		State->SetActiveWorldSpec(RuntimeSpec);
	}
	UE_LOG(LogWorldDirector, Display,
		TEXT("WORLD_DIRECTOR_PROJECT_CONSEQUENCES project=%s location=%s oldPurpose=%s newPurpose=%s controller=%s access=%d participants=%d"),
		*Project.Id, ActiveTarget ? *ActiveTarget->Id : TEXT("missing"), *PreviousPurpose.ToString(),
		ActiveTarget ? *ActiveTarget->PurposeTag.ToString() : TEXT("missing"),
		ActiveTarget ? *ActiveTarget->ControllerResidentId : TEXT("missing"),
		ActiveTarget ? static_cast<int32>(ActiveTarget->AccessPolicy) : -1,
		Project.RequiredParticipantResidentIds.Num());
	return true;
}

void UChangeProjectSubsystem::AdvanceProjects(const int64 ElapsedSimulationMinutes)
{
	for (FChangeProject& Project : Projects)
	{
		if (Project.State == EWorldDirectorProjectState::Proposed)
		{
			FValidationReport Report;
			if (!ValidateProposal(Project, Report))
			{
				for (const FValidationIssue& Issue : Report.Issues)
				{
					UE_LOG(LogWorldDirector, Warning,
						TEXT("WORLD_DIRECTOR_PROJECT_DECISION project=%s code=%s message=%s"),
						*Project.Id, *Issue.Code.ToString(), *Issue.Message);
				}
				const bool bTemporary = Report.Issues.ContainsByPredicate(
					[](const FValidationIssue& Issue)
					{
						return Issue.Code == TEXT("project.participant_unavailable") ||
							Issue.Code == TEXT("project.navigation_unavailable") ||
							Issue.Code == TEXT("project.physical_access_unavailable");
					});
				const bool bRefused = Report.Issues.ContainsByPredicate(
					[](const FValidationIssue& Issue)
					{
						return Issue.Code == TEXT("project.permission_refused") ||
							Issue.Code == TEXT("project.conversion_unsupported");
					});
				SetProjectState(Project, bTemporary ? EWorldDirectorProjectState::Delayed :
					bRefused ? EWorldDirectorProjectState::Refused : EWorldDirectorProjectState::Failed);
				continue;
			}
			SetProjectState(Project, EWorldDirectorProjectState::Validated);
			SetProjectState(Project, EWorldDirectorProjectState::PermissionResolved);
			SetProjectState(Project, EWorldDirectorProjectState::Scheduled);
		}
		if (Project.State == EWorldDirectorProjectState::Scheduled &&
			ElapsedSimulationMinutes >= Project.IntendedStartMinute)
		{
			const int32 IntendedHour = static_cast<int32>(
				(Project.IntendedStartMinute % 1440) / 60);
			const bool bParticipantUnavailable = IntendedHour < 8 || IntendedHour >= 21 ||
				Project.RequiredParticipantResidentIds.ContainsByPredicate(
					[this](const FString& ParticipantId)
					{
						const FResident* Resident = RuntimeSpec.Residents.FindByPredicate(
							[&ParticipantId](const FResident& Candidate)
							{ return Candidate.Id == ParticipantId; });
						return Resident == nullptr ||
							Resident->Availability == EWorldDirectorResidentAvailability::Traveling;
					});
			if (bParticipantUnavailable)
			{
				SetProjectState(Project, EWorldDirectorProjectState::Delayed);
				continue;
			}
			PreparationStartMinutes.Add(Project.Id, Project.IntendedStartMinute);
			SetProjectState(Project, EWorldDirectorProjectState::Preparation);
			continue;
		}
		if (Project.State != EWorldDirectorProjectState::Preparation ||
			ElapsedSimulationMinutes < PreparationStartMinutes.FindRef(Project.Id) +
				Project.RequiredTransitionMinutes)
		{
			continue;
		}

		const TObjectPtr<AWorldDirectorLocationActor>* TargetMatch = RuntimeTown
			? RuntimeTown->SpawnedLocations.FindByPredicate(
				[&Project](const AWorldDirectorLocationActor* Location)
				{ return Location != nullptr && Location->LocationId == Project.TargetLocationId; }) : nullptr;
		AWorldDirectorLocationActor* TargetActor = TargetMatch ? TargetMatch->Get() : nullptr;
		if (Project.RequiredConditionTags.Contains(TEXT("Condition.PlayerAway")) &&
			TargetActor != nullptr && GetWorld()->GetFirstPlayerController() != nullptr &&
			GetWorld()->GetFirstPlayerController()->GetPawn() != nullptr &&
			FVector::Dist2D(GetWorld()->GetFirstPlayerController()->GetPawn()->GetActorLocation(),
				TargetActor->GetActorLocation()) < 1500.0f)
		{
			SetProjectState(Project, EWorldDirectorProjectState::Delayed);
			continue;
		}
		const UTownSimulationSubsystem* Simulation = GetWorld()->GetSubsystem<UTownSimulationSubsystem>();
		const bool bBlockedByOccupant = Simulation &&
			Simulation->GetResidentStates().ContainsByPredicate(
				[&Project](const FWorldDirectorResidentRuntimeState& State)
				{
					return State.CurrentLocationId == Project.TargetLocationId &&
						!Project.RequiredParticipantResidentIds.Contains(State.ResidentId);
				});
		if (bBlockedByOccupant)
		{
			SetProjectState(Project, EWorldDirectorProjectState::Delayed);
			continue;
		}
		SetProjectState(Project, EWorldDirectorProjectState::Transition);
		FValidationReport ApplyReport;
		if (ApplyActiveTransition(Project, ApplyReport))
		{
			SetProjectState(Project, EWorldDirectorProjectState::Active);
		}
		else
		{
			for (const FValidationIssue& Issue : ApplyReport.Issues)
			{
				UE_LOG(LogWorldDirector, Error,
					TEXT("WORLD_DIRECTOR_PROJECT_APPLY_ISSUE project=%s code=%s message=%s"),
					*Project.Id, *Issue.Code.ToString(), *Issue.Message);
			}
			SetProjectState(Project, EWorldDirectorProjectState::Failed);
		}
	}
}

namespace
{
bool SerializeJsonObject(const TSharedRef<FJsonObject>& Object, FString& OutJson)
{
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
	return FJsonSerializer::Serialize(Object, Writer);
}

bool SaveJsonObject(const TSharedRef<FJsonObject>& Object, const FString& Path)
{
	FString Json;
	return SerializeJsonObject(Object, Json) && FFileHelper::SaveStringToFile(
		Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

TSharedPtr<FJsonObject> ParseJsonObject(const FString& Json)
{
	TSharedPtr<FJsonObject> Object;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	return FJsonSerializer::Deserialize(Reader, Object) ? Object : nullptr;
}

FString ValidationSection(const FString& Path)
{
	FString Section = Path;
	Section.RemoveFromStart(TEXT("$."));
	int32 End = INDEX_NONE;
	if (Section.FindChar(TEXT('['), End) || Section.FindChar(TEXT('.'), End))
	{
		Section.LeftInline(End);
	}
	return Section;
}
}

void UDirectorBridgeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	DirectorProvider = CreateWorldDirectorLocalCompanionProvider();
	SetDirectorConnected(true, DirectorProvider->GetProviderName());
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(
		this, &UDirectorBridgeSubsystem::HandleWorldCleanup);
}

void UDirectorBridgeSubsystem::Deinitialize()
{
	if (DirectorProvider.IsValid())
	{
		DirectorProvider->CancelAll();
		DirectorProvider.Reset();
	}
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
	bGenerationRunning = false;
	GenerationStage = EWorldDirectorGenerationStage::Cancelled;
	WorkingGenerationDocument.Reset();
	SetDirectorConnected(false, FString());
	Super::Deinitialize();
}

void UDirectorBridgeSubsystem::HandleWorldCleanup(
	UWorld* World,
	const bool bSessionEnded,
	const bool bCleanupResources)
{
	if (bGenerationRunning && World != nullptr && World->GetGameInstance() == GetGameInstance())
	{
		if (DirectorProvider.IsValid())
		{
			DirectorProvider->CancelAll();
		}
		bGenerationRunning = false;
		GenerationStage = EWorldDirectorGenerationStage::Cancelled;
		LastGenerationError = TEXT("Generation cancelled during world teardown.");
		OnGenerationFinished.Clear();
	}
}

bool UDirectorBridgeSubsystem::BeginWorldGeneration(
	const FString& PlayerPrompt,
	const int32 Seed,
	const float StageTimeoutSeconds,
	const bool bUseFixtureProviderForTesting,
	const FString& Model,
	const FString& ReasoningEffort)
{
	if (bGenerationRunning || !DirectorProvider.IsValid())
	{
		return false;
	}

	GenerationPlayerPrompt = PlayerPrompt;
	GenerationModel = bUseFixtureProviderForTesting
		? TEXT("deterministic fixture") : Model.TrimStartAndEnd();
	GenerationReasoningEffort = bUseFixtureProviderForTesting
		? TEXT("n/a") : ReasoningEffort.TrimStartAndEnd().ToLower();
	GenerationSeed = Seed;
	GenerationStageTimeoutSeconds = FMath::Max(1.0f, StageTimeoutSeconds);
	bFixtureProviderForTesting = bUseFixtureProviderForTesting;
	RepairAttempt = 0;
	ConsecutiveRepairIssueCount = 0;
	LastRepairIssueSignature.Reset();
	LastGenerationError.Reset();
	GeneratedWorldSpec = FGeneratedWorldSpec();
	LastResolvedWorldPlan = FResolvedWorldPlan();
	LastGenerationValidation = FValidationReport();
	GenerationStageHistory.Reset();
	GenerationIssueHistory.Reset();
	GenerationMetrics.Reset();
	GenerationLog.Reset();
	GenerationStartedAtSeconds = FPlatformTime::Seconds();
	GenerationFinishedElapsedSeconds = 0.0;
	CurrentStageStartedAtSeconds = 0.0;
	CurrentMetricIndex = INDEX_NONE;
	SelectedLayoutCandidateId.Reset();
	ActiveGenerationRequestId.Reset();
	GenerationRequestSerial = 0;
	GenerationRunId = FString::Printf(
		TEXT("%s-%s"),
		*FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ")),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
	GenerationRunDirectory = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("WorldRuns"), GenerationRunId));
	if (!IFileManager::Get().MakeDirectory(*GenerationRunDirectory, true))
	{
		LastGenerationError = TEXT("Could not create the generation run directory.");
		return false;
	}
	RecordGenerationLog(FString::Printf(
		TEXT("Run started; provider=%s mode=%s model=%s reasoning=%s seed=%d promptCharacters=%d"),
		*ProviderName, bFixtureProviderForTesting ? TEXT("fixture") : TEXT("cli"),
		GenerationModel.IsEmpty() ? TEXT("cli-default") : *GenerationModel,
		GenerationReasoningEffort.IsEmpty() ? TEXT("cli-default") : *GenerationReasoningEffort,
		GenerationSeed, GenerationPlayerPrompt.Len()));

	WorkingGenerationDocument = MakeShared<FJsonObject>();
	WorkingGenerationDocument->SetNumberField(TEXT("version"), 1);
	WorkingGenerationDocument->SetStringField(
		TEXT("id"), FString::Printf(TEXT("world.generated_%d"), Seed));
	WorkingGenerationDocument->SetNumberField(TEXT("seed"), Seed);
	WorkingGenerationDocument->SetObjectField(TEXT("brief"), MakeShared<FJsonObject>());
	WorkingGenerationDocument->SetObjectField(TEXT("topology"), MakeShared<FJsonObject>());
	for (const TCHAR* ArrayName : {
		TEXT("locations"), TEXT("residents"), TEXT("households"), TEXT("relationships"),
		TEXT("beliefs"), TEXT("facts"), TEXT("events"), TEXT("threats"), TEXT("changeProjects")})
	{
		WorkingGenerationDocument->SetArrayField(ArrayName, TArray<TSharedPtr<FJsonValue>>());
	}

	LayoutCandidates.Reset();

	bGenerationRunning = true;
	GenerationStage = EWorldDirectorGenerationStage::Interpret;
	RequestCurrentStage();
	return true;
}

void UDirectorBridgeSubsystem::CancelWorldGeneration()
{
	if (!bGenerationRunning)
	{
		return;
	}
	if (DirectorProvider.IsValid())
	{
		DirectorProvider->CancelAll();
	}
	GenerationFinishedElapsedSeconds = FPlatformTime::Seconds() - GenerationStartedAtSeconds;
	bGenerationRunning = false;
	GenerationStage = EWorldDirectorGenerationStage::Cancelled;
	LastGenerationError = TEXT("Generation was cancelled.");
	if (GenerationMetrics.IsValidIndex(CurrentMetricIndex))
	{
		FWorldDirectorGenerationStageMetric& Metric = GenerationMetrics[CurrentMetricIndex];
		Metric.DurationSeconds = FPlatformTime::Seconds() - CurrentStageStartedAtSeconds;
		Metric.Error = LastGenerationError;
	}
	RecordGenerationLog(LastGenerationError);
	SaveRunSummary();
	OnGenerationFinished.Broadcast(false, GenerationRunId, LastGenerationError);
}

FString UDirectorBridgeSubsystem::StageWireName() const
{
	switch (GenerationStage)
	{
	case EWorldDirectorGenerationStage::Interpret: return TEXT("interpret");
	case EWorldDirectorGenerationStage::Topology: return TEXT("topology");
	case EWorldDirectorGenerationStage::Layout: return TEXT("layout");
	case EWorldDirectorGenerationStage::Population: return TEXT("population");
	case EWorldDirectorGenerationStage::Repair: return TEXT("repair");
	default: return TEXT("unknown");
	}
}

int32 UDirectorBridgeSubsystem::StageFileOrdinal() const
{
	switch (GenerationStage)
	{
	case EWorldDirectorGenerationStage::Interpret: return 1;
	case EWorldDirectorGenerationStage::Topology: return 2;
	case EWorldDirectorGenerationStage::Layout: return 3;
	case EWorldDirectorGenerationStage::Population: return 4;
	case EWorldDirectorGenerationStage::Repair: return 5 + RepairAttempt;
	default: return 0;
	}
}

void UDirectorBridgeSubsystem::RequestCurrentStage()
{
	if (!bGenerationRunning || !DirectorProvider.IsValid() || !WorkingGenerationDocument.IsValid())
	{
		return;
	}
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("WorldDirector"));
	if (!Plugin.IsValid())
	{
		FinishGeneration(false, TEXT("WorldDirector plugin path is unavailable."));
		return;
	}

	const FString StageName = StageWireName();
	const int32 Ordinal = StageFileOrdinal();
	ActiveGenerationRequestId = FString::Printf(
		TEXT("%s-%02d-%llu-%s"),
		*GenerationRunId,
		Ordinal,
		static_cast<unsigned long long>(++GenerationRequestSerial),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
	const FString Prefix = FString::Printf(TEXT("%02d-%s"), Ordinal, *StageName);
	const FString RequestPath = FPaths::Combine(GenerationRunDirectory, Prefix + TEXT("-request.json"));
	const FString ResponsePath = FPaths::Combine(GenerationRunDirectory, Prefix + TEXT("-response.json"));
	const FString CompanionScript = FPaths::Combine(
		Plugin->GetBaseDir(), TEXT("Resources/Companion/world_director_companion.py"));

	const TSharedRef<FJsonObject> Request = MakeShared<FJsonObject>();
	Request->SetStringField(TEXT("stage"), StageName);
	Request->SetStringField(TEXT("runId"), GenerationRunId);
	Request->SetStringField(TEXT("requestId"), ActiveGenerationRequestId);
	Request->SetStringField(TEXT("playerPrompt"), GenerationPlayerPrompt);
	Request->SetStringField(TEXT("model"), GenerationModel);
	Request->SetStringField(TEXT("reasoningEffort"), GenerationReasoningEffort);
	Request->SetNumberField(TEXT("seed"), GenerationSeed);
	Request->SetNumberField(TEXT("attempt"), RepairAttempt);
	Request->SetStringField(
		TEXT("providerMode"), bFixtureProviderForTesting ? TEXT("fixture") : TEXT("cli"));
	Request->SetObjectField(TEXT("current"), WorkingGenerationDocument);

	const TSharedRef<FJsonObject> CapabilitySummary = MakeShared<FJsonObject>();
	CapabilitySummary->SetStringField(TEXT("theme"), TEXT("Asset-led stylized village and rural frontier"));
	CapabilitySummary->SetStringField(TEXT("environmentProfile"), TEXT("Environment.StylizedVillage"));
	CapabilitySummary->SetStringField(TEXT("terrainSystem"),
		TEXT("Seeded basin, valley, ridge, coast, or marsh terrain with grass, gravel, paving, farmfield, rock, and water surfaces"));
	TArray<TSharedPtr<FJsonValue>> CapabilityTags;
	if (const UCapabilityCatalogSubsystem* Catalog = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UCapabilityCatalogSubsystem>() : nullptr)
	{
		for (const FName Tag : Catalog->GetCapabilityTags())
		{
			CapabilityTags.Add(MakeShared<FJsonValueString>(Tag.ToString()));
		}
	}
	CapabilitySummary->SetArrayField(TEXT("supportedTags"), CapabilityTags);
	CapabilitySummary->SetNumberField(TEXT("residentMinimum"), 20);
	CapabilitySummary->SetNumberField(TEXT("residentMaximum"), 30);
	CapabilitySummary->SetNumberField(TEXT("certifiedPlotMinimum"), 12);
	CapabilitySummary->SetNumberField(TEXT("certifiedPlotMaximum"), 18);
	CapabilitySummary->SetNumberField(TEXT("homePlotMaximum"), 6);
	Request->SetObjectField(TEXT("capabilitySummary"), CapabilitySummary);

	// The provider is intentionally stateless between stages. Retain the decisions
	// later stages actually need as an explicit, compact world bible instead of
	// relying on hidden conversation history or an undifferentiated JSON dump.
	const TSharedRef<FJsonObject> WorldContext = MakeShared<FJsonObject>();
	WorldContext->SetStringField(TEXT("runId"), GenerationRunId);
	WorldContext->SetStringField(TEXT("requestId"), ActiveGenerationRequestId);
	WorldContext->SetStringField(TEXT("currentStage"), StageName);
	WorldContext->SetNumberField(TEXT("originalRootSeed"), GenerationSeed);
	double SelectedLayoutSeed = GenerationSeed;
	WorkingGenerationDocument->TryGetNumberField(TEXT("seed"), SelectedLayoutSeed);
	WorldContext->SetNumberField(TEXT("selectedLayoutSeed"), SelectedLayoutSeed);
	WorldContext->SetStringField(TEXT("selectedCandidateId"), SelectedLayoutCandidateId);
	WorldContext->SetNumberField(TEXT("repairAttempt"), RepairAttempt);
	WorldContext->SetNumberField(TEXT("consecutiveIssueSignatureCount"), ConsecutiveRepairIssueCount);

	const auto CopyStringField = [](const TSharedPtr<FJsonObject>& Source,
		const TSharedRef<FJsonObject>& Target, const TCHAR* Field)
	{
		FString Value;
		if (Source.IsValid() && Source->TryGetStringField(Field, Value))
		{
			Target->SetStringField(Field, Value);
		}
	};
	const auto CopyStringArrayField = [](const TSharedPtr<FJsonObject>& Source,
		const TSharedRef<FJsonObject>& Target, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Source.IsValid() && Source->TryGetArrayField(Field, Values) && Values != nullptr)
		{
			Target->SetArrayField(Field, *Values);
		}
	};
	const TSharedPtr<FJsonObject>* BriefObject = nullptr;
	if (WorkingGenerationDocument->TryGetObjectField(TEXT("brief"), BriefObject) &&
		BriefObject != nullptr && BriefObject->IsValid())
	{
		const TSharedRef<FJsonObject> CreativePillars = MakeShared<FJsonObject>();
		CopyStringField(*BriefObject, CreativePillars, TEXT("theme"));
		CopyStringField(*BriefObject, CreativePillars, TEXT("settlementIdentity"));
		CopyStringArrayField(*BriefObject, CreativePillars, TEXT("premises"));
		CopyStringArrayField(*BriefObject, CreativePillars, TEXT("terrainPreferences"));
		WorldContext->SetObjectField(TEXT("creativePillars"), CreativePillars);
	}
	const TSharedPtr<FJsonObject>* TopologyObject = nullptr;
	if (WorkingGenerationDocument->TryGetObjectField(TEXT("topology"), TopologyObject) &&
		TopologyObject != nullptr && TopologyObject->IsValid())
	{
		const TSharedRef<FJsonObject> LorePillars = MakeShared<FJsonObject>();
		for (const TCHAR* Field : {TEXT("governance"), TEXT("historicalWound"), TEXT("currentTension"),
			TEXT("supernaturalPremise"), TEXT("centralThreat")})
		{
			CopyStringField(*TopologyObject, LorePillars, Field);
		}
		CopyStringArrayField(*TopologyObject, LorePillars, TEXT("districts"));
		CopyStringArrayField(*TopologyObject, LorePillars, TEXT("landmarkLocationIds"));
		WorldContext->SetObjectField(TEXT("lorePillars"), LorePillars);
	}
	const TSharedRef<FJsonObject> LockedIds = MakeShared<FJsonObject>();
	for (const TCHAR* Section : {TEXT("locations"), TEXT("facts"), TEXT("threats")})
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		TArray<TSharedPtr<FJsonValue>> IdValues;
		if (WorkingGenerationDocument->TryGetArrayField(Section, Values) && Values != nullptr)
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
				FString Id;
				if (Object.IsValid() && Object->TryGetStringField(TEXT("id"), Id))
				{
					IdValues.Add(MakeShared<FJsonValueString>(Id));
				}
			}
		}
		LockedIds->SetArrayField(Section, IdValues);
	}
	WorldContext->SetObjectField(TEXT("lockedIds"), LockedIds);

	TArray<TSharedPtr<FJsonValue>> CandidateValues;
	for (const FWorldDirectorLayoutCandidate& Candidate : LayoutCandidates)
	{
		const TSharedRef<FJsonObject> CandidateObject = MakeShared<FJsonObject>();
		CandidateObject->SetStringField(TEXT("opaqueId"), Candidate.OpaqueId);
		CandidateObject->SetStringField(TEXT("summary"), Candidate.Summary);
		CandidateObject->SetStringField(TEXT("terrainArchetype"),
			StaticEnum<EWorldDirectorTerrainArchetype>()->GetNameStringByValue(static_cast<int64>(Candidate.TerrainArchetype)));
		CandidateObject->SetNumberField(TEXT("reliefCentimeters"), Candidate.ReliefCentimeters);
		CandidateObject->SetNumberField(TEXT("meanSlopeDegrees"), Candidate.MeanSlopeDegrees);
		CandidateObject->SetNumberField(TEXT("maximumRoadGradePercent"), Candidate.MaximumRoadGrade * 100.0f);
		CandidateObject->SetStringField(TEXT("physicalFingerprint"), Candidate.WorldFingerprint.Left(16));
		CandidateObject->SetBoolField(TEXT("selected"), Candidate.OpaqueId == SelectedLayoutCandidateId);
		CandidateValues.Add(MakeShared<FJsonValueObject>(CandidateObject));
		if (Candidate.OpaqueId == SelectedLayoutCandidateId)
		{
			WorldContext->SetObjectField(TEXT("selectedPhysicalLayout"), CandidateObject);
		}
	}
	Request->SetArrayField(TEXT("layoutCandidates"), CandidateValues);
	Request->SetObjectField(TEXT("worldContext"), WorldContext);

	TArray<TSharedPtr<FJsonValue>> ValidationValues;
	for (const FValidationIssue& Issue : LastGenerationValidation.Issues)
	{
		const TSharedRef<FJsonObject> IssueObject = MakeShared<FJsonObject>();
		IssueObject->SetStringField(TEXT("code"), Issue.Code.ToString());
		IssueObject->SetStringField(TEXT("path"), Issue.Path);
		IssueObject->SetStringField(TEXT("message"), Issue.Message);
		ValidationValues.Add(MakeShared<FJsonValueObject>(IssueObject));
	}
	Request->SetArrayField(TEXT("validationIssues"), ValidationValues);
	Request->SetBoolField(TEXT("replaceRepeatedFailure"), ConsecutiveRepairIssueCount >= 2);
	Request->SetBoolField(
		TEXT("testInvalidPopulationOnce"),
		GenerationStage == EWorldDirectorGenerationStage::Population &&
		FParse::Param(FCommandLine::Get(), TEXT("WorldDirectorInjectInvalidPopulation")));
	double CompanionTestDelay = 0.0;
	if (FParse::Value(FCommandLine::Get(), TEXT("WorldDirectorCompanionTestDelay="), CompanionTestDelay))
	{
		Request->SetNumberField(TEXT("testDelaySeconds"), FMath::Max(0.0, CompanionTestDelay));
	}

	if (!SaveJsonObject(Request, RequestPath))
	{
		FinishGeneration(false, TEXT("Could not persist the stage request."));
		return;
	}
	FWorldDirectorGenerationStageMetric& Metric = GenerationMetrics.AddDefaulted_GetRef();
	Metric.Stage = StageName;
	Metric.RequestId = ActiveGenerationRequestId;
	Metric.StartedAtUtc = FDateTime::UtcNow().ToIso8601();
	Metric.RequestPath = RequestPath;
	Metric.ResponsePath = ResponsePath;
	if (!bFixtureProviderForTesting)
	{
		Metric.PromptPath = FPaths::Combine(GenerationRunDirectory, Prefix + TEXT("-prompt.txt"));
		Metric.RawResponsePath = FPaths::Combine(
			GenerationRunDirectory, Prefix + TEXT("-raw-response.txt"));
		Metric.ProviderEventsPath = FPaths::Combine(
			GenerationRunDirectory, Prefix + TEXT("-provider-events.jsonl"));
		Metric.TelemetryPath = FPaths::Combine(
			GenerationRunDirectory, Prefix + TEXT("-telemetry.json"));
	}
	Metric.Model = GenerationModel.IsEmpty() ? TEXT("CLI default (not reported)") : GenerationModel;
	Metric.ReasoningEffort = GenerationReasoningEffort.IsEmpty()
		? TEXT("CLI default") : GenerationReasoningEffort;
	Metric.RequestBytes = IFileManager::Get().FileSize(*RequestPath);
	CurrentMetricIndex = GenerationMetrics.Num() - 1;
	CurrentStageStartedAtSeconds = FPlatformTime::Seconds();
	RecordGenerationLog(FString::Printf(TEXT("Stage %s requested; requestBytes=%lld timeout=%.1fs"),
		*StageName, Metric.RequestBytes, GenerationStageTimeoutSeconds));

	const TWeakObjectPtr<UDirectorBridgeSubsystem> WeakThis(this);
	const FString ConfiguredPython =
		FPlatformMisc::GetEnvironmentVariable(TEXT("WORLD_DIRECTOR_PYTHON"));
	const FString CompanionExecutable = ConfiguredPython.IsEmpty()
		? TEXT("/usr/bin/python3") : ConfiguredPython;
	const FString ExpectedRunId = GenerationRunId;
	const FString ExpectedRequestId = ActiveGenerationRequestId;
	const EWorldDirectorGenerationStage ExpectedStage = GenerationStage;
	DirectorProvider->RequestStage(
		CompanionExecutable, CompanionScript, RequestPath, ResponsePath,
		GenerationStageTimeoutSeconds,
		[WeakThis, ExpectedRunId, ExpectedRequestId, ExpectedStage](FWorldDirectorProviderResponse&& Response)
		{
			if (WeakThis.IsValid())
			{
				WeakThis->HandleStageResponse(
					ExpectedRunId, ExpectedRequestId, ExpectedStage, MoveTemp(Response));
			}
		});
}

void UDirectorBridgeSubsystem::HandleStageResponse(
	const FString& ExpectedRunId,
	const FString& ExpectedRequestId,
	const EWorldDirectorGenerationStage ExpectedStage,
	FWorldDirectorProviderResponse&& Response)
{
	if (!bGenerationRunning || ExpectedRunId != GenerationRunId ||
		ExpectedRequestId != ActiveGenerationRequestId || ExpectedStage != GenerationStage)
	{
		UE_LOG(LogWorldDirector, Warning,
			TEXT("WORLD_DIRECTOR_STALE_STAGE_RESPONSE ignoredRun=%s activeRun=%s ignoredRequest=%s activeRequest=%s ignoredStage=%s activeStage=%s"),
			*ExpectedRunId, *GenerationRunId,
			*ExpectedRequestId, *ActiveGenerationRequestId,
			*StaticEnum<EWorldDirectorGenerationStage>()->GetNameStringByValue(static_cast<int64>(ExpectedStage)),
			*StaticEnum<EWorldDirectorGenerationStage>()->GetNameStringByValue(static_cast<int64>(GenerationStage)));
		return;
	}
	const auto ApplyDiagnostics = [](
		const TSharedPtr<FJsonObject>& Diagnostics,
		FWorldDirectorGenerationStageMetric& Metric)
	{
		if (!Diagnostics.IsValid())
		{
			return;
		}
		Diagnostics->TryGetStringField(TEXT("requestedModel"), Metric.Model);
		Diagnostics->TryGetStringField(TEXT("reasoningEffort"), Metric.ReasoningEffort);
		Diagnostics->TryGetStringField(TEXT("threadId"), Metric.ProviderThreadId);
		Diagnostics->TryGetStringField(TEXT("promptPath"), Metric.PromptPath);
		Diagnostics->TryGetStringField(TEXT("rawResponsePath"), Metric.RawResponsePath);
		Diagnostics->TryGetStringField(TEXT("providerEventsPath"), Metric.ProviderEventsPath);
		Diagnostics->TryGetStringField(TEXT("telemetryPath"), Metric.TelemetryPath);
		Diagnostics->TryGetStringField(TEXT("costNote"), Metric.CostNote);
		double Number = 0.0;
		if (Diagnostics->TryGetNumberField(TEXT("inputTokens"), Number))
		{
			Metric.InputTokens = static_cast<int64>(Number);
		}
		if (Diagnostics->TryGetNumberField(TEXT("cachedInputTokens"), Number))
		{
			Metric.CachedInputTokens = static_cast<int64>(Number);
		}
		if (Diagnostics->TryGetNumberField(TEXT("outputTokens"), Number))
		{
			Metric.OutputTokens = static_cast<int64>(Number);
		}
		if (Diagnostics->TryGetNumberField(TEXT("reasoningOutputTokens"), Number))
		{
			Metric.ReasoningOutputTokens = static_cast<int64>(Number);
		}
		if (Diagnostics->TryGetNumberField(TEXT("promptCharacters"), Number))
		{
			Metric.PromptCharacters = static_cast<int64>(Number);
		}
	};
	const int32 CompletedMetricIndex = CurrentMetricIndex;
	if (GenerationMetrics.IsValidIndex(CompletedMetricIndex))
	{
		FWorldDirectorGenerationStageMetric& Metric = GenerationMetrics[CompletedMetricIndex];
		Metric.DurationSeconds = FPlatformTime::Seconds() - CurrentStageStartedAtSeconds;
		Metric.ResponseBytes = Response.ResponseJson.IsEmpty()
			? 0 : FTCHARToUTF8(*Response.ResponseJson).Length();
		Metric.ExitCode = Response.ExitCode;
		Metric.bSuccess = Response.bSuccess;
		Metric.Error = Response.Error;
		Metric.ProviderOutput = Response.ProviderOutput;
		FString TelemetryJson;
		if (!Metric.TelemetryPath.IsEmpty() &&
			FFileHelper::LoadFileToString(TelemetryJson, *Metric.TelemetryPath))
		{
			ApplyDiagnostics(ParseJsonObject(TelemetryJson), Metric);
		}
		RecordGenerationLog(FString::Printf(
			TEXT("Stage %s finished; success=%s duration=%.2fs responseBytes=%lld exitCode=%d%s%s"),
			*Metric.Stage, Metric.bSuccess ? TEXT("true") : TEXT("false"),
			Metric.DurationSeconds, Metric.ResponseBytes, Metric.ExitCode,
			Metric.Error.IsEmpty() ? TEXT("") : TEXT(" error="), *Metric.Error));
	}
	CurrentMetricIndex = INDEX_NONE;
	if (!Response.bSuccess)
	{
		FinishGeneration(false, Response.Error);
		return;
	}
	const TSharedPtr<FJsonObject> Envelope = ParseJsonObject(Response.ResponseJson);
	if (!Envelope.IsValid() || Envelope->GetStringField(TEXT("stage")) != StageWireName())
	{
		FinishGeneration(false, TEXT("Companion returned an invalid stage envelope."));
		return;
	}
	const TSharedPtr<FJsonObject>* Diagnostics = nullptr;
	if (GenerationMetrics.IsValidIndex(CompletedMetricIndex) &&
		Envelope->TryGetObjectField(TEXT("diagnostics"), Diagnostics) &&
		Diagnostics != nullptr && Diagnostics->IsValid())
	{
		FWorldDirectorGenerationStageMetric& Metric = GenerationMetrics[CompletedMetricIndex];
		ApplyDiagnostics(*Diagnostics, Metric);
	}
	const TSharedPtr<FJsonObject>* Payload = nullptr;
	if (!Envelope->TryGetObjectField(TEXT("payload"), Payload) || Payload == nullptr || !Payload->IsValid())
	{
		FinishGeneration(false, TEXT("Companion stage response has no payload object."));
		return;
	}
	FString ApplyError;
	const bool bApplied = GenerationStage == EWorldDirectorGenerationStage::Repair
		? ApplyTargetedRepairs(*Payload, ApplyError)
		: ApplyStagePayload(*Payload, ApplyError);
	if (!bApplied)
	{
		FinishGeneration(false, ApplyError);
		return;
	}

	const FString AcceptedPath = FPaths::Combine(
		GenerationRunDirectory,
		FString::Printf(TEXT("%02d-%s-accepted.json"), StageFileOrdinal(), *StageWireName()));
	SaveJsonObject(Envelope.ToSharedRef(), AcceptedPath);
	GenerationStageHistory.Add(StageWireName());

	if (GenerationStage == EWorldDirectorGenerationStage::Interpret)
	{
		GenerationStage = EWorldDirectorGenerationStage::Topology;
		RequestCurrentStage();
	}
	else if (GenerationStage == EWorldDirectorGenerationStage::Topology)
	{
		FString CandidateError;
		if (!BuildValidLayoutCandidates(CandidateError))
		{
			FinishGeneration(false, CandidateError);
			return;
		}
		GenerationStage = EWorldDirectorGenerationStage::Layout;
		RequestCurrentStage();
	}
	else if (GenerationStage == EWorldDirectorGenerationStage::Layout)
	{
		GenerationStage = EWorldDirectorGenerationStage::Population;
		RequestCurrentStage();
	}
	else if (GenerationStage == EWorldDirectorGenerationStage::Population ||
		GenerationStage == EWorldDirectorGenerationStage::Repair)
	{
		IntegrateAndValidate();
	}
}

bool UDirectorBridgeSubsystem::BuildValidLayoutCandidates(FString& OutError)
{
	FString Json;
	if (!WorkingGenerationDocument.IsValid() ||
		!SerializeJsonObject(WorkingGenerationDocument.ToSharedRef(), Json))
	{
		OutError = TEXT("Could not serialize semantic topology for layout generation.");
		return false;
	}
	FGeneratedWorldSpec TopologySpec;
	FValidationReport ParseReport;
	if (!FWorldDirectorJson::LoadGeneratedWorldSpec(Json, TopologySpec, ParseReport))
	{
		OutError = TEXT("Semantic topology is not shaped like the canonical world model.");
		return false;
	}

	LayoutCandidates.Reset();
	TSet<FString> PhysicalFingerprints;
	for (int32 Offset = 0; Offset < 12 && LayoutCandidates.Num() < 4; ++Offset)
	{
		const int64 CandidateSeed = static_cast<int64>(GenerationSeed) + Offset;
		if (CandidateSeed > MAX_int32)
		{
			break;
		}
		TopologySpec.Seed = static_cast<int32>(CandidateSeed);
		FResolvedWorldPlan CandidatePlan;
		FValidationReport CandidateReport;
		if (!FWorldDirectorCompiler::Resolve(TopologySpec, CandidatePlan, CandidateReport, false))
		{
			continue;
		}
		if (CandidatePlan.WorldFingerprint.IsEmpty() || PhysicalFingerprints.Contains(CandidatePlan.WorldFingerprint))
		{
			continue;
		}
		PhysicalFingerprints.Add(CandidatePlan.WorldFingerprint);
		FWorldDirectorLayoutCandidate& Candidate = LayoutCandidates.AddDefaulted_GetRef();
		Candidate.LayoutSeed = TopologySpec.Seed;
		Candidate.OpaqueId = TEXT("candidate.") + CandidatePlan.WorldFingerprint.Left(16);
		Candidate.Summary = FWorldDirectorPhysicalGenerator::BuildCandidateSummary(CandidatePlan);
		Candidate.TerrainArchetype = CandidatePlan.Terrain.Archetype;
		Candidate.ReliefCentimeters = CandidatePlan.Terrain.MaximumHeightCentimeters - CandidatePlan.Terrain.MinimumHeightCentimeters;
		Candidate.MeanSlopeDegrees = CandidatePlan.Terrain.MeanSlopeDegrees;
		Candidate.WorldFingerprint = CandidatePlan.WorldFingerprint;
		Candidate.TerrainFingerprint = CandidatePlan.Terrain.HeightFingerprint;
		Candidate.LayoutFingerprint = CandidatePlan.LayoutFingerprint;
		for (const FResolvedRoutePlan& Route : CandidatePlan.Routes)
		{
			Candidate.MaximumRoadGrade = FMath::Max(Candidate.MaximumRoadGrade, Route.MaximumGrade);
		}
	}
	if (LayoutCandidates.Num() < 2)
	{
		OutError = TEXT("Unreal could not produce multiple valid layout candidates from the accepted topology.");
		return false;
	}
	return true;
}

bool UDirectorBridgeSubsystem::ApplyStagePayload(
	const TSharedPtr<FJsonObject>& Payload,
	FString& OutError)
{
	if (!Payload.IsValid() || !WorkingGenerationDocument.IsValid())
	{
		OutError = TEXT("Generation working document is unavailable.");
		return false;
	}
	if (GenerationStage == EWorldDirectorGenerationStage::Interpret)
	{
		const TSharedPtr<FJsonObject>* Brief = nullptr;
		if (!Payload->TryGetObjectField(TEXT("brief"), Brief) || Brief == nullptr)
		{
			OutError = TEXT("Interpret stage did not return a world brief.");
			return false;
		}
		WorkingGenerationDocument->SetObjectField(TEXT("brief"), *Brief);
		return true;
	}
	if (GenerationStage == EWorldDirectorGenerationStage::Topology)
	{
		const TSharedPtr<FJsonObject>* Topology = nullptr;
		if (!Payload->TryGetObjectField(TEXT("topology"), Topology) || Topology == nullptr)
		{
			OutError = TEXT("Topology stage did not return semantic topology.");
			return false;
		}
		WorkingGenerationDocument->SetObjectField(TEXT("topology"), *Topology);
		for (const TCHAR* Field : { TEXT("locations"), TEXT("facts"), TEXT("threats") })
		{
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (!Payload->TryGetArrayField(Field, Values) || Values == nullptr)
			{
				OutError = FString::Printf(TEXT("Topology stage omitted '%s'."), Field);
				return false;
			}
			WorkingGenerationDocument->SetArrayField(Field, *Values);
		}
		return true;
	}
	if (GenerationStage == EWorldDirectorGenerationStage::Layout)
	{
		if (!Payload->TryGetStringField(TEXT("selectedCandidateId"), SelectedLayoutCandidateId))
		{
			OutError = TEXT("Layout stage did not select a candidate.");
			return false;
		}
		const FWorldDirectorLayoutCandidate* Match = LayoutCandidates.FindByPredicate(
			[this](const FWorldDirectorLayoutCandidate& Candidate)
			{ return Candidate.OpaqueId == SelectedLayoutCandidateId; });
		if (Match == nullptr)
		{
			OutError = TEXT("Layout stage selected an unknown opaque candidate ID.");
			return false;
		}
		WorkingGenerationDocument->SetNumberField(TEXT("seed"), Match->LayoutSeed);
		return true;
	}
	if (GenerationStage == EWorldDirectorGenerationStage::Population)
	{
		for (const TCHAR* Field : {
			TEXT("residents"), TEXT("households"), TEXT("relationships"),
			TEXT("beliefs"), TEXT("events"), TEXT("changeProjects") })
		{
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (!Payload->TryGetArrayField(Field, Values) || Values == nullptr)
			{
				OutError = FString::Printf(TEXT("Population stage omitted '%s'."), Field);
				return false;
			}
			WorkingGenerationDocument->SetArrayField(Field, *Values);
		}
		return true;
	}
	OutError = TEXT("Generation stage cannot accept a semantic payload.");
	return false;
}

bool UDirectorBridgeSubsystem::IntegrateAndValidate()
{
	const double IntegrateStartedAt = FPlatformTime::Seconds();
	FWorldDirectorGenerationStageMetric& IntegrateMetric = GenerationMetrics.AddDefaulted_GetRef();
	IntegrateMetric.Stage = TEXT("integrate");
	IntegrateMetric.StartedAtUtc = FDateTime::UtcNow().ToIso8601();
	GenerationStage = EWorldDirectorGenerationStage::Integrate;
	FString Json;
	if (!SerializeJsonObject(WorkingGenerationDocument.ToSharedRef(), Json))
	{
		IntegrateMetric.DurationSeconds = FPlatformTime::Seconds() - IntegrateStartedAt;
		IntegrateMetric.Error = TEXT("Could not serialize the integrated world document.");
		FinishGeneration(false, TEXT("Could not serialize the integrated world document."));
		return false;
	}
	UWorldGenerationSubsystem* Generation = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UWorldGenerationSubsystem>() : nullptr;
	const bool bLoaded = Generation != nullptr &&
		Generation->LoadAndValidateWorldSpec(Json, GeneratedWorldSpec, LastGenerationValidation);
	if (bLoaded)
	{
		LastGenerationValidation = Generation->ValidateFullSliceWorldSpec(GeneratedWorldSpec);
	}
	if (!bLoaded || !LastGenerationValidation.bValid)
	{
		IntegrateMetric.DurationSeconds = FPlatformTime::Seconds() - IntegrateStartedAt;
		IntegrateMetric.Error = TEXT("Validation requested a targeted repair.");
		TArray<FString> CurrentIssueKeys;
		for (const FValidationIssue& Issue : LastGenerationValidation.Issues)
		{
			CurrentIssueKeys.Add(Issue.Code.ToString() + TEXT("|") + Issue.Path);
		}
		CurrentIssueKeys.Sort();
		const FString CurrentIssueSignature = FString::Join(CurrentIssueKeys, TEXT(";"));
		if (!CurrentIssueSignature.IsEmpty() && CurrentIssueSignature == LastRepairIssueSignature)
		{
			++ConsecutiveRepairIssueCount;
		}
		else
		{
			LastRepairIssueSignature = CurrentIssueSignature;
			ConsecutiveRepairIssueCount = CurrentIssueSignature.IsEmpty() ? 0 : 1;
		}
		RecordGenerationLog(FString::Printf(TEXT("Integration found %d validation issue(s); repair attempt %d"),
			LastGenerationValidation.Issues.Num(), RepairAttempt + 1));
		GenerationIssueHistory.Append(LastGenerationValidation.Issues);
		SaveValidationReport();
		if (++RepairAttempt <= 3)
		{
			GenerationStage = EWorldDirectorGenerationStage::Repair;
			RequestCurrentStage();
			return false;
		}
		FinishGeneration(false,
			TEXT("Integrated world remained invalid after three targeted repair attempts."));
		return false;
	}

	FResolvedWorldPlan Resolved;
	FValidationReport ResolutionReport;
	const bool bResolved = Generation->ResolveWorldSpec(GeneratedWorldSpec, Resolved, ResolutionReport);
	GenerationIssueHistory.Append(ResolutionReport.Issues);
	if (!bResolved)
	{
		IntegrateMetric.DurationSeconds = FPlatformTime::Seconds() - IntegrateStartedAt;
		IntegrateMetric.Error = TEXT("Resolution to certified capabilities failed.");
		LastGenerationValidation = ResolutionReport;
		SaveValidationReport();
		FinishGeneration(false, TEXT("Integrated world could not resolve to certified capabilities."));
		return false;
	}
	LastResolvedWorldPlan = Resolved;

	FString AcceptedJson;
	FValidationReport SerializationReport;
	if (!Generation->SerializeWorldSpec(GeneratedWorldSpec, AcceptedJson, SerializationReport) ||
		!FFileHelper::SaveStringToFile(
			AcceptedJson,
			*FPaths::Combine(GenerationRunDirectory, TEXT("05-integrated-world.json")),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		IntegrateMetric.DurationSeconds = FPlatformTime::Seconds() - IntegrateStartedAt;
		IntegrateMetric.Error = TEXT("Could not persist the accepted integrated world.");
		FinishGeneration(false, TEXT("Could not persist the accepted integrated world."));
		return false;
	}
	FString RecipeJson;
	FValidationReport RecipeSerializationReport;
	if (!FWorldDirectorJson::SaveResolvedWorldPlan(Resolved, RecipeJson, RecipeSerializationReport) ||
		!FFileHelper::SaveStringToFile(
			RecipeJson,
			*FPaths::Combine(GenerationRunDirectory, TEXT("06-resolved-world-v3.json")),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		IntegrateMetric.DurationSeconds = FPlatformTime::Seconds() - IntegrateStartedAt;
		IntegrateMetric.Error = TEXT("Could not persist the replayable V3 physical recipe.");
		FinishGeneration(false, IntegrateMetric.Error);
		return false;
	}
	const TSharedRef<FJsonObject> Lab = MakeShared<FJsonObject>();
	Lab->SetNumberField(TEXT("recipeVersion"), Resolved.Version);
	Lab->SetStringField(TEXT("generatorVersion"), Resolved.GeneratorVersion);
	Lab->SetStringField(TEXT("contentVersion"), Resolved.ContentVersion);
	Lab->SetStringField(TEXT("environmentProfile"), Resolved.EnvironmentProfile.ToString());
	Lab->SetNumberField(TEXT("seed"), Resolved.Seed);
	Lab->SetStringField(TEXT("worldFingerprint"), Resolved.WorldFingerprint);
	Lab->SetStringField(TEXT("heightFingerprint"), Resolved.Terrain.HeightFingerprint);
	Lab->SetStringField(TEXT("surfaceFingerprint"), Resolved.Terrain.SurfaceFingerprint);
	Lab->SetStringField(TEXT("layoutFingerprint"), Resolved.LayoutFingerprint);
	Lab->SetStringField(TEXT("routeFingerprint"), Resolved.RouteFingerprint);
	Lab->SetStringField(TEXT("dressingFingerprint"), Resolved.DressingFingerprint);
	Lab->SetStringField(TEXT("terrainArchetype"),
		StaticEnum<EWorldDirectorTerrainArchetype>()->GetNameStringByValue(static_cast<int64>(Resolved.Terrain.Archetype)));
	Lab->SetNumberField(TEXT("minimumHeightCentimeters"), Resolved.Terrain.MinimumHeightCentimeters);
	Lab->SetNumberField(TEXT("maximumHeightCentimeters"), Resolved.Terrain.MaximumHeightCentimeters);
	Lab->SetNumberField(TEXT("meanSlopeDegrees"), Resolved.Terrain.MeanSlopeDegrees);
	Lab->SetNumberField(TEXT("districtCount"), Resolved.DistrictAnchors.Num());
	Lab->SetNumberField(TEXT("plotCount"), Resolved.Locations.Num());
	Lab->SetNumberField(TEXT("routeCount"), Resolved.Routes.Num());
	Lab->SetNumberField(TEXT("dressingCount"), Resolved.Dressing.Num());
	TArray<TSharedPtr<FJsonValue>> CandidateLabValues;
	for (const FWorldDirectorLayoutCandidate& Candidate : LayoutCandidates)
	{
		const TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("id"), Candidate.OpaqueId);
		Item->SetStringField(TEXT("summary"), Candidate.Summary);
		Item->SetStringField(TEXT("fingerprint"), Candidate.WorldFingerprint);
		Item->SetBoolField(TEXT("selected"), Candidate.OpaqueId == SelectedLayoutCandidateId);
		CandidateLabValues.Add(MakeShared<FJsonValueObject>(Item));
	}
	Lab->SetArrayField(TEXT("candidates"), CandidateLabValues);
	if (!SaveJsonObject(Lab, FPaths::Combine(GenerationRunDirectory, TEXT("world-generation-lab.json"))))
	{
		IntegrateMetric.DurationSeconds = FPlatformTime::Seconds() - IntegrateStartedAt;
		IntegrateMetric.Error = TEXT("Could not persist World Generation Lab metrics.");
		FinishGeneration(false, IntegrateMetric.Error);
		return false;
	}
	if (UWorld* World = GetWorld())
	{
		if (UWorldStateSubsystem* State = World->GetSubsystem<UWorldStateSubsystem>())
		{
			State->SetActiveWorldSpec(GeneratedWorldSpec);
		}
	}
	if (RepairAttempt > 0)
	{
		UE_LOG(LogWorldDirector, Display,
			TEXT("WORLD_DIRECTOR_GENERATION_REPAIR_RESULT=PASS attempts=%d"), RepairAttempt);
	}
	IntegrateMetric.DurationSeconds = FPlatformTime::Seconds() - IntegrateStartedAt;
	IntegrateMetric.bSuccess = true;
	IntegrateMetric.ResponseBytes = FTCHARToUTF8(*AcceptedJson).Length();
	RecordGenerationLog(FString::Printf(
		TEXT("Integration accepted; duration=%.2fs locations=%d residents=%d relationships=%d repairs=%d"),
		IntegrateMetric.DurationSeconds, GeneratedWorldSpec.Locations.Num(),
		GeneratedWorldSpec.Residents.Num(), GeneratedWorldSpec.Relationships.Num(), RepairAttempt));
	FinishGeneration(true, FString());
	return true;
}

bool UDirectorBridgeSubsystem::ApplyTargetedRepairs(
	const TSharedPtr<FJsonObject>& Payload,
	FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* Replacements = nullptr;
	if (!Payload.IsValid() ||
		!Payload->TryGetArrayField(TEXT("replacements"), Replacements) || Replacements == nullptr)
	{
		OutError = TEXT("Repair stage did not return targeted replacements.");
		return false;
	}
	TSet<FString> AllowedSections;
	for (const FValidationIssue& Issue : LastGenerationValidation.Issues)
	{
		AllowedSections.Add(ValidationSection(Issue.Path));
	}
	for (const TSharedPtr<FJsonValue>& ReplacementValue : *Replacements)
	{
		const TSharedPtr<FJsonObject> Replacement = ReplacementValue.IsValid()
			? ReplacementValue->AsObject() : nullptr;
		FString Section;
		double IndexNumber = -1;
		const TSharedPtr<FJsonValue> Value = Replacement.IsValid()
			? Replacement->TryGetField(TEXT("value")) : nullptr;
		const bool bHasIndex = Replacement.IsValid() &&
			Replacement->TryGetNumberField(TEXT("index"), IndexNumber);
		if (!Replacement.IsValid() ||
			!Replacement->TryGetStringField(TEXT("section"), Section) ||
			!Value.IsValid() ||
			!AllowedSections.Contains(Section))
		{
			OutError = TEXT("Repair attempted to modify a section not named by validation.");
			return false;
		}
		if (!bHasIndex)
		{
			if (Value->Type != EJson::Array)
			{
				OutError = TEXT("Whole-section repair value must be an array.");
				return false;
			}
			WorkingGenerationDocument->SetArrayField(Section, Value->AsArray());
			continue;
		}
		const int32 Index = static_cast<int32>(IndexNumber);
		const TArray<TSharedPtr<FJsonValue>>* Existing = nullptr;
		if (!WorkingGenerationDocument->TryGetArrayField(Section, Existing) ||
			Existing == nullptr || !Existing->IsValidIndex(Index))
		{
			OutError = TEXT("Repair replacement path does not identify an existing array entry.");
			return false;
		}
		TArray<TSharedPtr<FJsonValue>> Updated = *Existing;
		Updated[Index] = Value;
		WorkingGenerationDocument->SetArrayField(Section, Updated);
	}
	return true;
}

void UDirectorBridgeSubsystem::SaveValidationReport() const
{
	FString Json;
	if (FJsonObjectConverter::UStructToJsonObjectString(LastGenerationValidation, Json))
	{
		FFileHelper::SaveStringToFile(
			Json,
			*FPaths::Combine(
				GenerationRunDirectory,
				FString::Printf(TEXT("%02d-validation.json"), 5 + RepairAttempt)),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
}

void UDirectorBridgeSubsystem::FinishGeneration(const bool bSuccess, const FString& Error)
{
	GenerationFinishedElapsedSeconds = FPlatformTime::Seconds() - GenerationStartedAtSeconds;
	bGenerationRunning = false;
	LastGenerationError = Error;
	GenerationStage = bSuccess
		? EWorldDirectorGenerationStage::Completed
		: EWorldDirectorGenerationStage::Failed;
	RecordGenerationLog(FString::Printf(TEXT("Run finished; success=%s elapsed=%.2fs error=%s"),
		bSuccess ? TEXT("true") : TEXT("false"), GetGenerationElapsedSeconds(), *LastGenerationError));
	SaveRunSummary();
	OnGenerationFinished.Broadcast(bSuccess, GenerationRunId, LastGenerationError);
	UE_LOG(LogWorldDirector, Display,
		TEXT("WORLD_DIRECTOR_GENERATION_RESULT=%s run=%s error=%s"),
		bSuccess ? TEXT("PASS") : TEXT("FAIL"), *GenerationRunId, *LastGenerationError);
}

double UDirectorBridgeSubsystem::GetGenerationElapsedSeconds() const
{
	if (!bGenerationRunning && GenerationFinishedElapsedSeconds > 0.0)
	{
		return GenerationFinishedElapsedSeconds;
	}
	return GenerationStartedAtSeconds > 0.0
		? FMath::Max(0.0, FPlatformTime::Seconds() - GenerationStartedAtSeconds) : 0.0;
}

void UDirectorBridgeSubsystem::RecordGenerationLog(const FString& Message)
{
	GenerationLog.Add(FString::Printf(TEXT("%s  %s"),
		*FDateTime::UtcNow().ToIso8601(), *Message));
	if (GenerationLog.Num() > 250)
	{
		GenerationLog.RemoveAt(0, GenerationLog.Num() - 250);
	}
	UE_LOG(LogWorldDirector, Display, TEXT("WORLD_DIRECTOR_AI %s"), *Message);
}

void UDirectorBridgeSubsystem::RecordRuntimeCompilationMetric(
	const bool bSuccess, const double DurationSeconds, const int32 LocationCount,
	const int32 ResidentCount, const FString& Error)
{
	FWorldDirectorGenerationStageMetric& Metric = GenerationMetrics.AddDefaulted_GetRef();
	Metric.Stage = TEXT("unreal-compile");
	Metric.StartedAtUtc =
		(FDateTime::UtcNow() - FTimespan::FromSeconds(DurationSeconds)).ToIso8601();
	Metric.DurationSeconds = DurationSeconds;
	Metric.bSuccess = bSuccess;
	Metric.Error = Error;
	RecordGenerationLog(FString::Printf(
		TEXT("Unreal runtime compile finished; success=%s duration=%.2fs locations=%d residents=%d%s%s"),
		bSuccess ? TEXT("true") : TEXT("false"), DurationSeconds, LocationCount, ResidentCount,
		Error.IsEmpty() ? TEXT("") : TEXT(" error="), *Error));
	SaveRunSummary();
}

FString UDirectorBridgeSubsystem::BuildGenerationDiagnosticReport() const
{
	int64 TotalInputTokens = 0;
	int64 TotalCachedInputTokens = 0;
	int64 TotalOutputTokens = 0;
	int64 TotalReasoningTokens = 0;
	for (const FWorldDirectorGenerationStageMetric& Metric : GenerationMetrics)
	{
		TotalInputTokens += Metric.InputTokens;
		TotalCachedInputTokens += Metric.CachedInputTokens;
		TotalOutputTokens += Metric.OutputTokens;
		TotalReasoningTokens += Metric.ReasoningOutputTokens;
	}
	FString Report = FString::Printf(
		TEXT("WORLD DIRECTOR AI GENERATION\n"
			"State: %s\nProvider: %s\nModel: %s\nReasoning effort: %s\n"
			"Run: %s\nRun folder: %s\nElapsed: %.2f seconds\nPrompt characters: %d\n"
			"Seed: %d\nRepairs: %d\nValidation issues: %d\nSelected layout: %s\n"
			"Tokens: input=%lld (cached=%lld) output=%lld reasoning=%lld\n"
			"Provider-billed cost: unavailable; Codex CLI does not report a monetary charge.\n\n"
			"STAGE METRICS\n"),
		*StaticEnum<EWorldDirectorGenerationStage>()->GetNameStringByValue(static_cast<int64>(GenerationStage)),
		*ProviderName,
		GenerationModel.IsEmpty() ? TEXT("CLI default (not reported)") : *GenerationModel,
		GenerationReasoningEffort.IsEmpty() ? TEXT("CLI default") : *GenerationReasoningEffort,
		*GenerationRunId, *GenerationRunDirectory, GetGenerationElapsedSeconds(),
		GenerationPlayerPrompt.Len(), GenerationSeed, RepairAttempt, GenerationIssueHistory.Num(),
		*SelectedLayoutCandidateId, TotalInputTokens, TotalCachedInputTokens,
		TotalOutputTokens, TotalReasoningTokens);
	for (const FWorldDirectorGenerationStageMetric& Metric : GenerationMetrics)
	{
		const FString ExitLabel = Metric.ExitCode == INDEX_NONE
			? TEXT("n/a") : FString::FromInt(Metric.ExitCode);
		Report += FString::Printf(
			TEXT("- %-12s %7.2fs  request=%lld B  response=%lld B  exit=%s  %s\n"
				"  model=%s reasoning=%s tokens: input=%lld cached=%lld output=%lld reasoning=%lld\n"
				"  request: %s\n  constructed prompt: %s\n  raw model response: %s\n"
				"  structured response: %s\n  provider events: %s\n  telemetry: %s%s%s%s%s\n"),
			*Metric.Stage, Metric.DurationSeconds, Metric.RequestBytes, Metric.ResponseBytes,
			*ExitLabel, Metric.bSuccess ? TEXT("PASS") :
				(Metric.Error.IsEmpty() ? TEXT("IN PROGRESS") : TEXT("FAIL")),
			Metric.Model.IsEmpty() ? TEXT("n/a") : *Metric.Model,
			Metric.ReasoningEffort.IsEmpty() ? TEXT("n/a") : *Metric.ReasoningEffort,
			Metric.InputTokens, Metric.CachedInputTokens, Metric.OutputTokens,
			Metric.ReasoningOutputTokens,
			*Metric.RequestPath, *Metric.PromptPath, *Metric.RawResponsePath,
			*Metric.ResponsePath, *Metric.ProviderEventsPath, *Metric.TelemetryPath,
			Metric.Error.IsEmpty() ? TEXT("") : TEXT("\n  error: "), *Metric.Error,
			Metric.ProviderOutput.IsEmpty() ? TEXT("") : TEXT("\n  provider output: "),
				*Metric.ProviderOutput);
	}
	Report += TEXT("\nPHYSICAL WORLD\n");
	if (LastResolvedWorldPlan.WorldFingerprint.IsEmpty())
	{
		Report += TEXT("- Physical recipe not resolved yet.\n");
	}
	else
	{
		Report += FString::Printf(
			TEXT("- recipe=v%d generator=%s content=%s profile=%s\n"
				"- terrain=%s relief=%.0fcm meanSlope=%.1fdeg grid=%dx%d waterPoints=%d\n"
				"- districts=%d plots=%d routes=%d dressing=%d\n"
				"- world=%s\n- height=%s\n- surfaces=%s\n- layout=%s\n- routes=%s\n- dressing=%s\n"),
			LastResolvedWorldPlan.Version, *LastResolvedWorldPlan.GeneratorVersion,
			*LastResolvedWorldPlan.ContentVersion, *LastResolvedWorldPlan.EnvironmentProfile.ToString(),
			*StaticEnum<EWorldDirectorTerrainArchetype>()->GetNameStringByValue(static_cast<int64>(LastResolvedWorldPlan.Terrain.Archetype)),
			LastResolvedWorldPlan.Terrain.MaximumHeightCentimeters - LastResolvedWorldPlan.Terrain.MinimumHeightCentimeters,
			LastResolvedWorldPlan.Terrain.MeanSlopeDegrees, LastResolvedWorldPlan.Terrain.Resolution,
			LastResolvedWorldPlan.Terrain.Resolution, LastResolvedWorldPlan.Terrain.WaterControlPoints.Num(),
			LastResolvedWorldPlan.DistrictAnchors.Num(), LastResolvedWorldPlan.Locations.Num(),
			LastResolvedWorldPlan.Routes.Num(), LastResolvedWorldPlan.Dressing.Num(),
			*LastResolvedWorldPlan.WorldFingerprint, *LastResolvedWorldPlan.Terrain.HeightFingerprint,
			*LastResolvedWorldPlan.Terrain.SurfaceFingerprint, *LastResolvedWorldPlan.LayoutFingerprint,
			*LastResolvedWorldPlan.RouteFingerprint, *LastResolvedWorldPlan.DressingFingerprint);
	}
	Report += TEXT("\nVALIDATION / REPAIR\n");
	if (GenerationIssueHistory.IsEmpty())
	{
		Report += TEXT("- No validation issue recorded.\n");
	}
	for (const FValidationIssue& Issue : GenerationIssueHistory)
	{
		Report += FString::Printf(TEXT("- %s  %s  %s\n"),
			*Issue.Code.ToString(), *Issue.Path, *Issue.Message);
	}
	Report += TEXT("\nEVENT LOG\n");
	Report += FString::Join(GenerationLog, TEXT("\n"));
	return Report;
}

void UDirectorBridgeSubsystem::SaveRunSummary() const
{
	if (GenerationRunDirectory.IsEmpty())
	{
		return;
	}
	const TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
	Summary->SetStringField(TEXT("runId"), GenerationRunId);
	Summary->SetStringField(TEXT("provider"), ProviderName);
	Summary->SetStringField(TEXT("providerModel"),
		GenerationModel.IsEmpty() ? TEXT("CLI default (not reported)") : GenerationModel);
	Summary->SetStringField(TEXT("reasoningEffort"),
		GenerationReasoningEffort.IsEmpty() ? TEXT("CLI default") : GenerationReasoningEffort);
	Summary->SetStringField(TEXT("providerBilledCost"),
		TEXT("unavailable: Codex CLI does not report a monetary charge"));
	Summary->SetStringField(TEXT("state"),
		StaticEnum<EWorldDirectorGenerationStage>()->GetNameStringByValue(static_cast<int64>(GenerationStage)));
	Summary->SetNumberField(TEXT("seed"), GenerationSeed);
	Summary->SetNumberField(TEXT("promptCharacters"), GenerationPlayerPrompt.Len());
	Summary->SetNumberField(TEXT("elapsedSeconds"), GetGenerationElapsedSeconds());
	Summary->SetNumberField(TEXT("repairAttempts"), RepairAttempt);
	Summary->SetStringField(TEXT("error"), LastGenerationError);
	Summary->SetStringField(TEXT("worldFingerprint"), LastResolvedWorldPlan.WorldFingerprint);
	Summary->SetStringField(TEXT("terrainFingerprint"), LastResolvedWorldPlan.Terrain.HeightFingerprint);
	Summary->SetStringField(TEXT("surfaceFingerprint"), LastResolvedWorldPlan.Terrain.SurfaceFingerprint);
	Summary->SetStringField(TEXT("layoutFingerprint"), LastResolvedWorldPlan.LayoutFingerprint);
	Summary->SetNumberField(TEXT("rootSeed"), GenerationSeed);
	Summary->SetNumberField(TEXT("selectedLayoutSeed"), LastResolvedWorldPlan.Seed);
	Summary->SetStringField(TEXT("selectedCandidateId"), SelectedLayoutCandidateId);
	TArray<TSharedPtr<FJsonValue>> Metrics;
	int64 TotalInputTokens = 0;
	int64 TotalCachedInputTokens = 0;
	int64 TotalOutputTokens = 0;
	int64 TotalReasoningOutputTokens = 0;
	for (const FWorldDirectorGenerationStageMetric& Metric : GenerationMetrics)
	{
		TotalInputTokens += Metric.InputTokens;
		TotalCachedInputTokens += Metric.CachedInputTokens;
		TotalOutputTokens += Metric.OutputTokens;
		TotalReasoningOutputTokens += Metric.ReasoningOutputTokens;
		const TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("stage"), Metric.Stage);
		Item->SetStringField(TEXT("requestId"), Metric.RequestId);
		Item->SetStringField(TEXT("startedAtUtc"), Metric.StartedAtUtc);
		Item->SetNumberField(TEXT("durationSeconds"), Metric.DurationSeconds);
		Item->SetNumberField(TEXT("requestBytes"), static_cast<double>(Metric.RequestBytes));
		Item->SetNumberField(TEXT("responseBytes"), static_cast<double>(Metric.ResponseBytes));
		Item->SetNumberField(TEXT("exitCode"), Metric.ExitCode);
		Item->SetBoolField(TEXT("success"), Metric.bSuccess);
		Item->SetStringField(TEXT("error"), Metric.Error);
		Item->SetStringField(TEXT("providerOutput"), Metric.ProviderOutput);
		Item->SetStringField(TEXT("requestPath"), Metric.RequestPath);
		Item->SetStringField(TEXT("responsePath"), Metric.ResponsePath);
		Item->SetStringField(TEXT("rawResponsePath"), Metric.RawResponsePath);
		Item->SetStringField(TEXT("promptPath"), Metric.PromptPath);
		Item->SetStringField(TEXT("providerEventsPath"), Metric.ProviderEventsPath);
		Item->SetStringField(TEXT("telemetryPath"), Metric.TelemetryPath);
		Item->SetStringField(TEXT("model"), Metric.Model);
		Item->SetStringField(TEXT("reasoningEffort"), Metric.ReasoningEffort);
		Item->SetStringField(TEXT("providerThreadId"), Metric.ProviderThreadId);
		Item->SetNumberField(TEXT("inputTokens"), static_cast<double>(Metric.InputTokens));
		Item->SetNumberField(TEXT("cachedInputTokens"), static_cast<double>(Metric.CachedInputTokens));
		Item->SetNumberField(TEXT("outputTokens"), static_cast<double>(Metric.OutputTokens));
		Item->SetNumberField(
			TEXT("reasoningOutputTokens"), static_cast<double>(Metric.ReasoningOutputTokens));
		Item->SetNumberField(TEXT("promptCharacters"), static_cast<double>(Metric.PromptCharacters));
		Item->SetStringField(TEXT("costNote"), Metric.CostNote);
		Metrics.Add(MakeShared<FJsonValueObject>(Item));
	}
	Summary->SetArrayField(TEXT("stages"), Metrics);
	Summary->SetNumberField(TEXT("inputTokens"), static_cast<double>(TotalInputTokens));
	Summary->SetNumberField(TEXT("cachedInputTokens"), static_cast<double>(TotalCachedInputTokens));
	Summary->SetNumberField(TEXT("outputTokens"), static_cast<double>(TotalOutputTokens));
	Summary->SetNumberField(
		TEXT("reasoningOutputTokens"), static_cast<double>(TotalReasoningOutputTokens));
	TArray<TSharedPtr<FJsonValue>> Events;
	for (const FString& Event : GenerationLog)
	{
		Events.Add(MakeShared<FJsonValueString>(Event));
	}
	Summary->SetArrayField(TEXT("events"), Events);
	SaveJsonObject(Summary, FPaths::Combine(GenerationRunDirectory, TEXT("run-summary.json")));
}

void UDirectorBridgeSubsystem::SetDirectorConnected(
	const bool bConnected,
	const FString& InProviderName)
{
	bDirectorConnected = bConnected;
	ProviderName = bConnected ? InProviderName : FString();
}
