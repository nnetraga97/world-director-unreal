#include "WorldDirectorJson.h"

#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "WorldDirectorPhysicalGenerator.h"

namespace
{
void AppendWireInt32(TArray<uint8>& Bytes, const int32 Value)
{
	const uint32 Wire = static_cast<uint32>(Value);
	Bytes.Add(static_cast<uint8>(Wire));
	Bytes.Add(static_cast<uint8>(Wire >> 8));
	Bytes.Add(static_cast<uint8>(Wire >> 16));
	Bytes.Add(static_cast<uint8>(Wire >> 24));
}

void AppendWireString(TArray<uint8>& Bytes, const FString& Value)
{
	FTCHARToUTF8 Utf8(*Value);
	AppendWireInt32(Bytes, Utf8.Length());
	Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
}

FString RecomputeLayoutFingerprint(const FResolvedWorldPlan& Plan)
{
	TArray<uint8> Bytes;
	for (const FResolvedLocationPlan& Location : Plan.Locations)
	{
		AppendWireString(Bytes, Location.LocationId);
		AppendWireString(Bytes, Location.PurposeTag.ToString());
		AppendWireInt32(Bytes, Location.bPavedCourtyard ? 1 : 0);
		AppendWireString(Bytes, Location.DistrictId);
		AppendWireString(Bytes, Location.ShellAsset.ToString());
		AppendWireString(Bytes, Location.InteriorAsset.ToString());
		AppendWireInt32(Bytes, FMath::RoundToInt(Location.Transform.GetLocation().X));
		AppendWireInt32(Bytes, FMath::RoundToInt(Location.Transform.GetLocation().Y));
		AppendWireInt32(Bytes, FMath::RoundToInt(Location.Transform.GetLocation().Z));
		AppendWireInt32(Bytes, FMath::RoundToInt(Location.Transform.Rotator().Yaw * 100.0f));
		AppendWireInt32(Bytes, FMath::RoundToInt(Location.FootprintSize.X));
		AppendWireInt32(Bytes, FMath::RoundToInt(Location.FootprintSize.Y));
	}
	return FWorldDirectorPhysicalGenerator::FingerprintBytes(Bytes);
}

FString RecomputeRouteFingerprint(const FResolvedWorldPlan& Plan)
{
	TArray<uint8> Bytes;
	for (const FResolvedRoutePlan& Route : Plan.Routes)
	{
		AppendWireString(Bytes, Route.FromLocationId);
		AppendWireString(Bytes, Route.ToLocationId);
		AppendWireString(Bytes, Route.RouteType.ToString());
		AppendWireInt32(Bytes, FMath::RoundToInt(Route.WidthCentimeters));
		for (const FVector& Point : Route.ControlPoints)
		{
			AppendWireInt32(Bytes, FMath::RoundToInt(Point.X));
			AppendWireInt32(Bytes, FMath::RoundToInt(Point.Y));
			AppendWireInt32(Bytes, FMath::RoundToInt(Point.Z));
		}
	}
	return FWorldDirectorPhysicalGenerator::FingerprintBytes(Bytes);
}

FString RecomputeDressingFingerprint(const FResolvedWorldPlan& Plan)
{
	TArray<uint8> Bytes;
	for (const FWorldDirectorDressingInstance& Instance : Plan.Dressing)
	{
		AppendWireString(Bytes, Instance.MeshAsset.ToString());
		AppendWireString(Bytes, Instance.BiomeTag.ToString());
		AppendWireInt32(Bytes, FMath::RoundToInt(Instance.Transform.GetLocation().X));
		AppendWireInt32(Bytes, FMath::RoundToInt(Instance.Transform.GetLocation().Y));
		AppendWireInt32(Bytes, FMath::RoundToInt(Instance.Transform.GetLocation().Z));
		AppendWireInt32(Bytes, FMath::RoundToInt(Instance.Transform.Rotator().Pitch * 100.0f));
		AppendWireInt32(Bytes, FMath::RoundToInt(Instance.Transform.Rotator().Yaw * 100.0f));
		AppendWireInt32(Bytes, FMath::RoundToInt(Instance.Transform.Rotator().Roll * 100.0f));
		AppendWireInt32(Bytes, FMath::RoundToInt(Instance.Transform.GetScale3D().X * 1000.0f));
	}
	return FWorldDirectorPhysicalGenerator::FingerprintBytes(Bytes);
}

bool HasCurrentLocationFingerprintFields(const TSharedPtr<FJsonObject>& RootObject)
{
	const TArray<TSharedPtr<FJsonValue>>* LocationValues = nullptr;
	if (!RootObject.IsValid() ||
		!RootObject->TryGetArrayField(TEXT("locations"), LocationValues) ||
		LocationValues == nullptr)
	{
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : *LocationValues)
	{
		const TSharedPtr<FJsonObject> LocationObject = Value.IsValid() && Value->Type == EJson::Object
			? Value->AsObject() : nullptr;
		if (!LocationObject.IsValid() ||
			!LocationObject->HasField(TEXT("purposeTag")) ||
			!LocationObject->HasField(TEXT("bPavedCourtyard")))
		{
			return false;
		}
	}
	return true;
}

bool ValidateResolvedComponentFingerprints(
	const FResolvedWorldPlan& Plan,
	const TSharedPtr<FJsonObject>& RootObject,
	FValidationReport& Report)
{
	// V2 remains replay-only compatibility data. Its historic layout digest did
	// not include the purpose/civic fields added to the current resolved struct.
	if (Plan.Version == 2)
	{
		return true;
	}

	auto RequireMatch = [&](const FString& Stored, const FString& Recomputed,
		const FString& Path, const TCHAR* Label)
	{
		if (Stored.IsEmpty() || Recomputed.IsEmpty() ||
			!Stored.Equals(Recomputed, ESearchCase::IgnoreCase))
		{
			Report.AddError(
				TEXT("json.recipe_fingerprint_mismatch"), Path,
				FString::Printf(TEXT("V3 %s data does not match its persisted fingerprint."), Label));
		}
	};

	if (HasCurrentLocationFingerprintFields(RootObject))
	{
		RequireMatch(Plan.LayoutFingerprint, RecomputeLayoutFingerprint(Plan),
			TEXT("$.layoutFingerprint"), TEXT("layout"));
	}
	else
	{
		Report.AddWarning(
			TEXT("json.recipe_legacy_defaults"), TEXT("$.locations"),
			TEXT("Legacy V3 locations omit purpose/civic fields; safe defaults were applied and the historic layout digest was retained."));
	}
	RequireMatch(Plan.RouteFingerprint, RecomputeRouteFingerprint(Plan),
		TEXT("$.routeFingerprint"), TEXT("route"));
	RequireMatch(Plan.DressingFingerprint, RecomputeDressingFingerprint(Plan),
		TEXT("$.dressingFingerprint"), TEXT("dressing"));
	return Report.bValid;
}

const FProperty* FindWireProperty(const UStruct* Struct, const FString& WireName)
{
	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		if (It->GetName().Equals(WireName, ESearchCase::IgnoreCase))
		{
			return *It;
		}
	}
	return nullptr;
}

bool ValidateObjectKeys(
	const TSharedPtr<FJsonObject>& Object,
	const UStruct* Struct,
	const FString& Path,
	FValidationReport& Report)
{
	if (!Object.IsValid() || Struct == nullptr)
	{
		return false;
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object->Values)
	{
		const FProperty* Property = FindWireProperty(Struct, Field.Key);
		const FString FieldPath = Path + TEXT(".") + Field.Key;
		if (Property == nullptr)
		{
			Report.AddError(
				TEXT("json.unknown_field"), FieldPath,
				FString::Printf(TEXT("Field '%s' is not part of the canonical schema."), *Field.Key));
			continue;
		}
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (Field.Value.IsValid() && Field.Value->Type == EJson::Object)
			{
				ValidateObjectKeys(Field.Value->AsObject(), StructProperty->Struct, FieldPath, Report);
			}
			continue;
		}
		const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property);
		const FStructProperty* InnerStruct = ArrayProperty
			? CastField<FStructProperty>(ArrayProperty->Inner) : nullptr;
		if (InnerStruct != nullptr && Field.Value.IsValid() && Field.Value->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Values = Field.Value->AsArray();
			for (int32 Index = 0; Index < Values.Num(); ++Index)
			{
				if (Values[Index].IsValid() && Values[Index]->Type == EJson::Object)
				{
					ValidateObjectKeys(
						Values[Index]->AsObject(), InnerStruct->Struct,
						FString::Printf(TEXT("%s[%d]"), *FieldPath, Index), Report);
				}
			}
		}
	}
	return Report.bValid;
}

bool ValidateResolvedRecipePayload(
	const FResolvedWorldPlan& Plan,
	FValidationReport& Report)
{
	if (Plan.Version != 2 && Plan.Version != 3)
	{
		Report.AddError(
			TEXT("json.recipe_version"), TEXT("$.version"),
			TEXT("Only legacy V2 and terrain-rich V3 resolved physical recipes are replayable. V1 artifacts contain semantic data only."));
		return false;
	}
	if (Plan.WorldFingerprint.IsEmpty() || Plan.Terrain.HeightsCentimeters.IsEmpty())
	{
		Report.AddError(
			TEXT("json.recipe_incomplete"), TEXT("$"),
			FString::Printf(
				TEXT("V%d physical recipe must persist terrain samples and a world fingerprint."),
				Plan.Version));
		return false;
	}

	// V2 is a supported legacy replay format. Its original contract predates
	// the terrain blend, moisture, and coverage fields introduced by V3.
	if (Plan.Version == 2)
	{
		return true;
	}

	const int64 ExpectedSampleCount =
		static_cast<int64>(Plan.Terrain.Resolution) * Plan.Terrain.Resolution;
	const bool bGridShapeValid =
		Plan.Terrain.Resolution > 1 &&
		Plan.Terrain.ExtentCentimeters > 0 &&
		ExpectedSampleCount <= MAX_int32 &&
		Plan.Terrain.HeightsCentimeters.Num() == ExpectedSampleCount &&
		Plan.Terrain.SurfaceTypes.Num() == ExpectedSampleCount &&
		Plan.Terrain.MoistureValues.Num() == ExpectedSampleCount &&
		Plan.Terrain.SurfaceBlendWeights.Num() == ExpectedSampleCount * 4;
	if (!bGridShapeValid)
	{
		Report.AddError(
			TEXT("json.recipe_incomplete"), TEXT("$.terrain"),
			TEXT("V3 terrain must persist one height, surface, and moisture value plus four blend weights per grid sample."));
		return false;
	}
	if (Plan.Terrain.HeightFingerprint.IsEmpty() || Plan.Terrain.SurfaceFingerprint.IsEmpty())
	{
		Report.AddError(
			TEXT("json.recipe_incomplete"), TEXT("$.terrain"),
			TEXT("V3 terrain must persist both height and surface fingerprints."));
		return false;
	}
	TArray<uint8> HeightBytes;
	HeightBytes.Reserve((Plan.Terrain.HeightsCentimeters.Num() + 2) * sizeof(int32));
	AppendWireInt32(HeightBytes, Plan.Terrain.Resolution);
	AppendWireInt32(HeightBytes, Plan.Terrain.ExtentCentimeters);
	for (const int32 Height : Plan.Terrain.HeightsCentimeters)
	{
		AppendWireInt32(HeightBytes, Height);
	}
	const FString RecomputedHeightFingerprint =
		FWorldDirectorPhysicalGenerator::FingerprintBytes(HeightBytes);
	TArray<uint8> SurfaceBytes = Plan.Terrain.SurfaceTypes;
	SurfaceBytes.Append(Plan.Terrain.SurfaceBlendWeights);
	SurfaceBytes.Append(Plan.Terrain.MoistureValues);
	for (const FWorldDirectorFarmParcel& Parcel : Plan.Terrain.FarmParcels)
	{
		AppendWireString(SurfaceBytes, Parcel.ParcelId);
		AppendWireInt32(SurfaceBytes, FMath::RoundToInt(Parcel.Center.X));
		AppendWireInt32(SurfaceBytes, FMath::RoundToInt(Parcel.Center.Y));
		AppendWireInt32(SurfaceBytes, FMath::RoundToInt(Parcel.YawDegrees * 100.0f));
		AppendWireInt32(SurfaceBytes, FMath::RoundToInt(Parcel.GatePosition.X));
		AppendWireInt32(SurfaceBytes, FMath::RoundToInt(Parcel.GatePosition.Y));
		AppendWireInt32(SurfaceBytes, Parcel.BoundaryPoints.Num());
		for (const FVector2D& BoundaryPoint : Parcel.BoundaryPoints)
		{
			AppendWireInt32(SurfaceBytes, FMath::RoundToInt(BoundaryPoint.X));
			AppendWireInt32(SurfaceBytes, FMath::RoundToInt(BoundaryPoint.Y));
		}
	}
	const FString RecomputedSurfaceFingerprint =
		FWorldDirectorPhysicalGenerator::FingerprintBytes(SurfaceBytes);
	if (RecomputedHeightFingerprint.IsEmpty() || RecomputedSurfaceFingerprint.IsEmpty() ||
		!Plan.Terrain.HeightFingerprint.Equals(RecomputedHeightFingerprint, ESearchCase::IgnoreCase) ||
		!Plan.Terrain.SurfaceFingerprint.Equals(RecomputedSurfaceFingerprint, ESearchCase::IgnoreCase))
	{
		Report.AddError(
			TEXT("json.recipe_fingerprint_mismatch"), TEXT("$.terrain"),
			TEXT("V3 terrain payload does not match its persisted height or surface fingerprint."));
		return false;
	}
	for (int32 SampleIndex = 0; SampleIndex < ExpectedSampleCount; ++SampleIndex)
	{
		const int32 WeightOffset = SampleIndex * 4;
		const int32 WeightSum =
			Plan.Terrain.SurfaceBlendWeights[WeightOffset] +
			Plan.Terrain.SurfaceBlendWeights[WeightOffset + 1] +
			Plan.Terrain.SurfaceBlendWeights[WeightOffset + 2] +
			Plan.Terrain.SurfaceBlendWeights[WeightOffset + 3];
		if (WeightSum != 255 ||
			Plan.Terrain.SurfaceTypes[SampleIndex] > static_cast<uint8>(EWorldDirectorSurfaceType::Water))
		{
			Report.AddError(
				TEXT("json.recipe_surface_invalid"), TEXT("$.terrain.surfaceBlendWeights"),
				TEXT("V3 terrain contains an invalid surface class or non-normalized blend weights."));
			return false;
		}
	}
	const bool bCoverageValid =
		FMath::IsFinite(Plan.Terrain.BuildableRatio) &&
		FMath::IsFinite(Plan.Terrain.WaterCoverage) &&
		FMath::IsFinite(Plan.Terrain.RockCoverage) &&
		Plan.Terrain.BuildableRatio >= 0.0f && Plan.Terrain.BuildableRatio <= 1.0f &&
		Plan.Terrain.WaterCoverage >= 0.0f && Plan.Terrain.WaterCoverage <= 1.0f &&
		Plan.Terrain.RockCoverage >= 0.0f && Plan.Terrain.RockCoverage <= 1.0f;
	if (!bCoverageValid)
	{
		Report.AddError(
			TEXT("json.recipe_incomplete"), TEXT("$.terrain"),
			TEXT("V3 terrain coverage metrics must be finite normalized ratios."));
		return false;
	}
	return true;
}
}

bool FWorldDirectorJson::LoadGeneratedWorldSpec(
	const FString& Json,
	FGeneratedWorldSpec& OutSpec,
	FValidationReport& OutParseReport)
{
	OutSpec = FGeneratedWorldSpec();
	OutParseReport = FValidationReport();
	TSharedPtr<FJsonObject> RootObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		OutParseReport.AddError(
			TEXT("json.parse_or_shape"), TEXT("$"), TEXT("Input is not a JSON object."));
		return false;
	}
	if (!ValidateObjectKeys(
		RootObject, FGeneratedWorldSpec::StaticStruct(), TEXT("$"), OutParseReport))
	{
		return false;
	}
	FText FailureReason;
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(
			Json,
			&OutSpec,
			0,
			0,
			true,
			&FailureReason))
	{
		OutParseReport.AddError(
			TEXT("json.parse_or_shape"),
			TEXT("$"),
			FailureReason.ToString());
		return false;
	}
	return true;
}

bool FWorldDirectorJson::SaveGeneratedWorldSpec(
	const FGeneratedWorldSpec& Spec,
	FString& OutJson,
	FValidationReport& OutSerializationReport)
{
	OutSerializationReport = FValidationReport();
	if (!FJsonObjectConverter::UStructToJsonObjectString(Spec, OutJson))
	{
		OutSerializationReport.AddError(
			TEXT("json.serialize"),
			TEXT("$"),
			TEXT("FJsonObjectConverter could not serialize GeneratedWorldSpec."));
		return false;
	}
	return true;
}

bool FWorldDirectorJson::LoadResolvedWorldPlan(
	const FString& Json,
	FResolvedWorldPlan& OutPlan,
	FValidationReport& OutParseReport)
{
	OutPlan = FResolvedWorldPlan();
	OutParseReport = FValidationReport();
	TSharedPtr<FJsonObject> RootObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		OutParseReport.AddError(TEXT("json.parse_or_shape"), TEXT("$"), TEXT("Resolved recipe is not a JSON object."));
		return false;
	}
	double Version = 0.0;
	if (!RootObject->TryGetNumberField(TEXT("version"), Version))
	{
		OutParseReport.AddError(TEXT("json.recipe_version"), TEXT("$.version"),
			TEXT("Resolved physical recipe version is required."));
		return false;
	}
	const bool bIsV2 = Version == 2.0;
	const bool bIsV3 = Version == 3.0;
	if (!FMath::IsFinite(Version) || (!bIsV2 && !bIsV3))
	{
		OutParseReport.AddError(TEXT("json.recipe_version"), TEXT("$.version"),
			TEXT("Only legacy V2 and terrain-rich V3 resolved physical recipes are replayable. V1 artifacts contain semantic data only."));
		return false;
	}
	if (!ValidateObjectKeys(RootObject, FResolvedWorldPlan::StaticStruct(), TEXT("$"), OutParseReport))
	{
		return false;
	}
	FText FailureReason;
	// Resolved recipes are a versioned replay artifact. Unknown fields remain an
	// error through ValidateObjectKeys above, while missing additive fields must
	// retain their reflected defaults so genuine V2 and early V3 saves survive.
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &OutPlan, 0, 0, false, &FailureReason))
	{
		OutParseReport.AddError(TEXT("json.parse_or_shape"), TEXT("$"), FailureReason.ToString());
		return false;
	}
	return ValidateResolvedRecipePayload(OutPlan, OutParseReport) &&
		ValidateResolvedComponentFingerprints(OutPlan, RootObject, OutParseReport);
}

bool FWorldDirectorJson::SaveResolvedWorldPlan(
	const FResolvedWorldPlan& Plan,
	FString& OutJson,
	FValidationReport& OutSerializationReport)
{
	OutSerializationReport = FValidationReport();
	if (!ValidateResolvedRecipePayload(Plan, OutSerializationReport))
	{
		return false;
	}
	if (!FJsonObjectConverter::UStructToJsonObjectString(Plan, OutJson))
	{
		OutSerializationReport.AddError(TEXT("json.serialize_recipe"), TEXT("$"),
			TEXT("FJsonObjectConverter could not serialize the resolved physical recipe."));
		return false;
	}
	return true;
}
