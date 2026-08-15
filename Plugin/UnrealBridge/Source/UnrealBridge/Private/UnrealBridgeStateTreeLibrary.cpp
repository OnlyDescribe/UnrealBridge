#include "UnrealBridgeStateTreeLibrary.h"

#include "Misc/EngineVersionComparison.h"

#if !UE_VERSION_OLDER_THAN(5, 7, 0)

#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "ScopedTransaction.h"

#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorModule.h"
#include "StateTreeEditorNode.h"
#include "StateTreeEditorSchema.h"
#include "StateTreeFactory.h"
#include "StateTreeNodeClassCache.h"
#include "StateTreePropertyBindings.h"
#include "StateTreeSchema.h"
#include "StateTreeState.h"
#include "StateTreeExecutionTypes.h"
#include "StateTreeReference.h"

#include "StateTreeConditionBase.h"
#include "StateTreeConsiderationBase.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeTaskBase.h"
#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "Blueprint/StateTreeConsiderationBlueprintBase.h"
#include "Blueprint/StateTreeEvaluatorBlueprintBase.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "Components/StateTreeComponent.h"

#include "PropertyBindingBindableStructDescriptor.h"
#include "PropertyBindingDataView.h"
#include "PropertyBindingPath.h"
#include "StructUtils/PropertyBag.h"

#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "UnrealBridgeStateTree"

namespace BridgeStateTreeImpl
{

static FString LastError;

static void ClearError()
{
	LastError.Reset();
}

static bool SetError(const FString& Message)
{
	LastError = Message;
	UE_LOG(LogTemp, Warning, TEXT("UnrealBridge StateTree: %s"), *Message);
	return false;
}

static FString GuidToString(const FGuid& Guid)
{
	return Guid.IsValid() ? Guid.ToString(EGuidFormats::DigitsWithHyphens) : FString();
}

static bool ParseGuid(const FString& Text, FGuid& OutGuid, const TCHAR* Label, bool bAllowEmpty = false)
{
	if (Text.IsEmpty() && bAllowEmpty)
	{
		OutGuid.Invalidate();
		return true;
	}
	if (!FGuid::Parse(Text, OutGuid) || !OutGuid.IsValid())
	{
		return SetError(FString::Printf(TEXT("invalid %s GUID '%s'"), Label, *Text));
	}
	return true;
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

static FString TopLevelPropertyKey(const FString& PropertyPath)
{
	int32 End = PropertyPath.Len();
	int32 Dot = INDEX_NONE;
	int32 Bracket = INDEX_NONE;
	if (PropertyPath.FindChar(TEXT('.'), Dot)) End = FMath::Min(End, Dot);
	if (PropertyPath.FindChar(TEXT('['), Bracket)) End = FMath::Min(End, Bracket);
	return NormalizeToken(PropertyPath.Left(End));
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

static UStateTree* LoadStateTree(const FString& AssetPath, bool bReportError = true)
{
	if (AssetPath.IsEmpty())
	{
		if (bReportError) SetError(TEXT("StateTree asset path is empty"));
		return nullptr;
	}

	UStateTree* Tree = LoadObject<UStateTree>(nullptr, *AssetPath);
	if (!Tree && !AssetPath.Contains(TEXT(".")))
	{
		FString PackagePath;
		FString AssetName;
		if (SplitAssetPath(AssetPath, PackagePath, AssetName))
		{
			Tree = LoadObject<UStateTree>(nullptr, *(AssetPath + TEXT(".") + AssetName));
		}
	}
	if (!Tree && bReportError)
	{
		SetError(FString::Printf(TEXT("could not load StateTree '%s'"), *AssetPath));
	}
	return Tree;
}

static UStateTreeEditorData* GetEditorData(UStateTree* Tree, bool bReportError = true)
{
	if (!Tree)
	{
		return nullptr;
	}
#if WITH_EDITORONLY_DATA
	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(Tree->EditorData);
#else
	UStateTreeEditorData* EditorData = nullptr;
#endif
	if (!EditorData && bReportError)
	{
		SetError(FString::Printf(TEXT("StateTree '%s' has no editable editor data"), *Tree->GetPathName()));
	}
	return EditorData;
}

static void RemoveInvalidBindings(UStateTreeEditorData* EditorData)
{
	if (!EditorData)
	{
		return;
	}
	TMap<FGuid, const FPropertyBindingDataView> Values;
	EditorData->GetAllStructValues(Values);
	EditorData->EditorBindings.RemoveInvalidBindings(Values);
}

static void FinishMutation(UStateTree* Tree, bool bPruneBindings = true)
{
	if (!Tree)
	{
		return;
	}
	if (bPruneBindings)
	{
		RemoveInvalidBindings(GetEditorData(Tree, false));
	}
	Tree->MarkPackageDirty();
	if (GEditor)
	{
		if (UStateTreeEditingSubsystem* Subsystem = GEditor->GetEditorSubsystem<UStateTreeEditingSubsystem>())
		{
			if (TSharedPtr<FStateTreeViewModel> ViewModel = Subsystem->FindViewModel(Tree))
			{
				ViewModel->NotifyAssetChangedExternally();
			}
		}
	}
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

static bool ParseTransitionTrigger(const FString& Text, EStateTreeTransitionTrigger& OutTrigger)
{
	OutTrigger = EStateTreeTransitionTrigger::None;
	TArray<FString> Parts;
	Text.ParseIntoArray(Parts, TEXT("|"), true);
	if (Parts.IsEmpty())
	{
		return SetError(TEXT("transition trigger is empty"));
	}
	for (const FString& Part : Parts)
	{
		EStateTreeTransitionTrigger Value = EStateTreeTransitionTrigger::None;
		if (!ParseEnumToken(Part, Value, TEXT("transition trigger")))
		{
			return false;
		}
		OutTrigger |= Value;
	}
	return true;
}

static FString TransitionTriggerToString(EStateTreeTransitionTrigger Trigger)
{
	if (Trigger == EStateTreeTransitionTrigger::None)
	{
		return TEXT("None");
	}

	TArray<FString> Parts;
	const EStateTreeTransitionTrigger Values[] = {
		EStateTreeTransitionTrigger::OnStateCompleted,
		EStateTreeTransitionTrigger::OnStateSucceeded,
		EStateTreeTransitionTrigger::OnStateFailed,
		EStateTreeTransitionTrigger::OnTick,
		EStateTreeTransitionTrigger::OnEvent,
		EStateTreeTransitionTrigger::OnDelegate,
	};
	for (const EStateTreeTransitionTrigger Value : Values)
	{
		if (EnumHasAnyFlags(Trigger, Value))
		{
			Parts.Add(EnumToString(Value));
		}
	}
	return FString::Join(Parts, TEXT("|"));
}

static UStateTreeState* FindState(UStateTreeEditorData* EditorData, const FGuid& StateId)
{
	return EditorData ? EditorData->GetMutableStateByID(StateId) : nullptr;
}

static bool IsStateInside(const UStateTreeState* CandidateParent, const UStateTreeState* State)
{
	for (const UStateTreeState* Current = CandidateParent; Current; Current = Current->Parent)
	{
		if (Current == State)
		{
			return true;
		}
	}
	return false;
}

struct FTransitionHandle
{
	UStateTreeState* OwnerState = nullptr;
	FStateTreeTransition* Transition = nullptr;
	int32 Index = INDEX_NONE;
};

static FTransitionHandle FindTransition(UStateTreeEditorData* EditorData, const FGuid& TransitionId)
{
	FTransitionHandle Result;
	if (!EditorData)
	{
		return Result;
	}
	EditorData->VisitHierarchy([&](UStateTreeState& State, UStateTreeState*)
	{
		for (int32 Index = 0; Index < State.Transitions.Num(); ++Index)
		{
			if (State.Transitions[Index].ID == TransitionId)
			{
				Result.OwnerState = &State;
				Result.Transition = &State.Transitions[Index];
				Result.Index = Index;
				return EStateTreeVisitor::Break;
			}
		}
		return EStateTreeVisitor::Continue;
	});
	return Result;
}

struct FNodeHandle
{
	FStateTreeEditorNode* Node = nullptr;
	TArray<FStateTreeEditorNode>* Array = nullptr;
	UStateTreeState* OwnerState = nullptr;
	FStateTreeTransition* OwnerTransition = nullptr;
	FString Scope;
	int32 Index = INDEX_NONE;
	bool bSingleTask = false;
};

static bool MatchScopeFilter(const FString& Scope, const FString& Filter)
{
	return Filter.IsEmpty() || NormalizeToken(Scope) == NormalizeToken(Filter);
}

static FNodeHandle FindNode(UStateTreeEditorData* EditorData, const FGuid& NodeId)
{
	FNodeHandle Result;
	if (!EditorData)
	{
		return Result;
	}

	auto SearchArray = [&](TArray<FStateTreeEditorNode>& Array, const FString& Scope,
		UStateTreeState* State, FStateTreeTransition* Transition) -> bool
	{
		for (int32 Index = 0; Index < Array.Num(); ++Index)
		{
			if (Array[Index].ID == NodeId)
			{
				Result.Node = &Array[Index];
				Result.Array = &Array;
				Result.OwnerState = State;
				Result.OwnerTransition = Transition;
				Result.Scope = Scope;
				Result.Index = Index;
				return true;
			}
		}
		return false;
	};

	if (SearchArray(EditorData->Evaluators, TEXT("Evaluator"), nullptr, nullptr)
		|| SearchArray(EditorData->GlobalTasks, TEXT("GlobalTask"), nullptr, nullptr))
	{
		return Result;
	}

	EditorData->VisitHierarchy([&](UStateTreeState& State, UStateTreeState*)
	{
		if (SearchArray(State.EnterConditions, TEXT("EnterCondition"), &State, nullptr)
			|| SearchArray(State.Tasks, TEXT("Task"), &State, nullptr)
			|| SearchArray(State.Considerations, TEXT("Consideration"), &State, nullptr))
		{
			return EStateTreeVisitor::Break;
		}
		if (State.SingleTask.ID == NodeId)
		{
			Result.Node = &State.SingleTask;
			Result.OwnerState = &State;
			Result.Scope = TEXT("SingleTask");
			Result.Index = 0;
			Result.bSingleTask = true;
			return EStateTreeVisitor::Break;
		}
		for (FStateTreeTransition& Transition : State.Transitions)
		{
			if (SearchArray(Transition.Conditions, TEXT("TransitionCondition"), &State, &Transition))
			{
				return EStateTreeVisitor::Break;
			}
		}
		return EStateTreeVisitor::Continue;
	});
	return Result;
}

static bool IsBlueprintNode(const FStateTreeEditorNode& Node)
{
	return Node.Node.GetPtr<FStateTreeBlueprintTaskWrapper>()
		|| Node.Node.GetPtr<FStateTreeBlueprintEvaluatorWrapper>()
		|| Node.Node.GetPtr<FStateTreeBlueprintConditionWrapper>()
		|| Node.Node.GetPtr<FStateTreeBlueprintConsiderationWrapper>();
}

static FString GetAuthoredNodeTypePath(const FStateTreeEditorNode& Node)
{
	if (const FStateTreeBlueprintTaskWrapper* Wrapper = Node.Node.GetPtr<FStateTreeBlueprintTaskWrapper>())
		return GetPathNameSafe(Wrapper->TaskClass.Get());
	if (const FStateTreeBlueprintEvaluatorWrapper* Wrapper = Node.Node.GetPtr<FStateTreeBlueprintEvaluatorWrapper>())
		return GetPathNameSafe(Wrapper->EvaluatorClass.Get());
	if (const FStateTreeBlueprintConditionWrapper* Wrapper = Node.Node.GetPtr<FStateTreeBlueprintConditionWrapper>())
		return GetPathNameSafe(Wrapper->ConditionClass.Get());
	if (const FStateTreeBlueprintConsiderationWrapper* Wrapper = Node.Node.GetPtr<FStateTreeBlueprintConsiderationWrapper>())
		return GetPathNameSafe(Wrapper->ConsiderationClass.Get());
	return GetPathNameSafe(Node.Node.GetScriptStruct());
}

static bool IsNodeEnabled(const FStateTreeEditorNode& Node)
{
	if (const FStateTreeTaskBase* Task = Node.Node.GetPtr<FStateTreeTaskBase>())
	{
		return Task->bTaskEnabled;
	}
	return true;
}

static FBridgeStateTreeNodeInfo MakeNodeInfo(const FStateTreeEditorNode& Node, const FString& Scope,
	const FGuid& OwnerId, int32 Index)
{
	FBridgeStateTreeNodeInfo Info;
	Info.Id = GuidToString(Node.ID);
	Info.OwnerId = GuidToString(OwnerId);
	Info.Scope = Scope;
	Info.Name = Node.GetName().ToString();
	Info.NodeTypePath = GetAuthoredNodeTypePath(Node);
	const FStateTreeDataView Instance = Node.GetInstance();
	const FStateTreeDataView RuntimeData = Node.GetExecutionRuntimeData();
	Info.InstanceTypePath = GetPathNameSafe(Instance.GetStruct());
	Info.ExecutionRuntimeDataTypePath = GetPathNameSafe(RuntimeData.GetStruct());
	Info.ExpressionOperand = EnumToString(Node.ExpressionOperand);
	Info.ExpressionIndent = Node.ExpressionIndent;
	Info.Index = Index;
	Info.bEnabled = IsNodeEnabled(Node);
	Info.bBlueprintNode = IsBlueprintNode(Node);
	return Info;
}

static bool InitializeNode(FStateTreeEditorNode& Node, UObject* Outer, UStruct* Type)
{
	Node.Reset();
	if (UScriptStruct* ScriptStruct = Cast<UScriptStruct>(Type))
	{
		if (!ScriptStruct->IsChildOf(FStateTreeNodeBase::StaticStruct()))
		{
			return SetError(FString::Printf(TEXT("'%s' is not a StateTree node struct"), *ScriptStruct->GetPathName()));
		}
		Node.ID = FGuid::NewGuid();
		Node.Node.InitializeAs(ScriptStruct);
		FStateTreeNodeBase& Base = Node.Node.GetMutable<FStateTreeNodeBase>();
		if (const UScriptStruct* InstanceStruct = Cast<const UScriptStruct>(Base.GetInstanceDataType()))
			Node.Instance.InitializeAs(InstanceStruct);
		else if (const UClass* InstanceClass = Cast<const UClass>(Base.GetInstanceDataType()))
			Node.InstanceObject = NewObject<UObject>(Outer, InstanceClass);
		if (const UScriptStruct* RuntimeStruct = Cast<const UScriptStruct>(Base.GetExecutionRuntimeDataType()))
			Node.ExecutionRuntimeData.InitializeAs(RuntimeStruct);
		else if (const UClass* RuntimeClass = Cast<const UClass>(Base.GetExecutionRuntimeDataType()))
			Node.ExecutionRuntimeDataObject = NewObject<UObject>(Outer, RuntimeClass);
		return true;
	}

	if (UClass* Class = Cast<UClass>(Type))
	{
		if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			return SetError(FString::Printf(TEXT("StateTree Blueprint node class '%s' is abstract or deprecated"), *Class->GetPathName()));
		}
		if (Class->IsChildOf(UStateTreeTaskBlueprintBase::StaticClass()))
		{
			Node.Node.InitializeAs(FStateTreeBlueprintTaskWrapper::StaticStruct());
			Node.Node.GetMutable<FStateTreeBlueprintTaskWrapper>().TaskClass = Class;
		}
		else if (Class->IsChildOf(UStateTreeEvaluatorBlueprintBase::StaticClass()))
		{
			Node.Node.InitializeAs(FStateTreeBlueprintEvaluatorWrapper::StaticStruct());
			Node.Node.GetMutable<FStateTreeBlueprintEvaluatorWrapper>().EvaluatorClass = Class;
		}
		else if (Class->IsChildOf(UStateTreeConditionBlueprintBase::StaticClass()))
		{
			Node.Node.InitializeAs(FStateTreeBlueprintConditionWrapper::StaticStruct());
			Node.Node.GetMutable<FStateTreeBlueprintConditionWrapper>().ConditionClass = Class;
		}
		else if (Class->IsChildOf(UStateTreeConsiderationBlueprintBase::StaticClass()))
		{
			Node.Node.InitializeAs(FStateTreeBlueprintConsiderationWrapper::StaticStruct());
			Node.Node.GetMutable<FStateTreeBlueprintConsiderationWrapper>().ConsiderationClass = Class;
		}
		else
		{
			return SetError(FString::Printf(TEXT("'%s' is not a StateTree Blueprint node class"), *Class->GetPathName()));
		}
		Node.ID = FGuid::NewGuid();
		Node.InstanceObject = NewObject<UObject>(Outer, Class);
		return Node.InstanceObject != nullptr;
	}

	return SetError(TEXT("node type is neither UScriptStruct nor UClass"));
}

static UStruct* LoadNodeType(const FString& TypePath)
{
	if (UScriptStruct* ScriptStruct = LoadObject<UScriptStruct>(nullptr, *TypePath))
		return ScriptStruct;
	if (UClass* Class = LoadObject<UClass>(nullptr, *TypePath))
		return Class;
	SetError(FString::Printf(TEXT("could not load StateTree node type '%s'"), *TypePath));
	return nullptr;
}

static bool NodeMatchesScope(const UStruct* Type, const FString& Scope)
{
	const FString Key = NormalizeToken(Scope);
	const UScriptStruct* ScriptStruct = Cast<UScriptStruct>(Type);
	const UClass* Class = Cast<UClass>(Type);
	const bool bTask = (ScriptStruct && ScriptStruct->IsChildOf(FStateTreeTaskBase::StaticStruct()))
		|| (Class && Class->IsChildOf(UStateTreeTaskBlueprintBase::StaticClass()));
	const bool bEvaluator = (ScriptStruct && ScriptStruct->IsChildOf(FStateTreeEvaluatorBase::StaticStruct()))
		|| (Class && Class->IsChildOf(UStateTreeEvaluatorBlueprintBase::StaticClass()));
	const bool bCondition = (ScriptStruct && ScriptStruct->IsChildOf(FStateTreeConditionBase::StaticStruct()))
		|| (Class && Class->IsChildOf(UStateTreeConditionBlueprintBase::StaticClass()));
	const bool bConsideration = (ScriptStruct && ScriptStruct->IsChildOf(FStateTreeConsiderationBase::StaticStruct()))
		|| (Class && Class->IsChildOf(UStateTreeConsiderationBlueprintBase::StaticClass()));

	if ((Key == TEXT("task") || Key == TEXT("singletask") || Key == TEXT("globaltask")) && bTask) return true;
	if (Key == TEXT("evaluator") && bEvaluator) return true;
	if ((Key == TEXT("entercondition") || Key == TEXT("transitioncondition")) && bCondition) return true;
	if (Key == TEXT("consideration") && bConsideration) return true;
	return SetError(FString::Printf(TEXT("node type '%s' is incompatible with scope '%s'"), *GetPathNameSafe(Type), *Scope));
}

static bool IsAllowedBySchema(const UStateTreeEditorData* EditorData, const UStruct* Type)
{
	if (!EditorData || !EditorData->Schema || !Type)
	{
		return false;
	}
	if (const UScriptStruct* ScriptStruct = Cast<UScriptStruct>(Type))
		return EditorData->Schema->IsStructAllowed(ScriptStruct);
	if (const UClass* Class = Cast<UClass>(Type))
		return EditorData->Schema->IsClassAllowed(Class);
	return false;
}

static FPropertyBindingDataView GetNodeDataView(FStateTreeEditorNode& Node, const FString& DataSource)
{
	const FString Key = NormalizeToken(DataSource);
	if (Key == TEXT("node"))
		return FPropertyBindingDataView(Node.Node);
	if (Key == TEXT("instance") || Key == TEXT("instancedata"))
		return Node.InstanceObject ? FPropertyBindingDataView(Node.InstanceObject) : FPropertyBindingDataView(Node.Instance);
	if (Key == TEXT("executionruntimedata") || Key == TEXT("runtimedata"))
		return Node.ExecutionRuntimeDataObject ? FPropertyBindingDataView(Node.ExecutionRuntimeDataObject) : FPropertyBindingDataView(Node.ExecutionRuntimeData);
	SetError(FString::Printf(TEXT("unknown node data source '%s'; expected Node, Instance, or ExecutionRuntimeData"), *DataSource));
	return FPropertyBindingDataView();
}

struct FResolvedProperty
{
	FProperty* Property = nullptr;
	void* Value = nullptr;
	FProperty* TopLevelProperty = nullptr;
};

static bool ResolveProperty(FPropertyBindingDataView View, const FString& PropertyPath, FResolvedProperty& Out)
{
	if (!View.IsValid())
	{
		return SetError(TEXT("the requested data source has no instance data"));
	}
	if (PropertyPath.IsEmpty())
	{
		return SetError(TEXT("property path is empty"));
	}
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

static FBridgeStateTreePropertyResult ReadProperty(FPropertyBindingDataView View, const FString& PropertyPath, UObject* Owner)
{
	FBridgeStateTreePropertyResult Result;
	FResolvedProperty Resolved;
	if (!ResolveProperty(View, PropertyPath, Resolved))
	{
		Result.Error = LastError;
		return Result;
	}
	Resolved.Property->ExportTextItem_Direct(Result.Value, Resolved.Value, nullptr, Owner, PPF_None);
	Result.TypePath = Resolved.Property->GetCPPType(nullptr, CPPF_None);
	Result.bSuccess = true;
	return Result;
}

static bool WriteProperty(FPropertyBindingDataView View, const FString& PropertyPath,
	const FString& Value, UObject* Owner)
{
	FResolvedProperty Resolved;
	if (!ResolveProperty(View, PropertyPath, Resolved))
	{
		return false;
	}

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

	if (Owner)
	{
		Owner->Modify();
		Owner->PreEditChange(Resolved.TopLevelProperty);
	}
	Resolved.Property->CopySingleValue(Resolved.Value, Temp.GetData());
	Resolved.Property->DestroyValue(Temp.GetData());
	if (Owner)
	{
		FPropertyChangedEvent Event(Resolved.TopLevelProperty ? Resolved.TopLevelProperty : Resolved.Property,
			EPropertyChangeType::ValueSet);
		Owner->PostEditChangeProperty(Event);
	}
	return true;
}

struct FCompilerLogAccess : public FStateTreeCompilerLog
{
	const TArray<FStateTreeCompilerLogMessage>& GetMessages() const { return Messages; }
};

static FString SeverityToString(int32 Severity)
{
	if (Severity < EMessageSeverity::Error) return TEXT("CriticalError");
	if (Severity == EMessageSeverity::Error) return TEXT("Error");
	if (Severity == EMessageSeverity::PerformanceWarning) return TEXT("PerformanceWarning");
	if (Severity == EMessageSeverity::Warning) return TEXT("Warning");
	return TEXT("Info");
}

} // namespace BridgeStateTreeImpl

bool UUnrealBridgeStateTreeLibrary::IsStateTreeApiAvailable()
{
	return true;
}

FString UUnrealBridgeStateTreeLibrary::GetLastStateTreeError()
{
	return BridgeStateTreeImpl::LastError;
}

FBridgeStateTreeCreateResult UUnrealBridgeStateTreeLibrary::CreateStateTree(
	const FString& AssetPath, const FString& SchemaClassPath)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	FBridgeStateTreeCreateResult Result;

	if (IsPieRunning())
	{
		Result.Error = TEXT("refusing to create a StateTree while PIE is running");
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

	UClass* SchemaClass = LoadObject<UClass>(nullptr, *SchemaClassPath);
	if (!SchemaClass || !SchemaClass->IsChildOf(UStateTreeSchema::StaticClass())
		|| SchemaClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
	{
		Result.Error = FString::Printf(TEXT("'%s' is not a concrete UStateTreeSchema class"), *SchemaClassPath);
		SetError(Result.Error);
		return Result;
	}

	UStateTreeFactory* Factory = NewObject<UStateTreeFactory>(GetTransientPackage());
	Factory->SetSchemaClass(SchemaClass);
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UStateTree* Tree = Cast<UStateTree>(AssetTools.CreateAsset(
		AssetName, PackagePath, UStateTree::StaticClass(), Factory));
	if (!Tree)
	{
		Result.Error = FString::Printf(TEXT("failed to create StateTree at '%s/%s'; the path may already exist"),
			*PackagePath, *AssetName);
		SetError(Result.Error);
		return Result;
	}

	Tree->MarkPackageDirty();
	Result.bSuccess = true;
	Result.AssetPath = Tree->GetPathName();
	return Result;
}

FBridgeStateTreeAssetInfo UUnrealBridgeStateTreeLibrary::GetStateTreeInfo(const FString& AssetPath)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	FBridgeStateTreeAssetInfo Info;
	UStateTree* Tree = LoadStateTree(AssetPath);
	UStateTreeEditorData* EditorData = GetEditorData(Tree);
	if (!EditorData)
	{
		return Info;
	}

	Info.AssetPath = Tree->GetPathName();
	Info.SchemaClassPath = GetPathNameSafe(EditorData->Schema ? EditorData->Schema->GetClass() : nullptr);
	Info.EditorSchemaClassPath = GetPathNameSafe(EditorData->EditorSchema ? EditorData->EditorSchema->GetClass() : nullptr);
	Info.RootParametersId = GuidToString(EditorData->GetRootParametersGuid());
	Info.bReadyToRun = Tree->IsReadyToRun();
	Info.bDirty = Tree->GetOutermost()->IsDirty();
	Info.RootStateCount = EditorData->SubTrees.Num();
	Info.EditorDataHash = static_cast<int64>(UStateTreeEditingSubsystem::CalculateStateTreeHash(Tree));
	Info.LastCompiledEditorDataHash = static_cast<int64>(Tree->LastCompiledEditorDataHash);
	Info.BindingCount = EditorData->EditorBindings.GetBindings().Num();
	Info.BreakpointCount = EditorData->Breakpoints.Num();
	if (const UPropertyBag* Bag = EditorData->GetRootParametersPropertyBag().GetPropertyBagStruct())
	{
		Info.ParameterCount = Bag->GetPropertyDescs().Num();
	}
	Info.NodeCount = EditorData->Evaluators.Num() + EditorData->GlobalTasks.Num();
	EditorData->VisitHierarchy([&](UStateTreeState& State, UStateTreeState*)
	{
		++Info.StateCount;
		Info.TransitionCount += State.Transitions.Num();
		Info.NodeCount += State.EnterConditions.Num() + State.Tasks.Num() + State.Considerations.Num();
		if (State.SingleTask.ID.IsValid()) ++Info.NodeCount;
		for (const FStateTreeTransition& Transition : State.Transitions)
		{
			Info.NodeCount += Transition.Conditions.Num();
		}
		return EStateTreeVisitor::Continue;
	});
	return Info;
}

bool UUnrealBridgeStateTreeLibrary::ValidateStateTree(const FString& AssetPath)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	UStateTree* Tree = LoadStateTree(AssetPath);
	if (!Tree || !GetEditorData(Tree))
	{
		return false;
	}
	FScopedTransaction Transaction(LOCTEXT("ValidateStateTree", "UnrealBridge: Validate StateTree"));
	Tree->Modify();
	UStateTreeEditingSubsystem::ValidateStateTree(Tree);
	FinishMutation(Tree);
	return true;
}

FBridgeStateTreeCompileResult UUnrealBridgeStateTreeLibrary::CompileStateTree(
	const FString& AssetPath, bool bRunValidation)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	FBridgeStateTreeCompileResult Result;
	UStateTree* Tree = LoadStateTree(AssetPath);
	if (!Tree || !GetEditorData(Tree))
	{
		Result.Error = LastError;
		return Result;
	}

	if (bRunValidation)
	{
		UStateTreeEditingSubsystem::ValidateStateTree(Tree);
	}
	FCompilerLogAccess Log;
	Result.bSuccess = UStateTreeEditingSubsystem::CompileStateTree(Tree, Log);
	Result.bReadyToRun = Tree->IsReadyToRun();
	for (const FStateTreeCompilerLogMessage& Message : Log.GetMessages())
	{
		FBridgeStateTreeCompileMessage Out;
		Out.Severity = SeverityToString(Message.Severity);
		Out.Message = Message.Message;
		if (Message.State)
		{
			Out.StateId = GuidToString(Message.State->ID);
			Out.StatePath = Message.State->GetPath();
		}
		Out.ItemId = GuidToString(Message.Item.ID);
		Out.ItemName = Message.Item.Name.ToString();
		Result.Messages.Add(MoveTemp(Out));
	}
	if (!Result.bSuccess)
	{
		Result.Error = Result.Messages.IsEmpty()
			? TEXT("StateTree compiler failed without a diagnostic message")
			: Result.Messages.Last().Message;
		SetError(Result.Error);
	}
	FinishMutation(Tree, false);
	return Result;
}

TArray<FBridgeStateTreeStateInfo> UUnrealBridgeStateTreeLibrary::ListStateTreeStates(const FString& AssetPath)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	TArray<FBridgeStateTreeStateInfo> Result;
	UStateTreeEditorData* EditorData = GetEditorData(LoadStateTree(AssetPath));
	if (!EditorData)
	{
		return Result;
	}

	TMap<const UStateTreeState*, int32> Indices;
	for (int32 Index = 0; Index < EditorData->SubTrees.Num(); ++Index)
		Indices.Add(EditorData->SubTrees[Index], Index);

	EditorData->VisitHierarchy([&](UStateTreeState& State, UStateTreeState* Parent)
	{
		if (Parent)
		{
			Indices.FindOrAdd(&State) = Parent->Children.IndexOfByKey(&State);
		}
		FBridgeStateTreeStateInfo Info;
		Info.Id = GuidToString(State.ID);
		Info.ParentId = Parent ? GuidToString(Parent->ID) : FString();
		Info.Name = State.Name.ToString();
		Info.Path = State.GetPath();
		Info.Description = State.Description;
		Info.Type = EnumToString(State.Type);
		Info.SelectionBehavior = EnumToString(State.SelectionBehavior);
		Info.TasksCompletion = EnumToString(State.TasksCompletion);
		Info.Tag = State.Tag.ToString();
		Info.LinkedStateId = GuidToString(State.LinkedSubtree.ID);
		Info.LinkedAssetPath = GetPathNameSafe(State.LinkedAsset);
		Info.bEnabled = State.bEnabled;
		Info.bExpanded = State.bExpanded;
		for (const UStateTreeState* Current = Parent; Current; Current = Current->Parent) ++Info.Depth;
		Info.Index = Indices.FindRef(&State);
		Info.ChildCount = State.Children.Num();
		Info.TaskCount = State.Tasks.Num() + (State.SingleTask.ID.IsValid() ? 1 : 0);
		Info.EnterConditionCount = State.EnterConditions.Num();
		Info.ConsiderationCount = State.Considerations.Num();
		Info.TransitionCount = State.Transitions.Num();
		Result.Add(MoveTemp(Info));
		return EStateTreeVisitor::Continue;
	});
	return Result;
}

FString UUnrealBridgeStateTreeLibrary::AddStateTreeState(const FString& AssetPath,
	const FString& ParentStateId, const FString& Name, const FString& StateType, int32 InsertIndex)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	UStateTree* Tree = LoadStateTree(AssetPath);
	UStateTreeEditorData* EditorData = GetEditorData(Tree);
	if (!EditorData || Name.TrimStartAndEnd().IsEmpty())
	{
		if (Name.TrimStartAndEnd().IsEmpty()) SetError(TEXT("state name is empty"));
		return FString();
	}

	EStateTreeStateType Type = EStateTreeStateType::State;
	if (!ParseEnumToken(StateType, Type, TEXT("state type"))
		|| !EditorData->Schema || !EditorData->Schema->IsStateTypeAllowed(Type))
	{
		if (LastError.IsEmpty()) SetError(FString::Printf(TEXT("schema does not allow state type '%s'"), *StateType));
		return FString();
	}

	UStateTreeState* Parent = nullptr;
	if (!ParentStateId.IsEmpty())
	{
		FGuid ParentGuid;
		if (!ParseGuid(ParentStateId, ParentGuid, TEXT("parent state"))) return FString();
		Parent = FindState(EditorData, ParentGuid);
		if (!Parent)
		{
			SetError(FString::Printf(TEXT("parent state '%s' was not found"), *ParentStateId));
			return FString();
		}
	}

	FScopedTransaction Transaction(LOCTEXT("AddStateTreeState", "UnrealBridge: Add StateTree State"));
	Tree->Modify();
	EditorData->Modify();
	if (Parent) Parent->Modify();
	UObject* Outer = Parent ? static_cast<UObject*>(Parent) : static_cast<UObject*>(EditorData);
	UStateTreeState* NewState = NewObject<UStateTreeState>(Outer, NAME_None, RF_Transactional);
	NewState->Name = FName(*Name.TrimStartAndEnd());
	NewState->Type = Type;
	NewState->Parent = Parent;
	TArray<TObjectPtr<UStateTreeState>>& States = Parent ? Parent->Children : EditorData->SubTrees;
	const int32 TargetIndex = InsertIndex < 0 ? States.Num() : FMath::Clamp(InsertIndex, 0, States.Num());
	States.Insert(NewState, TargetIndex);
	FinishMutation(Tree);
	return GuidToString(NewState->ID);
}

bool UUnrealBridgeStateTreeLibrary::RemoveStateTreeState(const FString& AssetPath, const FString& StateId)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	UStateTree* Tree = LoadStateTree(AssetPath);
	UStateTreeEditorData* EditorData = GetEditorData(Tree);
	FGuid Guid;
	if (!EditorData || !ParseGuid(StateId, Guid, TEXT("state"))) return false;
	UStateTreeState* State = FindState(EditorData, Guid);
	if (!State) return SetError(FString::Printf(TEXT("state '%s' was not found"), *StateId));

	FScopedTransaction Transaction(LOCTEXT("RemoveStateTreeState", "UnrealBridge: Remove StateTree State"));
	Tree->Modify();
	State->Modify();
	UStateTreeState* Parent = State->Parent;
	if (Parent) Parent->Modify(); else EditorData->Modify();
	TArray<TObjectPtr<UStateTreeState>>& States = Parent ? Parent->Children : EditorData->SubTrees;
	const int32 Removed = States.Remove(State);
	if (Removed == 0) return SetError(TEXT("state exists but is not present in its parent array"));
	State->Parent = nullptr;
	FinishMutation(Tree);
	return true;
}

bool UUnrealBridgeStateTreeLibrary::MoveStateTreeState(const FString& AssetPath, const FString& StateId,
	const FString& NewParentStateId, int32 InsertIndex)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	UStateTree* Tree = LoadStateTree(AssetPath);
	UStateTreeEditorData* EditorData = GetEditorData(Tree);
	FGuid StateGuid;
	if (!EditorData || !ParseGuid(StateId, StateGuid, TEXT("state"))) return false;
	UStateTreeState* State = FindState(EditorData, StateGuid);
	if (!State) return SetError(FString::Printf(TEXT("state '%s' was not found"), *StateId));

	UStateTreeState* NewParent = nullptr;
	if (!NewParentStateId.IsEmpty())
	{
		FGuid ParentGuid;
		if (!ParseGuid(NewParentStateId, ParentGuid, TEXT("new parent state"))) return false;
		NewParent = FindState(EditorData, ParentGuid);
		if (!NewParent) return SetError(FString::Printf(TEXT("new parent state '%s' was not found"), *NewParentStateId));
	}
	if (NewParent == State || IsStateInside(NewParent, State))
		return SetError(TEXT("cannot move a state under itself or one of its descendants"));

	FScopedTransaction Transaction(LOCTEXT("MoveStateTreeState", "UnrealBridge: Move StateTree State"));
	Tree->Modify();
	EditorData->Modify();
	State->Modify();
	UStateTreeState* OldParent = State->Parent;
	if (OldParent) OldParent->Modify();
	if (NewParent) NewParent->Modify();
	TArray<TObjectPtr<UStateTreeState>>& OldArray = OldParent ? OldParent->Children : EditorData->SubTrees;
	if (OldArray.Remove(State) == 0) return SetError(TEXT("state exists but is not present in its old parent array"));
	TArray<TObjectPtr<UStateTreeState>>& NewArray = NewParent ? NewParent->Children : EditorData->SubTrees;
	const int32 TargetIndex = InsertIndex < 0 ? NewArray.Num() : FMath::Clamp(InsertIndex, 0, NewArray.Num());
	NewArray.Insert(State, TargetIndex);
	State->Parent = NewParent;
	FinishMutation(Tree);
	return true;
}

bool UUnrealBridgeStateTreeLibrary::SetStateTreeStateType(const FString& AssetPath,
	const FString& StateId, const FString& StateType)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	UStateTree* Tree = LoadStateTree(AssetPath);
	UStateTreeEditorData* EditorData = GetEditorData(Tree);
	FGuid Guid;
	if (!EditorData || !ParseGuid(StateId, Guid, TEXT("state"))) return false;
	UStateTreeState* State = FindState(EditorData, Guid);
	if (!State) return SetError(FString::Printf(TEXT("state '%s' was not found"), *StateId));
	EStateTreeStateType Type = EStateTreeStateType::State;
	if (!ParseEnumToken(StateType, Type, TEXT("state type"))) return false;
	if (Type == EStateTreeStateType::Linked || Type == EStateTreeStateType::LinkedAsset)
		return SetError(TEXT("use SetStateTreeLinkedState or SetStateTreeLinkedAsset for linked state types"));
	if (!EditorData->Schema || !EditorData->Schema->IsStateTypeAllowed(Type))
		return SetError(FString::Printf(TEXT("schema does not allow state type '%s'"), *StateType));
	FScopedTransaction Transaction(LOCTEXT("SetStateTreeStateType", "UnrealBridge: Set StateTree State Type"));
	Tree->Modify();
	State->Modify();
	State->Type = Type;
	State->LinkedAsset = nullptr;
	State->LinkedSubtree = FStateTreeStateLink();
	State->Parameters.bFixedLayout = false;
	FinishMutation(Tree);
	return true;
}

FBridgeStateTreePropertyResult UUnrealBridgeStateTreeLibrary::GetStateTreeStateProperty(
	const FString& AssetPath, const FString& StateId, const FString& PropertyPath)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	FBridgeStateTreePropertyResult Result;
	UStateTree* Tree = LoadStateTree(AssetPath);
	UStateTreeEditorData* EditorData = GetEditorData(Tree);
	FGuid Guid;
	if (!EditorData || !ParseGuid(StateId, Guid, TEXT("state")))
	{
		Result.Error = LastError;
		return Result;
	}
	UStateTreeState* State = FindState(EditorData, Guid);
	if (!State)
	{
		SetError(FString::Printf(TEXT("state '%s' was not found"), *StateId));
		Result.Error = LastError;
		return Result;
	}
	return ReadProperty(FPropertyBindingDataView(State), PropertyPath, State);
}

bool UUnrealBridgeStateTreeLibrary::SetStateTreeStateProperty(const FString& AssetPath,
	const FString& StateId, const FString& PropertyPath, const FString& Value)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	UStateTree* Tree = LoadStateTree(AssetPath);
	UStateTreeEditorData* EditorData = GetEditorData(Tree);
	FGuid Guid;
	if (!EditorData || !ParseGuid(StateId, Guid, TEXT("state"))) return false;
	UStateTreeState* State = FindState(EditorData, Guid);
	if (!State) return SetError(FString::Printf(TEXT("state '%s' was not found"), *StateId));
	const FString PropertyKey = TopLevelPropertyKey(PropertyPath);
	if (PropertyKey == TEXT("id") || PropertyKey == TEXT("parent") || PropertyKey == TEXT("children")
		|| PropertyKey == TEXT("transitions") || PropertyKey == TEXT("enterconditions")
		|| PropertyKey == TEXT("tasks") || PropertyKey == TEXT("singletask")
		|| PropertyKey == TEXT("considerations") || PropertyKey == TEXT("parameters")
		|| PropertyKey == TEXT("linkedasset") || PropertyKey == TEXT("linkedsubtree")
		|| PropertyKey == TEXT("type"))
	{
		return SetError(FString::Printf(TEXT("state property '%s' is structural; use the dedicated StateTree API"),
			*PropertyPath));
	}
	FScopedTransaction Transaction(LOCTEXT("SetStateTreeStateProperty", "UnrealBridge: Set StateTree State Property"));
	Tree->Modify();
	if (!WriteProperty(FPropertyBindingDataView(State), PropertyPath, Value, State)) return false;
	FinishMutation(Tree);
	return true;
}

bool UUnrealBridgeStateTreeLibrary::SetStateTreeLinkedState(const FString& AssetPath,
	const FString& StateId, const FString& LinkedStateId)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	UStateTree* Tree = LoadStateTree(AssetPath);
	UStateTreeEditorData* EditorData = GetEditorData(Tree);
	FGuid StateGuid;
	FGuid LinkedGuid;
	if (!EditorData || !ParseGuid(StateId, StateGuid, TEXT("state"))
		|| !ParseGuid(LinkedStateId, LinkedGuid, TEXT("linked state"))) return false;
	UStateTreeState* State = FindState(EditorData, StateGuid);
	UStateTreeState* Linked = FindState(EditorData, LinkedGuid);
	if (!State || !Linked) return SetError(TEXT("state or linked state was not found"));
	if (State == Linked) return SetError(TEXT("a state cannot link to itself"));
	FScopedTransaction Transaction(LOCTEXT("SetStateTreeLinkedState", "UnrealBridge: Set Linked State"));
	Tree->Modify();
	State->Modify();
	State->Type = EStateTreeStateType::Linked;
	State->SetLinkedState(Linked->GetLinkToState());
	FinishMutation(Tree);
	return true;
}

bool UUnrealBridgeStateTreeLibrary::SetStateTreeLinkedAsset(const FString& AssetPath,
	const FString& StateId, const FString& LinkedAssetPath)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	UStateTree* Tree = LoadStateTree(AssetPath);
	UStateTreeEditorData* EditorData = GetEditorData(Tree);
	FGuid StateGuid;
	if (!EditorData || !ParseGuid(StateId, StateGuid, TEXT("state"))) return false;
	UStateTreeState* State = FindState(EditorData, StateGuid);
	if (!State) return SetError(FString::Printf(TEXT("state '%s' was not found"), *StateId));
	UStateTree* Linked = LinkedAssetPath.IsEmpty() ? nullptr : LoadStateTree(LinkedAssetPath);
	if (!LinkedAssetPath.IsEmpty() && !Linked) return false;
	if (Linked == Tree) return SetError(TEXT("a StateTree state cannot link its own containing asset"));
	FScopedTransaction Transaction(LOCTEXT("SetStateTreeLinkedAsset", "UnrealBridge: Set Linked StateTree Asset"));
	Tree->Modify();
	State->Modify();
	if (Linked)
	{
		State->Type = EStateTreeStateType::LinkedAsset;
		State->SetLinkedStateAsset(Linked);
	}
	else
	{
		State->Type = EStateTreeStateType::State;
		State->LinkedAsset = nullptr;
		State->LinkedSubtree = FStateTreeStateLink();
		State->Parameters.bFixedLayout = false;
	}
	FinishMutation(Tree);
	return true;
}

namespace BridgeStateTreeImpl
{

static FString NodeKind(const UStruct* Type)
{
	if (const UScriptStruct* Struct = Cast<UScriptStruct>(Type))
	{
		if (Struct->IsChildOf(FStateTreeEvaluatorBase::StaticStruct())) return TEXT("Evaluator");
		if (Struct->IsChildOf(FStateTreeTaskBase::StaticStruct())) return TEXT("Task");
		if (Struct->IsChildOf(FStateTreeConditionBase::StaticStruct())) return TEXT("Condition");
		if (Struct->IsChildOf(FStateTreeConsiderationBase::StaticStruct())) return TEXT("Consideration");
	}
	if (const UClass* Class = Cast<UClass>(Type))
	{
		if (Class->IsChildOf(UStateTreeEvaluatorBlueprintBase::StaticClass())) return TEXT("Evaluator");
		if (Class->IsChildOf(UStateTreeTaskBlueprintBase::StaticClass())) return TEXT("Task");
		if (Class->IsChildOf(UStateTreeConditionBlueprintBase::StaticClass())) return TEXT("Condition");
		if (Class->IsChildOf(UStateTreeConsiderationBlueprintBase::StaticClass())) return TEXT("Consideration");
	}
	return FString();
}

static FString GetNodeTypeInstancePath(UStruct* Type)
{
	if (UClass* Class = Cast<UClass>(Type))
	{
		return Class->GetPathName();
	}
	if (UScriptStruct* Struct = Cast<UScriptStruct>(Type))
	{
		FInstancedStruct Temp;
		Temp.InitializeAs(Struct);
		if (const FStateTreeNodeBase* Base = Temp.GetPtr<FStateTreeNodeBase>())
		{
			return GetPathNameSafe(Base->GetInstanceDataType());
		}
	}
	return FString();
}

static void GatherNodeTypes(FStateTreeNodeClassCache& Cache, UStateTreeEditorData* EditorData,
	UStruct* NativeBase, UClass* BlueprintBase, const FString& Kind, const FString& KindFilter,
	bool bIncludeDisallowed, TSet<FString>& Seen, TArray<FBridgeStateTreeNodeTypeInfo>& Out)
{
	auto AddData = [&](const TArray<TSharedPtr<FStateTreeNodeClassData>>& Data)
	{
		for (const TSharedPtr<FStateTreeNodeClassData>& Entry : Data)
		{
			UStruct* Type = Entry ? Entry->GetStruct(true) : nullptr;
			if (!Type || Type == NativeBase || Type == BlueprintBase)
			{
				continue;
			}
			if (const UClass* Class = Cast<UClass>(Type))
			{
				if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
					continue;
			}
			const FString Path = Type->GetPathName();
			if (Seen.Contains(Path))
			{
				continue;
			}
			const bool bAllowed = IsAllowedBySchema(EditorData, Type);
			if (!bAllowed && !bIncludeDisallowed)
			{
				continue;
			}
			if (!KindFilter.IsEmpty() && NormalizeToken(KindFilter) != NormalizeToken(Kind))
			{
				continue;
			}
			Seen.Add(Path);
			FBridgeStateTreeNodeTypeInfo Info;
			Info.Kind = Kind;
			Info.TypePath = Path;
			Info.DisplayName = Type->GetDisplayNameText().ToString();
			Info.InstanceTypePath = GetNodeTypeInstancePath(Type);
			Info.bBlueprintClass = Type->IsA<UClass>();
			Info.bAllowedBySchema = bAllowed;
			Out.Add(MoveTemp(Info));
		}
	};

	TArray<TSharedPtr<FStateTreeNodeClassData>> NativeData;
	Cache.GetStructs(NativeBase, NativeData);
	AddData(NativeData);
	TArray<TSharedPtr<FStateTreeNodeClassData>> BlueprintData;
	Cache.GetClasses(BlueprintBase, BlueprintData);
	AddData(BlueprintData);
}

struct FNodeInsertionTarget
{
	TArray<FStateTreeEditorNode>* Array = nullptr;
	FStateTreeEditorNode* Single = nullptr;
	UStateTreeState* OwnerState = nullptr;
	FStateTreeTransition* OwnerTransition = nullptr;
	UObject* Outer = nullptr;
};

static bool ResolveNodeInsertionTarget(UStateTreeEditorData* EditorData, const FString& OwnerId,
	const FString& Scope, FNodeInsertionTarget& Out)
{
	const FString Key = NormalizeToken(Scope);
	if (Key == TEXT("evaluator"))
	{
		if (!OwnerId.IsEmpty()) return SetError(TEXT("Evaluator scope requires an empty OwnerId"));
		Out.Array = &EditorData->Evaluators;
		Out.Outer = EditorData;
		return true;
	}
	if (Key == TEXT("globaltask"))
	{
		if (!OwnerId.IsEmpty()) return SetError(TEXT("GlobalTask scope requires an empty OwnerId"));
		Out.Array = &EditorData->GlobalTasks;
		Out.Outer = EditorData;
		return true;
	}

	FGuid OwnerGuid;
	if (!ParseGuid(OwnerId, OwnerGuid, TEXT("node owner"))) return false;
	if (Key == TEXT("transitioncondition"))
	{
		FTransitionHandle Transition = FindTransition(EditorData, OwnerGuid);
		if (!Transition.Transition) return SetError(FString::Printf(TEXT("transition '%s' was not found"), *OwnerId));
		Out.Array = &Transition.Transition->Conditions;
		Out.OwnerState = Transition.OwnerState;
		Out.OwnerTransition = Transition.Transition;
		Out.Outer = Transition.OwnerState;
		return true;
	}

	UStateTreeState* State = FindState(EditorData, OwnerGuid);
	if (!State) return SetError(FString::Printf(TEXT("state '%s' was not found"), *OwnerId));
	Out.OwnerState = State;
	Out.Outer = State;
	if (Key == TEXT("entercondition")) Out.Array = &State->EnterConditions;
	else if (Key == TEXT("task")) Out.Array = &State->Tasks;
	else if (Key == TEXT("consideration")) Out.Array = &State->Considerations;
	else if (Key == TEXT("singletask")) Out.Single = &State->SingleTask;
	else return SetError(FString::Printf(TEXT("unknown node scope '%s'"), *Scope));
	return true;
}

} // namespace BridgeStateTreeImpl

TArray<FBridgeStateTreeNodeTypeInfo> UUnrealBridgeStateTreeLibrary::ListStateTreeNodeTypes(
	const FString& AssetPath, const FString& Kind, bool bIncludeDisallowed)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	TArray<FBridgeStateTreeNodeTypeInfo> Result;
	UStateTreeEditorData* EditorData = GetEditorData(LoadStateTree(AssetPath));
	if (!EditorData)
	{
		return Result;
	}
	const FString KindKey = NormalizeToken(Kind);
	if (!KindKey.IsEmpty() && KindKey != TEXT("evaluator") && KindKey != TEXT("task")
		&& KindKey != TEXT("condition") && KindKey != TEXT("consideration"))
	{
		SetError(FString::Printf(TEXT("unknown node kind '%s'; expected Evaluator, Task, Condition, or Consideration"), *Kind));
		return Result;
	}

	TSharedPtr<FStateTreeNodeClassCache> Cache = FStateTreeEditorModule::GetModule().GetNodeClassCache();
	if (!Cache)
	{
		SetError(TEXT("StateTree node class cache is unavailable"));
		return Result;
	}
	TSet<FString> Seen;
	GatherNodeTypes(*Cache, EditorData, FStateTreeEvaluatorBase::StaticStruct(),
		UStateTreeEvaluatorBlueprintBase::StaticClass(), TEXT("Evaluator"), Kind, bIncludeDisallowed, Seen, Result);
	GatherNodeTypes(*Cache, EditorData, FStateTreeTaskBase::StaticStruct(),
		UStateTreeTaskBlueprintBase::StaticClass(), TEXT("Task"), Kind, bIncludeDisallowed, Seen, Result);
	GatherNodeTypes(*Cache, EditorData, FStateTreeConditionBase::StaticStruct(),
		UStateTreeConditionBlueprintBase::StaticClass(), TEXT("Condition"), Kind, bIncludeDisallowed, Seen, Result);
	GatherNodeTypes(*Cache, EditorData, FStateTreeConsiderationBase::StaticStruct(),
		UStateTreeConsiderationBlueprintBase::StaticClass(), TEXT("Consideration"), Kind, bIncludeDisallowed, Seen, Result);
	Result.Sort([](const FBridgeStateTreeNodeTypeInfo& A, const FBridgeStateTreeNodeTypeInfo& B)
	{
		if (A.Kind != B.Kind) return A.Kind < B.Kind;
		return A.DisplayName < B.DisplayName;
	});
	return Result;
}

TArray<FBridgeStateTreeNodeInfo> UUnrealBridgeStateTreeLibrary::ListStateTreeNodes(
	const FString& AssetPath, const FString& Scope)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	TArray<FBridgeStateTreeNodeInfo> Result;
	UStateTreeEditorData* EditorData = GetEditorData(LoadStateTree(AssetPath));
	if (!EditorData)
	{
		return Result;
	}

	auto Append = [&](const TArray<FStateTreeEditorNode>& Nodes, const FString& NodeScope, const FGuid& OwnerId)
	{
		if (!MatchScopeFilter(NodeScope, Scope)) return;
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			Result.Add(MakeNodeInfo(Nodes[Index], NodeScope, OwnerId, Index));
		}
	};
	Append(EditorData->Evaluators, TEXT("Evaluator"), FGuid());
	Append(EditorData->GlobalTasks, TEXT("GlobalTask"), FGuid());
	EditorData->VisitHierarchy([&](UStateTreeState& State, UStateTreeState*)
	{
		Append(State.EnterConditions, TEXT("EnterCondition"), State.ID);
		Append(State.Tasks, TEXT("Task"), State.ID);
		if (State.SingleTask.ID.IsValid() && MatchScopeFilter(TEXT("SingleTask"), Scope))
			Result.Add(MakeNodeInfo(State.SingleTask, TEXT("SingleTask"), State.ID, 0));
		Append(State.Considerations, TEXT("Consideration"), State.ID);
		for (FStateTreeTransition& Transition : State.Transitions)
			Append(Transition.Conditions, TEXT("TransitionCondition"), Transition.ID);
		return EStateTreeVisitor::Continue;
	});
	return Result;
}

TArray<FBridgeStateTreePropertyInfo> UUnrealBridgeStateTreeLibrary::ListStateTreeNodeProperties(
	const FString& AssetPath, const FString& NodeId, const FString& DataSource, bool bIncludeInherited)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	TArray<FBridgeStateTreePropertyInfo> Result;
	UStateTreeEditorData* EditorData = GetEditorData(LoadStateTree(AssetPath));
	FGuid Guid;
	if (!EditorData || !ParseGuid(NodeId, Guid, TEXT("node"))) return Result;
	FNodeHandle Handle = FindNode(EditorData, Guid);
	if (!Handle.Node)
	{
		SetError(FString::Printf(TEXT("StateTree node '%s' was not found"), *NodeId));
		return Result;
	}

	FPropertyBindingDataView View = GetNodeDataView(*Handle.Node, DataSource);
	if (!View.IsValid()) return Result;
	const UStruct* Struct = View.GetStruct();
	UObject* Owner = Cast<UClass>(const_cast<UStruct*>(Struct))
		? static_cast<UObject*>(View.GetMutableMemory()) : nullptr;
	const EFieldIteratorFlags::SuperClassFlags Flags = bIncludeInherited
		? EFieldIteratorFlags::IncludeSuper : EFieldIteratorFlags::ExcludeSuper;
	for (TFieldIterator<FProperty> It(Struct, Flags); It; ++It)
	{
		FProperty* Property = *It;
		if (!Property || Property->HasAnyPropertyFlags(CPF_Deprecated)) continue;
		void* ValueAddress = Property->ContainerPtrToValuePtr<void>(View.GetMutableMemory());
		if (!ValueAddress) continue;

		FBridgeStateTreePropertyInfo Info;
		Info.DataSource = DataSource;
		Info.Path = Property->GetName();
		Info.DisplayName = Property->GetDisplayNameText().ToString();
		Info.Type = Property->GetCPPType(nullptr, CPPF_None);
		Property->ExportTextItem_Direct(Info.Value, ValueAddress, nullptr, Owner, PPF_None);
		const EStateTreePropertyUsage Usage = UE::StateTree::GetUsageFromMetaData(Property);
		Info.Usage = EnumToString(Usage);
		Info.Category = Property->GetMetaData(TEXT("Category"));
		Info.bEditable = Property->HasAnyPropertyFlags(CPF_Edit)
			&& !Property->HasAnyPropertyFlags(CPF_EditConst | CPF_DisableEditOnInstance);
		Info.bBindable = Usage != EStateTreePropertyUsage::Invalid
			&& !Property->GetBoolMetaData(TEXT("NoBinding"));
		Info.bInherited = Property->GetOwnerStruct() != Struct;
		Result.Add(MoveTemp(Info));
	}
	return Result;
}

FString UUnrealBridgeStateTreeLibrary::AddStateTreeNode(const FString& AssetPath, const FString& OwnerId,
	const FString& Scope, const FString& TypePath, int32 InsertIndex)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	UStateTree* Tree = LoadStateTree(AssetPath);
	UStateTreeEditorData* EditorData = GetEditorData(Tree);
	if (!EditorData) return FString();
	UStruct* Type = LoadNodeType(TypePath);
	if (!Type || !NodeMatchesScope(Type, Scope)) return FString();
	if (!IsAllowedBySchema(EditorData, Type))
	{
		SetError(FString::Printf(TEXT("schema '%s' does not allow node type '%s'"),
			*GetNameSafe(EditorData->Schema), *TypePath));
		return FString();
	}

	const FString ScopeKey = NormalizeToken(Scope);
	if (ScopeKey == TEXT("evaluator") && !EditorData->Schema->AllowEvaluators())
	{
		SetError(TEXT("this schema does not allow evaluators"));
		return FString();
	}
	if ((ScopeKey == TEXT("entercondition") || ScopeKey == TEXT("transitioncondition"))
		&& !EditorData->Schema->AllowEnterConditions())
	{
		SetError(TEXT("this schema does not allow conditions"));
		return FString();
	}
	if (ScopeKey == TEXT("consideration") && !EditorData->Schema->AllowUtilityConsiderations())
	{
		SetError(TEXT("this schema does not allow utility considerations"));
		return FString();
	}

	FNodeInsertionTarget Target;
	if (!ResolveNodeInsertionTarget(EditorData, OwnerId, Scope, Target)) return FString();
	if (Target.Single && Target.Single->ID.IsValid())
	{
		SetError(TEXT("the state already has a SingleTask; remove it before adding another"));
		return FString();
	}

	FScopedTransaction Transaction(LOCTEXT("AddStateTreeNode", "UnrealBridge: Add StateTree Node"));
	Tree->Modify();
	EditorData->Modify();
	if (Target.OwnerState) Target.OwnerState->Modify();
	FStateTreeEditorNode NewNode;
	if (!InitializeNode(NewNode, Target.Outer, Type)) return FString();
	const FGuid NewId = NewNode.ID;
	if (Target.Single)
	{
		*Target.Single = MoveTemp(NewNode);
	}
	else
	{
		const int32 TargetIndex = InsertIndex < 0 ? Target.Array->Num() : FMath::Clamp(InsertIndex, 0, Target.Array->Num());
		Target.Array->Insert(MoveTemp(NewNode), TargetIndex);
	}
	FinishMutation(Tree);
	return GuidToString(NewId);
}

bool UUnrealBridgeStateTreeLibrary::RemoveStateTreeNode(const FString& AssetPath, const FString& NodeId)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	UStateTree* Tree = LoadStateTree(AssetPath);
	UStateTreeEditorData* EditorData = GetEditorData(Tree);
	FGuid Guid;
	if (!EditorData || !ParseGuid(NodeId, Guid, TEXT("node"))) return false;
	FNodeHandle Handle = FindNode(EditorData, Guid);
	if (!Handle.Node) return SetError(FString::Printf(TEXT("node '%s' was not found"), *NodeId));
	FScopedTransaction Transaction(LOCTEXT("RemoveStateTreeNode", "UnrealBridge: Remove StateTree Node"));
	Tree->Modify();
	EditorData->Modify();
	if (Handle.OwnerState) Handle.OwnerState->Modify();
	if (Handle.bSingleTask)
		Handle.Node->Reset();
	else
		Handle.Array->RemoveAt(Handle.Index);
	FinishMutation(Tree);
	return true;
}

bool UUnrealBridgeStateTreeLibrary::MoveStateTreeNode(const FString& AssetPath,
	const FString& NodeId, int32 NewIndex)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	UStateTree* Tree = LoadStateTree(AssetPath);
	UStateTreeEditorData* EditorData = GetEditorData(Tree);
	FGuid Guid;
	if (!EditorData || !ParseGuid(NodeId, Guid, TEXT("node"))) return false;
	FNodeHandle Handle = FindNode(EditorData, Guid);
	if (!Handle.Node) return SetError(FString::Printf(TEXT("node '%s' was not found"), *NodeId));
	if (!Handle.Array) return SetError(TEXT("SingleTask nodes cannot be reordered"));
	if (Handle.Array->Num() <= 1) return true;
	const int32 TargetIndex = FMath::Clamp(NewIndex, 0, Handle.Array->Num() - 1);
	if (TargetIndex == Handle.Index) return true;
	FScopedTransaction Transaction(LOCTEXT("MoveStateTreeNode", "UnrealBridge: Move StateTree Node"));
	Tree->Modify();
	EditorData->Modify();
	if (Handle.OwnerState) Handle.OwnerState->Modify();
	FStateTreeEditorNode Node = MoveTemp((*Handle.Array)[Handle.Index]);
	Handle.Array->RemoveAt(Handle.Index);
	Handle.Array->Insert(MoveTemp(Node), TargetIndex);
	FinishMutation(Tree, false);
	return true;
}

FBridgeStateTreePropertyResult UUnrealBridgeStateTreeLibrary::GetStateTreeNodeProperty(
	const FString& AssetPath, const FString& NodeId, const FString& DataSource, const FString& PropertyPath)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	FBridgeStateTreePropertyResult Result;
	UStateTree* Tree = LoadStateTree(AssetPath);
	UStateTreeEditorData* EditorData = GetEditorData(Tree);
	FGuid Guid;
	if (!EditorData || !ParseGuid(NodeId, Guid, TEXT("node")))
	{
		Result.Error = LastError;
		return Result;
	}
	FNodeHandle Handle = FindNode(EditorData, Guid);
	if (!Handle.Node)
	{
		SetError(FString::Printf(TEXT("node '%s' was not found"), *NodeId));
		Result.Error = LastError;
		return Result;
	}
	FPropertyBindingDataView View = GetNodeDataView(*Handle.Node, DataSource);
	if (!View.IsValid())
	{
		Result.Error = LastError;
		return Result;
	}
	UObject* Owner = View.GetStruct()->IsA<UClass>() ? static_cast<UObject*>(View.GetMutableMemory()) : Tree;
	return ReadProperty(View, PropertyPath, Owner);
}

bool UUnrealBridgeStateTreeLibrary::SetStateTreeNodeProperty(const FString& AssetPath,
	const FString& NodeId, const FString& DataSource, const FString& PropertyPath, const FString& Value)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	UStateTree* Tree = LoadStateTree(AssetPath);
	UStateTreeEditorData* EditorData = GetEditorData(Tree);
	FGuid Guid;
	if (!EditorData || !ParseGuid(NodeId, Guid, TEXT("node"))) return false;
	FNodeHandle Handle = FindNode(EditorData, Guid);
	if (!Handle.Node) return SetError(FString::Printf(TEXT("node '%s' was not found"), *NodeId));
	FPropertyBindingDataView View = GetNodeDataView(*Handle.Node, DataSource);
	if (!View.IsValid()) return false;
	UObject* ValueOwner = View.GetStruct()->IsA<UClass>() ? static_cast<UObject*>(View.GetMutableMemory()) : nullptr;
	FScopedTransaction Transaction(LOCTEXT("SetStateTreeNodeProperty", "UnrealBridge: Set StateTree Node Property"));
	Tree->Modify();
	EditorData->Modify();
	if (Handle.OwnerState) Handle.OwnerState->Modify();
	if (!WriteProperty(View, PropertyPath, Value, ValueOwner)) return false;
	FinishMutation(Tree, false);
	return true;
}

TArray<FBridgeStateTreeTransitionInfo> UUnrealBridgeStateTreeLibrary::ListStateTreeTransitions(
	const FString& AssetPath, const FString& StateId)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	TArray<FBridgeStateTreeTransitionInfo> Result;
	UStateTreeEditorData* EditorData = GetEditorData(LoadStateTree(AssetPath));
	if (!EditorData) return Result;
	FGuid FilterGuid;
	if (!StateId.IsEmpty() && !ParseGuid(StateId, FilterGuid, TEXT("state"))) return Result;

	EditorData->VisitHierarchy([&](UStateTreeState& State, UStateTreeState*)
	{
		if (FilterGuid.IsValid() && State.ID != FilterGuid)
			return EStateTreeVisitor::Continue;
		for (int32 Index = 0; Index < State.Transitions.Num(); ++Index)
		{
			const FStateTreeTransition& Transition = State.Transitions[Index];
			FBridgeStateTreeTransitionInfo Info;
			Info.Id = GuidToString(Transition.ID);
			Info.StateId = GuidToString(State.ID);
			Info.Index = Index;
			Info.Trigger = TransitionTriggerToString(Transition.Trigger);
			Info.RequiredEventTag = Transition.RequiredEvent.Tag.ToString();
			Info.TargetType = EnumToString(Transition.State.LinkType);
			Info.TargetStateId = GuidToString(Transition.State.ID);
			Info.TargetStateName = Transition.State.Name.ToString();
			Info.Priority = EnumToString(Transition.Priority);
			Info.bEnabled = Transition.bTransitionEnabled;
			Info.bHasDelay = Transition.bDelayTransition;
			Info.DelayDuration = Transition.DelayDuration;
			Info.DelayRandomVariance = Transition.DelayRandomVariance;
			Info.ConditionCount = Transition.Conditions.Num();
			Result.Add(MoveTemp(Info));
		}
		return FilterGuid.IsValid() ? EStateTreeVisitor::Break : EStateTreeVisitor::Continue;
	});
	if (FilterGuid.IsValid() && !EditorData->GetStateByID(FilterGuid))
		SetError(FString::Printf(TEXT("state '%s' was not found"), *StateId));
	return Result;
}

FString UUnrealBridgeStateTreeLibrary::AddStateTreeTransition(const FString& AssetPath,
	const FString& StateId, const FString& Trigger, const FString& TargetType,
	const FString& TargetStateId, const FString& RequiredEventTag, int32 InsertIndex)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	UStateTree* Tree = LoadStateTree(AssetPath);
	UStateTreeEditorData* EditorData = GetEditorData(Tree);
	FGuid StateGuid;
	if (!EditorData || !ParseGuid(StateId, StateGuid, TEXT("state"))) return FString();
	UStateTreeState* State = FindState(EditorData, StateGuid);
	if (!State)
	{
		SetError(FString::Printf(TEXT("state '%s' was not found"), *StateId));
		return FString();
	}

	EStateTreeTransitionTrigger ParsedTrigger;
	EStateTreeTransitionType ParsedType;
	if (!ParseTransitionTrigger(Trigger, ParsedTrigger)
		|| !ParseEnumToken(TargetType, ParsedType, TEXT("transition target type"))) return FString();

	UStateTreeState* TargetState = nullptr;
	if (ParsedType == EStateTreeTransitionType::GotoState)
	{
		FGuid TargetGuid;
		if (!ParseGuid(TargetStateId, TargetGuid, TEXT("target state"))) return FString();
		TargetState = FindState(EditorData, TargetGuid);
		if (!TargetState)
		{
			SetError(FString::Printf(TEXT("target state '%s' was not found"), *TargetStateId));
			return FString();
		}
	}

	FGameplayTag EventTag;
	if (!RequiredEventTag.IsEmpty())
	{
		EventTag = FGameplayTag::RequestGameplayTag(FName(*RequiredEventTag), false);
		if (!EventTag.IsValid())
		{
			SetError(FString::Printf(TEXT("gameplay tag '%s' is not registered"), *RequiredEventTag));
			return FString();
		}
	}

	FStateTreeTransition NewTransition = EventTag.IsValid()
		? FStateTreeTransition(ParsedTrigger, EventTag, ParsedType, TargetState)
		: FStateTreeTransition(ParsedTrigger, ParsedType, TargetState);
	NewTransition.ID = FGuid::NewGuid();
	const FGuid NewId = NewTransition.ID;
	FScopedTransaction Transaction(LOCTEXT("AddStateTreeTransition", "UnrealBridge: Add StateTree Transition"));
	Tree->Modify();
	State->Modify();
	const int32 TargetIndex = InsertIndex < 0 ? State->Transitions.Num()
		: FMath::Clamp(InsertIndex, 0, State->Transitions.Num());
	State->Transitions.Insert(MoveTemp(NewTransition), TargetIndex);
	FinishMutation(Tree, false);
	return GuidToString(NewId);
}

bool UUnrealBridgeStateTreeLibrary::RemoveStateTreeTransition(const FString& AssetPath,
	const FString& TransitionId)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	UStateTree* Tree = LoadStateTree(AssetPath);
	UStateTreeEditorData* EditorData = GetEditorData(Tree);
	FGuid Guid;
	if (!EditorData || !ParseGuid(TransitionId, Guid, TEXT("transition"))) return false;
	FTransitionHandle Handle = FindTransition(EditorData, Guid);
	if (!Handle.Transition) return SetError(FString::Printf(TEXT("transition '%s' was not found"), *TransitionId));
	FScopedTransaction Transaction(LOCTEXT("RemoveStateTreeTransition", "UnrealBridge: Remove StateTree Transition"));
	Tree->Modify();
	Handle.OwnerState->Modify();
	Handle.OwnerState->Transitions.RemoveAt(Handle.Index);
	FinishMutation(Tree);
	return true;
}

bool UUnrealBridgeStateTreeLibrary::MoveStateTreeTransition(const FString& AssetPath,
	const FString& TransitionId, int32 NewIndex)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	UStateTree* Tree = LoadStateTree(AssetPath);
	UStateTreeEditorData* EditorData = GetEditorData(Tree);
	FGuid Guid;
	if (!EditorData || !ParseGuid(TransitionId, Guid, TEXT("transition"))) return false;
	FTransitionHandle Handle = FindTransition(EditorData, Guid);
	if (!Handle.Transition) return SetError(FString::Printf(TEXT("transition '%s' was not found"), *TransitionId));
	TArray<FStateTreeTransition>& Transitions = Handle.OwnerState->Transitions;
	if (Transitions.Num() <= 1) return true;
	const int32 TargetIndex = FMath::Clamp(NewIndex, 0, Transitions.Num() - 1);
	if (TargetIndex == Handle.Index) return true;
	FScopedTransaction Transaction(LOCTEXT("MoveStateTreeTransition", "UnrealBridge: Move StateTree Transition"));
	Tree->Modify();
	Handle.OwnerState->Modify();
	FStateTreeTransition Transition = MoveTemp(Transitions[Handle.Index]);
	Transitions.RemoveAt(Handle.Index);
	Transitions.Insert(MoveTemp(Transition), TargetIndex);
	FinishMutation(Tree, false);
	return true;
}

FBridgeStateTreePropertyResult UUnrealBridgeStateTreeLibrary::GetStateTreeTransitionProperty(
	const FString& AssetPath, const FString& TransitionId, const FString& PropertyPath)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	FBridgeStateTreePropertyResult Result;
	UStateTree* Tree = LoadStateTree(AssetPath);
	UStateTreeEditorData* EditorData = GetEditorData(Tree);
	FGuid Guid;
	if (!EditorData || !ParseGuid(TransitionId, Guid, TEXT("transition")))
	{
		Result.Error = LastError;
		return Result;
	}
	FTransitionHandle Handle = FindTransition(EditorData, Guid);
	if (!Handle.Transition)
	{
		SetError(FString::Printf(TEXT("transition '%s' was not found"), *TransitionId));
		Result.Error = LastError;
		return Result;
	}
	return ReadProperty(FPropertyBindingDataView(FStateTreeTransition::StaticStruct(), Handle.Transition),
		PropertyPath, Handle.OwnerState);
}

bool UUnrealBridgeStateTreeLibrary::SetStateTreeTransitionProperty(const FString& AssetPath,
	const FString& TransitionId, const FString& PropertyPath, const FString& Value)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	UStateTree* Tree = LoadStateTree(AssetPath);
	UStateTreeEditorData* EditorData = GetEditorData(Tree);
	FGuid Guid;
	if (!EditorData || !ParseGuid(TransitionId, Guid, TEXT("transition"))) return false;
	FTransitionHandle Handle = FindTransition(EditorData, Guid);
	if (!Handle.Transition) return SetError(FString::Printf(TEXT("transition '%s' was not found"), *TransitionId));
	const FString PropertyKey = TopLevelPropertyKey(PropertyPath);
	if (PropertyKey == TEXT("id") || PropertyKey == TEXT("conditions") || PropertyKey == TEXT("state"))
		return SetError(FString::Printf(TEXT("transition property '%s' is structural; use the dedicated StateTree API"),
			*PropertyPath));
	FScopedTransaction Transaction(LOCTEXT("SetStateTreeTransitionProperty", "UnrealBridge: Set StateTree Transition Property"));
	Tree->Modify();
	Handle.OwnerState->Modify();
	if (!WriteProperty(FPropertyBindingDataView(FStateTreeTransition::StaticStruct(), Handle.Transition),
		PropertyPath, Value, nullptr)) return false;
	FinishMutation(Tree, false);
	return true;
}

bool UUnrealBridgeStateTreeLibrary::SetStateTreeTransitionTarget(const FString& AssetPath,
	const FString& TransitionId, const FString& TargetType, const FString& TargetStateId)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	UStateTree* Tree = LoadStateTree(AssetPath);
	UStateTreeEditorData* EditorData = GetEditorData(Tree);
	FGuid TransitionGuid;
	if (!EditorData || !ParseGuid(TransitionId, TransitionGuid, TEXT("transition"))) return false;
	FTransitionHandle Handle = FindTransition(EditorData, TransitionGuid);
	if (!Handle.Transition) return SetError(FString::Printf(TEXT("transition '%s' was not found"), *TransitionId));
	EStateTreeTransitionType ParsedType;
	if (!ParseEnumToken(TargetType, ParsedType, TEXT("transition target type"))) return false;
	FStateTreeStateLink Link(ParsedType);
	if (ParsedType == EStateTreeTransitionType::GotoState)
	{
		FGuid StateGuid;
		if (!ParseGuid(TargetStateId, StateGuid, TEXT("target state"))) return false;
		UStateTreeState* TargetState = FindState(EditorData, StateGuid);
		if (!TargetState) return SetError(FString::Printf(TEXT("target state '%s' was not found"), *TargetStateId));
		Link = TargetState->GetLinkToState();
	}
	FScopedTransaction Transaction(LOCTEXT("SetStateTreeTransitionTarget", "UnrealBridge: Set StateTree Transition Target"));
	Tree->Modify();
	Handle.OwnerState->Modify();
	Handle.Transition->State = Link;
	FinishMutation(Tree, false);
	return true;
}

TArray<FBridgeStateTreeBindingInfo> UUnrealBridgeStateTreeLibrary::ListStateTreeBindings(
	const FString& AssetPath)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	TArray<FBridgeStateTreeBindingInfo> Result;
	UStateTreeEditorData* EditorData = GetEditorData(LoadStateTree(AssetPath));
	if (!EditorData) return Result;
	for (const FStateTreePropertyPathBinding& Binding : EditorData->EditorBindings.GetBindings())
	{
		FBridgeStateTreeBindingInfo Info;
		Info.SourceId = GuidToString(Binding.GetSourcePath().GetStructID());
		Info.SourcePath = Binding.GetSourcePath().ToString();
		Info.TargetId = GuidToString(Binding.GetTargetPath().GetStructID());
		Info.TargetPath = Binding.GetTargetPath().ToString();
		Info.bOutputBinding = Binding.IsOutputBinding();
		Result.Add(MoveTemp(Info));
	}
	return Result;
}

TArray<FBridgeStateTreeBindableStructInfo> UUnrealBridgeStateTreeLibrary::ListStateTreeBindableStructs(
	const FString& AssetPath, const FString& TargetId)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	TArray<FBridgeStateTreeBindableStructInfo> Result;
	UStateTreeEditorData* EditorData = GetEditorData(LoadStateTree(AssetPath));
	if (!EditorData) return Result;
	FGuid TargetGuid;
	if (!TargetId.IsEmpty() && !ParseGuid(TargetId, TargetGuid, TEXT("target item"))) return Result;
	TArray<TInstancedStruct<FPropertyBindingBindableStructDescriptor>> Descriptors;
	EditorData->GetBindableStructs(TargetGuid, Descriptors);
	for (const TInstancedStruct<FPropertyBindingBindableStructDescriptor>& Instanced : Descriptors)
	{
		if (!Instanced.IsValid()) continue;
		const FPropertyBindingBindableStructDescriptor& Desc = Instanced.Get();
		FBridgeStateTreeBindableStructInfo Info;
		Info.Id = GuidToString(Desc.ID);
		Info.Name = Desc.Name.ToString();
		Info.TypePath = GetPathNameSafe(Desc.Struct);
		Info.Category = Desc.Category;
		Info.Section = Desc.GetSection();
		Result.Add(MoveTemp(Info));
	}
	return Result;
}

bool UUnrealBridgeStateTreeLibrary::AddStateTreeBinding(const FString& AssetPath,
	const FString& SourceId, const FString& SourcePath, const FString& TargetId,
	const FString& TargetPath, bool bOutputBinding)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	UStateTree* Tree = LoadStateTree(AssetPath);
	UStateTreeEditorData* EditorData = GetEditorData(Tree);
	FGuid SourceGuid;
	FGuid TargetGuid;
	if (!EditorData || !ParseGuid(SourceId, SourceGuid, TEXT("binding source"))
		|| !ParseGuid(TargetId, TargetGuid, TEXT("binding target"))) return false;
	FPropertyBindingDataView SourceView;
	FPropertyBindingDataView TargetView;
	if (!EditorData->GetBindingDataViewByID(SourceGuid, SourceView))
		return SetError(FString::Printf(TEXT("binding source '%s' is not bindable"), *SourceId));
	if (!EditorData->GetBindingDataViewByID(TargetGuid, TargetView))
		return SetError(FString::Printf(TEXT("binding target '%s' is not bindable"), *TargetId));

	FPropertyBindingPath ParsedSource;
	FPropertyBindingPath ParsedTarget;
	if (!ParsedSource.FromString(SourcePath) || !ParsedTarget.FromString(TargetPath))
		return SetError(TEXT("source or target property path could not be parsed"));
	ParsedSource.SetStructID(SourceGuid);
	ParsedTarget.SetStructID(TargetGuid);
	FString ResolveError;
	if (!ParsedSource.UpdateSegmentsFromValue(SourceView, &ResolveError))
		return SetError(FString::Printf(TEXT("invalid source property path '%s': %s"), *SourcePath, *ResolveError));
	ResolveError.Reset();
	if (!ParsedTarget.UpdateSegmentsFromValue(TargetView, &ResolveError))
		return SetError(FString::Printf(TEXT("invalid target property path '%s': %s"), *TargetPath, *ResolveError));

	FScopedTransaction Transaction(LOCTEXT("AddStateTreeBinding", "UnrealBridge: Add StateTree Binding"));
	Tree->Modify();
	EditorData->Modify();
	if (bOutputBinding)
	{
		if (!EditorData->EditorBindings.AddOutputBinding(ParsedSource, ParsedTarget))
			return SetError(TEXT("StateTree rejected the output binding"));
	}
	else
	{
		EditorData->EditorBindings.AddBinding(ParsedSource, ParsedTarget);
	}
	EditorData->OnPropertyBindingChanged(ParsedSource, ParsedTarget);
	FinishMutation(Tree, false);
	return true;
}

bool UUnrealBridgeStateTreeLibrary::RemoveStateTreeBinding(const FString& AssetPath,
	const FString& TargetId, const FString& TargetPath)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	UStateTree* Tree = LoadStateTree(AssetPath);
	UStateTreeEditorData* EditorData = GetEditorData(Tree);
	FGuid TargetGuid;
	if (!EditorData || !ParseGuid(TargetId, TargetGuid, TEXT("binding target"))) return false;
	FPropertyBindingPath Path;
	if (!Path.FromString(TargetPath)) return SetError(FString::Printf(TEXT("invalid target property path '%s'"), *TargetPath));
	Path.SetStructID(TargetGuid);
	const int32 Before = EditorData->EditorBindings.GetBindings().Num();
	FScopedTransaction Transaction(LOCTEXT("RemoveStateTreeBinding", "UnrealBridge: Remove StateTree Binding"));
	Tree->Modify();
	EditorData->Modify();
	EditorData->EditorBindings.RemoveBindings(Path, FPropertyBindingBindingCollection::ESearchMode::Exact);
	if (EditorData->EditorBindings.GetBindings().Num() == Before)
		return SetError(TEXT("matching binding was not found"));
	FinishMutation(Tree, false);
	return true;
}

int32 UUnrealBridgeStateTreeLibrary::ClearStateTreeBindingsForItem(const FString& AssetPath,
	const FString& ItemId)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	UStateTree* Tree = LoadStateTree(AssetPath);
	UStateTreeEditorData* EditorData = GetEditorData(Tree);
	FGuid Guid;
	if (!EditorData || !ParseGuid(ItemId, Guid, TEXT("binding item"))) return 0;
	const int32 Before = EditorData->EditorBindings.GetBindings().Num();
	FScopedTransaction Transaction(LOCTEXT("ClearStateTreeBindingsForItem", "UnrealBridge: Clear StateTree Bindings"));
	Tree->Modify();
	EditorData->Modify();
	EditorData->EditorBindings.RemoveBindings([&](FPropertyBindingBinding& Binding)
	{
		return Binding.GetSourcePath().GetStructID() == Guid || Binding.GetTargetPath().GetStructID() == Guid;
	});
	const int32 Removed = Before - EditorData->EditorBindings.GetBindings().Num();
	FinishMutation(Tree, false);
	return Removed;
}

namespace BridgeStateTreeImpl
{

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
		Text = Text.Mid(Text.Find(TEXT("<")) + 1, Text.Len() - Text.Find(TEXT("<")) - 2);
		Text.TrimStartAndEndInline();
	}

	FString Prefix = Text;
	FString ObjectPath;
	if (Text.Split(TEXT(":"), &Prefix, &ObjectPath, ESearchCase::IgnoreCase, ESearchDir::FromStart))
	{
		Prefix.TrimStartAndEndInline();
		ObjectPath.TrimStartAndEndInline();
	}
	else
	{
		ObjectPath.Reset();
	}
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
			return SetError(FString::Printf(TEXT("type '%s' requires a ':' followed by a reflected type path"), *Prefix));
		Out.TypeObject = LoadBagTypeObject(ObjectPath, Out.ValueType);
		if (!Out.TypeObject)
			return SetError(FString::Printf(TEXT("could not load property bag type object '%s'"), *ObjectPath));
	}
	return true;
}

static FString BagTypeToString(const FPropertyBagPropertyDesc& Desc)
{
	FString Base = EnumToString(Desc.ValueType);
	if (Desc.ValueTypeObject)
	{
		Base += TEXT(":") + Desc.ValueTypeObject->GetPathName();
	}
	for (int32 Index = static_cast<int32>(Desc.ContainerTypes.Num()) - 1; Index >= 0; --Index)
	{
		Base = EnumToString(Desc.ContainerTypes[Index]) + TEXT("<") + Base + TEXT(">");
	}
	return Base;
}

static FInstancedPropertyBag& GetMutableRootBag(UStateTreeEditorData* EditorData)
{
	return const_cast<FInstancedPropertyBag&>(EditorData->GetRootParametersPropertyBag());
}

static FInstancedPropertyBag* GetParameterBag(UStateTreeEditorData* EditorData, const FString& ScopeId,
	UStateTreeState*& OutState)
{
	OutState = nullptr;
	if (ScopeId.IsEmpty())
	{
		return &GetMutableRootBag(EditorData);
	}
	FGuid StateGuid;
	if (!ParseGuid(ScopeId, StateGuid, TEXT("parameter scope state"))) return nullptr;
	OutState = FindState(EditorData, StateGuid);
	if (!OutState)
	{
		SetError(FString::Printf(TEXT("parameter scope state '%s' was not found"), *ScopeId));
		return nullptr;
	}
	return &OutState->Parameters.Parameters;
}

} // namespace BridgeStateTreeImpl

TArray<FBridgeStateTreeParameterInfo> UUnrealBridgeStateTreeLibrary::ListStateTreeParameters(
	const FString& AssetPath, const FString& ScopeId)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	TArray<FBridgeStateTreeParameterInfo> Result;
	UStateTreeEditorData* EditorData = GetEditorData(LoadStateTree(AssetPath));
	if (!EditorData) return Result;
	UStateTreeState* State = nullptr;
	FInstancedPropertyBag* Bag = GetParameterBag(EditorData, ScopeId, State);
	if (!Bag) return Result;
	const UPropertyBag* BagStruct = Bag->GetPropertyBagStruct();
	if (!BagStruct) return Result;
	for (const FPropertyBagPropertyDesc& Desc : BagStruct->GetPropertyDescs())
	{
		FBridgeStateTreeParameterInfo Info;
		Info.Id = GuidToString(Desc.ID);
		Info.Name = Desc.Name.ToString();
		Info.Type = BagTypeToString(Desc);
		const TValueOrError<FString, EPropertyBagResult> Value = Bag->GetValueSerializedString(Desc.Name);
		if (Value.HasValue()) Info.Value = Value.GetValue();
		Info.bOverridden = State && State->Parameters.PropertyOverrides.Contains(Desc.ID);
		Result.Add(MoveTemp(Info));
	}
	return Result;
}

FString UUnrealBridgeStateTreeLibrary::AddStateTreeRootParameter(const FString& AssetPath,
	const FString& Name, const FString& Type, const FString& DefaultValue)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	if (Name.TrimStartAndEnd().IsEmpty())
	{
		SetError(TEXT("root parameter name is empty"));
		return FString();
	}
	UStateTree* Tree = LoadStateTree(AssetPath);
	UStateTreeEditorData* EditorData = GetEditorData(Tree);
	if (!EditorData) return FString();
	FParsedBagType Parsed;
	if (!ParseBagType(Type, Parsed)) return FString();
	FInstancedPropertyBag& Bag = GetMutableRootBag(EditorData);
	const FName ParameterName(*Name.TrimStartAndEnd());
	if (Bag.FindPropertyDescByName(ParameterName))
	{
		SetError(FString::Printf(TEXT("root parameter '%s' already exists"), *Name));
		return FString();
	}

	FScopedTransaction Transaction(LOCTEXT("AddStateTreeRootParameter", "UnrealBridge: Add StateTree Root Parameter"));
	Tree->Modify();
	EditorData->Modify();
	const EPropertyBagAlterationResult AddResult = Parsed.Container == EPropertyBagContainerType::None
		? Bag.AddProperty(ParameterName, Parsed.ValueType, Parsed.TypeObject, false)
		: Bag.AddContainerProperty(ParameterName, Parsed.Container, Parsed.ValueType, Parsed.TypeObject, false);
	if (AddResult != EPropertyBagAlterationResult::Success)
	{
		SetError(FString::Printf(TEXT("failed to add root parameter '%s': %s"),
			*Name, *EnumToString(AddResult)));
		return FString();
	}
	if (!DefaultValue.IsEmpty() && Bag.SetValueSerializedString(ParameterName, DefaultValue) != EPropertyBagResult::Success)
	{
		Bag.RemovePropertyByName(ParameterName);
		SetError(FString::Printf(TEXT("could not import default value '%s' for root parameter '%s'"),
			*DefaultValue, *Name));
		return FString();
	}
	EditorData->OnParametersChanged(*Tree);
	FinishMutation(Tree);
	const FPropertyBagPropertyDesc* Desc = Bag.FindPropertyDescByName(ParameterName);
	return Desc ? GuidToString(Desc->ID) : FString();
}

bool UUnrealBridgeStateTreeLibrary::RemoveStateTreeRootParameter(const FString& AssetPath,
	const FString& Name)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	UStateTree* Tree = LoadStateTree(AssetPath);
	UStateTreeEditorData* EditorData = GetEditorData(Tree);
	if (!EditorData) return false;
	FInstancedPropertyBag& Bag = GetMutableRootBag(EditorData);
	if (!Bag.FindPropertyDescByName(FName(*Name)))
		return SetError(FString::Printf(TEXT("root parameter '%s' was not found"), *Name));
	FScopedTransaction Transaction(LOCTEXT("RemoveStateTreeRootParameter", "UnrealBridge: Remove StateTree Root Parameter"));
	Tree->Modify();
	EditorData->Modify();
	if (Bag.RemovePropertyByName(FName(*Name)) != EPropertyBagAlterationResult::Success)
		return SetError(FString::Printf(TEXT("failed to remove root parameter '%s'"), *Name));
	EditorData->OnParametersChanged(*Tree);
	FinishMutation(Tree);
	return true;
}

bool UUnrealBridgeStateTreeLibrary::RenameStateTreeRootParameter(const FString& AssetPath,
	const FString& OldName, const FString& NewName)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	UStateTree* Tree = LoadStateTree(AssetPath);
	UStateTreeEditorData* EditorData = GetEditorData(Tree);
	if (!EditorData || NewName.TrimStartAndEnd().IsEmpty())
	{
		if (NewName.TrimStartAndEnd().IsEmpty()) SetError(TEXT("new root parameter name is empty"));
		return false;
	}
	FInstancedPropertyBag& Bag = GetMutableRootBag(EditorData);
	FScopedTransaction Transaction(LOCTEXT("RenameStateTreeRootParameter", "UnrealBridge: Rename StateTree Root Parameter"));
	Tree->Modify();
	EditorData->Modify();
	const EPropertyBagAlterationResult Result = Bag.RenameProperty(FName(*OldName), FName(*NewName.TrimStartAndEnd()));
	if (Result != EPropertyBagAlterationResult::Success)
		return SetError(FString::Printf(TEXT("failed to rename root parameter '%s' to '%s': %s"),
			*OldName, *NewName, *EnumToString(Result)));
	EditorData->OnParametersChanged(*Tree);
	FinishMutation(Tree, false);
	return true;
}

bool UUnrealBridgeStateTreeLibrary::SetStateTreeParameterValue(const FString& AssetPath,
	const FString& ScopeId, const FString& Name, const FString& Value, bool bMarkOverridden)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	UStateTree* Tree = LoadStateTree(AssetPath);
	UStateTreeEditorData* EditorData = GetEditorData(Tree);
	if (!EditorData) return false;
	UStateTreeState* State = nullptr;
	FInstancedPropertyBag* Bag = GetParameterBag(EditorData, ScopeId, State);
	if (!Bag) return false;
	const FPropertyBagPropertyDesc* Desc = Bag->FindPropertyDescByName(FName(*Name));
	if (!Desc) return SetError(FString::Printf(TEXT("parameter '%s' was not found in the requested scope"), *Name));
	const FGuid PropertyId = Desc->ID;
	FScopedTransaction Transaction(LOCTEXT("SetStateTreeParameterValue", "UnrealBridge: Set StateTree Parameter"));
	Tree->Modify();
	EditorData->Modify();
	if (State) State->Modify();
	if (Bag->SetValueSerializedString(FName(*Name), Value) != EPropertyBagResult::Success)
		return SetError(FString::Printf(TEXT("could not import '%s' for parameter '%s'"), *Value, *Name));
	if (State)
	{
		State->SetParametersPropertyOverridden(PropertyId, bMarkOverridden);
		EditorData->OnStateParametersChanged(*Tree, State->ID);
	}
	else
	{
		EditorData->OnParametersChanged(*Tree);
	}
	FinishMutation(Tree, false);
	return true;
}

namespace BridgeStateTreeImpl
{

static bool ItemExists(UStateTreeEditorData* EditorData, const FGuid& Id,
	bool& bOutState, bool& bOutNode, bool& bOutTransition)
{
	bOutState = EditorData && EditorData->GetStateByID(Id) != nullptr;
	bOutNode = EditorData && FindNode(EditorData, Id).Node != nullptr;
	bOutTransition = EditorData && FindTransition(EditorData, Id).Transition != nullptr;
	return bOutState || bOutNode || bOutTransition;
}

static UStateTreeComponent* FindStateTreeComponent(const FString& ComponentPath)
{
	if (ComponentPath.IsEmpty())
	{
		SetError(TEXT("StateTreeComponent path is empty"));
		return nullptr;
	}
	if (UStateTreeComponent* Direct = FindObject<UStateTreeComponent>(nullptr, *ComponentPath))
	{
		return Direct;
	}
	for (TObjectIterator<UStateTreeComponent> It; It; ++It)
	{
		if (It->GetPathName() == ComponentPath)
		{
			return *It;
		}
	}
	SetError(FString::Printf(TEXT("StateTreeComponent '%s' was not found"), *ComponentPath));
	return nullptr;
}

static const UStateTree* GetComponentStateTree(const UStateTreeComponent* Component)
{
	if (!Component) return nullptr;
	const FStructProperty* Property = FindFProperty<FStructProperty>(Component->GetClass(), TEXT("StateTreeRef"));
	if (!Property || Property->Struct != FStateTreeReference::StaticStruct()) return nullptr;
	const FStateTreeReference* Reference = Property->ContainerPtrToValuePtr<FStateTreeReference>(Component);
	return Reference ? Reference->GetStateTree() : nullptr;
}

static FBridgeStateTreeComponentInfo MakeComponentInfo(UStateTreeComponent* Component)
{
	FBridgeStateTreeComponentInfo Info;
	if (!Component) return Info;
	Info.ComponentPath = Component->GetPathName();
	Info.ComponentName = Component->GetName();
	if (AActor* Owner = Component->GetOwner())
	{
		Info.OwnerPath = Owner->GetPathName();
#if WITH_EDITOR
		Info.OwnerLabel = Owner->GetActorLabel();
#endif
	}
	if (UWorld* World = Component->GetWorld())
	{
		switch (World->WorldType)
		{
		case EWorldType::Editor: Info.WorldType = TEXT("Editor"); break;
		case EWorldType::PIE: Info.WorldType = TEXT("PIE"); break;
		case EWorldType::Game: Info.WorldType = TEXT("Game"); break;
		case EWorldType::GamePreview: Info.WorldType = TEXT("GamePreview"); break;
		case EWorldType::EditorPreview: Info.WorldType = TEXT("EditorPreview"); break;
		case EWorldType::Inactive: Info.WorldType = TEXT("Inactive"); break;
		default: Info.WorldType = TEXT("Other"); break;
		}
	}
	Info.StateTreeAssetPath = GetPathNameSafe(GetComponentStateTree(Component));
	Info.RunStatus = EnumToString(Component->GetStateTreeRunStatus());
	Info.bRunning = Component->IsRunning();
	Info.bPaused = Component->IsPaused();
#if WITH_GAMEPLAY_DEBUGGER
	for (const FName StateName : Component->GetActiveStateNames())
	{
		Info.ActiveStateNames.Add(StateName.ToString());
	}
#endif
	return Info;
}

} // namespace BridgeStateTreeImpl

TArray<FBridgeStateTreeBreakpointInfo> UUnrealBridgeStateTreeLibrary::ListStateTreeBreakpoints(
	const FString& AssetPath)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	TArray<FBridgeStateTreeBreakpointInfo> Result;
	UStateTreeEditorData* EditorData = GetEditorData(LoadStateTree(AssetPath));
	if (!EditorData) return Result;
	for (const FStateTreeEditorBreakpoint& Breakpoint : EditorData->Breakpoints)
	{
		FBridgeStateTreeBreakpointInfo Info;
		Info.ItemId = GuidToString(Breakpoint.ID);
		Info.Type = EnumToString(Breakpoint.BreakpointType);
		Result.Add(MoveTemp(Info));
	}
	return Result;
}

bool UUnrealBridgeStateTreeLibrary::SetStateTreeBreakpoint(const FString& AssetPath,
	const FString& ItemId, const FString& BreakpointType, bool bEnabled)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	UStateTree* Tree = LoadStateTree(AssetPath);
	UStateTreeEditorData* EditorData = GetEditorData(Tree);
	FGuid Guid;
	EStateTreeBreakpointType Type;
	if (!EditorData || !ParseGuid(ItemId, Guid, TEXT("breakpoint item"))
		|| !ParseEnumToken(BreakpointType, Type, TEXT("breakpoint type"))) return false;
	if (Type == EStateTreeBreakpointType::Unset)
		return SetError(TEXT("Unset is not a valid breakpoint type"));
	bool bState = false;
	bool bNode = false;
	bool bTransition = false;
	if (!ItemExists(EditorData, Guid, bState, bNode, bTransition))
		return SetError(FString::Printf(TEXT("breakpoint item '%s' was not found"), *ItemId));
	if (Type == EStateTreeBreakpointType::OnTransition && !bTransition)
		return SetError(TEXT("OnTransition breakpoints require a transition GUID"));
	if (Type != EStateTreeBreakpointType::OnTransition && bTransition)
		return SetError(TEXT("transitions only support OnTransition breakpoints"));

#if WITH_STATETREE_TRACE_DEBUGGER
	EditorData->Modify();
	if (bEnabled)
	{
		if (!EditorData->HasBreakpoint(Guid, Type)) EditorData->AddBreakpoint(Guid, Type);
	}
	else
	{
		EditorData->RemoveBreakpoint(Guid, Type);
	}
	if (GEditor)
	{
		if (UStateTreeEditingSubsystem* Subsystem = GEditor->GetEditorSubsystem<UStateTreeEditingSubsystem>())
		{
			if (TSharedPtr<FStateTreeViewModel> ViewModel = Subsystem->FindViewModel(Tree))
				ViewModel->RefreshDebuggerBreakpoints();
		}
	}
	return true;
#else
	return SetError(TEXT("StateTree trace debugger support is disabled in this editor build"));
#endif
}

bool UUnrealBridgeStateTreeLibrary::ClearStateTreeBreakpoints(const FString& AssetPath)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	UStateTree* Tree = LoadStateTree(AssetPath);
	UStateTreeEditorData* EditorData = GetEditorData(Tree);
	if (!EditorData) return false;
#if WITH_STATETREE_TRACE_DEBUGGER
	EditorData->Modify();
	EditorData->RemoveAllBreakpoints();
	if (GEditor)
	{
		if (UStateTreeEditingSubsystem* Subsystem = GEditor->GetEditorSubsystem<UStateTreeEditingSubsystem>())
		{
			if (TSharedPtr<FStateTreeViewModel> ViewModel = Subsystem->FindViewModel(Tree))
				ViewModel->RefreshDebuggerBreakpoints();
		}
	}
	return true;
#else
	return SetError(TEXT("StateTree trace debugger support is disabled in this editor build"));
#endif
}

TArray<FBridgeStateTreeComponentInfo> UUnrealBridgeStateTreeLibrary::ListStateTreeComponents(bool bPIEOnly)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	TArray<FBridgeStateTreeComponentInfo> Result;
	for (TObjectIterator<UStateTreeComponent> It; It; ++It)
	{
		UStateTreeComponent* Component = *It;
		if (!IsValid(Component) || Component->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)
			|| !Component->GetWorld())
		{
			continue;
		}
		if (bPIEOnly && !Component->GetWorld()->IsPlayInEditor())
		{
			continue;
		}
		Result.Add(MakeComponentInfo(Component));
	}
	Result.Sort([](const FBridgeStateTreeComponentInfo& A, const FBridgeStateTreeComponentInfo& B)
	{
		return A.ComponentPath < B.ComponentPath;
	});
	return Result;
}

FBridgeStateTreeComponentInfo UUnrealBridgeStateTreeLibrary::GetStateTreeComponentInfo(
	const FString& ComponentPath)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	return MakeComponentInfo(FindStateTreeComponent(ComponentPath));
}

bool UUnrealBridgeStateTreeLibrary::SetStateTreeComponentAsset(const FString& ComponentPath,
	const FString& AssetPath)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	UStateTreeComponent* Component = FindStateTreeComponent(ComponentPath);
	if (!Component) return false;
	if (Component->IsRunning())
		return SetError(TEXT("stop StateTreeComponent logic before changing its StateTree asset"));
	UStateTree* Tree = AssetPath.IsEmpty() ? nullptr : LoadStateTree(AssetPath);
	if (!AssetPath.IsEmpty() && !Tree) return false;
	FScopedTransaction Transaction(LOCTEXT("SetStateTreeComponentAsset", "UnrealBridge: Set StateTree Component Asset"));
	Component->Modify();
	Component->SetStateTree(Tree);
	Component->MarkPackageDirty();
	return GetComponentStateTree(Component) == Tree;
}

bool UUnrealBridgeStateTreeLibrary::ControlStateTreeComponent(const FString& ComponentPath,
	const FString& Action, const FString& Reason)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	UStateTreeComponent* Component = FindStateTreeComponent(ComponentPath);
	if (!Component) return false;
	const FString Key = NormalizeToken(Action);
	if (Key == TEXT("start")) Component->StartLogic();
	else if (Key == TEXT("restart")) Component->RestartLogic();
	else if (Key == TEXT("stop")) Component->StopLogic(Reason);
	else if (Key == TEXT("pause")) Component->PauseLogic(Reason);
	else if (Key == TEXT("resume")) Component->ResumeLogic(Reason);
	else return SetError(FString::Printf(TEXT("unknown runtime action '%s'; expected Start, Restart, Stop, Pause, or Resume"), *Action));
	return true;
}

bool UUnrealBridgeStateTreeLibrary::SendStateTreeComponentEvent(const FString& ComponentPath,
	const FString& EventTag, const FString& Origin)
{
	using namespace BridgeStateTreeImpl;
	ClearError();
	UStateTreeComponent* Component = FindStateTreeComponent(ComponentPath);
	if (!Component) return false;
	const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*EventTag), false);
	if (!Tag.IsValid())
		return SetError(FString::Printf(TEXT("gameplay tag '%s' is not registered"), *EventTag));
	Component->SendStateTreeEvent(Tag, FConstStructView(), FName(*Origin));
	return true;
}

#undef LOCTEXT_NAMESPACE

#endif // !UE_VERSION_OLDER_THAN(5, 7, 0)
