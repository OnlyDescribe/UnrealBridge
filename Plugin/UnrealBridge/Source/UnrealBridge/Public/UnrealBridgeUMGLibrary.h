#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeUMGLibrary.generated.h"

/** Describes a single widget in a Widget Blueprint hierarchy. */
USTRUCT(BlueprintType)
struct FBridgeWidgetInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString Name;

	/** Widget class, e.g. "CanvasPanel", "TextBlock", "Button" */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString WidgetClass;

	/** Parent widget name (empty for root) */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString ParentName;

	/** Slot class if parented, e.g. "CanvasPanelSlot", "OverlaySlot" */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString SlotType;

	/** Whether this widget is exposed as a variable in the Blueprint */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	bool bIsVariable = false;

	/** Visibility setting, e.g. "Visible", "Collapsed", "Hidden" */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString Visibility;
};

/** Non-default property value on a widget. */
USTRUCT(BlueprintType)
struct FBridgeWidgetPropertyValue
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString Type;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString Value;
};

/** A track within a widget animation. */
USTRUCT(BlueprintType)
struct FBridgeWidgetAnimTrack
{
	GENERATED_BODY()

	/** The widget targeted by this track */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString WidgetName;

	/** Track type, e.g. "Color", "Transform", "Visibility" */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString TrackType;

	/** Display name of the track */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString DisplayName;
};

/** Describes a widget animation. */
USTRUCT(BlueprintType)
struct FBridgeWidgetAnimationInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	float Duration = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	TArray<FBridgeWidgetAnimTrack> Tracks;
};

/** A property binding on a widget. */
USTRUCT(BlueprintType)
struct FBridgeWidgetBindingInfo
{
	GENERATED_BODY()

	/** The widget this binding is on */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString WidgetName;

	/** The property being bound, e.g. "Text", "Visibility", "ColorAndOpacity" */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString PropertyName;

	/** The function providing the value */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString FunctionName;

	/** "Function" or "Property" */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString Kind;
};

/** An event binding on a widget (OnClicked, OnHovered, etc.). */
USTRUCT(BlueprintType)
struct FBridgeWidgetEventInfo
{
	GENERATED_BODY()

	/** The widget this event is on */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString WidgetName;

	/** Event name, e.g. "OnClicked", "OnHovered" */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString EventName;

	/** Bound function or node description */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString HandlerName;
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
 * UMG / Widget Blueprint introspection via UnrealBridge.
 */
UCLASS()
class UNREALBRIDGE_API UUnrealBridgeUMGLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Instantiate a UserWidget Blueprint in the active PIE world's viewport.
	 * This is runtime-only visual validation and does not modify the asset.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static bool AddWidgetBlueprintToPIEViewport(
		const FString& WidgetBlueprintPath, int32 ZOrder);

	/** Remove widgets added by AddWidgetBlueprintToPIEViewport. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static int32 RemovePIEPreviewWidgets();

	/**
	 * Render a Widget Blueprint through Slate into a transparent PNG.
	 *
	 * LogicalWidth/LogicalHeight are the layout space presented to the widget.
	 * Scale controls the Slate render scale and output target size. Use scale
	 * 1.0 for baseline captures of viewport-anchored CanvasPanel layouts.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static FBridgeWidgetRenderResult RenderWidgetBlueprintToPNG(
		const FString& WidgetBlueprintPath, int32 LogicalWidth, int32 LogicalHeight,
		float Scale, const FString& OutputFile);

	/**
	 * Get the widget hierarchy of a Widget Blueprint.
	 * Returns a flat list with parent references to reconstruct the tree.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static TArray<FBridgeWidgetInfo> GetWidgetTree(const FString& WidgetBlueprintPath);

	/**
	 * Get non-default property values for a specific widget.
	 * Only returns properties that differ from the widget class defaults.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static TArray<FBridgeWidgetPropertyValue> GetWidgetProperties(
		const FString& WidgetBlueprintPath, const FString& WidgetName);

	/**
	 * Get all widget animations with their tracks and durations.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static TArray<FBridgeWidgetAnimationInfo> GetWidgetAnimations(const FString& WidgetBlueprintPath);

	/**
	 * Get all property bindings (e.g. Text bound to a function).
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static TArray<FBridgeWidgetBindingInfo> GetWidgetBindings(const FString& WidgetBlueprintPath);

	/**
	 * Get widget event bindings (OnClicked, OnHovered, etc.) from the event graph.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static TArray<FBridgeWidgetEventInfo> GetWidgetEvents(const FString& WidgetBlueprintPath);

	/**
	 * Search widgets by name or class substring.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static TArray<FBridgeWidgetInfo> SearchWidgets(
		const FString& WidgetBlueprintPath, const FString& Query);

	/**
	 * Set a property on a widget by name. Value is parsed as text.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static bool SetWidgetProperty(
		const FString& WidgetBlueprintPath, const FString& WidgetName,
		const FString& PropertyName, const FString& Value);

	/**
	 * Apply a prevalidated WidgetTree edit batch as one undo unit.
	 *
	 * PatchJson shape: {"operations":[...]}. Supported operations:
	 *   create  {name,class,parent?,index?,is_variable?,properties?,slot_properties?}
	 *   set     {name|widget,properties?,slot_properties?}
	 *   move    {name|widget,parent,index?,slot_properties?}
	 *   reorder {name|widget,index}
	 *   delete  {name|widget,confirm_delete:true}
	 *
	 * Property values use UE exported-text syntax. All targets and topology are
	 * validated before mutation. On a mid-batch failure, the completed editor
	 * transaction is immediately undone. Compiling and saving are explicit.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static FBridgeWidgetBatchResult ApplyWidgetTreeBatch(
		const FString& WidgetBlueprintPath, const FString& PatchJson,
		bool bCompileAfter, bool bSaveAfter);
};
