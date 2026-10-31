#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Input/Events.h"
#include "Styling/SlateBrush.h"
#include "WorldDirectorTypes.h"

#include "WorldDirectorMapWidget.generated.h"

/**
 * Top-down map of the compiled town (M). An orthographic scene capture of the real
 * town, straight down with +X up and +Y right, sits under the plan overlays (routes,
 * districts, location labels, player, waypoint), so the picture shows the layout the
 * generator actually built.
 */
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
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseMove(
		const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(
		const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(
		const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseWheel(
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
	void GetContentRect(const FVector2D& WidgetSize, FVector2D& OutMin, FVector2D& OutMax) const;
	/** Widget units per world centimetre at the current zoom. */
	float MapScale(const FVector2D& WidgetSize) const;
	void FitViewToSettlement();
	void FitViewToTerrain();
	void ClampView();
	void UpdateAerialCapture(const FVector2D& WidgetSize);
	void ReleaseAerialCapture();
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

	UPROPERTY(Transient)
	TObjectPtr<class ASceneCapture2D> AerialCapture;

	UPROPERTY(Transient)
	TObjectPtr<class UTextureRenderTarget2D> AerialTarget;

	FSlateBrush AerialBrush;
	FResolvedWorldPlan MapPlan;
	FGeneratedWorldSpec MapSpec;
	FString HoveredLocationId;
	FString WaypointLocationId;
	FVector2D HoveredScreenPosition = FVector2D::ZeroVector;
	FVector2D WaypointWorldPosition = FVector2D::ZeroVector;
	/** World XY at the centre of the map frame. */
	FVector2D ViewCenter = FVector2D::ZeroVector;
	/** World centimetres across the shorter side of the map frame. */
	float ViewSpan = 0.0f;
	FVector2D CapturedCenter = FVector2D::ZeroVector;
	float CapturedSpan = 0.0f;
	FIntPoint CapturedSize = FIntPoint::ZeroValue;
	FVector2D DragStartPosition = FVector2D::ZeroVector;
	FVector2D DragStartCenter = FVector2D::ZeroVector;
	bool bHasWaypoint = false;
	bool bHasHoveredScreenPosition = false;
	bool bShowAerial = true;
	bool bShowAllLabels = true;
	bool bDragging = false;
	bool bDragMoved = false;
};
