// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMaterialUpdateDialog.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Images/SImage.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"
#include "PropertyCustomizationHelpers.h"
#include "Engine/Selection.h"
#include "Materials/MaterialInterface.h"

void SMaterialUpdateDialog::Construct(const FArguments& InArgs)
{
	bOkClicked = false;
	SelectedMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Paper2D/MaskedUnlitSpriteMaterial.MaskedUnlitSpriteMaterial"));

	this->ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(16.0f, 12.0f))
		[
			SNew(SVerticalBox)
			// Description
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 12)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Select a material to apply to all flipbooks and sprites created from this Aseprite asset.")))
				.AutoWrapText(true)
				.Font(FAppStyle::GetFontStyle("SmallFont"))
				.Justification(ETextJustify::Center)
			]

			// Material Picker Row
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 12)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 12, 0)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Material:")))
					.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UMaterialInterface::StaticClass())
					.DisplayThumbnail(true)
					.DisplayCompactSize(true)
					.DisplayUseSelected(false)
					.DisplayBrowse(false)
					.EnableContentPicker(true)
					.AllowClear(false)
					.ObjectPath(this, &SMaterialUpdateDialog::GetMaterialPath)
					.OnObjectChanged(this, &SMaterialUpdateDialog::OnMaterialChanged)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(8.0f, 0.0f, 0.0f, 0.0f))
				.VAlign(VAlign_Center)
				[
					// Reset to default material icon button
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ToolTipText(FText::FromString(TEXT("Reset to Default Paper2D Material")))
					.OnClicked_Lambda([this]()
					{
						SelectedMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Paper2D/MaskedUnlitSpriteMaterial.MaskedUnlitSpriteMaterial"));
						return FReply::Handled();
					})
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("GenericCommands.Undo"))
					]
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
					.OnClicked(this, &SMaterialUpdateDialog::OkClicked)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Cancel")))
					.HAlign(HAlign_Center)
					.OnClicked(this, &SMaterialUpdateDialog::CancelClicked)
				]
			]
		]
	];
}

bool SMaterialUpdateDialog::Show()
{
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(FText::FromString(TEXT("Change Materials")))
		.SizingRule(ESizingRule::UserSized)
		.ClientSize(FVector2D(320, 120))
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		.IsTopmostWindow(true);

	Window->SetContent(AsShared());
	ContainingWindow = Window;

	FSlateApplication::Get().AddModalWindow(Window, FSlateApplication::Get().GetActiveTopLevelWindow());

	return bOkClicked;
}

void SMaterialUpdateDialog::CloseDialog(bool bWasPicked)
{
	bOkClicked = bWasPicked;
	if (ContainingWindow.IsValid())
	{
		ContainingWindow.Pin()->RequestDestroyWindow();
	}
}

FReply SMaterialUpdateDialog::OkClicked()
{
	CloseDialog(true);
	return FReply::Handled();
}

FReply SMaterialUpdateDialog::CancelClicked()
{
	CloseDialog(false);
	return FReply::Handled();
}

FString SMaterialUpdateDialog::GetMaterialPath() const
{
	if (SelectedMaterial)
	{
		return SelectedMaterial->GetPathName();
	}
	return FString();
}

void SMaterialUpdateDialog::OnMaterialChanged(const FAssetData& AssetData)
{
	if (AssetData.IsValid())
	{
		SelectedMaterial = Cast<UMaterialInterface>(AssetData.GetAsset());
	}
	else
	{
		SelectedMaterial = nullptr;
	}
}