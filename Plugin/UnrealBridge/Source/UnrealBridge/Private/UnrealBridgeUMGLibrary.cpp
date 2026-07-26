#include "UnrealBridgeUMGLibrary.h"
#include "WidgetBlueprint.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Animation/WidgetAnimation.h"
#include "MovieScene.h"
#include "MovieSceneTrack.h"
#include "MovieScenePossessable.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "K2Node_ComponentBoundEvent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "ImageUtils.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/ScopedSlowTask.h"
#include "PixelFormat.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "RenderDeferredCleanup.h"
#include "RenderingThread.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "ShaderCompiler.h"
#include "Slate/WidgetRenderer.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprintEditorUtils.h"

// ─── Helpers ────────────────────────────────────────────────

namespace BridgeUMGImpl
{
	TArray<TWeakObjectPtr<UUserWidget>> PIEPreviewWidgets;

	UWidgetBlueprint* LoadWBP(const FString& Path)
	{
		UWidgetBlueprint* WBP = LoadObject<UWidgetBlueprint>(nullptr, *Path);
		if (!WBP)
		{
			UE_LOG(LogTemp, Warning, TEXT("UnrealBridge: Could not load Widget Blueprint '%s'"), *Path);
		}
		return WBP;
	}

	FString VisibilityToString(ESlateVisibility V)
	{
		switch (V)
		{
		case ESlateVisibility::Visible:				return TEXT("Visible");
		case ESlateVisibility::Collapsed:			return TEXT("Collapsed");
		case ESlateVisibility::Hidden:				return TEXT("Hidden");
		case ESlateVisibility::HitTestInvisible:	return TEXT("HitTestInvisible");
		case ESlateVisibility::SelfHitTestInvisible:return TEXT("SelfHitTestInvisible");
		default:									return TEXT("Unknown");
		}
	}

	FString PropertyTypeToString(const FProperty* Prop)
	{
		if (!Prop) return TEXT("Unknown");
		return Prop->GetCPPType();
	}

	void GatherWidgets(UWidget* Widget, const FString& ParentName, TArray<FBridgeWidgetInfo>& Out)
	{
		if (!Widget) return;

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
			for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
			{
				GatherWidgets(Panel->GetChildAt(i), Info.Name, Out);
			}
		}
	}

	UWidget* FindWidgetByName(UWidgetBlueprint* WBP, const FString& WidgetName)
	{
		if (!WBP || !WBP->WidgetTree) return nullptr;

		UWidget* Found = nullptr;
		WBP->WidgetTree->ForEachWidget([&](UWidget* W)
		{
			if (W && W->GetName() == WidgetName)
			{
				Found = W;
			}
		});
		return Found;
	}

	UClass* ResolveWidgetClass(const FString& Token)
	{
		FString ClassPath = Token.TrimStartAndEnd();
		if (ClassPath.IsEmpty()) return nullptr;

		if (!ClassPath.StartsWith(TEXT("/")))
		{
			ClassPath = FString::Printf(TEXT("/Script/UMG.%s"), *ClassPath);
		}
		else if (ClassPath.StartsWith(TEXT("/Game/")) && !ClassPath.EndsWith(TEXT("_C")))
		{
			const FString AssetName = FPackageName::GetShortName(ClassPath);
			if (!ClassPath.Contains(TEXT(".")))
			{
				ClassPath += FString::Printf(TEXT(".%s_C"), *AssetName);
			}
			else
			{
				ClassPath += TEXT("_C");
			}
		}

		UClass* Class = LoadClass<UWidget>(nullptr, *ClassPath);
		return Class && Class->IsChildOf(UWidget::StaticClass()) ? Class : nullptr;
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
				UClass* WidgetClass = ResolveWidgetClass(ClassToken);
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
}

// ─── GetWidgetTree ──────────────────────────────────────────

bool UUnrealBridgeUMGLibrary::AddWidgetBlueprintToPIEViewport(
	const FString& WidgetBlueprintPath, int32 ZOrder)
{
	if (!GEditor || !GEditor->PlayWorld)
	{
		return false;
	}

	UClass* WidgetClass = BridgeUMGImpl::ResolveWidgetClass(WidgetBlueprintPath);
	if (!WidgetClass || !WidgetClass->IsChildOf(UUserWidget::StaticClass()))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("UnrealBridge: '%s' is not a UserWidget Blueprint class."),
			*WidgetBlueprintPath);
		return false;
	}

	APlayerController* PlayerController = GEditor->PlayWorld->GetFirstPlayerController();
	if (!PlayerController)
	{
		return false;
	}

	UUserWidget* Widget = CreateWidget<UUserWidget>(
		PlayerController, TSubclassOf<UUserWidget>(WidgetClass));
	if (!Widget)
	{
		return false;
	}

	Widget->AddToViewport(ZOrder);
	BridgeUMGImpl::PIEPreviewWidgets.Add(Widget);
	return true;
}

int32 UUnrealBridgeUMGLibrary::RemovePIEPreviewWidgets()
{
	int32 RemovedCount = 0;
	for (const TWeakObjectPtr<UUserWidget>& WidgetPtr : BridgeUMGImpl::PIEPreviewWidgets)
	{
		if (UUserWidget* Widget = WidgetPtr.Get())
		{
			Widget->RemoveFromParent();
			++RemovedCount;
		}
	}
	BridgeUMGImpl::PIEPreviewWidgets.Reset();
	return RemovedCount;
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

TArray<FBridgeWidgetInfo> UUnrealBridgeUMGLibrary::GetWidgetTree(const FString& WidgetBlueprintPath)
{
	TArray<FBridgeWidgetInfo> Result;

	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath);
	if (!WBP) return Result;

	if (WBP->WidgetTree && WBP->WidgetTree->RootWidget)
	{
		BridgeUMGImpl::GatherWidgets(WBP->WidgetTree->RootWidget, TEXT(""), Result);
	}

	return Result;
}

// ─── GetWidgetProperties ────────────────────────────────────

TArray<FBridgeWidgetPropertyValue> UUnrealBridgeUMGLibrary::GetWidgetProperties(
	const FString& WidgetBlueprintPath, const FString& WidgetName)
{
	TArray<FBridgeWidgetPropertyValue> Result;

	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath);
	if (!WBP) return Result;

	UWidget* Widget = BridgeUMGImpl::FindWidgetByName(WBP, WidgetName);
	if (!Widget) return Result;

	UObject* CDO = Widget->GetClass()->GetDefaultObject();
	if (!CDO) return Result;

	for (TFieldIterator<FProperty> It(Widget->GetClass()); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop) continue;

		// Skip internal/transient properties
		if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_Deprecated))
			continue;

		void* WidgetValue = Prop->ContainerPtrToValuePtr<void>(Widget);
		void* CDOValue = Prop->ContainerPtrToValuePtr<void>(CDO);

		if (!Prop->Identical(WidgetValue, CDOValue))
		{
			FBridgeWidgetPropertyValue PV;
			PV.Name = Prop->GetName();
			PV.Type = BridgeUMGImpl::PropertyTypeToString(Prop);

			FString ExportedValue;
			Prop->ExportTextItem_Direct(ExportedValue, WidgetValue, CDOValue, Widget, PPF_None);
			PV.Value = ExportedValue;

			Result.Add(PV);
		}
	}

	return Result;
}

// ─── GetWidgetAnimations ────────────────────────────────────

TArray<FBridgeWidgetAnimationInfo> UUnrealBridgeUMGLibrary::GetWidgetAnimations(
	const FString& WidgetBlueprintPath)
{
	TArray<FBridgeWidgetAnimationInfo> Result;

	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath);
	if (!WBP) return Result;

	for (UWidgetAnimation* Anim : WBP->Animations)
	{
		if (!Anim) continue;

		FBridgeWidgetAnimationInfo Info;
#if WITH_EDITOR
		Info.Name = Anim->GetDisplayLabel();
		if (Info.Name.IsEmpty())
#endif
		{
			Info.Name = Anim->GetName();
		}

		Info.Duration = Anim->GetEndTime() - Anim->GetStartTime();

		UMovieScene* Scene = Anim->GetMovieScene();
		if (Scene)
		{
			// Build Guid -> widget name map from animation bindings
			TMap<FGuid, FString> GuidToWidget;
			for (const FWidgetAnimationBinding& Binding : Anim->AnimationBindings)
			{
				GuidToWidget.Add(Binding.AnimationGuid, Binding.WidgetName.ToString());
			}

			const TArray<FMovieSceneBinding>& Bindings = const_cast<const UMovieScene*>(Scene)->GetBindings();
			for (const FMovieSceneBinding& Binding : Bindings)
			{
				FString TargetWidget;
				// Find possessable to get bound widget name
				FMovieScenePossessable* Possessable = Scene->FindPossessable(Binding.GetObjectGuid());
				if (Possessable)
				{
					TargetWidget = Possessable->GetName();
				}
				// Fallback: look up via animation binding guid
				if (TargetWidget.IsEmpty())
				{
					if (FString* Found = GuidToWidget.Find(Binding.GetObjectGuid()))
					{
						TargetWidget = *Found;
					}
				}

				for (UMovieSceneTrack* Track : Binding.GetTracks())
				{
					if (!Track) continue;

					FBridgeWidgetAnimTrack TrackInfo;
					TrackInfo.WidgetName = TargetWidget;
					TrackInfo.TrackType = Track->GetClass()->GetName();
					TrackInfo.DisplayName = Track->GetDisplayName().ToString();
					Info.Tracks.Add(TrackInfo);
				}
			}

			// Master/global tracks (not bound to a widget)
			for (UMovieSceneTrack* Track : Scene->GetTracks())
			{
				if (!Track) continue;

				FBridgeWidgetAnimTrack TrackInfo;
				TrackInfo.TrackType = Track->GetClass()->GetName();
				TrackInfo.DisplayName = Track->GetDisplayName().ToString();
				Info.Tracks.Add(TrackInfo);
			}
		}

		Result.Add(Info);
	}

	return Result;
}

// ─── GetWidgetBindings ──────────────────────────────────────

TArray<FBridgeWidgetBindingInfo> UUnrealBridgeUMGLibrary::GetWidgetBindings(
	const FString& WidgetBlueprintPath)
{
	TArray<FBridgeWidgetBindingInfo> Result;

	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath);
	if (!WBP) return Result;

	for (const FDelegateEditorBinding& Binding : WBP->Bindings)
	{
		FBridgeWidgetBindingInfo Info;
		Info.WidgetName = Binding.ObjectName;
		Info.PropertyName = Binding.PropertyName.ToString();
		Info.FunctionName = Binding.FunctionName.ToString();

		if (Info.FunctionName.IsEmpty() && !Binding.SourceProperty.IsNone())
		{
			Info.FunctionName = Binding.SourceProperty.ToString();
		}

		Info.Kind = (Binding.Kind == EBindingKind::Function) ? TEXT("Function") : TEXT("Property");

		Result.Add(Info);
	}

	return Result;
}

// ─── GetWidgetEvents ────────────────────────────────────────

TArray<FBridgeWidgetEventInfo> UUnrealBridgeUMGLibrary::GetWidgetEvents(
	const FString& WidgetBlueprintPath)
{
	TArray<FBridgeWidgetEventInfo> Result;

	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath);
	if (!WBP) return Result;

	// Scan event graph for component bound event nodes (OnClicked, etc.)
	for (UEdGraph* Graph : WBP->UbergraphPages)
	{
		if (!Graph) continue;

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_ComponentBoundEvent* EventNode = Cast<UK2Node_ComponentBoundEvent>(Node))
			{
				FBridgeWidgetEventInfo Info;
				Info.WidgetName = EventNode->ComponentPropertyName.ToString();
				Info.EventName = EventNode->DelegatePropertyName.ToString();
				Info.HandlerName = EventNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
				Result.Add(Info);
			}
		}
	}

	return Result;
}

// ─── SearchWidgets ──────────────────────────────────────────

TArray<FBridgeWidgetInfo> UUnrealBridgeUMGLibrary::SearchWidgets(
	const FString& WidgetBlueprintPath, const FString& Query)
{
	TArray<FBridgeWidgetInfo> All = GetWidgetTree(WidgetBlueprintPath);
	TArray<FBridgeWidgetInfo> Result;

	FString Q = Query.ToLower();
	for (const FBridgeWidgetInfo& W : All)
	{
		if (W.Name.ToLower().Contains(Q) || W.WidgetClass.ToLower().Contains(Q))
		{
			Result.Add(W);
		}
	}

	return Result;
}

// ─── SetWidgetProperty ──────────────────────────────────────

bool UUnrealBridgeUMGLibrary::SetWidgetProperty(
	const FString& WidgetBlueprintPath, const FString& WidgetName,
	const FString& PropertyName, const FString& Value)
{
	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath);
	if (!WBP) return false;

	UWidget* Widget = BridgeUMGImpl::FindWidgetByName(WBP, WidgetName);
	if (!Widget) return false;

	FProperty* Prop = Widget->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop) return false;

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Widget);
	if (!Prop->ImportText_Direct(*Value, ValuePtr, Widget, PPF_None))
		return false;

	WBP->MarkPackageDirty();
	return true;
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
				UClass* WidgetClass = BridgeUMGImpl::ResolveWidgetClass(Operation->GetStringField(TEXT("class")));
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
