#include "WorldDirectorPhysicalGenerator.h"

#include "IPlatformCrypto.h"
#include "Math/RandomStream.h"
#include "WorldEnvironmentProfile.h"

namespace
{
constexpr int32 TerrainResolution = 65;
constexpr int32 TerrainExtent = 14000;
constexpr int32 TerrainDiameter = TerrainExtent * 2;
constexpr int32 PlotAttempts = 1200;

void AppendInt32(TArray<uint8>& Bytes, const int32 Value)
{
	const uint32 Wire = static_cast<uint32>(Value);
	Bytes.Add(static_cast<uint8>(Wire));
	Bytes.Add(static_cast<uint8>(Wire >> 8));
	Bytes.Add(static_cast<uint8>(Wire >> 16));
	Bytes.Add(static_cast<uint8>(Wire >> 24));
}

void AppendString(TArray<uint8>& Bytes, const FString& Value)
{
	FTCHARToUTF8 Utf8(*Value);
	AppendInt32(Bytes, Utf8.Length());
	Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
}

uint32 Mix32(uint32 Value)
{
	Value ^= Value >> 16;
	Value *= 0x7feb352dU;
	Value ^= Value >> 15;
	Value *= 0x846ca68bU;
	Value ^= Value >> 16;
	return Value;
}

bool CalculateSha256(const TArray<uint8>& Bytes, TArray<uint8>& OutHash)
{
	TUniquePtr<FEncryptionContext> Context = IPlatformCrypto::Get().CreateContext();
	return Context.IsValid() && Context->CalcSHA256(MakeArrayView(Bytes), OutHash) && OutHash.Num() == 32;
}

float HashNoise(const int32 X, const int32 Y, const int32 Seed)
{
	const uint32 Hash = Mix32(
		static_cast<uint32>(X) * 0x9e3779b9U ^
		static_cast<uint32>(Y) * 0x85ebca6bU ^
		static_cast<uint32>(Seed));
	return static_cast<float>(Hash & 0xffffU) / 32767.5f - 1.0f;
}

float SmoothNoise(const float X, const float Y, const int32 Seed)
{
	const int32 X0 = FMath::FloorToInt(X);
	const int32 Y0 = FMath::FloorToInt(Y);
	const float TX = X - X0;
	const float TY = Y - Y0;
	const float SX = TX * TX * (3.0f - 2.0f * TX);
	const float SY = TY * TY * (3.0f - 2.0f * TY);
	const float A = FMath::Lerp(HashNoise(X0, Y0, Seed), HashNoise(X0 + 1, Y0, Seed), SX);
	const float B = FMath::Lerp(HashNoise(X0, Y0 + 1, Seed), HashNoise(X0 + 1, Y0 + 1, Seed), SX);
	return FMath::Lerp(A, B, SY);
}

float FractalNoise(const float X, const float Y, const int32 Seed)
{
	return SmoothNoise(X * 2.1f, Y * 2.1f, Seed) * 0.58f
		+ SmoothNoise(X * 5.3f, Y * 5.3f, Seed + 17) * 0.28f
		+ SmoothNoise(X * 11.7f, Y * 11.7f, Seed + 43) * 0.14f;
}

EWorldDirectorTerrainArchetype ResolveArchetype(const FWorldBrief& Brief, const int32 Seed)
{
	FString Intent = FString::Join(Brief.TerrainPreferences, TEXT(" ")).ToLower();
	if (Intent.Contains(TEXT("coast")) || Intent.Contains(TEXT("shore")) || Intent.Contains(TEXT("harbor")))
	{
		return EWorldDirectorTerrainArchetype::Coast;
	}
	if (Intent.Contains(TEXT("marsh")) || Intent.Contains(TEXT("reed")) || Intent.Contains(TEXT("flood")))
	{
		return EWorldDirectorTerrainArchetype::Marsh;
	}
	if (Intent.Contains(TEXT("ridge")) || Intent.Contains(TEXT("hill")) || Intent.Contains(TEXT("mountain")))
	{
		return EWorldDirectorTerrainArchetype::Ridge;
	}
	if (Intent.Contains(TEXT("valley")) || Intent.Contains(TEXT("pass")) || Intent.Contains(TEXT("terrace")))
	{
		return EWorldDirectorTerrainArchetype::Valley;
	}
	if (Intent.Contains(TEXT("basin")) || Intent.Contains(TEXT("bowl")))
	{
		return EWorldDirectorTerrainArchetype::Basin;
	}
	const uint32 Magnitude = Seed < 0
		? 0U - static_cast<uint32>(Seed) : static_cast<uint32>(Seed);
	return static_cast<EWorldDirectorTerrainArchetype>(Magnitude % 5U);
}

int32 GenerateHeight(
	const EWorldDirectorTerrainArchetype Archetype,
	const float NX,
	const float NY,
	const int32 TerrainSeed)
{
	const float Angle = static_cast<float>(FMath::Abs(TerrainSeed % 6283)) / 1000.0f;
	const float RX = NX * FMath::Cos(Angle) - NY * FMath::Sin(Angle);
	const float RY = NX * FMath::Sin(Angle) + NY * FMath::Cos(Angle);
	const float Noise = FractalNoise(NX, NY, TerrainSeed);
	float Height = 0.0f;
	switch (Archetype)
	{
	case EWorldDirectorTerrainArchetype::Valley:
		Height = 120.0f + 1050.0f * FMath::Pow(FMath::Abs(RX + 0.16f * FMath::Sin(RY * 5.0f)), 1.7f)
			+ Noise * 170.0f + RY * 130.0f;
		break;
	case EWorldDirectorTerrainArchetype::Ridge:
		Height = 260.0f + 1120.0f * FMath::Exp(-FMath::Square(RX + Noise * 0.13f) * 5.0f)
			+ 330.0f * RY + Noise * 220.0f;
		break;
	case EWorldDirectorTerrainArchetype::Coast:
		Height = -180.0f + (RX + 0.34f) * 920.0f + Noise * 190.0f
			+ 130.0f * FMath::Sin(RY * 4.0f + Noise);
		break;
	case EWorldDirectorTerrainArchetype::Marsh:
		// Broad raised hummocks keep the wetland buildable while the secondary
		// frequencies preserve channels and seed-specific islands.
		Height = 88.0f + Noise * 135.0f
			+ 82.0f * FMath::Sin(RX * 4.0f + Noise) * FMath::Sin(RY * 3.0f - Noise)
			+ 34.0f * FMath::Sin(RX * 11.0f) * FMath::Sin(RY * 9.0f);
		break;
	case EWorldDirectorTerrainArchetype::Basin:
	default:
		Height = 80.0f + 920.0f * FMath::Pow(FMath::Clamp(FVector2D(NX, NY).Size() - 0.25f, 0.0f, 1.0f), 1.8f)
			+ Noise * 180.0f;
		break;
	}
	return FMath::RoundToInt(Height);
}

bool OverlapsAny(
	const FVector2D& Center,
	const FVector2D& Footprint,
	const float YawDegrees,
	const TArray<FBox2D>& Occupied)
{
	const float Radians = FMath::DegreesToRadians(YawDegrees);
	const float AbsCos = FMath::Abs(FMath::Cos(Radians));
	const float AbsSin = FMath::Abs(FMath::Sin(Radians));
	const FVector2D Half = Footprint * 0.5f;
	const FVector2D Extent(
		AbsCos * Half.X + AbsSin * Half.Y + 340.0f,
		AbsSin * Half.X + AbsCos * Half.Y + 340.0f);
	const FBox2D Candidate(Center - Extent, Center + Extent);
	for (const FBox2D& Other : Occupied)
	{
		if (Candidate.Intersect(Other))
		{
			return true;
		}
	}
	return false;
}

float DistanceToSegment(const FVector2D& Point, const FVector2D& A, const FVector2D& B)
{
	const FVector2D Delta = B - A;
	const float Denominator = Delta.SizeSquared();
	if (Denominator <= KINDA_SMALL_NUMBER)
	{
		return FVector2D::Distance(Point, A);
	}
	const float T = FMath::Clamp(FVector2D::DotProduct(Point - A, Delta) / Denominator, 0.0f, 1.0f);
	return FVector2D::Distance(Point, A + Delta * T);
}

FString ArchetypeName(const EWorldDirectorTerrainArchetype Archetype)
{
	return StaticEnum<EWorldDirectorTerrainArchetype>()->GetNameStringByValue(static_cast<int64>(Archetype));
}
}

FString FWorldDirectorPhysicalGenerator::FingerprintBytes(const TArray<uint8>& Bytes)
{
	TArray<uint8> Hash;
	if (!CalculateSha256(Bytes, Hash))
	{
		return FString();
	}
	return BytesToHex(Hash.GetData(), Hash.Num()).ToLower();
}

int32 FWorldDirectorPhysicalGenerator::DeriveStageSeed(
	const int32 RootSeed,
	const FString& StageName,
	const int32 Index)
{
	TArray<uint8> Bytes;
	AppendString(Bytes, TEXT("WorldDirector.StageSeed.v2"));
	AppendInt32(Bytes, RootSeed);
	AppendString(Bytes, StageName);
	AppendInt32(Bytes, Index);
	TArray<uint8> Hash;
	if (!CalculateSha256(Bytes, Hash))
	{
		return 0;
	}
	uint32 Value = 0;
	for (int32 ByteIndex = 0; ByteIndex < 4; ++ByteIndex)
	{
		Value |= static_cast<uint32>(Hash[ByteIndex]) << (ByteIndex * 8);
	}
	return static_cast<int32>(Value & 0x7fffffffU);
}

int32 FWorldDirectorPhysicalGenerator::SampleHeightCentimeters(
	const FWorldDirectorTerrainRecipe& Terrain,
	const FVector2D& Position)
{
	if (Terrain.Resolution < 2 || Terrain.HeightsCentimeters.Num() != Terrain.Resolution * Terrain.Resolution)
	{
		return 0;
	}
	const float Grid = static_cast<float>(Terrain.Resolution - 1);
	const float GX = FMath::Clamp((Position.X + Terrain.ExtentCentimeters) / (2.0f * Terrain.ExtentCentimeters) * Grid, 0.0f, Grid);
	const float GY = FMath::Clamp((Position.Y + Terrain.ExtentCentimeters) / (2.0f * Terrain.ExtentCentimeters) * Grid, 0.0f, Grid);
	const int32 X0 = FMath::FloorToInt(GX);
	const int32 Y0 = FMath::FloorToInt(GY);
	const int32 X1 = FMath::Min(X0 + 1, Terrain.Resolution - 1);
	const int32 Y1 = FMath::Min(Y0 + 1, Terrain.Resolution - 1);
	const float TX = GX - X0;
	const float TY = GY - Y0;
	const int32 A = Terrain.HeightsCentimeters[Y0 * Terrain.Resolution + X0];
	const int32 B = Terrain.HeightsCentimeters[Y0 * Terrain.Resolution + X1];
	const int32 C = Terrain.HeightsCentimeters[Y1 * Terrain.Resolution + X0];
	const int32 D = Terrain.HeightsCentimeters[Y1 * Terrain.Resolution + X1];
	return FMath::RoundToInt(FMath::Lerp(FMath::Lerp(static_cast<float>(A), static_cast<float>(B), TX),
		FMath::Lerp(static_cast<float>(C), static_cast<float>(D), TX), TY));
}

float FWorldDirectorPhysicalGenerator::SampleSlopeDegrees(
	const FWorldDirectorTerrainRecipe& Terrain,
	const FVector2D& Position)
{
	const float Step = 2.0f * Terrain.ExtentCentimeters / FMath::Max(1, Terrain.Resolution - 1);
	const float DX = static_cast<float>(SampleHeightCentimeters(Terrain, Position + FVector2D(Step, 0.0f))
		- SampleHeightCentimeters(Terrain, Position - FVector2D(Step, 0.0f))) / (2.0f * Step);
	const float DY = static_cast<float>(SampleHeightCentimeters(Terrain, Position + FVector2D(0.0f, Step))
		- SampleHeightCentimeters(Terrain, Position - FVector2D(0.0f, Step))) / (2.0f * Step);
	return FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(DX * DX + DY * DY)));
}

bool FWorldDirectorPhysicalGenerator::Generate(
	const FGeneratedWorldSpec& Spec,
	FResolvedWorldPlan& InOutPlan,
	FValidationReport& InOutReport)
{
	InOutPlan.Version = 2;
	InOutPlan.GeneratorVersion = TEXT("worldgen-physical-v2");
	const UWorldEnvironmentProfile* Profile = UWorldEnvironmentProfile::ResolveStylizedVillage();
	FString ProfileError;
	if (Profile == nullptr || !Profile->Validate(ProfileError))
	{
		InOutReport.AddError(TEXT("generator.profile_invalid"), TEXT("environmentProfile"), ProfileError);
		return false;
	}
	InOutPlan.ContentVersion = Profile->ContentVersion;
	InOutPlan.EnvironmentProfile = Profile->ProfileTag;
	InOutPlan.TerrainMap.Reset();
	if (FingerprintBytes(TArray<uint8>()).IsEmpty())
	{
		InOutReport.AddError(TEXT("generator.crypto_unavailable"), TEXT("fingerprints"),
			TEXT("The platform SHA-256 provider is unavailable; deterministic generation cannot continue."));
		return false;
	}
	for (const FString& Stage : {TEXT("terrain"), TEXT("districts"), TEXT("plots"), TEXT("roads"), TEXT("surfaces"), TEXT("dressing")})
	{
		InOutPlan.StageSeeds.Add(Stage, DeriveStageSeed(Spec.Seed, Stage));
	}

	FWorldDirectorTerrainRecipe& Terrain = InOutPlan.Terrain;
	Terrain = FWorldDirectorTerrainRecipe();
	Terrain.Resolution = TerrainResolution;
	Terrain.ExtentCentimeters = TerrainExtent;
	Terrain.Archetype = ResolveArchetype(Spec.Brief, Spec.Seed);
	Terrain.HeightsCentimeters.Reserve(TerrainResolution * TerrainResolution);
	Terrain.SurfaceTypes.Reserve(TerrainResolution * TerrainResolution);
	const int32 TerrainSeed = InOutPlan.StageSeeds[TEXT("terrain")];
	int32 MinimumHeight = MAX_int32;
	int32 MaximumHeight = MIN_int32;
	for (int32 Y = 0; Y < TerrainResolution; ++Y)
	{
		for (int32 X = 0; X < TerrainResolution; ++X)
		{
			const float NX = static_cast<float>(X) / (TerrainResolution - 1) * 2.0f - 1.0f;
			const float NY = static_cast<float>(Y) / (TerrainResolution - 1) * 2.0f - 1.0f;
			const int32 Height = GenerateHeight(Terrain.Archetype, NX, NY, TerrainSeed);
			Terrain.HeightsCentimeters.Add(Height);
			MinimumHeight = FMath::Min(MinimumHeight, Height);
			MaximumHeight = FMath::Max(MaximumHeight, Height);
		}
	}
	Terrain.MinimumHeightCentimeters = MinimumHeight;
	Terrain.MaximumHeightCentimeters = MaximumHeight;
	if (Terrain.Archetype == EWorldDirectorTerrainArchetype::Coast)
	{
		Terrain.WaterLevelCentimeters = 0;
		Terrain.WaterControlPoints = {FVector(-TerrainExtent, -TerrainExtent, 0), FVector(-TerrainExtent * 0.2f, 0, 0), FVector(-TerrainExtent, TerrainExtent, 0)};
	}
	else if (Terrain.Archetype == EWorldDirectorTerrainArchetype::Valley || Terrain.Archetype == EWorldDirectorTerrainArchetype::Marsh)
	{
		Terrain.WaterLevelCentimeters = Terrain.Archetype == EWorldDirectorTerrainArchetype::Marsh ? 45 : 115;
		const float Bend = static_cast<float>((TerrainSeed % 3200) - 1600);
		Terrain.WaterControlPoints = {FVector(-TerrainExtent, Bend, Terrain.WaterLevelCentimeters), FVector(0, -Bend * 0.45f, Terrain.WaterLevelCentimeters), FVector(TerrainExtent, Bend * 0.7f, Terrain.WaterLevelCentimeters)};
	}

	double TotalSlope = 0.0;
	TArray<uint8> HeightBytes;
	for (const int32 Height : Terrain.HeightsCentimeters)
	{
		AppendInt32(HeightBytes, Height);
	}
	Terrain.HeightFingerprint = FingerprintBytes(HeightBytes);
	for (int32 Y = 0; Y < TerrainResolution; ++Y)
	{
		for (int32 X = 0; X < TerrainResolution; ++X)
		{
			const FVector2D Position(
				-TerrainExtent + static_cast<float>(X) / (TerrainResolution - 1) * TerrainDiameter,
				-TerrainExtent + static_cast<float>(Y) / (TerrainResolution - 1) * TerrainDiameter);
			const int32 Height = Terrain.HeightsCentimeters[Y * TerrainResolution + X];
			const float Slope = SampleSlopeDegrees(Terrain, Position);
			TotalSlope += Slope;
			EWorldDirectorSurfaceType Surface = EWorldDirectorSurfaceType::Grass;
			if (Terrain.WaterLevelCentimeters != INDEX_NONE && Height <= Terrain.WaterLevelCentimeters + 15)
			{
				Surface = EWorldDirectorSurfaceType::Water;
			}
			else if (Slope > 24.0f)
			{
				Surface = EWorldDirectorSurfaceType::Rock;
			}
			else if ((static_cast<uint32>(X) + static_cast<uint32>(Y) +
				static_cast<uint32>(InOutPlan.StageSeeds[TEXT("surfaces")])) % 17U < 3U && Slope < 7.0f)
			{
				Surface = EWorldDirectorSurfaceType::Farmfield;
			}
			Terrain.SurfaceTypes.Add(static_cast<uint8>(Surface));
		}
	}
	Terrain.MeanSlopeDegrees = static_cast<float>(TotalSlope / Terrain.SurfaceTypes.Num());
	Terrain.SurfaceFingerprint = FingerprintBytes(Terrain.SurfaceTypes);

	FRandomStream DistrictRandom(InOutPlan.StageSeeds[TEXT("districts")]);
	const int32 DistrictCount = FMath::Clamp(Spec.Topology.Districts.Num(), 2, 5);
	for (int32 Index = 0; Index < DistrictCount; ++Index)
	{
		const float Angle = static_cast<float>(Index) / DistrictCount * 2.0f * PI + DistrictRandom.FRandRange(-0.35f, 0.35f);
		const float Radius = Index == 0 ? 900.0f : DistrictRandom.FRandRange(2800.0f, 5200.0f);
		FWorldDirectorDistrictAnchor& Anchor = InOutPlan.DistrictAnchors.AddDefaulted_GetRef();
		Anchor.DistrictId = Spec.Topology.Districts.IsValidIndex(Index)
			? Spec.Topology.Districts[Index] : FString::Printf(TEXT("district.%d"), Index);
		const FVector2D Position(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius);
		Anchor.Position = FVector(Position, SampleHeightCentimeters(Terrain, Position));
		Anchor.InfluenceRadiusCentimeters = DistrictRandom.FRandRange(3000.0f, 4800.0f);
	}

	FRandomStream PlotRandom(InOutPlan.StageSeeds[TEXT("plots")]);
	TArray<FBox2D> Occupied;
	for (int32 LocationIndex = 0; LocationIndex < InOutPlan.Locations.Num(); ++LocationIndex)
	{
		FResolvedLocationPlan& Location = InOutPlan.Locations[LocationIndex];
		const bool bLandmark = Location.LocationId == InOutPlan.LandmarkLocationId;
		const int32 DistrictIndex = bLandmark ? 0 : LocationIndex % InOutPlan.DistrictAnchors.Num();
		const FWorldDirectorDistrictAnchor& District = InOutPlan.DistrictAnchors[DistrictIndex];
		bool bPlaced = false;
		for (int32 Attempt = 0; Attempt < PlotAttempts; ++Attempt)
		{
			const float Angle = PlotRandom.FRandRange(-PI, PI);
			const bool bGlobalFallback = Attempt >= 320;
			const float Radius = bLandmark
				? PlotRandom.FRandRange(4600.0f, 7600.0f)
				: PlotRandom.FRandRange(700.0f, bGlobalFallback ? 10500.0f : District.InfluenceRadiusCentimeters);
			const FVector2D Origin = bGlobalFallback ? FVector2D::ZeroVector : FVector2D(District.Position);
			const FVector2D Candidate = Origin + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
			const FVector2D Facing = -Candidate.GetSafeNormal();
			const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Facing.Y, Facing.X)) + 90.0f;
			if (Candidate.Size() > TerrainExtent - 1800.0f ||
				OverlapsAny(Candidate, Location.FootprintSize, Yaw, Occupied))
			{
				continue;
			}
			const int32 GroundHeight = SampleHeightCentimeters(Terrain, Candidate);
			const float Slope = SampleSlopeDegrees(Terrain, Candidate);
			const float SlopeLimit = bLandmark ? Profile->MaximumPlotSlopeDegrees + 5.0f : Profile->MaximumPlotSlopeDegrees;
			if (Slope > SlopeLimit || (Terrain.WaterLevelCentimeters != INDEX_NONE && GroundHeight < Terrain.WaterLevelCentimeters + 45))
			{
				continue;
			}
			Location.Transform = FTransform(FRotator(0.0f, Yaw, 0.0f), FVector(Candidate, GroundHeight + 5.0f));
			Location.DistrictId = District.DistrictId;
			Location.GroundHeightCentimeters = GroundHeight;
			Location.GroundSlopeDegrees = Slope;
			const FVector EntranceOffset(0.0f, -(Location.FootprintSize.Y * 0.5f + 110.0f), 0.0f);
			FVector Entrance = Location.Transform.GetLocation() + Location.Transform.GetRotation().RotateVector(EntranceOffset);
			Entrance.Z = SampleHeightCentimeters(Terrain, FVector2D(Entrance)) + 8.0f;
			Location.EntranceTransform = FTransform(Location.Transform.GetRotation(), Entrance);
			const float Radians = FMath::DegreesToRadians(Yaw);
			const float AbsCos = FMath::Abs(FMath::Cos(Radians));
			const float AbsSin = FMath::Abs(FMath::Sin(Radians));
			const FVector2D Half = Location.FootprintSize * 0.5f;
			const FVector2D Extent(AbsCos * Half.X + AbsSin * Half.Y + 340.0f,
				AbsSin * Half.X + AbsCos * Half.Y + 340.0f);
			Occupied.Add(FBox2D(Candidate - Extent, Candidate + Extent));
			bPlaced = true;
			break;
		}
		if (!bPlaced)
		{
			InOutReport.AddError(TEXT("generator.plot_unsatisfied"), FString::Printf(TEXT("locations[%d]"), LocationIndex),
				TEXT("Terrain-aware plot search exhausted its deterministic candidate budget."));
			return false;
		}
	}

	// Grade deterministic pads into the authoritative terrain recipe. This keeps
	// floors, doors, collision, and navigation on the same physical surface.
	for (int32 Y = 0; Y < TerrainResolution; ++Y)
	{
		for (int32 X = 0; X < TerrainResolution; ++X)
		{
			const FVector2D Position(
				-TerrainExtent + static_cast<float>(X) / (TerrainResolution - 1) * TerrainDiameter,
				-TerrainExtent + static_cast<float>(Y) / (TerrainResolution - 1) * TerrainDiameter);
			float Height = Terrain.HeightsCentimeters[Y * TerrainResolution + X];
			for (const FResolvedLocationPlan& Location : InOutPlan.Locations)
			{
				const FVector Local = Location.Transform.InverseTransformPosition(
					FVector(Position, Location.GroundHeightCentimeters));
				const float OuterX = Location.FootprintSize.X * 0.5f + 650.0f;
				const float OuterY = Location.FootprintSize.Y * 0.5f + 650.0f;
				const float EdgeX = FMath::Max(FMath::Abs(Local.X) - OuterX, 0.0f);
				const float EdgeY = FMath::Max(FMath::Abs(Local.Y) - OuterY, 0.0f);
				const float OutsideDistance = FMath::Sqrt(EdgeX * EdgeX + EdgeY * EdgeY);
				if (OutsideDistance <= 700.0f)
				{
					const float Alpha = 1.0f - FMath::SmoothStep(0.0f, 700.0f, OutsideDistance);
					Height = FMath::Lerp(Height, static_cast<float>(Location.GroundHeightCentimeters), Alpha);
				}
			}
			Terrain.HeightsCentimeters[Y * TerrainResolution + X] = FMath::RoundToInt(Height);
		}
	}
	MinimumHeight = MAX_int32;
	MaximumHeight = MIN_int32;
	TotalSlope = 0.0;
	Terrain.SurfaceTypes.Reset();
	Terrain.SurfaceTypes.Reserve(TerrainResolution * TerrainResolution);
	for (int32 Y = 0; Y < TerrainResolution; ++Y)
	{
		for (int32 X = 0; X < TerrainResolution; ++X)
		{
			const FVector2D Position(
				-TerrainExtent + static_cast<float>(X) / (TerrainResolution - 1) * TerrainDiameter,
				-TerrainExtent + static_cast<float>(Y) / (TerrainResolution - 1) * TerrainDiameter);
			const int32 Height = Terrain.HeightsCentimeters[Y * TerrainResolution + X];
			const float Slope = SampleSlopeDegrees(Terrain, Position);
			MinimumHeight = FMath::Min(MinimumHeight, Height);
			MaximumHeight = FMath::Max(MaximumHeight, Height);
			TotalSlope += Slope;
			bool bPlotSurface = false;
			for (const FResolvedLocationPlan& Location : InOutPlan.Locations)
			{
				const FVector Local = Location.Transform.InverseTransformPosition(FVector(Position, Height));
				bPlotSurface |= FMath::Abs(Local.X) <= Location.FootprintSize.X * 0.5f + 420.0f &&
					FMath::Abs(Local.Y) <= Location.FootprintSize.Y * 0.5f + 420.0f;
			}
			EWorldDirectorSurfaceType Surface = bPlotSurface
				? EWorldDirectorSurfaceType::Paving : EWorldDirectorSurfaceType::Grass;
			if (!bPlotSurface && Terrain.WaterLevelCentimeters != INDEX_NONE && Height <= Terrain.WaterLevelCentimeters + 15)
			{
				Surface = EWorldDirectorSurfaceType::Water;
			}
			else if (!bPlotSurface && Slope > 24.0f)
			{
				Surface = EWorldDirectorSurfaceType::Rock;
			}
			else if (!bPlotSurface && (static_cast<uint32>(X) + static_cast<uint32>(Y) +
				static_cast<uint32>(InOutPlan.StageSeeds[TEXT("surfaces")])) % 17U < 3U && Slope < 7.0f)
			{
				Surface = EWorldDirectorSurfaceType::Farmfield;
			}
			Terrain.SurfaceTypes.Add(static_cast<uint8>(Surface));
		}
	}
	Terrain.MinimumHeightCentimeters = MinimumHeight;
	Terrain.MaximumHeightCentimeters = MaximumHeight;
	Terrain.MeanSlopeDegrees = static_cast<float>(TotalSlope / Terrain.SurfaceTypes.Num());
	HeightBytes.Reset();
	for (const int32 Height : Terrain.HeightsCentimeters)
	{
		AppendInt32(HeightBytes, Height);
	}
	Terrain.HeightFingerprint = FingerprintBytes(HeightBytes);
	Terrain.SurfaceFingerprint = FingerprintBytes(Terrain.SurfaceTypes);
	for (FResolvedLocationPlan& Location : InOutPlan.Locations)
	{
		FVector Entrance = Location.EntranceTransform.GetLocation();
		Entrance.Z = SampleHeightCentimeters(Terrain, FVector2D(Entrance)) + 8.0f;
		Location.EntranceTransform.SetLocation(Entrance);
	}

	InOutPlan.Routes.Reset();
	TSet<FString> RoutePairs;
	auto AddRoute = [&](const FString& FromId, const FString& ToId, const FName RouteType)
	{
		const FString PairKey = FromId < ToId ? FromId + TEXT("|") + ToId : ToId + TEXT("|") + FromId;
		if (RoutePairs.Contains(PairKey))
		{
			return;
		}
		const FResolvedLocationPlan* From = InOutPlan.Locations.FindByPredicate([&](const FResolvedLocationPlan& Value) { return Value.LocationId == FromId; });
		const FResolvedLocationPlan* To = InOutPlan.Locations.FindByPredicate([&](const FResolvedLocationPlan& Value) { return Value.LocationId == ToId; });
		if (From == nullptr || To == nullptr)
		{
			return;
		}
		RoutePairs.Add(PairKey);
		const FVector Start = From->EntranceTransform.GetLocation();
		const FVector End = To->EntranceTransform.GetLocation();
		FVector2D Perpendicular(-(End.Y - Start.Y), End.X - Start.X);
		Perpendicular.Normalize();
		FRandomStream RouteRandom(DeriveStageSeed(InOutPlan.StageSeeds[TEXT("roads")], PairKey));
		const FVector2D Mid2D = FVector2D((Start + End) * 0.5f) + Perpendicular * RouteRandom.FRandRange(-900.0f, 900.0f);
		FResolvedRoutePlan& Route = InOutPlan.Routes.AddDefaulted_GetRef();
		Route.FromLocationId = FromId;
		Route.ToLocationId = ToId;
		Route.RouteType = RouteType;
		Route.WidthCentimeters = RouteType == TEXT("Road") ? 420.0f : 240.0f;
		Route.ControlPoints.Reserve(9);
		const FVector2D Start2D(Start);
		const FVector2D End2D(End);
		for (int32 SampleIndex = 0; SampleIndex <= 8; ++SampleIndex)
		{
			const float T = static_cast<float>(SampleIndex) / 8.0f;
			const float OneMinusT = 1.0f - T;
			const FVector2D Point2D = OneMinusT * OneMinusT * Start2D +
				2.0f * OneMinusT * T * Mid2D + T * T * End2D;
			Route.ControlPoints.Add(FVector(Point2D, SampleHeightCentimeters(Terrain, Point2D) + 12.0f));
		}
		const FVector Delta = End - Start;
		Route.Transform = FTransform(FRotator(0.0f, FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)), 0.0f),
			(Start + End) * 0.5f, FVector(Delta.Size2D() / 100.0f, Route.WidthCentimeters / 100.0f, 0.04f));
		Route.MaximumGrade = 0.0f;
		for (int32 PointIndex = 1; PointIndex < Route.ControlPoints.Num(); ++PointIndex)
		{
			const FVector& Previous = Route.ControlPoints[PointIndex - 1];
			const FVector& Current = Route.ControlPoints[PointIndex];
			Route.MaximumGrade = FMath::Max(Route.MaximumGrade,
				FMath::Abs(Current.Z - Previous.Z) /
				FMath::Max(1.0f, FVector2D::Distance(FVector2D(Current), FVector2D(Previous))));
		}
	};
	for (const FTownTopologyEdge& Edge : Spec.Topology.Edges)
	{
		AddRoute(Edge.FromLocationId, Edge.ToLocationId, Edge.RouteType);
	}
	// Ensure physical connectivity even when semantic topology omitted an edge.
	for (int32 Index = 1; Index < InOutPlan.Locations.Num(); ++Index)
	{
		const FResolvedLocationPlan& Location = InOutPlan.Locations[Index];
		const FResolvedLocationPlan* Nearest = nullptr;
		float NearestDistance = MAX_flt;
		for (int32 Prior = 0; Prior < Index; ++Prior)
		{
			const float Distance = FVector2D::Distance(FVector2D(Location.EntranceTransform.GetLocation()), FVector2D(InOutPlan.Locations[Prior].EntranceTransform.GetLocation()));
			if (Distance < NearestDistance)
			{
				NearestDistance = Distance;
				Nearest = &InOutPlan.Locations[Prior];
			}
		}
		if (Nearest != nullptr)
		{
			AddRoute(Location.LocationId, Nearest->LocationId, TEXT("Path"));
		}
	}

	FRandomStream DressingRandom(InOutPlan.StageSeeds[TEXT("dressing")]);
	for (int32 Attempt = 0; Attempt < 360 && InOutPlan.Dressing.Num() < 110; ++Attempt)
	{
		const FVector2D Position(DressingRandom.FRandRange(-TerrainExtent + 600.0f, TerrainExtent - 600.0f),
			DressingRandom.FRandRange(-TerrainExtent + 600.0f, TerrainExtent - 600.0f));
		const int32 Height = SampleHeightCentimeters(Terrain, Position);
		if (SampleSlopeDegrees(Terrain, Position) > 24.0f ||
			(Terrain.WaterLevelCentimeters != INDEX_NONE && Height < Terrain.WaterLevelCentimeters + 30))
		{
			continue;
		}
		bool bExcluded = false;
		for (const FResolvedLocationPlan& Location : InOutPlan.Locations)
		{
			if (FVector2D::Distance(Position, FVector2D(Location.Transform.GetLocation())) < Location.FootprintSize.Size() * 0.65f + 500.0f)
			{
				bExcluded = true;
				break;
			}
		}
		for (const FResolvedRoutePlan& Route : InOutPlan.Routes)
		{
			for (int32 PointIndex = 1; PointIndex < Route.ControlPoints.Num(); ++PointIndex)
			{
				if (DistanceToSegment(Position, FVector2D(Route.ControlPoints[PointIndex - 1]), FVector2D(Route.ControlPoints[PointIndex])) < Route.WidthCentimeters + 220.0f)
				{
					bExcluded = true;
				}
			}
		}
		if (bExcluded)
		{
			continue;
		}
		const int32 MeshIndex = DressingRandom.RandRange(0, Profile->DressingMeshes.Num() - 1);
		FWorldDirectorDressingInstance& Instance = InOutPlan.Dressing.AddDefaulted_GetRef();
		Instance.MeshAsset = Profile->DressingMeshes[MeshIndex];
		Instance.Transform = FTransform(FRotator(0.0f, DressingRandom.FRandRange(0.0f, 360.0f), 0.0f),
			FVector(Position, Height), FVector(DressingRandom.FRandRange(0.82f, 1.18f)));
		Instance.BiomeTag = MeshIndex == 0 ? TEXT("Biome.Wilderness") : TEXT("Biome.SettlementEdge");
	}

	TArray<uint8> LayoutBytes;
	for (const FResolvedLocationPlan& Location : InOutPlan.Locations)
	{
		AppendString(LayoutBytes, Location.LocationId);
		AppendString(LayoutBytes, Location.DistrictId);
		AppendString(LayoutBytes, Location.ShellAsset.ToString());
		AppendString(LayoutBytes, Location.InteriorAsset.ToString());
		AppendInt32(LayoutBytes, FMath::RoundToInt(Location.Transform.GetLocation().X));
		AppendInt32(LayoutBytes, FMath::RoundToInt(Location.Transform.GetLocation().Y));
		AppendInt32(LayoutBytes, FMath::RoundToInt(Location.Transform.GetLocation().Z));
		AppendInt32(LayoutBytes, FMath::RoundToInt(Location.Transform.Rotator().Yaw * 100.0f));
		AppendInt32(LayoutBytes, FMath::RoundToInt(Location.FootprintSize.X));
		AppendInt32(LayoutBytes, FMath::RoundToInt(Location.FootprintSize.Y));
	}
	InOutPlan.LayoutFingerprint = FingerprintBytes(LayoutBytes);
	TArray<uint8> RouteBytes;
	for (const FResolvedRoutePlan& Route : InOutPlan.Routes)
	{
		AppendString(RouteBytes, Route.FromLocationId);
		AppendString(RouteBytes, Route.ToLocationId);
		AppendString(RouteBytes, Route.RouteType.ToString());
		AppendInt32(RouteBytes, FMath::RoundToInt(Route.WidthCentimeters));
		for (const FVector& Point : Route.ControlPoints)
		{
			AppendInt32(RouteBytes, FMath::RoundToInt(Point.X));
			AppendInt32(RouteBytes, FMath::RoundToInt(Point.Y));
			AppendInt32(RouteBytes, FMath::RoundToInt(Point.Z));
		}
	}
	InOutPlan.RouteFingerprint = FingerprintBytes(RouteBytes);
	TArray<uint8> DressingBytes;
	for (const FWorldDirectorDressingInstance& Instance : InOutPlan.Dressing)
	{
		AppendString(DressingBytes, Instance.MeshAsset.ToString());
		AppendInt32(DressingBytes, FMath::RoundToInt(Instance.Transform.GetLocation().X));
		AppendInt32(DressingBytes, FMath::RoundToInt(Instance.Transform.GetLocation().Y));
		AppendInt32(DressingBytes, FMath::RoundToInt(Instance.Transform.GetLocation().Z));
		AppendInt32(DressingBytes, FMath::RoundToInt(Instance.Transform.Rotator().Yaw * 100.0f));
	}
	InOutPlan.DressingFingerprint = FingerprintBytes(DressingBytes);
	TArray<uint8> WorldBytes;
	AppendString(WorldBytes, InOutPlan.GeneratorVersion);
	AppendString(WorldBytes, InOutPlan.ContentVersion);
	AppendString(WorldBytes, Terrain.HeightFingerprint);
	AppendString(WorldBytes, Terrain.SurfaceFingerprint);
	AppendInt32(WorldBytes, static_cast<int32>(Terrain.Archetype));
	AppendInt32(WorldBytes, Terrain.WaterLevelCentimeters);
	for (const FVector& WaterPoint : Terrain.WaterControlPoints)
	{
		AppendInt32(WorldBytes, FMath::RoundToInt(WaterPoint.X));
		AppendInt32(WorldBytes, FMath::RoundToInt(WaterPoint.Y));
		AppendInt32(WorldBytes, FMath::RoundToInt(WaterPoint.Z));
	}
	for (const FWorldDirectorDistrictAnchor& District : InOutPlan.DistrictAnchors)
	{
		AppendString(WorldBytes, District.DistrictId);
		AppendInt32(WorldBytes, FMath::RoundToInt(District.Position.X));
		AppendInt32(WorldBytes, FMath::RoundToInt(District.Position.Y));
		AppendInt32(WorldBytes, FMath::RoundToInt(District.InfluenceRadiusCentimeters));
	}
	for (const FResident& Resident : Spec.Residents)
	{
		AppendString(WorldBytes, Resident.Id);
		AppendString(WorldBytes, Resident.HomeLocationId);
		AppendString(WorldBytes, Resident.WorkplaceLocationId);
		AppendString(WorldBytes, Resident.OccupationTag.ToString());
	}
	AppendString(WorldBytes, InOutPlan.LayoutFingerprint);
	AppendString(WorldBytes, InOutPlan.RouteFingerprint);
	AppendString(WorldBytes, InOutPlan.DressingFingerprint);
	InOutPlan.WorldFingerprint = FingerprintBytes(WorldBytes);
	return InOutReport.bValid;
}

FString FWorldDirectorPhysicalGenerator::BuildCandidateSummary(const FResolvedWorldPlan& Plan)
{
	int32 Grass = 0;
	int32 Farm = 0;
	int32 Rock = 0;
	for (const uint8 Surface : Plan.Terrain.SurfaceTypes)
	{
		Grass += Surface == static_cast<uint8>(EWorldDirectorSurfaceType::Grass);
		Farm += Surface == static_cast<uint8>(EWorldDirectorSurfaceType::Farmfield);
		Rock += Surface == static_cast<uint8>(EWorldDirectorSurfaceType::Rock);
	}
	const float SurfaceCount = FMath::Max(1, Plan.Terrain.SurfaceTypes.Num());
	float MaximumRoadGrade = 0.0f;
	for (const FResolvedRoutePlan& Route : Plan.Routes)
	{
		MaximumRoadGrade = FMath::Max(MaximumRoadGrade, Route.MaximumGrade);
	}
	return FString::Printf(
		TEXT("%s terrain, %.0f cm relief, %.1f deg mean slope; %d districts, %d plots, %d curved routes (max grade %.1f%%); surfaces %.0f%% grass / %.0f%% farm / %.0f%% rock; %d dressing instances; fingerprint %s"),
		*ArchetypeName(Plan.Terrain.Archetype),
		Plan.Terrain.MaximumHeightCentimeters - Plan.Terrain.MinimumHeightCentimeters,
		Plan.Terrain.MeanSlopeDegrees, Plan.DistrictAnchors.Num(), Plan.Locations.Num(), Plan.Routes.Num(),
		MaximumRoadGrade * 100.0f, Grass / SurfaceCount * 100.0f, Farm / SurfaceCount * 100.0f,
		Rock / SurfaceCount * 100.0f, Plan.Dressing.Num(), *Plan.WorldFingerprint.Left(12));
}
