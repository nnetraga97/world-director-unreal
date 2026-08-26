#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "WorldDirectorLandingWidget.generated.h"

UCLASS()
class WORLDDIRECTORRUNTIME_API UWorldDirectorLandingWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitializeForBootstrap(class AWorldDirectorFixtureBootstrap* InBootstrap);
	void RefreshSavedWorlds();
	bool ApplyViewportLayout();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	UFUNCTION() void StartNewWorld();
	UFUNCTION() void ExploreSampleWorld();
	UFUNCTION() void LoadSelectedWorld();
	void BuildWidgetTree();

	UPROPERTY() TObjectPtr<class AWorldDirectorFixtureBootstrap> Bootstrap;
	UPROPERTY() TObjectPtr<class UButton> NewWorldButton;
	UPROPERTY() TObjectPtr<class UButton> SampleWorldButton;
	UPROPERTY() TObjectPtr<class UButton> LoadWorldButton;
	UPROPERTY() TObjectPtr<class UComboBoxString> SavedWorldPicker;
	UPROPERTY() TObjectPtr<class UTextBlock> SavedWorldStatusText;
	TArray<FString> SavedWorldPaths;
};
