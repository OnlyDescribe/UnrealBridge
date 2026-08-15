#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeUMGLibrary.generated.h"

/** Describes a single widget in a Widget Blueprint hierarchy. */
USTRUCT(BlueprintType)
struct FBridgeWidgetInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString WidgetClass;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString ParentName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString SlotType;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") bool bIsVariable = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString Visibility;
};

/** A reflected property value on a widget or its panel slot. */
USTRUCT(BlueprintType)
struct FBridgeWidgetPropertyValue
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString Type;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString Value;
};

/** A discoverable UWidget class that can be passed to AddWidget. */
USTRUCT(BlueprintType)
struct FBridgeWidgetClassInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString ClassPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString DisplayName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString Category;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") bool bIsPanel = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") bool bCanHaveMultipleChildren = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") bool bIsAbstract = false;
};

/** Common result for Widget Blueprint and widget-tree authoring operations. */
USTRUCT(BlueprintType)
struct FBridgeWidgetOperationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString Path;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString Error;
};

/** One static validation or compiler finding for a Widget Blueprint. */
USTRUCT(BlueprintType)
struct FBridgeWidgetValidationIssue
{
	GENERATED_BODY()

	/** "error", "warning", or "info". */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString Severity;
	/** Stable machine-readable identifier. */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString Code;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString WidgetName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString Message;
};

/** Compile + deliverability validation result for a Widget Blueprint. */
USTRUCT(BlueprintType)
struct FBridgeWidgetValidationReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") bool bFound = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") bool bCompiled = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") bool bCompileSucceeded = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") bool bSaved = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString CompileStatus;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") TArray<FBridgeWidgetValidationIssue> Issues;
};

/** A track within a widget animation. */
USTRUCT(BlueprintType)
struct FBridgeWidgetAnimTrack
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString WidgetName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString TrackType;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString DisplayName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString PropertyName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") int32 KeyCount = 0;
};

/** Describes a widget animation. */
USTRUCT(BlueprintType)
struct FBridgeWidgetAnimationInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") float Duration = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") float DisplayRate = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") TArray<FBridgeWidgetAnimTrack> Tracks;
};

/** A legacy UMG property binding on a widget. */
USTRUCT(BlueprintType)
struct FBridgeWidgetBindingInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString WidgetName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString PropertyName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString FunctionName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString Kind;
};

/** An event binding on a widget (OnClicked, OnHovered, etc.). */
USTRUCT(BlueprintType)
struct FBridgeWidgetEventInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString WidgetName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString EventName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG") FString HandlerName;
};

/** One float key used by AddWidgetAnimationFloatKeys. */
USTRUCT(BlueprintType)
struct FBridgeWidgetFloatKey
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "UnrealBridge|UMG") float Time = 0.f;
	UPROPERTY(BlueprintReadWrite, Category = "UnrealBridge|UMG") float Value = 0.f;
};

/** One color key used by AddWidgetAnimationColorKeys. */
USTRUCT(BlueprintType)
struct FBridgeWidgetColorKey
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "UnrealBridge|UMG") float Time = 0.f;
	UPROPERTY(BlueprintReadWrite, Category = "UnrealBridge|UMG") FLinearColor Value = FLinearColor::White;
};

/** One complete render-transform key used by AddWidgetAnimationTransformKeys. */
USTRUCT(BlueprintType)
struct FBridgeWidgetTransformKey
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "UnrealBridge|UMG") float Time = 0.f;
	UPROPERTY(BlueprintReadWrite, Category = "UnrealBridge|UMG") FVector2D Translation = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadWrite, Category = "UnrealBridge|UMG") FVector2D Scale = FVector2D(1.f, 1.f);
	UPROPERTY(BlueprintReadWrite, Category = "UnrealBridge|UMG") FVector2D Shear = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadWrite, Category = "UnrealBridge|UMG") float Angle = 0.f;
};

/** One MVVM ViewModel source configured on a Widget Blueprint. */
USTRUCT(BlueprintType)
struct FBridgeMVVMViewModelInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|MVVM") FString Id;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|MVVM") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|MVVM") FString ClassPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|MVVM") FString CreationType;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|MVVM") bool bOptional = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|MVVM") bool bCreateGetter = true;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|MVVM") bool bCreateSetter = false;
};

/** One compiled MVVM binding configured on a Widget Blueprint. */
USTRUCT(BlueprintType)
struct FBridgeMVVMBindingInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|MVVM") FString Id;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|MVVM") FString SourcePath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|MVVM") FString DestinationPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|MVVM") FString Mode;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|MVVM") bool bEnabled = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|MVVM") bool bCompile = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|MVVM") TArray<FString> Errors;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|MVVM") TArray<FString> Warnings;
};

/** Live widget state from an instance spawned by SpawnWidgetInstance. */
USTRUCT(BlueprintType)
struct FBridgeLiveWidgetInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") FString WidgetClass;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") FString ParentName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") FString Visibility;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") bool bEnabled = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") bool bHasKeyboardFocus = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") float RenderOpacity = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") FVector2D DesiredSize = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") FVector2D AbsolutePosition = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") FVector2D AbsoluteSize = FVector2D::ZeroVector;
};

/** Result of an atomic, declarative WidgetTree edit batch. */
USTRUCT(BlueprintType)
struct FBridgeWidgetBatchResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	bool bRolledBack = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	int32 AppliedOperations = 0;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString Error;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	TArray<FString> Messages;
};

/** Result of rendering a Widget Blueprint without opening PIE or the UMG designer. */
USTRUCT(BlueprintType)
struct FBridgeWidgetRenderResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString OutputFile;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	int32 Width = 0;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	int32 Height = 0;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString Error;
};

/**
 * Complete Widget Blueprint authoring, MVVM, animation, UI-material assignment,
 * and live validation surface for UnrealBridge.
 */
UCLASS()
class UNREALBRIDGE_API UUnrealBridgeUMGLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** List loaded native/widget classes accepted by AddWidget. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static TArray<FBridgeWidgetClassInfo> ListWidgetClasses(
		const FString& Query, bool bIncludeAbstract, int32 MaxResults);

	/** Create a Widget Blueprint. ParentClassPath defaults to UserWidget. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static FBridgeWidgetOperationResult CreateWidgetBlueprint(
		const FString& AssetPath, const FString& ParentClassPath);

	/** Add a widget to a tree. Empty ParentName creates the root and is only valid when no root exists. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static FBridgeWidgetOperationResult AddWidget(
		const FString& WidgetBlueprintPath, const FString& WidgetClassPath,
		const FString& WidgetName, const FString& ParentName, int32 InsertIndex);

	/** Remove a widget and its descendants from a Widget Blueprint. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static bool RemoveWidget(const FString& WidgetBlueprintPath, const FString& WidgetName);

	/** Rename a widget and update Blueprint variable, animation, legacy binding, and MVVM references. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static bool RenameWidget(
		const FString& WidgetBlueprintPath, const FString& WidgetName, const FString& NewName);

	/** Move a non-root widget under another panel, optionally at a specific child index. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static bool ReparentWidget(
		const FString& WidgetBlueprintPath, const FString& WidgetName,
		const FString& NewParentName, int32 InsertIndex);

	/** Control whether the compiled UserWidget exposes this widget as a member variable. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static bool SetWidgetIsVariable(
		const FString& WidgetBlueprintPath, const FString& WidgetName, bool bIsVariable);

	/** Render a compiled Widget Blueprint through Slate into a transparent PNG. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static FBridgeWidgetRenderResult RenderWidgetBlueprintToPNG(
		const FString& WidgetBlueprintPath, int32 LogicalWidth, int32 LogicalHeight,
		float Scale, const FString& OutputFile);

	/**
	 * Validate and apply create/set/move/reorder/delete operations as one
	 * editor transaction. Delete operations require confirm_delete=true.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static FBridgeWidgetBatchResult ApplyWidgetTreeBatch(
		const FString& WidgetBlueprintPath, const FString& PatchJson,
		bool bCompileAfter, bool bSaveAfter);

	/** Get the widget hierarchy as a flat parent-linked list. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static TArray<FBridgeWidgetInfo> GetWidgetTree(const FString& WidgetBlueprintPath);

	/** Get non-default properties for a widget template. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static TArray<FBridgeWidgetPropertyValue> GetWidgetProperties(
		const FString& WidgetBlueprintPath, const FString& WidgetName);

	/** Get non-default properties for a widget's panel slot. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static TArray<FBridgeWidgetPropertyValue> GetWidgetSlotProperties(
		const FString& WidgetBlueprintPath, const FString& WidgetName);

	/** Search widgets by name or class substring. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static TArray<FBridgeWidgetInfo> SearchWidgets(
		const FString& WidgetBlueprintPath, const FString& Query);

	/** Set a widget-template UPROPERTY from UE ImportText syntax. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static bool SetWidgetProperty(
		const FString& WidgetBlueprintPath, const FString& WidgetName,
		const FString& PropertyName, const FString& Value);

	/** Set a panel-slot UPROPERTY from UE ImportText syntax. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static bool SetWidgetSlotProperty(
		const FString& WidgetBlueprintPath, const FString& WidgetName,
		const FString& PropertyName, const FString& Value);

	/** Configure a CanvasPanelSlot without hand-authoring ImportText strings. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static bool SetCanvasSlotLayout(
		const FString& WidgetBlueprintPath, const FString& WidgetName,
		FVector2D Position, FVector2D Size, FVector2D AnchorMinimum,
		FVector2D AnchorMaximum, FVector2D Alignment, bool bAutoSize, int32 ZOrder);

	/**
	 * Assign an asset to a FSlateBrush property. BrushPropertyPath supports dotted
	 * style members such as "WidgetStyle.Normal". ResourcePath may be a texture,
	 * material, or material instance; empty clears the resource.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static bool SetWidgetBrush(
		const FString& WidgetBlueprintPath, const FString& WidgetName,
		const FString& BrushPropertyPath, const FString& ResourcePath,
		FLinearColor Tint, const FString& DrawAs, FVector2D ImageSize, FMargin Margin);

	/** Compile, validate, and optionally save a deliverable Widget Blueprint. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static FBridgeWidgetValidationReport CompileAndValidateWidgetBlueprint(
		const FString& WidgetBlueprintPath, bool bSave, bool bCheckAccessibility);

	// Animation authoring -----------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|Animation")
	static TArray<FBridgeWidgetAnimationInfo> GetWidgetAnimations(const FString& WidgetBlueprintPath);

	/** Create an animation with a finite playback range. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|Animation")
	static bool CreateWidgetAnimation(
		const FString& WidgetBlueprintPath, const FString& AnimationName,
		float DurationSeconds, int32 DisplayRate);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|Animation")
	static bool RemoveWidgetAnimation(
		const FString& WidgetBlueprintPath, const FString& AnimationName);

	/** Batch-add keys to a float property track, e.g. RenderOpacity. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|Animation")
	static bool AddWidgetAnimationFloatKeys(
		const FString& WidgetBlueprintPath, const FString& AnimationName,
		const FString& WidgetName, const FString& PropertyName,
		const TArray<FBridgeWidgetFloatKey>& Keys, const FString& Interpolation);

	/** Batch-add keys to a color property track, e.g. ColorAndOpacity or BrushColor. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|Animation")
	static bool AddWidgetAnimationColorKeys(
		const FString& WidgetBlueprintPath, const FString& AnimationName,
		const FString& WidgetName, const FString& PropertyName,
		const TArray<FBridgeWidgetColorKey>& Keys, const FString& Interpolation);

	/** Batch-add complete RenderTransform keys. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|Animation")
	static bool AddWidgetAnimationTransformKeys(
		const FString& WidgetBlueprintPath, const FString& AnimationName,
		const FString& WidgetName, const TArray<FBridgeWidgetTransformKey>& Keys,
		const FString& Interpolation);

	/** Remove all animation tracks for PropertyName on WidgetName. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|Animation")
	static int32 RemoveWidgetAnimationTrack(
		const FString& WidgetBlueprintPath, const FString& AnimationName,
		const FString& WidgetName, const FString& PropertyName);

	// Blueprint events and legacy bindings -----------------------------------

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static TArray<FBridgeWidgetBindingInfo> GetWidgetBindings(const FString& WidgetBlueprintPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static TArray<FBridgeWidgetEventInfo> GetWidgetEvents(const FString& WidgetBlueprintPath);

	// MVVM authoring (functional on UE 5.7+; older engines log safe no-ops) ----

	/** Create a Blueprint derived from MVVMViewModelBase (UE 5.7+). */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|MVVM")
	static FBridgeWidgetOperationResult CreateMVVMViewModelBlueprint(
		const FString& AssetPath, const FString& ParentClassPath);

	/** Enable/disable FieldNotify metadata on a ViewModel Blueprint variable. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|MVVM")
	static bool SetViewModelFieldNotify(
		const FString& ViewModelBlueprintPath, const FString& VariableName, bool bEnabled);

	/**
	 * Add a ViewModel source. CreationType: CreateInstance, Manual,
	 * GlobalViewModelCollection, PropertyPath, or Resolver.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|MVVM")
	static FString AddMVVMViewModel(
		const FString& WidgetBlueprintPath, const FString& ViewModelName,
		const FString& ViewModelClassPath, const FString& CreationType,
		const FString& CreationData,
		bool bOptional, bool bCreateGetter, bool bCreateSetter);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|MVVM")
	static bool RemoveMVVMViewModel(
		const FString& WidgetBlueprintPath, const FString& ViewModelName);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|MVVM")
	static TArray<FBridgeMVVMViewModelInfo> GetMVVMViewModels(const FString& WidgetBlueprintPath);

	/**
	 * Create an MVVM property binding. SourceFieldPath and DestinationFieldPath
	 * are dot-separated reflected property paths. DestinationWidgetName may be
	 * "self" for the UserWidget itself. Mode: OneTimeToDestination,
	 * OneWayToDestination, TwoWay, or OneWayToSource.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|MVVM")
	static FString AddMVVMBinding(
		const FString& WidgetBlueprintPath, const FString& ViewModelName,
		const FString& SourceFieldPath, const FString& DestinationWidgetName,
		const FString& DestinationFieldPath, const FString& Mode);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|MVVM")
	static bool RemoveMVVMBinding(
		const FString& WidgetBlueprintPath, const FString& BindingId);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|MVVM")
	static TArray<FBridgeMVVMBindingInfo> GetMVVMBindings(const FString& WidgetBlueprintPath);

	/** Configure automatic source/binding/event initialization for the generated view. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|MVVM")
	static bool SetMVVMViewSettings(
		const FString& WidgetBlueprintPath, bool bInitializeSourcesOnConstruct,
		bool bInitializeBindingsOnConstruct, bool bInitializeEventsOnConstruct,
		bool bCreateViewWithoutBindings);

	// Live validation ---------------------------------------------------------

	/** Spawn a compiled Widget Blueprint in PIE and add it to the viewport. Returns a session-local handle. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|Runtime")
	static FString SpawnWidgetInstance(const FString& WidgetBlueprintPath, int32 ZOrder);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|Runtime")
	static bool RemoveWidgetInstance(const FString& InstanceHandle);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|Runtime")
	static int32 RemoveAllWidgetInstances();

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|Runtime")
	static TArray<FBridgeLiveWidgetInfo> GetLiveWidgetTree(const FString& InstanceHandle);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|Runtime")
	static FString GetLiveWidgetProperty(
		const FString& InstanceHandle, const FString& WidgetName, const FString& PropertyPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|Runtime")
	static bool SetLiveWidgetProperty(
		const FString& InstanceHandle, const FString& WidgetName,
		const FString& PropertyPath, const FString& Value);

	/** Invoke a UButton's semantic OnClicked delegate (useful for deterministic functional validation). */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|Runtime")
	static bool ClickLiveButton(const FString& InstanceHandle, const FString& WidgetName);

	/** Set TextBlock, EditableText, or EditableTextBox text using the widget's public setter. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|Runtime")
	static bool SetLiveWidgetText(
		const FString& InstanceHandle, const FString& WidgetName, const FString& Text);

	/** Set a Slider or ProgressBar value using its public setter. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|Runtime")
	static bool SetLiveWidgetValue(
		const FString& InstanceHandle, const FString& WidgetName, float Value);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|Runtime")
	static bool SetLiveWidgetChecked(
		const FString& InstanceHandle, const FString& WidgetName, bool bChecked);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|Runtime")
	static bool FocusLiveWidget(const FString& InstanceHandle, const FString& WidgetName);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|Runtime")
	static bool PlayLiveWidgetAnimation(
		const FString& InstanceHandle, const FString& AnimationName,
		float StartTime, int32 NumLoops, const FString& PlayMode, float PlaybackSpeed);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|Runtime")
	static bool StopLiveWidgetAnimation(
		const FString& InstanceHandle, const FString& AnimationName);

	/** Set a scalar parameter on the dynamic UI material used by an Image or Border. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|Runtime")
	static bool SetLiveWidgetMaterialScalar(
		const FString& InstanceHandle, const FString& WidgetName,
		const FString& ParameterName, float Value);

	/** Set a vector parameter on the dynamic UI material used by an Image or Border. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|Runtime")
	static bool SetLiveWidgetMaterialVector(
		const FString& InstanceHandle, const FString& WidgetName,
		const FString& ParameterName, FLinearColor Value);

	/** Read a reflected property from a live CreateInstance/Manual MVVM source (UE 5.7+). */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|Runtime|MVVM")
	static FString GetLiveViewModelProperty(
		const FString& InstanceHandle, const FString& ViewModelName, const FString& PropertyPath);

	/** Write a live ViewModel property, broadcast FieldNotify, and execute its bindings (UE 5.7+). */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|Runtime|MVVM")
	static bool SetLiveViewModelProperty(
		const FString& InstanceHandle, const FString& ViewModelName,
		const FString& PropertyPath, const FString& Value);
};
