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
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

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
	UFUNCTION() void RevealCurrentArtifact();
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
	void RefreshMetadata(const class UDirectorBridgeSubsystem& Bridge);
	void SetDiagnosticText(const FString& Context, const FString& Body,
		const FString& ArtifactPath = FString());
	void SetView(EDiagnosticView InView);
	void UpdateNavigationState(int32 MetricCount);
	void UpdateViewButtonStyles();

	UPROPERTY() TObjectPtr<class UTextBlock> DiagnosticsText;
	UPROPERTY() TObjectPtr<class UTextBlock> ViewContextText;
	UPROPERTY() TObjectPtr<class UTextBlock> RunMetadataText;
	UPROPERTY() TObjectPtr<class UTextBlock> StageMetadataText;
	UPROPERTY() TObjectPtr<class UTextBlock> ArtifactPathText;
	UPROPERTY() TObjectPtr<class UProgressBar> RunProgress;
	UPROPERTY() TObjectPtr<class UScrollBox> DiagnosticsScroll;
	UPROPERTY() TObjectPtr<class UButton> PreviousStageButton;
	UPROPERTY() TObjectPtr<class UButton> NextStageButton;
	UPROPERTY() TObjectPtr<class UButton> RevealArtifactButton;
	UPROPERTY() TObjectPtr<class UButton> SummaryButton;
	UPROPERTY() TObjectPtr<class UButton> RequestButton;
	UPROPERTY() TObjectPtr<class UButton> PromptButton;
	UPROPERTY() TObjectPtr<class UButton> ResponseButton;
	UPROPERTY() TObjectPtr<class UButton> EventsButton;
	UPROPERTY() TObjectPtr<class UButton> TelemetryButton;
	UPROPERTY() TObjectPtr<class UButton> WorldButton;
	UPROPERTY() TObjectPtr<class UButton> CandidatesButton;
	UPROPERTY() TObjectPtr<class AWorldDirectorFixtureBootstrap> Bootstrap;
	double RefreshAccumulator = 0.0;
	FString LastRenderedContext;
	FString LastRenderedBody;
	FString LastRunMetadata;
	FString LastStageMetadata;
	FString CurrentArtifactPath;
	int32 SelectedMetricIndex = INDEX_NONE;
	EDiagnosticView View = EDiagnosticView::Summary;
};
