#include "WorldDirectorTypes.h"

void FValidationReport::AddError(
	const FName Code,
	const FString& Path,
	const FString& Message)
{
	bValid = false;
	FValidationIssue& Issue = Issues.AddDefaulted_GetRef();
	Issue.Severity = EWorldDirectorValidationSeverity::Error;
	Issue.Code = Code;
	Issue.Path = Path;
	Issue.Message = Message.Left(512);
}

void FValidationReport::AddWarning(
	const FName Code,
	const FString& Path,
	const FString& Message)
{
	FValidationIssue& Issue = Issues.AddDefaulted_GetRef();
	Issue.Severity = EWorldDirectorValidationSeverity::Warning;
	Issue.Code = Code;
	Issue.Path = Path;
	Issue.Message = Message.Left(512);
}
