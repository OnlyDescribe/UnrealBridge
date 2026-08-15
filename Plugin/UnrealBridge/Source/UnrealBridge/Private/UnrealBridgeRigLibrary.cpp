#include "UnrealBridgeRigLibrary.h"
#include "Misc/EngineVersionComparison.h"

#if !UE_VERSION_OLDER_THAN(5, 7, 0)

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "AssetToolsModule.h"
#include "ControlRig.h"
#include "ControlRigBlueprintFactory.h"
#include "ControlRigBlueprintLegacy.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "FileHelpers.h"
#include "IAssetTools.h"
#include "IKRigLogger.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/PackageName.h"
#include "PropertyBindingDataView.h"
#include "PropertyBindingPath.h"
#include "RetargetEditor/IKRetargetBatchOperation.h"
#include "RetargetEditor/IKRetargetFactory.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "Retargeter/IKRetargetChainMapping.h"
#include "Retargeter/IKRetargetOps.h"
#include "Retargeter/IKRetargetProcessor.h"
#include "Retargeter/IKRetargetProfile.h"
#include "Retargeter/IKRetargetSettings.h"
#include "Retargeter/IKRetargeter.h"
#include "Rig/IKRigDefinition.h"
#include "Rig/IKRigSkeleton.h"
#include "Rig/Solvers/IKRigSolverBase.h"
#include "RigEditor/IKRigController.h"
#include "RigEditor/IKRigDefinitionFactory.h"
#include "RigVMBlueprintLegacy.h"
#include "RigVMCore/RigVMRegistry.h"
#include "RigVMCore/RigVMTemplate.h"
#include "RigVMModel/RigVMController.h"
#include "RigVMModel/RigVMFunctionLibrary.h"
#include "RigVMModel/RigVMGraph.h"
#include "RigVMModel/RigVMLink.h"
#include "RigVMModel/RigVMNode.h"
#include "RigVMModel/RigVMPin.h"
#include "RigVMModel/Nodes/RigVMCommentNode.h"
#include "RigVMModel/Nodes/RigVMTemplateNode.h"
#include "RigVMModel/Nodes/RigVMUnitNode.h"
#include "RigVMModel/Nodes/RigVMVariableNode.h"
#include "Rigs/RigHierarchy.h"
#include "Rigs/RigHierarchyController.h"
#include "Rigs/RigHierarchyDefines.h"
#include "Rigs/RigHierarchyElements.h"
#include "ScopedTransaction.h"
#include "StructUtils/InstancedStruct.h"
#include "StructUtils/StructView.h"
#include "Units/RigUnit.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "UnrealBridgeRigLibrary"

namespace BridgeRigImpl
{

static FString LastError;

static void ClearError()
{
	LastError.Reset();
}

static bool SetError(const FString& Error)
{
	LastError = Error;
	UE_LOG(LogTemp, Warning, TEXT("UnrealBridge|Rig: %s"), *Error);
	return false;
}

static FString NormalizeToken(FString Value)
{
	Value.TrimStartAndEndInline();
	Value.ReplaceInline(TEXT("_"), TEXT(""));
	Value.ReplaceInline(TEXT("-"), TEXT(""));
	Value.ReplaceInline(TEXT(" "), TEXT(""));
	Value.ReplaceInline(TEXT("::"), TEXT(""));
	return Value.ToLower();
}

static FString NormalizeObjectPath(FString Path)
{
	Path.TrimStartAndEndInline();
	if (Path.StartsWith(TEXT("'")) && Path.EndsWith(TEXT("'")))
	{
		Path = Path.Mid(1, Path.Len() - 2);
	}
	int32 Quote = INDEX_NONE;
	if (Path.FindChar(TEXT('\''), Quote))
	{
		const int32 LastQuote = Path.Find(TEXT("'"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (LastQuote > Quote) Path = Path.Mid(Quote + 1, LastQuote - Quote - 1);
	}
	if (!Path.Contains(TEXT(".")))
	{
		FString AssetName;
		if (Path.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd)
			&& !AssetName.IsEmpty())
		{
			Path += TEXT(".") + AssetName;
		}
	}
	return Path;
}

static bool SplitAssetPath(const FString& InPath, FString& OutPackagePath, FString& OutAssetName)
{
	FString PackageName = InPath;
	PackageName.TrimStartAndEndInline();
	int32 Dot = INDEX_NONE;
	if (PackageName.FindChar(TEXT('.'), Dot)) PackageName = PackageName.Left(Dot);
	if (!PackageName.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(PackageName))
	{
		return SetError(FString::Printf(TEXT("'%s' is not a valid /Game asset path"), *InPath));
	}
	int32 Slash = INDEX_NONE;
	if (!PackageName.FindLastChar(TEXT('/'), Slash) || Slash <= 0 || Slash == PackageName.Len() - 1)
	{
		return SetError(FString::Printf(TEXT("asset path '%s' must include an asset name"), *InPath));
	}
	OutPackagePath = PackageName.Left(Slash);
	OutAssetName = PackageName.Mid(Slash + 1);
	return true;
}

static FString PackageObjectPath(const FString& PackagePath, const FString& AssetName)
{
	return PackagePath / AssetName + TEXT(".") + AssetName;
}

template <typename T>
static T* LoadRigAsset(const FString& AssetPath, const TCHAR* Kind)
{
	T* Asset = LoadObject<T>(nullptr, *NormalizeObjectPath(AssetPath));
	if (!Asset)
	{
		SetError(FString::Printf(TEXT("%s asset '%s' was not found or has the wrong type"), Kind, *AssetPath));
	}
	return Asset;
}

static UObject* LoadSkeletalSource(const FString& AssetPath)
{
	UObject* Object = LoadObject<UObject>(nullptr, *NormalizeObjectPath(AssetPath));
	if (!Cast<USkeletalMesh>(Object) && !Cast<USkeleton>(Object))
	{
		SetError(FString::Printf(TEXT("skeletal source '%s' must be a SkeletalMesh or Skeleton"), *AssetPath));
		return nullptr;
	}
	return Object;
}

static bool EnsureAssetDoesNotExist(const FString& PackagePath, const FString& AssetName)
{
	if (LoadObject<UObject>(nullptr, *PackageObjectPath(PackagePath, AssetName)))
	{
		return SetError(FString::Printf(TEXT("asset '%s/%s' already exists"), *PackagePath, *AssetName));
	}
	return true;
}

static bool SaveAsset(UObject* Asset)
{
	if (!Asset) return false;
	return UEditorLoadingAndSavingUtils::SavePackages({Asset->GetOutermost()}, false);
}

static void MarkChanged(UObject* Asset)
{
	if (!Asset) return;
	Asset->MarkPackageDirty();
}

template <typename EnumType>
static bool ParseEnumValue(const FString& Text, EnumType& OutValue, const TCHAR* Label)
{
	const UEnum* Enum = StaticEnum<EnumType>();
	const FString Wanted = NormalizeToken(Text);
	if (!Enum || Wanted.IsEmpty())
	{
		return SetError(FString::Printf(TEXT("%s value is empty"), Label));
	}
	for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
	{
		const int64 Value = Enum->GetValueByIndex(Index);
		const FString Name = Enum->GetNameStringByIndex(Index);
		const FString Authored = Enum->GetAuthoredNameStringByIndex(Index);
		const FString Display = Enum->GetDisplayNameTextByIndex(Index).ToString();
		if (NormalizeToken(Name) == Wanted || NormalizeToken(Authored) == Wanted || NormalizeToken(Display) == Wanted)
		{
			OutValue = static_cast<EnumType>(Value);
			return true;
		}
	}
	return SetError(FString::Printf(TEXT("unknown %s value '%s'"), Label, *Text));
}

template <typename EnumType>
static FString EnumValueToString(EnumType Value)
{
	const UEnum* Enum = StaticEnum<EnumType>();
	return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Value)) : FString();
}

static bool ParseElementType(const FString& Text, ERigElementType& OutType)
{
	const FString Token = NormalizeToken(Text);
	if (Token == TEXT("bone")) OutType = ERigElementType::Bone;
	else if (Token == TEXT("null") || Token == TEXT("space")) OutType = ERigElementType::Null;
	else if (Token == TEXT("control")) OutType = ERigElementType::Control;
	else if (Token == TEXT("curve")) OutType = ERigElementType::Curve;
	else if (Token == TEXT("connector")) OutType = ERigElementType::Connector;
	else if (Token == TEXT("socket")) OutType = ERigElementType::Socket;
	else if (Token == TEXT("reference")) OutType = ERigElementType::Reference;
	else if (Token == TEXT("all") || Token.IsEmpty()) OutType = ERigElementType::All;
	else return SetError(FString::Printf(TEXT("unknown hierarchy element type '%s'"), *Text));
	return true;
}

static FString ElementTypeToString(ERigElementType Type)
{
	switch (Type)
	{
	case ERigElementType::Bone: return TEXT("Bone");
	case ERigElementType::Null: return TEXT("Null");
	case ERigElementType::Control: return TEXT("Control");
	case ERigElementType::Curve: return TEXT("Curve");
	case ERigElementType::Connector: return TEXT("Connector");
	case ERigElementType::Socket: return TEXT("Socket");
	case ERigElementType::Reference: return TEXT("Reference");
	default: return TEXT("Unknown");
	}
}

static FRigElementKey MakeElementKey(const FString& Name, const FString& Type, bool bAllowEmpty = false)
{
	if (Name.TrimStartAndEnd().IsEmpty()) return bAllowEmpty ? FRigElementKey() : FRigElementKey();
	ERigElementType Parsed = ERigElementType::All;
	if (!ParseElementType(Type, Parsed) || Parsed == ERigElementType::All)
	{
		if (Parsed == ERigElementType::All) SetError(TEXT("a concrete hierarchy element type is required"));
		return FRigElementKey();
	}
	return FRigElementKey(FName(*Name), Parsed);
}

static UControlRigBlueprint* LoadControlRig(const FString& AssetPath)
{
	return LoadRigAsset<UControlRigBlueprint>(AssetPath, TEXT("Control Rig"));
}

static UIKRigDefinition* LoadIKRig(const FString& AssetPath)
{
	return LoadRigAsset<UIKRigDefinition>(AssetPath, TEXT("IK Rig"));
}

static UIKRetargeter* LoadRetargeter(const FString& AssetPath)
{
	return LoadRigAsset<UIKRetargeter>(AssetPath, TEXT("IK Retargeter"));
}

static URigVMGraph* FindGraph(UControlRigBlueprint* Blueprint, const FString& GraphName)
{
	if (!Blueprint) return nullptr;
	const FString Wanted = GraphName.TrimStartAndEnd();
	for (URigVMGraph* Graph : Blueprint->GetAllModels())
	{
		if (!Graph) continue;
		if (Wanted.IsEmpty() || Graph->GetName().Equals(Wanted, ESearchCase::IgnoreCase)
			|| Graph->GetGraphName().Equals(Wanted, ESearchCase::IgnoreCase)
			|| Graph->GetNodePath().Equals(Wanted, ESearchCase::IgnoreCase))
		{
			return Graph;
		}
	}
	SetError(FString::Printf(TEXT("Control Rig graph '%s' was not found"), *GraphName));
	return nullptr;
}

static URigVMController* FindGraphController(UControlRigBlueprint* Blueprint, const FString& GraphName, URigVMGraph** OutGraph = nullptr)
{
	URigVMGraph* Graph = FindGraph(Blueprint, GraphName);
	if (OutGraph) *OutGraph = Graph;
	if (!Graph) return nullptr;
	URigVMController* Controller = Blueprint->GetController(Graph);
	if (!Controller) SetError(FString::Printf(TEXT("graph '%s' has no editable RigVM controller"), *GraphName));
	return Controller;
}

struct FResolvedProperty
{
	FProperty* Property = nullptr;
	void* Value = nullptr;
	FProperty* TopLevelProperty = nullptr;
};

static bool ResolveProperty(FPropertyBindingDataView View, const FString& PropertyPath, FResolvedProperty& Out)
{
	if (!View.IsValid()) return SetError(TEXT("the requested rig settings have no instance data"));
	if (PropertyPath.TrimStartAndEnd().IsEmpty()) return SetError(TEXT("property path is empty"));
	FPropertyBindingPath Path;
	if (!Path.FromString(PropertyPath)) return SetError(FString::Printf(TEXT("invalid property path '%s'"), *PropertyPath));
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

static TArray<FBridgeRigPropertyInfo> ListProperties(FPropertyBindingDataView View, UObject* Owner)
{
	TArray<FBridgeRigPropertyInfo> Result;
	if (!View.IsValid()) return Result;
	const UStruct* Struct = View.GetStruct();
	for (TFieldIterator<FProperty> It(Struct, EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		FProperty* Property = *It;
		if (!Property || Property->HasAnyPropertyFlags(CPF_Deprecated)) continue;
		void* Address = Property->ContainerPtrToValuePtr<void>(View.GetMutableMemory());
		if (!Address) continue;
		FBridgeRigPropertyInfo Info;
		Info.Path = Property->GetName();
		Info.DisplayName = Property->GetDisplayNameText().ToString();
		Info.Type = Property->GetCPPType(nullptr, CPPF_None);
		Property->ExportTextItem_Direct(Info.Value, Address, nullptr, Owner, PPF_None);
		Info.Category = Property->GetMetaData(TEXT("Category"));
		Info.bEditable = Property->HasAnyPropertyFlags(CPF_Edit)
			&& !Property->HasAnyPropertyFlags(CPF_EditConst | CPF_DisableEditOnInstance);
		Result.Add(MoveTemp(Info));
	}
	Result.Sort([](const FBridgeRigPropertyInfo& A, const FBridgeRigPropertyInfo& B)
	{
		return A.Path < B.Path;
	});
	return Result;
}

static FBridgeRigPropertyResult ReadProperty(FPropertyBindingDataView View, const FString& PropertyPath, UObject* Owner)
{
	FBridgeRigPropertyResult Result;
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

static bool WriteProperty(FPropertyBindingDataView View, const FString& PropertyPath, const FString& Value,
	UObject* Owner, FProperty** OutTopLevelProperty = nullptr)
{
	FResolvedProperty Resolved;
	if (!ResolveProperty(View, PropertyPath, Resolved)) return false;
	if (!Resolved.Property->HasAnyPropertyFlags(CPF_Edit) || Resolved.Property->HasAnyPropertyFlags(CPF_EditConst))
	{
		return SetError(FString::Printf(TEXT("property '%s' is read-only"), *PropertyPath));
	}
	TArray<uint8> Temp;
	Temp.SetNumUninitialized(Resolved.Property->GetSize());
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
	if (OutTopLevelProperty) *OutTopLevelProperty = Resolved.TopLevelProperty ? Resolved.TopLevelProperty : Resolved.Property;
	return true;
}

static void AddIssue(FBridgeRigValidationReport& Report, const FString& Severity, const FString& Code,
	const FString& Subject, const FString& Message)
{
	FBridgeRigValidationIssue Issue;
	Issue.Severity = Severity;
	Issue.Code = Code;
	Issue.Subject = Subject;
	Issue.Message = Message;
	Report.Issues.Add(MoveTemp(Issue));
	if (Severity.Equals(TEXT("Error"), ESearchCase::IgnoreCase)) ++Report.ErrorCount;
	else if (Severity.Equals(TEXT("Warning"), ESearchCase::IgnoreCase)) ++Report.WarningCount;
}

static void AddQualityIssue(FBridgeAnimationQualityReport& Report, const FString& Severity, const FString& Code,
	const FString& Subject, const FString& Message)
{
	FBridgeRigValidationIssue Issue;
	Issue.Severity = Severity;
	Issue.Code = Code;
	Issue.Subject = Subject;
	Issue.Message = Message;
	Report.Issues.Add(MoveTemp(Issue));
}

static ERetargetSourceOrTarget ParseSide(const FString& Side, bool& bOutValid)
{
	const FString Token = NormalizeToken(Side);
	bOutValid = true;
	if (Token == TEXT("source") || Token == TEXT("from")) return ERetargetSourceOrTarget::Source;
	if (Token == TEXT("target") || Token == TEXT("to")) return ERetargetSourceOrTarget::Target;
	bOutValid = false;
	SetError(FString::Printf(TEXT("side '%s' must be Source or Target"), *Side));
	return ERetargetSourceOrTarget::Target;
}

struct FIKPropertyTarget
{
	FPropertyBindingDataView View;
	FIKRigSolverBase* Solver = nullptr;
	UIKRigEffectorGoal* Goal = nullptr;
	FIKRigSettingsBase* Settings = nullptr;
	FName TargetName = NAME_None;
	FString Kind;
};

static bool ResolveIKPropertyTarget(UIKRigController* Controller, const FString& TargetKind,
	int32 SolverIndex, const FString& TargetName, bool bCreateBoneSettings, FIKPropertyTarget& Out)
{
	if (!Controller) return false;
	Out.Kind = NormalizeToken(TargetKind);
	Out.TargetName = FName(*TargetName);
	if (Out.Kind == TEXT("goal"))
	{
		Out.Goal = Controller->GetGoal(Out.TargetName);
		if (!Out.Goal) return SetError(FString::Printf(TEXT("IK goal '%s' was not found"), *TargetName));
		Out.View = FPropertyBindingDataView(Out.Goal);
		return true;
	}
	FInstancedStruct* SolverStruct = Controller->GetSolverStructAtIndex(SolverIndex);
	Out.Solver = Controller->GetSolverAtIndex(SolverIndex);
	if (!SolverStruct || !Out.Solver)
	{
		return SetError(FString::Printf(TEXT("IK solver index %d is invalid"), SolverIndex));
	}
	const UScriptStruct* SettingsType = nullptr;
	if (Out.Kind == TEXT("solver") || Out.Kind == TEXT("solversettings"))
	{
		Out.Settings = Out.Solver->GetSolverSettings();
		SettingsType = Out.Solver->GetSolverSettingsType();
	}
	else if (Out.Kind == TEXT("goalsettings"))
	{
		if (!Out.Solver->UsesCustomGoalSettings())
			return SetError(TEXT("this solver does not expose per-goal settings"));
		Out.Settings = Out.Solver->GetGoalSettings(Out.TargetName);
		SettingsType = Out.Solver->GetGoalSettingsType();
	}
	else if (Out.Kind == TEXT("bonesettings"))
	{
		if (!Out.Solver->UsesCustomBoneSettings())
			return SetError(TEXT("this solver does not expose per-bone settings"));
		Out.Settings = Out.Solver->GetBoneSettings(Out.TargetName);
		if (!Out.Settings && bCreateBoneSettings)
		{
			if (!Controller->AddBoneSetting(Out.TargetName, SolverIndex))
				return SetError(FString::Printf(TEXT("could not add settings for bone '%s'"), *TargetName));
			Out.Settings = Out.Solver->GetBoneSettings(Out.TargetName);
		}
		SettingsType = Out.Solver->GetBoneSettingsType();
	}
	else
	{
		return SetError(FString::Printf(TEXT("TargetKind '%s' must be Solver, Goal, GoalSettings, or BoneSettings"), *TargetKind));
	}
	if (!Out.Settings || !SettingsType)
	{
		return SetError(FString::Printf(TEXT("%s settings '%s' are not available on solver %d"),
			*TargetKind, *TargetName, SolverIndex));
	}
	Out.View = FPropertyBindingDataView(FStructView(SettingsType, reinterpret_cast<uint8*>(Out.Settings)));
	return true;
}

static FMapProperty* GetProfilesProperty(UIKRetargeter* Retargeter)
{
	return Retargeter ? FindFProperty<FMapProperty>(Retargeter->GetClass(), TEXT("Profiles")) : nullptr;
}

static int32 FindProfileIndex(FScriptMapHelper& Helper, const FName ProfileName)
{
	for (FScriptMapHelper::FIterator It(Helper); It; ++It)
	{
		const FName* Key = reinterpret_cast<const FName*>(Helper.GetKeyPtr(It));
		if (Key && Key->IsEqual(ProfileName, ENameCase::IgnoreCase)) return It.GetInternalIndex();
	}
	return INDEX_NONE;
}

static FRetargetProfile* FindProfile(UIKRetargeter* Retargeter, const FName ProfileName, int32* OutIndex = nullptr)
{
	FMapProperty* ProfilesProperty = GetProfilesProperty(Retargeter);
	FStructProperty* ValueProperty = ProfilesProperty ? CastField<FStructProperty>(ProfilesProperty->ValueProp) : nullptr;
	if (!ProfilesProperty || !ValueProperty || ValueProperty->Struct != FRetargetProfile::StaticStruct())
	{
		SetError(TEXT("IK Retargeter profile storage is unavailable on this engine build"));
		return nullptr;
	}
	void* MapAddress = ProfilesProperty->ContainerPtrToValuePtr<void>(Retargeter);
	FScriptMapHelper Helper(ProfilesProperty, MapAddress);
	const int32 Index = FindProfileIndex(Helper, ProfileName);
	if (OutIndex) *OutIndex = Index;
	return Index == INDEX_NONE ? nullptr : reinterpret_cast<FRetargetProfile*>(Helper.GetValuePtr(Index));
}

static FNameProperty* GetCurrentProfileProperty(UIKRetargeter* Retargeter)
{
	return Retargeter ? FindFProperty<FNameProperty>(Retargeter->GetClass(), TEXT("CurrentProfile")) : nullptr;
}

static bool HasCoreDefaultRetargetOps(UIKRetargeterController* Controller)
{
	if (!Controller) return false;
	TSet<FName> MissingTypes = {
		TEXT("IKRetargetPelvisMotionOp"),
		TEXT("IKRetargetFKChainsOp"),
		TEXT("IKRetargetIKChainsOp"),
		TEXT("IKRetargetRunIKRigOp"),
		TEXT("IKRetargetRootMotionOp")
	};
	for (int32 Index = 0; Index < Controller->GetNumRetargetOps() && !MissingTypes.IsEmpty(); ++Index)
	{
		if (const FIKRetargetOpBase* Op = Controller->GetRetargetOpByIndex(Index))
		{
			if (const UScriptStruct* Type = Op->GetType()) MissingTypes.Remove(Type->GetFName());
		}
	}
	return MissingTypes.IsEmpty();
}

template <typename T>
static bool ImportStructText(const FString& Text, T& OutValue, const TCHAR* Label)
{
	const TCHAR* Start = *Text;
	const TCHAR* End = TBaseStructure<T>::Get()->ImportText(Start, &OutValue, nullptr, PPF_None, GLog,
		TBaseStructure<T>::Get()->GetName());
	if (!End || End == Start)
	{
		return SetError(FString::Printf(TEXT("could not parse %s value '%s'"), Label, *Text));
	}
	return true;
}

static bool MakeControlValue(ERigControlType Type, const FString& Text, FRigControlValue& OutValue)
{
	if (Text.TrimStartAndEnd().IsEmpty())
	{
		FRigControlSettings Settings;
		Settings.ControlType = Type;
		OutValue = Settings.GetIdentityValue();
		return true;
	}
	switch (Type)
	{
	case ERigControlType::Bool:
	{
		const FString Token = NormalizeToken(Text);
		if (Token == TEXT("true") || Token == TEXT("1")) OutValue.Set<bool>(true);
		else if (Token == TEXT("false") || Token == TEXT("0")) OutValue.Set<bool>(false);
		else return SetError(FString::Printf(TEXT("could not parse Bool control value '%s'"), *Text));
		return true;
	}
	case ERigControlType::Float:
	case ERigControlType::ScaleFloat:
	{
		float Value = 0.f;
		if (!LexTryParseString(Value, *Text)) return SetError(FString::Printf(TEXT("could not parse Float control value '%s'"), *Text));
		OutValue.Set<float>(Value);
		return true;
	}
	case ERigControlType::Integer:
	{
		int32 Value = 0;
		if (!LexTryParseString(Value, *Text)) return SetError(FString::Printf(TEXT("could not parse Integer control value '%s'"), *Text));
		OutValue.Set<int32>(Value);
		return true;
	}
	case ERigControlType::Vector2D:
	{
		FVector2D Value = FVector2D::ZeroVector;
		if (!ImportStructText(Text, Value, TEXT("Vector2D"))) return false;
		OutValue.Set<FVector3f>(FVector3f(Value.X, Value.Y, 0.f));
		return true;
	}
	case ERigControlType::Position:
	case ERigControlType::Scale:
	{
		FVector Value = FVector::ZeroVector;
		if (!ImportStructText(Text, Value, TEXT("Vector"))) return false;
		OutValue.Set<FVector3f>(FVector3f(Value));
		return true;
	}
	case ERigControlType::Rotator:
	{
		FRotator Value = FRotator::ZeroRotator;
		if (!ImportStructText(Text, Value, TEXT("Rotator"))) return false;
		OutValue.Set<FVector3f>(FVector3f(Value.Euler()));
		return true;
	}
	case ERigControlType::Transform:
	{
		FTransform Value = FTransform::Identity;
		if (!ImportStructText(Text, Value, TEXT("Transform"))) return false;
		OutValue.Set<FRigControlValue::FTransform_Float>(Value);
		return true;
	}
	case ERigControlType::TransformNoScale:
	{
		FTransformNoScale Value;
		if (!ImportStructText(Text, Value, TEXT("TransformNoScale"))) return false;
		OutValue.Set<FRigControlValue::FTransformNoScale_Float>(Value);
		return true;
	}
	case ERigControlType::EulerTransform:
	{
		FEulerTransform Value;
		if (!ImportStructText(Text, Value, TEXT("EulerTransform"))) return false;
		OutValue.Set<FRigControlValue::FEulerTransform_Float>(Value);
		return true;
	}
	default:
		return SetError(TEXT("unsupported Control Rig control value type"));
	}
}

} // namespace BridgeRigImpl

bool UUnrealBridgeRigLibrary::IsRigApiAvailable()
{
	BridgeRigImpl::ClearError();
	return true;
}

FString UUnrealBridgeRigLibrary::GetLastRigError()
{
	return BridgeRigImpl::LastError;
}

TArray<FBridgeRigTypeInfo> UUnrealBridgeRigLibrary::ListRigTypes(
	const FString& Kind, const FString& Query, int32 MaxResults)
{
	using namespace BridgeRigImpl;
	ClearError();
	TArray<FBridgeRigTypeInfo> Result;
	const FString KindToken = NormalizeToken(Kind);
	const FString QueryToken = Query.TrimStartAndEnd().ToLower();
	const int32 Limit = FMath::Clamp(MaxResults <= 0 ? 200 : MaxResults, 1, 2000);
	auto Matches = [&QueryToken](const FBridgeRigTypeInfo& Info)
	{
		return QueryToken.IsEmpty() || Info.TypePath.ToLower().Contains(QueryToken)
			|| Info.DisplayName.ToLower().Contains(QueryToken) || Info.Category.ToLower().Contains(QueryToken);
	};
	auto AddStructTypes = [&](UScriptStruct* Base, const FString& ResultKind)
	{
		for (TObjectIterator<UScriptStruct> It; It && Result.Num() < Limit; ++It)
		{
			UScriptStruct* Struct = *It;
			if (!Struct || Struct == Base || !Struct->IsChildOf(Base)) continue;
			FBridgeRigTypeInfo Info;
			Info.Kind = ResultKind;
			Info.TypePath = Struct->GetPathName();
			Info.DisplayName = Struct->GetDisplayNameText().ToString();
			Info.Category = Struct->GetMetaData(TEXT("Category"));
			Info.bDeprecated = Struct->HasMetaData(TEXT("Deprecated"));
			if (Matches(Info)) Result.Add(MoveTemp(Info));
		}
	};
	if (KindToken.IsEmpty() || KindToken == TEXT("controlrigunit") || KindToken == TEXT("unit"))
	{
		AddStructTypes(FRigUnit::StaticStruct(), TEXT("ControlRigUnit"));
	}
	if ((KindToken.IsEmpty() || KindToken == TEXT("rigvmtemplate") || KindToken == TEXT("template")) && Result.Num() < Limit)
	{
		for (const FRigVMTemplate& Template : FRigVMRegistry::Get().GetTemplates())
		{
			if (!Template.IsValid()) continue;
			FBridgeRigTypeInfo Info;
			Info.Kind = TEXT("RigVMTemplate");
			Info.TypePath = Template.GetNotation().ToString();
			Info.DisplayName = Template.GetName().ToString();
			Info.Category = TEXT("RigVM");
			if (Matches(Info)) Result.Add(MoveTemp(Info));
			if (Result.Num() >= Limit) break;
		}
	}
	if (KindToken.IsEmpty() || KindToken == TEXT("iksolver") || KindToken == TEXT("solver"))
	{
		AddStructTypes(FIKRigSolverBase::StaticStruct(), TEXT("IKSolver"));
	}
	if (KindToken.IsEmpty() || KindToken == TEXT("retargetop") || KindToken == TEXT("op"))
	{
		AddStructTypes(FIKRetargetOpBase::StaticStruct(), TEXT("RetargetOp"));
	}
	if (!KindToken.IsEmpty() && KindToken != TEXT("controlrigunit") && KindToken != TEXT("unit")
		&& KindToken != TEXT("rigvmtemplate") && KindToken != TEXT("template")
		&& KindToken != TEXT("iksolver") && KindToken != TEXT("solver")
		&& KindToken != TEXT("retargetop") && KindToken != TEXT("op"))
	{
		SetError(FString::Printf(TEXT("unknown rig type kind '%s'"), *Kind));
	}
	Result.Sort([](const FBridgeRigTypeInfo& A, const FBridgeRigTypeInfo& B)
	{
		return A.Kind == B.Kind ? A.DisplayName < B.DisplayName : A.Kind < B.Kind;
	});
	if (Result.Num() > Limit) Result.SetNum(Limit);
	return Result;
}

FBridgeRigOperationResult UUnrealBridgeRigLibrary::CreateControlRig(
	const FString& AssetPath, const FString& SourceSkeletalAssetPath, bool bModularRig, bool bImportCurves)
{
	using namespace BridgeRigImpl;
	ClearError();
	FBridgeRigOperationResult Result;
	FString PackagePath, AssetName;
	if (!SplitAssetPath(AssetPath, PackagePath, AssetName) || !EnsureAssetDoesNotExist(PackagePath, AssetName))
	{
		Result.Error = LastError;
		return Result;
	}
	UObject* Source = nullptr;
	if (!SourceSkeletalAssetPath.TrimStartAndEnd().IsEmpty())
	{
		Source = LoadSkeletalSource(SourceSkeletalAssetPath);
		if (!Source)
		{
			Result.Error = LastError;
			return Result;
		}
	}
	FScopedTransaction Transaction(LOCTEXT("CreateControlRig", "UnrealBridge: Create Control Rig"));
	UControlRigBlueprint* Blueprint = UControlRigBlueprintFactory::CreateNewControlRigAsset(PackagePath / AssetName, bModularRig);
	if (!Blueprint)
	{
		SetError(FString::Printf(TEXT("failed to create Control Rig '%s'"), *AssetPath));
		Result.Error = LastError;
		return Result;
	}
	if (Source)
	{
		URigHierarchyController* Controller = Blueprint->GetHierarchyController();
		if (!Controller)
		{
			SetError(TEXT("new Control Rig has no hierarchy controller"));
			Result.Error = LastError;
			return Result;
		}
		if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(Source))
		{
			Controller->ImportBones(Mesh, NAME_None, false, false, false, true);
			if (bImportCurves) Controller->ImportCurvesFromSkeletalMesh(Mesh, NAME_None, false, true);
			Blueprint->SetPreviewMesh(Mesh);
		}
		else if (USkeleton* Skeleton = Cast<USkeleton>(Source))
		{
			Controller->ImportBones(Skeleton, NAME_None, false, false, false, true);
			if (bImportCurves) Controller->ImportCurves(Skeleton, NAME_None, false, true);
		}
		Blueprint->GetSourceHierarchyImport() = Source;
		if (bImportCurves) Blueprint->GetSourceCurveImport() = Source;
		Blueprint->PropagateHierarchyFromBPToInstances();
	}
	if (!bModularRig) Blueprint->RecompileVM();
	MarkChanged(Blueprint);
	Result.bSuccess = true;
	Result.AssetPath = Blueprint->GetPathName();
	Result.Name = Blueprint->GetName();
	return Result;
}

FBridgeControlRigInfo UUnrealBridgeRigLibrary::GetControlRigInfo(const FString& AssetPath)
{
	using namespace BridgeRigImpl;
	ClearError();
	FBridgeControlRigInfo Result;
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	if (!Blueprint) return Result;
	Result.AssetPath = Blueprint->GetPathName();
	Result.PreviewSkeletalMeshPath = GetPathNameSafe(Blueprint->GetPreviewMesh());
	Result.GeneratedClassPath = GetPathNameSafe(Blueprint->GeneratedClass);
	Result.bModularRig = Blueprint->IsControlRigModule();
	Result.bDirty = Blueprint->GetOutermost()->IsDirty();
	if (URigHierarchy* Hierarchy = Blueprint->GetHierarchy())
	{
		for (const FRigElementKey& Key : Hierarchy->GetAllKeys())
		{
			switch (Key.Type)
			{
			case ERigElementType::Bone: ++Result.BoneCount; break;
			case ERigElementType::Control: ++Result.ControlCount; break;
			case ERigElementType::Null: ++Result.NullCount; break;
			case ERigElementType::Curve: ++Result.CurveCount; break;
			case ERigElementType::Connector: ++Result.ConnectorCount; break;
			default: break;
			}
		}
	}
	for (URigVMGraph* Graph : Blueprint->GetAllModels())
	{
		if (!Graph) continue;
		++Result.GraphCount;
		Result.NodeCount += Graph->GetNodes().Num();
	}
	return Result;
}

TArray<FBridgeRigElementInfo> UUnrealBridgeRigLibrary::ListControlRigElements(
	const FString& AssetPath, const FString& ElementType)
{
	using namespace BridgeRigImpl;
	ClearError();
	TArray<FBridgeRigElementInfo> Result;
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	if (!Blueprint) return Result;
	ERigElementType Filter = ERigElementType::All;
	if (!ParseElementType(ElementType, Filter)) return Result;
	URigHierarchy* Hierarchy = Blueprint->GetHierarchy();
	if (!Hierarchy) return Result;
	for (const FRigElementKey& Key : Hierarchy->GetAllKeys(false, Filter))
	{
		FBridgeRigElementInfo Info;
		Info.Name = Key.Name.ToString();
		Info.Type = ElementTypeToString(Key.Type);
		Info.DisplayName = Info.Name;
		const FRigElementKey Parent = Hierarchy->GetFirstParent(Key);
		if (Parent.IsValid())
		{
			Info.ParentName = Parent.Name.ToString();
			Info.ParentType = ElementTypeToString(Parent.Type);
		}
		if (Hierarchy->Find<FRigTransformElement>(Key))
		{
			Info.InitialLocalTransform = Hierarchy->GetLocalTransform(Key, true);
			Info.InitialGlobalTransform = Hierarchy->GetGlobalTransform(Key, true);
			Info.CurrentLocalTransform = Hierarchy->GetLocalTransform(Key, false);
			Info.CurrentGlobalTransform = Hierarchy->GetGlobalTransform(Key, false);
		}
		for (const FName Tag : Hierarchy->GetTags(Key)) Info.Tags.Add(Tag.ToString());
		if (const FRigBoneElement* Bone = Hierarchy->Find<FRigBoneElement>(Key))
		{
			Info.bImportedBone = Bone->BoneType == ERigBoneType::Imported;
		}
		if (const FRigControlElement* Control = Hierarchy->Find<FRigControlElement>(Key))
		{
			Info.DisplayName = Control->Settings.DisplayName.IsNone() ? Info.Name : Control->Settings.DisplayName.ToString();
			Info.ControlType = EnumValueToString(Control->Settings.ControlType);
		}
		Result.Add(MoveTemp(Info));
	}
	return Result;
}

bool UUnrealBridgeRigLibrary::ImportControlRigHierarchy(
	const FString& AssetPath, const FString& SourceSkeletalAssetPath, bool bReplaceExisting, bool bImportCurves)
{
	using namespace BridgeRigImpl;
	ClearError();
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	UObject* Source = LoadSkeletalSource(SourceSkeletalAssetPath);
	if (!Blueprint || !Source) return false;
	URigHierarchyController* Controller = Blueprint->GetHierarchyController();
	if (!Controller) return SetError(TEXT("Control Rig has no hierarchy controller"));
	FScopedTransaction Transaction(LOCTEXT("ImportControlRigHierarchy", "UnrealBridge: Import Control Rig Hierarchy"));
	static_cast<UBlueprint*>(Blueprint)->Modify();
	if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(Source))
	{
		Controller->ImportBones(Mesh, NAME_None, bReplaceExisting, bReplaceExisting, false, true);
		if (bImportCurves) Controller->ImportCurvesFromSkeletalMesh(Mesh, NAME_None, false, true);
		Blueprint->SetPreviewMesh(Mesh);
	}
	else
	{
		USkeleton* Skeleton = CastChecked<USkeleton>(Source);
		Controller->ImportBones(Skeleton, NAME_None, bReplaceExisting, bReplaceExisting, false, true);
		if (bImportCurves) Controller->ImportCurves(Skeleton, NAME_None, false, true);
	}
	Blueprint->GetSourceHierarchyImport() = Source;
	if (bImportCurves) Blueprint->GetSourceCurveImport() = Source;
	Blueprint->PropagateHierarchyFromBPToInstances();
	if (!Blueprint->IsControlRigModule()) Blueprint->RecompileVM();
	MarkChanged(Blueprint);
	return true;
}

FString UUnrealBridgeRigLibrary::AddControlRigBone(const FString& AssetPath, const FString& Name,
	const FString& ParentName, const FString& ParentType, const FTransform& Transform,
	bool bGlobalTransform, bool bImportedBone)
{
	using namespace BridgeRigImpl;
	ClearError();
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	if (!Blueprint || Name.TrimStartAndEnd().IsEmpty())
	{
		if (Blueprint) SetError(TEXT("bone name is empty"));
		return FString();
	}
	URigHierarchyController* Controller = Blueprint->GetHierarchyController();
	FRigElementKey Parent;
	if (!ParentName.TrimStartAndEnd().IsEmpty())
	{
		Parent = MakeElementKey(ParentName, ParentType);
		if (!Parent.IsValid() || !Blueprint->GetHierarchy()->Contains(Parent))
		{
			if (LastError.IsEmpty()) SetError(FString::Printf(TEXT("parent '%s' was not found"), *ParentName));
			return FString();
		}
	}
	FScopedTransaction Transaction(LOCTEXT("AddControlRigBone", "UnrealBridge: Add Control Rig Bone"));
	static_cast<UBlueprint*>(Blueprint)->Modify();
	const FRigElementKey Key = Controller->AddBone(FName(*Name), Parent, Transform, bGlobalTransform,
		bImportedBone ? ERigBoneType::Imported : ERigBoneType::User, true, false);
	if (!Key.IsValid())
	{
		SetError(FString::Printf(TEXT("failed to add Control Rig bone '%s'"), *Name));
		return FString();
	}
	MarkChanged(Blueprint);
	return Key.Name.ToString();
}

FString UUnrealBridgeRigLibrary::AddControlRigNull(const FString& AssetPath, const FString& Name,
	const FString& ParentName, const FString& ParentType, const FTransform& Transform, bool bGlobalTransform)
{
	using namespace BridgeRigImpl;
	ClearError();
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	if (!Blueprint || Name.TrimStartAndEnd().IsEmpty())
	{
		if (Blueprint) SetError(TEXT("null name is empty"));
		return FString();
	}
	FRigElementKey Parent;
	if (!ParentName.TrimStartAndEnd().IsEmpty())
	{
		Parent = MakeElementKey(ParentName, ParentType);
		if (!Parent.IsValid() || !Blueprint->GetHierarchy()->Contains(Parent))
		{
			if (LastError.IsEmpty()) SetError(FString::Printf(TEXT("parent '%s' was not found"), *ParentName));
			return FString();
		}
	}
	FScopedTransaction Transaction(LOCTEXT("AddControlRigNull", "UnrealBridge: Add Control Rig Null"));
	static_cast<UBlueprint*>(Blueprint)->Modify();
	const FRigElementKey Key = Blueprint->GetHierarchyController()->AddNull(
		FName(*Name), Parent, Transform, bGlobalTransform, true, false);
	if (!Key.IsValid())
	{
		SetError(FString::Printf(TEXT("failed to add Control Rig null '%s'"), *Name));
		return FString();
	}
	MarkChanged(Blueprint);
	return Key.Name.ToString();
}

FString UUnrealBridgeRigLibrary::AddControlRigControl(const FString& AssetPath, const FString& Name,
	const FString& ParentName, const FString& ParentType, const FString& ControlType,
	const FString& InitialValue, const FTransform& OffsetTransform, const FTransform& ShapeTransform,
	const FString& ShapeName, const FLinearColor& ShapeColor, bool bAnimatable)
{
	using namespace BridgeRigImpl;
	ClearError();
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	if (!Blueprint || Name.TrimStartAndEnd().IsEmpty())
	{
		if (Blueprint) SetError(TEXT("control name is empty"));
		return FString();
	}
	FRigElementKey Parent;
	if (!ParentName.TrimStartAndEnd().IsEmpty())
	{
		Parent = MakeElementKey(ParentName, ParentType);
		if (!Parent.IsValid() || !Blueprint->GetHierarchy()->Contains(Parent))
		{
			if (LastError.IsEmpty()) SetError(FString::Printf(TEXT("parent '%s' was not found"), *ParentName));
			return FString();
		}
	}
	ERigControlType ParsedType = ERigControlType::EulerTransform;
	if (!ParseEnumValue(ControlType, ParsedType, TEXT("control type"))) return FString();
	FRigControlValue Value;
	if (!MakeControlValue(ParsedType, InitialValue, Value)) return FString();
	FRigControlSettings Settings;
	Settings.ControlType = ParsedType;
	Settings.AnimationType = bAnimatable ? ERigControlAnimationType::AnimationControl : ERigControlAnimationType::VisualCue;
	Settings.DisplayName = FName(*Name);
	Settings.ShapeName = ShapeName.IsEmpty() ? FName(TEXT("Default")) : FName(*ShapeName);
	Settings.ShapeColor = ShapeColor;
	Settings.bShapeVisible = true;
	Settings.SetupLimitArrayForType();
	FScopedTransaction Transaction(LOCTEXT("AddControlRigControl", "UnrealBridge: Add Control Rig Control"));
	static_cast<UBlueprint*>(Blueprint)->Modify();
	const FRigElementKey Key = Blueprint->GetHierarchyController()->AddControl(
		FName(*Name), Parent, Settings, Value, OffsetTransform, ShapeTransform, true, false);
	if (!Key.IsValid())
	{
		SetError(FString::Printf(TEXT("failed to add Control Rig control '%s'"), *Name));
		return FString();
	}
	MarkChanged(Blueprint);
	return Key.Name.ToString();
}

FString UUnrealBridgeRigLibrary::AddControlRigCurve(const FString& AssetPath, const FString& Name, float Value)
{
	using namespace BridgeRigImpl;
	ClearError();
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	if (!Blueprint || Name.TrimStartAndEnd().IsEmpty())
	{
		if (Blueprint) SetError(TEXT("curve name is empty"));
		return FString();
	}
	FScopedTransaction Transaction(LOCTEXT("AddControlRigCurve", "UnrealBridge: Add Control Rig Curve"));
	static_cast<UBlueprint*>(Blueprint)->Modify();
	const FRigElementKey Key = Blueprint->GetHierarchyController()->AddCurve(FName(*Name), Value, true, false);
	if (!Key.IsValid())
	{
		SetError(FString::Printf(TEXT("failed to add Control Rig curve '%s'"), *Name));
		return FString();
	}
	MarkChanged(Blueprint);
	return Key.Name.ToString();
}

FString UUnrealBridgeRigLibrary::AddControlRigConnector(const FString& AssetPath, const FString& Name,
	const FString& ConnectorType, const FString& Description, bool bOptional, bool bArray)
{
	using namespace BridgeRigImpl;
	ClearError();
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	if (!Blueprint || Name.TrimStartAndEnd().IsEmpty())
	{
		if (Blueprint) SetError(TEXT("connector name is empty"));
		return FString();
	}
	EConnectorType ParsedType = EConnectorType::Secondary;
	if (!ParseEnumValue(ConnectorType, ParsedType, TEXT("connector type"))) return FString();
	FRigConnectorSettings Settings;
	Settings.Type = ParsedType;
	Settings.Description = Description;
	Settings.bOptional = ParsedType == EConnectorType::Secondary && bOptional;
	Settings.bIsArray = ParsedType == EConnectorType::Secondary && bArray;
	FScopedTransaction Transaction(LOCTEXT("AddControlRigConnector", "UnrealBridge: Add Control Rig Connector"));
	static_cast<UBlueprint*>(Blueprint)->Modify();
	const FRigElementKey Key = Blueprint->GetHierarchyController()->AddConnector(FName(*Name), Settings, true, false);
	if (!Key.IsValid())
	{
		SetError(FString::Printf(TEXT("failed to add Control Rig connector '%s'"), *Name));
		return FString();
	}
	MarkChanged(Blueprint);
	return Key.Name.ToString();
}

bool UUnrealBridgeRigLibrary::RemoveControlRigElement(
	const FString& AssetPath, const FString& Name, const FString& ElementType)
{
	using namespace BridgeRigImpl;
	ClearError();
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	if (!Blueprint) return false;
	const FRigElementKey Key = MakeElementKey(Name, ElementType);
	if (!Key.IsValid() || !Blueprint->GetHierarchy()->Contains(Key))
	{
		if (LastError.IsEmpty()) SetError(FString::Printf(TEXT("hierarchy element '%s' was not found"), *Name));
		return false;
	}
	FScopedTransaction Transaction(LOCTEXT("RemoveControlRigElement", "UnrealBridge: Remove Control Rig Element"));
	static_cast<UBlueprint*>(Blueprint)->Modify();
	if (!Blueprint->GetHierarchyController()->RemoveElement(Key, true, false))
		return SetError(FString::Printf(TEXT("failed to remove hierarchy element '%s'"), *Name));
	MarkChanged(Blueprint);
	return true;
}

FString UUnrealBridgeRigLibrary::RenameControlRigElement(const FString& AssetPath, const FString& Name,
	const FString& ElementType, const FString& NewName)
{
	using namespace BridgeRigImpl;
	ClearError();
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	if (!Blueprint || NewName.TrimStartAndEnd().IsEmpty())
	{
		if (Blueprint) SetError(TEXT("new hierarchy element name is empty"));
		return FString();
	}
	const FRigElementKey Key = MakeElementKey(Name, ElementType);
	if (!Key.IsValid() || !Blueprint->GetHierarchy()->Contains(Key))
	{
		if (LastError.IsEmpty()) SetError(FString::Printf(TEXT("hierarchy element '%s' was not found"), *Name));
		return FString();
	}
	FScopedTransaction Transaction(LOCTEXT("RenameControlRigElement", "UnrealBridge: Rename Control Rig Element"));
	static_cast<UBlueprint*>(Blueprint)->Modify();
	const FRigElementKey Renamed = Blueprint->GetHierarchyController()->RenameElement(Key, FName(*NewName), true, false, true);
	if (!Renamed.IsValid())
	{
		SetError(FString::Printf(TEXT("failed to rename hierarchy element '%s'"), *Name));
		return FString();
	}
	MarkChanged(Blueprint);
	return Renamed.Name.ToString();
}

bool UUnrealBridgeRigLibrary::ReparentControlRigElement(const FString& AssetPath, const FString& Name,
	const FString& ElementType, const FString& ParentName, const FString& ParentType, bool bMaintainGlobalTransform)
{
	using namespace BridgeRigImpl;
	ClearError();
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	if (!Blueprint) return false;
	const FRigElementKey Key = MakeElementKey(Name, ElementType);
	FRigElementKey Parent;
	if (!ParentName.TrimStartAndEnd().IsEmpty()) Parent = MakeElementKey(ParentName, ParentType);
	if (!Key.IsValid() || !Blueprint->GetHierarchy()->Contains(Key))
		return SetError(FString::Printf(TEXT("hierarchy element '%s' was not found"), *Name));
	if (!ParentName.TrimStartAndEnd().IsEmpty() && (!Parent.IsValid() || !Blueprint->GetHierarchy()->Contains(Parent)))
		return SetError(FString::Printf(TEXT("parent hierarchy element '%s' was not found"), *ParentName));
	FScopedTransaction Transaction(LOCTEXT("ReparentControlRigElement", "UnrealBridge: Reparent Control Rig Element"));
	static_cast<UBlueprint*>(Blueprint)->Modify();
	if (!Blueprint->GetHierarchyController()->SetParent(Key, Parent, bMaintainGlobalTransform, true, false))
		return SetError(FString::Printf(TEXT("failed to reparent hierarchy element '%s'"), *Name));
	MarkChanged(Blueprint);
	return true;
}

bool UUnrealBridgeRigLibrary::SetControlRigElementTransform(const FString& AssetPath, const FString& Name,
	const FString& ElementType, const FTransform& Transform, bool bGlobalTransform,
	bool bInitial, bool bAffectChildren)
{
	using namespace BridgeRigImpl;
	ClearError();
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	if (!Blueprint) return false;
	const FRigElementKey Key = MakeElementKey(Name, ElementType);
	URigHierarchy* Hierarchy = Blueprint->GetHierarchy();
	if (!Key.IsValid() || !Hierarchy->Find<FRigTransformElement>(Key))
		return SetError(FString::Printf(TEXT("transform hierarchy element '%s' was not found"), *Name));
	FScopedTransaction Transaction(LOCTEXT("SetControlRigElementTransform", "UnrealBridge: Set Control Rig Element Transform"));
	static_cast<UBlueprint*>(Blueprint)->Modify();
	if (bGlobalTransform) Hierarchy->SetGlobalTransform(Key, Transform, bInitial, bAffectChildren, true, false);
	else Hierarchy->SetLocalTransform(Key, Transform, bInitial, bAffectChildren, true, false);
	MarkChanged(Blueprint);
	return true;
}

bool UUnrealBridgeRigLibrary::SetControlRigControlShape(const FString& AssetPath, const FString& ControlName,
	const FString& ShapeName, const FLinearColor& ShapeColor, bool bVisible)
{
	using namespace BridgeRigImpl;
	ClearError();
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	if (!Blueprint) return false;
	const FRigElementKey Key(FName(*ControlName), ERigElementType::Control);
	if (!Blueprint->GetHierarchy()->Contains(Key))
		return SetError(FString::Printf(TEXT("Control Rig control '%s' was not found"), *ControlName));
	URigHierarchyController* Controller = Blueprint->GetHierarchyController();
	FRigControlSettings Settings = Controller->GetControlSettings(Key);
	Settings.ShapeName = FName(*ShapeName);
	Settings.ShapeColor = ShapeColor;
	Settings.bShapeVisible = bVisible;
	FScopedTransaction Transaction(LOCTEXT("SetControlRigControlShape", "UnrealBridge: Set Control Rig Control Shape"));
	static_cast<UBlueprint*>(Blueprint)->Modify();
	if (!Controller->SetControlSettings(Key, Settings, true))
		return SetError(FString::Printf(TEXT("failed to update control '%s' shape"), *ControlName));
	MarkChanged(Blueprint);
	return true;
}

bool UUnrealBridgeRigLibrary::AddControlRigElementTag(const FString& AssetPath, const FString& Name,
	const FString& ElementType, const FString& Tag)
{
	using namespace BridgeRigImpl;
	ClearError();
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	if (!Blueprint || Tag.TrimStartAndEnd().IsEmpty())
	{
		if (Blueprint) SetError(TEXT("tag is empty"));
		return false;
	}
	const FRigElementKey Key = MakeElementKey(Name, ElementType);
	if (!Key.IsValid() || !Blueprint->GetHierarchy()->Contains(Key))
		return SetError(FString::Printf(TEXT("hierarchy element '%s' was not found"), *Name));
	FScopedTransaction Transaction(LOCTEXT("AddControlRigElementTag", "UnrealBridge: Add Control Rig Element Tag"));
	static_cast<UBlueprint*>(Blueprint)->Modify();
	if (!Blueprint->GetHierarchy()->SetTag(Key, FName(*Tag)))
		return SetError(FString::Printf(TEXT("failed to add tag '%s'"), *Tag));
	MarkChanged(Blueprint);
	return true;
}

bool UUnrealBridgeRigLibrary::RemoveControlRigElementTag(const FString& AssetPath, const FString& Name,
	const FString& ElementType, const FString& Tag)
{
	using namespace BridgeRigImpl;
	ClearError();
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	if (!Blueprint) return false;
	const FRigElementKey Key = MakeElementKey(Name, ElementType);
	URigHierarchy* Hierarchy = Blueprint->GetHierarchy();
	if (!Key.IsValid() || !Hierarchy->Contains(Key))
		return SetError(FString::Printf(TEXT("hierarchy element '%s' was not found"), *Name));
	TArray<FName> Tags = Hierarchy->GetTags(Key);
	const int32 Removed = Tags.Remove(FName(*Tag));
	if (Removed == 0) return SetError(FString::Printf(TEXT("tag '%s' is not present on '%s'"), *Tag, *Name));
	FScopedTransaction Transaction(LOCTEXT("RemoveControlRigElementTag", "UnrealBridge: Remove Control Rig Element Tag"));
	static_cast<UBlueprint*>(Blueprint)->Modify();
	if (!Hierarchy->SetNameArrayMetadata(Key, URigHierarchy::TagMetadataName, Tags))
		return SetError(FString::Printf(TEXT("failed to remove tag '%s'"), *Tag));
	MarkChanged(Blueprint);
	return true;
}

TArray<FBridgeRigPropertyInfo> UUnrealBridgeRigLibrary::ListControlRigControlProperties(
	const FString& AssetPath, const FString& ControlName)
{
	using namespace BridgeRigImpl;
	ClearError();
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	if (!Blueprint) return {};
	const FRigElementKey Key(FName(*ControlName), ERigElementType::Control);
	if (!Blueprint->GetHierarchy()->Contains(Key))
	{
		SetError(FString::Printf(TEXT("Control Rig control '%s' was not found"), *ControlName));
		return {};
	}
	FRigControlSettings Settings = Blueprint->GetHierarchyController()->GetControlSettings(Key);
	return ListProperties(FPropertyBindingDataView(FStructView(
		FRigControlSettings::StaticStruct(), reinterpret_cast<uint8*>(&Settings))), Blueprint);
}

FBridgeRigPropertyResult UUnrealBridgeRigLibrary::GetControlRigControlProperty(
	const FString& AssetPath, const FString& ControlName, const FString& PropertyPath)
{
	using namespace BridgeRigImpl;
	ClearError();
	FBridgeRigPropertyResult Result;
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	if (!Blueprint)
	{
		Result.Error = LastError;
		return Result;
	}
	const FRigElementKey Key(FName(*ControlName), ERigElementType::Control);
	if (!Blueprint->GetHierarchy()->Contains(Key))
	{
		SetError(FString::Printf(TEXT("Control Rig control '%s' was not found"), *ControlName));
		Result.Error = LastError;
		return Result;
	}
	FRigControlSettings Settings = Blueprint->GetHierarchyController()->GetControlSettings(Key);
	return ReadProperty(FPropertyBindingDataView(FStructView(
		FRigControlSettings::StaticStruct(), reinterpret_cast<uint8*>(&Settings))),
		PropertyPath, Blueprint);
}

bool UUnrealBridgeRigLibrary::SetControlRigControlProperty(const FString& AssetPath,
	const FString& ControlName, const FString& PropertyPath, const FString& Value)
{
	using namespace BridgeRigImpl;
	ClearError();
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	if (!Blueprint) return false;
	const FRigElementKey Key(FName(*ControlName), ERigElementType::Control);
	if (!Blueprint->GetHierarchy()->Contains(Key))
		return SetError(FString::Printf(TEXT("Control Rig control '%s' was not found"), *ControlName));
	URigHierarchyController* Controller = Blueprint->GetHierarchyController();
	FRigControlSettings Settings = Controller->GetControlSettings(Key);
	if (!WriteProperty(FPropertyBindingDataView(FStructView(
		FRigControlSettings::StaticStruct(), reinterpret_cast<uint8*>(&Settings))),
		PropertyPath, Value, Blueprint)) return false;
	FScopedTransaction Transaction(LOCTEXT("SetControlRigControlProperty", "UnrealBridge: Set Control Rig Control Property"));
	static_cast<UBlueprint*>(Blueprint)->Modify();
	if (!Controller->SetControlSettings(Key, Settings, true))
		return SetError(FString::Printf(TEXT("failed to update control '%s' settings"), *ControlName));
	MarkChanged(Blueprint);
	return true;
}

TArray<FBridgeRigVMGraphInfo> UUnrealBridgeRigLibrary::ListControlRigGraphs(const FString& AssetPath)
{
	using namespace BridgeRigImpl;
	ClearError();
	TArray<FBridgeRigVMGraphInfo> Result;
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	if (!Blueprint) return Result;
	for (URigVMGraph* Graph : Blueprint->GetAllModels())
	{
		if (!Graph) continue;
		FBridgeRigVMGraphInfo Info;
		Info.Name = Graph->GetGraphName();
		Info.NodePath = Graph->GetNodePath();
		Info.NodeCount = Graph->GetNodes().Num();
		Info.LinkCount = Graph->GetLinks().Num();
		Info.bFunctionLibrary = Graph->IsA<URigVMFunctionLibrary>();
		Result.Add(MoveTemp(Info));
	}
	return Result;
}

TArray<FBridgeRigVMNodeInfo> UUnrealBridgeRigLibrary::ListControlRigNodes(
	const FString& AssetPath, const FString& GraphName)
{
	using namespace BridgeRigImpl;
	ClearError();
	TArray<FBridgeRigVMNodeInfo> Result;
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	URigVMGraph* Graph = Blueprint ? FindGraph(Blueprint, GraphName) : nullptr;
	if (!Graph) return Result;
	for (URigVMNode* Node : Graph->GetNodes())
	{
		if (!Node) continue;
		FBridgeRigVMNodeInfo Info;
		Info.Name = Node->GetName();
		Info.Path = Node->GetNodePath(true);
		Info.Title = Node->GetNodeTitle();
		Info.ClassName = Node->GetClass()->GetName();
		Info.Position = Node->GetPosition();
		for (URigVMPin* Pin : Node->GetPins())
		{
			if (!Pin) continue;
			FBridgeRigVMPinInfo PinInfo;
			PinInfo.Path = Pin->GetPinPath();
			PinInfo.Name = Pin->GetName();
			PinInfo.Direction = EnumValueToString(Pin->GetDirection());
			PinInfo.CPPType = Pin->GetCPPType();
			PinInfo.CPPTypeObjectPath = GetPathNameSafe(Pin->GetCPPTypeObject());
			PinInfo.DefaultValue = Pin->GetDefaultValue();
			PinInfo.bArray = Pin->IsArray();
			PinInfo.bLinked = Pin->IsLinked(true);
			Info.Pins.Add(MoveTemp(PinInfo));
		}
		Result.Add(MoveTemp(Info));
	}
	return Result;
}

TArray<FBridgeRigVMLinkInfo> UUnrealBridgeRigLibrary::ListControlRigLinks(
	const FString& AssetPath, const FString& GraphName)
{
	using namespace BridgeRigImpl;
	ClearError();
	TArray<FBridgeRigVMLinkInfo> Result;
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	URigVMGraph* Graph = Blueprint ? FindGraph(Blueprint, GraphName) : nullptr;
	if (!Graph) return Result;
	for (URigVMLink* Link : Graph->GetLinks())
	{
		if (!Link || !Link->GetSourcePin() || !Link->GetTargetPin()) continue;
		FBridgeRigVMLinkInfo Info;
		Info.SourcePinPath = Link->GetSourcePin()->GetPinPath();
		Info.TargetPinPath = Link->GetTargetPin()->GetPinPath();
		Result.Add(MoveTemp(Info));
	}
	return Result;
}

FString UUnrealBridgeRigLibrary::AddControlRigMemberVariable(const FString& AssetPath, const FString& Name,
	const FString& CPPType, const FString& DefaultValue, bool bPublic, bool bReadOnly)
{
	using namespace BridgeRigImpl;
	ClearError();
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	if (!Blueprint || Name.TrimStartAndEnd().IsEmpty() || CPPType.TrimStartAndEnd().IsEmpty())
	{
		if (Blueprint) SetError(TEXT("member variable name and CPPType are required"));
		return FString();
	}
	FScopedTransaction Transaction(LOCTEXT("AddControlRigMemberVariable", "UnrealBridge: Add Control Rig Member Variable"));
	static_cast<UBlueprint*>(Blueprint)->Modify();
	const FName ActualName = Blueprint->AddMemberVariable(FName(*Name), CPPType, bPublic, bReadOnly, DefaultValue);
	if (ActualName.IsNone())
	{
		SetError(FString::Printf(TEXT("failed to add Control Rig member variable '%s' of type '%s'"), *Name, *CPPType));
		return FString();
	}
	MarkChanged(Blueprint);
	return ActualName.ToString();
}

bool UUnrealBridgeRigLibrary::RemoveControlRigMemberVariable(const FString& AssetPath, const FString& Name)
{
	using namespace BridgeRigImpl;
	ClearError();
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	if (!Blueprint) return false;
	FScopedTransaction Transaction(LOCTEXT("RemoveControlRigMemberVariable", "UnrealBridge: Remove Control Rig Member Variable"));
	static_cast<UBlueprint*>(Blueprint)->Modify();
	if (!Blueprint->RemoveMemberVariable(FName(*Name)))
		return SetError(FString::Printf(TEXT("Control Rig member variable '%s' was not found"), *Name));
	MarkChanged(Blueprint);
	return true;
}

FString UUnrealBridgeRigLibrary::AddControlRigUnitNode(const FString& AssetPath, const FString& GraphName,
	const FString& UnitStructPath, const FString& MethodName, const FVector2D& Position, const FString& NodeName)
{
	using namespace BridgeRigImpl;
	ClearError();
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	URigVMController* Controller = Blueprint ? FindGraphController(Blueprint, GraphName) : nullptr;
	if (!Controller || UnitStructPath.TrimStartAndEnd().IsEmpty())
	{
		if (Controller) SetError(TEXT("unit struct path is empty"));
		return FString();
	}
	FScopedTransaction Transaction(LOCTEXT("AddControlRigUnitNode", "UnrealBridge: Add Control Rig Unit Node"));
	static_cast<UBlueprint*>(Blueprint)->Modify();
	URigVMUnitNode* Node = Controller->AddUnitNodeFromStructPath(UnitStructPath,
		MethodName.IsEmpty() ? FName(TEXT("Execute")) : FName(*MethodName), Position, NodeName, true, false);
	if (!Node)
	{
		SetError(FString::Printf(TEXT("failed to add RigVM unit '%s'"), *UnitStructPath));
		return FString();
	}
	MarkChanged(Blueprint);
	return Node->GetName();
}

FString UUnrealBridgeRigLibrary::AddControlRigTemplateNode(const FString& AssetPath, const FString& GraphName,
	const FString& Notation, const FVector2D& Position, const FString& NodeName)
{
	using namespace BridgeRigImpl;
	ClearError();
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	URigVMController* Controller = Blueprint ? FindGraphController(Blueprint, GraphName) : nullptr;
	if (!Controller || Notation.TrimStartAndEnd().IsEmpty())
	{
		if (Controller) SetError(TEXT("RigVM template notation is empty"));
		return FString();
	}
	FScopedTransaction Transaction(LOCTEXT("AddControlRigTemplateNode", "UnrealBridge: Add Control Rig Template Node"));
	static_cast<UBlueprint*>(Blueprint)->Modify();
	URigVMTemplateNode* Node = Controller->AddTemplateNode(FName(*Notation), Position, NodeName, true, false);
	if (!Node)
	{
		SetError(FString::Printf(TEXT("failed to add RigVM template '%s'"), *Notation));
		return FString();
	}
	MarkChanged(Blueprint);
	return Node->GetName();
}

FString UUnrealBridgeRigLibrary::AddControlRigVariableNode(const FString& AssetPath, const FString& GraphName,
	const FString& VariableName, const FString& CPPType, const FString& CPPTypeObjectPath, bool bGetter,
	const FString& DefaultValue, const FVector2D& Position, const FString& NodeName, bool bCreateMemberVariable)
{
	using namespace BridgeRigImpl;
	ClearError();
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	URigVMController* Controller = Blueprint ? FindGraphController(Blueprint, GraphName) : nullptr;
	if (!Controller || VariableName.TrimStartAndEnd().IsEmpty() || CPPType.TrimStartAndEnd().IsEmpty())
	{
		if (Controller) SetError(TEXT("variable name and CPPType are required"));
		return FString();
	}
	FScopedTransaction Transaction(LOCTEXT("AddControlRigVariableNode", "UnrealBridge: Add Control Rig Variable Node"));
	static_cast<UBlueprint*>(Blueprint)->Modify();
	FName ActualVariableName(*VariableName);
	if (bCreateMemberVariable)
	{
		ActualVariableName = Blueprint->AddMemberVariable(ActualVariableName, CPPType, true, false, DefaultValue);
		if (ActualVariableName.IsNone())
		{
			SetError(FString::Printf(TEXT("failed to create Control Rig member variable '%s'"), *VariableName));
			return FString();
		}
	}
	URigVMVariableNode* Node = Controller->AddVariableNodeFromObjectPath(ActualVariableName, CPPType,
		CPPTypeObjectPath, bGetter, DefaultValue, Position, NodeName, true, false);
	if (!Node)
	{
		SetError(FString::Printf(TEXT("failed to add RigVM variable node '%s'"), *ActualVariableName.ToString()));
		return FString();
	}
	MarkChanged(Blueprint);
	return Node->GetName();
}

FString UUnrealBridgeRigLibrary::AddControlRigCommentNode(const FString& AssetPath, const FString& GraphName,
	const FString& CommentText, const FVector2D& Position, const FVector2D& Size,
	const FLinearColor& Color, const FString& NodeName)
{
	using namespace BridgeRigImpl;
	ClearError();
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	URigVMController* Controller = Blueprint ? FindGraphController(Blueprint, GraphName) : nullptr;
	if (!Controller) return FString();
	FScopedTransaction Transaction(LOCTEXT("AddControlRigCommentNode", "UnrealBridge: Add Control Rig Comment Node"));
	static_cast<UBlueprint*>(Blueprint)->Modify();
	URigVMCommentNode* Node = Controller->AddCommentNode(CommentText, Position, Size, Color, NodeName, true, false);
	if (!Node)
	{
		SetError(TEXT("failed to add RigVM comment node"));
		return FString();
	}
	MarkChanged(Blueprint);
	return Node->GetName();
}

FString UUnrealBridgeRigLibrary::AddControlRigBranchNode(const FString& AssetPath, const FString& GraphName,
	const FVector2D& Position, const FString& NodeName)
{
	using namespace BridgeRigImpl;
	ClearError();
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	URigVMController* Controller = Blueprint ? FindGraphController(Blueprint, GraphName) : nullptr;
	if (!Controller) return FString();
	FScopedTransaction Transaction(LOCTEXT("AddControlRigBranchNode", "UnrealBridge: Add Control Rig Branch Node"));
	static_cast<UBlueprint*>(Blueprint)->Modify();
	URigVMNode* Node = Controller->AddBranchNode(Position, NodeName, true, false);
	if (!Node)
	{
		SetError(TEXT("failed to add RigVM branch node"));
		return FString();
	}
	MarkChanged(Blueprint);
	return Node->GetName();
}

bool UUnrealBridgeRigLibrary::RemoveControlRigNode(
	const FString& AssetPath, const FString& GraphName, const FString& NodeName)
{
	using namespace BridgeRigImpl;
	ClearError();
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	URigVMController* Controller = Blueprint ? FindGraphController(Blueprint, GraphName) : nullptr;
	if (!Controller) return false;
	FScopedTransaction Transaction(LOCTEXT("RemoveControlRigNode", "UnrealBridge: Remove Control Rig Node"));
	static_cast<UBlueprint*>(Blueprint)->Modify();
	if (!Controller->RemoveNodeByName(FName(*NodeName), true, false))
		return SetError(FString::Printf(TEXT("RigVM node '%s' was not found or could not be removed"), *NodeName));
	MarkChanged(Blueprint);
	return true;
}

bool UUnrealBridgeRigLibrary::SetControlRigNodePosition(const FString& AssetPath, const FString& GraphName,
	const FString& NodeName, const FVector2D& Position)
{
	using namespace BridgeRigImpl;
	ClearError();
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	URigVMController* Controller = Blueprint ? FindGraphController(Blueprint, GraphName) : nullptr;
	if (!Controller) return false;
	FScopedTransaction Transaction(LOCTEXT("SetControlRigNodePosition", "UnrealBridge: Move Control Rig Node"));
	static_cast<UBlueprint*>(Blueprint)->Modify();
	if (!Controller->SetNodePositionByName(FName(*NodeName), Position, true, false, false))
		return SetError(FString::Printf(TEXT("RigVM node '%s' was not found"), *NodeName));
	MarkChanged(Blueprint);
	return true;
}

bool UUnrealBridgeRigLibrary::SetControlRigPinDefaultValue(const FString& AssetPath, const FString& GraphName,
	const FString& PinPath, const FString& DefaultValue, bool bResizeArrays)
{
	using namespace BridgeRigImpl;
	ClearError();
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	URigVMController* Controller = Blueprint ? FindGraphController(Blueprint, GraphName) : nullptr;
	if (!Controller) return false;
	FScopedTransaction Transaction(LOCTEXT("SetControlRigPinDefaultValue", "UnrealBridge: Set Control Rig Pin Default"));
	static_cast<UBlueprint*>(Blueprint)->Modify();
	if (!Controller->SetPinDefaultValue(PinPath, DefaultValue, bResizeArrays, true, false, false, true))
		return SetError(FString::Printf(TEXT("failed to set RigVM pin '%s' default value"), *PinPath));
	MarkChanged(Blueprint);
	return true;
}

bool UUnrealBridgeRigLibrary::ConnectControlRigPins(const FString& AssetPath, const FString& GraphName,
	const FString& OutputPinPath, const FString& InputPinPath, bool bCreateCastNode)
{
	using namespace BridgeRigImpl;
	ClearError();
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	URigVMController* Controller = Blueprint ? FindGraphController(Blueprint, GraphName) : nullptr;
	if (!Controller) return false;
	FScopedTransaction Transaction(LOCTEXT("ConnectControlRigPins", "UnrealBridge: Connect Control Rig Pins"));
	static_cast<UBlueprint*>(Blueprint)->Modify();
	if (!Controller->AddLink(OutputPinPath, InputPinPath, true, false, ERigVMPinDirection::Output, bCreateCastNode))
		return SetError(FString::Printf(TEXT("failed to connect RigVM pins '%s' -> '%s'"), *OutputPinPath, *InputPinPath));
	MarkChanged(Blueprint);
	return true;
}

bool UUnrealBridgeRigLibrary::DisconnectControlRigPins(const FString& AssetPath, const FString& GraphName,
	const FString& OutputPinPath, const FString& InputPinPath)
{
	using namespace BridgeRigImpl;
	ClearError();
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	URigVMController* Controller = Blueprint ? FindGraphController(Blueprint, GraphName) : nullptr;
	if (!Controller) return false;
	FScopedTransaction Transaction(LOCTEXT("DisconnectControlRigPins", "UnrealBridge: Disconnect Control Rig Pins"));
	static_cast<UBlueprint*>(Blueprint)->Modify();
	if (!Controller->BreakLink(OutputPinPath, InputPinPath, true, false))
		return SetError(FString::Printf(TEXT("RigVM link '%s' -> '%s' was not found"), *OutputPinPath, *InputPinPath));
	MarkChanged(Blueprint);
	return true;
}

FBridgeRigLayoutResult UUnrealBridgeRigLibrary::AutoLayoutControlRigGraph(const FString& AssetPath,
	const FString& GraphName, float HorizontalSpacing, float VerticalSpacing)
{
	using namespace BridgeRigImpl;
	ClearError();
	FBridgeRigLayoutResult Result;
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	URigVMGraph* Graph = nullptr;
	URigVMController* Controller = Blueprint ? FindGraphController(Blueprint, GraphName, &Graph) : nullptr;
	if (!Controller || !Graph) return Result;
	HorizontalSpacing = FMath::Max(100.f, HorizontalSpacing);
	VerticalSpacing = FMath::Max(60.f, VerticalSpacing);
	TMap<URigVMNode*, int32> Layers;
	for (URigVMNode* Node : Graph->GetNodes()) if (Node) Layers.Add(Node, 0);
	bool bChangedOnLastPass = false;
	for (int32 Pass = 0; Pass < FMath::Max(1, Layers.Num()); ++Pass)
	{
		bChangedOnLastPass = false;
		for (URigVMLink* Link : Graph->GetLinks())
		{
			URigVMNode* Source = Link && Link->GetSourcePin() ? Link->GetSourcePin()->GetNode() : nullptr;
			URigVMNode* Target = Link && Link->GetTargetPin() ? Link->GetTargetPin()->GetNode() : nullptr;
			if (!Source || !Target || Source == Target) continue;
			const int32 Desired = Layers.FindRef(Source) + 1;
			int32& TargetLayer = Layers.FindOrAdd(Target);
			if (Desired > TargetLayer)
			{
				TargetLayer = Desired;
				bChangedOnLastPass = true;
			}
		}
		if (!bChangedOnLastPass) break;
	}
	if (bChangedOnLastPass)
	{
		Result.Warnings.Add(TEXT("graph contains a cycle; cyclic nodes were placed using the final relaxation pass"));
		for (TPair<URigVMNode*, int32>& Pair : Layers) Pair.Value = FMath::Min(Pair.Value, Layers.Num() - 1);
	}
	TMap<int32, TArray<URigVMNode*>> ByLayer;
	for (const TPair<URigVMNode*, int32>& Pair : Layers) ByLayer.FindOrAdd(Pair.Value).Add(Pair.Key);
	FScopedTransaction Transaction(LOCTEXT("AutoLayoutControlRigGraph", "UnrealBridge: Auto Layout Control Rig Graph"));
	static_cast<UBlueprint*>(Blueprint)->Modify();
	for (TPair<int32, TArray<URigVMNode*>>& Pair : ByLayer)
	{
		Pair.Value.Sort([](const URigVMNode& A, const URigVMNode& B)
		{
			if (!FMath::IsNearlyEqual(A.GetPosition().Y, B.GetPosition().Y)) return A.GetPosition().Y < B.GetPosition().Y;
			return A.GetName() < B.GetName();
		});
		for (int32 Row = 0; Row < Pair.Value.Num(); ++Row)
		{
			URigVMNode* Node = Pair.Value[Row];
			if (Controller->SetNodePositionByName(Node->GetFName(),
				FVector2D(Pair.Key * HorizontalSpacing, Row * VerticalSpacing), true, false, false))
			{
				++Result.NodesPositioned;
			}
		}
	}
	Result.LayerCount = ByLayer.Num();
	Result.bSuccess = Result.NodesPositioned == Layers.Num();
	MarkChanged(Blueprint);
	return Result;
}

FBridgeRigValidationReport UUnrealBridgeRigLibrary::CompileControlRig(const FString& AssetPath, bool bSave)
{
	using namespace BridgeRigImpl;
	ClearError();
	FBridgeRigValidationReport Report;
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	if (!Blueprint)
	{
		Report.Error = LastError;
		return Report;
	}
	Report.bFound = true;
	FCompilerResultsLog CompileLog;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &CompileLog);
	Report.ErrorCount = CompileLog.NumErrors;
	Report.WarningCount = CompileLog.NumWarnings;
	for (const TSharedRef<FTokenizedMessage>& Message : CompileLog.Messages)
	{
		FBridgeRigValidationIssue Issue;
		switch (Message->GetSeverity())
		{
		case EMessageSeverity::Error: Issue.Severity = TEXT("Error"); break;
		case EMessageSeverity::Warning:
		case EMessageSeverity::PerformanceWarning: Issue.Severity = TEXT("Warning"); break;
		default: Issue.Severity = TEXT("Info"); break;
		}
		Issue.Code = TEXT("ControlRigCompile");
		Issue.Subject = Blueprint->GetPathName();
		Issue.Message = Message->ToText().ToString();
		Report.Issues.Add(MoveTemp(Issue));
	}
	Report.bCompiled = Report.ErrorCount == 0;
	if (bSave) Report.bSaved = SaveAsset(Blueprint);
	Report.bSuccess = Report.bCompiled && (!bSave || Report.bSaved);
	if (!Report.bSuccess)
	{
		Report.Error = !Report.bCompiled ? TEXT("Control Rig compilation failed") : TEXT("Control Rig package could not be saved");
		LastError = Report.Error;
	}
	return Report;
}

FBridgeRigValidationReport UUnrealBridgeRigLibrary::ValidateControlRig(const FString& AssetPath, bool bSave)
{
	using namespace BridgeRigImpl;
	FBridgeRigValidationReport Report = CompileControlRig(AssetPath, false);
	UControlRigBlueprint* Blueprint = Report.bFound ? LoadControlRig(AssetPath) : nullptr;
	if (!Blueprint) return Report;
	URigHierarchy* Hierarchy = Blueprint->GetHierarchy();
	if (!Hierarchy)
	{
		AddIssue(Report, TEXT("Error"), TEXT("MissingHierarchy"), Blueprint->GetPathName(),
			TEXT("Control Rig has no hierarchy"));
	}
	else
	{
		const int32 BoneCount = Hierarchy->GetAllKeys(false, ERigElementType::Bone).Num();
		const int32 ControlCount = Hierarchy->GetAllKeys(false, ERigElementType::Control).Num();
		if (BoneCount == 0) AddIssue(Report, TEXT("Warning"), TEXT("NoBones"), Blueprint->GetPathName(),
			TEXT("Control Rig hierarchy contains no bones"));
		if (ControlCount == 0) AddIssue(Report, TEXT("Warning"), TEXT("NoControls"), Blueprint->GetPathName(),
			TEXT("Control Rig hierarchy contains no controls"));
	}
	int32 ExecutableNodes = 0;
	for (URigVMGraph* Graph : Blueprint->GetAllModels()) if (Graph) ExecutableNodes += Graph->GetNodes().Num();
	if (ExecutableNodes == 0) AddIssue(Report, TEXT("Warning"), TEXT("EmptyRigVM"), Blueprint->GetPathName(),
		TEXT("Control Rig has no RigVM nodes"));
	Report.bCompiled = Report.ErrorCount == 0;
	if (bSave) Report.bSaved = SaveAsset(Blueprint);
	Report.bSuccess = Report.ErrorCount == 0 && (!bSave || Report.bSaved);
	if (!Report.bSuccess && Report.Error.IsEmpty()) Report.Error = TEXT("Control Rig validation failed");
	return Report;
}

FBridgeControlRigEvaluationResult UUnrealBridgeRigLibrary::EvaluateControlRig(const FString& AssetPath,
	const FString& EventName, const TArray<FBridgeRigNamedTransform>& InputControls)
{
	using namespace BridgeRigImpl;
	ClearError();
	FBridgeControlRigEvaluationResult Result;
	UControlRigBlueprint* Blueprint = LoadControlRig(AssetPath);
	if (!Blueprint)
	{
		Result.Error = LastError;
		return Result;
	}
	UControlRig* Rig = Blueprint->CreateControlRig();
	if (!Rig)
	{
		SetError(TEXT("Control Rig instance could not be created; compile the asset first"));
		Result.Error = LastError;
		return Result;
	}
	Rig->Initialize(true);
	URigHierarchy* Hierarchy = Rig->GetHierarchy();
	if (!Hierarchy)
	{
		SetError(TEXT("Control Rig instance has no hierarchy"));
		Result.Error = LastError;
		return Result;
	}
	for (const FBridgeRigNamedTransform& Input : InputControls)
	{
		ERigElementType Type = ERigElementType::Control;
		if (!Input.Type.TrimStartAndEnd().IsEmpty() && !ParseElementType(Input.Type, Type))
		{
			Result.Error = LastError;
			return Result;
		}
		const FRigElementKey Key(FName(*Input.Name), Type);
		if (!Hierarchy->Find<FRigTransformElement>(Key))
		{
			SetError(FString::Printf(TEXT("evaluation input element '%s' was not found"), *Input.Name));
			Result.Error = LastError;
			return Result;
		}
		Hierarchy->SetGlobalTransform(Key, Input.Transform, false, true, false, false);
	}
	Result.EventName = EventName.IsEmpty() ? TEXT("Forwards Solve") : EventName;
	if (!Rig->Execute(FName(*Result.EventName)))
	{
		SetError(FString::Printf(TEXT("Control Rig event '%s' could not execute"), *Result.EventName));
		Result.Error = LastError;
		return Result;
	}
	for (const FRigElementKey& Key : Hierarchy->GetAllKeys(false, ERigElementType::Control))
	{
		FBridgeRigNamedTransform Item;
		Item.Name = Key.Name.ToString();
		Item.Type = TEXT("Control");
		Item.Transform = Hierarchy->GetGlobalTransform(Key, false);
		Result.Controls.Add(MoveTemp(Item));
	}
	for (const FRigElementKey& Key : Hierarchy->GetAllKeys(false, ERigElementType::Bone))
	{
		FBridgeRigNamedTransform Item;
		Item.Name = Key.Name.ToString();
		Item.Type = TEXT("Bone");
		Item.Transform = Hierarchy->GetGlobalTransform(Key, false);
		Result.Bones.Add(MoveTemp(Item));
	}
	Result.bSuccess = true;
	return Result;
}

FBridgeRigOperationResult UUnrealBridgeRigLibrary::CreateIKRig(
	const FString& AssetPath, const FString& SkeletalMeshPath)
{
	using namespace BridgeRigImpl;
	ClearError();
	FBridgeRigOperationResult Result;
	FString PackagePath, AssetName;
	if (!SplitAssetPath(AssetPath, PackagePath, AssetName) || !EnsureAssetDoesNotExist(PackagePath, AssetName))
	{
		Result.Error = LastError;
		return Result;
	}
	USkeletalMesh* Mesh = nullptr;
	if (!SkeletalMeshPath.TrimStartAndEnd().IsEmpty())
	{
		Mesh = LoadRigAsset<USkeletalMesh>(SkeletalMeshPath, TEXT("Skeletal Mesh"));
		if (!Mesh)
		{
			Result.Error = LastError;
			return Result;
		}
	}
	FScopedTransaction Transaction(LOCTEXT("CreateIKRig", "UnrealBridge: Create IK Rig"));
	UIKRigDefinitionFactory* Factory = NewObject<UIKRigDefinitionFactory>();
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UIKRigDefinition* Rig = Cast<UIKRigDefinition>(AssetTools.CreateAsset(
		AssetName, PackagePath, UIKRigDefinition::StaticClass(), Factory));
	if (!Rig)
	{
		SetError(FString::Printf(TEXT("failed to create IK Rig '%s'"), *AssetPath));
		Result.Error = LastError;
		return Result;
	}
	if (Mesh)
	{
		UIKRigController* Controller = UIKRigController::GetController(Rig);
		if (!Controller || !Controller->SetSkeletalMesh(Mesh))
		{
			SetError(FString::Printf(TEXT("IK Rig was created but mesh '%s' could not be assigned"), *SkeletalMeshPath));
			Result.AssetPath = Rig->GetPathName();
			Result.Error = LastError;
			return Result;
		}
	}
	MarkChanged(Rig);
	Result.bSuccess = true;
	Result.AssetPath = Rig->GetPathName();
	Result.Name = Rig->GetName();
	return Result;
}

FBridgeIKRigInfo UUnrealBridgeRigLibrary::GetIKRigInfo(const FString& AssetPath)
{
	using namespace BridgeRigImpl;
	ClearError();
	FBridgeIKRigInfo Result;
	UIKRigDefinition* Rig = LoadIKRig(AssetPath);
	if (!Rig) return Result;
	UIKRigController* Controller = UIKRigController::GetController(Rig);
	if (!Controller)
	{
		SetError(TEXT("IK Rig has no controller"));
		return Result;
	}
	USkeletalMesh* Mesh = Controller->GetSkeletalMesh();
	Result.AssetPath = Rig->GetPathName();
	Result.PreviewSkeletalMeshPath = GetPathNameSafe(Mesh);
	Result.SkeletonPath = Mesh ? GetPathNameSafe(Mesh->GetSkeleton()) : FString();
	Result.RetargetRoot = Controller->GetRetargetRoot().ToString();
	Result.BoneCount = Controller->GetIKRigSkeleton().BoneNames.Num();
	Result.SolverCount = Controller->GetNumSolvers();
	Result.GoalCount = Controller->GetAllGoals().Num();
	Result.ChainCount = Controller->GetRetargetChains().Num();
	Result.bDirty = Rig->GetOutermost()->IsDirty();
	return Result;
}

TArray<FBridgeIKSolverInfo> UUnrealBridgeRigLibrary::ListIKRigSolvers(const FString& AssetPath)
{
	using namespace BridgeRigImpl;
	ClearError();
	TArray<FBridgeIKSolverInfo> Result;
	UIKRigDefinition* Rig = LoadIKRig(AssetPath);
	UIKRigController* Controller = Rig ? UIKRigController::GetController(Rig) : nullptr;
	if (!Controller) return Result;
	for (int32 Index = 0; Index < Controller->GetNumSolvers(); ++Index)
	{
		FIKRigSolverBase* Solver = Controller->GetSolverAtIndex(Index);
		FInstancedStruct* SolverStruct = Controller->GetSolverStructAtIndex(Index);
		if (!Solver || !SolverStruct) continue;
		FBridgeIKSolverInfo Info;
		Info.Index = Index;
		Info.TypePath = GetPathNameSafe(SolverStruct->GetScriptStruct());
		Info.DisplayName = Solver->GetNiceName().ToString();
		Info.StartBone = Controller->GetStartBone(Index).ToString();
		Info.EndBone = Controller->GetEndBone(Index).ToString();
		Info.bEnabled = Controller->GetSolverEnabled(Index);
		TSet<FName> Goals;
		Solver->GetRequiredGoals(Goals);
		for (const FName Goal : Goals) Info.Goals.Add(Goal.ToString());
		if (Solver->UsesCustomBoneSettings())
		{
			TSet<FName> Bones;
			Solver->GetBonesWithSettings(Bones);
			for (const FName Bone : Bones) Info.BonesWithSettings.Add(Bone.ToString());
		}
		Info.Goals.Sort();
		Info.BonesWithSettings.Sort();
		Result.Add(MoveTemp(Info));
	}
	return Result;
}

TArray<FBridgeIKGoalInfo> UUnrealBridgeRigLibrary::ListIKRigGoals(const FString& AssetPath)
{
	using namespace BridgeRigImpl;
	ClearError();
	TArray<FBridgeIKGoalInfo> Result;
	UIKRigDefinition* Rig = LoadIKRig(AssetPath);
	UIKRigController* Controller = Rig ? UIKRigController::GetController(Rig) : nullptr;
	if (!Controller) return Result;
	for (UIKRigEffectorGoal* Goal : Controller->GetAllGoals())
	{
		if (!Goal) continue;
		FBridgeIKGoalInfo Info;
		Info.Name = Goal->GoalName.ToString();
		Info.BoneName = Goal->BoneName.ToString();
		Info.InitialTransform = Goal->InitialTransform;
		Info.CurrentTransform = Goal->CurrentTransform;
		Info.PositionAlpha = Goal->PositionAlpha;
		Info.RotationAlpha = Goal->RotationAlpha;
		for (int32 SolverIndex = 0; SolverIndex < Controller->GetNumSolvers(); ++SolverIndex)
		{
			if (Controller->IsGoalConnectedToSolver(Goal->GoalName, SolverIndex))
				Info.ConnectedSolverIndices.Add(SolverIndex);
		}
		Result.Add(MoveTemp(Info));
	}
	return Result;
}

TArray<FBridgeIKChainInfo> UUnrealBridgeRigLibrary::ListIKRigRetargetChains(const FString& AssetPath)
{
	using namespace BridgeRigImpl;
	ClearError();
	TArray<FBridgeIKChainInfo> Result;
	UIKRigDefinition* Rig = LoadIKRig(AssetPath);
	UIKRigController* Controller = Rig ? UIKRigController::GetController(Rig) : nullptr;
	if (!Controller) return Result;
	for (const FBoneChain& Chain : Controller->GetRetargetChains())
	{
		FBridgeIKChainInfo Info;
		Info.Name = Chain.ChainName.ToString();
		Info.StartBone = Chain.StartBone.BoneName.ToString();
		Info.EndBone = Chain.EndBone.BoneName.ToString();
		Info.GoalName = Chain.IKGoalName.ToString();
		TSet<int32> Indices;
		Info.bValid = Controller->ValidateChain(Chain.ChainName, nullptr, Indices);
		Info.BoneCount = Indices.Num();
		Result.Add(MoveTemp(Info));
	}
	return Result;
}

int32 UUnrealBridgeRigLibrary::AddIKRigSolver(const FString& AssetPath, const FString& SolverTypePath)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRigDefinition* Rig = LoadIKRig(AssetPath);
	UIKRigController* Controller = Rig ? UIKRigController::GetController(Rig) : nullptr;
	if (!Controller) return INDEX_NONE;
	FScopedTransaction Transaction(LOCTEXT("AddIKRigSolver", "UnrealBridge: Add IK Rig Solver"));
	Rig->Modify();
	const int32 Index = Controller->AddSolver(SolverTypePath);
	if (Index == INDEX_NONE)
	{
		SetError(FString::Printf(TEXT("failed to add IK solver type '%s'"), *SolverTypePath));
		return INDEX_NONE;
	}
	MarkChanged(Rig);
	return Index;
}

bool UUnrealBridgeRigLibrary::RemoveIKRigSolver(const FString& AssetPath, int32 SolverIndex)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRigDefinition* Rig = LoadIKRig(AssetPath);
	UIKRigController* Controller = Rig ? UIKRigController::GetController(Rig) : nullptr;
	if (!Controller) return false;
	FScopedTransaction Transaction(LOCTEXT("RemoveIKRigSolver", "UnrealBridge: Remove IK Rig Solver"));
	Rig->Modify();
	if (!Controller->RemoveSolver(SolverIndex))
		return SetError(FString::Printf(TEXT("IK solver index %d is invalid"), SolverIndex));
	MarkChanged(Rig);
	return true;
}

bool UUnrealBridgeRigLibrary::MoveIKRigSolver(const FString& AssetPath, int32 SolverIndex, int32 TargetIndex)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRigDefinition* Rig = LoadIKRig(AssetPath);
	UIKRigController* Controller = Rig ? UIKRigController::GetController(Rig) : nullptr;
	if (!Controller) return false;
	FScopedTransaction Transaction(LOCTEXT("MoveIKRigSolver", "UnrealBridge: Move IK Rig Solver"));
	Rig->Modify();
	if (!Controller->MoveSolverInStack(SolverIndex, TargetIndex))
		return SetError(FString::Printf(TEXT("could not move IK solver %d to %d"), SolverIndex, TargetIndex));
	MarkChanged(Rig);
	return true;
}

bool UUnrealBridgeRigLibrary::SetIKRigSolverEnabled(const FString& AssetPath, int32 SolverIndex, bool bEnabled)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRigDefinition* Rig = LoadIKRig(AssetPath);
	UIKRigController* Controller = Rig ? UIKRigController::GetController(Rig) : nullptr;
	if (!Controller) return false;
	FScopedTransaction Transaction(LOCTEXT("SetIKRigSolverEnabled", "UnrealBridge: Set IK Rig Solver Enabled"));
	Rig->Modify();
	if (!Controller->SetSolverEnabled(SolverIndex, bEnabled))
		return SetError(FString::Printf(TEXT("IK solver index %d is invalid"), SolverIndex));
	MarkChanged(Rig);
	return true;
}

bool UUnrealBridgeRigLibrary::SetIKRigSolverBones(const FString& AssetPath, int32 SolverIndex,
	const FString& StartBone, const FString& EndBone)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRigDefinition* Rig = LoadIKRig(AssetPath);
	UIKRigController* Controller = Rig ? UIKRigController::GetController(Rig) : nullptr;
	if (!Controller || !Controller->GetSolverAtIndex(SolverIndex))
		return Controller ? SetError(FString::Printf(TEXT("IK solver index %d is invalid"), SolverIndex)) : false;
	FScopedTransaction Transaction(LOCTEXT("SetIKRigSolverBones", "UnrealBridge: Set IK Rig Solver Bones"));
	Rig->Modify();
	if (!StartBone.TrimStartAndEnd().IsEmpty() && !Controller->SetStartBone(FName(*StartBone), SolverIndex))
		return SetError(FString::Printf(TEXT("solver %d does not support start bone '%s'"), SolverIndex, *StartBone));
	if (!EndBone.TrimStartAndEnd().IsEmpty() && !Controller->SetEndBone(FName(*EndBone), SolverIndex))
		return SetError(FString::Printf(TEXT("solver %d does not support end bone '%s'"), SolverIndex, *EndBone));
	MarkChanged(Rig);
	return true;
}

FString UUnrealBridgeRigLibrary::AddIKRigGoal(
	const FString& AssetPath, const FString& GoalName, const FString& BoneName)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRigDefinition* Rig = LoadIKRig(AssetPath);
	UIKRigController* Controller = Rig ? UIKRigController::GetController(Rig) : nullptr;
	if (!Controller || GoalName.TrimStartAndEnd().IsEmpty() || BoneName.TrimStartAndEnd().IsEmpty())
	{
		if (Controller) SetError(TEXT("goal name and bone name are required"));
		return FString();
	}
	FScopedTransaction Transaction(LOCTEXT("AddIKRigGoal", "UnrealBridge: Add IK Rig Goal"));
	Rig->Modify();
	const FName Added = Controller->AddNewGoal(FName(*GoalName), FName(*BoneName));
	if (Added.IsNone())
	{
		SetError(FString::Printf(TEXT("failed to add IK goal '%s' on bone '%s'"), *GoalName, *BoneName));
		return FString();
	}
	MarkChanged(Rig);
	return Added.ToString();
}

bool UUnrealBridgeRigLibrary::RemoveIKRigGoal(const FString& AssetPath, const FString& GoalName)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRigDefinition* Rig = LoadIKRig(AssetPath);
	UIKRigController* Controller = Rig ? UIKRigController::GetController(Rig) : nullptr;
	if (!Controller) return false;
	FScopedTransaction Transaction(LOCTEXT("RemoveIKRigGoal", "UnrealBridge: Remove IK Rig Goal"));
	Rig->Modify();
	if (!Controller->RemoveGoal(FName(*GoalName)))
		return SetError(FString::Printf(TEXT("IK goal '%s' was not found"), *GoalName));
	MarkChanged(Rig);
	return true;
}

bool UUnrealBridgeRigLibrary::ConnectIKRigGoalToSolver(
	const FString& AssetPath, const FString& GoalName, int32 SolverIndex)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRigDefinition* Rig = LoadIKRig(AssetPath);
	UIKRigController* Controller = Rig ? UIKRigController::GetController(Rig) : nullptr;
	if (!Controller) return false;
	FScopedTransaction Transaction(LOCTEXT("ConnectIKRigGoalToSolver", "UnrealBridge: Connect IK Goal To Solver"));
	Rig->Modify();
	if (!Controller->ConnectGoalToSolver(FName(*GoalName), SolverIndex))
		return SetError(FString::Printf(TEXT("failed to connect goal '%s' to solver %d"), *GoalName, SolverIndex));
	MarkChanged(Rig);
	return true;
}

bool UUnrealBridgeRigLibrary::DisconnectIKRigGoalFromSolver(
	const FString& AssetPath, const FString& GoalName, int32 SolverIndex)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRigDefinition* Rig = LoadIKRig(AssetPath);
	UIKRigController* Controller = Rig ? UIKRigController::GetController(Rig) : nullptr;
	if (!Controller) return false;
	FScopedTransaction Transaction(LOCTEXT("DisconnectIKRigGoalFromSolver", "UnrealBridge: Disconnect IK Goal From Solver"));
	Rig->Modify();
	if (!Controller->DisconnectGoalFromSolver(FName(*GoalName), SolverIndex))
		return SetError(FString::Printf(TEXT("goal '%s' is not connected to solver %d"), *GoalName, SolverIndex));
	MarkChanged(Rig);
	return true;
}

FString UUnrealBridgeRigLibrary::AddIKRigRetargetChain(const FString& AssetPath, const FString& ChainName,
	const FString& StartBone, const FString& EndBone, const FString& GoalName)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRigDefinition* Rig = LoadIKRig(AssetPath);
	UIKRigController* Controller = Rig ? UIKRigController::GetController(Rig) : nullptr;
	if (!Controller || ChainName.TrimStartAndEnd().IsEmpty())
	{
		if (Controller) SetError(TEXT("retarget chain name is empty"));
		return FString();
	}
	FScopedTransaction Transaction(LOCTEXT("AddIKRigRetargetChain", "UnrealBridge: Add IK Rig Retarget Chain"));
	Rig->Modify();
	const FName Added = Controller->AddRetargetChain(FName(*ChainName), FName(*StartBone), FName(*EndBone), FName(*GoalName));
	if (Added.IsNone())
	{
		SetError(FString::Printf(TEXT("failed to add retarget chain '%s'"), *ChainName));
		return FString();
	}
	MarkChanged(Rig);
	return Added.ToString();
}

bool UUnrealBridgeRigLibrary::RemoveIKRigRetargetChain(const FString& AssetPath, const FString& ChainName)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRigDefinition* Rig = LoadIKRig(AssetPath);
	UIKRigController* Controller = Rig ? UIKRigController::GetController(Rig) : nullptr;
	if (!Controller) return false;
	FScopedTransaction Transaction(LOCTEXT("RemoveIKRigRetargetChain", "UnrealBridge: Remove IK Rig Retarget Chain"));
	Rig->Modify();
	if (!Controller->RemoveRetargetChain(FName(*ChainName)))
		return SetError(FString::Printf(TEXT("retarget chain '%s' was not found"), *ChainName));
	MarkChanged(Rig);
	return true;
}

FString UUnrealBridgeRigLibrary::RenameIKRigRetargetChain(
	const FString& AssetPath, const FString& ChainName, const FString& NewName)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRigDefinition* Rig = LoadIKRig(AssetPath);
	UIKRigController* Controller = Rig ? UIKRigController::GetController(Rig) : nullptr;
	if (!Controller) return FString();
	FScopedTransaction Transaction(LOCTEXT("RenameIKRigRetargetChain", "UnrealBridge: Rename IK Rig Retarget Chain"));
	Rig->Modify();
	const FName Renamed = Controller->RenameRetargetChain(FName(*ChainName), FName(*NewName));
	if (Renamed.IsNone() || Renamed.IsEqual(FName(*ChainName)) && !ChainName.Equals(NewName, ESearchCase::IgnoreCase))
	{
		SetError(FString::Printf(TEXT("failed to rename retarget chain '%s'"), *ChainName));
		return FString();
	}
	MarkChanged(Rig);
	return Renamed.ToString();
}

bool UUnrealBridgeRigLibrary::SetIKRigRetargetRoot(const FString& AssetPath, const FString& RootBoneName)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRigDefinition* Rig = LoadIKRig(AssetPath);
	UIKRigController* Controller = Rig ? UIKRigController::GetController(Rig) : nullptr;
	if (!Controller) return false;
	FScopedTransaction Transaction(LOCTEXT("SetIKRigRetargetRoot", "UnrealBridge: Set IK Rig Retarget Root"));
	Rig->Modify();
	if (!Controller->SetRetargetRoot(FName(*RootBoneName)))
		return SetError(FString::Printf(TEXT("bone '%s' cannot be used as the retarget root"), *RootBoneName));
	MarkChanged(Rig);
	return true;
}

bool UUnrealBridgeRigLibrary::SetIKRigBoneExcluded(
	const FString& AssetPath, const FString& BoneName, bool bExcluded)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRigDefinition* Rig = LoadIKRig(AssetPath);
	UIKRigController* Controller = Rig ? UIKRigController::GetController(Rig) : nullptr;
	if (!Controller) return false;
	FScopedTransaction Transaction(LOCTEXT("SetIKRigBoneExcluded", "UnrealBridge: Set IK Rig Bone Excluded"));
	Rig->Modify();
	if (!Controller->SetBoneExcluded(FName(*BoneName), bExcluded))
		return SetError(FString::Printf(TEXT("IK Rig bone '%s' was not found"), *BoneName));
	MarkChanged(Rig);
	return true;
}

bool UUnrealBridgeRigLibrary::ApplyIKRigAutoSetup(
	const FString& AssetPath, bool bRetargetDefinition, bool bFullBodyIK)
{
	using namespace BridgeRigImpl;
	ClearError();
	if (!bRetargetDefinition && !bFullBodyIK) return SetError(TEXT("at least one IK Rig auto-setup option must be enabled"));
	UIKRigDefinition* Rig = LoadIKRig(AssetPath);
	UIKRigController* Controller = Rig ? UIKRigController::GetController(Rig) : nullptr;
	if (!Controller) return false;
	FScopedTransaction Transaction(LOCTEXT("ApplyIKRigAutoSetup", "UnrealBridge: Apply IK Rig Auto Setup"));
	Rig->Modify();
	if (bRetargetDefinition && !Controller->ApplyAutoGeneratedRetargetDefinition())
		return SetError(TEXT("the IK Rig skeleton did not match a known retarget template"));
	if (bFullBodyIK && !Controller->ApplyAutoFBIK())
		return SetError(TEXT("Full Body IK auto-setup could not characterize this skeleton"));
	MarkChanged(Rig);
	return true;
}

TArray<FBridgeRigPropertyInfo> UUnrealBridgeRigLibrary::ListIKRigProperties(const FString& AssetPath,
	const FString& TargetKind, int32 SolverIndex, const FString& TargetName)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRigDefinition* Rig = LoadIKRig(AssetPath);
	UIKRigController* Controller = Rig ? UIKRigController::GetController(Rig) : nullptr;
	FIKPropertyTarget Target;
	if (!ResolveIKPropertyTarget(Controller, TargetKind, SolverIndex, TargetName, false, Target)) return {};
	return ListProperties(Target.View, Target.Goal ? static_cast<UObject*>(Target.Goal) : static_cast<UObject*>(Rig));
}

FBridgeRigPropertyResult UUnrealBridgeRigLibrary::GetIKRigProperty(const FString& AssetPath,
	const FString& TargetKind, int32 SolverIndex, const FString& TargetName, const FString& PropertyPath)
{
	using namespace BridgeRigImpl;
	ClearError();
	FBridgeRigPropertyResult Result;
	UIKRigDefinition* Rig = LoadIKRig(AssetPath);
	UIKRigController* Controller = Rig ? UIKRigController::GetController(Rig) : nullptr;
	FIKPropertyTarget Target;
	if (!ResolveIKPropertyTarget(Controller, TargetKind, SolverIndex, TargetName, false, Target))
	{
		Result.Error = LastError;
		return Result;
	}
	return ReadProperty(Target.View, PropertyPath, Target.Goal ? static_cast<UObject*>(Target.Goal) : static_cast<UObject*>(Rig));
}

bool UUnrealBridgeRigLibrary::SetIKRigProperty(const FString& AssetPath, const FString& TargetKind,
	int32 SolverIndex, const FString& TargetName, const FString& PropertyPath, const FString& Value)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRigDefinition* Rig = LoadIKRig(AssetPath);
	UIKRigController* Controller = Rig ? UIKRigController::GetController(Rig) : nullptr;
	FIKPropertyTarget Target;
	if (!ResolveIKPropertyTarget(Controller, TargetKind, SolverIndex, TargetName, true, Target)) return false;
	FScopedTransaction Transaction(LOCTEXT("SetIKRigProperty", "UnrealBridge: Set IK Rig Property"));
	Rig->Modify();
	if (Target.Goal) Target.Goal->Modify();
	if (!WriteProperty(Target.View, PropertyPath, Value, Target.Goal ? static_cast<UObject*>(Target.Goal) : static_cast<UObject*>(Rig)))
		return false;
	if (Target.Goal)
	{
		Target.Goal->PostEditChange();
	}
	else if (Target.Kind == TEXT("solver") || Target.Kind == TEXT("solversettings"))
	{
		Target.Solver->SetSolverSettings(static_cast<FIKRigSolverSettingsBase*>(Target.Settings));
	}
	else if (Target.Kind == TEXT("goalsettings"))
	{
		Target.Solver->SetGoalSettings(Target.TargetName, static_cast<FIKRigGoalSettingsBase*>(Target.Settings));
	}
	else if (Target.Kind == TEXT("bonesettings"))
	{
		Target.Solver->SetBoneSettings(Target.TargetName, static_cast<FIKRigBoneSettingsBase*>(Target.Settings));
	}
	Controller->BroadcastNeedsReinitialized();
	MarkChanged(Rig);
	return true;
}

FBridgeRigValidationReport UUnrealBridgeRigLibrary::ValidateIKRig(const FString& AssetPath, bool bSave)
{
	using namespace BridgeRigImpl;
	ClearError();
	FBridgeRigValidationReport Report;
	UIKRigDefinition* Rig = LoadIKRig(AssetPath);
	if (!Rig)
	{
		Report.Error = LastError;
		return Report;
	}
	Report.bFound = true;
	UIKRigController* Controller = UIKRigController::GetController(Rig);
	if (!Controller)
	{
		AddIssue(Report, TEXT("Error"), TEXT("MissingController"), Rig->GetPathName(), TEXT("IK Rig has no editor controller"));
	}
	else
	{
		if (!Controller->GetSkeletalMesh()) AddIssue(Report, TEXT("Error"), TEXT("MissingPreviewMesh"),
			Rig->GetPathName(), TEXT("IK Rig has no skeletal mesh"));
		if (Controller->GetIKRigSkeleton().BoneNames.IsEmpty()) AddIssue(Report, TEXT("Error"), TEXT("EmptySkeleton"),
			Rig->GetPathName(), TEXT("IK Rig contains no skeleton bones"));
		for (int32 Index = 0; Index < Controller->GetNumSolvers(); ++Index)
		{
			FIKRigSolverBase* Solver = Controller->GetSolverAtIndex(Index);
			if (!Solver) continue;
			FText Warning;
			if (Solver->GetWarningMessage(Warning) && !Warning.IsEmpty())
				AddIssue(Report, TEXT("Warning"), TEXT("SolverWarning"), FString::Printf(TEXT("Solver[%d]"), Index), Warning.ToString());
		}
		for (UIKRigEffectorGoal* Goal : Controller->GetAllGoals())
		{
			if (Goal && !Controller->IsGoalConnectedToAnySolver(Goal->GoalName))
				AddIssue(Report, TEXT("Warning"), TEXT("UnconnectedGoal"), Goal->GoalName.ToString(),
					TEXT("goal is not connected to any solver"));
		}
		for (const FBoneChain& Chain : Controller->GetRetargetChains())
		{
			TSet<int32> Indices;
			if (!Controller->ValidateChain(Chain.ChainName, nullptr, Indices))
				AddIssue(Report, TEXT("Error"), TEXT("InvalidRetargetChain"), Chain.ChainName.ToString(),
					TEXT("retarget chain start/end bones do not form a valid hierarchy path"));
		}
		if (!Controller->GetRetargetChains().IsEmpty() && Controller->GetRetargetRoot().IsNone())
			AddIssue(Report, TEXT("Warning"), TEXT("MissingRetargetRoot"), Rig->GetPathName(),
				TEXT("retarget chains exist but no retarget root is configured"));
	}
	Report.bCompiled = Report.ErrorCount == 0;
	if (bSave) Report.bSaved = SaveAsset(Rig);
	Report.bSuccess = Report.ErrorCount == 0 && (!bSave || Report.bSaved);
	if (!Report.bSuccess) Report.Error = Report.ErrorCount > 0 ? TEXT("IK Rig validation failed") : TEXT("IK Rig package could not be saved");
	return Report;
}

FBridgeRigOperationResult UUnrealBridgeRigLibrary::CreateIKRetargeter(const FString& AssetPath,
	const FString& SourceIKRigPath, const FString& TargetIKRigPath, const FString& SourcePreviewMeshPath,
	const FString& TargetPreviewMeshPath, bool bAddDefaultOps)
{
	using namespace BridgeRigImpl;
	ClearError();
	FBridgeRigOperationResult Result;
	FString PackagePath, AssetName;
	if (!SplitAssetPath(AssetPath, PackagePath, AssetName) || !EnsureAssetDoesNotExist(PackagePath, AssetName))
	{
		Result.Error = LastError;
		return Result;
	}
	UIKRigDefinition* SourceRig = SourceIKRigPath.IsEmpty() ? nullptr : LoadIKRig(SourceIKRigPath);
	if (!SourceIKRigPath.IsEmpty() && !SourceRig) { Result.Error = LastError; return Result; }
	UIKRigDefinition* TargetRig = TargetIKRigPath.IsEmpty() ? nullptr : LoadIKRig(TargetIKRigPath);
	if (!TargetIKRigPath.IsEmpty() && !TargetRig) { Result.Error = LastError; return Result; }
	USkeletalMesh* SourceMesh = SourcePreviewMeshPath.IsEmpty() ? nullptr
		: LoadRigAsset<USkeletalMesh>(SourcePreviewMeshPath, TEXT("source preview Skeletal Mesh"));
	if (!SourcePreviewMeshPath.IsEmpty() && !SourceMesh) { Result.Error = LastError; return Result; }
	USkeletalMesh* TargetMesh = TargetPreviewMeshPath.IsEmpty() ? nullptr
		: LoadRigAsset<USkeletalMesh>(TargetPreviewMeshPath, TEXT("target preview Skeletal Mesh"));
	if (!TargetPreviewMeshPath.IsEmpty() && !TargetMesh) { Result.Error = LastError; return Result; }
	FScopedTransaction Transaction(LOCTEXT("CreateIKRetargeter", "UnrealBridge: Create IK Retargeter"));
	UIKRetargetFactory* Factory = NewObject<UIKRetargetFactory>();
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UIKRetargeter* Retargeter = Cast<UIKRetargeter>(AssetTools.CreateAsset(
		AssetName, PackagePath, UIKRetargeter::StaticClass(), Factory));
	if (!Retargeter)
	{
		SetError(FString::Printf(TEXT("failed to create IK Retargeter '%s'"), *AssetPath));
		Result.Error = LastError;
		return Result;
	}
	UIKRetargeterController* Controller = UIKRetargeterController::GetController(Retargeter);
	if (!Controller)
	{
		SetError(TEXT("new IK Retargeter has no controller"));
		Result.AssetPath = Retargeter->GetPathName();
		Result.Error = LastError;
		return Result;
	}
	Controller->SetIKRig(ERetargetSourceOrTarget::Source, SourceRig);
	Controller->SetIKRig(ERetargetSourceOrTarget::Target, TargetRig);
	if (SourceMesh) Controller->SetPreviewMesh(ERetargetSourceOrTarget::Source, SourceMesh);
	if (TargetMesh) Controller->SetPreviewMesh(ERetargetSourceOrTarget::Target, TargetMesh);
	if (bAddDefaultOps && !HasCoreDefaultRetargetOps(Controller)) Controller->AddDefaultOps();
	Controller->AssignIKRigToAllOps(ERetargetSourceOrTarget::Source, SourceRig);
	Controller->AssignIKRigToAllOps(ERetargetSourceOrTarget::Target, TargetRig);
	Controller->CleanAsset();
	MarkChanged(Retargeter);
	Result.bSuccess = true;
	Result.AssetPath = Retargeter->GetPathName();
	Result.Name = Retargeter->GetName();
	return Result;
}

FBridgeIKRetargeterInfo UUnrealBridgeRigLibrary::GetIKRetargeterInfo(const FString& AssetPath)
{
	using namespace BridgeRigImpl;
	ClearError();
	FBridgeIKRetargeterInfo Result;
	UIKRetargeter* Retargeter = LoadRetargeter(AssetPath);
	if (!Retargeter) return Result;
	UIKRetargeterController* Controller = UIKRetargeterController::GetController(Retargeter);
	if (!Controller) return Result;
	Result.AssetPath = Retargeter->GetPathName();
	Result.SourceIKRigPath = GetPathNameSafe(Controller->GetIKRig(ERetargetSourceOrTarget::Source));
	Result.TargetIKRigPath = GetPathNameSafe(Controller->GetIKRig(ERetargetSourceOrTarget::Target));
	Result.SourcePreviewMeshPath = GetPathNameSafe(Controller->GetPreviewMesh(ERetargetSourceOrTarget::Source));
	Result.TargetPreviewMeshPath = GetPathNameSafe(Controller->GetPreviewMesh(ERetargetSourceOrTarget::Target));
	Result.CurrentSourcePose = Controller->GetCurrentRetargetPoseName(ERetargetSourceOrTarget::Source).ToString();
	Result.CurrentTargetPose = Controller->GetCurrentRetargetPoseName(ERetargetSourceOrTarget::Target).ToString();
	if (FNameProperty* CurrentProperty = GetCurrentProfileProperty(Retargeter))
	{
		Result.CurrentProfile = CurrentProperty->GetPropertyValue_InContainer(Retargeter).ToString();
	}
	Result.OpCount = Controller->GetNumRetargetOps();
	for (int32 Index = 0; Index < Controller->GetNumRetargetOps(); ++Index)
	{
		if (const FIKRetargetOpBase* Op = Controller->GetRetargetOpByIndex(Index))
		{
			if (const FRetargetChainMapping* Mapping = Op->GetChainMapping())
			{
				Result.MappingCount += Mapping->GetChainPairs().Num();
				for (const FRetargetChainPair& Pair : Mapping->GetChainPairs())
					if (Pair.SourceChainName.IsNone()) ++Result.UnmappedChainCount;
			}
		}
	}
	Result.SourcePoseCount = Controller->GetRetargetPoses(ERetargetSourceOrTarget::Source).Num();
	Result.TargetPoseCount = Controller->GetRetargetPoses(ERetargetSourceOrTarget::Target).Num();
	Result.bDirty = Retargeter->GetOutermost()->IsDirty();
	return Result;
}

bool UUnrealBridgeRigLibrary::ConfigureIKRetargeterAssets(const FString& AssetPath,
	const FString& SourceIKRigPath, const FString& TargetIKRigPath,
	const FString& SourcePreviewMeshPath, const FString& TargetPreviewMeshPath)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRetargeter* Retargeter = LoadRetargeter(AssetPath);
	if (!Retargeter) return false;
	UIKRigDefinition* SourceRig = SourceIKRigPath.IsEmpty() ? nullptr : LoadIKRig(SourceIKRigPath);
	if (!SourceIKRigPath.IsEmpty() && !SourceRig) return false;
	UIKRigDefinition* TargetRig = TargetIKRigPath.IsEmpty() ? nullptr : LoadIKRig(TargetIKRigPath);
	if (!TargetIKRigPath.IsEmpty() && !TargetRig) return false;
	USkeletalMesh* SourceMesh = SourcePreviewMeshPath.IsEmpty() ? nullptr
		: LoadRigAsset<USkeletalMesh>(SourcePreviewMeshPath, TEXT("source preview Skeletal Mesh"));
	if (!SourcePreviewMeshPath.IsEmpty() && !SourceMesh) return false;
	USkeletalMesh* TargetMesh = TargetPreviewMeshPath.IsEmpty() ? nullptr
		: LoadRigAsset<USkeletalMesh>(TargetPreviewMeshPath, TEXT("target preview Skeletal Mesh"));
	if (!TargetPreviewMeshPath.IsEmpty() && !TargetMesh) return false;
	UIKRetargeterController* Controller = UIKRetargeterController::GetController(Retargeter);
	if (!Controller) return SetError(TEXT("IK Retargeter has no controller"));
	FScopedTransaction Transaction(LOCTEXT("ConfigureIKRetargeterAssets", "UnrealBridge: Configure IK Retargeter Assets"));
	Retargeter->Modify();
	Controller->SetIKRig(ERetargetSourceOrTarget::Source, SourceRig);
	Controller->SetIKRig(ERetargetSourceOrTarget::Target, TargetRig);
	Controller->SetPreviewMesh(ERetargetSourceOrTarget::Source, SourceMesh);
	Controller->SetPreviewMesh(ERetargetSourceOrTarget::Target, TargetMesh);
	Controller->AssignIKRigToAllOps(ERetargetSourceOrTarget::Source, SourceRig);
	Controller->AssignIKRigToAllOps(ERetargetSourceOrTarget::Target, TargetRig);
	Controller->CleanAsset();
	MarkChanged(Retargeter);
	return true;
}

TArray<FBridgeIKRetargetOpInfo> UUnrealBridgeRigLibrary::ListIKRetargetOps(const FString& AssetPath)
{
	using namespace BridgeRigImpl;
	ClearError();
	TArray<FBridgeIKRetargetOpInfo> Result;
	UIKRetargeter* Retargeter = LoadRetargeter(AssetPath);
	UIKRetargeterController* Controller = Retargeter ? UIKRetargeterController::GetController(Retargeter) : nullptr;
	if (!Controller) return Result;
	for (int32 Index = 0; Index < Controller->GetNumRetargetOps(); ++Index)
	{
		const FIKRetargetOpBase* Op = Controller->GetRetargetOpByIndex(Index);
		if (!Op) continue;
		FBridgeIKRetargetOpInfo Info;
		Info.Index = Index;
		Info.Name = Op->GetName().ToString();
		Info.TypePath = GetPathNameSafe(Op->GetType());
		Info.DisplayName = Op->GetDefaultName().ToString();
		Info.ParentOpName = Op->GetParentOpName().ToString();
		Info.TargetIKRigPath = GetPathNameSafe(Controller->GetTargetIKRigForOp(Op->GetName()));
		Info.bEnabled = Op->IsEnabled();
		Info.bHasChainMapping = Op->GetChainMapping() != nullptr;
		Result.Add(MoveTemp(Info));
	}
	return Result;
}

int32 UUnrealBridgeRigLibrary::AddIKRetargetOp(
	const FString& AssetPath, const FString& OpTypePath, const FString& OpName)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRetargeter* Retargeter = LoadRetargeter(AssetPath);
	UIKRetargeterController* Controller = Retargeter ? UIKRetargeterController::GetController(Retargeter) : nullptr;
	if (!Controller) return INDEX_NONE;
	FScopedTransaction Transaction(LOCTEXT("AddIKRetargetOp", "UnrealBridge: Add IK Retarget Op"));
	Retargeter->Modify();
	const int32 Index = Controller->AddRetargetOp(OpTypePath);
	if (Index == INDEX_NONE)
	{
		SetError(FString::Printf(TEXT("failed to add retarget op type '%s'"), *OpTypePath));
		return INDEX_NONE;
	}
	if (!OpName.TrimStartAndEnd().IsEmpty()) Controller->SetOpName(FName(*OpName), Index);
	Controller->RunOpInitialSetup(Index);
	MarkChanged(Retargeter);
	return Index;
}

bool UUnrealBridgeRigLibrary::AddDefaultIKRetargetOps(const FString& AssetPath)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRetargeter* Retargeter = LoadRetargeter(AssetPath);
	UIKRetargeterController* Controller = Retargeter ? UIKRetargeterController::GetController(Retargeter) : nullptr;
	if (!Controller) return false;
	if (HasCoreDefaultRetargetOps(Controller)) return true;
	FScopedTransaction Transaction(LOCTEXT("AddDefaultIKRetargetOps", "UnrealBridge: Add Default IK Retarget Ops"));
	Retargeter->Modify();
	Controller->AddDefaultOps();
	MarkChanged(Retargeter);
	return Controller->GetNumRetargetOps() > 0;
}

bool UUnrealBridgeRigLibrary::RemoveIKRetargetOp(const FString& AssetPath, int32 OpIndex)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRetargeter* Retargeter = LoadRetargeter(AssetPath);
	UIKRetargeterController* Controller = Retargeter ? UIKRetargeterController::GetController(Retargeter) : nullptr;
	if (!Controller) return false;
	FScopedTransaction Transaction(LOCTEXT("RemoveIKRetargetOp", "UnrealBridge: Remove IK Retarget Op"));
	Retargeter->Modify();
	if (!Controller->RemoveRetargetOp(OpIndex))
		return SetError(FString::Printf(TEXT("retarget op index %d is invalid"), OpIndex));
	MarkChanged(Retargeter);
	return true;
}

bool UUnrealBridgeRigLibrary::MoveIKRetargetOp(const FString& AssetPath, int32 OpIndex, int32 TargetIndex)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRetargeter* Retargeter = LoadRetargeter(AssetPath);
	UIKRetargeterController* Controller = Retargeter ? UIKRetargeterController::GetController(Retargeter) : nullptr;
	if (!Controller) return false;
	FScopedTransaction Transaction(LOCTEXT("MoveIKRetargetOp", "UnrealBridge: Move IK Retarget Op"));
	Retargeter->Modify();
	if (!Controller->MoveRetargetOpInStack(OpIndex, TargetIndex))
		return SetError(FString::Printf(TEXT("could not move retarget op %d to %d"), OpIndex, TargetIndex));
	MarkChanged(Retargeter);
	return true;
}

bool UUnrealBridgeRigLibrary::SetIKRetargetOpEnabled(const FString& AssetPath, int32 OpIndex, bool bEnabled)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRetargeter* Retargeter = LoadRetargeter(AssetPath);
	UIKRetargeterController* Controller = Retargeter ? UIKRetargeterController::GetController(Retargeter) : nullptr;
	if (!Controller) return false;
	FScopedTransaction Transaction(LOCTEXT("SetIKRetargetOpEnabled", "UnrealBridge: Set IK Retarget Op Enabled"));
	Retargeter->Modify();
	if (!Controller->SetRetargetOpEnabled(OpIndex, bEnabled))
		return SetError(FString::Printf(TEXT("retarget op index %d is invalid"), OpIndex));
	MarkChanged(Retargeter);
	return true;
}

bool UUnrealBridgeRigLibrary::SetIKRetargetOpParent(const FString& AssetPath,
	const FString& ChildOpName, const FString& ParentOpName)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRetargeter* Retargeter = LoadRetargeter(AssetPath);
	UIKRetargeterController* Controller = Retargeter ? UIKRetargeterController::GetController(Retargeter) : nullptr;
	if (!Controller) return false;
	FScopedTransaction Transaction(LOCTEXT("SetIKRetargetOpParent", "UnrealBridge: Set IK Retarget Op Parent"));
	Retargeter->Modify();
	if (!Controller->SetParentOpByName(FName(*ChildOpName), FName(*ParentOpName)))
		return SetError(FString::Printf(TEXT("could not parent retarget op '%s' to '%s'"), *ChildOpName, *ParentOpName));
	MarkChanged(Retargeter);
	return true;
}

bool UUnrealBridgeRigLibrary::AutoMapIKRetargetChains(const FString& AssetPath,
	const FString& MappingType, bool bForceRemap, const FString& OpName)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRetargeter* Retargeter = LoadRetargeter(AssetPath);
	UIKRetargeterController* Controller = Retargeter ? UIKRetargeterController::GetController(Retargeter) : nullptr;
	if (!Controller) return false;
	EAutoMapChainType Type = EAutoMapChainType::Fuzzy;
	if (!ParseEnumValue(MappingType, Type, TEXT("chain mapping type"))) return false;
	FScopedTransaction Transaction(LOCTEXT("AutoMapIKRetargetChains", "UnrealBridge: Auto Map IK Retarget Chains"));
	Retargeter->Modify();
	Controller->AutoMapChains(Type, bForceRemap, FName(*OpName));
	MarkChanged(Retargeter);
	return true;
}

bool UUnrealBridgeRigLibrary::SetIKRetargetChainMapping(const FString& AssetPath,
	const FString& TargetChainName, const FString& SourceChainName, const FString& OpName)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRetargeter* Retargeter = LoadRetargeter(AssetPath);
	UIKRetargeterController* Controller = Retargeter ? UIKRetargeterController::GetController(Retargeter) : nullptr;
	if (!Controller) return false;
	FScopedTransaction Transaction(LOCTEXT("SetIKRetargetChainMapping", "UnrealBridge: Set IK Retarget Chain Mapping"));
	Retargeter->Modify();
	if (!Controller->SetSourceChain(FName(*SourceChainName), FName(*TargetChainName), FName(*OpName)))
		return SetError(FString::Printf(TEXT("failed to map target chain '%s' to source chain '%s'"),
			*TargetChainName, *SourceChainName));
	MarkChanged(Retargeter);
	return true;
}

TArray<FBridgeIKChainMappingInfo> UUnrealBridgeRigLibrary::ListIKRetargetChainMappings(
	const FString& AssetPath, const FString& OpName)
{
	using namespace BridgeRigImpl;
	ClearError();
	TArray<FBridgeIKChainMappingInfo> Result;
	UIKRetargeter* Retargeter = LoadRetargeter(AssetPath);
	UIKRetargeterController* Controller = Retargeter ? UIKRetargeterController::GetController(Retargeter) : nullptr;
	if (!Controller) return Result;
	for (int32 Index = 0; Index < Controller->GetNumRetargetOps(); ++Index)
	{
		const FIKRetargetOpBase* Op = Controller->GetRetargetOpByIndex(Index);
		if (!Op || (!OpName.IsEmpty() && !Op->GetName().IsEqual(FName(*OpName), ENameCase::IgnoreCase))) continue;
		const FRetargetChainMapping* Mapping = Op->GetChainMapping();
		if (!Mapping) continue;
		for (const FRetargetChainPair& Pair : Mapping->GetChainPairs())
		{
			FBridgeIKChainMappingInfo Info;
			Info.OpName = Op->GetName().ToString();
			Info.TargetChainName = Pair.TargetChainName.ToString();
			Info.SourceChainName = Pair.SourceChainName.ToString();
			Info.bMapped = !Pair.SourceChainName.IsNone();
			Info.bSettingsAtDefault = Controller->AreChainSettingsAtDefault(Pair.TargetChainName, Op->GetName());
			Result.Add(MoveTemp(Info));
		}
	}
	if (!OpName.IsEmpty() && Result.IsEmpty() && Controller->GetIndexOfOpByName(FName(*OpName)) == INDEX_NONE)
		SetError(FString::Printf(TEXT("retarget op '%s' was not found"), *OpName));
	return Result;
}

TArray<FBridgeRigPropertyInfo> UUnrealBridgeRigLibrary::ListIKRetargetOpProperties(
	const FString& AssetPath, int32 OpIndex)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRetargeter* Retargeter = LoadRetargeter(AssetPath);
	UIKRetargeterController* Controller = Retargeter ? UIKRetargeterController::GetController(Retargeter) : nullptr;
	FIKRetargetOpBase* Op = Controller ? Controller->GetRetargetOpByIndex(OpIndex) : nullptr;
	if (!Op || !Op->GetSettings() || !Op->GetSettingsType())
	{
		if (Controller) SetError(FString::Printf(TEXT("retarget op index %d has no editable settings"), OpIndex));
		return {};
	}
	return ListProperties(FPropertyBindingDataView(FStructView(
		Op->GetSettingsType(), reinterpret_cast<uint8*>(Op->GetSettings()))), Retargeter);
}

FBridgeRigPropertyResult UUnrealBridgeRigLibrary::GetIKRetargetOpProperty(
	const FString& AssetPath, int32 OpIndex, const FString& PropertyPath)
{
	using namespace BridgeRigImpl;
	ClearError();
	FBridgeRigPropertyResult Result;
	UIKRetargeter* Retargeter = LoadRetargeter(AssetPath);
	UIKRetargeterController* Controller = Retargeter ? UIKRetargeterController::GetController(Retargeter) : nullptr;
	FIKRetargetOpBase* Op = Controller ? Controller->GetRetargetOpByIndex(OpIndex) : nullptr;
	if (!Op || !Op->GetSettings() || !Op->GetSettingsType())
	{
		if (Controller) SetError(FString::Printf(TEXT("retarget op index %d has no editable settings"), OpIndex));
		Result.Error = LastError;
		return Result;
	}
	return ReadProperty(FPropertyBindingDataView(FStructView(
		Op->GetSettingsType(), reinterpret_cast<uint8*>(Op->GetSettings()))),
		PropertyPath, Retargeter);
}

bool UUnrealBridgeRigLibrary::SetIKRetargetOpProperty(const FString& AssetPath, int32 OpIndex,
	const FString& PropertyPath, const FString& Value)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRetargeter* Retargeter = LoadRetargeter(AssetPath);
	UIKRetargeterController* Controller = Retargeter ? UIKRetargeterController::GetController(Retargeter) : nullptr;
	FIKRetargetOpBase* Op = Controller ? Controller->GetRetargetOpByIndex(OpIndex) : nullptr;
	if (!Op || !Op->GetSettings() || !Op->GetSettingsType())
		return Controller ? SetError(FString::Printf(TEXT("retarget op index %d has no editable settings"), OpIndex)) : false;
	FScopedTransaction Transaction(LOCTEXT("SetIKRetargetOpProperty", "UnrealBridge: Set IK Retarget Op Property"));
	Retargeter->Modify();
	FProperty* ChangedProperty = nullptr;
	if (!WriteProperty(FPropertyBindingDataView(FStructView(
		Op->GetSettingsType(), reinterpret_cast<uint8*>(Op->GetSettings()))),
		PropertyPath, Value, Retargeter, &ChangedProperty)) return false;
	FPropertyChangedEvent Event(ChangedProperty, EPropertyChangeType::ValueSet);
	Controller->OnOpPropertyChanged(Op->GetName(), Event);
	MarkChanged(Retargeter);
	return true;
}

TArray<FBridgeIKRetargetPoseInfo> UUnrealBridgeRigLibrary::ListIKRetargetPoses(
	const FString& AssetPath, const FString& Side)
{
	using namespace BridgeRigImpl;
	ClearError();
	TArray<FBridgeIKRetargetPoseInfo> Result;
	UIKRetargeter* Retargeter = LoadRetargeter(AssetPath);
	UIKRetargeterController* Controller = Retargeter ? UIKRetargeterController::GetController(Retargeter) : nullptr;
	bool bValid = false;
	const ERetargetSourceOrTarget ParsedSide = ParseSide(Side, bValid);
	if (!Controller || !bValid) return Result;
	const FName Current = Controller->GetCurrentRetargetPoseName(ParsedSide);
	for (const TPair<FName, FIKRetargetPose>& Pair : Controller->GetRetargetPoses(ParsedSide))
	{
		FBridgeIKRetargetPoseInfo Info;
		Info.Name = Pair.Key.ToString();
		Info.Side = ParsedSide == ERetargetSourceOrTarget::Source ? TEXT("Source") : TEXT("Target");
		Info.RootTranslationOffset = Pair.Value.GetRootTranslationDelta();
		Info.BoneRotationOffsetCount = Pair.Value.GetAllDeltaRotations().Num();
		Info.bCurrent = Pair.Key == Current;
		Result.Add(MoveTemp(Info));
	}
	Result.Sort([](const FBridgeIKRetargetPoseInfo& A, const FBridgeIKRetargetPoseInfo& B) { return A.Name < B.Name; });
	return Result;
}

FString UUnrealBridgeRigLibrary::CreateIKRetargetPose(
	const FString& AssetPath, const FString& PoseName, const FString& Side)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRetargeter* Retargeter = LoadRetargeter(AssetPath);
	UIKRetargeterController* Controller = Retargeter ? UIKRetargeterController::GetController(Retargeter) : nullptr;
	bool bValid = false;
	const ERetargetSourceOrTarget ParsedSide = ParseSide(Side, bValid);
	if (!Controller || !bValid) return FString();
	FScopedTransaction Transaction(LOCTEXT("CreateIKRetargetPose", "UnrealBridge: Create IK Retarget Pose"));
	Retargeter->Modify();
	const FName Created = Controller->CreateRetargetPose(FName(*PoseName), ParsedSide);
	if (Created.IsNone())
	{
		SetError(FString::Printf(TEXT("failed to create %s retarget pose '%s'"), *Side, *PoseName));
		return FString();
	}
	MarkChanged(Retargeter);
	return Created.ToString();
}

FString UUnrealBridgeRigLibrary::DuplicateIKRetargetPose(const FString& AssetPath,
	const FString& PoseName, const FString& NewName, const FString& Side)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRetargeter* Retargeter = LoadRetargeter(AssetPath);
	UIKRetargeterController* Controller = Retargeter ? UIKRetargeterController::GetController(Retargeter) : nullptr;
	bool bValid = false;
	const ERetargetSourceOrTarget ParsedSide = ParseSide(Side, bValid);
	if (!Controller || !bValid) return FString();
	FScopedTransaction Transaction(LOCTEXT("DuplicateIKRetargetPose", "UnrealBridge: Duplicate IK Retarget Pose"));
	Retargeter->Modify();
	const FName Created = Controller->DuplicateRetargetPose(FName(*PoseName), FName(*NewName), ParsedSide);
	if (Created.IsNone())
	{
		SetError(FString::Printf(TEXT("retarget pose '%s' was not found"), *PoseName));
		return FString();
	}
	MarkChanged(Retargeter);
	return Created.ToString();
}

bool UUnrealBridgeRigLibrary::RenameIKRetargetPose(const FString& AssetPath,
	const FString& PoseName, const FString& NewName, const FString& Side)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRetargeter* Retargeter = LoadRetargeter(AssetPath);
	UIKRetargeterController* Controller = Retargeter ? UIKRetargeterController::GetController(Retargeter) : nullptr;
	bool bValid = false;
	const ERetargetSourceOrTarget ParsedSide = ParseSide(Side, bValid);
	if (!Controller || !bValid) return false;
	FScopedTransaction Transaction(LOCTEXT("RenameIKRetargetPose", "UnrealBridge: Rename IK Retarget Pose"));
	Retargeter->Modify();
	if (!Controller->RenameRetargetPose(FName(*PoseName), FName(*NewName), ParsedSide))
		return SetError(FString::Printf(TEXT("retarget pose '%s' was not found"), *PoseName));
	MarkChanged(Retargeter);
	return true;
}

bool UUnrealBridgeRigLibrary::RemoveIKRetargetPose(
	const FString& AssetPath, const FString& PoseName, const FString& Side)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRetargeter* Retargeter = LoadRetargeter(AssetPath);
	UIKRetargeterController* Controller = Retargeter ? UIKRetargeterController::GetController(Retargeter) : nullptr;
	bool bValid = false;
	const ERetargetSourceOrTarget ParsedSide = ParseSide(Side, bValid);
	if (!Controller || !bValid) return false;
	FScopedTransaction Transaction(LOCTEXT("RemoveIKRetargetPose", "UnrealBridge: Remove IK Retarget Pose"));
	Retargeter->Modify();
	if (!Controller->RemoveRetargetPose(FName(*PoseName), ParsedSide))
		return SetError(FString::Printf(TEXT("retarget pose '%s' was not found"), *PoseName));
	MarkChanged(Retargeter);
	return true;
}

bool UUnrealBridgeRigLibrary::SetCurrentIKRetargetPose(
	const FString& AssetPath, const FString& PoseName, const FString& Side)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRetargeter* Retargeter = LoadRetargeter(AssetPath);
	UIKRetargeterController* Controller = Retargeter ? UIKRetargeterController::GetController(Retargeter) : nullptr;
	bool bValid = false;
	const ERetargetSourceOrTarget ParsedSide = ParseSide(Side, bValid);
	if (!Controller || !bValid) return false;
	FScopedTransaction Transaction(LOCTEXT("SetCurrentIKRetargetPose", "UnrealBridge: Set Current IK Retarget Pose"));
	Retargeter->Modify();
	if (!Controller->SetCurrentRetargetPose(FName(*PoseName), ParsedSide))
		return SetError(FString::Printf(TEXT("retarget pose '%s' was not found"), *PoseName));
	MarkChanged(Retargeter);
	return true;
}

bool UUnrealBridgeRigLibrary::SetIKRetargetPoseBoneRotation(const FString& AssetPath,
	const FString& Side, const FString& BoneName, const FQuat& RotationOffset)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRetargeter* Retargeter = LoadRetargeter(AssetPath);
	UIKRetargeterController* Controller = Retargeter ? UIKRetargeterController::GetController(Retargeter) : nullptr;
	bool bValid = false;
	const ERetargetSourceOrTarget ParsedSide = ParseSide(Side, bValid);
	if (!Controller || !bValid) return false;
	FScopedTransaction Transaction(LOCTEXT("SetIKRetargetPoseBoneRotation", "UnrealBridge: Set IK Retarget Pose Bone Rotation"));
	Retargeter->Modify();
	Controller->SetRotationOffsetForRetargetPoseBone(FName(*BoneName), RotationOffset.GetNormalized(), ParsedSide);
	MarkChanged(Retargeter);
	return true;
}

bool UUnrealBridgeRigLibrary::SetIKRetargetPoseRootOffset(
	const FString& AssetPath, const FString& Side, const FVector& TranslationOffset)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRetargeter* Retargeter = LoadRetargeter(AssetPath);
	UIKRetargeterController* Controller = Retargeter ? UIKRetargeterController::GetController(Retargeter) : nullptr;
	bool bValid = false;
	const ERetargetSourceOrTarget ParsedSide = ParseSide(Side, bValid);
	if (!Controller || !bValid) return false;
	FScopedTransaction Transaction(LOCTEXT("SetIKRetargetPoseRootOffset", "UnrealBridge: Set IK Retarget Pose Root Offset"));
	Retargeter->Modify();
	Controller->SetRootOffsetInRetargetPose(TranslationOffset, ParsedSide);
	MarkChanged(Retargeter);
	return true;
}

bool UUnrealBridgeRigLibrary::ResetIKRetargetPose(const FString& AssetPath, const FString& Side,
	const FString& PoseName, const TArray<FString>& BoneNames)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRetargeter* Retargeter = LoadRetargeter(AssetPath);
	UIKRetargeterController* Controller = Retargeter ? UIKRetargeterController::GetController(Retargeter) : nullptr;
	bool bValid = false;
	const ERetargetSourceOrTarget ParsedSide = ParseSide(Side, bValid);
	if (!Controller || !bValid) return false;
	TArray<FName> Bones;
	for (const FString& Bone : BoneNames) Bones.Add(FName(*Bone));
	FScopedTransaction Transaction(LOCTEXT("ResetIKRetargetPose", "UnrealBridge: Reset IK Retarget Pose"));
	Retargeter->Modify();
	Controller->ResetRetargetPose(FName(*PoseName), Bones, ParsedSide);
	MarkChanged(Retargeter);
	return true;
}

bool UUnrealBridgeRigLibrary::AutoAlignIKRetargetPose(const FString& AssetPath, const FString& Side,
	const TArray<FString>& BoneNames, const FString& Method)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRetargeter* Retargeter = LoadRetargeter(AssetPath);
	UIKRetargeterController* Controller = Retargeter ? UIKRetargeterController::GetController(Retargeter) : nullptr;
	bool bValid = false;
	const ERetargetSourceOrTarget ParsedSide = ParseSide(Side, bValid);
	if (!Controller || !bValid) return false;
	ERetargetAutoAlignMethod ParsedMethod = ERetargetAutoAlignMethod::ChainToChain;
	if (!ParseEnumValue(Method.IsEmpty() ? TEXT("ChainToChain") : Method, ParsedMethod, TEXT("auto-align method"))) return false;
	TArray<FName> Bones;
	for (const FString& Bone : BoneNames) Bones.Add(FName(*Bone));
	FScopedTransaction Transaction(LOCTEXT("AutoAlignIKRetargetPose", "UnrealBridge: Auto Align IK Retarget Pose"));
	Retargeter->Modify();
	if (Bones.IsEmpty()) Controller->AutoAlignAllBones(ParsedSide, ParsedMethod);
	else Controller->AutoAlignBones(Bones, ParsedMethod, ParsedSide);
	MarkChanged(Retargeter);
	return true;
}

TArray<FString> UUnrealBridgeRigLibrary::ListIKRetargetProfiles(const FString& AssetPath)
{
	using namespace BridgeRigImpl;
	ClearError();
	TArray<FString> Result;
	UIKRetargeter* Retargeter = LoadRetargeter(AssetPath);
	FMapProperty* ProfilesProperty = GetProfilesProperty(Retargeter);
	if (!Retargeter || !ProfilesProperty)
	{
		if (Retargeter) SetError(TEXT("IK Retargeter profile storage is unavailable on this engine build"));
		return Result;
	}
	FScriptMapHelper Helper(ProfilesProperty, ProfilesProperty->ContainerPtrToValuePtr<void>(Retargeter));
	for (FScriptMapHelper::FIterator It(Helper); It; ++It)
	{
		const FName* Key = reinterpret_cast<const FName*>(Helper.GetKeyPtr(It));
		if (Key) Result.Add(Key->ToString());
	}
	Result.Sort();
	return Result;
}

bool UUnrealBridgeRigLibrary::SaveCurrentIKRetargetProfile(const FString& AssetPath,
	const FString& ProfileName, bool bApplySourcePose, bool bApplyTargetPose, bool bForceAllIKOff)
{
	using namespace BridgeRigImpl;
	ClearError();
	if (ProfileName.TrimStartAndEnd().IsEmpty()) return SetError(TEXT("retarget profile name is empty"));
	UIKRetargeter* Retargeter = LoadRetargeter(AssetPath);
	FMapProperty* ProfilesProperty = GetProfilesProperty(Retargeter);
	FStructProperty* ValueProperty = ProfilesProperty ? CastField<FStructProperty>(ProfilesProperty->ValueProp) : nullptr;
	if (!Retargeter || !ProfilesProperty || !ValueProperty || ValueProperty->Struct != FRetargetProfile::StaticStruct())
		return Retargeter ? SetError(TEXT("IK Retargeter profile storage is unavailable on this engine build")) : false;
	FScopedTransaction Transaction(LOCTEXT("SaveCurrentIKRetargetProfile", "UnrealBridge: Save IK Retarget Profile"));
	Retargeter->Modify();
	FScriptMapHelper Helper(ProfilesProperty, ProfilesProperty->ContainerPtrToValuePtr<void>(Retargeter));
	const FName KeyName(*ProfileName);
	int32 Index = FindProfileIndex(Helper, KeyName);
	if (Index == INDEX_NONE)
	{
		Index = Helper.AddDefaultValue_Invalid_NeedsRehash();
		*reinterpret_cast<FName*>(Helper.GetKeyPtr(Index)) = KeyName;
		Helper.Rehash();
		Index = FindProfileIndex(Helper, KeyName);
	}
	if (Index == INDEX_NONE) return SetError(FString::Printf(TEXT("failed to allocate retarget profile '%s'"), *ProfileName));
	FRetargetProfile* Profile = reinterpret_cast<FRetargetProfile*>(Helper.GetValuePtr(Index));
	if (!Profile) return SetError(TEXT("retarget profile value storage is invalid"));
	Profile->FillProfileWithAssetSettings(Retargeter);
	Profile->bApplySourceRetargetPose = bApplySourcePose;
	Profile->SourceRetargetPoseName = bApplySourcePose
		? Retargeter->GetCurrentRetargetPoseName(ERetargetSourceOrTarget::Source) : NAME_None;
	Profile->bApplyTargetRetargetPose = bApplyTargetPose;
	Profile->TargetRetargetPoseName = bApplyTargetPose
		? Retargeter->GetCurrentRetargetPoseName(ERetargetSourceOrTarget::Target) : NAME_None;
	Profile->bForceAllIKOff = bForceAllIKOff;
	Retargeter->IncrementVersion();
	MarkChanged(Retargeter);
	return true;
}

bool UUnrealBridgeRigLibrary::RemoveIKRetargetProfile(const FString& AssetPath, const FString& ProfileName)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRetargeter* Retargeter = LoadRetargeter(AssetPath);
	FMapProperty* ProfilesProperty = GetProfilesProperty(Retargeter);
	if (!Retargeter || !ProfilesProperty)
		return Retargeter ? SetError(TEXT("IK Retargeter profile storage is unavailable on this engine build")) : false;
	FScopedTransaction Transaction(LOCTEXT("RemoveIKRetargetProfile", "UnrealBridge: Remove IK Retarget Profile"));
	Retargeter->Modify();
	FScriptMapHelper Helper(ProfilesProperty, ProfilesProperty->ContainerPtrToValuePtr<void>(Retargeter));
	const FName KeyName(*ProfileName);
	const int32 Index = FindProfileIndex(Helper, KeyName);
	if (Index == INDEX_NONE) return SetError(FString::Printf(TEXT("retarget profile '%s' was not found"), *ProfileName));
	Helper.RemoveAt(Index);
	if (FNameProperty* CurrentProperty = GetCurrentProfileProperty(Retargeter))
	{
		if (CurrentProperty->GetPropertyValue_InContainer(Retargeter).IsEqual(KeyName, ENameCase::IgnoreCase))
			CurrentProperty->SetPropertyValue_InContainer(Retargeter, NAME_None);
	}
	Retargeter->IncrementVersion();
	MarkChanged(Retargeter);
	return true;
}

bool UUnrealBridgeRigLibrary::SetCurrentIKRetargetProfile(const FString& AssetPath, const FString& ProfileName)
{
	using namespace BridgeRigImpl;
	ClearError();
	UIKRetargeter* Retargeter = LoadRetargeter(AssetPath);
	if (!Retargeter) return false;
	const FName KeyName(*ProfileName);
	if (!ProfileName.TrimStartAndEnd().IsEmpty() && !FindProfile(Retargeter, KeyName))
		return SetError(FString::Printf(TEXT("retarget profile '%s' was not found"), *ProfileName));
	FNameProperty* CurrentProperty = GetCurrentProfileProperty(Retargeter);
	if (!CurrentProperty) return SetError(TEXT("IK Retargeter current profile storage is unavailable"));
	FScopedTransaction Transaction(LOCTEXT("SetCurrentIKRetargetProfile", "UnrealBridge: Set Current IK Retarget Profile"));
	Retargeter->Modify();
	CurrentProperty->SetPropertyValue_InContainer(Retargeter, ProfileName.IsEmpty() ? NAME_None : KeyName);
	Retargeter->IncrementVersion();
	MarkChanged(Retargeter);
	return true;
}

FBridgeRetargetBatchResult UUnrealBridgeRigLibrary::BatchRetargetAnimations(const FString& RetargeterPath,
	const TArray<FString>& SourceAssetPaths, const FString& SourceMeshPath, const FString& TargetMeshPath,
	const FString& DestinationFolder, const FString& Search, const FString& Replace, const FString& Prefix,
	const FString& Suffix, bool bIncludeReferencedAssets, bool bOverwriteExisting, bool bSave)
{
	using namespace BridgeRigImpl;
	ClearError();
	FBridgeRetargetBatchResult Result;
	UIKRetargeter* Retargeter = LoadRetargeter(RetargeterPath);
	USkeletalMesh* SourceMesh = LoadRigAsset<USkeletalMesh>(SourceMeshPath, TEXT("source Skeletal Mesh"));
	USkeletalMesh* TargetMesh = LoadRigAsset<USkeletalMesh>(TargetMeshPath, TEXT("target Skeletal Mesh"));
	if (!Retargeter || !SourceMesh || !TargetMesh)
	{
		Result.Error = LastError;
		return Result;
	}
	FString Destination = DestinationFolder.TrimStartAndEnd();
	while (Destination.EndsWith(TEXT("/"))) Destination.LeftChopInline(1);
	if (!Destination.StartsWith(TEXT("/Game")) || !FPackageName::IsValidLongPackageName(Destination))
	{
		SetError(FString::Printf(TEXT("destination folder '%s' is not a valid /Game package path"), *DestinationFolder));
		Result.Error = LastError;
		return Result;
	}
	TArray<FAssetData> SourceAssets;
	for (const FString& Path : SourceAssetPaths)
	{
		UObject* Asset = LoadObject<UObject>(nullptr, *NormalizeObjectPath(Path));
		if (!Asset)
		{
			Result.FailedSourceAssetPaths.Add(Path);
			continue;
		}
		SourceAssets.Emplace(Asset);
	}
	if (SourceAssets.IsEmpty())
	{
		SetError(TEXT("no valid source animation assets were supplied"));
		Result.Error = LastError;
		return Result;
	}
	TArray<FAssetData> Created;
#if !UE_VERSION_OLDER_THAN(5, 8, 0)
	FIKRetargetBatchOperationInputs Inputs;
	Inputs.AssetsToRetarget = SourceAssets;
	Inputs.SourceMesh = SourceMesh;
	Inputs.TargetMesh = TargetMesh;
	Inputs.IKRetargetAsset = Retargeter;
	Inputs.Search = Search;
	Inputs.Replace = Replace;
	Inputs.Prefix = Prefix;
	Inputs.Suffix = Suffix;
	Inputs.TargetPath = Destination;
	Inputs.bUseSourcePath = false;
	Inputs.bIncludeReferencedAssets = bIncludeReferencedAssets;
	Inputs.bOverwriteExistingFiles = bOverwriteExisting;
	Created = UIKRetargetBatchOperation::RunBatchRetarget(Inputs);
#else
	Created = UIKRetargetBatchOperation::DuplicateAndRetarget(SourceAssets, SourceMesh, TargetMesh,
		Retargeter, Search, Replace, Prefix, Suffix, bIncludeReferencedAssets, bOverwriteExisting);
#endif
	if (Created.IsEmpty())
	{
		for (const FAssetData& Data : SourceAssets) Result.FailedSourceAssetPaths.AddUnique(Data.GetObjectPathString());
		SetError(TEXT("batch retargeting produced no assets; validate the retargeter and mesh compatibility"));
		Result.Error = LastError;
		return Result;
	}
	TArray<UObject*> CreatedObjects;
	TArray<FAssetRenameData> Renames;
	for (const FAssetData& Data : Created)
	{
		if (UObject* Asset = Data.GetAsset())
		{
			CreatedObjects.Add(Asset);
			if (!Destination.IsEmpty() && FPackageName::GetLongPackagePath(Asset->GetOutermost()->GetName()) != Destination)
				Renames.Emplace(Asset, Destination, Asset->GetName());
		}
	}
	if (!Renames.IsEmpty())
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		if (!AssetTools.RenameAssets(Renames))
		{
			SetError(TEXT("retargeted assets were created, but one or more could not be moved to the destination folder"));
			Result.Error = LastError;
		}
	}
	TArray<UPackage*> Packages;
	for (UObject* Asset : CreatedObjects)
	{
		if (!Asset) continue;
		Result.CreatedAssetPaths.Add(Asset->GetPathName());
		Packages.AddUnique(Asset->GetOutermost());
	}
	if (bSave && !Packages.IsEmpty() && !UEditorLoadingAndSavingUtils::SavePackages(Packages, false))
	{
		SetError(TEXT("one or more retargeted animation packages could not be saved"));
		Result.Error = LastError;
	}
	Result.bSuccess = Result.CreatedAssetPaths.Num() > 0 && Result.Error.IsEmpty();
	return Result;
}

FBridgeRigValidationReport UUnrealBridgeRigLibrary::ValidateIKRetargeter(
	const FString& AssetPath, bool bInitializeProcessor, bool bSave)
{
	using namespace BridgeRigImpl;
	ClearError();
	FBridgeRigValidationReport Report;
	UIKRetargeter* Retargeter = LoadRetargeter(AssetPath);
	if (!Retargeter)
	{
		Report.Error = LastError;
		return Report;
	}
	Report.bFound = true;
	UIKRetargeterController* Controller = UIKRetargeterController::GetController(Retargeter);
	if (!Controller)
	{
		AddIssue(Report, TEXT("Error"), TEXT("MissingController"), Retargeter->GetPathName(),
			TEXT("IK Retargeter has no editor controller"));
	}
	else
	{
		const UIKRigDefinition* SourceRig = Controller->GetIKRig(ERetargetSourceOrTarget::Source);
		const UIKRigDefinition* TargetRig = Controller->GetIKRig(ERetargetSourceOrTarget::Target);
		USkeletalMesh* SourceMesh = Controller->GetPreviewMesh(ERetargetSourceOrTarget::Source);
		USkeletalMesh* TargetMesh = Controller->GetPreviewMesh(ERetargetSourceOrTarget::Target);
		if (!SourceRig) AddIssue(Report, TEXT("Error"), TEXT("MissingSourceRig"), Retargeter->GetPathName(),
			TEXT("source IK Rig is not assigned"));
		if (!TargetRig) AddIssue(Report, TEXT("Error"), TEXT("MissingTargetRig"), Retargeter->GetPathName(),
			TEXT("target IK Rig is not assigned"));
		if (!SourceMesh) AddIssue(Report, TEXT("Error"), TEXT("MissingSourceMesh"), Retargeter->GetPathName(),
			TEXT("source preview mesh is not assigned"));
		if (!TargetMesh) AddIssue(Report, TEXT("Error"), TEXT("MissingTargetMesh"), Retargeter->GetPathName(),
			TEXT("target preview mesh is not assigned"));
		if (Controller->GetNumRetargetOps() == 0) AddIssue(Report, TEXT("Error"), TEXT("EmptyOpStack"),
			Retargeter->GetPathName(), TEXT("retarget op stack is empty"));
		for (int32 Index = 0; Index < Controller->GetNumRetargetOps(); ++Index)
		{
			const FIKRetargetOpBase* Op = Controller->GetRetargetOpByIndex(Index);
			if (!Op) continue;
			// Asset-side ops report "not initialized" until they are owned by a
			// processor. When the caller requests real processor initialization,
			// its logger below is the authoritative validation source and avoids
			// surfacing those transient warnings as false delivery issues.
			if (!bInitializeProcessor)
			{
				const FText Warning = Op->GetWarningMessage();
				if (!Warning.IsEmpty()) AddIssue(Report, TEXT("Warning"), TEXT("RetargetOpWarning"),
					Op->GetName().ToString(), Warning.ToString());
			}
			if (!Op->GetParentOpName().IsNone() && Controller->GetIndexOfOpByName(Op->GetParentOpName()) == INDEX_NONE)
				AddIssue(Report, TEXT("Error"), TEXT("MissingParentOp"), Op->GetName().ToString(),
					FString::Printf(TEXT("parent op '%s' does not exist"), *Op->GetParentOpName().ToString()));
			if (const FRetargetChainMapping* Mapping = Op->GetChainMapping())
			{
				for (const FRetargetChainPair& Pair : Mapping->GetChainPairs())
				{
					if (Pair.SourceChainName.IsNone()) AddIssue(Report, TEXT("Warning"), TEXT("UnmappedChain"),
						Pair.TargetChainName.ToString(), FString::Printf(TEXT("target chain is unmapped in op '%s'"),
							*Op->GetName().ToString()));
				}
			}
		}
		if (bInitializeProcessor && Report.ErrorCount == 0)
		{
			FRetargetProfile Profile;
			if (const FRetargetProfile* Current = Retargeter->GetCurrentProfile()) Profile = *Current;
			FIKRetargetProcessor Processor;
		#if !UE_VERSION_OLDER_THAN(5, 8, 0)
			FRetargetInitParameters InitParameters;
			InitParameters.SourceSkeletalMesh = SourceMesh;
			InitParameters.TargetSkeletalMesh = TargetMesh;
			InitParameters.RetargeterAsset = Retargeter;
			InitParameters.CustomProfile = &Profile;
			InitParameters.bSuppressWarnings = false;
			Processor.Initialize(InitParameters);
		#else
			Processor.Initialize(SourceMesh, TargetMesh, Retargeter, Profile, false);
		#endif
			for (const FText& Error : Processor.Log.GetErrors())
				AddIssue(Report, TEXT("Error"), TEXT("ProcessorInit"), Retargeter->GetPathName(), Error.ToString());
			for (const FText& Warning : Processor.Log.GetWarnings())
				AddIssue(Report, TEXT("Warning"), TEXT("ProcessorInit"), Retargeter->GetPathName(), Warning.ToString());
			for (const FText& Message : Processor.Log.GetMessages())
				AddIssue(Report, TEXT("Info"), TEXT("ProcessorInit"), Retargeter->GetPathName(), Message.ToString());
			if (!Processor.IsInitialized() && Processor.Log.GetErrors().IsEmpty())
				AddIssue(Report, TEXT("Error"), TEXT("ProcessorInit"), Retargeter->GetPathName(),
					TEXT("retarget processor failed to initialize"));
		}
	}
	Report.bCompiled = Report.ErrorCount == 0;
	if (bSave) Report.bSaved = SaveAsset(Retargeter);
	Report.bSuccess = Report.ErrorCount == 0 && (!bSave || Report.bSaved);
	if (!Report.bSuccess) Report.Error = Report.ErrorCount > 0
		? TEXT("IK Retargeter validation failed") : TEXT("IK Retargeter package could not be saved");
	return Report;
}

FBridgeAnimationQualityReport UUnrealBridgeRigLibrary::AnalyzeAnimationQuality(const FString& AnimationPath,
	const TArray<FString>& FootBoneNames, int32 NumSamples, float ContactHeightTolerance,
	float FootSlideSpeedTolerance, float JointAngularDeltaToleranceDegrees, int32 MaxReportedBones)
{
	using namespace BridgeRigImpl;
	ClearError();
	FBridgeAnimationQualityReport Report;
	UAnimSequence* Sequence = LoadRigAsset<UAnimSequence>(AnimationPath, TEXT("Animation Sequence"));
	if (!Sequence)
	{
		Report.Error = LastError;
		return Report;
	}
	USkeleton* Skeleton = Sequence->GetSkeleton();
	if (!Skeleton)
	{
		SetError(TEXT("animation sequence has no skeleton"));
		Report.Error = LastError;
		return Report;
	}
	const FReferenceSkeleton& Reference = Skeleton->GetReferenceSkeleton();
	const int32 BoneCount = Reference.GetNum();
	if (BoneCount == 0)
	{
		SetError(TEXT("animation skeleton contains no bones"));
		Report.Error = LastError;
		return Report;
	}
	Report.AnimationPath = Sequence->GetPathName();
	Report.Duration = Sequence->GetPlayLength();
	if (NumSamples <= 0)
	{
		const double Rate = Sequence->GetSamplingFrameRate().AsDecimal();
		NumSamples = FMath::CeilToInt(FMath::Max(1.0, Rate) * FMath::Max(0.0f, Report.Duration)) + 1;
	}
	NumSamples = FMath::Clamp(NumSamples, 2, 2000);
	Report.Samples = NumSamples;
	ContactHeightTolerance = FMath::Max(0.f, ContactHeightTolerance);
	FootSlideSpeedTolerance = FMath::Max(0.f, FootSlideSpeedTolerance);
	JointAngularDeltaToleranceDegrees = FMath::Max(0.f, JointAngularDeltaToleranceDegrees);
	MaxReportedBones = FMath::Clamp(MaxReportedBones <= 0 ? 32 : MaxReportedBones, 1, BoneCount);
	TSet<int32> FootIndices;
	if (!FootBoneNames.IsEmpty())
	{
		for (const FString& BoneName : FootBoneNames)
		{
			const int32 Index = Reference.FindBoneIndex(FName(*BoneName));
			if (Index == INDEX_NONE)
				AddQualityIssue(Report, TEXT("Warning"), TEXT("MissingFootBone"), BoneName,
					TEXT("requested foot bone does not exist in the animation skeleton"));
			else FootIndices.Add(Index);
		}
	}
	else
	{
		for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
		{
			const FString Name = Reference.GetBoneName(BoneIndex).ToString().ToLower();
			if (Name.Contains(TEXT("foot")) || Name.Contains(TEXT("ball")) || Name.Contains(TEXT("toe")))
				FootIndices.Add(BoneIndex);
		}
	}
	struct FWorkingMetric
	{
		float MinHeight = TNumericLimits<float>::Max();
		float MaxAngularDelta = 0.f;
		FQuat PreviousRotation = FQuat::Identity;
		bool bHasPrevious = false;
	};
	TArray<FWorkingMetric> Working;
	Working.SetNum(BoneCount);
	TMap<int32, TArray<FVector>> FootPositions;
	TMap<int32, TArray<float>> FootHeights;
	for (const int32 Index : FootIndices)
	{
		FootPositions.Add(Index).Reserve(NumSamples);
		FootHeights.Add(Index).Reserve(NumSamples);
	}
	TArray<FTransform> LocalPose;
	TArray<FTransform> GlobalPose;
	LocalPose.SetNum(BoneCount);
	GlobalPose.SetNum(BoneCount);
	FVector PreviousRoot = FVector::ZeroVector;
	bool bHasPreviousRoot = false;
	const float DeltaTime = NumSamples > 1 ? Report.Duration / static_cast<float>(NumSamples - 1) : 0.f;
	for (int32 Sample = 0; Sample < NumSamples; ++Sample)
	{
		const double Time = NumSamples > 1 ? static_cast<double>(Report.Duration) * Sample / (NumSamples - 1) : 0.0;
		const FAnimExtractContext ExtractionContext(Time);
		for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
		{
			Sequence->GetBoneTransform(LocalPose[BoneIndex], FSkeletonPoseBoneIndex(BoneIndex), ExtractionContext, false);
			const int32 Parent = Reference.GetParentIndex(BoneIndex);
			GlobalPose[BoneIndex] = Parent == INDEX_NONE ? LocalPose[BoneIndex] : LocalPose[BoneIndex] * GlobalPose[Parent];
			const FQuat Rotation = GlobalPose[BoneIndex].GetRotation().GetNormalized();
			FWorkingMetric& Metric = Working[BoneIndex];
			if (Metric.bHasPrevious)
			{
				Metric.MaxAngularDelta = FMath::Max(Metric.MaxAngularDelta,
					FMath::RadiansToDegrees(Metric.PreviousRotation.AngularDistance(Rotation)));
			}
			Metric.PreviousRotation = Rotation;
			Metric.bHasPrevious = true;
		}
		const FVector RootPosition = GlobalPose[0].GetTranslation();
		if (bHasPreviousRoot && DeltaTime > SMALL_NUMBER)
		{
			const FVector Delta = RootPosition - PreviousRoot;
			Report.MaximumRootSpeed = FMath::Max(Report.MaximumRootSpeed,
				FVector2D(Delta.X, Delta.Y).Size() / DeltaTime);
		}
		PreviousRoot = RootPosition;
		bHasPreviousRoot = true;
		for (const int32 FootIndex : FootIndices)
		{
			const FVector Position = GlobalPose[FootIndex].GetTranslation();
			const float RelativeHeight = Position.Z - RootPosition.Z;
			FootPositions.FindChecked(FootIndex).Add(Position);
			FootHeights.FindChecked(FootIndex).Add(RelativeHeight);
			Working[FootIndex].MinHeight = FMath::Min(Working[FootIndex].MinHeight, RelativeHeight);
		}
	}
	Report.MinimumFootHeight = FootIndices.IsEmpty() ? 0.f : TNumericLimits<float>::Max();
	for (const int32 FootIndex : FootIndices)
	{
		FWorkingMetric& Metric = Working[FootIndex];
		Report.MinimumFootHeight = FMath::Min(Report.MinimumFootHeight, Metric.MinHeight);
		const TArray<FVector>& Positions = FootPositions.FindChecked(FootIndex);
		const TArray<float>& Heights = FootHeights.FindChecked(FootIndex);
		float MaxSlide = 0.f;
		for (int32 Sample = 1; Sample < Positions.Num(); ++Sample)
		{
			if (DeltaTime <= SMALL_NUMBER) break;
			if (Heights[Sample] <= Metric.MinHeight + ContactHeightTolerance
				&& Heights[Sample - 1] <= Metric.MinHeight + ContactHeightTolerance)
			{
				const FVector Delta = Positions[Sample] - Positions[Sample - 1];
				MaxSlide = FMath::Max(MaxSlide, FVector2D(Delta.X, Delta.Y).Size() / DeltaTime);
			}
		}
		Report.MaximumFootSlideSpeed = FMath::Max(Report.MaximumFootSlideSpeed, MaxSlide);
	}
	for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
	{
		FBridgeAnimationBoneMetric Metric;
		Metric.BoneName = Reference.GetBoneName(BoneIndex).ToString();
		Metric.MinimumRelativeHeight = FootIndices.Contains(BoneIndex) ? Working[BoneIndex].MinHeight : 0.f;
		Metric.MaximumAngularDeltaDegrees = Working[BoneIndex].MaxAngularDelta;
		Metric.bFootBone = FootIndices.Contains(BoneIndex);
		if (Metric.bFootBone)
		{
			const TArray<FVector>& Positions = FootPositions.FindChecked(BoneIndex);
			const TArray<float>& Heights = FootHeights.FindChecked(BoneIndex);
			for (int32 Sample = 1; Sample < Positions.Num(); ++Sample)
			{
				if (DeltaTime <= SMALL_NUMBER) break;
				if (Heights[Sample] <= Working[BoneIndex].MinHeight + ContactHeightTolerance
					&& Heights[Sample - 1] <= Working[BoneIndex].MinHeight + ContactHeightTolerance)
				{
					const FVector Delta = Positions[Sample] - Positions[Sample - 1];
					Metric.MaximumHorizontalSpeed = FMath::Max(Metric.MaximumHorizontalSpeed,
						FVector2D(Delta.X, Delta.Y).Size() / DeltaTime);
				}
			}
		}
		Metric.bFlagged = Metric.MaximumAngularDeltaDegrees > JointAngularDeltaToleranceDegrees
			|| (Metric.bFootBone && Metric.MaximumHorizontalSpeed > FootSlideSpeedTolerance)
			|| (Metric.bFootBone && Metric.MinimumRelativeHeight < -ContactHeightTolerance);
		Report.MaximumJointAngularDeltaDegrees = FMath::Max(Report.MaximumJointAngularDeltaDegrees,
			Metric.MaximumAngularDeltaDegrees);
		Report.BoneMetrics.Add(MoveTemp(Metric));
	}
	Report.BoneMetrics.Sort([](const FBridgeAnimationBoneMetric& A, const FBridgeAnimationBoneMetric& B)
	{
		if (A.bFlagged != B.bFlagged) return A.bFlagged;
		if (A.bFootBone != B.bFootBone) return A.bFootBone;
		return A.MaximumAngularDeltaDegrees > B.MaximumAngularDeltaDegrees;
	});
	if (Report.BoneMetrics.Num() > MaxReportedBones) Report.BoneMetrics.SetNum(MaxReportedBones);
	if (Report.MinimumFootHeight < -ContactHeightTolerance)
		AddQualityIssue(Report, TEXT("Warning"), TEXT("FootPenetration"), Sequence->GetPathName(),
			FString::Printf(TEXT("minimum relative foot height %.3f is below tolerance %.3f"),
				Report.MinimumFootHeight, ContactHeightTolerance));
	if (Report.MaximumFootSlideSpeed > FootSlideSpeedTolerance)
		AddQualityIssue(Report, TEXT("Warning"), TEXT("FootSlide"), Sequence->GetPathName(),
			FString::Printf(TEXT("maximum contact foot speed %.3f exceeds tolerance %.3f"),
				Report.MaximumFootSlideSpeed, FootSlideSpeedTolerance));
	if (Report.MaximumJointAngularDeltaDegrees > JointAngularDeltaToleranceDegrees)
		AddQualityIssue(Report, TEXT("Warning"), TEXT("JointDiscontinuity"), Sequence->GetPathName(),
			FString::Printf(TEXT("maximum per-sample joint rotation %.3f degrees exceeds tolerance %.3f"),
				Report.MaximumJointAngularDeltaDegrees, JointAngularDeltaToleranceDegrees));
	if (FootIndices.IsEmpty())
		AddQualityIssue(Report, TEXT("Info"), TEXT("NoFootBones"), Sequence->GetPathName(),
			TEXT("no foot bones were supplied or auto-detected; foot contact checks were skipped"));
	Report.bSuccess = true;
	return Report;
}

#undef LOCTEXT_NAMESPACE

#endif // !UE_VERSION_OLDER_THAN(5, 7, 0)
