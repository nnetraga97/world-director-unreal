#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Input/Events.h"
#include "WorldDirectorTypes.h"

#include "WorldDirectorMapWidget.generated.h"

UCLASS()
class WORLDDIRECTORRUNTIME_API UWorldDirectorMapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitializeForBootstrap(class AWorldDirectorFixtureBootstrap* InBootstrap);
	void InitializeForTown(class AWorldDirectorTownActor* InTown);
	bool ApplyViewportLayout();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseMove(
		const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(
		const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnKeyDown(
		const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual int32 NativePaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

private:
	void RefreshMapSnapshot();
	void GetMapRect(const FVector2D& WidgetSize, FVector2D& OutMin, FVector2D& OutMax) const;
	FVector2D WorldToMap(const FVector2D& WorldPosition, const FVector2D& WidgetSize) const;
	FVector2D MapToWorld(const FVector2D& MapPosition, const FVector2D& WidgetSize) const;
	bool IsMapPosition(const FVector2D& Position, const FVector2D& WidgetSize) const;
	void UpdateHoveredFeature(const FVector2D& Position, const FVector2D& WidgetSize);
	void SetWaypoint(const FVector2D& WorldPosition, const FString& LocationId = FString());
	void ClearWaypoint();
	FString DisplayNameForLocation(const FString& LocationId) const;
	FString PurposeForLocation(const FString& LocationId) const;
	FLinearColor MarkerColor(const FString& LocationId) const;
	void RequestClose();

	UPROPERTY(Transient)
	TObjectPtr<class AWorldDirectorFixtureBootstrap> Bootstrap;

	UPROPERTY(Transient)
	TObjectPtr<class AWorldDirectorTownActor> Town;

	FResolvedWorldPlan MapPlan;
	FGeneratedWorldSpec MapSpec;
	FString HoveredLocationId;
	FString WaypointLocationId;
	FVector2D HoveredScreenPosition = FVector2D::ZeroVector;
	FVector2D WaypointWorldPosition = FVector2D::ZeroVector;
	bool bHasWaypoint = false;
	bool bHasHoveredScreenPosition = false;
};
