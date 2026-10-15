#pragma once

#include "Domain/LegacyCore/ProgressionCore.h"
#include "Domain/LegacyCoreV2/ProgressionCore.h"

namespace Jadebound
{
// Historical V1-to-V2 API only: frozen types make its contract independent of
// the current V3 domain. Pure conversion retains revision and owned state.
// Validation/refusal/output preservation are delegated to exact Source146.
// Current V3 callers use their separate Source145 bridge; no implicit V3 output.
JadeboundLegacyV2::Result ConvertLegacyV1Snapshot(
    const JadeboundLegacyV1::Snapshot& Source, JadeboundLegacyV2::Snapshot& Out);
}
