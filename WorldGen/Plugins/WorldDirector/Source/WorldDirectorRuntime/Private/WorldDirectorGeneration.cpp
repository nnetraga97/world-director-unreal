#include "WorldDirectorGeneration.h"

#include "Async/Async.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeLock.h"
#include "Misc/Paths.h"

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
}

TSharedRef<IWorldDirectorProvider> CreateWorldDirectorLocalCompanionProvider()
{
	return MakeShared<FWorldDirectorLocalCompanionProvider>();
}
