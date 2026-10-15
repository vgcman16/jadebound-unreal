#include "Domain/FrozenV2Migration.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace Jadebound::FrozenV2Migration {

JadeboundLegacyV2::Result ConvertLegacyV1Snapshot(const JadeboundLegacyV1::Snapshot& legacy, JadeboundLegacyV2::Snapshot& output) {
    const auto validLegacy = JadeboundLegacyV1::ProgressionCore::ValidateSnapshot(legacy);
    if (!validLegacy.Succeeded()) {
        // v2 appends enum values; all legacy error values keep their numbers.
        return {static_cast<JadeboundLegacyV2::ResultCode>(validLegacy.Code),
                static_cast<JadeboundLegacyV2::ValidationCode>(validLegacy.Validation), false, legacy.Revision};
    }
    JadeboundLegacyV2::Snapshot converted;
    converted.Revision = legacy.Revision;
    converted.Coins = legacy.Coins;
    for (std::uint32_t slot = 0; slot < JadeboundLegacyV2::InventorySlotCount; ++slot) {
        converted.Inventory[slot] = {static_cast<JadeboundLegacyV2::ItemId>(legacy.Inventory[slot].Id),
                                     legacy.Inventory[slot].Count};
    }
    converted.EquippedWeapon = static_cast<JadeboundLegacyV2::ItemId>(legacy.EquippedWeapon);
    converted.Quest = static_cast<JadeboundLegacyV2::QuestState>(legacy.Quest);
    converted.NextDropId = legacy.NextDropId;
    converted.QuestKillDropId = legacy.QuestKillDropId;
    auto processed = legacy.ProcessedDropIds;
    std::sort(processed.begin(), processed.end());
    // Exact v1 validation proves recorded IDs are contiguous and ordered;
    // processed IDs can have been picked up in any order.
    std::set_difference(legacy.RecordedDropIds.begin(), legacy.RecordedDropIds.end(),
                        processed.begin(), processed.end(),
                        std::back_inserter(converted.UnprocessedDropIds));
    const auto validConverted = JadeboundLegacyV2::ProgressionCore::ValidateSnapshot(converted);
    if (!validConverted.Succeeded()) {
        return validConverted;
    }
    output = std::move(converted);
    return {JadeboundLegacyV2::ResultCode::Ok, JadeboundLegacyV2::ValidationCode::None, true, output.Revision};
}

} // namespace Jadebound::FrozenV2Migration
