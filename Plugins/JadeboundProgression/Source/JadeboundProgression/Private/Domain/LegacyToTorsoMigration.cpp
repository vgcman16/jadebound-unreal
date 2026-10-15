#include "Domain/LegacyToTorsoMigration.h"
#include "Domain/TorsoMigration.h"

#include <algorithm>
#include <iterator>
#include <type_traits>
#include <utility>

namespace Jadebound
{
static_assert(JadeboundLegacyV1::SnapshotVersion == 1 && JadeboundLegacyV2::SnapshotVersion == 2 &&
              SnapshotVersion == 3, "This bridge binds the exact V1/V2/V3 representations");
static_assert(InventorySlotCount == JadeboundLegacyV1::InventorySlotCount &&
              InventorySlotCount == JadeboundLegacyV2::InventorySlotCount, "Retain every original inventory cell");
static_assert(MaximumPersistedCounter == JadeboundLegacyV2::MaximumPersistedCounter,
              "Retain the exact signed journal counter ceiling");
static_assert(JadeboundLegacyV1::MaximumRecordedDrops == JadeboundLegacyV2::MaximumUnprocessedDrops,
              "The entire possible V1 difference fits the frozen V2 outstanding ledger");
static_assert(static_cast<std::uint32_t>(JadeboundLegacyV1::ResultCode::NotConsumable) ==
              static_cast<std::uint32_t>(ResultCode::NotConsumable) &&
              static_cast<std::uint32_t>(JadeboundLegacyV1::ValidationCode::QuestDropMismatch) ==
              static_cast<std::uint32_t>(ValidationCode::QuestDropMismatch), "Historical errors retain their appended enum values");
static_assert(std::is_nothrow_move_assignable<Snapshot>::value,
              "Publish the complete V3 only after all allocations and validation");

Result Source145ConvertLegacyV1ToTorsoSnapshot(const JadeboundLegacyV1::Snapshot& Source,
                                             Snapshot& Out)
{
    const auto ValidV1 = JadeboundLegacyV1::ProgressionCore::ValidateSnapshot(Source);
    if (!ValidV1.Succeeded())
        return {static_cast<ResultCode>(ValidV1.Code), static_cast<ValidationCode>(ValidV1.Validation),
                false, Source.Revision};
    // V1 accepted uint64 revisions. V2/V3 use signed storage, so never truncate
    // even a historically valid V1 value before entering the frozen V2 contract.
    if (Source.Revision > MaximumPersistedCounter || Source.NextDropId > MaximumPersistedCounter ||
        Source.QuestKillDropId > MaximumPersistedCounter)
        return {ResultCode::InvalidSnapshot, ValidationCode::CounterOutOfRange, false, Source.Revision};

    JadeboundLegacyV2::Snapshot Intermediate;
    Intermediate.Revision = Source.Revision;
    Intermediate.Coins = Source.Coins;
    Intermediate.EquippedWeapon = static_cast<JadeboundLegacyV2::ItemId>(Source.EquippedWeapon);
    Intermediate.Quest = static_cast<JadeboundLegacyV2::QuestState>(Source.Quest);
    Intermediate.NextDropId = Source.NextDropId;
    Intermediate.QuestKillDropId = Source.QuestKillDropId;
    for (std::uint32_t I = 0; I < InventorySlotCount; ++I)
        Intermediate.Inventory[I] = {static_cast<JadeboundLegacyV2::ItemId>(Source.Inventory[I].Id),
                                     Source.Inventory[I].Count};

    // Exact V1 validation proves recorded IDs are ordered1..N, unique, and all
    // processed IDs are unique members. Only the private processed copy sorts.
    // An issued ID absent from this exact difference remains permanently claimed.
    auto Processed = Source.ProcessedDropIds;
    std::sort(Processed.begin(), Processed.end());
    Intermediate.UnprocessedDropIds.reserve(Source.RecordedDropIds.size() - Processed.size());
    std::set_difference(Source.RecordedDropIds.begin(), Source.RecordedDropIds.end(),
                        Processed.begin(), Processed.end(),
                        std::back_inserter(Intermediate.UnprocessedDropIds));
    const auto ValidV2 = JadeboundLegacyV2::ProgressionCore::ValidateSnapshot(Intermediate);
    if (!ValidV2.Succeeded())
        return {static_cast<ResultCode>(ValidV2.Code), static_cast<ValidationCode>(ValidV2.Validation),
                false, Source.Revision};

    Snapshot Candidate;
    // Call the unchanged frozen Source115 converter, including its V2 validation,
    // original-state copy, TorsoNone initialization and V3 validation. No reward
    // or loot API is used to simulate historical progression.
    const auto Converted = ConvertLegacyV2Snapshot(Intermediate, Candidate);
    if (!Converted.Succeeded()) return Converted;
    Out = std::move(Candidate);
    return {ResultCode::Ok, ValidationCode::None, true, Out.Revision};
}

bool Source145BuildLegacyV1TorsoMigrationCandidate(const JadeboundLegacyV1::Snapshot& Source,
                                                Snapshot& Out)
{
    Snapshot Candidate;
    if (!Source145ConvertLegacyV1ToTorsoSnapshot(Source, Candidate).Succeeded() ||
        Candidate.Revision >= MaximumPersistedCounter) return false;
    ++Candidate.Revision;
    if (!ProgressionCore::ValidateSnapshot(Candidate).Succeeded()) return false;
    Out = std::move(Candidate);
    return true;
}
}
