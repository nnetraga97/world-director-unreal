#pragma once

#include "CoreMinimal.h"
#include "WorldDirectorTypes.h"

class FJsonObject;

/**
 * The closed vocabulary behind FWorldPresentation: allowed values, what each one builds,
 * the tints the runtime applies, and the -WorldDirectorPresentationOverride= test hook.
 * The JSON schema enums and the companion's defaults mirror these tables.
 */
struct WORLDDIRECTORRUNTIME_API FWorldDirectorPresentation
{
	struct FOption
	{
		const TCHAR* Value;
		const TCHAR* Effect;
	};

	struct FField
	{
		/** Wire name, as in the JSON schema. */
		const TCHAR* Name;
		FString FWorldPresentation::* Member;
		TArray<FOption> Options;
	};

	static const TArray<FField>& Fields();

	/** Adds one error per field whose value is not in its closed set. */
	static void Validate(const FWorldPresentation& Presentation, const FString& Path, FValidationReport& Report);

	static bool IsDefault(const FWorldPresentation& Presentation);

	/** Brief values with -WorldDirectorPresentationOverride=field=value,... applied on top. */
	static FWorldPresentation ResolveEffective(const FWorldPresentation& FromBrief, TArray<FString>* OutOverriddenFields = nullptr);

	/** Manifest the model sees: every option and the visible effect it produces. */
	static TSharedRef<FJsonObject> BuildManifest();

	/**
	 * A roof colour: one of the pack's five roof materials (teal, slate, tan, rust, plum)
	 * plus a Base Color Tint. Tint alone multiplies an already coloured texture, so a red
	 * tint on a blue roof turns grey; picking the nearest pack roof first keeps hues true.
	 */
	struct FRoofLook
	{
		const TCHAR* Material = nullptr;
		FLinearColor Tint = FLinearColor::White;

		bool IsDefault() const { return Material == nullptr && Tint.Equals(FLinearColor::White); }
	};

	static FRoofLook RoofLook(const FString& RoofPalette);
	static FRoofLook AuthorityRoofLook(const FWorldPresentation& Presentation);
	static FLinearColor FoliageTint(const FString& Season);
	static int32 StartMinute(const FString& TimeOfDay);
};
