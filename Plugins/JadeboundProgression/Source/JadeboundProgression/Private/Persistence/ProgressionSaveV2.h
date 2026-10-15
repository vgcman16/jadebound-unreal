#pragma once

#include "Domain/LegacyCore/ProgressionCore.h"
#include "Domain/LegacyCoreV2/ProgressionCore.h"
#include "JadeboundProgressionActor.h"

// Server-internal pure adapters. No profile access, clocks, RPCs or awards.
// Historical formats are bound to frozen types; outputs publish only after full validation.
// Current V3 actor/loot callers must use ProgressionSaveV3 instead.
namespace Jadebound::Persistence
{
bool EncodeStateV2(const JadeboundLegacyV2::Snapshot& Source, FJadeboundProgressionState& Out);
bool DecodeStateV2(const FJadeboundProgressionState& Source, JadeboundLegacyV2::Snapshot& Out);
bool DecodeStateV1(const FJadeboundProgressionState& Source, JadeboundLegacyV1::Snapshot& Out);
bool ValidateDropsV2(const JadeboundLegacyV2::Snapshot& Candidate, const TArray<FJadeboundSavedDrop>& Drops);
bool ValidateDropsV1(const JadeboundLegacyV1::Snapshot& Candidate, const TArray<FJadeboundSavedDrop>& Drops);
// A format changes filenames only. Both versions share the character directory
// and its existing development Character.writer lease.
bool TryJournalPath(bool bDevelopment, const FString& CharacterId, int32 Slot, bool bVersion2, FString& Out);
// Historical guarded-V2 canonical future filenames. This helper does not select formats.
// The separately integrated V3 actor must perform its own explicit format selection.
bool TryForwardV3JournalPath(bool bDevelopment, const FString& CharacterId, int32 Slot, FString& Out);
// Pure conversion retains the old revision; this separate representation
// candidate advances it exactly once for the actor's verified durable write.
bool BuildMigrationCandidate(const JadeboundLegacyV1::Snapshot& Legacy, JadeboundLegacyV2::Snapshot& Out);
}
