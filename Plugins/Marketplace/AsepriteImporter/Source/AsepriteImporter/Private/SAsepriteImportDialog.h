// Copyright 2017 ~ 2022 Critical Failure Studio Ltd. All rights reserved.

#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
//5.4fix
#include "Aseprite.h"

class SAsepriteImportDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAsepriteImportDialog) : _ShowCancelButton(true)
	{
	}
	SLATE_ARGUMENT(bool, ShowCancelButton)
	SLATE_END_ARGS()

	void Construct(const FArguments &InArgs);

	// Show this widget as a modal window, will not return until the modal window is closed
	bool Show();
	// Close the dialog, using the selected option
	void CloseDialog(bool bWasPicked = false);

	FReply OkClicked();
	FReply CancelClicked();

	// Material property callbacks
	FString GetMaterialPath() const;
	void OnMaterialChanged(const FAssetData& AssetData);

private:
	// import params
	SAsepriteImportSettings TempSettings;
	// option
	bool bShowImportDialog;
	bool bIsPaperZDPluginEnabled;

	bool bOkClicked;
	// The window that holds this widget
	TWeakPtr<SWindow> ContainingWindow;
	TArray<TSharedPtr<FString>> PivotOptions;
};
