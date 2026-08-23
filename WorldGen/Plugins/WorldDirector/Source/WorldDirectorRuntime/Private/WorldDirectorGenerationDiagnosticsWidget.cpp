#include "WorldDirectorGenerationDiagnosticsWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformProcess.h"
#include "InputCoreTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "WorldDirectorSubsystems.h"
#include "WorldDirectorTownActors.h"

namespace
{
const FLinearColor DiagTextPrimary(0.90f, 0.92f, 0.92f);
const FLinearColor DiagTextSecondary(0.67f, 0.71f, 0.74f);
const FLinearColor DiagTextMuted(0.49f, 0.54f, 0.58f);
const FLinearColor DiagAccentBlue(0.34f, 0.76f, 0.98f);
const FLinearColor DiagAccentGreen(0.38f, 0.79f, 0.52f);
const FLinearColor DiagAccentAmber(0.92f, 0.68f, 0.30f);
const FLinearColor DiagAccentRed(0.93f, 0.37f, 0.34f);
const FLinearColor ButtonIdle(0.10f, 0.13f, 0.15f);
const FLinearColor ButtonActive(0.09f, 0.33f, 0.47f);

UTextBlock* DiagAddText(
	UWidgetTree* Tree,
	UVerticalBox* Layout,
	const FString& Text,
	const int32 Size,
	const FLinearColor& Color,
	const FMargin& Padding = FMargin(0.0f))
{
	UTextBlock* Block = Tree->ConstructWidget<UTextBlock>();
	Block->SetText(FText::FromString(Text));
	Block->SetFont(FSlateFontInfo(FCoreStyle::GetDefaultFont(), Size));
	Block->SetColorAndOpacity(FSlateColor(Color));
	Block->SetAutoWrapText(true);
	if (UVerticalBoxSlot* Slot = Layout->AddChildToVerticalBox(Block))
	{
		Slot->SetPadding(Padding);
	}
	return Block;
}

UButton* MakeButton(UWidgetTree* Tree, const FString& Label)
{
	UButton* Button = Tree->ConstructWidget<UButton>();
	Button->SetBackgroundColor(ButtonIdle);
	UBorder* Padding = Tree->ConstructWidget<UBorder>();
	Padding->SetBrushColor(FLinearColor::Transparent);
	Padding->SetPadding(FMargin(12.0f, 7.0f));
	UTextBlock* Text = Tree->ConstructWidget<UTextBlock>();
	Text->SetText(FText::FromString(Label));
	Text->SetFont(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 12));
	Text->SetColorAndOpacity(FSlateColor(DiagTextPrimary));
	Text->SetJustification(ETextJustify::Center);
	Padding->SetContent(Text);
	Button->SetContent(Padding);
	return Button;
}

UButton* AddButton(UWidgetTree* Tree, UHorizontalBox* Row, const FString& Label)
{
	UButton* Button = MakeButton(Tree, Label);
	if (UHorizontalBoxSlot* Slot = Row->AddChildToHorizontalBox(Button))
	{
		Slot->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
	}
	return Button;
}

UButton* AddButton(UWidgetTree* Tree, UWrapBox* Row, const FString& Label)
{
	UButton* Button = MakeButton(Tree, Label);
	if (UWrapBoxSlot* Slot = Row->AddChildToWrapBox(Button))
	{
		Slot->SetPadding(FMargin(0.0f, 0.0f, 9.0f, 8.0f));
	}
	return Button;
}

UBorder* AddCard(UWidgetTree* Tree, UVerticalBox* Layout, const FMargin& BottomPadding)
{
	UBorder* Card = Tree->ConstructWidget<UBorder>();
	Card->SetBrushColor(FLinearColor(0.045f, 0.052f, 0.058f, 1.0f));
	Card->SetPadding(FMargin(14.0f, 12.0f));
	if (UVerticalBoxSlot* Slot = Layout->AddChildToVerticalBox(Card))
	{
		Slot->SetPadding(BottomPadding);
	}
	return Card;
}

FString FormatBytes(const int64 Bytes)
{
	if (Bytes >= 1024 * 1024)
	{
		return FString::Printf(TEXT("%.2f MB"), static_cast<double>(Bytes) / (1024.0 * 1024.0));
	}
	if (Bytes >= 1024)
	{
		return FString::Printf(TEXT("%.1f KB"), static_cast<double>(Bytes) / 1024.0);
	}
	return FString::Printf(TEXT("%lld B"), Bytes);
}

FString StageStateLabel(const FWorldDirectorGenerationStageMetric& Metric)
{
	if (Metric.bSuccess)
	{
		return TEXT("COMPLETED");
	}
	if (!Metric.Error.IsEmpty())
	{
		return TEXT("FAILED");
	}
	return TEXT("IN PROGRESS");
}

float GenerationProgress(const EWorldDirectorGenerationStage Stage)
{
	switch (Stage)
	{
	case EWorldDirectorGenerationStage::Interpret:
		return 0.14f;
	case EWorldDirectorGenerationStage::Topology:
		return 0.34f;
	case EWorldDirectorGenerationStage::Layout:
		return 0.54f;
	case EWorldDirectorGenerationStage::Population:
		return 0.74f;
	case EWorldDirectorGenerationStage::Integrate:
		return 0.92f;
	case EWorldDirectorGenerationStage::Repair:
		return 0.82f;
	case EWorldDirectorGenerationStage::Completed:
		return 1.0f;
	default:
		return 0.0f;
	}
}

FLinearColor GenerationAccent(const EWorldDirectorGenerationStage Stage, const bool bRunning)
{
	if (Stage == EWorldDirectorGenerationStage::Completed)
	{
		return DiagAccentGreen;
	}
	if (Stage == EWorldDirectorGenerationStage::Failed ||
		Stage == EWorldDirectorGenerationStage::Cancelled)
	{
		return DiagAccentRed;
	}
	return bRunning ? DiagAccentBlue : DiagAccentAmber;
}
}

void UWorldDirectorGenerationDiagnosticsWidget::InitializeForBootstrap(
	AWorldDirectorFixtureBootstrap* InBootstrap)
{
	Bootstrap = InBootstrap;
}

TSharedRef<SWidget> UWorldDirectorGenerationDiagnosticsWidget::RebuildWidget()
{
	BuildWidgetTree();
	return Super::RebuildWidget();
}

void UWorldDirectorGenerationDiagnosticsWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	UpdateViewButtonStyles();
	RefreshDiagnostics();
}

void UWorldDirectorGenerationDiagnosticsWidget::ApplyViewportLayout()
{
	const FVector2D ViewportSize = UWidgetLayoutLibrary::GetViewportSize(this);
	if (ViewportSize.X <= 0.0f || ViewportSize.Y <= 0.0f)
	{
		return;
	}
	const float Margin = ViewportSize.X < 760.0f ? 12.0f : 24.0f;
	const FVector2D DesiredSize(
		FMath::Max(1.0f, FMath::Min(1180.0f, ViewportSize.X - Margin * 2.0f)),
		FMath::Max(1.0f, FMath::Min(940.0f, ViewportSize.Y - Margin * 2.0f)));
	const FVector2D Position(
		FMath::Max(Margin, (ViewportSize.X - DesiredSize.X) * 0.5f),
		FMath::Max(Margin, (ViewportSize.Y - DesiredSize.Y) * 0.5f));
	SetAnchorsInViewport(FAnchors(0.0f, 0.0f));
	SetAlignmentInViewport(FVector2D::ZeroVector);
	SetPositionInViewport(Position, false);
	SetDesiredSizeInViewport(DesiredSize);
}

void UWorldDirectorGenerationDiagnosticsWidget::BuildWidgetTree()
{
	if (WidgetTree == nullptr || WidgetTree->RootWidget != nullptr)
	{
		return;
	}
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
	Panel->SetBrushColor(FLinearColor(0.015f, 0.018f, 0.021f, 0.99f));
	Panel->SetPadding(FMargin(22.0f));
	WidgetTree->RootWidget = Panel;
	UVerticalBox* Layout = WidgetTree->ConstructWidget<UVerticalBox>();
	Panel->SetContent(Layout);

	DiagAddText(WidgetTree, Layout, TEXT("WORLD DIRECTOR"), 12, DiagAccentBlue);
	DiagAddText(WidgetTree, Layout, TEXT("AI generation diagnostics"), 27, DiagTextPrimary,
		FMargin(0.0f, 2.0f, 0.0f, 0.0f));
	DiagAddText(WidgetTree, Layout,
		TEXT("Inspect run state, stage timing, prompts, responses, token usage, validation repairs, and resolved-world artifacts. Hidden chain-of-thought is neither available nor recorded."),
		13, DiagTextSecondary, FMargin(0.0f, 6.0f, 0.0f, 12.0f));

	UBorder* RunCard = AddCard(WidgetTree, Layout, FMargin(0.0f, 0.0f, 0.0f, 10.0f));
	UVerticalBox* RunLayout = WidgetTree->ConstructWidget<UVerticalBox>();
	RunCard->SetContent(RunLayout);
	DiagAddText(WidgetTree, RunLayout, TEXT("RUN OVERVIEW"), 12, DiagAccentBlue);
	RunMetadataText = DiagAddText(WidgetTree, RunLayout, TEXT("No run data available yet."), 12, DiagTextSecondary,
		FMargin(0.0f, 5.0f, 0.0f, 8.0f));
	RunProgress = WidgetTree->ConstructWidget<UProgressBar>();
	RunProgress->SetPercent(0.0f);
	RunProgress->SetFillColorAndOpacity(DiagAccentBlue);
	RunLayout->AddChildToVerticalBox(RunProgress);

	UBorder* StageCard = AddCard(WidgetTree, Layout, FMargin(0.0f, 0.0f, 0.0f, 10.0f));
	UVerticalBox* StageLayout = WidgetTree->ConstructWidget<UVerticalBox>();
	StageCard->SetContent(StageLayout);
	UHorizontalBox* StageNavigation = WidgetTree->ConstructWidget<UHorizontalBox>();
	StageLayout->AddChildToVerticalBox(StageNavigation);
	PreviousStageButton = AddButton(WidgetTree, StageNavigation, TEXT("Previous stage"));
	PreviousStageButton->SetToolTipText(FText::FromString(TEXT("Select the previous recorded stage (Ctrl+Left).")));
	PreviousStageButton->OnClicked.AddDynamic(this, &UWorldDirectorGenerationDiagnosticsWidget::PreviousStage);
	NextStageButton = AddButton(WidgetTree, StageNavigation, TEXT("Next stage"));
	NextStageButton->SetToolTipText(FText::FromString(TEXT("Select the next recorded stage (Ctrl+Right).")));
	NextStageButton->OnClicked.AddDynamic(this, &UWorldDirectorGenerationDiagnosticsWidget::NextStage);
	StageMetadataText = DiagAddText(
		WidgetTree,
		StageLayout,
		TEXT("No AI stage has been recorded."),
		12,
		DiagTextSecondary,
		FMargin(0.0f, 9.0f, 0.0f, 0.0f));

	DiagAddText(WidgetTree, Layout, TEXT("RUN VIEWS"), 11, DiagTextMuted,
		FMargin(0.0f, 2.0f, 0.0f, 5.0f));
	UWrapBox* RunViews = WidgetTree->ConstructWidget<UWrapBox>();
	Layout->AddChildToVerticalBox(RunViews);
	SummaryButton = AddButton(WidgetTree, RunViews, TEXT("Overview"));
	SummaryButton->OnClicked.AddDynamic(this, &UWorldDirectorGenerationDiagnosticsWidget::ShowSummary);
	WorldButton = AddButton(WidgetTree, RunViews, TEXT("Physical world"));
	WorldButton->OnClicked.AddDynamic(this, &UWorldDirectorGenerationDiagnosticsWidget::ShowWorld);
	CandidatesButton = AddButton(WidgetTree, RunViews, TEXT("Layout candidates"));
	CandidatesButton->OnClicked.AddDynamic(this, &UWorldDirectorGenerationDiagnosticsWidget::ShowCandidates);

	DiagAddText(WidgetTree, Layout, TEXT("SELECTED STAGE ARTIFACTS"), 11, DiagTextMuted,
		FMargin(0.0f, 1.0f, 0.0f, 5.0f));
	UWrapBox* StageViews = WidgetTree->ConstructWidget<UWrapBox>();
	Layout->AddChildToVerticalBox(StageViews);
	RequestButton = AddButton(WidgetTree, StageViews, TEXT("Request"));
	RequestButton->OnClicked.AddDynamic(this, &UWorldDirectorGenerationDiagnosticsWidget::ShowRequest);
	PromptButton = AddButton(WidgetTree, StageViews, TEXT("Constructed prompt"));
	PromptButton->OnClicked.AddDynamic(this, &UWorldDirectorGenerationDiagnosticsWidget::ShowPrompt);
	ResponseButton = AddButton(WidgetTree, StageViews, TEXT("Model response"));
	ResponseButton->OnClicked.AddDynamic(this, &UWorldDirectorGenerationDiagnosticsWidget::ShowResponse);
	EventsButton = AddButton(WidgetTree, StageViews, TEXT("Provider events"));
	EventsButton->OnClicked.AddDynamic(this, &UWorldDirectorGenerationDiagnosticsWidget::ShowEvents);
	TelemetryButton = AddButton(WidgetTree, StageViews, TEXT("Telemetry"));
	TelemetryButton->OnClicked.AddDynamic(this, &UWorldDirectorGenerationDiagnosticsWidget::ShowTelemetry);

	ViewContextText = DiagAddText(WidgetTree, Layout, TEXT("VIEW: OVERVIEW"), 13, DiagAccentBlue,
		FMargin(0.0f, 5.0f, 0.0f, 4.0f));
	ArtifactPathText = DiagAddText(WidgetTree, Layout, TEXT(""), 10, DiagTextMuted,
		FMargin(0.0f, 0.0f, 0.0f, 6.0f));
	ArtifactPathText->SetVisibility(ESlateVisibility::Collapsed);

	UBorder* BodyCard = WidgetTree->ConstructWidget<UBorder>();
	BodyCard->SetBrushColor(FLinearColor(0.008f, 0.010f, 0.012f, 1.0f));
	BodyCard->SetPadding(FMargin(14.0f));
	if (UVerticalBoxSlot* Slot = Layout->AddChildToVerticalBox(BodyCard))
	{
		Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Slot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
	}
	DiagnosticsScroll = WidgetTree->ConstructWidget<UScrollBox>();
	BodyCard->SetContent(DiagnosticsScroll);
	DiagnosticsText = WidgetTree->ConstructWidget<UTextBlock>();
	DiagnosticsText->SetAutoWrapText(true);
	DiagnosticsText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Mono"), 12.0f));
	DiagnosticsText->SetColorAndOpacity(FSlateColor(FLinearColor(0.82f, 0.85f, 0.86f)));
	DiagnosticsScroll->AddChild(DiagnosticsText);

	UWrapBox* Actions = WidgetTree->ConstructWidget<UWrapBox>();
	Layout->AddChildToVerticalBox(Actions);
	UButton* CopyButton = AddButton(WidgetTree, Actions, TEXT("Copy visible report"));
	CopyButton->SetToolTipText(FText::FromString(TEXT("Copy run metadata, stage metadata, artifact path, and the current view (Ctrl+C).")));
	CopyButton->OnClicked.AddDynamic(this, &UWorldDirectorGenerationDiagnosticsWidget::CopyDiagnostics);
	RevealArtifactButton = AddButton(WidgetTree, Actions, TEXT("Reveal current artifact"));
	RevealArtifactButton->SetToolTipText(FText::FromString(TEXT("Open the folder containing the artifact shown above.")));
	RevealArtifactButton->OnClicked.AddDynamic(this, &UWorldDirectorGenerationDiagnosticsWidget::RevealCurrentArtifact);
	RevealArtifactButton->SetIsEnabled(false);
	UButton* FolderButton = AddButton(WidgetTree, Actions, TEXT("Open run folder"));
	FolderButton->SetToolTipText(FText::FromString(TEXT("Open the complete persisted run directory.")));
	FolderButton->OnClicked.AddDynamic(this, &UWorldDirectorGenerationDiagnosticsWidget::OpenRunFolder);
	UButton* CloseButton = AddButton(WidgetTree, Actions, TEXT("Close"));
	CloseButton->SetToolTipText(FText::FromString(TEXT("Close diagnostics (Escape or F8).")));
	CloseButton->OnClicked.AddDynamic(this, &UWorldDirectorGenerationDiagnosticsWidget::CloseDiagnostics);
	DiagAddText(WidgetTree, Layout,
		TEXT("Keyboard: Ctrl+Left/Right stage  |  Ctrl+C copy report  |  Escape or F8 close"),
		10, DiagTextMuted, FMargin(0.0f, 1.0f, 0.0f, 0.0f));

	UpdateNavigationState(0);
	UpdateViewButtonStyles();
}

void UWorldDirectorGenerationDiagnosticsWidget::NativeTick(
	const FGeometry& MyGeometry,
	const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!IsVisible())
	{
		return;
	}
	RefreshAccumulator += InDeltaTime;
	if (RefreshAccumulator >= 0.5)
	{
		RefreshAccumulator = 0.0;
		RefreshDiagnostics();
	}
}

FReply UWorldDirectorGenerationDiagnosticsWidget::NativeOnPreviewKeyDown(
	const FGeometry& InGeometry,
	const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::F8)
	{
		CloseDiagnostics();
		return FReply::Handled();
	}
	if (InKeyEvent.IsControlDown() && InKeyEvent.GetKey() == EKeys::Left)
	{
		PreviousStage();
		return FReply::Handled();
	}
	if (InKeyEvent.IsControlDown() && InKeyEvent.GetKey() == EKeys::Right)
	{
		NextStage();
		return FReply::Handled();
	}
	if (InKeyEvent.IsControlDown() && InKeyEvent.GetKey() == EKeys::C)
	{
		CopyDiagnostics();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

void UWorldDirectorGenerationDiagnosticsWidget::SetDiagnosticText(
	const FString& Context,
	const FString& Body,
	const FString& ArtifactPath)
{
	if (ViewContextText != nullptr && Context != LastRenderedContext)
	{
		LastRenderedContext = Context;
		ViewContextText->SetText(FText::FromString(Context));
	}
	if (DiagnosticsText != nullptr && Body != LastRenderedBody)
	{
		LastRenderedBody = Body;
		DiagnosticsText->SetText(FText::FromString(Body));
	}
	CurrentArtifactPath = ArtifactPath;
	if (ArtifactPathText != nullptr)
	{
		if (CurrentArtifactPath.IsEmpty())
		{
			ArtifactPathText->SetVisibility(ESlateVisibility::Collapsed);
		}
		else
		{
			ArtifactPathText->SetText(FText::FromString(TEXT("ARTIFACT: ") + CurrentArtifactPath));
			ArtifactPathText->SetVisibility(ESlateVisibility::Visible);
		}
	}
	if (RevealArtifactButton != nullptr)
	{
		RevealArtifactButton->SetIsEnabled(!CurrentArtifactPath.IsEmpty());
	}
}

void UWorldDirectorGenerationDiagnosticsWidget::RefreshMetadata(
	const UDirectorBridgeSubsystem& Bridge)
{
	const TArray<FWorldDirectorGenerationStageMetric>& Metrics = Bridge.GetGenerationMetrics();
	if (!Metrics.IsEmpty() && !Metrics.IsValidIndex(SelectedMetricIndex))
	{
		SelectedMetricIndex = Metrics.Num() - 1;
	}
	UpdateNavigationState(Metrics.Num());

	int64 TotalInputTokens = 0;
	int64 TotalCachedTokens = 0;
	int64 TotalOutputTokens = 0;
	int64 TotalReasoningTokens = 0;
	for (const FWorldDirectorGenerationStageMetric& Metric : Metrics)
	{
		TotalInputTokens += Metric.InputTokens;
		TotalCachedTokens += Metric.CachedInputTokens;
		TotalOutputTokens += Metric.OutputTokens;
		TotalReasoningTokens += Metric.ReasoningOutputTokens;
	}
	const EWorldDirectorGenerationStage GenerationStage = Bridge.GetGenerationStage();
	const FString State = StaticEnum<EWorldDirectorGenerationStage>()->GetNameStringByValue(
		static_cast<int64>(GenerationStage));
	const FString RunId = Bridge.GetGenerationRunId().IsEmpty()
		? TEXT("No run started") : Bridge.GetGenerationRunId();
	const FString RunMetadata = FString::Printf(
		TEXT("RUN %s\nSTATE %s  |  PROVIDER %s  |  ELAPSED %.1fs\n"
			"RECORDED STAGES %d  |  REPAIRS %d  |  VALIDATION ISSUES %d\n"
			"TOKENS input %lld  |  cached %lld  |  output %lld  |  reasoning %lld\n"
			"COST Provider-billed monetary cost is not reported by the local Codex CLI."),
		*RunId,
		*State,
		*Bridge.GetProviderName(),
		Bridge.GetGenerationElapsedSeconds(),
		Metrics.Num(),
		Bridge.GetGenerationRepairAttempts(),
		Bridge.GetGenerationIssueHistory().Num(),
		TotalInputTokens,
		TotalCachedTokens,
		TotalOutputTokens,
		TotalReasoningTokens);
	if (RunMetadataText != nullptr && RunMetadata != LastRunMetadata)
	{
		LastRunMetadata = RunMetadata;
		RunMetadataText->SetText(FText::FromString(RunMetadata));
	}
	if (RunProgress != nullptr)
	{
		RunProgress->SetPercent(GenerationProgress(GenerationStage));
		RunProgress->SetFillColorAndOpacity(GenerationAccent(GenerationStage, Bridge.IsGenerationRunning()));
	}

	FString StageMetadata = TEXT("No AI stage has been recorded. Start a run to populate request, prompt, response, event, and telemetry views.");
	if (Metrics.IsValidIndex(SelectedMetricIndex))
	{
		const FWorldDirectorGenerationStageMetric& Metric = Metrics[SelectedMetricIndex];
		const FString ExitCode = Metric.ExitCode == INDEX_NONE
			? TEXT("n/a") : FString::FromInt(Metric.ExitCode);
		StageMetadata = FString::Printf(
			TEXT("STAGE %d OF %d - %s - %s\n"
				"MODEL %s  |  REASONING %s  |  DURATION %.2fs  |  EXIT %s\n"
				"TOKENS input %lld  |  cached %lld  |  output %lld  |  reasoning %lld\n"
				"PAYLOAD request %s  |  response %s  |  prompt characters %lld\n"
				"THREAD %s\nCOST %s%s%s"),
			SelectedMetricIndex + 1,
			Metrics.Num(),
			Metric.Stage.IsEmpty() ? TEXT("pending") : *Metric.Stage,
			*StageStateLabel(Metric),
			Metric.Model.IsEmpty() ? TEXT("not reported") : *Metric.Model,
			Metric.ReasoningEffort.IsEmpty() ? TEXT("not reported") : *Metric.ReasoningEffort,
			Metric.DurationSeconds,
			*ExitCode,
			Metric.InputTokens,
			Metric.CachedInputTokens,
			Metric.OutputTokens,
			Metric.ReasoningOutputTokens,
			*FormatBytes(Metric.RequestBytes),
			*FormatBytes(Metric.ResponseBytes),
			Metric.PromptCharacters,
			Metric.ProviderThreadId.IsEmpty() ? TEXT("not reported") : *Metric.ProviderThreadId,
			Metric.CostNote.IsEmpty() ? TEXT("No monetary amount reported") : *Metric.CostNote,
			Metric.Error.IsEmpty() ? TEXT("") : TEXT("\nERROR "),
			*Metric.Error);
	}
	if (StageMetadataText != nullptr && StageMetadata != LastStageMetadata)
	{
		LastStageMetadata = StageMetadata;
		StageMetadataText->SetText(FText::FromString(StageMetadata));
	}
}

void UWorldDirectorGenerationDiagnosticsWidget::RefreshDiagnostics()
{
	const UDirectorBridgeSubsystem* Bridge = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UDirectorBridgeSubsystem>() : nullptr;
	if (DiagnosticsText == nullptr || ViewContextText == nullptr)
	{
		return;
	}
	if (Bridge == nullptr)
	{
		UpdateNavigationState(0);
		SetDiagnosticText(TEXT("VIEW: UNAVAILABLE"), TEXT("Director bridge is unavailable."));
		return;
	}

	RefreshMetadata(*Bridge);
	if (View == EDiagnosticView::Summary)
	{
		SetDiagnosticText(TEXT("VIEW: RUN OVERVIEW"), Bridge->BuildGenerationDiagnosticReport());
		return;
	}
	if (View == EDiagnosticView::World || View == EDiagnosticView::Candidates)
	{
		const bool bWorld = View == EDiagnosticView::World;
		const FString Context = bWorld
			? TEXT("VIEW: PHYSICAL WORLD - replayable V3 terrain, surfaces, routes, plots, lore, and fingerprints")
			: TEXT("VIEW: LAYOUT CANDIDATES - semantic metrics and physical fingerprints side by side");
		const FString Path = FPaths::Combine(Bridge->GetGenerationRunDirectory(),
			bWorld ? TEXT("06-resolved-world-v3.json") : TEXT("world-generation-lab.json"));
		FString Contents;
		if (!FFileHelper::LoadFileToString(Contents, *Path))
		{
			Contents = FString::Printf(TEXT("Artifact has not been produced yet.\n\nExpected path:\n%s"), *Path);
		}
		SetDiagnosticText(Context, Contents, Path);
		return;
	}

	const TArray<FWorldDirectorGenerationStageMetric>& Metrics = Bridge->GetGenerationMetrics();
	if (Metrics.IsEmpty())
	{
		SetDiagnosticText(
			TEXT("VIEW: SELECTED STAGE ARTIFACT"),
			TEXT("No AI stage artifacts exist yet. Start generation, then use the stage selector above."));
		return;
	}
	if (!Metrics.IsValidIndex(SelectedMetricIndex))
	{
		SelectedMetricIndex = Metrics.Num() - 1;
	}
	const FWorldDirectorGenerationStageMetric& Metric = Metrics[SelectedMetricIndex];
	FString ViewName;
	FString Path;
	switch (View)
	{
	case EDiagnosticView::Request:
		ViewName = TEXT("REQUEST ENVELOPE");
		Path = Metric.RequestPath;
		break;
	case EDiagnosticView::Prompt:
		ViewName = TEXT("CONSTRUCTED PROMPT");
		Path = Metric.PromptPath;
		break;
	case EDiagnosticView::Response:
		ViewName = TEXT("RAW MODEL RESPONSE");
		Path = Metric.RawResponsePath.IsEmpty() ? Metric.ResponsePath : Metric.RawResponsePath;
		break;
	case EDiagnosticView::Events:
		ViewName = TEXT("RAW PROVIDER EVENTS");
		Path = Metric.ProviderEventsPath;
		break;
	case EDiagnosticView::Telemetry:
		ViewName = TEXT("PARSED TELEMETRY");
		Path = Metric.TelemetryPath;
		break;
	default:
		ViewName = TEXT("STAGE ARTIFACT");
		break;
	}
	const FString Context = FString::Printf(
		TEXT("VIEW: %s - STAGE %d/%d %s - MODEL %s - REASONING %s"),
		*ViewName,
		SelectedMetricIndex + 1,
		Metrics.Num(),
		Metric.Stage.IsEmpty() ? TEXT("pending") : *Metric.Stage,
		Metric.Model.IsEmpty() ? TEXT("pending") : *Metric.Model,
		Metric.ReasoningEffort.IsEmpty() ? TEXT("pending") : *Metric.ReasoningEffort);
	FString Contents;
	if (Path.IsEmpty())
	{
		Contents = TEXT("This artifact has not been produced yet.");
	}
	else if (!FFileHelper::LoadFileToString(Contents, *Path))
	{
		Contents = FString::Printf(TEXT("Could not read artifact.\n\nExpected path:\n%s"), *Path);
	}
	SetDiagnosticText(Context, Contents, Path);
}

void UWorldDirectorGenerationDiagnosticsWidget::CopyDiagnostics()
{
	FString Report = LastRenderedContext;
	if (!LastRunMetadata.IsEmpty())
	{
		Report += TEXT("\n\n") + LastRunMetadata;
	}
	if (!LastStageMetadata.IsEmpty())
	{
		Report += TEXT("\n\n") + LastStageMetadata;
	}
	if (!CurrentArtifactPath.IsEmpty())
	{
		Report += TEXT("\n\nARTIFACT: ") + CurrentArtifactPath;
	}
	if (!LastRenderedBody.IsEmpty())
	{
		Report += TEXT("\n\n") + LastRenderedBody;
	}
	FPlatformApplicationMisc::ClipboardCopy(*Report);
}

void UWorldDirectorGenerationDiagnosticsWidget::SetView(const EDiagnosticView InView)
{
	View = InView;
	RefreshAccumulator = 0.0;
	UpdateViewButtonStyles();
	RefreshDiagnostics();
	if (DiagnosticsScroll != nullptr)
	{
		DiagnosticsScroll->SetScrollOffset(0.0f);
	}
}

void UWorldDirectorGenerationDiagnosticsWidget::UpdateNavigationState(const int32 MetricCount)
{
	if (PreviousStageButton != nullptr)
	{
		PreviousStageButton->SetIsEnabled(MetricCount > 0 && SelectedMetricIndex > 0);
	}
	if (NextStageButton != nullptr)
	{
		NextStageButton->SetIsEnabled(
			MetricCount > 0 && SelectedMetricIndex >= 0 && SelectedMetricIndex < MetricCount - 1);
	}
}

void UWorldDirectorGenerationDiagnosticsWidget::UpdateViewButtonStyles()
{
	const auto Apply = [this](UButton* Button, const EDiagnosticView ButtonView)
	{
		if (Button != nullptr)
		{
			Button->SetBackgroundColor(View == ButtonView ? ButtonActive : ButtonIdle);
		}
	};
	Apply(SummaryButton, EDiagnosticView::Summary);
	Apply(RequestButton, EDiagnosticView::Request);
	Apply(PromptButton, EDiagnosticView::Prompt);
	Apply(ResponseButton, EDiagnosticView::Response);
	Apply(EventsButton, EDiagnosticView::Events);
	Apply(TelemetryButton, EDiagnosticView::Telemetry);
	Apply(WorldButton, EDiagnosticView::World);
	Apply(CandidatesButton, EDiagnosticView::Candidates);
}

void UWorldDirectorGenerationDiagnosticsWidget::ShowSummary()
{
	SetView(EDiagnosticView::Summary);
}

void UWorldDirectorGenerationDiagnosticsWidget::ShowRequest()
{
	SetView(EDiagnosticView::Request);
}

void UWorldDirectorGenerationDiagnosticsWidget::ShowPrompt()
{
	SetView(EDiagnosticView::Prompt);
}

void UWorldDirectorGenerationDiagnosticsWidget::ShowResponse()
{
	SetView(EDiagnosticView::Response);
}

void UWorldDirectorGenerationDiagnosticsWidget::ShowEvents()
{
	SetView(EDiagnosticView::Events);
}

void UWorldDirectorGenerationDiagnosticsWidget::ShowTelemetry()
{
	SetView(EDiagnosticView::Telemetry);
}

void UWorldDirectorGenerationDiagnosticsWidget::ShowWorld()
{
	SetView(EDiagnosticView::World);
}

void UWorldDirectorGenerationDiagnosticsWidget::ShowCandidates()
{
	SetView(EDiagnosticView::Candidates);
}

void UWorldDirectorGenerationDiagnosticsWidget::PreviousStage()
{
	const UDirectorBridgeSubsystem* Bridge = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UDirectorBridgeSubsystem>() : nullptr;
	const int32 MetricCount = Bridge ? Bridge->GetGenerationMetrics().Num() : 0;
	if (MetricCount <= 0)
	{
		UpdateNavigationState(0);
		return;
	}
	SelectedMetricIndex = FMath::Clamp(SelectedMetricIndex - 1, 0, MetricCount - 1);
	RefreshDiagnostics();
}

void UWorldDirectorGenerationDiagnosticsWidget::NextStage()
{
	const UDirectorBridgeSubsystem* Bridge = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UDirectorBridgeSubsystem>() : nullptr;
	const int32 MetricCount = Bridge ? Bridge->GetGenerationMetrics().Num() : 0;
	if (MetricCount <= 0)
	{
		UpdateNavigationState(0);
		return;
	}
	SelectedMetricIndex = FMath::Clamp(SelectedMetricIndex + 1, 0, MetricCount - 1);
	RefreshDiagnostics();
}

void UWorldDirectorGenerationDiagnosticsWidget::RevealCurrentArtifact()
{
	if (!CurrentArtifactPath.IsEmpty())
	{
		FPlatformProcess::ExploreFolder(*FPaths::GetPath(CurrentArtifactPath));
	}
}

void UWorldDirectorGenerationDiagnosticsWidget::OpenRunFolder()
{
	const UDirectorBridgeSubsystem* Bridge = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UDirectorBridgeSubsystem>() : nullptr;
	if (Bridge != nullptr && !Bridge->GetGenerationRunDirectory().IsEmpty())
	{
		FPlatformProcess::ExploreFolder(*Bridge->GetGenerationRunDirectory());
	}
}

void UWorldDirectorGenerationDiagnosticsWidget::CloseDiagnostics()
{
	if (Bootstrap != nullptr)
	{
		Bootstrap->CloseGenerationDiagnostics();
	}
	else
	{
		RemoveFromParent();
	}
}

bool UWorldDirectorGenerationDiagnosticsWidget::IsDiagnosticsReady() const
{
	return DiagnosticsText != nullptr && !DiagnosticsText->GetText().IsEmpty();
}
