#pragma once

#include "Domain/ProgressionCore.h"
#include "JadeboundProgressionActor.h"

namespace Jadebound::Persistence
{
// Pure native/reflected adapters for the explicitly current V3 domain.
// No file paths, journal IO, migration, actor mutation, RPC or item creation.
// False returns preserve Out. Encoding and decoding publish only validated locals.
bool EncodeStateV3(const Snapshot& Source, FJadeboundProgressionState& Out);
bool DecodeStateV3(const FJadeboundProgressionState& Source, Snapshot& Out);
bool ValidateDropsV3(const Snapshot& Candidate, const TArray<FJadeboundSavedDrop>& Drops);
}
