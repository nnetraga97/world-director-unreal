#include "WorldDirectorJson.h"

#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"

namespace
{
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
	if (!RootObject->TryGetNumberField(TEXT("version"), Version) || static_cast<int32>(Version) != 2)
	{
		OutParseReport.AddError(TEXT("json.recipe_version"), TEXT("$.version"),
			TEXT("Only V2 resolved physical recipes are replayable. V1 artifacts contain semantic data only."));
		return false;
	}
	if (!ValidateObjectKeys(RootObject, FResolvedWorldPlan::StaticStruct(), TEXT("$"), OutParseReport))
	{
		return false;
	}
	FText FailureReason;
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &OutPlan, 0, 0, true, &FailureReason))
	{
		OutParseReport.AddError(TEXT("json.parse_or_shape"), TEXT("$"), FailureReason.ToString());
		return false;
	}
	if (OutPlan.WorldFingerprint.IsEmpty() || OutPlan.Terrain.HeightsCentimeters.IsEmpty())
	{
		OutParseReport.AddError(TEXT("json.recipe_incomplete"), TEXT("$"),
			TEXT("V2 physical recipe must persist terrain samples and a world fingerprint."));
		return false;
	}
	return true;
}

bool FWorldDirectorJson::SaveResolvedWorldPlan(
	const FResolvedWorldPlan& Plan,
	FString& OutJson,
	FValidationReport& OutSerializationReport)
{
	OutSerializationReport = FValidationReport();
	if (Plan.Version != 2 || Plan.WorldFingerprint.IsEmpty() ||
		!FJsonObjectConverter::UStructToJsonObjectString(Plan, OutJson))
	{
		OutSerializationReport.AddError(TEXT("json.serialize_recipe"), TEXT("$"),
			TEXT("Only complete V2 physical recipes can be persisted."));
		return false;
	}
	return true;
}
