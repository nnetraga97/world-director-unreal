#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

#include "WorldDirectorTypes.generated.h"

UENUM(BlueprintType)
enum class EWorldDirectorValidationSeverity : uint8
{
	Error,
	Warning
};

UENUM(BlueprintType)
enum class EWorldDirectorAccessPolicy : uint8
{
	Public,
	Residents,
	Workers,
	Restricted,
	Private
};

UENUM(BlueprintType)
enum class EWorldDirectorProjectState : uint8
{
	Proposed,
	Validated,
	PermissionResolved,
	Scheduled,
	Preparation,
	Transition,
	Active,
	Delayed,
	Refused,
	Failed
};

UENUM(BlueprintType)
enum class EWorldDirectorResidentAvailability : uint8
{
	Available,
	Traveling,
	Sleeping,
	Working,
	Socializing,
	Waiting
};

UENUM(BlueprintType)
enum class EWorldDirectorTerrainArchetype : uint8
{
	Basin,
	Valley,
	Ridge,
	Coast,
	Marsh
};

UENUM(BlueprintType)
enum class EWorldDirectorSurfaceType : uint8
{
	Grass,
	Gravel,
	Paving,
	Farmfield,
	Rock,
	Water
};

USTRUCT(BlueprintType)
struct WORLDDIRECTORRUNTIME_API FResidentMemory
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString FactId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Summary;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Day = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EmotionalSignificance = 0.0f;
};

USTRUCT(BlueprintType)
struct WORLDDIRECTORRUNTIME_API FWorldBrief
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Version = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Theme;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString SettlementIdentity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString PlayerPrompt;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FString> Premises;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FString> TerrainPreferences;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FName> RequiredCapabilityTags;
};

USTRUCT(BlueprintType)
struct WORLDDIRECTORRUNTIME_API FTownTopologyEdge
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString FromLocationId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString ToLocationId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName RouteType = TEXT("Path");
};

USTRUCT(BlueprintType)
struct WORLDDIRECTORRUNTIME_API FTownTopology
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Version = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FString> Districts;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Governance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString HistoricalWound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString CurrentTension;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString SupernaturalPremise;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString CentralThreat;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FTownTopologyEdge> Edges;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FString> LandmarkLocationIds;
};

USTRUCT(BlueprintType)
struct WORLDDIRECTORRUNTIME_API FWorldLocation
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Version = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName PurposeTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString OwnerResidentId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString ControllerResidentId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EWorldDirectorAccessPolicy AccessPolicy = EWorldDirectorAccessPolicy::Public;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 ResidentCapacity = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bRepurposable = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FName> RequiredCapabilityTags;
};

USTRUCT(BlueprintType)
struct WORLDDIRECTORRUNTIME_API FResident
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Version = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString HomeLocationId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString WorkplaceLocationId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString HouseholdId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName OccupationTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bEmployed = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString CurrentLocationId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Motivation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Fear;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FResidentMemory> ImportantMemories;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EWorldDirectorResidentAvailability Availability = EWorldDirectorResidentAvailability::Available;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FString> BeliefIds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FString> RelationshipIds;
};

USTRUCT(BlueprintType)
struct WORLDDIRECTORRUNTIME_API FHousehold
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Version = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString HomeLocationId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FString> MemberResidentIds;
};

USTRUCT(BlueprintType)
struct WORLDDIRECTORRUNTIME_API FRelationship
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Version = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString SourceResidentId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString TargetResidentId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName RelationshipType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bBidirectional = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString ReciprocalRelationshipId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Trust = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Affinity = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Fear = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Obligation = 0.0f;
};

USTRUCT(BlueprintType)
struct WORLDDIRECTORRUNTIME_API FBelief
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Version = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString HolderResidentId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString FactId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Confidence = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bImportantSecret = false;

	/** Empty means the holder learned the fact directly from the world. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString SourceResidentId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Secrecy = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EmotionalSignificance = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WillingnessToShare = 0.5f;
};

USTRUCT(BlueprintType)
struct WORLDDIRECTORRUNTIME_API FWorldFact
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Version = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Statement;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 EstablishedDay = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bEstablished = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bSecret = false;
};

USTRUCT(BlueprintType)
struct WORLDDIRECTORRUNTIME_API FWorldEvent
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Version = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Day = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Summary;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FString> ParticipantResidentIds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FString> LocationIds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FString> RevealedFactIds;
};

USTRUCT(BlueprintType)
struct WORLDDIRECTORRUNTIME_API FThreat
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Version = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName ThreatType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Premise;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FString> AffectedLocationIds;
};

USTRUCT(BlueprintType)
struct WORLDDIRECTORRUNTIME_API FChangeProject
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Version = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString InitiatorResidentId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString TargetLocationId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName DesiredPurposeTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Reason;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FString> RequiredParticipantResidentIds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FName> RequiredCapabilityTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FName> RequiredConditionTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int64 IntendedStartMinute = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 RequiredTransitionMinutes = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EWorldDirectorProjectState State = EWorldDirectorProjectState::Proposed;
};

USTRUCT(BlueprintType)
struct WORLDDIRECTORRUNTIME_API FGeneratedWorldSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Version = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Seed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FWorldBrief Brief;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FTownTopology Topology;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FWorldLocation> Locations;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FResident> Residents;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FHousehold> Households;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FRelationship> Relationships;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FBelief> Beliefs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FWorldFact> Facts;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FWorldEvent> Events;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FThreat> Threats;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FChangeProject> ChangeProjects;
};

USTRUCT(BlueprintType)
struct WORLDDIRECTORRUNTIME_API FResolvedLocationPlan
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString LocationId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FSoftObjectPath ShellAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FSoftObjectPath InteriorAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FTransform Transform;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FTransform EntranceTransform;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector2D FootprintSize = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bRepurposable = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString DistrictId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 GroundHeightCentimeters = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float GroundSlopeDegrees = 0.0f;
};

USTRUCT(BlueprintType)
struct WORLDDIRECTORRUNTIME_API FWorldDirectorScheduleStop
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Hour = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString LocationId;
};

USTRUCT(BlueprintType)
struct WORLDDIRECTORRUNTIME_API FResolvedResidentPlan
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString ResidentId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString HomeLocationId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString WorkplaceLocationId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FSoftObjectPath SkeletalMeshAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FSoftObjectPath> ModularPartAssets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FSoftObjectPath IdleAnimationAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FTransform SpawnTransform;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FWorldDirectorScheduleStop> Schedule;
};

USTRUCT(BlueprintType)
struct WORLDDIRECTORRUNTIME_API FResolvedRoutePlan
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString FromLocationId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString ToLocationId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName RouteType = TEXT("Path");

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FTransform Transform;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FVector> ControlPoints;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float WidthCentimeters = 360.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MaximumGrade = 0.0f;
};

USTRUCT(BlueprintType)
struct WORLDDIRECTORRUNTIME_API FWorldDirectorDistrictAnchor
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString DistrictId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector Position = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float InfluenceRadiusCentimeters = 3500.0f;
};

USTRUCT(BlueprintType)
struct WORLDDIRECTORRUNTIME_API FWorldDirectorTerrainRecipe
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EWorldDirectorTerrainArchetype Archetype = EWorldDirectorTerrainArchetype::Basin;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Resolution = 65;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 ExtentCentimeters = 14000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<int32> HeightsCentimeters;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<uint8> SurfaceTypes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FVector> WaterControlPoints;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 WaterLevelCentimeters = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MinimumHeightCentimeters = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MaximumHeightCentimeters = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MeanSlopeDegrees = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString HeightFingerprint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString SurfaceFingerprint;
};

USTRUCT(BlueprintType)
struct WORLDDIRECTORRUNTIME_API FWorldDirectorDressingInstance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FSoftObjectPath MeshAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FTransform Transform;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName BiomeTag;
};

USTRUCT(BlueprintType)
struct WORLDDIRECTORRUNTIME_API FResolvedWorldPlan
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Version = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString SourceSpecId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Seed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FSoftObjectPath TerrainMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString GeneratorVersion = TEXT("worldgen-physical-v2");

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString ContentVersion = TEXT("stylized-village-1");

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName EnvironmentProfile = TEXT("Profile.StylizedVillage");

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TMap<FString, int32> StageSeeds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FWorldDirectorTerrainRecipe Terrain;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FWorldDirectorDistrictAnchor> DistrictAnchors;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString LandmarkLocationId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FResolvedLocationPlan> Locations;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FResolvedResidentPlan> Residents;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FResolvedRoutePlan> Routes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FWorldDirectorDressingInstance> Dressing;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString LayoutFingerprint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString RouteFingerprint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString DressingFingerprint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString WorldFingerprint;
};

USTRUCT(BlueprintType)
struct WORLDDIRECTORRUNTIME_API FValidationIssue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EWorldDirectorValidationSeverity Severity = EWorldDirectorValidationSeverity::Error;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName Code;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Path;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Message;
};

USTRUCT(BlueprintType)
struct WORLDDIRECTORRUNTIME_API FValidationReport
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Version = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bValid = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FValidationIssue> Issues;

	void AddError(FName Code, const FString& Path, const FString& Message);
	void AddWarning(FName Code, const FString& Path, const FString& Message);
};
