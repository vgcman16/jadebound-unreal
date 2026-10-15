#include "Domain/LegacyMigration.h"
#include "Domain/FrozenV2Migration.h"

namespace Jadebound
{
JadeboundLegacyV2::Result ConvertLegacyV1Snapshot(
    const JadeboundLegacyV1::Snapshot& Source, JadeboundLegacyV2::Snapshot& Out)
{
    return FrozenV2Migration::ConvertLegacyV1Snapshot(Source, Out);
}
}
