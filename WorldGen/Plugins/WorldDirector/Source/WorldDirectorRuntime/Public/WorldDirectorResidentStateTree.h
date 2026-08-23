#pragma once

#include "Components/StateTreeComponentSchema.h"
#include "StateTreeExecutionTypes.h"
#include "StateTreeTaskBase.h"
#include "WorldDirectorTownActors.h"

#include "WorldDirectorResidentStateTree.generated.h"

/** StateTree schema whose actor context is the concrete resident character. */
UCLASS()
class WORLDDIRECTORRUNTIME_API UWorldDirectorResidentStateTreeSchema
	: public UStateTreeComponentSchema
{
	GENERATED_BODY()

public:
	UWorldDirectorResidentStateTreeSchema();
};

USTRUCT()
struct WORLDDIRECTORRUNTIME_API FWorldDirectorResidentLifeTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "World Director")
	int32 TickCount = 0;
};

/**
 * Long-running StateTree task that delegates each behavior tick to the resident.
 * The resident then resolves its current schedule intent through the simulation
 * subsystem and the Smart Object claim lifecycle.
 */
USTRUCT(meta = (DisplayName = "Run Resident Life", Category = "World Director"))
struct WORLDDIRECTORRUNTIME_API FWorldDirectorResidentLifeTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FWorldDirectorResidentLifeTask();
	using FInstanceDataType = FWorldDirectorResidentLifeTaskInstanceData;
	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual EStateTreeRunStatus EnterState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(
		FStateTreeExecutionContext& Context,
		float DeltaTime) const override;

	TStateTreeExternalDataHandle<AWorldDirectorResidentActor> ResidentHandle;
};
