#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "WorldDirectorInspectionWidget.generated.h"

UCLASS()
class WORLDDIRECTORRUNTIME_API UWorldDirectorInspectionWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitializeForTown(class AWorldDirectorTownActor* InTown);
	bool ApplyViewportLayout();
	bool IsInspectionReady() const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void BuildWidgetTree();
	void RefreshInspectionText();

	UFUNCTION()
	void CloseInspection();

	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> InspectionText;

	UPROPERTY(Transient)
	TObjectPtr<class AWorldDirectorTownActor> Town;

	FString RenderedInspection;
	double RefreshAccumulator = 0.0;
};
