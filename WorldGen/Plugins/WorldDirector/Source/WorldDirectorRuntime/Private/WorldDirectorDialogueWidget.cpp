#include "WorldDirectorDialogueWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"
#include "WorldDirectorSubsystems.h"
#include "WorldDirectorTownActors.h"

namespace
{
UButton* AddMenuButton(
	UWidgetTree* WidgetTree,
	UVerticalBox* Menu,
	const FString& Label)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>();
	UTextBlock* ButtonText = WidgetTree->ConstructWidget<UTextBlock>();
	ButtonText->SetText(FText::FromString(Label));
	ButtonText->SetColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.86f, 0.68f)));
	Button->SetContent(ButtonText);
	if (UVerticalBoxSlot* Slot = Menu->AddChildToVerticalBox(Button))
	{
		Slot->SetPadding(FMargin(0.0f, 5.0f));
	}
	return Button;
}
}

void UWorldDirectorDialogueWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (WidgetTree == nullptr)
	{
		return;
	}
	if (WidgetTree->RootWidget != nullptr)
	{
		RebuildResidentText();
		return;
	}

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
	Panel->SetBrushColor(FLinearColor(0.035f, 0.028f, 0.025f, 0.94f));
	Panel->SetPadding(FMargin(22.0f));
	WidgetTree->RootWidget = Panel;

	UVerticalBox* Menu = WidgetTree->ConstructWidget<UVerticalBox>();
	Panel->SetContent(Menu);
	TitleText = WidgetTree->ConstructWidget<UTextBlock>();
	TitleText->SetColorAndOpacity(FSlateColor(FLinearColor(0.96f, 0.78f, 0.38f)));
	TitleText->SetFont(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 24));
	Menu->AddChildToVerticalBox(TitleText);

	BodyText = WidgetTree->ConstructWidget<UTextBlock>();
	BodyText->SetAutoWrapText(true);
	BodyText->SetColorAndOpacity(FSlateColor(FLinearColor(0.88f, 0.84f, 0.76f)));
	if (UVerticalBoxSlot* BodySlot = Menu->AddChildToVerticalBox(BodyText))
	{
		BodySlot->SetPadding(FMargin(0.0f, 12.0f, 0.0f, 14.0f));
	}

	UButton* AskButton = AddMenuButton(WidgetTree, Menu, TEXT("Ask about what they know"));
	AskButton->OnClicked.AddDynamic(this, &UWorldDirectorDialogueWidget::AskAboutTown);
	UButton* HelpButton = AddMenuButton(WidgetTree, Menu, TEXT("Offer help"));
	HelpButton->OnClicked.AddDynamic(this, &UWorldDirectorDialogueWidget::OfferHelp);
	UButton* LeaveButton = AddMenuButton(WidgetTree, Menu, TEXT("Leave"));
	LeaveButton->OnClicked.AddDynamic(this, &UWorldDirectorDialogueWidget::LeaveConversation);
	RebuildResidentText();
}

void UWorldDirectorDialogueWidget::InitializeForResident(AWorldDirectorResidentActor* InResident)
{
	Resident = InResident;
	ResidentId = Resident != nullptr ? Resident->ResidentId : FString();
	RebuildResidentText();
}

void UWorldDirectorDialogueWidget::ApplyViewportLayout()
{
	const FVector2D ViewportSize = UWidgetLayoutLibrary::GetViewportSize(this);
	const FVector2D DesiredSize(
		FMath::Min(520.0, FMath::Max(360.0, ViewportSize.X - 48.0)),
		FMath::Min(500.0, FMath::Max(360.0, ViewportSize.Y - 96.0)));
	SetAnchorsInViewport(FAnchors(0.0f, 0.0f));
	SetAlignmentInViewport(FVector2D::ZeroVector);
	SetDesiredSizeInViewport(DesiredSize);
	SetPositionInViewport(FVector2D(
		24.0f, FMath::Max(24.0, ViewportSize.Y - DesiredSize.Y - 24.0)), false);
}

void UWorldDirectorDialogueWidget::RebuildResidentText()
{
	if (TitleText == nullptr || BodyText == nullptr || ResidentId.IsEmpty())
	{
		return;
	}
	const UWorldStateSubsystem* State = GetWorld()
		? GetWorld()->GetSubsystem<UWorldStateSubsystem>() : nullptr;
	if (State == nullptr || !State->HasActiveWorldSpec())
	{
		TitleText->SetText(FText::FromString(ResidentId));
		BodyText->SetText(FText::FromString(TEXT("Their story is unavailable.")));
		return;
	}
	const FGeneratedWorldSpec& Spec = State->GetActiveWorldSpec();
	const FResident* ResidentSpec = Spec.Residents.FindByPredicate(
		[this](const FResident& Candidate) { return Candidate.Id == ResidentId; });
	if (ResidentSpec == nullptr)
	{
		return;
	}
	TitleText->SetText(FText::FromString(ResidentSpec->DisplayName));
	KnownFactSummary.Reset();
	for (const FString& BeliefId : ResidentSpec->BeliefIds)
	{
		const FBelief* Belief = Spec.Beliefs.FindByPredicate(
			[&BeliefId](const FBelief& Candidate) { return Candidate.Id == BeliefId; });
		const FWorldFact* Fact = Belief ? Spec.Facts.FindByPredicate(
			[Belief](const FWorldFact& Candidate) { return Candidate.Id == Belief->FactId; }) : nullptr;
		if (Fact != nullptr)
		{
			KnownFactSummary += FString::Printf(TEXT("\n• %s"), *Fact->Statement);
		}
	}
	BodyText->SetText(FText::FromString(FString::Printf(
		TEXT("%s\n\nMotivation: %s\nConcern: %s"),
		*ResidentSpec->OccupationTag.ToString(), *ResidentSpec->Motivation, *ResidentSpec->Fear)));
}

void UWorldDirectorDialogueWidget::AskAboutTown()
{
	BodyText->SetText(FText::FromString(KnownFactSummary.IsEmpty()
		? TEXT("They have nothing they are willing to share.")
		: FString(TEXT("What they know:")) + KnownFactSummary));
}

void UWorldDirectorDialogueWidget::OfferHelp()
{
	BodyText->SetText(FText::FromString(
		TEXT("They acknowledge the offer. This social action is intentionally lightweight in the vertical slice.")));
}

void UWorldDirectorDialogueWidget::LeaveConversation()
{
	if (Resident != nullptr)
	{
		Resident->CloseDialogueMenu();
	}
	else
	{
		RemoveFromParent();
	}
}
