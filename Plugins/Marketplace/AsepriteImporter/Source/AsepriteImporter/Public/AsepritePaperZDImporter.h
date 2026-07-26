// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UAseprite;

/**
 * Helper class to automate PaperZD asset creation during Aseprite import.
 */
class ASEPRITEIMPORTER_API FAsepritePaperZDImporter
{
public:
	/**
	 * Processes the imported Aseprite asset to create corresponding PaperZD Animation Source and sequences.
	 * @param SpriteSheet The imported Aseprite asset containing flipbooks.
	 * @param bCreateAnimBPDraft When true, also creates an AnimBP draft with one state per sequence.
	 */
	static void ProcessPaperZDAssets(UAseprite* SpriteSheet, bool bCreateAnimBPDraft);
};
