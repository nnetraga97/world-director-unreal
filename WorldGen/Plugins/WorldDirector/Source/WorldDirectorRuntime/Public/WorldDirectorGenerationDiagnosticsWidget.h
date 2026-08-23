#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WorldDirectorGenerationDiagnosticsWidget.generated.h"

UCLASS()
class WORLDDIRECTORRUNTIME_API UWorldDirectorGenerationDiagnosticsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitializeForBootstrap(class AWorldDirectorFixtureBootstrap* InBootstrap);
	void ApplyViewportLayout();
	bool IsDiagnosticsReady() const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	enum class EDiagnosticView : uint8
	{
		Summary,
		Request,
		Prompt,
		Response,
		Events,
		Telemetry,
		World,
		Candidates
	};

	UFUNCTION() void CloseDiagnostics();
	UFUNCTION() void CopyDiagnostics();
	UFUNCTION() void OpenRunFolder();
	UFUNCTION() void ShowSummary();
	UFUNCTION() void ShowRequest();
	UFUNCTION() void ShowPrompt();
	UFUNCTION() void ShowResponse();
	UFUNCTION() void ShowEvents();
	UFUNCTION() void ShowTelemetry();
	UFUNCTION() void ShowWorld();
	UFUNCTION() void ShowCandidates();
	UFUNCTION() void PreviousStage();
	UFUNCTION() void NextStage();
	void BuildWidgetTree();
	void RefreshDiagnostics();
	void SetDiagnosticText(const FString& Context, const FString& Body);
	void SetView(EDiagnosticView InView);

	UPROPERTY() TObjectPtr<class UTextBlock> DiagnosticsText;
	UPROPERTY() TObjectPtr<class UTextBlock> ViewContextText;
	UPROPERTY() TObjectPtr<class AWorldDirectorFixtureBootstrap> Bootstrap;
	double RefreshAccumulator = 0.0;
	FString LastRenderedContext;
	FString LastRenderedBody;
	int32 SelectedMetricIndex = INDEX_NONE;
	EDiagnosticView View = EDiagnosticView::Summary;
};
