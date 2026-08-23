#include "WorldDirectorPhysicalGenerator.h"

#include "IPlatformCrypto.h"
#include "Math/RandomStream.h"
#include "WorldDirectorRuntime.h"
#include "WorldEnvironmentProfile.h"

namespace
{
constexpr int32 TerrainResolution = 193;
constexpr int32 TerrainExtent = 60000;
constexpr int32 TerrainDiameter = TerrainExtent * 2;
constexpr int32 PlotAttempts = 1800;
constexpr int32 RouteGridResolution = 65;

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

float SeedUnit(const int32 Seed, const uint32 Salt)
{
	return static_cast<float>(Mix32(static_cast<uint32>(Seed) ^ Salt) & 0x00ffffffU) /
		static_cast<float>(0x00ffffffU);
}

float SeedSigned(const int32 Seed, const uint32 Salt)
{
	return SeedUnit(Seed, Salt) * 2.0f - 1.0f;
}

float WrappedAngleDistance(const float A, const float B)
{
	return FMath::Abs(FMath::UnwindRadians(A - B));
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
	float Result = 0.0f;
	float Amplitude = 0.54f;
	float Frequency = 1.65f;
	for (int32 Octave = 0; Octave < 5; ++Octave)
	{
		Result += SmoothNoise(X * Frequency, Y * Frequency, Seed + Octave * 101) * Amplitude;
		Frequency *= 2.07f;
		Amplitude *= 0.48f;
	}
	return Result;
}

float RidgedNoise(const float X, const float Y, const int32 Seed)
{
	float Result = 0.0f;
	float Amplitude = 0.62f;
	float Frequency = 1.2f;
	for (int32 Octave = 0; Octave < 4; ++Octave)
	{
		const float Ridge = 1.0f - FMath::Abs(SmoothNoise(
			X * Frequency, Y * Frequency, Seed + 509 + Octave * 79));
		Result += Ridge * Ridge * Amplitude;
		Frequency *= 2.13f;
		Amplitude *= 0.46f;
	}
	return FMath::Clamp(Result, 0.0f, 1.25f);
}

float SmoothRange(const float Min, const float Max, const float Value)
{
	return FMath::SmoothStep(0.0f, 1.0f, FMath::GetRangePct(Min, Max, Value));
}

float TerrainAngle(const int32 Seed)
{
	return static_cast<float>(static_cast<uint32>(Seed) % 6283U) / 1000.0f;
}

FVector2D ToTerrainFrame(const FVector2D& WorldNormalized, const int32 Seed)
{
	const float Angle = TerrainAngle(Seed);
	return FVector2D(
		WorldNormalized.X * FMath::Cos(Angle) - WorldNormalized.Y * FMath::Sin(Angle),
		WorldNormalized.X * FMath::Sin(Angle) + WorldNormalized.Y * FMath::Cos(Angle));
}

FVector2D FromTerrainFrame(const FVector2D& Framed, const int32 Seed)
{
	const float Angle = TerrainAngle(Seed);
	return FVector2D(
		Framed.X * FMath::Cos(Angle) + Framed.Y * FMath::Sin(Angle),
		-Framed.X * FMath::Sin(Angle) + Framed.Y * FMath::Cos(Angle));
}

float RiverCenterInFrame(const float Along, const int32 Seed)
{
	const float Phase = static_cast<float>(static_cast<uint32>(Seed) % 4096U) / 4096.0f * 2.0f * PI;
	return 0.055f * FMath::Sin(Along * 4.2f + Phase) +
		0.028f * FMath::Sin(Along * 9.1f - Phase * 0.63f);
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
	const FVector2D Framed = ToTerrainFrame(FVector2D(NX, NY), TerrainSeed);
	const float WarpX = SmoothNoise(NX * 1.15f, NY * 1.15f, TerrainSeed + 311) * 0.16f;
	const float WarpY = SmoothNoise(NX * 1.15f, NY * 1.15f, TerrainSeed + 733) * 0.16f;
	const float RX = Framed.X + WarpX;
	const float RY = Framed.Y + WarpY;
	const float Broad = FractalNoise(NX * 0.72f, NY * 0.72f, TerrainSeed + 29);
	const float Detail = FractalNoise(NX * 2.4f, NY * 2.4f, TerrainSeed + 97);
	const float Ridges = RidgedNoise(NX, NY, TerrainSeed);
	const float Radius = FVector2D(NX, NY).Size();
	float Height = 0.0f;
	switch (Archetype)
	{
	case EWorldDirectorTerrainArchetype::Valley:
	{
		const float CrossDistance = FMath::Abs(RX - RiverCenterInFrame(RY, TerrainSeed));
		const float Wall = FMath::Pow(SmoothRange(0.07f, 0.86f, CrossDistance), 1.48f);
		const float Channel = FMath::Exp(-FMath::Square(CrossDistance) * 330.0f);
		Height = 460.0f + Wall * (12400.0f + 5200.0f * Ridges) + Broad * 1050.0f +
			Detail * 460.0f - Channel * 980.0f + RY * 260.0f;
		break;
	}
	case EWorldDirectorTerrainArchetype::Ridge:
	{
		const float Crest = FMath::Exp(-FMath::Square(RX - RiverCenterInFrame(RY, TerrainSeed) * 1.7f) * 7.0f);
		const float Secondary = FMath::Exp(-FMath::Square(RX + 0.58f + 0.08f * Broad) * 13.0f);
		const float Foothill = SmoothRange(0.35f, 1.05f, Radius);
		Height = 520.0f + Crest * (11800.0f + 5600.0f * Ridges) + Secondary * 5200.0f +
			Foothill * 2600.0f + Broad * 820.0f + Detail * 520.0f;
		break;
	}
	case EWorldDirectorTerrainArchetype::Coast:
	{
		const float Shoreline = -0.2f + RiverCenterInFrame(RY, TerrainSeed) * 1.25f;
		const float Inland = RX - Shoreline;
		const float Land = SmoothRange(-0.025f, 0.16f, Inland);
		const float CoastalRise = FMath::Pow(FMath::Clamp(Inland / 1.2f, 0.0f, 1.0f), 1.35f);
		Height = FMath::Lerp(-1150.0f + Inland * 900.0f,
			420.0f + CoastalRise * (9300.0f + 4200.0f * Ridges) + Broad * 760.0f + Detail * 420.0f,
			Land);
		break;
	}
	case EWorldDirectorTerrainArchetype::Marsh:
	{
		const float MainChannel = FMath::Exp(-FMath::Square(
			RX - RiverCenterInFrame(RY, TerrainSeed) * 1.9f) * 440.0f);
		const float SideChannel = FMath::Exp(-FMath::Square(
			RX + 0.34f + 0.05f * FMath::Sin(RY * 7.0f)) * 620.0f);
		const float Hummocks = FMath::Square(FMath::Max(0.0f, Broad + 0.22f));
		const float Rim = FMath::Pow(SmoothRange(0.68f, 1.12f, Radius), 1.6f);
		Height = 130.0f + Hummocks * 1050.0f + Detail * 240.0f + Rim * 3100.0f -
			MainChannel * 520.0f - SideChannel * 330.0f;
		break;
	}
	case EWorldDirectorTerrainArchetype::Basin:
	default:
	{
		// The old basin was a seed-textured copy of the same circular bowl. Its radial
		// rim dominated every heightmap, so adjacent seeds could correlate above 0.96
		// even though their fingerprints differed. Seed the macro silhouette itself:
		// offset and stretch the bowl, cut one to three passes through the rim, and
		// move the interior spurs. These are broad landform decisions, not surface noise.
		const FVector2D BasinOffset(
			SeedSigned(TerrainSeed, 0x214f11a3U) * 0.17f,
			SeedSigned(TerrainSeed, 0x7b12d991U) * 0.14f);
		const float AxisX = 0.72f + SeedUnit(TerrainSeed, 0xb7a32e19U) * 0.48f;
		const float AxisY = 0.72f + SeedUnit(TerrainSeed, 0x51c48d2bU) * 0.48f;
		const FVector2D BasinPoint(
			(RX - BasinOffset.X) / AxisX,
			(RY - BasinOffset.Y) / AxisY);
		const float BasinRadius = BasinPoint.Size();
		const float RimStart = 0.26f + SeedUnit(TerrainSeed, 0x98acd3e1U) * 0.14f;
		const float RimEnd = 0.90f + SeedUnit(TerrainSeed, 0x3f526c87U) * 0.18f;
		const float RimExponent = 1.28f + SeedUnit(TerrainSeed, 0xe1f04421U) * 0.64f;
		const float Rim = FMath::Pow(SmoothRange(RimStart, RimEnd, BasinRadius), RimExponent);
		const float BasinAngle = FMath::Atan2(BasinPoint.Y, BasinPoint.X);
		float BreachMask = 1.0f;
		const int32 BreachCount = 1 + static_cast<int32>(
			Mix32(static_cast<uint32>(TerrainSeed) ^ 0x7a98b62dU) % 3U);
		for (int32 BreachIndex = 0; BreachIndex < BreachCount; ++BreachIndex)
		{
			const uint32 Salt = 0xa13d08f1U + static_cast<uint32>(BreachIndex) * 0x61c88647U;
			const float BreachAngle = SeedUnit(TerrainSeed, Salt) * 2.0f * PI - PI;
			const float BreachWidth = 0.12f + SeedUnit(TerrainSeed, Salt ^ 0x6ef34119U) * 0.18f;
			const float BreachStrength = 0.42f + SeedUnit(TerrainSeed, Salt ^ 0x9d7a4b35U) * 0.34f;
			const float AngularDistance = WrappedAngleDistance(BasinAngle, BreachAngle);
			const float Notch = FMath::Exp(-FMath::Square(AngularDistance / BreachWidth) * 0.5f) *
				SmoothRange(0.34f, 0.88f, BasinRadius);
			BreachMask *= 1.0f - Notch * BreachStrength;
		}
		const float BrokenRim = 0.62f + 0.48f * RidgedNoise(
			BasinPoint.X * 0.86f, BasinPoint.Y * 0.86f, TerrainSeed + 1201);
		float SpurHeight = 0.0f;
		const int32 SpurCount = 1 + static_cast<int32>(
			Mix32(static_cast<uint32>(TerrainSeed) ^ 0x4e1bbcd7U) % 3U);
		for (int32 SpurIndex = 0; SpurIndex < SpurCount; ++SpurIndex)
		{
			const uint32 Salt = 0x29b7c541U + static_cast<uint32>(SpurIndex) * 0x9e3779b9U;
			const float SpurAngle = SeedUnit(TerrainSeed, Salt) * 2.0f * PI;
			const float SpurRadius = 0.34f + SeedUnit(TerrainSeed, Salt ^ 0x8274ad61U) * 0.42f;
			const FVector2D SpurCenter = BasinOffset + FVector2D(
				FMath::Cos(SpurAngle) * SpurRadius * AxisX,
				FMath::Sin(SpurAngle) * SpurRadius * AxisY);
			const float SpurFalloff = 13.0f + SeedUnit(TerrainSeed, Salt ^ 0x13f16d8bU) * 18.0f;
			const float SpurAmplitude = 1700.0f + SeedUnit(TerrainSeed, Salt ^ 0xda83f24dU) * 3900.0f;
			SpurHeight += FMath::Exp(-(
				FMath::Square(RX - SpurCenter.X) + FMath::Square(RY - SpurCenter.Y)) * SpurFalloff) *
				SpurAmplitude;
		}
		const float FloorTilt = RX * SeedSigned(TerrainSeed, 0x512ba7d1U) * 720.0f +
			RY * SeedSigned(TerrainSeed, 0xc89f12e5U) * 720.0f;
		Height = 620.0f + Broad * 880.0f + Detail * 360.0f + FloorTilt +
			Rim * 13200.0f * BrokenRim * BreachMask + SpurHeight;
		break;
	}
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
	// Reserve the full cut/fill skirt, not just a narrow structure clearance.
	// The V2 mismatch allowed neighboring terrain pads to overwrite one another.
	const FVector2D Extent(
		AbsCos * Half.X + AbsSin * Half.Y + 1450.0f,
		AbsSin * Half.X + AbsCos * Half.Y + 1450.0f);
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

bool SegmentIntersectsBox2D(const FVector2D& A, const FVector2D& B, const FBox2D& Box)
{
	if (!Box.bIsValid)
	{
		return false;
	}
	const float Start[] = {A.X, A.Y};
	const float Delta[] = {B.X - A.X, B.Y - A.Y};
	const float Minimum[] = {Box.Min.X, Box.Min.Y};
	const float Maximum[] = {Box.Max.X, Box.Max.Y};
	float MinimumT = 0.0f;
	float MaximumT = 1.0f;
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		if (FMath::Abs(Delta[Axis]) <= KINDA_SMALL_NUMBER)
		{
			if (Start[Axis] < Minimum[Axis] || Start[Axis] > Maximum[Axis])
			{
				return false;
			}
			continue;
		}
		float NearT = (Minimum[Axis] - Start[Axis]) / Delta[Axis];
		float FarT = (Maximum[Axis] - Start[Axis]) / Delta[Axis];
		if (NearT > FarT)
		{
			Swap(NearT, FarT);
		}
		MinimumT = FMath::Max(MinimumT, NearT);
		MaximumT = FMath::Min(MaximumT, FarT);
		if (MinimumT > MaximumT)
		{
			return false;
		}
	}
	return true;
}

FString ArchetypeName(const EWorldDirectorTerrainArchetype Archetype)
{
	return StaticEnum<EWorldDirectorTerrainArchetype>()->GetNameStringByValue(static_cast<int64>(Archetype));
}

FVector2D FramePositionToWorld(const FVector2D& FramedCentimeters, const int32 TerrainSeed)
{
	return FromTerrainFrame(FramedCentimeters / static_cast<float>(TerrainExtent), TerrainSeed) *
		static_cast<float>(TerrainExtent);
}

void ApplyThermalErosion(FWorldDirectorTerrainRecipe& Terrain, const int32 Passes)
{
	if (Terrain.Resolution < 3 || Terrain.HeightsCentimeters.Num() != Terrain.Resolution * Terrain.Resolution)
	{
		return;
	}
	TArray<float> Working;
	Working.Reserve(Terrain.HeightsCentimeters.Num());
	for (const int32 Height : Terrain.HeightsCentimeters)
	{
		Working.Add(static_cast<float>(Height));
	}
	TArray<float> Delta;
	Delta.Init(0.0f, Working.Num());
	const float TalusCentimeters = 390.0f;
	for (int32 Pass = 0; Pass < Passes; ++Pass)
	{
		Delta.Init(0.0f, Working.Num());
		for (int32 Y = 1; Y < Terrain.Resolution - 1; ++Y)
		{
			for (int32 X = 1; X < Terrain.Resolution - 1; ++X)
			{
				const int32 Index = Y * Terrain.Resolution + X;
				const int32 Neighbors[] = {Index - 1, Index + 1, Index - Terrain.Resolution, Index + Terrain.Resolution};
				for (const int32 Neighbor : Neighbors)
				{
					const float Difference = Working[Index] - Working[Neighbor];
					if (Difference > TalusCentimeters)
					{
						const float Transfer = (Difference - TalusCentimeters) * 0.055f;
						Delta[Index] -= Transfer;
						Delta[Neighbor] += Transfer;
					}
				}
			}
		}
		for (int32 Index = 0; Index < Working.Num(); ++Index)
		{
			Working[Index] += Delta[Index];
		}
	}
	for (int32 Index = 0; Index < Working.Num(); ++Index)
	{
		Terrain.HeightsCentimeters[Index] = FMath::RoundToInt(Working[Index]);
	}
}

FName ResolveSettlementMorphology(
	const EWorldDirectorTerrainArchetype Archetype,
	const int32 Seed)
{
	const bool bAlternate = (static_cast<uint32>(Seed) & 1U) != 0U;
	switch (Archetype)
	{
	case EWorldDirectorTerrainArchetype::Valley:
		return bAlternate ? TEXT("Morphology.RiverTerraces") : TEXT("Morphology.LinearRiverTown");
	case EWorldDirectorTerrainArchetype::Ridge:
		return bAlternate ? TEXT("Morphology.HillRing") : TEXT("Morphology.SwitchbackTerraces");
	case EWorldDirectorTerrainArchetype::Coast:
		return bAlternate ? TEXT("Morphology.HarborCrescent") : TEXT("Morphology.CoastalRibbon");
	case EWorldDirectorTerrainArchetype::Marsh:
		return bAlternate ? TEXT("Morphology.CausewayTown") : TEXT("Morphology.IslandClusters");
	case EWorldDirectorTerrainArchetype::Basin:
	default:
		return bAlternate ? TEXT("Morphology.Crossroads") : TEXT("Morphology.ClusteredBasin");
	}
}

float PlaceIdentityHandedness(const int32 Seed)
{
	return (static_cast<uint32>(Seed) & 2U) != 0U ? -1.0f : 1.0f;
}

FString ResolveSpatialGrammarDescription(
	const EWorldDirectorTerrainArchetype Archetype,
	const FName Morphology)
{
	switch (Archetype)
	{
	case EWorldDirectorTerrainArchetype::Valley:
		return Morphology == TEXT("Morphology.RiverTerraces")
			? TEXT("paired terrace wards stepping across a civic river approach")
			: TEXT("a long market spine with neighborhoods alternating between the river terraces");
	case EWorldDirectorTerrainArchetype::Ridge:
		return Morphology == TEXT("Morphology.HillRing")
			? TEXT("a crown district encircled by sheltered hillside wards")
			: TEXT("a civic overlook linked to neighborhoods on successive switchback shelves");
	case EWorldDirectorTerrainArchetype::Coast:
		return Morphology == TEXT("Morphology.HarborCrescent")
			? TEXT("a protected harbor head framed by a crescent of waterfront wards")
			: TEXT("an inland civic anchor with districts strung along a coastal road");
	case EWorldDirectorTerrainArchetype::Marsh:
		return Morphology == TEXT("Morphology.CausewayTown")
			? TEXT("a raised civic hummock feeding narrow inhabited causeways")
			: TEXT("a central refuge ringed by distinct inhabited islands");
	case EWorldDirectorTerrainArchetype::Basin:
	default:
		return Morphology == TEXT("Morphology.Crossroads")
			? TEXT("a landmark crossroads with four legible approach wards")
			: TEXT("a protected civic core surrounded by irregular basin neighborhoods");
	}
}

FString ResolveStoryMotifDescription(const EWorldDirectorTerrainArchetype Archetype)
{
	switch (Archetype)
	{
	case EWorldDirectorTerrainArchetype::Valley:
		return TEXT("paired flood-scar remnants frame the landmark approach");
	case EWorldDirectorTerrainArchetype::Ridge:
		return TEXT("watch cairns mark the exposed overlook and its sheltered descent");
	case EWorldDirectorTerrainArchetype::Coast:
		return TEXT("stormbreak debris and stone markers trace the harbor-facing threshold");
	case EWorldDirectorTerrainArchetype::Marsh:
		return TEXT("causeway remnants and reed-edge markers reveal the surviving dry route");
	case EWorldDirectorTerrainArchetype::Basin:
	default:
		return TEXT("old breach stones frame the road entering the protected basin core");
	}
}

FName ResolveStoryBiomeTag(
	const EWorldDirectorTerrainArchetype Archetype,
	const FName Morphology)
{
	FString Motif;
	switch (Archetype)
	{
	case EWorldDirectorTerrainArchetype::Valley: Motif = TEXT("FloodScar"); break;
	case EWorldDirectorTerrainArchetype::Ridge: Motif = TEXT("WatchCairns"); break;
	case EWorldDirectorTerrainArchetype::Coast: Motif = TEXT("Stormbreak"); break;
	case EWorldDirectorTerrainArchetype::Marsh: Motif = TEXT("CausewayRemnants"); break;
	case EWorldDirectorTerrainArchetype::Basin:
	default: Motif = TEXT("BreachStones"); break;
	}
	FString MorphologyName = Morphology.ToString();
	MorphologyName.RemoveFromStart(TEXT("Morphology."));
	return FName(*FString::Printf(TEXT("Story.%s.%s.%s"),
		*ArchetypeName(Archetype), *MorphologyName, *Motif));
}

FName ResolveStoryPlacementTag(
	const EWorldDirectorTerrainArchetype Archetype,
	const bool bSecondary)
{
	switch (Archetype)
	{
	case EWorldDirectorTerrainArchetype::Valley:
		return bSecondary ? TEXT("Dressing.GroundCover") : TEXT("Dressing.Deadwood");
	case EWorldDirectorTerrainArchetype::Ridge:
		return bSecondary ? TEXT("Dressing.Wayfinding") : TEXT("Dressing.Rock");
	case EWorldDirectorTerrainArchetype::Coast:
		return bSecondary ? TEXT("Dressing.Deadwood") : TEXT("Dressing.Rock");
	case EWorldDirectorTerrainArchetype::Marsh:
		return bSecondary ? TEXT("Dressing.GroundCover") : TEXT("Dressing.Deadwood");
	case EWorldDirectorTerrainArchetype::Basin:
	default:
		return bSecondary ? TEXT("Dressing.Deadwood") : TEXT("Dressing.Rock");
	}
}

FName ResolveDistrictAffinity(
	const EWorldDirectorTerrainArchetype Archetype,
	const FName Morphology,
	const int32 Index)
{
	if (Index == 0)
	{
		switch (Archetype)
		{
		case EWorldDirectorTerrainArchetype::Valley: return TEXT("Terrain.CivicRiverCrossing");
		case EWorldDirectorTerrainArchetype::Ridge: return TEXT("Terrain.CivicOverlook");
		case EWorldDirectorTerrainArchetype::Coast: return TEXT("Terrain.HarborHead");
		case EWorldDirectorTerrainArchetype::Marsh: return TEXT("Terrain.CausewayHub");
		case EWorldDirectorTerrainArchetype::Basin:
		default: return TEXT("Terrain.BasinCrossroads");
		}
	}
	switch (Archetype)
	{
	case EWorldDirectorTerrainArchetype::Valley:
		return Index % 2 == 0 ? TEXT("Terrain.WestRiverTerrace") : TEXT("Terrain.EastRiverTerrace");
	case EWorldDirectorTerrainArchetype::Ridge:
		return Morphology == TEXT("Morphology.HillRing")
			? TEXT("Terrain.ShelteredHillWard") : Index % 2 == 0
				? TEXT("Terrain.UpperSwitchbackShelf") : TEXT("Terrain.LowerSwitchbackShelf");
	case EWorldDirectorTerrainArchetype::Coast:
		return Index % 2 == 0 ? TEXT("Terrain.UpperCoastWard") : TEXT("Terrain.WaterfrontShelf");
	case EWorldDirectorTerrainArchetype::Marsh:
		return Morphology == TEXT("Morphology.CausewayTown")
			? TEXT("Terrain.RaisedCausewayWard") : TEXT("Terrain.InhabitedHummock");
	case EWorldDirectorTerrainArchetype::Basin:
	default:
		return Morphology == TEXT("Morphology.Crossroads")
			? TEXT("Terrain.ApproachWard") : TEXT("Terrain.InnerBasinWard");
	}
}

FVector2D MorphologyTarget(
	const EWorldDirectorTerrainArchetype Archetype,
	const FName Morphology,
	const int32 Index,
	const int32 Count,
	const int32 TerrainSeed)
{
	const int32 OuterCount = FMath::Max(1, Count - 1);
	const int32 OuterIndex = FMath::Max(0, Index - 1);
	const float T = OuterCount > 1 ? static_cast<float>(OuterIndex) / (OuterCount - 1) : 0.5f;
	const float Handedness = PlaceIdentityHandedness(TerrainSeed);
	const float AlongOffset = (static_cast<uint32>(TerrainSeed) & 4U) != 0U ? 2800.0f : -2800.0f;
	FVector2D Framed = FVector2D::ZeroVector;
	switch (Archetype)
	{
	case EWorldDirectorTerrainArchetype::Valley:
		Framed = Index == 0
			? FVector2D(Handedness * 6000.0f, AlongOffset)
			: FVector2D((OuterIndex % 2 == 0 ? -Handedness : Handedness) *
				(5200.0f + 1200.0f * (OuterIndex % 2)), FMath::Lerp(-12000.0f, 12000.0f, T));
		break;
	case EWorldDirectorTerrainArchetype::Ridge:
		if (Morphology == TEXT("Morphology.HillRing"))
		{
			const float Angle = Handedness * (static_cast<float>(OuterIndex) / OuterCount * 2.0f * PI + 0.35f);
			Framed = Index == 0 ? FVector2D(1800.0f * Handedness, AlongOffset * 0.45f) :
				FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * (8600.0f + (OuterIndex % 2) * 2000.0f);
		}
		else
		{
			Framed = Index == 0 ? FVector2D(2500.0f * Handedness, AlongOffset) :
				FVector2D(Handedness * (-8500.0f + (OuterIndex % 3) * 3400.0f),
					FMath::Lerp(-12000.0f, 12000.0f, T));
		}
		break;
	case EWorldDirectorTerrainArchetype::Coast:
		Framed = Index == 0 ? FVector2D(5200.0f, AlongOffset * 0.4f) :
			FVector2D(Morphology == TEXT("Morphology.HarborCrescent")
				? 2500.0f + FMath::Abs(T - 0.5f) * 6800.0f : 4700.0f + (OuterIndex % 2) * 2400.0f,
				FMath::Lerp(-13500.0f, 13500.0f, Handedness > 0.0f ? T : 1.0f - T));
		break;
	case EWorldDirectorTerrainArchetype::Marsh:
	{
		const float Angle = Handedness * (static_cast<float>(OuterIndex) / OuterCount * 2.0f * PI + 0.62f);
		const float Radius = 8200.0f + (OuterIndex % 2) * 2800.0f;
		Framed = Index == 0 ? FVector2D(3200.0f * Handedness, AlongOffset * 0.35f) :
			FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
		break;
	}
	case EWorldDirectorTerrainArchetype::Basin:
	default:
		if (Morphology == TEXT("Morphology.Crossroads"))
		{
			const FVector2D Axes[] = {
				FVector2D(9200, 0), FVector2D(-9200, 0),
				FVector2D(0, 9200), FVector2D(0, -9200)
			};
			Framed = Index == 0 ? FVector2D(AlongOffset * 0.3f, 0.0f) :
				Axes[(OuterIndex + (Handedness < 0.0f ? 1 : 0)) % UE_ARRAY_COUNT(Axes)];
		}
		else
		{
			const float Angle = Handedness * (static_cast<float>(OuterIndex) / OuterCount * 2.0f * PI + 0.2f);
			Framed = Index == 0 ? FVector2D::ZeroVector :
				FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * (7200.0f + (OuterIndex % 2) * 2400.0f);
		}
		break;
	}
	return FramePositionToWorld(Framed, TerrainSeed);
}

FVector2D ResolveLandmarkTarget(
	const FWorldDirectorTerrainRecipe& Terrain,
	const FVector2D& CivicCore,
	const int32 TerrainSeed)
{
	const float Handedness = PlaceIdentityHandedness(TerrainSeed);
	FVector2D FramedCore = ToTerrainFrame(
		CivicCore / static_cast<float>(Terrain.ExtentCentimeters), TerrainSeed) *
		static_cast<float>(Terrain.ExtentCentimeters);
	FVector2D Offset;
	switch (Terrain.Archetype)
	{
	case EWorldDirectorTerrainArchetype::Valley:
		// Stay on the civic bank, but climb far enough up the terrace for the
		// landmark to terminate a visible street rather than disappear in town.
		Offset = FVector2D(Handedness * 8200.0f, Handedness * 4400.0f);
		break;
	case EWorldDirectorTerrainArchetype::Ridge:
		Offset = FVector2D(Handedness * 6800.0f, -Handedness * 6200.0f);
		break;
	case EWorldDirectorTerrainArchetype::Coast:
		// Positive terrain-frame X is inland: landmarks sit on the first bluff
		// and face back through the harbor settlement toward the shore.
		Offset = FVector2D(9800.0f, Handedness * 5800.0f);
		break;
	case EWorldDirectorTerrainArchetype::Marsh:
		Offset = FVector2D(Handedness * 6800.0f, Handedness * 5600.0f);
		break;
	case EWorldDirectorTerrainArchetype::Basin:
	default:
	{
		const float Angle = static_cast<float>(static_cast<uint32>(TerrainSeed) % 2048U) / 2048.0f * 2.0f * PI;
		Offset = FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * 11500.0f;
		break;
	}
	}
	FramedCore += Offset;
	return FramePositionToWorld(FramedCore, TerrainSeed);
}

float SampleLocalProminence(
	const FWorldDirectorTerrainRecipe& Terrain,
	const FVector2D& Position,
	const float Radius = 4800.0f)
{
	float SurroundingHeight = 0.0f;
	constexpr int32 SampleCount = 8;
	for (int32 Index = 0; Index < SampleCount; ++Index)
	{
		const float Angle = static_cast<float>(Index) / SampleCount * 2.0f * PI;
		const FVector2D Offset(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius);
		SurroundingHeight += FWorldDirectorPhysicalGenerator::SampleHeightCentimeters(Terrain, Position + Offset);
	}
	return FWorldDirectorPhysicalGenerator::SampleHeightCentimeters(Terrain, Position) -
		SurroundingHeight / SampleCount;
}

struct FPlotEvaluation
{
	float MeanHeight = 0.0f;
	float MaximumSlope = 0.0f;
	float HeightRange = 0.0f;
	bool bFlooded = false;
};

FPlotEvaluation EvaluatePlot(
	const FWorldDirectorTerrainRecipe& Terrain,
	const FVector2D& Center,
	const FVector2D& Footprint,
	const float YawDegrees)
{
	FPlotEvaluation Result;
	const float Radians = FMath::DegreesToRadians(YawDegrees);
	const FVector2D Forward(FMath::Cos(Radians), FMath::Sin(Radians));
	const FVector2D Right(-Forward.Y, Forward.X);
	const FVector2D Half = Footprint * 0.5f + FVector2D(180.0f);
	const FVector2D Samples[] = {
		FVector2D::ZeroVector,
		Forward * Half.X + Right * Half.Y, Forward * Half.X - Right * Half.Y,
		-Forward * Half.X + Right * Half.Y, -Forward * Half.X - Right * Half.Y,
		Forward * Half.X, -Forward * Half.X, Right * Half.Y, -Right * Half.Y
	};
	float Minimum = MAX_flt;
	float Maximum = -MAX_flt;
	for (const FVector2D& Offset : Samples)
	{
		const FVector2D Position = Center + Offset;
		const float Height = static_cast<float>(FWorldDirectorPhysicalGenerator::SampleHeightCentimeters(Terrain, Position));
		Result.MeanHeight += Height;
		Minimum = FMath::Min(Minimum, Height);
		Maximum = FMath::Max(Maximum, Height);
		Result.MaximumSlope = FMath::Max(Result.MaximumSlope,
			FWorldDirectorPhysicalGenerator::SampleSlopeDegrees(Terrain, Position));
		Result.bFlooded |= Terrain.WaterLevelCentimeters != INDEX_NONE &&
			Height <= Terrain.WaterLevelCentimeters + 90.0f;
	}
	Result.MeanHeight /= UE_ARRAY_COUNT(Samples);
	Result.HeightRange = Maximum - Minimum;
	return Result;
}

float DistanceToPolyline(const FVector2D& Point, const TArray<FVector>& Points)
{
	float Distance = MAX_flt;
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		Distance = FMath::Min(Distance, DistanceToSegment(
			Point, FVector2D(Points[Index - 1]), FVector2D(Points[Index])));
	}
	return Distance;
}

float DistanceOutsideOrientedRectangle(
	const FVector2D& Position,
	const FResolvedLocationPlan& Location,
	const float Padding)
{
	const FVector Local = Location.Transform.InverseTransformPosition(
		FVector(Position, Location.GroundHeightCentimeters));
	const float DX = FMath::Max(FMath::Abs(Local.X) - Location.FootprintSize.X * 0.5f - Padding, 0.0f);
	const float DY = FMath::Max(FMath::Abs(Local.Y) - Location.FootprintSize.Y * 0.5f - Padding, 0.0f);
	return FMath::Sqrt(DX * DX + DY * DY);
}

bool IsInsideLocationEnvelope(
	const FVector2D& Position,
	const TArray<FResolvedLocationPlan>& Locations,
	const float Padding)
{
	for (const FResolvedLocationPlan& Location : Locations)
	{
		if (DistanceOutsideOrientedRectangle(Position, Location, Padding) <= KINDA_SMALL_NUMBER)
		{
			return true;
		}
	}
	return false;
}

float LocationSurfaceMask(
	const FVector2D& Position,
	const TArray<FResolvedLocationPlan>& Locations)
{
	float Mask = 0.0f;
	for (const FResolvedLocationPlan& Location : Locations)
	{
		if (!Location.bPavedCourtyard)
		{
			continue;
		}
		const float Distance = DistanceOutsideOrientedRectangle(Position, Location, 120.0f);
		Mask = FMath::Max(Mask, 1.0f - SmoothRange(0.0f, 420.0f, Distance));
	}
	return Mask;
}

float LocationClearanceMask(
	const FVector2D& Position,
	const TArray<FResolvedLocationPlan>& Locations)
{
	float Mask = 0.0f;
	for (const FResolvedLocationPlan& Location : Locations)
	{
		const float Distance = DistanceOutsideOrientedRectangle(Position, Location, 120.0f);
		Mask = FMath::Max(Mask, 1.0f - SmoothRange(0.0f, 420.0f, Distance));
	}
	return Mask;
}

float DistanceToRoutes(
	const FVector2D& Position,
	const TArray<FResolvedRoutePlan>& Routes,
	const float WidthBias = 0.0f)
{
	float Distance = MAX_flt;
	for (const FResolvedRoutePlan& Route : Routes)
	{
		for (int32 Index = 1; Index < Route.ControlPoints.Num(); ++Index)
		{
			Distance = FMath::Min(Distance,
				DistanceToSegment(Position, FVector2D(Route.ControlPoints[Index - 1]),
					FVector2D(Route.ControlPoints[Index])) - Route.WidthCentimeters * 0.5f - WidthBias);
		}
	}
	return Distance;
}

TArray<FVector2D> FindTerrainRoute(
	const FWorldDirectorTerrainRecipe& Terrain,
	const FVector2D& Start,
	const FVector2D& End,
	const TArray<FBox2D>& Occupied,
	const int32 StartOccupiedIndex,
	const int32 EndOccupiedIndex)
{
	const int32 NodeCount = RouteGridResolution * RouteGridResolution;
	// Route resolution follows the settlement footprint instead of the complete
	// 1.2 km terrain. A 65-cell world grid produced 18.75 m staircase turns for
	// 3–7 m roads; the same bounded search now resolves typical lanes at 3–6 m.
	FVector2D RouteMin(FMath::Min(Start.X, End.X), FMath::Min(Start.Y, End.Y));
	FVector2D RouteMax(FMath::Max(Start.X, End.X), FMath::Max(Start.Y, End.Y));
	for (const FBox2D& Box : Occupied)
	{
		if (Box.bIsValid)
		{
			RouteMin.X = FMath::Min(RouteMin.X, Box.Min.X);
			RouteMin.Y = FMath::Min(RouteMin.Y, Box.Min.Y);
			RouteMax.X = FMath::Max(RouteMax.X, Box.Max.X);
			RouteMax.Y = FMath::Max(RouteMax.Y, Box.Max.Y);
		}
	}
	RouteMin -= FVector2D(6500.0f);
	RouteMax += FVector2D(6500.0f);
	RouteMin.X = FMath::Clamp(RouteMin.X, -static_cast<float>(Terrain.ExtentCentimeters),
		static_cast<float>(Terrain.ExtentCentimeters));
	RouteMin.Y = FMath::Clamp(RouteMin.Y, -static_cast<float>(Terrain.ExtentCentimeters),
		static_cast<float>(Terrain.ExtentCentimeters));
	RouteMax.X = FMath::Clamp(RouteMax.X, -static_cast<float>(Terrain.ExtentCentimeters),
		static_cast<float>(Terrain.ExtentCentimeters));
	RouteMax.Y = FMath::Clamp(RouteMax.Y, -static_cast<float>(Terrain.ExtentCentimeters),
		static_cast<float>(Terrain.ExtentCentimeters));
	const float StepX = FMath::Max(100.0f, (RouteMax.X - RouteMin.X) / (RouteGridResolution - 1));
	const float StepY = FMath::Max(100.0f, (RouteMax.Y - RouteMin.Y) / (RouteGridResolution - 1));
	auto ToGrid = [&](const FVector2D& Point)
	{
		return FIntPoint(
			FMath::Clamp(FMath::RoundToInt((Point.X - RouteMin.X) / StepX), 0, RouteGridResolution - 1),
			FMath::Clamp(FMath::RoundToInt((Point.Y - RouteMin.Y) / StepY), 0, RouteGridResolution - 1));
	};
	auto ToWorld = [&](const int32 Index)
	{
		const int32 X = Index % RouteGridResolution;
		const int32 Y = Index / RouteGridResolution;
		return FVector2D(RouteMin.X + X * StepX, RouteMin.Y + Y * StepY);
	};
	const FIntPoint StartGrid = ToGrid(Start);
	const FIntPoint EndGrid = ToGrid(End);
	const int32 StartIndex = StartGrid.Y * RouteGridResolution + StartGrid.X;
	const int32 EndIndex = EndGrid.Y * RouteGridResolution + EndGrid.X;
	TArray<float> Cost;
	Cost.Init(MAX_flt, NodeCount);
	TArray<int32> Previous;
	Previous.Init(INDEX_NONE, NodeCount);
	TArray<uint8> Closed;
	Closed.Init(0, NodeCount);
	TArray<uint8> InOpen;
	InOpen.Init(0, NodeCount);
	TArray<int32> Open;
	Open.Add(StartIndex);
	InOpen[StartIndex] = 1;
	Cost[StartIndex] = 0.0f;
	const FIntPoint NeighborOffsets[] = {
		FIntPoint(-1, -1), FIntPoint(0, -1), FIntPoint(1, -1), FIntPoint(-1, 0),
		FIntPoint(1, 0), FIntPoint(-1, 1), FIntPoint(0, 1), FIntPoint(1, 1)
	};
	while (!Open.IsEmpty())
	{
		int32 BestOpen = 0;
		float BestScore = MAX_flt;
		for (int32 OpenIndex = 0; OpenIndex < Open.Num(); ++OpenIndex)
		{
			const FVector2D Position = ToWorld(Open[OpenIndex]);
			const float Score = Cost[Open[OpenIndex]] + FVector2D::Distance(Position, End);
			if (Score < BestScore)
			{
				BestScore = Score;
				BestOpen = OpenIndex;
			}
		}
		const int32 Current = Open[BestOpen];
		Open.RemoveAtSwap(BestOpen, 1, EAllowShrinking::No);
		InOpen[Current] = 0;
		if (Current == EndIndex)
		{
			break;
		}
		if (Closed[Current])
		{
			continue;
		}
		Closed[Current] = 1;
		const int32 CurrentX = Current % RouteGridResolution;
		const int32 CurrentY = Current / RouteGridResolution;
		const FVector2D CurrentPosition = ToWorld(Current);
		const float CurrentHeight = FWorldDirectorPhysicalGenerator::SampleHeightCentimeters(Terrain, CurrentPosition);
		for (const FIntPoint& Offset : NeighborOffsets)
		{
			const int32 X = CurrentX + Offset.X;
			const int32 Y = CurrentY + Offset.Y;
			if (X < 1 || Y < 1 || X >= RouteGridResolution - 1 || Y >= RouteGridResolution - 1)
			{
				continue;
			}
			const int32 Neighbor = Y * RouteGridResolution + X;
			if (Closed[Neighbor])
			{
				continue;
			}
			const FVector2D Position = ToWorld(Neighbor);
			const float Horizontal = FVector2D::Distance(CurrentPosition, Position);
			const float Height = FWorldDirectorPhysicalGenerator::SampleHeightCentimeters(Terrain, Position);
			const float Grade = FMath::Abs(Height - CurrentHeight) / FMath::Max(1.0f, Horizontal);
			float StepCost = Horizontal * (1.0f + FMath::Square(Grade * 7.0f) * 11.0f);
			if (Terrain.WaterLevelCentimeters != INDEX_NONE && Height <= Terrain.WaterLevelCentimeters + 20.0f)
			{
				StepCost += 28000.0f;
			}
			bool bBlockedByPlot = false;
			for (int32 BoxIndex = 0; BoxIndex < Occupied.Num(); ++BoxIndex)
			{
				if (SegmentIntersectsBox2D(CurrentPosition, Position, Occupied[BoxIndex]))
				{
					bBlockedByPlot = true;
					break;
				}
			}
			if (bBlockedByPlot)
			{
				continue;
			}
			const float CandidateCost = Cost[Current] + StepCost;
			if (CandidateCost < Cost[Neighbor])
			{
				Cost[Neighbor] = CandidateCost;
				Previous[Neighbor] = Current;
				if (!InOpen[Neighbor])
				{
					Open.Add(Neighbor);
					InOpen[Neighbor] = 1;
				}
			}
		}
	}
	TArray<FVector2D> Reversed;
	const bool bFoundGridPath = EndIndex == StartIndex || Previous[EndIndex] != INDEX_NONE;
	if (bFoundGridPath)
	{
		for (int32 Current = EndIndex; Current != INDEX_NONE; Current = Previous[Current])
		{
			Reversed.Add(ToWorld(Current));
			if (Current == StartIndex)
			{
				break;
			}
		}
	}
	TArray<FVector2D> Path;
	Path.Add(Start);
	for (int32 Index = Reversed.Num() - 2; Index > 0; --Index)
	{
		Path.Add(Reversed[Index]);
	}
	Path.Add(End);
	if (Path.Num() < 3)
	{
		if (!bFoundGridPath)
		{
			UE_LOG(LogWorldDirector, Warning,
				TEXT("WORLD_DIRECTOR_ROUTE_GRID_FALLBACK start=(%.0f,%.0f) end=(%.0f,%.0f) occupied=%d"),
				Start.X, Start.Y, End.X, End.Y, Occupied.Num());
		}
		Path = {Start, FMath::Lerp(Start, End, 0.33f), FMath::Lerp(Start, End, 0.66f), End};
	}
	const TArray<FVector2D> SafeGridPath = Path;
	auto DistanceToSafeGridPath = [&](const FVector2D& Position)
	{
		float Distance = MAX_flt;
		for (int32 Index = 1; Index < SafeGridPath.Num(); ++Index)
		{
			Distance = FMath::Min(Distance,
				DistanceToSegment(Position, SafeGridPath[Index - 1], SafeGridPath[Index]));
		}
		return Distance;
	};
	auto IsSafeRefinedSegment = [&](const FVector2D& A, const FVector2D& B)
	{
		for (int32 BoxIndex = 0; BoxIndex < Occupied.Num(); ++BoxIndex)
		{
			if (SegmentIntersectsBox2D(A, B, Occupied[BoxIndex]))
			{
				return false;
			}
		}
		const int32 SampleCount = FMath::Max(1,
			FMath::CeilToInt(FVector2D::Distance(A, B) / 100.0f));
		for (int32 SampleIndex = 0; SampleIndex <= SampleCount; ++SampleIndex)
		{
			const FVector2D Sample = FMath::Lerp(
				A, B, static_cast<float>(SampleIndex) / SampleCount);
			const float Height = FWorldDirectorPhysicalGenerator::SampleHeightCentimeters(Terrain, Sample);
			if ((Terrain.WaterLevelCentimeters != INDEX_NONE &&
				Height <= Terrain.WaterLevelCentimeters + 20.0f) ||
				DistanceToSafeGridPath(Sample) > FMath::Max(StepX, StepY) * 1.15f)
			{
				return false;
			}
		}
		return true;
	};
	// Collapse alternate A* samples only when the new chord preserves the exact
	// water/plot corridor. Keeping every grid node reintroduced staircase miters;
	// unconditional decimation was the earlier source of corner-cutting.
	if (Path.Num() > 2)
	{
		TArray<FVector2D> Simplified;
		Simplified.Reserve(Path.Num());
		Simplified.Add(Path[0]);
		int32 Index = 0;
		while (Index < Path.Num() - 1)
		{
			const int32 TwoAhead = FMath::Min(Index + 2, Path.Num() - 1);
			const int32 Next = TwoAhead > Index + 1 &&
				IsSafeRefinedSegment(Path[Index], Path[TwoAhead])
				? TwoAhead : Index + 1;
			Simplified.Add(Path[Next]);
			Index = Next;
		}
		Path = MoveTemp(Simplified);
	}
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		TArray<FVector2D> Smoothed = Path;
		for (int32 Index = 1; Index < Path.Num() - 1; ++Index)
		{
			const FVector2D Candidate =
				(Path[Index - 1] + Path[Index] * 2.0f + Path[Index + 1]) * 0.25f;
			if (IsSafeRefinedSegment(Path[Index - 1], Candidate) &&
				IsSafeRefinedSegment(Candidate, Path[Index + 1]))
			{
				Smoothed[Index] = Candidate;
			}
		}
		Path = MoveTemp(Smoothed);
	}
	// Convert the safe grid route into a rounded centerline. Chaikin subdivision
	// stays inside each pair of A* segments, so it removes staircase silhouettes
	// without inventing large shortcuts through water or building envelopes.
	for (int32 Pass = 0; Pass < 2 && Path.Num() >= 3; ++Pass)
	{
		TArray<FVector2D> Refined;
		Refined.Reserve(Path.Num() * 2);
		Refined.Add(Path[0]);
		for (int32 Index = 1; Index < Path.Num(); ++Index)
		{
			Refined.Add(FMath::Lerp(Path[Index - 1], Path[Index], 0.25f));
			Refined.Add(FMath::Lerp(Path[Index - 1], Path[Index], 0.75f));
		}
		Refined.Add(Path.Last());
		bool bRefinedPathSafe = true;
		for (int32 Index = 1; Index < Refined.Num(); ++Index)
		{
			if (!IsSafeRefinedSegment(Refined[Index - 1], Refined[Index]))
			{
				bRefinedPathSafe = false;
				break;
			}
		}
		if (!bRefinedPathSafe)
		{
			break;
		}
		Path = MoveTemp(Refined);
	}
	Path[0] = Start;
	Path.Last() = End;
	return Path;
}

const FWorldEnvironmentDressingAsset* PickDressingAsset(
	const UWorldEnvironmentProfile* Profile,
	const FName PlacementTag,
	FRandomStream& Random)
{
	if (Profile == nullptr)
	{
		return nullptr;
	}
	float TotalWeight = 0.0f;
	for (const FWorldEnvironmentDressingAsset& Asset : Profile->DressingAssets)
	{
		if (Asset.PlacementTag == PlacementTag)
		{
			TotalWeight += Asset.Weight;
		}
	}
	float Choice = Random.FRandRange(0.0f, TotalWeight);
	for (const FWorldEnvironmentDressingAsset& Asset : Profile->DressingAssets)
	{
		if (Asset.PlacementTag == PlacementTag && (Choice -= Asset.Weight) <= 0.0f)
		{
			return &Asset;
		}
	}
	return nullptr;
}

bool IsInsidePolygon(const FVector2D& Position, const TArray<FVector2D>& Boundary)
{
	if (Boundary.Num() < 3)
	{
		return false;
	}
	bool bInside = false;
	for (int32 Index = 0, Previous = Boundary.Num() - 1; Index < Boundary.Num(); Previous = Index++)
	{
		const FVector2D& A = Boundary[Index];
		const FVector2D& B = Boundary[Previous];
		const bool bCrossesScanline = (A.Y > Position.Y) != (B.Y > Position.Y);
		if (bCrossesScanline)
		{
			const float CrossingX = (B.X - A.X) * (Position.Y - A.Y) /
				(B.Y - A.Y) + A.X;
			bInside ^= Position.X < CrossingX;
		}
	}
	return bInside;
}

float DistanceToPolygonBoundary(const FVector2D& Position, const TArray<FVector2D>& Boundary)
{
	float Distance = MAX_flt;
	for (int32 Index = 0; Index < Boundary.Num(); ++Index)
	{
		Distance = FMath::Min(Distance, DistanceToSegment(
			Position, Boundary[Index], Boundary[(Index + 1) % Boundary.Num()]));
	}
	return Distance;
}

float FarmParcelMask(
	const FVector2D& Position,
	const TArray<FWorldDirectorFarmParcel>& Parcels)
{
	float Mask = 0.0f;
	for (const FWorldDirectorFarmParcel& Parcel : Parcels)
	{
		if (IsInsidePolygon(Position, Parcel.BoundaryPoints))
		{
			// A narrow grass-to-soil transition preserves an authored field edge while
			// avoiding the obvious airbrushed rectangles produced by the terrain lattice.
			Mask = FMath::Max(Mask, SmoothRange(
				0.0f, 520.0f, DistanceToPolygonBoundary(Position, Parcel.BoundaryPoints)));
		}
	}
	return Mask;
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
	InOutPlan.Version = 3;
	InOutPlan.GeneratorVersion = TEXT("worldgen-physical-v4");
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
	InOutPlan.StageSeeds.Reset();
	InOutPlan.DistrictAnchors.Reset();
	InOutPlan.Routes.Reset();
	InOutPlan.Dressing.Reset();
	InOutPlan.LayoutFingerprint.Reset();
	InOutPlan.RouteFingerprint.Reset();
	InOutPlan.DressingFingerprint.Reset();
	InOutPlan.WorldFingerprint.Reset();
	if (FingerprintBytes(TArray<uint8>()).IsEmpty())
	{
		InOutReport.AddError(TEXT("generator.crypto_unavailable"), TEXT("fingerprints"),
			TEXT("The platform SHA-256 provider is unavailable; deterministic generation cannot continue."));
		return false;
	}
	for (const FString& Stage : {TEXT("terrain"), TEXT("erosion"), TEXT("hydrology"), TEXT("districts"),
		TEXT("plots"), TEXT("roads"), TEXT("surfaces"), TEXT("dressing")})
	{
		InOutPlan.StageSeeds.Add(Stage, DeriveStageSeed(Spec.Seed, Stage));
	}

	FWorldDirectorTerrainRecipe& Terrain = InOutPlan.Terrain;
	Terrain = FWorldDirectorTerrainRecipe();
	Terrain.Resolution = TerrainResolution;
	Terrain.ExtentCentimeters = TerrainExtent;
	Terrain.Archetype = ResolveArchetype(Spec.Brief, Spec.Seed);
	Terrain.SettlementMorphology = ResolveSettlementMorphology(Terrain.Archetype, Spec.Seed);
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
	// A few conservative thermal passes remove high-frequency spikes while
	// retaining the seed-specific macro silhouette and carved channels.
	ApplyThermalErosion(Terrain, 4);
	MinimumHeight = MAX_int32;
	MaximumHeight = MIN_int32;
	for (const int32 Height : Terrain.HeightsCentimeters)
	{
		MinimumHeight = FMath::Min(MinimumHeight, Height);
		MaximumHeight = FMath::Max(MaximumHeight, Height);
	}
	Terrain.MinimumHeightCentimeters = MinimumHeight;
	Terrain.MaximumHeightCentimeters = MaximumHeight;
	if (Terrain.Archetype == EWorldDirectorTerrainArchetype::Coast)
	{
		Terrain.WaterLevelCentimeters = 0;
		for (int32 Index = 0; Index <= 12; ++Index)
		{
			const float Along = -1.0f + 2.0f * Index / 12.0f;
			const float Across = -0.2f + RiverCenterInFrame(Along, TerrainSeed) * 1.25f;
			const FVector2D World = FromTerrainFrame(FVector2D(Across, Along), TerrainSeed) * TerrainExtent;
			Terrain.WaterControlPoints.Add(FVector(World, Terrain.WaterLevelCentimeters));
		}
	}
	else if (Terrain.Archetype == EWorldDirectorTerrainArchetype::Valley || Terrain.Archetype == EWorldDirectorTerrainArchetype::Marsh)
	{
		Terrain.WaterLevelCentimeters = Terrain.Archetype == EWorldDirectorTerrainArchetype::Marsh ? 55 : -90;
		for (int32 Index = 0; Index <= 12; ++Index)
		{
			const float Along = -1.0f + 2.0f * Index / 12.0f;
			const float Across = RiverCenterInFrame(Along, TerrainSeed) *
				(Terrain.Archetype == EWorldDirectorTerrainArchetype::Marsh ? 1.9f : 1.0f);
			const FVector2D World = FromTerrainFrame(FVector2D(Across, Along), TerrainSeed) * TerrainExtent;
			Terrain.WaterControlPoints.Add(FVector(World, Terrain.WaterLevelCentimeters));
		}
	}
	const FString ArchetypeDescription = Terrain.Archetype == EWorldDirectorTerrainArchetype::Valley
		? TEXT("a river-cut valley whose roads follow the lower terraces")
		: Terrain.Archetype == EWorldDirectorTerrainArchetype::Ridge
			? TEXT("a broken highland ridge with defensible shelves and long views")
			: Terrain.Archetype == EWorldDirectorTerrainArchetype::Coast
				? TEXT("a storm coast rising from a connected shoreline")
				: Terrain.Archetype == EWorldDirectorTerrainArchetype::Marsh
					? TEXT("a wetland of raised hummocks, channels, and causeways")
					: TEXT("a sheltered basin enclosed by an asymmetric mountain rim");
	const FString SpatialGrammarDescription = ResolveSpatialGrammarDescription(
		Terrain.Archetype, Terrain.SettlementMorphology);
	const FString StoryMotifDescription = ResolveStoryMotifDescription(Terrain.Archetype);
	Terrain.EnvironmentalStory = FString::Printf(
		TEXT("%s uses %s. Its settlement reads as %s, and %s. The land records %s; its present spatial pressure is %s."),
		*Spec.Brief.SettlementIdentity,
		*ArchetypeDescription,
		*SpatialGrammarDescription,
		*StoryMotifDescription,
		Spec.Topology.HistoricalWound.IsEmpty() ? TEXT("an older, unnamed rupture") : *Spec.Topology.HistoricalWound,
		Spec.Topology.CurrentTension.IsEmpty() ? TEXT("control of the safest routes and buildable ground") : *Spec.Topology.CurrentTension);
	double TotalSlope = 0.0;

	FRandomStream DistrictRandom(InOutPlan.StageSeeds[TEXT("districts")]);
	const int32 DistrictCount = FMath::Clamp(Spec.Topology.Districts.Num(), 2, 5);
	for (int32 Index = 0; Index < DistrictCount; ++Index)
	{
		const FVector2D Target = MorphologyTarget(Terrain.Archetype, Terrain.SettlementMorphology,
			Index, DistrictCount, TerrainSeed);
		FVector2D BestPosition = Target;
		float BestScore = MAX_flt;
		for (int32 Attempt = 0; Attempt < 520; ++Attempt)
		{
			const float SearchRadius = Attempt < 80 ? 5500.0f : 12500.0f;
			const float Angle = DistrictRandom.FRandRange(-PI, PI);
			const FVector2D Candidate = Target + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) *
				DistrictRandom.FRandRange(0.0f, SearchRadius);
			if (FMath::Max(FMath::Abs(Candidate.X), FMath::Abs(Candidate.Y)) > TerrainExtent - 9000.0f)
			{
				continue;
			}
			const float Height = SampleHeightCentimeters(Terrain, Candidate);
			const float Slope = SampleSlopeDegrees(Terrain, Candidate);
			if (Slope > 12.5f || (Terrain.WaterLevelCentimeters != INDEX_NONE &&
				Height <= Terrain.WaterLevelCentimeters + 140.0f))
			{
				continue;
			}
			float SeparationPenalty = 0.0f;
			for (const FWorldDirectorDistrictAnchor& Existing : InOutPlan.DistrictAnchors)
			{
				const float Separation = FVector2D::Distance(Candidate, FVector2D(Existing.Position));
				if (Separation < 4400.0f)
				{
					SeparationPenalty += (4400.0f - Separation) * 4.0f;
				}
			}
			float AffinityPenalty = 0.0f;
			const float WaterDistance = DistanceToPolyline(Candidate, Terrain.WaterControlPoints);
			if (Terrain.Archetype == EWorldDirectorTerrainArchetype::Valley && WaterDistance < MAX_flt)
			{
				AffinityPenalty += FMath::Abs(WaterDistance - 7200.0f) * 0.4f;
			}
			else if (Terrain.Archetype == EWorldDirectorTerrainArchetype::Coast && WaterDistance < MAX_flt)
			{
				AffinityPenalty += FMath::Abs(WaterDistance - 8500.0f) * 0.3f;
			}
			const float Score = FVector2D::Distance(Candidate, Target) * 0.5f + Slope * 900.0f +
				SeparationPenalty + AffinityPenalty;
			if (Score < BestScore)
			{
				BestScore = Score;
				BestPosition = Candidate;
			}
		}
		FWorldDirectorDistrictAnchor& Anchor = InOutPlan.DistrictAnchors.AddDefaulted_GetRef();
		Anchor.DistrictId = Spec.Topology.Districts.IsValidIndex(Index)
			? Spec.Topology.Districts[Index] : FString::Printf(TEXT("district.%d"), Index);
		Anchor.Position = FVector(BestPosition, SampleHeightCentimeters(Terrain, BestPosition));
		Anchor.InfluenceRadiusCentimeters = Index == 0
			? DistrictRandom.FRandRange(7200.0f, 9000.0f)
			: DistrictRandom.FRandRange(5200.0f, 7200.0f);
		Anchor.TerrainAffinity = ResolveDistrictAffinity(
			Terrain.Archetype, Terrain.SettlementMorphology, Index);
	}

	FRandomStream PlotRandom(InOutPlan.StageSeeds[TEXT("plots")]);
	TArray<FBox2D> Occupied;
	TMap<FString, int32> OccupiedBoxIndices;
	TArray<int32> LocationOrder;
	for (int32 Index = 0; Index < InOutPlan.Locations.Num(); ++Index)
	{
		LocationOrder.Add(Index);
	}
	auto FindSemanticLocation = [&](const FString& LocationId) -> const FWorldLocation*
	{
		return Spec.Locations.FindByPredicate(
			[&LocationId](const FWorldLocation& Location) { return Location.Id == LocationId; });
	};
	LocationOrder.Sort([&](const int32 A, const int32 B)
	{
		const FResolvedLocationPlan& LocationA = InOutPlan.Locations[A];
		const FResolvedLocationPlan& LocationB = InOutPlan.Locations[B];
		auto Priority = [&](const FResolvedLocationPlan& Location)
		{
			if (Location.LocationId == InOutPlan.LandmarkLocationId)
			{
				return 0;
			}
			const FWorldLocation* Semantic = FindSemanticLocation(Location.LocationId);
			return Semantic != nullptr && Semantic->PurposeTag == TEXT("Purpose.Home") ? 2 : 1;
		};
		const int32 PriorityA = Priority(LocationA);
		const int32 PriorityB = Priority(LocationB);
		return PriorityA == PriorityB ? LocationA.LocationId < LocationB.LocationId : PriorityA < PriorityB;
	});
	const FVector2D CivicCore(InOutPlan.DistrictAnchors[0].Position);
	const FVector2D PreferredLandmarkTarget = ResolveLandmarkTarget(Terrain, CivicCore, TerrainSeed);
	TMap<FString, int32> PlacedLocationIndices;
	int32 HomeOrdinal = static_cast<int32>(static_cast<uint32>(
		InOutPlan.StageSeeds[TEXT("districts")]) % FMath::Max(1, InOutPlan.DistrictAnchors.Num() - 1));
	for (const int32 LocationIndex : LocationOrder)
	{
		FResolvedLocationPlan& Location = InOutPlan.Locations[LocationIndex];
		const bool bLandmark = Location.LocationId == InOutPlan.LandmarkLocationId;
		const FWorldLocation* SemanticLocation = FindSemanticLocation(Location.LocationId);
		const bool bHome = SemanticLocation != nullptr && SemanticLocation->PurposeTag == TEXT("Purpose.Home");
		const FString Purpose = SemanticLocation != nullptr ? SemanticLocation->PurposeTag.ToString() : FString();
		const bool bCivicPurpose = SemanticLocation != nullptr &&
			(Purpose.Contains(TEXT("Landmark")) || Purpose.Contains(TEXT("Clinic")) ||
			 Purpose.Contains(TEXT("Shelter")) || Purpose.Contains(TEXT("Headquarters")));
		const int32 StableDistrict = FMath::Abs(DeriveStageSeed(
			InOutPlan.StageSeeds[TEXT("districts")], Location.LocationId)) % InOutPlan.DistrictAnchors.Num();
		const int32 HomeDistrict = InOutPlan.DistrictAnchors.Num() > 1
			? 1 + HomeOrdinal % (InOutPlan.DistrictAnchors.Num() - 1) : 0;
		const int32 DistrictIndex = bLandmark || bCivicPurpose ? 0 : bHome && InOutPlan.DistrictAnchors.Num() > 1
			? HomeDistrict
			: StableDistrict % FMath::Min(2, InOutPlan.DistrictAnchors.Num());
		HomeOrdinal += bHome ? 1 : 0;
		const FWorldDirectorDistrictAnchor& District = InOutPlan.DistrictAnchors[DistrictIndex];
		bool bPlaced = false;
		float BestScore = MAX_flt;
		FVector2D BestCandidate = FVector2D::ZeroVector;
		float BestYaw = 0.0f;
		FPlotEvaluation BestEvaluation;
		const float SlopeLimit = bLandmark
			? Profile->MaximumPlotSlopeDegrees + 5.0f : Profile->MaximumPlotSlopeDegrees;
		const float VarianceLimit = bLandmark ? 520.0f : 330.0f;
		auto ConsiderCandidate = [&](const FVector2D& Candidate, const float Yaw, const bool bFallback)
		{
			if (FMath::Max(FMath::Abs(Candidate.X), FMath::Abs(Candidate.Y)) > TerrainExtent - 8500.0f ||
				OverlapsAny(Candidate, Location.FootprintSize, Yaw, Occupied))
			{
				return;
			}
			const FPlotEvaluation Evaluation = EvaluatePlot(Terrain, Candidate, Location.FootprintSize, Yaw);
			// The exhaustive fallback may use a slightly stronger cut/fill pad, but it
			// never accepts water or a genuinely steep footprint. This prevents wet
			// archetypes from failing merely because their dry hummocks occupy a small
			// fraction of the random search radius.
			const float AllowedSlope = SlopeLimit + (bFallback && !bLandmark ? 2.0f : 0.0f);
			const float AllowedVariance = VarianceLimit + (bFallback && !bLandmark ? 140.0f : 0.0f);
			if (Evaluation.MaximumSlope > AllowedSlope ||
				Evaluation.HeightRange > AllowedVariance || Evaluation.bFlooded)
			{
				return;
			}
			float TopologyPenalty = 0.0f;
			for (const FTownTopologyEdge& Edge : Spec.Topology.Edges)
			{
				const FString OtherId = Edge.FromLocationId == Location.LocationId ? Edge.ToLocationId :
					Edge.ToLocationId == Location.LocationId ? Edge.FromLocationId : FString();
				if (const int32* OtherIndex = PlacedLocationIndices.Find(OtherId))
				{
					const float Distance = FVector2D::Distance(Candidate,
						FVector2D(InOutPlan.Locations[*OtherIndex].Transform.GetLocation()));
					TopologyPenalty += FMath::Abs(
						Distance - (Edge.RouteType == TEXT("Road") ? 5200.0f : 3600.0f)) * 0.42f;
				}
			}
			const float LocalProminence = SampleLocalProminence(Terrain, Candidate);
			const float ApproachDistance = FVector2D::Distance(Candidate, CivicCore);
			const float ApproachGrade = FMath::Abs(Evaluation.MeanHeight -
				SampleHeightCentimeters(Terrain, CivicCore)) / FMath::Max(1.0f, ApproachDistance);
			float LandmarkAffinityPenalty = 0.0f;
			if (bLandmark && Terrain.Archetype == EWorldDirectorTerrainArchetype::Coast)
			{
				const float WaterDistance = DistanceToPolyline(Candidate, Terrain.WaterControlPoints);
				LandmarkAffinityPenalty = WaterDistance < MAX_flt
					? FMath::Abs(WaterDistance - 15000.0f) * 0.24f : 0.0f;
			}
			const float Score = Evaluation.MaximumSlope * 850.0f + Evaluation.HeightRange * 3.5f +
				FVector2D::Distance(Candidate, bLandmark ? PreferredLandmarkTarget : FVector2D(District.Position)) *
					(bLandmark ? 0.58f : 0.18f) + TopologyPenalty + LandmarkAffinityPenalty +
				(bLandmark ? ApproachGrade * 22000.0f - LocalProminence * 1.35f : 0.0f);
			if (Score < BestScore)
			{
				BestScore = Score;
				BestCandidate = Candidate;
				BestYaw = Yaw;
				BestEvaluation = Evaluation;
				bPlaced = true;
			}
		};
		for (int32 Attempt = 0; Attempt < PlotAttempts; ++Attempt)
		{
			const float Angle = PlotRandom.FRandRange(-PI, PI);
			const bool bGlobalFallback = Attempt >= 900;
			const float Radius = bLandmark
				? PlotRandom.FRandRange(0.0f, Attempt < 1200 ? 7200.0f : 12800.0f)
				: PlotRandom.FRandRange(1050.0f, bGlobalFallback ? 14000.0f : District.InfluenceRadiusCentimeters);
			const FVector2D Origin = bLandmark ? PreferredLandmarkTarget : bGlobalFallback
				? CivicCore : FVector2D(District.Position);
			const FVector2D Candidate = Origin + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
			const FVector2D FacingTarget = bLandmark ? CivicCore : FVector2D(District.Position);
			const FVector2D Facing = (FacingTarget - Candidate).GetSafeNormal();
			const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Facing.Y, Facing.X)) + 90.0f;
			ConsiderCandidate(Candidate, Yaw, false);
		}
		if (!bPlaced)
		{
			// Search the complete buildable envelope on a deterministic terrain-grid
			// lattice. The primary search remains organic and district-biased; this
			// pass is a bounded guarantee for sparse dry land such as reed marshes.
			constexpr int32 FallbackStride = 2;
			const int32 MarginSamples = FMath::CeilToInt(
				8500.0f / (static_cast<float>(TerrainDiameter) / (TerrainResolution - 1)));
			const int32 LocationSeed = DeriveStageSeed(
				InOutPlan.StageSeeds[TEXT("plots")], Location.LocationId);
			const int32 XParity = FMath::Abs(LocationSeed) % FallbackStride;
			const int32 YParity = FMath::Abs(LocationSeed / FallbackStride) % FallbackStride;
			for (int32 GridY = MarginSamples + YParity;
				GridY < TerrainResolution - MarginSamples; GridY += FallbackStride)
			{
				for (int32 GridX = MarginSamples + XParity;
					GridX < TerrainResolution - MarginSamples; GridX += FallbackStride)
				{
					const FVector2D Candidate(
						-TerrainExtent + static_cast<float>(GridX) / (TerrainResolution - 1) * TerrainDiameter,
						-TerrainExtent + static_cast<float>(GridY) / (TerrainResolution - 1) * TerrainDiameter);
					const FVector2D FacingTarget = bLandmark ? CivicCore : FVector2D(District.Position);
					const FVector2D Facing = (FacingTarget - Candidate).GetSafeNormal();
					const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Facing.Y, Facing.X)) + 90.0f;
					ConsiderCandidate(Candidate, Yaw, true);
				}
			}
		}
		if (!bPlaced)
		{
			InOutReport.AddError(TEXT("generator.plot_unsatisfied"), FString::Printf(TEXT("locations[%d]"), LocationIndex),
				TEXT("Terrain-aware plot search exhausted its deterministic candidate budget."));
			return false;
		}
		Location.Transform = FTransform(FRotator(0.0f, BestYaw, 0.0f),
			FVector(BestCandidate, BestEvaluation.MeanHeight + 5.0f));
		Location.DistrictId = District.DistrictId;
		Location.GroundHeightCentimeters = FMath::RoundToInt(BestEvaluation.MeanHeight);
		Location.GroundSlopeDegrees = BestEvaluation.MaximumSlope;
		const FVector EntranceOffset(0.0f, -(Location.FootprintSize.Y * 0.5f + 130.0f), 0.0f);
		FVector Entrance = Location.Transform.GetLocation() + Location.Transform.GetRotation().RotateVector(EntranceOffset);
		Entrance.Z = SampleHeightCentimeters(Terrain, FVector2D(Entrance)) + 8.0f;
		Location.EntranceTransform = FTransform(Location.Transform.GetRotation(), Entrance);
		const float Radians = FMath::DegreesToRadians(BestYaw);
		const float AbsCos = FMath::Abs(FMath::Cos(Radians));
		const float AbsSin = FMath::Abs(FMath::Sin(Radians));
		const FVector2D Half = Location.FootprintSize * 0.5f;
		const float ReservationPadding = bLandmark ? 1150.0f : bHome ? 650.0f : 800.0f;
		const FVector2D ReservedExtent(
			AbsCos * Half.X + AbsSin * Half.Y + ReservationPadding,
			AbsSin * Half.X + AbsCos * Half.Y + ReservationPadding);
		const int32 OccupiedIndex = Occupied.Add(
			FBox2D(BestCandidate - ReservedExtent, BestCandidate + ReservedExtent));
		OccupiedBoxIndices.Add(Location.LocationId, OccupiedIndex);
		PlacedLocationIndices.Add(Location.LocationId, LocationIndex);
	}

	// Grade compact pads with a broad, slope-limited cut/fill skirt. Placement
	// reserved the same envelope, so one building can no longer overwrite the
	// terrain solution of its neighbor.
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
				const float OutsideDistance = DistanceOutsideOrientedRectangle(Position, Location, 180.0f);
				if (OutsideDistance <= 900.0f)
				{
					const float Alpha = 1.0f - SmoothRange(0.0f, 900.0f, OutsideDistance);
					Height = FMath::Lerp(Height, static_cast<float>(Location.GroundHeightCentimeters), Alpha);
				}
			}
			Terrain.HeightsCentimeters[Y * TerrainResolution + X] = FMath::RoundToInt(Height);
		}
	}
	MinimumHeight = MAX_int32;
	MaximumHeight = MIN_int32;
	for (const int32 Height : Terrain.HeightsCentimeters)
	{
		MinimumHeight = FMath::Min(MinimumHeight, Height);
		MaximumHeight = FMath::Max(MaximumHeight, Height);
	}
	Terrain.MinimumHeightCentimeters = MinimumHeight;
	Terrain.MaximumHeightCentimeters = MaximumHeight;
	for (FResolvedLocationPlan& Location : InOutPlan.Locations)
	{
		Location.GroundHeightCentimeters = SampleHeightCentimeters(
			Terrain, FVector2D(Location.Transform.GetLocation()));
		Location.GroundSlopeDegrees = SampleSlopeDegrees(Terrain, FVector2D(Location.Transform.GetLocation()));
		FVector LocationPosition = Location.Transform.GetLocation();
		LocationPosition.Z = Location.GroundHeightCentimeters + 5.0f;
		Location.Transform.SetLocation(LocationPosition);
		FVector Entrance = Location.EntranceTransform.GetLocation();
		Entrance.Z = SampleHeightCentimeters(Terrain, FVector2D(Entrance)) + 8.0f;
		Location.EntranceTransform.SetLocation(Entrance);
	}

	InOutPlan.Routes.Reset();
	TSet<FString> RoutePairs;
	auto ResolveApproachGate = [&](const FResolvedLocationPlan& Location, const float RequestedDistance)
	{
		const FVector2D Entrance(Location.EntranceTransform.GetLocation());
		const FVector2D Center(Location.Transform.GetLocation());
		const FVector2D Outward = (Entrance - Center).GetSafeNormal();
		if (Outward.IsNearlyZero())
		{
			return Entrance;
		}
		const int32* OwnOccupiedIndexPtr = OccupiedBoxIndices.Find(Location.LocationId);
		const int32 OwnOccupiedIndex = OwnOccupiedIndexPtr != nullptr
			? *OwnOccupiedIndexPtr : INDEX_NONE;
		// The A* route begins at this gate, so it must be fully outside the
		// location's reserved envelope. Trying longer exits before shorter
		// fallbacks prevents a compact settlement from letting the path finder
		// wander through its own large public-building footprint.
		const float DistanceScales[] = {1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 0.75f, 0.5f, 0.35f};
		for (const float DistanceScale : DistanceScales)
		{
			const FVector2D Candidate = Entrance + Outward * RequestedDistance * DistanceScale;
			const int32 Height = SampleHeightCentimeters(Terrain, Candidate);
			const bool bInsideOwnPlot = OwnOccupiedIndex != INDEX_NONE &&
				Occupied.IsValidIndex(OwnOccupiedIndex) &&
				Occupied[OwnOccupiedIndex].IsInside(Candidate);
			bool bCutsNeighborPlot = false;
			for (int32 BoxIndex = 0; BoxIndex < Occupied.Num(); ++BoxIndex)
			{
				if (BoxIndex != OwnOccupiedIndex &&
					SegmentIntersectsBox2D(Entrance, Candidate, Occupied[BoxIndex]))
				{
					bCutsNeighborPlot = true;
					break;
				}
			}
			if (FMath::Max(FMath::Abs(Candidate.X), FMath::Abs(Candidate.Y)) < TerrainExtent - 1800.0f &&
				SampleSlopeDegrees(Terrain, Candidate) <= 22.0f &&
				(Terrain.WaterLevelCentimeters == INDEX_NONE || Height > Terrain.WaterLevelCentimeters + 45) &&
				!bInsideOwnPlot && !bCutsNeighborPlot)
			{
				return Candidate;
			}
		}
		return Entrance;
	};
	FString PrimaryLandmarkNeighbor;
	bool bPrimaryLandmarkIsRoad = false;
	int32 UnstableApproachJoinCount = 0;
	int32 SharedNetworkJunctionCount = 0;
	for (const FTownTopologyEdge& Edge : Spec.Topology.Edges)
	{
		const FString Neighbor = Edge.FromLocationId == InOutPlan.LandmarkLocationId
			? Edge.ToLocationId : Edge.ToLocationId == InOutPlan.LandmarkLocationId
				? Edge.FromLocationId : FString();
		if (Neighbor.IsEmpty())
		{
			continue;
		}
		const bool bRoad = Edge.RouteType == TEXT("Road");
		if (PrimaryLandmarkNeighbor.IsEmpty() || (bRoad && !bPrimaryLandmarkIsRoad) ||
			(bRoad == bPrimaryLandmarkIsRoad && Neighbor < PrimaryLandmarkNeighbor))
		{
			PrimaryLandmarkNeighbor = Neighbor;
			bPrimaryLandmarkIsRoad = bRoad;
		}
	}
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
		const FVector Start = From->EntranceTransform.GetLocation();
		const FVector End = To->EntranceTransform.GetLocation();
		const bool bLandmarkApproach =
			(FromId == InOutPlan.LandmarkLocationId && ToId == PrimaryLandmarkNeighbor) ||
			(ToId == InOutPlan.LandmarkLocationId && FromId == PrimaryLandmarkNeighbor);
		const float RouteWidth = bLandmarkApproach ? 720.0f : RouteType == TEXT("Road") ? 480.0f : 280.0f;
		const FVector2D Start2D(Start);
		const FVector2D SemanticEnd2D(End);
		const int32* StartOccupiedIndexPtr = OccupiedBoxIndices.Find(FromId);
		const int32* SemanticEndOccupiedIndexPtr = OccupiedBoxIndices.Find(ToId);
		const int32 StartOccupiedIndex = StartOccupiedIndexPtr != nullptr
			? *StartOccupiedIndexPtr : INDEX_NONE;
		const int32 SemanticEndOccupiedIndex = SemanticEndOccupiedIndexPtr != nullptr
			? *SemanticEndOccupiedIndexPtr : INDEX_NONE;
		FVector2D PhysicalEnd2D = SemanticEnd2D;
		bool bEndsAtSharedNetwork = false;
		if (!bLandmarkApproach && !InOutPlan.Routes.IsEmpty())
		{
			const FResolvedLocationPlan* Landmark = InOutPlan.Locations.FindByPredicate(
				[&](const FResolvedLocationPlan& Location)
				{
					return Location.LocationId == InOutPlan.LandmarkLocationId;
				});
			const FVector2D LandmarkEntrance = Landmark
				? FVector2D(Landmark->EntranceTransform.GetLocation()) : FVector2D::ZeroVector;
			float BestNetworkDistance = MAX_flt;
			for (const FResolvedRoutePlan& ExistingRoute : InOutPlan.Routes)
			{
				// Joining a route that already terminates at either semantic endpoint
				// can turn the new branch back through that endpoint's own yard. It is
				// also redundant connectivity, so reserve shared junctions for a
				// genuinely independent street segment.
				if (ExistingRoute.FromLocationId == FromId || ExistingRoute.ToLocationId == FromId ||
					ExistingRoute.FromLocationId == ToId || ExistingRoute.ToLocationId == ToId)
				{
					continue;
				}
				for (int32 SegmentIndex = 1;
					SegmentIndex < ExistingRoute.ControlPoints.Num(); ++SegmentIndex)
				{
					const FVector2D A(ExistingRoute.ControlPoints[SegmentIndex - 1]);
					const FVector2D B(ExistingRoute.ControlPoints[SegmentIndex]);
					const FVector2D Delta = B - A;
					const float Denominator = Delta.SizeSquared();
					if (Denominator <= KINDA_SMALL_NUMBER)
					{
						continue;
					}
					const float T = FMath::Clamp(
						FVector2D::DotProduct(Start2D - A, Delta) / Denominator, 0.0f, 1.0f);
					const FVector2D Candidate = A + Delta * T;
					// Keep the primary landmark reveal free of branch traffic. New
					// lanes can join its spine only after the deliberate approach.
					if (Landmark != nullptr &&
						FVector2D::Distance(Candidate, LandmarkEntrance) < 3600.0f)
					{
						continue;
					}
					const bool bNearAnyEntrance = InOutPlan.Locations.ContainsByPredicate(
						[&](const FResolvedLocationPlan& Location)
						{
							return FVector2D::Distance(
								Candidate, FVector2D(Location.EntranceTransform.GetLocation())) < 2600.0f;
						});
					if (bNearAnyEntrance)
					{
						continue;
					}
					// A junction is snapped back to an exact point after the A* grid
					// search. Keep it well clear of every plot so endpoint rounding
					// cannot make that final short segment clip a neighboring yard.
					const bool bInsideReservedPlot = Occupied.ContainsByPredicate(
						[&](const FBox2D& Box)
						{
							const FBox2D JunctionClearance(
								Box.Min - FVector2D(1400.0f),
								Box.Max + FVector2D(1400.0f));
							return JunctionClearance.IsInside(Candidate);
						});
					if (bInsideReservedPlot)
					{
						continue;
					}
					const float CandidateDistance = FVector2D::Distance(Start2D, Candidate);
					if (CandidateDistance < BestNetworkDistance)
					{
						BestNetworkDistance = CandidateDistance;
						PhysicalEnd2D = Candidate;
						bEndsAtSharedNetwork = true;
					}
				}
			}
		}
		RoutePairs.Add(PairKey);
		FResolvedRoutePlan& Route = InOutPlan.Routes.AddDefaulted_GetRef();
		Route.FromLocationId = FromId;
		Route.ToLocationId = ToId;
		Route.RouteType = RouteType;
		Route.WidthCentimeters = RouteWidth;
		SharedNetworkJunctionCount += bEndsAtSharedNetwork ? 1 : 0;
		const FVector2D StartGate = ResolveApproachGate(*From, bLandmarkApproach &&
			FromId == InOutPlan.LandmarkLocationId ? 4200.0f : 1750.0f);
		const FVector2D EndGate = bEndsAtSharedNetwork ? PhysicalEnd2D :
			ResolveApproachGate(*To, bLandmarkApproach &&
				ToId == InOutPlan.LandmarkLocationId ? 4200.0f : 1750.0f);
		const TArray<FVector2D> Path = FindTerrainRoute(
			Terrain, StartGate, EndGate, Occupied,
			StartOccupiedIndex,
			bEndsAtSharedNetwork ? INDEX_NONE : SemanticEndOccupiedIndex);
		Route.ControlPoints.Reserve(Path.Num() + 2);
		auto AddControlPoint = [&](const FVector2D& Point)
		{
			if (Route.ControlPoints.IsEmpty() ||
				!FVector2D(Route.ControlPoints.Last()).Equals(Point, 1.0f))
			{
				Route.ControlPoints.Add(FVector(Point, SampleHeightCentimeters(Terrain, Point)));
			}
		};
		AddControlPoint(Start2D);
		for (const FVector2D& Point : Path)
		{
			AddControlPoint(Point);
		}
		AddControlPoint(PhysicalEnd2D);
		// Chaikin refinement deliberately preserves the gate endpoints, which can
		// leave a tiny gate-adjacent segment attached to the much longer building
		// approach. Prune only those transition slivers, then relax the first and
		// last few points inside the already-safe graded corridor. The A* interior
		// remains untouched.
		const float MinimumApproachSegment = FMath::Max(
			160.0f, Route.WidthCentimeters * 0.72f);
		const TArray<FVector> SafeApproachCorridor = Route.ControlPoints;
		const float CorridorRadius = Route.WidthCentimeters * 0.5f + 720.0f;
		auto IsApproachSegmentSafe = [&](const FVector2D& A, const FVector2D& B)
		{
			for (int32 BoxIndex = 0; BoxIndex < Occupied.Num(); ++BoxIndex)
			{
				const bool bEndpointPlot = BoxIndex == StartOccupiedIndex ||
					(!bEndsAtSharedNetwork && BoxIndex == SemanticEndOccupiedIndex);
				if (!bEndpointPlot &&
					SegmentIntersectsBox2D(A, B, Occupied[BoxIndex]))
				{
					return false;
				}
			}
			const int32 SampleCount = FMath::Max(1,
				FMath::CeilToInt(FVector2D::Distance(A, B) / 100.0f));
			for (int32 SampleIndex = 0; SampleIndex <= SampleCount; ++SampleIndex)
			{
				const FVector2D Sample = FMath::Lerp(
					A, B, static_cast<float>(SampleIndex) / SampleCount);
				const float Height = SampleHeightCentimeters(Terrain, Sample);
				const bool bCutsStartPlotBeyondExit = StartOccupiedIndex != INDEX_NONE &&
					Occupied.IsValidIndex(StartOccupiedIndex) &&
					Occupied[StartOccupiedIndex].IsInside(Sample) &&
					FVector2D::Distance(Sample, Start2D) > 2400.0f;
				const bool bCutsEndPlotBeyondEntry = !bEndsAtSharedNetwork &&
					SemanticEndOccupiedIndex != INDEX_NONE &&
					Occupied.IsValidIndex(SemanticEndOccupiedIndex) &&
					Occupied[SemanticEndOccupiedIndex].IsInside(Sample) &&
					FVector2D::Distance(Sample, PhysicalEnd2D) > 2400.0f;
				if ((Terrain.WaterLevelCentimeters != INDEX_NONE &&
					Height <= Terrain.WaterLevelCentimeters + 20.0f) ||
					bCutsStartPlotBeyondExit || bCutsEndPlotBeyondEntry ||
					SampleSlopeDegrees(Terrain, Sample) > 24.0f ||
					DistanceToPolyline(Sample, SafeApproachCorridor) > CorridorRadius)
				{
					return false;
				}
			}
			return true;
		};
		while (Route.ControlPoints.Num() > 4 && FVector2D::Distance(
			FVector2D(Route.ControlPoints[1]), FVector2D(Route.ControlPoints[2])) <
			MinimumApproachSegment && IsApproachSegmentSafe(
				FVector2D(Route.ControlPoints[1]), FVector2D(Route.ControlPoints[3])))
		{
			Route.ControlPoints.RemoveAt(2, 1, EAllowShrinking::No);
		}
		while (Route.ControlPoints.Num() > 4 && FVector2D::Distance(
			FVector2D(Route.ControlPoints[Route.ControlPoints.Num() - 2]),
			FVector2D(Route.ControlPoints[Route.ControlPoints.Num() - 3])) <
			MinimumApproachSegment && IsApproachSegmentSafe(
				FVector2D(Route.ControlPoints[Route.ControlPoints.Num() - 4]),
				FVector2D(Route.ControlPoints[Route.ControlPoints.Num() - 2])))
		{
			Route.ControlPoints.RemoveAt(Route.ControlPoints.Num() - 3, 1, EAllowShrinking::No);
		}
		for (int32 Pass = 0; Pass < 2 && Route.ControlPoints.Num() >= 4; ++Pass)
		{
			TArray<FVector> SmoothedApproaches = Route.ControlPoints;
			for (int32 Index = 1; Index < Route.ControlPoints.Num() - 1; ++Index)
			{
				const bool bNearStart = Index <= 6;
				const bool bNearEnd = Index >= Route.ControlPoints.Num() - 7;
				if (!bNearStart && !bNearEnd)
				{
					continue;
				}
				const FVector2D Candidate = (
					FVector2D(Route.ControlPoints[Index - 1]) +
					FVector2D(Route.ControlPoints[Index]) * 2.0f +
					FVector2D(Route.ControlPoints[Index + 1])) * 0.25f;
				const float Height = SampleHeightCentimeters(Terrain, Candidate);
				if (IsApproachSegmentSafe(
						FVector2D(Route.ControlPoints[Index - 1]), Candidate) &&
					IsApproachSegmentSafe(
						Candidate, FVector2D(Route.ControlPoints[Index + 1])))
				{
					SmoothedApproaches[Index].X = Candidate.X;
					SmoothedApproaches[Index].Y = Candidate.Y;
					SmoothedApproaches[Index].Z = Height;
				}
			}
			Route.ControlPoints = MoveTemp(SmoothedApproaches);
		}
		// Smoothing can shorten the next segment beside a preserved gate. Remove
		// only redundant samples in those transition bands; keeping every segment
		// longer than the road half-width prevents a miter from folding over the
		// neighboring quad while retaining the straight building reveal.
		for (int32 Attempt = 0; Attempt < 8 && Route.ControlPoints.Num() > 4; ++Attempt)
		{
			bool bRemoved = false;
			const int32 StartLimit = FMath::Min(7, Route.ControlPoints.Num() - 2);
			for (int32 Index = 2; Index <= StartLimit; ++Index)
			{
				if (FVector2D::Distance(FVector2D(Route.ControlPoints[Index - 1]),
					FVector2D(Route.ControlPoints[Index])) < MinimumApproachSegment &&
					IsApproachSegmentSafe(
						FVector2D(Route.ControlPoints[Index - 1]),
						FVector2D(Route.ControlPoints[Index + 1])))
				{
					Route.ControlPoints.RemoveAt(Index, 1, EAllowShrinking::No);
					bRemoved = true;
					break;
				}
			}
			if (!bRemoved)
			{
				break;
			}
		}
		for (int32 Attempt = 0; Attempt < 8 && Route.ControlPoints.Num() > 4; ++Attempt)
		{
			bool bRemoved = false;
			const int32 EndLimit = FMath::Max(1, Route.ControlPoints.Num() - 8);
			for (int32 Index = Route.ControlPoints.Num() - 3; Index >= EndLimit; --Index)
			{
				if (FVector2D::Distance(FVector2D(Route.ControlPoints[Index]),
					FVector2D(Route.ControlPoints[Index + 1])) < MinimumApproachSegment &&
					IsApproachSegmentSafe(
						FVector2D(Route.ControlPoints[Index - 1]),
						FVector2D(Route.ControlPoints[Index + 1])))
				{
					Route.ControlPoints.RemoveAt(Index, 1, EAllowShrinking::No);
					bRemoved = true;
					break;
				}
			}
			if (!bRemoved)
			{
				break;
			}
		}
		// A preserved gate itself can become the short side of an acute join after
		// the safe path is rounded. Collapse only that proven-safe chord instead of
		// leaving a folded miter at the building approach.
		for (int32 Attempt = 0; Attempt < 8 && Route.ControlPoints.Num() > 3; ++Attempt)
		{
			bool bRemovedUnstableJoin = false;
			for (int32 Index = 1; Index < Route.ControlPoints.Num() - 1; ++Index)
			{
				const bool bInApproachBand = Index <= 7 ||
					Index >= Route.ControlPoints.Num() - 8;
				if (!bInApproachBand)
				{
					continue;
				}
				const FVector2D Incoming = (
					FVector2D(Route.ControlPoints[Index]) -
					FVector2D(Route.ControlPoints[Index - 1])).GetSafeNormal();
				const FVector2D Outgoing = (
					FVector2D(Route.ControlPoints[Index + 1]) -
					FVector2D(Route.ControlPoints[Index])).GetSafeNormal();
				const float TurnDegrees = FMath::RadiansToDegrees(FMath::Acos(
					FMath::Clamp(FVector2D::DotProduct(Incoming, Outgoing), -1.0f, 1.0f)));
				const float ShorterSegment = FMath::Min(
					FVector2D::Distance(FVector2D(Route.ControlPoints[Index - 1]),
						FVector2D(Route.ControlPoints[Index])),
					FVector2D::Distance(FVector2D(Route.ControlPoints[Index]),
						FVector2D(Route.ControlPoints[Index + 1])));
				if (TurnDegrees > 45.0f &&
					ShorterSegment < Route.WidthCentimeters * 0.5f &&
					IsApproachSegmentSafe(
						FVector2D(Route.ControlPoints[Index - 1]),
						FVector2D(Route.ControlPoints[Index + 1])))
				{
					Route.ControlPoints.RemoveAt(Index, 1, EAllowShrinking::No);
					bRemovedUnstableJoin = true;
					break;
				}
			}
			if (!bRemovedUnstableJoin)
			{
				break;
			}
		}
		// Smooth the vertical profile separately. The terrain corridor is cut/fill
		// graded to this profile below, which keeps roads grounded without copying
		// every small bump into the travel surface.
		for (int32 Pass = 0; Pass < 4; ++Pass)
		{
			TArray<float> SmoothedZ;
			SmoothedZ.Reserve(Route.ControlPoints.Num());
			for (const FVector& Point : Route.ControlPoints)
			{
				SmoothedZ.Add(Point.Z);
			}
			for (int32 Index = 1; Index < Route.ControlPoints.Num() - 1; ++Index)
			{
				SmoothedZ[Index] = (Route.ControlPoints[Index - 1].Z +
					Route.ControlPoints[Index].Z * 2.0f + Route.ControlPoints[Index + 1].Z) * 0.25f;
			}
			for (int32 Index = 1; Index < Route.ControlPoints.Num() - 1; ++Index)
			{
				Route.ControlPoints[Index].Z = SmoothedZ[Index];
			}
		}
		const FVector PhysicalEnd(
			PhysicalEnd2D, SampleHeightCentimeters(Terrain, PhysicalEnd2D));
		const FVector Delta = PhysicalEnd - Start;
		Route.Transform = FTransform(FRotator(0.0f, FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)), 0.0f),
			(Start + PhysicalEnd) * 0.5f, FVector(Delta.Size2D() / 100.0f, Route.WidthCentimeters / 100.0f, 0.04f));
		Route.MaximumGrade = 0.0f;
		for (int32 PointIndex = 1; PointIndex < Route.ControlPoints.Num(); ++PointIndex)
		{
			const FVector& Previous = Route.ControlPoints[PointIndex - 1];
			const FVector& Current = Route.ControlPoints[PointIndex];
			Route.MaximumGrade = FMath::Max(Route.MaximumGrade,
				FMath::Abs(Current.Z - Previous.Z) /
				FMath::Max(1.0f, FVector2D::Distance(FVector2D(Current), FVector2D(Previous))));
		}
		for (int32 PointIndex = 1; PointIndex < Route.ControlPoints.Num() - 1; ++PointIndex)
		{
			const FVector2D Incoming = (FVector2D(Route.ControlPoints[PointIndex]) -
				FVector2D(Route.ControlPoints[PointIndex - 1])).GetSafeNormal();
			const FVector2D Outgoing = (FVector2D(Route.ControlPoints[PointIndex + 1]) -
				FVector2D(Route.ControlPoints[PointIndex])).GetSafeNormal();
			const float TurnDegrees = FMath::RadiansToDegrees(FMath::Acos(
				FMath::Clamp(FVector2D::DotProduct(Incoming, Outgoing), -1.0f, 1.0f)));
			const float ShorterSegment = FMath::Min(
				FVector2D::Distance(FVector2D(Route.ControlPoints[PointIndex - 1]),
					FVector2D(Route.ControlPoints[PointIndex])),
				FVector2D::Distance(FVector2D(Route.ControlPoints[PointIndex]),
					FVector2D(Route.ControlPoints[PointIndex + 1])));
			UnstableApproachJoinCount += TurnDegrees > 45.0f &&
				ShorterSegment < Route.WidthCentimeters * 0.5f ? 1 : 0;
		}
	};
	// Semantic adjacency describes who must be reachable, not a requirement to
	// draw one independent ribbon for every relationship. Build one deterministic
	// physical tree instead: direct semantic links and same-district collectors
	// are strongly preferred, while every location still joins the shared network.
	// This turns the old loops and parallel corridors into a readable civic spine
	// with local branches and exactly one physical connection per new place.
	TMap<FString, FName> SemanticRouteTypes;
	auto CanonicalPair = [](const FString& A, const FString& B)
	{
		return A < B ? A + TEXT("|") + B : B + TEXT("|") + A;
	};
	for (const FTownTopologyEdge& Edge : Spec.Topology.Edges)
	{
		const FString PairKey = CanonicalPair(Edge.FromLocationId, Edge.ToLocationId);
		FName& StoredType = SemanticRouteTypes.FindOrAdd(PairKey, Edge.RouteType);
		if (Edge.RouteType == TEXT("Road"))
		{
			StoredType = TEXT("Road");
		}
	}
	TSet<FString> ConnectedLocationIds;
	if (!InOutPlan.Locations.IsEmpty())
	{
		ConnectedLocationIds.Add(InOutPlan.LandmarkLocationId.IsEmpty()
			? InOutPlan.Locations[0].LocationId : InOutPlan.LandmarkLocationId);
	}
	while (ConnectedLocationIds.Num() < InOutPlan.Locations.Num())
	{
		const FResolvedLocationPlan* BestConnected = nullptr;
		const FResolvedLocationPlan* BestDisconnected = nullptr;
		FName BestRouteType = TEXT("Path");
		float BestScore = MAX_flt;
		FString BestPairKey;
		for (const FResolvedLocationPlan& Disconnected : InOutPlan.Locations)
		{
			if (ConnectedLocationIds.Contains(Disconnected.LocationId))
			{
				continue;
			}
			for (const FResolvedLocationPlan& Connected : InOutPlan.Locations)
			{
				if (!ConnectedLocationIds.Contains(Connected.LocationId))
				{
					continue;
				}
				const FString PairKey = CanonicalPair(
					Disconnected.LocationId, Connected.LocationId);
				const FName* SemanticType = SemanticRouteTypes.Find(PairKey);
				const bool bPrimaryLandmarkPair =
					(Disconnected.LocationId == InOutPlan.LandmarkLocationId &&
						Connected.LocationId == PrimaryLandmarkNeighbor) ||
					(Connected.LocationId == InOutPlan.LandmarkLocationId &&
						Disconnected.LocationId == PrimaryLandmarkNeighbor);
				float Score = FVector2D::Distance(
					FVector2D(Disconnected.EntranceTransform.GetLocation()),
					FVector2D(Connected.EntranceTransform.GetLocation()));
				if (SemanticType != nullptr)
				{
					Score *= *SemanticType == TEXT("Road") ? 0.46f : 0.62f;
				}
				if (Disconnected.DistrictId == Connected.DistrictId)
				{
					Score *= 0.74f;
				}
				if (bPrimaryLandmarkPair)
				{
					Score *= 0.05f;
				}
				if (Score < BestScore - KINDA_SMALL_NUMBER ||
					(FMath::IsNearlyEqual(Score, BestScore) &&
						(BestPairKey.IsEmpty() || PairKey < BestPairKey)))
				{
					BestScore = Score;
					BestPairKey = PairKey;
					BestConnected = &Connected;
					BestDisconnected = &Disconnected;
					BestRouteType = SemanticType != nullptr ? *SemanticType :
						Disconnected.DistrictId == Connected.DistrictId ? TEXT("Path") : TEXT("Road");
				}
			}
		}
		if (BestConnected == nullptr || BestDisconnected == nullptr)
		{
			break;
		}
		AddRoute(BestDisconnected->LocationId, BestConnected->LocationId, BestRouteType);
		ConnectedLocationIds.Add(BestDisconnected->LocationId);
	}
	UE_LOG(LogWorldDirector, Display,
		TEXT("WORLD_DIRECTOR_ROUTE_GEOMETRY routes=%d sharedNetworkJunctions=%d unstableApproachJoins=%d"),
		InOutPlan.Routes.Num(), SharedNetworkJunctionCount, UnstableApproachJoinCount);

	// Cut and fill road shoulders into the same heightfield used for collision,
	// nav, and the visible ribbon. A route is no longer a floating decal over an
	// unrelated surface.
	for (int32 Y = 0; Y < TerrainResolution; ++Y)
	{
		for (int32 X = 0; X < TerrainResolution; ++X)
		{
			const FVector2D Position(
				-TerrainExtent + static_cast<float>(X) / (TerrainResolution - 1) * TerrainDiameter,
				-TerrainExtent + static_cast<float>(Y) / (TerrainResolution - 1) * TerrainDiameter);
			float Height = Terrain.HeightsCentimeters[Y * TerrainResolution + X];
			for (const FResolvedRoutePlan& Route : InOutPlan.Routes)
			{
				for (int32 PointIndex = 1; PointIndex < Route.ControlPoints.Num(); ++PointIndex)
				{
					const FVector2D A(Route.ControlPoints[PointIndex - 1]);
					const FVector2D B(Route.ControlPoints[PointIndex]);
					const FVector2D Delta = B - A;
					const float Denominator = Delta.SizeSquared();
					const float T = Denominator > KINDA_SMALL_NUMBER
						? FMath::Clamp(FVector2D::DotProduct(Position - A, Delta) / Denominator, 0.0f, 1.0f) : 0.0f;
					const float Distance = FVector2D::Distance(Position, A + Delta * T);
					const float Shoulder = Route.WidthCentimeters * 0.5f + 720.0f;
					if (Distance <= Shoulder)
					{
						const float TargetHeight = FMath::Lerp(
							Route.ControlPoints[PointIndex - 1].Z, Route.ControlPoints[PointIndex].Z, T);
						const float Alpha = 1.0f - SmoothRange(Route.WidthCentimeters * 0.48f, Shoulder, Distance);
						Height = FMath::Lerp(Height, TargetHeight, Alpha * 0.88f);
					}
				}
			}
			Terrain.HeightsCentimeters[Y * TerrainResolution + X] = FMath::RoundToInt(Height);
		}
	}
	for (FResolvedRoutePlan& Route : InOutPlan.Routes)
	{
		Route.MaximumGrade = 0.0f;
		for (FVector& Point : Route.ControlPoints)
		{
			Point.Z = SampleHeightCentimeters(Terrain, FVector2D(Point)) + 10.0f;
		}
		for (int32 Index = 1; Index < Route.ControlPoints.Num(); ++Index)
		{
			Route.MaximumGrade = FMath::Max(Route.MaximumGrade,
				FMath::Abs(Route.ControlPoints[Index].Z - Route.ControlPoints[Index - 1].Z) /
				FMath::Max(1.0f, FVector2D::Distance(
					FVector2D(Route.ControlPoints[Index]), FVector2D(Route.ControlPoints[Index - 1]))));
		}
	}

	// Coherent, terrain-aware farm parcels replace V2's diagonal modulo stripes.
	FRandomStream SurfaceRandom(InOutPlan.StageSeeds[TEXT("surfaces")]);
	Terrain.FarmParcels.Reset();
	const int32 DesiredFarmParcels = 2 + static_cast<int32>(
		static_cast<uint32>(InOutPlan.StageSeeds[TEXT("surfaces")]) % 3U);
	for (int32 Attempt = 0; Attempt < 500 && Terrain.FarmParcels.Num() < DesiredFarmParcels; ++Attempt)
	{
		const FWorldDirectorDistrictAnchor& District = InOutPlan.DistrictAnchors[
			Attempt % InOutPlan.DistrictAnchors.Num()];
		const float Angle = SurfaceRandom.FRandRange(-PI, PI);
		const FVector2D Candidate = FVector2D(District.Position) +
			FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * SurfaceRandom.FRandRange(9500.0f, 18500.0f);
		const float Height = SampleHeightCentimeters(Terrain, Candidate);
		if (FMath::Max(FMath::Abs(Candidate.X), FMath::Abs(Candidate.Y)) > TerrainExtent - 9000.0f ||
			SampleSlopeDegrees(Terrain, Candidate) > 6.5f ||
			(Terrain.WaterLevelCentimeters != INDEX_NONE && Height <= Terrain.WaterLevelCentimeters + 180.0f) ||
			IsInsideLocationEnvelope(Candidate, InOutPlan.Locations, 1700.0f) ||
			DistanceToRoutes(Candidate, InOutPlan.Routes, 900.0f) < 0.0f)
		{
			continue;
		}
		bool bOverlapsParcel = false;
		for (const FWorldDirectorFarmParcel& Existing : Terrain.FarmParcels)
		{
			bOverlapsParcel |= FVector2D::Distance(Candidate, Existing.Center) < 7600.0f;
		}
		if (bOverlapsParcel)
		{
			continue;
		}
		FWorldDirectorFarmParcel& Parcel = Terrain.FarmParcels.AddDefaulted_GetRef();
		Parcel.ParcelId = FString::Printf(TEXT("farm.%02d"), Terrain.FarmParcels.Num());
		Parcel.Center = Candidate;
		const FVector2D HalfExtent(
			SurfaceRandom.FRandRange(3900.0f, 6100.0f),
			SurfaceRandom.FRandRange(2300.0f, 3700.0f));
		const float YawRadians = SurfaceRandom.FRandRange(-PI, PI);
		Parcel.YawDegrees = FMath::RadiansToDegrees(YawRadians);
		const float CosYaw = FMath::Cos(YawRadians);
		const float SinYaw = FMath::Sin(YawRadians);
		const float BaseChamfer = FMath::Min(HalfExtent.X, HalfExtent.Y) *
			SurfaceRandom.FRandRange(0.28f, 0.52f);
		const float Chamfer0 = BaseChamfer * SurfaceRandom.FRandRange(0.72f, 1.25f);
		const float Chamfer1 = BaseChamfer * SurfaceRandom.FRandRange(0.72f, 1.25f);
		const float Chamfer2 = BaseChamfer * SurfaceRandom.FRandRange(0.72f, 1.25f);
		const float Chamfer3 = BaseChamfer * SurfaceRandom.FRandRange(0.72f, 1.25f);
		const FVector2D LocalBoundary[] = {
			FVector2D(-HalfExtent.X + Chamfer0, -HalfExtent.Y),
			FVector2D(HalfExtent.X - Chamfer1, -HalfExtent.Y),
			FVector2D(HalfExtent.X, -HalfExtent.Y + Chamfer1),
			FVector2D(HalfExtent.X, HalfExtent.Y - Chamfer2),
			FVector2D(HalfExtent.X - Chamfer2, HalfExtent.Y),
			FVector2D(-HalfExtent.X + Chamfer3, HalfExtent.Y),
			FVector2D(-HalfExtent.X, HalfExtent.Y - Chamfer3),
			FVector2D(-HalfExtent.X, -HalfExtent.Y + Chamfer0)};
		for (const FVector2D& Local : LocalBoundary)
		{
			Parcel.BoundaryPoints.Add(Parcel.Center + FVector2D(
				Local.X * CosYaw - Local.Y * SinYaw,
				Local.X * SinYaw + Local.Y * CosYaw));
		}
		float BestGateRouteDistance = MAX_flt;
		for (int32 EdgeIndex = 0; EdgeIndex < Parcel.BoundaryPoints.Num(); ++EdgeIndex)
		{
			const FVector2D EdgeMidpoint = (
				Parcel.BoundaryPoints[EdgeIndex] +
				Parcel.BoundaryPoints[(EdgeIndex + 1) % Parcel.BoundaryPoints.Num()]) * 0.5f;
			const float RouteDistance = DistanceToRoutes(EdgeMidpoint, InOutPlan.Routes);
			if (RouteDistance < BestGateRouteDistance)
			{
				BestGateRouteDistance = RouteDistance;
				Parcel.GatePosition = EdgeMidpoint;
			}
		}
	}

	Terrain.SurfaceTypes.Reset();
	Terrain.SurfaceBlendWeights.Reset();
	Terrain.MoistureValues.Reset();
	Terrain.SurfaceTypes.Reserve(TerrainResolution * TerrainResolution);
	Terrain.SurfaceBlendWeights.Reserve(TerrainResolution * TerrainResolution * 4);
	Terrain.MoistureValues.Reserve(TerrainResolution * TerrainResolution);
	MinimumHeight = MAX_int32;
	MaximumHeight = MIN_int32;
	TotalSlope = 0.0;
	int32 BuildableSamples = 0;
	int32 WaterSamples = 0;
	int32 RockSamples = 0;
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
			const bool bWater = Terrain.WaterLevelCentimeters != INDEX_NONE &&
				Height <= Terrain.WaterLevelCentimeters + 18;
			const float PlotSurfaceMask = LocationSurfaceMask(Position, InOutPlan.Locations);
			const float PlotClearanceMask = LocationClearanceMask(Position, InOutPlan.Locations);
			const bool bPlotSurface = PlotSurfaceMask >= 0.55f;
			const float RouteDistance = DistanceToRoutes(Position, InOutPlan.Routes);
			const float RoadSurfaceMask = 1.0f - SmoothRange(-40.0f, 300.0f, RouteDistance);
			const bool bRoadSurface = RoadSurfaceMask >= 0.55f;
			const float WaterDistance = DistanceToPolyline(Position, Terrain.WaterControlPoints);
			const float Lowland = 1.0f - SmoothRange(
				Terrain.MinimumHeightCentimeters, Terrain.MaximumHeightCentimeters, Height);
			const float MoistureNoise = FractalNoise(Position.X / TerrainExtent * 1.8f,
				Position.Y / TerrainExtent * 1.8f, InOutPlan.StageSeeds[TEXT("hydrology")]);
			const float Moisture = FMath::Clamp(
				(WaterDistance < MAX_flt ? 1.0f - SmoothRange(900.0f, 19000.0f, WaterDistance) : 0.18f) * 0.62f +
				Lowland * 0.23f + MoistureNoise * 0.12f + 0.14f, 0.0f, 1.0f);
			Terrain.MoistureValues.Add(FMath::RoundToInt(Moisture * 255.0f));
			float FarmWeight = Slope < 8.5f ? FarmParcelMask(Position, Terrain.FarmParcels) : 0.0f;
			if (PlotClearanceMask > 0.12f || RoadSurfaceMask > 0.12f || bWater)
			{
				FarmWeight = 0.0f;
			}
			const float Altitude = FMath::GetRangePct(
				Terrain.MinimumHeightCentimeters, Terrain.MaximumHeightCentimeters, Height);
			const float ExposureNoise = FMath::Clamp(0.5f + 0.5f * FractalNoise(
				Position.X / TerrainExtent * 0.82f, Position.Y / TerrainExtent * 0.82f,
				InOutPlan.StageSeeds[TEXT("surfaces")] + 1709), 0.0f, 1.0f);
			float RockWeight = FMath::Max(SmoothRange(20.0f, 36.0f, Slope),
				SmoothRange(0.72f, 0.96f, Altitude) * SmoothRange(14.0f, 27.0f, Slope));
			RockWeight *= FMath::Lerp(0.28f, 1.0f, ExposureNoise) * FMath::Lerp(1.0f, 0.72f, Moisture);
			const float ScreeWeight = SmoothRange(11.0f, 19.0f, Slope) *
				(1.0f - SmoothRange(25.0f, 34.0f, Slope)) * FMath::Lerp(0.16f, 0.5f, ExposureNoise);
			// The dedicated route mesh owns visible road gravel. Keeping the coarse
			// 625 cm terrain grid out of this channel prevents a second, much wider
			// tan road from appearing beneath the precise feathered ribbon.
			float GravelWeight = FMath::Max(ScreeWeight,
					FMath::Clamp((1.0f - SmoothRange(350.0f, 2400.0f, WaterDistance)) *
						(1.0f - Moisture) * 0.55f, 0.0f, 0.55f));
			RockWeight *= 1.0f - FMath::Max(RoadSurfaceMask, PlotClearanceMask);
			const float DominantNonGrass = FMath::Max(RockWeight, FMath::Max(GravelWeight, FarmWeight));
			const float GrassWeight = FMath::Max(0.04f, 1.0f - DominantNonGrass);
			const float WeightTotal = GrassWeight + GravelWeight + FarmWeight + RockWeight;
			int32 RemainingWeight = 255;
			const int32 QuantizedGrass = FMath::Clamp(
				FMath::RoundToInt(GrassWeight / WeightTotal * 255.0f), 0, RemainingWeight);
			RemainingWeight -= QuantizedGrass;
			const int32 QuantizedGravel = FMath::Clamp(
				FMath::RoundToInt(GravelWeight / WeightTotal * 255.0f), 0, RemainingWeight);
			RemainingWeight -= QuantizedGravel;
			const int32 QuantizedFarm = FMath::Clamp(
				FMath::RoundToInt(FarmWeight / WeightTotal * 255.0f), 0, RemainingWeight);
			RemainingWeight -= QuantizedFarm;
			Terrain.SurfaceBlendWeights.Add(static_cast<uint8>(QuantizedGrass));
			Terrain.SurfaceBlendWeights.Add(static_cast<uint8>(QuantizedGravel));
			Terrain.SurfaceBlendWeights.Add(static_cast<uint8>(QuantizedFarm));
			// Rock is the residual channel so the GPU vertex-color weights always
			// total exactly 255 and the material never brightens/darkens at seams.
			Terrain.SurfaceBlendWeights.Add(static_cast<uint8>(RemainingWeight));
			EWorldDirectorSurfaceType Surface = EWorldDirectorSurfaceType::Grass;
			if (bWater)
			{
				Surface = EWorldDirectorSurfaceType::Water;
			}
			else if (bPlotSurface)
			{
				Surface = EWorldDirectorSurfaceType::Paving;
			}
			else if (bRoadSurface)
			{
				Surface = EWorldDirectorSurfaceType::Gravel;
			}
			else if (FarmWeight > 0.52f)
			{
				Surface = EWorldDirectorSurfaceType::Farmfield;
			}
			else if (RockWeight > GrassWeight)
			{
				Surface = EWorldDirectorSurfaceType::Rock;
			}
			Terrain.SurfaceTypes.Add(static_cast<uint8>(Surface));
			BuildableSamples += !bWater && Slope <= Profile->MaximumPlotSlopeDegrees;
			WaterSamples += bWater;
			RockSamples += Surface == EWorldDirectorSurfaceType::Rock;
		}
	}
	Terrain.MinimumHeightCentimeters = MinimumHeight;
	Terrain.MaximumHeightCentimeters = MaximumHeight;
	Terrain.MeanSlopeDegrees = static_cast<float>(TotalSlope / FMath::Max(1, Terrain.SurfaceTypes.Num()));
	Terrain.BuildableRatio = static_cast<float>(BuildableSamples) / FMath::Max(1, Terrain.SurfaceTypes.Num());
	Terrain.WaterCoverage = static_cast<float>(WaterSamples) / FMath::Max(1, Terrain.SurfaceTypes.Num());
	Terrain.RockCoverage = static_cast<float>(RockSamples) / FMath::Max(1, Terrain.SurfaceTypes.Num());
	TArray<uint8> HeightBytes;
	AppendInt32(HeightBytes, Terrain.Resolution);
	AppendInt32(HeightBytes, Terrain.ExtentCentimeters);
	for (const int32 Height : Terrain.HeightsCentimeters)
	{
		AppendInt32(HeightBytes, Height);
	}
	Terrain.HeightFingerprint = FingerprintBytes(HeightBytes);
	TArray<uint8> SurfaceBytes = Terrain.SurfaceTypes;
	SurfaceBytes.Append(Terrain.SurfaceBlendWeights);
	SurfaceBytes.Append(Terrain.MoistureValues);
	for (const FWorldDirectorFarmParcel& Parcel : Terrain.FarmParcels)
	{
		AppendString(SurfaceBytes, Parcel.ParcelId);
		AppendInt32(SurfaceBytes, FMath::RoundToInt(Parcel.Center.X));
		AppendInt32(SurfaceBytes, FMath::RoundToInt(Parcel.Center.Y));
		AppendInt32(SurfaceBytes, FMath::RoundToInt(Parcel.YawDegrees * 100.0f));
		AppendInt32(SurfaceBytes, FMath::RoundToInt(Parcel.GatePosition.X));
		AppendInt32(SurfaceBytes, FMath::RoundToInt(Parcel.GatePosition.Y));
		AppendInt32(SurfaceBytes, Parcel.BoundaryPoints.Num());
		for (const FVector2D& BoundaryPoint : Parcel.BoundaryPoints)
		{
			AppendInt32(SurfaceBytes, FMath::RoundToInt(BoundaryPoint.X));
			AppendInt32(SurfaceBytes, FMath::RoundToInt(BoundaryPoint.Y));
		}
	}
	Terrain.SurfaceFingerprint = FingerprintBytes(SurfaceBytes);

	FRandomStream DressingRandom(InOutPlan.StageSeeds[TEXT("dressing")]);
	InOutPlan.Dressing.Reset();
	int32 CivicAnchorCount = 0;
	int32 CivicSeatCount = 0;
	int32 GuildBannerCount = 0;
	int32 FarmTransportCount = 0;
	int32 HomeUtilityCount = 0;
	int32 InnYardCount = 0;
	int32 CommunalFireCount = 0;
	auto AddIdentityDressing = [&](const FName PlacementTag, const FVector2D& Position,
		const float FacingYaw, const FName BiomeTag, const float MaximumSlope,
		const float EnvelopePadding) -> bool
	{
		if (FMath::Max(FMath::Abs(Position.X), FMath::Abs(Position.Y)) > TerrainExtent - 1200.0f ||
			SampleSlopeDegrees(Terrain, Position) > MaximumSlope)
		{
			return false;
		}
		const int32 Height = SampleHeightCentimeters(Terrain, Position);
		if ((Terrain.WaterLevelCentimeters != INDEX_NONE &&
				Height <= Terrain.WaterLevelCentimeters + 70) ||
			(EnvelopePadding >= 0.0f &&
				IsInsideLocationEnvelope(Position, InOutPlan.Locations, EnvelopePadding)))
		{
			return false;
		}
		const FWorldEnvironmentDressingAsset* Asset = PickDressingAsset(
			Profile, PlacementTag, DressingRandom);
		if (Asset == nullptr)
		{
			return false;
		}
		const float NominalScale = FMath::Clamp(1.0f, Asset->MinimumScale, Asset->MaximumScale);
		FWorldDirectorDressingInstance& Instance = InOutPlan.Dressing.AddDefaulted_GetRef();
		Instance.MeshAsset = Asset->MeshAsset;
		Instance.Transform = FTransform(FRotator(0.0f, FacingYaw, 0.0f),
			FVector(Position, Height), FVector(NominalScale));
		Instance.BiomeTag = BiomeTag;
		return true;
	};
	TArray<FVector2D> GroveCenters;
	float SettlementRadius = 9000.0f;
	for (const FResolvedLocationPlan& Location : InOutPlan.Locations)
	{
		SettlementRadius = FMath::Max(SettlementRadius,
			FVector2D::Distance(CivicCore, FVector2D(Location.Transform.GetLocation())) +
			Location.FootprintSize.Size() * 0.5f);
	}
	const int32 DesiredGroveCount = 13 + static_cast<int32>(
		static_cast<uint32>(InOutPlan.StageSeeds[TEXT("dressing")]) % 8U);
	// Seed the first lobes from actual outer wards. This pulls a forest wall up to
	// the inhabited edge instead of arranging isolated disks around world origin.
	for (int32 DistrictIndex = 1;
		DistrictIndex < InOutPlan.DistrictAnchors.Num() && GroveCenters.Num() < DesiredGroveCount;
		++DistrictIndex)
	{
		const FVector2D Anchor(InOutPlan.DistrictAnchors[DistrictIndex].Position);
		FVector2D Outward = (Anchor - CivicCore).GetSafeNormal();
		if (Outward.IsNearlyZero())
		{
			const float Angle = DistrictIndex * 2.39996323f;
			Outward = FVector2D(FMath::Cos(Angle), FMath::Sin(Angle));
		}
		const FVector2D Side(-Outward.Y, Outward.X);
		GroveCenters.Add(Anchor + Outward * DressingRandom.FRandRange(6200.0f, 9800.0f) +
			Side * DressingRandom.FRandRange(-3800.0f, 3800.0f));
	}
	for (int32 Attempt = 0; Attempt < 600 && GroveCenters.Num() < DesiredGroveCount; ++Attempt)
	{
		const float Angle = DressingRandom.FRandRange(-PI, PI);
		const float Radius = DressingRandom.FRandRange(
			SettlementRadius + 5200.0f, FMath::Min(52000.0f, SettlementRadius + 25000.0f));
		const FVector2D Candidate = CivicCore +
			FVector2D(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius);
		const int32 Height = SampleHeightCentimeters(Terrain, Candidate);
		const float Suitability = FractalNoise(
			Candidate.X / TerrainExtent * 2.15f,
			Candidate.Y / TerrainExtent * 2.15f,
			InOutPlan.StageSeeds[TEXT("dressing")] + 421);
		if (FMath::Max(FMath::Abs(Candidate.X), FMath::Abs(Candidate.Y)) > TerrainExtent - 1800.0f ||
			SampleSlopeDegrees(Terrain, Candidate) > 24.0f || Suitability < -0.42f ||
			(Terrain.WaterLevelCentimeters != INDEX_NONE && Height <= Terrain.WaterLevelCentimeters + 80) ||
			DistanceToRoutes(Candidate, InOutPlan.Routes) < 1800.0f ||
			IsInsideLocationEnvelope(Candidate, InOutPlan.Locations, 2600.0f))
		{
			continue;
		}
		bool bTooClose = false;
		for (const FVector2D& Existing : GroveCenters)
		{
			bTooClose |= FVector2D::Distance(Candidate, Existing) < 8500.0f;
		}
		if (!bTooClose)
		{
			GroveCenters.Add(Candidate);
		}
	}
	auto AddBiomeLayer = [&](const FName PlacementTag, const FName BiomeTag,
		const int32 TargetCount, const int32 AttemptBudget)
	{
		int32 Added = 0;
		for (int32 Attempt = 0; Attempt < AttemptBudget && Added < TargetCount; ++Attempt)
		{
			const bool bCanopy = PlacementTag == TEXT("Dressing.Canopy");
			const bool bGroundCover = PlacementTag == TEXT("Dressing.GroundCover");
			const bool bDeadwood = PlacementTag == TEXT("Dressing.Deadwood");
			const float GroveChance = bCanopy ? 0.84f : bGroundCover ? 0.7f : bDeadwood ? 0.72f : 0.0f;
			FVector2D Position;
			if (!GroveCenters.IsEmpty() && DressingRandom.FRand() < GroveChance)
			{
				const int32 GroveIndex = DressingRandom.RandRange(0, GroveCenters.Num() - 1);
				const FVector2D& Grove = GroveCenters[GroveIndex];
				const float MajorRadius = bCanopy ? 10800.0f : bGroundCover ? 11800.0f : 8200.0f;
				const float AxisRatio = FMath::Lerp(0.36f, 0.62f, SeedUnit(
					InOutPlan.StageSeeds[TEXT("dressing")], 0x7a31U + GroveIndex * 97U));
				const float Orientation = SeedUnit(
					InOutPlan.StageSeeds[TEXT("dressing")], 0x91d5U + GroveIndex * 131U) * 2.0f * PI;
				const float Angle = DressingRandom.FRandRange(-PI, PI);
				const float BoundaryWarp = FMath::Lerp(0.62f, 1.0f,
					0.5f + 0.5f * SmoothNoise(FMath::Cos(Angle) * 2.3f,
						FMath::Sin(Angle) * 2.3f,
						InOutPlan.StageSeeds[TEXT("dressing")] + GroveIndex * 47));
				const float Radius = FMath::Sqrt(DressingRandom.FRand()) * BoundaryWarp;
				const FVector2D LocalOffset(
					FMath::Cos(Angle) * MajorRadius * Radius,
					FMath::Sin(Angle) * MajorRadius * AxisRatio * Radius);
				const float CosOrientation = FMath::Cos(Orientation);
				const float SinOrientation = FMath::Sin(Orientation);
				Position = Grove + FVector2D(
					LocalOffset.X * CosOrientation - LocalOffset.Y * SinOrientation,
					LocalOffset.X * SinOrientation + LocalOffset.Y * CosOrientation);
			}
			else
			{
				Position = FVector2D(
					DressingRandom.FRandRange(-TerrainExtent + 1800.0f, TerrainExtent - 1800.0f),
					DressingRandom.FRandRange(-TerrainExtent + 1800.0f, TerrainExtent - 1800.0f));
			}
			if (FMath::Max(FMath::Abs(Position.X), FMath::Abs(Position.Y)) > TerrainExtent - 1800.0f)
			{
				continue;
			}
			const int32 Height = SampleHeightCentimeters(Terrain, Position);
			const float Slope = SampleSlopeDegrees(Terrain, Position);
			const bool bWater = Terrain.WaterLevelCentimeters != INDEX_NONE &&
				Height <= Terrain.WaterLevelCentimeters + 35;
			if (bWater || Slope > 33.0f || IsInsideLocationEnvelope(Position, InOutPlan.Locations, 850.0f))
			{
				continue;
			}
			const float RouteDistance = DistanceToRoutes(Position, InOutPlan.Routes);
			float NearestLocationDistance = MAX_flt;
			for (const FResolvedLocationPlan& Location : InOutPlan.Locations)
			{
				NearestLocationDistance = FMath::Min(NearestLocationDistance,
					DistanceOutsideOrientedRectangle(Position, Location, 0.0f));
			}
			const float ForestPattern = FractalNoise(Position.X / TerrainExtent * 2.5f,
				Position.Y / TerrainExtent * 2.5f, InOutPlan.StageSeeds[TEXT("dressing")]);
			const float ForestMacro = SmoothNoise(Position.X / TerrainExtent * 1.08f,
				Position.Y / TerrainExtent * 1.08f, InOutPlan.StageSeeds[TEXT("dressing")] + 991);
			const float HabitatScore = ForestPattern * 0.58f + ForestMacro * 0.42f;
			const float WaterDistance = DistanceToPolyline(Position, Terrain.WaterControlPoints);
			const float FarmMask = FarmParcelMask(Position, Terrain.FarmParcels);
			bool bAccepted = false;
			if (PlacementTag == TEXT("Dressing.Canopy"))
			{
				const float EdgeThreshold = NearestLocationDistance < 10500.0f ? -0.42f : -0.18f;
				bAccepted = NearestLocationDistance > 1450.0f && Slope < 27.0f &&
					RouteDistance > 1200.0f && FarmMask < 0.18f && HabitatScore >
					(Terrain.Archetype == EWorldDirectorTerrainArchetype::Marsh ? 0.02f : EdgeThreshold);
			}
			else if (PlacementTag == TEXT("Dressing.GroundCover"))
			{
				bAccepted = Slope < 27.0f && RouteDistance > 520.0f && FarmMask < 0.45f &&
					(WaterDistance < 9500.0f || ForestPattern > -0.2f);
			}
			else if (PlacementTag == TEXT("Dressing.Rock"))
			{
				bAccepted = RouteDistance > 700.0f && (Slope > 12.0f ||
					FMath::GetRangePct(Terrain.MinimumHeightCentimeters, Terrain.MaximumHeightCentimeters, Height) > 0.68f);
			}
			else if (PlacementTag == TEXT("Dressing.Deadwood"))
			{
				bAccepted = NearestLocationDistance > 2200.0f && Slope < 24.0f &&
					RouteDistance > 1000.0f && HabitatScore > -0.02f;
			}
			else if (PlacementTag == TEXT("Dressing.FarmAccent"))
			{
				bAccepted = FarmMask > 0.42f && FarmMask < 0.78f && Slope < 9.0f && RouteDistance > 650.0f;
			}
			if (!bAccepted)
			{
				continue;
			}
			const FWorldEnvironmentDressingAsset* Asset = PickDressingAsset(Profile, PlacementTag, DressingRandom);
			if (Asset == nullptr)
			{
				continue;
			}
			const float UniformScale = DressingRandom.FRandRange(Asset->MinimumScale, Asset->MaximumScale);
			const float Pitch = PlacementTag == TEXT("Dressing.Rock") ? DressingRandom.FRandRange(-11.0f, 11.0f) : 0.0f;
			const float Roll = PlacementTag == TEXT("Dressing.Rock") ? DressingRandom.FRandRange(-11.0f, 11.0f) : 0.0f;
			FWorldDirectorDressingInstance& Instance = InOutPlan.Dressing.AddDefaulted_GetRef();
			Instance.MeshAsset = Asset->MeshAsset;
			Instance.Transform = FTransform(
				FRotator(Pitch, DressingRandom.FRandRange(0.0f, 360.0f), Roll),
				FVector(Position, Height), FVector(UniformScale));
			Instance.BiomeTag = BiomeTag;
			++Added;
		}
	};
	const int32 CanopyTarget = Terrain.Archetype == EWorldDirectorTerrainArchetype::Coast ? 900 :
		Terrain.Archetype == EWorldDirectorTerrainArchetype::Marsh ? 760 : 1750;
	AddBiomeLayer(TEXT("Dressing.Canopy"), TEXT("Biome.ForestCanopy"), CanopyTarget, 36000);
	AddBiomeLayer(TEXT("Dressing.GroundCover"), TEXT("Biome.Understory"), 4600, 42000);
	AddBiomeLayer(TEXT("Dressing.Rock"), TEXT("Biome.RockySlope"), 720, 18000);
	AddBiomeLayer(TEXT("Dressing.Deadwood"), TEXT("Biome.ForestFloor"), 280, 12000);
	// Pull a few mature trees into each district's outward edge. The world-scale
	// forest remains procedural, but these restrained clusters make settlement
	// boundaries feel grown into the basin instead of cut out of a bare clearing.
	int32 SettlementGroveCount = 0;
	for (int32 DistrictIndex = 1; DistrictIndex < InOutPlan.DistrictAnchors.Num(); ++DistrictIndex)
	{
		const FVector2D Anchor(InOutPlan.DistrictAnchors[DistrictIndex].Position);
		const FVector2D Outward = (Anchor - CivicCore).GetSafeNormal();
		const FVector2D Side(-Outward.Y, Outward.X);
		int32 AddedForDistrict = 0;
		for (int32 Attempt = 0; Attempt < 72 && AddedForDistrict < 16; ++Attempt)
		{
			const FVector2D Position = Anchor +
				Outward * DressingRandom.FRandRange(2600.0f, 7600.0f) +
				Side * DressingRandom.FRandRange(-6200.0f, 6200.0f);
			const int32 Height = SampleHeightCentimeters(Terrain, Position);
			if (FMath::Max(FMath::Abs(Position.X), FMath::Abs(Position.Y)) > TerrainExtent - 1800.0f ||
				SampleSlopeDegrees(Terrain, Position) > 25.0f ||
				DistanceToRoutes(Position, InOutPlan.Routes) < 920.0f ||
				IsInsideLocationEnvelope(Position, InOutPlan.Locations, 1050.0f) ||
				FarmParcelMask(Position, Terrain.FarmParcels) > 0.2f ||
				(Terrain.WaterLevelCentimeters != INDEX_NONE &&
					Height <= Terrain.WaterLevelCentimeters + 80))
			{
				continue;
			}
			const FWorldEnvironmentDressingAsset* Asset = PickDressingAsset(
				Profile, TEXT("Dressing.Canopy"), DressingRandom);
			if (Asset == nullptr)
			{
				continue;
			}
			const float Scale = DressingRandom.FRandRange(Asset->MinimumScale, Asset->MaximumScale);
			FWorldDirectorDressingInstance& Instance = InOutPlan.Dressing.AddDefaulted_GetRef();
			Instance.MeshAsset = Asset->MeshAsset;
			Instance.Transform = FTransform(
				FRotator(0.0f, DressingRandom.FRandRange(0.0f, 360.0f), 0.0f),
				FVector(Position, Height), FVector(Scale));
			Instance.BiomeTag = TEXT("Biome.SettlementGrove");
			++AddedForDistrict;
			++SettlementGroveCount;
		}
	}
	// Trace the actual rotated parcel perimeter instead of hoping uniform random
	// rejection lands on a narrow field edge. Fence gaps are left wherever a
	// generated route reaches the parcel, creating readable farm entrances.
	for (const FWorldDirectorFarmParcel& Parcel : Terrain.FarmParcels)
	{
		for (int32 EdgeIndex = 0; EdgeIndex < Parcel.BoundaryPoints.Num(); ++EdgeIndex)
		{
			const FVector2D A = Parcel.BoundaryPoints[EdgeIndex];
			const FVector2D B = Parcel.BoundaryPoints[(EdgeIndex + 1) % Parcel.BoundaryPoints.Num()];
			const float EdgeLength = FVector2D::Distance(A, B);
			const int32 SegmentCount = FMath::Max(1, FMath::FloorToInt(EdgeLength / 310.0f));
			const float TangentYaw = FMath::RadiansToDegrees(FMath::Atan2(B.Y - A.Y, B.X - A.X));
			for (int32 Segment = 0; Segment < SegmentCount; ++Segment)
			{
				const FVector2D Position = FMath::Lerp(A, B,
					(static_cast<float>(Segment) + 0.5f) / SegmentCount);
				const int32 Height = SampleHeightCentimeters(Terrain, Position);
				if (FVector2D::Distance(Position, Parcel.GatePosition) < 620.0f ||
					DistanceToRoutes(Position, InOutPlan.Routes) < 420.0f ||
					SampleSlopeDegrees(Terrain, Position) > 11.0f ||
					(Terrain.WaterLevelCentimeters != INDEX_NONE &&
						Height <= Terrain.WaterLevelCentimeters + 70))
				{
					continue;
				}
				const FWorldEnvironmentDressingAsset* Asset = PickDressingAsset(
					Profile, TEXT("Dressing.FarmFence"), DressingRandom);
				if (Asset == nullptr)
				{
					continue;
				}
				const float UniformScale = DressingRandom.FRandRange(
					Asset->MinimumScale, Asset->MaximumScale);
				FWorldDirectorDressingInstance& Instance = InOutPlan.Dressing.AddDefaulted_GetRef();
				Instance.MeshAsset = Asset->MeshAsset;
				Instance.Transform = FTransform(FRotator(0.0f, TangentYaw, 0.0f),
					FVector(Position, Height), FVector(UniformScale));
				Instance.BiomeTag = TEXT("Biome.CultivatedEdge");
			}
		}
		for (int32 AccentIndex = 0; AccentIndex < 3; ++AccentIndex)
		{
			const int32 EdgeIndex = DressingRandom.RandRange(0, Parcel.BoundaryPoints.Num() - 1);
			const FVector2D EdgePoint = FMath::Lerp(
				Parcel.BoundaryPoints[EdgeIndex],
				Parcel.BoundaryPoints[(EdgeIndex + 1) % Parcel.BoundaryPoints.Num()],
				DressingRandom.FRandRange(0.18f, 0.82f));
			const FVector2D Position = FMath::Lerp(
				Parcel.Center, EdgePoint, DressingRandom.FRandRange(0.28f, 0.68f));
			const int32 Height = SampleHeightCentimeters(Terrain, Position);
			if (DistanceToRoutes(Position, InOutPlan.Routes) < 620.0f ||
				SampleSlopeDegrees(Terrain, Position) > 10.0f ||
				(Terrain.WaterLevelCentimeters != INDEX_NONE &&
					Height <= Terrain.WaterLevelCentimeters + 70))
			{
				continue;
			}
			const FWorldEnvironmentDressingAsset* Asset = PickDressingAsset(
				Profile, TEXT("Dressing.FarmAccent"), DressingRandom);
			if (Asset == nullptr)
			{
				continue;
			}
			const float Scale = DressingRandom.FRandRange(Asset->MinimumScale, Asset->MaximumScale);
			FWorldDirectorDressingInstance& Instance = InOutPlan.Dressing.AddDefaulted_GetRef();
			Instance.MeshAsset = Asset->MeshAsset;
			Instance.Transform = FTransform(
				FRotator(0.0f, DressingRandom.FRandRange(0.0f, 360.0f), 0.0f),
				FVector(Position, Height), FVector(Scale));
			Instance.BiomeTag = TEXT("Biome.CultivatedAccent");
		}

		// Put one cart just inside the deliberate route-facing field gate so the
		// parcel reads as a worked farmstead instead of an isolated texture patch.
		const FVector2D GateInward = (Parcel.Center - Parcel.GatePosition).GetSafeNormal();
		const FVector2D BestCartPosition = Parcel.GatePosition + GateInward * 520.0f;
		float CartYaw = Parcel.YawDegrees;
		float BestGateEdgeDistance = MAX_flt;
		for (int32 EdgeIndex = 0; EdgeIndex < Parcel.BoundaryPoints.Num(); ++EdgeIndex)
		{
			const FVector2D A = Parcel.BoundaryPoints[EdgeIndex];
			const FVector2D B = Parcel.BoundaryPoints[(EdgeIndex + 1) % Parcel.BoundaryPoints.Num()];
			const float GateEdgeDistance = DistanceToSegment(Parcel.GatePosition, A, B);
			if (GateEdgeDistance < BestGateEdgeDistance)
			{
				BestGateEdgeDistance = GateEdgeDistance;
				CartYaw = FMath::RadiansToDegrees(FMath::Atan2(B.Y - A.Y, B.X - A.X));
			}
		}
		FarmTransportCount += AddIdentityDressing(
			TEXT("Dressing.Transport"), BestCartPosition, CartYaw,
			TEXT("Biome.FarmTransport"), 10.0f, 180.0f);
	}

	// Give each plot a small, purpose-aware lived-in cluster. Broad biome density
	// cannot provide the carts, stalls, benches, crates, and sacks that make a
	// settlement read as inhabited at player height, so place those explicitly
	// along side and rear walls while preserving the front-door route corridor.
	for (const FResolvedLocationPlan& Location : InOutPlan.Locations)
	{
		const FWorldLocation* SemanticLocation = Spec.Locations.FindByPredicate(
			[&Location](const FWorldLocation& Candidate)
			{
				return Candidate.Id == Location.LocationId;
			});
		FString SemanticIdentity = Location.LocationId + TEXT("|") +
			(SemanticLocation != nullptr ? SemanticLocation->DisplayName : FString());
		SemanticIdentity.ToLowerInline();
		const bool bMarket = SemanticIdentity.Contains(TEXT("market")) ||
			SemanticIdentity.Contains(TEXT("inn")) || SemanticIdentity.Contains(TEXT("ferry"));
		const bool bHome = Location.PurposeTag == TEXT("Purpose.Home") ||
			(SemanticLocation != nullptr && SemanticLocation->PurposeTag == TEXT("Purpose.Home"));
		const int32 ClusterCount = Location.LocationId == InOutPlan.LandmarkLocationId
			? 6 : bHome ? 2 : 4;
		for (int32 ClusterIndex = 0; ClusterIndex < ClusterCount; ++ClusterIndex)
		{
			const bool bUseMarketStall = bMarket && ClusterIndex < 2;
			const FName PlacementTag = bUseMarketStall
				? TEXT("Dressing.MarketStall") : TEXT("Dressing.SettlementClutter");
			const FWorldEnvironmentDressingAsset* Asset = PickDressingAsset(
				Profile, PlacementTag, DressingRandom);
			if (Asset == nullptr)
			{
				continue;
			}
			const float SideSign = ClusterIndex % 2 == 0 ? -1.0f : 1.0f;
			const int32 Row = ClusterIndex / 2;
			const FVector LocalOffset = bUseMarketStall
				? FVector(SideSign * (Location.FootprintSize.X * 0.45f + 380.0f),
					-(Location.FootprintSize.Y * 0.5f + 420.0f + Row * 180.0f), 0.0f)
				: FVector(SideSign * (Location.FootprintSize.X * 0.5f + 180.0f + Row * 105.0f),
					Location.FootprintSize.Y * (0.08f + Row * 0.18f), 0.0f);
			const FVector WorldPosition3D = Location.Transform.TransformPositionNoScale(LocalOffset);
			const FVector2D WorldPosition(WorldPosition3D);
			if (DistanceToRoutes(WorldPosition, InOutPlan.Routes) < 120.0f)
			{
				continue;
			}
			const float UniformScale = DressingRandom.FRandRange(
				Asset->MinimumScale, Asset->MaximumScale);
			FWorldDirectorDressingInstance& Instance = InOutPlan.Dressing.AddDefaulted_GetRef();
			Instance.MeshAsset = Asset->MeshAsset;
			Instance.Transform = FTransform(
				FRotator(0.0f, Location.Transform.Rotator().Yaw +
					(bUseMarketStall ? 0.0f : SideSign * 90.0f) + DressingRandom.FRandRange(-7.0f, 7.0f), 0.0f),
				FVector(WorldPosition, SampleHeightCentimeters(Terrain, WorldPosition)),
				FVector(UniformScale));
			Instance.BiomeTag = bUseMarketStall
				? TEXT("Biome.MarketCluster") : TEXT("Biome.SettlementClutter");
		}

		const uint32 IdentitySeed = static_cast<uint32>(DeriveStageSeed(
			Spec.Seed, Location.LocationId + TEXT("|identity-prop")));
		if (bHome && IdentitySeed % 3U != 0U)
		{
			const float SideSign = (IdentitySeed & 1U) == 0U ? -1.0f : 1.0f;
			const FVector UtilityWorld = Location.Transform.TransformPositionNoScale(FVector(
				SideSign * Location.FootprintSize.X * 0.28f,
				Location.FootprintSize.Y * 0.5f + 285.0f, 0.0f));
			const FVector2D UtilityPosition(UtilityWorld);
			if (DistanceToRoutes(UtilityPosition, InOutPlan.Routes) > 150.0f)
			{
				HomeUtilityCount += AddIdentityDressing(
					TEXT("Dressing.HomeUtility"), UtilityPosition,
					Location.Transform.Rotator().Yaw + SideSign * 18.0f,
					TEXT("Biome.HomeUtility"), 10.0f, 75.0f);
			}
		}
		else if (bMarket)
		{
			const float SideSign = (IdentitySeed & 1U) == 0U ? -1.0f : 1.0f;
			const FVector YardWorld = Location.Transform.TransformPositionNoScale(FVector(
				SideSign * (Location.FootprintSize.X * 0.5f + 310.0f),
				Location.FootprintSize.Y * 0.34f, 0.0f));
			const FVector2D YardPosition(YardWorld);
			if (DistanceToRoutes(YardPosition, InOutPlan.Routes) > 170.0f)
			{
				InnYardCount += AddIdentityDressing(
					TEXT("Dressing.InnYard"), YardPosition,
					Location.Transform.Rotator().Yaw + 90.0f,
					TEXT("Biome.InnYard"), 10.0f, 80.0f);
			}
		}
	}

	// Place a small authored-feeling motif after the broad biome pass. These
	// deterministic pairs turn the morphology into readable place memory: the
	// landmark approach is framed, then the same material language reappears at
	// the outer districts. No new asset is invented; every mesh is selected from
	// the profile's certified dressing palette.
	const FName StoryBiomeTag = ResolveStoryBiomeTag(
		Terrain.Archetype, Terrain.SettlementMorphology);
	auto AddStoryInstance = [&](const FName PlacementTag, const FVector2D& PreferredPosition,
		const float FacingYaw) -> bool
	{
		FVector2D Position = PreferredPosition;
		bool bFound = false;
		for (int32 Attempt = 0; Attempt < 18; ++Attempt)
		{
			if (Attempt > 0)
			{
				const float Angle = Attempt * 2.39996323f + TerrainAngle(InOutPlan.StageSeeds[TEXT("dressing")]);
				const float Radius = 260.0f + (Attempt / 3) * 230.0f;
				Position = PreferredPosition + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
			}
			const int32 Height = SampleHeightCentimeters(Terrain, Position);
			const float MaximumStorySlope = PlacementTag == TEXT("Dressing.Rock") ? 35.0f : 25.0f;
			if (FMath::Max(FMath::Abs(Position.X), FMath::Abs(Position.Y)) <= TerrainExtent - 1500.0f &&
				SampleSlopeDegrees(Terrain, Position) <= MaximumStorySlope &&
				(Terrain.WaterLevelCentimeters == INDEX_NONE || Height > Terrain.WaterLevelCentimeters + 45) &&
				!IsInsideLocationEnvelope(Position, InOutPlan.Locations, 420.0f))
			{
				bFound = true;
				break;
			}
		}
		if (!bFound)
		{
			return false;
		}
		const FWorldEnvironmentDressingAsset* Asset = PickDressingAsset(Profile, PlacementTag, DressingRandom);
		if (Asset == nullptr)
		{
			return false;
		}
		const float Scale = FMath::Lerp(Asset->MinimumScale, Asset->MaximumScale, 0.76f);
		const float Pitch = PlacementTag == TEXT("Dressing.Rock") ? DressingRandom.FRandRange(-8.0f, 8.0f) : 0.0f;
		const float Roll = PlacementTag == TEXT("Dressing.Rock") ? DressingRandom.FRandRange(-8.0f, 8.0f) : 0.0f;
		FWorldDirectorDressingInstance& Instance = InOutPlan.Dressing.AddDefaulted_GetRef();
		Instance.MeshAsset = Asset->MeshAsset;
		Instance.Transform = FTransform(
			FRotator(Pitch, FacingYaw + DressingRandom.FRandRange(-9.0f, 9.0f), Roll),
			FVector(Position, SampleHeightCentimeters(Terrain, Position)), FVector(Scale));
		Instance.BiomeTag = StoryBiomeTag;
		return true;
	};

	const FResolvedLocationPlan* Landmark = InOutPlan.Locations.FindByPredicate(
		[&](const FResolvedLocationPlan& Location)
		{
			return Location.LocationId == InOutPlan.LandmarkLocationId;
		});
	if (Landmark != nullptr)
	{
		const FVector2D LandmarkCenter(Landmark->Transform.GetLocation());
		const FVector2D LandmarkEntrance(Landmark->EntranceTransform.GetLocation());
		const FVector2D ApproachAxis = (LandmarkEntrance - LandmarkCenter).GetSafeNormal();
		const FVector2D ApproachSide(-ApproachAxis.Y, ApproachAxis.X);
		const float FacingYaw = FMath::RadiansToDegrees(FMath::Atan2(-ApproachAxis.Y, -ApproachAxis.X));

		// Give the paved civic court a readable center and frame the guildhall door.
		// These are composed set pieces, not another world-scale scatter layer.
		const FVector2D WellPosition = LandmarkEntrance + ApproachAxis * 920.0f + ApproachSide * 620.0f;
		CivicAnchorCount += AddIdentityDressing(
			TEXT("Dressing.CivicWell"), WellPosition, FacingYaw,
			TEXT("Biome.CivicAnchor"), 8.0f, 90.0f);
		const FVector2D SeatPositions[] = {
			WellPosition + ApproachSide * 560.0f,
			WellPosition - ApproachSide * 560.0f,
			WellPosition + ApproachAxis * 560.0f};
		for (const FVector2D& SeatPosition : SeatPositions)
		{
			const FVector2D ToWell = (WellPosition - SeatPosition).GetSafeNormal();
			const float SeatYaw = FMath::RadiansToDegrees(FMath::Atan2(ToWell.Y, ToWell.X)) + 90.0f;
			CivicSeatCount += AddIdentityDressing(
				TEXT("Dressing.CivicSeat"), SeatPosition, SeatYaw,
				TEXT("Biome.CivicSeat"), 8.0f, 70.0f);
		}
		for (const float SideSign : {-1.0f, 1.0f})
		{
			GuildBannerCount += AddIdentityDressing(
				TEXT("Dressing.GuildBanner"),
				LandmarkEntrance + ApproachSide * SideSign * 520.0f - ApproachAxis * 55.0f,
				Landmark->Transform.Rotator().Yaw,
				TEXT("Biome.GuildBanner"), 9.0f, -1.0f);
		}
		for (int32 PairIndex = 0; PairIndex < 4; ++PairIndex)
		{
			const float Along = 2250.0f + PairIndex * 1550.0f;
			const float Across = 920.0f + (PairIndex % 2) * 340.0f;
			const FName PlacementTag = ResolveStoryPlacementTag(Terrain.Archetype, PairIndex % 2 != 0);
			AddStoryInstance(PlacementTag, LandmarkEntrance + ApproachAxis * Along + ApproachSide * Across, FacingYaw);
			AddStoryInstance(PlacementTag, LandmarkEntrance + ApproachAxis * Along - ApproachSide * Across, FacingYaw);
		}
	}
	for (int32 DistrictIndex = 1; DistrictIndex < InOutPlan.DistrictAnchors.Num(); ++DistrictIndex)
	{
		const FVector2D Anchor(InOutPlan.DistrictAnchors[DistrictIndex].Position);
		const FVector2D Outward = (Anchor - CivicCore).GetSafeNormal();
		const FVector2D Side(-Outward.Y, Outward.X);
		const float FacingYaw = FMath::RadiansToDegrees(FMath::Atan2(-Outward.Y, -Outward.X));
		const FName PlacementTag = ResolveStoryPlacementTag(Terrain.Archetype, DistrictIndex % 2 != 0);
		AddStoryInstance(PlacementTag, Anchor + Side * 1150.0f, FacingYaw);
		AddStoryInstance(PlacementTag, Anchor - Side * 1150.0f, FacingYaw);
	}
	for (int32 DistrictIndex = 1;
		DistrictIndex < InOutPlan.DistrictAnchors.Num() && CommunalFireCount == 0;
		++DistrictIndex)
	{
		const FVector2D Anchor(InOutPlan.DistrictAnchors[DistrictIndex].Position);
		const FVector2D Outward = (Anchor - CivicCore).GetSafeNormal();
		const FVector2D Side(-Outward.Y, Outward.X);
		for (const float SideSign : {-1.0f, 1.0f})
		{
			const FVector2D FirePosition = Anchor + Outward * 1250.0f + Side * SideSign * 760.0f;
			CommunalFireCount += AddIdentityDressing(
				TEXT("Dressing.CommunalFire"), FirePosition,
				FMath::RadiansToDegrees(FMath::Atan2(-Outward.Y, -Outward.X)),
				TEXT("Biome.CommunalFire"), 8.0f, 260.0f);
			if (CommunalFireCount > 0)
			{
				break;
			}
		}
	}

	int32 WayfindingCount = 0;
	int32 LampCount = 0;
	for (int32 RouteIndex = 0; RouteIndex < InOutPlan.Routes.Num(); ++RouteIndex)
	{
		const FResolvedRoutePlan& Route = InOutPlan.Routes[RouteIndex];
		for (int32 Index = 3; Index < Route.ControlPoints.Num() - 1; Index += 6)
		{
			const FVector& Point = Route.ControlPoints[Index];
			const FVector2D Direction = (FVector2D(Route.ControlPoints[Index + 1]) -
				FVector2D(Route.ControlPoints[Index - 1])).GetSafeNormal();
			const FVector2D Side(-Direction.Y, Direction.X);
			const bool bNearSettlement = FVector2D(Point).Size() < 26000.0f;
			FName PlacementTag;
			if (bNearSettlement)
			{
				if (LampCount >= 14 || (RouteIndex + Index / 3) % 2 != 0)
				{
					continue;
				}
				PlacementTag = TEXT("Dressing.Roadside");
			}
			else
			{
				if (WayfindingCount >= 10)
				{
					continue;
				}
				PlacementTag = TEXT("Dressing.Wayfinding");
			}
			if (PlacementTag == TEXT("Dressing.Wayfinding") && WayfindingCount >= 10)
			{
				continue;
			}
			const FWorldEnvironmentDressingAsset* Asset = PickDressingAsset(Profile, PlacementTag, DressingRandom);
			if (Asset == nullptr)
			{
				continue;
			}
			const float SideSign = (RouteIndex + Index) % 2 == 0 ? 1.0f : -1.0f;
			const FVector2D Position = FVector2D(Point) + Side * SideSign *
				(Route.WidthCentimeters * 0.5f + 220.0f);
			const float Scale = DressingRandom.FRandRange(Asset->MinimumScale, Asset->MaximumScale);
			FWorldDirectorDressingInstance& Instance = InOutPlan.Dressing.AddDefaulted_GetRef();
			Instance.MeshAsset = Asset->MeshAsset;
			Instance.Transform = FTransform(
				FRotator(0.0f, FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X)), 0.0f),
				FVector(Position, SampleHeightCentimeters(Terrain, Position)), FVector(Scale));
			Instance.BiomeTag = PlacementTag == TEXT("Dressing.Roadside")
				? TEXT("Biome.SettlementRoad") : TEXT("Biome.Wayfinding");
			LampCount += PlacementTag == TEXT("Dressing.Roadside");
			WayfindingCount += PlacementTag == TEXT("Dressing.Wayfinding");
		}
	}
	UE_LOG(LogWorldDirector, Display,
		TEXT("WORLD_DIRECTOR_DRESSING_COMPOSITION settlementGroves=%d roadsideLamps=%d wayfinding=%d civicAnchors=%d civicSeats=%d guildBanners=%d farmTransport=%d homeUtility=%d innYard=%d communalFire=%d"),
		SettlementGroveCount, LampCount, WayfindingCount, CivicAnchorCount,
		CivicSeatCount, GuildBannerCount, FarmTransportCount, HomeUtilityCount,
		InnYardCount, CommunalFireCount);

	TArray<uint8> LayoutBytes;
	for (const FResolvedLocationPlan& Location : InOutPlan.Locations)
	{
		AppendString(LayoutBytes, Location.LocationId);
		AppendString(LayoutBytes, Location.PurposeTag.ToString());
		AppendInt32(LayoutBytes, Location.bPavedCourtyard ? 1 : 0);
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
		AppendString(DressingBytes, Instance.BiomeTag.ToString());
		AppendInt32(DressingBytes, FMath::RoundToInt(Instance.Transform.GetLocation().X));
		AppendInt32(DressingBytes, FMath::RoundToInt(Instance.Transform.GetLocation().Y));
		AppendInt32(DressingBytes, FMath::RoundToInt(Instance.Transform.GetLocation().Z));
		AppendInt32(DressingBytes, FMath::RoundToInt(Instance.Transform.Rotator().Pitch * 100.0f));
		AppendInt32(DressingBytes, FMath::RoundToInt(Instance.Transform.Rotator().Yaw * 100.0f));
		AppendInt32(DressingBytes, FMath::RoundToInt(Instance.Transform.Rotator().Roll * 100.0f));
		AppendInt32(DressingBytes, FMath::RoundToInt(Instance.Transform.GetScale3D().X * 1000.0f));
	}
	InOutPlan.DressingFingerprint = FingerprintBytes(DressingBytes);
	TArray<uint8> WorldBytes;
	AppendString(WorldBytes, InOutPlan.GeneratorVersion);
	AppendString(WorldBytes, InOutPlan.ContentVersion);
	AppendString(WorldBytes, Terrain.HeightFingerprint);
	AppendString(WorldBytes, Terrain.SurfaceFingerprint);
	AppendInt32(WorldBytes, static_cast<int32>(Terrain.Archetype));
	AppendString(WorldBytes, Terrain.SettlementMorphology.ToString());
	AppendString(WorldBytes, Terrain.EnvironmentalStory);
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
		AppendString(WorldBytes, District.TerrainAffinity.ToString());
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
		TEXT("%s terrain / %s, %.0f cm relief, %.1f deg mean slope, %.0f%% buildable; %d districts, %d plots, %d terrain-routed connections (max grade %.1f%%); surfaces %.0f%% grass / %.0f%% farm / %.0f%% rock; %d biome instances; fingerprint %s"),
		*ArchetypeName(Plan.Terrain.Archetype),
		*Plan.Terrain.SettlementMorphology.ToString(),
		Plan.Terrain.MaximumHeightCentimeters - Plan.Terrain.MinimumHeightCentimeters,
		Plan.Terrain.MeanSlopeDegrees, Plan.Terrain.BuildableRatio * 100.0f,
		Plan.DistrictAnchors.Num(), Plan.Locations.Num(), Plan.Routes.Num(),
		MaximumRoadGrade * 100.0f, Grass / SurfaceCount * 100.0f, Farm / SurfaceCount * 100.0f,
		Rock / SurfaceCount * 100.0f, Plan.Dressing.Num(), *Plan.WorldFingerprint.Left(12));
}
