#include "WorldDirectorLandingWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "WorldDirectorRuntime.h"
#include "WorldDirectorTownActors.h"

namespace WorldDirectorLandingWidgetPrivate
{
const FLinearColor TextPrimary(0.96f, 0.94f, 0.90f);
const FLinearColor TextSecondary(0.69f, 0.71f, 0.72f);
const FLinearColor TextMuted(0.46f, 0.49f, 0.51f);
const FLinearColor AccentAmber(0.94f, 0.70f, 0.28f);
const FLinearColor AccentBlue(0.31f, 0.70f, 0.94f);

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

UButton* MakeActionButton(UWidgetTree* Tree, UHorizontalBox* Row, const FString& Label, const FLinearColor& Color)
{
	UButton* Button = Tree->ConstructWidget<UButton>();
	Button->SetBackgroundColor(Color);
	UBorder* Padding = Tree->ConstructWidget<UBorder>();
	Padding->SetBrushColor(FLinearColor::Transparent);
	Padding->SetPadding(FMargin(20.0f, 13.0f));
	UTextBlock* Text = Tree->ConstructWidget<UTextBlock>();
	Text->SetText(FText::FromString(Label));
	Text->SetFont(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 14));
	Text->SetColorAndOpacity(FSlateColor(TextPrimary));
	Text->SetJustification(ETextJustify::Center);
	Padding->SetContent(Text);
	Button->SetContent(Padding);
	if (UHorizontalBoxSlot* Slot = Row->AddChildToHorizontalBox(Button))
	{
		Slot->SetPadding(FMargin(0.0f, 0.0f, 12.0f, 0.0f));
	}
	return Button;
}

void AddFeature(UWidgetTree* Tree, UHorizontalBox* Row, const FString& Title, const FString& Body)
{
	UBorder* Card = Tree->ConstructWidget<UBorder>();
	Card->SetBrushColor(FLinearColor(0.075f, 0.085f, 0.095f, 0.84f));
	Card->SetPadding(FMargin(16.0f, 14.0f));
	if (UHorizontalBoxSlot* Slot = Row->AddChildToHorizontalBox(Card))
	{
		Slot->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
		Slot->SetSize(ESlateSizeRule::Fill);
	}
	UVerticalBox* Content = Tree->ConstructWidget<UVerticalBox>();
	Card->SetContent(Content);
	AddText(Tree, Content, Title, 13, AccentAmber);
	AddText(Tree, Content, Body, 12, TextSecondary, FMargin(0.0f, 6.0f, 0.0f, 0.0f));
}
}

void UWorldDirectorLandingWidget::InitializeForBootstrap(AWorldDirectorFixtureBootstrap* InBootstrap)
{
	Bootstrap = InBootstrap;
}

void UWorldDirectorLandingWidget::RefreshSavedWorlds()
{
	if (Bootstrap == nullptr || SavedWorldPicker == nullptr || LoadWorldButton == nullptr)
	{
		return;
	}

	Bootstrap->RefreshSavedWorldCatalog();
	SavedWorldPaths.Reset();
	SavedWorldPicker->ClearOptions();
	for (const FWorldDirectorSavedWorldEntry& Entry : Bootstrap->GetSavedWorldCatalog())
	{
		SavedWorldPicker->AddOption(Entry.DisplayName);
		SavedWorldPaths.Add(Entry.RecipePath);
	}

	const bool bHasSavedWorlds = !SavedWorldPaths.IsEmpty();
	LoadWorldButton->SetIsEnabled(bHasSavedWorlds);
	if (bHasSavedWorlds)
	{
		SavedWorldPicker->SetSelectedIndex(0);
		if (SavedWorldStatusText != nullptr)
		{
			SavedWorldStatusText->SetText(FText::FromString(FString::Printf(
				TEXT("%d completed world%s available. Select one to rebuild it locally."),
				SavedWorldPaths.Num(), SavedWorldPaths.Num() == 1 ? TEXT("") : TEXT("s"))));
		}
	}
	else if (SavedWorldStatusText != nullptr)
	{
		SavedWorldStatusText->SetText(FText::FromString(
			TEXT("No completed worlds are available yet. Worlds appear here after generation finishes and saves both replay artifacts.")));
	}
}

TSharedRef<SWidget> UWorldDirectorLandingWidget::RebuildWidget()
{
	BuildWidgetTree();
	return Super::RebuildWidget();
}

void UWorldDirectorLandingWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
}

bool UWorldDirectorLandingWidget::ApplyViewportLayout()
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

void UWorldDirectorLandingWidget::BuildWidgetTree()
{
	if (WidgetTree == nullptr || WidgetTree->RootWidget != nullptr)
	{
		return;
	}

	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>();
	WidgetTree->RootWidget = Root;

	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>();
	Backdrop->SetBrushColor(FLinearColor(0.010f, 0.015f, 0.022f, 0.96f));
	Root->AddChildToOverlay(Backdrop);

	USizeBox* FrameSize = WidgetTree->ConstructWidget<USizeBox>();
	FrameSize->SetMaxDesiredWidth(980.0f);
	FrameSize->SetMinDesiredWidth(560.0f);
	if (UOverlaySlot* Slot = Root->AddChildToOverlay(FrameSize))
	{
		Slot->SetHorizontalAlignment(HAlign_Center);
		Slot->SetVerticalAlignment(VAlign_Center);
	}

	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>();
	Frame->SetBrushColor(FLinearColor(0.035f, 0.045f, 0.058f, 0.98f));
	Frame->SetPadding(FMargin(42.0f, 36.0f, 42.0f, 32.0f));
	FrameSize->SetContent(Frame);

	UVerticalBox* Layout = WidgetTree->ConstructWidget<UVerticalBox>();
	Frame->SetContent(Layout);
	WorldDirectorLandingWidgetPrivate::AddText(WidgetTree, Layout, TEXT("WORLD DIRECTOR  /  RUNTIME WORLD LAB"), 12, WorldDirectorLandingWidgetPrivate::AccentAmber);
	WorldDirectorLandingWidgetPrivate::AddText(WidgetTree, Layout, TEXT("Make a world worth returning to."), 38, WorldDirectorLandingWidgetPrivate::TextPrimary,
		FMargin(0.0f, 10.0f, 0.0f, 0.0f));
	WorldDirectorLandingWidgetPrivate::AddText(WidgetTree, Layout,
		TEXT("Describe a place with a history. The director turns it into a living town where people, routines, and change continue after the world is built."),
		16, WorldDirectorLandingWidgetPrivate::TextSecondary, FMargin(0.0f, 10.0f, 0.0f, 28.0f));

	UHorizontalBox* Features = WidgetTree->ConstructWidget<UHorizontalBox>();
	Layout->AddChildToVerticalBox(Features);
	WorldDirectorLandingWidgetPrivate::AddFeature(WidgetTree, Features, TEXT("DIRECT THE PREMISE"), TEXT("Start with a brief, a seed, or an empty page."));
	WorldDirectorLandingWidgetPrivate::AddFeature(WidgetTree, Features, TEXT("WATCH IT TAKE SHAPE"), TEXT("Follow interpretation, layout, population, and validation."));
	WorldDirectorLandingWidgetPrivate::AddFeature(WidgetTree, Features, TEXT("STEP INTO THE RESULT"), TEXT("Explore a town whose simulation runs without the model."));

	WorldDirectorLandingWidgetPrivate::AddText(WidgetTree, Layout, TEXT("CHOOSE YOUR STARTING POINT"), 12, WorldDirectorLandingWidgetPrivate::TextMuted,
		FMargin(0.0f, 30.0f, 0.0f, 10.0f));
	UHorizontalBox* Actions = WidgetTree->ConstructWidget<UHorizontalBox>();
	Layout->AddChildToVerticalBox(Actions);
	NewWorldButton = WorldDirectorLandingWidgetPrivate::MakeActionButton(WidgetTree, Actions, TEXT("CREATE A NEW WORLD"), FLinearColor(0.42f, 0.27f, 0.09f, 1.0f));
	NewWorldButton->SetToolTipText(FText::FromString(TEXT("Open the world brief and generation settings.")));
	NewWorldButton->OnClicked.AddDynamic(this, &UWorldDirectorLandingWidget::StartNewWorld);
	SampleWorldButton = WorldDirectorLandingWidgetPrivate::MakeActionButton(WidgetTree, Actions, TEXT("ENTER THE DEMO TOWN"), FLinearColor(0.10f, 0.25f, 0.34f, 1.0f));
	SampleWorldButton->SetToolTipText(FText::FromString(TEXT("Load the offline certified town fixture without an AI call.")));
	SampleWorldButton->OnClicked.AddDynamic(this, &UWorldDirectorLandingWidget::ExploreSampleWorld);

	WorldDirectorLandingWidgetPrivate::AddText(WidgetTree, Layout, TEXT("RETURN TO A CREATED WORLD"), 12, WorldDirectorLandingWidgetPrivate::TextMuted,
		FMargin(0.0f, 28.0f, 0.0f, 10.0f));
	SavedWorldPicker = WidgetTree->ConstructWidget<UComboBoxString>();
	SavedWorldPicker->SetToolTipText(FText::FromString(
		TEXT("Completed worlds are rebuilt from their validated local replay recipe.")));
	if (UVerticalBoxSlot* Slot = Layout->AddChildToVerticalBox(SavedWorldPicker))
	{
		Slot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
	}
	UHorizontalBox* SavedWorldActions = WidgetTree->ConstructWidget<UHorizontalBox>();
	Layout->AddChildToVerticalBox(SavedWorldActions);
	LoadWorldButton = WorldDirectorLandingWidgetPrivate::MakeActionButton(
		WidgetTree, SavedWorldActions, TEXT("LOAD SELECTED WORLD"), FLinearColor(0.18f, 0.31f, 0.22f, 1.0f));
	LoadWorldButton->SetToolTipText(FText::FromString(TEXT("Rebuild the selected completed world.")));
	LoadWorldButton->OnClicked.AddDynamic(this, &UWorldDirectorLandingWidget::LoadSelectedWorld);
	SavedWorldStatusText = WorldDirectorLandingWidgetPrivate::AddText(WidgetTree, Layout, TEXT("Looking for completed worlds..."), 11, WorldDirectorLandingWidgetPrivate::TextMuted,
		FMargin(0.0f, 2.0f, 0.0f, 0.0f));

	WorldDirectorLandingWidgetPrivate::AddText(WidgetTree, Layout,
		TEXT("N  world builder    F8  AI diagnostics    Tab  interaction cursor"),
		11, WorldDirectorLandingWidgetPrivate::TextMuted, FMargin(0.0f, 30.0f, 0.0f, 0.0f));
}

FReply UWorldDirectorLandingWidget::NativeOnPreviewKeyDown(
	const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::N || InKeyEvent.GetKey() == EKeys::Enter)
	{
		if (InKeyEvent.GetKey() == EKeys::Enter && SavedWorldPicker != nullptr &&
			SavedWorldPicker->HasAnyUserFocus() && LoadWorldButton != nullptr &&
			LoadWorldButton->GetIsEnabled())
		{
			LoadSelectedWorld();
			return FReply::Handled();
		}
		StartNewWorld();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

void UWorldDirectorLandingWidget::StartNewWorld()
{
	if (Bootstrap != nullptr)
	{
		Bootstrap->CloseLandingPage();
		Bootstrap->OpenWorldCreationMenu();
	}
}

void UWorldDirectorLandingWidget::ExploreSampleWorld()
{
	if (Bootstrap != nullptr)
	{
		Bootstrap->BeginSampleWorldPreview();
	}
}

void UWorldDirectorLandingWidget::LoadSelectedWorld()
{
	if (Bootstrap == nullptr || SavedWorldPicker == nullptr)
	{
		return;
	}

	const int32 SelectedIndex = SavedWorldPicker->GetSelectedIndex();
	if (SavedWorldPaths.IsValidIndex(SelectedIndex))
	{
		Bootstrap->OpenSavedWorld(SavedWorldPaths[SelectedIndex]);
	}
}
