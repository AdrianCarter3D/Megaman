// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"

class SMaterialUpdateDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMaterialUpdateDialog)
	{
	}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Show this widget as a modal window, will not return until the modal window is closed
	bool Show();
	
	// Close the dialog, using the selected option
	void CloseDialog(bool bWasPicked = false);

	// Get the selected material
	UMaterialInterface* GetSelectedMaterial() const { return SelectedMaterial; }

	FReply OkClicked();
	FReply CancelClicked();

	// Material property callbacks
	FString GetMaterialPath() const;
	void OnMaterialChanged(const FAssetData& AssetData);

private:
	bool bOkClicked;
	UMaterialInterface* SelectedMaterial;
	
	// The window that holds this widget
	TWeakPtr<SWindow> ContainingWindow;
};