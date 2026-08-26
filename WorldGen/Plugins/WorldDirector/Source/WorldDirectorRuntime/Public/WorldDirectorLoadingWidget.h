#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "WorldDirectorLoadingWidget.generated.h"

UCLASS()
class WORLDDIRECTORRUNTIME_API UWorldDirectorLoadingWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitializeForBootstrap(class AWorldDirectorFixtureBootstrap* InBootstrap);
	void SetForSampleWorld(bool bInSampleWorld);
	bool ApplyViewportLayout();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UFUNCTION() void CancelGeneration();
	UFUNCTION() void OpenDiagnostics();
	void BuildWidgetTree();
	void RefreshLoadingState();

	UPROPERTY() TObjectPtr<class AWorldDirectorFixtureBootstrap> Bootstrap;
	UPROPERTY() TObjectPtr<class UTextBlock> TitleText;
	UPROPERTY() TObjectPtr<class UTextBlock> StageText;
	UPROPERTY() TObjectPtr<class UTextBlock> DetailText;
	UPROPERTY() TObjectPtr<class UProgressBar> ProgressBar;
	UPROPERTY() TObjectPtr<class UButton> CancelButton;
	UPROPERTY() TObjectPtr<class UButton> DiagnosticsButton;
	bool bSampleWorld = false;
	float SampleProgress = 0.16f;
	float RefreshAccumulator = 0.0f;
};
