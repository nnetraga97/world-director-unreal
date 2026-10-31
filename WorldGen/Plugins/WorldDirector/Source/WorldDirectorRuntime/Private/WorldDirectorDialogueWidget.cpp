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

namespace
{
FString ReadableTag(const FName Tag)
{
	FString Text = Tag.ToString();
	int32 Dot = INDEX_NONE;
	if (Text.FindLastChar(TEXT('.'), Dot))
	{
		Text.RightChopInline(Dot + 1);
	}
	return Text;
}

FString LocationName(const FGeneratedWorldSpec& Spec, const FString& LocationId)
{
	const FWorldLocation* Location = Spec.Locations.FindByPredicate(
		[&LocationId](const FWorldLocation& Candidate) { return Candidate.Id == LocationId; });
	return Location != nullptr && !Location->DisplayName.IsEmpty() ? Location->DisplayName : LocationId;
}

FString ResidentName(const FGeneratedWorldSpec& Spec, const FString& ResidentId)
{
	const FResident* Resident = Spec.Residents.FindByPredicate(
		[&ResidentId](const FResident& Candidate) { return Candidate.Id == ResidentId; });
	return Resident != nullptr ? Resident->DisplayName : ResidentId;
}

// A belief is shared once the player's rapport outweighs how closely it is guarded.
bool WillShare(const FBelief& Belief, const float Rapport)
{
	return Belief.Secrecy <= 0.4f + Rapport;
}
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
	FString Situation;
	if (const UTownSimulationSubsystem* Simulation = GetWorld()->GetSubsystem<UTownSimulationSubsystem>())
	{
		const FWorldDirectorResidentRuntimeState* Runtime = Simulation->GetResidentStates().FindByPredicate(
			[this](const FWorldDirectorResidentRuntimeState& Candidate) { return Candidate.ResidentId == ResidentId; });
		if (Runtime != nullptr)
		{
			Situation = FString::Printf(TEXT("\nRight now: %s, %s."),
				*StaticEnum<EWorldDirectorResidentAvailability>()->GetNameStringByValue(
					static_cast<int64>(Runtime->Availability)).ToLower(),
				*LocationName(Spec, Runtime->IntendedLocationId));
		}
		if (Simulation->IsThreatAlertActive() && Resident != nullptr &&
			Resident->Availability == EWorldDirectorResidentAvailability::Socializing)
		{
			Situation += TEXT("\nThe alarm has everyone on edge.");
		}
	}
	BodyText->SetText(FText::FromString(FString::Printf(
		TEXT("%s at %s, lives at %s.%s\n\nWants to: %s\nAfraid: %s"),
		*ReadableTag(ResidentSpec->OccupationTag), *LocationName(Spec, ResidentSpec->WorkplaceLocationId),
		*LocationName(Spec, ResidentSpec->HomeLocationId), *Situation,
		*ResidentSpec->Motivation, *ResidentSpec->Fear)));
}

void UWorldDirectorDialogueWidget::AskAboutTown()
{
	const UWorldStateSubsystem* State = GetWorld() ? GetWorld()->GetSubsystem<UWorldStateSubsystem>() : nullptr;
	const UTownSimulationSubsystem* Simulation = GetWorld() ? GetWorld()->GetSubsystem<UTownSimulationSubsystem>() : nullptr;
	if (State == nullptr || !State->HasActiveWorldSpec() || Simulation == nullptr)
	{
		BodyText->SetText(FText::FromString(TEXT("They have nothing they are willing to share.")));
		return;
	}
	const FGeneratedWorldSpec& Spec = State->GetActiveWorldSpec();
	const float Rapport = Simulation->GetPlayerRapport(ResidentId);
	FString Known;
	int32 Withheld = 0;
	// Runtime beliefs include what this resident has heard from neighbours since the
	// town started, not just what they were generated knowing.
	if (const TArray<FBelief>* Beliefs = Simulation->GetRuntimeBeliefsForResident(ResidentId))
	{
		for (const FBelief& Belief : *Beliefs)
		{
			const FWorldFact* Fact = Spec.Facts.FindByPredicate(
				[&Belief](const FWorldFact& Candidate) { return Candidate.Id == Belief.FactId; });
			if (Fact == nullptr)
			{
				continue;
			}
			if (!WillShare(Belief, Rapport))
			{
				++Withheld;
				continue;
			}
			Known += FString::Printf(TEXT("\n- %s"), *Fact->Statement);
			if (!Belief.SourceResidentId.IsEmpty())
			{
				Known += FString::Printf(TEXT(" (heard from %s)"), *ResidentName(Spec, Belief.SourceResidentId));
			}
		}
	}
	const TArray<FString>& Events = Simulation->GetRuntimeEventLog();
	if (!Events.IsEmpty())
	{
		Known += FString::Printf(TEXT("\n\nLately: %s"), *Events.Last());
	}
	if (Withheld > 0)
	{
		Known += FString::Printf(TEXT("\n\nThey are holding something back (%d)."), Withheld);
	}
	BodyText->SetText(FText::FromString(Known.IsEmpty()
		? TEXT("They have nothing they are willing to share.")
		: FString(TEXT("What they know:")) + Known));
}

void UWorldDirectorDialogueWidget::OfferHelp()
{
	UTownSimulationSubsystem* Simulation = GetWorld() ? GetWorld()->GetSubsystem<UTownSimulationSubsystem>() : nullptr;
	const UWorldStateSubsystem* State = GetWorld() ? GetWorld()->GetSubsystem<UWorldStateSubsystem>() : nullptr;
	if (Simulation == nullptr || State == nullptr || !State->HasActiveWorldSpec())
	{
		return;
	}
	const FGeneratedWorldSpec& Spec = State->GetActiveWorldSpec();
	const float Before = Simulation->GetPlayerRapport(ResidentId);
	const float After = Simulation->AddPlayerRapport(ResidentId, 0.2f);
	FString Reply = After > Before
		? TEXT("They accept your help, and some of the wariness leaves their face.")
		: TEXT("They already trust you as much as they trust anyone.");
	// The first secret that this help unlocks is told right away.
	if (const TArray<FBelief>* Beliefs = Simulation->GetRuntimeBeliefsForResident(ResidentId))
	{
		for (const FBelief& Belief : *Beliefs)
		{
			if (!WillShare(Belief, Before) && WillShare(Belief, After))
			{
				if (const FWorldFact* Fact = Spec.Facts.FindByPredicate(
					[&Belief](const FWorldFact& Candidate) { return Candidate.Id == Belief.FactId; }))
				{
					Reply += FString::Printf(TEXT("\n\nThey lower their voice: \"%s\""), *Fact->Statement);
					break;
				}
			}
		}
	}
	for (const FChangeProject& Project : Spec.ChangeProjects)
	{
		if (Project.RequiredParticipantResidentIds.Contains(ResidentId) &&
			Project.State != EWorldDirectorProjectState::Active)
		{
			Reply += FString::Printf(TEXT("\n\nThey are working to turn %s into something new (%s). Stay clear of it at night so the work can go ahead."),
				*LocationName(Spec, Project.TargetLocationId), *ReadableTag(Project.DesiredPurposeTag));
			break;
		}
	}
	BodyText->SetText(FText::FromString(Reply));
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
