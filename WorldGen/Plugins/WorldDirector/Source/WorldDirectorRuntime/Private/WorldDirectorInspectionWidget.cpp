#include "WorldDirectorInspectionWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"
#include "GameFramework/PlayerController.h"
#include "WorldDirectorSubsystems.h"
#include "WorldDirectorTownActors.h"

namespace
{
void AppendSection(FString& Out, const TCHAR* Heading)
{
	Out += FString::Printf(TEXT("\n%s\n"), Heading);
}

FString AccessName(const EWorldDirectorAccessPolicy Access)
{
	return StaticEnum<EWorldDirectorAccessPolicy>()->GetNameStringByValue(
		static_cast<int64>(Access));
}
}

void UWorldDirectorInspectionWidget::InitializeForTown(AWorldDirectorTownActor* InTown)
{
	Town = InTown;
}

TSharedRef<SWidget> UWorldDirectorInspectionWidget::RebuildWidget()
{
	BuildWidgetTree();
	return Super::RebuildWidget();
}

void UWorldDirectorInspectionWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	ApplyViewportLayout();
	RefreshInspectionText();
}

bool UWorldDirectorInspectionWidget::ApplyViewportLayout()
{
	const FVector2D ViewportSize = UWidgetLayoutLibrary::GetViewportSize(this);
	if (ViewportSize.X <= 0.0 || ViewportSize.Y <= 0.0)
	{
		return false;
	}
	const FVector2D DesiredSize(
		FMath::Min(820.0, FMath::Max(420.0, ViewportSize.X - 48.0)),
		FMath::Min(900.0, FMath::Max(480.0, ViewportSize.Y - 48.0)));
	SetAnchorsInViewport(FAnchors(0.0f, 0.0f));
	SetAlignmentInViewport(FVector2D::ZeroVector);
	SetPositionInViewport(FVector2D(24.0f, 24.0f), false);
	SetDesiredSizeInViewport(DesiredSize);
	return true;
}

void UWorldDirectorInspectionWidget::BuildWidgetTree()
{
	if (WidgetTree == nullptr || WidgetTree->RootWidget != nullptr)
	{
		return;
	}
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
	Panel->SetBrushColor(FLinearColor(0.025f, 0.022f, 0.02f, 0.97f));
	Panel->SetPadding(FMargin(24.0f));
	WidgetTree->RootWidget = Panel;
	UVerticalBox* Layout = WidgetTree->ConstructWidget<UVerticalBox>();
	Panel->SetContent(Layout);
	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>();
	Title->SetText(FText::FromString(TEXT("WORLD DIRECTOR INSPECTION")));
	Title->SetFont(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 25));
	Title->SetColorAndOpacity(FSlateColor(FLinearColor(0.96f, 0.78f, 0.38f)));
	Layout->AddChildToVerticalBox(Title);
	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
	if (UVerticalBoxSlot* ScrollSlot = Layout->AddChildToVerticalBox(Scroll))
	{
		ScrollSlot->SetPadding(FMargin(0.0f, 12.0f, 0.0f, 12.0f));
		ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	InspectionText = WidgetTree->ConstructWidget<UTextBlock>();
	InspectionText->SetAutoWrapText(true);
	InspectionText->SetColorAndOpacity(FSlateColor(FLinearColor(0.88f, 0.84f, 0.76f)));
	Scroll->AddChild(InspectionText);
	UButton* CloseButton = WidgetTree->ConstructWidget<UButton>();
	UTextBlock* CloseText = WidgetTree->ConstructWidget<UTextBlock>();
	CloseText->SetText(FText::FromString(TEXT("Close inspection")));
	CloseText->SetColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.86f, 0.68f)));
	CloseButton->SetContent(CloseText);
	CloseButton->OnClicked.AddDynamic(this, &UWorldDirectorInspectionWidget::CloseInspection);
	Layout->AddChildToVerticalBox(CloseButton);
}

void UWorldDirectorInspectionWidget::NativeTick(
	const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshAccumulator += InDeltaTime;
	if (RefreshAccumulator >= 1.0)
	{
		RefreshAccumulator = 0.0;
		RefreshInspectionText();
	}
}

void UWorldDirectorInspectionWidget::RefreshInspectionText()
{
	RenderedInspection.Reset();
	const UWorldStateSubsystem* WorldState = GetWorld()
		? GetWorld()->GetSubsystem<UWorldStateSubsystem>() : nullptr;
	const UTownSimulationSubsystem* Simulation = GetWorld()
		? GetWorld()->GetSubsystem<UTownSimulationSubsystem>() : nullptr;
	const UChangeProjectSubsystem* Projects = GetWorld()
		? GetWorld()->GetSubsystem<UChangeProjectSubsystem>() : nullptr;
	const UDirectorBridgeSubsystem* Bridge = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UDirectorBridgeSubsystem>() : nullptr;
	if (WorldState == nullptr || !WorldState->HasActiveWorldSpec())
	{
		RenderedInspection = TEXT("No active generated world.");
		InspectionText->SetText(FText::FromString(RenderedInspection));
		return;
	}
	const FGeneratedWorldSpec& Spec = WorldState->GetActiveWorldSpec();
	RenderedInspection += FString::Printf(TEXT("World: %s\nSeed: %d\n"),
		*Spec.Brief.SettlementIdentity, Spec.Seed);

	AppendSection(RenderedInspection, TEXT("ACCEPTED WORLD FACTS"));
	for (const FWorldFact& Fact : Spec.Facts)
	{
		RenderedInspection += FString::Printf(TEXT("- %s: %s%s\n"), *Fact.Id, *Fact.Statement,
			Fact.bSecret ? TEXT(" [secret]") : TEXT(""));
	}
	AppendSection(RenderedInspection, TEXT("NPC RELATIONSHIPS"));
	for (const FRelationship& Relationship : Spec.Relationships)
	{
		RenderedInspection += FString::Printf(TEXT("- %s -> %s (%s trust %.2f)\n"),
			*Relationship.SourceResidentId, *Relationship.TargetResidentId,
			*Relationship.RelationshipType.ToString(), Relationship.Trust);
	}
	AppendSection(RenderedInspection, TEXT("BELIEFS AND MEMORIES"));
	for (const FResident& Resident : Spec.Residents)
	{
		RenderedInspection += FString::Printf(TEXT("- %s: %d beliefs, %d memories"),
			*Resident.DisplayName, Resident.BeliefIds.Num(), Resident.ImportantMemories.Num());
		if (!Resident.BeliefIds.IsEmpty())
		{
			RenderedInspection += FString::Printf(TEXT("; belief IDs: %s"),
				*FString::Join(Resident.BeliefIds, TEXT(", ")));
		}
		if (Simulation != nullptr)
		{
			if (const TArray<FBelief>* RuntimeBeliefs =
				Simulation->GetRuntimeBeliefsForResident(Resident.Id))
			{
				TArray<FString> RuntimeBeliefSummaries;
				for (const FBelief& Belief : *RuntimeBeliefs)
				{
					RuntimeBeliefSummaries.Add(FString::Printf(
						TEXT("%s %.2f%s"), *Belief.FactId, Belief.Confidence,
						Belief.bImportantSecret ? TEXT(" secret") : TEXT("")));
				}
				RenderedInspection += FString::Printf(TEXT("; live beliefs: %s"),
					*FString::Join(RuntimeBeliefSummaries, TEXT(", ")));
			}
		}
		if (!Resident.ImportantMemories.IsEmpty())
		{
			RenderedInspection += TEXT("; memories: ");
			TArray<FString> MemorySummaries;
			for (const FResidentMemory& Memory : Resident.ImportantMemories)
			{
				MemorySummaries.Add(Memory.Summary);
			}
			RenderedInspection += FString::Join(MemorySummaries, TEXT(" | "));
		}
		RenderedInspection += TEXT("\n");
	}
	AppendSection(RenderedInspection, TEXT("LOCATION OWNERSHIP AND PURPOSE"));
	for (const FWorldLocation& Location : Spec.Locations)
	{
		RenderedInspection += FString::Printf(TEXT("- %s: %s; owner=%s; controller=%s; access=%s\n"),
			*Location.DisplayName, *Location.PurposeTag.ToString(), *Location.OwnerResidentId,
			*Location.ControllerResidentId, *AccessName(Location.AccessPolicy));
	}
	AppendSection(RenderedInspection, TEXT("GENERATION STAGES"));
	RenderedInspection += Bridge && !Bridge->GetGenerationStageHistory().IsEmpty()
		? FString::Join(Bridge->GetGenerationStageHistory(), TEXT(" -> ")) + TEXT("\n")
		: TEXT("Persisted replay/fixture; inspect Saved/WorldRuns for stage files.\n");
	AppendSection(RenderedInspection, TEXT("VALIDATION FAILURES AND REPAIRS"));
	if (Bridge && !Bridge->GetGenerationIssueHistory().IsEmpty())
	{
		for (const FValidationIssue& Issue : Bridge->GetGenerationIssueHistory())
		{
			RenderedInspection += FString::Printf(TEXT("- %s %s: %s\n"),
				*Issue.Code.ToString(), *Issue.Path, *Issue.Message);
		}
		RenderedInspection += FString::Printf(TEXT("Repair attempts: %d\n"),
			Bridge->GetGenerationRepairAttempts());
	}
	else
	{
		RenderedInspection += TEXT("No validation failure recorded in this live session.\n");
	}
	AppendSection(RenderedInspection, TEXT("CURRENT PROJECTS"));
	if (Projects != nullptr)
	{
		for (const FChangeProject& Project : Projects->GetProjects())
		{
			RenderedInspection += FString::Printf(TEXT("- %s: %s\n"), *Project.Id,
				*StaticEnum<EWorldDirectorProjectState>()->GetNameStringByValue(
					static_cast<int64>(Project.State)));
		}
	}
	AppendSection(RenderedInspection, TEXT("RECENT EVENTS"));
	for (const FWorldEvent& Event : Spec.Events)
	{
		RenderedInspection += FString::Printf(TEXT("- day %d, %s: %s\n"),
			Event.Day, *Event.Id, *Event.Summary);
	}
	if (Spec.Events.IsEmpty())
	{
		RenderedInspection += TEXT("No authored recent events.\n");
	}
	AppendSection(RenderedInspection, TEXT("DIRECTOR PROPOSAL / SIMULATION DECISION / BEFORE AND AFTER"));
	if (Projects != nullptr)
	{
		for (const FWorldDirectorProjectInspectionRecord& Record : Projects->GetInspectionRecords())
		{
			RenderedInspection += FString::Printf(
				TEXT("Project: %s\nProposal: %s\nDecision: %s\nLifecycle: %s\nBefore: %s\nAfter: %s\n"),
				*Record.ProjectId, *Record.Proposal, *Record.SimulationDecision,
				*FString::Join(Record.Lifecycle, TEXT(" -> ")), *Record.BeforeState, *Record.AfterState);
		}
	}
	AppendSection(RenderedInspection, TEXT("SIMULATION DECISION"));
	if (Simulation != nullptr)
	{
		for (const FWorldDirectorResidentRuntimeState& Resident : Simulation->GetResidentStates())
		{
			RenderedInspection += FString::Printf(TEXT("- %s: %s -> %s (%s)\n"),
				*Resident.ResidentId, *Resident.CurrentLocationId, *Resident.IntendedLocationId,
				*Resident.ActivityTag.ToString());
		}
	}
	InspectionText->SetText(FText::FromString(RenderedInspection));
}

bool UWorldDirectorInspectionWidget::IsInspectionReady() const
{
	const UWorldStateSubsystem* WorldState = GetWorld()
		? GetWorld()->GetSubsystem<UWorldStateSubsystem>() : nullptr;
	const UTownSimulationSubsystem* Simulation = GetWorld()
		? GetWorld()->GetSubsystem<UTownSimulationSubsystem>() : nullptr;
	const UChangeProjectSubsystem* Projects = GetWorld()
		? GetWorld()->GetSubsystem<UChangeProjectSubsystem>() : nullptr;
	const FGeneratedWorldSpec* Spec = WorldState && WorldState->HasActiveWorldSpec()
		? &WorldState->GetActiveWorldSpec() : nullptr;
	return InspectionText != nullptr && Spec != nullptr &&
		!Spec->Facts.IsEmpty() && !Spec->Relationships.IsEmpty() &&
		!Spec->Residents.IsEmpty() && !Spec->Locations.IsEmpty() &&
		Simulation != nullptr && !Simulation->GetResidentStates().IsEmpty() &&
		Projects != nullptr && !Projects->GetProjects().IsEmpty() &&
		!Projects->GetInspectionRecords().IsEmpty() &&
		RenderedInspection.Contains(TEXT("ACCEPTED WORLD FACTS")) &&
		RenderedInspection.Contains(TEXT("NPC RELATIONSHIPS")) &&
		RenderedInspection.Contains(TEXT("BELIEFS AND MEMORIES")) &&
		RenderedInspection.Contains(TEXT("LOCATION OWNERSHIP AND PURPOSE")) &&
		RenderedInspection.Contains(TEXT("GENERATION STAGES")) &&
		RenderedInspection.Contains(TEXT("VALIDATION FAILURES AND REPAIRS")) &&
		RenderedInspection.Contains(TEXT("CURRENT PROJECTS")) &&
		RenderedInspection.Contains(TEXT("RECENT EVENTS")) &&
		RenderedInspection.Contains(TEXT("DIRECTOR PROPOSAL / SIMULATION DECISION / BEFORE AND AFTER"));
}

void UWorldDirectorInspectionWidget::CloseInspection()
{
	if (Town != nullptr)
	{
		Town->CloseInspectionMenu();
	}
	else
	{
		RemoveFromParent();
	}
}
