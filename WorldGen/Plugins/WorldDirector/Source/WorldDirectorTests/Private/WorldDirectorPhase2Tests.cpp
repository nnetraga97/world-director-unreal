#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "WorldDirectorJson.h"
#include "WorldDirectorCompiler.h"
#include "WorldDirectorTownActors.h"
#include "WorldDirectorValidation.h"
#include "WorldEnvironmentProfile.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "ProceduralMeshComponent.h"

namespace
{
bool LoadFixture(const TCHAR* Filename, FString& OutJson)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("WorldDirector"));
	if (!Plugin.IsValid())
	{
		return false;
	}
	return FFileHelper::LoadFileToString(
		OutJson,
		*(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources/Fixtures"), Filename)));
}

TSet<FName> MakeFixtureCapabilityTags()
{
	return {
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
}

bool HasIssueCode(const FValidationReport& Report, const FName Code)
{
	return Report.Issues.ContainsByPredicate(
		[Code](const FValidationIssue& Issue) { return Issue.Code == Code; });
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWorldDirectorValidFixtureRoundTripTest,
	"WorldDirector.Phase2.ValidFixtureRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldDirectorValidFixtureRoundTripTest::RunTest(const FString& Parameters)
{
	FString SourceJson;
	if (!TestTrue(TEXT("Hand-authored valid fixture exists"), LoadFixture(TEXT("valid-world.json"), SourceJson)))
	{
		return false;
	}

	FGeneratedWorldSpec FirstSpec;
	FValidationReport ParseReport;
	if (!TestTrue(
		TEXT("Valid fixture loads in strict JSON mode"),
		FWorldDirectorJson::LoadGeneratedWorldSpec(SourceJson, FirstSpec, ParseReport)))
	{
		return false;
	}

	const FValidationReport ValidationReport =
		FWorldDirectorValidator::Validate(FirstSpec, MakeFixtureCapabilityTags());
	TestTrue(TEXT("Valid fixture satisfies all invariants"), ValidationReport.bValid);
	TestEqual(TEXT("Valid fixture has no issues"), ValidationReport.Issues.Num(), 0);
	TestEqual(TEXT("Stable world ID survives load"), FirstSpec.Id, FString(TEXT("world.brackenford")));
	TestEqual(TEXT("Resident count survives load"), FirstSpec.Residents.Num(), 2);
	TestEqual(TEXT("Location count survives load"), FirstSpec.Locations.Num(), 2);

	FString FirstSerializedJson;
	FValidationReport SerializationReport;
	if (!TestTrue(
		TEXT("Validated fixture serializes"),
		FWorldDirectorJson::SaveGeneratedWorldSpec(
			FirstSpec,
			FirstSerializedJson,
			SerializationReport)))
	{
		return false;
	}

	FGeneratedWorldSpec SecondSpec;
	FValidationReport SecondParseReport;
	if (!TestTrue(
		TEXT("Serialized fixture loads again"),
		FWorldDirectorJson::LoadGeneratedWorldSpec(
			FirstSerializedJson,
			SecondSpec,
			SecondParseReport)))
	{
		return false;
	}

	FString SecondSerializedJson;
	FValidationReport SecondSerializationReport;
	TestTrue(
		TEXT("Second-generation fixture serializes"),
		FWorldDirectorJson::SaveGeneratedWorldSpec(
			SecondSpec,
			SecondSerializedJson,
			SecondSerializationReport));
	TestEqual(
		TEXT("Known schema data round-trips without loss"),
		SecondSerializedJson,
		FirstSerializedJson);

	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWorldDirectorUnknownJsonFieldTest,
	"WorldDirector.Phase2.UnknownJsonFieldRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldDirectorUnknownJsonFieldTest::RunTest(const FString& Parameters)
{
	FString SourceJson;
	if (!LoadFixture(TEXT("valid-world.json"), SourceJson))
	{
		AddError(TEXT("Could not load valid fixture."));
		return false;
	}
	const FString UnknownRootJson = SourceJson.Replace(
		TEXT("\"version\": 1,"), TEXT("\"version\": 1, \"unexpectedRoot\": true,"),
		ESearchCase::CaseSensitive);
	FGeneratedWorldSpec Spec;
	FValidationReport Report;
	TestFalse(TEXT("Unknown JSON fields are rejected instead of silently dropped"),
		FWorldDirectorJson::LoadGeneratedWorldSpec(UnknownRootJson, Spec, Report));
	TestTrue(TEXT("Unknown JSON field reports a precise code"),
		HasIssueCode(Report, TEXT("json.unknown_field")));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWorldDirectorInvalidFixtureTest,
	"WorldDirector.Phase2.InvalidFixtureRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldDirectorInvalidFixtureTest::RunTest(const FString& Parameters)
{
	FString SourceJson;
	if (!TestTrue(TEXT("Hand-authored invalid fixture exists"), LoadFixture(TEXT("invalid-world.json"), SourceJson)))
	{
		return false;
	}

	FGeneratedWorldSpec Spec;
	FValidationReport ParseReport;
	if (!TestTrue(
		TEXT("Invalid semantic fixture still has a readable JSON shape"),
		FWorldDirectorJson::LoadGeneratedWorldSpec(SourceJson, Spec, ParseReport)))
	{
		return false;
	}

	const FValidationReport Report =
		FWorldDirectorValidator::Validate(Spec, MakeFixtureCapabilityTags());
	TestFalse(TEXT("Invalid fixture is rejected"), Report.bValid);
	TestTrue(TEXT("Missing references are useful errors"), HasIssueCode(Report, TEXT("reference.missing")));
	TestTrue(TEXT("Capacity failure is reported"), HasIssueCode(Report, TEXT("household.capacity_exceeded")));
	TestTrue(TEXT("Household inconsistency is reported"), HasIssueCode(Report, TEXT("household.membership_inconsistent")));
	TestTrue(TEXT("Unknown mechanical tags are rejected"), HasIssueCode(Report, TEXT("capability.tag_missing")));
	TestTrue(TEXT("Unestablished secret fact is rejected"), HasIssueCode(Report, TEXT("secret.fact_not_established")));
	TestTrue(TEXT("Retroactive fact invention is rejected"), HasIssueCode(Report, TEXT("fact.retroactive_invention")));
	TestTrue(TEXT("AI asset paths are rejected"), HasIssueCode(Report, TEXT("ai.asset_path_forbidden")));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWorldDirectorRelationshipInvariantTest,
	"WorldDirector.Phase2.RelationshipConsistency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldDirectorRelationshipInvariantTest::RunTest(const FString& Parameters)
{
	FString SourceJson;
	if (!LoadFixture(TEXT("valid-world.json"), SourceJson))
	{
		AddError(TEXT("Could not load valid fixture."));
		return false;
	}
	FGeneratedWorldSpec Spec;
	FValidationReport ParseReport;
	if (!FWorldDirectorJson::LoadGeneratedWorldSpec(SourceJson, Spec, ParseReport))
	{
		AddError(TEXT("Could not parse valid fixture."));
		return false;
	}
	Spec.Relationships[0].ReciprocalRelationshipId = TEXT("relationship.missing");
	const FValidationReport Report =
		FWorldDirectorValidator::Validate(Spec, MakeFixtureCapabilityTags());
	TestTrue(
		TEXT("Broken reciprocal relationship is rejected"),
		HasIssueCode(Report, TEXT("relationship.reciprocal_inconsistent")));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWorldDirectorOrphanedSecretTest,
	"WorldDirector.Phase8.OrphanedSecretRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldDirectorOrphanedSecretTest::RunTest(const FString& Parameters)
{
	FString SourceJson;
	FGeneratedWorldSpec Spec;
	FValidationReport ParseReport;
	if (!LoadFixture(TEXT("valid-world.json"), SourceJson) ||
		!FWorldDirectorJson::LoadGeneratedWorldSpec(SourceJson, Spec, ParseReport))
	{
		AddError(TEXT("Could not load valid fixture."));
		return false;
	}
	FWorldFact& Secret = Spec.Facts.AddDefaulted_GetRef();
	Secret.Id = TEXT("fact.unheld_secret");
	Secret.Statement = TEXT("No resident has been assigned this closed-world secret.");
	Secret.bEstablished = true;
	Secret.bSecret = true;
	const FValidationReport Report =
		FWorldDirectorValidator::Validate(Spec, MakeFixtureCapabilityTags());
	TestTrue(TEXT("Orphaned secret facts are rejected"),
		HasIssueCode(Report, TEXT("secret.orphaned")));
	return !HasAnyErrors();
}

#endif

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWorldDirectorLivingTownFixtureTest,
	"WorldDirector.Phase4.LivingTownFixture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldDirectorLivingTownFixtureTest::RunTest(const FString& Parameters)
{
	FString SourceJson;
	if (!TestTrue(TEXT("Living-town fixture exists"), LoadFixture(TEXT("living-town.json"), SourceJson)))
	{
		return false;
	}
	FGeneratedWorldSpec Spec;
	FValidationReport ParseReport;
	if (!TestTrue(TEXT("Living-town fixture parses"),
		FWorldDirectorJson::LoadGeneratedWorldSpec(SourceJson, Spec, ParseReport)))
	{
		return false;
	}
	const FValidationReport SemanticReport =
		FWorldDirectorValidator::Validate(Spec, MakeFixtureCapabilityTags());
	TestTrue(TEXT("Living-town fixture passes semantic validation"), SemanticReport.bValid);
	TestEqual(TEXT("Full slice uses all 18 certified locations"), Spec.Locations.Num(), 18);
	TestEqual(TEXT("Population is exactly 24 residents"), Spec.Residents.Num(), 24);
	TestEqual(TEXT("Six households are defined"), Spec.Households.Num(), 6);
	TestEqual(TEXT("Social graph has 46 reciprocal directed relationships"), Spec.Relationships.Num(), 46);
	TestEqual(TEXT("One initial central threat is defined"), Spec.Threats.Num(), 1);
	TestEqual(TEXT("One director project proposal is defined"), Spec.ChangeProjects.Num(), 1);
	if (!Spec.ChangeProjects.IsEmpty())
	{
		const FChangeProject& Project = Spec.ChangeProjects[0];
		TestEqual(TEXT("Project begins proposed"), Project.State,
			EWorldDirectorProjectState::Proposed);
		TestTrue(TEXT("Project names required conditions"),
			Project.RequiredConditionTags.Contains(TEXT("Condition.Overnight")) &&
			Project.RequiredConditionTags.Contains(TEXT("Condition.PlayerAway")));
		TestTrue(TEXT("Project has a bounded transition time"),
			Project.RequiredTransitionMinutes >= 60 && Project.RequiredTransitionMinutes <= 1440);
	}
	TMap<FString, TSet<FString>> SocialAdjacency;
	for (const FRelationship& Relationship : Spec.Relationships)
	{
		SocialAdjacency.FindOrAdd(Relationship.SourceResidentId).Add(Relationship.TargetResidentId);
		SocialAdjacency.FindOrAdd(Relationship.TargetResidentId).Add(Relationship.SourceResidentId);
	}
	TSet<FString> ConnectedResidents;
	TArray<FString> PendingResidents = {Spec.Residents[0].Id};
	while (!PendingResidents.IsEmpty())
	{
		const FString ResidentId = PendingResidents.Pop(EAllowShrinking::No);
		if (ConnectedResidents.Contains(ResidentId))
		{
			continue;
		}
		ConnectedResidents.Add(ResidentId);
		if (const TSet<FString>* Neighbors = SocialAdjacency.Find(ResidentId))
		{
			for (const FString& Neighbor : *Neighbors)
			{
				if (!ConnectedResidents.Contains(Neighbor))
				{
					PendingResidents.Add(Neighbor);
				}
			}
		}
	}
	TestEqual(TEXT("All residents belong to one connected social graph"),
		ConnectedResidents.Num(), Spec.Residents.Num());
	TestEqual(TEXT("Generation-time fact set is non-empty and closed"), Spec.Facts.Num(), 6);
	TestTrue(TEXT("Settlement identity is explicit"), !Spec.Brief.SettlementIdentity.IsEmpty());
	TestTrue(TEXT("Terrain preferences are explicit"), !Spec.Brief.TerrainPreferences.IsEmpty());
	TestTrue(TEXT("Districts are explicit"), !Spec.Topology.Districts.IsEmpty());
	TestTrue(TEXT("Governance is explicit"), !Spec.Topology.Governance.IsEmpty());
	TestTrue(TEXT("Historical wound is explicit"), !Spec.Topology.HistoricalWound.IsEmpty());
	TestTrue(TEXT("Current tension is explicit"), !Spec.Topology.CurrentTension.IsEmpty());
	TestTrue(TEXT("Supernatural premise is explicit"), !Spec.Topology.SupernaturalPremise.IsEmpty());
	TestTrue(TEXT("Central threat is explicit"), !Spec.Topology.CentralThreat.IsEmpty());
	for (int32 Index = 0; Index < Spec.Residents.Num(); ++Index)
	{
		const FResident& Resident = Spec.Residents[Index];
		TestTrue(*FString::Printf(TEXT("Resident %d has a motivation"), Index), !Resident.Motivation.IsEmpty());
		TestTrue(*FString::Printf(TEXT("Resident %d has a fear"), Index), !Resident.Fear.IsEmpty());
		TestTrue(*FString::Printf(TEXT("Resident %d has an important memory"), Index), !Resident.ImportantMemories.IsEmpty());
		TestTrue(*FString::Printf(TEXT("Resident %d has structured beliefs"), Index), !Resident.BeliefIds.IsEmpty());
		TestTrue(*FString::Printf(TEXT("Resident %d has relationships"), Index), !Resident.RelationshipIds.IsEmpty());
	}
	FResolvedWorldPlan Plan;
	FValidationReport ResolveReport;
	if (!TestTrue(TEXT("Living town resolves through the certified compiler"),
		FWorldDirectorCompiler::Resolve(Spec, Plan, ResolveReport, false)))
	{
		return false;
	}
	TestEqual(TEXT("All 24 residents resolve"), Plan.Residents.Num(), 24);
	TestEqual(TEXT("All 18 locations resolve"), Plan.Locations.Num(), 18);
	TSet<FString> ResidentAppearances;
	for (const FResolvedResidentPlan& Resident : Plan.Residents)
	{
		TArray<FString> AppearanceParts = {Resident.SkeletalMeshAsset.ToString()};
		for (const FSoftObjectPath& Part : Resident.ModularPartAssets)
		{
			AppearanceParts.Add(Part.ToString());
		}
		ResidentAppearances.Add(FString::Join(AppearanceParts, TEXT("|")));
	}
	TestEqual(TEXT("All 24 fixture residents resolve to visually distinct modular combinations"),
		ResidentAppearances.Num(), Plan.Residents.Num());
	int32 MultiwingHomeCount = 0;
	TSet<FString> ShellSubstyles;
	for (const FResolvedLocationPlan& Location : Plan.Locations)
	{
		ShellSubstyles.Add(Location.ShellAsset.ToString());
	}
	TestTrue(TEXT("Town uses at least three compatible architectural substyles"),
		ShellSubstyles.Num() >= 3);
	for (int32 Index = 0; Index < 6; ++Index)
	{
		const FString Shell = Plan.Locations[Index].ShellAsset.ToString();
		TestTrue(*FString::Printf(TEXT("Home %d uses a runtime-qualified shell"), Index),
			Shell.Contains(TEXT("Home_Compact_01")) || Shell.Contains(TEXT("Home_Multiwing_01")));
		MultiwingHomeCount += Shell.Contains(TEXT("Home_Multiwing_01")) ? 1 : 0;
	}
	TestEqual(TEXT("Three larger homes are used only in certified map slots"), MultiwingHomeCount, 3);
	Spec.Locations[6].PurposeTag = TEXT("Purpose.Clinic");
	FResolvedWorldPlan ClinicPlan;
	FValidationReport ClinicReport;
	TestTrue(TEXT("A supported public conversion purpose resolves"),
		FWorldDirectorCompiler::Resolve(Spec, ClinicPlan, ClinicReport, false));
	TestTrue(TEXT("Public clinic uses the runtime-qualified longhouse shell"),
		ClinicPlan.Locations[6].ShellAsset.ToString().Contains(TEXT("Workplace_Longhouse_01")));
	FGeneratedWorldSpec RepetitiveSpec = Spec;
	for (FWorldLocation& Location : RepetitiveSpec.Locations)
	{
		Location.PurposeTag = TEXT("Purpose.Workplace");
	}
	FResolvedWorldPlan RepetitivePlan;
	FValidationReport RepetitiveReport;
	FWorldDirectorCompiler::Resolve(RepetitiveSpec, RepetitivePlan, RepetitiveReport, false);
	TestTrue(TEXT("Excessive resolved shell repetition is reported"),
		HasIssueCode(RepetitiveReport, TEXT("compiler.asset_overuse")));
	FGeneratedWorldSpec TooManyHomesSpec = Spec;
	TooManyHomesSpec.Locations[6].PurposeTag = TEXT("Purpose.Home");
	FResolvedWorldPlan TooManyHomesPlan;
	FValidationReport TooManyHomesReport;
	TestFalse(TEXT("A seventh home is rejected before it can spill outside certified home plots"),
		FWorldDirectorCompiler::Resolve(
			TooManyHomesSpec, TooManyHomesPlan, TooManyHomesReport, false));
	TestTrue(TEXT("Home plot overflow reports a precise compiler code"),
		HasIssueCode(TooManyHomesReport, TEXT("compiler.home_plot_count")));
	return !HasAnyErrors();
}

#endif

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWorldDirectorCompilerResolveFixtureTest,
	"WorldDirector.Phase3.ResolveFixture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldDirectorCompilerResolveFixtureTest::RunTest(const FString& Parameters)
{
	FString SourceJson;
	if (!TestTrue(TEXT("Compiler fixture exists"), LoadFixture(TEXT("compiler-town.json"), SourceJson)))
	{
		return false;
	}
	FGeneratedWorldSpec Spec;
	FValidationReport ParseReport;
	if (!TestTrue(TEXT("Compiler fixture parses"), FWorldDirectorJson::LoadGeneratedWorldSpec(SourceJson, Spec, ParseReport)))
	{
		return false;
	}
	const FValidationReport SemanticReport = FWorldDirectorValidator::Validate(Spec, MakeFixtureCapabilityTags());
	if (!TestTrue(TEXT("Compiler fixture passes semantic validation"), SemanticReport.bValid))
	{
		return false;
	}

	FResolvedWorldPlan FirstPlan;
	FValidationReport FirstReport;
	const int32 OriginalSeed = Spec.Seed;
	if (!TestTrue(TEXT("Fixture resolves to certified assets"), FWorldDirectorCompiler::Resolve(Spec, FirstPlan, FirstReport)))
	{
		return false;
	}
	TestEqual(TEXT("Eight buildings resolve"), FirstPlan.Locations.Num(), 8);
	TestEqual(TEXT("Eight residents resolve"), FirstPlan.Residents.Num(), 8);
	TestTrue(TEXT("Road and path geometry resolves"), FirstPlan.Routes.Num() >= 5);
	TestEqual(TEXT("Landmark remains a stable semantic ID"), FirstPlan.LandmarkLocationId, FString(TEXT("location.guildhall")));
	TestEqual(TEXT("Resolved physical recipe uses V2"), FirstPlan.Version, 2);
	TestTrue(TEXT("Generated terrain replaces the legacy fixed terrain map"), FirstPlan.TerrainMap.IsNull());
	TestEqual(TEXT("Terrain height grid is complete"), FirstPlan.Terrain.HeightsCentimeters.Num(),
		FirstPlan.Terrain.Resolution * FirstPlan.Terrain.Resolution);
	TestEqual(TEXT("Terrain surface grid is complete"), FirstPlan.Terrain.SurfaceTypes.Num(),
		FirstPlan.Terrain.HeightsCentimeters.Num());
	TestTrue(TEXT("Physical recipe has a full SHA-256 fingerprint"), FirstPlan.WorldFingerprint.Len() == 64);

	FResolvedWorldPlan SecondPlan;
	FValidationReport SecondReport;
	TestTrue(TEXT("Same seed resolves again"), FWorldDirectorCompiler::Resolve(Spec, SecondPlan, SecondReport, false));
	TestEqual(TEXT("Same seed reproduces first placement"), FirstPlan.Locations[0].Transform.ToString(), SecondPlan.Locations[0].Transform.ToString());
	TestEqual(TEXT("Same seed reproduces the full physical fingerprint"), FirstPlan.WorldFingerprint, SecondPlan.WorldFingerprint);
	FString RecipeJson;
	FValidationReport RecipeReport;
	TestTrue(TEXT("V2 physical recipe serializes"), FWorldDirectorJson::SaveResolvedWorldPlan(FirstPlan, RecipeJson, RecipeReport));
	FResolvedWorldPlan ReplayedPlan;
	TestTrue(TEXT("V2 physical recipe validates and reloads"), FWorldDirectorJson::LoadResolvedWorldPlan(RecipeJson, ReplayedPlan, RecipeReport));
	TestEqual(TEXT("Reload preserves the full physical fingerprint"), ReplayedPlan.WorldFingerprint, FirstPlan.WorldFingerprint);

	Spec.Seed += 1;
	FResolvedWorldPlan VariedPlan;
	FValidationReport VariedReport;
	TestTrue(TEXT("Adjacent seed also resolves"), FWorldDirectorCompiler::Resolve(Spec, VariedPlan, VariedReport, false));
	TestNotEqual(TEXT("Different seed changes terrain samples"), FirstPlan.Terrain.HeightFingerprint, VariedPlan.Terrain.HeightFingerprint);
	TestNotEqual(TEXT("Different seed changes physical layout"), FirstPlan.LayoutFingerprint, VariedPlan.LayoutFingerprint);
	TestNotEqual(TEXT("Different seed changes the road graph geometry"), FirstPlan.RouteFingerprint, VariedPlan.RouteFingerprint);
	TestNotEqual(TEXT("Different seed changes biome dressing"), FirstPlan.DressingFingerprint, VariedPlan.DressingFingerprint);

	Spec.Seed = OriginalSeed + 4;
	FResolvedWorldPlan DeformedPlan;
	FValidationReport DeformedReport;
	TestTrue(TEXT("Alternate seed resolves"),
		FWorldDirectorCompiler::Resolve(Spec, DeformedPlan, DeformedReport, false));
	int32 MovedLocationCount = 0;
	for (int32 Index = 0; Index < FirstPlan.Locations.Num(); ++Index)
	{
		MovedLocationCount += !FirstPlan.Locations[Index].Transform.GetLocation().Equals(
			DeformedPlan.Locations[Index].Transform.GetLocation(), 1.0f);
	}
	TestTrue(TEXT("Alternate seed materially changes physical plot placement"),
		MovedLocationCount >= FirstPlan.Locations.Num() / 2);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWorldDirectorPhysicalDiversityCorpusTest,
	"WorldDirector.WorldGeneration.PhysicalDiversity50Seeds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldDirectorPhysicalDiversityCorpusTest::RunTest(const FString& Parameters)
{
	FString SourceJson;
	FGeneratedWorldSpec Spec;
	FValidationReport ParseReport;
	if (!LoadFixture(TEXT("compiler-town.json"), SourceJson) ||
		!FWorldDirectorJson::LoadGeneratedWorldSpec(SourceJson, Spec, ParseReport))
	{
		AddError(TEXT("Could not load the diversity benchmark fixture."));
		return false;
	}
	TSet<FString> WorldFingerprints;
	FResolvedWorldPlan Previous;
	for (int32 SeedIndex = 0; SeedIndex < 50; ++SeedIndex)
	{
		Spec.Seed = 71000 + SeedIndex;
		FResolvedWorldPlan Plan;
		FValidationReport Report;
		if (!TestTrue(*FString::Printf(TEXT("Seed %d resolves"), Spec.Seed),
			FWorldDirectorCompiler::Resolve(Spec, Plan, Report, false)))
		{
			for (const FValidationIssue& Issue : Report.Issues)
			{
				AddError(FString::Printf(TEXT("%s %s %s"), *Issue.Code.ToString(), *Issue.Path, *Issue.Message));
			}
			continue;
		}
		TestFalse(*FString::Printf(TEXT("Seed %d fingerprint is unique"), Spec.Seed),
			WorldFingerprints.Contains(Plan.WorldFingerprint));
		WorldFingerprints.Add(Plan.WorldFingerprint);
		if (SeedIndex > 0)
		{
			int32 DifferentAxes = 0;
			DifferentAxes += Previous.Terrain.HeightFingerprint != Plan.Terrain.HeightFingerprint;
			DifferentAxes += Previous.Terrain.SurfaceFingerprint != Plan.Terrain.SurfaceFingerprint;
			DifferentAxes += Previous.LayoutFingerprint != Plan.LayoutFingerprint;
			DifferentAxes += Previous.RouteFingerprint != Plan.RouteFingerprint;
			DifferentAxes += Previous.DressingFingerprint != Plan.DressingFingerprint;
			DifferentAxes += Previous.DistrictAnchors.Num() > 0 && Plan.DistrictAnchors.Num() > 0 &&
				!Previous.DistrictAnchors[0].Position.Equals(Plan.DistrictAnchors[0].Position, 1.0f);
			TestTrue(*FString::Printf(TEXT("Adjacent seeds %d/%d differ on at least four physical axes"),
				Spec.Seed - 1, Spec.Seed), DifferentAxes >= 4);
		}
		Previous = MoveTemp(Plan);
	}
	TestEqual(TEXT("All 50 benchmark seeds have unique physical fingerprints"), WorldFingerprints.Num(), 50);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWorldDirectorAllArchetypesPhysicalCoverageTest,
	"WorldDirector.WorldGeneration.AllArchetypesPhysicalCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldDirectorAllArchetypesPhysicalCoverageTest::RunTest(const FString& Parameters)
{
	FString SourceJson;
	FGeneratedWorldSpec BaseSpec;
	FValidationReport ParseReport;
	if (!LoadFixture(TEXT("compiler-town.json"), SourceJson) ||
		!FWorldDirectorJson::LoadGeneratedWorldSpec(SourceJson, BaseSpec, ParseReport))
	{
		AddError(TEXT("Could not load the archetype coverage fixture."));
		return false;
	}
	struct FArchetypeCase
	{
		const TCHAR* Intent;
		EWorldDirectorTerrainArchetype Expected;
		bool bExpectsWater;
	};
	const FArchetypeCase Cases[] = {
		{TEXT("sheltered basin"), EWorldDirectorTerrainArchetype::Basin, false},
		{TEXT("river valley"), EWorldDirectorTerrainArchetype::Valley, true},
		{TEXT("mountain ridge"), EWorldDirectorTerrainArchetype::Ridge, false},
		{TEXT("storm coast"), EWorldDirectorTerrainArchetype::Coast, true},
		{TEXT("reed marsh"), EWorldDirectorTerrainArchetype::Marsh, true}
	};
	for (int32 CaseIndex = 0; CaseIndex < UE_ARRAY_COUNT(Cases); ++CaseIndex)
	{
		FGeneratedWorldSpec Spec = BaseSpec;
		Spec.Seed = 82000 + CaseIndex;
		Spec.Brief.TerrainPreferences = {Cases[CaseIndex].Intent};
		FResolvedWorldPlan Plan;
		FValidationReport Report;
		if (!TestTrue(*FString::Printf(TEXT("%s recipe resolves"), Cases[CaseIndex].Intent),
			FWorldDirectorCompiler::Resolve(Spec, Plan, Report, false)))
		{
			for (const FValidationIssue& Issue : Report.Issues)
			{
				AddError(FString::Printf(TEXT("%s %s %s"), *Issue.Code.ToString(), *Issue.Path, *Issue.Message));
			}
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s selects its requested archetype"), Cases[CaseIndex].Intent),
			Plan.Terrain.Archetype, Cases[CaseIndex].Expected);

		int32 RenderedWaterCells = 0;
		for (int32 Y = 0; Y < Plan.Terrain.Resolution - 1; ++Y)
		{
			for (int32 X = 0; X < Plan.Terrain.Resolution - 1; ++X)
			{
				RenderedWaterCells += Plan.Terrain.SurfaceTypes[Y * Plan.Terrain.Resolution + X] ==
					static_cast<uint8>(EWorldDirectorSurfaceType::Water);
			}
		}
		TestEqual(*FString::Printf(TEXT("%s water classification matches its recipe"), Cases[CaseIndex].Intent),
			RenderedWaterCells > 0, Cases[CaseIndex].bExpectsWater);

		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
			*FString::Printf(TEXT("WorldDirectorArchetypeTestWorld%d"), CaseIndex));
		if (!TestNotNull(TEXT("Transient archetype world created"), World))
		{
			continue;
		}
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		World->AddToRoot();
		WorldContext.SetCurrentWorld(World);
		AWorldDirectorTownActor* Town = nullptr;
		const bool bSpawned = FWorldDirectorCompiler::Spawn(World, Plan, Town, Report);
		TestTrue(*FString::Printf(TEXT("%s runtime town spawns"), Cases[CaseIndex].Intent), bSpawned);
		if (Town != nullptr)
		{
			int32 TerrainQuadCount = 0;
			for (int32 SectionIndex = 0; SectionIndex < 5; ++SectionIndex)
			{
				if (const FProcMeshSection* Section = Town->TerrainMesh->GetProcMeshSection(SectionIndex))
				{
					TerrainQuadCount += Section->ProcVertexBuffer.Num() / 4;
				}
			}
			TestEqual(*FString::Printf(TEXT("%s emits a watertight collision quad grid"), Cases[CaseIndex].Intent),
				TerrainQuadCount, (Plan.Terrain.Resolution - 1) * (Plan.Terrain.Resolution - 1));
			const FProcMeshSection* WaterSection = Town->WaterMesh->GetProcMeshSection(0);
			TestEqual(*FString::Printf(TEXT("%s water surface presence matches classification"), Cases[CaseIndex].Intent),
				WaterSection != nullptr, Cases[CaseIndex].bExpectsWater);
			if (WaterSection != nullptr)
			{
				TestEqual(*FString::Printf(TEXT("%s emits one visual water quad per classified cell"), Cases[CaseIndex].Intent),
					WaterSection->ProcVertexBuffer.Num(), RenderedWaterCells * 4);
			}
		}
		World->DestroyWorld(true);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWorldDirectorCompilerSpawnFixtureTest,
	"WorldDirector.Phase3.SpawnFixture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldDirectorCompilerSpawnFixtureTest::RunTest(const FString& Parameters)
{
	FString SourceJson;
	FGeneratedWorldSpec Spec;
	FValidationReport Report;
	if (!LoadFixture(TEXT("compiler-town.json"), SourceJson) ||
		!FWorldDirectorJson::LoadGeneratedWorldSpec(SourceJson, Spec, Report))
	{
		AddError(TEXT("Could not load compiler fixture."));
		return false;
	}
	FResolvedWorldPlan Plan;
	if (!FWorldDirectorCompiler::Resolve(Spec, Plan, Report))
	{
		AddError(TEXT("Could not resolve compiler fixture."));
		return false;
	}

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("WorldDirectorCompilerTestWorld"));
	if (!TestNotNull(TEXT("Transient runtime world created"), World))
	{
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	World->AddToRoot();
	WorldContext.SetCurrentWorld(World);
	AWorldDirectorTownActor* Town = nullptr;
	const bool bSpawned = FWorldDirectorCompiler::Spawn(World, Plan, Town, Report);
	TestTrue(TEXT("Resolved fixture spawns without editor intervention"), bSpawned);
	if (Town != nullptr)
	{
		TestEqual(TEXT("All locations spawn"), Town->SpawnedLocations.Num(), 8);
		TestEqual(TEXT("All residents spawn"), Town->SpawnedResidents.Num(), 8);
		for (int32 Index = 0; Index < Town->SpawnedLocations.Num(); ++Index)
		{
			const AWorldDirectorLocationActor* Location = Town->SpawnedLocations[Index];
			TestNotNull(*FString::Printf(TEXT("Location %d has a working door actor"), Index), Location ? Location->DoorActor.Get() : nullptr);
			if (Location && Location->DoorActor)
			{
				const bool bWasOpen = Location->DoorActor->bIsOpen;
				Location->DoorActor->ToggleDoor();
				TestNotEqual(*FString::Printf(TEXT("Door %d toggles"), Index), bWasOpen, Location->DoorActor->bIsOpen);
				Location->DoorActor->SetDoorOpen(true);
			}
		}
	}
	World->DestroyWorld(true);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWorldDirectorEnvironmentProfileAssetTest,
	"WorldDirector.WorldGeneration.EnvironmentProfileAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldDirectorEnvironmentProfileAssetTest::RunTest(const FString& Parameters)
{
	const UWorldEnvironmentProfile* Profile = UWorldEnvironmentProfile::ResolveStylizedVillage();
	if (!TestNotNull(TEXT("Stylized village environment profile resolves"), Profile))
	{
		return false;
	}
	FString ValidationError;
	const bool bProfileValid = Profile->Validate(ValidationError);
	TestTrue(*FString::Printf(TEXT("Environment profile is certified: %s"), *ValidationError), bProfileValid);
	TestFalse(TEXT("Profile does not reference the prototype grid material"), Profile->OpaqueMasterMaterial.ToString().Contains(TEXT("WorldGrid")));
	TestNotNull(TEXT("Opaque terrain material loads"), Profile->OpaqueMasterMaterial.TryLoad());
	TestNotNull(TEXT("Rock material loads"), Profile->RockMaterial.TryLoad());
	TestNotNull(TEXT("Water material loads"), Profile->WaterMaterial.TryLoad());
	for (const FWorldEnvironmentSurfaceAsset& Surface : Profile->Surfaces)
	{
		TestNotNull(*FString::Printf(TEXT("%s base color loads"), *Surface.SurfaceTag.ToString()), Surface.BaseColorTexture.TryLoad());
		TestNotNull(*FString::Printf(TEXT("%s normal loads"), *Surface.SurfaceTag.ToString()), Surface.NormalTexture.TryLoad());
	}
	for (int32 MeshIndex = 0; MeshIndex < Profile->DressingMeshes.Num(); ++MeshIndex)
	{
		TestNotNull(*FString::Printf(TEXT("Dressing mesh %d loads"), MeshIndex), Profile->DressingMeshes[MeshIndex].TryLoad());
	}
	return !HasAnyErrors();
}

#endif
