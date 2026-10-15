#include "Domain/TorsoMigration.h"

#include <type_traits>
#include <utility>

namespace Jadebound
{
static_assert(JadeboundLegacyV2::SnapshotVersion == 2, "Keep the historical decoder fixed");
static_assert(SnapshotVersion == 3, "This conversion is explicitly v2 to v3");
static_assert(InventorySlotCount == JadeboundLegacyV2::InventorySlotCount, "Retain all slots");
static_assert(MaximumPersistedCounter == JadeboundLegacyV2::MaximumPersistedCounter,
              "Do not change signed persistence bounds through migration");
static_assert(std::is_nothrow_move_assignable<Snapshot>::value,
              "Publish the complete output atomically");

Result ConvertLegacyV2Snapshot(const JadeboundLegacyV2::Snapshot& Source, Snapshot& Out)
{
    const auto ValidOld = JadeboundLegacyV2::ProgressionCore::ValidateSnapshot(Source);
    if (!ValidOld.Succeeded())
        return {static_cast<ResultCode>(ValidOld.Code),
                static_cast<ValidationCode>(ValidOld.Validation), false, Source.Revision};
    Snapshot Candidate;
    Candidate.Revision = Source.Revision;
    Candidate.Coins = Source.Coins;
    Candidate.EquippedWeapon = static_cast<ItemId>(Source.EquippedWeapon);
    Candidate.EquippedTorso = ItemId::None;
    Candidate.Quest = static_cast<QuestState>(Source.Quest);
    Candidate.NextDropId = Source.NextDropId;
    Candidate.QuestKillDropId = Source.QuestKillDropId;
    for (std::uint32_t I = 0; I < InventorySlotCount; ++I)
        Candidate.Inventory[I] = {static_cast<ItemId>(Source.Inventory[I].Id), Source.Inventory[I].Count};
    // Allocate/copy every variable-sized field before touching output.
    Candidate.UnprocessedDropIds = Source.UnprocessedDropIds;
    const auto ValidNew = ProgressionCore::ValidateSnapshot(Candidate);
    if (!ValidNew.Succeeded()) return ValidNew;
    Out = std::move(Candidate);
    return {ResultCode::Ok, ValidationCode::None, true, Out.Revision};
}

bool BuildTorsoMigrationCandidate(const JadeboundLegacyV2::Snapshot& Source, Snapshot& Out)
{
    Snapshot Candidate;
    if (!ConvertLegacyV2Snapshot(Source, Candidate).Succeeded() ||
        Candidate.Revision >= MaximumPersistedCounter) return false;
    ++Candidate.Revision;
    if (!ProgressionCore::ValidateSnapshot(Candidate).Succeeded()) return false;
    Out = std::move(Candidate);
    return true;
}
}
