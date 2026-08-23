#include "WorldDirectorValidation.h"

#include "WorldDirectorJson.h"

namespace
{
template <typename ItemType>
TMap<FString, const ItemType*> IndexById(
	const TArray<ItemType>& Items,
	const FString& Path,
	FValidationReport& Report)
{
	TMap<FString, const ItemType*> Result;
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		const ItemType& Item = Items[Index];
		const FString ItemPath = FString::Printf(TEXT("%s[%d].id"), *Path, Index);
		if (Item.Id.IsEmpty())
		{
			Report.AddError(TEXT("id.empty"), ItemPath, TEXT("Stable ID must not be empty."));
			continue;
		}
		if (Result.Contains(Item.Id))
		{
			Report.AddError(
				TEXT("id.duplicate"),
				ItemPath,
				FString::Printf(TEXT("Stable ID '%s' is duplicated."), *Item.Id));
			continue;
		}
		Result.Add(Item.Id, &Item);
	}
	return Result;
}

void RequireReference(
	const FString& Id,
	const TSet<FString>& ValidIds,
	const FString& Path,
	const TCHAR* Kind,
	FValidationReport& Report)
{
	if (Id.IsEmpty() || !ValidIds.Contains(Id))
	{
		Report.AddError(
			TEXT("reference.missing"),
			Path,
			FString::Printf(TEXT("Referenced %s ID '%s' does not exist."), Kind, *Id));
	}
}

void RequireCapabilityTag(
	const FName Tag,
	const TSet<FName>& CapabilityTags,
	const FString& Path,
	FValidationReport& Report)
{
	if (Tag.IsNone() || !CapabilityTags.Contains(Tag))
	{
		Report.AddError(
			TEXT("capability.tag_missing"),
			Path,
			FString::Printf(TEXT("Mechanical capability tag '%s' is not registered."), *Tag.ToString()));
	}
}

void RequireVersion(
	const int32 Version,
	const FString& Path,
	FValidationReport& Report)
{
	if (Version != 1)
	{
		Report.AddError(
			TEXT("schema.version_unsupported"),
			Path,
			FString::Printf(TEXT("Only schema version 1 is supported; received %d."), Version));
	}
}
}

bool FWorldDirectorValidator::LooksLikeUnrealAssetPath(const FString& Value)
{
	return Value.Contains(TEXT("/Game/"), ESearchCase::IgnoreCase)
		|| Value.Contains(TEXT("/Engine/"), ESearchCase::IgnoreCase)
		|| Value.Contains(TEXT(".uasset"), ESearchCase::IgnoreCase)
		|| Value.Contains(TEXT("SoftObjectPath"), ESearchCase::IgnoreCase);
}

FValidationReport FWorldDirectorValidator::Validate(
	const FGeneratedWorldSpec& Spec,
	const TSet<FName>& CapabilityTags)
{
	FValidationReport Report;
	RequireVersion(Spec.Version, TEXT("$.version"), Report);
	RequireVersion(Spec.Brief.Version, TEXT("$.brief.version"), Report);
	RequireVersion(Spec.Topology.Version, TEXT("$.topology.version"), Report);

	if (Spec.Id.IsEmpty())
	{
		Report.AddError(TEXT("id.empty"), TEXT("$.id"), TEXT("World spec ID must not be empty."));
	}
	if (Spec.Brief.Id.IsEmpty())
	{
		Report.AddError(TEXT("id.empty"), TEXT("$.brief.id"), TEXT("World brief ID must not be empty."));
	}
	if (Spec.Topology.Id.IsEmpty())
	{
		Report.AddError(TEXT("id.empty"), TEXT("$.topology.id"), TEXT("Town topology ID must not be empty."));
	}
	if (Spec.Brief.Theme.IsEmpty() || Spec.Brief.SettlementIdentity.IsEmpty() ||
		Spec.Brief.TerrainPreferences.IsEmpty())
	{
		Report.AddError(
			TEXT("generation.brief_incomplete"), TEXT("$.brief"),
			TEXT("World brief requires a theme, settlement identity, and terrain preferences."));
	}
	if (Spec.Topology.Districts.IsEmpty() || Spec.Topology.Governance.IsEmpty() ||
		Spec.Topology.HistoricalWound.IsEmpty() || Spec.Topology.CurrentTension.IsEmpty() ||
		Spec.Topology.SupernaturalPremise.IsEmpty() || Spec.Topology.CentralThreat.IsEmpty())
	{
		Report.AddError(
			TEXT("generation.topology_incomplete"), TEXT("$.topology"),
			TEXT("Semantic topology requires districts, governance, historical wound, current tension, supernatural premise, and central threat."));
	}

	const TMap<FString, const FWorldLocation*> LocationIndex =
		IndexById(Spec.Locations, TEXT("$.locations"), Report);
	const TMap<FString, const FResident*> ResidentIndex =
		IndexById(Spec.Residents, TEXT("$.residents"), Report);
	const TMap<FString, const FHousehold*> HouseholdIndex =
		IndexById(Spec.Households, TEXT("$.households"), Report);
	const TMap<FString, const FRelationship*> RelationshipIndex =
		IndexById(Spec.Relationships, TEXT("$.relationships"), Report);
	const TMap<FString, const FBelief*> BeliefIndex =
		IndexById(Spec.Beliefs, TEXT("$.beliefs"), Report);
	const TMap<FString, const FWorldFact*> FactIndex =
		IndexById(Spec.Facts, TEXT("$.facts"), Report);
	IndexById(Spec.Events, TEXT("$.events"), Report);
	IndexById(Spec.Threats, TEXT("$.threats"), Report);
	IndexById(Spec.ChangeProjects, TEXT("$.changeProjects"), Report);

	TSet<FString> LocationIds;
	LocationIndex.GetKeys(LocationIds);
	TSet<FString> ResidentIds;
	ResidentIndex.GetKeys(ResidentIds);
	TSet<FString> HouseholdIds;
	HouseholdIndex.GetKeys(HouseholdIds);
	TSet<FString> RelationshipIds;
	RelationshipIndex.GetKeys(RelationshipIds);
	TSet<FString> BeliefIds;
	BeliefIndex.GetKeys(BeliefIds);
	TSet<FString> FactIds;
	FactIndex.GetKeys(FactIds);

	for (int32 Index = 0; Index < Spec.Brief.RequiredCapabilityTags.Num(); ++Index)
	{
		RequireCapabilityTag(
			Spec.Brief.RequiredCapabilityTags[Index],
			CapabilityTags,
			FString::Printf(TEXT("$.brief.requiredCapabilityTags[%d]"), Index),
			Report);
	}

	for (int32 Index = 0; Index < Spec.Locations.Num(); ++Index)
	{
		const FWorldLocation& Location = Spec.Locations[Index];
		const FString Path = FString::Printf(TEXT("$.locations[%d]"), Index);
		RequireVersion(Location.Version, Path + TEXT(".version"), Report);
		RequireCapabilityTag(Location.PurposeTag, CapabilityTags, Path + TEXT(".purposeTag"), Report);
		if (Location.OwnerResidentId.IsEmpty() && Location.ControllerResidentId.IsEmpty())
		{
			Report.AddError(
				TEXT("location.authority_missing"),
				Path,
				TEXT("Location requires an ownerResidentId or controllerResidentId."));
		}
		if (!Location.OwnerResidentId.IsEmpty())
		{
			RequireReference(Location.OwnerResidentId, ResidentIds, Path + TEXT(".ownerResidentId"), TEXT("resident"), Report);
		}
		if (!Location.ControllerResidentId.IsEmpty())
		{
			RequireReference(Location.ControllerResidentId, ResidentIds, Path + TEXT(".controllerResidentId"), TEXT("resident"), Report);
		}
		for (int32 TagIndex = 0; TagIndex < Location.RequiredCapabilityTags.Num(); ++TagIndex)
		{
			RequireCapabilityTag(
				Location.RequiredCapabilityTags[TagIndex],
				CapabilityTags,
				FString::Printf(TEXT("%s.requiredCapabilityTags[%d]"), *Path, TagIndex),
				Report);
		}
	}

	for (int32 Index = 0; Index < Spec.Residents.Num(); ++Index)
	{
		const FResident& Resident = Spec.Residents[Index];
		const FString Path = FString::Printf(TEXT("$.residents[%d]"), Index);
		RequireVersion(Resident.Version, Path + TEXT(".version"), Report);
		RequireReference(Resident.HomeLocationId, LocationIds, Path + TEXT(".homeLocationId"), TEXT("location"), Report);
		RequireReference(Resident.HouseholdId, HouseholdIds, Path + TEXT(".householdId"), TEXT("household"), Report);
		if (Resident.bEmployed)
		{
			RequireReference(Resident.WorkplaceLocationId, LocationIds, Path + TEXT(".workplaceLocationId"), TEXT("location"), Report);
			RequireCapabilityTag(Resident.OccupationTag, CapabilityTags, Path + TEXT(".occupationTag"), Report);
		}
		if (!Resident.CurrentLocationId.IsEmpty())
		{
			RequireReference(Resident.CurrentLocationId, LocationIds, Path + TEXT(".currentLocationId"), TEXT("location"), Report);
		}
		for (int32 MemoryIndex = 0; MemoryIndex < Resident.ImportantMemories.Num(); ++MemoryIndex)
		{
			const FResidentMemory& Memory = Resident.ImportantMemories[MemoryIndex];
			const FString MemoryPath = FString::Printf(TEXT("%s.importantMemories[%d]"), *Path, MemoryIndex);
			RequireReference(Memory.FactId, FactIds, MemoryPath + TEXT(".factId"), TEXT("fact"), Report);
			if (Memory.EmotionalSignificance < 0.0f || Memory.EmotionalSignificance > 1.0f)
			{
				Report.AddError(TEXT("memory.significance_range"), MemoryPath + TEXT(".emotionalSignificance"),
					TEXT("Emotional significance must be between 0 and 1."));
			}
		}
		for (int32 BeliefIdIndex = 0; BeliefIdIndex < Resident.BeliefIds.Num(); ++BeliefIdIndex)
		{
			RequireReference(
				Resident.BeliefIds[BeliefIdIndex],
				BeliefIds,
				FString::Printf(TEXT("%s.beliefIds[%d]"), *Path, BeliefIdIndex),
				TEXT("belief"),
				Report);
		}
		for (int32 RelationshipIdIndex = 0; RelationshipIdIndex < Resident.RelationshipIds.Num(); ++RelationshipIdIndex)
		{
			RequireReference(
				Resident.RelationshipIds[RelationshipIdIndex],
				RelationshipIds,
				FString::Printf(TEXT("%s.relationshipIds[%d]"), *Path, RelationshipIdIndex),
				TEXT("relationship"),
				Report);
		}
	}

	for (int32 Index = 0; Index < Spec.Households.Num(); ++Index)
	{
		const FHousehold& Household = Spec.Households[Index];
		const FString Path = FString::Printf(TEXT("$.households[%d]"), Index);
		RequireVersion(Household.Version, Path + TEXT(".version"), Report);
		RequireReference(Household.HomeLocationId, LocationIds, Path + TEXT(".homeLocationId"), TEXT("location"), Report);
		for (int32 MemberIndex = 0; MemberIndex < Household.MemberResidentIds.Num(); ++MemberIndex)
		{
			const FString& MemberId = Household.MemberResidentIds[MemberIndex];
			RequireReference(
				MemberId,
				ResidentIds,
				FString::Printf(TEXT("%s.memberResidentIds[%d]"), *Path, MemberIndex),
				TEXT("resident"),
				Report);
			if (const FResident* const* Resident = ResidentIndex.Find(MemberId))
			{
				if ((*Resident)->HouseholdId != Household.Id || (*Resident)->HomeLocationId != Household.HomeLocationId)
				{
					Report.AddError(
						TEXT("household.membership_inconsistent"),
						Path,
						FString::Printf(TEXT("Resident '%s' does not point back to household '%s' and its home."), *MemberId, *Household.Id));
				}
			}
		}
		if (const FWorldLocation* const* Home = LocationIndex.Find(Household.HomeLocationId))
		{
			if (Household.MemberResidentIds.Num() > (*Home)->ResidentCapacity)
			{
				Report.AddError(
					TEXT("household.capacity_exceeded"),
					Path + TEXT(".memberResidentIds"),
					FString::Printf(
						TEXT("Household has %d members but home '%s' capacity is %d."),
						Household.MemberResidentIds.Num(),
						*Household.HomeLocationId,
						(*Home)->ResidentCapacity));
			}
		}
	}

	for (int32 Index = 0; Index < Spec.Relationships.Num(); ++Index)
	{
		const FRelationship& Relationship = Spec.Relationships[Index];
		const FString Path = FString::Printf(TEXT("$.relationships[%d]"), Index);
		RequireVersion(Relationship.Version, Path + TEXT(".version"), Report);
		RequireReference(Relationship.SourceResidentId, ResidentIds, Path + TEXT(".sourceResidentId"), TEXT("resident"), Report);
		RequireReference(Relationship.TargetResidentId, ResidentIds, Path + TEXT(".targetResidentId"), TEXT("resident"), Report);
		if (Relationship.SourceResidentId == Relationship.TargetResidentId)
		{
			Report.AddError(TEXT("relationship.self_reference"), Path, TEXT("Relationship endpoints must be different residents."));
		}
		const float Scores[] = {Relationship.Trust, Relationship.Affinity, Relationship.Fear, Relationship.Obligation};
		const TCHAR* ScoreNames[] = {TEXT("trust"), TEXT("affinity"), TEXT("fear"), TEXT("obligation")};
		for (int32 ScoreIndex = 0; ScoreIndex < UE_ARRAY_COUNT(Scores); ++ScoreIndex)
		{
			if (Scores[ScoreIndex] < 0.0f || Scores[ScoreIndex] > 1.0f)
			{
				Report.AddError(TEXT("relationship.score_range"), Path + TEXT(".") + ScoreNames[ScoreIndex],
					TEXT("Relationship mechanics must stay between 0 and 1."));
			}
		}
		if (Relationship.bBidirectional)
		{
			const FRelationship* const* Reciprocal = RelationshipIndex.Find(Relationship.ReciprocalRelationshipId);
			if (!Reciprocal
				|| (*Reciprocal)->SourceResidentId != Relationship.TargetResidentId
				|| (*Reciprocal)->TargetResidentId != Relationship.SourceResidentId
				|| (*Reciprocal)->RelationshipType != Relationship.RelationshipType
				|| (*Reciprocal)->ReciprocalRelationshipId != Relationship.Id)
			{
				Report.AddError(
					TEXT("relationship.reciprocal_inconsistent"),
					Path + TEXT(".reciprocalRelationshipId"),
					TEXT("Bidirectional relationship requires a matching inverse relationship."));
			}
		}
	}

	for (int32 Index = 0; Index < Spec.Beliefs.Num(); ++Index)
	{
		const FBelief& Belief = Spec.Beliefs[Index];
		const FString Path = FString::Printf(TEXT("$.beliefs[%d]"), Index);
		RequireVersion(Belief.Version, Path + TEXT(".version"), Report);
		RequireReference(Belief.HolderResidentId, ResidentIds, Path + TEXT(".holderResidentId"), TEXT("resident"), Report);
		RequireReference(Belief.FactId, FactIds, Path + TEXT(".factId"), TEXT("fact"), Report);
		if (Belief.Confidence < 0.0f || Belief.Confidence > 1.0f)
		{
			Report.AddError(TEXT("belief.confidence_range"), Path + TEXT(".confidence"), TEXT("Confidence must be between 0 and 1."));
		}
		if (!Belief.SourceResidentId.IsEmpty())
		{
			RequireReference(Belief.SourceResidentId, ResidentIds, Path + TEXT(".sourceResidentId"), TEXT("resident"), Report);
		}
		if (Belief.Secrecy < 0.0f || Belief.Secrecy > 1.0f ||
			Belief.EmotionalSignificance < 0.0f || Belief.EmotionalSignificance > 1.0f ||
			Belief.WillingnessToShare < 0.0f || Belief.WillingnessToShare > 1.0f)
		{
			Report.AddError(TEXT("belief.social_range"), Path,
				TEXT("Secrecy, emotional significance, and willingness to share must be between 0 and 1."));
		}
		if (Belief.bImportantSecret)
		{
			const FWorldFact* const* Fact = FactIndex.Find(Belief.FactId);
			if (!Fact || !(*Fact)->bEstablished)
			{
				Report.AddError(
					TEXT("secret.fact_not_established"),
					Path + TEXT(".factId"),
					TEXT("Important secret must reference an established world fact."));
			}
		}
	}

	for (int32 Index = 0; Index < Spec.Facts.Num(); ++Index)
	{
		const FWorldFact& Fact = Spec.Facts[Index];
		RequireVersion(Fact.Version, FString::Printf(TEXT("$.facts[%d].version"), Index), Report);
		if (Fact.bSecret && !Spec.Beliefs.ContainsByPredicate(
			[&Fact](const FBelief& Belief) { return Belief.FactId == Fact.Id; }))
		{
			Report.AddError(
				TEXT("secret.orphaned"),
				FString::Printf(TEXT("$.facts[%d]"), Index),
				TEXT("A secret fact must be held by at least one resident belief."));
		}
	}

	for (int32 Index = 0; Index < Spec.Events.Num(); ++Index)
	{
		const FWorldEvent& Event = Spec.Events[Index];
		const FString Path = FString::Printf(TEXT("$.events[%d]"), Index);
		RequireVersion(Event.Version, Path + TEXT(".version"), Report);
		for (int32 ParticipantIndex = 0; ParticipantIndex < Event.ParticipantResidentIds.Num(); ++ParticipantIndex)
		{
			RequireReference(Event.ParticipantResidentIds[ParticipantIndex], ResidentIds, FString::Printf(TEXT("%s.participantResidentIds[%d]"), *Path, ParticipantIndex), TEXT("resident"), Report);
		}
		for (int32 LocationIdIndex = 0; LocationIdIndex < Event.LocationIds.Num(); ++LocationIdIndex)
		{
			RequireReference(Event.LocationIds[LocationIdIndex], LocationIds, FString::Printf(TEXT("%s.locationIds[%d]"), *Path, LocationIdIndex), TEXT("location"), Report);
		}
		for (int32 FactIdIndex = 0; FactIdIndex < Event.RevealedFactIds.Num(); ++FactIdIndex)
		{
			const FString& FactId = Event.RevealedFactIds[FactIdIndex];
			RequireReference(FactId, FactIds, FString::Printf(TEXT("%s.revealedFactIds[%d]"), *Path, FactIdIndex), TEXT("fact"), Report);
			if (const FWorldFact* const* Fact = FactIndex.Find(FactId))
			{
				if (!(*Fact)->bEstablished || (*Fact)->EstablishedDay > Event.Day)
				{
					Report.AddError(
						TEXT("fact.retroactive_invention"),
						FString::Printf(TEXT("%s.revealedFactIds[%d]"), *Path, FactIdIndex),
						TEXT("A revelation cannot establish a fact after the event that reveals it."));
				}
			}
		}
	}

	for (int32 Index = 0; Index < Spec.Threats.Num(); ++Index)
	{
		const FThreat& Threat = Spec.Threats[Index];
		const FString Path = FString::Printf(TEXT("$.threats[%d]"), Index);
		RequireVersion(Threat.Version, Path + TEXT(".version"), Report);
		for (int32 LocationIdIndex = 0; LocationIdIndex < Threat.AffectedLocationIds.Num(); ++LocationIdIndex)
		{
			RequireReference(Threat.AffectedLocationIds[LocationIdIndex], LocationIds, FString::Printf(TEXT("%s.affectedLocationIds[%d]"), *Path, LocationIdIndex), TEXT("location"), Report);
		}
	}

	for (int32 Index = 0; Index < Spec.ChangeProjects.Num(); ++Index)
	{
		const FChangeProject& Project = Spec.ChangeProjects[Index];
		const FString Path = FString::Printf(TEXT("$.changeProjects[%d]"), Index);
		RequireVersion(Project.Version, Path + TEXT(".version"), Report);
		RequireReference(Project.InitiatorResidentId, ResidentIds, Path + TEXT(".initiatorResidentId"), TEXT("resident"), Report);
		RequireReference(Project.TargetLocationId, LocationIds, Path + TEXT(".targetLocationId"), TEXT("location"), Report);
		RequireCapabilityTag(Project.DesiredPurposeTag, CapabilityTags, Path + TEXT(".desiredPurposeTag"), Report);
		for (int32 ParticipantIndex = 0; ParticipantIndex < Project.RequiredParticipantResidentIds.Num(); ++ParticipantIndex)
		{
			RequireReference(Project.RequiredParticipantResidentIds[ParticipantIndex], ResidentIds, FString::Printf(TEXT("%s.requiredParticipantResidentIds[%d]"), *Path, ParticipantIndex), TEXT("resident"), Report);
		}
		for (int32 TagIndex = 0; TagIndex < Project.RequiredCapabilityTags.Num(); ++TagIndex)
		{
			RequireCapabilityTag(Project.RequiredCapabilityTags[TagIndex], CapabilityTags, FString::Printf(TEXT("%s.requiredCapabilityTags[%d]"), *Path, TagIndex), Report);
		}
		const TSet<FName> SupportedConditions = {
			TEXT("Condition.ThreatActive"), TEXT("Condition.Overnight"), TEXT("Condition.PlayerAway")
		};
		for (int32 ConditionIndex = 0; ConditionIndex < Project.RequiredConditionTags.Num(); ++ConditionIndex)
		{
			if (!SupportedConditions.Contains(Project.RequiredConditionTags[ConditionIndex]))
			{
				Report.AddError(TEXT("project.condition_unsupported"),
					FString::Printf(TEXT("%s.requiredConditionTags[%d]"), *Path, ConditionIndex),
					TEXT("Project condition is not implemented by the runtime evaluator."));
			}
		}
		if (Project.IntendedStartMinute < 0)
		{
			Report.AddError(TEXT("project.timing_invalid"), Path + TEXT(".intendedStartMinute"),
				TEXT("Project intended timing cannot be negative."));
		}
		if (Project.RequiredTransitionMinutes < 60 || Project.RequiredTransitionMinutes > 1440)
		{
			Report.AddError(TEXT("project.transition_time_invalid"), Path + TEXT(".requiredTransitionMinutes"),
				TEXT("Project transition time must be between 60 and 1440 in-game minutes."));
		}
	}

	for (int32 Index = 0; Index < Spec.Topology.Edges.Num(); ++Index)
	{
		const FTownTopologyEdge& Edge = Spec.Topology.Edges[Index];
		RequireReference(Edge.FromLocationId, LocationIds, FString::Printf(TEXT("$.topology.edges[%d].fromLocationId"), Index), TEXT("location"), Report);
		RequireReference(Edge.ToLocationId, LocationIds, FString::Printf(TEXT("$.topology.edges[%d].toLocationId"), Index), TEXT("location"), Report);
	}
	for (int32 Index = 0; Index < Spec.Topology.LandmarkLocationIds.Num(); ++Index)
	{
		RequireReference(Spec.Topology.LandmarkLocationIds[Index], LocationIds, FString::Printf(TEXT("$.topology.landmarkLocationIds[%d]"), Index), TEXT("location"), Report);
	}

	FString AiFacingJson;
	FValidationReport SerializationReport;
	if (FWorldDirectorJson::SaveGeneratedWorldSpec(Spec, AiFacingJson, SerializationReport)
		&& LooksLikeUnrealAssetPath(AiFacingJson))
	{
		Report.AddError(
			TEXT("ai.asset_path_forbidden"),
			TEXT("$"),
			TEXT("AI-facing GeneratedWorldSpec contains an Unreal asset path."));
	}

	return Report;
}
