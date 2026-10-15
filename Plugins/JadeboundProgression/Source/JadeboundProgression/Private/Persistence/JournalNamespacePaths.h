#pragma once

#include "CoreMinimal.h"
#include "Persistence/JournalFormatSelection.h"

class IPlatformFile;

namespace Jadebound::Persistence
{
struct JournalNamespacePaths
{
    FString Banks[3][2]; // V1/V2/V3, A/B. No inferred or client-provided filenames.
    FString ForwardV4[2]; // Presence guard only; no V4 codec or migration.
};
// Uses the existing character/slot admission and shared writer directory.
bool BuildJournalNamespacePaths(bool bDevelopment, const FString& CharacterId, JournalNamespacePaths& Out);
bool HasJournalEntry(IPlatformFile& Files, const FString& Path);
JournalPresence ObserveJournalPresence(IPlatformFile& Files, const JournalNamespacePaths& Paths);
bool HasForwardV4Entry(IPlatformFile& Files, const JournalNamespacePaths& Paths);
}
