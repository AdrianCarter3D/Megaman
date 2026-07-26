// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsepriteImporterStyle.h"
#include "AsepriteImporter.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/SlateStyleRegistry.h"
#include "Slate/SlateGameResources.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleMacros.h"

#define RootToContentDir Style->RootToContentDir

TSharedPtr<FSlateStyleSet> FAsepriteImporterStyle::StyleInstance = nullptr;

void FAsepriteImporterStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FAsepriteImporterStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FAsepriteImporterStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("AsepriteImporterStyle"));
	return StyleSetName;
}

const FVector2D Icon64x64(64.0f, 64.0f);

TSharedRef<FSlateStyleSet> FAsepriteImporterStyle::Create()
{
	TSharedRef<FSlateStyleSet> Style = MakeShareable(new FSlateStyleSet("AsepriteImporterStyle"));
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("AsepriteImporter")->GetBaseDir() / TEXT("Resources"));

	FString ContentDir = IPluginManager::Get().FindPlugin("AsepriteImporter")->GetBaseDir() / TEXT("Resources");
	// UE_LOG(LogTemp, Warning, TEXT("ContentDir: %s"), *ContentDir);
	// Custom Asset Thunbnail 변경
	Style->Set("ClassThumbnail.Aseprite", new IMAGE_BRUSH_SVG(TEXT("ButtonIcon"), Icon64x64));
	// Style->Set("AsepriteImporter.PluginAction", new IMAGE_BRUSH_SVG(TEXT("ButtonIcon"), Icon64x64));
	return Style;
}

void FAsepriteImporterStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle &FAsepriteImporterStyle::Get()
{
	return *StyleInstance;
}
