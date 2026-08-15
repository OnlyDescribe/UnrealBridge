#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeSmartObjectLibrary.generated.h"

USTRUCT(BlueprintType)
struct FBridgeSmartObjectCreateResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString AssetPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString Error;
};

USTRUCT(BlueprintType)
struct FBridgeSmartObjectValidationMessage
{
	GENERATED_BODY()

	/** Info, Warning, Error, or CriticalError. */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString Severity;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString Message;
};

USTRUCT(BlueprintType)
struct FBridgeSmartObjectValidationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") TArray<FBridgeSmartObjectValidationMessage> Messages;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString Error;
};

USTRUCT(BlueprintType)
struct FBridgeSmartObjectDefinitionInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString AssetPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString UserTagFilterJson;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") TArray<FString> ActivityTags;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString UserTagsFilteringPolicy;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString ActivityTagsMergingPolicy;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString WorldConditionSchemaClassPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString PreviewObjectActorClassPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString PreviewObjectMeshPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString PreviewUserActorClassPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString PreviewValidationFilterClassPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString RootBindableId;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString ParametersBindableId;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") int32 SlotCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") int32 DefaultBehaviorCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") int32 DefinitionDataCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") int32 ObjectConditionCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") int32 ParameterCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") int32 BindingCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") bool bHasBeenValidated = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") bool bValid = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") bool bDirty = false;
};

USTRUCT(BlueprintType)
struct FBridgeSmartObjectSlotInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString Id;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") int32 Index = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FVector Offset = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FRotator Rotation = FRotator::ZeroRotator;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") bool bEnabled = true;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString UserTagFilterJson;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") TArray<FString> ActivityTags;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") TArray<FString> RuntimeTags;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") int32 BehaviorCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") int32 DefinitionDataCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") int32 ConditionCount = 0;
};

USTRUCT(BlueprintType)
struct FBridgeSmartObjectTypeInfo
{
	GENERATED_BODY()

	/** Behavior, DefinitionData, Annotation, or WorldCondition. */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString Kind;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString TypePath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString DisplayName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") bool bAllowedBySchema = true;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") bool bAllowedAtDefinition = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") bool bAllowedAtSlot = false;
};

USTRUCT(BlueprintType)
struct FBridgeSmartObjectBehaviorInfo
{
	GENERATED_BODY()

	/** Empty means a definition-level default behavior. */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString SlotId;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") int32 Index = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString ObjectPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString ClassPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") int32 PropertyCount = 0;
};

USTRUCT(BlueprintType)
struct FBridgeSmartObjectDefinitionDataInfo
{
	GENERATED_BODY()

	/** Empty means definition-level data; a GUID means slot data/annotation. */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString SlotId;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString Id;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") int32 Index = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString TypePath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString DisplayName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") bool bAnnotation = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") bool bHasTransform = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") int32 PropertyCount = 0;
};

USTRUCT(BlueprintType)
struct FBridgeSmartObjectPropertyInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString Path;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString DisplayName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString Type;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString Value;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString Category;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") bool bEditable = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") bool bBindable = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") bool bInherited = false;
};

USTRUCT(BlueprintType)
struct FBridgeSmartObjectPropertyResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString Value;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString Type;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString Error;
};

USTRUCT(BlueprintType)
struct FBridgeSmartObjectWorldConditionInfo
{
	GENERATED_BODY()

	/** Empty means object preconditions; a GUID means one slot's preconditions. */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString SlotId;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") int32 Index = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString TypePath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString DisplayName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString Operator;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") int32 ExpressionDepth = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") bool bInvert = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") int32 PropertyCount = 0;
};

USTRUCT(BlueprintType)
struct FBridgeSmartObjectParameterInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString Id;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString Type;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString Value;
};

USTRUCT(BlueprintType)
struct FBridgeSmartObjectBindableStructInfo
{
	GENERATED_BODY()

	/** Root, Parameters, Slot, or DefinitionData. */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString Kind;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString Id;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString TypePath;
};

USTRUCT(BlueprintType)
struct FBridgeSmartObjectBindingInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString SourceId;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString SourcePath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString TargetId;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString TargetPath;
};

USTRUCT(BlueprintType)
struct FBridgeSmartObjectComponentInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString ComponentPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString ComponentName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString OwnerPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString OwnerLabel;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString WorldType;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString BaseDefinitionPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString AppliedDefinitionPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString RegisteredHandle;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString RegistrationType;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FTransform Transform;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FVector BoundsMin = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FVector BoundsMax = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") bool bBoundToSimulation = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") bool bEnabled = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") bool bCanBePartOfCollection = false;
};

USTRUCT(BlueprintType)
struct FBridgeSmartObjectCollectionInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString ActorPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString ActorLabel;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString WorldType;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") int32 EntryCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FVector BoundsMin = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FVector BoundsMax = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") bool bRegistered = false;
};

USTRUCT(BlueprintType)
struct FBridgeSmartObjectCollectionEntryInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") int32 Index = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString SmartObjectHandle;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString ComponentPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString DefinitionPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FTransform Transform;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FVector BoundsMin = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FVector BoundsMax = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") TArray<FString> Tags;
};

USTRUCT(BlueprintType)
struct FBridgeSmartObjectQueryResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") int32 Rank = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") float Distance = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString SmartObjectHandle;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString SlotHandle;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString ComponentPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString OwnerPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString DefinitionPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FTransform SlotTransform;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString SlotState;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") TArray<FString> ActivityTags;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") TArray<FString> RuntimeTags;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") TArray<FString> BehaviorClassPaths;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") bool bEnabled = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") bool bCanBeClaimed = false;
};

USTRUCT(BlueprintType)
struct FBridgeSmartObjectRuntimeSlotInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString SmartObjectHandle;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString SlotHandle;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") int32 SlotIndex = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString SlotState;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString DefinitionPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString ComponentPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString OwnerPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FTransform SlotTransform;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") TArray<FString> ActivityTags;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") TArray<FString> RuntimeTags;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") bool bEnabled = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") bool bCanBeClaimed = false;
};

USTRUCT(BlueprintType)
struct FBridgeSmartObjectClaimResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") bool bSuccess = false;
	/** Opaque editor-session token used by occupy/release; never persist it in an asset. */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString ClaimToken;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString SmartObjectHandle;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString SlotHandle;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString UserActorPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString Priority;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString SlotState;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString BehaviorObjectPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString Error;
};

/** Options shared by live entrance lookup and offline definition entrance validation. */
USTRUCT(BlueprintType)
struct FBridgeSmartObjectEntranceRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UnrealBridge|SmartObject") FString UserActorPath;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UnrealBridge|SmartObject") FString ValidationFilterClassPath;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UnrealBridge|SmartObject") FVector SearchLocation = FVector::ZeroVector;
	/** First or NearestToSearchLocation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UnrealBridge|SmartObject") FString SelectionMethod = TEXT("First");
	/** Entry or Exit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UnrealBridge|SmartObject") FString LocationType = TEXT("Entry");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UnrealBridge|SmartObject") float CapsuleRadius = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UnrealBridge|SmartObject") float CapsuleHeight = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UnrealBridge|SmartObject") float CapsuleStepHeight = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UnrealBridge|SmartObject") bool bProjectNavigationLocation = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UnrealBridge|SmartObject") bool bTraceGroundLocation = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UnrealBridge|SmartObject") bool bCheckTransitionTrajectory = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UnrealBridge|SmartObject") bool bCheckEntranceLocationOverlap = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UnrealBridge|SmartObject") bool bCheckSlotLocationOverlap = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UnrealBridge|SmartObject") bool bUseSlotLocationAsFallback = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UnrealBridge|SmartObject") bool bUseUpAxisLockedRotation = false;
};

USTRUCT(BlueprintType)
struct FBridgeSmartObjectEntranceResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") bool bFound = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") bool bValid = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString SlotHandle;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FVector Location = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FRotator Rotation = FRotator::ZeroRotator;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") TArray<FString> Tags;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") bool bHasNavigationNode = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|SmartObject") FString Error;
};

/**
 * Smart Object definition authoring, condition/binding editing, world collection
 * management, spatial queries, claim lifecycle, entrance validation, and debug control.
 *
 * The functional implementation is available on UE 5.7+. Older supported engines
 * keep the reflected class and return safe defaults with a descriptive last error.
 */
UCLASS()
class UNREALBRIDGE_API UUnrealBridgeSmartObjectLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Capability and definition lifecycle

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool IsSmartObjectApiAvailable();

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static FString GetLastSmartObjectError();

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static FBridgeSmartObjectCreateResult CreateSmartObjectDefinition(const FString& AssetPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static FBridgeSmartObjectDefinitionInfo GetSmartObjectDefinitionInfo(const FString& AssetPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static FBridgeSmartObjectValidationResult ValidateSmartObjectDefinition(const FString& AssetPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static TArray<FBridgeSmartObjectPropertyInfo> ListSmartObjectDefinitionProperties(
		const FString& AssetPath, bool bIncludeInherited = true);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static FBridgeSmartObjectPropertyResult GetSmartObjectDefinitionProperty(
		const FString& AssetPath, const FString& PropertyPath);

	/** Value uses Unreal export-text syntax. Structural arrays have dedicated APIs and are rejected here. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool SetSmartObjectDefinitionProperty(
		const FString& AssetPath, const FString& PropertyPath, const FString& Value);

	/** SlotId empty targets the definition query. Returns Epic's FGameplayTagQuery expression JSON. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static FString GetSmartObjectTagQueryJson(const FString& AssetPath, const FString& SlotId = TEXT(""));

	/** Empty QueryJson clears the query. SlotId empty targets the definition query. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool SetSmartObjectTagQueryJson(const FString& AssetPath, const FString& QueryJson,
		const FString& SlotId = TEXT(""));

	/** TagSet is Activity, or Runtime for slots. The full set is replaced. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool SetSmartObjectTags(const FString& AssetPath, const TArray<FString>& Tags,
		const FString& SlotId = TEXT(""), const FString& TagSet = TEXT("Activity"));

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool SetSmartObjectTagPolicies(const FString& AssetPath,
		const FString& UserTagsFilteringPolicy, const FString& ActivityTagsMergingPolicy);

	// Slots

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static TArray<FBridgeSmartObjectSlotInfo> ListSmartObjectSlots(const FString& AssetPath);

	/** InsertIndex -1 appends. Returns the new stable editor GUID. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static FString AddSmartObjectSlot(const FString& AssetPath, const FString& Name,
		const FVector& Offset, const FRotator& Rotation, bool bEnabled = true, int32 InsertIndex = -1);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static FString DuplicateSmartObjectSlot(const FString& AssetPath, const FString& SourceSlotId,
		const FString& NewName = TEXT(""), int32 InsertIndex = -1);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool RemoveSmartObjectSlot(const FString& AssetPath, const FString& SlotId);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool MoveSmartObjectSlot(const FString& AssetPath, const FString& SlotId, int32 NewIndex);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static TArray<FBridgeSmartObjectPropertyInfo> ListSmartObjectSlotProperties(
		const FString& AssetPath, const FString& SlotId);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static FBridgeSmartObjectPropertyResult GetSmartObjectSlotProperty(
		const FString& AssetPath, const FString& SlotId, const FString& PropertyPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool SetSmartObjectSlotProperty(const FString& AssetPath, const FString& SlotId,
		const FString& PropertyPath, const FString& Value);

	// Behavior definitions

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static TArray<FBridgeSmartObjectTypeInfo> ListSmartObjectBehaviorTypes();

	/** SlotId empty lists definition-level fallback behaviors. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static TArray<FBridgeSmartObjectBehaviorInfo> ListSmartObjectBehaviorDefinitions(
		const FString& AssetPath, const FString& SlotId = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static FString AddSmartObjectBehaviorDefinition(const FString& AssetPath, const FString& BehaviorClassPath,
		const FString& SlotId = TEXT(""), int32 InsertIndex = -1);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool RemoveSmartObjectBehaviorDefinition(const FString& AssetPath, const FString& BehaviorObjectPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool MoveSmartObjectBehaviorDefinition(const FString& AssetPath,
		const FString& BehaviorObjectPath, int32 NewIndex);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static TArray<FBridgeSmartObjectPropertyInfo> ListSmartObjectBehaviorProperties(
		const FString& AssetPath, const FString& BehaviorObjectPath, bool bIncludeInherited = true);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static FBridgeSmartObjectPropertyResult GetSmartObjectBehaviorProperty(const FString& AssetPath,
		const FString& BehaviorObjectPath, const FString& PropertyPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool SetSmartObjectBehaviorProperty(const FString& AssetPath, const FString& BehaviorObjectPath,
		const FString& PropertyPath, const FString& Value);

	// Definition data and slot annotations

	/** SlotId empty discovers definition-level data; a slot GUID discovers slot data/annotations. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static TArray<FBridgeSmartObjectTypeInfo> ListSmartObjectDefinitionDataTypes(
		const FString& AssetPath, const FString& SlotId = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static TArray<FBridgeSmartObjectDefinitionDataInfo> ListSmartObjectDefinitionData(
		const FString& AssetPath, const FString& SlotId = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static FString AddSmartObjectDefinitionData(const FString& AssetPath, const FString& StructTypePath,
		const FString& SlotId = TEXT(""), int32 InsertIndex = -1);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool RemoveSmartObjectDefinitionData(const FString& AssetPath, const FString& DataId);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool MoveSmartObjectDefinitionData(const FString& AssetPath, const FString& DataId, int32 NewIndex);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static TArray<FBridgeSmartObjectPropertyInfo> ListSmartObjectDefinitionDataProperties(
		const FString& AssetPath, const FString& DataId, bool bIncludeInherited = true);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static FBridgeSmartObjectPropertyResult GetSmartObjectDefinitionDataProperty(const FString& AssetPath,
		const FString& DataId, const FString& PropertyPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool SetSmartObjectDefinitionDataProperty(const FString& AssetPath, const FString& DataId,
		const FString& PropertyPath, const FString& Value);

	// Object and slot selection conditions

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static TArray<FBridgeSmartObjectTypeInfo> ListSmartObjectWorldConditionTypes(
		const FString& AssetPath, const FString& SlotId = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static TArray<FBridgeSmartObjectWorldConditionInfo> ListSmartObjectWorldConditions(
		const FString& AssetPath, const FString& SlotId = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static int32 AddSmartObjectWorldCondition(const FString& AssetPath, const FString& ConditionStructPath,
		const FString& SlotId = TEXT(""), const FString& Operator = TEXT("And"),
		int32 ExpressionDepth = 0, bool bInvert = false, int32 InsertIndex = -1);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool RemoveSmartObjectWorldCondition(const FString& AssetPath,
		const FString& SlotId, int32 ConditionIndex);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool MoveSmartObjectWorldCondition(const FString& AssetPath,
		const FString& SlotId, int32 ConditionIndex, int32 NewIndex);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool SetSmartObjectWorldConditionExpression(const FString& AssetPath, const FString& SlotId,
		int32 ConditionIndex, const FString& Operator, int32 ExpressionDepth, bool bInvert);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static TArray<FBridgeSmartObjectPropertyInfo> ListSmartObjectWorldConditionProperties(
		const FString& AssetPath, const FString& SlotId, int32 ConditionIndex, bool bIncludeInherited = true);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static FBridgeSmartObjectPropertyResult GetSmartObjectWorldConditionProperty(const FString& AssetPath,
		const FString& SlotId, int32 ConditionIndex, const FString& PropertyPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool SetSmartObjectWorldConditionProperty(const FString& AssetPath, const FString& SlotId,
		int32 ConditionIndex, const FString& PropertyPath, const FString& Value);

	// Definition parameters and property bindings

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static TArray<FBridgeSmartObjectParameterInfo> ListSmartObjectParameters(const FString& AssetPath);

	/** Type syntax matches StateTree property bags, e.g. Float or Object:/Script/Engine.Actor. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static FString AddSmartObjectParameter(const FString& AssetPath, const FString& Name,
		const FString& Type, const FString& DefaultValue = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool RemoveSmartObjectParameter(const FString& AssetPath, const FString& Name);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool RenameSmartObjectParameter(const FString& AssetPath,
		const FString& OldName, const FString& NewName);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool SetSmartObjectParameterValue(const FString& AssetPath,
		const FString& Name, const FString& Value);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static TArray<FBridgeSmartObjectBindableStructInfo> ListSmartObjectBindableStructs(const FString& AssetPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static TArray<FBridgeSmartObjectBindingInfo> ListSmartObjectBindings(const FString& AssetPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool AddSmartObjectBinding(const FString& AssetPath, const FString& SourceId,
		const FString& SourcePath, const FString& TargetId, const FString& TargetPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool RemoveSmartObjectBinding(const FString& AssetPath,
		const FString& TargetId, const FString& TargetPath);

	// Loaded world components and persistent collections

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static TArray<FBridgeSmartObjectComponentInfo> ListSmartObjectComponents(bool bPIEOnly = false);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static FBridgeSmartObjectComponentInfo GetSmartObjectComponentInfo(const FString& ComponentPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static FString AddSmartObjectComponent(const FString& ActorPath, const FString& DefinitionAssetPath,
		const FString& ComponentName = TEXT("SmartObject"), bool bCanBePartOfCollection = false,
		bool bRegisterWithSubsystem = true);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool RemoveSmartObjectComponent(const FString& ComponentPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool SetSmartObjectComponentDefinition(const FString& ComponentPath,
		const FString& DefinitionAssetPath, bool bRegisterWithSubsystem = true);

	/** Action: Register, Unregister, RemoveFromSimulation, Refresh, Enable, or Disable. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool ControlSmartObjectComponent(const FString& ComponentPath, const FString& Action);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static TArray<FBridgeSmartObjectCollectionInfo> ListPersistentSmartObjectCollections(bool bPIEOnly = false);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static FString CreatePersistentSmartObjectCollection(const FString& ActorLabel = TEXT("SmartObjectPersistentCollection"));

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool DestroyPersistentSmartObjectCollection(const FString& CollectionActorPath);

	/** Action: Rebuild, Clear, Register, or Unregister. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool ControlPersistentSmartObjectCollection(const FString& CollectionActorPath, const FString& Action);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static TArray<FBridgeSmartObjectCollectionEntryInfo> ListPersistentSmartObjectCollectionEntries(
		const FString& CollectionActorPath);

	// Runtime query, lifecycle, tags, and entrance validation

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static TArray<FBridgeSmartObjectQueryResult> QuerySmartObjects(const FVector& Center, const FVector& Extent,
		const TArray<FString>& UserTags, const TArray<FString>& ActivityTags,
		const TArray<FString>& BehaviorClassPaths, const FString& ActivityMatch = TEXT("All"),
		const FString& ClaimPriority = TEXT("Normal"), bool bEvaluateConditions = true,
		bool bIncludeClaimedSlots = false, bool bIncludeDisabledSlots = false,
		const FString& UserActorPath = TEXT(""), bool bSortByDistance = true, int32 MaxResults = 0);

	/** Exactly one of SmartObjectHandle or ComponentPath should normally be supplied. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static TArray<FBridgeSmartObjectRuntimeSlotInfo> ListSmartObjectRuntimeSlots(
		const FString& SmartObjectHandle = TEXT(""), const FString& ComponentPath = TEXT(""),
		const FString& ClaimPriority = TEXT("Normal"));

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static FString CreateRuntimeSmartObject(const FString& DefinitionAssetPath, const FTransform& Transform,
		const FString& OwnerActorPath = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool DestroyRuntimeSmartObject(const FString& SmartObjectHandle);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static FBridgeSmartObjectClaimResult ClaimSmartObjectSlot(const FString& SlotHandle,
		const FString& UserActorPath = TEXT(""), const FString& ClaimPriority = TEXT("Normal"));

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static FBridgeSmartObjectClaimResult OccupySmartObjectClaim(const FString& ClaimToken,
		const FString& BehaviorClassPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool ReleaseSmartObjectClaim(const FString& ClaimToken);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static TArray<FBridgeSmartObjectClaimResult> ListSmartObjectClaims();

	/** Scope is Object or Slot. Existing runtime tags are replaced when bReplace is true. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool SetSmartObjectRuntimeTags(const FString& Handle, const FString& Scope,
		const TArray<FString>& Tags, bool bReplace = true);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool SetSmartObjectRuntimeEnabled(const FString& SmartObjectHandle, bool bEnabled,
		const FString& ReasonTag = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool SetSmartObjectRuntimeSlotEnabled(const FString& SlotHandle, bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool SendSmartObjectSlotEvent(const FString& SlotHandle, const FString& EventTag);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static FBridgeSmartObjectEntranceResult FindSmartObjectEntrance(const FString& SlotHandle,
		const FBridgeSmartObjectEntranceRequest& Request);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static TArray<FBridgeSmartObjectEntranceResult> ValidateSmartObjectDefinitionEntrances(
		const FString& AssetPath, const FTransform& OwnerTransform,
		const FBridgeSmartObjectEntranceRequest& Request, const FString& SkipActorPath = TEXT(""));

	/** Action: InitializeRuntime, CleanupRuntime, RegisterAll, or UnregisterAll. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|SmartObject")
	static bool DebugSmartObjectSubsystem(const FString& Action);
};
