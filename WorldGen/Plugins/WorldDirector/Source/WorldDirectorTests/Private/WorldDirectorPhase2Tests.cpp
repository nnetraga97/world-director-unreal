#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Interfaces/IPluginManager.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "WorldDirectorJson.h"
#include "WorldDirectorCompiler.h"
#include "WorldDirectorPhysicalGenerator.h"
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

bool LoadSchema(FString& OutJson)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("WorldDirector"));
	if (!Plugin.IsValid())
	{
		return false;
	}
	return FFileHelper::LoadFileToString(
		OutJson,
		*(FPaths::Combine(
			Plugin->GetBaseDir(), TEXT("Resources/Schemas"), TEXT("world-director.schema.json"))));
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

TSharedPtr<FJsonObject> ParseJsonObject(const FString& Json)
{
	TSharedPtr<FJsonObject> Object;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	return FJsonSerializer::Deserialize(Reader, Object) ? Object : nullptr;
}

bool SerializeJsonObject(const TSharedPtr<FJsonObject>& Object, FString& OutJson)
{
	OutJson.Reset();
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
	return Object.IsValid() && FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
}

float TestDistanceToSegment(
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

bool IsInsideExpandedLocation(
	const FVector2D& Point,
	const FResolvedLocationPlan& Location,
	const float Padding)
{
	const FVector Local = Location.Transform.InverseTransformPosition(
		FVector(Point, Location.Transform.GetLocation().Z));
	return FMath::Abs(Local.X) <= Location.FootprintSize.X * 0.5f + Padding &&
		FMath::Abs(Local.Y) <= Location.FootprintSize.Y * 0.5f + Padding;
}

float TestRenderedTerrainHeight(
	const FWorldDirectorTerrainRecipe& Terrain,
	const FVector2D& Position)
{
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
	const float A = Terrain.HeightsCentimeters[Y0 * Terrain.Resolution + X0];
	const float B = Terrain.HeightsCentimeters[Y0 * Terrain.Resolution + X1];
	const float C = Terrain.HeightsCentimeters[Y1 * Terrain.Resolution + X0];
	const float D = Terrain.HeightsCentimeters[Y1 * Terrain.Resolution + X1];
	return TX + TY <= 1.0f
		? A + TX * (B - A) + TY * (C - A)
		: D + (1.0f - TY) * (B - D) + (1.0f - TX) * (C - D);
}

int32 CountRenderedRouteSamples(const TArray<FVector>& SourcePoints)
{
	TArray<FVector> UniquePoints;
	for (const FVector& Point : SourcePoints)
	{
		if (UniquePoints.IsEmpty() || !Point.Equals(UniquePoints.Last(), 1.0f))
		{
			UniquePoints.Add(Point);
		}
	}
	if (UniquePoints.Num() < 2)
	{
		return 0;
	}
	int32 Count = 1;
	for (int32 Index = 1; Index < UniquePoints.Num(); ++Index)
	{
		Count += FMath::Max(1, FMath::CeilToInt(
			FVector2D::Distance(FVector2D(UniquePoints[Index - 1]),
				FVector2D(UniquePoints[Index])) / 180.0f));
	}
	return Count;
}

TArray<FVector2D> BuildExpectedRouteJunctionCenters(const FResolvedWorldPlan& Plan)
{
	TArray<FVector2D> Centers;
	TArray<float> Radii;
	auto QueueCenter = [&](const FVector2D& Center, const float Radius)
	{
		for (int32 Index = 0; Index < Centers.Num(); ++Index)
		{
			if (FVector2D::Distance(Centers[Index], Center) <=
				FMath::Max(180.0f, FMath::Min(Radii[Index], Radius) * 0.7f))
			{
				Radii[Index] = FMath::Max(Radii[Index], Radius);
				return;
			}
		}
		Centers.Add(Center);
		Radii.Add(Radius);
	};
	for (int32 RouteIndex = 0; RouteIndex < Plan.Routes.Num(); ++RouteIndex)
	{
		const FResolvedRoutePlan& Route = Plan.Routes[RouteIndex];
		if (Route.ControlPoints.IsEmpty())
		{
			continue;
		}
		auto TouchesEarlierRoute = [&](const FVector2D& Endpoint)
		{
			for (int32 EarlierIndex = 0; EarlierIndex < RouteIndex; ++EarlierIndex)
			{
				const FResolvedRoutePlan& Earlier = Plan.Routes[EarlierIndex];
				for (int32 SegmentIndex = 1; SegmentIndex < Earlier.ControlPoints.Num(); ++SegmentIndex)
				{
					if (TestDistanceToSegment(
						Endpoint,
						FVector2D(Earlier.ControlPoints[SegmentIndex - 1]),
						FVector2D(Earlier.ControlPoints[SegmentIndex])) <= 120.0f)
					{
						return true;
					}
				}
			}
			return false;
		};
		const float Radius = Route.WidthCentimeters * 0.62f;
		if (TouchesEarlierRoute(FVector2D(Route.ControlPoints[0])))
		{
			QueueCenter(FVector2D(Route.ControlPoints[0]), Radius);
		}
		if (TouchesEarlierRoute(FVector2D(Route.ControlPoints.Last())))
		{
			QueueCenter(FVector2D(Route.ControlPoints.Last()), Radius);
		}
	}
	return Centers;
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
	TestFalse(TEXT("Purpose-aware shell selection avoids the asset-overuse warning"),
		HasIssueCode(ResolveReport, TEXT("compiler.asset_overuse")));
	TSet<FString> PavedCourtyardIds;
	for (const FResolvedLocationPlan& Location : Plan.Locations)
	{
		if (Location.bPavedCourtyard)
		{
			PavedCourtyardIds.Add(Location.LocationId);
		}
	}
	TestEqual(TEXT("Large towns reserve stone paving for three civic focal points"),
		PavedCourtyardIds.Num(), 3);
	TestTrue(TEXT("Primary frontier hall owns the main paved court"),
		PavedCourtyardIds.Contains(TEXT("location.frontier_hall")));
	TestTrue(TEXT("Covered market receives a secondary paved court"),
		PavedCourtyardIds.Contains(TEXT("location.market_square")));
	TestTrue(TEXT("Council archive receives the other secondary paved court"),
		PavedCourtyardIds.Contains(TEXT("location.council_archive")));
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
	TSet<FString> ResidentialShells;
	TSet<FString> ShellSubstyles;
	TMap<FString, int32> ShellUseCounts;
	int32 MostRepeatedShellCount = 0;
	for (const FResolvedLocationPlan& Location : Plan.Locations)
	{
		const FString Shell = Location.ShellAsset.ToString();
		ShellSubstyles.Add(Shell);
		MostRepeatedShellCount = FMath::Max(
			MostRepeatedShellCount, ++ShellUseCounts.FindOrAdd(Shell));
		const FString Interior = Location.InteriorAsset.ToString();
		if (Shell.Contains(TEXT("Home_Compact_01")) ||
			Shell.Contains(TEXT("Home_Tall_01")) ||
			Shell.Contains(TEXT("Workplace_Inn_01")))
		{
			TestTrue(*FString::Printf(TEXT("%s keeps its certified small interior pairing"),
				*Location.LocationId), Interior.Contains(TEXT("Interior_Home_Small_01")));
		}
		else if (Shell.Contains(TEXT("Home_Compact_02")) ||
			Shell.Contains(TEXT("Home_Multiwing_01")) ||
			Shell.Contains(TEXT("Home_Tall_02")))
		{
			TestTrue(*FString::Printf(TEXT("%s keeps its certified roomy interior pairing"),
				*Location.LocationId), Interior.Contains(TEXT("Interior_Home_Compact_01")));
		}
		else if (Shell.Contains(TEXT("Workplace_Longhouse_01")) ||
			Shell.Contains(TEXT("Workplace_Guildhall_01")))
		{
			TestTrue(*FString::Printf(TEXT("%s keeps its certified workplace interior pairing"),
				*Location.LocationId), Interior.Contains(TEXT("Interior_Workplace_Longhouse_01")));
		}
	}
	TestEqual(TEXT("Full town uses all eight purpose-compatible shell archetypes"),
		ShellSubstyles.Num(), 8);
	TestTrue(TEXT("No shell occupies more than one third of the 18-location town"),
		MostRepeatedShellCount <= 6);
	for (int32 Index = 0; Index < 6; ++Index)
	{
		const FString Shell = Plan.Locations[Index].ShellAsset.ToString();
		TestTrue(*FString::Printf(TEXT("Home %d uses a certified residential shell"), Index),
			Shell.Contains(TEXT("BP_Cap_Home_")));
		ResidentialShells.Add(Shell);
	}
	TestEqual(TEXT("Six homes exercise all five certified residential silhouettes"),
		ResidentialShells.Num(), 5);
	const FResolvedLocationPlan* PrimaryLandmark = Plan.Locations.FindByPredicate(
		[&Plan](const FResolvedLocationPlan& Location)
		{
			return Location.LocationId == Plan.LandmarkLocationId;
		});
	if (TestNotNull(TEXT("Primary landmark resolves to a physical shell"), PrimaryLandmark))
	{
		TestTrue(TEXT("Primary landmark always owns a paved civic court"),
			PrimaryLandmark->bPavedCourtyard);
		const FString GuildhallShell = PrimaryLandmark->ShellAsset.ToString();
		TestTrue(TEXT("Primary landmark receives the unique guildhall silhouette"),
			GuildhallShell.Contains(TEXT("Workplace_Guildhall_01")));
		TestEqual(TEXT("Guildhall silhouette is reserved for exactly one location"),
			ShellUseCounts.FindRef(GuildhallShell), 1);
	}
	for (const FResolvedLocationPlan& Location : Plan.Locations)
	{
		const FWorldLocation* SourceLocation = Spec.Locations.FindByPredicate(
			[&Location](const FWorldLocation& Candidate)
			{
				return Candidate.Id == Location.LocationId;
			});
		if (SourceLocation != nullptr &&
			SourceLocation->PurposeTag != TEXT("Purpose.Home") &&
			Location.LocationId != Plan.LandmarkLocationId)
		{
			const FString Shell = Location.ShellAsset.ToString();
			TestTrue(*FString::Printf(TEXT("%s uses a certified public shell"), *Location.LocationId),
				Shell.Contains(TEXT("Workplace_Longhouse_01")) ||
				Shell.Contains(TEXT("Workplace_Inn_01")));
		}
	}
	FResolvedWorldPlan RepeatedPlan;
	FValidationReport RepeatedReport;
	if (TestTrue(TEXT("Living town shell assignment resolves deterministically"),
		FWorldDirectorCompiler::Resolve(Spec, RepeatedPlan, RepeatedReport, false)))
	{
		for (int32 Index = 0; Index < Plan.Locations.Num(); ++Index)
		{
			TestEqual(*FString::Printf(TEXT("Location %d keeps the same shell for the same seed"), Index),
				RepeatedPlan.Locations[Index].ShellAsset.ToString(),
				Plan.Locations[Index].ShellAsset.ToString());
			TestEqual(*FString::Printf(TEXT("Location %d keeps the same civic surface for the same seed"), Index),
				RepeatedPlan.Locations[Index].bPavedCourtyard,
				Plan.Locations[Index].bPavedCourtyard);
		}
	}
	Spec.Locations[6].PurposeTag = TEXT("Purpose.Clinic");
	FResolvedWorldPlan ClinicPlan;
	FValidationReport ClinicReport;
	TestTrue(TEXT("A supported public conversion purpose resolves"),
		FWorldDirectorCompiler::Resolve(Spec, ClinicPlan, ClinicReport, false));
	TestTrue(TEXT("Public clinic uses the hospitality-scale inn shell"),
		ClinicPlan.Locations[6].ShellAsset.ToString().Contains(TEXT("Workplace_Inn_01")));
	TestTrue(TEXT("Public clinic preserves the inn's compatible small interior"),
		ClinicPlan.Locations[6].InteriorAsset.ToString().Contains(TEXT("Interior_Home_Small_01")));
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
	TSet<FString> CompilerFixtureShells;
	int32 CompilerFixtureGuildhallCount = 0;
	for (const FResolvedLocationPlan& Location : FirstPlan.Locations)
	{
		const FString Shell = Location.ShellAsset.ToString();
		CompilerFixtureShells.Add(Shell);
		CompilerFixtureGuildhallCount += Shell.Contains(TEXT("Workplace_Guildhall_01")) ? 1 : 0;
	}
	TestEqual(TEXT("Compact fixture demonstrates seven selected shell archetypes"),
		CompilerFixtureShells.Num(), 7);
	TestEqual(TEXT("Compact fixture reserves one guildhall for its primary landmark"),
		CompilerFixtureGuildhallCount, 1);
	TSet<FString> CompactPavedCourtyardIds;
	for (const FResolvedLocationPlan& Location : FirstPlan.Locations)
	{
		if (Location.bPavedCourtyard)
		{
			CompactPavedCourtyardIds.Add(Location.LocationId);
		}
	}
	TestEqual(TEXT("Compact towns reserve paving for one landmark and one public satellite"),
		CompactPavedCourtyardIds.Num(), 2);
	TestTrue(TEXT("Compact landmark has a paved court"),
		CompactPavedCourtyardIds.Contains(TEXT("location.guildhall")));
	TestTrue(TEXT("Compact inn has the one secondary paved court"),
		CompactPavedCourtyardIds.Contains(TEXT("location.inn")));
	TestTrue(TEXT("Road and path geometry resolves"), FirstPlan.Routes.Num() >= 5);
	TestEqual(TEXT("Physical route compiler emits one shared spanning network"),
		FirstPlan.Routes.Num(), FirstPlan.Locations.Num() - 1);
	int32 UnstableApproachJoinCount = 0;
	for (const FResolvedRoutePlan& Route : FirstPlan.Routes)
	{
		for (int32 PointIndex = 1; PointIndex < Route.ControlPoints.Num() - 1; ++PointIndex)
		{
			const FVector2D Incoming = (FVector2D(Route.ControlPoints[PointIndex]) -
				FVector2D(Route.ControlPoints[PointIndex - 1])).GetSafeNormal();
			const FVector2D Outgoing = (FVector2D(Route.ControlPoints[PointIndex + 1]) -
				FVector2D(Route.ControlPoints[PointIndex])).GetSafeNormal();
			const float TurnDegrees = FMath::RadiansToDegrees(FMath::Acos(
				FMath::Clamp(FVector2D::DotProduct(Incoming, Outgoing), -1.0f, 1.0f)));
			const float ShorterSegment = FMath::Min(
				FVector2D::Distance(FVector2D(Route.ControlPoints[PointIndex - 1]),
					FVector2D(Route.ControlPoints[PointIndex])),
				FVector2D::Distance(FVector2D(Route.ControlPoints[PointIndex]),
					FVector2D(Route.ControlPoints[PointIndex + 1])));
			UnstableApproachJoinCount += TurnDegrees > 45.0f &&
				ShorterSegment < Route.WidthCentimeters * 0.5f ? 1 : 0;
		}
	}
	TestEqual(TEXT("Rounded building approaches contain no acute sub-half-width joins"),
		UnstableApproachJoinCount, 0);
	for (int32 RouteIndex = 1; RouteIndex < FirstPlan.Routes.Num(); ++RouteIndex)
	{
		const FResolvedRoutePlan& Branch = FirstPlan.Routes[RouteIndex];
		float DistanceToEarlierNetwork = MAX_flt;
		if (!Branch.ControlPoints.IsEmpty())
		{
			const FVector2D BranchEnd(Branch.ControlPoints.Last());
			for (int32 ExistingIndex = 0; ExistingIndex < RouteIndex; ++ExistingIndex)
			{
				const FResolvedRoutePlan& Existing = FirstPlan.Routes[ExistingIndex];
				for (int32 SegmentIndex = 1; SegmentIndex < Existing.ControlPoints.Num(); ++SegmentIndex)
				{
					const FVector2D A(Existing.ControlPoints[SegmentIndex - 1]);
					const FVector2D B(Existing.ControlPoints[SegmentIndex]);
					const FVector2D Delta = B - A;
					const float Denominator = Delta.SizeSquared();
					const float T = Denominator > KINDA_SMALL_NUMBER
						? FMath::Clamp(FVector2D::DotProduct(BranchEnd - A, Delta) /
							Denominator, 0.0f, 1.0f) : 0.0f;
					DistanceToEarlierNetwork = FMath::Min(DistanceToEarlierNetwork,
						FVector2D::Distance(BranchEnd, A + Delta * T));
				}
			}
		}
		TestTrue(*FString::Printf(TEXT("Physical branch %d terminates on the existing road network"),
			RouteIndex), DistanceToEarlierNetwork <= 2.0f);
	}
	TSet<FString> ReachableLocations;
	ReachableLocations.Add(FirstPlan.LandmarkLocationId);
	bool bExpandedRoutes = true;
	while (bExpandedRoutes)
	{
		bExpandedRoutes = false;
		for (const FResolvedRoutePlan& Route : FirstPlan.Routes)
		{
			if (ReachableLocations.Contains(Route.FromLocationId) &&
				!ReachableLocations.Contains(Route.ToLocationId))
			{
				ReachableLocations.Add(Route.ToLocationId);
				bExpandedRoutes = true;
			}
			else if (ReachableLocations.Contains(Route.ToLocationId) &&
				!ReachableLocations.Contains(Route.FromLocationId))
			{
				ReachableLocations.Add(Route.FromLocationId);
				bExpandedRoutes = true;
			}
		}
	}
	TestEqual(TEXT("Shared physical road network reaches every generated location"),
		ReachableLocations.Num(), FirstPlan.Locations.Num());
	TestEqual(TEXT("Landmark remains a stable semantic ID"), FirstPlan.LandmarkLocationId, FString(TEXT("location.guildhall")));
	TestEqual(TEXT("Resolved physical recipe uses terrain-rich V3"), FirstPlan.Version, 3);
	TestTrue(TEXT("Generated terrain replaces the legacy fixed terrain map"), FirstPlan.TerrainMap.IsNull());
	TestTrue(TEXT("V3 terrain covers a world-scale play space"),
		FirstPlan.Terrain.ExtentCentimeters >= 50000 && FirstPlan.Terrain.Resolution >= 129);
	TestEqual(TEXT("Terrain height grid is complete"), FirstPlan.Terrain.HeightsCentimeters.Num(),
		FirstPlan.Terrain.Resolution * FirstPlan.Terrain.Resolution);
	TestEqual(TEXT("Terrain surface grid is complete"), FirstPlan.Terrain.SurfaceTypes.Num(),
		FirstPlan.Terrain.HeightsCentimeters.Num());
	TestEqual(TEXT("Terrain moisture grid is complete"), FirstPlan.Terrain.MoistureValues.Num(),
		FirstPlan.Terrain.HeightsCentimeters.Num());
	TestEqual(TEXT("Terrain stores four blend weights per sample"), FirstPlan.Terrain.SurfaceBlendWeights.Num(),
		FirstPlan.Terrain.HeightsCentimeters.Num() * 4);
	TestTrue(TEXT("Terrain has meaningful macro relief"),
		FirstPlan.Terrain.MaximumHeightCentimeters - FirstPlan.Terrain.MinimumHeightCentimeters >= 3000.0f);
	TestTrue(TEXT("Terrain exposes a usable buildable-area ratio"),
		FirstPlan.Terrain.BuildableRatio > 0.0f && FirstPlan.Terrain.BuildableRatio <= 1.0f);
	TestTrue(TEXT("Terrain water coverage is normalized"),
		FirstPlan.Terrain.WaterCoverage >= 0.0f && FirstPlan.Terrain.WaterCoverage <= 1.0f);
	TestTrue(TEXT("Terrain rock coverage is normalized"),
		FirstPlan.Terrain.RockCoverage >= 0.0f && FirstPlan.Terrain.RockCoverage <= 1.0f);
	TestFalse(TEXT("V3 terrain selects a settlement morphology"), FirstPlan.Terrain.SettlementMorphology.IsNone());
	TestFalse(TEXT("V3 terrain persists its environmental story"), FirstPlan.Terrain.EnvironmentalStory.IsEmpty());
	TSet<FName> DistrictAffinities;
	for (const FWorldDirectorDistrictAnchor& District : FirstPlan.DistrictAnchors)
	{
		DistrictAffinities.Add(District.TerrainAffinity);
	}
	TestTrue(TEXT("District grammar assigns a distinct civic and neighborhood terrain role"),
		DistrictAffinities.Num() >= 2);
	TSet<FString> HomeDistricts;
	for (const FResolvedLocationPlan& Location : FirstPlan.Locations)
	{
		if (Location.LocationId.StartsWith(TEXT("location.home_")))
		{
			HomeDistricts.Add(Location.DistrictId);
		}
	}
	TestTrue(TEXT("Homes occupy multiple morphology-defined neighborhoods"), HomeDistricts.Num() >= 2);
	const FResolvedLocationPlan* Landmark = FirstPlan.Locations.FindByPredicate(
		[&](const FResolvedLocationPlan& Location)
		{
			return Location.LocationId == FirstPlan.LandmarkLocationId;
		});
	if (TestNotNull(TEXT("Landmark has a resolved physical plot"), Landmark) &&
		!FirstPlan.DistrictAnchors.IsEmpty())
	{
		TestEqual(TEXT("Landmark belongs to the civic district"),
			Landmark->DistrictId, FirstPlan.DistrictAnchors[0].DistrictId);
		const FVector2D LandmarkCenter(Landmark->Transform.GetLocation());
		const FVector2D LandmarkEntrance(Landmark->EntranceTransform.GetLocation());
		const FVector2D LandmarkFront = (LandmarkEntrance - LandmarkCenter).GetSafeNormal();
		const FVector2D ToCivicCore = (FVector2D(FirstPlan.DistrictAnchors[0].Position) -
			LandmarkEntrance).GetSafeNormal();
		TestTrue(TEXT("Landmark entrance deliberately faces its civic approach"),
			FVector2D::DotProduct(LandmarkFront, ToCivicCore) > 0.9f);

		bool bHasAlignedLandmarkApproach = false;
		for (const FResolvedRoutePlan& Route : FirstPlan.Routes)
		{
			const bool bStartsAtLandmark = Route.FromLocationId == FirstPlan.LandmarkLocationId;
			const bool bEndsAtLandmark = Route.ToLocationId == FirstPlan.LandmarkLocationId;
			if ((!bStartsAtLandmark && !bEndsAtLandmark) || Route.ControlPoints.Num() < 2)
			{
				continue;
			}
			const FVector2D TowardBuilding = bStartsAtLandmark
				? (FVector2D(Route.ControlPoints[0]) - FVector2D(Route.ControlPoints[1])).GetSafeNormal()
				: (FVector2D(Route.ControlPoints.Last()) -
					FVector2D(Route.ControlPoints[Route.ControlPoints.Num() - 2])).GetSafeNormal();
			const FVector2D EntranceToBuilding = (LandmarkCenter - LandmarkEntrance).GetSafeNormal();
			bHasAlignedLandmarkApproach |= Route.WidthCentimeters >= 700.0f &&
				FVector2D::DotProduct(TowardBuilding, EntranceToBuilding) > 0.9f;
		}
		TestTrue(TEXT("A widened route preserves a straight landmark reveal at its entrance"),
			bHasAlignedLandmarkApproach);

		int32 StoryInstanceCount = 0;
		int32 LandmarkStoryInstanceCount = 0;
		for (const FWorldDirectorDressingInstance& Instance : FirstPlan.Dressing)
		{
			if (Instance.BiomeTag.ToString().StartsWith(TEXT("Story.")))
			{
				++StoryInstanceCount;
				LandmarkStoryInstanceCount += FVector2D::Distance(
					FVector2D(Instance.Transform.GetLocation()), LandmarkEntrance) < 10000.0f;
			}
		}
		TestTrue(TEXT("Environmental story uses a deterministic authored cluster"),
			StoryInstanceCount >= 6);
		TestTrue(TEXT("Environmental story visibly frames the landmark approach"),
			LandmarkStoryInstanceCount >= 4);
	}
	TSet<uint8> SurfaceClasses;
	TSet<uint8> MoistureBands;
	for (int32 SampleIndex = 0; SampleIndex < FirstPlan.Terrain.HeightsCentimeters.Num(); ++SampleIndex)
	{
		SurfaceClasses.Add(FirstPlan.Terrain.SurfaceTypes[SampleIndex]);
		MoistureBands.Add(FirstPlan.Terrain.MoistureValues[SampleIndex] / 16);
		const int32 WeightOffset = SampleIndex * 4;
		const int32 WeightSum =
			FirstPlan.Terrain.SurfaceBlendWeights[WeightOffset] +
			FirstPlan.Terrain.SurfaceBlendWeights[WeightOffset + 1] +
			FirstPlan.Terrain.SurfaceBlendWeights[WeightOffset + 2] +
			FirstPlan.Terrain.SurfaceBlendWeights[WeightOffset + 3];
		if (WeightSum != 255)
		{
			AddError(FString::Printf(
				TEXT("Terrain blend weights at sample %d sum to %d instead of exactly 255."),
				SampleIndex, WeightSum));
			break;
		}
	}
	TestTrue(TEXT("Terrain classification produces at least three distinct surface classes"),
		SurfaceClasses.Num() >= 3);
	TestTrue(TEXT("Terrain moisture varies spatially instead of using one global value"),
		MoistureBands.Num() >= 3);
	int32 CanopyInstanceCount = 0;
	int32 GroundCoverInstanceCount = 0;
	int32 CultivatedEdgeInstanceCount = 0;
	int32 CultivatedAccentInstanceCount = 0;
	int32 SettlementClutterInstanceCount = 0;
	int32 SettlementGroveInstanceCount = 0;
	int32 CivicAnchorInstanceCount = 0;
	int32 CivicSeatInstanceCount = 0;
	int32 GuildBannerInstanceCount = 0;
	int32 FarmTransportInstanceCount = 0;
	int32 HomeUtilityInstanceCount = 0;
	int32 CommunalFireInstanceCount = 0;
	for (const FWorldDirectorDressingInstance& Instance : FirstPlan.Dressing)
	{
		CanopyInstanceCount += Instance.BiomeTag == TEXT("Biome.ForestCanopy");
		GroundCoverInstanceCount += Instance.BiomeTag == TEXT("Biome.Understory");
		CultivatedEdgeInstanceCount += Instance.BiomeTag == TEXT("Biome.CultivatedEdge");
		CultivatedAccentInstanceCount += Instance.BiomeTag == TEXT("Biome.CultivatedAccent");
		SettlementGroveInstanceCount += Instance.BiomeTag == TEXT("Biome.SettlementGrove");
		SettlementClutterInstanceCount += Instance.BiomeTag == TEXT("Biome.SettlementClutter") ||
			Instance.BiomeTag == TEXT("Biome.MarketCluster");
		CivicAnchorInstanceCount += Instance.BiomeTag == TEXT("Biome.CivicAnchor");
		CivicSeatInstanceCount += Instance.BiomeTag == TEXT("Biome.CivicSeat");
		GuildBannerInstanceCount += Instance.BiomeTag == TEXT("Biome.GuildBanner");
		FarmTransportInstanceCount += Instance.BiomeTag == TEXT("Biome.FarmTransport");
		HomeUtilityInstanceCount += Instance.BiomeTag == TEXT("Biome.HomeUtility");
		CommunalFireInstanceCount += Instance.BiomeTag == TEXT("Biome.CommunalFire");
	}
	TestTrue(TEXT("Basin ecology resolves as woodland mass instead of sparse tree dots"),
		CanopyInstanceCount >= 1200);
	TestTrue(TEXT("Basin ecology includes a visually useful understory layer"),
		GroundCoverInstanceCount >= 3000);
	TestTrue(TEXT("Cultivated parcels receive readable perimeter treatment"),
		CultivatedEdgeInstanceCount >= 40);
	TestTrue(TEXT("Cultivated parcels receive field-interior working detail"),
		CultivatedAccentInstanceCount >= 2);
	TestTrue(TEXT("District edges receive restrained settlement grove clusters"),
		SettlementGroveInstanceCount >= 6);
	TestTrue(TEXT("Generated plots receive purpose-aware lived-in prop clusters"),
		SettlementClutterInstanceCount >= FirstPlan.Locations.Num() * 2);
	TestEqual(TEXT("Civic court has one visual anchor"), CivicAnchorInstanceCount, 1);
	TestTrue(TEXT("Civic court has a seated gathering composition"), CivicSeatInstanceCount >= 2);
	TestEqual(TEXT("Guildhall entrance is framed by a banner pair"), GuildBannerInstanceCount, 2);
	TestTrue(TEXT("Farm parcels include working transport"), FarmTransportInstanceCount >= 1);
	TestTrue(TEXT("Homes receive rear-yard utility storytelling"), HomeUtilityInstanceCount >= 2);
	TestEqual(TEXT("A neighborhood receives one communal fire"), CommunalFireInstanceCount, 1);
	TestTrue(TEXT("Physical recipe has a full SHA-256 fingerprint"), FirstPlan.WorldFingerprint.Len() == 64);

	FResolvedWorldPlan SecondPlan;
	FValidationReport SecondReport;
	TestTrue(TEXT("Same seed resolves again"), FWorldDirectorCompiler::Resolve(Spec, SecondPlan, SecondReport, false));
	TestEqual(TEXT("Same seed reproduces first placement"), FirstPlan.Locations[0].Transform.ToString(), SecondPlan.Locations[0].Transform.ToString());
	TestEqual(TEXT("Same seed reproduces the full physical fingerprint"), FirstPlan.WorldFingerprint, SecondPlan.WorldFingerprint);
	FString RecipeJson;
	FValidationReport RecipeReport;
	TestTrue(TEXT("Complete V3 physical recipe serializes"), FWorldDirectorJson::SaveResolvedWorldPlan(FirstPlan, RecipeJson, RecipeReport));
	FResolvedWorldPlan ReplayedPlan;
	TestTrue(TEXT("Complete V3 physical recipe validates and reloads"), FWorldDirectorJson::LoadResolvedWorldPlan(RecipeJson, ReplayedPlan, RecipeReport));
	TestEqual(TEXT("Reload preserves the full physical fingerprint"), ReplayedPlan.WorldFingerprint, FirstPlan.WorldFingerprint);
	TestTrue(TEXT("Reload preserves terrain moisture"),
		ReplayedPlan.Terrain.MoistureValues == FirstPlan.Terrain.MoistureValues);
	TestTrue(TEXT("Reload preserves terrain blend weights"),
		ReplayedPlan.Terrain.SurfaceBlendWeights == FirstPlan.Terrain.SurfaceBlendWeights);
	TestEqual(TEXT("Reload preserves the environmental story"),
		ReplayedPlan.Terrain.EnvironmentalStory, FirstPlan.Terrain.EnvironmentalStory);
	TestEqual(TEXT("Reload preserves resolved purpose semantics"),
		ReplayedPlan.Locations[0].PurposeTag, FirstPlan.Locations[0].PurposeTag);
	for (int32 LocationIndex = 0; LocationIndex < FirstPlan.Locations.Num(); ++LocationIndex)
	{
		TestEqual(*FString::Printf(TEXT("Reload preserves civic surface selection for location %d"),
			LocationIndex), ReplayedPlan.Locations[LocationIndex].bPavedCourtyard,
			FirstPlan.Locations[LocationIndex].bPavedCourtyard);
	}

	TSharedPtr<FJsonObject> TamperedCourtyardRoot = ParseJsonObject(RecipeJson);
	const TArray<TSharedPtr<FJsonValue>>* TamperedLocations = nullptr;
	bool bChangedPavedCourtyard = false;
	if (TamperedCourtyardRoot.IsValid() &&
		TamperedCourtyardRoot->TryGetArrayField(TEXT("locations"), TamperedLocations) &&
		TamperedLocations != nullptr)
	{
		for (const TSharedPtr<FJsonValue>& Value : *TamperedLocations)
		{
			const TSharedPtr<FJsonObject> LocationObject = Value.IsValid() && Value->Type == EJson::Object
				? Value->AsObject() : nullptr;
			bool bPaved = false;
			if (LocationObject.IsValid() &&
				LocationObject->TryGetBoolField(TEXT("bPavedCourtyard"), bPaved) && bPaved)
			{
				LocationObject->SetBoolField(TEXT("bPavedCourtyard"), false);
				bChangedPavedCourtyard = true;
				break;
			}
		}
	}
	TestTrue(TEXT("Fingerprint tamper fixture changes one real paved court"), bChangedPavedCourtyard);
	FString TamperedCourtyardJson;
	TestTrue(TEXT("Fingerprint tamper fixture serializes"),
		SerializeJsonObject(TamperedCourtyardRoot, TamperedCourtyardJson));
	FResolvedWorldPlan RejectedCourtyardPlan;
	FValidationReport TamperedCourtyardReport;
	TestFalse(TEXT("V3 replay rejects civic-surface data changed without a matching layout fingerprint"),
		FWorldDirectorJson::LoadResolvedWorldPlan(
			TamperedCourtyardJson, RejectedCourtyardPlan, TamperedCourtyardReport));
	TestTrue(TEXT("Civic-surface tampering reports a fingerprint mismatch"),
		HasIssueCode(TamperedCourtyardReport, TEXT("json.recipe_fingerprint_mismatch")));

	TSharedPtr<FJsonObject> TamperedRouteRoot = ParseJsonObject(RecipeJson);
	const TArray<TSharedPtr<FJsonValue>>* TamperedRoutes = nullptr;
	bool bChangedRoute = false;
	if (TamperedRouteRoot.IsValid() &&
		TamperedRouteRoot->TryGetArrayField(TEXT("routes"), TamperedRoutes) &&
		TamperedRoutes != nullptr && !TamperedRoutes->IsEmpty())
	{
		const TSharedPtr<FJsonObject> RouteObject = (*TamperedRoutes)[0]->AsObject();
		if (RouteObject.IsValid())
		{
			RouteObject->SetNumberField(TEXT("widthCentimeters"),
				RouteObject->GetNumberField(TEXT("widthCentimeters")) + 10.0);
			bChangedRoute = true;
		}
	}
	TestTrue(TEXT("Route tamper fixture changes one persisted route"), bChangedRoute);
	FString TamperedRouteJson;
	TestTrue(TEXT("Route tamper fixture serializes"),
		SerializeJsonObject(TamperedRouteRoot, TamperedRouteJson));
	FResolvedWorldPlan RejectedRoutePlan;
	FValidationReport TamperedRouteReport;
	TestFalse(TEXT("V3 replay rejects route data changed without a matching route fingerprint"),
		FWorldDirectorJson::LoadResolvedWorldPlan(
			TamperedRouteJson, RejectedRoutePlan, TamperedRouteReport));
	TestTrue(TEXT("Route tampering reports a fingerprint mismatch"),
		HasIssueCode(TamperedRouteReport, TEXT("json.recipe_fingerprint_mismatch")));

	TSharedPtr<FJsonObject> EarlyV3Root = ParseJsonObject(RecipeJson);
	const TArray<TSharedPtr<FJsonValue>>* EarlyV3Locations = nullptr;
	if (EarlyV3Root.IsValid() &&
		EarlyV3Root->TryGetArrayField(TEXT("locations"), EarlyV3Locations) &&
		EarlyV3Locations != nullptr)
	{
		for (const TSharedPtr<FJsonValue>& Value : *EarlyV3Locations)
		{
			const TSharedPtr<FJsonObject> LocationObject = Value->AsObject();
			if (LocationObject.IsValid())
			{
				LocationObject->RemoveField(TEXT("purposeTag"));
				LocationObject->RemoveField(TEXT("bPavedCourtyard"));
			}
		}
	}
	FString EarlyV3Json;
	TestTrue(TEXT("Early V3 compatibility fixture serializes"),
		SerializeJsonObject(EarlyV3Root, EarlyV3Json));
	FResolvedWorldPlan ReplayedEarlyV3Plan;
	FValidationReport EarlyV3Report;
	TestTrue(TEXT("Early V3 recipe without additive location fields remains replayable"),
		FWorldDirectorJson::LoadResolvedWorldPlan(
			EarlyV3Json, ReplayedEarlyV3Plan, EarlyV3Report));
	TestTrue(TEXT("Early V3 replay explicitly reports applied legacy defaults"),
		HasIssueCode(EarlyV3Report, TEXT("json.recipe_legacy_defaults")));
	TestTrue(TEXT("Early V3 replay defaults omitted purposes safely"),
		!ReplayedEarlyV3Plan.Locations.IsEmpty() &&
		ReplayedEarlyV3Plan.Locations[0].PurposeTag.IsNone());
	TestFalse(TEXT("Early V3 replay does not invent civic paving"),
		ReplayedEarlyV3Plan.Locations.IsEmpty()
			? true : ReplayedEarlyV3Plan.Locations[0].bPavedCourtyard);

	FResolvedWorldPlan IncompleteV3Plan = FirstPlan;
	IncompleteV3Plan.Terrain.MoistureValues.Reset();
	FString IncompleteRecipeJson;
	FValidationReport IncompleteRecipeReport;
	TestFalse(TEXT("V3 recipe without its moisture grid cannot be persisted"),
		FWorldDirectorJson::SaveResolvedWorldPlan(
			IncompleteV3Plan, IncompleteRecipeJson, IncompleteRecipeReport));
	TestTrue(TEXT("Incomplete V3 recipe reports a precise persistence error"),
		HasIssueCode(IncompleteRecipeReport, TEXT("json.recipe_incomplete")));

	FResolvedWorldPlan TamperedV3Plan = FirstPlan;
	++TamperedV3Plan.Terrain.HeightsCentimeters[0];
	FString TamperedRecipeJson;
	FValidationReport TamperedRecipeReport;
	TestFalse(TEXT("V3 recipe rejects height data changed without a matching fingerprint"),
		FWorldDirectorJson::SaveResolvedWorldPlan(
			TamperedV3Plan, TamperedRecipeJson, TamperedRecipeReport));
	TestTrue(TEXT("Tampered V3 recipe reports a fingerprint mismatch"),
		HasIssueCode(TamperedRecipeReport, TEXT("json.recipe_fingerprint_mismatch")));

	FResolvedWorldPlan LegacyV2Plan = FirstPlan;
	LegacyV2Plan.Version = 2;
	LegacyV2Plan.Terrain.SurfaceBlendWeights.Reset();
	LegacyV2Plan.Terrain.MoistureValues.Reset();
	FString LegacyRecipeJson;
	FValidationReport LegacyRecipeReport;
	TestTrue(TEXT("Legacy V2 recipe remains persistable without V3-only terrain arrays"),
		FWorldDirectorJson::SaveResolvedWorldPlan(
			LegacyV2Plan, LegacyRecipeJson, LegacyRecipeReport));
	TSharedPtr<FJsonObject> GenuineLegacyRoot = ParseJsonObject(LegacyRecipeJson);
	if (GenuineLegacyRoot.IsValid())
	{
		const TSharedPtr<FJsonObject> TerrainObject = GenuineLegacyRoot->GetObjectField(TEXT("terrain"));
		if (TerrainObject.IsValid())
		{
			for (const TCHAR* Field : {
				TEXT("surfaceBlendWeights"), TEXT("moistureValues"), TEXT("buildableRatio"),
				TEXT("waterCoverage"), TEXT("rockCoverage"), TEXT("settlementMorphology"),
				TEXT("environmentalStory")})
			{
				TerrainObject->RemoveField(Field);
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* LegacyLocations = nullptr;
		if (GenuineLegacyRoot->TryGetArrayField(TEXT("locations"), LegacyLocations) &&
			LegacyLocations != nullptr)
		{
			for (const TSharedPtr<FJsonValue>& Value : *LegacyLocations)
			{
				const TSharedPtr<FJsonObject> LocationObject = Value->AsObject();
				if (LocationObject.IsValid())
				{
					LocationObject->RemoveField(TEXT("purposeTag"));
					LocationObject->RemoveField(TEXT("bPavedCourtyard"));
					LocationObject->RemoveField(TEXT("groundHeightCentimeters"));
					LocationObject->RemoveField(TEXT("groundSlopeDegrees"));
				}
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* LegacyDistricts = nullptr;
		if (GenuineLegacyRoot->TryGetArrayField(TEXT("districtAnchors"), LegacyDistricts) &&
			LegacyDistricts != nullptr)
		{
			for (const TSharedPtr<FJsonValue>& Value : *LegacyDistricts)
			{
				if (const TSharedPtr<FJsonObject> DistrictObject = Value->AsObject())
				{
					DistrictObject->RemoveField(TEXT("terrainAffinity"));
				}
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* LegacyRoutes = nullptr;
		if (GenuineLegacyRoot->TryGetArrayField(TEXT("routes"), LegacyRoutes) &&
			LegacyRoutes != nullptr)
		{
			for (const TSharedPtr<FJsonValue>& Value : *LegacyRoutes)
			{
				if (const TSharedPtr<FJsonObject> RouteObject = Value->AsObject())
				{
					RouteObject->RemoveField(TEXT("maximumGrade"));
				}
			}
		}
	}
	TestTrue(TEXT("Genuine legacy V2 shape serializes after removing additive fields"),
		SerializeJsonObject(GenuineLegacyRoot, LegacyRecipeJson));
	FResolvedWorldPlan ReplayedLegacyPlan;
	TestTrue(TEXT("Genuine legacy V2 recipe with omitted additive fields remains replayable"),
		FWorldDirectorJson::LoadResolvedWorldPlan(
			LegacyRecipeJson, ReplayedLegacyPlan, LegacyRecipeReport));
	TestEqual(TEXT("Legacy replay preserves its recipe version"), ReplayedLegacyPlan.Version, 2);
	TestTrue(TEXT("Legacy replay defaults omitted purpose tags safely"),
		!ReplayedLegacyPlan.Locations.IsEmpty() &&
		ReplayedLegacyPlan.Locations[0].PurposeTag.IsNone());

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
	FWorldDirectorSchemaMatchesStrictImportTest,
	"WorldDirector.Contract.SchemaMatchesStrictImport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldDirectorSchemaMatchesStrictImportTest::RunTest(const FString& Parameters)
{
	// The spec is imported with FJsonObjectConverter strict mode, which requires EVERY
	// UPROPERTY to be present. The schema handed to the model is therefore only honest
	// if each $def lists every struct field in both "properties" and "required".
	// When these drifted, the model omitted a field the schema called optional
	// (Resident.currentLocationId) and the whole run failed at integration with
	// "Missing JSON value named CurrentLocationId" -- unrecoverably, because a parse
	// failure reports path "$" and no targeted repair can address it.
	FString SchemaJson;
	if (!TestTrue(TEXT("world-director.schema.json loads"), LoadSchema(SchemaJson)))
	{
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SchemaJson);
	if (!TestTrue(TEXT("schema parses"), FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid()))
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* Defs = nullptr;
	if (!TestTrue(TEXT("schema exposes $defs"), Root->TryGetObjectField(TEXT("$defs"), Defs)))
	{
		return false;
	}

	struct FSchemaBinding
	{
		const TCHAR* DefName;
		UScriptStruct* Struct;
	};
	const FSchemaBinding Bindings[] = {
		{TEXT("WorldBrief"), FWorldBrief::StaticStruct()},
		{TEXT("TownTopology"), FTownTopology::StaticStruct()},
		{TEXT("TownTopologyEdge"), FTownTopologyEdge::StaticStruct()},
		{TEXT("WorldLocation"), FWorldLocation::StaticStruct()},
		{TEXT("Resident"), FResident::StaticStruct()},
		{TEXT("ResidentMemory"), FResidentMemory::StaticStruct()},
		{TEXT("Household"), FHousehold::StaticStruct()},
		{TEXT("Relationship"), FRelationship::StaticStruct()},
		{TEXT("Belief"), FBelief::StaticStruct()},
		{TEXT("WorldFact"), FWorldFact::StaticStruct()},
		{TEXT("WorldEvent"), FWorldEvent::StaticStruct()},
		{TEXT("Threat"), FThreat::StaticStruct()},
		{TEXT("ChangeProject"), FChangeProject::StaticStruct()},
		{TEXT("GeneratedWorldSpec"), FGeneratedWorldSpec::StaticStruct()}
	};

	for (const FSchemaBinding& Binding : Bindings)
	{
		if (Binding.Struct == nullptr)
		{
			AddError(FString::Printf(TEXT("%s has no reflected struct"), Binding.DefName));
			continue;
		}
		const TSharedPtr<FJsonObject>* Def = nullptr;
		if (!(*Defs)->TryGetObjectField(Binding.DefName, Def))
		{
			AddError(FString::Printf(TEXT("schema is missing $defs/%s"), Binding.DefName));
			continue;
		}

		TSet<FString> StructFields;
		for (TFieldIterator<FProperty> PropIt(Binding.Struct); PropIt; ++PropIt)
		{
			StructFields.Add(Binding.Struct->GetAuthoredNameForField(*PropIt));
		}

		TSet<FString> SchemaProperties;
		const TSharedPtr<FJsonObject>* Properties = nullptr;
		if ((*Def)->TryGetObjectField(TEXT("properties"), Properties))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Properties)->Values)
			{
				SchemaProperties.Add(Pair.Key);
			}
		}

		TSet<FString> SchemaRequired;
		const TArray<TSharedPtr<FJsonValue>>* Required = nullptr;
		if ((*Def)->TryGetArrayField(TEXT("required"), Required))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Required)
			{
				SchemaRequired.Add(Value->AsString());
			}
		}

		for (const FString& Field : StructFields)
		{
			if (!SchemaProperties.Contains(Field))
			{
				AddError(FString::Printf(
					TEXT("%s.%s exists on the struct but the schema never describes it, so the model cannot emit it."),
					Binding.DefName, *Field));
			}
			else if (!SchemaRequired.Contains(Field))
			{
				AddError(FString::Printf(
					TEXT("%s.%s is mandatory under strict import but the schema lists it as optional; ")
					TEXT("a response that omits it fails the whole run."),
					Binding.DefName, *Field));
			}
		}
		for (const FString& Property : SchemaProperties)
		{
			if (!StructFields.Contains(Property))
			{
				AddError(FString::Printf(
					TEXT("schema $defs/%s declares '%s', which no longer exists on the struct; ")
					TEXT("a response that includes it is rejected as an unknown field."),
					Binding.DefName, *Property));
			}
		}
	}
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
	auto HeightCorrelation = [](const TArray<int32>& A, const TArray<int32>& B)
	{
		if (A.Num() != B.Num() || A.IsEmpty())
		{
			return 1.0;
		}
		double MeanA = 0.0;
		double MeanB = 0.0;
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			MeanA += A[Index];
			MeanB += B[Index];
		}
		MeanA /= A.Num();
		MeanB /= B.Num();
		double Covariance = 0.0;
		double VarianceA = 0.0;
		double VarianceB = 0.0;
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			const double DeltaA = A[Index] - MeanA;
			const double DeltaB = B[Index] - MeanB;
			Covariance += DeltaA * DeltaB;
			VarianceA += DeltaA * DeltaA;
			VarianceB += DeltaB * DeltaB;
		}
		return Covariance / FMath::Sqrt(FMath::Max(1.0, VarianceA * VarianceB));
	};
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
		int32 PavedCourtyardCount = 0;
		for (const FResolvedLocationPlan& Location : Plan.Locations)
		{
			PavedCourtyardCount += Location.bPavedCourtyard ? 1 : 0;
		}
		TestEqual(*FString::Printf(TEXT("Seed %d keeps compact civic paving scarce"), Spec.Seed),
			PavedCourtyardCount, 2);
		bool bRouteClearanceValid = true;
		FString RouteClearanceFailure;
		for (const FResolvedRoutePlan& Route : Plan.Routes)
		{
			const FResolvedLocationPlan* From = Plan.Locations.FindByPredicate(
				[&](const FResolvedLocationPlan& Location)
				{
					return Location.LocationId == Route.FromLocationId;
				});
			const FResolvedLocationPlan* To = Plan.Locations.FindByPredicate(
				[&](const FResolvedLocationPlan& Location)
				{
					return Location.LocationId == Route.ToLocationId;
				});
			if (From == nullptr || To == nullptr || Route.ControlPoints.Num() < 2)
			{
				bRouteClearanceValid = false;
				RouteClearanceFailure = TEXT("missing semantic endpoint or control points");
				break;
			}
			const FVector2D RouteStart(Route.ControlPoints[0]);
			const FVector2D RouteEnd(Route.ControlPoints.Last());
			const float EndToSemanticDistance = FVector2D::Distance(
				RouteEnd, FVector2D(To->EntranceTransform.GetLocation()));
			const bool bEndsAtSemanticTo = EndToSemanticDistance <= 100.0f;
			const float PlotPadding = Route.WidthCentimeters * 0.66f + 100.0f;
			for (int32 SegmentIndex = 1;
				SegmentIndex < Route.ControlPoints.Num() && bRouteClearanceValid; ++SegmentIndex)
			{
				const FVector2D A(Route.ControlPoints[SegmentIndex - 1]);
				const FVector2D B(Route.ControlPoints[SegmentIndex]);
				const int32 SampleCount = FMath::Max(1,
					FMath::CeilToInt(FVector2D::Distance(A, B) / 100.0f));
				for (int32 SampleIndex = 0; SampleIndex <= SampleCount; ++SampleIndex)
				{
					const FVector2D Sample = FMath::Lerp(
						A, B, static_cast<float>(SampleIndex) / SampleCount);
					if (Plan.Terrain.WaterLevelCentimeters != INDEX_NONE &&
						FWorldDirectorPhysicalGenerator::SampleHeightCentimeters(
							Plan.Terrain, Sample) <= Plan.Terrain.WaterLevelCentimeters + 20.0f)
					{
						bRouteClearanceValid = false;
						RouteClearanceFailure = FString::Printf(
							TEXT("%s-%s enters water"), *Route.FromLocationId, *Route.ToLocationId);
						break;
					}
					for (const FResolvedLocationPlan& Location : Plan.Locations)
					{
						const bool bAllowedFromExit = Location.LocationId == From->LocationId &&
							FVector2D::Distance(Sample, RouteStart) <= 2400.0f;
						const bool bAllowedToEntry = bEndsAtSemanticTo &&
							Location.LocationId == To->LocationId &&
							FVector2D::Distance(Sample, RouteEnd) <= 2400.0f;
						if (!bAllowedFromExit && !bAllowedToEntry &&
							IsInsideExpandedLocation(Sample, Location, PlotPadding))
						{
							bRouteClearanceValid = false;
							RouteClearanceFailure = FString::Printf(
								TEXT("%s-%s cuts expanded plot %s at (%.0f,%.0f), endToSemantic=%.1f"),
								*Route.FromLocationId, *Route.ToLocationId, *Location.LocationId,
								Sample.X, Sample.Y, EndToSemanticDistance);
							break;
						}
					}
					if (!bRouteClearanceValid)
					{
						break;
					}
				}
			}
			if (!bRouteClearanceValid)
			{
				break;
			}
		}
		TestTrue(*FString::Printf(
			TEXT("Seed %d routes preserve post-smoothing plot/water clearance: %s"),
			Spec.Seed, RouteClearanceFailure.IsEmpty() ? TEXT("ok") : *RouteClearanceFailure),
			bRouteClearanceValid);
		WorldFingerprints.Add(Plan.WorldFingerprint);
		if (SeedIndex > 0)
		{
			const double TerrainCorrelation = HeightCorrelation(
				Previous.Terrain.HeightsCentimeters, Plan.Terrain.HeightsCentimeters);
			TestTrue(*FString::Printf(
				TEXT("Adjacent Basin seeds %d/%d create distinct macro landforms (correlation %.3f)"),
				Spec.Seed - 1, Spec.Seed, TerrainCorrelation), FMath::Abs(TerrainCorrelation) < 0.94);
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
	TSet<FName> ArchetypeStoryMotifs;
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
		TestTrue(*FString::Printf(TEXT("%s has visible macro relief"), Cases[CaseIndex].Intent),
			Plan.Terrain.MaximumHeightCentimeters - Plan.Terrain.MinimumHeightCentimeters >= 2500.0f);
		TestEqual(*FString::Printf(TEXT("%s moisture grid is complete"), Cases[CaseIndex].Intent),
			Plan.Terrain.MoistureValues.Num(), Plan.Terrain.HeightsCentimeters.Num());
		TestEqual(*FString::Printf(TEXT("%s blend grid is complete"), Cases[CaseIndex].Intent),
			Plan.Terrain.SurfaceBlendWeights.Num(), Plan.Terrain.HeightsCentimeters.Num() * 4);
		TSet<FName> TerrainAffinities;
		for (const FWorldDirectorDistrictAnchor& District : Plan.DistrictAnchors)
		{
			TerrainAffinities.Add(District.TerrainAffinity);
		}
		TestTrue(*FString::Printf(TEXT("%s has distinct civic and neighborhood terrain roles"), Cases[CaseIndex].Intent),
			TerrainAffinities.Num() >= 2);
		int32 StoryInstanceCount = 0;
		for (const FWorldDirectorDressingInstance& Instance : Plan.Dressing)
		{
			if (Instance.BiomeTag.ToString().StartsWith(TEXT("Story.")))
			{
				++StoryInstanceCount;
				ArchetypeStoryMotifs.Add(Instance.BiomeTag);
			}
		}
		TestTrue(*FString::Printf(TEXT("%s has a visible morphology-specific story cluster"), Cases[CaseIndex].Intent),
			StoryInstanceCount >= 4);

		int32 RenderedWaterCells = 0;
		int32 ClassifiedWaterSamples = 0;
		int32 ClassifiedRockSamples = 0;
		for (const uint8 SurfaceType : Plan.Terrain.SurfaceTypes)
		{
			ClassifiedWaterSamples += SurfaceType == static_cast<uint8>(EWorldDirectorSurfaceType::Water);
			ClassifiedRockSamples += SurfaceType == static_cast<uint8>(EWorldDirectorSurfaceType::Rock);
		}
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
		const float SampleCount = static_cast<float>(FMath::Max(1, Plan.Terrain.SurfaceTypes.Num()));
		TestTrue(*FString::Printf(TEXT("%s water coverage matches classification"), Cases[CaseIndex].Intent),
			FMath::IsNearlyEqual(
				Plan.Terrain.WaterCoverage,
				static_cast<float>(ClassifiedWaterSamples) / SampleCount,
				KINDA_SMALL_NUMBER));
		TestTrue(*FString::Printf(TEXT("%s rock coverage matches classification"), Cases[CaseIndex].Intent),
			FMath::IsNearlyEqual(
				Plan.Terrain.RockCoverage,
				static_cast<float>(ClassifiedRockSamples) / SampleCount,
				KINDA_SMALL_NUMBER));

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
			const FProcMeshSection* TerrainSection = Town->TerrainMesh->GetProcMeshSection(0);
			if (TestNotNull(*FString::Printf(TEXT("%s emits a continuous terrain section"),
				Cases[CaseIndex].Intent), TerrainSection))
			{
				TestEqual(*FString::Printf(TEXT("%s shares one vertex per terrain sample"),
					Cases[CaseIndex].Intent), TerrainSection->ProcVertexBuffer.Num(),
					Plan.Terrain.Resolution * Plan.Terrain.Resolution);
				TestEqual(*FString::Printf(TEXT("%s emits a complete collision triangle grid"),
					Cases[CaseIndex].Intent), TerrainSection->ProcIndexBuffer.Num(),
					(Plan.Terrain.Resolution - 1) * (Plan.Terrain.Resolution - 1) * 6);
			}
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
	TestEqual(TEXT("Every terrain archetype emits its own environmental-story motif"),
		ArchetypeStoryMotifs.Num(), static_cast<int32>(UE_ARRAY_COUNT(Cases)));
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
		const FProcMeshSection* RouteSection = Town->RouteMesh
			? Town->RouteMesh->GetProcMeshSection(0) : nullptr;
		if (TestNotNull(TEXT("Feathered route renderer emits visible geometry"), RouteSection))
		{
			TestTrue(TEXT("Route renderer emits complete triangles"),
				RouteSection->ProcVertexBuffer.Num() > 0 &&
				RouteSection->ProcIndexBuffer.Num() >= 3 &&
				RouteSection->ProcIndexBuffer.Num() % 3 == 0);
			int32 BaseRouteVertexCount = 0;
			int32 BaseRouteIndexCount = 0;
			for (const FResolvedRoutePlan& Route : Plan.Routes)
			{
				const int32 RenderSampleCount = CountRenderedRouteSamples(Route.ControlPoints);
				BaseRouteVertexCount += RenderSampleCount * 7;
				BaseRouteIndexCount += FMath::Max(0, RenderSampleCount - 1) * 36;
			}
			const TArray<FVector2D> ExpectedJunctionCenters =
				BuildExpectedRouteJunctionCenters(Plan);
			TestEqual(TEXT("Route renderer emits one spatially proven 33-vertex patch per physical junction"),
				RouteSection->ProcVertexBuffer.Num(),
				BaseRouteVertexCount + ExpectedJunctionCenters.Num() * 33);
			TestEqual(TEXT("Route renderer emits exact ribbon and junction topology"),
				RouteSection->ProcIndexBuffer.Num(),
				BaseRouteIndexCount + ExpectedJunctionCenters.Num() * 144);
			for (int32 JunctionIndex = 0;
				JunctionIndex < ExpectedJunctionCenters.Num(); ++JunctionIndex)
			{
				const int32 CenterVertexIndex = BaseRouteVertexCount + JunctionIndex * 33;
				if (RouteSection->ProcVertexBuffer.IsValidIndex(CenterVertexIndex))
				{
					TestTrue(*FString::Printf(TEXT("Junction %d center matches its physical branch endpoint"),
						JunctionIndex), FVector2D::Distance(
							FVector2D(RouteSection->ProcVertexBuffer[CenterVertexIndex].Position),
							ExpectedJunctionCenters[JunctionIndex]) <= 2.0f);
				}
			}
			float MinimumRouteGap = MAX_flt;
			float MaximumRouteGap = -MAX_flt;
			for (const FProcMeshVertex& Vertex : RouteSection->ProcVertexBuffer)
			{
				const float Gap = Vertex.Position.Z -
					TestRenderedTerrainHeight(Plan.Terrain, FVector2D(Vertex.Position));
				MinimumRouteGap = FMath::Min(MinimumRouteGap, Gap);
				MaximumRouteGap = FMath::Max(MaximumRouteGap, Gap);
			}
			TestTrue(TEXT("Non-colliding route overlay stays within four centimeters of authoritative terrain"),
				MinimumRouteGap >= 1.5f && MaximumRouteGap <= 4.0f);
			float MinimumRouteTriangleGap = MAX_flt;
			for (int32 TriangleIndex = 0;
				TriangleIndex + 2 < RouteSection->ProcIndexBuffer.Num(); TriangleIndex += 3)
			{
				const FVector& A = RouteSection->ProcVertexBuffer[
					RouteSection->ProcIndexBuffer[TriangleIndex]].Position;
				const FVector& B = RouteSection->ProcVertexBuffer[
					RouteSection->ProcIndexBuffer[TriangleIndex + 1]].Position;
				const FVector& C = RouteSection->ProcVertexBuffer[
					RouteSection->ProcIndexBuffer[TriangleIndex + 2]].Position;
				const FVector Center = (A + B + C) / 3.0f;
				MinimumRouteTriangleGap = FMath::Min(
					MinimumRouteTriangleGap,
					Center.Z - TestRenderedTerrainHeight(Plan.Terrain, FVector2D(Center)));
			}
			TestTrue(TEXT("Every route triangle remains visibly above collision terrain between vertices"),
				MinimumRouteTriangleGap >= 0.5f);
		}
		TestEqual(TEXT("Route overlay has no collision"),
			Town->RouteMesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		TestFalse(TEXT("Route overlay never affects navigation"),
			Town->RouteMesh->CanEverAffectNavigation());
		const FProcMeshSection* PavingSection = Town->PavingMesh
			? Town->PavingMesh->GetProcMeshSection(0) : nullptr;
		if (TestNotNull(TEXT("Semantic civic courts emit paving geometry"), PavingSection))
		{
			int32 ExpectedCourtyardCount = 0;
			for (const FResolvedLocationPlan& Location : Plan.Locations)
			{
				ExpectedCourtyardCount += Location.bPavedCourtyard ? 1 : 0;
			}
			TestTrue(TEXT("Paving renderer emits terrain-conforming tessellation for every selected court"),
				PavingSection->ProcVertexBuffer.Num() >= ExpectedCourtyardCount * 17);
			TestTrue(TEXT("Paving renderer emits complete triangles"),
				PavingSection->ProcIndexBuffer.Num() >= ExpectedCourtyardCount * 24 * 3 &&
				PavingSection->ProcIndexBuffer.Num() % 3 == 0);
			float MinimumPavingGap = MAX_flt;
			float MaximumPavingGap = -MAX_flt;
			for (const FProcMeshVertex& Vertex : PavingSection->ProcVertexBuffer)
			{
				const float Gap = Vertex.Position.Z -
					TestRenderedTerrainHeight(Plan.Terrain, FVector2D(Vertex.Position));
				MinimumPavingGap = FMath::Min(MinimumPavingGap, Gap);
				MaximumPavingGap = FMath::Max(MaximumPavingGap, Gap);
			}
			TestTrue(TEXT("Non-colliding paving overlay stays within five centimeters of authoritative terrain"),
				MinimumPavingGap >= 3.5f && MaximumPavingGap <= 5.5f);
			float MinimumPavingTriangleGap = MAX_flt;
			for (int32 TriangleIndex = 0;
				TriangleIndex + 2 < PavingSection->ProcIndexBuffer.Num(); TriangleIndex += 3)
			{
				const FVector& A = PavingSection->ProcVertexBuffer[
					PavingSection->ProcIndexBuffer[TriangleIndex]].Position;
				const FVector& B = PavingSection->ProcVertexBuffer[
					PavingSection->ProcIndexBuffer[TriangleIndex + 1]].Position;
				const FVector& C = PavingSection->ProcVertexBuffer[
					PavingSection->ProcIndexBuffer[TriangleIndex + 2]].Position;
				const FVector Center = (A + B + C) / 3.0f;
				MinimumPavingTriangleGap = FMath::Min(
					MinimumPavingTriangleGap,
					Center.Z - TestRenderedTerrainHeight(Plan.Terrain, FVector2D(Center)));
			}
			TestTrue(TEXT("Every paving triangle remains visibly above collision terrain between vertices"),
				MinimumPavingTriangleGap >= 0.5f);
		}
		TestEqual(TEXT("Paving overlay has no collision"),
			Town->PavingMesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		TestFalse(TEXT("Paving overlay never affects navigation"),
			Town->PavingMesh->CanEverAffectNavigation());
		const FProcMeshSection* HorizonSection = Town->HorizonTerrainMesh
			? Town->HorizonTerrainMesh->GetProcMeshSection(0) : nullptr;
		if (TestNotNull(TEXT("A non-colliding visual terrain continuation spawns"), HorizonSection))
		{
			TestTrue(TEXT("Visual terrain continuation contains render geometry"),
				HorizonSection->ProcVertexBuffer.Num() > 0 && HorizonSection->ProcIndexBuffer.Num() > 0);
		}
		TestEqual(TEXT("Visual terrain continuation has no collision"),
			Town->HorizonTerrainMesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		TestFalse(TEXT("Visual terrain continuation never affects navigation"),
			Town->HorizonTerrainMesh->CanEverAffectNavigation());
		TestTrue(TEXT("Visual terrain continuation extends beyond playable terrain bounds"),
			Town->HorizonTerrainMesh->Bounds.BoxExtent.X > Town->TerrainMesh->Bounds.BoxExtent.X * 2.0f);
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
	TestNotNull(TEXT("Four-layer terrain blend material loads"), Profile->TerrainBlendMaterial.TryLoad());
	TestNotNull(TEXT("Dedicated civic paving material loads"), Profile->PavingMaterial.TryLoad());
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
	TestFalse(TEXT("Runtime biome dressing palette is populated"), Profile->DressingAssets.IsEmpty());
	TSet<FName> PlacementTags;
	for (int32 AssetIndex = 0; AssetIndex < Profile->DressingAssets.Num(); ++AssetIndex)
	{
		const FWorldEnvironmentDressingAsset& Asset = Profile->DressingAssets[AssetIndex];
		PlacementTags.Add(Asset.PlacementTag);
		TestNotNull(*FString::Printf(TEXT("Biome dressing asset %d loads"), AssetIndex),
			Asset.MeshAsset.TryLoad());
		TestTrue(*FString::Printf(TEXT("Biome dressing asset %d has positive selection weight"), AssetIndex),
			Asset.Weight > 0.0f);
		TestTrue(*FString::Printf(TEXT("Biome dressing asset %d has a valid scale range"), AssetIndex),
			Asset.MinimumScale > 0.0f && Asset.MaximumScale >= Asset.MinimumScale);
	}
	for (const FName RequiredPlacementTag : {
		FName(TEXT("Dressing.Canopy")),
		FName(TEXT("Dressing.GroundCover")),
		FName(TEXT("Dressing.Rock")),
		FName(TEXT("Dressing.Deadwood")),
		FName(TEXT("Dressing.FarmFence")),
		FName(TEXT("Dressing.FarmAccent")),
		FName(TEXT("Dressing.Wayfinding")),
		FName(TEXT("Dressing.Roadside")),
		FName(TEXT("Dressing.CivicWell")),
		FName(TEXT("Dressing.CivicSeat")),
		FName(TEXT("Dressing.GuildBanner")),
		FName(TEXT("Dressing.Transport")),
		FName(TEXT("Dressing.HomeUtility")),
		FName(TEXT("Dressing.InnYard")),
		FName(TEXT("Dressing.CommunalFire")),
		FName(TEXT("Dressing.SettlementClutter")),
		FName(TEXT("Dressing.MarketStall"))})
	{
		TestTrue(*FString::Printf(TEXT("Biome palette covers %s"), *RequiredPlacementTag.ToString()),
			PlacementTags.Contains(RequiredPlacementTag));
	}
	return !HasAnyErrors();
}

#endif
