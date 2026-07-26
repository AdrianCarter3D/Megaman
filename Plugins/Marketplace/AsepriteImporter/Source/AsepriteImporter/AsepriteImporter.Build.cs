// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AsepriteImporter : ModuleRules
{
	public AsepriteImporter(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		// CppStandard = CppStandardVersion.Latest;
		// bEnableNonInlinedGenCppWarnings = true;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				// ... add other public dependencies that you statically link with here ...
			}
			);


		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Projects",
				"InputCore",
				"EditorFramework",
				"DesktopPlatform",
				"Core",
				"CoreUObject",
				"Json",
				"Slate",
				"SlateCore",
				"Engine",
				"Paper2D",
				"UnrealEd",
				"Paper2DEditor",
				"AssetTools",
				"ContentBrowser",
				"ToolMenus",
				"PropertyEditor",
				"Niagara", // Niagara is required for the emitter
				"NiagaraEditor", // Niagara is required for the emitter
				// ... add private dependencies that you statically link with here ...	
			});

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"AssetRegistry"
			});

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"AssetRegistry"
			});
	}
}
