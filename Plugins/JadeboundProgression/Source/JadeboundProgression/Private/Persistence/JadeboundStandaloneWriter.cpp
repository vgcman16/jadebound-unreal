#include "JadeboundProgressionActor.h"

#include "Engine/World.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"

bool AJadeboundProgressionActor::HasJournalWriterLease() const
{
    return bDevelopmentModel
        ? HasDevelopmentContext() && static_cast<bool>(DevelopmentWriterLease)
        : static_cast<bool>(StandaloneWriterLease);
}

bool AJadeboundProgressionActor::AcquireStandaloneWriterLease()
{
    if (!IsInGameThread() || !HasAuthority() || GetNetMode() == NM_Client || bDevelopmentModel ||
        !GetWorld() || GetWorld()->bIsTearingDown || IsActorBeingDestroyed() || bPersistenceEndingPlay ||
        GetSaveCharacterId() != TEXT("Traveler01")) return false;
    if (StandaloneWriterLease) return true;
    // Only startup admits a new writer. A held candidate must retain its original
    // lease; it must never reacquire ownership and overwrite a newer character.
    if (bUnsavedCandidate || HeldSaveCandidate || bPersistenceBlocked ||
        PersistencePresentation.FailureKind != EJadeboundPersistenceFailureKind::None) return false;

    const FString BankPath = GetJournalPath(0);
    if (BankPath.IsEmpty())
    {
        SetPersistenceFailure(EJadeboundPersistenceFailureKind::RecoveryRequired,
            FText::FromString(TEXT("Your character's save location is unavailable. Existing progress is protected.")));
        return false;
    }
    IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
    const FString Directory = FPaths::GetPath(BankPath);
    if (!Files.CreateDirectoryTree(*Directory))
    {
        SetPersistenceFailure(EJadeboundPersistenceFailureKind::RecoveryRequired,
            FText::FromString(TEXT("Your character's save folder could not be opened. Existing progress is protected.")));
        return false;
    }
    // Both journal schemas share this companion. File presence is not ownership:
    // on the Windows backend the retained handle denies another write opener.
    // OPEN_ALWAYS preserves it; no journal or companion payload is overwritten.
    const FString Companion = FPaths::Combine(Directory, TEXT("Jadebound_Traveler01_Progression.writer"));
    StandaloneWriterLease.Reset(Files.OpenWrite(*Companion, true, false));
    if (!StandaloneWriterLease)
    {
        // OpenWrite does not distinguish contention from other IO failures.
        SetPersistenceFailure(EJadeboundPersistenceFailureKind::RecoveryRequired,
            FText::FromString(TEXT("Your character's progress is already open or unavailable. Close any other Jadebound session using this character and restart. Existing saves are protected.")));
        return false;
    }
    return true;
}
