#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WorldDirectorCreateWorldWidget.generated.h"

UCLASS()
class WORLDDIRECTORRUNTIME_API UWorldDirectorCreateWorldWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitializeForBootstrap(class AWorldDirectorFixtureBootstrap* InBootstrap);
	bool ApplyViewportLayout();
	void SetGenerationResult(bool bSuccess, const FString& Error);
	void PrepareForNewWorld();
	bool IsCreationMenuReady() const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UFUNCTION() void CreateWorld();
	UFUNCTION() void CancelGeneration();
	UFUNCTION() void OpenDiagnostics();
	UFUNCTION() void CloseMenu();
	UFUNCTION() void ToggleFixtureDebug(bool bChecked);
	void BuildWidgetTree();
	void RefreshStatus();

	UPROPERTY() TObjectPtr<class AWorldDirectorFixtureBootstrap> Bootstrap;
	UPROPERTY() TObjectPtr<class UMultiLineEditableTextBox> PromptInput;
	UPROPERTY() TObjectPtr<class UEditableTextBox> SeedInput;
	UPROPERTY() TObjectPtr<class UComboBoxString> ModelInput;
	UPROPERTY() TObjectPtr<class UComboBoxString> ReasoningInput;
	UPROPERTY() TObjectPtr<class UTextBlock> StatusText;
	UPROPERTY() TObjectPtr<class UTextBlock> DetailText;
	UPROPERTY() TObjectPtr<class UButton> CreateButton;
	UPROPERTY() TObjectPtr<class UButton> CancelButton;
	UPROPERTY() TObjectPtr<class UButton> CloseButton;
	bool bUseFixtureDebug = false;
	bool bTerminalResultShown = false;
	double RefreshAccumulator = 0.0;
};
