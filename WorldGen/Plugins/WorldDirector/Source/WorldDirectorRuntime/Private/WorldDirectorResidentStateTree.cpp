#include "WorldDirectorResidentStateTree.h"

#include "StateTreeExecutionContext.h"
#include "StateTreeLinker.h"

UWorldDirectorResidentStateTreeSchema::UWorldDirectorResidentStateTreeSchema()
{
	ContextActorClass = AWorldDirectorResidentActor::StaticClass();
	GetContextActorDataDesc().Struct = AWorldDirectorResidentActor::StaticClass();
}

FWorldDirectorResidentLifeTask::FWorldDirectorResidentLifeTask()
{
	bShouldCallTick = true;
	bShouldCopyBoundPropertiesOnTick = false;
}

bool FWorldDirectorResidentLifeTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(ResidentHandle);
	return true;
}

EStateTreeRunStatus FWorldDirectorResidentLifeTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	AWorldDirectorResidentActor& Resident = Context.GetExternalData(ResidentHandle);
	Resident.TickResidentLife(0.0f);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FWorldDirectorResidentLifeTask::Tick(
	FStateTreeExecutionContext& Context,
	const float DeltaTime) const
{
	AWorldDirectorResidentActor& Resident = Context.GetExternalData(ResidentHandle);
	Resident.TickResidentLife(DeltaTime);
	return EStateTreeRunStatus::Running;
}
