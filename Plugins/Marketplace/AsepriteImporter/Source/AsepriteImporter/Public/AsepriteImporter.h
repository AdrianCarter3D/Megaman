// Copyright ENFP-Dev-Studio, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "UObject/NoExportTypes.h"
#include "IAssetTypeActions.h"
#include "AsepriteImporter.generated.h"

// 사용자 설정 클래스 정의
UCLASS(config = AsepriteImporter)
class ASEPRITEIMPORTER_API UAsepriteImporterSettings : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, Category = "Settings")
	FString AsepriteExecutablePath;

	UPROPERTY(Config, EditAnywhere, Category = "Settings")
	bool bCreateFlipbooks = true;

	// Flipbook FPS 설정 (UI와 타입 일치를 위해 uint32 사용)
	// Flipbook FPS setting (using uint32 for type consistency with UI)
	UPROPERTY(Config, EditAnywhere, Category = "Settings", Meta = (ClampMin = "1", ClampMax = "1000"))
	uint32 FlipbookFramePerSecond = 15;

	UPROPERTY(Config, EditAnywhere, Category = "Settings")
	bool bSplitLayers = false;

	UPROPERTY(Config, EditAnywhere, Category = "Settings")
	bool bSkipEmptyTagFramesWhenCreateFlipbooks = true;

	UPROPERTY(Config, EditAnywhere, Category = "PaperZD")
	bool bCreatePaperZDAssets = false;

	UPROPERTY(Config, EditAnywhere, Category = "PaperZD", Meta = (EditCondition = "bCreatePaperZDAssets"))
	bool bCreatePaperZDAnimBPDraft = false;

	UPROPERTY(Config, EditAnywhere, Category = "Settings", Meta = (ToolTip = "Layer separator used when generating split-layer flipbook names. Changing this can affect reimport matching. Empty values are allowed."))
	FString FlipbookLayerSeparator = TEXT("_L_");

	UPROPERTY(Config, EditAnywhere, Category = "Settings", Meta = (ToolTip = "Tag separator used when generating split-layer flipbook names. Changing this can affect reimport matching. Empty values are allowed."))
	FString FlipbookTagSeparator = TEXT("_T_");

	// 2025.01.07
	UPROPERTY(Config, EditAnywhere, Category = "Settings", Meta = (ClampMin = "0.01", ClampMax = "100.0"))
	float PixelPerUnit = 2.56f;

	// 2025.01.07
	UPROPERTY(Config, EditAnywhere, Category = "Settings", Meta = (ClampMin = "0.0", ClampMax = "1.0"))
	FVector2D PivotType = FVector2D(0.5f, 0.5f); // Center_Center

	UPROPERTY(Config, EditAnywhere, Category = "Settings")
	bool bShowImportDialog = true;

	UPROPERTY(Config, EditAnywhere, Category = "Settings")
	TSoftObjectPtr<class UMaterialInterface> SpriteMaterial;
	
    UAsepriteImporterSettings(const FObjectInitializer &ObjectInitializer);
};

class FToolBarBuilder;
class FMenuBuilder;

class FAsepriteImporterModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	/** This function will be bound to Command. */
	// void PluginButtonClicked();

private:
	void OnPostEngineInit();
	bool CheckAndRequestAsepriteExecutablePath(); // 경로를 가져오는 함수
												  // void RegisterMenus();

	// 사용자에게 경로를 설정하도록 요청하는 함수
	// void RequestAsepriteExecutablePath();
private:
	TSharedPtr<class FUICommandList> PluginCommands;
	TSharedPtr<IAssetTypeActions> SpriteSheetImportAssetTypeActions;
};
