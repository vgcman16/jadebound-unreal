#pragma once

#include "Domain/LegacyMigration.h"
#include "JadeboundProgressionActor.h"

// Server-internal pure adapters. No profile access, clocks, RPCs or awards.
// Both legacy and current decoding publish output only after full validation.
namespace Jadebound::Persistence
{
bool EncodeStateV2(const Snapshot& Source, FJadeboundProgressionState& Out);
bool DecodeStateV2(const FJadeboundProgressionState& Source, Snapshot& Out);
bool DecodeStateV1(const FJadeboundProgressionState& Source, JadeboundLegacyV1::Snapshot& Out);
bool ValidateDropsV2(const Snapshot& Candidate, const TArray<FJadeboundSavedDrop>& Drops);
bool ValidateDropsV1(const JadeboundLegacyV1::Snapshot& Candidate, const TArray<FJadeboundSavedDrop>& Drops);
// A format changes filenames only. Both versions share the character directory
// and its existing development Character.writer lease.
bool TryJournalPath(bool bDevelopment, const FString& CharacterId, int32 Slot, bool bVersion2, FString& Out);
// Guard-only canonical future filenames; no V3 codec, migration or support.
// Current actor queries only while its original writer lease remains held.
bool TryForwardV3JournalPath(bool bDevelopment, const FString& CharacterId, int32 Slot, FString& Out);
// Pure conversion retains the old revision; this separate representation
// candidate advances it exactly once for the actor's verified durable write.
bool BuildMigrationCandidate(const JadeboundLegacyV1::Snapshot& Legacy, Snapshot& Out);
}
