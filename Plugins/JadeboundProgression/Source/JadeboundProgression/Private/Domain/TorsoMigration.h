#pragma once

#include "Domain/ProgressionCore.h"
#include "Domain/LegacyCoreV2/ProgressionCore.h"

namespace Jadebound
{
// Pure validated conversion. No files, writer lease, awards or live actor state.
// This retains the exact old revision; it is not the durable migration write.
Result ConvertLegacyV2Snapshot(const JadeboundLegacyV2::Snapshot& Source, Snapshot& Out);

// A distinct representation candidate advances once. Caller must keep the old
// physical banks and remain unready until the entire candidate is read back.
bool BuildTorsoMigrationCandidate(const JadeboundLegacyV2::Snapshot& Source, Snapshot& Out);
}
