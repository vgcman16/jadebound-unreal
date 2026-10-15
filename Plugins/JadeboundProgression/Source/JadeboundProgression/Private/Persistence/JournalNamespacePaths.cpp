#include "Persistence/JournalNamespacePaths.h"
#include "Persistence/ProgressionSaveV2.h"
#include "Domain/ProgressionCore.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/Paths.h"

namespace Jadebound::Persistence
{
static_assert(SnapshotVersion == 3, "Review all namespace and forward guards when advancing the current format");
bool BuildJournalNamespacePaths(bool bDevelopment, const FString& CharacterId, JournalNamespacePaths& Out)
{
    Out = {};
    JournalNamespacePaths Candidate;
    for (int32 Slot = 0; Slot < 2; ++Slot)
    {
        if (!TryJournalPath(bDevelopment,CharacterId,Slot,false,Candidate.Banks[0][Slot]) ||
            !TryJournalPath(bDevelopment,CharacterId,Slot,true,Candidate.Banks[1][Slot]) ||
            !TryForwardV3JournalPath(bDevelopment,CharacterId,Slot,Candidate.Banks[2][Slot])) return false;
        const TCHAR* ForwardName = bDevelopment
            ? (Slot == 0 ? TEXT("Progression_V4_A.sav") : TEXT("Progression_V4_B.sav"))
            : (Slot == 0 ? TEXT("Jadebound_Traveler01_Progression_V4_A.sav") : TEXT("Jadebound_Traveler01_Progression_V4_B.sav"));
        Candidate.ForwardV4[Slot] = FPaths::Combine(FPaths::GetPath(Candidate.Banks[2][Slot]),ForwardName);
    }
    Out = MoveTemp(Candidate);
    return true;
}
bool HasJournalEntry(IPlatformFile& Files,const FString& Path)
{
    // Presence, not decode validity. Directories and partial first banks must
    // never expose a fresh character or revive an older-format history.
    return Files.FileExists(*Path) || Files.DirectoryExists(*Path) || Files.GetStatData(*Path).bIsValid;
}
JournalPresence ObserveJournalPresence(IPlatformFile& Files,const JournalNamespacePaths& Paths)
{
    return {HasJournalEntry(Files,Paths.Banks[0][0]),HasJournalEntry(Files,Paths.Banks[0][1]),
            HasJournalEntry(Files,Paths.Banks[1][0]),HasJournalEntry(Files,Paths.Banks[1][1]),
            HasJournalEntry(Files,Paths.Banks[2][0]),HasJournalEntry(Files,Paths.Banks[2][1])};
}
bool HasForwardV4Entry(IPlatformFile& Files,const JournalNamespacePaths& Paths)
{
    return HasJournalEntry(Files,Paths.ForwardV4[0]) || HasJournalEntry(Files,Paths.ForwardV4[1]);
}
}
