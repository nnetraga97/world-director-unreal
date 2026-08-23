#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "WorldDirectorDialogueWidget.generated.h"

UCLASS()
class WORLDDIRECTORRUNTIME_API UWorldDirectorDialogueWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitializeForResident(class AWorldDirectorResidentActor* InResident);
	void ApplyViewportLayout();
	bool IsDialogueMenuReady() const { return TitleText != nullptr && BodyText != nullptr; }

protected:
	virtual void NativeConstruct() override;

private:
	void RebuildResidentText();

	UFUNCTION()
	void AskAboutTown();

	UFUNCTION()
	void OfferHelp();

	UFUNCTION()
	void LeaveConversation();

	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> TitleText;

	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> BodyText;

	UPROPERTY(Transient)
	TObjectPtr<class AWorldDirectorResidentActor> Resident;

	FString ResidentId;
	FString KnownFactSummary;
};
