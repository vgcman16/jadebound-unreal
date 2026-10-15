#include "Persistence/ProgressionSaveV3.h"

#include <algorithm>
#include <utility>

namespace Jadebound::Persistence
{
static_assert(SnapshotVersion == 3, "This codec only supports the explicit torso V3 domain");

bool EncodeStateV3(const Snapshot& Source, FJadeboundProgressionState& Out)
{
    if (!ProgressionCore::ValidateSnapshot(Source).Succeeded()) return false;
    FJadeboundProgressionState Candidate;
    Candidate.Version = 3;
    Candidate.ExactRevision = Source.Revision;
    Candidate.Revision = static_cast<int64>(Source.Revision);
    Candidate.Coins = Source.Coins;
    Candidate.EquippedWeapon = static_cast<int32>(Source.EquippedWeapon);
    Candidate.EquippedTorso = static_cast<int32>(Source.EquippedTorso);
    Candidate.QuestState = static_cast<int32>(Source.Quest);
    Candidate.Slots.SetNum(static_cast<int32>(InventorySlotCount));
    for (uint32 Index = 0; Index < InventorySlotCount; ++Index)
    {
        Candidate.Slots[Index].ItemId = static_cast<int32>(Source.Inventory[Index].Id);
        Candidate.Slots[Index].Count = static_cast<int32>(Source.Inventory[Index].Count);
    }
    Candidate.NextDropId = static_cast<int64>(Source.NextDropId);
    Candidate.QuestKillDropId = static_cast<int64>(Source.QuestKillDropId);
    for (const auto Id : Source.UnprocessedDropIds) Candidate.UnprocessedDropIds.Add(static_cast<int64>(Id));
    // Default legacy arrays stay empty. V3 carries one exact outstanding ledger.
    Out = MoveTemp(Candidate);
    return true;
}

bool DecodeStateV3(const FJadeboundProgressionState& Source, Snapshot& Out)
{
    if (Source.Version != 3 || Source.Slots.Num() != static_cast<int32>(InventorySlotCount) ||
        Source.NextDropId <= 0 || Source.QuestKillDropId < 0 || Source.Revision < 0 ||
        Source.ExactRevision > MaximumPersistedCounter ||
        static_cast<uint64>(Source.Revision) != Source.ExactRevision ||
        !Source.RecordedDropIds.IsEmpty() || !Source.ProcessedDropIds.IsEmpty() ||
        Source.UnprocessedDropIds.Num() > static_cast<int32>(MaximumUnprocessedDrops)) return false;
    Snapshot Candidate;
    Candidate.Revision = Source.ExactRevision;
    Candidate.Coins = Source.Coins;
    Candidate.EquippedWeapon = static_cast<ItemId>(Source.EquippedWeapon);
    Candidate.EquippedTorso = static_cast<ItemId>(Source.EquippedTorso);
    Candidate.Quest = static_cast<QuestState>(Source.QuestState);
    for (uint32 Index = 0; Index < InventorySlotCount; ++Index)
    {
        if (Source.Slots[Index].ItemId < 0 || Source.Slots[Index].Count < 0) return false;
        Candidate.Inventory[Index] = {static_cast<ItemId>(Source.Slots[Index].ItemId), static_cast<uint32>(Source.Slots[Index].Count)};
    }
    Candidate.NextDropId = static_cast<uint64>(Source.NextDropId);
    Candidate.QuestKillDropId = static_cast<uint64>(Source.QuestKillDropId);
    for (const auto Id : Source.UnprocessedDropIds)
    {
        if (Id <= 0) return false;
        Candidate.UnprocessedDropIds.push_back(static_cast<uint64>(Id));
    }
    if (!ProgressionCore::ValidateSnapshot(Candidate).Succeeded()) return false;
    Out = std::move(Candidate);
    return true;
}

namespace
{
bool ValidateDropRecordV3(const FJadeboundSavedDrop& Drop)
{
    // Same finite courtyard location policy as the frozen native V1/V2 adapter.
    if (Drop.DropId <= 0 || Drop.Location.ContainsNaN() || !FMath::IsFinite(Drop.Location.X) ||
        !FMath::IsFinite(Drop.Location.Y) || !FMath::IsFinite(Drop.Location.Z)) return false;
    const FVector Local = FRotator(0,135,0).UnrotateVector(Drop.Location - FVector(150,350,0));
    return FMath::Abs(Local.X) <= 700.0 && FMath::Abs(Local.Y) <= 600.0 && Drop.Location.Z >= 0.0 && Drop.Location.Z <= 300.0;
}
}

bool ValidateDropsV3(const Snapshot& Candidate, const TArray<FJadeboundSavedDrop>& Drops)
{
    if (!ProgressionCore::ValidateSnapshot(Candidate).Succeeded() ||
        Drops.Num() != static_cast<int32>(Candidate.UnprocessedDropIds.size())) return false;
    TSet<int64> Seen;
    for (const auto& Drop : Drops)
    {
        if (!ValidateDropRecordV3(Drop) || Seen.Contains(Drop.DropId)) return false;
        const uint64 Id = static_cast<uint64>(Drop.DropId);
        if (!std::binary_search(Candidate.UnprocessedDropIds.begin(), Candidate.UnprocessedDropIds.end(), Id)) return false;
        const bool bParcel = Id == Candidate.QuestKillDropId;
        if (Drop.ItemId != (bParcel ? 5 : 3) || Drop.Count != (bParcel ? 1 : 2)) return false;
        Seen.Add(Drop.DropId);
    }
    return true;
}
}
