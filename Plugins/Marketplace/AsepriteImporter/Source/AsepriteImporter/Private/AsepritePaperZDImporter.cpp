// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsepritePaperZDImporter.h"
#include "Aseprite.h"
#include "PaperFlipbook.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "Factories/Factory.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "PackageTools.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace
{
	static const TCHAR* PaperZDAnimationSourceFlipbookClassPath = TEXT("/Script/PaperZD.PaperZDAnimationSource_Flipbook");
	static const TCHAR* PaperZDAnimSequenceFlipbookClassPath = TEXT("/Script/PaperZD.PaperZDAnimSequence_Flipbook");
	static const TCHAR* PaperZDAnimBPClassPath = TEXT("/Script/PaperZD.PaperZDAnimBP");
	static const TCHAR* PaperZDAnimBPGeneratedClassPath = TEXT("/Script/PaperZD.PaperZDAnimBPGeneratedClass");
	static const TCHAR* PaperZDAnimInstanceClassPath = TEXT("/Script/PaperZD.PaperZDAnimInstance");
	static const TCHAR* PaperZDAnimGraphNodeStateMachineClassPath = TEXT("/Script/PaperZDEditor.PaperZDAnimGraphNode_StateMachine");
	static const TCHAR* PaperZDStateGraphNodeStateClassPath = TEXT("/Script/PaperZDEditor.PaperZDStateGraphNode_State");
	static const TCHAR* PaperZDAnimGraphNodePlaySequenceClassPath = TEXT("/Script/PaperZDEditor.PaperZDAnimGraphNode_PlaySequence");

	UClass* LoadPaperZDClass(const TCHAR* ClassPath)
	{
		return LoadObject<UClass>(nullptr, ClassPath);
	}

	UObject* FindAsset(FAssetRegistryModule& AssetRegistryModule, const FString& PackageName, const FString& AssetName)
	{
		const FSoftObjectPath ObjectPath(PackageName + TEXT(".") + AssetName);
		const FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(ObjectPath);
		return AssetData.IsValid() ? AssetData.GetAsset() : nullptr;
	}

	UObject* FindOrCreateAsset(FAssetToolsModule& AssetToolsModule, FAssetRegistryModule& AssetRegistryModule, UClass* AssetClass, const FString& PackagePath, const FString& DesiredAssetName)
	{
		const FString TentativePackageName = UPackageTools::SanitizePackageName(PackagePath / DesiredAssetName);

		if (UObject* ExistingAsset = FindAsset(AssetRegistryModule, TentativePackageName, DesiredAssetName))
		{
			return ExistingAsset->IsA(AssetClass) ? ExistingAsset : nullptr;
		}

		FString DefaultSuffix;
		FString AssetName;
		FString PackageName;
		AssetToolsModule.Get().CreateUniqueAssetName(TentativePackageName, DefaultSuffix, PackageName, AssetName);

		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			return nullptr;
		}

		UObject* NewAsset = NewObject<UObject>(Package, AssetClass, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		if (NewAsset)
		{
			FAssetRegistryModule::AssetCreated(NewAsset);
			Package->MarkPackageDirty();
		}

		return NewAsset;
	}

	UObject* FindOrCreateAssetWithFactory(FAssetToolsModule& AssetToolsModule, FAssetRegistryModule& AssetRegistryModule, UClass* AssetClass, UFactory* Factory, const FString& PackagePath, const FString& DesiredAssetName)
	{
		const FString TentativePackageName = UPackageTools::SanitizePackageName(PackagePath / DesiredAssetName);

		if (UObject* ExistingAsset = FindAsset(AssetRegistryModule, TentativePackageName, DesiredAssetName))
		{
			return ExistingAsset->IsA(AssetClass) ? ExistingAsset : nullptr;
		}

		FString DefaultSuffix;
		FString AssetName;
		FString PackageName;
		AssetToolsModule.Get().CreateUniqueAssetName(TentativePackageName, DefaultSuffix, PackageName, AssetName);
		const FString CreatedPackagePath = FPackageName::GetLongPackagePath(PackageName);
		return AssetToolsModule.Get().CreateAsset(AssetName, CreatedPackagePath, AssetClass, Factory);
	}

	UObject* GetObjectPropertyValue(UObject* Object, const FName PropertyName)
	{
		if (!Object)
		{
			return nullptr;
		}

		if (FObjectPropertyBase* ObjectProperty = FindFProperty<FObjectPropertyBase>(Object->GetClass(), PropertyName))
		{
			return ObjectProperty->GetObjectPropertyValue(ObjectProperty->ContainerPtrToValuePtr<void>(Object));
		}

		return nullptr;
	}

	void SetObjectProperty(UObject* Object, const FName PropertyName, UObject* Value)
	{
		if (!Object)
		{
			return;
		}

		if (FObjectPropertyBase* ObjectProperty = FindFProperty<FObjectPropertyBase>(Object->GetClass(), PropertyName))
		{
			Object->Modify();
			ObjectProperty->SetObjectPropertyValue(ObjectProperty->ContainerPtrToValuePtr<void>(Object), Value);
		}
	}

	void SetPlaySequenceNodeData(UObject* PlaySequenceNode, UObject* AnimSequence)
	{
		if (!PlaySequenceNode || !AnimSequence)
		{
			return;
		}

		FStructProperty* AnimNodeProperty = FindFProperty<FStructProperty>(PlaySequenceNode->GetClass(), TEXT("AnimNode"));
		if (!AnimNodeProperty)
		{
			return;
		}

		void* AnimNodeData = AnimNodeProperty->ContainerPtrToValuePtr<void>(PlaySequenceNode);
		FObjectPropertyBase* AnimSequenceProperty = FindFProperty<FObjectPropertyBase>(AnimNodeProperty->Struct, TEXT("AnimSequence"));
		if (AnimSequenceProperty)
		{
			AnimSequenceProperty->SetObjectPropertyValue(AnimSequenceProperty->ContainerPtrToValuePtr<void>(AnimNodeData), AnimSequence);
		}

		FBoolProperty* LoopProperty = FindFProperty<FBoolProperty>(AnimNodeProperty->Struct, TEXT("bLoopAnimation"));
		if (LoopProperty)
		{
			LoopProperty->SetPropertyValue(LoopProperty->ContainerPtrToValuePtr<void>(AnimNodeData), true);
		}
	}

	UEdGraphPin* FindFirstPin(UEdGraphNode* Node, const EEdGraphPinDirection Direction)
	{
		if (!Node)
		{
			return nullptr;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == Direction)
			{
				return Pin;
			}
		}

		return nullptr;
	}

	void ConnectFirstPins(UEdGraphNode* OutputNode, UEdGraphNode* InputNode)
	{
		UEdGraphPin* OutputPin = FindFirstPin(OutputNode, EGPD_Output);
		UEdGraphPin* InputPin = FindFirstPin(InputNode, EGPD_Input);
		if (OutputPin && InputPin && !OutputPin->LinkedTo.Contains(InputPin))
		{
			OutputPin->MakeLinkTo(InputPin);
		}
	}

	UEdGraphNode* FindFirstNodeOfClass(UEdGraph* Graph, UClass* NodeClass)
	{
		if (!Graph || !NodeClass)
		{
			return nullptr;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->IsA(NodeClass))
			{
				return Node;
			}
		}

		return nullptr;
	}

	UEdGraphNode* CreateGraphNode(UEdGraph* Graph, UClass* NodeClass, const FVector2D Position)
	{
		if (!Graph || !NodeClass)
		{
			return nullptr;
		}

		Graph->Modify();
		UEdGraphNode* Node = NewObject<UEdGraphNode>(Graph, NodeClass, NAME_None, RF_Transactional);
		if (!Node)
		{
			return nullptr;
		}

		Graph->AddNode(Node, true, false);
		Node->CreateNewGuid();
		Node->NodePosX = Position.X;
		Node->NodePosY = Position.Y;
		Node->AllocateDefaultPins();
		Node->PostPlacedNewNode();
		Node->ReconstructNode();
		return Node;
	}

	UEdGraphNode* FindSinkNode(UEdGraph* Graph)
	{
		if (!Graph)
		{
			return nullptr;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && FindFirstPin(Node, EGPD_Input) && !FindFirstPin(Node, EGPD_Output))
			{
				return Node;
			}
		}

		return Graph->Nodes.Num() > 0 ? Graph->Nodes[0] : nullptr;
	}

	void RenameGraphForState(UEdGraph* Graph, const FString& DesiredName)
	{
		if (Graph && !DesiredName.IsEmpty())
		{
			Graph->Rename(*DesiredName, nullptr, REN_DontCreateRedirectors | REN_NonTransactional);
		}
	}

	UEdGraphNode* FindStateByGraphName(UEdGraph* StateMachineGraph, UClass* StateClass, const FString& StateName)
	{
		if (!StateMachineGraph || !StateClass)
		{
			return nullptr;
		}

		for (UEdGraphNode* Node : StateMachineGraph->Nodes)
		{
			if (!Node || !Node->IsA(StateClass))
			{
				continue;
			}

			if (UEdGraph* BoundGraph = Cast<UEdGraph>(GetObjectPropertyValue(Node, TEXT("BoundGraph"))))
			{
				if (BoundGraph->GetName() == StateName)
				{
					return Node;
				}
			}
		}

		return nullptr;
	}

	void AddPlaySequenceToState(UEdGraphNode* StateNode, UClass* PlaySequenceClass, UObject* AnimSequence)
	{
		UEdGraph* BoundGraph = Cast<UEdGraph>(GetObjectPropertyValue(StateNode, TEXT("BoundGraph")));
		if (!BoundGraph || !PlaySequenceClass || !AnimSequence)
		{
			return;
		}

		if (FindFirstNodeOfClass(BoundGraph, PlaySequenceClass))
		{
			return;
		}

		UEdGraphNode* PlaySequenceNode = CreateGraphNode(BoundGraph, PlaySequenceClass, FVector2D(0.0f, 0.0f));
		SetPlaySequenceNodeData(PlaySequenceNode, AnimSequence);
		ConnectFirstPins(PlaySequenceNode, FindSinkNode(BoundGraph));
	}

	UObject* FindOrCreateAnimBP(FAssetToolsModule& AssetToolsModule, FAssetRegistryModule& AssetRegistryModule, UClass* AnimBPClass, UClass* AnimBPGeneratedClass, UClass* AnimInstanceClass, const FString& PackagePath, const FString& DesiredAssetName, UObject* AnimSource)
	{
		if (!AnimBPClass || !AnimBPGeneratedClass || !AnimInstanceClass || !AnimSource)
		{
			return nullptr;
		}

		const FString TentativePackageName = UPackageTools::SanitizePackageName(PackagePath / DesiredAssetName);
		if (UObject* ExistingAsset = FindAsset(AssetRegistryModule, TentativePackageName, DesiredAssetName))
		{
			return ExistingAsset->IsA(AnimBPClass) ? ExistingAsset : nullptr;
		}

		FString DefaultSuffix;
		FString AssetName;
		FString PackageName;
		AssetToolsModule.Get().CreateUniqueAssetName(TentativePackageName, DefaultSuffix, PackageName, AssetName);

		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			return nullptr;
		}

		UObject* Blueprint = FKismetEditorUtilities::CreateBlueprint(AnimInstanceClass, Package, *AssetName, BPTYPE_Normal, AnimBPClass, AnimBPGeneratedClass);
		if (Blueprint)
		{
			SetObjectProperty(Blueprint, TEXT("SupportedAnimationSource"), AnimSource);
			FAssetRegistryModule::AssetCreated(Blueprint);
			Package->MarkPackageDirty();
		}

		return Blueprint;
	}

	void CreateAnimBPDraft(FAssetToolsModule& AssetToolsModule, FAssetRegistryModule& AssetRegistryModule, UAseprite* SpriteSheet, UObject* AnimSource, const TArray<UObject*>& AnimSequences)
	{
		UClass* AnimBPClass = LoadPaperZDClass(PaperZDAnimBPClassPath);
		UClass* AnimBPGeneratedClass = LoadPaperZDClass(PaperZDAnimBPGeneratedClassPath);
		UClass* AnimInstanceClass = LoadPaperZDClass(PaperZDAnimInstanceClassPath);
		UClass* StateMachineClass = LoadPaperZDClass(PaperZDAnimGraphNodeStateMachineClassPath);
		UClass* StateClass = LoadPaperZDClass(PaperZDStateGraphNodeStateClassPath);
		UClass* PlaySequenceClass = LoadPaperZDClass(PaperZDAnimGraphNodePlaySequenceClassPath);
		if (!AnimBPClass || !AnimBPGeneratedClass || !AnimInstanceClass || !StateMachineClass || !StateClass || !PlaySequenceClass)
		{
			return;
		}

		const FString PackagePath = FPackageName::GetLongPackagePath(SpriteSheet->GetOutermost()->GetPathName());
		const FString AnimBPName = SpriteSheet->GetName() + TEXT("_AnimBP");
		UObject* AnimBPObject = FindOrCreateAnimBP(AssetToolsModule, AssetRegistryModule, AnimBPClass, AnimBPGeneratedClass, AnimInstanceClass, PackagePath, AnimBPName, AnimSource);
		UBlueprint* AnimBP = Cast<UBlueprint>(AnimBPObject);
		if (!AnimBP)
		{
			return;
		}

		UEdGraph* AnimationGraph = Cast<UEdGraph>(GetObjectPropertyValue(AnimBP, TEXT("AnimationGraph")));
		if (!AnimationGraph)
		{
			return;
		}

		UEdGraphNode* StateMachineNode = FindFirstNodeOfClass(AnimationGraph, StateMachineClass);
		if (!StateMachineNode)
		{
			StateMachineNode = CreateGraphNode(AnimationGraph, StateMachineClass, FVector2D(200.0f, 0.0f));
			ConnectFirstPins(StateMachineNode, FindSinkNode(AnimationGraph));
		}

		UEdGraph* StateMachineGraph = Cast<UEdGraph>(GetObjectPropertyValue(StateMachineNode, TEXT("StateMachineGraph")));
		if (!StateMachineGraph)
		{
			return;
		}

		int32 StateIndex = 0;
		for (UObject* AnimSequence : AnimSequences)
		{
			if (!AnimSequence)
			{
				continue;
			}

			FString StateName = AnimSequence->GetName();
			StateName.RemoveFromStart(TEXT("Seq_"));
			UEdGraphNode* StateNode = FindStateByGraphName(StateMachineGraph, StateClass, StateName);
			if (!StateNode)
			{
				const int32 Column = StateIndex % 4;
				const int32 Row = StateIndex / 4;
				StateNode = CreateGraphNode(StateMachineGraph, StateClass, FVector2D(260.0f * Column, 160.0f * Row));
				RenameGraphForState(Cast<UEdGraph>(GetObjectPropertyValue(StateNode, TEXT("BoundGraph"))), StateName);
			}

			AddPlaySequenceToState(StateNode, PlaySequenceClass, AnimSequence);
			++StateIndex;
		}

		AnimBP->Modify();
		AnimBP->MarkPackageDirty();
		FKismetEditorUtilities::CompileBlueprint(AnimBP);
	}

	void SetFlipbookAnimData(UObject* AnimSequence, UPaperFlipbook* Flipbook)
	{
		if (!AnimSequence || !Flipbook)
		{
			return;
		}

		FArrayProperty* AnimDataProperty = FindFProperty<FArrayProperty>(AnimSequence->GetClass(), TEXT("AnimData"));
		if (!AnimDataProperty)
		{
			return;
		}

		FStructProperty* InnerProperty = CastField<FStructProperty>(AnimDataProperty->Inner);
		if (!InnerProperty)
		{
			return;
		}

		FObjectPropertyBase* AnimationProperty = FindFProperty<FObjectPropertyBase>(InnerProperty->Struct, TEXT("Animation"));
		if (!AnimationProperty)
		{
			return;
		}

		AnimSequence->Modify();

		FScriptArrayHelper AnimDataHelper(AnimDataProperty, AnimDataProperty->ContainerPtrToValuePtr<void>(AnimSequence));
		if (AnimDataHelper.Num() == 0)
		{
			AnimDataHelper.AddValue();
		}

		uint8* EntryData = AnimDataHelper.GetRawPtr(0);
		AnimationProperty->SetObjectPropertyValue(AnimationProperty->ContainerPtrToValuePtr<void>(EntryData), Flipbook);
	}
}

void FAsepritePaperZDImporter::ProcessPaperZDAssets(UAseprite* SpriteSheet, bool bCreateAnimBPDraft)
{
	if (!SpriteSheet || SpriteSheet->Flipbooks.Num() == 0)
	{
		return;
	}

	UClass* AnimSourceClass = LoadPaperZDClass(PaperZDAnimationSourceFlipbookClassPath);
	UClass* AnimSequenceClass = LoadPaperZDClass(PaperZDAnimSequenceFlipbookClassPath);
	if (!AnimSourceClass || !AnimSequenceClass)
	{
		return;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

	const FString PackagePath = FPackageName::GetLongPackagePath(SpriteSheet->GetOutermost()->GetPathName());
	const FString SourceName = SpriteSheet->GetName() + TEXT("_AnimSource");
	UObject* AnimSource = FindOrCreateAsset(AssetToolsModule, AssetRegistryModule, AnimSourceClass, PackagePath, SourceName);
	if (!AnimSource)
	{
		return;
	}

	TArray<UObject*> AnimSequences;
	for (const TSoftObjectPtr<UPaperFlipbook>& FlipbookPtr : SpriteSheet->Flipbooks)
	{
		UPaperFlipbook* Flipbook = FlipbookPtr.LoadSynchronous();
		if (!Flipbook)
		{
			continue;
		}

		const FString SequenceName = TEXT("Seq_") + Flipbook->GetName();
		const FString FlipbookPackagePath = FPackageName::GetLongPackagePath(Flipbook->GetOutermost()->GetPathName());
		const FString SequencePackagePath = FlipbookPackagePath.Replace(TEXT("/Flipbooks"), TEXT("/Sequences"));
		UObject* AnimSequence = FindOrCreateAsset(AssetToolsModule, AssetRegistryModule, AnimSequenceClass, SequencePackagePath, SequenceName);
		if (AnimSequence)
		{
			SetObjectProperty(AnimSequence, TEXT("AnimSource"), AnimSource);
			SetFlipbookAnimData(AnimSequence, Flipbook);
			AnimSequence->MarkPackageDirty();
			AnimSequence->PostEditChange();
			AnimSequences.Add(AnimSequence);
		}
	}

	if (bCreateAnimBPDraft)
	{
		CreateAnimBPDraft(AssetToolsModule, AssetRegistryModule, SpriteSheet, AnimSource, AnimSequences);
	}

	AnimSource->MarkPackageDirty();
	AnimSource->PostEditChange();
}
