#include "WorldDirectorCompiler.h"
#include "WorldDirectorRuntime.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#include "Engine/World.h"
#include "Math/RandomStream.h"
#include "UObject/SoftObjectPath.h"
#include "WorldDirectorPhysicalGenerator.h"
#include "WorldDirectorTownActors.h"

namespace
{
struct FBuildingCapability
{
	const TCHAR* ShellClass;
	const TCHAR* InteriorClass;
	FVector2D Footprint;
	bool bWorkplace;
	bool bLandmark;
};

struct FCharacterFamily
{
	const TCHAR* Body;
	const TCHAR* Head;
	const TCHAR* Legs;
	const TCHAR* Feet;
};

const FBuildingCapability BuildingCapabilities[] = {
	{TEXT("/Game/CapabilityPack/Buildings/BP_Cap_Home_Compact_01.BP_Cap_Home_Compact_01_C"), TEXT("/Game/CapabilityPack/Interiors/BP_Interior_Home_Small_01.BP_Interior_Home_Small_01_C"), FVector2D(791.0, 864.0), false, false},
	{TEXT("/Game/CapabilityPack/Buildings/BP_Cap_Home_Compact_02.BP_Cap_Home_Compact_02_C"), TEXT("/Game/CapabilityPack/Interiors/BP_Interior_Home_Compact_01.BP_Interior_Home_Compact_01_C"), FVector2D(968.0, 1346.0), false, false},
	{TEXT("/Game/CapabilityPack/Buildings/BP_Cap_Home_Tall_01.BP_Cap_Home_Tall_01_C"), TEXT("/Game/CapabilityPack/Interiors/BP_Interior_Home_Small_01.BP_Interior_Home_Small_01_C"), FVector2D(821.0, 864.0), false, false},
	{TEXT("/Game/CapabilityPack/Buildings/BP_Cap_Home_Multiwing_01.BP_Cap_Home_Multiwing_01_C"), TEXT("/Game/CapabilityPack/Interiors/BP_Interior_Home_Compact_01.BP_Interior_Home_Compact_01_C"), FVector2D(1735.0, 1239.0), false, false},
	{TEXT("/Game/CapabilityPack/Buildings/BP_Cap_Home_Tall_02.BP_Cap_Home_Tall_02_C"), TEXT("/Game/CapabilityPack/Interiors/BP_Interior_Home_Compact_01.BP_Interior_Home_Compact_01_C"), FVector2D(1410.0, 1928.0), false, false},
	{TEXT("/Game/CapabilityPack/Buildings/BP_Cap_Workplace_Longhouse_01.BP_Cap_Workplace_Longhouse_01_C"), TEXT("/Game/CapabilityPack/Interiors/BP_Interior_Workplace_Longhouse_01.BP_Interior_Workplace_Longhouse_01_C"), FVector2D(1330.0, 1783.0), true, false},
	{TEXT("/Game/CapabilityPack/Buildings/BP_Cap_Workplace_Inn_01.BP_Cap_Workplace_Inn_01_C"), TEXT("/Game/CapabilityPack/Interiors/BP_Interior_Home_Small_01.BP_Interior_Home_Small_01_C"), FVector2D(722.0, 912.0), true, false},
	{TEXT("/Game/CapabilityPack/Buildings/BP_Cap_Workplace_Guildhall_01.BP_Cap_Workplace_Guildhall_01_C"), TEXT("/Game/CapabilityPack/Interiors/BP_Interior_Workplace_Longhouse_01.BP_Interior_Workplace_Longhouse_01_C"), FVector2D(1238.0, 1633.0), true, true},
	// Purpose-specific public shells (FVP houses 13, 14, 11, 12). Each has its front
	// door on the local -Y face, where the compiler places the certified entrance.
	{TEXT("/Game/CapabilityPack/Buildings/BP_Cap_Workplace_Watermill_01.BP_Cap_Workplace_Watermill_01_C"), TEXT("/Game/CapabilityPack/Interiors/BP_Interior_Home_Small_01.BP_Interior_Home_Small_01_C"), FVector2D(1084.0, 912.0), true, false},
	{TEXT("/Game/CapabilityPack/Buildings/BP_Cap_Workplace_Windmill_01.BP_Cap_Workplace_Windmill_01_C"), TEXT("/Game/CapabilityPack/Interiors/BP_Interior_Home_Compact_01.BP_Interior_Home_Compact_01_C"), FVector2D(1707.0, 962.0), true, false},
	{TEXT("/Game/CapabilityPack/Buildings/BP_Cap_Workplace_CivicHall_01.BP_Cap_Workplace_CivicHall_01_C"), TEXT("/Game/CapabilityPack/Interiors/BP_Interior_Workplace_Longhouse_01.BP_Interior_Workplace_Longhouse_01_C"), FVector2D(1581.0, 1510.0), true, false},
	{TEXT("/Game/CapabilityPack/Buildings/BP_Cap_Workplace_CivicHall_02.BP_Cap_Workplace_CivicHall_02_C"), TEXT("/Game/CapabilityPack/Interiors/BP_Interior_Workplace_Longhouse_01.BP_Interior_Workplace_Longhouse_01_C"), FVector2D(1301.0, 1303.0), true, false}
};

enum class EBuildingCapabilityIndex : uint8
{
	HomeCompact = 0,
	HomeCompactDeep = 1,
	HomeTall = 2,
	HomeMultiwing = 3,
	HomeTallBroad = 4,
	WorkplaceLonghouse = 5,
	WorkplaceInn = 6,
	WorkplaceGuildhall = 7,
	WorkplaceWatermill = 8,
	WorkplaceWindmill = 9,
	WorkplaceCivicHall = 10,
	WorkplaceCivicHallBroad = 11
};

struct FShellUseCounts
{
	int32 Longhouse = 0;
	int32 Inn = 0;
	int32 Watermill = 0;
	int32 Windmill = 0;
	int32 CivicHall = 0;
};

bool IdentityContains(const FWorldLocation& Location, std::initializer_list<const TCHAR*> Words)
{
	FString Identity = Location.Id + TEXT("|") + Location.DisplayName;
	Identity.ToLowerInline();
	for (const TCHAR* Word : Words)
	{
		if (Identity.Contains(Word))
		{
			return true;
		}
	}
	return false;
}

const int32 CertifiedHomeCapabilityIndices[] = {
	static_cast<int32>(EBuildingCapabilityIndex::HomeCompact),
	static_cast<int32>(EBuildingCapabilityIndex::HomeCompactDeep),
	static_cast<int32>(EBuildingCapabilityIndex::HomeTall),
	static_cast<int32>(EBuildingCapabilityIndex::HomeMultiwing),
	static_cast<int32>(EBuildingCapabilityIndex::HomeTallBroad)
};

const FCharacterFamily MaleCharacterFamilies[] = {
	{TEXT("/Game/Vendor/Quaternius/Characters/Male/Farmer_Body.Farmer_Body"), TEXT("/Game/Vendor/Quaternius/Characters/Male/Parts/Farmer_Head.Farmer_Head"), TEXT("/Game/Vendor/Quaternius/Characters/Male/Parts/Farmer_Legs.Farmer_Legs"), TEXT("/Game/Vendor/Quaternius/Characters/Male/Parts/Farmer_Feet.Farmer_Feet")},
	{TEXT("/Game/Vendor/Quaternius/Characters/Male/Parts/Adventurer_Body.Adventurer_Body"), TEXT("/Game/Vendor/Quaternius/Characters/Male/Parts/Adventurer_Head.Adventurer_Head"), TEXT("/Game/Vendor/Quaternius/Characters/Male/Parts/Adventurer_Legs.Adventurer_Legs"), TEXT("/Game/Vendor/Quaternius/Characters/Male/Parts/Adventurer_Feet.Adventurer_Feet")},
	{TEXT("/Game/Vendor/Quaternius/Characters/Male/Parts/King_Body.King_Body"), TEXT("/Game/Vendor/Quaternius/Characters/Male/Parts/King_Head.King_Head"), TEXT("/Game/Vendor/Quaternius/Characters/Male/Parts/King_Legs.King_Legs"), TEXT("/Game/Vendor/Quaternius/Characters/Male/Parts/King_Feet.King_Feet")},
	{TEXT("/Game/Vendor/Quaternius/Characters/Male/Parts/Worker_Body.Worker_Body"), TEXT("/Game/Vendor/Quaternius/Characters/Male/Parts/Worker_Head.Worker_Head"), TEXT("/Game/Vendor/Quaternius/Characters/Male/Parts/Worker_Legs.Worker_Legs"), TEXT("/Game/Vendor/Quaternius/Characters/Male/Parts/Worker_Feet.Worker_Feet")}
};

const FCharacterFamily FemaleCharacterFamilies[] = {
	{TEXT("/Game/Vendor/Quaternius/Characters/Female/Medieval_Body.Medieval_Body"), TEXT("/Game/Vendor/Quaternius/Characters/Female/Parts/Medieval_Head.Medieval_Head"), TEXT("/Game/Vendor/Quaternius/Characters/Female/Parts/Medieval_Legs.Medieval_Legs"), TEXT("/Game/Vendor/Quaternius/Characters/Female/Parts/Medieval_Feet.Medieval_Feet")},
	{TEXT("/Game/Vendor/Quaternius/Characters/Female/Parts/Adventurer_Body.Adventurer_Body"), TEXT("/Game/Vendor/Quaternius/Characters/Female/Parts/Adventurer_Head.Adventurer_Head"), TEXT("/Game/Vendor/Quaternius/Characters/Female/Parts/Adventurer_Legs.Adventurer_Legs"), TEXT("/Game/Vendor/Quaternius/Characters/Female/Parts/Adventurer_Feet.Adventurer_Feet")},
	{TEXT("/Game/Vendor/Quaternius/Characters/Female/Parts/Witch_Body.Witch_Body"), TEXT("/Game/Vendor/Quaternius/Characters/Female/Parts/Witch_Head.Witch_Head"), TEXT("/Game/Vendor/Quaternius/Characters/Female/Parts/Witch_Legs.Witch_Legs"), TEXT("/Game/Vendor/Quaternius/Characters/Female/Parts/Witch_Feet.Witch_Feet")},
	{TEXT("/Game/Vendor/Quaternius/Characters/Female/Parts/Worker_Body.Worker_Body"), TEXT("/Game/Vendor/Quaternius/Characters/Female/Parts/Worker_Head.Worker_Head"), TEXT("/Game/Vendor/Quaternius/Characters/Female/Parts/Worker_Legs.Worker_Legs"), TEXT("/Game/Vendor/Quaternius/Characters/Female/Parts/Worker_Feet.Worker_Feet")}
};

bool IsWorkplace(const FWorldLocation& Location)
{
	return Location.PurposeTag != TEXT("Purpose.Home");
}

int32 CivicPavingPriority(const FWorldLocation& Location)
{
	if (Location.PurposeTag == TEXT("Purpose.Home"))
	{
		return INDEX_NONE;
	}

	FString SemanticIdentity = Location.Id + TEXT("|") + Location.DisplayName +
		TEXT("|") + Location.PurposeTag.ToString();
	SemanticIdentity.ToLowerInline();
	if (SemanticIdentity.Contains(TEXT("market")) ||
		SemanticIdentity.Contains(TEXT("square")) ||
		SemanticIdentity.Contains(TEXT("plaza")) ||
		SemanticIdentity.Contains(TEXT("council")))
	{
		return 0;
	}
	if (SemanticIdentity.Contains(TEXT("guild")) ||
		SemanticIdentity.Contains(TEXT("inn")) ||
		SemanticIdentity.Contains(TEXT("tavern")) ||
		SemanticIdentity.Contains(TEXT("hall")))
	{
		return 1;
	}
	if (Location.PurposeTag == TEXT("Purpose.Headquarters") ||
		Location.PurposeTag == TEXT("Purpose.Clinic"))
	{
		return 2;
	}
	if (Location.PurposeTag == TEXT("Purpose.Landmark"))
	{
		return 3;
	}
	return INDEX_NONE;
}

bool PrefersInnShell(const FWorldLocation& Location, const int32 WorldSeed, bool& bOutChosenByName)
{
	bOutChosenByName = true;
	if (Location.PurposeTag == TEXT("Purpose.Clinic") ||
		Location.PurposeTag == TEXT("Purpose.Shelter"))
	{
		return true;
	}
	if (Location.PurposeTag == TEXT("Purpose.Headquarters"))
	{
		return false;
	}

	FString SemanticIdentity = Location.Id + TEXT("|") + Location.DisplayName;
	SemanticIdentity.ToLowerInline();
	if (SemanticIdentity.Contains(TEXT("inn")) ||
		SemanticIdentity.Contains(TEXT("market")) ||
		SemanticIdentity.Contains(TEXT("ferry")) ||
		SemanticIdentity.Contains(TEXT("healing")) ||
		SemanticIdentity.Contains(TEXT("pilgrim")) ||
		SemanticIdentity.Contains(TEXT("shelter")))
	{
		return true;
	}
	if (SemanticIdentity.Contains(TEXT("longhouse")) ||
		SemanticIdentity.Contains(TEXT("mill")) ||
		SemanticIdentity.Contains(TEXT("granary")) ||
		SemanticIdentity.Contains(TEXT("watch")) ||
		SemanticIdentity.Contains(TEXT("archive")))
	{
		return false;
	}

	bOutChosenByName = false;
	const int32 LocationSeed = FWorldDirectorPhysicalGenerator::DeriveStageSeed(
		WorldSeed, Location.Id + TEXT("|building-archetype"));
	return (static_cast<uint32>(LocationSeed) & 1U) != 0;
}

const FBuildingCapability* ChooseCapability(
	const FWorldLocation& Location,
	int32 HomeOrdinal,
	bool bLandmark,
	int32 WorldSeed,
	FShellUseCounts& Uses,
	FWorldDirectorExpressionEntry& OutExpression)
{
	int32& LonghouseUseCount = Uses.Longhouse;
	int32& InnUseCount = Uses.Inn;
	OutExpression.Outcome = TEXT("Applied");
	if (bLandmark)
	{
		// Reserve the civic-hall silhouette for the primary landmark. Keeping it out of
		// every other selection pool makes the landmark readable from any approach.
		OutExpression.Detail = TEXT("Guildhall: the town's landmark.");
		return &BuildingCapabilities[static_cast<int32>(
			EBuildingCapabilityIndex::WorkplaceGuildhall)];
	}
	if (IsWorkplace(Location))
	{
		// Buildings whose trade has a matching silhouette get it: a waterwheel mill, a
		// windmill for grain, a large hall for councils and headquarters. Each is capped
		// so the town keeps one recognizable mill rather than a row of them.
		const bool bTradeWorkplace = Location.PurposeTag == TEXT("Purpose.Workplace");
		if (bTradeWorkplace && Uses.Watermill < 1 && IdentityContains(Location,
			{TEXT("mill"), TEXT("waterwheel"), TEXT("weir"), TEXT("sluice"), TEXT("watermill")}) &&
			!IdentityContains(Location, {TEXT("windmill")}))
		{
			++Uses.Watermill;
			OutExpression.Detail = TEXT("Watermill: the name has a mill word.");
			return &BuildingCapabilities[static_cast<int32>(EBuildingCapabilityIndex::WorkplaceWatermill)];
		}
		if (bTradeWorkplace && Uses.Windmill < 1 && IdentityContains(Location,
			{TEXT("windmill"), TEXT("granary"), TEXT("grain"), TEXT("flour"), TEXT("threshing")}))
		{
			++Uses.Windmill;
			OutExpression.Detail = TEXT("Windmill: the name has a grain word.");
			return &BuildingCapabilities[static_cast<int32>(EBuildingCapabilityIndex::WorkplaceWindmill)];
		}
		if (Uses.CivicHall < 2 && (Location.PurposeTag == TEXT("Purpose.Headquarters") ||
			bTradeWorkplace && IdentityContains(Location, {TEXT("council"), TEXT("court"), TEXT("reeve"), TEXT("moot"),
				TEXT("assembly"), TEXT("archive"), TEXT("town hall")})))
		{
			const EBuildingCapabilityIndex Hall = Uses.CivicHall == 0
				? EBuildingCapabilityIndex::WorkplaceCivicHall : EBuildingCapabilityIndex::WorkplaceCivicHallBroad;
			++Uses.CivicHall;
			OutExpression.Detail = Location.PurposeTag == TEXT("Purpose.Headquarters")
				? TEXT("Civic hall: the seat of authority.") : TEXT("Civic hall: the name has a council word.");
			return &BuildingCapabilities[static_cast<int32>(Hall)];
		}
		// Respect the location's semantic role (market/ferry/shelter read as an inn,
		// mill/granary/watch as a longhouse). Strict alternation used to override that
		// for every other building; only rebalance once one shell clearly dominates.
		bool bChosenByName = false;
		bool bUseInn = PrefersInnShell(Location, WorldSeed, bChosenByName);
		const bool bPreferredInn = bUseInn;
		if (LonghouseUseCount >= InnUseCount + 3)
		{
			bUseInn = true;
		}
		else if (InnUseCount >= LonghouseUseCount + 3)
		{
			bUseInn = false;
		}
		const TCHAR* Shell = bUseInn ? TEXT("Inn") : TEXT("Longhouse");
		if (bUseInn != bPreferredInn)
		{
			OutExpression.Outcome = TEXT("Defaulted");
			OutExpression.Detail = FString::Printf(TEXT("%s: swapped to avoid repeating one shell."), Shell);
		}
		else if (!bChosenByName)
		{
			OutExpression.Outcome = TEXT("Defaulted");
			OutExpression.Detail = FString::Printf(TEXT("%s: no building word in the name; seeded choice."), Shell);
		}
		else
		{
			OutExpression.Detail = FString::Printf(TEXT("%s: chosen by purpose or name."), Shell);
		}
		if (bUseInn)
		{
			++InnUseCount;
			return &BuildingCapabilities[static_cast<int32>(
				EBuildingCapabilityIndex::WorkplaceInn)];
		}
		++LonghouseUseCount;
		return &BuildingCapabilities[static_cast<int32>(
			EBuildingCapabilityIndex::WorkplaceLonghouse)];
	}
	OutExpression.Detail = TEXT("Cottage: homes rotate through five cottage shapes.");
	return &BuildingCapabilities[CertifiedHomeCapabilityIndices[
		HomeOrdinal % UE_ARRAY_COUNT(CertifiedHomeCapabilityIndices)]];
}

bool VerifyAsset(const FSoftObjectPath& Path, const FString& PropertyPath, FValidationReport& Report)
{
	if (Path.IsNull() || Path.TryLoad() == nullptr)
	{
		Report.AddError(
			TEXT("compiler.asset_missing"),
			PropertyPath,
			FString::Printf(TEXT("Certified asset could not be loaded: %s"), *Path.ToString()));
		return false;
	}
	return true;
}

int32 PreferredCharacterFamily(const FName OccupationTag, const bool bFemale)
{
	if (OccupationTag == TEXT("Occupation.Guard"))
	{
		return 1;
	}
	if (OccupationTag == TEXT("Occupation.Reeve"))
	{
		return bFemale ? 0 : 2;
	}
	if (OccupationTag == TEXT("Occupation.Herbalist"))
	{
		return bFemale ? 2 : 0;
	}
	if (OccupationTag == TEXT("Occupation.Worker"))
	{
		return 3;
	}
	return 0;
}
}

TArray<TSharedPtr<FJsonValue>> FWorldDirectorCompiler::BuildBuildingManifest()
{
	// Mirrors ChooseCapability so the model knows which names produce which building.
	const TCHAR* Rows[][2] = {
		{TEXT("Guildhall"), TEXT("the landmark (first landmarkLocationIds entry)")},
		{TEXT("Watermill"), TEXT("one Purpose.Workplace named with mill, waterwheel, weir or sluice")},
		{TEXT("Windmill"), TEXT("one Purpose.Workplace named with windmill, granary, grain, flour or threshing")},
		{TEXT("Civic hall"), TEXT("up to two: Purpose.Headquarters, or a Purpose.Workplace named with council, court, reeve, moot, assembly, archive or town hall")},
		{TEXT("Inn"), TEXT("Purpose.Clinic, Purpose.Shelter, or names with inn, market, ferry, healing, pilgrim or shelter")},
		{TEXT("Longhouse"), TEXT("names with longhouse, granary, watch or archive; other workplaces alternate inn and longhouse")},
		{TEXT("Cottage"), TEXT("Purpose.Home; five cottage shapes in rotation")},
	};
	TArray<TSharedPtr<FJsonValue>> Manifest;
	for (const auto& Row : Rows)
	{
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("building"), Row[0]);
		Entry->SetStringField(TEXT("when"), Row[1]);
		Manifest.Add(MakeShared<FJsonValueObject>(Entry));
	}
	return Manifest;
}

bool FWorldDirectorCompiler::Resolve(
	const FGeneratedWorldSpec& Spec,
	FResolvedWorldPlan& OutPlan,
	FValidationReport& OutReport,
	const bool bLoadAssets)
{
	OutPlan = FResolvedWorldPlan();
	OutReport = FValidationReport();
	OutPlan.Id = FString::Printf(TEXT("resolved.%s"), *Spec.Id);
	OutPlan.SourceSpecId = Spec.Id;
	OutPlan.Seed = Spec.Seed;
	if (!Spec.Topology.LandmarkLocationIds.IsEmpty())
	{
		OutPlan.LandmarkLocationId = Spec.Topology.LandmarkLocationIds[0];
	}

	if (Spec.Locations.Num() < 6 || Spec.Locations.Num() > 18)
	{
		OutReport.AddError(
			TEXT("compiler.location_count"), TEXT("locations"),
			TEXT("The compiler supports six to eighteen locations; full-slice generation requires twelve to eighteen."));
		return false;
	}
	int32 HomeLocationCount = 0;
	for (const FWorldLocation& Location : Spec.Locations)
	{
		HomeLocationCount += Location.PurposeTag == TEXT("Purpose.Home");
	}
	if (HomeLocationCount > 6)
	{
		OutReport.AddError(
			TEXT("compiler.home_plot_count"), TEXT("locations"),
			TEXT("The certified physical layout supports at most six dedicated home plots."));
		return false;
	}

	const uint32 StructureSeed = static_cast<uint32>(FWorldDirectorPhysicalGenerator::DeriveStageSeed(
		Spec.Seed, TEXT("structures")));
	// Stone courts are semantic focal points, not a blanket treatment for every
	// non-home shell. Keep the primary landmark and at most two ranked public
	// satellites so grass, yards, and working ground remain the visual default.
	TArray<int32> SecondaryPavingCandidates;
	for (int32 Index = 0; Index < Spec.Locations.Num(); ++Index)
	{
		if (Spec.Locations[Index].Id != OutPlan.LandmarkLocationId &&
			CivicPavingPriority(Spec.Locations[Index]) != INDEX_NONE)
		{
			SecondaryPavingCandidates.Add(Index);
		}
	}
	SecondaryPavingCandidates.Sort([&](const int32 A, const int32 B)
	{
		const int32 PriorityA = CivicPavingPriority(Spec.Locations[A]);
		const int32 PriorityB = CivicPavingPriority(Spec.Locations[B]);
		if (PriorityA != PriorityB)
		{
			return PriorityA < PriorityB;
		}
		const uint32 SeedA = static_cast<uint32>(FWorldDirectorPhysicalGenerator::DeriveStageSeed(
			Spec.Seed, Spec.Locations[A].Id + TEXT("|civic-paving")));
		const uint32 SeedB = static_cast<uint32>(FWorldDirectorPhysicalGenerator::DeriveStageSeed(
			Spec.Seed, Spec.Locations[B].Id + TEXT("|civic-paving")));
		return SeedA != SeedB ? SeedA < SeedB : Spec.Locations[A].Id < Spec.Locations[B].Id;
	});
	TSet<FString> PavedLocationIds;
	TArray<FString> OrderedPavedLocationIds;
	if (!OutPlan.LandmarkLocationId.IsEmpty())
	{
		PavedLocationIds.Add(OutPlan.LandmarkLocationId);
		OrderedPavedLocationIds.Add(OutPlan.LandmarkLocationId);
	}
	const int32 SecondaryPavingBudget = Spec.Locations.Num() >= 12 ? 2 : 1;
	for (int32 CandidateIndex = 0;
		CandidateIndex < SecondaryPavingCandidates.Num() &&
		CandidateIndex < SecondaryPavingBudget;
		++CandidateIndex)
	{
		const FString& LocationId = Spec.Locations[SecondaryPavingCandidates[CandidateIndex]].Id;
		PavedLocationIds.Add(LocationId);
		OrderedPavedLocationIds.Add(LocationId);
	}
	UE_LOG(LogWorldDirector, Display,
		TEXT("WORLD_DIRECTOR_CIVIC_SURFACE_SELECTION secondaryBudget=%d count=%d locations=%s"),
		SecondaryPavingBudget, OrderedPavedLocationIds.Num(),
		*FString::Join(OrderedPavedLocationIds, TEXT(",")));
	int32 HomeOrdinal = static_cast<int32>(StructureSeed % UE_ARRAY_COUNT(CertifiedHomeCapabilityIndices));
	FShellUseCounts ShellUses;
	int32 HomeShellEligibleCount = 0;
	int32 PublicShellEligibleCount = 0;
	int32 LandmarkShellEligibleCount = 0;
	for (int32 Index = 0; Index < Spec.Locations.Num(); ++Index)
	{
		const FWorldLocation& Location = Spec.Locations[Index];
		const bool bLandmark = OutPlan.LandmarkLocationId == Location.Id;
		const bool bHome = !IsWorkplace(Location);
		FWorldDirectorExpressionEntry Expression;
		Expression.Field = FString::Printf(TEXT("locations[%d] %s"), Index, *Location.DisplayName);
		Expression.Value = Location.PurposeTag.ToString();
		const FBuildingCapability* Capability = ChooseCapability(
			Location, HomeOrdinal, bLandmark, Spec.Seed, ShellUses, Expression);
		OutPlan.Expression.Add(Expression);
		HomeOrdinal += !bLandmark && bHome ? 1 : 0;
		LandmarkShellEligibleCount += bLandmark ? 1 : 0;
		HomeShellEligibleCount += !bLandmark && bHome ? 1 : 0;
		PublicShellEligibleCount += !bLandmark && !bHome ? 1 : 0;

		FResolvedLocationPlan& Resolved = OutPlan.Locations.AddDefaulted_GetRef();
		Resolved.LocationId = Location.Id;
		Resolved.PurposeTag = Location.PurposeTag;
		Resolved.bPavedCourtyard = PavedLocationIds.Contains(Location.Id);
		Resolved.ShellAsset = FSoftObjectPath(Capability->ShellClass);
		Resolved.InteriorAsset = FSoftObjectPath(Capability->InteriorClass);
		Resolved.FootprintSize = Capability->Footprint;
		Resolved.bRepurposable = Location.bRepurposable;

		if (bLoadAssets)
		{
			VerifyAsset(Resolved.ShellAsset, FString::Printf(TEXT("locations[%d].shellAsset"), Index), OutReport);
			VerifyAsset(Resolved.InteriorAsset, FString::Printf(TEXT("locations[%d].interiorAsset"), Index), OutReport);
		}
	}
	if (!FWorldDirectorPhysicalGenerator::Generate(Spec, OutPlan, OutReport))
	{
		for (const FValidationIssue& Issue : OutReport.Issues)
		{
			UE_LOG(LogWorldDirector, Error, TEXT("WORLD_DIRECTOR_PHYSICAL_RESOLVE_FAIL code=%s path=%s message=%s"),
				*Issue.Code.ToString(), *Issue.Path, *Issue.Message);
		}
		return false;
	}

	// Prose fields reach players only as text. Listing them keeps the expression report
	// honest about how much of the model's writing changes what gets built.
	auto AddTextOnly = [&OutPlan](const TCHAR* Field, const FString& Value)
	{
		FWorldDirectorExpressionEntry& Entry = OutPlan.Expression.AddDefaulted_GetRef();
		Entry.Field = Field;
		Entry.Value = Value.Left(120);
		Entry.Outcome = TEXT("TextOnly");
		Entry.Detail = TEXT("Shown as text (dialogue, motives or the environment summary); changes nothing that is built.");
	};
	AddTextOnly(TEXT("brief.theme"), Spec.Brief.Theme);
	AddTextOnly(TEXT("topology.governance"), Spec.Topology.Governance);
	AddTextOnly(TEXT("topology.historicalWound"), Spec.Topology.HistoricalWound);
	AddTextOnly(TEXT("topology.currentTension"), Spec.Topology.CurrentTension);
	AddTextOnly(TEXT("topology.supernaturalPremise"), Spec.Topology.SupernaturalPremise);
	AddTextOnly(TEXT("topology.centralThreat"), Spec.Topology.CentralThreat);

	TMap<FString, int32> ShellUseCounts;
	int32 MostRepeatedShellCount = 0;
	for (const FResolvedLocationPlan& Location : OutPlan.Locations)
	{
		const int32 Count = ++ShellUseCounts.FindOrAdd(Location.ShellAsset.ToString());
		MostRepeatedShellCount = FMath::Max(MostRepeatedShellCount, Count);
	}
	const int32 RepetitionLimit = FMath::Max(3, (OutPlan.Locations.Num() + 2) / 3);
	const int32 ExpectedDistinctShellCount =
		FMath::Min(HomeShellEligibleCount,
			static_cast<int32>(UE_ARRAY_COUNT(CertifiedHomeCapabilityIndices))) +
		FMath::Min(PublicShellEligibleCount, 2 + FMath::Min(ShellUses.Watermill + ShellUses.Windmill + ShellUses.CivicHall, 4)) +
		FMath::Min(LandmarkShellEligibleCount, 1);
	if (MostRepeatedShellCount > RepetitionLimit ||
		ShellUseCounts.Num() < ExpectedDistinctShellCount)
	{
		OutReport.AddWarning(
			TEXT("compiler.asset_overuse"), TEXT("locations"),
			FString::Printf(
				TEXT("Resolved town repeats one shell %d times across %d locations (limit %d) and uses %d distinct shells (expected %d for the supplied purposes)."),
				MostRepeatedShellCount, OutPlan.Locations.Num(), RepetitionLimit,
				ShellUseCounts.Num(), ExpectedDistinctShellCount));
	}

	const FResolvedLocationPlan* LandmarkPlan = OutPlan.Locations.FindByPredicate(
		[&OutPlan](const FResolvedLocationPlan& Location)
		{
			return Location.LocationId == OutPlan.LandmarkLocationId;
		});
	if (LandmarkPlan == nullptr)
	{
		OutReport.AddError(
			TEXT("compiler.landmark_missing"), TEXT("topology.landmarkLocationIds[0]"),
			TEXT("A resolved landmark is required for navigation viability."));
	}

	TMap<FString, int32> ResidentSlotsByHome;
	TSet<FString> UsedAppearanceSignatures;
	for (int32 Index = 0; Index < Spec.Residents.Num(); ++Index)
	{
		const FResident& Resident = Spec.Residents[Index];
		const FResolvedLocationPlan* Home = OutPlan.Locations.FindByPredicate(
			[&Resident](const FResolvedLocationPlan& Location)
			{
				return Location.LocationId == Resident.HomeLocationId;
			});
		if (Home == nullptr)
		{
			continue;
		}

		FResolvedResidentPlan& Resolved = OutPlan.Residents.AddDefaulted_GetRef();
		Resolved.ResidentId = Resident.Id;
		Resolved.HomeLocationId = Resident.HomeLocationId;
		Resolved.WorkplaceLocationId = Resident.WorkplaceLocationId;
		const bool bFemale = Index % 2 != 0;
		const FCharacterFamily* Families = bFemale ? FemaleCharacterFamilies : MaleCharacterFamilies;
		const int32 FamilyCount = bFemale
			? UE_ARRAY_COUNT(FemaleCharacterFamilies) : UE_ARRAY_COUNT(MaleCharacterFamilies);
		const int32 PreferredFamily = PreferredCharacterFamily(Resident.OccupationTag, bFemale);
		const uint32 ResidentHash = static_cast<uint32>(FWorldDirectorPhysicalGenerator::DeriveStageSeed(
			Spec.Seed, Resident.Id + TEXT("|") + Resident.OccupationTag.ToString()));
		const int32 StartAppearance =
			(PreferredFamily + FamilyCount * static_cast<int32>(ResidentHash % FamilyCount)) %
			(FamilyCount * FamilyCount);
		int32 Appearance = StartAppearance;
		for (int32 Attempt = 0; Attempt < FamilyCount * FamilyCount; ++Attempt)
		{
			const int32 Candidate = (StartAppearance + Attempt) % (FamilyCount * FamilyCount);
			const FString Signature = FString::Printf(
				TEXT("%c:%d"), bFemale ? TEXT('F') : TEXT('M'), Candidate);
			if (!UsedAppearanceSignatures.Contains(Signature))
			{
				Appearance = Candidate;
				UsedAppearanceSignatures.Add(Signature);
				break;
			}
		}
		const int32 BodyFamilyIndex = Appearance % FamilyCount;
		const int32 HeadFamilyIndex = Appearance / FamilyCount;
		Resolved.SkeletalMeshAsset = FSoftObjectPath(Families[BodyFamilyIndex].Body);
		Resolved.ModularPartAssets = {
			FSoftObjectPath(Families[HeadFamilyIndex].Head),
			FSoftObjectPath(Families[BodyFamilyIndex].Legs),
			FSoftObjectPath(Families[BodyFamilyIndex].Feet)
		};
		Resolved.IdleAnimationAsset = FSoftObjectPath(bFemale
			? TEXT("/Game/Vendor/Quaternius/Characters/Female/Animations/AnimationsIdle_Neutral.AnimationsIdle_Neutral")
			: TEXT("/Game/Vendor/Quaternius/Characters/Male/Animations/AnimationsCharacterArmature_Idle_Neutral.AnimationsCharacterArmature_Idle_Neutral"));
		FTransform SpawnTransform = Home->EntranceTransform;
		const int32 HouseholdSlot = ResidentSlotsByHome.FindOrAdd(Resident.HomeLocationId)++;
		const int32 SpawnColumn = HouseholdSlot % 6;
		const int32 SpawnRow = HouseholdSlot / 6;
		const FVector LocalSpawnOffset(
			(SpawnColumn - 2.5f) * 120.0f,
			-250.0f - SpawnRow * 140.0f,
			100.0f);
		SpawnTransform.AddToTranslation(
			Home->Transform.GetRotation().RotateVector(LocalSpawnOffset));
		Resolved.SpawnTransform = SpawnTransform;
		Resolved.Schedule = BuildResidentSchedule(Resident, OutPlan.LandmarkLocationId, Spec.Seed);
		if (bLoadAssets)
		{
			VerifyAsset(
				Resolved.SkeletalMeshAsset,
				FString::Printf(TEXT("residents[%d].skeletalMeshAsset"), Index),
				OutReport);
			for (int32 PartIndex = 0; PartIndex < Resolved.ModularPartAssets.Num(); ++PartIndex)
			{
				VerifyAsset(
					Resolved.ModularPartAssets[PartIndex],
					FString::Printf(TEXT("residents[%d].modularPartAssets[%d]"), Index, PartIndex),
					OutReport);
			}
			VerifyAsset(
				Resolved.IdleAnimationAsset,
				FString::Printf(TEXT("residents[%d].idleAnimationAsset"), Index),
				OutReport);
		}
	}

	OutReport.bValid = !OutReport.Issues.ContainsByPredicate(
		[](const FValidationIssue& Issue)
		{
			return Issue.Severity == EWorldDirectorValidationSeverity::Error;
		});
	return OutReport.bValid;
}

TArray<FWorldDirectorScheduleStop> FWorldDirectorCompiler::BuildResidentSchedule(
	const FResident& Resident,
	const FString& LandmarkLocationId,
	const int32 WorldSeed)
{
	const FString& Home = Resident.HomeLocationId;
	const FString& Work = Resident.WorkplaceLocationId.IsEmpty() ? Home : Resident.WorkplaceLocationId;
	const uint32 Hash = static_cast<uint32>(FWorldDirectorPhysicalGenerator::DeriveStageSeed(
		WorldSeed, Resident.Id + TEXT("|schedule")));
	const FName Occupation = Resident.OccupationTag;

	if (Occupation == TEXT("Occupation.Guard") && (Hash & 1U) != 0U)
	{
		// Night watch: sleeps through the day, stands watch overnight.
		return {{0, Work}, {7, Home}, {19, Work}};
	}

	int32 WorkStart = 7;
	int32 WorkEnd = 17;
	if (Occupation == TEXT("Occupation.Farmer"))
	{
		WorkStart = 6;
		WorkEnd = 16;
	}
	else if (Occupation == TEXT("Occupation.Merchant"))
	{
		WorkStart = 9;
		WorkEnd = 18;
	}
	else if (Occupation == TEXT("Occupation.Guard"))
	{
		WorkStart = 7;
		WorkEnd = 19;
	}
	else if (Occupation == TEXT("Occupation.Herbalist"))
	{
		WorkStart = 8;
		WorkEnd = 17;
	}
	else if (Occupation == TEXT("Occupation.Reeve"))
	{
		WorkStart = 8;
		WorkEnd = 16;
	}
	const int32 Bedtime = 21 + static_cast<int32>((Hash >> 3) % 2U);
	// Reeves always hold the evening at the landmark; about two in three others join.
	const bool bEveningAtLandmark = !LandmarkLocationId.IsEmpty() &&
		(Occupation == TEXT("Occupation.Reeve") || (Hash >> 1) % 3U != 0U);
	TArray<FWorldDirectorScheduleStop> Schedule = {{0, Home}, {WorkStart, Work}};
	if (WorkEnd < Bedtime)
	{
		Schedule.Add({WorkEnd, bEveningAtLandmark ? LandmarkLocationId : Home});
	}
	Schedule.Add({Bedtime, Home});
	return Schedule;
}

bool FWorldDirectorCompiler::Spawn(
	UWorld* World,
	const FResolvedWorldPlan& Plan,
	AWorldDirectorTownActor*& OutTown,
	FValidationReport& OutReport)
{
	OutTown = nullptr;
	OutReport = FValidationReport();
	if (World == nullptr)
	{
		OutReport.AddError(TEXT("compiler.world_missing"), TEXT("world"), TEXT("No target UWorld was supplied."));
		return false;
	}
	AWorldDirectorTownActor* SpawnedTown = World->SpawnActor<AWorldDirectorTownActor>(
		AWorldDirectorTownActor::StaticClass(), FTransform::Identity);
	if (SpawnedTown == nullptr)
	{
		OutReport.AddError(TEXT("compiler.spawn_failed"), TEXT("town"), TEXT("Could not spawn the town root actor."));
		return false;
	}
	if (!SpawnedTown->BuildFromPlan(Plan, OutReport))
	{
		SpawnedTown->DestroyCompiledContent();
		SpawnedTown->Destroy();
		return false;
	}
	OutTown = SpawnedTown;
	return true;
}
