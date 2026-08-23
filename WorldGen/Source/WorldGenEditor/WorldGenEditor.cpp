#include "WorldGenEditor.h"

#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "WorldGenAssetAuthoringLibrary.h"

namespace
{
FAutoConsoleCommand AuthorCompilerTownMapCommand(
	TEXT("WorldGen.AuthorCompilerTownMap"),
	TEXT("Authors the World Director compiler fixture map from the certified landscape."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		const bool bSucceeded = UWorldGenAssetAuthoringLibrary::AuthorCompilerTownMap();
		UE_LOG(LogTemp, Display, TEXT("WORLD_DIRECTOR_AUTHOR_MAP_RESULT=%s"), bSucceeded ? TEXT("PASS") : TEXT("FAIL"));
	}));
}

IMPLEMENT_MODULE(FDefaultModuleImpl, WorldGenEditor)
