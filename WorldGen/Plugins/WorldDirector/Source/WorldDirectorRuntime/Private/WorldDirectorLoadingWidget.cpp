#include "WorldDirectorLoadingWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"
#include "WorldDirectorRuntime.h"
#include "WorldDirectorSubsystems.h"
#include "WorldDirectorTownActors.h"

namespace WorldDirectorLoadingWidgetPrivate
{
const FLinearColor TextPrimary(0.96f, 0.94f, 0.90f);
const FLinearColor TextSecondary(0.69f, 0.71f, 0.72f);
const FLinearColor TextMuted(0.46f, 0.49f, 0.51f);
const FLinearColor AccentAmber(0.94f, 0.70f, 0.28f);
const FLinearColor AccentBlue(0.31f, 0.70f, 0.94f);

UTextBlock* AddText(UWidgetTree* Tree, UVerticalBox* Layout, const FString& Text, const int32 Size,
	const FLinearColor& Color, const FMargin& Padding = FMargin(0.0f))
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

UButton* AddButton(UWidgetTree* Tree, UVerticalBox* Layout, const FString& Label, const FLinearColor& Color)
{
	UButton* Button = Tree->ConstructWidget<UButton>();
	Button->SetBackgroundColor(Color);
	UBorder* Padding = Tree->ConstructWidget<UBorder>();
	Padding->SetBrushColor(FLinearColor::Transparent);
	Padding->SetPadding(FMargin(16.0f, 10.0f));
	UTextBlock* Text = Tree->ConstructWidget<UTextBlock>();
	Text->SetText(FText::FromString(Label));
	Text->SetFont(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 13));
	Text->SetColorAndOpacity(FSlateColor(TextPrimary));
	Text->SetJustification(ETextJustify::Center);
	Padding->SetContent(Text);
	Button->SetContent(Padding);
	if (UVerticalBoxSlot* Slot = Layout->AddChildToVerticalBox(Button))
	{
		Slot->SetPadding(FMargin(0.0f, 14.0f, 0.0f, 0.0f));
	}
	return Button;
}
}

void UWorldDirectorLoadingWidget::InitializeForBootstrap(AWorldDirectorFixtureBootstrap* InBootstrap)
{
	Bootstrap = InBootstrap;
}

void UWorldDirectorLoadingWidget::SetForSampleWorld(const bool bInSampleWorld)
{
	bSampleWorld = bInSampleWorld;
	SampleProgress = 0.16f;
}

TSharedRef<SWidget> UWorldDirectorLoadingWidget::RebuildWidget()
{
	BuildWidgetTree();
	return Super::RebuildWidget();
}

void UWorldDirectorLoadingWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	RefreshLoadingState();
}

bool UWorldDirectorLoadingWidget::ApplyViewportLayout()
{
	const FVector2D ViewportSize = UWidgetLayoutLibrary::GetViewportSize(this);
	if (ViewportSize.X <= 0.0f || ViewportSize.Y <= 0.0f)
	{
		return false;
	}
	SetAnchorsInViewport(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
	SetAlignmentInViewport(FVector2D::ZeroVector);
	return true;
}

void UWorldDirectorLoadingWidget::BuildWidgetTree()
{
	if (WidgetTree == nullptr || WidgetTree->RootWidget != nullptr)
	{
		return;
	}
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>();
	WidgetTree->RootWidget = Root;
	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>();
	Backdrop->SetBrushColor(FLinearColor(0.008f, 0.012f, 0.018f, 0.985f));
	Root->AddChildToOverlay(Backdrop);

	USizeBox* CardSize = WidgetTree->ConstructWidget<USizeBox>();
	CardSize->SetMinDesiredWidth(520.0f);
	CardSize->SetMaxDesiredWidth(680.0f);
	if (UOverlaySlot* Slot = Root->AddChildToOverlay(CardSize))
	{
		Slot->SetHorizontalAlignment(HAlign_Center);
		Slot->SetVerticalAlignment(VAlign_Center);
	}

	UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
	Card->SetBrushColor(FLinearColor(0.035f, 0.045f, 0.058f, 0.98f));
	Card->SetPadding(FMargin(38.0f, 34.0f));
	CardSize->SetContent(Card);
	UVerticalBox* Layout = WidgetTree->ConstructWidget<UVerticalBox>();
	Card->SetContent(Layout);
	WorldDirectorLoadingWidgetPrivate::AddText(WidgetTree, Layout, TEXT("WORLD DIRECTOR"), 12, WorldDirectorLoadingWidgetPrivate::AccentAmber);
	TitleText = WorldDirectorLoadingWidgetPrivate::AddText(WidgetTree, Layout, TEXT("BUILDING YOUR WORLD"), 29, WorldDirectorLoadingWidgetPrivate::TextPrimary,
		FMargin(0.0f, 9.0f, 0.0f, 0.0f));
	StageText = WorldDirectorLoadingWidgetPrivate::AddText(WidgetTree, Layout, TEXT("Preparing the director..."), 15, WorldDirectorLoadingWidgetPrivate::AccentBlue,
		FMargin(0.0f, 8.0f, 0.0f, 0.0f));
	ProgressBar = WidgetTree->ConstructWidget<UProgressBar>();
	ProgressBar->SetPercent(0.0f);
	ProgressBar->SetFillColorAndOpacity(WorldDirectorLoadingWidgetPrivate::AccentBlue);
	if (UVerticalBoxSlot* Slot = Layout->AddChildToVerticalBox(ProgressBar))
	{
		Slot->SetPadding(FMargin(0.0f, 24.0f, 0.0f, 0.0f));
	}
	DetailText = WorldDirectorLoadingWidgetPrivate::AddText(WidgetTree, Layout,
		TEXT("The current world remains safe while the next one is interpreted, validated, and compiled."),
		13, WorldDirectorLoadingWidgetPrivate::TextSecondary, FMargin(0.0f, 13.0f, 0.0f, 0.0f));
	CancelButton = WorldDirectorLoadingWidgetPrivate::AddButton(WidgetTree, Layout, TEXT("CANCEL GENERATION"), FLinearColor(0.28f, 0.08f, 0.07f, 1.0f));
	CancelButton->OnClicked.AddDynamic(this, &UWorldDirectorLoadingWidget::CancelGeneration);
	DiagnosticsButton = WorldDirectorLoadingWidgetPrivate::AddButton(WidgetTree, Layout, TEXT("OPEN AI DIAGNOSTICS"), FLinearColor(0.08f, 0.21f, 0.29f, 1.0f));
	DiagnosticsButton->OnClicked.AddDynamic(this, &UWorldDirectorLoadingWidget::OpenDiagnostics);
	WorldDirectorLoadingWidgetPrivate::AddText(WidgetTree, Layout, TEXT("Generation can take several minutes in live model mode."), 11, WorldDirectorLoadingWidgetPrivate::TextMuted,
		FMargin(0.0f, 18.0f, 0.0f, 0.0f));
}

void UWorldDirectorLoadingWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshAccumulator += InDeltaTime;
	if (RefreshAccumulator >= 0.2f)
	{
		RefreshAccumulator = 0.0f;
		RefreshLoadingState();
	}
}

void UWorldDirectorLoadingWidget::RefreshLoadingState()
{
	if (TitleText == nullptr || StageText == nullptr || ProgressBar == nullptr || DetailText == nullptr)
	{
		return;
	}
	if (bSampleWorld)
	{
		TitleText->SetText(FText::FromString(TEXT("LOADING THE DEMO TOWN")));
		StageText->SetText(FText::FromString(TEXT("Compiling the certified offline fixture")));
		DetailText->SetText(FText::FromString(
			TEXT("Spawning the terrain, locations, residents, and simulation state for a safe first look.")));
		ProgressBar->SetPercent(SampleProgress);
		ProgressBar->SetFillColorAndOpacity(WorldDirectorLoadingWidgetPrivate::AccentAmber);
		if (CancelButton != nullptr) CancelButton->SetVisibility(ESlateVisibility::Collapsed);
		if (DiagnosticsButton != nullptr) DiagnosticsButton->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	UDirectorBridgeSubsystem* Bridge = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UDirectorBridgeSubsystem>() : nullptr;
	if (Bridge == nullptr)
	{
		return;
	}
	const EWorldDirectorGenerationStage Stage = Bridge->GetGenerationStage();
	float Progress = 0.04f;
	FString Label = TEXT("Preparing the director...");
	switch (Stage)
	{
	case EWorldDirectorGenerationStage::Interpret: Progress = 0.14f; Label = TEXT("Stage 1 of 5  /  Understanding the world brief"); break;
	case EWorldDirectorGenerationStage::Topology: Progress = 0.34f; Label = TEXT("Stage 2 of 5  /  Planning world relationships"); break;
	case EWorldDirectorGenerationStage::Layout: Progress = 0.54f; Label = TEXT("Stage 3 of 5  /  Designing terrain and settlement layout"); break;
	case EWorldDirectorGenerationStage::Population: Progress = 0.74f; Label = TEXT("Stage 4 of 5  /  Writing locations, people, and lore"); break;
	case EWorldDirectorGenerationStage::Repair: Progress = 0.82f; Label = FString::Printf(TEXT("Validation repair pass %d"), FMath::Max(1, Bridge->GetGenerationRepairAttempts())); break;
	case EWorldDirectorGenerationStage::Integrate: Progress = 0.92f; Label = TEXT("Stage 5 of 5  /  Building the playable world"); break;
	default: break;
	}
	TitleText->SetText(FText::FromString(TEXT("BUILDING YOUR WORLD")));
	StageText->SetText(FText::FromString(Label));
	DetailText->SetText(FText::FromString(FString::Printf(TEXT("Run %s  |  %.1f seconds elapsed"),
		Bridge->GetGenerationRunId().IsEmpty() ? TEXT("starting") : *Bridge->GetGenerationRunId(),
		Bridge->GetGenerationElapsedSeconds())));
	ProgressBar->SetPercent(Progress);
	ProgressBar->SetFillColorAndOpacity(WorldDirectorLoadingWidgetPrivate::AccentBlue);
}

void UWorldDirectorLoadingWidget::CancelGeneration()
{
	if (Bootstrap != nullptr)
	{
		Bootstrap->CancelPlayerWorldGeneration();
	}
}

void UWorldDirectorLoadingWidget::OpenDiagnostics()
{
	if (Bootstrap != nullptr)
	{
		Bootstrap->OpenGenerationDiagnostics();
	}
}
