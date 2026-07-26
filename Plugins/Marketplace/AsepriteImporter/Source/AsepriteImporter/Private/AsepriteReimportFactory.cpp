// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsepriteReimportFactory.h"
#include "Engine/Texture2D.h"
#include "AsepriteImporterLog.h"
#include "HAL/FileManager.h"
#include "EditorFramework/AssetImportData.h"
#include "Aseprite.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsepriteReimportFactory)

#define LOCTEXT_NAMESPACE "PaperJsonImporter"

//////////////////////////////////////////////////////////////////////////
// UAsepriteReimportFactory

UAsepriteReimportFactory::UAsepriteReimportFactory(const FObjectInitializer &ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UAseprite::StaticClass();

	bCreateNew = false;
}

bool UAsepriteReimportFactory::CanReimport(UObject *Obj, TArray<FString> &OutFilenames)
{
	UAseprite *SpriteSheet = Cast<UAseprite>(Obj);
	if (SpriteSheet && SpriteSheet->AssetImportData)
	{
		SpriteSheet->AssetImportData->ExtractFilenames(OutFilenames);
		return true;
	}
	return false;
}

void UAsepriteReimportFactory::SetReimportPaths(UObject *Obj, const TArray<FString> &NewReimportPaths)
{
	UAseprite *SpriteSheet = Cast<UAseprite>(Obj);
	if (SpriteSheet && ensure(NewReimportPaths.Num() == 1))
	{
		SpriteSheet->AssetImportData->UpdateFilenameOnly(NewReimportPaths[0]);
	}
}

EReimportResult::Type UAsepriteReimportFactory::Reimport(UObject *Obj)
{
	UAseprite *SpriteSheet = Cast<UAseprite>(Obj);
	if (!SpriteSheet)
	{
		return EReimportResult::Failed;
	}

	// Make sure file is valid and exists
	const FString Filename = SpriteSheet->AssetImportData->GetFirstFilename();
	if (!Filename.Len() || IFileManager::Get().FileSize(*Filename) == INDEX_NONE)
	{
		return EReimportResult::Failed;
	}

	// Configure the importer with the reimport settings
	Importer.SetReimportData(SpriteSheet->SpriteNames, SpriteSheet->Sprites);
	// Reimport에서는 파라미터를 재사용한다
	Importer.ExistingImportSettings.bCreateFlipbooks = SpriteSheet->bCreateFlipbooks;
	Importer.ExistingImportSettings.ImportBehaviorVersion = SpriteSheet->ImportBehaviorVersion;
	Importer.ExistingImportSettings.FlipbookFramePerSecond = SpriteSheet->FlipbookFramePerSecond;
	Importer.ExistingImportSettings.bSplitLayers = SpriteSheet->bSplitLayers;
	Importer.ExistingImportSettings.bSkipEmptyTagFramesWhenCreateFlipbooks = SpriteSheet->bSkipEmptyTagFramesWhenCreateFlipbooks;
	Importer.ExistingImportSettings.bCreatePaperZDAssets = SpriteSheet->bCreatePaperZDAssets;
	Importer.ExistingImportSettings.bCreatePaperZDAnimBPDraft = SpriteSheet->bCreatePaperZDAnimBPDraft;
	Importer.ExistingImportSettings.FlipbookLayerSeparator = SpriteSheet->ImportBehaviorVersion >= AsepriteImportBehaviorVersion::CustomFlipbookNaming ? SpriteSheet->FlipbookLayerSeparator : TEXT("_L_");
	Importer.ExistingImportSettings.FlipbookTagSeparator = SpriteSheet->ImportBehaviorVersion >= AsepriteImportBehaviorVersion::CustomFlipbookNaming ? SpriteSheet->FlipbookTagSeparator : TEXT("_T_");
	// 2025.01.07 - Fix for reimport losing PPU and Pivot settings
	Importer.ExistingImportSettings.PixelPerUnit = SpriteSheet->PixelPerUnit;
	Importer.ExistingImportSettings.PivotType = SpriteSheet->PivotType;
	//
	Importer.ExistingBaseTextureName = SpriteSheet->TextureName;
	Importer.ExistingBaseTexture = SpriteSheet->Texture;
	Importer.ExistingNormalMapTextureName = SpriteSheet->NormalMapTextureName;
	Importer.ExistingNormalMapTexture = SpriteSheet->NormalMapTexture;
	Importer.SetReimportFlipbookData(SpriteSheet->SpriteNames, SpriteSheet->Flipbooks); // Aseprite only

	// Run the import again
	EReimportResult::Type Result = EReimportResult::Failed;
	bool OutCanceled = false;

	if (ImportObject(SpriteSheet->GetClass(), SpriteSheet->GetOuter(), *SpriteSheet->GetName(), RF_Public | RF_Standalone, Filename, nullptr, OutCanceled) != nullptr)
	{
		UE_LOG(LogAsepriteImporter, Log, TEXT("Imported successfully"));

		SpriteSheet->AssetImportData->Update(Filename);

		// Try to find the outer package so we can dirty it up
		if (SpriteSheet->GetOuter())
		{
			SpriteSheet->GetOuter()->MarkPackageDirty();
		}
		else
		{
			SpriteSheet->MarkPackageDirty();
		}
		Result = EReimportResult::Succeeded;
	}
	else
	{
		if (OutCanceled)
		{
			UE_LOG(LogAsepriteImporter, Warning, TEXT("-- import canceled"));
		}
		else
		{
			UE_LOG(LogAsepriteImporter, Warning, TEXT("-- import failed"));
		}

		Result = EReimportResult::Failed;
	}

	return Result;
}

int32 UAsepriteReimportFactory::GetPriority() const
{
	return ImportPriority;
}
//////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE

