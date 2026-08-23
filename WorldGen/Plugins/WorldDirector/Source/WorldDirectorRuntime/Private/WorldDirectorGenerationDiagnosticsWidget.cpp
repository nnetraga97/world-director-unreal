#include "WorldDirectorGenerationDiagnosticsWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "WorldDirectorSubsystems.h"
#include "WorldDirectorTownActors.h"

namespace
{
UButton* AddDiagnosticButton(UWidgetTree* Tree, UHorizontalBox* Row, const FString& Label)
{
	UButton* Button = Tree->ConstructWidget<UButton>();
	UTextBlock* Text = Tree->ConstructWidget<UTextBlock>();
	Text->SetText(FText::FromString(Label));
	Text->SetColorAndOpacity(FSlateColor(FLinearColor(0.94f, 0.87f, 0.68f)));
	Button->SetContent(Text);
	if (UHorizontalBoxSlot* Slot = Row->AddChildToHorizontalBox(Button))
	{
		Slot->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
	}
	return Button;
}

UButton* AddDiagnosticButton(UWidgetTree* Tree, UWrapBox* Row, const FString& Label)
{
	UButton* Button = Tree->ConstructWidget<UButton>();
	UTextBlock* Text = Tree->ConstructWidget<UTextBlock>();
	Text->SetText(FText::FromString(Label));
	Text->SetColorAndOpacity(FSlateColor(FLinearColor(0.94f, 0.87f, 0.68f)));
	Button->SetContent(Text);
	if (UWrapBoxSlot* Slot = Row->AddChildToWrapBox(Button))
	{
		Slot->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 8.0f));
	}
	return Button;
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
	RefreshDiagnostics();
}

void UWorldDirectorGenerationDiagnosticsWidget::ApplyViewportLayout()
{
	SetAnchorsInViewport(FAnchors(0.0f, 0.0f));
	SetAlignmentInViewport(FVector2D(0.0f, 0.0f));
	SetPositionInViewport(FVector2D(24.0f, 24.0f), false);
	const FVector2D ViewportSize = UWidgetLayoutLibrary::GetViewportSize(this);
	SetDesiredSizeInViewport(FVector2D(
		FMath::Min(980.0, FMath::Max(360.0, ViewportSize.X - 48.0)),
		FMath::Min(920.0, FMath::Max(420.0, ViewportSize.Y - 48.0))));
}

void UWorldDirectorGenerationDiagnosticsWidget::BuildWidgetTree()
{
	if (WidgetTree == nullptr || WidgetTree->RootWidget != nullptr)
	{
		return;
	}
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
	Panel->SetBrushColor(FLinearColor(0.018f, 0.020f, 0.023f, 0.985f));
	Panel->SetPadding(FMargin(24.0f));
	WidgetTree->RootWidget = Panel;
	UVerticalBox* Layout = WidgetTree->ConstructWidget<UVerticalBox>();
	Panel->SetContent(Layout);
	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>();
	Title->SetText(FText::FromString(TEXT("AI GENERATION DIAGNOSTICS")));
	Title->SetFont(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 25));
	Title->SetColorAndOpacity(FSlateColor(FLinearColor(0.42f, 0.78f, 0.96f)));
	Layout->AddChildToVerticalBox(Title);
	UTextBlock* Help = WidgetTree->ConstructWidget<UTextBlock>();
	Help->SetText(FText::FromString(
		TEXT("Inspect each stage's exact request, constructed prompt, structured response, raw provider events, token usage, latency, and validation repairs. Hidden chain-of-thought is not available or recorded.")));
	Help->SetAutoWrapText(true);
	Help->SetColorAndOpacity(FSlateColor(FLinearColor(0.68f, 0.72f, 0.76f)));
	if (UVerticalBoxSlot* Slot = Layout->AddChildToVerticalBox(Help))
	{
		Slot->SetPadding(FMargin(0.0f, 6.0f, 0.0f, 12.0f));
	}
	UWrapBox* Views = WidgetTree->ConstructWidget<UWrapBox>();
	if (UVerticalBoxSlot* Slot = Layout->AddChildToVerticalBox(Views))
	{
		Slot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
	}
	UButton* Previous = AddDiagnosticButton(WidgetTree, Views, TEXT("Previous stage"));
	Previous->OnClicked.AddDynamic(this, &UWorldDirectorGenerationDiagnosticsWidget::PreviousStage);
	UButton* Next = AddDiagnosticButton(WidgetTree, Views, TEXT("Next stage"));
	Next->OnClicked.AddDynamic(this, &UWorldDirectorGenerationDiagnosticsWidget::NextStage);
	UButton* Summary = AddDiagnosticButton(WidgetTree, Views, TEXT("Summary"));
	Summary->OnClicked.AddDynamic(this, &UWorldDirectorGenerationDiagnosticsWidget::ShowSummary);
	UButton* Request = AddDiagnosticButton(WidgetTree, Views, TEXT("Request"));
	Request->OnClicked.AddDynamic(this, &UWorldDirectorGenerationDiagnosticsWidget::ShowRequest);
	UButton* Prompt = AddDiagnosticButton(WidgetTree, Views, TEXT("Prompt"));
	Prompt->OnClicked.AddDynamic(this, &UWorldDirectorGenerationDiagnosticsWidget::ShowPrompt);
	UButton* Response = AddDiagnosticButton(WidgetTree, Views, TEXT("Response"));
	Response->OnClicked.AddDynamic(this, &UWorldDirectorGenerationDiagnosticsWidget::ShowResponse);
	UButton* Events = AddDiagnosticButton(WidgetTree, Views, TEXT("Events"));
	Events->OnClicked.AddDynamic(this, &UWorldDirectorGenerationDiagnosticsWidget::ShowEvents);
	UButton* Telemetry = AddDiagnosticButton(WidgetTree, Views, TEXT("Telemetry"));
	Telemetry->OnClicked.AddDynamic(this, &UWorldDirectorGenerationDiagnosticsWidget::ShowTelemetry);
	UButton* World = AddDiagnosticButton(WidgetTree, Views, TEXT("Physical world"));
	World->OnClicked.AddDynamic(this, &UWorldDirectorGenerationDiagnosticsWidget::ShowWorld);
	UButton* Candidates = AddDiagnosticButton(WidgetTree, Views, TEXT("Candidates"));
	Candidates->OnClicked.AddDynamic(this, &UWorldDirectorGenerationDiagnosticsWidget::ShowCandidates);

	ViewContextText = WidgetTree->ConstructWidget<UTextBlock>();
	ViewContextText->SetColorAndOpacity(FSlateColor(FLinearColor(0.42f, 0.78f, 0.96f)));
	ViewContextText->SetAutoWrapText(true);
	if (UVerticalBoxSlot* Slot = Layout->AddChildToVerticalBox(ViewContextText))
	{
		Slot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
	}

	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
	if (UVerticalBoxSlot* Slot = Layout->AddChildToVerticalBox(Scroll))
	{
		Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Slot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
	}
	DiagnosticsText = WidgetTree->ConstructWidget<UTextBlock>();
	DiagnosticsText->SetAutoWrapText(true);
	DiagnosticsText->SetFont(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 13));
	DiagnosticsText->SetColorAndOpacity(FSlateColor(FLinearColor(0.84f, 0.86f, 0.86f)));
	Scroll->AddChild(DiagnosticsText);
	UHorizontalBox* Actions = WidgetTree->ConstructWidget<UHorizontalBox>();
	Layout->AddChildToVerticalBox(Actions);
	UButton* Copy = AddDiagnosticButton(WidgetTree, Actions, TEXT("Copy current view"));
	Copy->OnClicked.AddDynamic(this, &UWorldDirectorGenerationDiagnosticsWidget::CopyDiagnostics);
	UButton* Folder = AddDiagnosticButton(WidgetTree, Actions, TEXT("Open run folder"));
	Folder->OnClicked.AddDynamic(this, &UWorldDirectorGenerationDiagnosticsWidget::OpenRunFolder);
	UButton* Close = AddDiagnosticButton(WidgetTree, Actions, TEXT("Close"));
	Close->OnClicked.AddDynamic(this, &UWorldDirectorGenerationDiagnosticsWidget::CloseDiagnostics);
}

void UWorldDirectorGenerationDiagnosticsWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshAccumulator += InDeltaTime;
	if (RefreshAccumulator >= 1.0)
	{
		RefreshAccumulator = 0.0;
		RefreshDiagnostics();
	}
}

void UWorldDirectorGenerationDiagnosticsWidget::SetDiagnosticText(
	const FString& Context, const FString& Body)
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
		SetDiagnosticText(TEXT("Unavailable"), TEXT("Director bridge is unavailable."));
		return;
	}
	if (View == EDiagnosticView::Summary)
	{
		SetDiagnosticText(TEXT("SUMMARY — live run overview"), Bridge->BuildGenerationDiagnosticReport());
		return;
	}
	if (View == EDiagnosticView::World || View == EDiagnosticView::Candidates)
	{
		const bool bWorld = View == EDiagnosticView::World;
		const FString Context = bWorld
			? TEXT("PHYSICAL WORLD — replayable V2 recipe, terrain, surfaces, routes, plots, and fingerprints")
			: TEXT("CANDIDATES — side-by-side semantic metrics and physical fingerprints");
		const FString Path = FPaths::Combine(Bridge->GetGenerationRunDirectory(),
			bWorld ? TEXT("06-resolved-world-v2.json") : TEXT("world-generation-lab.json"));
		FString Contents;
		if (!FFileHelper::LoadFileToString(Contents, *Path))
		{
			Contents = FString::Printf(TEXT("Artifact has not been produced yet:\n%s"), *Path);
		}
		SetDiagnosticText(Context, Contents);
		return;
	}

	const TArray<FWorldDirectorGenerationStageMetric>& Metrics = Bridge->GetGenerationMetrics();
	if (Metrics.IsEmpty())
	{
		SetDiagnosticText(TEXT("No stage selected"), TEXT("No AI stage artifacts exist yet."));
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
		ViewName = TEXT("REQUEST");
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
		break;
	}
	const FString Context = FString::Printf(
		TEXT("%s — stage %d/%d: %s — model %s — reasoning %s"),
		*ViewName, SelectedMetricIndex + 1, Metrics.Num(), *Metric.Stage,
		Metric.Model.IsEmpty() ? TEXT("pending") : *Metric.Model,
		Metric.ReasoningEffort.IsEmpty() ? TEXT("pending") : *Metric.ReasoningEffort);
	FString Contents;
	if (Path.IsEmpty())
	{
		Contents = TEXT("This artifact has not been produced yet.");
	}
	else if (!FFileHelper::LoadFileToString(Contents, *Path))
	{
		Contents = FString::Printf(TEXT("Could not read artifact:\n%s"), *Path);
	}
	SetDiagnosticText(Context, Contents);
}

void UWorldDirectorGenerationDiagnosticsWidget::CopyDiagnostics()
{
	if (DiagnosticsText != nullptr)
	{
		FPlatformApplicationMisc::ClipboardCopy(*DiagnosticsText->GetText().ToString());
	}
}

void UWorldDirectorGenerationDiagnosticsWidget::SetView(const EDiagnosticView InView)
{
	View = InView;
	RefreshAccumulator = 0.0;
	RefreshDiagnostics();
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
	SelectedMetricIndex = FMath::Max(0, SelectedMetricIndex - 1);
	RefreshDiagnostics();
}

void UWorldDirectorGenerationDiagnosticsWidget::NextStage()
{
	const UDirectorBridgeSubsystem* Bridge = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UDirectorBridgeSubsystem>() : nullptr;
	const int32 LastIndex = Bridge ? Bridge->GetGenerationMetrics().Num() - 1 : INDEX_NONE;
	SelectedMetricIndex = FMath::Min(LastIndex, SelectedMetricIndex + 1);
	RefreshDiagnostics();
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
