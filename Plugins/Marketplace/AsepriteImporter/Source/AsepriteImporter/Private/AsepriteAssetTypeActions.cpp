// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsepriteAssetTypeActions.h"
#include "Misc/PackageName.h"
#include "Misc/FeedbackContext.h"
#include "EditorFramework/AssetImportData.h"
#include "PaperSprite.h"
#include "Aseprite.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "PaperFlipbookFactory.h"
#include "PackageTools.h"
#include "PaperFlipbookHelpers.h"
#include "PaperFlipbook.h"
#include "ToolMenuSection.h"
#include "AsepriteJsonImporter.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

//////////////////////////////////////////////////////////////////////////
// FAsepriteImportAssetTypeActions

FText FAsepriteAssetTypeActions::GetName() const
{
	return LOCTEXT("FAsepriteAssetTypeActionsName", "Aseprite");
}

FColor FAsepriteAssetTypeActions::GetTypeColor() const
{
	return FColor::Cyan;
}

UClass *FAsepriteAssetTypeActions::GetSupportedClass() const
{
	return UAseprite::StaticClass();
}

uint32 FAsepriteAssetTypeActions::GetCategories()
{
	return EAssetTypeCategories::Misc;
}

bool FAsepriteAssetTypeActions::IsImportedAsset() const
{
	return true;
}

void FAsepriteAssetTypeActions::GetResolvedSourceFilePaths(const TArray<UObject *> &TypeAssets, TArray<FString> &OutSourceFilePaths) const
{
	for (auto &Asset : TypeAssets)
	{
		const auto SpriteSheet = CastChecked<UAseprite>(Asset);
		if (SpriteSheet->AssetImportData)
		{
			SpriteSheet->AssetImportData->ExtractFilenames(OutSourceFilePaths);
		}
	}
}

void FAsepriteAssetTypeActions::GetActions(const TArray<UObject *> &InObjects, FToolMenuSection &Section)
{
	auto SpriteSheetImports = GetTypedWeakObjectPtrs<UAseprite>(InObjects);

	Section.AddMenuEntry(
		"SpriteSheet_CreateFlipbooks",
		LOCTEXT("SpriteSheet_CreateFlipbooks", "Create Flipbooks"),
		LOCTEXT("SpriteSheet_CreateFlipbooksTooltip", "Creates flipbooks from sprites in this sprite sheet."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.PaperFlipbook"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FAsepriteAssetTypeActions::ExecuteCreateFlipbooks, SpriteSheetImports),
			FCanExecuteAction()));

	Section.AddMenuEntry(
		"SpriteSheet_Materials",
		LOCTEXT("SpriteSheet_Materials", "Create Flipbook Materials"),
		LOCTEXT("SpriteSheet_MaterialsTooltip", "Creates materials from sprites in this sprite sheet."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.PaperFlipbook"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FAsepriteAssetTypeActions::ExecuteCreateSpriteAnimationMaterial, SpriteSheetImports),
			FCanExecuteAction()));

	Section.AddMenuEntry(
		"SpriteSheet_Emitters",
		LOCTEXT("SpriteSheet_Materials", "Create Flipbook Emitters"),
		LOCTEXT("SpriteSheet_MaterialsTooltip", "Creates materials from Aseprite."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.PaperFlipbook"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FAsepriteAssetTypeActions::ExecuteCreateNiagaraSpriteRenderer, SpriteSheetImports),
			FCanExecuteAction()));

	Section.AddMenuEntry(
		"SpriteSheet_UpdateMaterials",
		LOCTEXT("SpriteSheet_UpdateMaterials", "Change Materials"),
		LOCTEXT("SpriteSheet_UpdateMaterialsTooltip", "Changes materials for all sprites and flipbooks created from this Aseprite asset."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.MaterialInterface"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FAsepriteAssetTypeActions::ExecuteUpdateFlipbookMaterials, SpriteSheetImports),
			FCanExecuteAction()));
}

//////////////////////////////////////////////////////////////////////////

void FAsepriteAssetTypeActions::ExecuteCreateFlipbooks(TArray<TWeakObjectPtr<UAseprite>> Objects)
{
	for (int SpriteSheetIndex = 0; SpriteSheetIndex < Objects.Num(); ++SpriteSheetIndex)
	{
		UAseprite *SpriteSheet = Objects[SpriteSheetIndex].Get();
		if (SpriteSheet != nullptr)
		{
			FAssetToolsModule &AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");
			FContentBrowserModule &ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

			check(SpriteSheet->SpriteNames.Num() == SpriteSheet->Sprites.Num());
			bool useSpriteNames = (SpriteSheet->SpriteNames.Num() == SpriteSheet->Sprites.Num());

			// Create a list of sprites and sprite names to feed into paper flipbook helpers
			TMap<FString, TArray<UPaperSprite *>> SpriteFlipbookMap;
			{
				TArray<UPaperSprite *> Sprites;
				TArray<FString> SpriteNames;

				for (int SpriteIndex = 0; SpriteIndex < SpriteSheet->Sprites.Num(); ++SpriteIndex)
				{
					TSoftObjectPtr<class UPaperSprite> SpriteSoftPtr = SpriteSheet->Sprites[SpriteIndex];
					UPaperSprite *Sprite = SpriteSoftPtr.LoadSynchronous();
					if (Sprite != nullptr)
					{
						const FString SpriteName = useSpriteNames ? SpriteSheet->SpriteNames[SpriteIndex] : Sprite->GetName();
						Sprites.Add(Sprite);
						SpriteNames.Add(SpriteName);
					}
				}
				FString BaseName = FPaths::GetBaseFilename(SpriteSheet->AssetImportData->GetFirstFilename());
				FAsepriteJsonImporter::ExtractFlipbooksFromSprites(SpriteFlipbookMap, BaseName, Sprites, SpriteNames, SpriteSheet->TagNames, SpriteSheet->LayerNames);
			}

			// Create one flipbook for every grouped flipbook name
			if (SpriteFlipbookMap.Num() > 0)
			{
				UPaperFlipbookFactory *FlipbookFactory = NewObject<UPaperFlipbookFactory>();

				GWarn->BeginSlowTask(NSLOCTEXT("Paper2D", "Paper2D_CreateFlipbooks", "Creating flipbooks from selection"), true, true);

				int Progress = 0;
				int TotalProgress = SpriteFlipbookMap.Num();
				TArray<UObject *> ObjectsToSync;
				for (auto It : SpriteFlipbookMap)
				{
					GWarn->UpdateProgress(Progress++, TotalProgress);

					const FString &FlipbookName = It.Key;
					auto FlipbookFirstTagIndexPtr = SpriteSheet->TagNames.FindKey(FlipbookName);
					FString SubFolderPath = TEXT("");

					if (FlipbookFirstTagIndexPtr != nullptr)
					{
						const int32 FlipbookFirstTagIndex = *FlipbookFirstTagIndexPtr;
						const FString LayerName = SpriteSheet->LayerNames[FlipbookFirstTagIndex];
						if (SpriteSheet->LayerToGroupMap.Contains(LayerName))
						{
							const FString GroupName = SpriteSheet->LayerToGroupMap[LayerName];
							if (GroupName.IsEmpty() == false)
							{
								SubFolderPath = GroupName.IsEmpty() ? TEXT("") : GroupName + TEXT("/");
							}
						}
					}

					TArray<UPaperSprite *> &Sprites = It.Value;
					
					// Calculate base FPS from this animation's first frame (same logic as AsepriteJsonImporter)
					float BaseFPS = 15.0f; // Default fallback FPS
					if (SpriteSheet->SpriteFrameDurations.Num() > 0 && Sprites.Num() > 0)
					{
						// Find the frame index of the first sprite in this animation
						int32 FirstSpriteFrameIndex = 0;
						UPaperSprite* FirstSprite = Sprites[0];
						
						// Find the frame index that corresponds to this sprite
						for (int32 FrameIdx = 0; FrameIdx < SpriteSheet->Sprites.Num(); ++FrameIdx)
						{
							if (SpriteSheet->Sprites[FrameIdx].Get() == FirstSprite)
							{
								FirstSpriteFrameIndex = FrameIdx;
								break;
							}
						}
						
						// Use this animation's first frame duration as base for calculating FPS
						float FirstFrameDurationMs = SpriteSheet->SpriteFrameDurations[FirstSpriteFrameIndex];
						BaseFPS = 1000.0f / FirstFrameDurationMs;
					}
					
					const FString PackagePath = FPackageName::GetLongPackagePath(SpriteSheet->GetOutermost()->GetPathName()) + TEXT("/Flipbooks/") + SubFolderPath;
					const FString TentativePackagePath = UPackageTools::SanitizePackageName(PackagePath + TEXT("/") + FlipbookName);
					FString DefaultSuffix;
					FString AssetName;
					FString PackageName;
					AssetToolsModule.Get().CreateUniqueAssetName(TentativePackagePath, DefaultSuffix, /*out*/ PackageName, /*out*/ AssetName);

					FlipbookFactory->KeyFrames.Empty();
					for (int32 SpriteIndex = 0; SpriteIndex < Sprites.Num(); ++SpriteIndex)
					{
						FPaperFlipbookKeyFrame *KeyFrame = new (FlipbookFactory->KeyFrames) FPaperFlipbookKeyFrame();
						KeyFrame->Sprite = Sprites[SpriteIndex];
						
						// Find the frame index that corresponds to this sprite
						int32 CurrentSpriteFrameIndex = SpriteIndex; // Default fallback
						for (int32 FrameIdx = 0; FrameIdx < SpriteSheet->Sprites.Num(); ++FrameIdx)
						{
							if (SpriteSheet->Sprites[FrameIdx].Get() == Sprites[SpriteIndex])
							{
								CurrentSpriteFrameIndex = FrameIdx;
								break;
							}
						}
						
						// Use Aseprite's actual frame durations relative to base FPS (same logic as AsepriteJsonImporter)
						float BaseFrameDuration = 1000.0f / BaseFPS;
						float CurrentFrameDuration = SpriteSheet->SpriteFrameDurations[CurrentSpriteFrameIndex];
						KeyFrame->FrameRun = FMath::Max(1, FMath::RoundToInt(CurrentFrameDuration / BaseFrameDuration));
					}

					if (UObject *NewAsset = AssetToolsModule.Get().CreateAsset(AssetName, PackagePath, UPaperFlipbook::StaticClass(), FlipbookFactory))
					{
						UPaperFlipbook *Flipbook = Cast<UPaperFlipbook>(NewAsset);
						
						// Set flipbook FPS using reflection (same as AsepriteJsonImporter)
						if (FProperty* FramesPerSecondProperty = FindFProperty<FProperty>(UPaperFlipbook::StaticClass(), TEXT("FramesPerSecond")))
						{
							FramesPerSecondProperty->SetValue_InContainer(Flipbook, &BaseFPS);
						}
						
						ObjectsToSync.Add(Flipbook);
					}

					if (GWarn->ReceivedUserCancel())
					{
						break;
					}
				}

				GWarn->EndSlowTask();

				if (ObjectsToSync.Num() > 0)
				{
					ContentBrowserModule.Get().SyncBrowserToAssets(ObjectsToSync);
				}
			}
		}
	}
}

void FAsepriteAssetTypeActions::ExecuteCreateSpriteAnimationMaterial(TArray<TWeakObjectPtr<UAseprite>> Objects)
{
	FContentBrowserModule &ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	for (int SpriteSheetIndex = 0; SpriteSheetIndex < Objects.Num(); ++SpriteSheetIndex)
	{
		UAseprite *SpriteSheet = Objects[SpriteSheetIndex].Get();
		if (SpriteSheet != nullptr)
		{

			TArray<UObject *> ObjectsToSync;
			FAsepriteJsonImporter::CreateSpriteAnimationMaterial(ObjectsToSync, SpriteSheet);
			if (ObjectsToSync.Num() > 0)
			{
				ContentBrowserModule.Get().SyncBrowserToAssets(ObjectsToSync);
			}
		}
	}
}

void FAsepriteAssetTypeActions::ExecuteCreateNiagaraSpriteRenderer(TArray<TWeakObjectPtr<UAseprite>> Objects)
{
	FContentBrowserModule &ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	for (int SpriteSheetIndex = 0; SpriteSheetIndex < Objects.Num(); ++SpriteSheetIndex)
	{
		UAseprite *SpriteSheet = Objects[SpriteSheetIndex].Get();
		if (SpriteSheet != nullptr)
		{
			TArray<UObject *> ObjectsToSync;
			FAsepriteJsonImporter::CreateNiagaraSpriteEmitter(ObjectsToSync, SpriteSheet);

			if (ObjectsToSync.Num() > 0)
			{
				ContentBrowserModule.Get().SyncBrowserToAssets(ObjectsToSync);
			}
		}
	}
}

void FAsepriteAssetTypeActions::ExecuteUpdateFlipbookMaterials(TArray<TWeakObjectPtr<UAseprite>> Objects)
{
	// 2025.01.07 - Bulk update materials for all flipbooks created from Aseprite assets
	for (int SpriteSheetIndex = 0; SpriteSheetIndex < Objects.Num(); ++SpriteSheetIndex)
	{
		UAseprite* SpriteSheet = Objects[SpriteSheetIndex].Get();
		if (SpriteSheet != nullptr)
		{
			FAsepriteJsonImporter::UpdateFlipbookMaterials(SpriteSheet);
		}
	}
}

#undef LOCTEXT_NAMESPACE
