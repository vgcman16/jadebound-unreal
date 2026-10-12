#pragma once

#include "JadeboundProgressionActor.h"
#include "Domain/ProgressionCore.h"

namespace Jadebound::Presentation
{
// ExactRevision is save-only. Clients derive its presentation shadow from the
// owner-only signed wire revision; authority still checks its own Core snapshot.
inline bool ApplyReplicatedInventoryRevision(FJadeboundProgressionState& State)
{
    State.ExactRevision = State.Revision < 0 ? 0 : static_cast<uint64>(State.Revision);
    return State.Version == static_cast<int32>(Jadebound::SnapshotVersion) && State.Revision >= 0 &&
        State.Slots.Num() == static_cast<int32>(Jadebound::InventorySlotCount);
}
}
