// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsepriteImporter.h"
#include "AsepriteImporterStyle.h"
#include "AsepriteImporterCommands.h"
#include "Misc/MessageDialog.h"
#include "ToolMenus.h"
#include "DesktopPlatformModule.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"
#include "UObject/NoExportTypes.h"
#include "IDesktopPlatform.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/ScopedSlowTask.h"
// #include "AssetViewUtils.h"
// #include "EditorReimportHandler.h"
#include "AssetToolsModule.h"
#include "AsepriteAssetTypeActions.h"
#include "ISettingsModule.h"
#include "SAsepritePathDialog.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "AsepriteImporterLog.h"

#include "PaperFlipbook.h"

static const FName AsepriteImporterTabName("AsepriteImporter");

#define LOCTEXT_NAMESPACE "FAsepriteImporterModule"

FString GetAsepriteExecutablePath()
{
	FString MainDrivePath;

#if PLATFORM_WINDOWS
	MainDrivePath = TEXT("C:\\");
#elif PLATFORM_MAC || PLATFORM_LINUX
	MainDrivePath = TEXT("/");
#endif

	FString AsepriteExecutablePath;

#if PLATFORM_WINDOWS
	// Steam 경로
	AsepriteExecutablePath = FPaths::Combine(MainDrivePath, TEXT("Program Files (x86)"), TEXT("Steam"), TEXT("steamapps"), TEXT("common"), TEXT("Aseprite"), TEXT("Aseprite.exe"));
	if (FPaths::FileExists(AsepriteExecutablePath))
	{
		return AsepriteExecutablePath;
	}

	// 일반 설치 경로 (x86)
	AsepriteExecutablePath = FPaths::Combine(MainDrivePath, TEXT("Program Files (x86)"), TEXT("Aseprite"), TEXT("Aseprite.exe"));
	if (FPaths::FileExists(AsepriteExecutablePath))
	{
		return AsepriteExecutablePath;
	}

	// 일반 설치 경로
	AsepriteExecutablePath = FPaths::Combine(MainDrivePath, TEXT("Program Files"), TEXT("Aseprite"), TEXT("Aseprite.exe"));

#elif PLATFORM_MAC
	// Steam 경로
	FString UserHomeDir = FPlatformProcess::UserDir();
	AsepriteExecutablePath = FPaths::Combine(UserHomeDir, TEXT("Library"), TEXT("Application Support"), TEXT("Steam"), TEXT("steamapps"), TEXT("common"), TEXT("Aseprite"), TEXT("Aseprite.app"), TEXT("Contents"), TEXT("MacOS"), TEXT("aseprite"));
	if (FPaths::FileExists(AsepriteExecutablePath))
	{
		return AsepriteExecutablePath;
	}

	// 일반 설치 경로
	AsepriteExecutablePath = TEXT("/Applications/Aseprite.app/Contents/MacOS/aseprite");

#elif PLATFORM_LINUX
	// Steam 경로만 검사
	FString UserHomeDir = FPlatformProcess::UserDir();
	AsepriteExecutablePath = FPaths::Combine(UserHomeDir, TEXT(".steam"), TEXT("debian-installation"), TEXT("steamapps"), TEXT("common"), TEXT("Aseprite"), TEXT("aseprite"));
#endif

	if (FPaths::FileExists(AsepriteExecutablePath))
	{
		return AsepriteExecutablePath;
	}
	else
	{
		return TEXT(""); // 빈 문자열을 반환하거나 원하는 에러 메시지를 반환할 수 있습니다.
	}
}

UAsepriteImporterSettings::UAsepriteImporterSettings(const FObjectInitializer &ObjectInitializer)
	: Super(ObjectInitializer)
{
	// TODO SpriteMaterial의 기본값 설정 필요
}

void FAsepriteImporterModule::StartupModule()
{
	CheckAndRequestAsepriteExecutablePath();
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

	// Plugin 버튼을 주석처리 이제 Factory로 불러오면 된다
	// Aseprite Asset Style
	FAsepriteImporterStyle::Initialize();
	FAsepriteImporterStyle::ReloadTextures();


	// FAsepriteImporterCommands::Register();
	// PluginCommands = MakeShareable(new FUICommandList);
	// PluginCommands->MapAction(
	// 	FAsepriteImporterCommands::Get().PluginAction,
	// 	FExecuteAction::CreateRaw(this, &FAsepriteImporterModule::PluginButtonClicked),
	// 	FCanExecuteAction());

	// UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAsepriteImporterModule::RegisterMenus));

	// Register asset types
	IAssetTools &AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	SpriteSheetImportAssetTypeActions = MakeShareable(new FAsepriteAssetTypeActions);
	AssetTools.RegisterAssetTypeActions(SpriteSheetImportAssetTypeActions.ToSharedRef());

	// 사용자 설정을 등록합니다.
	ISettingsModule *SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if (SettingsModule != nullptr)
	{
		SettingsModule->RegisterSettings("Project", "Plugins", "Aseprite Importer",
										 LOCTEXT("RuntimeSettingsName", "Aseprite Importer"),
										 LOCTEXT("RuntimeSettingsDescription", "Configure the Aseprite Importer plugin"),
										 GetMutableDefault<UAsepriteImporterSettings>());
	}
}

void FAsepriteImporterModule::OnPostEngineInit()
{
	// UThumbnailManager::Get().RegisterCustomRenderer(UAseprite::StaticClass(), UAsepriteThumbnailRenderer::StaticClass());
}

void FAsepriteImporterModule::ShutdownModule()
{
	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FAsepriteImporterModule::OnPostEngineInit);
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	// 플러그인 버튼은 숨긴다
	// UToolMenus::UnRegisterStartupCallback(this);
	// UToolMenus::UnregisterOwner(this);

	// Aseprite Asset Style
	FAsepriteImporterStyle::Shutdown();
	// FAsepriteImporterCommands::Unregister();

	// Unregister asset types
	if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
	{
		IAssetTools &AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
		if (SpriteSheetImportAssetTypeActions.IsValid())
		{
			AssetTools.UnregisterAssetTypeActions(SpriteSheetImportAssetTypeActions.ToSharedRef());
		}
	}

	// // 사용자 설정을 해제합니다.
	ISettingsModule *SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if (SettingsModule != nullptr)
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "AsepriteImporter");
	}
}

bool FAsepriteImporterModule::CheckAndRequestAsepriteExecutablePath()
{
	UAsepriteImporterSettings *Settings = GetMutableDefault<UAsepriteImporterSettings>();
	FString AsepriteExecutablePath = Settings->AsepriteExecutablePath;

	// 경로 유효성 검증
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	bool bPathValid = !AsepriteExecutablePath.IsEmpty() &&
					  PlatformFile.FileExists(*AsepriteExecutablePath) &&
					  FPaths::GetCleanFilename(AsepriteExecutablePath).Equals(TEXT("Aseprite.exe"), ESearchCase::IgnoreCase);

	if (bPathValid)
	{
		UE_LOG(LogAsepriteImporter, Log, TEXT("Aseprite path is valid: %s"), *AsepriteExecutablePath);
		return true;
	}

	// 자동으로 경로 찾기 시도
	FString AutoFoundPath = GetAsepriteExecutablePath();
	if (!AutoFoundPath.IsEmpty() && PlatformFile.FileExists(*AutoFoundPath))
	{
		UE_LOG(LogAsepriteImporter, Log, TEXT("Aseprite path auto-detected: %s"), *AutoFoundPath);
		Settings->AsepriteExecutablePath = AutoFoundPath;
		Settings->SaveConfig();
		return true;
	}

	// 자동으로 찾지 못했으면 사용자에게 선택 다이얼로그 표시
	TSharedRef<SAsepritePathDialog> PathDialog = SNew(SAsepritePathDialog)
		.CurrentPath(AsepriteExecutablePath);

	bool bUserAccepted = PathDialog->Show();

	if (bUserAccepted)
	{
		FString SelectedPath = PathDialog->GetSelectedPath();
		Settings->AsepriteExecutablePath = SelectedPath;
		Settings->SaveConfig();
		UE_LOG(LogAsepriteImporter, Log, TEXT("Aseprite path configured by user: %s"), *SelectedPath);
		return true;
	}
	else
	{
		UE_LOG(LogAsepriteImporter, Warning, TEXT("User cancelled Aseprite path configuration"));
		return false;
	}
}

// void FAsepriteImporterModule::PluginButtonClicked()
// {
// 	IPlatformFile &PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
// 	if (PlatformFile.CreateDirectoryTree(*FPaths::ProjectDir()))
// 	{
// 		bool result = CheckAndRequestAsepriteExecutablePath();
// 		if (result == false)
// 		{
// 			FText DialogTitle = FText::FromString("Aseprite Path Error");
// 			FText DialogText = FText::FromString("Could not find Aseprite executable. Plugin will not work without the correct path.");
// 			FMessageDialog::Open(EAppMsgType::Ok, DialogText, &DialogTitle);
// 			return;
// 		}

// 		IDesktopPlatform *DesktopPlatform = FDesktopPlatformModule::Get();
// 		if (DesktopPlatform)
// 		{
// 			// 다이얼로그 설정
// 			TArray<FString> OutFiles;
// 			FString FileTypes = TEXT("Aseprite Files (*.ase;*.aseprite)|*.ase;*.aseprite");
// 			// FString DefaultPath = FPaths::ProjectDir();

// 			bool bOpened = DesktopPlatform->OpenFileDialog(
// 				FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
// 				TEXT("Select an Aseprite File"),
// 				TEXT(""),
// 				TEXT(""),
// 				FileTypes,
// 				// EFileDialogFlags::None,
// 				EFileDialogFlags::Multiple, // 여러 개 동시에 가능하도록 처리
// 				OutFiles);

// 			FAssetRegistryModule &AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

// 			if (bOpened)
// 			{
// 				UE_LOG(LogTemp, Warning, TEXT("Opened"));
// 				int32 TotalSteps = OutFiles.Num();
// 				FScopedSlowTask SlowTask(TotalSteps, FText::FromString(TEXT("Processing Files...")));
// 				SlowTask.MakeDialog();
// 				int32 SuccessCount = 0;

// 				for (int32 i = 0; i < TotalSteps; i++)
// 				{
// 					SlowTask.EnterProgressFrame(1); // 로딩 바를 1 단계씩 증가시킵니다.
// 					FString AsepriteFilePath = OutFiles[i];
// 					UE_LOG(LogTemp, Warning, TEXT("ChosenFilePath: %s"), *AsepriteFilePath);

// 					// FString AsepriteExecutablePath = TEXT("C:\\Program Files (x86)\\Steam\\steamapps\\common\\Aseprite\\Aseprite.exe"); // 여기에 Aseprite 실행 파일 경로를 지정합니다.
// 					UAsepriteImporterSettings *Settings = GetMutableDefault<UAsepriteImporterSettings>();
// 					FString AsepriteExecutablePath = Settings->AsepriteExecutablePath;

// 					UE_LOG(LogTemp, Warning, TEXT("Aseprite Path: %s"), *AsepriteExecutablePath);

// 					// FString OutputSpriteSheet = AsepriteFilePath.Replace(TEXT(".aseprite"), TEXT(".png"));
// 					// FString OutputJson = AsepriteFilePath.Replace(TEXT(".aseprite"), TEXT(".json"));
// 					FString TempFilePath = FPaths::CreateTempFilename(*FPaths::ProjectIntermediateDir());
// 					FString FileNameWithoutExt = FPaths::GetBaseFilename(AsepriteFilePath);
// 					FString OutputSpriteSheet = FPaths::Combine(TempFilePath, TEXT("Tex_") + FileNameWithoutExt.Replace(TEXT(" "), TEXT("_")) + TEXT(".png"));
// 					FString OutputJson = FPaths::Combine(TempFilePath, TEXT("SpriteSheet_") + FileNameWithoutExt.Replace(TEXT(" "), TEXT("_")) + TEXT(".json"));

// 					FString SplitLayer = Settings->bSplitLayers ? FString("--split-layers") : FString("");
// 					UE_LOG(LogTemp, Warning, TEXT("SplitLayer: %s"), *SplitLayer);
// 					// FString OutputSpriteSheet = ContentDirectory.Replace(TEXT(".aseprite"), TEXT(".png"));
// 					// FString OutputJson = ContentDirectory.Replace(TEXT(".aseprite"), TEXT(".json"));
// 					FString CommandLine = FString::Printf(TEXT("-b  --sheet \"%s\" --sheet-type packed \"%s\" --format json-hash --list-layers --list-tags --list-slices --data \"%s\" \"%s\""), *OutputSpriteSheet, *SplitLayer, *OutputJson, *AsepriteFilePath);

// 					int32 ReturnCode;
// 					FString StdOut;
// 					FString StdErr;

// 					FPlatformProcess::ExecProcess(*AsepriteExecutablePath, *CommandLine, &ReturnCode, &StdOut, &StdErr);

// 					UE_LOG(LogTemp, Warning, TEXT("StdErr: %s"), *StdErr);
// 					UE_LOG(LogTemp, Warning, TEXT("StdOut: %s"), *StdOut);
// 					UE_LOG(LogTemp, Warning, TEXT("ReturnCode: %d"), ReturnCode);

// 					if (ReturnCode == 0)
// 					{
// 						UE_LOG(LogTemp, Warning, TEXT("Copying files..."));
// 						FString ContentDirectory = FPaths::ProjectContentDir();
// 						// FString FileExtensionJson = FPaths::GetExtension(OutputJson);
// 						// FString FileExtensionSpriteSheet = FPaths::GetExtension(OutputSpriteSheet);
// 						// FString TargetPathJson = ContentDirectory + "/" + FPaths::GetCleanFilename(OutputJson);
// 						// FString TargetPathSpriteSheet = ContentDirectory + "/" + FPaths::GetCleanFilename(OutputSpriteSheet);
// 						// FString BaseFileName = FPaths::GetBaseFilename(TargetPathJson);

// 						// int32 Counter = 1;

// 						// while (PlatformFile.FileExists(*TargetPathJson) || PlatformFile.FileExists(*TargetPathSpriteSheet))
// 						// {
// 						// 	TargetPathJson = FPaths::Combine(ContentDirectory, BaseFileName + FString::Printf(TEXT("_%d."), Counter) + FileExtensionJson);
// 						// 	TargetPathSpriteSheet = FPaths::Combine(ContentDirectory, BaseFileName + FString::Printf(TEXT("_%d."), Counter) + FileExtensionSpriteSheet);

// 						// 	Counter++;
// 						// }

// 						// 덮어 쓰는 것이 활용도가 더 높을 듯
// 						FString TargetPathJson = ContentDirectory + "/" + FPaths::GetCleanFilename(OutputJson);
// 						FString TargetPathSpriteSheet = ContentDirectory + "/" + FPaths::GetCleanFilename(OutputSpriteSheet);

// 						TArray<FString> FilesToDelete = {
// 							TargetPathJson,
// 							TargetPathSpriteSheet};

// 						// AssetViewUtils::DeleteAssets(FilesToDelete);
// 						if (PlatformFile.FileExists(*TargetPathJson))
// 						{
// 							bool deleted1 = PlatformFile.DeleteFile(*TargetPathJson);
// 							if (deleted1)
// 							{
// 								UE_LOG(LogTemp, Warning, TEXT("Exist JSON file deleted!"));
// 							}
// 						}
// 						if (PlatformFile.FileExists(*TargetPathSpriteSheet))
// 						{
// 							bool deleted2 = PlatformFile.DeleteFile(*TargetPathSpriteSheet);
// 							if (deleted2)
// 							{
// 								UE_LOG(LogTemp, Warning, TEXT("Exist JSON file deleted!"));
// 							}
// 						}

// 						bool bUseCopyFile = false;
// 						if (bUseCopyFile)
// 						{
// 							bool bSuccess1 = PlatformFile.CopyFile(*TargetPathJson, *OutputJson);
// 							bool bSuccess2 = PlatformFile.CopyFile(*TargetPathSpriteSheet, *OutputSpriteSheet);

// 							UE_LOG(LogTemp, Warning, TEXT("Deleting temp files..."));
// 							if (PlatformFile.FileExists(*OutputJson))
// 							{
// 								PlatformFile.DeleteFile(*OutputJson);
// 							}
// 							if (PlatformFile.FileExists(*OutputSpriteSheet))
// 							{
// 								PlatformFile.DeleteFile(*OutputSpriteSheet);
// 							}

// 							if (bSuccess1 && bSuccess2)
// 							{
// 								TArray<FString> FilesToScan = {
// 									TargetPathJson,
// 									TargetPathSpriteSheet};
// 								AssetRegistryModule.Get().ScanModifiedAssetFiles(FilesToScan);
// 								SuccessCount++;
// 							}
// 						}
// 						else
// 						{
// 							FAssetToolsModule &AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

// 							// 파일을 읽어서 임포트할 애셋으로 만듭니다.
// 							TArray<UObject *> ImportedAssets = AssetToolsModule.Get().ImportAssets({OutputJson}, TEXT("/Game/"));

// 							if (ImportedAssets.Num() > 0)
// 							{
// 								SuccessCount++;
// 								// 애셋이 성공적으로 임포트됨
// 							}
// 						}
// 					}
// 					else
// 					{
// 						UE_LOG(LogTemp, Warning, TEXT("Failed!"));
// 					}

// 					// // 2. Paper2D의 임포트 기능을 호출하여 에셋을 임포트합니다.
// 					// UAsepriteImportFactory *ImportFactory = NewObject<UAsepriteImportFactory>();
// 					// UObject *ImportedSpriteSheet = ImportFactory->FactoryCreateFile(UAseprite::StaticClass(), /*Parent=*/nullptr, /*Name=*/FName(*FPaths::GetBaseFilename(JsonFilePath)), RF_Public | RF_Standalone, 0, TEXT("json"), *OutputJson, /*Warn=*/nullptr);

// 					// // 3. 컨텐츠 브라우저에 에셋을 추가합니다.
// 					// if (ImportedSpriteSheet)
// 					// {
// 					// 	ImportedSpriteSheet->MarkPackageDirty();
// 					// 	FAssetRegistryModule::AssetCreated(ImportedSpriteSheet);

// 					// 	// 4. 에셋을 디스크에 저장합니다.
// 					// 	FString PackageFileName = FPackageName::LongPackageNameToFilename(ImportedSpriteSheet->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
// 					// 	UPackage::SavePackage(ImportedSpriteSheet->GetOutermost(), ImportedSpriteSheet, EObjectFlags::RF_Public | EObjectFlags::RF_Standalone, *PackageFileName);
// 					// }

// 					// ChosenFilePath = OutFiles[0];
// 				}

// 				AssetRegistryModule.Get().SearchAllAssets(true);

// 				UE_LOG(LogTemp, Warning, TEXT("Success!"));
// 				FText DialogTitle = FText::FromString("Aseprite Importer");
// 				FText DialogText = FText::FromString(FString::Printf(TEXT("%d out of %d Aseprite files have been successfully imported. Please wait a moment until the Content Browser recognizes the files."), SuccessCount, TotalSteps));
// 				FMessageDialog::Open(EAppMsgType::Ok, DialogText, &DialogTitle);
// 			}
// 		}
// 	}
// }

// void FAsepriteImporterModule::RegisterMenus()
// {
// 	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
// 	FToolMenuOwnerScoped OwnerScoped(this);

// 	{
// 		UToolMenu *Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
// 		{
// 			FToolMenuSection &Section = Menu->FindOrAddSection("WindowLayout");
// 			Section.AddMenuEntryWithCommandList(FAsepriteImporterCommands::Get().PluginAction, PluginCommands);
// 		}
// 	}

// 	{
// 		UToolMenu *ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
// 		{
// 			FToolMenuSection &Section = ToolbarMenu->FindOrAddSection("PluginTools");
// 			{
// 				FToolMenuEntry &Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FAsepriteImporterCommands::Get().PluginAction));
// 				Entry.SetCommandList(PluginCommands);
// 			}
// 		}
// 	}
// }

#undef LOCTEXT_NAMESPACE
IMPLEMENT_MODULE(FAsepriteImporterModule, AsepriteImporter)
DEFINE_LOG_CATEGORY(LogAsepriteImporter);