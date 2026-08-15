#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeRigLibrary.generated.h"

USTRUCT(BlueprintType)
struct FBridgeRigOperationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig") bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig") FString AssetPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig") FString Error;
};

USTRUCT(BlueprintType)
struct FBridgeRigTypeInfo
{
	GENERATED_BODY()

	/** ControlRigUnit, RigVMTemplate, IKSolver, or RetargetOp. */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig") FString Kind;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig") FString TypePath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig") FString DisplayName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig") FString Category;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig") bool bDeprecated = false;
};

USTRUCT(BlueprintType)
struct FBridgeRigPropertyInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig") FString Path;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig") FString DisplayName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig") FString Type;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig") FString Value;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig") FString Category;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig") bool bEditable = false;
};

USTRUCT(BlueprintType)
struct FBridgeRigPropertyResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig") bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig") FString Value;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig") FString Type;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig") FString Error;
};

USTRUCT(BlueprintType)
struct FBridgeRigValidationIssue
{
	GENERATED_BODY()

	/** Error, Warning, or Info. */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig") FString Severity;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig") FString Code;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig") FString Subject;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig") FString Message;
};

USTRUCT(BlueprintType)
struct FBridgeRigValidationReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig") bool bFound = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig") bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig") bool bCompiled = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig") bool bSaved = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig") int32 ErrorCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig") int32 WarningCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig") TArray<FBridgeRigValidationIssue> Issues;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig") FString Error;
};

USTRUCT(BlueprintType)
struct FBridgeControlRigInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|ControlRig") FString AssetPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|ControlRig") FString PreviewSkeletalMeshPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|ControlRig") FString GeneratedClassPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|ControlRig") int32 BoneCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|ControlRig") int32 ControlCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|ControlRig") int32 NullCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|ControlRig") int32 CurveCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|ControlRig") int32 ConnectorCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|ControlRig") int32 GraphCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|ControlRig") int32 NodeCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|ControlRig") bool bModularRig = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|ControlRig") bool bDirty = false;
};

USTRUCT(BlueprintType)
struct FBridgeRigElementInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Hierarchy") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Hierarchy") FString Type;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Hierarchy") FString ParentName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Hierarchy") FString ParentType;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Hierarchy") FString DisplayName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Hierarchy") FString ControlType;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Hierarchy") FTransform InitialLocalTransform = FTransform::Identity;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Hierarchy") FTransform InitialGlobalTransform = FTransform::Identity;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Hierarchy") FTransform CurrentLocalTransform = FTransform::Identity;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Hierarchy") FTransform CurrentGlobalTransform = FTransform::Identity;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Hierarchy") TArray<FString> Tags;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Hierarchy") bool bImportedBone = false;
};

USTRUCT(BlueprintType)
struct FBridgeRigVMGraphInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|RigVM") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|RigVM") FString NodePath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|RigVM") int32 NodeCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|RigVM") int32 LinkCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|RigVM") bool bFunctionLibrary = false;
};

USTRUCT(BlueprintType)
struct FBridgeRigVMPinInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|RigVM") FString Path;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|RigVM") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|RigVM") FString Direction;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|RigVM") FString CPPType;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|RigVM") FString CPPTypeObjectPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|RigVM") FString DefaultValue;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|RigVM") bool bArray = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|RigVM") bool bLinked = false;
};

USTRUCT(BlueprintType)
struct FBridgeRigVMNodeInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|RigVM") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|RigVM") FString Path;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|RigVM") FString Title;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|RigVM") FString ClassName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|RigVM") FVector2D Position = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|RigVM") TArray<FBridgeRigVMPinInfo> Pins;
};

USTRUCT(BlueprintType)
struct FBridgeRigVMLinkInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|RigVM") FString SourcePinPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|RigVM") FString TargetPinPath;
};

USTRUCT(BlueprintType)
struct FBridgeRigLayoutResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|RigVM") bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|RigVM") int32 NodesPositioned = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|RigVM") int32 LayerCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|RigVM") TArray<FString> Warnings;
};

USTRUCT(BlueprintType)
struct FBridgeRigNamedTransform
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "UnrealBridge|Rig") FString Name;
	UPROPERTY(BlueprintReadWrite, Category = "UnrealBridge|Rig") FString Type;
	UPROPERTY(BlueprintReadWrite, Category = "UnrealBridge|Rig") FTransform Transform = FTransform::Identity;
};

USTRUCT(BlueprintType)
struct FBridgeControlRigEvaluationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|ControlRig") bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|ControlRig") FString EventName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|ControlRig") TArray<FBridgeRigNamedTransform> Controls;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|ControlRig") TArray<FBridgeRigNamedTransform> Bones;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|ControlRig") FString Error;
};

USTRUCT(BlueprintType)
struct FBridgeIKRigInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|IKRig") FString AssetPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|IKRig") FString PreviewSkeletalMeshPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|IKRig") FString SkeletonPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|IKRig") FString RetargetRoot;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|IKRig") int32 BoneCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|IKRig") int32 SolverCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|IKRig") int32 GoalCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|IKRig") int32 ChainCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|IKRig") bool bDirty = false;
};

USTRUCT(BlueprintType)
struct FBridgeIKSolverInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|IKRig") int32 Index = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|IKRig") FString TypePath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|IKRig") FString DisplayName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|IKRig") FString StartBone;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|IKRig") FString EndBone;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|IKRig") TArray<FString> Goals;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|IKRig") TArray<FString> BonesWithSettings;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|IKRig") bool bEnabled = false;
};

USTRUCT(BlueprintType)
struct FBridgeIKGoalInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|IKRig") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|IKRig") FString BoneName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|IKRig") FTransform InitialTransform = FTransform::Identity;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|IKRig") FTransform CurrentTransform = FTransform::Identity;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|IKRig") float PositionAlpha = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|IKRig") float RotationAlpha = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|IKRig") TArray<int32> ConnectedSolverIndices;
};

USTRUCT(BlueprintType)
struct FBridgeIKChainInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|IKRig") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|IKRig") FString StartBone;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|IKRig") FString EndBone;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|IKRig") FString GoalName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|IKRig") int32 BoneCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|IKRig") bool bValid = false;
};

USTRUCT(BlueprintType)
struct FBridgeIKRetargeterInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") FString AssetPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") FString SourceIKRigPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") FString TargetIKRigPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") FString SourcePreviewMeshPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") FString TargetPreviewMeshPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") FString CurrentSourcePose;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") FString CurrentTargetPose;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") FString CurrentProfile;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") int32 OpCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") int32 MappingCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") int32 UnmappedChainCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") int32 SourcePoseCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") int32 TargetPoseCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") bool bDirty = false;
};

USTRUCT(BlueprintType)
struct FBridgeIKRetargetOpInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") int32 Index = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") FString TypePath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") FString DisplayName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") FString ParentOpName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") FString TargetIKRigPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") bool bEnabled = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") bool bHasChainMapping = false;
};

USTRUCT(BlueprintType)
struct FBridgeIKChainMappingInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") FString OpName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") FString TargetChainName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") FString SourceChainName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") bool bMapped = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") bool bSettingsAtDefault = true;
};

USTRUCT(BlueprintType)
struct FBridgeIKRetargetPoseInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") FString Side;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") FVector RootTranslationOffset = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") int32 BoneRotationOffsetCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") bool bCurrent = false;
};

USTRUCT(BlueprintType)
struct FBridgeRetargetBatchResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") TArray<FString> CreatedAssetPaths;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") TArray<FString> FailedSourceAssetPaths;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Retargeter") FString Error;
};

USTRUCT(BlueprintType)
struct FBridgeAnimationBoneMetric
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Quality") FString BoneName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Quality") float MinimumRelativeHeight = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Quality") float MaximumHorizontalSpeed = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Quality") float MaximumAngularDeltaDegrees = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Quality") bool bFootBone = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Quality") bool bFlagged = false;
};

USTRUCT(BlueprintType)
struct FBridgeAnimationQualityReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Quality") bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Quality") FString AnimationPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Quality") float Duration = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Quality") int32 Samples = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Quality") float MaximumRootSpeed = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Quality") float MinimumFootHeight = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Quality") float MaximumFootSlideSpeed = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Quality") float MaximumJointAngularDeltaDegrees = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Quality") TArray<FBridgeAnimationBoneMetric> BoneMetrics;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Quality") TArray<FBridgeRigValidationIssue> Issues;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Rig|Quality") FString Error;
};

/** Complete Control Rig, IK Rig, IK Retargeter authoring and animation-delivery surface. */
UCLASS()
class UNREALBRIDGE_API UUnrealBridgeRigLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig")
	static bool IsRigApiAvailable();

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig")
	static FString GetLastRigError();

	/** Discover ControlRigUnit, RigVMTemplate, IKSolver, and RetargetOp types. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig")
	static TArray<FBridgeRigTypeInfo> ListRigTypes(const FString& Kind, const FString& Query, int32 MaxResults);

	// Control Rig asset and hierarchy --------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|ControlRig")
	static FBridgeRigOperationResult CreateControlRig(const FString& AssetPath, const FString& SourceSkeletalAssetPath, bool bModularRig, bool bImportCurves);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|ControlRig")
	static FBridgeControlRigInfo GetControlRigInfo(const FString& AssetPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|ControlRig")
	static TArray<FBridgeRigElementInfo> ListControlRigElements(const FString& AssetPath, const FString& ElementType);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|ControlRig")
	static bool ImportControlRigHierarchy(const FString& AssetPath, const FString& SourceSkeletalAssetPath, bool bReplaceExisting, bool bImportCurves);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|ControlRig")
	static FString AddControlRigBone(const FString& AssetPath, const FString& Name, const FString& ParentName, const FString& ParentType, const FTransform& Transform, bool bGlobalTransform, bool bImportedBone);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|ControlRig")
	static FString AddControlRigNull(const FString& AssetPath, const FString& Name, const FString& ParentName, const FString& ParentType, const FTransform& Transform, bool bGlobalTransform);

	/** InitialValue uses UE export text, e.g. "True", "1.0", "(X=0,Y=0,Z=0)", or a Transform tuple. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|ControlRig")
	static FString AddControlRigControl(const FString& AssetPath, const FString& Name, const FString& ParentName, const FString& ParentType, const FString& ControlType, const FString& InitialValue, const FTransform& OffsetTransform, const FTransform& ShapeTransform, const FString& ShapeName, const FLinearColor& ShapeColor, bool bAnimatable);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|ControlRig")
	static FString AddControlRigCurve(const FString& AssetPath, const FString& Name, float Value);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|ControlRig")
	static FString AddControlRigConnector(const FString& AssetPath, const FString& Name, const FString& ConnectorType, const FString& Description, bool bOptional, bool bArray);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|ControlRig")
	static bool RemoveControlRigElement(const FString& AssetPath, const FString& Name, const FString& ElementType);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|ControlRig")
	static FString RenameControlRigElement(const FString& AssetPath, const FString& Name, const FString& ElementType, const FString& NewName);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|ControlRig")
	static bool ReparentControlRigElement(const FString& AssetPath, const FString& Name, const FString& ElementType, const FString& ParentName, const FString& ParentType, bool bMaintainGlobalTransform);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|ControlRig")
	static bool SetControlRigElementTransform(const FString& AssetPath, const FString& Name, const FString& ElementType, const FTransform& Transform, bool bGlobalTransform, bool bInitial, bool bAffectChildren);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|ControlRig")
	static bool SetControlRigControlShape(const FString& AssetPath, const FString& ControlName, const FString& ShapeName, const FLinearColor& ShapeColor, bool bVisible);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|ControlRig")
	static bool AddControlRigElementTag(const FString& AssetPath, const FString& Name, const FString& ElementType, const FString& Tag);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|ControlRig")
	static bool RemoveControlRigElementTag(const FString& AssetPath, const FString& Name, const FString& ElementType, const FString& Tag);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|ControlRig")
	static TArray<FBridgeRigPropertyInfo> ListControlRigControlProperties(const FString& AssetPath, const FString& ControlName);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|ControlRig")
	static FBridgeRigPropertyResult GetControlRigControlProperty(const FString& AssetPath, const FString& ControlName, const FString& PropertyPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|ControlRig")
	static bool SetControlRigControlProperty(const FString& AssetPath, const FString& ControlName, const FString& PropertyPath, const FString& Value);

	// RigVM graph ----------------------------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|RigVM")
	static TArray<FBridgeRigVMGraphInfo> ListControlRigGraphs(const FString& AssetPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|RigVM")
	static TArray<FBridgeRigVMNodeInfo> ListControlRigNodes(const FString& AssetPath, const FString& GraphName);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|RigVM")
	static TArray<FBridgeRigVMLinkInfo> ListControlRigLinks(const FString& AssetPath, const FString& GraphName);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|RigVM")
	static FString AddControlRigMemberVariable(const FString& AssetPath, const FString& Name, const FString& CPPType, const FString& DefaultValue, bool bPublic, bool bReadOnly);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|RigVM")
	static bool RemoveControlRigMemberVariable(const FString& AssetPath, const FString& Name);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|RigVM")
	static FString AddControlRigUnitNode(const FString& AssetPath, const FString& GraphName, const FString& UnitStructPath, const FString& MethodName, const FVector2D& Position, const FString& NodeName);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|RigVM")
	static FString AddControlRigTemplateNode(const FString& AssetPath, const FString& GraphName, const FString& Notation, const FVector2D& Position, const FString& NodeName);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|RigVM")
	static FString AddControlRigVariableNode(const FString& AssetPath, const FString& GraphName, const FString& VariableName, const FString& CPPType, const FString& CPPTypeObjectPath, bool bGetter, const FString& DefaultValue, const FVector2D& Position, const FString& NodeName, bool bCreateMemberVariable);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|RigVM")
	static FString AddControlRigCommentNode(const FString& AssetPath, const FString& GraphName, const FString& CommentText, const FVector2D& Position, const FVector2D& Size, const FLinearColor& Color, const FString& NodeName);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|RigVM")
	static FString AddControlRigBranchNode(const FString& AssetPath, const FString& GraphName, const FVector2D& Position, const FString& NodeName);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|RigVM")
	static bool RemoveControlRigNode(const FString& AssetPath, const FString& GraphName, const FString& NodeName);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|RigVM")
	static bool SetControlRigNodePosition(const FString& AssetPath, const FString& GraphName, const FString& NodeName, const FVector2D& Position);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|RigVM")
	static bool SetControlRigPinDefaultValue(const FString& AssetPath, const FString& GraphName, const FString& PinPath, const FString& DefaultValue, bool bResizeArrays);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|RigVM")
	static bool ConnectControlRigPins(const FString& AssetPath, const FString& GraphName, const FString& OutputPinPath, const FString& InputPinPath, bool bCreateCastNode);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|RigVM")
	static bool DisconnectControlRigPins(const FString& AssetPath, const FString& GraphName, const FString& OutputPinPath, const FString& InputPinPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|RigVM")
	static FBridgeRigLayoutResult AutoLayoutControlRigGraph(const FString& AssetPath, const FString& GraphName, float HorizontalSpacing, float VerticalSpacing);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|ControlRig")
	static FBridgeRigValidationReport CompileControlRig(const FString& AssetPath, bool bSave);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|ControlRig")
	static FBridgeRigValidationReport ValidateControlRig(const FString& AssetPath, bool bSave);

	/** Evaluate an event on a transient Control Rig instance without changing the asset. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|ControlRig")
	static FBridgeControlRigEvaluationResult EvaluateControlRig(const FString& AssetPath, const FString& EventName, const TArray<FBridgeRigNamedTransform>& InputControls);

	// IK Rig ---------------------------------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|IKRig")
	static FBridgeRigOperationResult CreateIKRig(const FString& AssetPath, const FString& SkeletalMeshPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|IKRig")
	static FBridgeIKRigInfo GetIKRigInfo(const FString& AssetPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|IKRig")
	static TArray<FBridgeIKSolverInfo> ListIKRigSolvers(const FString& AssetPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|IKRig")
	static TArray<FBridgeIKGoalInfo> ListIKRigGoals(const FString& AssetPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|IKRig")
	static TArray<FBridgeIKChainInfo> ListIKRigRetargetChains(const FString& AssetPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|IKRig")
	static int32 AddIKRigSolver(const FString& AssetPath, const FString& SolverTypePath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|IKRig")
	static bool RemoveIKRigSolver(const FString& AssetPath, int32 SolverIndex);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|IKRig")
	static bool MoveIKRigSolver(const FString& AssetPath, int32 SolverIndex, int32 TargetIndex);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|IKRig")
	static bool SetIKRigSolverEnabled(const FString& AssetPath, int32 SolverIndex, bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|IKRig")
	static bool SetIKRigSolverBones(const FString& AssetPath, int32 SolverIndex, const FString& StartBone, const FString& EndBone);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|IKRig")
	static FString AddIKRigGoal(const FString& AssetPath, const FString& GoalName, const FString& BoneName);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|IKRig")
	static bool RemoveIKRigGoal(const FString& AssetPath, const FString& GoalName);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|IKRig")
	static bool ConnectIKRigGoalToSolver(const FString& AssetPath, const FString& GoalName, int32 SolverIndex);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|IKRig")
	static bool DisconnectIKRigGoalFromSolver(const FString& AssetPath, const FString& GoalName, int32 SolverIndex);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|IKRig")
	static FString AddIKRigRetargetChain(const FString& AssetPath, const FString& ChainName, const FString& StartBone, const FString& EndBone, const FString& GoalName);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|IKRig")
	static bool RemoveIKRigRetargetChain(const FString& AssetPath, const FString& ChainName);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|IKRig")
	static FString RenameIKRigRetargetChain(const FString& AssetPath, const FString& ChainName, const FString& NewName);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|IKRig")
	static bool SetIKRigRetargetRoot(const FString& AssetPath, const FString& RootBoneName);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|IKRig")
	static bool SetIKRigBoneExcluded(const FString& AssetPath, const FString& BoneName, bool bExcluded);

	/** Auto-generate a humanoid retarget definition and/or Full Body IK solver setup. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|IKRig")
	static bool ApplyIKRigAutoSetup(const FString& AssetPath, bool bRetargetDefinition, bool bFullBodyIK);

	/** TargetKind: Solver, Goal, GoalSettings, or BoneSettings. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|IKRig")
	static TArray<FBridgeRigPropertyInfo> ListIKRigProperties(const FString& AssetPath, const FString& TargetKind, int32 SolverIndex, const FString& TargetName);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|IKRig")
	static FBridgeRigPropertyResult GetIKRigProperty(const FString& AssetPath, const FString& TargetKind, int32 SolverIndex, const FString& TargetName, const FString& PropertyPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|IKRig")
	static bool SetIKRigProperty(const FString& AssetPath, const FString& TargetKind, int32 SolverIndex, const FString& TargetName, const FString& PropertyPath, const FString& Value);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|IKRig")
	static FBridgeRigValidationReport ValidateIKRig(const FString& AssetPath, bool bSave);

	// IK Retargeter --------------------------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static FBridgeRigOperationResult CreateIKRetargeter(const FString& AssetPath, const FString& SourceIKRigPath, const FString& TargetIKRigPath, const FString& SourcePreviewMeshPath, const FString& TargetPreviewMeshPath, bool bAddDefaultOps);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static FBridgeIKRetargeterInfo GetIKRetargeterInfo(const FString& AssetPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static bool ConfigureIKRetargeterAssets(const FString& AssetPath, const FString& SourceIKRigPath, const FString& TargetIKRigPath, const FString& SourcePreviewMeshPath, const FString& TargetPreviewMeshPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static TArray<FBridgeIKRetargetOpInfo> ListIKRetargetOps(const FString& AssetPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static int32 AddIKRetargetOp(const FString& AssetPath, const FString& OpTypePath, const FString& OpName);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static bool AddDefaultIKRetargetOps(const FString& AssetPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static bool RemoveIKRetargetOp(const FString& AssetPath, int32 OpIndex);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static bool MoveIKRetargetOp(const FString& AssetPath, int32 OpIndex, int32 TargetIndex);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static bool SetIKRetargetOpEnabled(const FString& AssetPath, int32 OpIndex, bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static bool SetIKRetargetOpParent(const FString& AssetPath, const FString& ChildOpName, const FString& ParentOpName);

	/** MappingType: Exact, Fuzzy, or Clear. Empty OpName applies to all chain-mapping ops. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static bool AutoMapIKRetargetChains(const FString& AssetPath, const FString& MappingType, bool bForceRemap, const FString& OpName);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static bool SetIKRetargetChainMapping(const FString& AssetPath, const FString& TargetChainName, const FString& SourceChainName, const FString& OpName);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static TArray<FBridgeIKChainMappingInfo> ListIKRetargetChainMappings(const FString& AssetPath, const FString& OpName);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static TArray<FBridgeRigPropertyInfo> ListIKRetargetOpProperties(const FString& AssetPath, int32 OpIndex);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static FBridgeRigPropertyResult GetIKRetargetOpProperty(const FString& AssetPath, int32 OpIndex, const FString& PropertyPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static bool SetIKRetargetOpProperty(const FString& AssetPath, int32 OpIndex, const FString& PropertyPath, const FString& Value);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static TArray<FBridgeIKRetargetPoseInfo> ListIKRetargetPoses(const FString& AssetPath, const FString& Side);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static FString CreateIKRetargetPose(const FString& AssetPath, const FString& PoseName, const FString& Side);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static FString DuplicateIKRetargetPose(const FString& AssetPath, const FString& PoseName, const FString& NewName, const FString& Side);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static bool RenameIKRetargetPose(const FString& AssetPath, const FString& PoseName, const FString& NewName, const FString& Side);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static bool RemoveIKRetargetPose(const FString& AssetPath, const FString& PoseName, const FString& Side);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static bool SetCurrentIKRetargetPose(const FString& AssetPath, const FString& PoseName, const FString& Side);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static bool SetIKRetargetPoseBoneRotation(const FString& AssetPath, const FString& Side, const FString& BoneName, const FQuat& RotationOffset);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static bool SetIKRetargetPoseRootOffset(const FString& AssetPath, const FString& Side, const FVector& TranslationOffset);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static bool ResetIKRetargetPose(const FString& AssetPath, const FString& Side, const FString& PoseName, const TArray<FString>& BoneNames);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static bool AutoAlignIKRetargetPose(const FString& AssetPath, const FString& Side, const TArray<FString>& BoneNames, const FString& Method);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static TArray<FString> ListIKRetargetProfiles(const FString& AssetPath);

	/** Snapshot the current op settings and optional source/target poses into a named runtime profile. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static bool SaveCurrentIKRetargetProfile(const FString& AssetPath, const FString& ProfileName, bool bApplySourcePose, bool bApplyTargetPose, bool bForceAllIKOff);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static bool RemoveIKRetargetProfile(const FString& AssetPath, const FString& ProfileName);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static bool SetCurrentIKRetargetProfile(const FString& AssetPath, const FString& ProfileName);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static FBridgeRetargetBatchResult BatchRetargetAnimations(const FString& RetargeterPath, const TArray<FString>& SourceAssetPaths, const FString& SourceMeshPath, const FString& TargetMeshPath, const FString& DestinationFolder, const FString& Search, const FString& Replace, const FString& Prefix, const FString& Suffix, bool bIncludeReferencedAssets, bool bOverwriteExisting, bool bSave);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Retargeter")
	static FBridgeRigValidationReport ValidateIKRetargeter(const FString& AssetPath, bool bInitializeProcessor, bool bSave);

	/** Sample a sequence for root spikes, foot sliding/penetration, and per-joint angular discontinuities. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Rig|Quality")
	static FBridgeAnimationQualityReport AnalyzeAnimationQuality(const FString& AnimationPath, const TArray<FString>& FootBoneNames, int32 NumSamples, float ContactHeightTolerance, float FootSlideSpeedTolerance, float JointAngularDeltaToleranceDegrees, int32 MaxReportedBones);
};
