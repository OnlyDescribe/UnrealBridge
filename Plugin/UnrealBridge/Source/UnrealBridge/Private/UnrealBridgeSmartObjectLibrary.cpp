#include "UnrealBridgeSmartObjectLibrary.h"

#include "Misc/EngineVersionComparison.h"

#if !UE_VERSION_OLDER_THAN(5, 7, 0)

#include "AssetToolsModule.h"
#include "Factories/DataAssetFactory.h"
#include "IAssetTools.h"
#include "ScopedTransaction.h"

#include "Annotations/SmartObjectSlotEntranceAnnotation.h"
#include "GameplayTagContainer.h"
#include "Logging/TokenizedMessage.h"
#include "SmartObjectAnnotation.h"
#include "SmartObjectBindingCollection.h"
#include "SmartObjectComponent.h"
#include "SmartObjectDefinition.h"
#include "SmartObjectPersistentCollection.h"
#include "SmartObjectRequestTypes.h"
#include "SmartObjectRuntime.h"
#include "SmartObjectSubsystem.h"
#include "SmartObjectTypes.h"

#include "PropertyBindingBinding.h"
#include "PropertyBindingDataView.h"
#include "PropertyBindingPath.h"
#include "StructUtils/InstancedStruct.h"
#include "StructUtils/PropertyBag.h"
#include "WorldConditionBase.h"
#include "WorldConditionQuery.h"
#include "WorldConditionSchema.h"

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "UnrealBridgeSmartObject"

namespace BridgeSmartObjectImpl
{

static FString LastError;

static void ClearError()
{
	LastError.Reset();
}

static bool SetError(const FString& Message)
{
	LastError = Message;
	UE_LOG(LogTemp, Warning, TEXT("UnrealBridge SmartObject: %s"), *Message);
	return false;
}

static FString NormalizeToken(FString Value)
{
	Value.TrimStartAndEndInline();
	Value.ReplaceInline(TEXT(" "), TEXT(""));
	Value.ReplaceInline(TEXT("_"), TEXT(""));
	Value.ReplaceInline(TEXT("-"), TEXT(""));
	Value.ToLowerInline();
	return Value;
}

static FString GuidToString(const FGuid& Guid)
{
	return Guid.IsValid() ? Guid.ToString(EGuidFormats::DigitsWithHyphens) : FString();
}

static bool ParseGuid(const FString& Text, FGuid& OutGuid, const TCHAR* Label)
{
	if (!FGuid::Parse(Text, OutGuid) || !OutGuid.IsValid())
	{
		return SetError(FString::Printf(TEXT("invalid %s GUID '%s'"), Label, *Text));
	}
	return true;
}

template <typename T>
static FString EnumToString(T Value)
{
	if (const UEnum* Enum = StaticEnum<T>())
	{
		return Enum->GetNameStringByValue(static_cast<int64>(Value));
	}
	return FString::FromInt(static_cast<int32>(Value));
}

template <typename T>
static bool ParseEnumToken(const FString& Text, T& OutValue, const TCHAR* Label)
{
	const UEnum* Enum = StaticEnum<T>();
	if (!Enum)
	{
		return SetError(FString::Printf(TEXT("reflection enum unavailable for %s"), Label));
	}
	const int64 Direct = Enum->GetValueByNameString(Text, EGetByNameFlags::None);
	if (Direct != INDEX_NONE)
	{
		OutValue = static_cast<T>(Direct);
		return true;
	}
	const FString Wanted = NormalizeToken(Text);
	for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
	{
		if (NormalizeToken(Enum->GetNameStringByIndex(Index)) == Wanted
			|| NormalizeToken(Enum->GetDisplayNameTextByIndex(Index).ToString()) == Wanted)
		{
			OutValue = static_cast<T>(Enum->GetValueByIndex(Index));
			return true;
		}
	}
	return SetError(FString::Printf(TEXT("unknown %s '%s'"), Label, *Text));
}

static FString WorldTypeToString(const UWorld* World)
{
	if (!World) return TEXT("None");
	switch (World->WorldType)
	{
	case EWorldType::Editor: return TEXT("Editor");
	case EWorldType::PIE: return TEXT("PIE");
	case EWorldType::Game: return TEXT("Game");
	case EWorldType::GamePreview: return TEXT("GamePreview");
	case EWorldType::EditorPreview: return TEXT("EditorPreview");
	case EWorldType::Inactive: return TEXT("Inactive");
	default: return TEXT("Other");
	}
}

static UWorld* GetCurrentWorld()
{
	if (!GEditor)
	{
		SetError(TEXT("GEditor is unavailable"));
		return nullptr;
	}
	if (GEditor->PlayWorld)
	{
		return GEditor->PlayWorld;
	}
	return GEditor->GetEditorWorldContext().World();
}

static bool IsPieRunning()
{
	return GEditor && GEditor->PlayWorld != nullptr;
}

static bool SplitAssetPath(const FString& Path, FString& OutPackagePath, FString& OutAssetName)
{
	int32 LastSlash = INDEX_NONE;
	if (!Path.StartsWith(TEXT("/")) || !Path.FindLastChar(TEXT('/'), LastSlash) || LastSlash == 0)
	{
		return false;
	}
	OutPackagePath = Path.Left(LastSlash);
	OutAssetName = Path.Mid(LastSlash + 1);
	int32 Dot = INDEX_NONE;
	if (OutAssetName.FindChar(TEXT('.'), Dot))
	{
		OutAssetName = OutAssetName.Left(Dot);
	}
	return !OutPackagePath.IsEmpty() && !OutAssetName.IsEmpty();
}

static USmartObjectDefinition* LoadDefinition(const FString& AssetPath, bool bReportError = true)
{
	if (AssetPath.IsEmpty())
	{
		if (bReportError) SetError(TEXT("Smart Object Definition asset path is empty"));
		return nullptr;
	}
	USmartObjectDefinition* Definition = LoadObject<USmartObjectDefinition>(nullptr, *AssetPath);
	if (!Definition && !AssetPath.Contains(TEXT(".")))
	{
		FString PackagePath;
		FString AssetName;
		if (SplitAssetPath(AssetPath, PackagePath, AssetName))
		{
			Definition = LoadObject<USmartObjectDefinition>(nullptr, *(AssetPath + TEXT(".") + AssetName));
		}
	}
	if (!Definition && bReportError)
	{
		SetError(FString::Printf(TEXT("could not load Smart Object Definition '%s'"), *AssetPath));
	}
	return Definition;
}

static AActor* FindActor(const FString& ObjectPath, bool bReportError = true)
{
	if (ObjectPath.IsEmpty()) return nullptr;
	if (AActor* Direct = FindObject<AActor>(nullptr, *ObjectPath)) return Direct;
	for (TObjectIterator<AActor> It; It; ++It)
	{
		if (It->GetPathName() == ObjectPath) return *It;
	}
	if (bReportError) SetError(FString::Printf(TEXT("actor '%s' was not found"), *ObjectPath));
	return nullptr;
}

static USmartObjectComponent* FindComponent(const FString& ObjectPath, bool bReportError = true)
{
	if (ObjectPath.IsEmpty()) return nullptr;
	if (USmartObjectComponent* Direct = FindObject<USmartObjectComponent>(nullptr, *ObjectPath)) return Direct;
	for (TObjectIterator<USmartObjectComponent> It; It; ++It)
	{
		if (It->GetPathName() == ObjectPath) return *It;
	}
	if (bReportError) SetError(FString::Printf(TEXT("SmartObjectComponent '%s' was not found"), *ObjectPath));
	return nullptr;
}

static ASmartObjectPersistentCollection* FindCollection(const FString& ObjectPath, bool bReportError = true)
{
	if (ObjectPath.IsEmpty()) return nullptr;
	if (ASmartObjectPersistentCollection* Direct = FindObject<ASmartObjectPersistentCollection>(nullptr, *ObjectPath)) return Direct;
	for (TObjectIterator<ASmartObjectPersistentCollection> It; It; ++It)
	{
		if (It->GetPathName() == ObjectPath) return *It;
	}
	if (bReportError) SetError(FString::Printf(TEXT("SmartObjectPersistentCollection '%s' was not found"), *ObjectPath));
	return nullptr;
}

static TArray<FString> TagsToStrings(const FGameplayTagContainer& Container)
{
	TArray<FGameplayTag> Tags;
	Container.GetGameplayTagArray(Tags);
	TArray<FString> Result;
	Result.Reserve(Tags.Num());
	for (const FGameplayTag& Tag : Tags) Result.Add(Tag.ToString());
	Result.Sort();
	return Result;
}

static bool StringsToTags(const TArray<FString>& Names, FGameplayTagContainer& OutTags)
{
	OutTags.Reset();
	for (const FString& Name : Names)
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*Name), false);
		if (!Tag.IsValid())
		{
			return SetError(FString::Printf(TEXT("gameplay tag '%s' is not registered"), *Name));
		}
		OutTags.AddTag(Tag);
	}
	return true;
}

static FString TagQueryToJson(const FGameplayTagQuery& Query)
{
	if (Query.IsEmpty()) return FString();
	FGameplayTagQueryExpression Expression;
	Query.GetQueryExpr(Expression);
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	if (!Expression.ConvertToJsonObject(Json)) return FString();
	FString Output;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Json, Writer);
	return Output;
}

static bool JsonToTagQuery(const FString& JsonText, FGameplayTagQuery& OutQuery)
{
	if (JsonText.TrimStartAndEnd().IsEmpty())
	{
		OutQuery = FGameplayTagQuery();
		return true;
	}
	TSharedPtr<FJsonObject> Json;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		return SetError(TEXT("tag query JSON could not be parsed"));
	}
	FGameplayTagQueryExpression Expression;
	if (!FGameplayTagQueryExpression::MakeFromJsonObject(Json.ToSharedRef(), Expression))
	{
		return SetError(TEXT("tag query JSON is not a valid FGameplayTagQueryExpression"));
	}
	OutQuery.Build(Expression, TEXT("Authored by UnrealBridge"));
	return true;
}

static int32 CountProperties(const UStruct* Struct, bool bIncludeInherited = true)
{
	if (!Struct) return 0;
	const EFieldIteratorFlags::SuperClassFlags Flags = bIncludeInherited
		? EFieldIteratorFlags::IncludeSuper : EFieldIteratorFlags::ExcludeSuper;
	int32 Count = 0;
	for (TFieldIterator<FProperty> It(Struct, Flags); It; ++It)
	{
		if (*It && !It->HasAnyPropertyFlags(CPF_Deprecated)) ++Count;
	}
	return Count;
}

struct FResolvedProperty
{
	FProperty* Property = nullptr;
	void* Value = nullptr;
	FProperty* TopLevelProperty = nullptr;
};

static bool ResolveProperty(FPropertyBindingDataView View, const FString& PropertyPath, FResolvedProperty& Out)
{
	if (!View.IsValid()) return SetError(TEXT("the requested data source has no instance data"));
	if (PropertyPath.IsEmpty()) return SetError(TEXT("property path is empty"));
	FPropertyBindingPath Path;
	if (!Path.FromString(PropertyPath))
	{
		return SetError(FString::Printf(TEXT("invalid property path '%s'"), *PropertyPath));
	}
	TArray<FPropertyBindingPathIndirection> Indirections;
	FString Error;
	if (!Path.ResolveIndirectionsWithValue(View, Indirections, &Error, true) || Indirections.IsEmpty())
	{
		return SetError(FString::Printf(TEXT("could not resolve property '%s' on '%s': %s"),
			*PropertyPath, *GetPathNameSafe(View.GetStruct()), *Error));
	}
	const FPropertyBindingPathIndirection& Leaf = Indirections.Last();
	Out.Property = const_cast<FProperty*>(Leaf.GetProperty());
	Out.Value = Leaf.GetMutablePropertyAddress();
	if (!Path.GetSegments().IsEmpty())
	{
		Out.TopLevelProperty = FindFProperty<FProperty>(View.GetStruct(), Path.GetSegments()[0].GetName());
	}
	return Out.Property && Out.Value;
}

static TArray<FBridgeSmartObjectPropertyInfo> ListProperties(FPropertyBindingDataView View, bool bIncludeInherited)
{
	TArray<FBridgeSmartObjectPropertyInfo> Result;
	if (!View.IsValid()) return Result;
	const UStruct* Struct = View.GetStruct();
	UObject* Owner = Cast<UClass>(const_cast<UStruct*>(Struct)) ? static_cast<UObject*>(View.GetMutableMemory()) : nullptr;
	const EFieldIteratorFlags::SuperClassFlags Flags = bIncludeInherited
		? EFieldIteratorFlags::IncludeSuper : EFieldIteratorFlags::ExcludeSuper;
	for (TFieldIterator<FProperty> It(Struct, Flags); It; ++It)
	{
		FProperty* Property = *It;
		if (!Property || Property->HasAnyPropertyFlags(CPF_Deprecated)) continue;
		void* Address = Property->ContainerPtrToValuePtr<void>(View.GetMutableMemory());
		if (!Address) continue;
		FBridgeSmartObjectPropertyInfo Info;
		Info.Path = Property->GetName();
		Info.DisplayName = Property->GetDisplayNameText().ToString();
		Info.Type = Property->GetCPPType(nullptr, CPPF_None);
		Property->ExportTextItem_Direct(Info.Value, Address, nullptr, Owner, PPF_None);
		Info.Category = Property->GetMetaData(TEXT("Category"));
		Info.bEditable = Property->HasAnyPropertyFlags(CPF_Edit)
			&& !Property->HasAnyPropertyFlags(CPF_EditConst | CPF_DisableEditOnInstance);
		Info.bBindable = !Property->GetBoolMetaData(TEXT("NoBinding"));
		Info.bInherited = Property->GetOwnerStruct() != Struct;
		Result.Add(MoveTemp(Info));
	}
	return Result;
}

static FBridgeSmartObjectPropertyResult ReadProperty(
	FPropertyBindingDataView View, const FString& PropertyPath, UObject* Owner)
{
	FBridgeSmartObjectPropertyResult Result;
	FResolvedProperty Resolved;
	if (!ResolveProperty(View, PropertyPath, Resolved))
	{
		Result.Error = LastError;
		return Result;
	}
	Resolved.Property->ExportTextItem_Direct(Result.Value, Resolved.Value, nullptr, Owner, PPF_None);
	Result.Type = Resolved.Property->GetCPPType(nullptr, CPPF_None);
	Result.bSuccess = true;
	return Result;
}

static bool WriteProperty(FPropertyBindingDataView View, const FString& PropertyPath,
	const FString& Value, UObject* Owner)
{
	FResolvedProperty Resolved;
	if (!ResolveProperty(View, PropertyPath, Resolved)) return false;
	const int32 Size = Resolved.Property->GetSize();
	TArray<uint8> Temp;
	Temp.SetNumUninitialized(Size);
	Resolved.Property->InitializeValue(Temp.GetData());
	const TCHAR* Start = *Value;
	const TCHAR* End = Resolved.Property->ImportText_Direct(Start, Temp.GetData(), Owner, PPF_None, GLog);
	if (!End || End == Start)
	{
		Resolved.Property->DestroyValue(Temp.GetData());
		return SetError(FString::Printf(TEXT("could not import '%s' into property '%s' (%s)"),
			*Value, *PropertyPath, *Resolved.Property->GetCPPType()));
	}
	Resolved.Property->CopySingleValue(Resolved.Value, Temp.GetData());
	Resolved.Property->DestroyValue(Temp.GetData());
	return true;
}

static void NotifyDefinitionChanged(USmartObjectDefinition* Definition, const FName RootPropertyName,
	EPropertyChangeType::Type ChangeType = EPropertyChangeType::ValueSet)
{
	if (!Definition) return;
	FProperty* RootProperty = FindFProperty<FProperty>(Definition->GetClass(), RootPropertyName);
	if (RootProperty)
	{
		FEditPropertyChain Chain;
		Chain.AddTail(RootProperty);
		FPropertyChangedEvent Event(RootProperty, ChangeType);
		FPropertyChangedChainEvent ChainEvent(Chain, Event);
		// Access is public on UObject and virtual dispatch still reaches the
		// definition's protected override, preserving Epic's binding/validation work.
		static_cast<UObject*>(Definition)->PostEditChangeChainProperty(ChainEvent);
	}
	else
	{
		Definition->PostEditChange();
	}
	Definition->MarkPackageDirty();
}

static FString TopLevelPropertyKey(const FString& PropertyPath)
{
	int32 End = PropertyPath.Len();
	int32 Dot = INDEX_NONE;
	int32 Bracket = INDEX_NONE;
	if (PropertyPath.FindChar(TEXT('.'), Dot)) End = FMath::Min(End, Dot);
	if (PropertyPath.FindChar(TEXT('['), Bracket)) End = FMath::Min(End, Bracket);
	return NormalizeToken(PropertyPath.Left(End));
}

static FString TopLevelPropertyName(const FString& PropertyPath)
{
	int32 End = PropertyPath.Len();
	int32 Dot = INDEX_NONE;
	int32 Bracket = INDEX_NONE;
	if (PropertyPath.FindChar(TEXT('.'), Dot)) End = FMath::Min(End, Dot);
	if (PropertyPath.FindChar(TEXT('['), Bracket)) End = FMath::Min(End, Bracket);
	return PropertyPath.Left(End);
}

static FSmartObjectSlotDefinition* FindSlot(USmartObjectDefinition* Definition,
	const FString& SlotId, int32* OutIndex = nullptr)
{
	if (!Definition) return nullptr;
	FGuid Guid;
	if (!ParseGuid(SlotId, Guid, TEXT("slot"))) return nullptr;
	const int32 Index = Definition->FindSlotByID(Guid);
	if (Index == INDEX_NONE)
	{
		SetError(FString::Printf(TEXT("slot '%s' was not found"), *SlotId));
		return nullptr;
	}
	if (OutIndex) *OutIndex = Index;
	return &Definition->GetMutableSlots()[Index];
}

using FBehaviorArray = TArray<TObjectPtr<USmartObjectBehaviorDefinition>>;
using FDefinitionDataArray = TArray<FSmartObjectDefinitionDataProxy>;

static FBehaviorArray* GetDefaultBehaviors(USmartObjectDefinition* Definition)
{
	FArrayProperty* Property = FindFProperty<FArrayProperty>(Definition->GetClass(), TEXT("DefaultBehaviorDefinitions"));
	return Property ? Property->ContainerPtrToValuePtr<FBehaviorArray>(Definition) : nullptr;
}

static FDefinitionDataArray* GetRootDefinitionData(USmartObjectDefinition* Definition)
{
	FArrayProperty* Property = FindFProperty<FArrayProperty>(Definition->GetClass(), TEXT("DefinitionData"));
	return Property ? Property->ContainerPtrToValuePtr<FDefinitionDataArray>(Definition) : nullptr;
}

static FInstancedPropertyBag* GetParameters(USmartObjectDefinition* Definition)
{
	FStructProperty* Property = FindFProperty<FStructProperty>(Definition->GetClass(), TEXT("Parameters"));
	return Property ? Property->ContainerPtrToValuePtr<FInstancedPropertyBag>(Definition) : nullptr;
}

static FSmartObjectBindingCollection* GetBindingCollection(USmartObjectDefinition* Definition)
{
	FStructProperty* Property = FindFProperty<FStructProperty>(Definition->GetClass(), TEXT("BindingCollection"));
	return Property ? Property->ContainerPtrToValuePtr<FSmartObjectBindingCollection>(Definition) : nullptr;
}

static FGuid GetGuidProperty(USmartObjectDefinition* Definition, const FName Name)
{
	FStructProperty* Property = FindFProperty<FStructProperty>(Definition->GetClass(), Name);
	if (!Property) return FGuid();
	const FGuid* Guid = Property->ContainerPtrToValuePtr<FGuid>(Definition);
	return Guid ? *Guid : FGuid();
}

static bool GetBehaviorArrayForSlot(USmartObjectDefinition* Definition, const FString& SlotId,
	FBehaviorArray*& OutArray, FName& OutRootProperty)
{
	if (SlotId.IsEmpty())
	{
		OutArray = GetDefaultBehaviors(Definition);
		OutRootProperty = TEXT("DefaultBehaviorDefinitions");
		if (!OutArray) return SetError(TEXT("definition default behavior array is unavailable"));
		return true;
	}
	FSmartObjectSlotDefinition* Slot = FindSlot(Definition, SlotId);
	if (!Slot) return false;
	OutArray = &Slot->BehaviorDefinitions;
	OutRootProperty = TEXT("Slots");
	return true;
}

struct FBehaviorLocation
{
	FBehaviorArray* Array = nullptr;
	FString SlotId;
	FName RootProperty;
	int32 Index = INDEX_NONE;
	USmartObjectBehaviorDefinition* Behavior = nullptr;
};

static FBehaviorLocation FindBehavior(USmartObjectDefinition* Definition, const FString& ObjectPath)
{
	FBehaviorLocation Result;
	if (!Definition) return Result;
	auto Search = [&](FBehaviorArray* Array, const FString& SlotId, const FName Root) -> bool
	{
		if (!Array) return false;
		for (int32 Index = 0; Index < Array->Num(); ++Index)
		{
			USmartObjectBehaviorDefinition* Behavior = (*Array)[Index];
			if (Behavior && (Behavior->GetPathName() == ObjectPath || Behavior->GetName() == ObjectPath))
			{
				Result.Array = Array;
				Result.SlotId = SlotId;
				Result.RootProperty = Root;
				Result.Index = Index;
				Result.Behavior = Behavior;
				return true;
			}
		}
		return false;
	};
	if (Search(GetDefaultBehaviors(Definition), FString(), TEXT("DefaultBehaviorDefinitions"))) return Result;
	for (FSmartObjectSlotDefinition& Slot : Definition->GetMutableSlots())
	{
		if (Search(&Slot.BehaviorDefinitions, GuidToString(Slot.ID), TEXT("Slots"))) return Result;
	}
	SetError(FString::Printf(TEXT("behavior object '%s' was not found in '%s'"), *ObjectPath, *Definition->GetPathName()));
	return Result;
}

static bool GetDefinitionDataArrayForSlot(USmartObjectDefinition* Definition, const FString& SlotId,
	FDefinitionDataArray*& OutArray, FName& OutRootProperty)
{
	if (SlotId.IsEmpty())
	{
		OutArray = GetRootDefinitionData(Definition);
		OutRootProperty = TEXT("DefinitionData");
		if (!OutArray) return SetError(TEXT("definition data array is unavailable"));
		return true;
	}
	FSmartObjectSlotDefinition* Slot = FindSlot(Definition, SlotId);
	if (!Slot) return false;
	OutArray = &Slot->DefinitionData;
	OutRootProperty = TEXT("Slots");
	return true;
}

struct FDefinitionDataLocation
{
	FDefinitionDataArray* Array = nullptr;
	FString SlotId;
	FName RootProperty;
	int32 Index = INDEX_NONE;
	FSmartObjectDefinitionDataProxy* Proxy = nullptr;
};

static FDefinitionDataLocation FindDefinitionData(USmartObjectDefinition* Definition, const FString& DataId)
{
	FDefinitionDataLocation Result;
	FGuid Guid;
	if (!Definition || !ParseGuid(DataId, Guid, TEXT("definition data"))) return Result;
	auto Search = [&](FDefinitionDataArray* Array, const FString& SlotId, const FName Root) -> bool
	{
		if (!Array) return false;
		for (int32 Index = 0; Index < Array->Num(); ++Index)
		{
			if ((*Array)[Index].ID == Guid)
			{
				Result.Array = Array;
				Result.SlotId = SlotId;
				Result.RootProperty = Root;
				Result.Index = Index;
				Result.Proxy = &(*Array)[Index];
				return true;
			}
		}
		return false;
	};
	if (Search(GetRootDefinitionData(Definition), FString(), TEXT("DefinitionData"))) return Result;
	for (FSmartObjectSlotDefinition& Slot : Definition->GetMutableSlots())
	{
		if (Search(&Slot.DefinitionData, GuidToString(Slot.ID), TEXT("Slots"))) return Result;
	}
	SetError(FString::Printf(TEXT("definition data '%s' was not found"), *DataId));
	return Result;
}

static int32 GetEditableConditionCount(const FWorldConditionQueryDefinition& Query)
{
	const FArrayProperty* Property = FindFProperty<FArrayProperty>(FWorldConditionQueryDefinition::StaticStruct(), TEXT("EditableConditions"));
	if (!Property) return 0;
	const TArray<FWorldConditionEditable>* Conditions = Property->ContainerPtrToValuePtr<TArray<FWorldConditionEditable>>(&Query);
	return Conditions ? Conditions->Num() : 0;
}

static FSmartObjectDefinitionPreviewData* GetPreviewData(USmartObjectDefinition* Definition)
{
	FStructProperty* Property = FindFProperty<FStructProperty>(Definition->GetClass(), TEXT("PreviewData"));
	return Property ? Property->ContainerPtrToValuePtr<FSmartObjectDefinitionPreviewData>(Definition) : nullptr;
}

static int32 SeverityRankToMessageType(const EMessageSeverity::Type Severity)
{
	return static_cast<int32>(Severity);
}

static FString SeverityToString(EMessageSeverity::Type Severity)
{
	const int32 Value = SeverityRankToMessageType(Severity);
	if (Value < EMessageSeverity::Error) return TEXT("CriticalError");
	if (Severity == EMessageSeverity::Error) return TEXT("Error");
	if (Severity == EMessageSeverity::Warning) return TEXT("Warning");
	return TEXT("Info");
}

} // namespace BridgeSmartObjectImpl

bool UUnrealBridgeSmartObjectLibrary::IsSmartObjectApiAvailable()
{
	return true;
}

FString UUnrealBridgeSmartObjectLibrary::GetLastSmartObjectError()
{
	return BridgeSmartObjectImpl::LastError;
}

FBridgeSmartObjectCreateResult UUnrealBridgeSmartObjectLibrary::CreateSmartObjectDefinition(const FString& AssetPath)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	FBridgeSmartObjectCreateResult Result;
	if (IsPieRunning())
	{
		Result.Error = TEXT("refusing to create a Smart Object Definition while PIE is running");
		SetError(Result.Error);
		return Result;
	}
	FString PackagePath;
	FString AssetName;
	if (!SplitAssetPath(AssetPath, PackagePath, AssetName))
	{
		Result.Error = FString::Printf(TEXT("invalid asset path '%s'; expected /Game/Folder/AssetName"), *AssetPath);
		SetError(Result.Error);
		return Result;
	}
	UDataAssetFactory* Factory = NewObject<UDataAssetFactory>(GetTransientPackage());
	Factory->DataAssetClass = USmartObjectDefinition::StaticClass();
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	USmartObjectDefinition* Definition = Cast<USmartObjectDefinition>(AssetTools.CreateAsset(
		AssetName, PackagePath, USmartObjectDefinition::StaticClass(), Factory));
	if (!Definition)
	{
		Result.Error = FString::Printf(TEXT("failed to create Smart Object Definition at '%s/%s'; the path may already exist"),
			*PackagePath, *AssetName);
		SetError(Result.Error);
		return Result;
	}
	Definition->MarkPackageDirty();
	Result.bSuccess = true;
	Result.AssetPath = Definition->GetPathName();
	return Result;
}

FBridgeSmartObjectDefinitionInfo UUnrealBridgeSmartObjectLibrary::GetSmartObjectDefinitionInfo(const FString& AssetPath)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	FBridgeSmartObjectDefinitionInfo Info;
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	if (!Definition) return Info;
	Info.AssetPath = Definition->GetPathName();
	Info.UserTagFilterJson = TagQueryToJson(Definition->GetUserTagFilter());
	Info.ActivityTags = TagsToStrings(Definition->GetActivityTags());
	Info.UserTagsFilteringPolicy = EnumToString(Definition->GetUserTagsFilteringPolicy());
	Info.ActivityTagsMergingPolicy = EnumToString(Definition->GetActivityTagsMergingPolicy());
	Info.WorldConditionSchemaClassPath = GetPathNameSafe(Definition->GetWorldConditionSchemaClass());
	if (FSmartObjectDefinitionPreviewData* Preview = GetPreviewData(Definition))
	{
		Info.PreviewObjectActorClassPath = Preview->ObjectActorClass.ToSoftObjectPath().ToString();
		Info.PreviewObjectMeshPath = Preview->ObjectMeshPath.ToString();
		Info.PreviewUserActorClassPath = Preview->UserActorClass.ToSoftObjectPath().ToString();
		Info.PreviewValidationFilterClassPath = Preview->UserValidationFilterClass.ToSoftObjectPath().ToString();
	}
	Info.RootBindableId = GuidToString(GetGuidProperty(Definition, TEXT("RootID")));
	Info.ParametersBindableId = GuidToString(GetGuidProperty(Definition, TEXT("ParametersID")));
	Info.SlotCount = Definition->GetSlots().Num();
	if (FBehaviorArray* Behaviors = GetDefaultBehaviors(Definition)) Info.DefaultBehaviorCount = Behaviors->Num();
	if (FDefinitionDataArray* Data = GetRootDefinitionData(Definition)) Info.DefinitionDataCount = Data->Num();
	Info.ObjectConditionCount = GetEditableConditionCount(Definition->GetPreconditions());
	if (FInstancedPropertyBag* Parameters = GetParameters(Definition))
	{
		if (const UPropertyBag* Bag = Parameters->GetPropertyBagStruct()) Info.ParameterCount = Bag->GetPropertyDescs().Num();
	}
	if (FSmartObjectBindingCollection* Bindings = GetBindingCollection(Definition)) Info.BindingCount = Bindings->GetNumBindings();
	Info.bHasBeenValidated = Definition->HasBeenValidated();
	Info.bValid = Definition->IsDefinitionValid();
	Info.bDirty = Definition->GetOutermost()->IsDirty();
	return Info;
}

FBridgeSmartObjectValidationResult UUnrealBridgeSmartObjectLibrary::ValidateSmartObjectDefinition(const FString& AssetPath)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	FBridgeSmartObjectValidationResult Result;
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	if (!Definition)
	{
		Result.Error = LastError;
		return Result;
	}
	TArray<TPair<EMessageSeverity::Type, FText>> Messages;
	Result.bSuccess = Definition->Validate(&Messages);
	for (const TPair<EMessageSeverity::Type, FText>& Message : Messages)
	{
		FBridgeSmartObjectValidationMessage Out;
		Out.Severity = SeverityToString(Message.Key);
		Out.Message = Message.Value.ToString();
		Result.Messages.Add(MoveTemp(Out));
	}
	if (!Result.bSuccess)
	{
		Result.Error = Result.Messages.IsEmpty()
			? TEXT("Smart Object Definition validation failed without a diagnostic")
			: Result.Messages.Last().Message;
		SetError(Result.Error);
	}
	return Result;
}

TArray<FBridgeSmartObjectPropertyInfo> UUnrealBridgeSmartObjectLibrary::ListSmartObjectDefinitionProperties(
	const FString& AssetPath, bool bIncludeInherited)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	return Definition ? ListProperties(FPropertyBindingDataView(Definition), bIncludeInherited)
		: TArray<FBridgeSmartObjectPropertyInfo>();
}

FBridgeSmartObjectPropertyResult UUnrealBridgeSmartObjectLibrary::GetSmartObjectDefinitionProperty(
	const FString& AssetPath, const FString& PropertyPath)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	if (!Definition)
	{
		FBridgeSmartObjectPropertyResult Result;
		Result.Error = LastError;
		return Result;
	}
	return ReadProperty(FPropertyBindingDataView(Definition), PropertyPath, Definition);
}

bool UUnrealBridgeSmartObjectLibrary::SetSmartObjectDefinitionProperty(
	const FString& AssetPath, const FString& PropertyPath, const FString& Value)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	const FString Key = TopLevelPropertyKey(PropertyPath);
	if (Key == TEXT("slots") || Key == TEXT("defaultbehaviordefinitions") || Key == TEXT("definitiondata")
		|| Key == TEXT("parameters") || Key == TEXT("bindingcollection") || Key == TEXT("preconditions")
		|| Key == TEXT("rootid") || Key == TEXT("parametersid"))
	{
		return SetError(FString::Printf(TEXT("structural property '%s' has a dedicated Smart Object API"), *PropertyPath));
	}
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	if (!Definition) return false;
	FScopedTransaction Transaction(LOCTEXT("SetSmartObjectDefinitionProperty", "UnrealBridge: Set Smart Object Definition Property"));
	Definition->Modify();
	if (!WriteProperty(FPropertyBindingDataView(Definition), PropertyPath, Value, Definition)) return false;
	NotifyDefinitionChanged(Definition, FName(*TopLevelPropertyName(PropertyPath)));
	return true;
}

FString UUnrealBridgeSmartObjectLibrary::GetSmartObjectTagQueryJson(
	const FString& AssetPath, const FString& SlotId)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	if (!Definition) return FString();
	if (SlotId.IsEmpty()) return TagQueryToJson(Definition->GetUserTagFilter());
	FSmartObjectSlotDefinition* Slot = FindSlot(Definition, SlotId);
	return Slot ? TagQueryToJson(Slot->UserTagFilter) : FString();
}

bool UUnrealBridgeSmartObjectLibrary::SetSmartObjectTagQueryJson(
	const FString& AssetPath, const FString& QueryJson, const FString& SlotId)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	if (!Definition) return false;
	FGameplayTagQuery Query;
	if (!JsonToTagQuery(QueryJson, Query)) return false;
	FScopedTransaction Transaction(LOCTEXT("SetSmartObjectTagQuery", "UnrealBridge: Set Smart Object Tag Query"));
	Definition->Modify();
	if (SlotId.IsEmpty())
	{
		Definition->SetUserTagFilter(Query);
		NotifyDefinitionChanged(Definition, TEXT("UserTagFilter"));
	}
	else
	{
		FSmartObjectSlotDefinition* Slot = FindSlot(Definition, SlotId);
		if (!Slot) return false;
		Slot->UserTagFilter = MoveTemp(Query);
		NotifyDefinitionChanged(Definition, TEXT("Slots"));
	}
	return true;
}

bool UUnrealBridgeSmartObjectLibrary::SetSmartObjectTags(const FString& AssetPath,
	const TArray<FString>& Tags, const FString& SlotId, const FString& TagSet)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	if (!Definition) return false;
	FGameplayTagContainer Container;
	if (!StringsToTags(Tags, Container)) return false;
	const FString Key = NormalizeToken(TagSet);
	FScopedTransaction Transaction(LOCTEXT("SetSmartObjectTags", "UnrealBridge: Set Smart Object Tags"));
	Definition->Modify();
	if (SlotId.IsEmpty())
	{
		if (Key != TEXT("activity")) return SetError(TEXT("definition-level TagSet must be Activity"));
		Definition->SetActivityTags(Container);
		NotifyDefinitionChanged(Definition, TEXT("ActivityTags"));
		return true;
	}
	FSmartObjectSlotDefinition* Slot = FindSlot(Definition, SlotId);
	if (!Slot) return false;
	if (Key == TEXT("activity")) Slot->ActivityTags = Container;
	else if (Key == TEXT("runtime")) Slot->RuntimeTags = Container;
	else return SetError(TEXT("slot TagSet must be Activity or Runtime"));
	NotifyDefinitionChanged(Definition, TEXT("Slots"));
	return true;
}

bool UUnrealBridgeSmartObjectLibrary::SetSmartObjectTagPolicies(const FString& AssetPath,
	const FString& UserTagsFilteringPolicy, const FString& ActivityTagsMergingPolicy)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	ESmartObjectTagFilteringPolicy Filtering;
	ESmartObjectTagMergingPolicy Merging;
	if (!Definition || !ParseEnumToken(UserTagsFilteringPolicy, Filtering, TEXT("user tag filtering policy"))
		|| !ParseEnumToken(ActivityTagsMergingPolicy, Merging, TEXT("activity tag merging policy"))) return false;
	FScopedTransaction Transaction(LOCTEXT("SetSmartObjectTagPolicies", "UnrealBridge: Set Smart Object Tag Policies"));
	Definition->Modify();
	Definition->SetUserTagsFilteringPolicy(Filtering);
	Definition->SetActivityTagsMergingPolicy(Merging);
	NotifyDefinitionChanged(Definition, TEXT("UserTagsFilteringPolicy"));
	return true;
}

TArray<FBridgeSmartObjectSlotInfo> UUnrealBridgeSmartObjectLibrary::ListSmartObjectSlots(const FString& AssetPath)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	TArray<FBridgeSmartObjectSlotInfo> Result;
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	if (!Definition) return Result;
	for (int32 Index = 0; Index < Definition->GetSlots().Num(); ++Index)
	{
		const FSmartObjectSlotDefinition& Slot = Definition->GetSlot(Index);
		FBridgeSmartObjectSlotInfo Info;
		Info.Id = GuidToString(Slot.ID);
		Info.Index = Index;
		Info.Name = Slot.Name.ToString();
		Info.Offset = FVector(Slot.Offset);
		Info.Rotation = FRotator(Slot.Rotation);
		Info.bEnabled = Slot.bEnabled;
		Info.UserTagFilterJson = TagQueryToJson(Slot.UserTagFilter);
		Info.ActivityTags = TagsToStrings(Slot.ActivityTags);
		Info.RuntimeTags = TagsToStrings(Slot.RuntimeTags);
		Info.BehaviorCount = Slot.BehaviorDefinitions.Num();
		Info.DefinitionDataCount = Slot.DefinitionData.Num();
		Info.ConditionCount = GetEditableConditionCount(Slot.SelectionPreconditions);
		Result.Add(MoveTemp(Info));
	}
	return Result;
}

FString UUnrealBridgeSmartObjectLibrary::AddSmartObjectSlot(const FString& AssetPath, const FString& Name,
	const FVector& Offset, const FRotator& Rotation, bool bEnabled, int32 InsertIndex)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	if (!Definition) return FString();
	FScopedTransaction Transaction(LOCTEXT("AddSmartObjectSlot", "UnrealBridge: Add Smart Object Slot"));
	Definition->Modify();
	TArrayView<FSmartObjectSlotDefinition> Slots = Definition->GetMutableSlots();
	const int32 TargetIndex = InsertIndex < 0 ? Slots.Num() : FMath::Clamp(InsertIndex, 0, Slots.Num());
	// TArrayView cannot insert; the reflected array points to the same storage and is safe to mutate in editor.
	FArrayProperty* Property = FindFProperty<FArrayProperty>(Definition->GetClass(), TEXT("Slots"));
	TArray<FSmartObjectSlotDefinition>* MutableArray = Property
		? Property->ContainerPtrToValuePtr<TArray<FSmartObjectSlotDefinition>>(Definition) : nullptr;
	if (!MutableArray) return SetError(TEXT("definition slot array is unavailable")), FString();
	FSmartObjectSlotDefinition NewSlot;
	NewSlot.ID = FGuid::NewGuid();
	NewSlot.Name = FName(*Name);
	NewSlot.Offset = FVector3f(Offset);
	NewSlot.Rotation = FRotator3f(Rotation);
	NewSlot.bEnabled = bEnabled;
	NewSlot.SelectionPreconditions.SetSchemaClass(Definition->GetWorldConditionSchemaClass());
	NewSlot.SelectionPreconditions.Initialize(Definition);
	const FGuid NewId = NewSlot.ID;
	MutableArray->Insert(MoveTemp(NewSlot), TargetIndex);
	(void)UE::SmartObject::Delegates::OnSlotDefinitionCreated.ExecuteIfBound(*Definition, (*MutableArray)[TargetIndex]);
	NotifyDefinitionChanged(Definition, TEXT("Slots"));
	return GuidToString(NewId);
}

FString UUnrealBridgeSmartObjectLibrary::DuplicateSmartObjectSlot(const FString& AssetPath,
	const FString& SourceSlotId, const FString& NewName, int32 InsertIndex)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	int32 SourceIndex = INDEX_NONE;
	FSmartObjectSlotDefinition* Source = FindSlot(Definition, SourceSlotId, &SourceIndex);
	if (!Source) return FString();
	FArrayProperty* Property = FindFProperty<FArrayProperty>(Definition->GetClass(), TEXT("Slots"));
	TArray<FSmartObjectSlotDefinition>* Slots = Property
		? Property->ContainerPtrToValuePtr<TArray<FSmartObjectSlotDefinition>>(Definition) : nullptr;
	if (!Slots) return SetError(TEXT("definition slot array is unavailable")), FString();
	FScopedTransaction Transaction(LOCTEXT("DuplicateSmartObjectSlot", "UnrealBridge: Duplicate Smart Object Slot"));
	Definition->Modify();
	FSmartObjectSlotDefinition Copy = *Source;
	Copy.ID = FGuid::NewGuid();
	for (FSmartObjectDefinitionDataProxy& Proxy : Copy.DefinitionData) Proxy.ID = FGuid::NewGuid();
	if (!NewName.IsEmpty()) Copy.Name = FName(*NewName);
	const int32 TargetIndex = InsertIndex < 0 ? SourceIndex + 1 : FMath::Clamp(InsertIndex, 0, Slots->Num());
	const FGuid NewId = Copy.ID;
	Slots->Insert(MoveTemp(Copy), TargetIndex);
	NotifyDefinitionChanged(Definition, TEXT("Slots"));
	return GuidToString(NewId);
}

bool UUnrealBridgeSmartObjectLibrary::RemoveSmartObjectSlot(const FString& AssetPath, const FString& SlotId)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	int32 Index = INDEX_NONE;
	if (!FindSlot(Definition, SlotId, &Index)) return false;
	FArrayProperty* Property = FindFProperty<FArrayProperty>(Definition->GetClass(), TEXT("Slots"));
	TArray<FSmartObjectSlotDefinition>* Slots = Property
		? Property->ContainerPtrToValuePtr<TArray<FSmartObjectSlotDefinition>>(Definition) : nullptr;
	if (!Slots) return SetError(TEXT("definition slot array is unavailable"));
	FScopedTransaction Transaction(LOCTEXT("RemoveSmartObjectSlot", "UnrealBridge: Remove Smart Object Slot"));
	Definition->Modify();
	Slots->RemoveAt(Index);
	NotifyDefinitionChanged(Definition, TEXT("Slots"));
	return true;
}

bool UUnrealBridgeSmartObjectLibrary::MoveSmartObjectSlot(const FString& AssetPath,
	const FString& SlotId, int32 NewIndex)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	int32 Index = INDEX_NONE;
	if (!FindSlot(Definition, SlotId, &Index)) return false;
	FArrayProperty* Property = FindFProperty<FArrayProperty>(Definition->GetClass(), TEXT("Slots"));
	TArray<FSmartObjectSlotDefinition>* Slots = Property
		? Property->ContainerPtrToValuePtr<TArray<FSmartObjectSlotDefinition>>(Definition) : nullptr;
	if (!Slots || !Slots->IsValidIndex(NewIndex)) return SetError(TEXT("new slot index is out of range"));
	if (Index == NewIndex) return true;
	FScopedTransaction Transaction(LOCTEXT("MoveSmartObjectSlot", "UnrealBridge: Move Smart Object Slot"));
	Definition->Modify();
	FSmartObjectSlotDefinition Copy = MoveTemp((*Slots)[Index]);
	Slots->RemoveAt(Index);
	Slots->Insert(MoveTemp(Copy), NewIndex);
	NotifyDefinitionChanged(Definition, TEXT("Slots"));
	return true;
}

TArray<FBridgeSmartObjectPropertyInfo> UUnrealBridgeSmartObjectLibrary::ListSmartObjectSlotProperties(
	const FString& AssetPath, const FString& SlotId)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FSmartObjectSlotDefinition* Slot = FindSlot(Definition, SlotId);
	return Slot ? ListProperties(FPropertyBindingDataView(FStructView::Make(*Slot)), true)
		: TArray<FBridgeSmartObjectPropertyInfo>();
}

FBridgeSmartObjectPropertyResult UUnrealBridgeSmartObjectLibrary::GetSmartObjectSlotProperty(
	const FString& AssetPath, const FString& SlotId, const FString& PropertyPath)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FSmartObjectSlotDefinition* Slot = FindSlot(Definition, SlotId);
	if (!Slot)
	{
		FBridgeSmartObjectPropertyResult Result;
		Result.Error = LastError;
		return Result;
	}
	return ReadProperty(FPropertyBindingDataView(FStructView::Make(*Slot)), PropertyPath, Definition);
}

bool UUnrealBridgeSmartObjectLibrary::SetSmartObjectSlotProperty(const FString& AssetPath,
	const FString& SlotId, const FString& PropertyPath, const FString& Value)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	const FString Key = TopLevelPropertyKey(PropertyPath);
	if (Key == TEXT("id") || Key == TEXT("behaviordefinitions") || Key == TEXT("definitiondata")
		|| Key == TEXT("selectionpreconditions"))
	{
		return SetError(FString::Printf(TEXT("structural slot property '%s' has a dedicated API"), *PropertyPath));
	}
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FSmartObjectSlotDefinition* Slot = FindSlot(Definition, SlotId);
	if (!Slot) return false;
	FScopedTransaction Transaction(LOCTEXT("SetSmartObjectSlotProperty", "UnrealBridge: Set Smart Object Slot Property"));
	Definition->Modify();
	if (!WriteProperty(FPropertyBindingDataView(FStructView::Make(*Slot)), PropertyPath, Value, Definition)) return false;
	NotifyDefinitionChanged(Definition, TEXT("Slots"));
	return true;
}

TArray<FBridgeSmartObjectTypeInfo> UUnrealBridgeSmartObjectLibrary::ListSmartObjectBehaviorTypes()
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	TArray<FBridgeSmartObjectTypeInfo> Result;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (!Class || Class == USmartObjectBehaviorDefinition::StaticClass()
			|| !Class->IsChildOf(USmartObjectBehaviorDefinition::StaticClass())
			|| Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) continue;
		FBridgeSmartObjectTypeInfo Info;
		Info.Kind = TEXT("Behavior");
		Info.TypePath = Class->GetPathName();
		Info.DisplayName = Class->GetDisplayNameText().ToString();
		Info.bAllowedAtDefinition = true;
		Info.bAllowedAtSlot = true;
		Result.Add(MoveTemp(Info));
	}
	Result.Sort([](const FBridgeSmartObjectTypeInfo& A, const FBridgeSmartObjectTypeInfo& B)
	{
		return A.DisplayName < B.DisplayName;
	});
	return Result;
}

TArray<FBridgeSmartObjectBehaviorInfo> UUnrealBridgeSmartObjectLibrary::ListSmartObjectBehaviorDefinitions(
	const FString& AssetPath, const FString& SlotId)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	TArray<FBridgeSmartObjectBehaviorInfo> Result;
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FBehaviorArray* Behaviors = nullptr;
	FName Root;
	if (!Definition || !GetBehaviorArrayForSlot(Definition, SlotId, Behaviors, Root)) return Result;
	for (int32 Index = 0; Index < Behaviors->Num(); ++Index)
	{
		USmartObjectBehaviorDefinition* Behavior = (*Behaviors)[Index];
		if (!Behavior) continue;
		FBridgeSmartObjectBehaviorInfo Info;
		Info.SlotId = SlotId;
		Info.Index = Index;
		Info.ObjectPath = Behavior->GetPathName();
		Info.Name = Behavior->GetName();
		Info.ClassPath = Behavior->GetClass()->GetPathName();
		Info.PropertyCount = CountProperties(Behavior->GetClass());
		Result.Add(MoveTemp(Info));
	}
	return Result;
}

FString UUnrealBridgeSmartObjectLibrary::AddSmartObjectBehaviorDefinition(const FString& AssetPath,
	const FString& BehaviorClassPath, const FString& SlotId, int32 InsertIndex)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	UClass* Class = LoadObject<UClass>(nullptr, *BehaviorClassPath);
	if (!Definition) return FString();
	if (!Class || !Class->IsChildOf(USmartObjectBehaviorDefinition::StaticClass())
		|| Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
	{
		SetError(FString::Printf(TEXT("'%s' is not a concrete Smart Object behavior definition class"), *BehaviorClassPath));
		return FString();
	}
	FBehaviorArray* Behaviors = nullptr;
	FName Root;
	if (!GetBehaviorArrayForSlot(Definition, SlotId, Behaviors, Root)) return FString();
	FScopedTransaction Transaction(LOCTEXT("AddSmartObjectBehavior", "UnrealBridge: Add Smart Object Behavior"));
	Definition->Modify();
	USmartObjectBehaviorDefinition* Behavior = NewObject<USmartObjectBehaviorDefinition>(Definition, Class,
		NAME_None, RF_Transactional);
	if (!Behavior) return SetError(TEXT("failed to instantiate behavior definition")), FString();
	const int32 TargetIndex = InsertIndex < 0 ? Behaviors->Num() : FMath::Clamp(InsertIndex, 0, Behaviors->Num());
	Behaviors->Insert(Behavior, TargetIndex);
	NotifyDefinitionChanged(Definition, Root);
	return Behavior->GetPathName();
}

bool UUnrealBridgeSmartObjectLibrary::RemoveSmartObjectBehaviorDefinition(
	const FString& AssetPath, const FString& BehaviorObjectPath)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FBehaviorLocation Location = FindBehavior(Definition, BehaviorObjectPath);
	if (!Location.Behavior) return false;
	FScopedTransaction Transaction(LOCTEXT("RemoveSmartObjectBehavior", "UnrealBridge: Remove Smart Object Behavior"));
	Definition->Modify();
	Location.Behavior->Modify();
	Location.Array->RemoveAt(Location.Index);
	Location.Behavior->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
	Location.Behavior->MarkAsGarbage();
	NotifyDefinitionChanged(Definition, Location.RootProperty);
	return true;
}

bool UUnrealBridgeSmartObjectLibrary::MoveSmartObjectBehaviorDefinition(const FString& AssetPath,
	const FString& BehaviorObjectPath, int32 NewIndex)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FBehaviorLocation Location = FindBehavior(Definition, BehaviorObjectPath);
	if (!Location.Behavior) return false;
	if (!Location.Array->IsValidIndex(NewIndex)) return SetError(TEXT("new behavior index is out of range"));
	if (Location.Index == NewIndex) return true;
	FScopedTransaction Transaction(LOCTEXT("MoveSmartObjectBehavior", "UnrealBridge: Move Smart Object Behavior"));
	Definition->Modify();
	TObjectPtr<USmartObjectBehaviorDefinition> Behavior = (*Location.Array)[Location.Index];
	Location.Array->RemoveAt(Location.Index);
	Location.Array->Insert(Behavior, NewIndex);
	NotifyDefinitionChanged(Definition, Location.RootProperty);
	return true;
}

TArray<FBridgeSmartObjectPropertyInfo> UUnrealBridgeSmartObjectLibrary::ListSmartObjectBehaviorProperties(
	const FString& AssetPath, const FString& BehaviorObjectPath, bool bIncludeInherited)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FBehaviorLocation Location = FindBehavior(Definition, BehaviorObjectPath);
	return Location.Behavior ? ListProperties(FPropertyBindingDataView(Location.Behavior), bIncludeInherited)
		: TArray<FBridgeSmartObjectPropertyInfo>();
}

FBridgeSmartObjectPropertyResult UUnrealBridgeSmartObjectLibrary::GetSmartObjectBehaviorProperty(
	const FString& AssetPath, const FString& BehaviorObjectPath, const FString& PropertyPath)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FBehaviorLocation Location = FindBehavior(Definition, BehaviorObjectPath);
	if (!Location.Behavior)
	{
		FBridgeSmartObjectPropertyResult Result;
		Result.Error = LastError;
		return Result;
	}
	return ReadProperty(FPropertyBindingDataView(Location.Behavior), PropertyPath, Location.Behavior);
}

bool UUnrealBridgeSmartObjectLibrary::SetSmartObjectBehaviorProperty(const FString& AssetPath,
	const FString& BehaviorObjectPath, const FString& PropertyPath, const FString& Value)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FBehaviorLocation Location = FindBehavior(Definition, BehaviorObjectPath);
	if (!Location.Behavior) return false;
	FScopedTransaction Transaction(LOCTEXT("SetSmartObjectBehaviorProperty", "UnrealBridge: Set Smart Object Behavior Property"));
	Definition->Modify();
	Location.Behavior->Modify();
	if (!WriteProperty(FPropertyBindingDataView(Location.Behavior), PropertyPath, Value, Location.Behavior)) return false;
	NotifyDefinitionChanged(Definition, Location.RootProperty);
	return true;
}

TArray<FBridgeSmartObjectTypeInfo> UUnrealBridgeSmartObjectLibrary::ListSmartObjectDefinitionDataTypes(
	const FString& AssetPath, const FString& SlotId)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	TArray<FBridgeSmartObjectTypeInfo> Result;
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	if (!Definition || (!SlotId.IsEmpty() && !FindSlot(Definition, SlotId))) return Result;
	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		UScriptStruct* Struct = *It;
		if (!Struct || Struct == FSmartObjectDefinitionData::StaticStruct()
			|| !Struct->IsChildOf(FSmartObjectDefinitionData::StaticStruct())
			|| Struct->HasMetaData(TEXT("Hidden")) || Struct->HasMetaData(TEXT("Deprecated"))) continue;
		const bool bAnnotation = Struct->IsChildOf(FSmartObjectSlotAnnotation::StaticStruct());
		FBridgeSmartObjectTypeInfo Info;
		Info.Kind = bAnnotation ? TEXT("Annotation") : TEXT("DefinitionData");
		Info.TypePath = Struct->GetPathName();
		Info.DisplayName = Struct->GetDisplayNameText().ToString();
		Info.bAllowedAtDefinition = !bAnnotation;
		Info.bAllowedAtSlot = true;
		if ((SlotId.IsEmpty() && Info.bAllowedAtDefinition) || (!SlotId.IsEmpty() && Info.bAllowedAtSlot))
			Result.Add(MoveTemp(Info));
	}
	Result.Sort([](const FBridgeSmartObjectTypeInfo& A, const FBridgeSmartObjectTypeInfo& B)
	{
		return A.DisplayName < B.DisplayName;
	});
	return Result;
}

TArray<FBridgeSmartObjectDefinitionDataInfo> UUnrealBridgeSmartObjectLibrary::ListSmartObjectDefinitionData(
	const FString& AssetPath, const FString& SlotId)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	TArray<FBridgeSmartObjectDefinitionDataInfo> Result;
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FDefinitionDataArray* Data = nullptr;
	FName Root;
	if (!Definition || !GetDefinitionDataArrayForSlot(Definition, SlotId, Data, Root)) return Result;
	for (int32 Index = 0; Index < Data->Num(); ++Index)
	{
		FSmartObjectDefinitionDataProxy& Proxy = (*Data)[Index];
		const UScriptStruct* Struct = Proxy.Data.GetScriptStruct();
		if (!Struct) continue;
		FBridgeSmartObjectDefinitionDataInfo Info;
		Info.SlotId = SlotId;
		Info.Id = GuidToString(Proxy.ID);
		Info.Index = Index;
		Info.TypePath = Struct->GetPathName();
		Info.DisplayName = Struct->GetDisplayNameText().ToString();
		Info.bAnnotation = Struct->IsChildOf(FSmartObjectSlotAnnotation::StaticStruct());
		if (Info.bAnnotation)
		{
			const FSmartObjectSlotAnnotation* Annotation = reinterpret_cast<const FSmartObjectSlotAnnotation*>(Proxy.Data.GetMemory());
			Info.bHasTransform = Annotation && Annotation->HasTransform();
		}
		Info.PropertyCount = CountProperties(Struct);
		Result.Add(MoveTemp(Info));
	}
	return Result;
}

FString UUnrealBridgeSmartObjectLibrary::AddSmartObjectDefinitionData(const FString& AssetPath,
	const FString& StructTypePath, const FString& SlotId, int32 InsertIndex)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	UScriptStruct* Struct = LoadObject<UScriptStruct>(nullptr, *StructTypePath);
	if (!Definition) return FString();
	if (!Struct || !Struct->IsChildOf(FSmartObjectDefinitionData::StaticStruct())
		|| Struct == FSmartObjectDefinitionData::StaticStruct())
	{
		SetError(FString::Printf(TEXT("'%s' is not a concrete Smart Object definition data struct"), *StructTypePath));
		return FString();
	}
	if (SlotId.IsEmpty() && Struct->IsChildOf(FSmartObjectSlotAnnotation::StaticStruct()))
	{
		SetError(TEXT("slot annotations can only be added to a slot"));
		return FString();
	}
	FDefinitionDataArray* Data = nullptr;
	FName Root;
	if (!GetDefinitionDataArrayForSlot(Definition, SlotId, Data, Root)) return FString();
	FSmartObjectDefinitionDataProxy Proxy;
	Proxy.Data.InitializeAsScriptStruct(Struct);
	Proxy.ID = FGuid::NewGuid();
	const FGuid NewId = Proxy.ID;
	const int32 TargetIndex = InsertIndex < 0 ? Data->Num() : FMath::Clamp(InsertIndex, 0, Data->Num());
	FScopedTransaction Transaction(LOCTEXT("AddSmartObjectDefinitionData", "UnrealBridge: Add Smart Object Definition Data"));
	Definition->Modify();
	Data->Insert(MoveTemp(Proxy), TargetIndex);
	NotifyDefinitionChanged(Definition, Root);
	return GuidToString(NewId);
}

bool UUnrealBridgeSmartObjectLibrary::RemoveSmartObjectDefinitionData(
	const FString& AssetPath, const FString& DataId)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FDefinitionDataLocation Location = FindDefinitionData(Definition, DataId);
	if (!Location.Proxy) return false;
	FScopedTransaction Transaction(LOCTEXT("RemoveSmartObjectDefinitionData", "UnrealBridge: Remove Smart Object Definition Data"));
	Definition->Modify();
	Location.Array->RemoveAt(Location.Index);
	NotifyDefinitionChanged(Definition, Location.RootProperty);
	return true;
}

bool UUnrealBridgeSmartObjectLibrary::MoveSmartObjectDefinitionData(
	const FString& AssetPath, const FString& DataId, int32 NewIndex)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FDefinitionDataLocation Location = FindDefinitionData(Definition, DataId);
	if (!Location.Proxy) return false;
	if (!Location.Array->IsValidIndex(NewIndex)) return SetError(TEXT("new definition data index is out of range"));
	if (Location.Index == NewIndex) return true;
	FScopedTransaction Transaction(LOCTEXT("MoveSmartObjectDefinitionData", "UnrealBridge: Move Smart Object Definition Data"));
	Definition->Modify();
	FSmartObjectDefinitionDataProxy Proxy = MoveTemp((*Location.Array)[Location.Index]);
	Location.Array->RemoveAt(Location.Index);
	Location.Array->Insert(MoveTemp(Proxy), NewIndex);
	NotifyDefinitionChanged(Definition, Location.RootProperty);
	return true;
}

TArray<FBridgeSmartObjectPropertyInfo> UUnrealBridgeSmartObjectLibrary::ListSmartObjectDefinitionDataProperties(
	const FString& AssetPath, const FString& DataId, bool bIncludeInherited)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FDefinitionDataLocation Location = FindDefinitionData(Definition, DataId);
	return Location.Proxy ? ListProperties(FPropertyBindingDataView(FStructView(
		Location.Proxy->Data.GetScriptStruct(), Location.Proxy->Data.GetMutableMemory())), bIncludeInherited)
		: TArray<FBridgeSmartObjectPropertyInfo>();
}

FBridgeSmartObjectPropertyResult UUnrealBridgeSmartObjectLibrary::GetSmartObjectDefinitionDataProperty(
	const FString& AssetPath, const FString& DataId, const FString& PropertyPath)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FDefinitionDataLocation Location = FindDefinitionData(Definition, DataId);
	if (!Location.Proxy)
	{
		FBridgeSmartObjectPropertyResult Result;
		Result.Error = LastError;
		return Result;
	}
	return ReadProperty(FPropertyBindingDataView(FStructView(
		Location.Proxy->Data.GetScriptStruct(), Location.Proxy->Data.GetMutableMemory())), PropertyPath, Definition);
}

bool UUnrealBridgeSmartObjectLibrary::SetSmartObjectDefinitionDataProperty(const FString& AssetPath,
	const FString& DataId, const FString& PropertyPath, const FString& Value)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FDefinitionDataLocation Location = FindDefinitionData(Definition, DataId);
	if (!Location.Proxy) return false;
	FScopedTransaction Transaction(LOCTEXT("SetSmartObjectDefinitionDataProperty", "UnrealBridge: Set Smart Object Definition Data Property"));
	Definition->Modify();
	if (!WriteProperty(FPropertyBindingDataView(FStructView(
		Location.Proxy->Data.GetScriptStruct(), Location.Proxy->Data.GetMutableMemory())),
		PropertyPath, Value, Definition)) return false;
	NotifyDefinitionChanged(Definition, Location.RootProperty);
	return true;
}

namespace BridgeSmartObjectImpl
{

static TArray<FWorldConditionEditable>* GetEditableConditions(FWorldConditionQueryDefinition& Query)
{
	FArrayProperty* Property = FindFProperty<FArrayProperty>(FWorldConditionQueryDefinition::StaticStruct(), TEXT("EditableConditions"));
	return Property ? Property->ContainerPtrToValuePtr<TArray<FWorldConditionEditable>>(&Query) : nullptr;
}

static bool GetConditionQuery(USmartObjectDefinition* Definition, const FString& SlotId,
	FWorldConditionQueryDefinition*& OutQuery, FName& OutRootProperty)
{
	if (SlotId.IsEmpty())
	{
		OutQuery = &Definition->GetMutablePreconditions();
		OutRootProperty = TEXT("Preconditions");
		return true;
	}
	FSmartObjectSlotDefinition* Slot = FindSlot(Definition, SlotId);
	if (!Slot) return false;
	OutQuery = &Slot->SelectionPreconditions;
	OutRootProperty = TEXT("Slots");
	return true;
}

static bool ReinitializeConditions(USmartObjectDefinition* Definition,
	FWorldConditionQueryDefinition& Query, const FName RootProperty)
{
	if (!Query.Initialize(Definition))
	{
		return SetError(TEXT("World Condition query initialization failed; inspect the selected condition types and properties"));
	}
	NotifyDefinitionChanged(Definition, RootProperty);
	return true;
}

struct FParsedBagType
{
	EPropertyBagContainerType Container = EPropertyBagContainerType::None;
	EPropertyBagPropertyType ValueType = EPropertyBagPropertyType::None;
	UObject* TypeObject = nullptr;
};

static UObject* LoadBagTypeObject(const FString& Path, EPropertyBagPropertyType Type)
{
	if (Type == EPropertyBagPropertyType::Enum) return LoadObject<UEnum>(nullptr, *Path);
	if (Type == EPropertyBagPropertyType::Struct) return LoadObject<UScriptStruct>(nullptr, *Path);
	if (Type == EPropertyBagPropertyType::Object || Type == EPropertyBagPropertyType::SoftObject
		|| Type == EPropertyBagPropertyType::Class || Type == EPropertyBagPropertyType::SoftClass)
		return LoadObject<UClass>(nullptr, *Path);
	return nullptr;
}

static bool ParseBagType(FString Text, FParsedBagType& Out)
{
	Text.TrimStartAndEndInline();
	if ((Text.StartsWith(TEXT("Array<"), ESearchCase::IgnoreCase)
		|| Text.StartsWith(TEXT("Set<"), ESearchCase::IgnoreCase)) && Text.EndsWith(TEXT(">")))
	{
		Out.Container = Text.StartsWith(TEXT("Array<"), ESearchCase::IgnoreCase)
			? EPropertyBagContainerType::Array : EPropertyBagContainerType::Set;
		const int32 Open = Text.Find(TEXT("<"));
		Text = Text.Mid(Open + 1, Text.Len() - Open - 2);
		Text.TrimStartAndEndInline();
	}

	FString Prefix = Text;
	FString ObjectPath;
	if (Text.Split(TEXT(":"), &Prefix, &ObjectPath, ESearchCase::IgnoreCase, ESearchDir::FromStart))
	{
		Prefix.TrimStartAndEndInline();
		ObjectPath.TrimStartAndEndInline();
	}
	else ObjectPath.Reset();
	const FString Key = NormalizeToken(Prefix);
	if (Key == TEXT("bool") || Key == TEXT("boolean")) Out.ValueType = EPropertyBagPropertyType::Bool;
	else if (Key == TEXT("byte")) Out.ValueType = EPropertyBagPropertyType::Byte;
	else if (Key == TEXT("int") || Key == TEXT("int32")) Out.ValueType = EPropertyBagPropertyType::Int32;
	else if (Key == TEXT("int64")) Out.ValueType = EPropertyBagPropertyType::Int64;
	else if (Key == TEXT("uint32")) Out.ValueType = EPropertyBagPropertyType::UInt32;
	else if (Key == TEXT("uint64")) Out.ValueType = EPropertyBagPropertyType::UInt64;
	else if (Key == TEXT("float")) Out.ValueType = EPropertyBagPropertyType::Float;
	else if (Key == TEXT("double")) Out.ValueType = EPropertyBagPropertyType::Double;
	else if (Key == TEXT("name")) Out.ValueType = EPropertyBagPropertyType::Name;
	else if (Key == TEXT("string")) Out.ValueType = EPropertyBagPropertyType::String;
	else if (Key == TEXT("text")) Out.ValueType = EPropertyBagPropertyType::Text;
	else if (Key == TEXT("enum")) Out.ValueType = EPropertyBagPropertyType::Enum;
	else if (Key == TEXT("struct")) Out.ValueType = EPropertyBagPropertyType::Struct;
	else if (Key == TEXT("object")) Out.ValueType = EPropertyBagPropertyType::Object;
	else if (Key == TEXT("softobject")) Out.ValueType = EPropertyBagPropertyType::SoftObject;
	else if (Key == TEXT("class")) Out.ValueType = EPropertyBagPropertyType::Class;
	else if (Key == TEXT("softclass")) Out.ValueType = EPropertyBagPropertyType::SoftClass;
	else return SetError(FString::Printf(TEXT("unknown property bag type '%s'"), *Text));

	const bool bNeedsObject = Out.ValueType == EPropertyBagPropertyType::Enum
		|| Out.ValueType == EPropertyBagPropertyType::Struct
		|| Out.ValueType == EPropertyBagPropertyType::Object
		|| Out.ValueType == EPropertyBagPropertyType::SoftObject
		|| Out.ValueType == EPropertyBagPropertyType::Class
		|| Out.ValueType == EPropertyBagPropertyType::SoftClass;
	if (bNeedsObject)
	{
		if (ObjectPath.IsEmpty())
			return SetError(FString::Printf(TEXT("type '%s' requires ':' followed by a reflected type path"), *Prefix));
		Out.TypeObject = LoadBagTypeObject(ObjectPath, Out.ValueType);
		if (!Out.TypeObject)
			return SetError(FString::Printf(TEXT("could not load property bag type object '%s'"), *ObjectPath));
	}
	return true;
}

static FString BagTypeToString(const FPropertyBagPropertyDesc& Desc)
{
	FString Base = EnumToString(Desc.ValueType);
	if (Desc.ValueTypeObject) Base += TEXT(":") + Desc.ValueTypeObject->GetPathName();
	for (int32 Index = static_cast<int32>(Desc.ContainerTypes.Num()) - 1; Index >= 0; --Index)
	{
		Base = EnumToString(Desc.ContainerTypes[Index]) + TEXT("<") + Base + TEXT(">");
	}
	return Base;
}

static bool IsBindableId(USmartObjectDefinition* Definition, const FGuid& Id)
{
	if (Id == GetGuidProperty(Definition, TEXT("RootID"))
		|| Id == GetGuidProperty(Definition, TEXT("ParametersID"))) return true;
	for (const FSmartObjectSlotDefinition& Slot : Definition->GetSlots())
	{
		if (Slot.ID == Id) return true;
		for (const FSmartObjectDefinitionDataProxy& Proxy : Slot.DefinitionData)
		{
			if (Proxy.ID == Id) return true;
		}
	}
	return false;
}

} // namespace BridgeSmartObjectImpl

TArray<FBridgeSmartObjectTypeInfo> UUnrealBridgeSmartObjectLibrary::ListSmartObjectWorldConditionTypes(
	const FString& AssetPath, const FString& SlotId)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	TArray<FBridgeSmartObjectTypeInfo> Result;
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FWorldConditionQueryDefinition* Query = nullptr;
	FName Root;
	if (!Definition || !GetConditionQuery(Definition, SlotId, Query, Root)) return Result;
	const TSubclassOf<UWorldConditionSchema> SchemaClass = Query->GetSchemaClass();
	const UWorldConditionSchema* Schema = SchemaClass ? SchemaClass->GetDefaultObject<UWorldConditionSchema>() : nullptr;
	if (!Schema)
	{
		SetError(TEXT("Smart Object World Condition schema is unavailable"));
		return Result;
	}
	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		UScriptStruct* Struct = *It;
		if (!Struct || Struct == FWorldConditionBase::StaticStruct()
			|| !Struct->IsChildOf(FWorldConditionBase::StaticStruct())
			|| Struct->HasMetaData(TEXT("Hidden")) || Struct->HasMetaData(TEXT("Deprecated"))) continue;
		FBridgeSmartObjectTypeInfo Info;
		Info.Kind = TEXT("WorldCondition");
		Info.TypePath = Struct->GetPathName();
		Info.DisplayName = Struct->GetDisplayNameText().ToString();
		Info.bAllowedBySchema = Schema->IsStructAllowed(Struct);
		Info.bAllowedAtDefinition = Info.bAllowedBySchema;
		Info.bAllowedAtSlot = Info.bAllowedBySchema;
		if (Info.bAllowedBySchema) Result.Add(MoveTemp(Info));
	}
	Result.Sort([](const FBridgeSmartObjectTypeInfo& A, const FBridgeSmartObjectTypeInfo& B)
	{
		return A.DisplayName < B.DisplayName;
	});
	return Result;
}

TArray<FBridgeSmartObjectWorldConditionInfo> UUnrealBridgeSmartObjectLibrary::ListSmartObjectWorldConditions(
	const FString& AssetPath, const FString& SlotId)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	TArray<FBridgeSmartObjectWorldConditionInfo> Result;
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FWorldConditionQueryDefinition* Query = nullptr;
	FName Root;
	if (!Definition || !GetConditionQuery(Definition, SlotId, Query, Root)) return Result;
	TArray<FWorldConditionEditable>* Conditions = GetEditableConditions(*Query);
	if (!Conditions) return Result;
	for (int32 Index = 0; Index < Conditions->Num(); ++Index)
	{
		const FWorldConditionEditable& Condition = (*Conditions)[Index];
		const UScriptStruct* Struct = Condition.Condition.GetScriptStruct();
		FBridgeSmartObjectWorldConditionInfo Info;
		Info.SlotId = SlotId;
		Info.Index = Index;
		Info.TypePath = GetPathNameSafe(Struct);
		Info.DisplayName = Struct ? Struct->GetDisplayNameText().ToString() : TEXT("None");
		Info.Operator = EnumToString(Condition.Operator);
		Info.ExpressionDepth = Condition.ExpressionDepth;
		Info.bInvert = Condition.bInvert;
		Info.PropertyCount = CountProperties(Struct);
		Result.Add(MoveTemp(Info));
	}
	return Result;
}

int32 UUnrealBridgeSmartObjectLibrary::AddSmartObjectWorldCondition(const FString& AssetPath,
	const FString& ConditionStructPath, const FString& SlotId, const FString& Operator,
	int32 ExpressionDepth, bool bInvert, int32 InsertIndex)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FWorldConditionQueryDefinition* Query = nullptr;
	FName Root;
	if (!Definition || !GetConditionQuery(Definition, SlotId, Query, Root)) return INDEX_NONE;
	UScriptStruct* Struct = LoadObject<UScriptStruct>(nullptr, *ConditionStructPath);
	if (!Struct || Struct == FWorldConditionBase::StaticStruct()
		|| !Struct->IsChildOf(FWorldConditionBase::StaticStruct()))
	{
		SetError(FString::Printf(TEXT("'%s' is not a concrete World Condition struct"), *ConditionStructPath));
		return INDEX_NONE;
	}
	const TSubclassOf<UWorldConditionSchema> SchemaClass = Query->GetSchemaClass();
	const UWorldConditionSchema* Schema = SchemaClass ? SchemaClass->GetDefaultObject<UWorldConditionSchema>() : nullptr;
	if (!Schema || !Schema->IsStructAllowed(Struct))
	{
		SetError(FString::Printf(TEXT("schema '%s' does not allow condition '%s'"), *GetNameSafe(Schema), *ConditionStructPath));
		return INDEX_NONE;
	}
	EWorldConditionOperator ParsedOperator;
	if (!ParseEnumToken(Operator, ParsedOperator, TEXT("World Condition operator"))) return INDEX_NONE;
	TArray<FWorldConditionEditable>* Conditions = GetEditableConditions(*Query);
	if (!Conditions) return SetError(TEXT("editable World Condition array is unavailable")), INDEX_NONE;
	FWorldConditionEditable NewCondition;
	NewCondition.ExpressionDepth = static_cast<uint8>(FMath::Clamp(ExpressionDepth, 0, 255));
	NewCondition.Operator = ParsedOperator;
	NewCondition.bInvert = bInvert;
	NewCondition.Condition.InitializeAs(Struct);
	const int32 TargetIndex = InsertIndex < 0 ? Conditions->Num() : FMath::Clamp(InsertIndex, 0, Conditions->Num());
	FScopedTransaction Transaction(LOCTEXT("AddSmartObjectWorldCondition", "UnrealBridge: Add Smart Object World Condition"));
	Definition->Modify();
	const FWorldConditionQueryDefinition Backup = *Query;
	Conditions->Insert(MoveTemp(NewCondition), TargetIndex);
	if (!ReinitializeConditions(Definition, *Query, Root))
	{
		*Query = Backup;
		return INDEX_NONE;
	}
	return TargetIndex;
}

bool UUnrealBridgeSmartObjectLibrary::RemoveSmartObjectWorldCondition(const FString& AssetPath,
	const FString& SlotId, int32 ConditionIndex)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FWorldConditionQueryDefinition* Query = nullptr;
	FName Root;
	if (!Definition || !GetConditionQuery(Definition, SlotId, Query, Root)) return false;
	TArray<FWorldConditionEditable>* Conditions = GetEditableConditions(*Query);
	if (!Conditions || !Conditions->IsValidIndex(ConditionIndex)) return SetError(TEXT("World Condition index is out of range"));
	FScopedTransaction Transaction(LOCTEXT("RemoveSmartObjectWorldCondition", "UnrealBridge: Remove Smart Object World Condition"));
	Definition->Modify();
	const FWorldConditionQueryDefinition Backup = *Query;
	Conditions->RemoveAt(ConditionIndex);
	if (!ReinitializeConditions(Definition, *Query, Root))
	{
		*Query = Backup;
		return false;
	}
	return true;
}

bool UUnrealBridgeSmartObjectLibrary::MoveSmartObjectWorldCondition(const FString& AssetPath,
	const FString& SlotId, int32 ConditionIndex, int32 NewIndex)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FWorldConditionQueryDefinition* Query = nullptr;
	FName Root;
	if (!Definition || !GetConditionQuery(Definition, SlotId, Query, Root)) return false;
	TArray<FWorldConditionEditable>* Conditions = GetEditableConditions(*Query);
	if (!Conditions || !Conditions->IsValidIndex(ConditionIndex) || !Conditions->IsValidIndex(NewIndex))
		return SetError(TEXT("World Condition index is out of range"));
	if (ConditionIndex == NewIndex) return true;
	FScopedTransaction Transaction(LOCTEXT("MoveSmartObjectWorldCondition", "UnrealBridge: Move Smart Object World Condition"));
	Definition->Modify();
	const FWorldConditionQueryDefinition Backup = *Query;
	FWorldConditionEditable Condition = MoveTemp((*Conditions)[ConditionIndex]);
	Conditions->RemoveAt(ConditionIndex);
	Conditions->Insert(MoveTemp(Condition), NewIndex);
	if (!ReinitializeConditions(Definition, *Query, Root))
	{
		*Query = Backup;
		return false;
	}
	return true;
}

bool UUnrealBridgeSmartObjectLibrary::SetSmartObjectWorldConditionExpression(const FString& AssetPath,
	const FString& SlotId, int32 ConditionIndex, const FString& Operator, int32 ExpressionDepth, bool bInvert)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FWorldConditionQueryDefinition* Query = nullptr;
	FName Root;
	EWorldConditionOperator ParsedOperator;
	if (!Definition || !GetConditionQuery(Definition, SlotId, Query, Root)
		|| !ParseEnumToken(Operator, ParsedOperator, TEXT("World Condition operator"))) return false;
	TArray<FWorldConditionEditable>* Conditions = GetEditableConditions(*Query);
	if (!Conditions || !Conditions->IsValidIndex(ConditionIndex)) return SetError(TEXT("World Condition index is out of range"));
	FScopedTransaction Transaction(LOCTEXT("SetSmartObjectWorldConditionExpression", "UnrealBridge: Set Smart Object World Condition Expression"));
	Definition->Modify();
	const FWorldConditionQueryDefinition Backup = *Query;
	FWorldConditionEditable& Condition = (*Conditions)[ConditionIndex];
	Condition.Operator = ParsedOperator;
	Condition.ExpressionDepth = static_cast<uint8>(FMath::Clamp(ExpressionDepth, 0, 255));
	Condition.bInvert = bInvert;
	if (!ReinitializeConditions(Definition, *Query, Root))
	{
		*Query = Backup;
		return false;
	}
	return true;
}

TArray<FBridgeSmartObjectPropertyInfo> UUnrealBridgeSmartObjectLibrary::ListSmartObjectWorldConditionProperties(
	const FString& AssetPath, const FString& SlotId, int32 ConditionIndex, bool bIncludeInherited)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FWorldConditionQueryDefinition* Query = nullptr;
	FName Root;
	if (!Definition || !GetConditionQuery(Definition, SlotId, Query, Root)) return {};
	TArray<FWorldConditionEditable>* Conditions = GetEditableConditions(*Query);
	if (!Conditions || !Conditions->IsValidIndex(ConditionIndex))
	{
		SetError(TEXT("World Condition index is out of range"));
		return {};
	}
	return ListProperties(FPropertyBindingDataView((*Conditions)[ConditionIndex].Condition), bIncludeInherited);
}

FBridgeSmartObjectPropertyResult UUnrealBridgeSmartObjectLibrary::GetSmartObjectWorldConditionProperty(
	const FString& AssetPath, const FString& SlotId, int32 ConditionIndex, const FString& PropertyPath)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	FBridgeSmartObjectPropertyResult Result;
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FWorldConditionQueryDefinition* Query = nullptr;
	FName Root;
	if (!Definition || !GetConditionQuery(Definition, SlotId, Query, Root))
	{
		Result.Error = LastError;
		return Result;
	}
	TArray<FWorldConditionEditable>* Conditions = GetEditableConditions(*Query);
	if (!Conditions || !Conditions->IsValidIndex(ConditionIndex))
	{
		SetError(TEXT("World Condition index is out of range"));
		Result.Error = LastError;
		return Result;
	}
	return ReadProperty(FPropertyBindingDataView((*Conditions)[ConditionIndex].Condition), PropertyPath, Definition);
}

bool UUnrealBridgeSmartObjectLibrary::SetSmartObjectWorldConditionProperty(const FString& AssetPath,
	const FString& SlotId, int32 ConditionIndex, const FString& PropertyPath, const FString& Value)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FWorldConditionQueryDefinition* Query = nullptr;
	FName Root;
	if (!Definition || !GetConditionQuery(Definition, SlotId, Query, Root)) return false;
	TArray<FWorldConditionEditable>* Conditions = GetEditableConditions(*Query);
	if (!Conditions || !Conditions->IsValidIndex(ConditionIndex)) return SetError(TEXT("World Condition index is out of range"));
	FScopedTransaction Transaction(LOCTEXT("SetSmartObjectWorldConditionProperty", "UnrealBridge: Set Smart Object World Condition Property"));
	Definition->Modify();
	const FWorldConditionQueryDefinition Backup = *Query;
	if (!WriteProperty(FPropertyBindingDataView((*Conditions)[ConditionIndex].Condition), PropertyPath, Value, Definition)) return false;
	if (!ReinitializeConditions(Definition, *Query, Root))
	{
		*Query = Backup;
		return false;
	}
	return true;
}

TArray<FBridgeSmartObjectParameterInfo> UUnrealBridgeSmartObjectLibrary::ListSmartObjectParameters(
	const FString& AssetPath)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	TArray<FBridgeSmartObjectParameterInfo> Result;
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FInstancedPropertyBag* Parameters = Definition ? GetParameters(Definition) : nullptr;
	const UPropertyBag* Bag = Parameters ? Parameters->GetPropertyBagStruct() : nullptr;
	if (!Definition || !Bag) return Result;
	for (const FPropertyBagPropertyDesc& Desc : Bag->GetPropertyDescs())
	{
		FBridgeSmartObjectParameterInfo Info;
		Info.Id = GuidToString(Desc.ID);
		Info.Name = Desc.Name.ToString();
		Info.Type = BagTypeToString(Desc);
		const TValueOrError<FString, EPropertyBagResult> Value = Parameters->GetValueSerializedString(Desc.Name);
		if (Value.HasValue()) Info.Value = Value.GetValue();
		Result.Add(MoveTemp(Info));
	}
	return Result;
}

FString UUnrealBridgeSmartObjectLibrary::AddSmartObjectParameter(const FString& AssetPath,
	const FString& Name, const FString& Type, const FString& DefaultValue)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	if (Name.TrimStartAndEnd().IsEmpty()) return SetError(TEXT("parameter name is empty")), FString();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FInstancedPropertyBag* Parameters = Definition ? GetParameters(Definition) : nullptr;
	if (!Definition || !Parameters) return FString();
	FParsedBagType Parsed;
	if (!ParseBagType(Type, Parsed)) return FString();
	const FName ParameterName(*Name.TrimStartAndEnd());
	if (Parameters->FindPropertyDescByName(ParameterName))
		return SetError(FString::Printf(TEXT("parameter '%s' already exists"), *Name)), FString();
	FScopedTransaction Transaction(LOCTEXT("AddSmartObjectParameter", "UnrealBridge: Add Smart Object Parameter"));
	Definition->Modify();
	const EPropertyBagAlterationResult AddResult = Parsed.Container == EPropertyBagContainerType::None
		? Parameters->AddProperty(ParameterName, Parsed.ValueType, Parsed.TypeObject, false)
		: Parameters->AddContainerProperty(ParameterName, Parsed.Container, Parsed.ValueType, Parsed.TypeObject, false);
	if (AddResult != EPropertyBagAlterationResult::Success)
		return SetError(FString::Printf(TEXT("failed to add parameter '%s': %s"), *Name, *EnumToString(AddResult))), FString();
	if (!DefaultValue.IsEmpty() && Parameters->SetValueSerializedString(ParameterName, DefaultValue) != EPropertyBagResult::Success)
	{
		Parameters->RemovePropertyByName(ParameterName);
		return SetError(FString::Printf(TEXT("could not import default value '%s' for parameter '%s'"),
			*DefaultValue, *Name)), FString();
	}
	NotifyDefinitionChanged(Definition, TEXT("Parameters"));
	const FPropertyBagPropertyDesc* Desc = Parameters->FindPropertyDescByName(ParameterName);
	return Desc ? GuidToString(Desc->ID) : FString();
}

bool UUnrealBridgeSmartObjectLibrary::RemoveSmartObjectParameter(const FString& AssetPath, const FString& Name)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FInstancedPropertyBag* Parameters = Definition ? GetParameters(Definition) : nullptr;
	if (!Definition || !Parameters) return false;
	if (!Parameters->FindPropertyDescByName(FName(*Name)))
		return SetError(FString::Printf(TEXT("parameter '%s' was not found"), *Name));
	FScopedTransaction Transaction(LOCTEXT("RemoveSmartObjectParameter", "UnrealBridge: Remove Smart Object Parameter"));
	Definition->Modify();
	if (Parameters->RemovePropertyByName(FName(*Name)) != EPropertyBagAlterationResult::Success)
		return SetError(FString::Printf(TEXT("failed to remove parameter '%s'"), *Name));
	NotifyDefinitionChanged(Definition, TEXT("Parameters"));
	return true;
}

bool UUnrealBridgeSmartObjectLibrary::RenameSmartObjectParameter(const FString& AssetPath,
	const FString& OldName, const FString& NewName)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	if (NewName.TrimStartAndEnd().IsEmpty()) return SetError(TEXT("new parameter name is empty"));
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FInstancedPropertyBag* Parameters = Definition ? GetParameters(Definition) : nullptr;
	if (!Definition || !Parameters) return false;
	FScopedTransaction Transaction(LOCTEXT("RenameSmartObjectParameter", "UnrealBridge: Rename Smart Object Parameter"));
	Definition->Modify();
	const EPropertyBagAlterationResult RenameResult = Parameters->RenameProperty(
		FName(*OldName), FName(*NewName.TrimStartAndEnd()));
	if (RenameResult != EPropertyBagAlterationResult::Success)
		return SetError(FString::Printf(TEXT("failed to rename parameter '%s' to '%s': %s"),
			*OldName, *NewName, *EnumToString(RenameResult)));
	NotifyDefinitionChanged(Definition, TEXT("Parameters"));
	return true;
}

bool UUnrealBridgeSmartObjectLibrary::SetSmartObjectParameterValue(const FString& AssetPath,
	const FString& Name, const FString& Value)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FInstancedPropertyBag* Parameters = Definition ? GetParameters(Definition) : nullptr;
	if (!Definition || !Parameters) return false;
	if (!Parameters->FindPropertyDescByName(FName(*Name)))
		return SetError(FString::Printf(TEXT("parameter '%s' was not found"), *Name));
	FScopedTransaction Transaction(LOCTEXT("SetSmartObjectParameter", "UnrealBridge: Set Smart Object Parameter"));
	Definition->Modify();
	if (Parameters->SetValueSerializedString(FName(*Name), Value) != EPropertyBagResult::Success)
		return SetError(FString::Printf(TEXT("could not import '%s' for parameter '%s'"), *Value, *Name));
	NotifyDefinitionChanged(Definition, TEXT("Parameters"));
	return true;
}

TArray<FBridgeSmartObjectBindableStructInfo> UUnrealBridgeSmartObjectLibrary::ListSmartObjectBindableStructs(
	const FString& AssetPath)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	TArray<FBridgeSmartObjectBindableStructInfo> Result;
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	if (!Definition) return Result;
	auto Add = [&](const FString& Kind, const FGuid& Id, const FString& Name, const UStruct* Struct)
	{
		FBridgeSmartObjectBindableStructInfo Info;
		Info.Kind = Kind;
		Info.Id = GuidToString(Id);
		Info.Name = Name;
		Info.TypePath = GetPathNameSafe(Struct);
		Result.Add(MoveTemp(Info));
	};
	Add(TEXT("Root"), GetGuidProperty(Definition, TEXT("RootID")), TEXT("Root"), Definition->GetClass());
	const FInstancedPropertyBag* Parameters = GetParameters(Definition);
	Add(TEXT("Parameters"), GetGuidProperty(Definition, TEXT("ParametersID")), TEXT("Parameters"),
		Parameters ? Parameters->GetPropertyBagStruct() : nullptr);
	for (const FSmartObjectSlotDefinition& Slot : Definition->GetSlots())
	{
		Add(TEXT("Slot"), Slot.ID, Slot.Name.ToString(), FSmartObjectSlotDefinition::StaticStruct());
		for (const FSmartObjectDefinitionDataProxy& Proxy : Slot.DefinitionData)
		{
			const UScriptStruct* Struct = Proxy.Data.GetScriptStruct();
			Add(TEXT("DefinitionData"), Proxy.ID,
				Slot.Name.ToString() + TEXT(" ") + (Struct ? Struct->GetDisplayNameText().ToString() : TEXT("None")), Struct);
		}
	}
	return Result;
}

TArray<FBridgeSmartObjectBindingInfo> UUnrealBridgeSmartObjectLibrary::ListSmartObjectBindings(
	const FString& AssetPath)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	TArray<FBridgeSmartObjectBindingInfo> Result;
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FSmartObjectBindingCollection* Collection = Definition ? GetBindingCollection(Definition) : nullptr;
	if (!Definition || !Collection) return Result;
	Collection->ForEachBinding([&](const FPropertyBindingBinding& Binding)
	{
		FBridgeSmartObjectBindingInfo Info;
		Info.SourceId = GuidToString(Binding.GetSourcePath().GetStructID());
		Info.SourcePath = Binding.GetSourcePath().ToString();
		Info.TargetId = GuidToString(Binding.GetTargetPath().GetStructID());
		Info.TargetPath = Binding.GetTargetPath().ToString();
		Result.Add(MoveTemp(Info));
	});
	return Result;
}

bool UUnrealBridgeSmartObjectLibrary::AddSmartObjectBinding(const FString& AssetPath,
	const FString& SourceId, const FString& SourcePath, const FString& TargetId, const FString& TargetPath)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FSmartObjectBindingCollection* Collection = Definition ? GetBindingCollection(Definition) : nullptr;
	FGuid SourceGuid;
	FGuid TargetGuid;
	if (!Definition || !Collection || !ParseGuid(SourceId, SourceGuid, TEXT("binding source"))
		|| !ParseGuid(TargetId, TargetGuid, TEXT("binding target"))) return false;
	if (!IsBindableId(Definition, SourceGuid) || !IsBindableId(Definition, TargetGuid))
		return SetError(TEXT("binding source or target GUID is not a bindable Smart Object item"));
	FPropertyBindingPath Source(SourceGuid);
	FPropertyBindingPath Target(TargetGuid);
	if (!Source.FromString(SourcePath) || !Target.FromString(TargetPath))
		return SetError(TEXT("binding source or target property path could not be parsed"));
	FScopedTransaction Transaction(LOCTEXT("AddSmartObjectBinding", "UnrealBridge: Add Smart Object Binding"));
	Definition->Modify();
	Collection->AddBinding(Source, Target);
	if (!Collection->HasBinding(Target)) return SetError(TEXT("Smart Object binding collection rejected the binding"));
	NotifyDefinitionChanged(Definition, TEXT("BindingCollection"));
	return true;
}

bool UUnrealBridgeSmartObjectLibrary::RemoveSmartObjectBinding(const FString& AssetPath,
	const FString& TargetId, const FString& TargetPath)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	FSmartObjectBindingCollection* Collection = Definition ? GetBindingCollection(Definition) : nullptr;
	FGuid TargetGuid;
	if (!Definition || !Collection || !ParseGuid(TargetId, TargetGuid, TEXT("binding target"))) return false;
	FPropertyBindingPath Target(TargetGuid);
	if (!Target.FromString(TargetPath)) return SetError(TEXT("binding target property path could not be parsed"));
	if (!Collection->HasBinding(Target)) return SetError(TEXT("binding target was not found"));
	FScopedTransaction Transaction(LOCTEXT("RemoveSmartObjectBinding", "UnrealBridge: Remove Smart Object Binding"));
	Definition->Modify();
	Collection->RemoveBindings(Target);
	NotifyDefinitionChanged(Definition, TEXT("BindingCollection"));
	return true;
}

namespace BridgeSmartObjectImpl
{

static FString RegistrationTypeToString(ESmartObjectRegistrationType Type)
{
	switch (Type)
	{
	case ESmartObjectRegistrationType::NotRegistered: return TEXT("NotRegistered");
	case ESmartObjectRegistrationType::BindToExistingInstance: return TEXT("BindToExistingInstance");
	case ESmartObjectRegistrationType::Dynamic: return TEXT("Dynamic");
	default: return TEXT("Unknown");
	}
}

static FBridgeSmartObjectComponentInfo MakeComponentInfo(USmartObjectComponent* Component)
{
	FBridgeSmartObjectComponentInfo Info;
	if (!Component) return Info;
	Info.ComponentPath = Component->GetPathName();
	Info.ComponentName = Component->GetName();
	if (AActor* Owner = Component->GetOwner())
	{
		Info.OwnerPath = Owner->GetPathName();
		Info.OwnerLabel = Owner->GetActorLabel();
	}
	Info.WorldType = WorldTypeToString(Component->GetWorld());
	Info.BaseDefinitionPath = GetPathNameSafe(Component->GetBaseDefinition());
	Info.AppliedDefinitionPath = GetPathNameSafe(Component->GetDefinition());
	Info.RegisteredHandle = Component->GetRegisteredHandle().IsValid()
		? LexToString(Component->GetRegisteredHandle()) : FString();
	Info.RegistrationType = RegistrationTypeToString(Component->GetRegistrationType());
	Info.Transform = Component->GetComponentTransform();
	const FBox Bounds = Component->GetSmartObjectBounds();
	if (Bounds.IsValid)
	{
		Info.BoundsMin = Bounds.Min;
		Info.BoundsMax = Bounds.Max;
	}
	Info.bBoundToSimulation = Component->IsBoundToSimulation();
	Info.bEnabled = Info.bBoundToSimulation && Component->IsSmartObjectEnabled();
	Info.bCanBePartOfCollection = Component->GetCanBePartOfCollection();
	return Info;
}

static bool IsCollectionRegistered(const ASmartObjectPersistentCollection* Collection)
{
	if (!Collection) return false;
#if WITH_EDITORONLY_DATA
	if (const USmartObjectSubsystem* Subsystem = USmartObjectSubsystem::GetCurrent(Collection->GetWorld()))
	{
		for (const TWeakObjectPtr<ASmartObjectPersistentCollection>& Registered : Subsystem->GetRegisteredCollections())
		{
			if (Registered.Get() == Collection) return true;
		}
	}
#endif
	return false;
}

static bool InvokeCollectionEditorAction(ASmartObjectPersistentCollection* Collection, const FName FunctionName)
{
	if (!Collection) return false;
	UFunction* Function = Collection->FindFunction(FunctionName);
	if (!Function)
		return SetError(FString::Printf(TEXT("collection editor action '%s' is unavailable"), *FunctionName.ToString()));
	Collection->ProcessEvent(Function, nullptr);
	return true;
}

static bool RegisterCollection(ASmartObjectPersistentCollection* Collection)
{
	if (!Collection) return false;
	if (IsCollectionRegistered(Collection)) return true;
	USmartObjectSubsystem* Subsystem = USmartObjectSubsystem::GetCurrent(Collection->GetWorld());
	if (!Subsystem) return SetError(TEXT("Smart Object subsystem is unavailable for the collection world"));
	const ESmartObjectCollectionRegistrationResult Result = Subsystem->RegisterCollection(*Collection);
	if (Result == ESmartObjectCollectionRegistrationResult::Succeeded) return true;
	return SetError(FString::Printf(TEXT("collection registration failed: %s"), *EnumToString(Result)));
}

static bool UnregisterCollection(ASmartObjectPersistentCollection* Collection)
{
	if (!Collection) return false;
	if (!IsCollectionRegistered(Collection)) return true;
	USmartObjectSubsystem* Subsystem = USmartObjectSubsystem::GetCurrent(Collection->GetWorld());
	if (!Subsystem) return SetError(TEXT("Smart Object subsystem is unavailable for the collection world"));
	Subsystem->UnregisterCollection(*Collection);
	return true;
}

static FBridgeSmartObjectCollectionInfo MakeCollectionInfo(ASmartObjectPersistentCollection* Collection)
{
	FBridgeSmartObjectCollectionInfo Info;
	if (!Collection) return Info;
	Info.ActorPath = Collection->GetPathName();
	Info.ActorLabel = Collection->GetActorLabel();
	Info.WorldType = WorldTypeToString(Collection->GetWorld());
	Info.EntryCount = Collection->GetEntries().Num();
	const FBox Bounds = Collection->GetBounds();
	if (Bounds.IsValid)
	{
		Info.BoundsMin = Bounds.Min;
		Info.BoundsMax = Bounds.Max;
	}
	Info.bRegistered = IsCollectionRegistered(Collection);
	return Info;
}

static USmartObjectSubsystem* GetSubsystem(UWorld* World)
{
	USmartObjectSubsystem* Subsystem = World ? USmartObjectSubsystem::GetCurrent(World) : nullptr;
	if (!Subsystem) SetError(TEXT("Smart Object subsystem is unavailable in the selected world"));
	return Subsystem;
}

static bool ParseSmartObjectHandle(const FString& Text, FSmartObjectHandle& OutHandle)
{
	FGuid Guid;
	if (!FGuid::Parse(Text, Guid) || !Guid.IsValid())
		return SetError(FString::Printf(TEXT("invalid Smart Object handle '%s'"), *Text));
	OutHandle = FSmartObjectHandleFactory::CreateHandleFromGuid(Guid);
	return true;
}

static bool ParseSlotHandle(USmartObjectSubsystem* Subsystem, const FString& Text,
	FSmartObjectSlotHandle& OutHandle)
{
	int32 Colon = INDEX_NONE;
	if (!Text.FindLastChar(TEXT(':'), Colon) || Colon <= 0)
		return SetError(FString::Printf(TEXT("invalid Smart Object slot handle '%s'"), *Text));
	FSmartObjectHandle ObjectHandle;
	if (!ParseSmartObjectHandle(Text.Left(Colon), ObjectHandle)) return false;
	int32 SlotIndex = INDEX_NONE;
	if (!LexTryParseString(SlotIndex, *Text.Mid(Colon + 1)) || SlotIndex < 0)
		return SetError(FString::Printf(TEXT("invalid slot index in handle '%s'"), *Text));
	TArray<FSmartObjectSlotHandle> Slots;
	Subsystem->GetAllSlots(ObjectHandle, Slots);
	for (const FSmartObjectSlotHandle& Slot : Slots)
	{
		if (Slot.GetSlotIndex() == SlotIndex)
		{
			OutHandle = Slot;
			return true;
		}
	}
	return SetError(FString::Printf(TEXT("slot handle '%s' is not accessible in the selected world"), *Text));
}

static bool BuildActivityQuery(const TArray<FString>& Names, const FString& Match,
	FGameplayTagQuery& OutQuery)
{
	FGameplayTagContainer Tags;
	if (!StringsToTags(Names, Tags)) return false;
	if (Tags.IsEmpty())
	{
		OutQuery = FGameplayTagQuery();
		return true;
	}
	const FString Key = NormalizeToken(Match);
	if (Key == TEXT("all")) OutQuery = FGameplayTagQuery::MakeQuery_MatchAllTags(Tags);
	else if (Key == TEXT("any")) OutQuery = FGameplayTagQuery::MakeQuery_MatchAnyTags(Tags);
	else if (Key == TEXT("none") || Key == TEXT("no")) OutQuery = FGameplayTagQuery::MakeQuery_MatchNoTags(Tags);
	else if (Key == TEXT("exactall")) OutQuery = FGameplayTagQuery::MakeQuery_ExactMatchAllTags(Tags);
	else if (Key == TEXT("exactany")) OutQuery = FGameplayTagQuery::MakeQuery_ExactMatchAnyTags(Tags);
	else return SetError(FString::Printf(TEXT("unknown activity match '%s'; expected All, Any, None, ExactAll, or ExactAny"), *Match));
	return true;
}

static void AppendEffectiveBehaviorClasses(const FSmartObjectSlotDefinition& Slot,
	USmartObjectDefinition& Definition, TArray<FString>& OutClasses)
{
	auto Add = [&](const FBehaviorArray& Behaviors)
	{
		for (const USmartObjectBehaviorDefinition* Behavior : Behaviors)
		{
			if (Behavior) OutClasses.AddUnique(Behavior->GetClass()->GetPathName());
		}
	};
	Add(Slot.BehaviorDefinitions);
	if (FBehaviorArray* Defaults = GetDefaultBehaviors(&Definition)) Add(*Defaults);
	OutClasses.Sort();
}

static FBridgeSmartObjectRuntimeSlotInfo MakeRuntimeSlotInfo(USmartObjectSubsystem* Subsystem,
	const FSmartObjectSlotHandle& SlotHandle, ESmartObjectClaimPriority Priority)
{
	FBridgeSmartObjectRuntimeSlotInfo Info;
	Info.SmartObjectHandle = LexToString(SlotHandle.GetSmartObjectHandle());
	Info.SlotHandle = LexToString(SlotHandle);
	Info.SlotIndex = SlotHandle.GetSlotIndex();
	Info.SlotState = EnumToString(Subsystem->GetSlotState(SlotHandle));
	if (const TOptional<FTransform> Transform = Subsystem->GetSlotTransform(SlotHandle)) Info.SlotTransform = Transform.GetValue();
	Subsystem->ReadSlotData(SlotHandle, [&](FConstSmartObjectSlotView View)
	{
		Info.DefinitionPath = View.GetSmartObjectDefinition().GetPathName();
		FGameplayTagContainer Activity;
		View.GetActivityTags(Activity);
		Info.ActivityTags = TagsToStrings(Activity);
		Info.RuntimeTags = TagsToStrings(View.GetTags());
		Info.bEnabled = View.IsEnabled();
		Info.bCanBeClaimed = View.CanBeClaimed(Priority);
	});
	const FSmartObjectRequestResult RequestResult(SlotHandle.GetSmartObjectHandle(), SlotHandle);
	if (USmartObjectComponent* Component = Subsystem->GetSmartObjectComponentByRequestResult(RequestResult))
	{
		Info.ComponentPath = Component->GetPathName();
		if (AActor* Owner = Component->GetOwner()) Info.OwnerPath = Owner->GetPathName();
	}
	else if (const FSmartObjectActorUserData* OwnerData =
		Subsystem->GetOwnerData(SlotHandle.GetSmartObjectHandle()).GetPtr<const FSmartObjectActorUserData>())
	{
		if (const AActor* Owner = OwnerData->UserActor.Get()) Info.OwnerPath = Owner->GetPathName();
	}
	return Info;
}

struct FStoredClaim
{
	TWeakObjectPtr<UWorld> World;
	FSmartObjectClaimHandle Handle;
	FString UserActorPath;
	FString Priority;
	FString BehaviorObjectPath;
	TSharedPtr<FInstancedStruct> UserData;
};

static TMap<FString, FStoredClaim> StoredClaims;
static TMap<FString, TSharedPtr<FInstancedStruct>> DynamicOwnerData;

static FBridgeSmartObjectClaimResult MakeClaimResult(const FString& Token, const FStoredClaim& Stored,
	USmartObjectSubsystem* Subsystem, bool bSuccess, const FString& Error = FString())
{
	FBridgeSmartObjectClaimResult Result;
	Result.bSuccess = bSuccess;
	Result.ClaimToken = Token;
	Result.SmartObjectHandle = Stored.Handle.IsValid() ? LexToString(Stored.Handle.SmartObjectHandle) : FString();
	Result.SlotHandle = Stored.Handle.IsValid() ? LexToString(Stored.Handle.SlotHandle) : FString();
	Result.UserActorPath = Stored.UserActorPath;
	Result.Priority = Stored.Priority;
	Result.BehaviorObjectPath = Stored.BehaviorObjectPath;
	if (Subsystem && Stored.Handle.IsValid() && Subsystem->IsSmartObjectSlotValid(Stored.Handle.SlotHandle))
		Result.SlotState = EnumToString(Subsystem->GetSlotState(Stored.Handle.SlotHandle));
	Result.Error = Error;
	return Result;
}

static bool BuildEntranceRequest(const FBridgeSmartObjectEntranceRequest& In,
	FSmartObjectSlotEntranceLocationRequest& Out)
{
	Out.UserActor = In.UserActorPath.IsEmpty() ? nullptr : FindActor(In.UserActorPath);
	if (!In.UserActorPath.IsEmpty() && !Out.UserActor) return false;
	if (!In.ValidationFilterClassPath.IsEmpty())
	{
		UClass* FilterClass = LoadObject<UClass>(nullptr, *In.ValidationFilterClassPath);
		if (!FilterClass || !FilterClass->IsChildOf(USmartObjectSlotValidationFilter::StaticClass()))
			return SetError(FString::Printf(TEXT("'%s' is not a Smart Object slot validation filter class"),
				*In.ValidationFilterClassPath));
		Out.ValidationFilter = FilterClass;
	}
	if (In.CapsuleRadius > 0.0f && In.CapsuleHeight > 0.0f && In.CapsuleStepHeight > 0.0f)
	{
		Out.UserCapsuleParams = FSmartObjectUserCapsuleParams(
			In.CapsuleRadius, In.CapsuleHeight, In.CapsuleStepHeight);
	}
	Out.SearchLocation = In.SearchLocation;
	if (!ParseEnumToken(In.SelectionMethod, Out.SelectMethod, TEXT("entrance selection method"))
		|| !ParseEnumToken(In.LocationType, Out.LocationType, TEXT("entrance location type"))) return false;
	Out.bProjectNavigationLocation = In.bProjectNavigationLocation;
	Out.bTraceGroundLocation = In.bTraceGroundLocation;
	Out.bCheckTransitionTrajectory = In.bCheckTransitionTrajectory;
	Out.bCheckEntranceLocationOverlap = In.bCheckEntranceLocationOverlap;
	Out.bCheckSlotLocationOverlap = In.bCheckSlotLocationOverlap;
	Out.bUseSlotLocationAsFallback = In.bUseSlotLocationAsFallback;
	Out.bUseUpAxisLockedRotation = In.bUseUpAxisLockedRotation;
	return true;
}

static FBridgeSmartObjectEntranceResult MakeEntranceResult(const FSmartObjectSlotEntranceLocationResult& In)
{
	FBridgeSmartObjectEntranceResult Out;
	Out.bFound = true;
	Out.bValid = In.bIsValid;
	Out.SlotHandle = In.EntranceHandle.IsValid() ? LexToString(In.EntranceHandle.GetSlotHandle()) : FString();
	Out.Location = In.Location;
	Out.Rotation = In.Rotation;
	Out.Tags = TagsToStrings(In.Tags);
	Out.bHasNavigationNode = In.HasNodeRef();
	return Out;
}

} // namespace BridgeSmartObjectImpl

TArray<FBridgeSmartObjectComponentInfo> UUnrealBridgeSmartObjectLibrary::ListSmartObjectComponents(bool bPIEOnly)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	TArray<FBridgeSmartObjectComponentInfo> Result;
	for (TObjectIterator<USmartObjectComponent> It; It; ++It)
	{
		USmartObjectComponent* Component = *It;
		if (!IsValid(Component) || Component->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)
			|| !Component->GetWorld()) continue;
		if (bPIEOnly && !Component->GetWorld()->IsPlayInEditor()) continue;
		Result.Add(MakeComponentInfo(Component));
	}
	Result.Sort([](const FBridgeSmartObjectComponentInfo& A, const FBridgeSmartObjectComponentInfo& B)
	{
		return A.ComponentPath < B.ComponentPath;
	});
	return Result;
}

FBridgeSmartObjectComponentInfo UUnrealBridgeSmartObjectLibrary::GetSmartObjectComponentInfo(
	const FString& ComponentPath)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	return MakeComponentInfo(FindComponent(ComponentPath));
}

FString UUnrealBridgeSmartObjectLibrary::AddSmartObjectComponent(const FString& ActorPath,
	const FString& DefinitionAssetPath, const FString& ComponentName, bool bCanBePartOfCollection,
	bool bRegisterWithSubsystem)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	AActor* Actor = FindActor(ActorPath);
	USmartObjectDefinition* Definition = LoadDefinition(DefinitionAssetPath);
	if (!Actor || !Definition) return FString();
	FScopedTransaction Transaction(LOCTEXT("AddSmartObjectComponent", "UnrealBridge: Add Smart Object Component"));
	Actor->Modify();
	const FName RequestedName = ComponentName.IsEmpty() ? FName(TEXT("SmartObject")) : FName(*ComponentName);
	const FName UniqueName = MakeUniqueObjectName(Actor, USmartObjectComponent::StaticClass(), RequestedName);
	USmartObjectComponent* Component = NewObject<USmartObjectComponent>(Actor, UniqueName, RF_Transactional);
	if (!Component) return SetError(TEXT("failed to create SmartObjectComponent")), FString();
	Component->SetDefinition(Definition);
	if (FBoolProperty* CollectionProperty = FindFProperty<FBoolProperty>(Component->GetClass(), TEXT("bCanBePartOfCollection")))
		CollectionProperty->SetPropertyValue_InContainer(Component, bCanBePartOfCollection);
	if (USceneComponent* Root = Actor->GetRootComponent()) Component->SetupAttachment(Root);
	Actor->AddInstanceComponent(Component);
	Component->RegisterComponent();
	if (bRegisterWithSubsystem && !Component->IsBoundToSimulation())
	{
		if (USmartObjectSubsystem* Subsystem = GetSubsystem(Actor->GetWorld()))
		{
			if (!Subsystem->RegisterSmartObject(Component))
			{
				Actor->RemoveInstanceComponent(Component);
				Component->DestroyComponent();
				SetError(TEXT("Smart Object subsystem rejected the new component"));
				return FString();
			}
		}
		else return FString();
	}
	Actor->MarkPackageDirty();
	return Component->GetPathName();
}

bool UUnrealBridgeSmartObjectLibrary::RemoveSmartObjectComponent(const FString& ComponentPath)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectComponent* Component = FindComponent(ComponentPath);
	if (!Component) return false;
	AActor* Owner = Component->GetOwner();
	FScopedTransaction Transaction(LOCTEXT("RemoveSmartObjectComponent", "UnrealBridge: Remove Smart Object Component"));
	if (Owner) Owner->Modify();
	Component->Modify();
	if (USmartObjectSubsystem* Subsystem = GetSubsystem(Component->GetWorld()))
	{
		if (Component->IsBoundToSimulation()) Subsystem->RemoveSmartObject(Component);
	}
	if (Owner) Owner->RemoveInstanceComponent(Component);
	Component->DestroyComponent();
	if (Owner) Owner->MarkPackageDirty();
	return true;
}

bool UUnrealBridgeSmartObjectLibrary::SetSmartObjectComponentDefinition(const FString& ComponentPath,
	const FString& DefinitionAssetPath, bool bRegisterWithSubsystem)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectComponent* Component = FindComponent(ComponentPath);
	USmartObjectDefinition* Definition = DefinitionAssetPath.IsEmpty() ? nullptr : LoadDefinition(DefinitionAssetPath);
	if (!Component || (!DefinitionAssetPath.IsEmpty() && !Definition)) return false;
	USmartObjectSubsystem* Subsystem = GetSubsystem(Component->GetWorld());
	if (!Subsystem) return false;
	FScopedTransaction Transaction(LOCTEXT("SetSmartObjectComponentDefinition", "UnrealBridge: Set Smart Object Component Definition"));
	Component->Modify();
	const bool bWasBound = Component->IsBoundToSimulation();
	if (bWasBound) Subsystem->RemoveSmartObject(Component);
	Component->SetDefinition(Definition);
	if (bRegisterWithSubsystem && Definition && !Subsystem->RegisterSmartObject(Component))
		return SetError(TEXT("Smart Object subsystem rejected the component after its definition changed"));
	Component->MarkPackageDirty();
	return true;
}

bool UUnrealBridgeSmartObjectLibrary::ControlSmartObjectComponent(
	const FString& ComponentPath, const FString& Action)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectComponent* Component = FindComponent(ComponentPath);
	if (!Component) return false;
	USmartObjectSubsystem* Subsystem = GetSubsystem(Component->GetWorld());
	if (!Subsystem) return false;
	const FString Key = NormalizeToken(Action);
	if (Key == TEXT("register"))
		return Component->IsBoundToSimulation() || Subsystem->RegisterSmartObject(Component);
	if (Key == TEXT("unregister"))
		return !Component->IsBoundToSimulation() || Subsystem->UnregisterSmartObject(Component);
	if (Key == TEXT("removefromsimulation") || Key == TEXT("remove"))
		return !Component->IsBoundToSimulation() || Subsystem->RemoveSmartObject(Component);
	if (Key == TEXT("refresh"))
	{
		if (Component->IsBoundToSimulation()) Subsystem->RemoveSmartObject(Component);
		return Subsystem->RegisterSmartObject(Component);
	}
	if (Key == TEXT("enable")) return Component->SetSmartObjectEnabled(true);
	if (Key == TEXT("disable")) return Component->SetSmartObjectEnabled(false);
	return SetError(FString::Printf(TEXT("unknown component action '%s'"), *Action));
}

TArray<FBridgeSmartObjectCollectionInfo> UUnrealBridgeSmartObjectLibrary::ListPersistentSmartObjectCollections(
	bool bPIEOnly)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	TArray<FBridgeSmartObjectCollectionInfo> Result;
	for (TObjectIterator<ASmartObjectPersistentCollection> It; It; ++It)
	{
		ASmartObjectPersistentCollection* Collection = *It;
		if (!IsValid(Collection) || Collection->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)
			|| !Collection->GetWorld()) continue;
		if (bPIEOnly && !Collection->GetWorld()->IsPlayInEditor()) continue;
		Result.Add(MakeCollectionInfo(Collection));
	}
	Result.Sort([](const FBridgeSmartObjectCollectionInfo& A, const FBridgeSmartObjectCollectionInfo& B)
	{
		return A.ActorPath < B.ActorPath;
	});
	return Result;
}

FString UUnrealBridgeSmartObjectLibrary::CreatePersistentSmartObjectCollection(const FString& ActorLabel)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	if (IsPieRunning()) return SetError(TEXT("refusing to create a persistent collection while PIE is running")), FString();
	UWorld* World = GetCurrentWorld();
	if (!World) return FString();
	FScopedTransaction Transaction(LOCTEXT("CreateSmartObjectCollection", "UnrealBridge: Create Smart Object Collection"));
	FActorSpawnParameters Params;
	Params.Name = MakeUniqueObjectName(World->PersistentLevel, ASmartObjectPersistentCollection::StaticClass(),
		TEXT("SmartObjectPersistentCollection"));
	Params.ObjectFlags |= RF_Transactional;
	ASmartObjectPersistentCollection* Collection = World->SpawnActor<ASmartObjectPersistentCollection>(
		ASmartObjectPersistentCollection::StaticClass(), FTransform::Identity, Params);
	if (!Collection) return SetError(TEXT("failed to spawn SmartObjectPersistentCollection")), FString();
	Collection->SetActorLabel(ActorLabel.IsEmpty() ? TEXT("SmartObjectPersistentCollection") : ActorLabel);
	if (!InvokeCollectionEditorAction(Collection, TEXT("RebuildCollection")))
	{
		World->EditorDestroyActor(Collection, true);
		return FString();
	}
	Collection->MarkPackageDirty();
	return Collection->GetPathName();
}

bool UUnrealBridgeSmartObjectLibrary::DestroyPersistentSmartObjectCollection(const FString& CollectionActorPath)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	ASmartObjectPersistentCollection* Collection = FindCollection(CollectionActorPath);
	if (!Collection || !Collection->GetWorld()) return false;
	FScopedTransaction Transaction(LOCTEXT("DestroySmartObjectCollection", "UnrealBridge: Destroy Smart Object Collection"));
	UWorld* World = Collection->GetWorld();
	if (!UnregisterCollection(Collection)) return false;
	return World->EditorDestroyActor(Collection, true);
}

bool UUnrealBridgeSmartObjectLibrary::ControlPersistentSmartObjectCollection(
	const FString& CollectionActorPath, const FString& Action)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	ASmartObjectPersistentCollection* Collection = FindCollection(CollectionActorPath);
	if (!Collection) return false;
	const FString Key = NormalizeToken(Action);
	FScopedTransaction Transaction(LOCTEXT("ControlSmartObjectCollection", "UnrealBridge: Control Smart Object Collection"));
	Collection->Modify();
	if (Key == TEXT("rebuild"))
	{
		if (!InvokeCollectionEditorAction(Collection, TEXT("RebuildCollection"))) return false;
	}
	else if (Key == TEXT("clear"))
	{
		if (!InvokeCollectionEditorAction(Collection, TEXT("ClearCollection"))) return false;
	}
	else if (Key == TEXT("register")) return RegisterCollection(Collection);
	else if (Key == TEXT("unregister")) return UnregisterCollection(Collection);
	else return SetError(FString::Printf(TEXT("unknown collection action '%s'"), *Action));
	Collection->MarkPackageDirty();
	return true;
}

TArray<FBridgeSmartObjectCollectionEntryInfo> UUnrealBridgeSmartObjectLibrary::ListPersistentSmartObjectCollectionEntries(
	const FString& CollectionActorPath)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	TArray<FBridgeSmartObjectCollectionEntryInfo> Result;
	ASmartObjectPersistentCollection* Collection = FindCollection(CollectionActorPath);
	if (!Collection || !Collection->GetWorld()) return Result;
	const FSmartObjectContainer& Container = Collection->GetSmartObjectContainer();
	for (int32 Index = 0; Index < Collection->GetEntries().Num(); ++Index)
	{
		const FSmartObjectCollectionEntry& Entry = Collection->GetEntries()[Index];
		FBridgeSmartObjectCollectionEntryInfo Info;
		Info.Index = Index;
		Info.SmartObjectHandle = LexToString(Entry.GetHandle());
		Info.ComponentPath = GetPathNameSafe(Container.GetSmartObjectComponent(Entry.GetHandle()));
		Info.DefinitionPath = GetPathNameSafe(Container.GetDefinitionForEntry(Entry, Collection->GetWorld()));
		Info.Transform = Entry.GetTransform();
		const FBox Bounds = Entry.GetWorldBounds();
		if (Bounds.IsValid)
		{
			Info.BoundsMin = Bounds.Min;
			Info.BoundsMax = Bounds.Max;
		}
		Info.Tags = TagsToStrings(Entry.GetTags());
		Result.Add(MoveTemp(Info));
	}
	return Result;
}

TArray<FBridgeSmartObjectQueryResult> UUnrealBridgeSmartObjectLibrary::QuerySmartObjects(
	const FVector& Center, const FVector& Extent, const TArray<FString>& UserTags,
	const TArray<FString>& ActivityTags, const TArray<FString>& BehaviorClassPaths,
	const FString& ActivityMatch, const FString& ClaimPriority, bool bEvaluateConditions,
	bool bIncludeClaimedSlots, bool bIncludeDisabledSlots, const FString& UserActorPath,
	bool bSortByDistance, int32 MaxResults)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	TArray<FBridgeSmartObjectQueryResult> Result;
	UWorld* World = GetCurrentWorld();
	USmartObjectSubsystem* Subsystem = GetSubsystem(World);
	if (!Subsystem) return Result;

	ESmartObjectClaimPriority Priority;
	if (!ParseEnumToken(ClaimPriority, Priority, TEXT("claim priority"))) return Result;
	FSmartObjectRequestFilter Filter;
	if (!StringsToTags(UserTags, Filter.UserTags)
		|| !BuildActivityQuery(ActivityTags, ActivityMatch, Filter.ActivityRequirements)) return Result;
	Filter.ClaimPriority = Priority;
	Filter.bShouldEvaluateConditions = bEvaluateConditions;
	Filter.bShouldIncludeClaimedSlots = bIncludeClaimedSlots;
	Filter.bShouldIncludeDisabledSlots = bIncludeDisabledSlots;
	for (const FString& ClassPath : BehaviorClassPaths)
	{
		UClass* Class = LoadObject<UClass>(nullptr, *ClassPath);
		if (!Class || !Class->IsChildOf(USmartObjectBehaviorDefinition::StaticClass()))
		{
			SetError(FString::Printf(TEXT("'%s' is not a Smart Object behavior definition class"), *ClassPath));
			return Result;
		}
		Filter.BehaviorDefinitionClasses.Add(Class);
	}

	AActor* UserActor = UserActorPath.IsEmpty() ? nullptr : FindActor(UserActorPath);
	if (!UserActorPath.IsEmpty() && !UserActor) return Result;
	const FVector SafeExtent(FMath::Abs(Extent.X), FMath::Abs(Extent.Y), FMath::Abs(Extent.Z));
	const FSmartObjectRequest Request(FBox(Center - SafeExtent, Center + SafeExtent), Filter);
	const FSmartObjectActorUserData ActorUserData(UserActor);
	const FConstStructView UserData = UserActor
		? FConstStructView::Make(ActorUserData) : FConstStructView();
	TArray<FSmartObjectRequestResult> RawResults;
	Subsystem->FindSmartObjects(Request, RawResults, UserData);
	Result.Reserve(RawResults.Num());
	for (const FSmartObjectRequestResult& Raw : RawResults)
	{
		const FBridgeSmartObjectRuntimeSlotInfo Slot = MakeRuntimeSlotInfo(Subsystem, Raw.SlotHandle, Priority);
		FBridgeSmartObjectQueryResult Info;
		Info.SmartObjectHandle = Slot.SmartObjectHandle;
		Info.SlotHandle = Slot.SlotHandle;
		Info.ComponentPath = Slot.ComponentPath;
		Info.OwnerPath = Slot.OwnerPath;
		Info.DefinitionPath = Slot.DefinitionPath;
		Info.SlotTransform = Slot.SlotTransform;
		Info.Distance = FVector::Distance(Center, Slot.SlotTransform.GetLocation());
		Info.SlotState = Slot.SlotState;
		Info.ActivityTags = Slot.ActivityTags;
		Info.RuntimeTags = Slot.RuntimeTags;
		Info.bEnabled = Slot.bEnabled;
		Info.bCanBeClaimed = Slot.bCanBeClaimed;
		Subsystem->ReadSlotData(Raw.SlotHandle, [&](FConstSmartObjectSlotView View)
		{
			AppendEffectiveBehaviorClasses(View.GetDefinition(),
				const_cast<USmartObjectDefinition&>(View.GetSmartObjectDefinition()), Info.BehaviorClassPaths);
		});
		Result.Add(MoveTemp(Info));
	}
	if (bSortByDistance)
	{
		Result.StableSort([](const FBridgeSmartObjectQueryResult& A, const FBridgeSmartObjectQueryResult& B)
		{
			return A.Distance < B.Distance;
		});
	}
	if (MaxResults > 0 && Result.Num() > MaxResults) Result.SetNum(MaxResults);
	for (int32 Index = 0; Index < Result.Num(); ++Index) Result[Index].Rank = Index;
	return Result;
}

TArray<FBridgeSmartObjectRuntimeSlotInfo> UUnrealBridgeSmartObjectLibrary::ListSmartObjectRuntimeSlots(
	const FString& SmartObjectHandle, const FString& ComponentPath, const FString& ClaimPriority)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	TArray<FBridgeSmartObjectRuntimeSlotInfo> Result;
	ESmartObjectClaimPriority Priority;
	if (!ParseEnumToken(ClaimPriority, Priority, TEXT("claim priority"))) return Result;

	USmartObjectComponent* Component = ComponentPath.IsEmpty() ? nullptr : FindComponent(ComponentPath);
	if (!ComponentPath.IsEmpty() && !Component) return Result;
	UWorld* World = Component ? Component->GetWorld() : GetCurrentWorld();
	USmartObjectSubsystem* Subsystem = GetSubsystem(World);
	if (!Subsystem) return Result;
	FSmartObjectHandle Handle;
	if (!SmartObjectHandle.IsEmpty())
	{
		if (!ParseSmartObjectHandle(SmartObjectHandle, Handle)) return Result;
	}
	else if (Component)
	{
		Handle = Component->GetRegisteredHandle();
		if (!Handle.IsValid())
		{
			SetError(FString::Printf(TEXT("component '%s' is not bound to the Smart Object simulation"), *ComponentPath));
			return Result;
		}
	}
	else
	{
		SetError(TEXT("either SmartObjectHandle or ComponentPath must be supplied"));
		return Result;
	}
	if (Component && Component->GetRegisteredHandle().IsValid()
		&& Component->GetRegisteredHandle() != Handle)
	{
		SetError(TEXT("SmartObjectHandle does not identify the supplied component"));
		return Result;
	}
	if (!Subsystem->IsSmartObjectValid(Handle))
	{
		SetError(FString::Printf(TEXT("Smart Object '%s' is not accessible in the selected world"), *LexToString(Handle)));
		return Result;
	}
	TArray<FSmartObjectSlotHandle> Slots;
	Subsystem->GetAllSlots(Handle, Slots);
	Result.Reserve(Slots.Num());
	for (const FSmartObjectSlotHandle& Slot : Slots) Result.Add(MakeRuntimeSlotInfo(Subsystem, Slot, Priority));
	Result.Sort([](const FBridgeSmartObjectRuntimeSlotInfo& A, const FBridgeSmartObjectRuntimeSlotInfo& B)
	{
		return A.SlotIndex < B.SlotIndex;
	});
	return Result;
}

FString UUnrealBridgeSmartObjectLibrary::CreateRuntimeSmartObject(const FString& DefinitionAssetPath,
	const FTransform& Transform, const FString& OwnerActorPath)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectDefinition* Definition = LoadDefinition(DefinitionAssetPath);
	UWorld* World = GetCurrentWorld();
	USmartObjectSubsystem* Subsystem = GetSubsystem(World);
	if (!Definition || !Subsystem) return FString();
	AActor* Owner = OwnerActorPath.IsEmpty() ? nullptr : FindActor(OwnerActorPath);
	if (!OwnerActorPath.IsEmpty() && !Owner) return FString();
	TSharedPtr<FInstancedStruct> OwnerData;
	FConstStructView OwnerView;
	if (Owner)
	{
		OwnerData = MakeShared<FInstancedStruct>();
		OwnerData->InitializeAs<FSmartObjectActorUserData>(Owner);
		OwnerView = FConstStructView(*OwnerData);
	}
	const FSmartObjectHandle Handle = Subsystem->CreateSmartObject(*Definition, Transform, OwnerView);
	if (!Handle.IsValid())
	{
		SetError(TEXT("Smart Object subsystem rejected the dynamic object; ensure its runtime is initialized and the definition is valid"));
		return FString();
	}
	const FString HandleText = LexToString(Handle);
	if (OwnerData.IsValid()) DynamicOwnerData.Add(HandleText, MoveTemp(OwnerData));
	return HandleText;
}

bool UUnrealBridgeSmartObjectLibrary::DestroyRuntimeSmartObject(const FString& SmartObjectHandle)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	UWorld* World = GetCurrentWorld();
	USmartObjectSubsystem* Subsystem = GetSubsystem(World);
	FSmartObjectHandle Handle;
	if (!Subsystem || !ParseSmartObjectHandle(SmartObjectHandle, Handle)) return false;
	if (!Subsystem->IsSmartObjectValid(Handle))
		return SetError(FString::Printf(TEXT("Smart Object '%s' is not accessible in the selected world"), *SmartObjectHandle));
	TArray<FString> ClaimsToRelease;
	for (const TPair<FString, FStoredClaim>& Pair : StoredClaims)
	{
		if (Pair.Value.World.Get() == World && Pair.Value.Handle.SmartObjectHandle == Handle)
			ClaimsToRelease.Add(Pair.Key);
	}
	for (const FString& Token : ClaimsToRelease)
	{
		if (FStoredClaim* Stored = StoredClaims.Find(Token)) Subsystem->MarkSlotAsFree(Stored->Handle);
		StoredClaims.Remove(Token);
	}
	const bool bDestroyed = Subsystem->DestroySmartObject(Handle);
	if (bDestroyed) DynamicOwnerData.Remove(SmartObjectHandle);
	else SetError(FString::Printf(TEXT("failed to destroy Smart Object '%s'"), *SmartObjectHandle));
	return bDestroyed;
}

FBridgeSmartObjectClaimResult UUnrealBridgeSmartObjectLibrary::ClaimSmartObjectSlot(
	const FString& SlotHandle, const FString& UserActorPath, const FString& ClaimPriority)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	FBridgeSmartObjectClaimResult Result;
	UWorld* World = GetCurrentWorld();
	USmartObjectSubsystem* Subsystem = GetSubsystem(World);
	if (!Subsystem)
	{
		Result.Error = LastError;
		return Result;
	}
	FSmartObjectSlotHandle Slot;
	ESmartObjectClaimPriority Priority;
	if (!ParseSlotHandle(Subsystem, SlotHandle, Slot)
		|| !ParseEnumToken(ClaimPriority, Priority, TEXT("claim priority")))
	{
		Result.Error = LastError;
		return Result;
	}
	AActor* UserActor = UserActorPath.IsEmpty() ? nullptr : FindActor(UserActorPath);
	if (!UserActorPath.IsEmpty() && !UserActor)
	{
		Result.Error = LastError;
		return Result;
	}
	TSharedPtr<FInstancedStruct> UserData;
	FConstStructView UserView;
	if (UserActor)
	{
		UserData = MakeShared<FInstancedStruct>();
		UserData->InitializeAs<FSmartObjectActorUserData>(UserActor);
		UserView = FConstStructView(*UserData);
	}
	const FSmartObjectClaimHandle Claim = Subsystem->MarkSlotAsClaimed(Slot, Priority, UserView);
	if (!Claim.IsValid())
	{
		Result.SlotHandle = SlotHandle;
		Result.UserActorPath = UserActorPath;
		Result.Priority = EnumToString(Priority);
		Result.Error = TEXT("the Smart Object slot could not be claimed");
		SetError(Result.Error);
		return Result;
	}
	const FString Token = GuidToString(FGuid::NewGuid());
	FStoredClaim Stored;
	Stored.World = World;
	Stored.Handle = Claim;
	Stored.UserActorPath = UserActorPath;
	Stored.Priority = EnumToString(Priority);
	Stored.UserData = MoveTemp(UserData);
	StoredClaims.Add(Token, MoveTemp(Stored));
	return MakeClaimResult(Token, StoredClaims.FindChecked(Token), Subsystem, true);
}

FBridgeSmartObjectClaimResult UUnrealBridgeSmartObjectLibrary::OccupySmartObjectClaim(
	const FString& ClaimToken, const FString& BehaviorClassPath)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	FBridgeSmartObjectClaimResult Result;
	FStoredClaim* Stored = StoredClaims.Find(ClaimToken);
	if (!Stored)
	{
		Result.ClaimToken = ClaimToken;
		Result.Error = FString::Printf(TEXT("unknown Smart Object claim token '%s'"), *ClaimToken);
		SetError(Result.Error);
		return Result;
	}
	UWorld* World = Stored->World.Get();
	USmartObjectSubsystem* Subsystem = GetSubsystem(World);
	if (!Subsystem)
	{
		Result = MakeClaimResult(ClaimToken, *Stored, nullptr, false, LastError);
		return Result;
	}
	UClass* BehaviorClass = LoadObject<UClass>(nullptr, *BehaviorClassPath);
	if (!BehaviorClass || !BehaviorClass->IsChildOf(USmartObjectBehaviorDefinition::StaticClass()))
	{
		const FString Error = FString::Printf(TEXT("'%s' is not a Smart Object behavior definition class"), *BehaviorClassPath);
		SetError(Error);
		return MakeClaimResult(ClaimToken, *Stored, Subsystem, false, Error);
	}
	const USmartObjectBehaviorDefinition* Behavior = Subsystem->MarkSlotAsOccupied(Stored->Handle, BehaviorClass);
	if (!Behavior)
	{
		const FString Error = FString::Printf(TEXT("claim '%s' has no compatible '%s' behavior or is no longer claimable"),
			*ClaimToken, *BehaviorClassPath);
		SetError(Error);
		return MakeClaimResult(ClaimToken, *Stored, Subsystem, false, Error);
	}
	Stored->BehaviorObjectPath = Behavior->GetPathName();
	return MakeClaimResult(ClaimToken, *Stored, Subsystem, true);
}

bool UUnrealBridgeSmartObjectLibrary::ReleaseSmartObjectClaim(const FString& ClaimToken)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	FStoredClaim* Stored = StoredClaims.Find(ClaimToken);
	if (!Stored) return SetError(FString::Printf(TEXT("unknown Smart Object claim token '%s'"), *ClaimToken));
	UWorld* World = Stored->World.Get();
	USmartObjectSubsystem* Subsystem = GetSubsystem(World);
	if (!Subsystem)
	{
		StoredClaims.Remove(ClaimToken);
		return false;
	}
	const bool bReleased = Subsystem->MarkSlotAsFree(Stored->Handle);
	if (bReleased) StoredClaims.Remove(ClaimToken);
	else SetError(FString::Printf(TEXT("claim '%s' could not be released"), *ClaimToken));
	return bReleased;
}

TArray<FBridgeSmartObjectClaimResult> UUnrealBridgeSmartObjectLibrary::ListSmartObjectClaims()
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	TArray<FBridgeSmartObjectClaimResult> Result;
	TArray<FString> Tokens;
	StoredClaims.GetKeys(Tokens);
	Tokens.Sort();
	Result.Reserve(Tokens.Num());
	for (const FString& Token : Tokens)
	{
		const FStoredClaim& Stored = StoredClaims.FindChecked(Token);
		USmartObjectSubsystem* Subsystem = USmartObjectSubsystem::GetCurrent(Stored.World.Get());
		const bool bValid = Subsystem && Stored.Handle.IsValid()
			&& Subsystem->IsClaimedSmartObjectValid(Stored.Handle);
		Result.Add(MakeClaimResult(Token, Stored, Subsystem, bValid,
			bValid ? FString() : TEXT("claim is no longer valid in its world")));
	}
	return Result;
}

bool UUnrealBridgeSmartObjectLibrary::SetSmartObjectRuntimeTags(const FString& Handle,
	const FString& Scope, const TArray<FString>& Tags, bool bReplace)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	UWorld* World = GetCurrentWorld();
	USmartObjectSubsystem* Subsystem = GetSubsystem(World);
	if (!Subsystem) return false;
	FGameplayTagContainer Desired;
	if (!StringsToTags(Tags, Desired)) return false;
	const FString Key = NormalizeToken(Scope);
	if (Key == TEXT("object") || Key == TEXT("smartobject") || Key == TEXT("instance"))
	{
		FSmartObjectHandle ObjectHandle;
		if (!ParseSmartObjectHandle(Handle, ObjectHandle)) return false;
		if (!Subsystem->IsSmartObjectValid(ObjectHandle))
			return SetError(FString::Printf(TEXT("Smart Object '%s' is not accessible in the selected world"), *Handle));
		const FGameplayTagContainer Existing = Subsystem->GetInstanceTags(ObjectHandle);
		if (bReplace)
		{
			for (const FGameplayTag& Tag : Existing)
			{
				if (!Desired.HasTagExact(Tag)) Subsystem->RemoveTagFromInstance(ObjectHandle, Tag);
			}
		}
		for (const FGameplayTag& Tag : Desired)
		{
			if (!Existing.HasTagExact(Tag)) Subsystem->AddTagToInstance(ObjectHandle, Tag);
		}
		return true;
	}
	if (Key == TEXT("slot"))
	{
		FSmartObjectSlotHandle Slot;
		if (!ParseSlotHandle(Subsystem, Handle, Slot)) return false;
		const FGameplayTagContainer Existing = Subsystem->GetSlotTags(Slot);
		if (bReplace)
		{
			for (const FGameplayTag& Tag : Existing)
			{
				if (!Desired.HasTagExact(Tag)) Subsystem->RemoveTagFromSlot(Slot, Tag);
			}
		}
		for (const FGameplayTag& Tag : Desired)
		{
			if (!Existing.HasTagExact(Tag)) Subsystem->AddTagToSlot(Slot, Tag);
		}
		return true;
	}
	return SetError(FString::Printf(TEXT("unknown runtime tag scope '%s'; expected Object or Slot"), *Scope));
}

bool UUnrealBridgeSmartObjectLibrary::SetSmartObjectRuntimeEnabled(const FString& SmartObjectHandle,
	bool bEnabled, const FString& ReasonTag)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	UWorld* World = GetCurrentWorld();
	USmartObjectSubsystem* Subsystem = GetSubsystem(World);
	FSmartObjectHandle Handle;
	if (!Subsystem || !ParseSmartObjectHandle(SmartObjectHandle, Handle)) return false;
	if (!Subsystem->IsSmartObjectValid(Handle))
		return SetError(FString::Printf(TEXT("Smart Object '%s' is not accessible in the selected world"), *SmartObjectHandle));
	if (ReasonTag.IsEmpty()) return Subsystem->SetEnabled(Handle, bEnabled);
	const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*ReasonTag), false);
	if (!Tag.IsValid()) return SetError(FString::Printf(TEXT("gameplay tag '%s' is not registered"), *ReasonTag));
	return Subsystem->SetEnabledForReason(Handle, Tag, bEnabled);
}

bool UUnrealBridgeSmartObjectLibrary::SetSmartObjectRuntimeSlotEnabled(const FString& SlotHandle,
	bool bEnabled)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectSubsystem* Subsystem = GetSubsystem(GetCurrentWorld());
	FSmartObjectSlotHandle Slot;
	if (!Subsystem || !ParseSlotHandle(Subsystem, SlotHandle, Slot)) return false;
	Subsystem->SetSlotEnabled(Slot, bEnabled);
	return true;
}

bool UUnrealBridgeSmartObjectLibrary::SendSmartObjectSlotEvent(const FString& SlotHandle,
	const FString& EventTag)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectSubsystem* Subsystem = GetSubsystem(GetCurrentWorld());
	FSmartObjectSlotHandle Slot;
	if (!Subsystem || !ParseSlotHandle(Subsystem, SlotHandle, Slot)) return false;
	const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*EventTag), false);
	if (!Tag.IsValid()) return SetError(FString::Printf(TEXT("gameplay tag '%s' is not registered"), *EventTag));
	if (!Subsystem->SendSlotEvent(Slot, Tag))
		return SetError(FString::Printf(TEXT("failed to send '%s' to slot '%s'"), *EventTag, *SlotHandle));
	return true;
}

FBridgeSmartObjectEntranceResult UUnrealBridgeSmartObjectLibrary::FindSmartObjectEntrance(
	const FString& SlotHandle, const FBridgeSmartObjectEntranceRequest& Request)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	FBridgeSmartObjectEntranceResult Result;
	USmartObjectSubsystem* Subsystem = GetSubsystem(GetCurrentWorld());
	FSmartObjectSlotHandle Slot;
	FSmartObjectSlotEntranceLocationRequest NativeRequest;
	if (!Subsystem || !ParseSlotHandle(Subsystem, SlotHandle, Slot)
		|| !BuildEntranceRequest(Request, NativeRequest))
	{
		Result.Error = LastError;
		return Result;
	}
	FSmartObjectSlotEntranceLocationResult NativeResult;
	if (!Subsystem->FindEntranceLocationForSlot(Slot, NativeRequest, NativeResult))
	{
		Result.SlotHandle = SlotHandle;
		Result.Error = TEXT("no entrance location passed the request and validation settings");
		return Result;
	}
	Result = MakeEntranceResult(NativeResult);
	Result.SlotHandle = SlotHandle;
	return Result;
}

TArray<FBridgeSmartObjectEntranceResult> UUnrealBridgeSmartObjectLibrary::ValidateSmartObjectDefinitionEntrances(
	const FString& AssetPath, const FTransform& OwnerTransform,
	const FBridgeSmartObjectEntranceRequest& Request, const FString& SkipActorPath)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	TArray<FBridgeSmartObjectEntranceResult> Result;
	USmartObjectDefinition* Definition = LoadDefinition(AssetPath);
	UWorld* World = GetCurrentWorld();
	if (!Definition || !World) return Result;
	AActor* SkipActor = SkipActorPath.IsEmpty() ? nullptr : FindActor(SkipActorPath);
	if (!SkipActorPath.IsEmpty() && !SkipActor) return Result;
	FSmartObjectSlotEntranceLocationRequest NativeRequest;
	if (!BuildEntranceRequest(Request, NativeRequest)) return Result;
	TArray<FSmartObjectSlotEntranceLocationResult> NativeResults;
	USmartObjectSubsystem::QueryAllValidatedEntranceLocations(
		World, *Definition, OwnerTransform, SkipActor, NativeRequest, NativeResults);
	Result.Reserve(NativeResults.Num());
	for (const FSmartObjectSlotEntranceLocationResult& NativeResult : NativeResults)
	{
		Result.Add(MakeEntranceResult(NativeResult));
	}
	return Result;
}

bool UUnrealBridgeSmartObjectLibrary::DebugSmartObjectSubsystem(const FString& Action)
{
	using namespace BridgeSmartObjectImpl;
	ClearError();
	USmartObjectSubsystem* Subsystem = GetSubsystem(GetCurrentWorld());
	if (!Subsystem) return false;
	const FString Key = NormalizeToken(Action);
#if WITH_SMARTOBJECT_DEBUG
	if (Key == TEXT("initializeruntime") || Key == TEXT("initialize"))
	{
		Subsystem->DebugInitializeRuntime();
		return true;
	}
	if (Key == TEXT("cleanupruntime") || Key == TEXT("cleanup"))
	{
		for (TPair<FString, FStoredClaim>& Pair : StoredClaims)
		{
			if (Pair.Value.World.Get() == Subsystem->GetWorld()
				&& Subsystem->IsClaimedSmartObjectValid(Pair.Value.Handle))
			{
				Subsystem->MarkSlotAsFree(Pair.Value.Handle);
			}
		}
		StoredClaims.Reset();
		DynamicOwnerData.Reset();
		Subsystem->DebugCleanupRuntime();
		return true;
	}
	if (Key == TEXT("registerall") || Key == TEXT("register"))
	{
		Subsystem->DebugRegisterAllSmartObjects();
		return true;
	}
	if (Key == TEXT("unregisterall") || Key == TEXT("unregister"))
	{
		Subsystem->DebugUnregisterAllSmartObjects();
		return true;
	}
#else
	return SetError(TEXT("Smart Object debug controls are disabled in this engine configuration"));
#endif
	return SetError(FString::Printf(TEXT("unknown subsystem action '%s'; expected InitializeRuntime, CleanupRuntime, RegisterAll, or UnregisterAll"), *Action));
}

#undef LOCTEXT_NAMESPACE

#endif // !UE_VERSION_OLDER_THAN(5, 7, 0)
