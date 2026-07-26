// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Aseprite.generated.h"

class UTexture2D;
class UPaperFlipbook; // Asprite only

namespace AsepriteImportBehaviorVersion
{
	static constexpr int32 Legacy = 0;
	static constexpr int32 CustomFlipbookNaming = 1;
	static constexpr int32 Current = CustomFlipbookNaming;
}

// 2024.03.05
// import dialog와 json importer에서 reimport에서 사용하기 위함
struct ASEPRITEIMPORTER_API SAsepriteImportSettings
{
	int32 ImportBehaviorVersion = 0;
	bool bCreateFlipbooks;
	uint32 FlipbookFramePerSecond;
	bool bSplitLayers;
	bool bSkipEmptyTagFramesWhenCreateFlipbooks;
	bool bCreatePaperZDAssets = false;
	bool bCreatePaperZDAnimBPDraft = false;
	FString FlipbookLayerSeparator;
	FString FlipbookTagSeparator;
	float PixelPerUnit;
	FVector2D PivotType;
	// 기본 선택된 머터리얼 정보를 저장
	TSoftObjectPtr<class UMaterialInterface> SpriteMaterial;
};

UCLASS(BlueprintType, meta = (DisplayThumbnail = "true"))
class ASEPRITEIMPORTER_API UAseprite : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	// The names of sprites during import
	UPROPERTY(VisibleAnywhere, Category = Data)
	TArray<FString> SpriteNames;

	UPROPERTY(VisibleAnywhere, Category = Data)
	TArray<int32> SpriteFrameDurations; // Aseprite only unit: milliseconds

	UPROPERTY(VisibleAnywhere, Category = Data)
	TArray<TSoftObjectPtr<class UPaperSprite>> Sprites;

	// The name of the default or diffuse texture during import
	UPROPERTY(VisibleAnywhere, Category = Data)
	FString TextureName;

	// The asset that was created for TextureName
	UPROPERTY(VisibleAnywhere, Category = Data)
	TObjectPtr<UTexture2D> Texture;

	// The name of the normal map texture during import (if any)
	UPROPERTY(VisibleAnywhere, Category = Data)
	FString NormalMapTextureName;

	// The asset that was created for NormalMapTextureName (if any)
	UPROPERTY(VisibleAnywhere, Category = Data)
	TObjectPtr<UTexture2D> NormalMapTexture;

	UPROPERTY(VisibleAnywhere, Category = Data)
	TArray<TSoftObjectPtr<class UPaperFlipbook>> Flipbooks; // Asprite only

	UPROPERTY(VisibleAnywhere, Category = Data)
	TMap<int32, FString> LayerNames;

	UPROPERTY(VisibleAnywhere, Category = Data)
	TMap<int32, FString> TagNames;

	UPROPERTY(VisibleAnywhere, Category = Data)
	bool bCreateFlipbooks;

	UPROPERTY(VisibleAnywhere, Category = Data)
	uint32 FlipbookFramePerSecond;

	UPROPERTY(VisibleAnywhere, Category = Data)
	bool bSplitLayers;

	UPROPERTY(VisibleAnywhere, Category = Data)
	bool bSkipEmptyTagFramesWhenCreateFlipbooks;

	UPROPERTY(VisibleAnywhere, Category = Data)
	bool bCreatePaperZDAssets = false;

	UPROPERTY(VisibleAnywhere, Category = Data)
	bool bCreatePaperZDAnimBPDraft = false;

	UPROPERTY(VisibleAnywhere, Category = Data)
	int32 ImportBehaviorVersion = 0;

	UPROPERTY(VisibleAnywhere, Category = Data)
	FString FlipbookLayerSeparator;

	UPROPERTY(VisibleAnywhere, Category = Data)
	FString FlipbookTagSeparator;

	// 2025.01.07
	UPROPERTY(VisibleAnywhere, Category = Data)
	float PixelPerUnit = 2.56f;

	// 2025.01.07
	UPROPERTY(VisibleAnywhere, Category = Data)
	FVector2D PivotType = FVector2D(0.5f, 0.5f); // Center_Center
	

	UPROPERTY(VisibleAnywhere, Category = Data)
	uint32 FrameWidth;

	UPROPERTY(VisibleAnywhere, Category = Data)
	uint32 FrameHeight;

	// Layer Group Map 2024.09.19
	UPROPERTY(VisibleAnywhere, Category = Data)
	TMap<FString, FString> LayerToGroupMap;

	// Reimport를 확인하는 시점에 옵션이 달라졌을 경우, 원래 조건으로 import하기 위함

#if WITH_EDITORONLY_DATA
	// Import data for this
	UPROPERTY(VisibleAnywhere, Instanced, Category = ImportSettings)
	TObjectPtr<class UAssetImportData> AssetImportData;

	// UObject interface
	virtual void PostInitProperties() override;
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag> &OutTags) const override;
	virtual void Serialize(FArchive &Ar) override;
	// End of UObject interface
#endif
};
