#include "WorldDirectorCompiler.h"
#include "WorldDirectorRuntime.h"

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
	{TEXT("/Game/CapabilityPack/Buildings/BP_Cap_Workplace_Guildhall_01.BP_Cap_Workplace_Guildhall_01_C"), TEXT("/Game/CapabilityPack/Interiors/BP_Interior_Workplace_Longhouse_01.BP_Interior_Workplace_Longhouse_01_C"), FVector2D(1238.0, 1633.0), true, true}
};

const int32 TraversableHomeCapabilityIndices[] = {0, 3};

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
	// Every current non-residential purpose needs the certified public/workplace
	// longhouse. Treating a clinic or shelter as a residence can select a home
	// shell whose door is not traversable at the corresponding public plot.
	return Location.PurposeTag != TEXT("Purpose.Home");
}

const FBuildingCapability* ChooseCapability(
	const FWorldLocation& Location,
	int32 HomeOrdinal,
	bool bLandmark)
{
	if (bLandmark)
	{
		// The vendor guildhall remains available for remediation, but its doorway divides
		// the runtime navmesh. The longhouse is the certified traversable landmark shell.
		return &BuildingCapabilities[5];
	}
	if (IsWorkplace(Location))
	{
		return &BuildingCapabilities[5];
	}
	return &BuildingCapabilities[TraversableHomeCapabilityIndices[HomeOrdinal % UE_ARRAY_COUNT(TraversableHomeCapabilityIndices)]];
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
	int32 HomeOrdinal = static_cast<int32>(StructureSeed % UE_ARRAY_COUNT(TraversableHomeCapabilityIndices));
	for (int32 Index = 0; Index < Spec.Locations.Num(); ++Index)
	{
		const FWorldLocation& Location = Spec.Locations[Index];
		const bool bLandmark = OutPlan.LandmarkLocationId == Location.Id;
		const FBuildingCapability* Capability = ChooseCapability(
			Location, HomeOrdinal, bLandmark);
		HomeOrdinal += IsWorkplace(Location) ? 0 : 1;

		FResolvedLocationPlan& Resolved = OutPlan.Locations.AddDefaulted_GetRef();
		Resolved.LocationId = Location.Id;
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

	TMap<FString, int32> ShellUseCounts;
	int32 MostRepeatedShellCount = 0;
	for (const FResolvedLocationPlan& Location : OutPlan.Locations)
	{
		const int32 Count = ++ShellUseCounts.FindOrAdd(Location.ShellAsset.ToString());
		MostRepeatedShellCount = FMath::Max(MostRepeatedShellCount, Count);
	}
	const int32 RepetitionLimit = FMath::Max(4, (OutPlan.Locations.Num() * 3 + 3) / 4);
	if (MostRepeatedShellCount > RepetitionLimit ||
		(OutPlan.Locations.Num() >= 12 && ShellUseCounts.Num() < 3))
	{
		OutReport.AddWarning(
			TEXT("compiler.asset_overuse"), TEXT("locations"),
			FString::Printf(
				TEXT("Resolved town repeats one shell %d times across %d locations and uses %d distinct shells."),
				MostRepeatedShellCount, OutPlan.Locations.Num(), ShellUseCounts.Num()));
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
		Resolved.Schedule = {
			{0, Resident.HomeLocationId},
			{8, Resident.WorkplaceLocationId},
			{18, OutPlan.LandmarkLocationId},
			{21, Resident.HomeLocationId}
		};
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
