#include "WorldDirectorMapWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Border.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Layout/Clipping.h"
#include "ProceduralMeshComponent.h"
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
constexpr float MinimumViewSpan = 3000.0f;
// A drag shorter than this is a click (set waypoint), not a pan.
constexpr float DragThreshold = 4.0f;
// Long side of the aerial capture in pixels; a town needs no more.
constexpr float MaxAerialResolution = 2048.0f;

const FSlateBrush* WhiteBrush()
{
	return FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
}

void DrawBox(
	FSlateWindowElementList& OutDrawElements,
	const int32 LayerId,
	const FGeometry& WidgetGeometry,
	const FVector2D& LocalPosition,
	const FVector2D& LocalSize,
	const FLinearColor& Color)
{
	FSlateDrawElement::MakeBox(
		OutDrawElements, LayerId,
		WidgetGeometry.ToPaintGeometry(
			UE::Slate::CastToVector2f(LocalSize),
			FSlateLayoutTransform(UE::Slate::CastToVector2f(LocalPosition))),
		WhiteBrush(), ESlateDrawEffect::None, Color);
}

void DrawLine(
	FSlateWindowElementList& OutDrawElements,
	const int32 LayerId,
	const FGeometry& WidgetGeometry,
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
		OutDrawElements, LayerId, WidgetGeometry.ToPaintGeometry(), Points,
		ESlateDrawEffect::None, Color, true, Thickness);
	if (bClosed)
	{
		TArray<FVector2D> ClosingLine;
		ClosingLine.Add(Points.Last());
		ClosingLine.Add(Points[0]);
		FSlateDrawElement::MakeLines(
			OutDrawElements, LayerId, WidgetGeometry.ToPaintGeometry(), ClosingLine,
			ESlateDrawEffect::None, Color, true, Thickness);
	}
}

void DrawText(
	FSlateWindowElementList& OutDrawElements,
	const int32 LayerId,
	const FGeometry& WidgetGeometry,
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
		WidgetGeometry.ToPaintGeometry(
			UE::Slate::CastToVector2f(Size),
			FSlateLayoutTransform(UE::Slate::CastToVector2f(Position))),
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

/** Largest 1, 2 or 5 x 10^n metres that fits in MaxCentimetres, for the scale bar. */
float NiceScaleBarCentimetres(const float MaxCentimetres)
{
	const float MaxMetres = FMath::Max(1.0f, MaxCentimetres / 100.0f);
	const float Magnitude = FMath::Pow(10.0f, FMath::FloorToFloat(FMath::LogX(10.0f, MaxMetres)));
	for (const float Step : { 5.0f, 2.0f, 1.0f })
	{
		if (Step * Magnitude <= MaxMetres)
		{
			return Step * Magnitude * 100.0f;
		}
	}
	return Magnitude * 100.0f;
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
	bDragging = false;
	RefreshMapSnapshot();
	FitViewToSettlement();
	// The town may have changed since the map was last open; always capture afresh.
	CapturedSpan = 0.0f;
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

void UWorldDirectorMapWidget::NativeDestruct()
{
	ReleaseAerialCapture();
	Super::NativeDestruct();
}

void UWorldDirectorMapWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	UpdateAerialCapture(MyGeometry.GetLocalSize());
	Invalidate(EInvalidateWidget::Paint);
}

bool UWorldDirectorMapWidget::ApplyViewportLayout()
{
	const FVector2D ViewportSize = UWidgetLayoutLibrary::GetViewportSize(this);
	if (ViewportSize.X <= 0.0f || ViewportSize.Y <= 0.0f)
	{
		return false;
	}
	// Let the viewport slot own the size. Supplying GetViewportSize() as an
	// explicit desired size can be stale in PIE/editor viewports, leaving a
	// shorter widget than the visible game area. That makes both the map frame
	// and AbsoluteToLocal mouse coordinates appear offset from the cursor.
	SetAnchorsInViewport(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
	SetAlignmentInViewport(FVector2D::ZeroVector);
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

void UWorldDirectorMapWidget::GetContentRect(
	const FVector2D& WidgetSize, FVector2D& OutMin, FVector2D& OutMax) const
{
	GetMapRect(WidgetSize, OutMin, OutMax);
	OutMin += FVector2D(2.0f);
	OutMax -= FVector2D(2.0f);
}

float UWorldDirectorMapWidget::MapScale(const FVector2D& WidgetSize) const
{
	FVector2D ContentMin;
	FVector2D ContentMax;
	GetContentRect(WidgetSize, ContentMin, ContentMax);
	const FVector2D ContentSize = ContentMax - ContentMin;
	return FMath::Min(ContentSize.X, ContentSize.Y) / FMath::Max(1.0f, ViewSpan);
}

void UWorldDirectorMapWidget::FitViewToSettlement()
{
	// The town as built: buildings, presentation features, the perimeter and the
	// justice pieces. Trees and field crops would widen this to the whole terrain, and
	// piers can sit a few hundred metres off on the nearest shore, so they are left for
	// zooming out (wheel, or T for the whole terrain).
	FBox2D Bounds(ForceInit);
	for (const FResolvedLocationPlan& Location : MapPlan.Locations)
	{
		Bounds += FVector2D(Location.Transform.GetLocation());
	}
	for (const FWorldDirectorFeatureInstance& Feature : MapPlan.Features)
	{
		Bounds += FVector2D(Feature.Transform.GetLocation());
	}
	for (const FWorldDirectorDressingInstance& Dressing : MapPlan.Dressing)
	{
		const FString Tag = Dressing.BiomeTag.ToString();
		if (Tag.StartsWith(TEXT("Biome.Fortification")) || Tag == TEXT("Biome.Justice"))
		{
			Bounds += FVector2D(Dressing.Transform.GetLocation());
		}
	}
	if (!Bounds.bIsValid)
	{
		FitViewToTerrain();
		return;
	}
	ViewCenter = Bounds.GetCenter();
	// Location points are building centres; the margin keeps whole shells, walls and
	// labels inside the frame.
	ViewSpan = Bounds.GetSize().GetMax() + 5000.0f;
	ClampView();
}

void UWorldDirectorMapWidget::FitViewToTerrain()
{
	ViewCenter = FVector2D::ZeroVector;
	ViewSpan = static_cast<float>(MapPlan.Terrain.ExtentCentimeters) * 2.0f;
	ClampView();
}

void UWorldDirectorMapWidget::ClampView()
{
	const float Extent = FMath::Max(1000.0f, static_cast<float>(MapPlan.Terrain.ExtentCentimeters));
	ViewSpan = FMath::Clamp(ViewSpan, MinimumViewSpan, Extent * 2.2f);
	ViewCenter.X = FMath::Clamp(ViewCenter.X, -Extent, Extent);
	ViewCenter.Y = FMath::Clamp(ViewCenter.Y, -Extent, Extent);
}

void UWorldDirectorMapWidget::UpdateAerialCapture(const FVector2D& WidgetSize)
{
	UWorld* World = GetWorld();
	if (!bShowAerial || Town == nullptr || World == nullptr)
	{
		return;
	}
	FVector2D ContentMin;
	FVector2D ContentMax;
	GetContentRect(WidgetSize, ContentMin, ContentMax);
	const FVector2D ContentSize = ContentMax - ContentMin;
	if (ContentSize.X < 32.0f || ContentSize.Y < 32.0f || ViewSpan <= 0.0f)
	{
		return;
	}
	// Capture at the frame's pixel size so the image stays sharp on high-DPI screens.
	const float PixelsPerUnit = FMath::Max(1.0f, UWidgetLayoutLibrary::GetViewportScale(this));
	const float Resolution = FMath::Min(PixelsPerUnit, MaxAerialResolution / ContentSize.GetMax());
	const FIntPoint TargetSize(
		FMath::Max(64, FMath::RoundToInt(ContentSize.X * Resolution)),
		FMath::Max(64, FMath::RoundToInt(ContentSize.Y * Resolution)));

	if (AerialTarget == nullptr)
	{
		AerialTarget = NewObject<UTextureRenderTarget2D>(this);
		AerialTarget->RenderTargetFormat = RTF_RGBA8;
		AerialTarget->ClearColor = FLinearColor(0.02f, 0.03f, 0.03f, 1.0f);
		AerialTarget->InitAutoFormat(TargetSize.X, TargetSize.Y);
		AerialBrush.SetResourceObject(AerialTarget);
		AerialBrush.DrawAs = ESlateBrushDrawType::Image;
	}
	else if (AerialTarget->SizeX != TargetSize.X || AerialTarget->SizeY != TargetSize.Y)
	{
		AerialTarget->ResizeTarget(TargetSize.X, TargetSize.Y);
	}
	AerialBrush.ImageSize = FVector2D(TargetSize);

	FBox TerrainBounds(FVector(-60000.0f, -60000.0f, -5000.0f), FVector(60000.0f, 60000.0f, 10000.0f));
	if (Town->TerrainMesh != nullptr && Town->TerrainMesh->Bounds.GetBox().IsValid)
	{
		TerrainBounds = Town->TerrainMesh->Bounds.GetBox();
	}
	if (AerialCapture == nullptr)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParameters.ObjectFlags |= RF_Transient;
		AerialCapture = World->SpawnActor<ASceneCapture2D>(
			ASceneCapture2D::StaticClass(), FTransform::Identity, SpawnParameters);
		if (AerialCapture == nullptr)
		{
			bShowAerial = false;
			return;
		}
		USceneCaptureComponent2D* Capture = AerialCapture->GetCaptureComponent2D();
		Capture->bCaptureEveryFrame = false;
		Capture->bCaptureOnMovement = false;
		Capture->bExcludeFromSceneTextureExtents = true;
		Capture->ProjectionType = ECameraProjectionMode::Orthographic;
		Capture->bAutoCalculateOrthoPlanes = false;
		Capture->bUpdateOrthoPlanes = false;
		// Unlit surface colour reads the same at night, in fog and in storms, so the
		// map shows the layout whatever atmosphere the world chose.
		Capture->CaptureSource = ESceneCaptureSource::SCS_BaseColor;
		Capture->ShowFlags.SetFog(false);
		Capture->ShowFlags.SetVolumetricFog(false);
		Capture->ShowFlags.SetAtmosphere(false);
		Capture->ShowFlags.SetCloud(false);
		Capture->TextureTarget = AerialTarget;
	}
	if (CapturedSize == TargetSize && CapturedSpan > 0.0f &&
		CapturedCenter.Equals(ViewCenter, 1.0f) && FMath::IsNearlyEqual(CapturedSpan, ViewSpan, 1.0f))
	{
		return;
	}

	USceneCaptureComponent2D* Capture = AerialCapture->GetCaptureComponent2D();
	// Straight down with yaw 0 puts world +X at the top of the image and +Y on the right,
	// the same axes WorldToMap uses.
	const float CameraHeight = TerrainBounds.Max.Z + 6000.0f;
	AerialCapture->SetActorLocationAndRotation(
		FVector(ViewCenter.X, ViewCenter.Y, CameraHeight), FRotator(-90.0f, 0.0f, 0.0f));
	// Orthographic depth is linear; the engine's default far plane would leave roofs
	// and ground metres apart in depth precision.
	Capture->MaxViewDistanceOverride = CameraHeight - TerrainBounds.Min.Z + 6000.0f;
	Capture->OrthoWidth = ContentSize.X / MapScale(WidgetSize);
	Capture->CaptureScene();
	CapturedSize = TargetSize;
	CapturedCenter = ViewCenter;
	CapturedSpan = ViewSpan;
}

void UWorldDirectorMapWidget::ReleaseAerialCapture()
{
	if (AerialCapture != nullptr)
	{
		AerialCapture->Destroy();
		AerialCapture = nullptr;
	}
	CapturedSpan = 0.0f;
}

FVector2D UWorldDirectorMapWidget::WorldToMap(
	const FVector2D& WorldPosition, const FVector2D& WidgetSize) const
{
	FVector2D ContentMin;
	FVector2D ContentMax;
	GetContentRect(WidgetSize, ContentMin, ContentMax);
	const FVector2D ContentCenter = (ContentMin + ContentMax) * 0.5f;
	const float Scale = MapScale(WidgetSize);
	// North (+X) is up and +Y is right: a camera looking straight down, not a mirror.
	return FVector2D(
		ContentCenter.X + (WorldPosition.Y - ViewCenter.Y) * Scale,
		ContentCenter.Y - (WorldPosition.X - ViewCenter.X) * Scale);
}

FVector2D UWorldDirectorMapWidget::MapToWorld(
	const FVector2D& MapPosition, const FVector2D& WidgetSize) const
{
	FVector2D ContentMin;
	FVector2D ContentMax;
	GetContentRect(WidgetSize, ContentMin, ContentMax);
	const FVector2D ContentCenter = (ContentMin + ContentMax) * 0.5f;
	const float Scale = FMath::Max(KINDA_SMALL_NUMBER, MapScale(WidgetSize));
	return FVector2D(
		ViewCenter.X - (MapPosition.Y - ContentCenter.Y) / Scale,
		ViewCenter.Y + (MapPosition.X - ContentCenter.X) / Scale);
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
	const FVector2D Position = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
	const FVector2D WidgetSize = InGeometry.GetLocalSize();
	if (bDragging)
	{
		const FVector2D Delta = Position - DragStartPosition;
		bDragMoved = bDragMoved || Delta.Size() > DragThreshold;
		if (bDragMoved)
		{
			// Keep the ground under the cursor: screen right is +Y, screen down is -X.
			const float Scale = FMath::Max(KINDA_SMALL_NUMBER, MapScale(WidgetSize));
			ViewCenter = FVector2D(
				DragStartCenter.X + Delta.Y / Scale,
				DragStartCenter.Y - Delta.X / Scale);
			ClampView();
		}
	}
	UpdateHoveredFeature(Position, WidgetSize);
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
	bDragging = true;
	bDragMoved = false;
	DragStartPosition = Position;
	DragStartCenter = ViewCenter;
	return FReply::Handled()
		.CaptureMouse(TakeWidget())
		.SetUserFocus(TakeWidget(), EFocusCause::Mouse);
}

FReply UWorldDirectorMapWidget::NativeOnMouseButtonUp(
	const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !bDragging)
	{
		return FReply::Handled();
	}
	bDragging = false;
	if (!bDragMoved)
	{
		// A click without a drag places the waypoint, snapping to a hovered location.
		const FVector2D Position =
			InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		const FVector2D WidgetSize = InGeometry.GetLocalSize();
		UpdateHoveredFeature(Position, WidgetSize);
		const FResolvedLocationPlan* Hovered = HoveredLocationId.IsEmpty()
			? nullptr
			: MapPlan.Locations.FindByPredicate(
				[this](const FResolvedLocationPlan& Location)
				{
					return Location.LocationId == HoveredLocationId;
				});
		SetWaypoint(
			Hovered != nullptr ? FVector2D(Hovered->Transform.GetLocation()) : MapToWorld(Position, WidgetSize),
			Hovered != nullptr ? HoveredLocationId : FString());
	}
	Invalidate(EInvalidateWidget::Paint);
	return FReply::Handled().ReleaseMouseCapture();
}

FReply UWorldDirectorMapWidget::NativeOnMouseWheel(
	const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	const FVector2D Position = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
	const FVector2D WidgetSize = InGeometry.GetLocalSize();
	if (!IsMapPosition(Position, WidgetSize) || FMath::IsNearlyZero(InMouseEvent.GetWheelDelta()))
	{
		return FReply::Handled();
	}
	// Zoom about the cursor: the ground under it stays put.
	const FVector2D Anchor = MapToWorld(Position, WidgetSize);
	ViewSpan *= InMouseEvent.GetWheelDelta() > 0.0f ? 0.8f : 1.25f;
	ClampView();
	const FVector2D Drift = MapToWorld(Position, WidgetSize) - Anchor;
	ViewCenter -= Drift;
	ClampView();
	Invalidate(EInvalidateWidget::Paint);
	return FReply::Handled();
}

FReply UWorldDirectorMapWidget::NativeOnKeyDown(
	const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape || Key == EKeys::M)
	{
		RequestClose();
		return FReply::Handled();
	}
	if (Key == EKeys::F)
	{
		FitViewToSettlement();
		return FReply::Handled();
	}
	if (Key == EKeys::T)
	{
		FitViewToTerrain();
		return FReply::Handled();
	}
	if (Key == EKeys::V)
	{
		bShowAerial = !bShowAerial;
		CapturedSpan = 0.0f;
		return FReply::Handled();
	}
	if (Key == EKeys::L)
	{
		bShowAllLabels = !bShowAllLabels;
		return FReply::Handled();
	}
	if (Key == EKeys::Equals || Key == EKeys::Add || Key == EKeys::Hyphen || Key == EKeys::Subtract)
	{
		ViewSpan *= (Key == EKeys::Equals || Key == EKeys::Add) ? 0.8f : 1.25f;
		ClampView();
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
	FVector2D ContentMin;
	FVector2D ContentMax;
	GetContentRect(Size, ContentMin, ContentMax);
	const float Scale = MapScale(Size);
	const FVector2D SidebarMin(
		FMath::Max(Size.X - MapSidebarWidth, MapMax.X + MapOuterMargin * 0.5f), MapHeaderHeight);
	const FVector2D SidebarMax(Size.X - MapOuterMargin, Size.Y - MapFooterHeight);
	const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 25.0f);
	const FSlateFontInfo SectionFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 12.0f);
	const FSlateFontInfo BodyFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 13.0f);
	const FSlateFontInfo SmallFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 11.0f);
	const FSlateFontInfo LabelFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 10.0f);
	const FLinearColor Ink(0.88f, 0.91f, 0.90f, 1.0f);
	const FLinearColor Muted(0.55f, 0.63f, 0.63f, 1.0f);
	const FLinearColor Gold(0.96f, 0.72f, 0.30f, 1.0f);
	const bool bAerial = bShowAerial && AerialTarget != nullptr && CapturedSpan > 0.0f;

	DrawBox(OutDrawElements, CurrentLayer++, AllottedGeometry, FVector2D::ZeroVector, Size,
		FLinearColor(0.012f, 0.018f, 0.021f, 0.98f));
	DrawText(OutDrawElements, CurrentLayer++, AllottedGeometry, FVector2D(MapOuterMargin, 24.0f),
		FVector2D(700.0f, 34.0f), TEXT("SETTLEMENT MAP"), TitleFont, Gold);
	const FString ViewName = bShowAerial ? TEXT("TOP-DOWN VIEW") : TEXT("SCHEMATIC");
	DrawText(OutDrawElements, CurrentLayer++, AllottedGeometry, FVector2D(MapOuterMargin + 290.0f, 31.0f),
		FVector2D(900.0f, 22.0f),
		MapSpec.Brief.SettlementIdentity.IsEmpty()
			? ViewName + TEXT(" / NORTH UP")
			: MapSpec.Brief.SettlementIdentity + TEXT("  /  ") + ViewName + TEXT(" / NORTH UP"),
		BodyFont, Muted);
	DrawText(OutDrawElements, CurrentLayer++, AllottedGeometry, FVector2D(Size.X - 190.0f, 33.0f),
		FVector2D(160.0f, 22.0f), TEXT("M / ESC  CLOSE"), SmallFont, Ink);

	DrawBox(OutDrawElements, CurrentLayer++, AllottedGeometry, MapMin, MapMax - MapMin,
		FLinearColor(0.045f, 0.09f, 0.085f, 1.0f));
	DrawBox(OutDrawElements, CurrentLayer++, AllottedGeometry, SidebarMin, SidebarMax - SidebarMin,
		FLinearColor(0.035f, 0.043f, 0.045f, 1.0f));

	// Everything world-anchored is clipped to the map frame so a zoomed-in view does not
	// spill roads and labels over the header and sidebar.
	OutDrawElements.PushClip(FSlateClippingZone(AllottedGeometry.MakeChild(
		UE::Slate::CastToVector2f(ContentMax - ContentMin),
		FSlateLayoutTransform(UE::Slate::CastToVector2f(ContentMin)))));

	if (bAerial)
	{
		// The capture can lag the view by a frame while panning; draw it where it was taken.
		const FVector2D CaptureCenter = WorldToMap(CapturedCenter, Size);
		const float CaptureScale = CapturedSpan / FMath::Max(1.0f, ViewSpan);
		const FVector2D CaptureSize = (ContentMax - ContentMin) * CaptureScale;
		FSlateDrawElement::MakeBox(
			OutDrawElements, CurrentLayer++,
			AllottedGeometry.ToPaintGeometry(
				UE::Slate::CastToVector2f(CaptureSize),
				FSlateLayoutTransform(UE::Slate::CastToVector2f(CaptureCenter - CaptureSize * 0.5f))),
			// Scene captures leave alpha at zero; the colour channels are the picture.
			&AerialBrush, ESlateDrawEffect::IgnoreTextureAlpha, FLinearColor::White);
	}
	else
	{
		const FLinearColor GridColor(0.18f, 0.31f, 0.28f, 0.55f);
		for (int32 GridIndex = 1; GridIndex < 5; ++GridIndex)
		{
			const float X = FMath::Lerp(ContentMin.X, ContentMax.X, GridIndex / 5.0f);
			const float Y = FMath::Lerp(ContentMin.Y, ContentMax.Y, GridIndex / 5.0f);
			DrawLine(OutDrawElements, CurrentLayer, AllottedGeometry,
				{ FVector2D(X, ContentMin.Y), FVector2D(X, ContentMax.Y) }, GridColor, 1.0f);
			DrawLine(OutDrawElements, CurrentLayer++, AllottedGeometry,
				{ FVector2D(ContentMin.X, Y), FVector2D(ContentMax.X, Y) }, GridColor, 1.0f);
		}
	}

	// Water and cultivated parcels are subdued context layers underneath roads. Water is
	// translucent, so the unlit capture shows the bed; the line marks where it runs.
	if (MapPlan.Terrain.WaterControlPoints.Num() > 1)
	{
		TArray<FVector2D> Water;
		for (const FVector& Point : MapPlan.Terrain.WaterControlPoints)
		{
			Water.Add(WorldToMap(FVector2D(Point), Size));
		}
		if (!bAerial)
		{
			DrawLine(OutDrawElements, CurrentLayer++, AllottedGeometry, Water,
				FLinearColor(0.22f, 0.54f, 0.72f, 0.8f), 8.0f);
		}
		DrawLine(OutDrawElements, CurrentLayer++, AllottedGeometry, Water,
			FLinearColor(0.42f, 0.76f, 0.86f, bAerial ? 0.6f : 0.85f), bAerial ? 2.0f : 1.5f);
	}
	for (const FWorldDirectorFarmParcel& Parcel : MapPlan.Terrain.FarmParcels)
	{
		TArray<FVector2D> Boundary;
		for (const FVector2D& Point : Parcel.BoundaryPoints)
		{
			Boundary.Add(WorldToMap(Point, Size));
		}
		DrawLine(OutDrawElements, CurrentLayer++, AllottedGeometry, Boundary,
			FLinearColor(0.67f, 0.61f, 0.35f, bAerial ? 0.35f : 0.5f), 1.0f, true);
	}
	for (const FWorldDirectorDistrictAnchor& District : MapPlan.DistrictAnchors)
	{
		const FVector2D Center = WorldToMap(FVector2D(District.Position), Size);
		const float Radius = FMath::Max(18.0f, District.InfluenceRadiusCentimeters * Scale);
		DrawLine(OutDrawElements, CurrentLayer++, AllottedGeometry, MakeCircle(Center, Radius, 48),
			bAerial ? FLinearColor(0.95f, 0.92f, 0.70f, 0.35f) : FLinearColor(0.38f, 0.53f, 0.40f, 0.25f),
			bAerial ? 1.5f : 1.0f);
		DrawText(OutDrawElements, CurrentLayer++, AllottedGeometry, Center + FVector2D(-Radius * 0.7f, -Radius * 0.7f),
			FVector2D(220.0f, 18.0f), District.DistrictId.ToUpper(), SmallFont,
			bAerial ? FLinearColor(0.98f, 0.95f, 0.78f, 0.75f) : FLinearColor(0.54f, 0.67f, 0.58f, 0.7f));
	}

	for (const FResolvedRoutePlan& Route : MapPlan.Routes)
	{
		TArray<FVector2D> RoutePoints;
		for (const FVector& Point : Route.ControlPoints)
		{
			RoutePoints.Add(WorldToMap(FVector2D(Point), Size));
		}
		const bool bRoad = Route.RouteType == TEXT("Road");
		// Over the capture the real roads are visible; a thin line just traces them.
		DrawLine(OutDrawElements, CurrentLayer++, AllottedGeometry, RoutePoints,
			bRoad
				? FLinearColor(0.87f, 0.73f, 0.45f, bAerial ? 0.45f : 0.92f)
				: FLinearColor(0.68f, 0.69f, 0.55f, bAerial ? 0.35f : 0.78f),
			bAerial ? 1.5f : (bRoad ? 4.0f : 2.0f));
	}

	for (const FResolvedLocationPlan& Location : MapPlan.Locations)
	{
		const FVector2D Marker = WorldToMap(FVector2D(Location.Transform.GetLocation()), Size);
		const FLinearColor Color = MarkerColor(Location.LocationId);
		const bool bHovered = Location.LocationId == HoveredLocationId;
		const bool bLandmark = Location.LocationId == MapPlan.LandmarkLocationId;
		// Over the capture the building itself is visible, so the marker shrinks to a pin.
		const float Radius = bHovered ? 8.0f : (bAerial ? 4.0f : 6.0f);
		DrawBox(OutDrawElements, CurrentLayer++, AllottedGeometry,
			Marker - FVector2D(Radius), FVector2D(Radius * 2.0f), Color);
		DrawLine(OutDrawElements, CurrentLayer++, AllottedGeometry,
			MakeCircle(Marker, Radius + (bHovered ? 5.0f : 2.0f)),
			bHovered ? Ink : (bAerial ? FLinearColor(0.0f, 0.0f, 0.0f, 0.8f) : Color),
			bHovered ? 2.0f : 1.0f);
		if (bHovered || bLandmark || bShowAllLabels)
		{
			const FString Label = DisplayNameForLocation(Location.LocationId);
			const FVector2D LabelPosition = Marker + FVector2D(9.0f, -8.0f);
			const FVector2D LabelSize(FMath::Min(220.0f, 6.4f * Label.Len() + 8.0f), 16.0f);
			DrawBox(OutDrawElements, CurrentLayer++, AllottedGeometry, LabelPosition, LabelSize,
				FLinearColor(0.01f, 0.015f, 0.018f, bAerial ? 0.72f : 0.5f));
			DrawText(OutDrawElements, CurrentLayer++, AllottedGeometry, LabelPosition + FVector2D(4.0f, 1.0f),
				FVector2D(LabelSize.X, 16.0f), Label, LabelFont, bHovered ? Ink : Color);
		}
	}

	if (bHasWaypoint)
	{
		const FVector2D Waypoint = WorldToMap(WaypointWorldPosition, Size);
		DrawLine(OutDrawElements, CurrentLayer++, AllottedGeometry, MakeCircle(Waypoint, 12.0f),
			FLinearColor(0.93f, 0.38f, 0.74f, 1.0f), 2.0f);
		DrawLine(OutDrawElements, CurrentLayer++, AllottedGeometry,
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
			const FLinearColor PlayerColor(0.98f, 0.94f, 0.76f, 1.0f);
			const FVector2D Player = WorldToMap(FVector2D(Pawn->GetActorLocation()), Size);
			DrawLine(OutDrawElements, CurrentLayer++, AllottedGeometry, MakeCircle(Player, 9.0f),
				PlayerColor, 2.0f);
			DrawBox(OutDrawElements, CurrentLayer++, AllottedGeometry,
				Player - FVector2D(3.0f), FVector2D(6.0f), PlayerColor);
			// Facing: yaw 0 looks along +X (up on the map), yaw 90 along +Y (right).
			const float Yaw = FMath::DegreesToRadians(PlayerController->GetControlRotation().Yaw);
			const FVector2D Facing(FMath::Sin(Yaw), -FMath::Cos(Yaw));
			DrawLine(OutDrawElements, CurrentLayer++, AllottedGeometry,
				{ Player + Facing * 9.0f, Player + Facing * 22.0f }, PlayerColor, 2.5f);
		}
	}

	// Scale bar, bottom left of the frame.
	{
		const float BarCentimetres = NiceScaleBarCentimetres(140.0f / FMath::Max(KINDA_SMALL_NUMBER, Scale));
		const float BarLength = BarCentimetres * Scale;
		const FVector2D BarStart(ContentMin.X + 16.0f, ContentMax.Y - 18.0f);
		DrawBox(OutDrawElements, CurrentLayer++, AllottedGeometry, BarStart - FVector2D(6.0f, 22.0f),
			FVector2D(BarLength + 12.0f, 30.0f), FLinearColor(0.01f, 0.015f, 0.018f, 0.7f));
		DrawLine(OutDrawElements, CurrentLayer++, AllottedGeometry,
			{ BarStart + FVector2D(0.0f, -5.0f), BarStart, BarStart + FVector2D(BarLength, 0.0f),
			  BarStart + FVector2D(BarLength, -5.0f) },
			Ink, 2.0f);
		DrawText(OutDrawElements, CurrentLayer++, AllottedGeometry, BarStart + FVector2D(0.0f, -20.0f),
			FVector2D(120.0f, 16.0f), FString::Printf(TEXT("%.0f m"), BarCentimetres / 100.0f), SmallFont, Ink);
	}

	// North arrow, top left of the frame.
	{
		const FVector2D ArrowBase(ContentMin.X + 22.0f, ContentMin.Y + 44.0f);
		DrawBox(OutDrawElements, CurrentLayer++, AllottedGeometry, ArrowBase - FVector2D(14.0f, 38.0f),
			FVector2D(28.0f, 46.0f), FLinearColor(0.01f, 0.015f, 0.018f, 0.7f));
		DrawLine(OutDrawElements, CurrentLayer++, AllottedGeometry,
			{ ArrowBase, ArrowBase + FVector2D(0.0f, -20.0f), ArrowBase + FVector2D(-5.0f, -14.0f),
			  ArrowBase + FVector2D(0.0f, -20.0f), ArrowBase + FVector2D(5.0f, -14.0f) },
			Ink, 2.0f);
		DrawText(OutDrawElements, CurrentLayer++, AllottedGeometry, ArrowBase + FVector2D(-4.0f, -36.0f),
			FVector2D(20.0f, 16.0f), TEXT("N"), SectionFont, Ink);
	}

	OutDrawElements.PopClip();
	DrawLine(OutDrawElements, CurrentLayer++, AllottedGeometry,
		{ MapMin, FVector2D(MapMax.X, MapMin.Y), MapMax, FVector2D(MapMin.X, MapMax.Y) },
		FLinearColor(0.35f, 0.55f, 0.48f, 0.8f), 1.5f, true);

	const float SidebarTextX = SidebarMin.X + 20.0f;
	const float SidebarTextWidth = SidebarMax.X - SidebarTextX - 16.0f;
	DrawText(OutDrawElements, CurrentLayer++, AllottedGeometry, FVector2D(SidebarTextX, SidebarMin.Y + 22.0f),
		FVector2D(220.0f, 22.0f), TEXT("MAP DETAILS"), SectionFont, Gold);
	DrawText(OutDrawElements, CurrentLayer++, AllottedGeometry, FVector2D(SidebarTextX, SidebarMin.Y + 60.0f),
		FVector2D(220.0f, 18.0f), TEXT("HOVERED FEATURE"), SmallFont, Muted);
	const FString HoverTitle = HoveredLocationId.IsEmpty()
		? TEXT("Move over a location marker") : DisplayNameForLocation(HoveredLocationId);
	DrawText(OutDrawElements, CurrentLayer++, AllottedGeometry, FVector2D(SidebarTextX, SidebarMin.Y + 82.0f),
		FVector2D(SidebarTextWidth, 42.0f), HoverTitle, BodyFont, Ink);
	if (!HoveredLocationId.IsEmpty())
	{
		DrawText(OutDrawElements, CurrentLayer++, AllottedGeometry, FVector2D(SidebarTextX, SidebarMin.Y + 112.0f),
			FVector2D(230.0f, 20.0f), PurposeForLocation(HoveredLocationId), SmallFont, Muted);
	}

	const float WaypointY = SidebarMin.Y + 166.0f;
	DrawText(OutDrawElements, CurrentLayer++, AllottedGeometry, FVector2D(SidebarTextX, WaypointY),
		FVector2D(220.0f, 18.0f), TEXT("WAYPOINT"), SmallFont, Muted);
	const FString WaypointText = !bHasWaypoint
		? TEXT("None set")
		: WaypointLocationId.IsEmpty()
			? FString::Printf(TEXT("Free point  %+.0f, %+.0f"), WaypointWorldPosition.X, WaypointWorldPosition.Y)
			: DisplayNameForLocation(WaypointLocationId);
	DrawText(OutDrawElements, CurrentLayer++, AllottedGeometry, FVector2D(SidebarTextX, WaypointY + 22.0f),
			FVector2D(SidebarTextWidth, 40.0f), WaypointText, BodyFont,
			bHasWaypoint ? FLinearColor(0.98f, 0.66f, 0.86f, 1.0f) : Muted);

	// What the generator laid out, so the picture can be read against the plan.
	const float LayoutY = WaypointY + 84.0f;
	DrawText(OutDrawElements, CurrentLayer++, AllottedGeometry, FVector2D(SidebarTextX, LayoutY),
		FVector2D(220.0f, 18.0f), TEXT("LAYOUT"), SmallFont, Muted);
	const UEnum* ArchetypeEnum = StaticEnum<EWorldDirectorTerrainArchetype>();
	const FString Landform = !MapSpec.Brief.Landform.IsEmpty()
		? MapSpec.Brief.Landform
		: (ArchetypeEnum != nullptr
			? ArchetypeEnum->GetNameStringByValue(static_cast<int64>(MapPlan.Terrain.Archetype)).ToLower()
			: FString());
	int32 Homes = 0;
	for (const FResolvedLocationPlan& Location : MapPlan.Locations)
	{
		Homes += Location.PurposeTag.ToString().Contains(TEXT("Home")) ? 1 : 0;
	}
	const TArray<TPair<FString, FString>> LayoutRows = {
		{ TEXT("Landform"), Landform },
		{ TEXT("Districts"), FString::FromInt(MapPlan.DistrictAnchors.Num()) },
		{ TEXT("Buildings"), FString::Printf(TEXT("%d  (%d homes)"), MapPlan.Locations.Num(), Homes) },
		{ TEXT("Roads"), FString::FromInt(MapPlan.Routes.Num()) },
		{ TEXT("Perimeter"), MapPlan.Presentation.Fortification },
		{ TEXT("Waterfront"), MapPlan.Presentation.WaterLife }
	};
	for (int32 Index = 0; Index < LayoutRows.Num(); ++Index)
	{
		const float Y = LayoutY + 22.0f + Index * 20.0f;
		DrawText(OutDrawElements, CurrentLayer++, AllottedGeometry, FVector2D(SidebarTextX, Y),
			FVector2D(100.0f, 18.0f), LayoutRows[Index].Key, SmallFont, Muted);
		DrawText(OutDrawElements, CurrentLayer++, AllottedGeometry, FVector2D(SidebarTextX + 96.0f, Y),
			FVector2D(SidebarTextWidth - 96.0f, 18.0f), LayoutRows[Index].Value, SmallFont, Ink);
	}

	const float LegendY = SidebarMax.Y - 192.0f;
	DrawText(OutDrawElements, CurrentLayer++, AllottedGeometry, FVector2D(SidebarTextX, LegendY),
		FVector2D(220.0f, 18.0f), TEXT("LEGEND"), SmallFont, Muted);
	const TArray<TPair<FString, FLinearColor>> Legend = {
		{ TEXT("Landmark"), FLinearColor(0.98f, 0.73f, 0.22f, 1.0f) },
		{ TEXT("Civic"), FLinearColor(0.86f, 0.55f, 0.38f, 1.0f) },
		{ TEXT("Home"), FLinearColor(0.43f, 0.72f, 0.86f, 1.0f) },
		{ TEXT("Workplace"), FLinearColor(0.46f, 0.82f, 0.65f, 1.0f) },
		{ TEXT("Waypoint"), FLinearColor(0.93f, 0.38f, 0.74f, 1.0f) },
		{ TEXT("You (line shows facing)"), FLinearColor(0.98f, 0.94f, 0.76f, 1.0f) }
	};
	for (int32 Index = 0; Index < Legend.Num(); ++Index)
	{
		const float Y = LegendY + 25.0f + Index * 23.0f;
		DrawBox(OutDrawElements, CurrentLayer++, AllottedGeometry,
			FVector2D(SidebarTextX, Y + 3.0f), FVector2D(10.0f), Legend[Index].Value);
		DrawText(OutDrawElements, CurrentLayer++, AllottedGeometry, FVector2D(SidebarTextX + 20.0f, Y),
			FVector2D(220.0f, 18.0f), Legend[Index].Key, SmallFont, Ink);
	}

	DrawText(OutDrawElements, CurrentLayer++, AllottedGeometry, FVector2D(MapOuterMargin, Size.Y - 28.0f),
		FVector2D(Size.X - MapOuterMargin * 2.0f, 20.0f),
		TEXT("Drag  pan     Wheel / + -  zoom     Click  waypoint     RMB  clear     F  fit town     T  whole terrain     V  top-down / schematic     L  labels"),
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
		DrawBox(OutDrawElements, CurrentLayer++, AllottedGeometry, TooltipPosition, TooltipSize,
			FLinearColor(0.02f, 0.03f, 0.03f, 0.96f));
		DrawText(OutDrawElements, CurrentLayer++, AllottedGeometry, TooltipPosition + FVector2D(10.0f, 8.0f),
			FVector2D(210.0f, 18.0f), DisplayNameForLocation(HoveredLocationId), BodyFont, Ink);
		DrawText(OutDrawElements, CurrentLayer++, AllottedGeometry, TooltipPosition + FVector2D(10.0f, 30.0f),
			FVector2D(210.0f, 16.0f), PurposeForLocation(HoveredLocationId), SmallFont, Gold);
	}

	return CurrentLayer;
}
