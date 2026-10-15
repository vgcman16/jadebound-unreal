#pragma once

#include "Domain/ProgressionCore.h"
#include "Domain/LegacyCore/ProgressionCore.h"

namespace Jadebound
{
// Pure V1 -> validated frozenV2 -> frozen Source115 V3 conversion. Retains the
// source revision and owned state; initializes EquippedTorso=None. No rewards,
// loot grants, IO, actor/profile publication or journal bank changes.
// Invalid input returns failure. Allocation exceptions propagate. Every failure
// or exception preserves Out; Source is never modified.
Result Source145ConvertLegacyV1ToTorsoSnapshot(const JadeboundLegacyV1::Snapshot& Source,
                                             Snapshot& Out);

// Separate representation-write candidate: converts into locals, increments
// source revision exactly once, refuses MaximumPersistedCounter, validates V3,
// then publishes Out. Repeating with the same Source is deterministic and never
// increments the previous Out again. The caller still owns durable bank writing,
// readback, writer lease and readiness; none of those are implemented here.
// false or an allocation exception preserves every field of Out.
bool Source145BuildLegacyV1TorsoMigrationCandidate(const JadeboundLegacyV1::Snapshot& Source,
                                                Snapshot& Out);
}
