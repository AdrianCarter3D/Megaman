// Copyright ENFP-Dev-Studio, Inc. All Rights Reserved.

#include "SAsepritePathDialog.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"
#include "IDesktopPlatform.h"
#include "DesktopPlatformModule.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"

void SAsepritePathDialog::Construct(const FArguments& InArgs)
{
	bOkClicked = false;
	CurrentPath = InArgs._CurrentPath;
	SelectedPath = CurrentPath;

	this->ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(16.0f, 12.0f))
		[
			SNew(SVerticalBox)
			// Title/Description
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 12)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Configure Aseprite Executable Path")))
				.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
				.Justification(ETextJustify::Center)
			]

			// Description
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 16)
			[
				SNew(STextBlock)
				.Text(FText::FromString(CurrentPath.IsEmpty()
					? TEXT("The plugin requires Aseprite to process .ase/.aseprite files.\nPlease select the location of Aseprite.exe on your system.")
					: TEXT("The current path is invalid or the file does not exist.\nPlease select a valid Aseprite.exe location.")))
				.AutoWrapText(true)
				.Font(FAppStyle::GetFontStyle("SmallFont"))
				.Justification(ETextJustify::Left)
			]

			// Current Path Label
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 4)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Aseprite Executable Path:")))
				.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
			]

			// Path Display and Browse Button Row
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 16)
			[
				SNew(SHorizontalBox)
				// Path display (read-only text box)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
					.Padding(FMargin(8.0f, 4.0f))
					[
						SNew(STextBlock)
						.Text(this, &SAsepritePathDialog::GetPathText)
						.Font(FAppStyle::GetFontStyle("SmallFont"))
						.ColorAndOpacity(this, &SAsepritePathDialog::GetPathTextColor)
					]
				]
				// Browse button
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(8, 0, 0, 0)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Browse...")))
					.OnClicked(this, &SAsepritePathDialog::BrowseClicked)
					.ToolTipText(FText::FromString(TEXT("Select Aseprite.exe location")))
				]
			]

			// Buttons
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0, 8, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 6, 0)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("OK")))
					.HAlign(HAlign_Center)
					.OnClicked(this, &SAsepritePathDialog::OkClicked)
					.IsEnabled(this, &SAsepritePathDialog::IsPathValid)
					.ToolTipText_Lambda([this]()
					{
						return IsPathValid()
							? FText::FromString(TEXT("Save path and continue import"))
							: FText::FromString(TEXT("Please select a valid Aseprite.exe file"));
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Cancel")))
					.HAlign(HAlign_Center)
					.OnClicked(this, &SAsepritePathDialog::CancelClicked)
					.ToolTipText(FText::FromString(TEXT("Cancel and abort import")))
				]
			]
		]
	];
}

bool SAsepritePathDialog::Show()
{
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(FText::FromString(TEXT("Aseprite Configuration")))
		.SizingRule(ESizingRule::Autosized)
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		.IsTopmostWindow(true);

	Window->SetContent(AsShared());
	ContainingWindow = Window;

	FSlateApplication::Get().AddModalWindow(Window, FSlateApplication::Get().GetActiveTopLevelWindow());

	return bOkClicked;
}

void SAsepritePathDialog::CloseDialog(bool bWasPicked)
{
	bOkClicked = bWasPicked;
	if (ContainingWindow.IsValid())
	{
		ContainingWindow.Pin()->RequestDestroyWindow();
	}
}

FReply SAsepritePathDialog::OkClicked()
{
	if (IsPathValid())
	{
		CloseDialog(true);
	}
	return FReply::Handled();
}

FReply SAsepritePathDialog::CancelClicked()
{
	CloseDialog(false);
	return FReply::Handled();
}

FReply SAsepritePathDialog::BrowseClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		TArray<FString> OutFiles;
		const FString FileTypes = TEXT("Aseprite Executable (*.exe)|*.exe");

		// Determine default path
		FString DefaultPath;
		if (!SelectedPath.IsEmpty())
		{
			DefaultPath = FPaths::GetPath(SelectedPath);
		}
		else if (!CurrentPath.IsEmpty())
		{
			DefaultPath = FPaths::GetPath(CurrentPath);
		}
		else
		{
			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			DefaultPath = TEXT("C:/Program Files/Aseprite");
			if (!PlatformFile.DirectoryExists(*DefaultPath))
			{
				DefaultPath = TEXT("C:/Program Files (x86)/Aseprite");
				if (!PlatformFile.DirectoryExists(*DefaultPath))
				{
					DefaultPath = FPaths::ConvertRelativePathToFull(FPaths::RootDir());
				}
			}
		}

		bool bOpened = DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(ContainingWindow.Pin()),
			TEXT("Select Aseprite.exe"),
			DefaultPath,
			TEXT(""),
			FileTypes,
			EFileDialogFlags::None,
			OutFiles
		);

		if (bOpened && OutFiles.Num() > 0)
		{
			SelectedPath = OutFiles[0];
		}
	}

	return FReply::Handled();
}

FText SAsepritePathDialog::GetPathText() const
{
	if (SelectedPath.IsEmpty())
	{
		return FText::FromString(TEXT("No path selected"));
	}
	return FText::FromString(SelectedPath);
}

bool SAsepritePathDialog::IsPathValid() const
{
	if (SelectedPath.IsEmpty())
	{
		return false;
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	FString FileName = FPaths::GetCleanFilename(SelectedPath);

	return PlatformFile.FileExists(*SelectedPath) &&
		   FileName.Equals(TEXT("Aseprite.exe"), ESearchCase::IgnoreCase);
}

FSlateColor SAsepritePathDialog::GetPathTextColor() const
{
	if (SelectedPath.IsEmpty())
	{
		return FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)); // Gray for empty
	}
	else if (IsPathValid())
	{
		return FSlateColor(FLinearColor(0.0f, 1.0f, 0.0f)); // Green for valid
	}
	else
	{
		return FSlateColor(FLinearColor(1.0f, 0.0f, 0.0f)); // Red for invalid
	}
}
