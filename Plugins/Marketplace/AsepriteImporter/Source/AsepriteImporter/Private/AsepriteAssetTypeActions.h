// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetTypeActions_Base.h"

class UAseprite;

class FAsepriteAssetTypeActions : public FAssetTypeActions_Base
{
public:
	// IAssetTypeActions interface
	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override;
	virtual UClass* GetSupportedClass() const override;
	virtual void GetActions(const TArray<UObject*>& InObjects, struct FToolMenuSection& Section) override;
	virtual uint32 GetCategories() override;
	virtual bool IsImportedAsset() const override;
	virtual void GetResolvedSourceFilePaths(const TArray<UObject*>& TypeAssets, TArray<FString>& OutSourceFilePaths) const override;
	// End of IAssetTypeActions interface
	void ExecuteCreateFlipbooks(TArray<TWeakObjectPtr<UAseprite>> Objects);
	void ExecuteCreateSpriteAnimationMaterial(TArray<TWeakObjectPtr<UAseprite>> Objects);
	void ExecuteCreateNiagaraSpriteRenderer(TArray<TWeakObjectPtr<UAseprite>> Objects);
	void ExecuteUpdateFlipbookMaterials(TArray<TWeakObjectPtr<UAseprite>> Objects); // 2025.01.07 - Bulk material update
};
