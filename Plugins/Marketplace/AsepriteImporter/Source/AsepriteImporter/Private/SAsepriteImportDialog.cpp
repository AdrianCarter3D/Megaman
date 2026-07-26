// Copyright 2017 ~ 2022 Critical Failure Studio Ltd. All rights reserved.

#include "SAsepriteImportDialog.h"
#include "Aseprite.h"
#include "Framework/Text/TextLayout.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SSeparator.h"
#include "Styling/AppStyle.h"
#include "Editor.h"
#include "Interfaces/IPluginManager.h"

// 5.4fix
#include "Widgets/Input/SNumericEntryBox.h"
#include "AsepriteImporter.h"
#include "PropertyCustomizationHelpers.h"

#define LOCTEXT_NAMESPACE "Aseprite Import Dialog"

// --- 피벗 텍스트를 FVector2D로 변환 ---
static FVector2D GetPivotVector(const FString& PivotText)
{
    if (PivotText == TEXT("Top Left")) return FVector2D(0.0f, 0.0f);
    if (PivotText == TEXT("Top Center")) return FVector2D(0.5f, 0.0f);
    if (PivotText == TEXT("Top Right")) return FVector2D(1.0f, 0.0f);
    if (PivotText == TEXT("Center Left")) return FVector2D(0.0f, 0.5f);
    if (PivotText == TEXT("Center Center")) return FVector2D(0.5f, 0.5f);
    if (PivotText == TEXT("Center Right")) return FVector2D(1.0f, 0.5f);
    if (PivotText == TEXT("Bottom Left")) return FVector2D(0.0f, 1.0f);
    if (PivotText == TEXT("Bottom Center")) return FVector2D(0.5f, 1.0f);
    if (PivotText == TEXT("Bottom Right")) return FVector2D(1.0f, 1.0f);
    
    return FVector2D::ZeroVector; // 기본값
}

static FString GetPivotTypeText(const FVector2D& PivotType)
{
	if (PivotType == FVector2D(0.0f, 0.0f)) return TEXT("Top Left");
	if (PivotType == FVector2D(0.5f, 0.0f)) return TEXT("Top Center");
	if (PivotType == FVector2D(1.0f, 0.0f)) return TEXT("Top Right");
	if (PivotType == FVector2D(0.0f, 0.5f)) return TEXT("Center Left");
	if (PivotType == FVector2D(0.5f, 0.5f)) return TEXT("Center Center");
	if (PivotType == FVector2D(1.0f, 0.5f)) return TEXT("Center Right");
	if (PivotType == FVector2D(0.0f, 1.0f)) return TEXT("Bottom Left");
	if (PivotType == FVector2D(0.5f, 1.0f)) return TEXT("Bottom Center");
	if (PivotType == FVector2D(1.0f, 1.0f)) return TEXT("Bottom Right");
	return TEXT("Center Center"); // 기본값
}

static bool IsPaperZDPluginEnabled()
{
	TSharedPtr<IPlugin> PaperZDPlugin = IPluginManager::Get().FindPlugin(TEXT("PaperZD"));
	return PaperZDPlugin.IsValid() && PaperZDPlugin->IsEnabled();
}

void SAsepriteImportDialog::Construct(const FArguments &InArgs)
{
	int32 Padding = 10;
	bOkClicked = false;
	bIsPaperZDPluginEnabled = IsPaperZDPluginEnabled();
	
    PivotOptions.Add(MakeShared<FString>(TEXT("Top Left")));
    PivotOptions.Add(MakeShared<FString>(TEXT("Top Center")));
    PivotOptions.Add(MakeShared<FString>(TEXT("Top Right")));
    PivotOptions.Add(MakeShared<FString>(TEXT("Center Left")));
    PivotOptions.Add(MakeShared<FString>(TEXT("Center Center")));
    PivotOptions.Add(MakeShared<FString>(TEXT("Center Right")));
    PivotOptions.Add(MakeShared<FString>(TEXT("Bottom Left")));
    PivotOptions.Add(MakeShared<FString>(TEXT("Bottom Center")));
    PivotOptions.Add(MakeShared<FString>(TEXT("Bottom Right")));

	// initial selected as var
	// find TempSettings.PivotType in PivotOptions
	auto InitSelected = PivotOptions.FindByPredicate([this](TSharedPtr<FString> Option) { return GetPivotVector(*Option) == TempSettings.PivotType; });

	ChildSlot
    .VAlign(VAlign_Fill)
    .HAlign(HAlign_Fill)
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
            .Padding(Padding)
            .AutoHeight()
            [
                SNew(SCheckBox)
				.Padding(FMargin(5.0f, 0.0f)) // 좌우 10, 상하 0
                .IsChecked_Lambda([this]
                {
                    return TempSettings.bCreateFlipbooks ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
                {
                    TempSettings.bCreateFlipbooks = (NewState == ECheckBoxState::Checked);
                    if (!TempSettings.bCreateFlipbooks)
                    {
                        TempSettings.bCreatePaperZDAssets = false;
                        TempSettings.bCreatePaperZDAnimBPDraft = false;
                    }
                })
                [
                    SNew(STextBlock).Text(FText::FromString(TEXT("Create Flipbooks")))
                ]
            ]
        + SVerticalBox::Slot()
            .Padding(Padding)
            .AutoHeight()
            [
                SNew(SCheckBox)
				.Padding(FMargin(5.0f, 0.0f)) // 좌우 10, 상하 0
                .IsEnabled_Lambda([this]
                {
                    return TempSettings.bCreateFlipbooks;
                })
                .ToolTipText(FText::FromString(TEXT("Skip Empty Tag Frames when Create Flipbooks")))
                .IsChecked_Lambda([this]
                {
                    return TempSettings.bSkipEmptyTagFramesWhenCreateFlipbooks ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
                {
                    TempSettings.bSkipEmptyTagFramesWhenCreateFlipbooks = (NewState == ECheckBoxState::Checked);
                })
                [
                    SNew(STextBlock).Text(FText::FromString(TEXT("Skip Empty Tag Frames")))
                ]
            ]
        + SVerticalBox::Slot()
            .Padding(Padding)
            .AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    .Padding(0, 0, 10, 0)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Flipbook FPS:")))
                    ]
                + SHorizontalBox::Slot()
                    .AutoWidth()
                    [
                        SNew(SNumericEntryBox<uint32>)
                        .MinValue(1)
                        .MinSliderValue(1)
                        .MaxValue(240)
                        .MaxSliderValue(60)
                        .MinDesiredValueWidth(60.0f)
                        .LabelVAlign(VAlign_Center)
                        .IsEnabled_Lambda([this]
                        {
                            return TempSettings.bCreateFlipbooks;
                        })
                        .ToolTipText(FText::FromString(TEXT("The number of frames per second to play the flipbook animation.")))
                        .Value_Lambda([this]
                        {
                            return TempSettings.FlipbookFramePerSecond;
                        })
                        .OnValueCommitted_Lambda([this](uint32 NewValue, ETextCommit::Type CommitType)
                        {
                            TempSettings.FlipbookFramePerSecond = NewValue;
                        })
                    ]
            ]
        + SVerticalBox::Slot()
			.Padding(Padding)
            .VAlign(VAlign_Center)
            .AutoHeight()
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0, 0, 0, 5)
                    [
                        SNew(STextBlock)
                        .ToolTipText(FText::FromString(TEXT("Pixel Per Unit")))
                        .Text(FText::FromString(TEXT("Pixel Per Unit")))
                    ]
                + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        SNew(SNumericEntryBox<float>)
                        .MinValue(0.01f)
                        .LabelVAlign(VAlign_Center)
                        .Value_Lambda([this]
                        {
                            return TempSettings.PixelPerUnit;
                        })
                        .OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type CommitType)
                        {
                            TempSettings.PixelPerUnit = NewValue;
                        })
                    ]
            ]
        + SVerticalBox::Slot()
			.Padding(Padding)
            .VAlign(VAlign_Center)
            .AutoHeight()
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0, 0, 0, 5)
                    [
                        SNew(STextBlock)
                        .ToolTipText(FText::FromString(TEXT("Pivot Type")))
                        .Text(FText::FromString(TEXT("Pivot Type")))
                    ]
                + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        SNew(SComboBox<TSharedPtr<FString>>)
                        .OptionsSource(&PivotOptions)
						.InitiallySelectedItem(InitSelected ? *InitSelected : PivotOptions[4])
                        .OnSelectionChanged_Lambda([this](TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
                        {
                            if (NewSelection.IsValid())
                            {
                                TempSettings.PivotType = GetPivotVector(*NewSelection);
                            }
                        })
                        .OnGenerateWidget_Lambda([this](TSharedPtr<FString> Option)
                        {
                            return SNew(STextBlock).Text(FText::FromString(*Option));
                        })
                        .Content()
                        [
                            SNew(STextBlock).Text_Lambda([this]()
                            {
								return FText::FromString(GetPivotTypeText(TempSettings.PivotType));
                            })
                        ]
                    ]
            ]
        + SVerticalBox::Slot()
            .Padding(Padding)
            .AutoHeight()
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0, 0, 0, 5)
                    [
                        SNew(STextBlock)
                        .ToolTipText(FText::FromString(TEXT("Sprite Material")))
                        .Text(FText::FromString(TEXT("Material")))
                    ]
                + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        SNew(SHorizontalBox)
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
                            .ObjectPath(this, &SAsepriteImportDialog::GetMaterialPath)
                            .OnObjectChanged(this, &SAsepriteImportDialog::OnMaterialChanged)
                        ]
                        + SHorizontalBox::Slot()
                        .AutoWidth()
                        .Padding(FMargin(5.0f, 0.0f))
                        .VAlign(VAlign_Center)
                        [
                            // 기본 머터리얼로 되돌리기 버튼
                            // Reset to default material button
                            SNew(SButton)
                            .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                            .ToolTipText(FText::FromString(TEXT("Reset to Default Material")))
                            .OnClicked_Lambda([this]()
                            {
                                TempSettings.SpriteMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Paper2D/MaskedUnlitSpriteMaterial.MaskedUnlitSpriteMaterial")));
                                return FReply::Handled();
                            })
                            [
                                SNew(STextBlock)
                                .Font(FAppStyle::Get().GetFontStyle("FontAwesome.10"))
                                .Text(FText::FromString(TEXT("\xf021"))) // FontAwesome refresh icon
                                .ColorAndOpacity(FSlateColor::UseForeground())
                            ]
                        ]
                    ]
            ]
        + SVerticalBox::Slot()
            .Padding(Padding)
            .AutoHeight()
            [
                SNew(SCheckBox)
				.Padding(FMargin(5.0f, 0.0f)) // 좌우 10, 상하 0
                .IsChecked_Lambda([this]
                {
                    return TempSettings.bSplitLayers ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
                {
                    TempSettings.bSplitLayers = (NewState == ECheckBoxState::Checked);
                })
                [
                    SNew(STextBlock).Text(FText::FromString(TEXT("Split Layers")))
                ]
            ]
        + SVerticalBox::Slot()
            .Padding(Padding)
            .AutoHeight()
            [
                SNew(SCheckBox)
				.Padding(FMargin(5.0f, 0.0f)) // 좌우 10, 상하 0
                .IsChecked_Lambda([this]
                {
                    return bShowImportDialog ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
                {
                    bShowImportDialog = (NewState == ECheckBoxState::Checked);
                })
                [
                    SNew(STextBlock).Text(FText::FromString(TEXT("Show Import Dialog")))
                ]
            ]
        + SVerticalBox::Slot()
            .Padding(FMargin(Padding, Padding, Padding, 0))
            .AutoHeight()
            [
                SNew(SSeparator)
                .Thickness(1.0f)
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
            ]
        + SVerticalBox::Slot()
            .Padding(FMargin(Padding, 8.0f, Padding, Padding))
            .AutoHeight()
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0, 0, 0, 4.0f)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("PaperZD (Experimental)")))
                    ]
                + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 0.0f, 0.0f, 7.0f)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]
                        {
                            return bIsPaperZDPluginEnabled
                                ? FText::FromString(TEXT("Experimental: creates PaperZD-ready assets from imported flipbooks."))
                                : FText::FromString(TEXT("Experimental: this feature requires the PaperZD plugin."));
                        })
                        .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                        .AutoWrapText(true)
                    ]
                + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 0.0f, 0.0f, 0.0f)
                    [
                        SNew(SCheckBox)
                        .Padding(FMargin(5.0f, 0.0f))
                        .IsEnabled_Lambda([this]
                        {
                            return TempSettings.bCreateFlipbooks && bIsPaperZDPluginEnabled;
                        })
                        .ToolTipText(FText::FromString(TEXT("Experimental feature. Requires the PaperZD plugin. Creates a PaperZD Animation Source and Flipbook animation sequences.")))
                        .IsChecked_Lambda([this]
                        {
                            return (TempSettings.bCreateFlipbooks && bIsPaperZDPluginEnabled && TempSettings.bCreatePaperZDAssets) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                        })
                        .OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
                        {
                            TempSettings.bCreatePaperZDAssets = bIsPaperZDPluginEnabled && TempSettings.bCreateFlipbooks && (NewState == ECheckBoxState::Checked);
                            if (!TempSettings.bCreatePaperZDAssets)
                            {
                                TempSettings.bCreatePaperZDAnimBPDraft = false;
                            }
                        })
                        [
                            SNew(STextBlock).Text(FText::FromString(TEXT("Create PaperZD Assets")))
                        ]
                    ]
                + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 5.0f, 0.0f, 0.0f)
                    [
                        SNew(SCheckBox)
                        .Padding(FMargin(5.0f, 0.0f))
                        .IsEnabled_Lambda([this]
                        {
                            return TempSettings.bCreateFlipbooks && bIsPaperZDPluginEnabled && TempSettings.bCreatePaperZDAssets;
                        })
                        .ToolTipText(FText::FromString(TEXT("Experimental feature. Requires the PaperZD plugin. Creates a draft PaperZD AnimBP with one state per imported flipbook; transitions are left for the user.")))
                        .IsChecked_Lambda([this]
                        {
                            return (TempSettings.bCreateFlipbooks && bIsPaperZDPluginEnabled && TempSettings.bCreatePaperZDAssets && TempSettings.bCreatePaperZDAnimBPDraft) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                        })
                        .OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
                        {
                            TempSettings.bCreatePaperZDAnimBPDraft = bIsPaperZDPluginEnabled && TempSettings.bCreateFlipbooks && (NewState == ECheckBoxState::Checked);
                            if (TempSettings.bCreatePaperZDAnimBPDraft)
                            {
                                TempSettings.bCreatePaperZDAssets = true;
                            }
                        })
                        [
                            SNew(STextBlock).Text(FText::FromString(TEXT("Create PaperZD AnimBP Draft")))
                        ]
                    ]
            ]
        + SVerticalBox::Slot()
            .Padding(FMargin(Padding, 0, Padding, 0))
            .AutoHeight()
            [
                SNew(SSeparator)
                .Thickness(1.0f)
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
            ]
        + SVerticalBox::Slot()
            .Padding(Padding)
            .AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                    .FillWidth(0.5f)
                    [
                        SNew(SButton)
                        .OnClicked(this, &SAsepriteImportDialog::OkClicked)
                        [
                            SNew(STextBlock)
                            .Text(LOCTEXT("CreateAnimBlueprintOk", "OK"))
                            .Justification(ETextJustify::Center)
                        ]
                    ]
                + SHorizontalBox::Slot()
                    .FillWidth(0.5f)
                    [
                        SNew(SButton)
                        .OnClicked(this, &SAsepriteImportDialog::CancelClicked)
                        [
                            SNew(STextBlock)
                            .Text(LOCTEXT("CreateAnimBlueprintCancel", "Cancel"))
                            .Justification(ETextJustify::Center)
                        ]
                    ]
            ]
    ];

}

bool SAsepriteImportDialog::Show()
{
	UAsepriteImporterSettings *Settings = GetMutableDefault<UAsepriteImporterSettings>();
	bIsPaperZDPluginEnabled = IsPaperZDPluginEnabled();

	TempSettings.bCreateFlipbooks = Settings->bCreateFlipbooks;
	TempSettings.FlipbookFramePerSecond = Settings->FlipbookFramePerSecond;
	TempSettings.bSplitLayers = Settings->bSplitLayers;
	TempSettings.bSkipEmptyTagFramesWhenCreateFlipbooks = Settings->bSkipEmptyTagFramesWhenCreateFlipbooks;
	TempSettings.bCreatePaperZDAssets = Settings->bCreatePaperZDAssets;
	TempSettings.bCreatePaperZDAnimBPDraft = Settings->bCreatePaperZDAnimBPDraft;
	TempSettings.PixelPerUnit = Settings->PixelPerUnit;
	TempSettings.PivotType = Settings->PivotType;

	if (!bIsPaperZDPluginEnabled || !TempSettings.bCreateFlipbooks)
	{
		TempSettings.bCreatePaperZDAssets = false;
		TempSettings.bCreatePaperZDAnimBPDraft = false;
	}

	// Load material from settings, or use default Paper2D material if not set
	// 설정에서 머터리얼을 불러오거나, 설정되지 않은 경우 Paper2D 기본 머터리얼 사용
	if (Settings->SpriteMaterial.IsValid())
	{
		TempSettings.SpriteMaterial = Settings->SpriteMaterial.LoadSynchronous();
	}
	else
	{
		// 기본 Paper2D 머터리얼을 설정
		// Set default Paper2D material
		TempSettings.SpriteMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Paper2D/MaskedUnlitSpriteMaterial.MaskedUnlitSpriteMaterial")));
	}

	bShowImportDialog = Settings->bShowImportDialog;

	// Slate prepass first
	SlatePrepass(1.0f);

	// The desired size of this widget
	FVector2D Size = GetDesiredSize();

	TSharedRef<SWindow> Window = SNew(SWindow)
									 .Title(LOCTEXT("AsepriteImportDialogTitle", "Aseprite Importer"))
									 .ClientSize(FVector2D(320, Size.Y + 24.0f))
									 .SupportsMinimize(false)
									 .SupportsMaximize(false)
									 .SizingRule(ESizingRule::FixedSize)
										 [AsShared()];

	// Assign to the weak variable
	ContainingWindow = Window;
	GEditor->EditorAddModalWindow(Window);
	return bOkClicked;
}

FReply SAsepriteImportDialog::OkClicked()
{
	CloseDialog(true);
	return FReply::Handled();
}

FReply SAsepriteImportDialog::CancelClicked()
{
	CloseDialog(false);
	return FReply::Handled();
}

void SAsepriteImportDialog::CloseDialog(bool bWasPicked)
{
	bOkClicked = bWasPicked;
	// UE_LOG(LogTemp, Warning, TEXT("CloseDialog, bWasPicked: %d"), bWasPicked);
	if (bOkClicked)
	{
		UAsepriteImporterSettings *Settings = GetMutableDefault<UAsepriteImporterSettings>();
		Settings->bCreateFlipbooks = TempSettings.bCreateFlipbooks;
		Settings->FlipbookFramePerSecond = TempSettings.FlipbookFramePerSecond;
		Settings->bSplitLayers = TempSettings.bSplitLayers;
		Settings->bSkipEmptyTagFramesWhenCreateFlipbooks = TempSettings.bSkipEmptyTagFramesWhenCreateFlipbooks;
		const bool bCanCreatePaperZDAssets = bIsPaperZDPluginEnabled && TempSettings.bCreateFlipbooks;
		Settings->bCreatePaperZDAssets = bCanCreatePaperZDAssets && TempSettings.bCreatePaperZDAssets;
		Settings->bCreatePaperZDAnimBPDraft = bCanCreatePaperZDAssets && TempSettings.bCreatePaperZDAssets && TempSettings.bCreatePaperZDAnimBPDraft;
		Settings->PixelPerUnit = TempSettings.PixelPerUnit;
		Settings->PivotType = TempSettings.PivotType;
		Settings->SpriteMaterial = TempSettings.SpriteMaterial;
		// 나머지는 파라미터고 이건 설정이므로 따로 저장
		Settings->bShowImportDialog = bShowImportDialog;
	}

	if (ContainingWindow.IsValid())
	{
		ContainingWindow.Pin()->RequestDestroyWindow();
	}
}

FString SAsepriteImportDialog::GetMaterialPath() const
{
	if (!TempSettings.SpriteMaterial.IsNull())
	{
		FString Path = TempSettings.SpriteMaterial.ToSoftObjectPath().ToString();
		// UE_LOG(LogTemp, Warning, TEXT("GetMaterialPath: %s"), *Path);
		return Path;
	}

	// 머터리얼이 null인 경우 기본 Paper2D 머터리얼 경로 반환
	// Return default Paper2D material path if material is null
	return TEXT("/Paper2D/MaskedUnlitSpriteMaterial.MaskedUnlitSpriteMaterial");
}

void SAsepriteImportDialog::OnMaterialChanged(const FAssetData& AssetData)
{
	if (AssetData.IsValid())
	{
		// UE_LOG(LogTemp, Warning, TEXT("Material changed: %s"), *AssetData.ToSoftObjectPath().ToString());
		TempSettings.SpriteMaterial = TSoftObjectPtr<UMaterialInterface>(AssetData.ToSoftObjectPath());
	}
}

#undef LOCTEXT_NAMESPACE
