#include "WorldDirectorCreateWorldWidget.h"
#include "WorldDirectorRuntime.h"

#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"
#include "WorldDirectorSubsystems.h"
#include "WorldDirectorTownActors.h"

namespace
{
UTextBlock* AddText(UWidgetTree* Tree, UVerticalBox* Layout, const FString& Text, int32 Size,
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

UButton* AddButton(UWidgetTree* Tree, UHorizontalBox* Row, const FString& Label)
{
	UButton* Button = Tree->ConstructWidget<UButton>();
	UTextBlock* Text = Tree->ConstructWidget<UTextBlock>();
	Text->SetText(FText::FromString(Label));
	Text->SetColorAndOpacity(FSlateColor(FLinearColor(0.96f, 0.89f, 0.70f)));
	Text->SetJustification(ETextJustify::Center);
	Button->SetContent(Text);
	if (UHorizontalBoxSlot* Slot = Row->AddChildToHorizontalBox(Button))
	{
		Slot->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
		Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	return Button;
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
	RefreshStatus();
}

bool UWorldDirectorCreateWorldWidget::ApplyViewportLayout()
{
	const FVector2D ViewportSize = UWidgetLayoutLibrary::GetViewportSize(this);
	if (ViewportSize.X <= 0.0 || ViewportSize.Y <= 0.0)
	{
		return false;
	}
	const FVector2D DesiredSize(
		FMath::Min(760.0, FMath::Max(360.0, ViewportSize.X - 48.0)),
		FMath::Min(760.0, FMath::Max(480.0, ViewportSize.Y - 48.0)));
	const FVector2D Position(
		FMath::Max(24.0, (ViewportSize.X - DesiredSize.X) * 0.5),
		FMath::Max(24.0, (ViewportSize.Y - DesiredSize.Y) * 0.5));
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
	Panel->SetBrushColor(FLinearColor(0.025f, 0.022f, 0.020f, 0.98f));
	Panel->SetPadding(FMargin(34.0f));
	WidgetTree->RootWidget = Panel;
	UVerticalBox* Layout = WidgetTree->ConstructWidget<UVerticalBox>();
	Panel->SetContent(Layout);

	AddText(WidgetTree, Layout, TEXT("CREATE A LIVING WORLD"), 31,
		FLinearColor(0.96f, 0.76f, 0.34f));
	AddText(WidgetTree, Layout,
		TEXT("Describe the town you want, or leave this blank and the director will invent one. "
			"The model creates meaning; Unreal validates it and builds only from certified assets."),
		16, FLinearColor(0.82f, 0.80f, 0.74f), FMargin(0.0f, 12.0f, 0.0f, 18.0f));

	PromptInput = WidgetTree->ConstructWidget<UMultiLineEditableTextBox>();
	PromptInput->SetHintText(FText::FromString(
		TEXT("Example: A river frontier town held together by a mill, old debts, and a guarded civic secret.")));
	PromptInput->SetForegroundColor(FLinearColor(0.94f, 0.92f, 0.87f));
	if (UVerticalBoxSlot* PromptSlot = Layout->AddChildToVerticalBox(PromptInput))
	{
		PromptSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 14.0f));
		PromptSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	UHorizontalBox* SeedRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (UVerticalBoxSlot* RowSlot = Layout->AddChildToVerticalBox(SeedRow))
	{
		RowSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 14.0f));
	}
	UTextBlock* SeedLabel = WidgetTree->ConstructWidget<UTextBlock>();
	SeedLabel->SetText(FText::FromString(TEXT("Seed")));
	SeedLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.86f, 0.82f, 0.72f)));
	SeedRow->AddChildToHorizontalBox(SeedLabel);
	SeedInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	SeedInput->SetText(FText::AsNumber(FMath::RandRange(1000, 999999)));
	if (UHorizontalBoxSlot* SeedSlot = SeedRow->AddChildToHorizontalBox(SeedInput))
	{
		SeedSlot->SetPadding(FMargin(14.0f, 0.0f, 24.0f, 0.0f));
		SeedSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	UCheckBox* FixtureCheck = WidgetTree->ConstructWidget<UCheckBox>();
	FixtureCheck->OnCheckStateChanged.AddDynamic(this, &UWorldDirectorCreateWorldWidget::ToggleFixtureDebug);
	SeedRow->AddChildToHorizontalBox(FixtureCheck);
	UTextBlock* FixtureLabel = WidgetTree->ConstructWidget<UTextBlock>();
	FixtureLabel->SetText(FText::FromString(TEXT(" Debug fixture (no model)")));
	FixtureLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.68f, 0.66f, 0.62f)));
	SeedRow->AddChildToHorizontalBox(FixtureLabel);

	UHorizontalBox* AgentRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (UVerticalBoxSlot* RowSlot = Layout->AddChildToVerticalBox(AgentRow))
	{
		RowSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 14.0f));
	}
	UTextBlock* ModelLabel = WidgetTree->ConstructWidget<UTextBlock>();
	ModelLabel->SetText(FText::FromString(TEXT("Model")));
	ModelLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.86f, 0.82f, 0.72f)));
	AgentRow->AddChildToHorizontalBox(ModelLabel);
	ModelInput = WidgetTree->ConstructWidget<UComboBoxString>();
	for (const TCHAR* Model : {
		TEXT("gpt-5.6-terra"), TEXT("gpt-5.6-sol"), TEXT("gpt-5.6-luna"), TEXT("gpt-5.5")})
	{
		ModelInput->AddOption(Model);
	}
	ModelInput->SetSelectedOption(TEXT("gpt-5.6-terra"));
	if (UHorizontalBoxSlot* ModelSlot = AgentRow->AddChildToHorizontalBox(ModelInput))
	{
		ModelSlot->SetPadding(FMargin(12.0f, 0.0f, 24.0f, 0.0f));
		ModelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	UTextBlock* ReasoningLabel = WidgetTree->ConstructWidget<UTextBlock>();
	ReasoningLabel->SetText(FText::FromString(TEXT("Reasoning")));
	ReasoningLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.86f, 0.82f, 0.72f)));
	AgentRow->AddChildToHorizontalBox(ReasoningLabel);
	ReasoningInput = WidgetTree->ConstructWidget<UComboBoxString>();
	for (const TCHAR* Effort : { TEXT("low"), TEXT("medium"), TEXT("high"), TEXT("xhigh") })
	{
		ReasoningInput->AddOption(Effort);
	}
	ReasoningInput->SetSelectedOption(TEXT("medium"));
	if (UHorizontalBoxSlot* ReasoningSlot = AgentRow->AddChildToHorizontalBox(ReasoningInput))
	{
		ReasoningSlot->SetPadding(FMargin(12.0f, 0.0f, 0.0f, 0.0f));
		ReasoningSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	StatusText = AddText(WidgetTree, Layout, TEXT("Ready"), 18,
		FLinearColor(0.94f, 0.78f, 0.42f), FMargin(0.0f, 4.0f));
	DetailText = AddText(WidgetTree, Layout,
		TEXT("Real AI generation uses the locally authenticated Codex CLI and can take several minutes."),
		14, FLinearColor(0.68f, 0.68f, 0.65f), FMargin(0.0f, 4.0f, 0.0f, 16.0f));

	UHorizontalBox* Actions = WidgetTree->ConstructWidget<UHorizontalBox>();
	Layout->AddChildToVerticalBox(Actions);
	CreateButton = AddButton(WidgetTree, Actions, TEXT("Create World"));
	CreateButton->OnClicked.AddDynamic(this, &UWorldDirectorCreateWorldWidget::CreateWorld);
	CancelButton = AddButton(WidgetTree, Actions, TEXT("Cancel"));
	CancelButton->OnClicked.AddDynamic(this, &UWorldDirectorCreateWorldWidget::CancelGeneration);
	CancelButton->SetIsEnabled(false);
	UButton* Diagnostics = AddButton(WidgetTree, Actions, TEXT("AI Diagnostics"));
	Diagnostics->OnClicked.AddDynamic(this, &UWorldDirectorCreateWorldWidget::OpenDiagnostics);
	CloseButton = AddButton(WidgetTree, Actions, TEXT("Close"));
	CloseButton->OnClicked.AddDynamic(this, &UWorldDirectorCreateWorldWidget::CloseMenu);
}

void UWorldDirectorCreateWorldWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshAccumulator += InDeltaTime;
	if (RefreshAccumulator >= 0.2)
	{
		RefreshAccumulator = 0.0;
		RefreshStatus();
	}
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
		StatusText->SetText(FText::FromString(TEXT("Could not start generation")));
		DetailText->SetText(FText::FromString(
			TEXT("A generation may already be running, or the local provider is unavailable. Open AI Diagnostics for details.")));
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
		ModelInput == nullptr || ReasoningInput == nullptr)
	{
		return;
	}
	CreateButton->SetIsEnabled(!bRunning);
	CancelButton->SetIsEnabled(bRunning);
	CloseButton->SetIsEnabled(!bRunning);
	ModelInput->SetIsEnabled(!bRunning);
	ReasoningInput->SetIsEnabled(!bRunning);
	if (bTerminalResultShown)
	{
		return;
	}
	if (bRunning)
	{
		const FString Stage = StaticEnum<EWorldDirectorGenerationStage>()->GetNameStringByValue(
			static_cast<int64>(Bridge->GetGenerationStage()));
		StatusText->SetText(FText::FromString(FString::Printf(
			TEXT("Generating: %s (%.1f seconds)"), *Stage, Bridge->GetGenerationElapsedSeconds())));
		DetailText->SetText(FText::FromString(FString::Printf(
			TEXT("Run %s\nCompleted stages: %s\nRepairs: %d  Validation issues: %d"),
			*Bridge->GetGenerationRunId(),
			*FString::Join(Bridge->GetGenerationStageHistory(), TEXT(" -> ")),
			Bridge->GetGenerationRepairAttempts(), Bridge->GetGenerationIssueHistory().Num())));
	}
	else if (Bridge->GetGenerationStage() == EWorldDirectorGenerationStage::Completed)
	{
		StatusText->SetText(FText::FromString(TEXT("Ready to create another world")));
		DetailText->SetText(FText::FromString(FString::Printf(
			TEXT("Previous run %s completed in %.1f seconds. Its diagnostics and artifacts remain available."),
			*Bridge->GetGenerationRunId(), Bridge->GetGenerationElapsedSeconds())));
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
	if (StatusText != nullptr)
	{
		StatusText->SetText(FText::FromString(Error.Contains(TEXT("cancelled"), ESearchCase::IgnoreCase)
			? TEXT("Generation cancelled") : TEXT("Generation failed")));
	}
	if (DetailText != nullptr)
	{
		DetailText->SetText(FText::FromString(Error + TEXT("\nOpen AI Diagnostics to inspect stage timings, validation, and run files.")));
	}
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
	if (StatusText != nullptr)
	{
		StatusText->SetText(FText::FromString(TEXT("Ready")));
	}
	if (DetailText != nullptr)
	{
		DetailText->SetText(FText::FromString(
			TEXT("Real AI generation uses the locally authenticated Codex CLI and can take several minutes.")));
	}
}

bool UWorldDirectorCreateWorldWidget::IsCreationMenuReady() const
{
	return PromptInput != nullptr && SeedInput != nullptr && ModelInput != nullptr &&
		ReasoningInput != nullptr && StatusText != nullptr &&
		CreateButton != nullptr && CancelButton != nullptr && CloseButton != nullptr && Bootstrap != nullptr;
}
