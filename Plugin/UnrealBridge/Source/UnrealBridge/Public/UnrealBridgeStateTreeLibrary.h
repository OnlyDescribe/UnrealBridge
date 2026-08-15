#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeStateTreeLibrary.generated.h"

USTRUCT(BlueprintType)
struct FBridgeStateTreeCreateResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString AssetPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Error;
};

USTRUCT(BlueprintType)
struct FBridgeStateTreeCompileMessage
{
	GENERATED_BODY()

	/** "Info", "Warning", "Error", or "CriticalError". */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Severity;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Message;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString StateId;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString StatePath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString ItemId;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString ItemName;
};

USTRUCT(BlueprintType)
struct FBridgeStateTreeCompileResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	bool bReadyToRun = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	TArray<FBridgeStateTreeCompileMessage> Messages;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Error;
};

USTRUCT(BlueprintType)
struct FBridgeStateTreeAssetInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString AssetPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString SchemaClassPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString EditorSchemaClassPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString RootParametersId;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	bool bReadyToRun = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	bool bDirty = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	int32 RootStateCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	int32 StateCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	int32 NodeCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	int32 TransitionCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	int32 BindingCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	int32 ParameterCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	int32 BreakpointCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	int64 EditorDataHash = 0;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	int64 LastCompiledEditorDataHash = 0;
};

USTRUCT(BlueprintType)
struct FBridgeStateTreeStateInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString ParentId;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Path;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Description;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Type;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString SelectionBehavior;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString TasksCompletion;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Tag;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString LinkedStateId;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString LinkedAssetPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	bool bEnabled = true;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	bool bExpanded = true;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	int32 Depth = 0;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	int32 Index = 0;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	int32 ChildCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	int32 TaskCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	int32 EnterConditionCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	int32 ConsiderationCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	int32 TransitionCount = 0;
};

USTRUCT(BlueprintType)
struct FBridgeStateTreeNodeInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Id;

	/** Empty for evaluators/global tasks; transition GUID for transition conditions; state GUID otherwise. */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString OwnerId;

	/** Evaluator, GlobalTask, EnterCondition, Task, SingleTask, Consideration, or TransitionCondition. */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Scope;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString NodeTypePath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString InstanceTypePath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString ExecutionRuntimeDataTypePath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString ExpressionOperand;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	int32 ExpressionIndent = 0;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	int32 Index = 0;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	bool bEnabled = true;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	bool bBlueprintNode = false;
};

USTRUCT(BlueprintType)
struct FBridgeStateTreeNodeTypeInfo
{
	GENERATED_BODY()

	/** Evaluator, Task, Condition, or Consideration. */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Kind;

	/** Pass this value to AddStateTreeNode. */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString TypePath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString InstanceTypePath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	bool bBlueprintClass = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	bool bAllowedBySchema = false;
};

USTRUCT(BlueprintType)
struct FBridgeStateTreePropertyInfo
{
	GENERATED_BODY()

	/** Node, Instance, or ExecutionRuntimeData. */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString DataSource;

	/** Property path accepted by the node property and binding APIs. */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Path;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Type;

	/** Current value in Unreal export-text syntax. */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Value;

	/** Invalid, Context, Input, Parameter, or Output. */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Usage;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Category;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	bool bEditable = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	bool bBindable = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	bool bInherited = false;
};

USTRUCT(BlueprintType)
struct FBridgeStateTreeTransitionInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString StateId;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	int32 Index = 0;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Trigger;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString RequiredEventTag;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString TargetType;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString TargetStateId;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString TargetStateName;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Priority;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	bool bEnabled = true;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	bool bHasDelay = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	float DelayDuration = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	float DelayRandomVariance = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	int32 ConditionCount = 0;
};

USTRUCT(BlueprintType)
struct FBridgeStateTreeBindingInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString SourceId;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString SourcePath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString TargetId;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString TargetPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	bool bOutputBinding = false;
};

USTRUCT(BlueprintType)
struct FBridgeStateTreeBindableStructInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString TypePath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Category;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Section;
};

USTRUCT(BlueprintType)
struct FBridgeStateTreeParameterInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Type;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Value;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	bool bOverridden = false;
};

USTRUCT(BlueprintType)
struct FBridgeStateTreePropertyResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Value;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString TypePath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Error;
};

USTRUCT(BlueprintType)
struct FBridgeStateTreeBreakpointInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString ItemId;

	/** OnEnter, OnExit, or OnTransition. */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Type;
};

USTRUCT(BlueprintType)
struct FBridgeStateTreeComponentInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString ComponentPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString ComponentName;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString OwnerPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString OwnerLabel;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString WorldType;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString StateTreeAssetPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString RunStatus;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	bool bRunning = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	bool bPaused = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	TArray<FString> ActiveStateNames;
};

/**
 * StateTree authoring, inspection, binding, compilation, debugger, and runtime control.
 *
 * The implementation uses StateTree's editor data model and compiler directly. The
 * full API is available on UE 5.7+; older supported engines return safe stubs and a
 * descriptive error from GetLastStateTreeError().
 */
UCLASS()
class UNREALBRIDGE_API UUnrealBridgeStateTreeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Capability and asset lifecycle

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static bool IsStateTreeApiAvailable();

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static FString GetLastStateTreeError();

	/** Create a StateTree with the requested UStateTreeSchema subclass. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static FBridgeStateTreeCreateResult CreateStateTree(const FString& AssetPath, const FString& SchemaClassPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static FBridgeStateTreeAssetInfo GetStateTreeInfo(const FString& AssetPath);

	/** Run StateTree's safety validation/fixup pass without compiling. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static bool ValidateStateTree(const FString& AssetPath);

	/** Compile editor data into runnable data and return the complete compiler log. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static FBridgeStateTreeCompileResult CompileStateTree(const FString& AssetPath, bool bRunValidation = true);

	// State hierarchy

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static TArray<FBridgeStateTreeStateInfo> ListStateTreeStates(const FString& AssetPath);

	/** ParentStateId empty creates a root/subtree state. InsertIndex -1 appends. Returns the new GUID. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static FString AddStateTreeState(const FString& AssetPath, const FString& ParentStateId, const FString& Name,
		const FString& StateType = TEXT("State"), int32 InsertIndex = -1);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static bool RemoveStateTreeState(const FString& AssetPath, const FString& StateId);

	/** Move a state to a new parent (empty = root) and insertion index (-1 = append). */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static bool MoveStateTreeState(const FString& AssetPath, const FString& StateId,
		const FString& NewParentStateId, int32 InsertIndex = -1);

	/** Set State, Group, or Subtree. Use the dedicated link APIs for linked types. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static bool SetStateTreeStateType(const FString& AssetPath, const FString& StateId,
		const FString& StateType);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static FBridgeStateTreePropertyResult GetStateTreeStateProperty(const FString& AssetPath,
		const FString& StateId, const FString& PropertyPath);

	/** Value uses Unreal export-text syntax. Nested paths and array indices are supported. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static bool SetStateTreeStateProperty(const FString& AssetPath, const FString& StateId,
		const FString& PropertyPath, const FString& Value);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static bool SetStateTreeLinkedState(const FString& AssetPath, const FString& StateId,
		const FString& LinkedStateId);

	/** Empty LinkedAssetPath clears the link and restores a regular State. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static bool SetStateTreeLinkedAsset(const FString& AssetPath, const FString& StateId,
		const FString& LinkedAssetPath);

	// Nodes

	/** List native node structs and Blueprint node classes known to StateTree's node cache. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static TArray<FBridgeStateTreeNodeTypeInfo> ListStateTreeNodeTypes(const FString& AssetPath,
		const FString& Kind = TEXT(""), bool bIncludeDisallowed = false);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static TArray<FBridgeStateTreeNodeInfo> ListStateTreeNodes(const FString& AssetPath,
		const FString& Scope = TEXT(""));

	/** Discover top-level properties and current values for one node data source. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static TArray<FBridgeStateTreePropertyInfo> ListStateTreeNodeProperties(const FString& AssetPath,
		const FString& NodeId, const FString& DataSource = TEXT("Instance"), bool bIncludeInherited = true);

	/**
	 * Add a native node struct or Blueprint node class. Scope is Evaluator, GlobalTask,
	 * EnterCondition, Task, SingleTask, Consideration, or TransitionCondition. OwnerId
	 * is empty for global scopes, a state GUID for state scopes, and a transition GUID
	 * for TransitionCondition. InsertIndex -1 appends. Returns the new node GUID.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static FString AddStateTreeNode(const FString& AssetPath, const FString& OwnerId,
		const FString& Scope, const FString& TypePath, int32 InsertIndex = -1);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static bool RemoveStateTreeNode(const FString& AssetPath, const FString& NodeId);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static bool MoveStateTreeNode(const FString& AssetPath, const FString& NodeId, int32 NewIndex);

	/** DataSource is Node, Instance, or ExecutionRuntimeData. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static FBridgeStateTreePropertyResult GetStateTreeNodeProperty(const FString& AssetPath,
		const FString& NodeId, const FString& DataSource, const FString& PropertyPath);

	/** DataSource is Node, Instance, or ExecutionRuntimeData; value uses export-text syntax. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static bool SetStateTreeNodeProperty(const FString& AssetPath, const FString& NodeId,
		const FString& DataSource, const FString& PropertyPath, const FString& Value);

	// Transitions

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static TArray<FBridgeStateTreeTransitionInfo> ListStateTreeTransitions(const FString& AssetPath,
		const FString& StateId = "");

	/** Trigger accepts flag names joined by '|'. TargetType is GotoState/NextState/etc. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static FString AddStateTreeTransition(const FString& AssetPath, const FString& StateId,
		const FString& Trigger, const FString& TargetType, const FString& TargetStateId = TEXT(""),
		const FString& RequiredEventTag = TEXT(""), int32 InsertIndex = -1);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static bool RemoveStateTreeTransition(const FString& AssetPath, const FString& TransitionId);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static bool MoveStateTreeTransition(const FString& AssetPath, const FString& TransitionId,
		int32 NewIndex);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static FBridgeStateTreePropertyResult GetStateTreeTransitionProperty(const FString& AssetPath,
		const FString& TransitionId, const FString& PropertyPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static bool SetStateTreeTransitionProperty(const FString& AssetPath, const FString& TransitionId,
		const FString& PropertyPath, const FString& Value);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static bool SetStateTreeTransitionTarget(const FString& AssetPath, const FString& TransitionId,
		const FString& TargetType, const FString& TargetStateId = TEXT(""));

	// Property bindings

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static TArray<FBridgeStateTreeBindingInfo> ListStateTreeBindings(const FString& AssetPath);

	/** TargetId scopes which sources are accessible; empty returns the generally bindable set. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static TArray<FBridgeStateTreeBindableStructInfo> ListStateTreeBindableStructs(const FString& AssetPath,
		const FString& TargetId = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static bool AddStateTreeBinding(const FString& AssetPath, const FString& SourceId,
		const FString& SourcePath, const FString& TargetId, const FString& TargetPath,
		bool bOutputBinding = false);

	/** Remove the exact binding identified by target GUID and target property path. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static bool RemoveStateTreeBinding(const FString& AssetPath, const FString& TargetId,
		const FString& TargetPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static int32 ClearStateTreeBindingsForItem(const FString& AssetPath, const FString& ItemId);

	// Parameters

	/** ScopeId empty lists root parameters; a state GUID lists that state's parameter bag. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static TArray<FBridgeStateTreeParameterInfo> ListStateTreeParameters(const FString& AssetPath,
		const FString& ScopeId = TEXT(""));

	/** Type examples: Bool, Float, Struct:/Script/CoreUObject.Vector, Array<Object:/Script/Engine.Actor>. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static FString AddStateTreeRootParameter(const FString& AssetPath, const FString& Name,
		const FString& Type, const FString& DefaultValue = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static bool RemoveStateTreeRootParameter(const FString& AssetPath, const FString& Name);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static bool RenameStateTreeRootParameter(const FString& AssetPath, const FString& OldName,
		const FString& NewName);

	/** ScopeId empty targets root parameters; a state GUID targets its parameter bag. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static bool SetStateTreeParameterValue(const FString& AssetPath, const FString& ScopeId,
		const FString& Name, const FString& Value, bool bMarkOverridden = true);

	// Debugger breakpoints

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static TArray<FBridgeStateTreeBreakpointInfo> ListStateTreeBreakpoints(const FString& AssetPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static bool SetStateTreeBreakpoint(const FString& AssetPath, const FString& ItemId,
		const FString& BreakpointType, bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static bool ClearStateTreeBreakpoints(const FString& AssetPath);

	// Runtime StateTreeComponent control

	/** Enumerate live editor and PIE StateTreeComponents. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static TArray<FBridgeStateTreeComponentInfo> ListStateTreeComponents(bool bPIEOnly = false);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static FBridgeStateTreeComponentInfo GetStateTreeComponentInfo(const FString& ComponentPath);

	/** Set the component asset while its logic is stopped. Empty AssetPath clears it. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static bool SetStateTreeComponentAsset(const FString& ComponentPath, const FString& AssetPath);

	/** Action is Start, Restart, Stop, Pause, or Resume. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static bool ControlStateTreeComponent(const FString& ComponentPath, const FString& Action,
		const FString& Reason = TEXT("UnrealBridge"));

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static bool SendStateTreeComponentEvent(const FString& ComponentPath, const FString& EventTag,
		const FString& Origin = TEXT("UnrealBridge"));
};
