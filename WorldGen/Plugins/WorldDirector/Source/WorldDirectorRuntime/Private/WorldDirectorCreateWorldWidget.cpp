#include "WorldDirectorCreateWorldWidget.h"
#include "WorldDirectorRuntime.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/ProgressBar.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "WorldDirectorSubsystems.h"
#include "WorldDirectorTownActors.h"

namespace
{
const FLinearColor TextPrimary(0.95f, 0.93f, 0.88f);
const FLinearColor TextSecondary(0.70f, 0.70f, 0.67f);
const FLinearColor TextMuted(0.54f, 0.55f, 0.55f);
const FLinearColor AccentAmber(0.94f, 0.70f, 0.28f);
const FLinearColor AccentBlue(0.30f, 0.70f, 0.94f);
const FLinearColor AccentGreen(0.38f, 0.78f, 0.50f);
const FLinearColor AccentRed(0.92f, 0.36f, 0.31f);

UTextBlock* AddText(
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

UVerticalBox* AddSection(
	UWidgetTree* Tree,
	UVerticalBox* Parent,
	const FString& Title,
	const FString& Description)
{
	UBorder* Card = Tree->ConstructWidget<UBorder>();
	Card->SetBrushColor(FLinearColor(0.055f, 0.052f, 0.048f, 1.0f));
	Card->SetPadding(FMargin(18.0f, 16.0f));
	if (UVerticalBoxSlot* CardSlot = Parent->AddChildToVerticalBox(Card))
	{
		CardSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
	}

	UVerticalBox* Content = Tree->ConstructWidget<UVerticalBox>();
	Card->SetContent(Content);
	AddText(Tree, Content, Title, 15, AccentAmber);
	if (!Description.IsEmpty())
	{
		AddText(Tree, Content, Description, 13, TextSecondary, FMargin(0.0f, 4.0f, 0.0f, 12.0f));
	}
	return Content;
}

UVerticalBox* AddField(
	UWidgetTree* Tree,
	UWrapBox* Row,
	const FString& Label,
	const FString& Description,
	const float Width)
{
	USizeBox* FieldSize = Tree->ConstructWidget<USizeBox>();
	FieldSize->SetMinDesiredWidth(Width);
	FieldSize->SetMaxDesiredWidth(Width);
	if (UWrapBoxSlot* Slot = Row->AddChildToWrapBox(FieldSize))
	{
		Slot->SetPadding(FMargin(0.0f, 0.0f, 16.0f, 14.0f));
	}

	UVerticalBox* Field = Tree->ConstructWidget<UVerticalBox>();
	FieldSize->SetContent(Field);
	AddText(Tree, Field, Label, 13, TextPrimary);
	AddText(Tree, Field, Description, 11, TextMuted, FMargin(0.0f, 3.0f, 0.0f, 7.0f));
	return Field;
}

UButton* AddActionButton(
	UWidgetTree* Tree,
	UWrapBox* Row,
	const FString& Label,
	const FLinearColor& Background)
{
	UButton* Button = Tree->ConstructWidget<UButton>();
	Button->SetBackgroundColor(Background);

	UBorder* Padding = Tree->ConstructWidget<UBorder>();
	Padding->SetBrushColor(FLinearColor::Transparent);
	Padding->SetPadding(FMargin(16.0f, 9.0f));
	UTextBlock* Text = Tree->ConstructWidget<UTextBlock>();
	Text->SetText(FText::FromString(Label));
	Text->SetFont(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 14));
	Text->SetColorAndOpacity(FSlateColor(TextPrimary));
	Text->SetJustification(ETextJustify::Center);
	Padding->SetContent(Text);
	Button->SetContent(Padding);

	if (UWrapBoxSlot* Slot = Row->AddChildToWrapBox(Button))
	{
		Slot->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 8.0f));
	}
	return Button;
}

struct FStagePresentation
{
	float Progress = 0.0f;
	FString Label = TEXT("Ready to configure");
};

FStagePresentation DescribeStage(
	const EWorldDirectorGenerationStage Stage,
	const int32 RepairAttempts)
{
	switch (Stage)
	{
	case EWorldDirectorGenerationStage::Interpret:
		return { 0.14f, TEXT("Stage 1 of 5 - Understanding the world brief") };
	case EWorldDirectorGenerationStage::Topology:
		return { 0.34f, TEXT("Stage 2 of 5 - Planning world relationships") };
	case EWorldDirectorGenerationStage::Layout:
		return { 0.54f, TEXT("Stage 3 of 5 - Designing terrain and settlement layout") };
	case EWorldDirectorGenerationStage::Population:
		return { 0.74f, TEXT("Stage 4 of 5 - Writing locations, people, and lore") };
	case EWorldDirectorGenerationStage::Integrate:
		return { 0.92f, TEXT("Stage 5 of 5 - Building the playable world") };
	case EWorldDirectorGenerationStage::Repair:
		return { 0.82f, FString::Printf(TEXT("Validation repair pass %d"), FMath::Max(1, RepairAttempts)) };
	case EWorldDirectorGenerationStage::Completed:
		return { 1.0f, TEXT("All generation stages completed") };
	case EWorldDirectorGenerationStage::Failed:
		return { 0.0f, TEXT("Generation stopped with an error") };
	case EWorldDirectorGenerationStage::Cancelled:
		return { 0.0f, TEXT("Generation cancelled") };
	default:
		return { 0.0f, TEXT("Ready to configure") };
	}
}
}

void UWorldDirectorCreateWorldWidget::InitializeForBootstrap(AWorldDirectorFixtureBootstrap* InBootstrap)
{
	Bootstrap = InBootstrap;
}

TSharedRef<SWidget> UWorldDirectorCreateWorldWidget::RebuildWidget()
{
	BuildWidgetTree();
	return Super::RebuildWidget();
}

void UWorldDirectorCreateWorldWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	UpdatePromptCharacterCount();
	RefreshStatus();
}

bool UWorldDirectorCreateWorldWidget::ApplyViewportLayout()
{
	const FVector2D ViewportSize = UWidgetLayoutLibrary::GetViewportSize(this);
	if (ViewportSize.X <= 0.0 || ViewportSize.Y <= 0.0)
	{
		return false;
	}

	const float Margin = ViewportSize.X < 720.0f ? 12.0f : 28.0f;
	const FVector2D DesiredSize(
		FMath::Max(1.0f, FMath::Min(840.0f, ViewportSize.X - Margin * 2.0f)),
		FMath::Max(1.0f, FMath::Min(860.0f, ViewportSize.Y - Margin * 2.0f)));
	const FVector2D Position(
		FMath::Max(Margin, (ViewportSize.X - DesiredSize.X) * 0.5f),
		FMath::Max(Margin, (ViewportSize.Y - DesiredSize.Y) * 0.5f));
	SetAnchorsInViewport(FAnchors(0.0f, 0.0f));
	SetAlignmentInViewport(FVector2D::ZeroVector);
	SetDesiredSizeInViewport(DesiredSize);
	SetPositionInViewport(Position, false);
	UE_LOG(LogWorldDirector, Display,
		TEXT("WORLD_DIRECTOR_PLAYER_MENU_LAYOUT viewport=%.0fx%.0f position=%.0f,%.0f size=%.0fx%.0f"),
		ViewportSize.X, ViewportSize.Y, Position.X, Position.Y, DesiredSize.X, DesiredSize.Y);
	return true;
}

void UWorldDirectorCreateWorldWidget::BuildWidgetTree()
{
	if (WidgetTree == nullptr || WidgetTree->RootWidget != nullptr)
	{
		return;
	}

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
	Panel->SetBrushColor(FLinearColor(0.022f, 0.021f, 0.020f, 0.99f));
	Panel->SetPadding(FMargin(24.0f));
	WidgetTree->RootWidget = Panel;

	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
	Scroll->SetScrollBarVisibility(ESlateVisibility::Visible);
	Panel->SetContent(Scroll);
	UVerticalBox* Layout = WidgetTree->ConstructWidget<UVerticalBox>();
	Scroll->AddChild(Layout);

	AddText(WidgetTree, Layout, TEXT("WORLD DIRECTOR"), 13, AccentAmber);
	AddText(WidgetTree, Layout, TEXT("Create a living world"), 31, TextPrimary,
		FMargin(0.0f, 3.0f, 0.0f, 0.0f));
	AddText(WidgetTree, Layout,
		TEXT("Give the director a strong premise, choose how the AI should work, then follow each stage as Unreal validates and builds the result."),
		15, TextSecondary, FMargin(0.0f, 8.0f, 0.0f, 18.0f));

	UVerticalBox* BriefSection = AddSection(
		WidgetTree,
		Layout,
		TEXT("1  WORLD BRIEF"),
		TEXT("Describe the terrain, culture, central tension, or landmark that should define this seed. Leave it blank for a fully invented world."));
	PromptInput = WidgetTree->ConstructWidget<UMultiLineEditableTextBox>();
	PromptInput->SetHintText(FText::FromString(
		TEXT("Example: A mountain river town built around an ancient mill, divided by old debts and a guarded civic secret.")));
	PromptInput->SetForegroundColor(TextPrimary);
	PromptInput->SetAutoWrapText(true);
	PromptInput->SetToolTipText(FText::FromString(
		TEXT("Optional creative direction sent to the selected model. Ctrl+Enter starts generation.")));
	PromptInput->OnTextChanged.AddDynamic(this, &UWorldDirectorCreateWorldWidget::HandlePromptChanged);
	USizeBox* PromptSize = WidgetTree->ConstructWidget<USizeBox>();
	PromptSize->SetMinDesiredHeight(145.0f);
	PromptSize->SetContent(PromptInput);
	BriefSection->AddChildToVerticalBox(PromptSize);
	PromptCharacterText = AddText(
		WidgetTree, BriefSection, TEXT("0 characters - Optional"), 11, TextMuted,
		FMargin(0.0f, 7.0f, 0.0f, 0.0f));

	UVerticalBox* ConfigurationSection = AddSection(
		WidgetTree,
		Layout,
		TEXT("2  GENERATION SETTINGS"),
		TEXT("The seed controls deterministic world construction. Model and reasoning selections are locked once a run starts."));
	UWrapBox* Fields = WidgetTree->ConstructWidget<UWrapBox>();
	Fields->SetInnerSlotPadding(FVector2D(0.0f, 0.0f));
	ConfigurationSection->AddChildToVerticalBox(Fields);

	UVerticalBox* SeedField = AddField(
		WidgetTree, Fields, TEXT("Seed"), TEXT("Reuse this number to replay a world."), 170.0f);
	SeedInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	SeedInput->SetText(FText::AsNumber(FMath::RandRange(1000, 999999)));
	SeedInput->SetToolTipText(FText::FromString(TEXT("Positive whole number used for deterministic generation.")));
	SeedField->AddChildToVerticalBox(SeedInput);

	UVerticalBox* ModelField = AddField(
		WidgetTree, Fields, TEXT("Model"), TEXT("Balances generation quality and speed."), 225.0f);
	ModelInput = WidgetTree->ConstructWidget<UComboBoxString>();
	for (const TCHAR* Model : {
		TEXT("gpt-5.6-terra"), TEXT("gpt-5.6-sol"), TEXT("gpt-5.6-luna"), TEXT("gpt-5.5") })
	{
		ModelInput->AddOption(Model);
	}
	ModelInput->SetSelectedOption(TEXT("gpt-5.6-terra"));
	ModelInput->SetToolTipText(FText::FromString(
		TEXT("AI model used for semantic world planning. Terra is the balanced default.")));
	ModelField->AddChildToVerticalBox(ModelInput);

	UVerticalBox* ReasoningField = AddField(
		WidgetTree, Fields, TEXT("Reasoning"), TEXT("More reasoning can increase run time."), 190.0f);
	ReasoningInput = WidgetTree->ConstructWidget<UComboBoxString>();
	RefreshReasoningOptions(ModelInput->GetSelectedOption());
	ModelInput->OnSelectionChanged.AddDynamic(this, &UWorldDirectorCreateWorldWidget::HandleModelChanged);
	ReasoningInput->SetToolTipText(FText::FromString(
		TEXT("Reasoning effort requested from the selected model. Medium is the balanced default.")));
	ReasoningField->AddChildToVerticalBox(ReasoningInput);

	FixtureCheck = WidgetTree->ConstructWidget<UCheckBox>();
	FixtureCheck->SetToolTipText(FText::FromString(
		TEXT("Uses deterministic fixture data for UI and world-building tests. No AI model is contacted.")));
	FixtureCheck->OnCheckStateChanged.AddDynamic(this, &UWorldDirectorCreateWorldWidget::ToggleFixtureDebug);
	UTextBlock* FixtureLabel = WidgetTree->ConstructWidget<UTextBlock>();
	FixtureLabel->SetText(FText::FromString(TEXT("Use offline fixture - no model call")));
	FixtureLabel->SetFont(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 13));
	FixtureLabel->SetColorAndOpacity(FSlateColor(TextPrimary));
	FixtureCheck->SetContent(FixtureLabel);
	if (UVerticalBoxSlot* Slot = ConfigurationSection->AddChildToVerticalBox(FixtureCheck))
	{
		Slot->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 0.0f));
	}
	ProviderModeText = AddText(
		WidgetTree,
		ConfigurationSection,
		TEXT("LIVE MODEL MODE - Uses the locally authenticated Codex CLI. A run may take several minutes."),
		12,
		TextSecondary,
		FMargin(0.0f, 8.0f, 0.0f, 0.0f));

	StatusPanel = WidgetTree->ConstructWidget<UBorder>();
	StatusPanel->SetBrushColor(FLinearColor(0.10f, 0.075f, 0.035f, 1.0f));
	StatusPanel->SetPadding(FMargin(18.0f, 15.0f));
	if (UVerticalBoxSlot* StatusSlot = Layout->AddChildToVerticalBox(StatusPanel))
	{
		StatusSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
	}
	UVerticalBox* StatusLayout = WidgetTree->ConstructWidget<UVerticalBox>();
	StatusPanel->SetContent(StatusLayout);
	StatusText = AddText(WidgetTree, StatusLayout, TEXT("READY TO GENERATE"), 19, AccentAmber);
	ProgressStageText = AddText(
		WidgetTree, StatusLayout, TEXT("Ready to configure"), 12, TextSecondary,
		FMargin(0.0f, 4.0f, 0.0f, 8.0f));
	StageProgress = WidgetTree->ConstructWidget<UProgressBar>();
	StageProgress->SetPercent(0.0f);
	StageProgress->SetFillColorAndOpacity(AccentAmber);
	StatusLayout->AddChildToVerticalBox(StageProgress);
	DetailText = AddText(
		WidgetTree,
		StatusLayout,
		TEXT("Your brief and settings are ready. Open AI Diagnostics at any time to inspect the current or previous run."),
		13,
		TextSecondary,
		FMargin(0.0f, 9.0f, 0.0f, 0.0f));

	AddText(WidgetTree, Layout, TEXT("3  CREATE AND FOLLOW THE RUN"), 15, AccentAmber,
		FMargin(0.0f, 4.0f, 0.0f, 8.0f));
	UWrapBox* Actions = WidgetTree->ConstructWidget<UWrapBox>();
	Layout->AddChildToVerticalBox(Actions);
	CreateButton = AddActionButton(WidgetTree, Actions, TEXT("Create world"), FLinearColor(0.40f, 0.25f, 0.07f));
	CreateButton->SetToolTipText(FText::FromString(TEXT("Start generation (Ctrl+Enter).")));
	CreateButton->OnClicked.AddDynamic(this, &UWorldDirectorCreateWorldWidget::CreateWorld);
	DiagnosticsButton = AddActionButton(WidgetTree, Actions, TEXT("AI diagnostics"), FLinearColor(0.08f, 0.21f, 0.29f));
	DiagnosticsButton->SetToolTipText(FText::FromString(TEXT("Open the run inspector (F8).")));
	DiagnosticsButton->OnClicked.AddDynamic(this, &UWorldDirectorCreateWorldWidget::OpenDiagnostics);
	CancelButton = AddActionButton(WidgetTree, Actions, TEXT("Cancel run"), FLinearColor(0.28f, 0.08f, 0.07f));
	CancelButton->SetToolTipText(FText::FromString(TEXT("Cancel the active generation run.")));
	CancelButton->OnClicked.AddDynamic(this, &UWorldDirectorCreateWorldWidget::CancelGeneration);
	CancelButton->SetIsEnabled(false);
	CloseButton = AddActionButton(WidgetTree, Actions, TEXT("Close"), FLinearColor(0.12f, 0.12f, 0.12f));
	CloseButton->SetToolTipText(FText::FromString(TEXT("Return to the current world (Escape).")));
	CloseButton->OnClicked.AddDynamic(this, &UWorldDirectorCreateWorldWidget::CloseMenu);
	AddText(WidgetTree, Layout, TEXT("Keyboard: Ctrl+Enter create  |  F8 diagnostics  |  Escape close"),
		11, TextMuted, FMargin(0.0f, 2.0f, 0.0f, 0.0f));
}

void UWorldDirectorCreateWorldWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!IsVisible())
	{
		return;
	}
	RefreshAccumulator += InDeltaTime;
	if (RefreshAccumulator >= 0.2)
	{
		RefreshAccumulator = 0.0;
		RefreshStatus();
	}
}

FReply UWorldDirectorCreateWorldWidget::NativeOnPreviewKeyDown(
	const FGeometry& InGeometry,
	const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::F8)
	{
		OpenDiagnostics();
		return FReply::Handled();
	}
	if (InKeyEvent.GetKey() == EKeys::Escape && CloseButton != nullptr && CloseButton->GetIsEnabled())
	{
		CloseMenu();
		return FReply::Handled();
	}
	if (InKeyEvent.GetKey() == EKeys::Enter && InKeyEvent.IsControlDown() &&
		CreateButton != nullptr && CreateButton->GetIsEnabled())
	{
		CreateWorld();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

void UWorldDirectorCreateWorldWidget::CreateWorld()
{
	if (Bootstrap == nullptr || PromptInput == nullptr || SeedInput == nullptr ||
		ModelInput == nullptr || ReasoningInput == nullptr)
	{
		return;
	}
	bTerminalResultShown = false;
	int32 Seed = FCString::Atoi(*SeedInput->GetText().ToString());
	if (Seed <= 0)
	{
		Seed = FMath::RandRange(1000, 999999999);
		SeedInput->SetText(FText::AsNumber(Seed));
	}
	if (!Bootstrap->BeginPlayerWorldGeneration(
		PromptInput->GetText().ToString(), Seed, bUseFixtureDebug,
		ModelInput->GetSelectedOption(), ReasoningInput->GetSelectedOption()))
	{
		bTerminalResultShown = true;
		StatusText->SetText(FText::FromString(TEXT("COULD NOT START GENERATION")));
		DetailText->SetText(FText::FromString(
			TEXT("Another run may already be active, or the selected local provider is unavailable. Open AI Diagnostics for the exact error and run artifacts.")));
		UpdateStatusVisuals(AccentRed, 0.0f, TEXT("Generation did not start"));
	}
}

void UWorldDirectorCreateWorldWidget::CancelGeneration()
{
	if (Bootstrap != nullptr)
	{
		Bootstrap->CancelPlayerWorldGeneration();
	}
}

void UWorldDirectorCreateWorldWidget::OpenDiagnostics()
{
	if (Bootstrap != nullptr)
	{
		Bootstrap->OpenGenerationDiagnostics();
	}
}

void UWorldDirectorCreateWorldWidget::CloseMenu()
{
	if (Bootstrap != nullptr)
	{
		Bootstrap->CloseWorldCreationMenu();
	}
}

void UWorldDirectorCreateWorldWidget::ToggleFixtureDebug(const bool bChecked)
{
	bUseFixtureDebug = bChecked;
	if (ProviderModeText != nullptr)
	{
		ProviderModeText->SetText(FText::FromString(bUseFixtureDebug
			? TEXT("OFFLINE FIXTURE MODE - Deterministic local test data; no AI model will be contacted.")
			: TEXT("LIVE MODEL MODE - Uses the locally authenticated Codex CLI. A run may take several minutes.")));
		ProviderModeText->SetColorAndOpacity(FSlateColor(bUseFixtureDebug ? AccentBlue : TextSecondary));
	}
}

void UWorldDirectorCreateWorldWidget::HandlePromptChanged(const FText& NewText)
{
	(void)NewText;
	UpdatePromptCharacterCount();
}

void UWorldDirectorCreateWorldWidget::HandleModelChanged(
	const FString SelectedItem,
	const ESelectInfo::Type SelectionType)
{
	(void)SelectionType;
	RefreshReasoningOptions(SelectedItem);
}

void UWorldDirectorCreateWorldWidget::RefreshReasoningOptions(const FString& SelectedModel)
{
	if (ReasoningInput == nullptr)
	{
		return;
	}
	const FString PreviousSelection = ReasoningInput->GetSelectedOption();
	ReasoningInput->ClearOptions();
	for (const TCHAR* Effort : { TEXT("low"), TEXT("medium"), TEXT("high"), TEXT("xhigh") })
	{
		ReasoningInput->AddOption(Effort);
	}
	if (SelectedModel.StartsWith(TEXT("gpt-5.6")))
	{
		ReasoningInput->AddOption(TEXT("max"));
		if (!SelectedModel.Contains(TEXT("luna"), ESearchCase::IgnoreCase))
		{
			ReasoningInput->AddOption(TEXT("ultra"));
		}
	}
	ReasoningInput->SetSelectedOption(
		ReasoningInput->FindOptionIndex(PreviousSelection) != INDEX_NONE
			? PreviousSelection
			: TEXT("medium"));
}

void UWorldDirectorCreateWorldWidget::UpdatePromptCharacterCount()
{
	if (PromptInput == nullptr || PromptCharacterText == nullptr)
	{
		return;
	}
	const int32 CharacterCount = PromptInput->GetText().ToString().Len();
	PromptCharacterText->SetText(FText::FromString(FString::Printf(
		TEXT("%d character%s - Optional"), CharacterCount, CharacterCount == 1 ? TEXT("") : TEXT("s"))));
}

void UWorldDirectorCreateWorldWidget::UpdateStatusVisuals(
	const FLinearColor& Accent,
	const float Progress,
	const FString& ProgressLabel)
{
	if (StatusPanel != nullptr)
	{
		StatusPanel->SetBrushColor(FLinearColor(
			0.035f + Accent.R * 0.08f,
			0.035f + Accent.G * 0.08f,
			0.035f + Accent.B * 0.08f,
			1.0f));
	}
	if (StatusText != nullptr)
	{
		StatusText->SetColorAndOpacity(FSlateColor(Accent));
	}
	if (ProgressStageText != nullptr)
	{
		ProgressStageText->SetText(FText::FromString(ProgressLabel));
	}
	if (StageProgress != nullptr)
	{
		StageProgress->SetPercent(FMath::Clamp(Progress, 0.0f, 1.0f));
		StageProgress->SetFillColorAndOpacity(Accent);
	}
}

void UWorldDirectorCreateWorldWidget::RefreshStatus()
{
	UDirectorBridgeSubsystem* Bridge = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UDirectorBridgeSubsystem>() : nullptr;
	if (Bridge == nullptr || StatusText == nullptr || DetailText == nullptr)
	{
		return;
	}
	const bool bRunning = Bridge->IsGenerationRunning();
	if (CreateButton == nullptr || CancelButton == nullptr || CloseButton == nullptr ||
		PromptInput == nullptr || SeedInput == nullptr || FixtureCheck == nullptr ||
		ModelInput == nullptr || ReasoningInput == nullptr)
	{
		return;
	}
	CreateButton->SetIsEnabled(!bRunning);
	CancelButton->SetIsEnabled(bRunning);
	CloseButton->SetIsEnabled(!bRunning);
	PromptInput->SetIsEnabled(!bRunning);
	SeedInput->SetIsEnabled(!bRunning);
	FixtureCheck->SetIsEnabled(!bRunning);
	ModelInput->SetIsEnabled(!bRunning);
	ReasoningInput->SetIsEnabled(!bRunning);
	if (bTerminalResultShown)
	{
		return;
	}

	const EWorldDirectorGenerationStage Stage = Bridge->GetGenerationStage();
	const FStagePresentation Presentation = DescribeStage(Stage, Bridge->GetGenerationRepairAttempts());
	if (bRunning)
	{
		StatusText->SetText(FText::FromString(TEXT("GENERATING WORLD")));
		DetailText->SetText(FText::FromString(FString::Printf(
			TEXT("Run %s  |  %.1f seconds elapsed\nCompleted: %s\nRepairs: %d  |  Validation issues: %d"),
			Bridge->GetGenerationRunId().IsEmpty() ? TEXT("starting") : *Bridge->GetGenerationRunId(),
			Bridge->GetGenerationElapsedSeconds(),
			Bridge->GetGenerationStageHistory().IsEmpty()
				? TEXT("No stage completed yet")
				: *FString::Join(Bridge->GetGenerationStageHistory(), TEXT(" -> ")),
			Bridge->GetGenerationRepairAttempts(),
			Bridge->GetGenerationIssueHistory().Num())));
		UpdateStatusVisuals(AccentBlue, Presentation.Progress, Presentation.Label);
		return;
	}

	if (Stage == EWorldDirectorGenerationStage::Completed)
	{
		StatusText->SetText(FText::FromString(TEXT("WORLD READY")));
		DetailText->SetText(FText::FromString(FString::Printf(
			TEXT("Previous run %s completed in %.1f seconds. Its prompts, responses, metrics, and world artifacts remain available in AI Diagnostics."),
			*Bridge->GetGenerationRunId(), Bridge->GetGenerationElapsedSeconds())));
		UpdateStatusVisuals(AccentGreen, 1.0f, Presentation.Label);
	}
	else if (Stage == EWorldDirectorGenerationStage::Failed)
	{
		StatusText->SetText(FText::FromString(TEXT("GENERATION FAILED")));
		DetailText->SetText(FText::FromString(Bridge->GetLastGenerationError().IsEmpty()
			? TEXT("The run stopped before a playable world was produced. Open AI Diagnostics for details.")
			: Bridge->GetLastGenerationError()));
		UpdateStatusVisuals(AccentRed, Presentation.Progress, Presentation.Label);
	}
	else if (Stage == EWorldDirectorGenerationStage::Cancelled)
	{
		StatusText->SetText(FText::FromString(TEXT("GENERATION CANCELLED")));
		DetailText->SetText(FText::FromString(
			TEXT("No new world was applied. The partial run and any available artifacts remain inspectable.")));
		UpdateStatusVisuals(AccentRed, Presentation.Progress, Presentation.Label);
	}
	else
	{
		StatusText->SetText(FText::FromString(TEXT("READY TO GENERATE")));
		DetailText->SetText(FText::FromString(
			TEXT("Your brief and settings are ready. Open AI Diagnostics at any time to inspect the current or previous run.")));
		UpdateStatusVisuals(AccentAmber, 0.0f, TEXT("Ready to configure"));
	}
}

void UWorldDirectorCreateWorldWidget::SetGenerationResult(const bool bSuccess, const FString& Error)
{
	if (bSuccess)
	{
		bTerminalResultShown = false;
		RemoveFromParent();
		return;
	}
	bTerminalResultShown = true;
	const bool bCancelled = Error.Contains(TEXT("cancelled"), ESearchCase::IgnoreCase);
	if (StatusText != nullptr)
	{
		StatusText->SetText(FText::FromString(bCancelled
			? TEXT("GENERATION CANCELLED") : TEXT("GENERATION FAILED")));
	}
	if (DetailText != nullptr)
	{
		DetailText->SetText(FText::FromString(Error +
			TEXT("\nOpen AI Diagnostics to inspect stage timing, validation, prompts, responses, and run files.")));
	}
	UpdateStatusVisuals(AccentRed, 0.0f,
		bCancelled ? TEXT("Generation cancelled") : TEXT("Generation stopped with an error"));
}

void UWorldDirectorCreateWorldWidget::PrepareForNewWorld()
{
	bTerminalResultShown = false;
	if (PromptInput != nullptr)
	{
		PromptInput->SetText(FText::GetEmpty());
	}
	if (SeedInput != nullptr)
	{
		SeedInput->SetText(FText::AsNumber(FMath::RandRange(1000, 999999)));
	}
	UpdatePromptCharacterCount();
	if (StatusText != nullptr)
	{
		StatusText->SetText(FText::FromString(TEXT("READY TO GENERATE")));
	}
	if (DetailText != nullptr)
	{
		DetailText->SetText(FText::FromString(
			TEXT("Your brief and settings are ready. Open AI Diagnostics at any time to inspect the current or previous run.")));
	}
	UpdateStatusVisuals(AccentAmber, 0.0f, TEXT("Ready to configure"));
	ToggleFixtureDebug(bUseFixtureDebug);
}

bool UWorldDirectorCreateWorldWidget::IsCreationMenuReady() const
{
	return PromptInput != nullptr && SeedInput != nullptr && ModelInput != nullptr &&
		ReasoningInput != nullptr && FixtureCheck != nullptr && StatusText != nullptr &&
		StageProgress != nullptr && CreateButton != nullptr && CancelButton != nullptr &&
		DiagnosticsButton != nullptr && CloseButton != nullptr && Bootstrap != nullptr;
}
