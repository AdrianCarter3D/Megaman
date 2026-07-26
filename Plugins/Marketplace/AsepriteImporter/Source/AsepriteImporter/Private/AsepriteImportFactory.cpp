// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsepriteImportFactory.h"
#include "AsepriteImporterLog.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "EditorFramework/AssetImportData.h"
#include "Editor.h"
#include "AssetToolsModule.h"
#include "Aseprite.h"
#include "Subsystems/ImportSubsystem.h"

#include "AsepriteImporter.h"		  // ASEPRTIE ONLY
#include "IContentBrowserSingleton.h" // ASEPRTIE ONLY
#include "ContentBrowserModule.h"	  // ASEPRTIE ONLY
#include "PaperFlipbookFactory.h"	  // ASEPRTIE ONLY
#include "PaperFlipbookHelpers.h"	  // ASEPRTIE ONLY
#include "PaperFlipbook.h"			  // ASEPRTIE ONLY
#include "AsepritePaperZDImporter.h"  // ASEPRTIE ONLY

#include "SlateFwd.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "SAsepriteImportDialog.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsepriteImportFactory)

//////////////////////////////////////////////////////////////////////////
// UAsepriteImportFactory

UAsepriteImportFactory::UAsepriteImportFactory(const FObjectInitializer &ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = false;
	// bEditAfterNew = true;
	SupportedClass = UAseprite::StaticClass();

	bEditorImport = true;
	bText = true;

	Formats.Add(TEXT("aseprite;Aseprite File"));
	Formats.Add(TEXT("ase;Aseprite File"));

	// Formats.Add(TEXT("json;Spritesheet JSON file"));
	// Formats.Add(TEXT("paper2dsprites;Spritesheet JSON file"));
}

FText UAsepriteImportFactory::GetToolTip() const
{
	return NSLOCTEXT("Aseprite", "AsepriteFactoryDescription", "Aseprite sprite sheet importer");
}

bool UAsepriteImportFactory::FactoryCanImport(const FString &Filename)
{
	FString FileContent;
	if (FFileHelper::LoadFileToString(/*out*/ FileContent, *Filename))
	{
		return FAsepriteJsonImporter::CanImportJSON(FileContent);
	}

	return false;
}

UObject *UAsepriteImportFactory::FactoryCreateText(UClass *InClass, UObject *InParent, FName InName, EObjectFlags Flags, UObject *Context, const TCHAR *Type, const TCHAR *&Buffer, const TCHAR *BufferEnd, FFeedbackContext *Warn, bool &bOutOperationCanceled)
{
	Flags |= RF_Transactional;

	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPreImport(this, InClass, InParent, InName, Type);

	bool bClickOK = true;

	UAsepriteImporterSettings *Settings = GetMutableDefault<UAsepriteImporterSettings>();
	// Reimport일때는 다이얼로그를 띄우지 않는다
	if (Importer.bIsReimporting == false && Settings->bShowImportDialog)
	{
		TSharedRef<SAsepriteImportDialog> Dialog = SNew(SAsepriteImportDialog);
		bClickOK = Dialog->Show();
	}

	if (bClickOK == false)
	{
		// import dialog에서 취소한 경우에 대한 처리
		// 2024.03.05
		bOutOperationCanceled = true;
		GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, nullptr);
		return nullptr;
	}

	FString AsepriteFilePath = UFactory::GetCurrentFilename();

	FString JsonPath = Importer.ExtractSpriteSheet(AsepriteFilePath);
	if (JsonPath.IsEmpty())
	{
		GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, nullptr);
		return nullptr;
	}

	FAssetToolsModule &AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");

	const FString FactoryJSONFilename = JsonPath;
	FString CurrentSourcePath;
	FString FilenameNoExtension;
	FString UnusedExtension;
	FPaths::Split(FactoryJSONFilename, CurrentSourcePath, FilenameNoExtension, UnusedExtension);

	const FString LongPackagePath = FPackageName::GetLongPackagePath(InParent->GetOutermost()->GetPathName());
	UAseprite *Result = nullptr;

	const FString NameForErrors(InName.ToString());
	// UE_LOG(LogAsepriteImporter, Warning, TEXT("Importing sprite sheet from JSON file %s"), *FileContent);
	FString FileContent;
	FFileHelper::LoadFileToString(/*out*/ FileContent, *JsonPath);
	if (Importer.ImportFromString(FileContent, NameForErrors, /*bSilent=*/false) &&
		Importer.ImportTextures(LongPackagePath, CurrentSourcePath))
	{
		UAseprite *SpriteSheet = NewObject<UAseprite>(InParent, InName, Flags);
		if (Importer.PerformImport(LongPackagePath, Flags, SpriteSheet))
		{
			SpriteSheet->AssetImportData->Update(AsepriteFilePath);

			Result = SpriteSheet;

			if (SpriteSheet->Sprites.Num() > 0)
			{
				bool bCreateFlipbooks = Importer.bIsReimporting ? Importer.ExistingImportSettings.bCreateFlipbooks : Settings->bCreateFlipbooks;
				if (bCreateFlipbooks)
				{
					Importer.ExecuteCreateFlipbooks(SpriteSheet); // Aseprite only
					const bool bCreatePaperZDAssets = Importer.bIsReimporting ? Importer.ExistingImportSettings.bCreatePaperZDAssets : Settings->bCreatePaperZDAssets;
					if (bCreatePaperZDAssets)
					{
						const bool bCreateAnimBPDraft = Importer.bIsReimporting ? Importer.ExistingImportSettings.bCreatePaperZDAnimBPDraft : Settings->bCreatePaperZDAnimBPDraft;
						FAsepritePaperZDImporter::ProcessPaperZDAssets(SpriteSheet, bCreateAnimBPDraft); // Aseprite only
					}
				}
			}
		}
		// 나중에 Reimport등을 위해 저장해둔다
		if (Importer.bIsReimporting)
		{
			SpriteSheet->ImportBehaviorVersion = Importer.ExistingImportSettings.ImportBehaviorVersion;
			SpriteSheet->bCreateFlipbooks = Importer.ExistingImportSettings.bCreateFlipbooks;
			SpriteSheet->FlipbookFramePerSecond = Importer.ExistingImportSettings.FlipbookFramePerSecond;
			SpriteSheet->bSplitLayers = Importer.ExistingImportSettings.bSplitLayers;
			SpriteSheet->bSkipEmptyTagFramesWhenCreateFlipbooks = Importer.ExistingImportSettings.bSkipEmptyTagFramesWhenCreateFlipbooks;
			SpriteSheet->bCreatePaperZDAssets = Importer.ExistingImportSettings.bCreatePaperZDAssets;
			SpriteSheet->bCreatePaperZDAnimBPDraft = Importer.ExistingImportSettings.bCreatePaperZDAnimBPDraft;
			SpriteSheet->FlipbookLayerSeparator = Importer.ExistingImportSettings.FlipbookLayerSeparator;
			SpriteSheet->FlipbookTagSeparator = Importer.ExistingImportSettings.FlipbookTagSeparator;
			SpriteSheet->PixelPerUnit = Importer.ExistingImportSettings.PixelPerUnit;
			SpriteSheet->PivotType = Importer.ExistingImportSettings.PivotType;
		}
		else
		{
			SpriteSheet->ImportBehaviorVersion = AsepriteImportBehaviorVersion::Current;
			SpriteSheet->bCreateFlipbooks = Settings->bCreateFlipbooks;
			SpriteSheet->FlipbookFramePerSecond = Settings->FlipbookFramePerSecond;
			SpriteSheet->bSplitLayers = Settings->bSplitLayers;
			SpriteSheet->bSkipEmptyTagFramesWhenCreateFlipbooks = Settings->bSkipEmptyTagFramesWhenCreateFlipbooks;
			SpriteSheet->bCreatePaperZDAssets = Settings->bCreatePaperZDAssets;
			SpriteSheet->bCreatePaperZDAnimBPDraft = Settings->bCreatePaperZDAnimBPDraft;
			SpriteSheet->FlipbookLayerSeparator = Settings->FlipbookLayerSeparator;
			SpriteSheet->FlipbookTagSeparator = Settings->FlipbookTagSeparator;
			SpriteSheet->PixelPerUnit = Settings->PixelPerUnit;
			SpriteSheet->PivotType = Settings->PivotType;
		}
	}

	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, Result);

	// Reset the importer to ensure that no leftover data can contaminate future imports.
	Importer = FAsepriteJsonImporter();

	return Result;
}
