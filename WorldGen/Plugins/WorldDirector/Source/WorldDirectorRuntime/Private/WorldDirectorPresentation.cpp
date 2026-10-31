#include "WorldDirectorPresentation.h"

#include "Dom/JsonObject.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "WorldDirectorRuntime.h"

const TArray<FWorldDirectorPresentation::FField>& FWorldDirectorPresentation::Fields()
{
	// The first option of each field is its default and matches FWorldPresentation.
	static const TArray<FField> Table = {
		{TEXT("fortification"), &FWorldPresentation::Fortification, {
			{TEXT("open"), TEXT("No perimeter; fields and roads run straight into town.")},
			{TEXT("fenced"), TEXT("Low timber fence ring around the houses.")},
			{TEXT("palisade"), TEXT("Tall timber palisade with gatehouses where roads enter.")},
			{TEXT("stockade"), TEXT("Palisade plus watchtowers, spiked barricades and banners at the gates.")}}},
		{TEXT("waterLife"), &FWorldPresentation::WaterLife, {
			{TEXT("none"), TEXT("No boats or piers.")},
			{TEXT("fishing"), TEXT("Two timber piers with rowboats; bridges where roads cross water. Needs coast, valley or marsh.")},
			{TEXT("harbor"), TEXT("Four piers with moored boats, crates and barrels; bridges where roads cross water. Needs coast, valley or marsh.")}}},
		{TEXT("justice"), &FWorldPresentation::Justice, {
			{TEXT("lenient"), TEXT("No public punishment.")},
			{TEXT("stern"), TEXT("A gallows beside the seat of authority.")},
			{TEXT("grim"), TEXT("A gallows and coffins by the seat of authority, and a guillotine by the landmark.")}}},
		{TEXT("fire"), &FWorldPresentation::Fire, {
			{TEXT("hearths"), TEXT("Cookfires at homes only.")},
			{TEXT("braziers"), TEXT("Lit braziers flank the landmark, the seat of authority and the gates.")},
			{TEXT("vigil"), TEXT("Braziers along the main streets and watchfires in the square, lit day and night.")},
			{TEXT("smouldering"), TEXT("Smoke rising from buildings the threat touches, with burnt wood piles.")}}},
		{TEXT("timeOfDay"), &FWorldPresentation::TimeOfDay, {
			{TEXT("dawn"), TEXT("The player arrives at 06:00.")},
			{TEXT("morning"), TEXT("The player arrives at 09:00.")},
			{TEXT("midday"), TEXT("The player arrives at 12:00.")},
			{TEXT("dusk"), TEXT("The player arrives at 18:30.")},
			{TEXT("night"), TEXT("The player arrives at 22:00.")}}},
		{TEXT("weather"), &FWorldPresentation::Weather, {
			{TEXT("clear"), TEXT("Clear sky.")},
			{TEXT("overcast"), TEXT("Grey, flat light and a light breeze.")},
			{TEXT("misty"), TEXT("Thick ground fog that hides the far edge of town.")},
			{TEXT("stormy"), TEXT("Dark, desaturated light and trees and banners whipping in a gale.")}}},
		{TEXT("season"), &FWorldPresentation::Season, {
			{TEXT("summer"), TEXT("Full green foliage.")},
			{TEXT("spring"), TEXT("Fresh pale green foliage.")},
			{TEXT("autumn"), TEXT("Amber and rust foliage.")}}},
		{TEXT("roofPalette"), &FWorldPresentation::RoofPalette, {
			{TEXT("natural"), TEXT("The pack's own roof colours.")},
			{TEXT("weathered"), TEXT("Greyed, sun-faded roofs.")},
			{TEXT("sooty"), TEXT("Dark, soot-stained roofs.")},
			{TEXT("ochre"), TEXT("Warm ochre roofs.")},
			{TEXT("slate"), TEXT("Cool blue-grey roofs.")},
			{TEXT("moss"), TEXT("Moss-green roofs.")}}},
		{TEXT("authorityColor"), &FWorldPresentation::AuthorityColor, {
			{TEXT("natural"), TEXT("Landmark and seat of authority share the town's roof palette.")},
			{TEXT("crimson"), TEXT("Crimson roofs on the landmark and seat of authority.")},
			{TEXT("azure"), TEXT("Azure roofs on the landmark and seat of authority.")},
			{TEXT("gold"), TEXT("Gold roofs on the landmark and seat of authority.")},
			{TEXT("verdant"), TEXT("Green roofs on the landmark and seat of authority.")},
			{TEXT("black"), TEXT("Black roofs on the landmark and seat of authority.")}}},
	};
	return Table;
}

namespace
{
bool IsAllowed(const FWorldDirectorPresentation::FField& Field, const FString& Value)
{
	for (const FWorldDirectorPresentation::FOption& Option : Field.Options)
	{
		if (Value.Equals(Option.Value, ESearchCase::CaseSensitive))
		{
			return true;
		}
	}
	return false;
}
}

void FWorldDirectorPresentation::Validate(
	const FWorldPresentation& Presentation, const FString& Path, FValidationReport& Report)
{
	for (const FField& Field : Fields())
	{
		const FString& Value = Presentation.*(Field.Member);
		if (!IsAllowed(Field, Value))
		{
			Report.AddError(TEXT("brief.presentation_unsupported"),
				FString::Printf(TEXT("%s.%s"), *Path, Field.Name),
				FString::Printf(TEXT("'%s' is not a supported %s."), *Value, Field.Name));
		}
	}
}

bool FWorldDirectorPresentation::IsDefault(const FWorldPresentation& Presentation)
{
	for (const FField& Field : Fields())
	{
		if (!(Presentation.*(Field.Member)).Equals(Field.Options[0].Value, ESearchCase::CaseSensitive))
		{
			return false;
		}
	}
	return true;
}

FWorldPresentation FWorldDirectorPresentation::ResolveEffective(
	const FWorldPresentation& FromBrief, TArray<FString>* OutOverriddenFields)
{
	FWorldPresentation Effective = FromBrief;
	FString Override;
	if (!FParse::Value(FCommandLine::Get(), TEXT("WorldDirectorPresentationOverride="), Override, false))
	{
		return Effective;
	}
	TArray<FString> Pairs;
	Override.TrimStartAndEnd().ParseIntoArray(Pairs, TEXT(","), true);
	for (const FString& Pair : Pairs)
	{
		FString Name;
		FString Value;
		if (!Pair.Split(TEXT("="), &Name, &Value))
		{
			UE_LOG(LogWorldDirector, Warning, TEXT("WORLD_DIRECTOR_PRESENTATION_OVERRIDE_IGNORED entry=%s reason=not_field_equals_value"), *Pair);
			continue;
		}
		Name.TrimStartAndEndInline();
		Value.TrimStartAndEndInline();
		const FField* Field = Fields().FindByPredicate(
			[&Name](const FField& Candidate) { return Name.Equals(Candidate.Name, ESearchCase::IgnoreCase); });
		if (Field == nullptr || !IsAllowed(*Field, Value))
		{
			UE_LOG(LogWorldDirector, Warning, TEXT("WORLD_DIRECTOR_PRESENTATION_OVERRIDE_IGNORED entry=%s reason=unknown_field_or_value"), *Pair);
			continue;
		}
		Effective.*(Field->Member) = Value;
		if (OutOverriddenFields != nullptr)
		{
			OutOverriddenFields->AddUnique(Field->Name);
		}
	}
	return Effective;
}

TSharedRef<FJsonObject> FWorldDirectorPresentation::BuildManifest()
{
	TSharedRef<FJsonObject> Manifest = MakeShared<FJsonObject>();
	for (const FField& Field : Fields())
	{
		TSharedRef<FJsonObject> Options = MakeShared<FJsonObject>();
		for (const FOption& Option : Field.Options)
		{
			Options->SetStringField(Option.Value, Option.Effect);
		}
		Manifest->SetObjectField(Field.Name, Options);
	}
	return Manifest;
}

namespace
{
// Average texture colours: 01 teal (51,101,114), 02 slate (83,119,119), 03 tan
// (137,111,79), 04 rust (122,65,36), 05 plum (124,83,102).
const TCHAR* const TealRoof = TEXT("/Game/Fantastic_Village_Pack/materials/MI_rooftiles_01.MI_rooftiles_01");
const TCHAR* const SlateRoof = TEXT("/Game/Fantastic_Village_Pack/materials/MI_rooftiles_02.MI_rooftiles_02");
const TCHAR* const TanRoof = TEXT("/Game/Fantastic_Village_Pack/materials/MI_rooftiles_03.MI_rooftiles_03");
const TCHAR* const RustRoof = TEXT("/Game/Fantastic_Village_Pack/materials/MI_rooftiles_04.MI_rooftiles_04");
}

FWorldDirectorPresentation::FRoofLook FWorldDirectorPresentation::RoofLook(const FString& RoofPalette)
{
	if (RoofPalette == TEXT("weathered")) { return {nullptr, FLinearColor(0.8f, 0.78f, 0.76f)}; }
	if (RoofPalette == TEXT("sooty")) { return {nullptr, FLinearColor(0.5f, 0.48f, 0.47f)}; }
	if (RoofPalette == TEXT("ochre")) { return {TanRoof, FLinearColor(1.1f, 1.0f, 0.85f)}; }
	if (RoofPalette == TEXT("slate")) { return {SlateRoof, FLinearColor::White}; }
	if (RoofPalette == TEXT("moss")) { return {TealRoof, FLinearColor(0.85f, 1.15f, 0.7f)}; }
	return {};
}

FWorldDirectorPresentation::FRoofLook FWorldDirectorPresentation::AuthorityRoofLook(const FWorldPresentation& Presentation)
{
	const FString& Color = Presentation.AuthorityColor;
	if (Color == TEXT("crimson")) { return {RustRoof, FLinearColor(1.3f, 0.75f, 0.7f)}; }
	if (Color == TEXT("azure")) { return {TealRoof, FLinearColor(0.9f, 1.05f, 1.3f)}; }
	if (Color == TEXT("gold")) { return {TanRoof, FLinearColor(1.3f, 1.12f, 0.6f)}; }
	if (Color == TEXT("verdant")) { return {TealRoof, FLinearColor(0.75f, 1.25f, 0.7f)}; }
	if (Color == TEXT("black")) { return {SlateRoof, FLinearColor(0.3f, 0.3f, 0.33f)}; }
	return RoofLook(Presentation.RoofPalette);
}

FLinearColor FWorldDirectorPresentation::FoliageTint(const FString& Season)
{
	if (Season == TEXT("spring")) { return FLinearColor(0.92f, 1.08f, 0.8f); }
	if (Season == TEXT("autumn")) { return FLinearColor(1.9f, 0.72f, 0.28f); }
	return FLinearColor::White;
}

int32 FWorldDirectorPresentation::StartMinute(const FString& TimeOfDay)
{
	if (TimeOfDay == TEXT("morning")) { return 9 * 60; }
	if (TimeOfDay == TEXT("midday")) { return 12 * 60; }
	if (TimeOfDay == TEXT("dusk")) { return 18 * 60 + 30; }
	if (TimeOfDay == TEXT("night")) { return 22 * 60; }
	return 6 * 60;
}
