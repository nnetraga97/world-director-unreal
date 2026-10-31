#include "WorldDirectorGeneration.h"

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeLock.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
struct FCompanionRequestState
{
	FGuid Id = FGuid::NewGuid();
	TAtomic<bool> bCancelled = false;
	FCriticalSection ProcessMutex;
	FProcHandle Process;
};

FString QuoteProcessArgument(const FString& Value)
{
	FString Escaped = Value;
	Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
	return FString::Printf(TEXT("\"%s\""), *Escaped);
}

class FWorldDirectorLocalCompanionProvider final
	: public IWorldDirectorProvider
	, public TSharedFromThis<FWorldDirectorLocalCompanionProvider>
{
public:
	virtual FGuid RequestStage(
		const FString& CompanionExecutable,
		const FString& CompanionScript,
		const FString& RequestPath,
		const FString& ResponsePath,
		const float TimeoutSeconds,
		FWorldDirectorProviderCompletion&& Completion) override
	{
		const TSharedRef<FCompanionRequestState> State = MakeShared<FCompanionRequestState>();
		{
			FScopeLock Lock(&RequestsMutex);
			Requests.Add(State);
		}
		const TWeakPtr<FWorldDirectorLocalCompanionProvider> WeakProvider = AsShared();

		Async(EAsyncExecution::ThreadPool,
			[State, CompanionExecutable, CompanionScript, RequestPath, ResponsePath,
			 TimeoutSeconds, Completion = MoveTemp(Completion), WeakProvider]() mutable
			{
				FWorldDirectorProviderResponse Response;
				if (State->bCancelled.Load())
				{
					Response.bCancelled = true;
					Response.Error = TEXT("Generation request was cancelled before launch.");
				}
				else
				{
					void* ReadPipe = nullptr;
					void* WritePipe = nullptr;
					const bool bPipeCreated = FPlatformProcess::CreatePipe(ReadPipe, WritePipe);
					const FString Arguments = FString::Printf(
						TEXT("%s --request %s --output %s"),
						*QuoteProcessArgument(CompanionScript),
						*QuoteProcessArgument(RequestPath),
						*QuoteProcessArgument(ResponsePath));
					uint32 ProcessId = 0;
					{
						FScopeLock ProcessLock(&State->ProcessMutex);
						State->Process = FPlatformProcess::CreateProc(
							*CompanionExecutable, *Arguments, false, true, true,
							&ProcessId, 0, nullptr, WritePipe, nullptr, WritePipe);
					}
					if (!State->Process.IsValid())
					{
						Response.Error = FString::Printf(
							TEXT("Could not launch local companion executable '%s'."),
							*CompanionExecutable);
					}
					else
					{
						const double StartedAt = FPlatformTime::Seconds();
						while (FPlatformProcess::IsProcRunning(State->Process))
						{
							if (bPipeCreated)
							{
								Response.ProviderOutput += FPlatformProcess::ReadPipe(ReadPipe);
							}
							if (State->bCancelled.Load() ||
								FPlatformTime::Seconds() - StartedAt >= TimeoutSeconds)
							{
								Response.bCancelled = State->bCancelled.Load();
								Response.bTimedOut = !Response.bCancelled;
								FPlatformProcess::TerminateProc(State->Process, true);
								break;
							}
							FPlatformProcess::Sleep(0.05f);
						}
						if (bPipeCreated)
						{
							Response.ProviderOutput += FPlatformProcess::ReadPipe(ReadPipe);
						}
						FPlatformProcess::GetProcReturnCode(State->Process, &Response.ExitCode);
						FPlatformProcess::CloseProc(State->Process);
						if (Response.bTimedOut)
						{
							Response.Error = FString::Printf(
								TEXT("Generation stage exceeded its %.1f second timeout."), TimeoutSeconds);
						}
						else if (Response.bCancelled)
						{
							Response.Error = TEXT("Generation request was cancelled.");
						}
						else if (Response.ExitCode != 0)
						{
							Response.Error = FString::Printf(
								TEXT("Local companion exited with code %d.%s%s"), Response.ExitCode,
								Response.ProviderOutput.IsEmpty() ? TEXT("") : TEXT(" Output: "),
								*Response.ProviderOutput.Right(4000));
						}
						else if (!FFileHelper::LoadFileToString(Response.ResponseJson, *ResponsePath))
						{
							Response.Error = TEXT("Local companion did not produce a response file.");
						}
						else
						{
							Response.bSuccess = true;
						}
					}
					if (bPipeCreated)
					{
						FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
					}
					if (Response.ProviderOutput.Len() > 8000)
					{
						Response.ProviderOutput = Response.ProviderOutput.Right(8000);
					}
				}
				if (const TSharedPtr<FWorldDirectorLocalCompanionProvider> Provider = WeakProvider.Pin())
				{
					FScopeLock Lock(&Provider->RequestsMutex);
					Provider->Requests.Remove(State);
				}

				AsyncTask(ENamedThreads::GameThread,
					[Completion = MoveTemp(Completion), Response = MoveTemp(Response)]() mutable
					{
						Completion(MoveTemp(Response));
					});
			});
		return State->Id;
	}

	virtual void CancelAll() override
	{
		FScopeLock Lock(&RequestsMutex);
		for (const TSharedRef<FCompanionRequestState>& State : Requests)
		{
			State->bCancelled.Store(true);
			FScopeLock ProcessLock(&State->ProcessMutex);
			if (State->Process.IsValid() && FPlatformProcess::IsProcRunning(State->Process))
			{
				FPlatformProcess::TerminateProc(State->Process, true);
			}
		}
		Requests.Reset();
	}

	virtual FString GetProviderName() const override
	{
		return TEXT("Local CLI Companion");
	}

private:
	FCriticalSection RequestsMutex;
	TArray<TSharedRef<FCompanionRequestState>> Requests;
};

/** Mirrors discover_claude() in the companion, for the menu's purposes. */
bool IsClaudeCodeInstalled()
{
	const FString Configured = FPlatformMisc::GetEnvironmentVariable(TEXT("WORLD_DIRECTOR_CLAUDE_PATH"));
	if (!Configured.IsEmpty())
	{
		return FPaths::FileExists(Configured);
	}
	const FString Home = FPlatformProcess::UserHomeDir();
	const FString Candidates[] = {
		FPaths::Combine(Home, TEXT(".local/bin/claude")),
		FPaths::Combine(Home, TEXT(".claude/local/claude")),
		FPaths::Combine(Home, TEXT(".npm-global/bin/claude")),
		TEXT("/opt/homebrew/bin/claude"),
		TEXT("/usr/local/bin/claude"),
	};
	for (const FString& Candidate : Candidates)
	{
		if (FPaths::FileExists(Candidate))
		{
			return true;
		}
	}
	return false;
}

/**
 * Claude models run through the Claude Code CLI, the engine behind the Claude Agent SDK.
 * The slugs name the CLI's aliases, which always resolve to the newest model in each
 * family, so the menu cannot go stale the way hardcoded Codex names did.
 */
void AppendClaudeModels(TArray<FWorldDirectorModelOption>& Options)
{
	if (!IsClaudeCodeInstalled())
	{
		return;
	}
	static const TCHAR* const ClaudeModels[][2] = {
		{ TEXT("claude-opus"), TEXT("Claude Opus (latest)") },
		{ TEXT("claude-sonnet"), TEXT("Claude Sonnet (latest)") },
		{ TEXT("claude-fable"), TEXT("Claude Fable (latest)") },
		{ TEXT("claude-haiku"), TEXT("Claude Haiku (latest)") },
	};
	for (const auto& Model : ClaudeModels)
	{
		FWorldDirectorModelOption& Option = Options.AddDefaulted_GetRef();
		Option.Slug = Model[0];
		Option.DisplayName = Model[1];
		Option.Description = TEXT("Runs through the Claude Code CLI with your Claude sign-in, or ANTHROPIC_API_KEY when set.");
		Option.ReasoningEfforts = { TEXT("low"), TEXT("medium"), TEXT("high") };
		Option.DefaultReasoningEffort = TEXT("medium");
	}
}
}

TSharedRef<IWorldDirectorProvider> CreateWorldDirectorLocalCompanionProvider()
{
	return MakeShared<FWorldDirectorLocalCompanionProvider>();
}

TArray<FWorldDirectorModelOption> LoadWorldDirectorModelCatalog()
{
	TArray<FWorldDirectorModelOption> Options = LoadCodexModelCatalog();
	AppendClaudeModels(Options);
	return Options;
}

TArray<FWorldDirectorModelOption> LoadCodexModelCatalog()
{
	TArray<FWorldDirectorModelOption> Options;
	FString CodexHome = FPlatformMisc::GetEnvironmentVariable(TEXT("CODEX_HOME"));
	if (CodexHome.IsEmpty())
	{
		CodexHome = FPaths::Combine(FPlatformProcess::UserHomeDir(), TEXT(".codex"));
	}
	FString CatalogJson;
	if (!FFileHelper::LoadFileToString(CatalogJson, *FPaths::Combine(CodexHome, TEXT("models_cache.json"))))
	{
		return Options;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CatalogJson);
	const TArray<TSharedPtr<FJsonValue>>* Models = nullptr;
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid() ||
		!Root->TryGetArrayField(TEXT("models"), Models) || Models == nullptr)
	{
		return Options;
	}
	// World generation must finish inside the run budget; deeper efforts were
	// measured to exceed it, so they are not offered here.
	static const TCHAR* const GenerationEfforts[] = {TEXT("low"), TEXT("medium"), TEXT("high")};
	for (const TSharedPtr<FJsonValue>& Value : *Models)
	{
		const TSharedPtr<FJsonObject> Model = Value.IsValid() ? Value->AsObject() : nullptr;
		FString Slug;
		FString Visibility;
		if (!Model.IsValid() || !Model->TryGetStringField(TEXT("slug"), Slug) || Slug.IsEmpty())
		{
			continue;
		}
		Model->TryGetStringField(TEXT("visibility"), Visibility);
		if (!Visibility.IsEmpty() && Visibility != TEXT("list"))
		{
			continue;
		}
		FWorldDirectorModelOption& Option = Options.AddDefaulted_GetRef();
		Option.Slug = Slug;
		Model->TryGetStringField(TEXT("display_name"), Option.DisplayName);
		Model->TryGetStringField(TEXT("description"), Option.Description);
		TSet<FString> Supported;
		const TArray<TSharedPtr<FJsonValue>>* Levels = nullptr;
		if (Model->TryGetArrayField(TEXT("supported_reasoning_levels"), Levels) && Levels != nullptr)
		{
			for (const TSharedPtr<FJsonValue>& Level : *Levels)
			{
				FString Effort;
				if (Level.IsValid() && Level->AsObject().IsValid() &&
					Level->AsObject()->TryGetStringField(TEXT("effort"), Effort))
				{
					Supported.Add(Effort);
				}
			}
		}
		for (const TCHAR* Effort : GenerationEfforts)
		{
			if (Supported.IsEmpty() || Supported.Contains(Effort))
			{
				Option.ReasoningEfforts.Add(Effort);
			}
		}
		Model->TryGetStringField(TEXT("default_reasoning_level"), Option.DefaultReasoningEffort);
		if (!Option.ReasoningEfforts.Contains(Option.DefaultReasoningEffort))
		{
			Option.DefaultReasoningEffort = Option.ReasoningEfforts.Contains(TEXT("medium"))
				? TEXT("medium") : (Option.ReasoningEfforts.IsEmpty() ? FString() : Option.ReasoningEfforts[0]);
		}
	}
	return Options;
}

FString GetWorldDirectorDefaultModel()
{
	FString Configured;
	GConfig->GetString(TEXT("WorldDirector"), TEXT("DefaultModel"), Configured, GGameIni);
	Configured.TrimStartAndEndInline();
	const TArray<FWorldDirectorModelOption> Catalog = LoadWorldDirectorModelCatalog();
	if (Catalog.IsEmpty())
	{
		return Configured;
	}
	if (!Configured.IsEmpty() && Catalog.ContainsByPredicate(
		[&Configured](const FWorldDirectorModelOption& Option) { return Option.Slug == Configured; }))
	{
		return Configured;
	}
	return Catalog[0].Slug;
}

FString GetWorldDirectorDefaultReasoningEffort(const FString& Model)
{
	FString Configured;
	GConfig->GetString(TEXT("WorldDirector"), TEXT("DefaultReasoningEffort"), Configured, GGameIni);
	Configured = Configured.TrimStartAndEnd().ToLower();
	for (const FWorldDirectorModelOption& Option : LoadWorldDirectorModelCatalog())
	{
		if (Option.Slug == Model)
		{
			return Option.ReasoningEfforts.Contains(Configured) ? Configured : Option.DefaultReasoningEffort;
		}
	}
	return Configured;
}
