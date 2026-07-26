// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/SoftObjectPtr.h"

//5.4fix
#include "Aseprite.h"

class FJsonObject;
class UPaperSprite;
class UAseprite;
class UTexture2D;
class UPaperFlipbook; // Aseprite only

//////////////////////////////////////////////////////////////////////////
// FSpriteFrame

// Represents one parsed frame in a sprite sheet
struct FSpriteFrame
{
	FName FrameName;

	FIntPoint SpritePosInSheet;
	FIntPoint SpriteSizeInSheet;

	FIntPoint SpriteSourcePos;
	FIntPoint SpriteSourceSize;

	FVector2D ImageSourceSize;

	FVector2D Pivot;

	bool bTrimmed;
	bool bRotated;

	int32 Duration; // ASEPRITE ONLY
	FString TagName; // ASEPRITE ONLY
	FString LayerName; // ASEPRITE ONLY
	FString LayerGroupName; // ASEPRITE ONLY
};

struct FFrameTag
{
	//  { "name": "Closed", "from": 0, "to": 0, "direction": "forward", "color": "#000000ff" }
	FString Name;
	int32 From;
	int32 To;
	int32 Direction;
	FString Color;
};

struct FLayer
{
	// { "name": "back-arm", "group": "Hero", "opacity": 255, "blendMode": "normal" },
	FString Name;
	FString Group;
	int32 Opacity; // TODO
	FString BlendMode; // TODO
};

// TODO - Aseprite only - add support for slices

//////////////////////////////////////////////////////////////////////////
// FJsonAsepriteImporter

// Parses a json from FileContents and imports / reimports a spritesheet
class FAsepriteJsonImporter
{
public:
	FAsepriteJsonImporter();

	static bool CanImportJSON(const FString &FileContents);

	bool ImportFromString(const FString &FileContents, const FString &NameForErrors, bool bSilent);
	bool ImportFromArchive(FArchive *Archive, const FString &NameForErrors, bool bSilent);

	bool ImportTextures(const FString &LongPackagePath, const FString &SourcePath);

	bool PerformImport(const FString &LongPackagePath, EObjectFlags Flags, UAseprite *SpriteSheet);

	void SetReimportData(const TArray<FString> &ExistingSpriteNames, const TArray<TSoftObjectPtr<class UPaperSprite>> &ExistingSpriteSoftPtrs);

	static UTexture2D *ImportOrReimportTexture(UTexture2D *ExistingTexture, const FString &TextureSourcePath, const FString &DestinationAssetFolder);
	static UTexture2D *ImportTexture(const FString &TextureSourcePath, const FString &DestinationAssetFolder);

protected:
	bool Import(TSharedPtr<FJsonObject> SpriteDescriptorObject, const FString &NameForErrors, bool bSilent);
	UPaperSprite *FindExistingSprite(const FString &Name);

protected:
	TArray<FSpriteFrame> Frames;

	FString ImageName;
	UTexture2D *ImageTexture;

	FString ComputedNormalMapName;
	UTexture2D *NormalMapTexture;

	// Aseprite only
	TArray<FFrameTag> FrameTags;
	TArray<FLayer> Layers;
	TMap<FString, FString> LayerToGroupMap;

public:
	bool bIsReimporting;
	// The name of the default or diffuse texture during a previous import
	FString ExistingBaseTextureName;

	// Reimport시에는 옵션은 그대로 가져가야 한다
	SAsepriteImportSettings ExistingImportSettings;


	// The asset that was created for ExistingBaseTextureName during a previous import
	UTexture2D *ExistingBaseTexture;

	// The name of the normal map texture during a previous import (if any)
	FString ExistingNormalMapTextureName;

	// The asset that was created for ExistingNormalMapTextureName  during a previous import (if any)
	UTexture2D *ExistingNormalMapTexture;

	// Map of a sprite name (as seen in the importer) -> UPaperSprite
	TMap<FString, UPaperSprite *> ExistingSprites;

	// Aseprite only
	void SetReimportFlipbookData(const TArray<FString> &ExistingSpriteNames, const TArray<TSoftObjectPtr<class UPaperFlipbook>> &ExistingFlipbooksSoftPtrs); // Aseprite only
	bool bIsReimportingFlipbooks;
	void ExecuteCreateFlipbooks(TWeakObjectPtr<UAseprite> Object);
	TArray<TSoftObjectPtr<class UPaperFlipbook>> ExistingFlipbooks; // Asprite only
	FString ExtractSpriteSheet(FString InFilename);
	static void ExtractFlipbooksFromSprites(TMap<FString, TArray<UPaperSprite *>> &OutSpriteFlipbookMap, const FString BaseName, const TArray<UPaperSprite *> &Sprites, const TArray<FString> &InSpriteNames, const TMap<int32, FString> &TagNames, const TMap<int32, FString> &LayerNames); // Aseprite only
	// static FString GetCleanerSpriteName(const FString& Name);
	// static bool ExtractSpriteNumber(const FString& String, FString& BareString, int32& Number);
	// static void ExtractTagAndLayerFromFrameName(const FString& Filename, bool SplitLayer, FString& OutImageName, FString& OutLayerName, FString& OutTagName, FString& OutIndex);
	// static void ExtractInfoFromFrameName(const FString& FrameName, FString& OutImageName, FString& OutLayerName, int32& OutIndex);
	// Aseprite only end

	static void CreateSpriteAnimationMaterial(TArray<UObject *> &OutMaterials, TWeakObjectPtr<UAseprite> Object); // Aseprite only
	static void CreateNiagaraSpriteEmitter(TArray<UObject *> &OutNiagaraEmitters, TWeakObjectPtr<UAseprite> Object); // Aseprite only
	static void UpdateFlipbookMaterials(TWeakObjectPtr<UAseprite> Object); // 2025.01.07 - Bulk material update
};
