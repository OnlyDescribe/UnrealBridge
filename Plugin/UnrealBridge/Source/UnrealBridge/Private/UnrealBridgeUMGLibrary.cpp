#include "UnrealBridgeUMGLibrary.h"

#include "Animation/MovieScene2DTransformSection.h"
#include "Animation/MovieScene2DTransformTrack.h"
#include "Animation/WidgetAnimation.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/EditableText.h"
#include "Components/EditableTextBox.h"
#include "Components/Image.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "RenderDeferredCleanup.h"
#include "RenderingThread.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "ShaderCompiler.h"
#include "Slate/WidgetRenderer.h"
#include "WidgetBlueprintEditorUtils.h"
#include "IAssetTools.h"
#include "K2Node_ComponentBoundEvent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/PackageName.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSection.h"
#include "MovieSceneTrack.h"
#include "Runtime/Launch/Resources/Version.h"
#include "ScopedTransaction.h"
#include "Sections/MovieSceneColorSection.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Styling/SlateBrush.h"
#include "Tracks/MovieSceneColorTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "UObject/UObjectIterator.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"

#if !UE_VERSION_OLDER_THAN(5, 7, 0)
#include "INotifyFieldValueChanged.h"
#include "MVVMBlueprintView.h"
#include "MVVMBlueprintViewBinding.h"
#include "MVVMBlueprintViewModelContext.h"
#include "MVVMPropertyPath.h"
#include "MVVMSubsystem.h"
#include "MVVMWidgetBlueprintExtension_View.h"
#include "Types/MVVMBindingMode.h"
#include "Types/MVVMFieldVariant.h"
#include "View/MVVMView.h"
#include "WidgetBlueprintExtension.h"
#endif

namespace BridgeUMGImpl
{
	static TMap<FString, TWeakObjectPtr<UUserWidget>> LiveInstances;

	static void LogFailure(const TCHAR* FunctionName, const FString& Message)
	{
		UE_LOG(LogTemp, Warning, TEXT("UnrealBridge UMG.%s: %s"), FunctionName, *Message);
	}

	static void LogMVVMUnavailable(const TCHAR* FunctionName)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("UnrealBridge UMG.%s: MVVM authoring/runtime support requires UE 5.7+; this call is a safe no-op on the current engine"),
			FunctionName);
	}

	static bool SplitAssetPath(const FString& InPath, FString& OutPackagePath, FString& OutAssetName)
	{
		FString Path = InPath;
		Path.TrimStartAndEndInline();
		if (Path.EndsWith(TEXT("_C")))
		{
			Path.LeftChopInline(2);
		}
		int32 DotIndex = INDEX_NONE;
		if (Path.FindLastChar(TEXT('.'), DotIndex))
		{
			Path.LeftInline(DotIndex);
		}
		int32 SlashIndex = INDEX_NONE;
		if (!Path.StartsWith(TEXT("/Game/")) || !Path.FindLastChar(TEXT('/'), SlashIndex) || SlashIndex <= 5 || SlashIndex >= Path.Len() - 1)
		{
			return false;
		}
		OutPackagePath = Path.Left(SlashIndex);
		OutAssetName = Path.Mid(SlashIndex + 1);
		return FPackageName::IsValidLongPackageName(Path);
	}

	static UWidgetBlueprint* LoadWBP(const FString& Path, const TCHAR* FunctionName = TEXT("Load"))
	{
		UWidgetBlueprint* WBP = LoadObject<UWidgetBlueprint>(nullptr, *Path);
		if (!WBP)
		{
			FString AssetPath = Path;
			if (AssetPath.EndsWith(TEXT("_C")))
			{
				AssetPath.LeftChopInline(2);
			}
			WBP = LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
		}
		if (!WBP)
		{
			LogFailure(FunctionName, FString::Printf(TEXT("could not load Widget Blueprint '%s'"), *Path));
		}
		return WBP;
	}

	static UClass* ResolveClass(const FString& ClassPath, UClass* RequiredBase, const FString& DefaultPath = FString())
	{
		FString Candidate = ClassPath.IsEmpty() ? DefaultPath : ClassPath;
		Candidate.TrimStartAndEndInline();
		if (Candidate.IsEmpty())
		{
			return nullptr;
		}

		UClass* Result = LoadObject<UClass>(nullptr, *Candidate);
		if (!Result && !Candidate.EndsWith(TEXT("_C")))
		{
			Result = LoadObject<UClass>(nullptr, *(Candidate + TEXT("_C")));
		}
		if (!Result)
		{
			if (UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *Candidate))
			{
				Result = BP->GeneratedClass;
			}
		}
		if (!Result && !Candidate.Contains(TEXT("/")))
		{
			const FString Clean = Candidate.StartsWith(TEXT("U")) ? Candidate.Mid(1) : Candidate;
			for (TObjectIterator<UClass> It; It; ++It)
			{
				if (It->GetName().Equals(Candidate, ESearchCase::IgnoreCase)
					|| It->GetName().Equals(Clean, ESearchCase::IgnoreCase))
				{
					Result = *It;
					break;
				}
			}
		}
		return Result && (!RequiredBase || Result->IsChildOf(RequiredBase)) ? Result : nullptr;
	}

	static FString VisibilityToString(ESlateVisibility Visibility)
	{
		switch (Visibility)
		{
		case ESlateVisibility::Visible: return TEXT("Visible");
		case ESlateVisibility::Collapsed: return TEXT("Collapsed");
		case ESlateVisibility::Hidden: return TEXT("Hidden");
		case ESlateVisibility::HitTestInvisible: return TEXT("HitTestInvisible");
		case ESlateVisibility::SelfHitTestInvisible: return TEXT("SelfHitTestInvisible");
		default: return TEXT("Unknown");
		}
	}

	static UWidget* FindWidgetByName(UWidgetBlueprint* WBP, const FString& WidgetName)
	{
		if (!WBP || !WBP->WidgetTree)
		{
			return nullptr;
		}
		return WBP->WidgetTree->FindWidget(FName(*WidgetName));
	}

	static void GatherWidgets(UWidget* Widget, const FString& ParentName, TArray<FBridgeWidgetInfo>& Out)
	{
		if (!Widget)
		{
			return;
		}
		FBridgeWidgetInfo Info;
		Info.Name = Widget->GetName();
		Info.WidgetClass = Widget->GetClass()->GetName();
		Info.ParentName = ParentName;
		Info.bIsVariable = Widget->bIsVariable;
		Info.Visibility = VisibilityToString(Widget->GetVisibility());
		if (UPanelSlot* Slot = Widget->Slot)
		{
			Info.SlotType = Slot->GetClass()->GetName();
		}
		Out.Add(Info);
		if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
			{
				GatherWidgets(Panel->GetChildAt(Index), Info.Name, Out);
			}
		}
	}

	static void GatherLiveWidgets(UWidget* Widget, const FString& ParentName, TArray<FBridgeLiveWidgetInfo>& Out)
	{
		if (!Widget)
		{
			return;
		}
		const FGeometry Geometry = Widget->GetCachedGeometry();
		FBridgeLiveWidgetInfo Info;
		Info.Name = Widget->GetName();
		Info.WidgetClass = Widget->GetClass()->GetName();
		Info.ParentName = ParentName;
		Info.Visibility = VisibilityToString(Widget->GetVisibility());
		Info.bEnabled = Widget->GetIsEnabled();
		Info.bHasKeyboardFocus = Widget->HasKeyboardFocus();
		Info.RenderOpacity = Widget->GetRenderOpacity();
		Info.DesiredSize = Widget->GetDesiredSize();
		Info.AbsolutePosition = Geometry.GetAbsolutePosition();
		Info.AbsoluteSize = Geometry.GetAbsoluteSize();
		Out.Add(Info);
		if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
			{
				GatherLiveWidgets(Panel->GetChildAt(Index), Info.Name, Out);
			}
		}
	}

	static TArray<FBridgeWidgetPropertyValue> GatherDifferentProperties(UObject* Object)
	{
		TArray<FBridgeWidgetPropertyValue> Result;
		if (!Object)
		{
			return Result;
		}
		UObject* CDO = Object->GetClass()->GetDefaultObject();
		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			FProperty* Property = *It;
			if (!Property || Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_Deprecated))
			{
				continue;
			}
			void* Value = Property->ContainerPtrToValuePtr<void>(Object);
			void* DefaultValue = CDO ? Property->ContainerPtrToValuePtr<void>(CDO) : nullptr;
			if (DefaultValue && Property->Identical(Value, DefaultValue))
			{
				continue;
			}
			FBridgeWidgetPropertyValue Info;
			Info.Name = Property->GetName();
			Info.Type = Property->GetCPPType();
			Property->ExportTextItem_Direct(Info.Value, Value, DefaultValue, Object, PPF_None);
			Result.Add(MoveTemp(Info));
		}
		Result.Sort([](const FBridgeWidgetPropertyValue& A, const FBridgeWidgetPropertyValue& B)
		{
			return A.Name < B.Name;
		});
		return Result;
	}

	static bool ResolvePropertyPath(UObject* Object, const FString& PropertyPath,
		FProperty*& OutProperty, void*& OutValue, FProperty*& OutRootProperty, FString& OutError)
	{
		OutProperty = nullptr;
		OutValue = nullptr;
		OutRootProperty = nullptr;
		if (!Object)
		{
			OutError = TEXT("object is null");
			return false;
		}
		TArray<FString> Segments;
		PropertyPath.ParseIntoArray(Segments, TEXT("."), true);
		if (Segments.IsEmpty())
		{
			OutError = TEXT("property path is empty");
			return false;
		}

		UStruct* CurrentStruct = Object->GetClass();
		void* CurrentContainer = Object;
		for (int32 Index = 0; Index < Segments.Num(); ++Index)
		{
			FProperty* Property = FindFProperty<FProperty>(CurrentStruct, FName(*Segments[Index]));
			if (!Property)
			{
				OutError = FString::Printf(TEXT("property '%s' was not found in '%s'"),
					*Segments[Index], *CurrentStruct->GetName());
				return false;
			}
			if (Index == 0)
			{
				OutRootProperty = Property;
			}
			void* Value = Property->ContainerPtrToValuePtr<void>(CurrentContainer);
			if (Index == Segments.Num() - 1)
			{
				OutProperty = Property;
				OutValue = Value;
				return true;
			}

			if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				CurrentStruct = StructProperty->Struct;
				CurrentContainer = Value;
			}
			else if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
			{
				UObject* NestedObject = ObjectProperty->GetObjectPropertyValue(Value);
				if (!NestedObject)
				{
					OutError = FString::Printf(TEXT("object property '%s' is null"), *Segments[Index]);
					return false;
				}
				CurrentStruct = NestedObject->GetClass();
				CurrentContainer = NestedObject;
			}
			else
			{
				OutError = FString::Printf(TEXT("property '%s' cannot contain child fields"), *Segments[Index]);
				return false;
			}
		}
		return false;
	}

	static bool ImportPropertyValue(UObject* Object, const FString& PropertyPath, const FString& TextValue,
		bool bNotify, FString& OutError, FProperty** OutRootProperty = nullptr)
	{
		FProperty* Property = nullptr;
		FProperty* RootProperty = nullptr;
		void* Value = nullptr;
		if (!ResolvePropertyPath(Object, PropertyPath, Property, Value, RootProperty, OutError))
		{
			return false;
		}
		Object->Modify();
		if (bNotify)
		{
			Object->PreEditChange(RootProperty);
		}
		if (!Property->ImportText_Direct(*TextValue, Value, Object, PPF_None))
		{
			OutError = FString::Printf(TEXT("ImportText rejected '%s' for %s (%s)"),
				*TextValue, *PropertyPath, *Property->GetCPPType());
			return false;
		}
		if (bNotify)
		{
			FPropertyChangedEvent Event(RootProperty, EPropertyChangeType::ValueSet);
			Object->PostEditChangeProperty(Event);
		}
		if (OutRootProperty)
		{
			*OutRootProperty = RootProperty;
		}
		return true;
	}

	static FString ExportPropertyValue(UObject* Object, const FString& PropertyPath, FString& OutError)
	{
		FProperty* Property = nullptr;
		FProperty* RootProperty = nullptr;
		void* Value = nullptr;
		if (!ResolvePropertyPath(Object, PropertyPath, Property, Value, RootProperty, OutError))
		{
			return FString();
		}
		FString Result;
		Property->ExportTextItem_Direct(Result, Value, nullptr, Object, PPF_None);
		return Result;
	}

	static void FinishTemplateWrite(UWidgetBlueprint* WBP, bool bStructural)
	{
		if (!WBP)
		{
			return;
		}
		if (bStructural)
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
		}
		else
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(WBP);
		}
		WBP->MarkPackageDirty();
	}

	FString JsonValueToImportText(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid()) return FString();
		switch (Value->Type)
		{
		case EJson::String: return Value->AsString();
		case EJson::Number: return FString::SanitizeFloat(Value->AsNumber());
		case EJson::Boolean: return Value->AsBool() ? TEXT("True") : TEXT("False");
		case EJson::Null: return TEXT("None");
		default:
			FString Serialized;
			const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Serialized);
			FJsonSerializer::Serialize(Value, TEXT(""), Writer);
			return Serialized;
		}
	}

	bool ValidatePropertyNames(UClass* Class, const TSharedPtr<FJsonObject>& Properties, FString& OutError)
	{
		if (!Properties.IsValid()) return true;
		if (!Class)
		{
			OutError = TEXT("Cannot validate properties without a widget class.");
			return false;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Properties->Values)
		{
			FProperty* Property = Class->FindPropertyByName(FName(*Pair.Key));
			if (!Property)
			{
				OutError = FString::Printf(TEXT("Property '%s' does not exist on %s."),
					*Pair.Key, *Class->GetName());
				return false;
			}
			if (Property->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient | CPF_Deprecated))
			{
				OutError = FString::Printf(TEXT("Property '%s' is not safely editable."), *Pair.Key);
				return false;
			}
		}
		return true;
	}

	bool ApplyProperties(UObject* Object, const TSharedPtr<FJsonObject>& Properties, FString& OutError)
	{
		if (!Properties.IsValid()) return true;
		if (!Object)
		{
			OutError = TEXT("Property target is null.");
			return false;
		}
		Object->SetFlags(RF_Transactional);
		Object->Modify();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Properties->Values)
		{
			FProperty* Property = Object->GetClass()->FindPropertyByName(FName(*Pair.Key));
			if (!Property)
			{
				OutError = FString::Printf(TEXT("Property '%s' does not exist on %s."),
					*Pair.Key, *Object->GetClass()->GetName());
				return false;
			}
			const FString ImportText = JsonValueToImportText(Pair.Value);
			Object->PreEditChange(Property);
			void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
			if (!Property->ImportText_Direct(*ImportText, ValuePtr, Object, PPF_None))
			{
				OutError = FString::Printf(TEXT("Could not import '%s' into %s.%s."),
					*ImportText, *Object->GetName(), *Pair.Key);
				return false;
			}
			FPropertyChangedEvent ChangeEvent(Property, EPropertyChangeType::ValueSet);
			Object->PostEditChangeProperty(ChangeEvent);
		}
		return true;
	}

	TSharedPtr<FJsonObject> GetOptionalObject(
		const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		if (!Object.IsValid()) return nullptr;
		const TSharedPtr<FJsonObject>* Value = nullptr;
		return Object->TryGetObjectField(Field, Value) && Value ? *Value : nullptr;
	}

	struct FSimWidget
	{
		UClass* Class = nullptr;
		FString Parent;
		bool bExists = true;
		int32 ChildCount = 0;
	};

	bool WouldCreateCycle(
		const TMap<FString, FSimWidget>& Sim, const FString& Name, const FString& NewParent)
	{
		FString Current = NewParent;
		TSet<FString> Seen;
		while (!Current.IsEmpty() && !Seen.Contains(Current))
		{
			if (Current == Name) return true;
			Seen.Add(Current);
			const FSimWidget* Node = Sim.Find(Current);
			if (!Node || !Node->bExists) break;
			Current = Node->Parent;
		}
		return false;
	}

	bool PrevalidateBatch(
		UWidgetBlueprint* WBP,
		const TArray<TSharedPtr<FJsonValue>>& Operations,
		FString& OutError)
	{
		TMap<FString, FSimWidget> Sim;
		WBP->WidgetTree->ForEachWidget([&](UWidget* Widget)
		{
			if (!Widget) return;
			FSimWidget Node;
			Node.Class = Widget->GetClass();
			if (UPanelWidget* Parent = Widget->GetParent()) Node.Parent = Parent->GetName();
			if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget)) Node.ChildCount = Panel->GetChildrenCount();
			Sim.Add(Widget->GetName(), MoveTemp(Node));
		});

		for (int32 OperationIndex = 0; OperationIndex < Operations.Num(); ++OperationIndex)
		{
			const TSharedPtr<FJsonObject> Operation = Operations[OperationIndex]->AsObject();
			if (!Operation.IsValid())
			{
				OutError = FString::Printf(TEXT("Operation %d is not an object."), OperationIndex);
				return false;
			}
			FString Kind;
			FString Name;
			if (!Operation->TryGetStringField(TEXT("op"), Kind))
			{
				OutError = FString::Printf(TEXT("Operation %d requires non-empty 'op'."), OperationIndex);
				return false;
			}
			if (!Operation->TryGetStringField(TEXT("name"), Name))
			{
				Operation->TryGetStringField(TEXT("widget"), Name);
			}
			if (Name.IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("Operation %d requires non-empty 'name' (or 'widget')."), OperationIndex);
				return false;
			}
			Kind = Kind.ToLower();

			if (Kind == TEXT("create"))
			{
				if (const FSimWidget* Existing = Sim.Find(Name); Existing && Existing->bExists)
				{
					OutError = FString::Printf(TEXT("Widget '%s' already exists."), *Name);
					return false;
				}
				FString ClassToken;
				if (!Operation->TryGetStringField(TEXT("class"), ClassToken))
				{
					OutError = FString::Printf(TEXT("Create '%s' requires 'class'."), *Name);
					return false;
				}
				UClass* WidgetClass = ResolveClass(ClassToken, UWidget::StaticClass());
				if (!WidgetClass)
				{
					OutError = FString::Printf(TEXT("Could not resolve widget class '%s'."), *ClassToken);
					return false;
				}
				FString Parent;
				Operation->TryGetStringField(TEXT("parent"), Parent);
				if (Parent.IsEmpty())
				{
					if (WBP->WidgetTree->RootWidget)
					{
						OutError = TEXT("A root widget already exists; create requires a parent.");
						return false;
					}
				}
				else
				{
					FSimWidget* ParentNode = Sim.Find(Parent);
					if (!ParentNode || !ParentNode->bExists || !ParentNode->Class->IsChildOf(UPanelWidget::StaticClass()))
					{
						OutError = FString::Printf(TEXT("Parent '%s' is missing or is not a panel."), *Parent);
						return false;
					}
					const UPanelWidget* PanelCDO = Cast<UPanelWidget>(ParentNode->Class->GetDefaultObject());
					if (PanelCDO && !PanelCDO->CanHaveMultipleChildren() && ParentNode->ChildCount >= 1)
					{
						OutError = FString::Printf(TEXT("Parent '%s' cannot accept another child."), *Parent);
						return false;
					}
					++ParentNode->ChildCount;
				}
				if (!ValidatePropertyNames(WidgetClass, GetOptionalObject(Operation, TEXT("properties")), OutError))
					return false;
				FSimWidget NewNode;
				NewNode.Class = WidgetClass;
				NewNode.Parent = Parent;
				Sim.Add(Name, MoveTemp(NewNode));
			}
			else
			{
				FSimWidget* Node = Sim.Find(Name);
				if (!Node || !Node->bExists)
				{
					OutError = FString::Printf(TEXT("Widget '%s' does not exist at operation %d."), *Name, OperationIndex);
					return false;
				}

				if (Kind == TEXT("set"))
				{
					if (!ValidatePropertyNames(Node->Class, GetOptionalObject(Operation, TEXT("properties")), OutError))
						return false;
				}
				else if (Kind == TEXT("move"))
				{
					FString Parent;
					if (!Operation->TryGetStringField(TEXT("parent"), Parent) || Parent.IsEmpty())
					{
						OutError = FString::Printf(TEXT("Move '%s' requires 'parent'."), *Name);
						return false;
					}
					FSimWidget* ParentNode = Sim.Find(Parent);
					if (!ParentNode || !ParentNode->bExists || !ParentNode->Class->IsChildOf(UPanelWidget::StaticClass())
						|| WouldCreateCycle(Sim, Name, Parent))
					{
						OutError = FString::Printf(TEXT("Move '%s' has an invalid or cyclic parent '%s'."), *Name, *Parent);
						return false;
					}
					if (Node->Parent != Parent)
					{
						const UPanelWidget* PanelCDO = Cast<UPanelWidget>(ParentNode->Class->GetDefaultObject());
						if (PanelCDO && !PanelCDO->CanHaveMultipleChildren() && ParentNode->ChildCount >= 1)
						{
							OutError = FString::Printf(TEXT("Parent '%s' cannot accept another child."), *Parent);
							return false;
						}
						if (FSimWidget* OldParent = Sim.Find(Node->Parent)) --OldParent->ChildCount;
						++ParentNode->ChildCount;
						Node->Parent = Parent;
					}
				}
				else if (Kind == TEXT("reorder"))
				{
					if (Node->Parent.IsEmpty() || !Operation->HasTypedField<EJson::Number>(TEXT("index")))
					{
						OutError = FString::Printf(TEXT("Reorder '%s' requires a parented widget and numeric index."), *Name);
						return false;
					}
				}
				else if (Kind == TEXT("delete"))
				{
					bool bConfirmed = false;
					Operation->TryGetBoolField(TEXT("confirm_delete"), bConfirmed);
					if (!bConfirmed || Node->Parent.IsEmpty())
					{
						OutError = FString::Printf(TEXT("Delete '%s' requires confirm_delete=true and cannot delete the root."), *Name);
						return false;
					}
					TArray<FString> Pending{ Name };
					while (Pending.Num() > 0)
					{
						const FString Current = Pending.Pop();
						if (FSimWidget* CurrentNode = Sim.Find(Current)) CurrentNode->bExists = false;
						for (const TPair<FString, FSimWidget>& Pair : Sim)
							if (Pair.Value.bExists && Pair.Value.Parent == Current) Pending.Add(Pair.Key);
					}
					if (FSimWidget* OldParent = Sim.Find(Node->Parent)) --OldParent->ChildCount;
				}
				else
				{
					OutError = FString::Printf(TEXT("Unsupported operation '%s'."), *Kind);
					return false;
				}
			}
		}
		return true;
	}

	static UWidgetAnimation* FindAnimation(UWidgetBlueprint* WBP, const FString& AnimationName)
	{
		if (!WBP)
		{
			return nullptr;
		}
		for (UWidgetAnimation* Animation : WBP->Animations)
		{
			if (!Animation)
			{
				continue;
			}
			if (Animation->GetName().Equals(AnimationName, ESearchCase::IgnoreCase))
			{
				return Animation;
			}
#if WITH_EDITOR
			if (Animation->GetDisplayLabel().Equals(AnimationName, ESearchCase::IgnoreCase))
			{
				return Animation;
			}
#endif
		}
		return nullptr;
	}

	static FGuid FindOrAddAnimationBinding(UWidgetBlueprint* WBP, UWidgetAnimation* Animation,
		const FString& WidgetName, FString& OutError)
	{
		UWidget* Widget = FindWidgetByName(WBP, WidgetName);
		if (!Widget)
		{
			OutError = FString::Printf(TEXT("widget '%s' was not found"), *WidgetName);
			return FGuid();
		}
		for (const FWidgetAnimationBinding& Binding : Animation->AnimationBindings)
		{
			if (Binding.WidgetName == Widget->GetFName() && !Binding.SlotWidgetName.IsValid())
			{
				return Binding.AnimationGuid;
			}
		}
		UMovieScene* Scene = Animation->GetMovieScene();
		if (!Scene)
		{
			OutError = TEXT("animation has no MovieScene");
			return FGuid();
		}
		const FGuid Guid = Scene->AddPossessable(Widget->GetName(), Widget->GetClass());
		if (!Guid.IsValid())
		{
			OutError = TEXT("MovieScene failed to create a possessable binding");
			return FGuid();
		}
		FWidgetAnimationBinding Binding;
		Binding.AnimationGuid = Guid;
		Binding.WidgetName = Widget->GetFName();
		Binding.bIsRootWidget = false;
		Animation->AnimationBindings.Add(Binding);
		return Guid;
	}

	static FFrameNumber SecondsToFrame(const UMovieScene* Scene, float Seconds)
	{
		const FFrameTime FrameTime = FMath::Max(0.f, Seconds) * Scene->GetTickResolution();
		return FrameTime.RoundToFrame();
	}

	static void AddFloatKey(FMovieSceneFloatChannel& Channel, FFrameNumber Time, float Value, const FString& Interpolation)
	{
		if (Interpolation.Equals(TEXT("constant"), ESearchCase::IgnoreCase))
		{
			Channel.AddConstantKey(Time, Value);
		}
		else if (Interpolation.Equals(TEXT("linear"), ESearchCase::IgnoreCase))
		{
			Channel.AddLinearKey(Time, Value);
		}
		else
		{
			Channel.AddCubicKey(Time, Value, RCTM_Auto);
		}
	}

	static UMovieScenePropertyTrack* FindPropertyTrack(UMovieScene* Scene, const FGuid& Guid,
		UClass* TrackClass, const FString& PropertyName)
	{
		for (UMovieSceneTrack* Track : Scene->FindTracks(TrackClass, Guid))
		{
			if (UMovieScenePropertyTrack* PropertyTrack = Cast<UMovieScenePropertyTrack>(Track))
			{
				if (PropertyTrack->GetPropertyName().ToString().Equals(PropertyName, ESearchCase::IgnoreCase))
				{
					return PropertyTrack;
				}
			}
		}
		return nullptr;
	}

	static UMovieScenePropertyTrack* FindOrAddPropertyTrack(UMovieScene* Scene, const FGuid& Guid,
		UClass* TrackClass, const FString& PropertyName)
	{
		if (UMovieScenePropertyTrack* Existing = FindPropertyTrack(Scene, Guid, TrackClass, PropertyName))
		{
			return Existing;
		}
		UMovieScenePropertyTrack* Track = Cast<UMovieScenePropertyTrack>(Scene->AddTrack(TrackClass, Guid));
		if (Track)
		{
			Track->SetPropertyNameAndPath(FName(*PropertyName), PropertyName);
#if WITH_EDITORONLY_DATA
			Track->SetDisplayName(FText::FromString(PropertyName));
#endif
		}
		return Track;
	}

	static UMovieSceneSection* FindOrAddSection(UMovieScenePropertyTrack* Track)
	{
		if (!Track)
		{
			return nullptr;
		}
		const TArray<UMovieSceneSection*>& Sections = Track->GetAllSections();
		if (!Sections.IsEmpty() && Sections[0])
		{
			return Sections[0];
		}
		UMovieSceneSection* Section = Track->CreateNewSection();
		if (Section)
		{
			Section->SetRange(TRange<FFrameNumber>::All());
			Track->AddSection(*Section);
		}
		return Section;
	}

	static UUserWidget* ResolveLiveInstance(const FString& Handle, const TCHAR* FunctionName)
	{
		TWeakObjectPtr<UUserWidget>* Entry = LiveInstances.Find(Handle);
		UUserWidget* Widget = Entry ? Entry->Get() : nullptr;
		if (!Widget)
		{
			LiveInstances.Remove(Handle);
			LogFailure(FunctionName, FString::Printf(TEXT("unknown or expired instance handle '%s'"), *Handle));
		}
		return Widget;
	}

	static UWidget* ResolveLiveWidget(UUserWidget* Instance, const FString& WidgetName)
	{
		if (!Instance)
		{
			return nullptr;
		}
		if (WidgetName.IsEmpty() || WidgetName.Equals(TEXT("self"), ESearchCase::IgnoreCase)
			|| WidgetName == Instance->GetName())
		{
			return Instance;
		}
		return Instance->WidgetTree ? Instance->WidgetTree->FindWidget(FName(*WidgetName)) : nullptr;
	}

	static UWidgetAnimation* FindLiveAnimation(UUserWidget* Instance, const FString& AnimationName)
	{
		if (!Instance)
		{
			return nullptr;
		}
		if (UWidgetBlueprintGeneratedClass* Class = Cast<UWidgetBlueprintGeneratedClass>(Instance->GetClass()))
		{
			for (UWidgetAnimation* Animation : Class->Animations)
			{
				if (!Animation)
				{
					continue;
				}
				if (Animation->GetName().Equals(AnimationName, ESearchCase::IgnoreCase)
#if WITH_EDITOR
					|| Animation->GetDisplayLabel().Equals(AnimationName, ESearchCase::IgnoreCase)
#endif
					)
				{
					return Animation;
				}
			}
		}
		return nullptr;
	}

	static FString BlueprintStatusToString(EBlueprintStatus Status)
	{
		switch (Status)
		{
		case BS_Unknown: return TEXT("Unknown");
		case BS_Dirty: return TEXT("Dirty");
		case BS_Error: return TEXT("Error");
		case BS_UpToDate: return TEXT("UpToDate");
		case BS_BeingCreated: return TEXT("BeingCreated");
		case BS_UpToDateWithWarnings: return TEXT("UpToDateWithWarnings");
		default: return TEXT("Unknown");
		}
	}

	static void AddIssue(FBridgeWidgetValidationReport& Report, const FString& Severity,
		const FString& Code, const FString& WidgetName, const FString& Message)
	{
		FBridgeWidgetValidationIssue Issue;
		Issue.Severity = Severity;
		Issue.Code = Code;
		Issue.WidgetName = WidgetName;
		Issue.Message = Message;
		Report.Issues.Add(MoveTemp(Issue));
	}

#if !UE_VERSION_OLDER_THAN(5, 7, 0)
	static UMVVMBlueprintView* GetMVVMView(UWidgetBlueprint* WBP, bool bCreate)
	{
		if (!WBP)
		{
			return nullptr;
		}
		UMVVMWidgetBlueprintExtension_View* Extension = bCreate
			? UWidgetBlueprintExtension::RequestExtension<UMVVMWidgetBlueprintExtension_View>(WBP)
			: UWidgetBlueprintExtension::GetExtension<UMVVMWidgetBlueprintExtension_View>(WBP);
		if (Extension && bCreate && !Extension->GetBlueprintView())
		{
			Extension->CreateBlueprintViewInstance();
		}
		return Extension ? Extension->GetBlueprintView() : nullptr;
	}

	static bool ParseCreationType(const FString& Text, EMVVMBlueprintViewModelContextCreationType& Out)
	{
		if (Text.Equals(TEXT("Manual"), ESearchCase::IgnoreCase)) Out = EMVVMBlueprintViewModelContextCreationType::Manual;
		else if (Text.Equals(TEXT("CreateInstance"), ESearchCase::IgnoreCase)) Out = EMVVMBlueprintViewModelContextCreationType::CreateInstance;
		else if (Text.Equals(TEXT("GlobalViewModelCollection"), ESearchCase::IgnoreCase)) Out = EMVVMBlueprintViewModelContextCreationType::GlobalViewModelCollection;
		else if (Text.Equals(TEXT("PropertyPath"), ESearchCase::IgnoreCase)) Out = EMVVMBlueprintViewModelContextCreationType::PropertyPath;
		else if (Text.Equals(TEXT("Resolver"), ESearchCase::IgnoreCase)) Out = EMVVMBlueprintViewModelContextCreationType::Resolver;
		else return false;
		return true;
	}

	static bool ParseBindingMode(const FString& Text, EMVVMBindingMode& Out)
	{
		if (Text.Equals(TEXT("OneTimeToDestination"), ESearchCase::IgnoreCase)) Out = EMVVMBindingMode::OneTimeToDestination;
		else if (Text.Equals(TEXT("OneWayToDestination"), ESearchCase::IgnoreCase)) Out = EMVVMBindingMode::OneWayToDestination;
		else if (Text.Equals(TEXT("TwoWay"), ESearchCase::IgnoreCase)) Out = EMVVMBindingMode::TwoWay;
		else if (Text.Equals(TEXT("OneWayToSource"), ESearchCase::IgnoreCase)) Out = EMVVMBindingMode::OneWayToSource;
		else return false;
		return true;
	}

	static FString CreationTypeToString(EMVVMBlueprintViewModelContextCreationType Value)
	{
		const UEnum* Enum = StaticEnum<EMVVMBlueprintViewModelContextCreationType>();
		return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Value)) : TEXT("Unknown");
	}

	static FString BindingModeToString(EMVVMBindingMode Value)
	{
		const UEnum* Enum = StaticEnum<EMVVMBindingMode>();
		return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Value)) : TEXT("Unknown");
	}

	static UStruct* NextFieldOwner(const FProperty* Property)
	{
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			return StructProperty->Struct;
		}
		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			return ObjectProperty->PropertyClass;
		}
		return nullptr;
	}

	static bool AppendMVVMPropertyPath(UWidgetBlueprint* WBP, UStruct* Root,
		const FString& FieldPath, FMVVMBlueprintPropertyPath& OutPath, FString& OutError)
	{
		TArray<FString> Segments;
		FieldPath.ParseIntoArray(Segments, TEXT("."), true);
		if (!Root || Segments.IsEmpty())
		{
			OutError = TEXT("field path is empty or has no root class");
			return false;
		}
		UStruct* Current = Root;
		for (int32 Index = 0; Index < Segments.Num(); ++Index)
		{
			const FProperty* Property = FindFProperty<FProperty>(Current, FName(*Segments[Index]));
			if (!Property)
			{
				OutError = FString::Printf(TEXT("field '%s' was not found on '%s'"), *Segments[Index], *Current->GetName());
				return false;
			}
			OutPath.AppendPropertyPath(WBP, UE::MVVM::FMVVMConstFieldVariant(Property));
			if (Index < Segments.Num() - 1)
			{
				Current = NextFieldOwner(Property);
				if (!Current)
				{
					OutError = FString::Printf(TEXT("field '%s' cannot contain '%s'"), *Segments[Index], *Segments[Index + 1]);
					return false;
				}
			}
		}
		return true;
	}

	static UObject* ResolveLiveViewModel(UUserWidget* Instance, const FString& ViewModelName,
		INotifyFieldValueChanged*& OutNotify)
	{
		OutNotify = nullptr;
		UMVVMView* View = UMVVMSubsystem::GetViewFromUserWidget(Instance);
		if (!View)
		{
			return nullptr;
		}
		TScriptInterface<INotifyFieldValueChanged> ViewModel = View->GetViewModel(FName(*ViewModelName));
		OutNotify = ViewModel.GetInterface();
		return ViewModel.GetObject();
	}
#endif
}

// Asset and tree authoring ---------------------------------------------------

TArray<FBridgeWidgetClassInfo> UUnrealBridgeUMGLibrary::ListWidgetClasses(
	const FString& Query, bool bIncludeAbstract, int32 MaxResults)
{
	TArray<FBridgeWidgetClassInfo> Result;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (!Class || !Class->IsChildOf(UWidget::StaticClass()) || Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			continue;
		}
		const bool bAbstract = Class->HasAnyClassFlags(CLASS_Abstract);
		if (bAbstract && !bIncludeAbstract)
		{
			continue;
		}
		const FString Searchable = Class->GetName() + TEXT(" ") + Class->GetPathName() + TEXT(" ") + Class->GetDisplayNameText().ToString();
		if (!Query.IsEmpty() && !Searchable.Contains(Query, ESearchCase::IgnoreCase))
		{
			continue;
		}
		FBridgeWidgetClassInfo Info;
		Info.Name = Class->GetName();
		Info.ClassPath = Class->GetPathName();
		Info.DisplayName = Class->GetDisplayNameText().ToString();
		Info.Category = Class->GetMetaData(TEXT("Category"));
		Info.bIsPanel = Class->IsChildOf(UPanelWidget::StaticClass());
		Info.bIsAbstract = bAbstract;
		if (Info.bIsPanel && !bAbstract)
		{
			if (const UPanelWidget* PanelCDO = Cast<UPanelWidget>(Class->GetDefaultObject()))
			{
				Info.bCanHaveMultipleChildren = PanelCDO->CanHaveMultipleChildren();
			}
		}
		Result.Add(MoveTemp(Info));
	}
	Result.Sort([](const FBridgeWidgetClassInfo& A, const FBridgeWidgetClassInfo& B)
	{
		return A.ClassPath < B.ClassPath;
	});
	if (MaxResults > 0 && Result.Num() > MaxResults)
	{
		Result.SetNum(MaxResults);
	}
	return Result;
}

FBridgeWidgetOperationResult UUnrealBridgeUMGLibrary::CreateWidgetBlueprint(
	const FString& AssetPath, const FString& ParentClassPath)
{
	using namespace BridgeUMGImpl;
	FBridgeWidgetOperationResult Result;
	FString PackagePath;
	FString AssetName;
	if (!SplitAssetPath(AssetPath, PackagePath, AssetName))
	{
		Result.Error = TEXT("invalid AssetPath; expected /Game/Folder/WBP_Name");
		return Result;
	}
	UClass* ParentClass = ResolveClass(ParentClassPath, UUserWidget::StaticClass(), TEXT("/Script/UMG.UserWidget"));
	if (!ParentClass)
	{
		Result.Error = FString::Printf(TEXT("could not resolve UserWidget parent class '%s'"), *ParentClassPath);
		return Result;
	}
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
	Factory->BlueprintType = BPTYPE_Normal;
	Factory->ParentClass = ParentClass;
	UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(AssetTools.CreateAsset(AssetName, PackagePath, UWidgetBlueprint::StaticClass(), Factory));
	if (!WBP)
	{
		Result.Error = FString::Printf(TEXT("asset creation failed; '%s/%s' may already exist"), *PackagePath, *AssetName);
		return Result;
	}
	WBP->MarkPackageDirty();
	Result.bSuccess = true;
	Result.Path = WBP->GetPathName();
	Result.Name = WBP->GetName();
	return Result;
}

FBridgeWidgetOperationResult UUnrealBridgeUMGLibrary::AddWidget(
	const FString& WidgetBlueprintPath, const FString& WidgetClassPath,
	const FString& WidgetName, const FString& ParentName, int32 InsertIndex)
{
	using namespace BridgeUMGImpl;
	FBridgeWidgetOperationResult Result;
	UWidgetBlueprint* WBP = LoadWBP(WidgetBlueprintPath, TEXT("add_widget"));
	if (!WBP || !WBP->WidgetTree)
	{
		Result.Error = TEXT("Widget Blueprint has no WidgetTree");
		return Result;
	}
	FText NameReason;
	if (WidgetName.IsEmpty() || !FName::IsValidXName(FName(*WidgetName), INVALID_OBJECTNAME_CHARACTERS, &NameReason))
	{
		Result.Error = WidgetName.IsEmpty() ? TEXT("WidgetName is empty") : NameReason.ToString();
		return Result;
	}
	if (FindWidgetByName(WBP, WidgetName))
	{
		Result.Error = FString::Printf(TEXT("widget '%s' already exists"), *WidgetName);
		return Result;
	}
	UClass* WidgetClass = ResolveClass(WidgetClassPath, UWidget::StaticClass());
	if (!WidgetClass || WidgetClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
	{
		Result.Error = FString::Printf(TEXT("'%s' is not a concrete UWidget class"), *WidgetClassPath);
		return Result;
	}
	UPanelWidget* Parent = nullptr;
	if (!ParentName.IsEmpty())
	{
		Parent = Cast<UPanelWidget>(FindWidgetByName(WBP, ParentName));
		if (!Parent)
		{
			Result.Error = FString::Printf(TEXT("parent '%s' is missing or is not a panel"), *ParentName);
			return Result;
		}
		if (!Parent->CanAddMoreChildren())
		{
			Result.Error = FString::Printf(TEXT("panel '%s' cannot accept another child"), *ParentName);
			return Result;
		}
	}
	else if (WBP->WidgetTree->RootWidget)
	{
		Result.Error = TEXT("ParentName is empty but the Widget Blueprint already has a root");
		return Result;
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("UnrealBridge Add Widget")));
	WBP->Modify();
	WBP->WidgetTree->Modify();
	UWidget* Widget = WBP->WidgetTree->ConstructWidget<UWidget>(WidgetClass, FName(*WidgetName));
	if (!Widget)
	{
		Result.Error = TEXT("WidgetTree::ConstructWidget returned null");
		return Result;
	}
	if (Parent)
	{
		Parent->Modify();
		UPanelSlot* Slot = InsertIndex >= 0 && InsertIndex <= Parent->GetChildrenCount()
			? Parent->InsertChildAt(InsertIndex, Widget)
			: Parent->AddChild(Widget);
		if (!Slot)
		{
			WBP->WidgetTree->RemoveWidget(Widget);
			Result.Error = FString::Printf(TEXT("panel '%s' rejected widget '%s'"), *ParentName, *WidgetName);
			return Result;
		}
	}
	else
	{
		WBP->WidgetTree->RootWidget = Widget;
	}
	FinishTemplateWrite(WBP, true);
	Result.bSuccess = true;
	Result.Path = WBP->GetPathName();
	Result.Name = Widget->GetName();
	return Result;
}

bool UUnrealBridgeUMGLibrary::RemoveWidget(const FString& WidgetBlueprintPath, const FString& WidgetName)
{
	using namespace BridgeUMGImpl;
	UWidgetBlueprint* WBP = LoadWBP(WidgetBlueprintPath, TEXT("remove_widget"));
	UWidget* Widget = FindWidgetByName(WBP, WidgetName);
	if (!Widget)
	{
		LogFailure(TEXT("remove_widget"), FString::Printf(TEXT("widget '%s' was not found"), *WidgetName));
		return false;
	}
	TArray<UWidget*> Removing;
	UWidgetTree::ForWidgetAndChildren(Widget, [&Removing](UWidget* Child) { Removing.Add(Child); });
	TSet<FName> RemovingNames;
	for (UWidget* Child : Removing) RemovingNames.Add(Child->GetFName());

	const FScopedTransaction Transaction(FText::FromString(TEXT("UnrealBridge Remove Widget")));
	WBP->Modify();
	WBP->WidgetTree->Modify();
	WBP->Bindings.RemoveAll([&RemovingNames](const FDelegateEditorBinding& Binding)
	{
		return RemovingNames.Contains(FName(*Binding.ObjectName));
	});
	for (UEdGraph* Graph : WBP->UbergraphPages)
	{
		if (!Graph) continue;
		TArray<UEdGraphNode*> Nodes = Graph->Nodes;
		for (UEdGraphNode* Node : Nodes)
		{
			if (UK2Node_ComponentBoundEvent* Event = Cast<UK2Node_ComponentBoundEvent>(Node))
			{
				if (RemovingNames.Contains(Event->ComponentPropertyName)) Graph->RemoveNode(Node);
			}
		}
	}
	for (UWidgetAnimation* Animation : WBP->Animations)
	{
		if (!Animation) continue;
		Animation->Modify();
		for (int32 Index = Animation->AnimationBindings.Num() - 1; Index >= 0; --Index)
		{
			const FWidgetAnimationBinding& Binding = Animation->AnimationBindings[Index];
			if (RemovingNames.Contains(Binding.WidgetName))
			{
				if (UMovieScene* Scene = Animation->GetMovieScene()) Scene->RemovePossessable(Binding.AnimationGuid);
				Animation->AnimationBindings.RemoveAt(Index);
			}
		}
	}
#if !UE_VERSION_OLDER_THAN(5, 7, 0)
	if (UMVVMBlueprintView* View = GetMVVMView(WBP, false))
	{
		for (int32 Index = View->GetNumBindings() - 1; Index >= 0; --Index)
		{
			const FMVVMBlueprintViewBinding* Binding = View->GetBindingAt(Index);
			if (Binding && RemovingNames.Contains(Binding->DestinationPath.GetWidgetName())) View->RemoveBindingAt(Index);
		}
		View->OnBindingsUpdated.Broadcast();
	}
#endif
	if (WBP->WidgetTree->RootWidget == Widget)
	{
		WBP->WidgetTree->RootWidget = nullptr;
	}
	const bool bRemoved = WBP->WidgetTree->RemoveWidget(Widget);
	FinishTemplateWrite(WBP, true);
	return bRemoved || !FindWidgetByName(WBP, WidgetName);
}

bool UUnrealBridgeUMGLibrary::RenameWidget(
	const FString& WidgetBlueprintPath, const FString& WidgetName, const FString& NewName)
{
	using namespace BridgeUMGImpl;
	UWidgetBlueprint* WBP = LoadWBP(WidgetBlueprintPath, TEXT("rename_widget"));
	UWidget* Widget = FindWidgetByName(WBP, WidgetName);
	if (!Widget || NewName.IsEmpty() || FindWidgetByName(WBP, NewName))
	{
		LogFailure(TEXT("rename_widget"), TEXT("source widget is missing, NewName is empty, or NewName is already in use"));
		return false;
	}
	FText NameError;
	if (!FName::IsValidXName(FName(*NewName), INVALID_OBJECTNAME_CHARACTERS, &NameError))
	{
		LogFailure(TEXT("rename_widget"), NameError.ToString());
		return false;
	}
	const FName OldFName(*WidgetName);
	const FName NewFName(*NewName);
	const FScopedTransaction Transaction(FText::FromString(TEXT("UnrealBridge Rename Widget")));
	WBP->Modify();
	Widget->Modify();
	FBlueprintEditorUtils::ReplaceVariableReferences(WBP, OldFName, NewFName);
	for (FDelegateEditorBinding& Binding : WBP->Bindings)
	{
		if (Binding.ObjectName.Equals(WidgetName, ESearchCase::CaseSensitive)) Binding.ObjectName = NewName;
	}
	for (UWidgetAnimation* Animation : WBP->Animations)
	{
		if (!Animation) continue;
		Animation->Modify();
		for (FWidgetAnimationBinding& Binding : Animation->AnimationBindings)
		{
			if (Binding.WidgetName == OldFName)
			{
				Binding.WidgetName = NewFName;
				if (FMovieScenePossessable* Possessable = Animation->GetMovieScene()->FindPossessable(Binding.AnimationGuid))
				{
					Possessable->SetName(NewName);
				}
			}
		}
	}
#if !UE_VERSION_OLDER_THAN(5, 7, 0)
	if (UMVVMWidgetBlueprintExtension_View* Extension = UWidgetBlueprintExtension::GetExtension<UMVVMWidgetBlueprintExtension_View>(WBP))
	{
		Extension->RenameWidgetExtensions(OldFName, NewFName);
		if (UMVVMBlueprintView* View = Extension->GetBlueprintView())
		{
			for (FMVVMBlueprintViewBinding& Binding : View->GetBindings())
			{
				if (Binding.SourcePath.GetWidgetName() == OldFName) Binding.SourcePath.SetWidgetName(NewFName);
				if (Binding.DestinationPath.GetWidgetName() == OldFName) Binding.DestinationPath.SetWidgetName(NewFName);
			}
			View->OnBindingsUpdated.Broadcast();
		}
	}
#endif
	if (!Widget->Rename(*NewName, WBP->WidgetTree, REN_DontCreateRedirectors))
	{
		LogFailure(TEXT("rename_widget"), TEXT("UObject rename failed"));
		return false;
	}
	FinishTemplateWrite(WBP, true);
	return true;
}

bool UUnrealBridgeUMGLibrary::ReparentWidget(
	const FString& WidgetBlueprintPath, const FString& WidgetName,
	const FString& NewParentName, int32 InsertIndex)
{
	using namespace BridgeUMGImpl;
	UWidgetBlueprint* WBP = LoadWBP(WidgetBlueprintPath, TEXT("reparent_widget"));
	UWidget* Widget = FindWidgetByName(WBP, WidgetName);
	UPanelWidget* NewParent = Cast<UPanelWidget>(FindWidgetByName(WBP, NewParentName));
	if (!Widget || !NewParent || WBP->WidgetTree->RootWidget == Widget || Widget == NewParent)
	{
		LogFailure(TEXT("reparent_widget"), TEXT("widget/parent is invalid, or root/self reparenting was requested"));
		return false;
	}
	for (UWidget* Cursor = NewParent; Cursor; )
	{
		if (Cursor == Widget)
		{
			LogFailure(TEXT("reparent_widget"), TEXT("the new parent is a descendant of the widget"));
			return false;
		}
		int32 ChildIndex = INDEX_NONE;
		Cursor = UWidgetTree::FindWidgetParent(Cursor, ChildIndex);
	}
	if (!NewParent->CanAddMoreChildren() && NewParent->GetChildIndex(Widget) == INDEX_NONE)
	{
		LogFailure(TEXT("reparent_widget"), TEXT("the new parent cannot accept another child"));
		return false;
	}
	const FScopedTransaction Transaction(FText::FromString(TEXT("UnrealBridge Reparent Widget")));
	WBP->Modify();
	Widget->Modify();
	int32 OldIndex = INDEX_NONE;
	if (UPanelWidget* OldParent = UWidgetTree::FindWidgetParent(Widget, OldIndex))
	{
		OldParent->Modify();
		OldParent->RemoveChild(Widget);
	}
	NewParent->Modify();
	UPanelSlot* Slot = InsertIndex >= 0 && InsertIndex <= NewParent->GetChildrenCount()
		? NewParent->InsertChildAt(InsertIndex, Widget)
		: NewParent->AddChild(Widget);
	if (!Slot)
	{
		LogFailure(TEXT("reparent_widget"), TEXT("the target panel rejected the widget"));
		return false;
	}
	FinishTemplateWrite(WBP, true);
	return true;
}

bool UUnrealBridgeUMGLibrary::SetWidgetIsVariable(
	const FString& WidgetBlueprintPath, const FString& WidgetName, bool bIsVariable)
{
	using namespace BridgeUMGImpl;
	UWidgetBlueprint* WBP = LoadWBP(WidgetBlueprintPath, TEXT("set_widget_is_variable"));
	UWidget* Widget = FindWidgetByName(WBP, WidgetName);
	if (!Widget)
	{
		LogFailure(TEXT("set_widget_is_variable"), FString::Printf(TEXT("widget '%s' was not found"), *WidgetName));
		return false;
	}
	Widget->Modify();
	Widget->bIsVariable = bIsVariable;
	FinishTemplateWrite(WBP, true);
	return true;
}

bool UUnrealBridgeUMGLibrary::AddWidgetBlueprintToPIEViewport(
	const FString& WidgetBlueprintPath, int32 ZOrder)
{
	return !SpawnWidgetInstance(WidgetBlueprintPath, ZOrder).IsEmpty();
}

int32 UUnrealBridgeUMGLibrary::RemovePIEPreviewWidgets()
{
	return RemoveAllWidgetInstances();
}

FBridgeWidgetRenderResult UUnrealBridgeUMGLibrary::RenderWidgetBlueprintToPNG(
	const FString& WidgetBlueprintPath, int32 LogicalWidth, int32 LogicalHeight,
	float Scale, const FString& OutputFile)
{
	FBridgeWidgetRenderResult Result;
	if (LogicalWidth <= 0 || LogicalHeight <= 0)
	{
		Result.Error = TEXT("LogicalWidth and LogicalHeight must be positive.");
		return Result;
	}
	if (Scale <= 0.0f)
	{
		Result.Error = TEXT("Scale must be greater than zero.");
		return Result;
	}
	if (OutputFile.IsEmpty())
	{
		Result.Error = TEXT("OutputFile must not be empty.");
		return Result;
	}

	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath);
	if (!WBP || !WBP->GeneratedClass)
	{
		Result.Error = FString::Printf(TEXT("Widget Blueprint '%s' is missing or not compiled."),
			*WidgetBlueprintPath);
		return Result;
	}
	UClass* GeneratedClass = WBP->GeneratedClass.Get();
	if (!GeneratedClass || !GeneratedClass->IsChildOf(UUserWidget::StaticClass()))
	{
		Result.Error = TEXT("The generated class is not a UUserWidget subclass.");
		return Result;
	}

	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		Result.Error = TEXT("No editor world is available for widget construction.");
		return Result;
	}
	if (!FSlateApplication::IsInitialized())
	{
		Result.Error = TEXT("Slate is not initialized.");
		return Result;
	}

	UUserWidget* Instance = CreateWidget<UUserWidget>(EditorWorld, GeneratedClass);
	if (!Instance)
	{
		Result.Error = TEXT("CreateWidget returned null.");
		return Result;
	}
	Instance->SetDesignerFlags(EWidgetDesignFlags::Designing |
		EWidgetDesignFlags::Previewing | EWidgetDesignFlags::ExecutePreConstruct);

	const uint32 PhysicalWidth = FMath::Max(1u,
		static_cast<uint32>(FMath::RoundToInt(static_cast<float>(LogicalWidth) * Scale)));
	const uint32 PhysicalHeight = FMath::Max(1u,
		static_cast<uint32>(FMath::RoundToInt(static_cast<float>(LogicalHeight) * Scale)));
	const bool bUseGammaCorrection = true;
	const bool bIsLinearSpace = !bUseGammaCorrection;
	const EPixelFormat Format =
		FSlateApplication::Get().GetRenderer()->GetSlateRecommendedColorFormat();

	UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
	RenderTarget->ClearColor = FLinearColor::Transparent;
	RenderTarget->Filter = TF_Bilinear;
	RenderTarget->SRGB = bIsLinearSpace;
	RenderTarget->TargetGamma = 1.0f;
	RenderTarget->InitCustomFormat(PhysicalWidth, PhysicalHeight, Format, bIsLinearSpace);
	RenderTarget->UpdateResourceImmediate(true);

	FWidgetRenderer* Renderer = new FWidgetRenderer(bUseGammaCorrection, true);
	Renderer->SetIsPrepassNeeded(true);
	const TSharedRef<SWidget> SlateWidget = Instance->TakeWidget();
	const FVector2D LogicalSize(LogicalWidth, LogicalHeight);

	// The warm-up draw creates material handles. Finish shader compilation before
	// the evidence draw so material-backed brushes are not silently omitted.
	Renderer->DrawWidget(RenderTarget, SlateWidget, Scale, LogicalSize, 0.0f);
	FlushRenderingCommands();
	if (GShaderCompilingManager)
	{
		GShaderCompilingManager->FinishAllCompilation();
	}
	Renderer->DrawWidget(RenderTarget, SlateWidget, Scale, LogicalSize, 0.0f);
	FlushRenderingCommands();

	FString AbsoluteOutput = FPaths::ConvertRelativePathToFull(OutputFile);
	if (!AbsoluteOutput.EndsWith(TEXT(".png"), ESearchCase::IgnoreCase))
	{
		AbsoluteOutput += TEXT(".png");
	}
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(AbsoluteOutput), true);
	TUniquePtr<FArchive> Writer(IFileManager::Get().CreateFileWriter(*AbsoluteOutput));
	const bool bExported = Writer && FImageUtils::ExportRenderTarget2DAsPNG(RenderTarget, *Writer);
	if (Writer)
	{
		Writer->Close();
	}
	BeginCleanup(Renderer);

	if (!bExported)
	{
		Result.Error = FString::Printf(TEXT("Could not export widget render to '%s'."), *AbsoluteOutput);
		return Result;
	}

	Result.bSuccess = true;
	Result.OutputFile = AbsoluteOutput;
	Result.Width = static_cast<int32>(PhysicalWidth);
	Result.Height = static_cast<int32>(PhysicalHeight);
	return Result;
}

FBridgeWidgetBatchResult UUnrealBridgeUMGLibrary::ApplyWidgetTreeBatch(
	const FString& WidgetBlueprintPath, const FString& PatchJson,
	bool bCompileAfter, bool bSaveAfter)
{
	FBridgeWidgetBatchResult Result;
	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath);
	if (!WBP || !WBP->WidgetTree)
	{
		Result.Error = TEXT("Widget Blueprint or WidgetTree could not be loaded.");
		return Result;
	}

	TSharedPtr<FJsonObject> Patch;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PatchJson);
	if (!FJsonSerializer::Deserialize(Reader, Patch) || !Patch.IsValid())
	{
		Result.Error = TEXT("PatchJson is not a valid JSON object.");
		return Result;
	}
	const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
	if (!Patch->TryGetArrayField(TEXT("operations"), Operations) || !Operations)
	{
		Result.Error = TEXT("PatchJson requires an 'operations' array.");
		return Result;
	}
	if (Operations->Num() == 0)
	{
		Result.bSuccess = true;
		return Result;
	}
	if (!BridgeUMGImpl::PrevalidateBatch(WBP, *Operations, Result.Error))
	{
		return Result;
	}

	bool bFailed = false;
	bool bStructural = false;
	{
		const FScopedTransaction Transaction(
			NSLOCTEXT("UnrealBridge", "ApplyWidgetTreeBatch", "Apply WidgetTree Batch"));
		WBP->SetFlags(RF_Transactional);
		WBP->WidgetTree->SetFlags(RF_Transactional);
		WBP->Modify();
		WBP->WidgetTree->Modify();

		for (int32 OperationIndex = 0; OperationIndex < Operations->Num(); ++OperationIndex)
		{
			const TSharedPtr<FJsonObject> Operation = (*Operations)[OperationIndex]->AsObject();
			FString Kind = Operation->GetStringField(TEXT("op")).ToLower();
			FString Name;
			if (!Operation->TryGetStringField(TEXT("name"), Name))
			{
				Operation->TryGetStringField(TEXT("widget"), Name);
			}
			FString Failure;

			if (Kind == TEXT("create"))
			{
				UClass* WidgetClass = BridgeUMGImpl::ResolveClass(Operation->GetStringField(TEXT("class")), UWidget::StaticClass());
				UWidget* Widget = WBP->WidgetTree->ConstructWidget<UWidget>(WidgetClass, FName(*Name));
				if (!Widget)
				{
					Failure = FString::Printf(TEXT("Failed to construct widget '%s'."), *Name);
				}
				else
				{
					Widget->SetFlags(RF_Transactional);
					Widget->Modify();
					bool bIsVariable = false;
					Operation->TryGetBoolField(TEXT("is_variable"), bIsVariable);
					Widget->bIsVariable = bIsVariable;
					WBP->OnVariableAdded(Widget->GetFName());

					FString ParentName;
					Operation->TryGetStringField(TEXT("parent"), ParentName);
					if (ParentName.IsEmpty())
					{
						WBP->WidgetTree->RootWidget = Widget;
					}
					else if (UPanelWidget* Parent = Cast<UPanelWidget>(
						BridgeUMGImpl::FindWidgetByName(WBP, ParentName)))
					{
						Parent->SetFlags(RF_Transactional);
						Parent->Modify();
						UPanelSlot* Slot = Parent->AddChild(Widget);
						if (!Slot)
						{
							Failure = FString::Printf(TEXT("Parent '%s' rejected '%s'."), *ParentName, *Name);
						}
						else
						{
							double IndexNumber = -1;
							if (Operation->TryGetNumberField(TEXT("index"), IndexNumber) && IndexNumber >= 0)
								Parent->ShiftChild(FMath::Clamp(static_cast<int32>(IndexNumber), 0, Parent->GetChildrenCount() - 1), Widget);
							BridgeUMGImpl::ApplyProperties(Slot,
								BridgeUMGImpl::GetOptionalObject(Operation, TEXT("slot_properties")), Failure);
						}
					}
					if (Failure.IsEmpty())
						BridgeUMGImpl::ApplyProperties(Widget,
							BridgeUMGImpl::GetOptionalObject(Operation, TEXT("properties")), Failure);
				}
				bStructural = true;
			}
			else if (Kind == TEXT("set"))
			{
				UWidget* Widget = BridgeUMGImpl::FindWidgetByName(WBP, Name);
				BridgeUMGImpl::ApplyProperties(Widget,
					BridgeUMGImpl::GetOptionalObject(Operation, TEXT("properties")), Failure);
				if (Failure.IsEmpty() && Operation->HasField(TEXT("slot_properties")))
					BridgeUMGImpl::ApplyProperties(Widget ? Widget->Slot : nullptr,
						BridgeUMGImpl::GetOptionalObject(Operation, TEXT("slot_properties")), Failure);
			}
			else if (Kind == TEXT("reorder"))
			{
				UWidget* Widget = BridgeUMGImpl::FindWidgetByName(WBP, Name);
				UPanelWidget* Parent = Widget ? Widget->GetParent() : nullptr;
				if (!Parent)
				{
					Failure = FString::Printf(TEXT("Widget '%s' has no panel parent."), *Name);
				}
				else
				{
					Parent->SetFlags(RF_Transactional);
					Parent->Modify();
					double IndexNumber = 0;
					Operation->TryGetNumberField(TEXT("index"), IndexNumber);
					Parent->ShiftChild(FMath::Clamp(static_cast<int32>(IndexNumber), 0, Parent->GetChildrenCount() - 1), Widget);
					bStructural = true;
				}
			}
			else if (Kind == TEXT("move"))
			{
				UWidget* Widget = BridgeUMGImpl::FindWidgetByName(WBP, Name);
				UPanelWidget* OldParent = Widget ? Widget->GetParent() : nullptr;
				UPanelWidget* NewParent = Cast<UPanelWidget>(BridgeUMGImpl::FindWidgetByName(
					WBP, Operation->GetStringField(TEXT("parent"))));
				if (!Widget || !OldParent || !NewParent)
				{
					Failure = FString::Printf(TEXT("Move '%s' could not resolve both parents."), *Name);
				}
				else if (OldParent == NewParent)
				{
					double IndexNumber = OldParent->GetChildIndex(Widget);
					Operation->TryGetNumberField(TEXT("index"), IndexNumber);
					OldParent->SetFlags(RF_Transactional);
					OldParent->Modify();
					OldParent->ShiftChild(FMath::Clamp(static_cast<int32>(IndexNumber), 0, OldParent->GetChildrenCount() - 1), Widget);
				}
				else
				{
					TMap<FName, FString> ExportedSlotProperties;
					FWidgetBlueprintEditorUtils::ExportPropertiesToText(Widget->Slot, ExportedSlotProperties);
					Widget->SetFlags(RF_Transactional);
					Widget->Modify();
					OldParent->SetFlags(RF_Transactional);
					NewParent->SetFlags(RF_Transactional);
					OldParent->Modify();
					NewParent->Modify();
					Widget->RemoveFromParent();
					double IndexNumber = -1;
					UPanelSlot* NewSlot = nullptr;
					if (Operation->TryGetNumberField(TEXT("index"), IndexNumber) && IndexNumber >= 0)
						NewSlot = NewParent->InsertChildAt(
							FMath::Clamp(static_cast<int32>(IndexNumber), 0, NewParent->GetChildrenCount()), Widget);
					else
						NewSlot = NewParent->AddChild(Widget);
					if (!NewSlot)
					{
						Failure = FString::Printf(TEXT("New parent rejected '%s'."), *Name);
					}
					else
					{
						FWidgetBlueprintEditorUtils::ImportPropertiesFromText(NewSlot, ExportedSlotProperties);
						BridgeUMGImpl::ApplyProperties(NewSlot,
							BridgeUMGImpl::GetOptionalObject(Operation, TEXT("slot_properties")), Failure);
					}
				}
				bStructural = true;
			}
			else if (Kind == TEXT("delete"))
			{
				UWidget* Widget = BridgeUMGImpl::FindWidgetByName(WBP, Name);
				if (!Widget)
				{
					Failure = FString::Printf(TEXT("Widget '%s' no longer exists."), *Name);
				}
				else
				{
					FWidgetBlueprintEditorUtils::DeleteWidgets(WBP, { Widget },
						FWidgetBlueprintEditorUtils::EDeleteWidgetWarningType::DeleteSilently);
					bStructural = true;
				}
			}

			if (!Failure.IsEmpty())
			{
				Result.Error = FString::Printf(TEXT("Operation %d (%s %s) failed: %s"),
					OperationIndex, *Kind, *Name, *Failure);
				bFailed = true;
				break;
			}
			++Result.AppliedOperations;
			Result.Messages.Add(FString::Printf(TEXT("%s %s"), *Kind, *Name));
		}

		if (!bFailed)
		{
			if (bStructural) FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
			else FBlueprintEditorUtils::MarkBlueprintAsModified(WBP);
			if (bCompileAfter)
			{
				FKismetEditorUtilities::CompileBlueprint(WBP);
				if (WBP->Status == BS_Error)
				{
					Result.Error = TEXT("Widget Blueprint compilation failed.");
					bFailed = true;
				}
			}
			if (!bFailed && bSaveAfter)
			{
				WBP->MarkPackageDirty();
				if (!UEditorLoadingAndSavingUtils::SavePackages({ WBP->GetOutermost() }, false))
				{
					Result.Error = TEXT("Widget Blueprint save failed.");
					bFailed = true;
				}
			}
		}
	}

	if (bFailed)
	{
		Result.bRolledBack = GEditor && GEditor->UndoTransaction();
		if (!Result.bRolledBack)
		{
			Result.Error += TEXT(" Automatic editor undo failed; inspect the asset before continuing.");
		}
		Result.AppliedOperations = 0;
		Result.Messages.Reset();
		return Result;
	}

	Result.bSuccess = true;
	return Result;
}

TArray<FBridgeWidgetInfo> UUnrealBridgeUMGLibrary::GetWidgetTree(const FString& WidgetBlueprintPath)
{
	TArray<FBridgeWidgetInfo> Result;
	if (UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath, TEXT("get_widget_tree")))
	{
		if (WBP->WidgetTree) BridgeUMGImpl::GatherWidgets(WBP->WidgetTree->RootWidget, FString(), Result);
	}
	return Result;
}

TArray<FBridgeWidgetPropertyValue> UUnrealBridgeUMGLibrary::GetWidgetProperties(
	const FString& WidgetBlueprintPath, const FString& WidgetName)
{
	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath, TEXT("get_widget_properties"));
	return BridgeUMGImpl::GatherDifferentProperties(BridgeUMGImpl::FindWidgetByName(WBP, WidgetName));
}

TArray<FBridgeWidgetPropertyValue> UUnrealBridgeUMGLibrary::GetWidgetSlotProperties(
	const FString& WidgetBlueprintPath, const FString& WidgetName)
{
	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath, TEXT("get_widget_slot_properties"));
	UWidget* Widget = BridgeUMGImpl::FindWidgetByName(WBP, WidgetName);
	return BridgeUMGImpl::GatherDifferentProperties(Widget ? Widget->Slot : nullptr);
}

TArray<FBridgeWidgetInfo> UUnrealBridgeUMGLibrary::SearchWidgets(
	const FString& WidgetBlueprintPath, const FString& Query)
{
	TArray<FBridgeWidgetInfo> Result;
	for (const FBridgeWidgetInfo& Info : GetWidgetTree(WidgetBlueprintPath))
	{
		if (Info.Name.Contains(Query, ESearchCase::IgnoreCase) || Info.WidgetClass.Contains(Query, ESearchCase::IgnoreCase))
		{
			Result.Add(Info);
		}
	}
	return Result;
}

bool UUnrealBridgeUMGLibrary::SetWidgetProperty(
	const FString& WidgetBlueprintPath, const FString& WidgetName,
	const FString& PropertyName, const FString& Value)
{
	using namespace BridgeUMGImpl;
	UWidgetBlueprint* WBP = LoadWBP(WidgetBlueprintPath, TEXT("set_widget_property"));
	UWidget* Widget = FindWidgetByName(WBP, WidgetName);
	FString Error;
	if (!Widget || !ImportPropertyValue(Widget, PropertyName, Value, true, Error))
	{
		LogFailure(TEXT("set_widget_property"), Widget ? Error : FString::Printf(TEXT("widget '%s' was not found"), *WidgetName));
		return false;
	}
	FinishTemplateWrite(WBP, false);
	return true;
}

bool UUnrealBridgeUMGLibrary::SetWidgetSlotProperty(
	const FString& WidgetBlueprintPath, const FString& WidgetName,
	const FString& PropertyName, const FString& Value)
{
	using namespace BridgeUMGImpl;
	UWidgetBlueprint* WBP = LoadWBP(WidgetBlueprintPath, TEXT("set_widget_slot_property"));
	UWidget* Widget = FindWidgetByName(WBP, WidgetName);
	FString Error;
	if (!Widget || !Widget->Slot || !ImportPropertyValue(Widget->Slot, PropertyName, Value, true, Error))
	{
		LogFailure(TEXT("set_widget_slot_property"), Widget && !Widget->Slot ? TEXT("widget has no panel slot") : Error);
		return false;
	}
	FinishTemplateWrite(WBP, false);
	return true;
}

bool UUnrealBridgeUMGLibrary::SetCanvasSlotLayout(
	const FString& WidgetBlueprintPath, const FString& WidgetName,
	FVector2D Position, FVector2D Size, FVector2D AnchorMinimum,
	FVector2D AnchorMaximum, FVector2D Alignment, bool bAutoSize, int32 ZOrder)
{
	using namespace BridgeUMGImpl;
	UWidgetBlueprint* WBP = LoadWBP(WidgetBlueprintPath, TEXT("set_canvas_slot_layout"));
	UWidget* Widget = FindWidgetByName(WBP, WidgetName);
	UCanvasPanelSlot* Slot = Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr;
	if (!Slot)
	{
		LogFailure(TEXT("set_canvas_slot_layout"), TEXT("widget is missing or is not in a CanvasPanelSlot"));
		return false;
	}
	Slot->Modify();
	Slot->SetAnchors(FAnchors(AnchorMinimum.X, AnchorMinimum.Y, AnchorMaximum.X, AnchorMaximum.Y));
	Slot->SetAlignment(Alignment);
	Slot->SetPosition(Position);
	Slot->SetSize(Size);
	Slot->SetAutoSize(bAutoSize);
	Slot->SetZOrder(ZOrder);
	Slot->PostEditChange();
	FinishTemplateWrite(WBP, false);
	return true;
}

bool UUnrealBridgeUMGLibrary::SetWidgetBrush(
	const FString& WidgetBlueprintPath, const FString& WidgetName,
	const FString& BrushPropertyPath, const FString& ResourcePath,
	FLinearColor Tint, const FString& DrawAs, FVector2D ImageSize, FMargin Margin)
{
	using namespace BridgeUMGImpl;
	UWidgetBlueprint* WBP = LoadWBP(WidgetBlueprintPath, TEXT("set_widget_brush"));
	UWidget* Widget = FindWidgetByName(WBP, WidgetName);
	FProperty* Property = nullptr;
	FProperty* RootProperty = nullptr;
	void* Value = nullptr;
	FString Error;
	if (!Widget || !ResolvePropertyPath(Widget, BrushPropertyPath, Property, Value, RootProperty, Error))
	{
		LogFailure(TEXT("set_widget_brush"), Widget ? Error : TEXT("widget was not found"));
		return false;
	}
	FStructProperty* StructProperty = CastField<FStructProperty>(Property);
	if (!StructProperty || StructProperty->Struct != FSlateBrush::StaticStruct())
	{
		LogFailure(TEXT("set_widget_brush"), FString::Printf(TEXT("'%s' is not an FSlateBrush property"), *BrushPropertyPath));
		return false;
	}
	UObject* Resource = nullptr;
	if (!ResourcePath.IsEmpty())
	{
		Resource = LoadObject<UObject>(nullptr, *ResourcePath);
		if (!Resource)
		{
			LogFailure(TEXT("set_widget_brush"), FString::Printf(TEXT("could not load resource '%s'"), *ResourcePath));
			return false;
		}
	}
	ESlateBrushDrawType::Type DrawType = ESlateBrushDrawType::Image;
	if (DrawAs.Equals(TEXT("NoDrawType"), ESearchCase::IgnoreCase)) DrawType = ESlateBrushDrawType::NoDrawType;
	else if (DrawAs.Equals(TEXT("Box"), ESearchCase::IgnoreCase)) DrawType = ESlateBrushDrawType::Box;
	else if (DrawAs.Equals(TEXT("Border"), ESearchCase::IgnoreCase)) DrawType = ESlateBrushDrawType::Border;
	else if (DrawAs.Equals(TEXT("RoundedBox"), ESearchCase::IgnoreCase)) DrawType = ESlateBrushDrawType::RoundedBox;
	else if (!DrawAs.IsEmpty() && !DrawAs.Equals(TEXT("Image"), ESearchCase::IgnoreCase))
	{
		LogFailure(TEXT("set_widget_brush"), FString::Printf(TEXT("unknown DrawAs '%s'"), *DrawAs));
		return false;
	}
	Widget->Modify();
	Widget->PreEditChange(RootProperty);
	FSlateBrush* Brush = static_cast<FSlateBrush*>(Value);
	Brush->SetResourceObject(Resource);
	Brush->TintColor = FSlateColor(Tint);
	Brush->DrawAs = DrawType;
	Brush->ImageSize = ImageSize;
	Brush->Margin = Margin;
	FPropertyChangedEvent Event(RootProperty, EPropertyChangeType::ValueSet);
	Widget->PostEditChangeProperty(Event);
	FinishTemplateWrite(WBP, false);
	return true;
}

FBridgeWidgetValidationReport UUnrealBridgeUMGLibrary::CompileAndValidateWidgetBlueprint(
	const FString& WidgetBlueprintPath, bool bSave, bool bCheckAccessibility)
{
	using namespace BridgeUMGImpl;
	FBridgeWidgetValidationReport Report;
	UWidgetBlueprint* WBP = LoadWBP(WidgetBlueprintPath, TEXT("compile_and_validate_widget_blueprint"));
	if (!WBP)
	{
		return Report;
	}
	Report.bFound = true;
	if (!WBP->WidgetTree || !WBP->WidgetTree->RootWidget)
	{
		AddIssue(Report, TEXT("error"), TEXT("UMG_NO_ROOT"), FString(), TEXT("Widget Blueprint has no root widget"));
	}

	TSet<FName> Names;
	if (WBP->WidgetTree)
	{
		TArray<UWidget*> Widgets;
		WBP->WidgetTree->GetAllWidgets(Widgets);
		for (UWidget* Widget : Widgets)
		{
			if (!Widget) continue;
			if (Names.Contains(Widget->GetFName()))
			{
				AddIssue(Report, TEXT("error"), TEXT("UMG_DUPLICATE_NAME"), Widget->GetName(), TEXT("Widget name is not unique"));
			}
			Names.Add(Widget->GetFName());
			if (Widget != WBP->WidgetTree->RootWidget && !Widget->Slot)
			{
				AddIssue(Report, TEXT("error"), TEXT("UMG_MISSING_SLOT"), Widget->GetName(), TEXT("Non-root widget has no panel slot"));
			}
			if (bCheckAccessibility && (Widget->IsA<UButton>() || Widget->IsA<UCheckBox>()
				|| Widget->IsA<USlider>() || Widget->IsA<UEditableText>() || Widget->IsA<UEditableTextBox>()))
			{
				FString Error;
				const FString Behavior = ExportPropertyValue(Widget, TEXT("AccessibleBehavior"), Error);
				if (Behavior.Contains(TEXT("NotAccessible"), ESearchCase::IgnoreCase))
				{
					AddIssue(Report, TEXT("warning"), TEXT("UMG_INTERACTIVE_NOT_ACCESSIBLE"), Widget->GetName(),
						TEXT("Interactive widget explicitly opts out of accessibility"));
				}
			}
		}
	}

	for (UWidgetAnimation* Animation : WBP->Animations)
	{
		if (!Animation || !Animation->GetMovieScene())
		{
			AddIssue(Report, TEXT("error"), TEXT("UMG_INVALID_ANIMATION"), FString(), TEXT("Animation has no MovieScene"));
			continue;
		}
		for (const FWidgetAnimationBinding& Binding : Animation->AnimationBindings)
		{
			if (!Binding.bIsRootWidget && WBP->WidgetTree && !WBP->WidgetTree->FindWidget(Binding.WidgetName))
			{
				AddIssue(Report, TEXT("error"), TEXT("UMG_ANIMATION_MISSING_WIDGET"), Binding.WidgetName.ToString(),
					FString::Printf(TEXT("Animation '%s' targets a missing widget"), *Animation->GetName()));
			}
			if (!Animation->GetMovieScene()->FindPossessable(Binding.AnimationGuid))
			{
				AddIssue(Report, TEXT("error"), TEXT("UMG_ANIMATION_MISSING_POSSESSABLE"), Binding.WidgetName.ToString(),
					FString::Printf(TEXT("Animation '%s' has a stale binding GUID"), *Animation->GetName()));
			}
		}
	}

	FKismetEditorUtilities::CompileBlueprint(WBP);
	Report.bCompiled = true;
	Report.CompileStatus = BlueprintStatusToString(WBP->Status);
	Report.bCompileSucceeded = WBP->Status == BS_UpToDate || WBP->Status == BS_UpToDateWithWarnings;
	if (!Report.bCompileSucceeded)
	{
		AddIssue(Report, TEXT("error"), TEXT("UMG_COMPILE_FAILED"), FString(),
			FString::Printf(TEXT("Widget Blueprint compile status is %s"), *Report.CompileStatus));
	}
#if !UE_VERSION_OLDER_THAN(5, 7, 0)
	if (UMVVMBlueprintView* View = GetMVVMView(WBP, false))
	{
		for (const FMVVMBlueprintViewBinding& Binding : View->GetBindings())
		{
			for (const FText& Error : View->GetBindingMessages(Binding.BindingId, UE::MVVM::EBindingMessageType::Error))
			{
				AddIssue(Report, TEXT("error"), TEXT("UMG_MVVM_BINDING_ERROR"), FString(), Error.ToString());
				Report.bCompileSucceeded = false;
			}
			for (const FText& Warning : View->GetBindingMessages(Binding.BindingId, UE::MVVM::EBindingMessageType::Warning))
			{
				AddIssue(Report, TEXT("warning"), TEXT("UMG_MVVM_BINDING_WARNING"), FString(), Warning.ToString());
			}
		}
	}
#endif
	if (bSave && Report.bCompileSucceeded)
	{
		Report.bSaved = UEditorLoadingAndSavingUtils::SavePackages({ WBP->GetOutermost() }, false);
		if (!Report.bSaved)
		{
			AddIssue(Report, TEXT("error"), TEXT("UMG_SAVE_FAILED"), FString(), TEXT("The compiled Widget Blueprint package could not be saved"));
		}
	}
	return Report;
}

// Animation authoring -------------------------------------------------------

TArray<FBridgeWidgetAnimationInfo> UUnrealBridgeUMGLibrary::GetWidgetAnimations(
	const FString& WidgetBlueprintPath)
{
	using namespace BridgeUMGImpl;
	TArray<FBridgeWidgetAnimationInfo> Result;
	UWidgetBlueprint* WBP = LoadWBP(WidgetBlueprintPath, TEXT("get_widget_animations"));
	if (!WBP) return Result;
	for (UWidgetAnimation* Animation : WBP->Animations)
	{
		if (!Animation) continue;
		FBridgeWidgetAnimationInfo Info;
		Info.Name = Animation->GetName();
#if WITH_EDITOR
		if (!Animation->GetDisplayLabel().IsEmpty()) Info.Name = Animation->GetDisplayLabel();
#endif
		Info.Duration = Animation->GetEndTime() - Animation->GetStartTime();
		UMovieScene* Scene = Animation->GetMovieScene();
		if (!Scene)
		{
			Result.Add(MoveTemp(Info));
			continue;
		}
		Info.DisplayRate = static_cast<float>(Scene->GetDisplayRate().AsDecimal());
		TMap<FGuid, FString> GuidToWidget;
		for (const FWidgetAnimationBinding& AnimationBinding : Animation->AnimationBindings)
		{
			GuidToWidget.Add(AnimationBinding.AnimationGuid, AnimationBinding.WidgetName.ToString());
		}
		const TArray<FMovieSceneBinding>& Bindings = const_cast<const UMovieScene*>(Scene)->GetBindings();
		for (const FMovieSceneBinding& Binding : Bindings)
		{
			const FString Target = GuidToWidget.FindRef(Binding.GetObjectGuid());
			for (UMovieSceneTrack* Track : Binding.GetTracks())
			{
				if (!Track) continue;
				FBridgeWidgetAnimTrack TrackInfo;
				TrackInfo.WidgetName = Target;
				TrackInfo.TrackType = Track->GetClass()->GetName();
				TrackInfo.DisplayName = Track->GetDisplayName().ToString();
				if (UMovieScenePropertyTrack* PropertyTrack = Cast<UMovieScenePropertyTrack>(Track))
				{
					TrackInfo.PropertyName = PropertyTrack->GetPropertyName().ToString();
				}
				for (UMovieSceneSection* Section : Track->GetAllSections())
				{
					if (!Section) continue;
					for (FMovieSceneFloatChannel* Channel : Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>())
					{
						if (Channel) TrackInfo.KeyCount += Channel->GetNumKeys();
					}
				}
				Info.Tracks.Add(MoveTemp(TrackInfo));
			}
		}
		Result.Add(MoveTemp(Info));
	}
	return Result;
}

bool UUnrealBridgeUMGLibrary::CreateWidgetAnimation(
	const FString& WidgetBlueprintPath, const FString& AnimationName,
	float DurationSeconds, int32 DisplayRate)
{
	using namespace BridgeUMGImpl;
	UWidgetBlueprint* WBP = LoadWBP(WidgetBlueprintPath, TEXT("create_widget_animation"));
	if (!WBP || AnimationName.IsEmpty() || DurationSeconds <= 0.f || DisplayRate <= 0 || FindAnimation(WBP, AnimationName))
	{
		LogFailure(TEXT("create_widget_animation"), TEXT("invalid name/range, or animation already exists"));
		return false;
	}
	FText NameError;
	if (!FName::IsValidXName(FName(*AnimationName), INVALID_OBJECTNAME_CHARACTERS, &NameError))
	{
		LogFailure(TEXT("create_widget_animation"), NameError.ToString());
		return false;
	}
	const FScopedTransaction Transaction(FText::FromString(TEXT("UnrealBridge Create Widget Animation")));
	WBP->Modify();
	UWidgetAnimation* Animation = NewObject<UWidgetAnimation>(WBP, FName(*AnimationName), RF_Transactional);
	Animation->SetDisplayLabel(AnimationName);
	Animation->MovieScene = NewObject<UMovieScene>(Animation, FName(*AnimationName), RF_Transactional);
	Animation->MovieScene->SetDisplayRate(FFrameRate(DisplayRate, 1));
	const FFrameNumber Start = SecondsToFrame(Animation->MovieScene, 0.f);
	const FFrameNumber End = SecondsToFrame(Animation->MovieScene, DurationSeconds);
	Animation->MovieScene->SetPlaybackRange(TRange<FFrameNumber>(Start, End + 1));
	Animation->MovieScene->GetEditorData().WorkStart = 0.f;
	Animation->MovieScene->GetEditorData().WorkEnd = DurationSeconds;
	WBP->Animations.Add(Animation);
	FinishTemplateWrite(WBP, true);
	return true;
}

bool UUnrealBridgeUMGLibrary::RemoveWidgetAnimation(
	const FString& WidgetBlueprintPath, const FString& AnimationName)
{
	using namespace BridgeUMGImpl;
	UWidgetBlueprint* WBP = LoadWBP(WidgetBlueprintPath, TEXT("remove_widget_animation"));
	UWidgetAnimation* Animation = FindAnimation(WBP, AnimationName);
	if (!Animation) return false;
	const FScopedTransaction Transaction(FText::FromString(TEXT("UnrealBridge Remove Widget Animation")));
	WBP->Modify();
	WBP->Animations.Remove(Animation);
	FinishTemplateWrite(WBP, true);
	return true;
}

bool UUnrealBridgeUMGLibrary::AddWidgetAnimationFloatKeys(
	const FString& WidgetBlueprintPath, const FString& AnimationName,
	const FString& WidgetName, const FString& PropertyName,
	const TArray<FBridgeWidgetFloatKey>& Keys, const FString& Interpolation)
{
	using namespace BridgeUMGImpl;
	UWidgetBlueprint* WBP = LoadWBP(WidgetBlueprintPath, TEXT("add_widget_animation_float_keys"));
	UWidgetAnimation* Animation = FindAnimation(WBP, AnimationName);
	if (!Animation || Keys.IsEmpty() || PropertyName.IsEmpty()) return false;
	FString Error;
	const FGuid Guid = FindOrAddAnimationBinding(WBP, Animation, WidgetName, Error);
	if (!Guid.IsValid())
	{
		LogFailure(TEXT("add_widget_animation_float_keys"), Error);
		return false;
	}
	const FScopedTransaction Transaction(FText::FromString(TEXT("UnrealBridge Add Widget Float Animation Keys")));
	Animation->Modify();
	UMovieScene* Scene = Animation->GetMovieScene();
	Scene->Modify();
	UMovieSceneFloatTrack* Track = Cast<UMovieSceneFloatTrack>(FindOrAddPropertyTrack(Scene, Guid, UMovieSceneFloatTrack::StaticClass(), PropertyName));
	UMovieSceneFloatSection* Section = Cast<UMovieSceneFloatSection>(FindOrAddSection(Track));
	if (!Section) return false;
	Section->Modify();
	for (const FBridgeWidgetFloatKey& Key : Keys)
	{
		AddFloatKey(Section->GetChannel(), SecondsToFrame(Scene, Key.Time), Key.Value, Interpolation);
	}
	FinishTemplateWrite(WBP, true);
	return true;
}

bool UUnrealBridgeUMGLibrary::AddWidgetAnimationColorKeys(
	const FString& WidgetBlueprintPath, const FString& AnimationName,
	const FString& WidgetName, const FString& PropertyName,
	const TArray<FBridgeWidgetColorKey>& Keys, const FString& Interpolation)
{
	using namespace BridgeUMGImpl;
	UWidgetBlueprint* WBP = LoadWBP(WidgetBlueprintPath, TEXT("add_widget_animation_color_keys"));
	UWidgetAnimation* Animation = FindAnimation(WBP, AnimationName);
	if (!Animation || Keys.IsEmpty() || PropertyName.IsEmpty()) return false;
	FString Error;
	const FGuid Guid = FindOrAddAnimationBinding(WBP, Animation, WidgetName, Error);
	if (!Guid.IsValid())
	{
		LogFailure(TEXT("add_widget_animation_color_keys"), Error);
		return false;
	}
	const FScopedTransaction Transaction(FText::FromString(TEXT("UnrealBridge Add Widget Color Animation Keys")));
	Animation->Modify();
	UMovieScene* Scene = Animation->GetMovieScene();
	Scene->Modify();
	UMovieSceneColorTrack* Track = Cast<UMovieSceneColorTrack>(FindOrAddPropertyTrack(Scene, Guid, UMovieSceneColorTrack::StaticClass(), PropertyName));
	UMovieSceneColorSection* Section = Cast<UMovieSceneColorSection>(FindOrAddSection(Track));
	if (!Section) return false;
	Section->Modify();
	TArrayView<FMovieSceneFloatChannel*> Channels = Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
	if (Channels.Num() < 4) return false;
	for (const FBridgeWidgetColorKey& Key : Keys)
	{
		const FFrameNumber Frame = SecondsToFrame(Scene, Key.Time);
		AddFloatKey(*Channels[0], Frame, Key.Value.R, Interpolation);
		AddFloatKey(*Channels[1], Frame, Key.Value.G, Interpolation);
		AddFloatKey(*Channels[2], Frame, Key.Value.B, Interpolation);
		AddFloatKey(*Channels[3], Frame, Key.Value.A, Interpolation);
	}
	FinishTemplateWrite(WBP, true);
	return true;
}

bool UUnrealBridgeUMGLibrary::AddWidgetAnimationTransformKeys(
	const FString& WidgetBlueprintPath, const FString& AnimationName,
	const FString& WidgetName, const TArray<FBridgeWidgetTransformKey>& Keys,
	const FString& Interpolation)
{
	using namespace BridgeUMGImpl;
	UWidgetBlueprint* WBP = LoadWBP(WidgetBlueprintPath, TEXT("add_widget_animation_transform_keys"));
	UWidgetAnimation* Animation = FindAnimation(WBP, AnimationName);
	if (!Animation || Keys.IsEmpty()) return false;
	FString Error;
	const FGuid Guid = FindOrAddAnimationBinding(WBP, Animation, WidgetName, Error);
	if (!Guid.IsValid())
	{
		LogFailure(TEXT("add_widget_animation_transform_keys"), Error);
		return false;
	}
	const FScopedTransaction Transaction(FText::FromString(TEXT("UnrealBridge Add Widget Transform Animation Keys")));
	Animation->Modify();
	UMovieScene* Scene = Animation->GetMovieScene();
	Scene->Modify();
	UMovieScene2DTransformTrack* Track = Cast<UMovieScene2DTransformTrack>(
		FindOrAddPropertyTrack(Scene, Guid, UMovieScene2DTransformTrack::StaticClass(), TEXT("RenderTransform")));
	UMovieScene2DTransformSection* Section = Cast<UMovieScene2DTransformSection>(FindOrAddSection(Track));
	if (!Section) return false;
	Section->Modify();
	Section->SetMask(FMovieScene2DTransformMask(EMovieScene2DTransformChannel::AllTransform));
	for (const FBridgeWidgetTransformKey& Key : Keys)
	{
		const FFrameNumber Frame = SecondsToFrame(Scene, Key.Time);
		AddFloatKey(Section->Translation[0], Frame, Key.Translation.X, Interpolation);
		AddFloatKey(Section->Translation[1], Frame, Key.Translation.Y, Interpolation);
		AddFloatKey(Section->Rotation, Frame, Key.Angle, Interpolation);
		AddFloatKey(Section->Scale[0], Frame, Key.Scale.X, Interpolation);
		AddFloatKey(Section->Scale[1], Frame, Key.Scale.Y, Interpolation);
		AddFloatKey(Section->Shear[0], Frame, Key.Shear.X, Interpolation);
		AddFloatKey(Section->Shear[1], Frame, Key.Shear.Y, Interpolation);
	}
	FinishTemplateWrite(WBP, true);
	return true;
}

int32 UUnrealBridgeUMGLibrary::RemoveWidgetAnimationTrack(
	const FString& WidgetBlueprintPath, const FString& AnimationName,
	const FString& WidgetName, const FString& PropertyName)
{
	using namespace BridgeUMGImpl;
	UWidgetBlueprint* WBP = LoadWBP(WidgetBlueprintPath, TEXT("remove_widget_animation_track"));
	UWidgetAnimation* Animation = FindAnimation(WBP, AnimationName);
	if (!Animation) return 0;
	FGuid Guid;
	for (const FWidgetAnimationBinding& Binding : Animation->AnimationBindings)
	{
		if (Binding.WidgetName.ToString().Equals(WidgetName, ESearchCase::IgnoreCase))
		{
			Guid = Binding.AnimationGuid;
			break;
		}
	}
	if (!Guid.IsValid()) return 0;
	UMovieScene* Scene = Animation->GetMovieScene();
	const FMovieSceneBinding* Binding = Scene->FindBinding(Guid);
	if (!Binding) return 0;
	TArray<UMovieSceneTrack*> ToRemove;
	for (UMovieSceneTrack* Track : Binding->GetTracks())
	{
		if (UMovieScenePropertyTrack* PropertyTrack = Cast<UMovieScenePropertyTrack>(Track))
		{
			if (PropertyTrack->GetPropertyName().ToString().Equals(PropertyName, ESearchCase::IgnoreCase)) ToRemove.Add(Track);
		}
	}
	for (UMovieSceneTrack* Track : ToRemove) Scene->RemoveTrack(*Track);
	if (!ToRemove.IsEmpty()) FinishTemplateWrite(WBP, true);
	return ToRemove.Num();
}

// Event and legacy binding introspection -----------------------------------

TArray<FBridgeWidgetBindingInfo> UUnrealBridgeUMGLibrary::GetWidgetBindings(
	const FString& WidgetBlueprintPath)
{
	TArray<FBridgeWidgetBindingInfo> Result;
	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath, TEXT("get_widget_bindings"));
	if (!WBP) return Result;
	for (const FDelegateEditorBinding& Binding : WBP->Bindings)
	{
		FBridgeWidgetBindingInfo Info;
		Info.WidgetName = Binding.ObjectName;
		Info.PropertyName = Binding.PropertyName.ToString();
		Info.FunctionName = Binding.FunctionName.IsNone() ? Binding.SourceProperty.ToString() : Binding.FunctionName.ToString();
		Info.Kind = Binding.Kind == EBindingKind::Function ? TEXT("Function") : TEXT("Property");
		Result.Add(MoveTemp(Info));
	}
	return Result;
}

TArray<FBridgeWidgetEventInfo> UUnrealBridgeUMGLibrary::GetWidgetEvents(
	const FString& WidgetBlueprintPath)
{
	TArray<FBridgeWidgetEventInfo> Result;
	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath, TEXT("get_widget_events"));
	if (!WBP) return Result;
	for (UEdGraph* Graph : WBP->UbergraphPages)
	{
		if (!Graph) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_ComponentBoundEvent* Event = Cast<UK2Node_ComponentBoundEvent>(Node))
			{
				FBridgeWidgetEventInfo Info;
				Info.WidgetName = Event->ComponentPropertyName.ToString();
				Info.EventName = Event->DelegatePropertyName.ToString();
				Info.HandlerName = Event->GetNodeTitle(ENodeTitleType::ListView).ToString();
				Result.Add(MoveTemp(Info));
			}
		}
	}
	return Result;
}

// MVVM authoring ------------------------------------------------------------

FBridgeWidgetOperationResult UUnrealBridgeUMGLibrary::CreateMVVMViewModelBlueprint(
	const FString& AssetPath, const FString& ParentClassPath)
{
	using namespace BridgeUMGImpl;
	FBridgeWidgetOperationResult Result;
#if UE_VERSION_OLDER_THAN(5, 7, 0)
	LogMVVMUnavailable(TEXT("create_mvvm_view_model_blueprint"));
	Result.Error = TEXT("MVVM authoring requires UE 5.7+");
	return Result;
#else
	FString PackagePath;
	FString AssetName;
	if (!SplitAssetPath(AssetPath, PackagePath, AssetName))
	{
		Result.Error = TEXT("invalid AssetPath; expected /Game/Folder/BP_ViewModel");
		return Result;
	}
	UClass* MVVMBase = ResolveClass(TEXT("/Script/ModelViewViewModel.MVVMViewModelBase"), UObject::StaticClass());
	UClass* ParentClass = ParentClassPath.IsEmpty() ? MVVMBase : ResolveClass(ParentClassPath, MVVMBase);
	if (!MVVMBase || !ParentClass)
	{
		Result.Error = FString::Printf(TEXT("could not resolve MVVM ViewModel parent '%s'"), *ParentClassPath);
		return Result;
	}
	const FString LongPackageName = PackagePath + TEXT("/") + AssetName;
	if (FindObject<UObject>(nullptr, *(LongPackageName + TEXT(".") + AssetName)) || FPackageName::DoesPackageExist(LongPackageName))
	{
		Result.Error = FString::Printf(TEXT("asset already exists at '%s'"), *LongPackageName);
		return Result;
	}
	UPackage* Package = CreatePackage(*LongPackageName);
	UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(
		ParentClass, Package, FName(*AssetName), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), FName(TEXT("UnrealBridge")));
	if (!BP)
	{
		Result.Error = TEXT("FKismetEditorUtilities::CreateBlueprint returned null");
		return Result;
	}
	FAssetRegistryModule::AssetCreated(BP);
	BP->MarkPackageDirty();
	Result.bSuccess = true;
	Result.Path = BP->GetPathName();
	Result.Name = BP->GetName();
	return Result;
#endif
}

bool UUnrealBridgeUMGLibrary::SetViewModelFieldNotify(
	const FString& ViewModelBlueprintPath, const FString& VariableName, bool bEnabled)
{
	using namespace BridgeUMGImpl;
#if UE_VERSION_OLDER_THAN(5, 7, 0)
	LogMVVMUnavailable(TEXT("set_view_model_field_notify"));
	return false;
#else
	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *ViewModelBlueprintPath);
	if (!BP)
	{
		LogFailure(TEXT("set_view_model_field_notify"), FString::Printf(TEXT("could not load Blueprint '%s'"), *ViewModelBlueprintPath));
		return false;
	}
	const FName VarName(*VariableName);
	if (FBlueprintEditorUtils::FindNewVariableIndex(BP, VarName) == INDEX_NONE)
	{
		LogFailure(TEXT("set_view_model_field_notify"), FString::Printf(TEXT("variable '%s' was not found"), *VariableName));
		return false;
	}
	BP->Modify();
	if (bEnabled)
	{
		FBlueprintEditorUtils::SetBlueprintVariableMetaData(BP, VarName, nullptr, FName(TEXT("FieldNotify")), FString());
	}
	else
	{
		FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(BP, VarName, nullptr, FName(TEXT("FieldNotify")));
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	BP->MarkPackageDirty();
	return true;
#endif
}

FString UUnrealBridgeUMGLibrary::AddMVVMViewModel(
	const FString& WidgetBlueprintPath, const FString& ViewModelName,
	const FString& ViewModelClassPath, const FString& CreationType,
	const FString& CreationData,
	bool bOptional, bool bCreateGetter, bool bCreateSetter)
{
	using namespace BridgeUMGImpl;
#if UE_VERSION_OLDER_THAN(5, 7, 0)
	LogMVVMUnavailable(TEXT("add_mvvm_view_model"));
	return FString();
#else
	UWidgetBlueprint* WBP = LoadWBP(WidgetBlueprintPath, TEXT("add_mvvm_view_model"));
	if (!WBP || ViewModelName.IsEmpty()) return FString();
	UMVVMBlueprintView* View = GetMVVMView(WBP, true);
	if (!View || View->FindViewModel(FName(*ViewModelName)))
	{
		LogFailure(TEXT("add_mvvm_view_model"), TEXT("MVVM view could not be created or ViewModelName is already in use"));
		return FString();
	}
	UClass* ViewModelClass = ResolveClass(ViewModelClassPath, UObject::StaticClass());
	if (!ViewModelClass || !ViewModelClass->ImplementsInterface(UNotifyFieldValueChanged::StaticClass()))
	{
		LogFailure(TEXT("add_mvvm_view_model"), FString::Printf(TEXT("'%s' does not implement NotifyFieldValueChanged"), *ViewModelClassPath));
		return FString();
	}
	EMVVMBlueprintViewModelContextCreationType ParsedType;
	if (!ParseCreationType(CreationType, ParsedType))
	{
		LogFailure(TEXT("add_mvvm_view_model"), FString::Printf(TEXT("unknown CreationType '%s'"), *CreationType));
		return FString();
	}
	FMVVMBlueprintViewModelContext Context(ViewModelClass, FName(*ViewModelName));
	if (!Context.IsValid())
	{
		LogFailure(TEXT("add_mvvm_view_model"), TEXT("engine rejected the ViewModel context/class"));
		return FString();
	}
	Context.CreationType = ParsedType;
	Context.bOptional = bOptional || ParsedType == EMVVMBlueprintViewModelContextCreationType::Manual;
	Context.bCreateGetterFunction = bCreateGetter;
	Context.bCreateSetterFunction = bCreateSetter || ParsedType == EMVVMBlueprintViewModelContextCreationType::Manual;
	if (ParsedType == EMVVMBlueprintViewModelContextCreationType::GlobalViewModelCollection)
	{
		Context.GlobalViewModelIdentifier = FName(*CreationData);
	}
	else if (ParsedType == EMVVMBlueprintViewModelContextCreationType::PropertyPath)
	{
		Context.ViewModelPropertyPath = CreationData;
	}
	else if (ParsedType == EMVVMBlueprintViewModelContextCreationType::Resolver)
	{
		Context.Resolver = Context.CreateDefaultResolver(WBP->GetPackage());
		if (!Context.Resolver)
		{
			LogFailure(TEXT("add_mvvm_view_model"), TEXT("no compatible default resolver is configured for this ViewModel class"));
			return FString();
		}
	}
	View->Modify();
	View->AddViewModel(Context);
	View->OnViewModelsUpdated.Broadcast();
	FinishTemplateWrite(WBP, true);
	return Context.GetViewModelId().ToString(EGuidFormats::DigitsWithHyphens);
#endif
}

bool UUnrealBridgeUMGLibrary::RemoveMVVMViewModel(
	const FString& WidgetBlueprintPath, const FString& ViewModelName)
{
	using namespace BridgeUMGImpl;
#if UE_VERSION_OLDER_THAN(5, 7, 0)
	LogMVVMUnavailable(TEXT("remove_mvvm_view_model"));
	return false;
#else
	UWidgetBlueprint* WBP = LoadWBP(WidgetBlueprintPath, TEXT("remove_mvvm_view_model"));
	UMVVMBlueprintView* View = GetMVVMView(WBP, false);
	const FMVVMBlueprintViewModelContext* Context = View ? View->FindViewModel(FName(*ViewModelName)) : nullptr;
	if (!Context) return false;
	const FGuid Id = Context->GetViewModelId();
	View->Modify();
	const bool bRemoved = View->RemoveViewModel(Id);
	if (bRemoved)
	{
		View->OnViewModelsUpdated.Broadcast();
		FinishTemplateWrite(WBP, true);
	}
	return bRemoved;
#endif
}

TArray<FBridgeMVVMViewModelInfo> UUnrealBridgeUMGLibrary::GetMVVMViewModels(const FString& WidgetBlueprintPath)
{
	using namespace BridgeUMGImpl;
	TArray<FBridgeMVVMViewModelInfo> Result;
#if UE_VERSION_OLDER_THAN(5, 7, 0)
	LogMVVMUnavailable(TEXT("get_mvvm_view_models"));
#else
	UWidgetBlueprint* WBP = LoadWBP(WidgetBlueprintPath, TEXT("get_mvvm_view_models"));
	UMVVMBlueprintView* View = GetMVVMView(WBP, false);
	if (!View) return Result;
	for (const FMVVMBlueprintViewModelContext& Context : View->GetViewModels())
	{
		FBridgeMVVMViewModelInfo Info;
		Info.Id = Context.GetViewModelId().ToString(EGuidFormats::DigitsWithHyphens);
		Info.Name = Context.GetViewModelName().ToString();
		Info.ClassPath = Context.GetViewModelClass() ? Context.GetViewModelClass()->GetPathName() : FString();
		Info.CreationType = CreationTypeToString(Context.CreationType);
		Info.bOptional = Context.bOptional;
		Info.bCreateGetter = Context.bCreateGetterFunction;
		Info.bCreateSetter = Context.bCreateSetterFunction;
		Result.Add(MoveTemp(Info));
	}
#endif
	return Result;
}

FString UUnrealBridgeUMGLibrary::AddMVVMBinding(
	const FString& WidgetBlueprintPath, const FString& ViewModelName,
	const FString& SourceFieldPath, const FString& DestinationWidgetName,
	const FString& DestinationFieldPath, const FString& Mode)
{
	using namespace BridgeUMGImpl;
#if UE_VERSION_OLDER_THAN(5, 7, 0)
	LogMVVMUnavailable(TEXT("add_mvvm_binding"));
	return FString();
#else
	UWidgetBlueprint* WBP = LoadWBP(WidgetBlueprintPath, TEXT("add_mvvm_binding"));
	UMVVMBlueprintView* View = GetMVVMView(WBP, false);
	const FMVVMBlueprintViewModelContext* Context = View ? View->FindViewModel(FName(*ViewModelName)) : nullptr;
	if (!Context || !Context->GetViewModelClass())
	{
		LogFailure(TEXT("add_mvvm_binding"), FString::Printf(TEXT("ViewModel '%s' was not found"), *ViewModelName));
		return FString();
	}
	EMVVMBindingMode ParsedMode;
	if (!ParseBindingMode(Mode, ParsedMode))
	{
		LogFailure(TEXT("add_mvvm_binding"), FString::Printf(TEXT("unknown binding mode '%s'"), *Mode));
		return FString();
	}
	FMVVMBlueprintPropertyPath SourcePath;
	SourcePath.SetViewModelId(Context->GetViewModelId());
	FString Error;
	if (!AppendMVVMPropertyPath(WBP, Context->GetViewModelClass(), SourceFieldPath, SourcePath, Error))
	{
		LogFailure(TEXT("add_mvvm_binding"), TEXT("source: ") + Error);
		return FString();
	}

	FMVVMBlueprintPropertyPath DestinationPath;
	UStruct* DestinationRoot = nullptr;
	if (DestinationWidgetName.IsEmpty() || DestinationWidgetName.Equals(TEXT("self"), ESearchCase::IgnoreCase))
	{
		DestinationPath.SetSelfContext();
		DestinationRoot = WBP->SkeletonGeneratedClass ? WBP->SkeletonGeneratedClass : WBP->GeneratedClass;
	}
	else
	{
		UWidget* DestinationWidget = FindWidgetByName(WBP, DestinationWidgetName);
		if (!DestinationWidget)
		{
			LogFailure(TEXT("add_mvvm_binding"), FString::Printf(TEXT("destination widget '%s' was not found"), *DestinationWidgetName));
			return FString();
		}
		DestinationPath.SetWidgetName(DestinationWidget->GetFName());
		DestinationRoot = DestinationWidget->GetClass();
	}
	if (!AppendMVVMPropertyPath(WBP, DestinationRoot, DestinationFieldPath, DestinationPath, Error))
	{
		LogFailure(TEXT("add_mvvm_binding"), TEXT("destination: ") + Error);
		return FString();
	}

	View->Modify();
	FMVVMBlueprintViewBinding& Binding = View->AddDefaultBinding();
	Binding.SourcePath = MoveTemp(SourcePath);
	Binding.DestinationPath = MoveTemp(DestinationPath);
	Binding.BindingType = ParsedMode;
	Binding.bEnabled = true;
	Binding.bCompile = true;
	View->OnBindingsUpdated.Broadcast();
	FinishTemplateWrite(WBP, true);
	return Binding.BindingId.ToString(EGuidFormats::DigitsWithHyphens);
#endif
}

bool UUnrealBridgeUMGLibrary::RemoveMVVMBinding(
	const FString& WidgetBlueprintPath, const FString& BindingId)
{
	using namespace BridgeUMGImpl;
#if UE_VERSION_OLDER_THAN(5, 7, 0)
	LogMVVMUnavailable(TEXT("remove_mvvm_binding"));
	return false;
#else
	FGuid Id;
	if (!FGuid::Parse(BindingId, Id)) return false;
	UWidgetBlueprint* WBP = LoadWBP(WidgetBlueprintPath, TEXT("remove_mvvm_binding"));
	UMVVMBlueprintView* View = GetMVVMView(WBP, false);
	FMVVMBlueprintViewBinding* Binding = View ? View->GetBinding(Id) : nullptr;
	if (!Binding) return false;
	View->Modify();
	View->RemoveBinding(Binding);
	View->OnBindingsUpdated.Broadcast();
	FinishTemplateWrite(WBP, true);
	return true;
#endif
}

TArray<FBridgeMVVMBindingInfo> UUnrealBridgeUMGLibrary::GetMVVMBindings(const FString& WidgetBlueprintPath)
{
	using namespace BridgeUMGImpl;
	TArray<FBridgeMVVMBindingInfo> Result;
#if UE_VERSION_OLDER_THAN(5, 7, 0)
	LogMVVMUnavailable(TEXT("get_mvvm_bindings"));
#else
	UWidgetBlueprint* WBP = LoadWBP(WidgetBlueprintPath, TEXT("get_mvvm_bindings"));
	UMVVMBlueprintView* View = GetMVVMView(WBP, false);
	if (!View) return Result;
	for (const FMVVMBlueprintViewBinding& Binding : View->GetBindings())
	{
		FBridgeMVVMBindingInfo Info;
		Info.Id = Binding.BindingId.ToString(EGuidFormats::DigitsWithHyphens);
		Info.SourcePath = Binding.SourcePath.ToString(WBP, false, false);
		Info.DestinationPath = Binding.DestinationPath.ToString(WBP, false, false);
		Info.Mode = BindingModeToString(Binding.BindingType);
		Info.bEnabled = Binding.bEnabled;
		Info.bCompile = Binding.bCompile;
		for (const FText& Message : View->GetBindingMessages(Binding.BindingId, UE::MVVM::EBindingMessageType::Error))
		{
			Info.Errors.Add(Message.ToString());
		}
		for (const FText& Message : View->GetBindingMessages(Binding.BindingId, UE::MVVM::EBindingMessageType::Warning))
		{
			Info.Warnings.Add(Message.ToString());
		}
		Result.Add(MoveTemp(Info));
	}
#endif
	return Result;
}

bool UUnrealBridgeUMGLibrary::SetMVVMViewSettings(
	const FString& WidgetBlueprintPath, bool bInitializeSourcesOnConstruct,
	bool bInitializeBindingsOnConstruct, bool bInitializeEventsOnConstruct,
	bool bCreateViewWithoutBindings)
{
	using namespace BridgeUMGImpl;
#if UE_VERSION_OLDER_THAN(5, 7, 0)
	LogMVVMUnavailable(TEXT("set_mvvm_view_settings"));
	return false;
#else
	UWidgetBlueprint* WBP = LoadWBP(WidgetBlueprintPath, TEXT("set_mvvm_view_settings"));
	UMVVMBlueprintView* View = GetMVVMView(WBP, true);
	UMVVMBlueprintViewSettings* Settings = View ? View->GetSettings() : nullptr;
	if (!Settings) return false;
	Settings->Modify();
	Settings->bInitializeSourcesOnConstruct = bInitializeSourcesOnConstruct;
	Settings->bInitializeBindingsOnConstruct = bInitializeBindingsOnConstruct;
	Settings->bInitializeEventsOnConstruct = bInitializeEventsOnConstruct;
	Settings->bCreateViewWithoutBindings = bCreateViewWithoutBindings;
	FinishTemplateWrite(WBP, true);
	return true;
#endif
}

// Live validation -----------------------------------------------------------

FString UUnrealBridgeUMGLibrary::SpawnWidgetInstance(const FString& WidgetBlueprintPath, int32 ZOrder)
{
	using namespace BridgeUMGImpl;
	UWidgetBlueprint* WBP = LoadWBP(WidgetBlueprintPath, TEXT("spawn_widget_instance"));
	if (!WBP || !GEditor || !GEditor->PlayWorld)
	{
		LogFailure(TEXT("spawn_widget_instance"), TEXT("a running PIE world is required"));
		return FString();
	}
	if (!WBP->GeneratedClass || WBP->Status == BS_Dirty || WBP->Status == BS_Error)
	{
		FKismetEditorUtilities::CompileBlueprint(WBP);
	}
	UClass* WidgetClass = WBP->GeneratedClass;
	if (!WidgetClass || !WidgetClass->IsChildOf(UUserWidget::StaticClass()))
	{
		LogFailure(TEXT("spawn_widget_instance"), TEXT("Widget Blueprint has no compiled UserWidget class"));
		return FString();
	}
	UUserWidget* Instance = CreateWidget<UUserWidget>(GEditor->PlayWorld, WidgetClass);
	if (!Instance)
	{
		LogFailure(TEXT("spawn_widget_instance"), TEXT("CreateWidget returned null"));
		return FString();
	}
	Instance->AddToViewport(ZOrder);
	const FString Handle = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	LiveInstances.Add(Handle, Instance);
	return Handle;
}

bool UUnrealBridgeUMGLibrary::RemoveWidgetInstance(const FString& InstanceHandle)
{
	using namespace BridgeUMGImpl;
	UUserWidget* Instance = ResolveLiveInstance(InstanceHandle, TEXT("remove_widget_instance"));
	if (!Instance) return false;
	Instance->StopAllAnimations();
	Instance->RemoveFromParent();
	LiveInstances.Remove(InstanceHandle);
	return true;
}

int32 UUnrealBridgeUMGLibrary::RemoveAllWidgetInstances()
{
	using namespace BridgeUMGImpl;
	int32 Removed = 0;
	for (TPair<FString, TWeakObjectPtr<UUserWidget>>& Entry : LiveInstances)
	{
		if (UUserWidget* Instance = Entry.Value.Get())
		{
			Instance->StopAllAnimations();
			Instance->RemoveFromParent();
			++Removed;
		}
	}
	LiveInstances.Reset();
	return Removed;
}

TArray<FBridgeLiveWidgetInfo> UUnrealBridgeUMGLibrary::GetLiveWidgetTree(const FString& InstanceHandle)
{
	using namespace BridgeUMGImpl;
	TArray<FBridgeLiveWidgetInfo> Result;
	UUserWidget* Instance = ResolveLiveInstance(InstanceHandle, TEXT("get_live_widget_tree"));
	if (Instance && Instance->WidgetTree)
	{
		GatherLiveWidgets(Instance->WidgetTree->RootWidget, FString(), Result);
	}
	return Result;
}

FString UUnrealBridgeUMGLibrary::GetLiveWidgetProperty(
	const FString& InstanceHandle, const FString& WidgetName, const FString& PropertyPath)
{
	using namespace BridgeUMGImpl;
	UUserWidget* Instance = ResolveLiveInstance(InstanceHandle, TEXT("get_live_widget_property"));
	UWidget* Widget = ResolveLiveWidget(Instance, WidgetName);
	FString Error;
	const FString Result = ExportPropertyValue(Widget, PropertyPath, Error);
	if (!Error.IsEmpty()) LogFailure(TEXT("get_live_widget_property"), Error);
	return Result;
}

bool UUnrealBridgeUMGLibrary::SetLiveWidgetProperty(
	const FString& InstanceHandle, const FString& WidgetName,
	const FString& PropertyPath, const FString& Value)
{
	using namespace BridgeUMGImpl;
	UUserWidget* Instance = ResolveLiveInstance(InstanceHandle, TEXT("set_live_widget_property"));
	UWidget* Widget = ResolveLiveWidget(Instance, WidgetName);
	FString Error;
	if (!Widget || !ImportPropertyValue(Widget, PropertyPath, Value, false, Error))
	{
		LogFailure(TEXT("set_live_widget_property"), Widget ? Error : TEXT("live widget was not found"));
		return false;
	}
	Widget->SynchronizeProperties();
	return true;
}

bool UUnrealBridgeUMGLibrary::ClickLiveButton(const FString& InstanceHandle, const FString& WidgetName)
{
	using namespace BridgeUMGImpl;
	UUserWidget* Instance = ResolveLiveInstance(InstanceHandle, TEXT("click_live_button"));
	UButton* Button = Cast<UButton>(ResolveLiveWidget(Instance, WidgetName));
	if (!Button)
	{
		LogFailure(TEXT("click_live_button"), TEXT("live widget is missing or is not a Button"));
		return false;
	}
	Button->OnClicked.Broadcast();
	return true;
}

bool UUnrealBridgeUMGLibrary::SetLiveWidgetText(
	const FString& InstanceHandle, const FString& WidgetName, const FString& Text)
{
	using namespace BridgeUMGImpl;
	UUserWidget* Instance = ResolveLiveInstance(InstanceHandle, TEXT("set_live_widget_text"));
	UWidget* Widget = ResolveLiveWidget(Instance, WidgetName);
	const FText NewText = FText::FromString(Text);
	if (UTextBlock* TextBlock = Cast<UTextBlock>(Widget)) TextBlock->SetText(NewText);
	else if (UEditableText* Editable = Cast<UEditableText>(Widget)) Editable->SetText(NewText);
	else if (UEditableTextBox* EditableBox = Cast<UEditableTextBox>(Widget)) EditableBox->SetText(NewText);
	else
	{
		LogFailure(TEXT("set_live_widget_text"), TEXT("live widget is not TextBlock, EditableText, or EditableTextBox"));
		return false;
	}
	return true;
}

bool UUnrealBridgeUMGLibrary::SetLiveWidgetValue(
	const FString& InstanceHandle, const FString& WidgetName, float Value)
{
	using namespace BridgeUMGImpl;
	UUserWidget* Instance = ResolveLiveInstance(InstanceHandle, TEXT("set_live_widget_value"));
	UWidget* Widget = ResolveLiveWidget(Instance, WidgetName);
	if (USlider* Slider = Cast<USlider>(Widget)) Slider->SetValue(Value);
	else if (UProgressBar* Progress = Cast<UProgressBar>(Widget)) Progress->SetPercent(Value);
	else
	{
		LogFailure(TEXT("set_live_widget_value"), TEXT("live widget is not Slider or ProgressBar"));
		return false;
	}
	return true;
}

bool UUnrealBridgeUMGLibrary::SetLiveWidgetChecked(
	const FString& InstanceHandle, const FString& WidgetName, bool bChecked)
{
	using namespace BridgeUMGImpl;
	UUserWidget* Instance = ResolveLiveInstance(InstanceHandle, TEXT("set_live_widget_checked"));
	UCheckBox* CheckBox = Cast<UCheckBox>(ResolveLiveWidget(Instance, WidgetName));
	if (!CheckBox)
	{
		LogFailure(TEXT("set_live_widget_checked"), TEXT("live widget is missing or is not a CheckBox"));
		return false;
	}
	CheckBox->SetIsChecked(bChecked);
	return true;
}

bool UUnrealBridgeUMGLibrary::FocusLiveWidget(const FString& InstanceHandle, const FString& WidgetName)
{
	using namespace BridgeUMGImpl;
	UUserWidget* Instance = ResolveLiveInstance(InstanceHandle, TEXT("focus_live_widget"));
	UWidget* Widget = ResolveLiveWidget(Instance, WidgetName);
	if (!Widget) return false;
	Widget->SetKeyboardFocus();
	return Widget->HasKeyboardFocus();
}

bool UUnrealBridgeUMGLibrary::PlayLiveWidgetAnimation(
	const FString& InstanceHandle, const FString& AnimationName,
	float StartTime, int32 NumLoops, const FString& PlayMode, float PlaybackSpeed)
{
	using namespace BridgeUMGImpl;
	UUserWidget* Instance = ResolveLiveInstance(InstanceHandle, TEXT("play_live_widget_animation"));
	UWidgetAnimation* Animation = FindLiveAnimation(Instance, AnimationName);
	if (!Animation)
	{
		LogFailure(TEXT("play_live_widget_animation"), FString::Printf(TEXT("animation '%s' was not found"), *AnimationName));
		return false;
	}
	EUMGSequencePlayMode::Type Mode = EUMGSequencePlayMode::Forward;
	if (PlayMode.Equals(TEXT("Reverse"), ESearchCase::IgnoreCase)) Mode = EUMGSequencePlayMode::Reverse;
	else if (PlayMode.Equals(TEXT("PingPong"), ESearchCase::IgnoreCase)) Mode = EUMGSequencePlayMode::PingPong;
	else if (!PlayMode.IsEmpty() && !PlayMode.Equals(TEXT("Forward"), ESearchCase::IgnoreCase))
	{
		LogFailure(TEXT("play_live_widget_animation"), FString::Printf(TEXT("unknown PlayMode '%s'"), *PlayMode));
		return false;
	}
	Instance->PlayAnimation(Animation, FMath::Max(0.f, StartTime), FMath::Max(0, NumLoops), Mode,
		FMath::Max(KINDA_SMALL_NUMBER, PlaybackSpeed), true);
	return true;
}

bool UUnrealBridgeUMGLibrary::StopLiveWidgetAnimation(
	const FString& InstanceHandle, const FString& AnimationName)
{
	using namespace BridgeUMGImpl;
	UUserWidget* Instance = ResolveLiveInstance(InstanceHandle, TEXT("stop_live_widget_animation"));
	UWidgetAnimation* Animation = FindLiveAnimation(Instance, AnimationName);
	if (!Animation) return false;
	Instance->StopAnimation(Animation);
	return true;
}

bool UUnrealBridgeUMGLibrary::SetLiveWidgetMaterialScalar(
	const FString& InstanceHandle, const FString& WidgetName,
	const FString& ParameterName, float Value)
{
	using namespace BridgeUMGImpl;
	UUserWidget* Instance = ResolveLiveInstance(InstanceHandle, TEXT("set_live_widget_material_scalar"));
	UWidget* Widget = ResolveLiveWidget(Instance, WidgetName);
	UMaterialInstanceDynamic* MID = nullptr;
	if (UImage* Image = Cast<UImage>(Widget)) MID = Image->GetDynamicMaterial();
	else if (UBorder* Border = Cast<UBorder>(Widget)) MID = Border->GetDynamicMaterial();
	if (!MID)
	{
		LogFailure(TEXT("set_live_widget_material_scalar"), TEXT("widget is not an Image/Border with a material brush"));
		return false;
	}
	MID->SetScalarParameterValue(FName(*ParameterName), Value);
	return true;
}

bool UUnrealBridgeUMGLibrary::SetLiveWidgetMaterialVector(
	const FString& InstanceHandle, const FString& WidgetName,
	const FString& ParameterName, FLinearColor Value)
{
	using namespace BridgeUMGImpl;
	UUserWidget* Instance = ResolveLiveInstance(InstanceHandle, TEXT("set_live_widget_material_vector"));
	UWidget* Widget = ResolveLiveWidget(Instance, WidgetName);
	UMaterialInstanceDynamic* MID = nullptr;
	if (UImage* Image = Cast<UImage>(Widget)) MID = Image->GetDynamicMaterial();
	else if (UBorder* Border = Cast<UBorder>(Widget)) MID = Border->GetDynamicMaterial();
	if (!MID)
	{
		LogFailure(TEXT("set_live_widget_material_vector"), TEXT("widget is not an Image/Border with a material brush"));
		return false;
	}
	MID->SetVectorParameterValue(FName(*ParameterName), Value);
	return true;
}

FString UUnrealBridgeUMGLibrary::GetLiveViewModelProperty(
	const FString& InstanceHandle, const FString& ViewModelName, const FString& PropertyPath)
{
	using namespace BridgeUMGImpl;
#if UE_VERSION_OLDER_THAN(5, 7, 0)
	LogMVVMUnavailable(TEXT("get_live_view_model_property"));
	return FString();
#else
	UUserWidget* Instance = ResolveLiveInstance(InstanceHandle, TEXT("get_live_view_model_property"));
	INotifyFieldValueChanged* Notify = nullptr;
	UObject* ViewModel = ResolveLiveViewModel(Instance, ViewModelName, Notify);
	FString Error;
	const FString Result = ExportPropertyValue(ViewModel, PropertyPath, Error);
	if (!Error.IsEmpty()) LogFailure(TEXT("get_live_view_model_property"), Error);
	return Result;
#endif
}

bool UUnrealBridgeUMGLibrary::SetLiveViewModelProperty(
	const FString& InstanceHandle, const FString& ViewModelName,
	const FString& PropertyPath, const FString& Value)
{
	using namespace BridgeUMGImpl;
#if UE_VERSION_OLDER_THAN(5, 7, 0)
	LogMVVMUnavailable(TEXT("set_live_view_model_property"));
	return false;
#else
	UUserWidget* Instance = ResolveLiveInstance(InstanceHandle, TEXT("set_live_view_model_property"));
	INotifyFieldValueChanged* Notify = nullptr;
	UObject* ViewModel = ResolveLiveViewModel(Instance, ViewModelName, Notify);
	FString Error;
	FProperty* RootProperty = nullptr;
	if (!ViewModel || !Notify || !ImportPropertyValue(ViewModel, PropertyPath, Value, false, Error, &RootProperty))
	{
		LogFailure(TEXT("set_live_view_model_property"), ViewModel ? Error : TEXT("live ViewModel was not found"));
		return false;
	}
	const UE::FieldNotification::FFieldId FieldId = Notify->GetFieldNotificationDescriptor().GetField(
		ViewModel->GetClass(), RootProperty->GetFName());
	if (FieldId.IsValid())
	{
		Notify->BroadcastFieldValueChanged(FieldId);
	}
	if (UMVVMView* View = UMVVMSubsystem::GetViewFromUserWidget(Instance))
	{
		View->ExecuteViewModelBindings(FName(*ViewModelName));
	}
	return true;
#endif
}
