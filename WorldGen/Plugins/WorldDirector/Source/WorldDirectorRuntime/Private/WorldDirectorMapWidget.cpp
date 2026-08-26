#include "WorldDirectorMapWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Border.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "WorldDirectorSubsystems.h"
#include "WorldDirectorTownActors.h"

namespace
{
constexpr float MapHeaderHeight = 82.0f;
constexpr float MapFooterHeight = 42.0f;
constexpr float MapOuterMargin = 28.0f;
constexpr float MapSidebarWidth = 292.0f;
constexpr float MapPadding = 26.0f;
constexpr float LocationHoverRadius = 18.0f;

const FSlateBrush* WhiteBrush()
{
	return FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
}

void DrawBox(
	FSlateWindowElementList& OutDrawElements,
	const int32 LayerId,
	const FPaintGeometry& Geometry,
	const FLinearColor& Color)
{
	FSlateDrawElement::MakeBox(
		OutDrawElements, LayerId, Geometry, WhiteBrush(), ESlateDrawEffect::None, Color);
}

void DrawLine(
	FSlateWindowElementList& OutDrawElements,
	const int32 LayerId,
	const TArray<FVector2D>& Points,
	const FLinearColor& Color,
	const float Thickness = 1.0f,
	const bool bClosed = false)
{
	if (Points.Num() < 2)
	{
		return;
	}
	FSlateDrawElement::MakeLines(
		OutDrawElements, LayerId, FPaintGeometry(), Points,
		ESlateDrawEffect::None, Color, true, Thickness);
	if (bClosed)
	{
		TArray<FVector2D> ClosingLine;
		ClosingLine.Add(Points.Last());
		ClosingLine.Add(Points[0]);
		FSlateDrawElement::MakeLines(
			OutDrawElements, LayerId, FPaintGeometry(), ClosingLine,
			ESlateDrawEffect::None, Color, true, Thickness);
	}
}

void DrawText(
	FSlateWindowElementList& OutDrawElements,
	const int32 LayerId,
	const FVector2D& Position,
	const FVector2D& Size,
	const FString& Text,
	const FSlateFontInfo& Font,
	const FLinearColor& Color)
{
	if (Text.IsEmpty())
	{
		return;
	}
	FSlateDrawElement::MakeText(
		OutDrawElements, LayerId,
		FPaintGeometry(Position, Size, 1.0f),
		FText::FromString(Text), Font, ESlateDrawEffect::None, Color);
}

TArray<FVector2D> MakeCircle(const FVector2D& Center, const float Radius, const int32 Segments = 24)
{
	TArray<FVector2D> Points;
	Points.Reserve(Segments);
	for (int32 Index = 0; Index < Segments; ++Index)
	{
		const float Angle = 2.0f * PI * static_cast<float>(Index) / static_cast<float>(Segments);
		Points.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
	}
	return Points;
}

const FWorldLocation* FindLocation(const FGeneratedWorldSpec& Spec, const FString& LocationId)
{
	return Spec.Locations.FindByPredicate(
		[&LocationId](const FWorldLocation& Location)
		{
			return Location.Id == LocationId;
		});
}
}

void UWorldDirectorMapWidget::InitializeForBootstrap(AWorldDirectorFixtureBootstrap* InBootstrap)
{
	Bootstrap = InBootstrap;
}

void UWorldDirectorMapWidget::InitializeForTown(AWorldDirectorTownActor* InTown)
{
	Town = InTown;
	HoveredLocationId.Reset();
	WaypointLocationId.Reset();
	bHasWaypoint = false;
	bHasHoveredScreenPosition = false;
	RefreshMapSnapshot();
}

TSharedRef<SWidget> UWorldDirectorMapWidget::RebuildWidget()
{
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		UBorder* Root = WidgetTree->ConstructWidget<UBorder>();
		Root->SetBrushColor(FLinearColor(0.015f, 0.02f, 0.022f, 0.98f));
		WidgetTree->RootWidget = Root;
	}
	return Super::RebuildWidget();
}

void UWorldDirectorMapWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	SetIsEnabled(true);
	ApplyViewportLayout();
	RefreshMapSnapshot();
}

void UWorldDirectorMapWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Invalidate(EInvalidateWidget::Paint);
}

bool UWorldDirectorMapWidget::ApplyViewportLayout()
{
	const FVector2D ViewportSize = UWidgetLayoutLibrary::GetViewportSize(this);
	if (ViewportSize.X <= 0.0f || ViewportSize.Y <= 0.0f)
	{
		return false;
	}
	SetAnchorsInViewport(FAnchors(0.0f, 0.0f));
	SetAlignmentInViewport(FVector2D::ZeroVector);
	SetPositionInViewport(FVector2D::ZeroVector, false);
	SetDesiredSizeInViewport(ViewportSize);
	return true;
}

void UWorldDirectorMapWidget::RefreshMapSnapshot()
{
	if (Bootstrap != nullptr && Town == nullptr)
	{
		Town = Bootstrap->CompiledTown;
	}
	if (Town != nullptr)
	{
		MapPlan = Town->GetCompiledPlan();
	}
	const UWorldStateSubsystem* WorldState = GetWorld()
		? GetWorld()->GetSubsystem<UWorldStateSubsystem>() : nullptr;
	if (WorldState != nullptr && WorldState->HasActiveWorldSpec())
	{
		MapSpec = WorldState->GetActiveWorldSpec();
	}
}

void UWorldDirectorMapWidget::GetMapRect(
	const FVector2D& WidgetSize, FVector2D& OutMin, FVector2D& OutMax) const
{
	const float SidebarLeft = FMath::Max(
		WidgetSize.X - MapSidebarWidth - MapOuterMargin, WidgetSize.X * 0.63f);
	OutMin = FVector2D(MapOuterMargin, MapHeaderHeight);
	OutMax = FVector2D(
		FMath::Max(OutMin.X + 180.0f, SidebarLeft - MapOuterMargin * 0.5f),
		FMath::Max(OutMin.Y + 180.0f, WidgetSize.Y - MapFooterHeight));
}

FVector2D UWorldDirectorMapWidget::WorldToMap(
	const FVector2D& WorldPosition, const FVector2D& WidgetSize) const
{
	FVector2D MapMin;
	FVector2D MapMax;
	GetMapRect(WidgetSize, MapMin, MapMax);
	const float Extent = FMath::Max(1.0f, static_cast<float>(MapPlan.Terrain.ExtentCentimeters));
	const float NormalizedX = FMath::GetRangePct(-Extent, Extent, WorldPosition.X);
	const float NormalizedY = FMath::GetRangePct(-Extent, Extent, WorldPosition.Y);
	return FVector2D(
		FMath::Lerp(MapMin.X + MapPadding, MapMax.X - MapPadding, NormalizedX),
		FMath::Lerp(MapMax.Y - MapPadding, MapMin.Y + MapPadding, NormalizedY));
}

FVector2D UWorldDirectorMapWidget::MapToWorld(
	const FVector2D& MapPosition, const FVector2D& WidgetSize) const
{
	FVector2D MapMin;
	FVector2D MapMax;
	GetMapRect(WidgetSize, MapMin, MapMax);
	const FVector2D ContentMin = MapMin + FVector2D(MapPadding);
	const FVector2D ContentMax = MapMax - FVector2D(MapPadding);
	const float Extent = FMath::Max(1.0f, static_cast<float>(MapPlan.Terrain.ExtentCentimeters));
	const float NormalizedX = FMath::Clamp(
		(MapPosition.X - ContentMin.X) / FMath::Max(1.0f, ContentMax.X - ContentMin.X), 0.0f, 1.0f);
	const float NormalizedY = FMath::Clamp(
		(ContentMax.Y - MapPosition.Y) / FMath::Max(1.0f, ContentMax.Y - ContentMin.Y), 0.0f, 1.0f);
	return FVector2D(
		FMath::Lerp(-Extent, Extent, NormalizedX),
		FMath::Lerp(-Extent, Extent, NormalizedY));
}

bool UWorldDirectorMapWidget::IsMapPosition(
	const FVector2D& Position, const FVector2D& WidgetSize) const
{
	FVector2D MapMin;
	FVector2D MapMax;
	GetMapRect(WidgetSize, MapMin, MapMax);
	return Position.X >= MapMin.X && Position.X <= MapMax.X &&
		Position.Y >= MapMin.Y && Position.Y <= MapMax.Y;
}

void UWorldDirectorMapWidget::UpdateHoveredFeature(
	const FVector2D& Position, const FVector2D& WidgetSize)
{
	HoveredLocationId.Reset();
	bHasHoveredScreenPosition = false;
	if (!IsMapPosition(Position, WidgetSize))
	{
		return;
	}
	HoveredScreenPosition = Position;
	bHasHoveredScreenPosition = true;
	float BestDistance = LocationHoverRadius;
	for (const FResolvedLocationPlan& Location : MapPlan.Locations)
	{
		const float Distance = FVector2D::Distance(
			WorldToMap(FVector2D(Location.Transform.GetLocation()), WidgetSize), Position);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			HoveredLocationId = Location.LocationId;
		}
	}
}

void UWorldDirectorMapWidget::SetWaypoint(
	const FVector2D& WorldPosition, const FString& LocationId)
{
	WaypointWorldPosition = WorldPosition;
	WaypointLocationId = LocationId;
	bHasWaypoint = true;
}

void UWorldDirectorMapWidget::ClearWaypoint()
{
	WaypointLocationId.Reset();
	bHasWaypoint = false;
}

FString UWorldDirectorMapWidget::DisplayNameForLocation(const FString& LocationId) const
{
	if (const FWorldLocation* Location = FindLocation(MapSpec, LocationId))
	{
		return Location->DisplayName.IsEmpty() ? Location->Id : Location->DisplayName;
	}
	return LocationId;
}

FString UWorldDirectorMapWidget::PurposeForLocation(const FString& LocationId) const
{
	if (const FWorldLocation* Location = FindLocation(MapSpec, LocationId))
	{
		return Location->PurposeTag.ToString();
	}
	if (const FResolvedLocationPlan* Location = MapPlan.Locations.FindByPredicate(
		[&LocationId](const FResolvedLocationPlan& Candidate)
		{
			return Candidate.LocationId == LocationId;
		}))
	{
		return Location->PurposeTag.ToString();
	}
	return TEXT("Location");
}

FLinearColor UWorldDirectorMapWidget::MarkerColor(const FString& LocationId) const
{
	if (LocationId == MapPlan.LandmarkLocationId)
	{
		return FLinearColor(0.98f, 0.73f, 0.22f, 1.0f);
	}
	const FString Purpose = PurposeForLocation(LocationId);
	if (Purpose.Contains(TEXT("Home"), ESearchCase::IgnoreCase))
	{
		return FLinearColor(0.43f, 0.72f, 0.86f, 1.0f);
	}
	if (Purpose.Contains(TEXT("Landmark"), ESearchCase::IgnoreCase) ||
		Purpose.Contains(TEXT("Clinic"), ESearchCase::IgnoreCase) ||
		Purpose.Contains(TEXT("Shelter"), ESearchCase::IgnoreCase) ||
		Purpose.Contains(TEXT("Headquarters"), ESearchCase::IgnoreCase))
	{
		return FLinearColor(0.86f, 0.55f, 0.38f, 1.0f);
	}
	return FLinearColor(0.46f, 0.82f, 0.65f, 1.0f);
}

void UWorldDirectorMapWidget::RequestClose()
{
	if (Bootstrap != nullptr)
	{
		Bootstrap->CloseMapView();
	}
}

FReply UWorldDirectorMapWidget::NativeOnMouseMove(
	const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	UpdateHoveredFeature(
		InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()),
		InGeometry.GetLocalSize());
	Invalidate(EInvalidateWidget::Paint);
	return FReply::Handled();
}

FReply UWorldDirectorMapWidget::NativeOnMouseButtonDown(
	const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	const FVector2D Position =
		InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
	const FVector2D WidgetSize = InGeometry.GetLocalSize();
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		ClearWaypoint();
		Invalidate(EInvalidateWidget::Paint);
		return FReply::Handled();
	}
	if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton ||
		!IsMapPosition(Position, WidgetSize))
	{
		return FReply::Handled();
	}
	UpdateHoveredFeature(Position, WidgetSize);
	const FVector2D WorldPosition = !HoveredLocationId.IsEmpty()
		? FVector2D(MapPlan.Locations.FindByPredicate(
			[&](const FResolvedLocationPlan& Location)
			{
				return Location.LocationId == HoveredLocationId;
			})->Transform.GetLocation())
		: MapToWorld(Position, WidgetSize);
	SetWaypoint(WorldPosition, HoveredLocationId);
	Invalidate(EInvalidateWidget::Paint);
	return FReply::Handled().SetUserFocus(TakeWidget(), EFocusCause::Mouse);
}

FReply UWorldDirectorMapWidget::NativeOnKeyDown(
	const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::M)
	{
		RequestClose();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

int32 UWorldDirectorMapWidget::NativePaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	const int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	const bool bParentEnabled) const
{
	int32 CurrentLayer = Super::NativePaint(
		Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId,
		InWidgetStyle, bParentEnabled);
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	FVector2D MapMin;
	FVector2D MapMax;
	GetMapRect(Size, MapMin, MapMax);
	const FVector2D SidebarMin(
		FMath::Max(Size.X - MapSidebarWidth, MapMax.X + MapOuterMargin * 0.5f), MapHeaderHeight);
	const FVector2D SidebarMax(Size.X - MapOuterMargin, Size.Y - MapFooterHeight);
	const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 25.0f);
	const FSlateFontInfo SectionFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 12.0f);
	const FSlateFontInfo BodyFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 13.0f);
	const FSlateFontInfo SmallFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 11.0f);
	const FLinearColor Ink(0.88f, 0.91f, 0.90f, 1.0f);
	const FLinearColor Muted(0.55f, 0.63f, 0.63f, 1.0f);
	const FLinearColor Gold(0.96f, 0.72f, 0.30f, 1.0f);

	DrawBox(OutDrawElements, CurrentLayer++, AllottedGeometry.ToPaintGeometry(),
		FLinearColor(0.012f, 0.018f, 0.021f, 0.98f));
	DrawText(OutDrawElements, CurrentLayer++, FVector2D(MapOuterMargin, 24.0f),
		FVector2D(700.0f, 34.0f), TEXT("SETTLEMENT MAP"), TitleFont, Gold);
	DrawText(OutDrawElements, CurrentLayer++, FVector2D(MapOuterMargin + 244.0f, 31.0f),
		FVector2D(600.0f, 22.0f),
		MapSpec.Brief.SettlementIdentity.IsEmpty()
			? TEXT("LIVE WORLD / NORTH UP")
			: MapSpec.Brief.SettlementIdentity + TEXT("  /  LIVE WORLD / NORTH UP"),
		BodyFont, Muted);
	DrawText(OutDrawElements, CurrentLayer++, FVector2D(Size.X - 190.0f, 33.0f),
		FVector2D(160.0f, 22.0f), TEXT("M / ESC  CLOSE"), SmallFont, Ink);

	DrawBox(OutDrawElements, CurrentLayer++,
		FPaintGeometry(MapMin, MapMax - MapMin, 1.0f), FLinearColor(0.045f, 0.09f, 0.085f, 1.0f));
	DrawBox(OutDrawElements, CurrentLayer++,
		FPaintGeometry(SidebarMin, SidebarMax - SidebarMin, 1.0f), FLinearColor(0.035f, 0.043f, 0.045f, 1.0f));

	const FVector2D ContentMin = MapMin + FVector2D(MapPadding);
	const FVector2D ContentMax = MapMax - FVector2D(MapPadding);
	const FLinearColor GridColor(0.18f, 0.31f, 0.28f, 0.55f);
	for (int32 GridIndex = 1; GridIndex < 5; ++GridIndex)
	{
		const float X = FMath::Lerp(ContentMin.X, ContentMax.X, GridIndex / 5.0f);
		const float Y = FMath::Lerp(ContentMin.Y, ContentMax.Y, GridIndex / 5.0f);
		DrawLine(OutDrawElements, CurrentLayer, { FVector2D(X, ContentMin.Y), FVector2D(X, ContentMax.Y) }, GridColor, 1.0f);
		DrawLine(OutDrawElements, CurrentLayer++, { FVector2D(ContentMin.X, Y), FVector2D(ContentMax.X, Y) }, GridColor, 1.0f);
	}
	DrawLine(OutDrawElements, CurrentLayer++,
		{ FVector2D(ContentMin.X, ContentMin.Y), FVector2D(ContentMax.X, ContentMin.Y),
		  FVector2D(ContentMax.X, ContentMax.Y), FVector2D(ContentMin.X, ContentMax.Y) },
		FLinearColor(0.35f, 0.55f, 0.48f, 0.8f), 1.5f, true);

	// Water and cultivated parcels are subdued context layers underneath roads.
	if (MapPlan.Terrain.WaterControlPoints.Num() > 1)
	{
		TArray<FVector2D> Water;
		for (const FVector& Point : MapPlan.Terrain.WaterControlPoints)
		{
			Water.Add(WorldToMap(FVector2D(Point), Size));
		}
		DrawLine(OutDrawElements, CurrentLayer++, Water, FLinearColor(0.22f, 0.54f, 0.72f, 0.8f), 8.0f);
		DrawLine(OutDrawElements, CurrentLayer++, Water, FLinearColor(0.42f, 0.76f, 0.86f, 0.85f), 1.5f);
	}
	for (const FWorldDirectorFarmParcel& Parcel : MapPlan.Terrain.FarmParcels)
	{
		TArray<FVector2D> Boundary;
		for (const FVector2D& Point : Parcel.BoundaryPoints)
		{
			Boundary.Add(WorldToMap(Point, Size));
		}
		DrawLine(OutDrawElements, CurrentLayer++, Boundary, FLinearColor(0.67f, 0.61f, 0.35f, 0.5f), 1.0f, true);
	}
	for (const FWorldDirectorDistrictAnchor& District : MapPlan.DistrictAnchors)
	{
		const FVector2D Center = WorldToMap(FVector2D(District.Position), Size);
		const float Radius = FMath::Max(
			18.0f, District.InfluenceRadiusCentimeters /
			FMath::Max(1.0f, static_cast<float>(MapPlan.Terrain.ExtentCentimeters) * 2.0f) *
			(ContentMax.X - ContentMin.X));
		DrawLine(OutDrawElements, CurrentLayer++, MakeCircle(Center, Radius),
			FLinearColor(0.38f, 0.53f, 0.40f, 0.25f), 1.0f);
		DrawText(OutDrawElements, CurrentLayer++, Center + FVector2D(7.0f, -8.0f),
			FVector2D(150.0f, 18.0f), District.DistrictId, SmallFont,
			FLinearColor(0.54f, 0.67f, 0.58f, 0.7f));
	}

	for (const FResolvedRoutePlan& Route : MapPlan.Routes)
	{
		TArray<FVector2D> RoutePoints;
		for (const FVector& Point : Route.ControlPoints)
		{
			RoutePoints.Add(WorldToMap(FVector2D(Point), Size));
		}
		DrawLine(OutDrawElements, CurrentLayer++, RoutePoints,
			Route.RouteType == TEXT("Road")
				? FLinearColor(0.87f, 0.73f, 0.45f, 0.92f)
				: FLinearColor(0.68f, 0.69f, 0.55f, 0.78f),
			Route.RouteType == TEXT("Road") ? 4.0f : 2.0f);
	}

	for (const FResolvedLocationPlan& Location : MapPlan.Locations)
	{
		const FVector2D Marker = WorldToMap(FVector2D(Location.Transform.GetLocation()), Size);
		const FLinearColor Color = MarkerColor(Location.LocationId);
		const bool bHovered = Location.LocationId == HoveredLocationId;
		const float Radius = bHovered ? 9.0f : 6.0f;
		DrawBox(OutDrawElements, CurrentLayer++,
			FPaintGeometry(Marker - FVector2D(Radius), FVector2D(Radius * 2.0f), 1.0f), Color);
		DrawLine(OutDrawElements, CurrentLayer++, MakeCircle(Marker, Radius + (bHovered ? 5.0f : 2.0f)),
			bHovered ? Ink : Color, bHovered ? 2.0f : 1.0f);
		if (bHovered || Location.LocationId == MapPlan.LandmarkLocationId)
		{
			DrawText(OutDrawElements, CurrentLayer++, Marker + FVector2D(12.0f, -11.0f),
				FVector2D(220.0f, 20.0f), DisplayNameForLocation(Location.LocationId),
				SmallFont, bHovered ? Ink : Color);
		}
	}

	if (bHasWaypoint)
	{
		const FVector2D Waypoint = WorldToMap(WaypointWorldPosition, Size);
		DrawLine(OutDrawElements, CurrentLayer++, MakeCircle(Waypoint, 12.0f),
			FLinearColor(0.93f, 0.38f, 0.74f, 1.0f), 2.0f);
		DrawLine(OutDrawElements, CurrentLayer++,
			{ Waypoint + FVector2D(-17.0f, 0.0f), Waypoint + FVector2D(17.0f, 0.0f),
			  Waypoint, Waypoint + FVector2D(0.0f, 17.0f),
			  Waypoint, Waypoint + FVector2D(0.0f, -17.0f) },
			FLinearColor(0.98f, 0.66f, 0.86f, 1.0f), 1.5f);
	}

	if (const APlayerController* PlayerController = GetWorld()
		? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		if (const APawn* Pawn = PlayerController->GetPawn())
		{
			const FVector2D Player = WorldToMap(FVector2D(Pawn->GetActorLocation()), Size);
			DrawLine(OutDrawElements, CurrentLayer++, MakeCircle(Player, 9.0f),
				FLinearColor(0.98f, 0.94f, 0.76f, 1.0f), 2.0f);
			DrawBox(OutDrawElements, CurrentLayer++,
				FPaintGeometry(Player - FVector2D(3.0f), FVector2D(6.0f), 1.0f),
				FLinearColor(0.98f, 0.94f, 0.76f, 1.0f));
		}
	}

	DrawText(OutDrawElements, CurrentLayer++, FVector2D(MapMin.X + 12.0f, MapMin.Y + 10.0f),
		FVector2D(30.0f, 18.0f), TEXT("N"), SectionFont, Ink);
	DrawText(OutDrawElements, CurrentLayer++, FVector2D(MapMin.X + 12.0f, MapMax.Y - 28.0f),
		FVector2D(30.0f, 18.0f), TEXT("S"), SmallFont, Muted);
	DrawText(OutDrawElements, CurrentLayer++, FVector2D(MapMin.X + 13.0f, MapMax.Y - 19.0f),
		FVector2D(30.0f, 18.0f), TEXT("W"), SmallFont, Muted);
	DrawText(OutDrawElements, CurrentLayer++, FVector2D(MapMax.X - 24.0f, MapMax.Y - 19.0f),
		FVector2D(30.0f, 18.0f), TEXT("E"), SmallFont, Muted);

	const float SidebarTextX = SidebarMin.X + 20.0f;
	DrawText(OutDrawElements, CurrentLayer++, FVector2D(SidebarTextX, SidebarMin.Y + 22.0f),
		FVector2D(220.0f, 22.0f), TEXT("MAP DETAILS"), SectionFont, Gold);
	DrawText(OutDrawElements, CurrentLayer++, FVector2D(SidebarTextX, SidebarMin.Y + 60.0f),
		FVector2D(220.0f, 18.0f), TEXT("HOVERED FEATURE"), SmallFont, Muted);
	const FString HoverTitle = HoveredLocationId.IsEmpty()
		? TEXT("Move over a location marker") : DisplayNameForLocation(HoveredLocationId);
	DrawText(OutDrawElements, CurrentLayer++, FVector2D(SidebarTextX, SidebarMin.Y + 82.0f),
		FVector2D(SidebarMax.X - SidebarTextX - 16.0f, 42.0f), HoverTitle, BodyFont, Ink);
	if (!HoveredLocationId.IsEmpty())
	{
		DrawText(OutDrawElements, CurrentLayer++, FVector2D(SidebarTextX, SidebarMin.Y + 112.0f),
			FVector2D(230.0f, 20.0f), PurposeForLocation(HoveredLocationId), SmallFont, Muted);
	}

	const float WaypointY = SidebarMin.Y + 166.0f;
	DrawText(OutDrawElements, CurrentLayer++, FVector2D(SidebarTextX, WaypointY),
		FVector2D(220.0f, 18.0f), TEXT("WAYPOINT"), SmallFont, Muted);
	const FString WaypointText = !bHasWaypoint
		? TEXT("None set")
		: WaypointLocationId.IsEmpty()
			? FString::Printf(TEXT("Free point  %+.0f, %+.0f"), WaypointWorldPosition.X, WaypointWorldPosition.Y)
			: DisplayNameForLocation(WaypointLocationId);
	DrawText(OutDrawElements, CurrentLayer++, FVector2D(SidebarTextX, WaypointY + 22.0f),
			FVector2D(SidebarMax.X - SidebarTextX - 16.0f, 40.0f), WaypointText, BodyFont,
			bHasWaypoint ? FLinearColor(0.98f, 0.66f, 0.86f, 1.0f) : Muted);

	const float LegendY = SidebarMax.Y - 192.0f;
	DrawText(OutDrawElements, CurrentLayer++, FVector2D(SidebarTextX, LegendY),
		FVector2D(220.0f, 18.0f), TEXT("LEGEND"), SmallFont, Muted);
	const TArray<TPair<FString, FLinearColor>> Legend = {
		{ TEXT("Landmark"), FLinearColor(0.98f, 0.73f, 0.22f, 1.0f) },
		{ TEXT("Civic"), FLinearColor(0.86f, 0.55f, 0.38f, 1.0f) },
		{ TEXT("Home"), FLinearColor(0.43f, 0.72f, 0.86f, 1.0f) },
		{ TEXT("Waypoint"), FLinearColor(0.93f, 0.38f, 0.74f, 1.0f) }
	};
	for (int32 Index = 0; Index < Legend.Num(); ++Index)
	{
		const float Y = LegendY + 25.0f + Index * 23.0f;
		DrawBox(OutDrawElements, CurrentLayer++,
			FPaintGeometry(FVector2D(SidebarTextX, Y + 3.0f), FVector2D(10.0f), 1.0f), Legend[Index].Value);
		DrawText(OutDrawElements, CurrentLayer++, FVector2D(SidebarTextX + 20.0f, Y),
			FVector2D(180.0f, 18.0f), Legend[Index].Key, SmallFont, Ink);
	}

	DrawText(OutDrawElements, CurrentLayer++, FVector2D(MapOuterMargin, Size.Y - 28.0f),
		FVector2D(Size.X - MapOuterMargin * 2.0f, 20.0f),
		TEXT("LMB  set waypoint     RMB  clear waypoint     Hover markers for details"),
		SmallFont, Muted);

	if (!HoveredLocationId.IsEmpty() && bHasHoveredScreenPosition)
	{
		const FVector2D TooltipSize(230.0f, 54.0f);
		FVector2D TooltipPosition = HoveredScreenPosition + FVector2D(18.0f, 18.0f);
		if (TooltipPosition.X + TooltipSize.X > MapMax.X)
		{
			TooltipPosition.X = HoveredScreenPosition.X - TooltipSize.X - 18.0f;
		}
		if (TooltipPosition.Y + TooltipSize.Y > MapMax.Y)
		{
			TooltipPosition.Y = HoveredScreenPosition.Y - TooltipSize.Y - 18.0f;
		}
		DrawBox(OutDrawElements, CurrentLayer++, FPaintGeometry(TooltipPosition, TooltipSize, 1.0f),
			FLinearColor(0.02f, 0.03f, 0.03f, 0.96f));
		DrawText(OutDrawElements, CurrentLayer++, TooltipPosition + FVector2D(10.0f, 8.0f),
			FVector2D(210.0f, 18.0f), DisplayNameForLocation(HoveredLocationId), BodyFont, Ink);
		DrawText(OutDrawElements, CurrentLayer++, TooltipPosition + FVector2D(10.0f, 30.0f),
			FVector2D(210.0f, 16.0f), PurposeForLocation(HoveredLocationId), SmallFont, Gold);
	}

	return CurrentLayer;
}
