#pragma once

#include "CoreMinimal.h"
#include "Misc/Paths.h"

namespace Jadebound::Development
{
inline constexpr int32 MaxCharacters = 4;

// Server configuration only. A canonical GUID is one bounded path segment;
// login options, display names and RPC arguments never select this value.
inline bool IsCharacterId(const FString& Value)
{
    if (Value.Len() != 32) return false;
    bool bNonzero = false;
    for (TCHAR Ch : Value)
    {
        if (!((Ch >= TEXT('0') && Ch <= TEXT('9')) || (Ch >= TEXT('a') && Ch <= TEXT('f')))) return false;
        bNonzero |= Ch != TEXT('0');
    }
    return bNonzero;
}

inline bool IsLoopbackAddress(const FString& Address)
{
    return Address == TEXT("127.0.0.1") || Address == TEXT("::1") || Address == TEXT("[::1]");
}

inline bool TryJournalPath(const FString& CharacterId, int32 Slot, FString& OutPath)
{
    OutPath.Reset();
    if (!IsCharacterId(CharacterId) || (Slot != 0 && Slot != 1)) return false;
    OutPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"),
        TEXT("JadeboundDevelopment"), CharacterId, Slot == 0 ? TEXT("Progression_A.sav") : TEXT("Progression_B.sav"));
    return true;
}
}
