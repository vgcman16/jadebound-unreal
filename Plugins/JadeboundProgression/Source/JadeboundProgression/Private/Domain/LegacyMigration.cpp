#include "LegacyMigration.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace Jadebound {

Result ConvertLegacyV1Snapshot(const JadeboundLegacyV1::Snapshot& legacy, Snapshot& output) {
    const auto validLegacy = JadeboundLegacyV1::ProgressionCore::ValidateSnapshot(legacy);
    if (!validLegacy.Succeeded()) {
        // v2 appends enum values; all legacy error values keep their numbers.
        return {static_cast<ResultCode>(validLegacy.Code),
                static_cast<ValidationCode>(validLegacy.Validation), false, legacy.Revision};
    }
    Snapshot converted;
    converted.Revision = legacy.Revision;
    converted.Coins = legacy.Coins;
    for (std::uint32_t slot = 0; slot < InventorySlotCount; ++slot) {
        converted.Inventory[slot] = {static_cast<ItemId>(legacy.Inventory[slot].Id),
                                     legacy.Inventory[slot].Count};
    }
    converted.EquippedWeapon = static_cast<ItemId>(legacy.EquippedWeapon);
    converted.Quest = static_cast<QuestState>(legacy.Quest);
    converted.NextDropId = legacy.NextDropId;
    converted.QuestKillDropId = legacy.QuestKillDropId;
    auto processed = legacy.ProcessedDropIds;
    std::sort(processed.begin(), processed.end());
    // Exact v1 validation proves recorded IDs are contiguous and ordered;
    // processed IDs can have been picked up in any order.
    std::set_difference(legacy.RecordedDropIds.begin(), legacy.RecordedDropIds.end(),
                        processed.begin(), processed.end(),
                        std::back_inserter(converted.UnprocessedDropIds));
    const auto validConverted = ProgressionCore::ValidateSnapshot(converted);
    if (!validConverted.Succeeded()) {
        return validConverted;
    }
    output = std::move(converted);
    return {ResultCode::Ok, ValidationCode::None, true, output.Revision};
}

} // namespace Jadebound
