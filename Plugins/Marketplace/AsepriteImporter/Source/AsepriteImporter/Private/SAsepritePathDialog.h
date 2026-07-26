// Copyright ENFP-Dev-Studio, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class SAsepritePathDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAsepritePathDialog)
		: _CurrentPath(TEXT(""))
	{
	}
	SLATE_ARGUMENT(FString, CurrentPath)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Show this widget as a modal window, will not return until the modal window is closed
	bool Show();

	// Close the dialog, using the selected option
	void CloseDialog(bool bWasPicked = false);

	// Get the selected path
	FString GetSelectedPath() const { return SelectedPath; }

	FReply OkClicked();
	FReply CancelClicked();
	FReply BrowseClicked();

	// Path display
	FText GetPathText() const;

private:
	bool bOkClicked;
	FString SelectedPath;
	FString CurrentPath;

	// The window that holds this widget
	TWeakPtr<SWindow> ContainingWindow;

	// Validate the current path
	bool IsPathValid() const;
	FSlateColor GetPathTextColor() const;
};
