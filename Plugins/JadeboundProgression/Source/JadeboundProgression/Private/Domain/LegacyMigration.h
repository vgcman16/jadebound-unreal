#pragma once

#include "ProgressionCore.h"
#include "LegacyCore/ProgressionCore.h"

namespace Jadebound {

// Pure conversion only. Validate the exact namespaced v1 contract, retain its
// revision and owned state, then validate v2 before assigning output. Failure
// leaves output unchanged. This performs no journal write or gameplay commit.
// The actor must durably commit and read back v2 at a strictly higher revision
// before exposing the converted model as writable; that integration is pending.
Result ConvertLegacyV1Snapshot(const JadeboundLegacyV1::Snapshot& legacy, Snapshot& output);

} // namespace Jadebound
