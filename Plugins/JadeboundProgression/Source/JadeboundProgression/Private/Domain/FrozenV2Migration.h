#pragma once

#include "Domain/LegacyCore/ProgressionCore.h"
#include "Domain/LegacyCoreV2/ProgressionCore.h"

namespace Jadebound::FrozenV2Migration
{
// Exact historical V1 -> V2 pure conversion, retained for the V2 API only.
// No revision increment, reward, native state publication or journal access.
// The current V3 actor must use the separately reviewed Source145 bridge.
JadeboundLegacyV2::Result ConvertLegacyV1Snapshot(const JadeboundLegacyV1::Snapshot& Source,
                                                JadeboundLegacyV2::Snapshot& Out);
}
