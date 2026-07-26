// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsepriteJsonImporter.h"
#include "AsepriteImporterLog.h"
#include "Misc/Paths.h"
#include "Misc/FeedbackContext.h"
#include "Serialization/JsonSerializer.h"
#include "EditorReimportHandler.h"
#include "PaperSprite.h"
#include "PaperJSONHelpers.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "PackageTools.h"
#include "ObjectTools.h"
#include "Aseprite.h"
#include "PaperImporterSettings.h"
// 5.4 fix
#include "AsepriteImporter.h"
#include "PaperFlipbook.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "EditorFramework/AssetImportData.h"
#include "PaperFlipbookFactory.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/MessageDialog.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserDelegates.h"
#include "SMaterialUpdateDialog.h"
#include "SAsepritePathDialog.h"
#include "IDesktopPlatform.h"
#include "DesktopPlatformModule.h"

// Create Material Flipbook
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionFloor.h"
#include "Materials/MaterialExpressionFrac.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionFmod.h"

// Create Niagara Sprite Renderer
#include "NiagaraEditorDataBase.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraEmitter.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraEmitterFactoryNew.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraSpriteRendererProperties.h"
#include "HAL/PlatformFilemanager.h"
#include "Materials/MaterialExpressionParticleSubUV.h"
#include "Misc/MessageDialog.h"
#include "UObject/ConstructorHelpers.h"

struct FTagRange
{
	int32 From;
	int32 To;
	FString TagName;

	FTagRange(int32 InFrom, int32 InTo, FString InTagName)
		: From(InFrom), To(InTo), TagName(InTagName) {}
};

TArray<FTagRange> CreateTagRanges(const TMap<int32, FString> &TagNames)
{
	TArray<FTagRange> Ranges;
	if (TagNames.Num() == 0)
		return Ranges;

	TArray<int32> Keys;
	TagNames.GetKeys(Keys);
	Keys.Sort();

	int32 RangeStart = Keys[0];
	FString CurrentString = TagNames[RangeStart];

	for (int32 i = 1; i < Keys.Num(); ++i)
	{
		// 키가 연속적이지 않거나 문자열이 변경된 경우
		if (Keys[i] != Keys[i - 1] + 1 || TagNames[Keys[i]] != CurrentString)
		{
			Ranges.Add(FTagRange(RangeStart, Keys[i - 1], CurrentString));
			RangeStart = Keys[i];
			CurrentString = TagNames[Keys[i]];
		}
	}

	// 마지막 범위 추가
	Ranges.Add(FTagRange(RangeStart, Keys.Last(), CurrentString));

	return Ranges;
}

static FString MakeLayerTagFlipbookName(const FString &BaseName, const FString &LayerName, const FString &TagName, const FString &LayerSeparator, const FString &TagSeparator)
{
	return BaseName + LayerSeparator + LayerName + TagSeparator + TagName;
}

TSharedPtr<FJsonObject> ParseJSON(const FString &FileContents, const FString &NameForErrors, bool bSilent)
{
	// Load the file up (JSON format)
	if (!FileContents.IsEmpty())
	{
		const TSharedRef<TJsonReader<>> &Reader = TJsonReaderFactory<>::Create(FileContents);

		TSharedPtr<FJsonObject> SpriteDescriptorObject;
		if (FJsonSerializer::Deserialize(Reader, SpriteDescriptorObject) && SpriteDescriptorObject.IsValid())
		{
			// File was loaded and deserialized OK!
			return SpriteDescriptorObject;
		}
		else
		{
			if (!bSilent)
			{
				UE_LOG(LogAsepriteImporter, Warning, TEXT("Failed to parse sprite descriptor file '%s'.  Error: '%s'"), *NameForErrors, *Reader->GetErrorMessage());
			}
			return nullptr;
		}
	}
	else
	{
		if (!bSilent)
		{
			UE_LOG(LogAsepriteImporter, Warning, TEXT("Sprite descriptor file '%s' was empty.  This sprite cannot be imported."), *NameForErrors);
		}
		return nullptr;
	}
}

TSharedPtr<FJsonObject> ParseJSON(FArchive *const Stream, const FString &NameForErrors, bool bSilent)
{
	const TSharedRef<TJsonReader<>> &Reader = TJsonReaderFactory<>::Create(Stream);

	TSharedPtr<FJsonObject> SpriteDescriptorObject;
	if (FJsonSerializer::Deserialize(Reader, SpriteDescriptorObject) && SpriteDescriptorObject.IsValid())
	{
		// File was loaded and deserialized OK!
		return SpriteDescriptorObject;
	}
	else
	{
		if (!bSilent)
		{
			UE_LOG(LogAsepriteImporter, Warning, TEXT("Failed to parse sprite descriptor file '%s'.  Error: '%s'"), *NameForErrors, *Reader->GetErrorMessage());
		}
		return nullptr;
	}
}

static void ExtractLayerName(const FString &FrameName, FString &OutImageName, FString &OutLayerName, int32 &OutIndex)
{
	OutLayerName = TEXT("");
	OutImageName = TEXT("");
	OutIndex = -1;

	// 인덱스와 확장자명 추출
	int32 DotIndex = FrameName.Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	if (DotIndex != INDEX_NONE)
	{
		// 확장자명 이전의 문자열 추출
		FString BeforeExtension = FrameName.Left(DotIndex);

		// 마지막 공백 위치 찾기
		int32 LastSpaceIndex = BeforeExtension.Find(TEXT(" "), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if (LastSpaceIndex != INDEX_NONE)
		{
			FString IndexString = BeforeExtension.Mid(LastSpaceIndex + 1);
			OutIndex = FCString::Atoi(*IndexString);

			// 인덱스 추출 성공
			// 레이어 이름 추출
			int32 OpenParenIndex = BeforeExtension.Find(TEXT("("), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
			if (OpenParenIndex != INDEX_NONE)
			{
				int32 CloseParenIndex = BeforeExtension.Find(TEXT(")"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
				if (CloseParenIndex > OpenParenIndex)
				{
					OutLayerName = BeforeExtension.Mid(OpenParenIndex + 1, CloseParenIndex - OpenParenIndex - 1);
					// UE_LOG(LogTemp, Log, TEXT("LayerName: %s"), *OutLayerName);

					// 이미지 이름 추출
					OutImageName = BeforeExtension.Left(OpenParenIndex).TrimEnd();
					// UE_LOG(LogTemp, Log, TEXT("ImageName: %s"), *OutImageName);
				}
			}
		}
	}
	// if (DotIndex != INDEX_NONE)
	// {
	// 	FString IndexString = FrameName.Mid(DotIndex + 1);
	// 	OutIndex = FCString::Atoi(*IndexString);

	// 	// 레이어 이름 추출
	// 	int32 OpenParenIndex = FrameName.Find(TEXT("("), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	// 	int32 CloseParenIndex = FrameName.Find(TEXT(")"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	// 	if (OpenParenIndex != INDEX_NONE && CloseParenIndex != INDEX_NONE && OpenParenIndex < CloseParenIndex)
	// 	{
	// 		OutLayerName = FrameName.Mid(OpenParenIndex + 1, CloseParenIndex - OpenParenIndex - 1);
	// 	}

	// 	// 이미지 이름 추출
	// 	OutImageName = FrameName.Left(OpenParenIndex).TrimEnd();
	// }
}

static void FillTagName(const TArray<FFrameTag> &FrameTags, int32 Index, FString &OutTagName)
{
	// OutIndex가 -1이면 Tag가 없는 것이다.
	if (Index == -1)
	{
		return;
	}

	// Tag를 찾는다.
	for (const FFrameTag &FrameTag : FrameTags)
	{
		if (FrameTag.From <= Index && Index <= FrameTag.To)
		{
			// Tag를 찾았다.
			// Tag를 OutTagName에 넣어준다.
			OutTagName = FrameTag.Name;
			return;
		}
	}
}

bool ReadIntegerNoDefault(const TSharedPtr<class FJsonObject> Item, const FString &Key, int32 &Out_Value)
{
	if (Item->TryGetNumberField(Key, Out_Value))
	{
		return true;
	}
	else
	{
		Out_Value = 0;
		return false;
	}
}

bool ParseMetaBlock(const FString &NameForErrors, TSharedPtr<FJsonObject> &SpriteDescriptorObject, FString &OutImage, bool bSilent, TArray<FFrameTag> &OutFrameTags, TArray<FLayer> &OutLayers, TMap<FString, FString> &OutLayerToGroupMap)
{
	bool bLoadedSuccessfully = true;
	TSharedPtr<FJsonObject> MetaBlock = FPaperJSONHelpers::ReadObject(SpriteDescriptorObject, TEXT("meta"));
	if (MetaBlock.IsValid())
	{
		const FString AppName = FPaperJSONHelpers::ReadString(MetaBlock, TEXT("app"), TEXT(""));
		OutImage = FPaperJSONHelpers::ReadString(MetaBlock, TEXT("image"), TEXT(""));

		// Aseprite only
		OutFrameTags.Empty();
		TArray<TSharedPtr<FJsonValue>> FrameTagsArray = FPaperJSONHelpers::ReadArray(MetaBlock, TEXT("frameTags"));
		for (const TSharedPtr<FJsonValue> &FrameTagValue : FrameTagsArray)
		{
			TSharedPtr<FJsonObject> FrameTagObject = FrameTagValue->AsObject();
			FFrameTag NewFrameTag;
			NewFrameTag.Name = FPaperJSONHelpers::ReadString(FrameTagObject, TEXT("name"), TEXT(""));
			ReadIntegerNoDefault(FrameTagObject, TEXT("from"), NewFrameTag.From);
			ReadIntegerNoDefault(FrameTagObject, TEXT("to"), NewFrameTag.To);
			ReadIntegerNoDefault(FrameTagObject, TEXT("direction"), NewFrameTag.Direction);
			NewFrameTag.Color = FPaperJSONHelpers::ReadString(FrameTagObject, TEXT("color"), TEXT(""));
			OutFrameTags.Add(NewFrameTag);
		}

		OutLayers.Empty();
		OutLayerToGroupMap.Reset();
		TArray<TSharedPtr<FJsonValue>> LayersArray = FPaperJSONHelpers::ReadArray(MetaBlock, TEXT("layers"));
		for (const TSharedPtr<FJsonValue> &LayerValue : LayersArray)
		{
			TSharedPtr<FJsonObject> LayerObject = LayerValue->AsObject();
			FLayer NewLayer;
			NewLayer.Name = FPaperJSONHelpers::ReadString(LayerObject, TEXT("name"), TEXT(""));
			NewLayer.Group = FPaperJSONHelpers::ReadString(LayerObject, TEXT("group"), TEXT(""));
			ReadIntegerNoDefault(LayerObject, TEXT("opacity"), NewLayer.Opacity);

			// 2024.09.19 이렇게 하면 group도 하나의 Layer로 만들어진다. 현재는 사용을 안하니 상관은 없지만, 버그니 수정
			// BlendMode를 읽어서 레이어인지 그룹인지 구분 (그룹은 BlendMode가 비어있음)
			// Read BlendMode to distinguish between layers and groups (groups have empty BlendMode)
			auto blendMode = FPaperJSONHelpers::ReadString(LayerObject, TEXT("blendMode"), TEXT(""));
			NewLayer.BlendMode = blendMode.IsEmpty() ? TEXT("normal") : blendMode;
			if (blendMode.IsEmpty() == false)
			{
				// Group layer의 경우 이름만 가지고 있고 다른 정보가 없다 이를 활용해서 Group Layer를 구분할 수 있다.
				// 여기로 들어오면 Group Layer이다. 이럴 경우 OutLayerName에 추가하지 않는다.
				// 즉 BlendMode가 있는 경우에만 OutLayerName에 추가한다.
				// 추후 지원 고민
				OutLayers.Add(NewLayer);
				// 2024.09.19 그룹에 대한 처리는 추후 하더라도 레이어와 그룹에 대한 정보를 저장은 해두려고 함
				// 그리고 그룹을 서브 폴더로 만들어서 저장할 수 있도록 하려고 함
				OutLayerToGroupMap.Add(NewLayer.Name, NewLayer.Group);
			}
		}

		const FString FlashPrefix(TEXT("Adobe Flash"));
		const FString TexturePackerPrefix(TEXT("http://www.codeandweb.com/texturepacker"));
		const FString AsepritePrefix(TEXT("https://www.aseprite.org/"));

		// UE_LOG(LogAsepriteImporter, Log, TEXT("App Name is '%s'"), *AppName);
		if (AppName.StartsWith(FlashPrefix) || AppName.StartsWith(TexturePackerPrefix) || AppName.StartsWith(AsepritePrefix))
		{
			// Cool, we (mostly) know how to handle these sorts of files!
			if (!bSilent)
			{
				UE_LOG(LogAsepriteImporter, Log, TEXT("Parsing sprite sheet exported from '%s'"), *AppName);
			}
		}
		else if (!AppName.IsEmpty())
		{
			// It's got an app tag inside a meta block, so we'll take a crack at it
			if (!bSilent)
			{
				UE_LOG(LogAsepriteImporter, Warning, TEXT("Unexpected 'app' named '%s' while parsing sprite descriptor file '%s'.  Parsing will continue but the format may not be fully supported"), *AppName, *NameForErrors);
			}
		}
		else
		{
			// Probably not a sprite sheet
			if (!bSilent)
			{
				UE_LOG(LogAsepriteImporter, Warning, TEXT("Failed to parse sprite descriptor file '%s'.  Expected 'app' key indicating the exporter (might not be a sprite sheet)"), *NameForErrors);
			}
			bLoadedSuccessfully = false;
		}

		if (OutImage.IsEmpty())
		{
			if (!bSilent)
			{
				UE_LOG(LogAsepriteImporter, Warning, TEXT("Failed to parse sprite descriptor file '%s'.  Expected valid 'image' tag"), *NameForErrors);
			}
			bLoadedSuccessfully = false;
		}
	}
	else
	{
		if (!bSilent)
		{
			UE_LOG(LogAsepriteImporter, Warning, TEXT("Failed to parse sprite descriptor file '%s'.  Missing meta block"), *NameForErrors);
		}
		bLoadedSuccessfully = false;
	}
	return bLoadedSuccessfully;
}

static bool ParseFrame(TSharedPtr<FJsonObject> &FrameData, FSpriteFrame &OutFrame)
{
	bool bReadFrameSuccessfully = true;
	// An example frame:
	//   "frame": {"x":210,"y":10,"w":190,"h":223},
	//   "rotated": false,
	//   "trimmed": true,
	//   "spriteSourceSize": {"x":0,"y":11,"w":216,"h":240},
	//   "sourceSize": {"w":216,"h":240},
	//   "pivot": {"x":0.5,"y":0.5}			[optional]
	//   "duration": 100				[optional, in milliseconds, overrides global framerate if present] ASEPRITE ONLY

	OutFrame.bRotated = FPaperJSONHelpers::ReadBoolean(FrameData, TEXT("rotated"), false);
	OutFrame.bTrimmed = FPaperJSONHelpers::ReadBoolean(FrameData, TEXT("trimmed"), false);

	bReadFrameSuccessfully = bReadFrameSuccessfully && FPaperJSONHelpers::ReadIntegerNoDefault(FrameData, "duration", OutFrame.Duration);
	bReadFrameSuccessfully = bReadFrameSuccessfully && FPaperJSONHelpers::ReadRectangle(FrameData, "frame", /*out*/ OutFrame.SpritePosInSheet, /*out*/ OutFrame.SpriteSizeInSheet);

	if (OutFrame.bTrimmed)
	{
		bReadFrameSuccessfully = bReadFrameSuccessfully && FPaperJSONHelpers::ReadSize(FrameData, "sourceSize", /*out*/ OutFrame.ImageSourceSize);
		bReadFrameSuccessfully = bReadFrameSuccessfully && FPaperJSONHelpers::ReadRectangle(FrameData, "spriteSourceSize", /*out*/ OutFrame.SpriteSourcePos, /*out*/ OutFrame.SpriteSourceSize);
	}
	else
	{
		OutFrame.SpriteSourcePos = FIntPoint::ZeroValue;
		OutFrame.SpriteSourceSize = OutFrame.SpriteSizeInSheet;
		OutFrame.ImageSourceSize = OutFrame.SpriteSizeInSheet;
	}

	if (!FPaperJSONHelpers::ReadXY(FrameData, "pivot", /*out*/ OutFrame.Pivot))
	{
		OutFrame.Pivot.X = 0.5f;
		OutFrame.Pivot.Y = 0.5f;
	}

	// A few more prerequisites to sort out before rotation can be enabled
	if (OutFrame.bRotated)
	{
		// Sprite Source Pos is from top left, our pivot when rotated is bottom left
		OutFrame.SpriteSourcePos.Y = OutFrame.ImageSourceSize.Y - OutFrame.SpriteSourcePos.Y - OutFrame.SpriteSizeInSheet.Y;

		// We rotate final sprite geometry 90 deg CCW to fixup
		// These need to be swapped to be valid in texture space.
		Swap(OutFrame.SpriteSizeInSheet.X, OutFrame.SpriteSizeInSheet.Y);
		Swap(OutFrame.ImageSourceSize.X, OutFrame.ImageSourceSize.Y);

		Swap(OutFrame.SpriteSourcePos.X, OutFrame.SpriteSourcePos.Y);
		Swap(OutFrame.SpriteSourceSize.X, OutFrame.SpriteSourceSize.Y);
	}

	return bReadFrameSuccessfully;
}

static bool ParseFramesFromSpriteHash(TSharedPtr<FJsonObject> ObjectBlock, const TMap<FString, FString> &LayerToGroupMap, const TArray<FFrameTag> &InFrameTags, TArray<FSpriteFrame> &OutSpriteFrames, TSet<FName> &FrameNames)
{
	GWarn->BeginSlowTask(NSLOCTEXT("Paper2D", "PaperJsonImporterFactory_ParsingSpriteFrame", "Parsing Sprite Frame"), true, true);
	bool bLoadedSuccessfully = true;

	UAsepriteImporterSettings *Settings = GetMutableDefault<UAsepriteImporterSettings>();
	// Parse all of the frames
	int32 FrameCount = 0;
	for (auto FrameIt = ObjectBlock->Values.CreateIterator(); FrameIt; ++FrameIt)
	{
		GWarn->StatusUpdate(FrameCount, ObjectBlock->Values.Num(), NSLOCTEXT("Paper2D", "PaperJsonImporterFactory_ParsingSpriteFrames", "Parsing Sprite Frames"));

		bool bReadFrameSuccessfully = true;

		FSpriteFrame Frame;
		Frame.FrameName = *FrameIt.Key();
		// UE_LOG(LogAsepriteImporter, Log, TEXT("Parsing frame %s"), *Frame.FrameName.ToString());
		// 여기서 프레임에 맞는 Tag와 Layer를 찾아서 넣어준다.
		// Layer를 찾는 루틴
		FString OutImageName = TEXT("");
		int32 FrameIndex = -1;
		ExtractLayerName(Frame.FrameName.ToString(), OutImageName, Frame.LayerName, FrameIndex);
		// UE_LOG(LogAsepriteImporter, Log, TEXT("Layer Name is %s"), *Frame.LayerName);
		// UE_LOG(LogAsepriteImporter, Log, TEXT("Frame Index is %d"), FrameIndex);
		if (LayerToGroupMap.Contains(Frame.LayerName))
			Frame.LayerGroupName = LayerToGroupMap[Frame.LayerName];

		if (InFrameTags.Num() > 0)
		{
			// Tag를 찾는 루틴
			FillTagName(InFrameTags, FrameIndex, Frame.TagName);
			// UE_LOG(LogAsepriteImporter, Log, TEXT("Tag Name is %s"), *Frame.TagName);
		}

		if (FrameNames.Contains(Frame.FrameName))
		{
			bReadFrameSuccessfully = false;
		}
		else
		{
			FrameNames.Add(Frame.FrameName);
		}

		TSharedPtr<FJsonValue> FrameDataAsValue = FrameIt.Value();
		TSharedPtr<FJsonObject> FrameData;
		if (FrameDataAsValue->Type == EJson::Object)
		{
			FrameData = FrameDataAsValue->AsObject();
			bReadFrameSuccessfully = bReadFrameSuccessfully && ParseFrame(FrameData, /*out*/ Frame);
		}
		else
		{
			bReadFrameSuccessfully = false;
		}

		if (bReadFrameSuccessfully)
		{
			OutSpriteFrames.Add(Frame);
		}
		else
		{
			UE_LOG(LogAsepriteImporter, Warning, TEXT("Frame %s is in an unexpected format"), *Frame.FrameName.ToString());
			bLoadedSuccessfully = false;
		}

		FrameCount++;
	}

	GWarn->EndSlowTask();
	return bLoadedSuccessfully;
}

// static bool ParseFramesFromSpriteArray(const TArray<TSharedPtr<FJsonValue>> &ArrayBlock, TArray<FSpriteFrame> &OutSpriteFrames, TSet<FName> &FrameNames)
// {
// 	GWarn->BeginSlowTask(NSLOCTEXT("Paper2D", "PaperJsonImporterFactory_ParsingSpriteFrame", "Parsing Sprite Frame"), true, true);
// 	bool bLoadedSuccessfully = true;

// 	// Parse all of the frames
// 	for (int32 FrameCount = 0; FrameCount < ArrayBlock.Num(); ++FrameCount)
// 	{
// 		GWarn->StatusUpdate(FrameCount, ArrayBlock.Num(), NSLOCTEXT("Paper2D", "PaperJsonImporterFactory_ParsingSpriteFrames", "Parsing Sprite Frames"));
// 		bool bReadFrameSuccessfully = true;
// 		FSpriteFrame Frame;

// 		const TSharedPtr<FJsonValue> &FrameDataAsValue = ArrayBlock[FrameCount];
// 		if (FrameDataAsValue->Type == EJson::Object)
// 		{
// 			TSharedPtr<FJsonObject> FrameData;
// 			FrameData = FrameDataAsValue->AsObject();

// 			FString FrameFilename = FPaperJSONHelpers::ReadString(FrameData, TEXT("filename"), TEXT(""));
// 			if (!FrameFilename.IsEmpty())
// 			{
// 				Frame.FrameName = FName(*FrameFilename); // case insensitive
// 				if (FrameNames.Contains(Frame.FrameName))
// 				{
// 					bReadFrameSuccessfully = false;
// 				}
// 				else
// 				{
// 					FrameNames.Add(Frame.FrameName);
// 				}

// 				bReadFrameSuccessfully = bReadFrameSuccessfully && ParseFrame(FrameData, /*out*/ Frame);
// 			}
// 			else
// 			{
// 				bReadFrameSuccessfully = false;
// 			}
// 		}
// 		else
// 		{
// 			bReadFrameSuccessfully = false;
// 		}

// 		if (bReadFrameSuccessfully)
// 		{
// 			OutSpriteFrames.Add(Frame);
// 		}
// 		else
// 		{
// 			UE_LOG(LogAsepriteImporter, Warning, TEXT("Frame %s is in an unexpected format"), *Frame.FrameName.ToString());
// 			bLoadedSuccessfully = false;
// 		}
// 	}

// 	GWarn->EndSlowTask();
// 	return bLoadedSuccessfully;
// }

static ESpritePivotMode::Type GetBestPivotType(FVector2D JsonPivot)
{
	// Not assuming layout of ESpritePivotMode
	if (JsonPivot.X == 0 && JsonPivot.Y == 0)
	{
		return ESpritePivotMode::Top_Left;
	}
	else if (JsonPivot.X == 0.5f && JsonPivot.Y == 0)
	{
		return ESpritePivotMode::Top_Center;
	}
	else if (JsonPivot.X == 1.0f && JsonPivot.Y == 0)
	{
		return ESpritePivotMode::Top_Right;
	}
	else if (JsonPivot.X == 0 && JsonPivot.Y == 0.5f)
	{
		return ESpritePivotMode::Center_Left;
	}
	else if (JsonPivot.X == 0.5f && JsonPivot.Y == 0.5f)
	{
		return ESpritePivotMode::Center_Center;
	}
	else if (JsonPivot.X == 1.0f && JsonPivot.Y == 0.5f)
	{
		return ESpritePivotMode::Center_Right;
	}
	else if (JsonPivot.X == 0 && JsonPivot.Y == 1.0f)
	{
		return ESpritePivotMode::Bottom_Left;
	}
	else if (JsonPivot.X == 0.5f && JsonPivot.Y == 1.0f)
	{
		return ESpritePivotMode::Bottom_Center;
	}
	else if (JsonPivot.X == 1.0f && JsonPivot.Y == 1.0f)
	{
		return ESpritePivotMode::Bottom_Right;
	}
	else
	{
		return ESpritePivotMode::Custom;
	}
}

////////////////////////////////////////////
// FAsepriteJsonImporter

FAsepriteJsonImporter::FAsepriteJsonImporter()
	: ImageTexture(nullptr), NormalMapTexture(nullptr), bIsReimporting(false), ExistingBaseTextureName(), ExistingBaseTexture(nullptr), ExistingNormalMapTextureName(), ExistingNormalMapTexture(nullptr),
	  bIsReimportingFlipbooks(false) // Aseprite only
{
}

void FAsepriteJsonImporter::SetReimportData(const TArray<FString> &ExistingSpriteNames, const TArray<TSoftObjectPtr<class UPaperSprite>> &ExistingSpriteSoftPtrs)
{
	check(ExistingSpriteNames.Num() == ExistingSpriteSoftPtrs.Num());
	if (ExistingSpriteNames.Num() == ExistingSpriteSoftPtrs.Num())
	{
		for (int i = 0; i < ExistingSpriteSoftPtrs.Num(); ++i)
		{
			const TSoftObjectPtr<class UPaperSprite> SpriteSoftPtr = ExistingSpriteSoftPtrs[i];
			UPaperSprite *LoadedSprite = SpriteSoftPtr.LoadSynchronous();
			if (LoadedSprite != nullptr)
			{
				ExistingSprites.Add(ExistingSpriteNames[i], LoadedSprite);
			}
		}
	}
	bIsReimporting = true;
}

bool FAsepriteJsonImporter::Import(TSharedPtr<FJsonObject> SpriteDescriptorObject, const FString &NameForErrors, bool bSilent)
{
	bool bLoadedSuccessfully = ParseMetaBlock(NameForErrors, SpriteDescriptorObject, /*out*/ ImageName, bSilent, /*out*/ FrameTags, /*out*/ Layers, /*out*/ LayerToGroupMap);
	if (bLoadedSuccessfully)
	{
		TSharedPtr<FJsonObject> ObjectFrameBlock = FPaperJSONHelpers::ReadObject(SpriteDescriptorObject, TEXT("frames"));
		TSet<FName> FrameNames; // 중복을 체크하기 위한 Set

		if (ObjectFrameBlock.IsValid())
		{
			bLoadedSuccessfully = ParseFramesFromSpriteHash(ObjectFrameBlock, LayerToGroupMap, FrameTags, /*out*/ Frames, /*inout*/ FrameNames);
		}
		else
		{
			UE_LOG(LogAsepriteImporter, Warning, TEXT("Invalid Case"));

			// 2024.03.04 애초에 hash로 export하기 때문에 이 부분은 실행되지 않는다.
			// Try loading as an array
			// TArray<TSharedPtr<FJsonValue>> ArrayBlock = FPaperJSONHelpers::ReadArray(SpriteDescriptorObject, TEXT("frames"));
			// if (ArrayBlock.Num() > 0)
			// {
			// 	bLoadedSuccessfully = ParseFramesFromSpriteArray(ArrayBlock, /*out*/ Frames, /*inout*/ FrameNames);
			// }
			// else
			// {
			// 	if (!bSilent)
			// 	{
			// 		UE_LOG(LogAsepriteImporter, Warning, TEXT("Failed to parse sprite descriptor file '%s'.  Missing frames block"), *NameForErrors);
			// 	}
			// 	bLoadedSuccessfully = false;
			// }
		}

		if (bLoadedSuccessfully && (Frames.Num() == 0))
		{
			if (!bSilent)
			{
				UE_LOG(LogAsepriteImporter, Warning, TEXT("Failed to parse sprite descriptor file '%s'.  No frames loaded"), *NameForErrors);
			}
			bLoadedSuccessfully = false;
		}
	}
	return bLoadedSuccessfully;
}

bool FAsepriteJsonImporter::CanImportJSON(const FString &FileContents)
{
	TSharedPtr<FJsonObject> SpriteDescriptorObject = ParseJSON(FileContents, FString(), /*bSilent=*/true);
	if (SpriteDescriptorObject.IsValid())
	{
		FString Unused;
		TArray<FFrameTag> FrameTags; // only for check if it's a valid file
		TArray<FLayer> Layers;		 // only for check if it's a valid file
		TMap<FString, FString> LayerToGroupMap;
		return ParseMetaBlock(FString(), SpriteDescriptorObject, /*out*/ Unused, /*bSilent=*/true, FrameTags, Layers, LayerToGroupMap);
	}

	return false;
}

bool FAsepriteJsonImporter::ImportFromString(const FString &FileContents, const FString &NameForErrors, bool bSilent)
{
	TSharedPtr<FJsonObject> SpriteDescriptorObject = ParseJSON(FileContents, NameForErrors, bSilent);
	return SpriteDescriptorObject.IsValid() && Import(SpriteDescriptorObject, NameForErrors, bSilent);
}

bool FAsepriteJsonImporter::ImportFromArchive(FArchive *Archive, const FString &NameForErrors, bool bSilent)
{
	TSharedPtr<FJsonObject> SpriteDescriptorObject = ParseJSON(Archive, NameForErrors, bSilent);
	return SpriteDescriptorObject.IsValid() && Import(SpriteDescriptorObject, NameForErrors, bSilent);
}

bool FAsepriteJsonImporter::ImportTextures(const FString &LongPackagePath, const FString &SourcePath)
{
	bool bLoadedSuccessfully = true;
	const FString TargetSubPath = LongPackagePath / TEXT("Textures");

	// Load the base texture
	const FString SourceSheetImageFilename = FPaths::Combine(*SourcePath, *ImageName);
	ImageTexture = ImportOrReimportTexture((bIsReimporting && (ExistingBaseTextureName == ImageName)) ? ExistingBaseTexture : nullptr, SourceSheetImageFilename, TargetSubPath);
	if (ImageTexture == nullptr)
	{
		UE_LOG(LogAsepriteImporter, Warning, TEXT("Failed to import sprite sheet image '%s'."), *SourceSheetImageFilename);
		bLoadedSuccessfully = false;
	}

	// Try reimporting the normal map
	// Note: We are checking to see if the *base* texture has been renamed, since the .JSON doesn't actually store a name for the normal map.
	// If the base name has changed, we start from scratch for the normal map too, rather than reimport it even if the old computed one still exists
	if (bIsReimporting && (ExistingBaseTextureName == ImageName) && (ExistingNormalMapTexture != nullptr))
	{
		if (FReimportManager::Instance()->Reimport(ExistingNormalMapTexture, /*bAskForNewFileIfMissing=*/true))
		{
			NormalMapTexture = ExistingNormalMapTexture;
			ComputedNormalMapName = ExistingNormalMapTextureName;
		}
	}

	// If we weren't reimporting (or failed the reimport), try scanning for a normal map (which may not exist, and that is not an error)
	if (NormalMapTexture == nullptr)
	{
		const UPaperImporterSettings *ImporterSettings = GetDefault<UPaperImporterSettings>();

		// Create a list of names to test of the form [ImageName-[BaseMapSuffix]][NormalMapSuffix] or [ImageName][NormalMapSuffix], preferring the former
		const FString ImageNameNoExtension = FPaths::GetBaseFilename(ImageName);
		const FString ImageTypeExtension = FPaths::GetExtension(ImageName, /*bIncludeDot=*/true);
		const FString NormalMapNameNoSuffix = ImporterSettings->RemoveSuffixFromBaseMapName(ImageNameNoExtension);

		TArray<FString> NamesToTest;
		ImporterSettings->GenerateNormalMapNamesToTest(NormalMapNameNoSuffix, /*inout*/ NamesToTest);
		ImporterSettings->GenerateNormalMapNamesToTest(ImageNameNoExtension, /*inout*/ NamesToTest);

		// Test each name for a file we can try to import
		for (const FString &NameToTestNoExtension : NamesToTest)
		{
			const FString NameToTest = NameToTestNoExtension + ImageTypeExtension;
			const FString NormalMapSourceImageFilename = FPaths::Combine(*SourcePath, *NameToTest);

			if (FPaths::FileExists(NormalMapSourceImageFilename))
			{
				NormalMapTexture = ImportTexture(NormalMapSourceImageFilename, TargetSubPath);
				if (NormalMapTexture != nullptr)
				{
					ComputedNormalMapName = NameToTest;
				}
				break;
			}
		}
	}

	return bLoadedSuccessfully;
}

UTexture2D *FAsepriteJsonImporter::ImportOrReimportTexture(UTexture2D *ExistingTexture, const FString &TextureSourcePath, const FString &DestinationAssetFolder)
{
	UTexture2D *ResultTexture = nullptr;

	// Try reimporting if we have an existing texture
	if (ExistingTexture != nullptr)
	{
		if (FReimportManager::Instance()->Reimport(ExistingTexture, /*bAskForNewFileIfMissing=*/true))
		{
			ResultTexture = ExistingTexture;
		}
	}

	// If that fails, import the original textures
	if (ResultTexture == nullptr)
	{
		ResultTexture = ImportTexture(TextureSourcePath, DestinationAssetFolder);
	}

	return ResultTexture;
}

UTexture2D *FAsepriteJsonImporter::ImportTexture(const FString &TextureSourcePath, const FString &DestinationAssetFolder)
{
	FAssetToolsModule &AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");

	TArray<FString> TextureFileNames;
	TextureFileNames.Add(TextureSourcePath);
	TArray<UObject *> ImportedSheets = AssetToolsModule.Get().ImportAssets(TextureFileNames, DestinationAssetFolder);

	UTexture2D *ImportedTexture = (ImportedSheets.Num() > 0) ? Cast<UTexture2D>(ImportedSheets[0]) : nullptr;

	if (ImportedTexture != nullptr)
	{
		// Change the compression settings
		GetDefault<UPaperImporterSettings>()->ApplyTextureSettings(ImportedTexture);
	}

	return ImportedTexture;
}

UPaperSprite *FAsepriteJsonImporter::FindExistingSprite(const FString &Name)
{
	return ExistingSprites.FindRef(Name);
}

bool FAsepriteJsonImporter::PerformImport(const FString &LongPackagePath, EObjectFlags Flags, UAseprite *SpriteSheet)
{
	FAssetToolsModule &AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");
	UAsepriteImporterSettings *Settings = GetMutableDefault<UAsepriteImporterSettings>();

	GWarn->BeginSlowTask(NSLOCTEXT("Paper2D", "PaperJsonImporterFactory_ImportingSpriteFrame", "Importing Sprite Frame"), true, true);
	for (int32 FrameIndex = 0; FrameIndex < Frames.Num(); ++FrameIndex)
	{
		GWarn->StatusUpdate(FrameIndex, Frames.Num(), NSLOCTEXT("Paper2D", "PaperJsonImporterFactory_ImportingSpriteFrames", "Importing Sprite Frames"));

		// Check for user canceling the import
		if (GWarn->ReceivedUserCancel())
		{
			break;
		}

		const FSpriteFrame &Frame = Frames[FrameIndex];

		// Create a package for the frame
		const FString TargetSubPath = LongPackagePath + TEXT("/Frames");

		UObject *OuterForFrame = nullptr; // @TODO: Use this if we don't want them to be individual assets - Flipbook;

		// Create a frame in the package
		UPaperSprite *TargetSprite = nullptr;

		if (bIsReimporting)
		{
			TargetSprite = FindExistingSprite(Frame.FrameName.ToString());
		}

		if (TargetSprite == nullptr)
		{
			const FString SanitizedFrameName = ObjectTools::SanitizeObjectName(Frame.FrameName.ToString());
			const FString TentativePackagePath = UPackageTools::SanitizePackageName(TargetSubPath + TEXT("/") + SanitizedFrameName);
			FString DefaultSuffix;
			FString AssetName;
			FString PackageName;
			AssetToolsModule.Get().CreateUniqueAssetName(TentativePackagePath, DefaultSuffix, /*out*/ PackageName, /*out*/ AssetName);

			// Create a unique package name and asset name for the frame
			OuterForFrame = CreatePackage(*PackageName);

			// Create the asset
			TargetSprite = NewObject<UPaperSprite>(OuterForFrame, *AssetName, Flags);
			FAssetRegistryModule::AssetCreated(TargetSprite);
		}

		TargetSprite->Modify();
		FSpriteAssetInitParameters SpriteInitParams;
		SpriteInitParams.Texture = ImageTexture;

		if (NormalMapTexture != nullptr)
		{
			// Put the normal map into the additional textures array and ask for a lit material instead of unlit
			SpriteInitParams.AdditionalTextures.Add(NormalMapTexture);
		}

		GetDefault<UPaperImporterSettings>()->ApplySettingsForSpriteInit(/*inout*/ SpriteInitParams, (NormalMapTexture != nullptr) ? ESpriteInitMaterialLightingMode::ForceLit : ESpriteInitMaterialLightingMode::Automatic);

		// Apply material only for new imports, not for re-imports
		// Re-imports should preserve user's current material settings
		if (!bIsReimporting)
		{
			TSoftObjectPtr<UMaterialInterface> SpriteMaterial = Settings->SpriteMaterial;
			if (!SpriteMaterial.IsNull())
			{
				UMaterialInterface *LoadedMaterial = SpriteMaterial.LoadSynchronous();
				if (LoadedMaterial)
				{
					SpriteInitParams.DefaultMaterialOverride = LoadedMaterial;
					UE_LOG(LogAsepriteImporter, Log, TEXT("Applied material for new import: %s"), *LoadedMaterial->GetName());
				}
			}
		}
		else
		{
			UE_LOG(LogAsepriteImporter, Log, TEXT("Re-import: Preserving existing material settings for sprite: %s"), *TargetSprite->GetName());
		}
		SpriteInitParams.Offset = Frame.SpritePosInSheet;
		SpriteInitParams.Dimension = Frame.SpriteSizeInSheet;

		// Re-import: Preserve user-modified PPU and Pivot settings
		float PixelsPerUnit = Settings->PixelPerUnit;
		ESpritePivotMode::Type PivotType = GetBestPivotType(Settings->PivotType);
		FVector2D TextureSpacePivotPoint = FVector2D::ZeroVector;

		if (bIsReimporting)
		{
			// Try to find existing sprite to preserve user modifications
			UPaperSprite* ExistingSprite = FindExistingSprite(Frame.FrameName.ToString());
			if (ExistingSprite)
			{
				// Preserve PPU from existing sprite
				PixelsPerUnit = ExistingSprite->GetPixelsPerUnrealUnit();

				// Preserve pivot from existing sprite using reflection (properties are protected)
				if (FProperty* PivotModeProperty = FindFProperty<FProperty>(UPaperSprite::StaticClass(), TEXT("PivotMode")))
				{
					TEnumAsByte<ESpritePivotMode::Type>* PivotModePtr = PivotModeProperty->ContainerPtrToValuePtr<TEnumAsByte<ESpritePivotMode::Type>>(ExistingSprite);
					if (PivotModePtr)
					{
						PivotType = PivotModePtr->GetValue();
					}
				}

				if (FProperty* CustomPivotProperty = FindFProperty<FProperty>(UPaperSprite::StaticClass(), TEXT("CustomPivotPoint")))
				{
					FVector2D* CustomPivotPtr = CustomPivotProperty->ContainerPtrToValuePtr<FVector2D>(ExistingSprite);
					if (CustomPivotPtr)
					{
						TextureSpacePivotPoint = *CustomPivotPtr;
					}
				}
			}
		}

		SpriteInitParams.SetPixelsPerUnrealUnit(PixelsPerUnit);

		TargetSprite->InitializeSprite(SpriteInitParams, false);

		TargetSprite->SetRotated(Frame.bRotated, false);
		TargetSprite->SetTrim(Frame.bTrimmed, Frame.SpriteSourcePos, Frame.ImageSourceSize, false);

		// Set up pivot using preserved or default settings
		TargetSprite->SetPivotMode(PivotType, TextureSpacePivotPoint, false);

		TargetSprite->RebuildData();

		// Create the entry in the animation
		SpriteSheet->SpriteNames.Add(Frame.FrameName.ToString());
		SpriteSheet->SpriteFrameDurations.Add(Frame.Duration);
		SpriteSheet->Sprites.Add(TargetSprite);

		TargetSprite->PostEditChange();
	}

	const int32 ImportBehaviorVersion = bIsReimporting ? ExistingImportSettings.ImportBehaviorVersion : AsepriteImportBehaviorVersion::Current;
	const bool bUseCustomFlipbookNaming = ImportBehaviorVersion >= AsepriteImportBehaviorVersion::CustomFlipbookNaming;
	bool bSkipEmptyTagFramesWhenCreateFlipbooks = bIsReimporting ? ExistingImportSettings.bSkipEmptyTagFramesWhenCreateFlipbooks : Settings->bSkipEmptyTagFramesWhenCreateFlipbooks;
	const FString FlipbookLayerSeparator = bUseCustomFlipbookNaming ? (bIsReimporting ? ExistingImportSettings.FlipbookLayerSeparator : Settings->FlipbookLayerSeparator) : TEXT("_L_");
	const FString FlipbookTagSeparator = bUseCustomFlipbookNaming ? (bIsReimporting ? ExistingImportSettings.FlipbookTagSeparator : Settings->FlipbookTagSeparator) : TEXT("_T_");
	FString BaseName = FPaths::GetBaseFilename(ImageName);

	for (auto index = 0; index < Frames.Num(); ++index)
	{
		auto LayerName = Frames[index].LayerName;
		auto TagName = Frames[index].TagName;

		// Layer Group을 사용하기 위해서 Layer Names를 부활 시키고 TagNames와 항상 같은 배열 크기를
		// 가지도록 한다. TagNames가 없는 경우에는 LayerNames에는 빈 문자열을 넣는다.
		if (LayerName.IsEmpty())
		{
			if (TagName.IsEmpty())
			{
				if (bSkipEmptyTagFramesWhenCreateFlipbooks == false)
				{
					SpriteSheet->TagNames.Add(index, BaseName + TEXT("_") + FString::FromInt(index));
					SpriteSheet->LayerNames.Add(index, TEXT(""));
				}
			}
			else
			{
				SpriteSheet->TagNames.Add(index, BaseName + TEXT("_") + TagName);
				SpriteSheet->LayerNames.Add(index, TEXT(""));
			}
		}
		else
		{
			if (TagName.IsEmpty())
			{
				if (bSkipEmptyTagFramesWhenCreateFlipbooks == false)
				{
					SpriteSheet->TagNames.Add(index, BaseName + TEXT("_") + FString::FromInt(index));
					SpriteSheet->LayerNames.Add(index, LayerName);
				}
			}
			else
			{
				const FString FlipbookName = MakeLayerTagFlipbookName(BaseName, LayerName, TagName, FlipbookLayerSeparator, FlipbookTagSeparator);
				SpriteSheet->TagNames.Add(index, FlipbookName);
				SpriteSheet->LayerNames.Add(index, LayerName);
			}
		}
	}

	SpriteSheet->LayerToGroupMap = LayerToGroupMap;
	SpriteSheet->TextureName = ImageName;
	SpriteSheet->Texture = ImageTexture;
	SpriteSheet->FrameWidth = Frames[0].SpriteSizeInSheet.X;
	SpriteSheet->FrameHeight = Frames[0].SpriteSizeInSheet.Y;
	SpriteSheet->NormalMapTextureName = ComputedNormalMapName;
	SpriteSheet->NormalMapTexture = NormalMapTexture;

	GWarn->EndSlowTask();
	return true;
}

FString FAsepriteJsonImporter::ExtractSpriteSheet(FString InFilename)
{
	UAsepriteImporterSettings *Settings = GetMutableDefault<UAsepriteImporterSettings>();
	FString AsepriteExecutablePath = Settings->AsepriteExecutablePath;

	// 경로 유효성 검증: 비어있거나 파일이 존재하지 않는 경우
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	bool bPathValid = !AsepriteExecutablePath.IsEmpty() &&
					  PlatformFile.FileExists(*AsepriteExecutablePath) &&
					  FPaths::GetCleanFilename(AsepriteExecutablePath).Equals(TEXT("Aseprite.exe"), ESearchCase::IgnoreCase);

	if (!bPathValid)
	{
		// 새로운 Slate 다이얼로그로 경로 설정
		TSharedRef<SAsepritePathDialog> PathDialog = SNew(SAsepritePathDialog)
			.CurrentPath(AsepriteExecutablePath);

		bool bUserAccepted = PathDialog->Show();

		if (!bUserAccepted)
		{
			UE_LOG(LogAsepriteImporter, Warning, TEXT("User cancelled Aseprite executable configuration."));
			return TEXT("");
		}

		// 사용자가 선택한 경로 가져오기
		FString SelectedPath = PathDialog->GetSelectedPath();

		// Settings에 경로 저장
		Settings->AsepriteExecutablePath = SelectedPath;
		Settings->SaveConfig();
		AsepriteExecutablePath = SelectedPath;

		UE_LOG(LogAsepriteImporter, Log, TEXT("Aseprite executable path configured: %s"), *AsepriteExecutablePath);
	}

	FString AsepriteFilePath = InFilename;
	UE_LOG(LogAsepriteImporter, Warning, TEXT("ChosenFilePath: %s"), *AsepriteFilePath);
	UE_LOG(LogAsepriteImporter, Warning, TEXT("Aseprite Path: %s"), *AsepriteExecutablePath);

	// 2023.09.25 임시 경로의 경우 import할 때마다 경로가 변경되다 보니 reimport가 json이 다른 파일로 처리된다
	// PaperSpriteSheet 이슈긴 한데 일리가 있으므로 json파일 자체를 제대로 옮겨오자
	// Content / Aseprite / 파일명 / 파일명.json
	FString UserDir = FPlatformProcess::UserDir();
	FString AsepriteFolderPath = FPaths::Combine(*UserDir, TEXT("AsepriteImporter"));
	if (!PlatformFile.DirectoryExists(*AsepriteFolderPath))
	{
		PlatformFile.CreateDirectory(*AsepriteFolderPath);
	}
	FString FileNameWithoutExt = FPaths::GetBaseFilename(AsepriteFilePath);
	FString OutputSpriteSheet = FPaths::Combine(AsepriteFolderPath, FileNameWithoutExt + TEXT(".png"));
	FString OutputJson = FPaths::Combine(AsepriteFolderPath, FileNameWithoutExt + TEXT(".json"));

	// FString TempDir = FPaths::CreateTempFilename(*FPaths::ProjectIntermediateDir());
	// FString FileNameWithoutExt = FPaths::GetBaseFilename(AsepriteFilePath);
	// FString OutputSpriteSheet = FPaths::Combine(TempDir, FileNameWithoutExt.Replace(TEXT(" "), TEXT("_")) + TEXT(".png"));
	// FString OutputJson = FPaths::Combine(TempDir, FileNameWithoutExt.Replace(TEXT(" "), TEXT("_")) + TEXT(".json"));

	bool bSplitLayers = bIsReimporting ? ExistingImportSettings.bSplitLayers : GetDefault<UAsepriteImporterSettings>()->bSplitLayers;
	FString SplitLayer = bSplitLayers ? FString("--split-layers") : FString("");
	FString ListLayer = bSplitLayers ? FString("--list-layers") : FString("");

	FString CommandLine = FString::Printf(TEXT("-b  --sheet \"%s\" --sheet-type packed \"%s\" --format json-hash \"%s\" --list-tags --list-slices --data \"%s\" \"%s\""), *OutputSpriteSheet, *SplitLayer, *ListLayer, *OutputJson, *AsepriteFilePath);

	int32 ReturnCode;
	FString StdOut;
	FString StdErr;

	FPlatformProcess::ExecProcess(*AsepriteExecutablePath, *CommandLine, &ReturnCode, &StdOut, &StdErr);

	// UE_LOG(LogAsepriteImporter, Warning, TEXT("StdErr: %s"), *StdErr);
	// UE_LOG(LogAsepriteImporter, Warning, TEXT("StdOut: %s"), *StdOut);
	// UE_LOG(LogAsepriteImporter, Warning, TEXT("ReturnCode: %d"), ReturnCode);

	if (ReturnCode != 0)
	{
		FText DialogTitle = FText::FromString("Aseprite Error");
		FText DialogText = FText::FromString("Aseprite encountered an error. Please check the log for more details.");
		FMessageDialog::Open(EAppMsgType::Ok, DialogText, DialogTitle);
		return TEXT("");
	}

	return OutputJson;
}

// Aseprite only
void FAsepriteJsonImporter::SetReimportFlipbookData(const TArray<FString> &ExistingSpriteNames, const TArray<TSoftObjectPtr<class UPaperFlipbook>> &ExistingFlipbooksSoftPtrs)
{
	UE_LOG(LogAsepriteImporter, Log, TEXT("SetReimportFlipbookData called with %d existing flipbooks"), ExistingFlipbooksSoftPtrs.Num());
	
	if (ExistingFlipbooksSoftPtrs.Num() == 0)
	{
		UE_LOG(LogAsepriteImporter, Log, TEXT("Skip Reimport Flipbooks - No existing flipbooks found."));
		bIsReimportingFlipbooks = false;
		return;
	}

	// int32 TotalFrames = 0;
	// for (auto Flipbook : ExistingFlipbooksSoftPtrs)
	// {
	// 	TotalFrames += Flipbook->GetNumKeyFrames();
	// }
	// if (TotalFrames == ExistingSpriteNames.Num())
	// {
	// 	for (int i = 0; i < ExistingFlipbooksSoftPtrs.Num(); ++i)
	// 	{
	// 		const TSoftObjectPtr<class UPaperFlipbook> FlipbookSoftPtr = ExistingFlipbooksSoftPtrs[i];
	// 		UPaperFlipbook *LoadedFlipbook = FlipbookSoftPtr.LoadSynchronous();
	// 		if (LoadedFlipbook != nullptr)
	// 		{
	// 			ExistingFlipbooks.Add(LoadedFlipbook);
	// 		}
	// 	}
	// }
	// 2024.05.20 check를 제거하는 이유는 이제 옵션에 따라 스킵하기도 하므로
	// 항상 전체 사이즈가 같을 필요가 없다.
	for (int i = 0; i < ExistingFlipbooksSoftPtrs.Num(); ++i)
	{
		const TSoftObjectPtr<class UPaperFlipbook> FlipbookSoftPtr = ExistingFlipbooksSoftPtrs[i];
		UPaperFlipbook *LoadedFlipbook = FlipbookSoftPtr.LoadSynchronous();
		if (LoadedFlipbook != nullptr)
		{
			ExistingFlipbooks.Add(FlipbookSoftPtr);  // Add the TSoftObjectPtr, not the raw pointer
		}
	}

	bIsReimportingFlipbooks = true;
}

void FAsepriteJsonImporter::ExecuteCreateFlipbooks(TWeakObjectPtr<UAseprite> Object)
{
	UAseprite *SpriteSheet = Object.Get();
	if (SpriteSheet != nullptr)
	{
		UAsepriteImporterSettings *Settings = GetMutableDefault<UAsepriteImporterSettings>();

		uint32 FlipbookFramePerSecond = bIsReimporting ? ExistingImportSettings.FlipbookFramePerSecond : Settings->FlipbookFramePerSecond;

		FAssetToolsModule &AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");
		FContentBrowserModule &ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

		check(SpriteSheet->SpriteNames.Num() == SpriteSheet->Sprites.Num());
		bool useSpriteNames = (SpriteSheet->SpriteNames.Num() == SpriteSheet->Sprites.Num());

		// Create a list of sprites and sprite names to feed into paper flipbook helpers
		TMap<FString, TArray<UPaperSprite *>> SpriteFlipbookMap;
		{
			TArray<UPaperSprite *> Sprites;
			TArray<FString> SpriteNames;
			TMap<int32, FString> TagNames;
			TMap<int32, FString> LayerNames;

			for (int SpriteIndex = 0; SpriteIndex < SpriteSheet->Sprites.Num(); ++SpriteIndex)
			{
				TSoftObjectPtr<class UPaperSprite> SpriteSoftPtr = SpriteSheet->Sprites[SpriteIndex];
				UPaperSprite *Sprite = SpriteSoftPtr.LoadSynchronous();
				if (Sprite != nullptr)
				{
					const FString SpriteName = useSpriteNames ? SpriteSheet->SpriteNames[SpriteIndex] : Sprite->GetName();

					Sprites.Add(Sprite);
					SpriteNames.Add(SpriteName);
				}
			}

			TagNames = SpriteSheet->TagNames;
			LayerNames = SpriteSheet->LayerNames;
			FString BaseName = FPaths::GetBaseFilename(SpriteSheet->AssetImportData->GetFirstFilename());
			// for (auto &Layer : LayerNames)
			// {
			// 	UE_LOG(LogTemp, Warning, TEXT("LAYER_NAME_CHECLK_TYPEACTION: %d, %s"), Layer.Key, *Layer.Value);
			// }
			// for (auto &Tag : TagNames)
			// {
			// 	UE_LOG(LogTemp, Warning, TEXT("TAG_NAME_CHECLK_TYPEACTION: %d, %s"), Tag.Key, *Tag.Value);
			// }
			ExtractFlipbooksFromSprites(/*out*/ SpriteFlipbookMap, BaseName, Sprites, SpriteNames, TagNames, LayerNames);
		}

		if (SpriteFlipbookMap.Num() > 0)
		{
			UPaperFlipbookFactory *FlipbookFactory = NewObject<UPaperFlipbookFactory>();

			GWarn->BeginSlowTask(NSLOCTEXT("Paper2D", "Paper2D_CreateFlipbooks", "Creating flipbooks from selection"), true, true);

			int Progress = 0;
			int TotalProgress = SpriteFlipbookMap.Num();
			TArray<UObject *> ObjectsToSync;

			for (auto It : SpriteFlipbookMap)
			{
				GWarn->UpdateProgress(Progress++, TotalProgress);

				const FString &FlipbookName = It.Key;
				auto FlipbookFirstTagIndexPtr = SpriteSheet->TagNames.FindKey(FlipbookName);
				FString SubFolderPath = TEXT("");
				
				// Debug: Log re-import status for each flipbook
				// UE_LOG(LogAsepriteImporter, Verbose, TEXT("Processing Flipbook: '%s' (bIsReimporting: %s, bIsReimportingFlipbooks: %s, ExistingFlipbooks.Num(): %d)"), 
				//	*FlipbookName, bIsReimporting ? TEXT("TRUE") : TEXT("FALSE"), 
				//	bIsReimportingFlipbooks ? TEXT("TRUE") : TEXT("FALSE"), ExistingFlipbooks.Num());

				if (FlipbookFirstTagIndexPtr != nullptr)
				{
					const int32 FlipbookFirstTagIndex = *FlipbookFirstTagIndexPtr;
					const FString LayerName = SpriteSheet->LayerNames[FlipbookFirstTagIndex];
					if (SpriteSheet->LayerToGroupMap.Contains(LayerName))
					{
						const FString GroupName = SpriteSheet->LayerToGroupMap[LayerName];
						if (GroupName.IsEmpty() == false)
						{
							SubFolderPath = GroupName.IsEmpty() ? TEXT("") : GroupName + TEXT("/");
						}
					}
				}
				UE_LOG(LogTemp, Warning, TEXT("SubFolderPath: %s"), *SubFolderPath);
				TArray<UPaperSprite *> &Sprites = It.Value;

				FlipbookFactory->KeyFrames.Empty();
				
				// Calculate base FPS from this animation's first frame when using Aseprite timing
				float BaseFPS = static_cast<float>(FlipbookFramePerSecond); // Default to user setting
				
				for (int32 SpriteIndex = 0; SpriteIndex < Sprites.Num(); ++SpriteIndex)
				{
					FPaperFlipbookKeyFrame *KeyFrame = new (FlipbookFactory->KeyFrames) FPaperFlipbookKeyFrame();
					KeyFrame->Sprite = Sprites[SpriteIndex];
					KeyFrame->FrameRun = 1;
				}

				if (!bIsReimporting)
				{
					const FString PackagePath = FPackageName::GetLongPackagePath(SpriteSheet->GetOutermost()->GetPathName()) + TEXT("/Flipbooks/") + SubFolderPath;
					const FString TentativePackagePath = UPackageTools::SanitizePackageName(PackagePath + FlipbookName);
					FString DefaultSuffix;
					FString AssetName;
					FString PackageName;
					AssetToolsModule.Get().CreateUniqueAssetName(TentativePackagePath, DefaultSuffix, /*out*/ PackageName, /*out*/ AssetName);

					if (UObject *NewAsset = AssetToolsModule.Get().CreateAsset(AssetName, PackagePath, UPaperFlipbook::StaticClass(), FlipbookFactory))
					{
						UPaperFlipbook *Flipbook = Cast<UPaperFlipbook>(NewAsset);
						SpriteSheet->Flipbooks.Add(Flipbook);
						
						// Set flipbook FPS using reflection
						if (FProperty* FramesPerSecondProperty = FindFProperty<FProperty>(UPaperFlipbook::StaticClass(), TEXT("FramesPerSecond")))
						{
							FramesPerSecondProperty->SetValue_InContainer(Flipbook, &BaseFPS);
							UE_LOG(LogAsepriteImporter, Log, TEXT("Set flipbook FramesPerSecond to: %f"), BaseFPS);
						}
						
						// Set material for new flipbook (new import only)
						TSoftObjectPtr<UMaterialInterface> SpriteMaterial = Settings->SpriteMaterial;
						if (!SpriteMaterial.IsNull())
						{
							UMaterialInterface *LoadedMaterial = SpriteMaterial.LoadSynchronous();
							if (LoadedMaterial)
							{
								if (FProperty *MaterialProperty = FindFProperty<FProperty>(UPaperFlipbook::StaticClass(), TEXT("DefaultMaterial")))
								{
									MaterialProperty->SetValue_InContainer(Flipbook, &LoadedMaterial);
									UE_LOG(LogAsepriteImporter, Log, TEXT("Applied material to new flipbook: %s"), *LoadedMaterial->GetName());
								}
							}
						}

						ObjectsToSync.Add(Flipbook);
					}
				}
				else
				{
					// 2025.01.07 - Re-import: Update existing flipbook instead of creating new one
					// This preserves references from Blueprints/Actors while updating animation data
					UPaperFlipbook* ExistingFlipbook = nullptr;
					
					// Find existing flipbook by name from the ExistingFlipbooks array
					// UE_LOG(LogAsepriteImporter, Verbose, TEXT("Re-import: Looking for existing flipbook with TagName '%s' among %d existing flipbooks"), *FlipbookName, ExistingFlipbooks.Num());
					
					for (int32 i = 0; i < ExistingFlipbooks.Num(); ++i)
					{
						const TSoftObjectPtr<UPaperFlipbook>& FlipbookPtr = ExistingFlipbooks[i];
						UPaperFlipbook* LoadedFlipbook = FlipbookPtr.LoadSynchronous();
						if (LoadedFlipbook)
						{
							FString ExistingFlipbookName = LoadedFlipbook->GetName();
							
							// Apply same sanitization as used during asset creation
							FString SanitizedFlipbookName = UPackageTools::SanitizePackageName(FlipbookName);
							bool bNameMatch = ExistingFlipbookName.Equals(SanitizedFlipbookName, ESearchCase::IgnoreCase);
							
							// UE_LOG(LogAsepriteImporter, Warning, TEXT("DEBUG: Comparing ExistingFlipbook='%s' vs FlipbookName='%s' (Sanitized: '%s'), Match=%s"), 
							//	*ExistingFlipbookName, *FlipbookName, *SanitizedFlipbookName, bNameMatch ? TEXT("YES") : TEXT("NO"));
							
							if (bNameMatch)
							{
								ExistingFlipbook = LoadedFlipbook;
								UE_LOG(LogAsepriteImporter, Log, TEXT("Re-import: Found matching flipbook for update: %s"), *ExistingFlipbookName);
								break;
							}
						}
					}
					
					if (ExistingFlipbook)
					{
						UE_LOG(LogAsepriteImporter, Log, TEXT("Re-import: Updating existing flipbook '%s'"), *ExistingFlipbook->GetName());

						// Calculate base FPS from this animation's first frame when using Aseprite timing (for re-import)
						float ReimportBaseFPS = static_cast<float>(FlipbookFramePerSecond); // Default to user setting
						// Update existing flipbook with new keyframes
						// Use reflection to access KeyFrames property since it's protected
						if (FProperty* KeyFramesProperty = FindFProperty<FProperty>(UPaperFlipbook::StaticClass(), TEXT("KeyFrames")))
						{
							// Get the current KeyFrames array
							TArray<FPaperFlipbookKeyFrame>* KeyFramesPtr = KeyFramesProperty->ContainerPtrToValuePtr<TArray<FPaperFlipbookKeyFrame>>(ExistingFlipbook);
							if (KeyFramesPtr)
							{
								// Backup existing keyframes to preserve user-modified FrameRun values
								TArray<FPaperFlipbookKeyFrame> OldKeyFrames = *KeyFramesPtr;

								// Clear existing keyframes
								KeyFramesPtr->Empty();

								// Add new keyframes
								for (int32 SpriteIndex = 0; SpriteIndex < Sprites.Num(); ++SpriteIndex)
								{
									FPaperFlipbookKeyFrame KeyFrame;
									KeyFrame.Sprite = Sprites[SpriteIndex];

									// Preserve FrameRun from old keyframes when possible
									// Even if frame count changes, keep existing values for matching indices
									// Only new frames beyond old count get default value 1
									if (SpriteIndex < OldKeyFrames.Num())
									{
										KeyFrame.FrameRun = OldKeyFrames[SpriteIndex].FrameRun;  // Keep user-modified values
									}
									else
									{
										KeyFrame.FrameRun = 1; // Default for newly added frames
									}

									KeyFramesPtr->Add(KeyFrame);
								}
							}
						}

						// Re-import: Preserve user-modified FPS settings
						// Do not overwrite FPS - user may have adjusted it in UE
						// (FPS is often adjusted per-animation in UE after import)

						// Mark as dirty and add to sync list
						ExistingFlipbook->MarkPackageDirty();
						ObjectsToSync.Add(ExistingFlipbook);
						
						// 2025.01.07 - CRITICAL: Add updated flipbook back to SpriteSheet->Flipbooks
						// This ensures that subsequent re-imports can find the flipbook
						bool bAlreadyInArray = false;
						for (const TSoftObjectPtr<UPaperFlipbook>& ExistingPtr : SpriteSheet->Flipbooks)
						{
							if (ExistingPtr.Get() == ExistingFlipbook)
							{
								bAlreadyInArray = true;
								break;
							}
						}
						if (!bAlreadyInArray)
						{
							SpriteSheet->Flipbooks.Add(ExistingFlipbook);
							// UE_LOG(LogAsepriteImporter, Verbose, TEXT("Added updated flipbook to SpriteSheet->Flipbooks: %s"), *ExistingFlipbook->GetName());
						}
						
						UE_LOG(LogAsepriteImporter, Log, TEXT("Updated existing flipbook: %s"), *FlipbookName);
					}
					else
					{
						UE_LOG(LogAsepriteImporter, Log, TEXT("Re-import: Creating new flipbook for tag '%s' (no existing flipbook found)"), *FlipbookName);
						
						// If no existing flipbook found, create new one (new tag case)
						const FString PackagePath = FPackageName::GetLongPackagePath(SpriteSheet->GetOutermost()->GetPathName()) + TEXT("/Flipbooks/") + SubFolderPath;
						const FString TentativePackagePath = UPackageTools::SanitizePackageName(PackagePath + FlipbookName);
						FString DefaultSuffix;
						FString AssetName;
						FString PackageName;
						AssetToolsModule.Get().CreateUniqueAssetName(TentativePackagePath, DefaultSuffix, /*out*/ PackageName, /*out*/ AssetName);

						if (UObject *NewAsset = AssetToolsModule.Get().CreateAsset(AssetName, PackagePath, UPaperFlipbook::StaticClass(), FlipbookFactory))
						{
							UPaperFlipbook *Flipbook = Cast<UPaperFlipbook>(NewAsset);
							SpriteSheet->Flipbooks.Add(Flipbook);
							
							// Set material for new flipbook (this is a new tag, so apply current settings)
							TSoftObjectPtr<UMaterialInterface> SpriteMaterial = Settings->SpriteMaterial;
							if (!SpriteMaterial.IsNull())
							{
								UMaterialInterface *LoadedMaterial = SpriteMaterial.LoadSynchronous();
								if (LoadedMaterial)
								{
									if (FProperty *MaterialProperty = FindFProperty<FProperty>(UPaperFlipbook::StaticClass(), TEXT("DefaultMaterial")))
									{
										MaterialProperty->SetValue_InContainer(Flipbook, &LoadedMaterial);
										UE_LOG(LogAsepriteImporter, Log, TEXT("Applied material to new flipbook: %s"), *LoadedMaterial->GetName());
									}
								}
							}

							ObjectsToSync.Add(Flipbook);
							UE_LOG(LogAsepriteImporter, Log, TEXT("Created new flipbook for new tag: %s"), *FlipbookName);
						}
					}
				}

				if (GWarn->ReceivedUserCancel())
				{
					break;
				}
			}

			// 2025.01.07 - Mark deleted flipbooks (tags that no longer exist)
			if (bIsReimporting && bIsReimportingFlipbooks)
			{
				TArray<UPaperFlipbook*> UpdatedFlipbooks;
				// Collect all flipbooks that were updated or created
				for (UObject* Obj : ObjectsToSync)
				{
					if (UPaperFlipbook* Flipbook = Cast<UPaperFlipbook>(Obj))
					{
						UpdatedFlipbooks.Add(Flipbook);
					}
				}
				
				// Find existing flipbooks that were not updated (deleted tags)
				for (const TSoftObjectPtr<UPaperFlipbook>& FlipbookPtr : ExistingFlipbooks)
				{
					UPaperFlipbook* ExistingFlipbook = FlipbookPtr.LoadSynchronous();
					if (ExistingFlipbook && !UpdatedFlipbooks.Contains(ExistingFlipbook))
					{
						FString CurrentName = ExistingFlipbook->GetName();
						if (!CurrentName.Contains(TEXT("_deleted")))
						{
							// Try to rename the asset in the content browser (more visible)
							FString NewName = CurrentName + TEXT("_deleted");
							FString CurrentPath = ExistingFlipbook->GetPathName();
							FString PackagePath = FPackageName::GetLongPackagePath(CurrentPath);
							FString NewPackagePath = PackagePath + TEXT("/") + NewName;
							
							// Use AssetTools to rename the asset properly
							TArray<FAssetRenameData> AssetsToRename;
							AssetsToRename.Add(FAssetRenameData(ExistingFlipbook, PackagePath, NewName));
							
							bool bRenameSuccessful = AssetToolsModule.Get().RenameAssets(AssetsToRename);
							
							if (bRenameSuccessful)
							{
								UE_LOG(LogAsepriteImporter, Log, TEXT("Renamed deleted flipbook: %s -> %s"), *CurrentName, *NewName);
							}
							else
							{
								// Fallback to simple object rename if asset rename fails
								ExistingFlipbook->Rename(*NewName);
								ExistingFlipbook->MarkPackageDirty();
								UE_LOG(LogAsepriteImporter, Log, TEXT("Marked flipbook as deleted (object rename): %s -> %s"), *CurrentName, *NewName);
							}
						}
					}
				}
			}
			
			// 2025.01.07 - CRITICAL: Update SpriteSheet->Flipbooks array after re-import
			// This ensures that subsequent re-imports have the correct flipbook list
			if (bIsReimporting && bIsReimportingFlipbooks)
			{
				// Rebuild the flipbooks array with current flipbooks (excluding deleted ones)
				TArray<TSoftObjectPtr<UPaperFlipbook>> NewFlipbooksArray;
				
				for (UObject* Obj : ObjectsToSync)
				{
					if (UPaperFlipbook* Flipbook = Cast<UPaperFlipbook>(Obj))
					{
						// Only add flipbooks that are not marked as deleted
						if (!Flipbook->GetName().Contains(TEXT("_deleted")))
						{
							NewFlipbooksArray.Add(Flipbook);
						}
					}
				}
				
				// Update the SpriteSheet's flipbooks array
				SpriteSheet->Flipbooks = NewFlipbooksArray;
				UE_LOG(LogAsepriteImporter, Log, TEXT("Updated SpriteSheet->Flipbooks array: %d flipbooks"), NewFlipbooksArray.Num());
				
				// Log the updated flipbooks for debugging
				for (int32 i = 0; i < SpriteSheet->Flipbooks.Num(); ++i)
				{
					UPaperFlipbook* Flipbook = SpriteSheet->Flipbooks[i].Get();
					if (Flipbook)
					{
						// UE_LOG(LogAsepriteImporter, Verbose, TEXT("  Updated Flipbook[%d]: %s"), i, *Flipbook->GetName());
					}
				}
			}

			GWarn->EndSlowTask();

			if (ObjectsToSync.Num() > 0)
			{
				ContentBrowserModule.Get().SyncBrowserToAssets(ObjectsToSync);
			}
		}
	}
}

void FAsepriteJsonImporter::ExtractFlipbooksFromSprites(TMap<FString, TArray<UPaperSprite *>> &OutSpriteFlipbookMap, const FString BaseName, const TArray<UPaperSprite *> &Sprites, const TArray<FString> &InSpriteNames, const TMap<int32, FString> &TagNames, const TMap<int32, FString> &LayerNames)
{
	OutSpriteFlipbookMap.Reset();

	if (true)
	{
		check((InSpriteNames.Num() == 0) || (InSpriteNames.Num() == Sprites.Num()));

		for (int32 SpriteIndex = 0; SpriteIndex < Sprites.Num(); ++SpriteIndex)
		{
			UPaperSprite *Sprite = Sprites[SpriteIndex];
			const FString *TagName = TagNames.Find(SpriteIndex);
			if (TagName != nullptr && !TagName->IsEmpty())
			{
				OutSpriteFlipbookMap.FindOrAdd(*TagName).Add(Sprite);
			}
		}
	}
	else
	{
		// Legacy 이제 Tag와 Layer 스프라이트 이름 정보로 만들 것이다
		// 아래의 추측 방법은 정보가 없을때나 유용하지 지금처럼 직접 CLI를 사용해서 변환하는 경우에는 불필요하다

		// Local copy
		// check((InSpriteNames.Num() == 0) || (InSpriteNames.Num() == Sprites.Num()));
		// TArray<FString> SpriteNames = InSpriteNames;
		// if (InSpriteNames.Num() == 0)
		// {
		// 	SpriteNames.Reset();
		// 	for (int32 SpriteIndex = 0; SpriteIndex < Sprites.Num(); ++SpriteIndex)
		// 	{
		// 		check(Sprites[SpriteIndex] != nullptr);
		// 		SpriteNames.Add(Sprites[SpriteIndex]->GetName());
		// 	}
		// }

		// // Group them
		// TMap<FString, UPaperSprite *> SpriteNameMap;
		// TArray<UPaperSprite *> RemainingSprites;

		// for (int32 SpriteIndex = 0; SpriteIndex < Sprites.Num(); ++SpriteIndex)
		// {
		// 	UPaperSprite *Sprite = Sprites[SpriteIndex];
		// 	const FString SpriteName = SpriteNames[SpriteIndex];

		// 	SpriteNameMap.Add(SpriteName, Sprite);

		// 	int32 SpriteNumber = 0;
		// 	FString SpriteBareString;
		// 	if (ExtractSpriteNumber(SpriteName, /*out*/ SpriteBareString, /*out*/ SpriteNumber))
		// 	{
		// 		SpriteBareString = ObjectTools::SanitizeObjectName(SpriteBareString);
		// 		OutSpriteFlipbookMap.FindOrAdd(SpriteBareString).Add(Sprite);
		// 	}
		// 	else
		// 	{
		// 		RemainingSprites.Add(Sprite);
		// 	}
		// }

		// // Natural sort using the same method as above
		// struct FSpriteSortPredicate
		// {
		// 	FSpriteSortPredicate() {}

		// 	// Sort predicate operator
		// 	bool operator()(UPaperSprite &LHS, UPaperSprite &RHS) const
		// 	{
		// 		FString LeftString;
		// 		int32 LeftNumber;
		// 		ExtractSpriteNumber(LHS.GetName(), /*out*/ LeftString, /*out*/ LeftNumber);

		// 		FString RightString;
		// 		int32 RightNumber;
		// 		ExtractSpriteNumber(RHS.GetName(), /*out*/ RightString, /*out*/ RightNumber);

		// 		return (LeftString == RightString) ? (LeftNumber < RightNumber) : (LeftString < RightString);
		// 	}
		// };

		// // Sort sprites
		// TArray<FString> Keys;
		// OutSpriteFlipbookMap.GetKeys(Keys);
		// for (auto SpriteName : Keys)
		// {
		// 	OutSpriteFlipbookMap[SpriteName].Sort(FSpriteSortPredicate());
		// }

		// // Create a flipbook from all remaining sprites
		// // Not sure if this is desirable behavior, might want one flipbook per sprite
		// if (RemainingSprites.Num() > 0)
		// {
		// 	RemainingSprites.Sort(FSpriteSortPredicate());

		// 	const FString DesiredName = GetCleanerSpriteName(RemainingSprites[0]->GetName()) + TEXT("_Flipbook");
		// 	const FString SanitizedName = ObjectTools::SanitizeObjectName(DesiredName);

		// 	OutSpriteFlipbookMap.Add(SanitizedName, RemainingSprites);
		// }
	}
}

UMaterialExpressionConstant *CreateConstantExpression(UMaterial *Material, float Value)
{
	UMaterialExpressionConstant *Constant = NewObject<UMaterialExpressionConstant>(Material);
	Constant->R = Value;
	Material->GetEditorOnlyData()->ExpressionCollection.AddExpression(Constant);
	return Constant;
}

// void CreateSpriteAnimationMaterial(UMaterial *NewMaterial, UTexture *Texture, int32 StartIndex, int32 EndIndex, int32 FrameWidth, int32 FrameHeight, int32 TotalFramesNum)
// {
// if (StartIndex < 0 || EndIndex >= TotalFramesNum || StartIndex > EndIndex)
// 	return;

// int32 NumFrames = EndIndex - StartIndex + 1;
// int32 TextureWidth = Texture->Source.GetSizeX();
// int32 TextureHeight = Texture->Source.GetSizeY();
// int32 NumFramesX = TextureWidth / FrameWidth;
// int32 NumFramesY = TextureHeight / FrameHeight;

// // Add a texture sample expression
// UMaterialExpressionTextureSample *TextureSample = NewObject<UMaterialExpressionTextureSample>(NewMaterial);
// TextureSample->Texture = Texture;
// TextureSample->SamplerType = SAMPLERTYPE_Color;
// NewMaterial->GetEditorOnlyData()->ExpressionCollection.AddExpression(TextureSample);

// // Add texture coordinate expression
// UMaterialExpressionTextureCoordinate *TexCoord = NewObject<UMaterialExpressionTextureCoordinate>(NewMaterial);
// TexCoord->CoordinateIndex = 0;
// NewMaterial->GetEditorOnlyData()->ExpressionCollection.AddExpression(TexCoord);

// // Time-based animation
// UMaterialExpressionTime *TimeExpression = NewObject<UMaterialExpressionTime>(NewMaterial);
// UMaterialExpressionScalarParameter *AnimationSpeed = NewObject<UMaterialExpressionScalarParameter>(NewMaterial);
// AnimationSpeed->DefaultValue = 1.0f; // Adjust speed as necessary
// AnimationSpeed->ParameterName = TEXT("AnimationSpeed");
// NewMaterial->GetEditorOnlyData()->ExpressionCollection.AddExpression(AnimationSpeed);

// UMaterialExpressionMultiply *TimeSpeedMultiply = NewObject<UMaterialExpressionMultiply>(NewMaterial);
// TimeSpeedMultiply->A.Expression = TimeExpression;
// TimeSpeedMultiply->B.Expression = AnimationSpeed;
// NewMaterial->GetEditorOnlyData()->ExpressionCollection.AddExpression(TimeSpeedMultiply);

// UMaterialExpressionMultiply *FramesTimeMultiply = NewObject<UMaterialExpressionMultiply>(NewMaterial);
// FramesTimeMultiply->A.Expression = TimeSpeedMultiply;
// FramesTimeMultiply->B.Expression = CreateConstantExpression(NewMaterial, (float)NumFrames);
// NewMaterial->GetEditorOnlyData()->ExpressionCollection.AddExpression(FramesTimeMultiply);

// UMaterialExpressionFrac *FracFrameIndex = NewObject<UMaterialExpressionFrac>(NewMaterial);
// FracFrameIndex->Input.Expression = FramesTimeMultiply;
// NewMaterial->GetEditorOnlyData()->ExpressionCollection.AddExpression(FracFrameIndex);

// // Calculate frame number from fractional index
// UMaterialExpressionMultiply *FrameNumberExpression = NewObject<UMaterialExpressionMultiply>(NewMaterial);
// FrameNumberExpression->A.Expression = FracFrameIndex;
// FrameNumberExpression->B.Expression = CreateConstantExpression(NewMaterial, (float)NumFrames);
// NewMaterial->GetEditorOnlyData()->ExpressionCollection.AddExpression(FrameNumberExpression);

// // Calculate column (U) and row (V) from frame number
// UMaterialExpressionFmod *FrameRow = NewObject<UMaterialExpressionFmod>(NewMaterial);
// FrameRow->A.Expression = FrameNumberExpression;
// FrameRow->B.Expression = CreateConstantExpression(NewMaterial, (float)NumFramesX);
// NewMaterial->GetEditorOnlyData()->ExpressionCollection.AddExpression(FrameRow);

// UMaterialExpressionDivide *UCoord = NewObject<UMaterialExpressionDivide>(NewMaterial);
// UCoord->A.Expression = FrameRow;
// UCoord->B.Expression = CreateConstantExpression(NewMaterial, (float)NumFramesX);
// NewMaterial->GetEditorOnlyData()->ExpressionCollection.AddExpression(UCoord);

// UMaterialExpressionFloor *FrameColumn = NewObject<UMaterialExpressionFloor>(NewMaterial);
// FrameColumn->Input.Expression = UCoord; // This should be a divide expression based on frame number and NumFramesX
// NewMaterial->GetEditorOnlyData()->ExpressionCollection.AddExpression(FrameColumn);

// UMaterialExpressionDivide *VCoord = NewObject<UMaterialExpressionDivide>(NewMaterial);
// VCoord->A.Expression = FrameColumn;
// VCoord->B.Expression = CreateConstantExpression(NewMaterial, (float)NumFramesY);
// NewMaterial->GetEditorOnlyData()->ExpressionCollection.AddExpression(VCoord);

// // Calculate final UVs by adding texture coordinate offsets
// UMaterialExpressionAdd *FinalU = NewObject<UMaterialExpressionAdd>(NewMaterial);
// FinalU->A.Expression = TexCoord;
// FinalU->B.Expression = UCoord;
// NewMaterial->GetEditorOnlyData()->ExpressionCollection.AddExpression(FinalU);

// UMaterialExpressionAdd *FinalV = NewObject<UMaterialExpressionAdd>(NewMaterial);
// FinalV->A.Expression = TexCoord;
// FinalV->B.Expression = VCoord;
// NewMaterial->GetEditorOnlyData()->ExpressionCollection.AddExpression(FinalV);

// // Assign the final UV to the texture sample
// TextureSample->Coordinates.Expression = FinalV; // Assumes vertical sprite sheet animation

// // Set the material's base color to be the output of the texture sample
// NewMaterial->GetEditorOnlyData()->BaseColor.Expression = TextureSample;

// // Finalize the material setup
// NewMaterial->PreEditChange(nullptr);
// NewMaterial->PostEditChange();
// }

void FAsepriteJsonImporter::CreateSpriteAnimationMaterial(TArray<UObject *> &OutMaterials, TWeakObjectPtr<UAseprite> Object)
{
	UAseprite *SpriteSheet = Object.Get();
	if (SpriteSheet != nullptr)
	{
		float RowFrameCount = 1.0;
		float ColFrameCount = 1.0;

		if (SpriteSheet->FrameWidth > 0 && SpriteSheet->FrameHeight > 0)
		{
			RowFrameCount = SpriteSheet->Texture->GetSizeX() / SpriteSheet->FrameWidth;
			ColFrameCount = SpriteSheet->Texture->GetSizeY() / SpriteSheet->FrameHeight;
		}
		else
		{
			if (SpriteSheet->Sprites.Num() > 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("GetSourceSize: %f"), SpriteSheet->Sprites[0]->GetSourceSize().X);
				const auto Size = SpriteSheet->Sprites[0]->GetSourceSize();
				RowFrameCount = SpriteSheet->Texture->GetSizeX() / Size.X;
				ColFrameCount = SpriteSheet->Texture->GetSizeY() / Size.Y;
			}
		}

		if (RowFrameCount <= 0.0 || ColFrameCount <= 0.0)
		{
			FText DialogTitle = FText::FromString("Aseprite Error");
			FText DialogText = FText::FromString("Failed To Create Niagara Emitter. Frame Width or Frame Height is invalid. Please reimport the asset.");
			FMessageDialog::Open(EAppMsgType::Ok, DialogText, DialogTitle);
			return;
		}

		// UE_LOG(LogTemp, Warning, TEXT("RowFrameCount: %f"), RowFrameCount);
		// UE_LOG(LogTemp, Warning, TEXT("ColFrameCount: %f"), ColFrameCount);

		for (FTagRange TagRange : CreateTagRanges(SpriteSheet->TagNames))
		{
			FString FlipbookMaterialAssetPath = TEXT("/AsepriteImporter/FlipbookMaterial.FlipbookMaterial");
			FSoftObjectPath FlipbookMaterialTemplateAssetRef(FlipbookMaterialAssetPath);
			UMaterial *TemplateMaterial = Cast<UMaterial>(FlipbookMaterialTemplateAssetRef.TryLoad());

			FAssetToolsModule &AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");
			const FString PackagePath = FPackageName::GetLongPackagePath(SpriteSheet->GetOutermost()->GetPathName()) + TEXT("/Material/");
			UObject *NewDuplicatedMaterial = AssetToolsModule.Get().DuplicateAsset(*FString::Printf(TEXT("M_Flipbook_%s"), *TagRange.TagName.Replace(TEXT(" "), TEXT("_"))), PackagePath, TemplateMaterial);
			UMaterial *NewMaterial = Cast<UMaterial>(NewDuplicatedMaterial);

			if (NewMaterial)
			{
				// 텍스처 변경
				NewMaterial->SetTextureParameterValueEditorOnly(FName("Texture"), SpriteSheet->Texture);

				// 가로, 세로 크기 변경 (SubUV 크기)
				NewMaterial->SetScalarParameterValueEditorOnly(FName("Col"), RowFrameCount);
				NewMaterial->SetScalarParameterValueEditorOnly(FName("Row"), ColFrameCount);

				// 시작 및 끝 인덱스 변경
				NewMaterial->SetScalarParameterValueEditorOnly(FName("StartIndex"), TagRange.From);
				NewMaterial->SetScalarParameterValueEditorOnly(FName("EndIndex"), TagRange.To);

				// FString currentTagName = TEXT("");
				// int32 currentTagIndex = 0;
				// CreateSpriteAnimationMaterial(Cast<UMaterial>(NewAsset), SpriteSheet->Texture, 0, 4, 16, 16, 5);
				// Create a material for each flipbook
				// for (int32 FlipbookIndex = 0; FlipbookIndex < SpriteSheet->Flipbooks.Num(); ++FlipbookIndex)
				// {
				// 	auto Flipbook = SpriteSheet->Flipbooks[FlipbookIndex];
				// 	if (Flipbook != nullptr)
				// 	{
				// 		UObject *NewAsset = NewObject<UMaterial>(SpriteSheet, *FString::Printf(TEXT("%s_Material"), *Flipbook->GetName()), RF_Public | RF_Standalone);
				// 		NewMaterial->PostEditChange();

				// 		// Create a material for each flipbook
				// 		auto NewMaterial = Cast<UMaterial>(NewAsset);
				// 		CreateSpriteAnimationMaterial(NewMaterial, SpriteSheet->Texture, 0, 0, 0, 0, 0);

				// 		// Create a material for each flipbook
				// 		OutMaterials.Add(NewMaterial);
				// 	}
				// }

				OutMaterials.Push(NewMaterial);
			}
		}
	}
}

// 1. NiagaraSystem 생성
// UNiagaraSystem *NiagaraSystem = NewObject<UNiagaraSystem>(Outer, NAME_None, RF_Transactional);

// 2. NiagaraEmitter 추가
// UNiagaraEmitter *Emitter = NewObject<UNiagaraEmitter>(NiagaraSystem, NAME_None, RF_Transactional);
// NiagaraSystem->AddEmitter(Emitter);

void FAsepriteJsonImporter::CreateNiagaraSpriteEmitter(TArray<UObject *> &OutNiagaraEmitters, TWeakObjectPtr<UAseprite> Object)
{
	FAssetToolsModule &AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");
	UAseprite *SpriteSheet = Object.Get();

	float RowFrameCount = 1.0;
	float ColFrameCount = 1.0;
	if (SpriteSheet->FrameWidth > 0 && SpriteSheet->FrameHeight > 0)
	{
		RowFrameCount = SpriteSheet->Texture->GetSizeX() / SpriteSheet->FrameWidth;
		ColFrameCount = SpriteSheet->Texture->GetSizeY() / SpriteSheet->FrameHeight;
	}
	else
	{
		if (SpriteSheet->Sprites.Num() > 0)
		{
			const auto Size = SpriteSheet->Sprites[0]->GetSourceSize();
			RowFrameCount = SpriteSheet->Texture->GetSizeX() / Size.X;
			ColFrameCount = SpriteSheet->Texture->GetSizeY() / Size.Y;
		}
	}

	if (RowFrameCount <= 0.0 || ColFrameCount <= 0.0)
	{
		FText DialogTitle = FText::FromString("Aseprite Error");
		FText DialogText = FText::FromString("Failed To Create Niagara Emitter. Frame Width or Frame Height is invalid. Please reimport the asset.");
		FMessageDialog::Open(EAppMsgType::Ok, DialogText, DialogTitle);
		return;
	}

	// 패키지 경로 생성
	const FString PackagePath = FPackageName::GetLongPackagePath(SpriteSheet->GetOutermost()->GetPathName()) + TEXT("/Material/");

	// 기존 템플릿을 에셋으로 가져온다.
	FString TemplateMaterialAssetPath = TEXT("/AsepriteImporter/SpriteRenderMaterial.SpriteRenderMaterial");
	FSoftObjectPath TemplateMaterialAssetRef(TemplateMaterialAssetPath);

	// 캐스팅해서 복제
	UMaterial *TemplateMaterial = Cast<UMaterial>(TemplateMaterialAssetRef.TryLoad());

	auto NewDuplicatedMaterial = Cast<UMaterial>(AssetToolsModule.Get().DuplicateAsset(*FString::Printf(TEXT("M_Sprite_%s"), *SpriteSheet->GetName()), PackagePath, TemplateMaterial));
	if (NewDuplicatedMaterial)
	{

		for (TObjectPtr<UMaterialExpression> MaterialExpression : NewDuplicatedMaterial->GetExpressions())
		{
			if (UMaterialExpressionTextureSample *TextureSample = Cast<UMaterialExpressionTextureSample>(MaterialExpression))
			{
				// 텍스처 교체
				TextureSample->Texture = SpriteSheet->Texture;

				// Material 업데이트
				NewDuplicatedMaterial->PostEditChange();
				break;
			}
		}

		// ------ 직접 마테리얼을 생성하는 방법인데 너무 난이도가 높다 우선 제외 ---- //
		// // 배경은 투명하게
		// // NewMaterial->BlendMode = EBlendMode::BLEND_Translucent; // Template에서는 이미 되어 있음
		//
		// // Add a texture sample expression
		// UMaterialExpressionTextureSample *TextureSample = NewObject<UMaterialExpressionTextureSampleParameter2D>(NewMaterial);
		// TextureSample->Texture = SpriteSheet->Texture;
		//
		// // 에디터 온리 데이터를 가져온다
		// // auto EditorOnlyData = NewMaterial->GetEditorOnlyData();
		//
		// // Add Texture Image to the Material
		// EditorOnlyData->ExpressionCollection.AddExpression(TextureSample);
		//
		// // Connect RGBA to BaseColor
		// EditorOnlyData->BaseColor.Expression = TextureSample;
		//
		// // Connect Alpha to Opacity
		// EditorOnlyData->Opacity.Expression = TextureSample;
		// ------ 직접 마테리얼을 생성하는 방법인데 너무 난이도가 높다 우선 제외 ---- //

		// Emitter는 Emitter경로에 Flipbook 수 만큼 생성
		const FString EmitterPackagePath = FPackageName::GetLongPackagePath(SpriteSheet->GetOutermost()->GetPathName()) + TEXT("/Emitter/");
		FString EmitterTemplateAssetPath = TEXT("/AsepriteImporter/SpriteRenderEmitter.SpriteRenderEmitter");
		FSoftObjectPath EmitterTemplateAssetRef(EmitterTemplateAssetPath);
		UNiagaraEmitter *TemplateEmitter = Cast<UNiagaraEmitter>(EmitterTemplateAssetRef.TryLoad());
		FAssetData EmitterAssetData(TemplateEmitter);

		// UE_LOG(LogTemp, Warning, TEXT("EmitterAssetData.IsValid(): %d"), EmitterAssetData.IsValid());

		if (EmitterAssetData.IsValid())
		{
			for (auto Ranges = CreateTagRanges(SpriteSheet->TagNames); FTagRange TagRange : Ranges)
			{
				// 플러그인에 포함된 에셋을 가지고 변경하는 식으로 해야할 것 같다 방법 1
				if (true)
				{

					// auto NewDuplicatedMaterial = Cast<UMaterial>(AssetToolsModule.Get().DuplicateAsset(*FString::Printf(TEXT("M_SU_%s"), *SpriteSheet->GetName()), PackagePath, TemplateMaterial));
					auto NewDuplicatedEmitter = Cast<UNiagaraEmitter>(AssetToolsModule.Get().DuplicateAsset(TagRange.TagName.Replace(TEXT(" "), TEXT("_")), EmitterPackagePath, TemplateEmitter));
					// auto EmiterData = NewDuplicatedEmitter->GetEmitterData(FGuid(TEXT("")));
					auto EmitterData = NewDuplicatedEmitter->GetLatestEmitterData();
					// UNiagaraEmitter *EmitterTest1 = Cast<UNiagaraEmitter>(StaticLoadObject(UNiagaraEmitter::StaticClass(), nullptr, *AssetPath));
					// auto EmitterData1 = EmitterTest1->GetEmitterData(FGuid(TEXT("")));
					// 렌더 프롭에 접근해서 머터리얼 변경
					const auto RenderProps = Cast<UNiagaraSpriteRendererProperties>(EmitterData->GetRenderers().Last());
					RenderProps->SubImageSize.X = RowFrameCount;
					RenderProps->SubImageSize.Y = ColFrameCount;
					RenderProps->bSubImageBlend = false;
					RenderProps->Material = NewDuplicatedMaterial;

					// auto UpdateScript = EmitterData->EmitterUpdateScriptProps.Script;

					TArray<UNiagaraScript *> OutScripts;
					EmitterData->GetScripts(OutScripts);

					for (auto OutScript : OutScripts)
					{
						// UE_LOG(LogTemp, Warning, TEXT("All Script: %s"), *OutScript->GetName());
						if (OutScript->GetUsage() == ENiagaraScriptUsage::ParticleUpdateScript)
						{
							// UE_LOG(LogTemp, Warning, TEXT("Script: %d"), OutScript->IsParticleUpdateScript());
							FString TargetNameEndFrameRange = TEXT("Constants.") + NewDuplicatedEmitter->GetName() + TEXT(".SubUVAnimation.") + TEXT("End Frame Range Override");
							FString TargetNameStartFrameRange = TEXT("Constants.") + NewDuplicatedEmitter->GetName() + TEXT(".SubUVAnimation.") + TEXT("Start Frame Range Override");
							// FString TargetName2 = TEXT("Constants.") + NewDuplicatedEmitter->GetName() + TEXT(".SubUVAnimation.") + TEXT("Start Frame Offset");
							if (OutScript->IsParticleUpdateScript())
							{
								TArray<FNiagaraVariable> OutParameters;
								OutScript->RapidIterationParameters.GetParameters(OutParameters);
								for (FNiagaraVariable OutParameter : OutParameters)
								{
									if (OutParameter.GetName() == TargetNameStartFrameRange)
										OutScript->RapidIterationParameters.SetParameterValue(TagRange.From, OutParameter);
									if (OutParameter.GetName() == TargetNameEndFrameRange)
										OutScript->RapidIterationParameters.SetParameterValue(TagRange.To, OutParameter);
								}
							}
						}
					}

					NewDuplicatedEmitter->PostEditChange();
					OutNiagaraEmitters.Add(NewDuplicatedEmitter);

					// TArray<FNiagaraVariable> OutParameters;
					// UpdateScript->RapidIterationParameters.GetParameters(OutParameters);
					// for (FNiagaraVariable OutParameter : OutParameters)
					// {
					// 	UE_LOG(LogTemp, Warning, TEXT("GetParameters: %s"), *OutParameter.GetName().ToString());
					// }
					// int32 VarIndex = UpdateScript->RapidIterationParameters.IndexOf(EndFrameRangeVar);
					// auto NewEndFrameRange = 10.0f;
					//
					// if (VarIndex != INDEX_NONE)
					// {
					// 	UE_LOG(LogTemp, Warning, TEXT("VarIndex: %d"), VarIndex);
					// 	// 변수 값 설정
					// 	// UpdateScript->RapidIterationParameters.SetParameterData(&NewEndFrameRange, EndFrameRangeVar, VarIndex);
					//
					// 	// Emitter 업데이트
					// 	NewDuplicatedEmitter->PostEditChange();
					// 	NewDuplicatedEmitter->MarkPackageDirty();
					// }
					// UpdateScript->SetVariableDefault(EndFrameRangeVar, NewEndFrameRange);

					// TArray<FNiagaraVariable> RapidIterationParameters;
					// Script->RapidIterationParameters.GetParameters(RapidIterationParameters);
					// TArray<FString> ValidFunctionCallNames;
					// for (const FNiagaraVariable &RapidIterationParameter : RapidIterationParameters)
					// {
					// 	FString EmitterName;
					// 	FString FunctionCallName;
					// 	FString InputName;
					// 	if (FNiagaraParameterUtilities::SplitRapidIterationParameterName(RapidIterationParameter, Script->GetUsage(), EmitterName, FunctionCallName, InputName))
					// 	{
					// 		if (EmitterName != NewDuplicatedEmitter->GetUniqueEmitterName() || ValidFunctionCallNames.Contains(FunctionCallName) == false)
					// 		{
					// 			Script->RapidIterationParameters.RemoveParameter(RapidIterationParameter);
					// 		}
					// 	}
					// }

					// EmitterData->UpdateScriptProps.Script->RapidIterationParameters.SetParameterData((uint8 *)&NewSpawnRate, SpawnRateVar);

					// 3. Sprite Renderer 설정
					// UNiagaraSpriteRendererProperties *SpriteRenderer = NewObject<UNiagaraSpriteRendererProperties>(EmitterTest, NAME_None, RF_Transactional);
					// EmitterTest->RendererProperties.Add(SpriteRenderer);

					// 4. SubUV 파라미터 설정
					// auto Param = EmitterData->GetEditorParameters();
					// if (auto *Source = EmitterData2->UpdateScriptProps.Script->GetLatestSource())
					// if (auto ScriptSource = Cast<UNiagaraScriptSource>(EmitterData->GraphSource))
					// {
					// StatelessModule을 어떻게 가져오지?
					// UNiagaraScriptVariable* Variable = ScriptSource->NodeGraph->GetScriptVariable(TEXT("SubUVanimation"));

					// NIAGARAEDITOR_API class UNiagaraNodeOutput* FindEquivalentOutputNode(ENiagaraScriptUsage TargetUsageType, FGuid TargetUsageId = FGuid()) const;
					//
					// TArray<FNiagaraVariable> NIAGARAEDITOR_API FindStaticSwitchInputs(bool bReachableOnly = false, const TArray<FNiagaraVariable>& InStaticVars = TArray<FNiagaraVariable>()) const;
					//
					// NIAGARAEDITOR_API bool GetPropertyMetadata(FName PropertyName, FString& OutValue) const;
					//
					// NIAGARAEDITOR_API void ConditionalRefreshParameterReferences();
					//
					// NIAGARAEDITOR_API TOptional<FNiagaraVariableMetaData> GetMetaData(const FNiagaraVariable& InVar) const;
					//
					// NIAGARAEDITOR_API const FScriptVariableMap& GetAllMetaData() const;
					//
					// NIAGARAEDITOR_API FScriptVariableMap& GetAllMetaData();
					//
					// NIAGARAEDITOR_API UNiagaraScriptVariable* GetScriptVariable(FName ParameterName) const;
					//
					// NIAGARAEDITOR_API UNiagaraScriptVariable* GetScriptVariable(FGuid VariableGuid) const;
					//
					// NIAGARAEDITOR_API TArray<UNiagaraScriptVariable*> GetChildScriptVariablesForInput(FGuid VariableGuid) const;
					//
					// NIAGARAEDITOR_API TArray<FGuid> GetChildScriptVariableGuidsForInput(FGuid VariableGuid) const;
					//
					// NIAGARAEDITOR_API bool RenameParameter(const FNiagaraVariable& Parameter, FName NewName, bool bRenameRequestedFromStaticSwitch = false, bool* bMerged = nullptr, bool bSuppressEvents = false);
					//
					// NIAGARAEDITOR_API void ChangeParameterType(const TArray<FNiagaraVariable>& ParametersToChange, const FNiagaraTypeDefinition& NewType, bool bAllowOrphanedPins = false);
					//
					// const TMap<FNiagaraVariable, FInputPinsAndOutputPins> NIAGARAEDITOR_API CollectVarsToInOutPinsMap() const;

					// Duplicated했으므로 굳이 새로 추가할 필요 없다. ---- Don't Erase
					// 방법은 남겨 둘 것
					// FString TemplateSubUVAnimationPath = TEXT("/Niagara/Modules/Update/SubUV/V2/SubUVAnimation.SubUVAnimation");
					// FSoftObjectPath SubUVAnimationAssetRef(TemplateSubUVAnimationPath);
					// UNiagaraScript *SubUVAnimationAssetScript = Cast<UNiagaraScript>(SubUVAnimationAssetRef.TryLoad());
					// FAssetData ScriptAssetData(SubUVAnimationAssetScript);
					//
					// auto NodeGraph = ScriptSource->NodeGraph;
					// auto OutputNode = NodeGraph->FindEquivalentOutputNode(ENiagaraScriptUsage::ParticleUpdateScript);
					// UE_LOG(LogTemp, Warning, TEXT("ScriptAssetData.IsValid(): %d"), ScriptAssetData.IsValid());
					// if (ScriptAssetData.IsValid())
					// {
					// 	FNiagaraStackGraphUtilities::AddScriptModuleToStack(SubUVAnimationAssetScript, *OutputNode);
					// }
					// ---- Don't Erase

					// 용도는 있겠지만 지금 도움이 되는 것은 아닌듯 테스트 용 ---- Don't Erase
					// USubUVAnimationFactory *SubUVAnimationFactoryNew = NewObject<USubUVAnimationFactory>();
					// UObject *NewAsset2 = AssetToolsModule.Get().CreateAsset(TEXT("SubUVAnimation"), TEXT("/Game/SubUVAnimation"), USubUVAnimation::StaticClass(), SubUVAnimationFactoryNew);
					//
					// auto SubUVAnim = Cast<USubUVAnimation>(NewAsset2);
					// SubUVAnim->SubUVTexture = SpriteSheet->Texture;
					// SubUVAnim->SubImages_Horizontal = 8;
					// SubUVAnim->SubImages_Vertical = 8;

					// SubUV 노드 추가
					// UNiagaraNodeFunctionCall *SubUVNode = NewObject<UNiagaraNodeFunctionCall>(Graph);
					// Graph->AddNode(SubUVNode);
					// SubUVNode->FunctionScript = LoadObject<UNiagaraScript>(nullptr, TEXT("/Niagara/Modules/Spawn/SpawnSubUV.SpawnSubUV"));
					// SubUVNode->NodePosX = 0;
					// SubUVNode->NodePosY = 0;
					//
					// // SubUV 파라미터 설정
					// SubUVNode->SetNodeVariable(TEXT("SubImageIndex"), FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("SubImageIndex")));
					// SubUVNode->SetNodeVariable(TEXT("SubImageTime"), FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("SubImageTime")));
					// ---- Don't Erase
					// }
				}
				else
				{
					// Factory를 이용한 직접 생성 방법 2 현재 동작은 확인되었으나, 갈 길이 너무 멀다. 방법 1을 다시 시도
					UNiagaraEmitterFactoryNew *EmitterFactoryNew = NewObject<UNiagaraEmitterFactoryNew>();
					UObject *NewAsset = AssetToolsModule.Get().CreateAsset(TEXT("NewEmitterEXT"), TEXT("/Game/Emitter"), UNiagaraEmitter::StaticClass(), EmitterFactoryNew);
					auto EmitterTest2 = Cast<UNiagaraEmitter>(NewAsset);
					auto EmitterData2 = EmitterTest2->GetEmitterData(FGuid(TEXT("")));
				}
			}
		}
	}

	// 시스템 컴파일
	// NiagaraSystem->Compile();
	// OutNiagaraEmitters.Append(NiagaraSystem)

	// if (EmitterData2 != nullptr)
	// {
	// const auto RenderProps = Cast<UNiagaraSpriteRendererProperties>(EmitterData1->GetRenderers().Last());
	// constexpr int32 X = 5;
	// constexpr int32 Y = 5;
	// RenderProps->SubImageSize.Set(X, Y);
	// FNiagaraVariable SpawnRateVar(FNiagaraTypeDefinition::GetIntDef(), TEXT("SpawnBurstInstantaneous.SpawnCount"));
	// uint8 NewSpawnRate = 2;
	// EmitterData1->UpdateScriptProps.Script->RapidIterationParameters.SetParameterData((uint8 *)&NewSpawnRate, SpawnRateVar);

	// UE_LOG(LogTemp, Warning, TEXT("Renderer: %s"), EmitterData->EmitterUpdateScriptProps.Script.Get()->GetName())
	// FNiagaraStackGraphUtilities::AddModuleFromAssetPath(TEXT("/Niagara/Modules/Update/Color/Color.Color"), *ParticleUpdateOutputNode);
	// FSoftObjectPath AssetRef(AssetPath);
	// UNiagaraScript* AssetScript = Cast<UNiagaraScript>(AssetRef.TryLoad());
	// FAssetData ScriptAssetData(AssetScript);
	// if (ScriptAssetData.IsValid())
	// {
	// 	return FNiagaraStackGraphUtilities::AddScriptModuleToStack(ScriptAssetData, TargetOutputNode);
	// }
	// else
	// {
	// 	UE_LOG(LogNiagaraEditor, Error, TEXT("Failed to create default modules for emitter.  Missing %s"), *AssetRef.ToString());
	// 	return nullptr;
	// }
	// }

	// 변경된 내용을 적용합니다.
	// EmitterData->PostLoad();
	// EmitterData->CacheFromCompiledData();

	// TArray<UNiagaraScript *> OutScripts;
	// EmitterData1->GetScripts(OutScripts);
	//
	// for (auto script : OutScripts)
	// {
	// 	UE_LOG(LogTemp, Warning, TEXT("All Script: %s"), *script->GetName());
	// 	if (script->GetUsage() == ENiagaraScriptUsage::ParticleUpdateScript)
	// 	{
	// 		UE_LOG(LogTemp, Warning, TEXT("Script: %s"), *script->GetName());
	// 	}
	// }

	// for(auto data: EmitterTest->GetEditorOnlyParametersAdapters())
	// {
	// 	UE_LOG(LogTemp, Warning, TEXT("Emitter: %s"), *data->GetName());
	// }

	// Create a Animation for each flipbook
	// for (int32 FlipbookIndex = 0; FlipbookIndex < SpriteSheet->Flipbooks.Num(); ++FlipbookIndex)
	// {
	// 	auto Flipbook = SpriteSheet->Flipbooks[FlipbookIndex];
	// 	if (Flipbook != nullptr)
	// 	{

	// 새 Niagara Emitter 생성
	// UNiagaraEmitter *NewEmitter = NewObject<UNiagaraEmitter>(GetTransientPackage(), FName(*FString::Printf(TEXT("E_%s"), *Flipbook->GetName())), RF_Standalone);
	// UObject *NewAsset = AssetToolsModule.Get().CreateAsset(AssetName, PackagePath, UNiagaraEmitter::StaticClass());	// Emitter 생성
	// FGuid emptyGUID = FGuid();
	// auto EmitterData = NewEmitter->GetEmitterData(emptyGUID);
	// if (EmitterData != nullptr)
	// {
	// 	UE_LOG(LogTemp, Warning, TEXT("Renderer: %d"), EmitterData->GetRenderers().Num())
	// }

	// for(auto Render: NewEmitter->GetRenderers())
	// {
	// 	UE_LOG(LogTemp, Warning, TEXT("Renderer: %s"), *Render->GetName());
	// }

	// UNiagaraSpriteRendererProperties *SpriteRenderer = NewObject<UNiagaraSpriteRendererProperties>(NewEmitter, "Renderer", RF_Standalone);
	// SpriteRenderer->Material = NewMaterial;
	// 스폰 모듈 생성
	// UNiagaraScript *SpawnScript = NewObject<UNiagaraScript>(NewEmitter, "SpawnScript", RF_Standalone);
	// SpawnScript->Usage = ENiagaraScriptUsage::EmitterSpawnScript;
	// UNiagaraScriptSource *SpawnScriptSource = NewObject<UNiagaraScriptSource>(SpawnScript, "SpawnScriptSource", RF_Standalone);
	// SpawnScript->SetSource(SpawnScriptSource);

	// Spawn Burst Instantaneous 모듈 추가
	// UNiagaraNodeFunctionCall *SpawnBurstNode = NewObject<UNiagaraNodeFunctionCall>(SpawnScriptSource->NodeGraph, "SpawnBurstNode", RF_Standalone);
	// SpawnBurstNode->FunctionScript = LoadObject<UNiagaraScript>(nullptr, TEXT("/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous"));
	// SpawnBurstNode->NodePosX = 0;
	// SpawnBurstNode->NodePosY = 0;
	// SpawnScriptSource->NodeGraph->AddNode(SpawnBurstNode);

	// Spawn Burst 모듈의 입력 설정
	// SpawnBurstNode->SetInputPin("Spawn Count", FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), "SpawnCount"), FString("1"));
	// SpawnBurstNode->SetInputPin("Spawn Time", FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), "SpawnTime"), FString("0.0"));

	// 기본 업데이트 스크립트 생성 (여기서는 비워둡니다)
	// UNiagaraScript *UpdateScript = NewObject<UNiagaraScript>(NewEmitter, "UpdateScript", RF_Standalone);
	// UpdateScript->Usage = ENiagaraScriptUsage::EmitterUpdateScript;
	// UNiagaraScriptSource *UpdateScriptSource = NewObject<UNiagaraScriptSource>(UpdateScript, "UpdateScriptSource", RF_Standalone);
	// UpdateScript->SetSource(UpdateScriptSource);

	// Emitter에 스크립트 설정
	// NewEmitter->SpawnScriptProps.Script = SpawnScript;
	// NewEmitter->UpdateScriptProps.Script = UpdateScript;

	// 기본 렌더러 추가 (스프라이트 렌더러)
	// UNiagaraSpriteRendererProperties *SpriteRenderer = NewObject<UNiagaraSpriteRendererProperties>(NewEmitter, "Renderer", RF_Standalone);
	// NewEmitter->AddRenderer(SpriteRenderer);

	// OutNiagaraEmitters.Add(NewEmitter);
	// 	}
	// }
}

// static void ExtractTagAndLayerFromFrameName(const FString &Filename, bool SplitLayer, FString &OutImageName, FString &OutLayerName, FString &OutTagName, FString &OutIndex)
// {
// 	OutLayerName = TEXT("");
// 	OutTagName = TEXT("");
// 	OutIndex = TEXT("");

// 	// 1. 인덱스와 확장자명 추출
// 	int32 DotIndex;
// 	if (Filename.FindLastChar('.', DotIndex))
// 	{
// 		FString IndexAndExtension = Filename.Mid(DotIndex + 1);
// 		// 인덱스 추출 (확장자명 바로 앞 숫자)
// 		int32 IndexStart = IndexAndExtension.Find(TEXT(" "), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
// 		if (IndexStart != INDEX_NONE)
// 		{
// 			FString IndexString = IndexAndExtension.Mid(0, IndexStart);
// 			OutIndex = IndexString;
// 			// OutIndex = FCString::Atoi(*IndexString);
// 		}

// 		// 확장자명을 제외한 나머지 부분
// 		OutImageName = Filename.Left(DotIndex);
// 	}
// 	else
// 	{
// 		OutImageName = Filename;
// 	}

// 	// 2. 태그 이름 추출 (# 이후의 텍스트)
// 	int32 HashIndex = OutImageName.Find(TEXT("#"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
// 	if (HashIndex != INDEX_NONE)
// 	{
// 		OutTagName = OutImageName.Mid(HashIndex + 1);
// 		OutImageName = OutImageName.Left(HashIndex);
// 	}

// 	if (SplitLayer)
// 	{

// 		// 3. 레이어 이름 추출 (괄호 안의 텍스트)
// 		int32 ParenthesisOpenIndex = OutImageName.Find(TEXT("("), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
// 		int32 ParenthesisCloseIndex = OutImageName.Find(TEXT(")"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
// 		if (ParenthesisOpenIndex != INDEX_NONE && ParenthesisCloseIndex != INDEX_NONE && ParenthesisOpenIndex < ParenthesisCloseIndex)
// 		{
// 			OutLayerName = OutImageName.Mid(ParenthesisOpenIndex + 1, ParenthesisCloseIndex - ParenthesisOpenIndex - 1);
// 			// 레이어 이름을 제외한 나머지 부분
// 			OutImageName = OutImageName.Left(ParenthesisOpenIndex).TrimEnd();
// 		}
// 	}

// 	// 결과 정리
// 	OutImageName = OutImageName.TrimStartAndEnd();
// 	OutLayerName = OutLayerName.TrimStartAndEnd();
// 	OutTagName = OutTagName.TrimStartAndEnd();
// }

// FString FAsepriteJsonImporter::GetCleanerSpriteName(const FString &Name)
// {
// 	int32 LastCharacter = Name.Len() - 1;

// 	// Strip off "_Sprite"
// 	if (Name.Right(7) == TEXT("_Sprite"))
// 	{
// 		LastCharacter = Name.Len() - 7;
// 	}

// 	while (LastCharacter >= 0 && FChar::IsPunct(Name[LastCharacter]))
// 	{
// 		LastCharacter--;
// 	}

// 	if (LastCharacter > 0)
// 	{
// 		return Name.Left(LastCharacter + 1);
// 	}
// 	else
// 	{
// 		return Name;
// 	}
// }

// bool FAsepriteJsonImporter::ExtractSpriteNumber(const FString &String, FString &BareString, int32 &Number)
// {
// 	bool bExtracted = false;

// 	int32 LastCharacter = String.Len() - 1;
// 	if (LastCharacter >= 0)
// 	{
// 		// Find the last character that isn't a digit (Handle sprite names with numbers inside inverted commas / parentheses)
// 		while (LastCharacter >= 0 && !FChar::IsDigit(String[LastCharacter]))
// 		{
// 			LastCharacter--;
// 		}

// 		// Only proceed if we found a number in the sprite name
// 		if (LastCharacter >= 0)
// 		{
// 			while (LastCharacter > 0 && FChar::IsDigit(String[LastCharacter - 1]))
// 			{
// 				LastCharacter--;
// 			}

// 			if (LastCharacter >= 0)
// 			{
// 				const int32 FirstDigit = LastCharacter;
// 				int32 EndCharacter = FirstDigit;
// 				while (EndCharacter > 0 && !FChar::IsAlnum(String[EndCharacter - 1]))
// 				{
// 					EndCharacter--;
// 				}

// 				if (EndCharacter == 0)
// 				{
// 					// This string consists of non alnum + number, eg. _42
// 					// The flipbook / category name in this case will be _
// 					// Otherwise, we strip out all trailing non-alnum chars
// 					EndCharacter = FirstDigit;
// 				}

// 				const FString NumberString = String.Mid(FirstDigit);
// 				BareString = GetCleanerSpriteName((EndCharacter > 0) ? String.Left(EndCharacter) : String);
// 				Number = FCString::Atoi(*NumberString);
// 				bExtracted = true;
// 			}
// 		}
// 	}

// 	if (!bExtracted)
// 	{
// 		BareString = String;
// 		Number = -1;
// 	}
// 	return bExtracted;
// }

void FAsepriteJsonImporter::UpdateFlipbookMaterials(TWeakObjectPtr<UAseprite> Object)
{
	// 2025.01.07 - Bulk update materials for all flipbooks created from this Aseprite asset
	UAseprite* SpriteSheet = Object.Get();
	if (SpriteSheet == nullptr)
	{
		return;
	}

	// Show material selection dialog
	TSharedRef<SMaterialUpdateDialog> MaterialDialog = SNew(SMaterialUpdateDialog);
	
	bool bDialogResult = MaterialDialog->Show();
	if (!bDialogResult)
	{
		// User cancelled
		return;
	}

	UMaterialInterface* SelectedMaterial = MaterialDialog->GetSelectedMaterial();
	if (!SelectedMaterial)
	{
		// No material selected
		return;
	}

	// Apply selected material to sprites only (Paper2D correct approach)
	// Flipbooks will automatically use their sprites' materials during rendering
	int32 UpdatedSpriteCount = 0;
		
	// Update all sprites from this Aseprite asset
	for (const TSoftObjectPtr<UPaperSprite>& SpritePtr : SpriteSheet->Sprites)
	{
		UPaperSprite* Sprite = SpritePtr.LoadSynchronous();
		if (Sprite)
		{
			// Update both DefaultMaterial and any override material
			// This ensures the new material is used regardless of how it was originally set
			
			// 1. Update DefaultMaterial using reflection
			if (FProperty* SpriteMaterialProperty = FindFProperty<FProperty>(UPaperSprite::StaticClass(), TEXT("DefaultMaterial")))
			{
				SpriteMaterialProperty->SetValue_InContainer(Sprite, &SelectedMaterial);
			}
			
			// 2. Update DefaultMaterialOverride if it exists (this is what Import sets)
			if (FProperty* MaterialOverrideProperty = FindFProperty<FProperty>(UPaperSprite::StaticClass(), TEXT("DefaultMaterialOverride")))
			{
				MaterialOverrideProperty->SetValue_InContainer(Sprite, &SelectedMaterial);
			}
			
			Sprite->MarkPackageDirty();
			
			// Force sprite to rebuild render data with new material
			Sprite->PostEditChange();
			
			UpdatedSpriteCount++;
			UE_LOG(LogAsepriteImporter, Log, TEXT("Updated sprite material (both Default and Override): %s"), *Sprite->GetName());
		}
	}

	// Count affected flipbooks and update their materials too
	int32 AffectedFlipbookCount = 0;
	for (const TSoftObjectPtr<UPaperFlipbook>& FlipbookPtr : SpriteSheet->Flipbooks)
	{
		UPaperFlipbook* Flipbook = FlipbookPtr.LoadSynchronous();
		if (Flipbook)
		{
			// Update Flipbook's DefaultMaterial to ensure consistency
			// Flipbook material has higher priority than sprite materials, so we need to update it too
			if (FProperty* FlipbookMaterialProperty = FindFProperty<FProperty>(UPaperFlipbook::StaticClass(), TEXT("DefaultMaterial")))
			{
				FlipbookMaterialProperty->SetValue_InContainer(Flipbook, &SelectedMaterial);
				UE_LOG(LogAsepriteImporter, Log, TEXT("Updated flipbook material: %s"), *Flipbook->GetName());
			}
			
			Flipbook->MarkPackageDirty();
			
			// Force flipbook to refresh its rendering with updated materials
			Flipbook->PostEditChange();
			AffectedFlipbookCount++;
		}
	}

	UE_LOG(LogAsepriteImporter, Log, TEXT("Material update complete - Updated %d sprites and %d flipbooks, Material: %s"), 
		UpdatedSpriteCount, AffectedFlipbookCount, *SelectedMaterial->GetName());
	
	// Show success message
	FText DialogTitle = FText::FromString(TEXT("Material Update Complete"));
	FText DialogText = FText::FromString(FString::Printf(
		TEXT("Successfully updated materials to '%s':\n• %d Sprites updated (both Default and Override materials)\n• %d Flipbooks updated (DefaultMaterial property)\n\nChanges should be visible immediately in the viewport."), 
		*SelectedMaterial->GetName(), UpdatedSpriteCount, AffectedFlipbookCount));
	FMessageDialog::Open(EAppMsgType::Ok, DialogText, DialogTitle);
}
