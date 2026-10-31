#pragma once

#include "CoreMinimal.h"
#include "WorldDirectorTypes.h"

#include "WorldDirectorGeneration.generated.h"

UENUM(BlueprintType)
enum class EWorldDirectorGenerationStage : uint8
{
	Idle,
	Interpret,
	Topology,
	Layout,
	Population,
	Integrate,
	Repair,
	Completed,
	Failed,
	Cancelled
};

USTRUCT(BlueprintType)
struct WORLDDIRECTORRUNTIME_API FWorldDirectorLayoutCandidate
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString OpaqueId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Summary;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 LayoutSeed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EWorldDirectorTerrainArchetype TerrainArchetype = EWorldDirectorTerrainArchetype::Basin;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float ReliefCentimeters = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MeanSlopeDegrees = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MaximumRoadGrade = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString WorldFingerprint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString TerrainFingerprint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString LayoutFingerprint;
};

USTRUCT(BlueprintType)
struct WORLDDIRECTORRUNTIME_API FWorldDirectorGenerationStageMetric
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FString Stage;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FString RequestId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FString StartedAtUtc;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	double DurationSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int64 RequestBytes = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int64 ResponseBytes = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 ExitCode = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	bool bSuccess = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FString Error;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FString ProviderOutput;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FString RequestPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FString ResponsePath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FString RawResponsePath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FString PromptPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FString ProviderEventsPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FString TelemetryPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FString Model;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FString ReasoningEffort;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FString ProviderThreadId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int64 InputTokens = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int64 CachedInputTokens = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int64 OutputTokens = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int64 ReasoningOutputTokens = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int64 PromptCharacters = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FString CostNote;

	/** Cost the provider reported for this stage (the Claude Code CLI does), or negative. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	double BilledCostUsd = -1.0;
};

struct FWorldDirectorProviderResponse
{
	bool bSuccess = false;
	bool bTimedOut = false;
	bool bCancelled = false;
	int32 ExitCode = INDEX_NONE;
	FString ResponseJson;
	FString Error;
	FString ProviderOutput;
};

using FWorldDirectorProviderCompletion = TFunction<void(FWorldDirectorProviderResponse&&)>;

class IWorldDirectorProvider
{
public:
	virtual ~IWorldDirectorProvider() = default;

	virtual FGuid RequestStage(
		const FString& CompanionExecutable,
		const FString& CompanionScript,
		const FString& RequestPath,
		const FString& ResponsePath,
		float TimeoutSeconds,
		FWorldDirectorProviderCompletion&& Completion) = 0;

	virtual void CancelAll() = 0;
	virtual FString GetProviderName() const = 0;
};

WORLDDIRECTORRUNTIME_API TSharedRef<IWorldDirectorProvider> CreateWorldDirectorLocalCompanionProvider();

/** A model the generator can call: one the installed Codex CLI serves, or a Claude model. */
struct FWorldDirectorModelOption
{
	FString Slug;
	FString DisplayName;
	FString Description;
	/** Reasoning efforts offered for world generation (the catalog's levels, capped at "high"). */
	TArray<FString> ReasoningEfforts;
	FString DefaultReasoningEffort;
};

/**
 * The Codex catalog followed by the Claude models (claude-opus, claude-sonnet, claude-fable,
 * claude-haiku) when the Claude Code CLI is installed. The companion sends claude- models
 * to `claude -p` and everything else to Codex.
 */
WORLDDIRECTORRUNTIME_API TArray<FWorldDirectorModelOption> LoadWorldDirectorModelCatalog();

/**
 * Lists visible models from $CODEX_HOME/models_cache.json (default ~/.codex). Model names
 * change between Codex releases; hardcoding them is what left the menu offering retired
 * models. Returns an empty array when Codex has not cached a catalog yet.
 */
WORLDDIRECTORRUNTIME_API TArray<FWorldDirectorModelOption> LoadCodexModelCatalog();

/** [WorldDirector] DefaultModel from Game config if the catalog offers it, else the first catalog model. */
WORLDDIRECTORRUNTIME_API FString GetWorldDirectorDefaultModel();

/** [WorldDirector] DefaultReasoningEffort if the model supports it, else the model's own default. */
WORLDDIRECTORRUNTIME_API FString GetWorldDirectorDefaultReasoningEffort(const FString& Model);
