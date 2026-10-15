#include "Persistence/ProgressionSaveV2.h"

#include "Development/DevelopmentIdentity.h"
#include "Domain/FrozenV2Migration.h"
#include "Misc/Paths.h"

#include <algorithm>
#include <utility>

namespace Jadebound::Persistence
{
bool EncodeStateV2(const JadeboundLegacyV2::Snapshot& Source, FJadeboundProgressionState& Out)
{
    if (!JadeboundLegacyV2::ProgressionCore::ValidateSnapshot(Source).Succeeded()) return false;
    FJadeboundProgressionState Candidate;
    Candidate.Version = static_cast<int32>(JadeboundLegacyV2::SnapshotVersion);
    Candidate.ExactRevision = Source.Revision;
    Candidate.Revision = static_cast<int64>(Source.Revision);
    Candidate.Coins = Source.Coins;
    Candidate.EquippedWeapon = static_cast<int32>(Source.EquippedWeapon);
    Candidate.QuestState = static_cast<int32>(Source.Quest);
    Candidate.Slots.SetNum(static_cast<int32>(JadeboundLegacyV2::InventorySlotCount));
    for (uint32 Index = 0; Index < JadeboundLegacyV2::InventorySlotCount; ++Index)
    {
        Candidate.Slots[Index].ItemId = static_cast<int32>(Source.Inventory[Index].Id);
        Candidate.Slots[Index].Count = static_cast<int32>(Source.Inventory[Index].Count);
    }
    Candidate.NextDropId = static_cast<int64>(Source.NextDropId);
    Candidate.QuestKillDropId = static_cast<int64>(Source.QuestKillDropId);
    for (const auto Id : Source.UnprocessedDropIds) Candidate.UnprocessedDropIds.Add(static_cast<int64>(Id));
    // Constructor legacy arrays are empty. A v2 payload never carries both forms.
    Out = MoveTemp(Candidate);
    return true;
}

bool DecodeStateV2(const FJadeboundProgressionState& Source, JadeboundLegacyV2::Snapshot& Out)
{
    if (Source.Version != 2 || Source.EquippedTorso != 0 || Source.Slots.Num() != static_cast<int32>(JadeboundLegacyV2::InventorySlotCount) ||
        Source.NextDropId <= 0 || Source.QuestKillDropId < 0 || Source.Revision < 0 ||
        Source.ExactRevision > JadeboundLegacyV2::MaximumPersistedCounter ||
        static_cast<uint64>(Source.Revision) != Source.ExactRevision ||
        !Source.RecordedDropIds.IsEmpty() || !Source.ProcessedDropIds.IsEmpty() ||
        Source.UnprocessedDropIds.Num() > static_cast<int32>(JadeboundLegacyV2::MaximumUnprocessedDrops)) return false;
    JadeboundLegacyV2::Snapshot Candidate;
    Candidate.Revision = Source.ExactRevision;
    Candidate.Coins = Source.Coins;
    Candidate.EquippedWeapon = static_cast<JadeboundLegacyV2::ItemId>(Source.EquippedWeapon);
    Candidate.Quest = static_cast<JadeboundLegacyV2::QuestState>(Source.QuestState);
    for (uint32 Index = 0; Index < JadeboundLegacyV2::InventorySlotCount; ++Index)
    {
        if (Source.Slots[Index].ItemId < 0 || Source.Slots[Index].Count < 0) return false;
        Candidate.Inventory[Index] = {static_cast<JadeboundLegacyV2::ItemId>(Source.Slots[Index].ItemId), static_cast<uint32>(Source.Slots[Index].Count)};
    }
    Candidate.NextDropId = static_cast<uint64>(Source.NextDropId);
    Candidate.QuestKillDropId = static_cast<uint64>(Source.QuestKillDropId);
    for (const auto Id : Source.UnprocessedDropIds)
    {
        if (Id <= 0) return false;
        Candidate.UnprocessedDropIds.push_back(static_cast<uint64>(Id));
    }
    if (!JadeboundLegacyV2::ProgressionCore::ValidateSnapshot(Candidate).Succeeded()) return false;
    Out = std::move(Candidate);
    return true;
}

bool DecodeStateV1(const FJadeboundProgressionState& Source, JadeboundLegacyV1::Snapshot& Out)
{
    // Exact old uint64 revision validation happens BEFORE v2 eligibility/ranking.
    if (Source.Version != 1 || Source.EquippedTorso != 0 || Source.Slots.Num() != static_cast<int32>(JadeboundLegacyV1::InventorySlotCount) ||
        Source.NextDropId <= 0 || Source.QuestKillDropId < 0 || !Source.UnprocessedDropIds.IsEmpty() ||
        Source.RecordedDropIds.Num() > static_cast<int32>(JadeboundLegacyV1::MaximumRecordedDrops) ||
        Source.ProcessedDropIds.Num() > static_cast<int32>(JadeboundLegacyV1::MaximumRecordedDrops) ||
        Source.Revision != static_cast<int64>(std::min(Source.ExactRevision, static_cast<uint64>(MAX_int64)))) return false;
    JadeboundLegacyV1::Snapshot Candidate;
    Candidate.Revision = Source.ExactRevision;
    Candidate.Coins = Source.Coins;
    Candidate.EquippedWeapon = static_cast<JadeboundLegacyV1::ItemId>(Source.EquippedWeapon);
    Candidate.Quest = static_cast<JadeboundLegacyV1::QuestState>(Source.QuestState);
    for (uint32 Index = 0; Index < JadeboundLegacyV1::InventorySlotCount; ++Index)
    {
        if (Source.Slots[Index].ItemId < 0 || Source.Slots[Index].Count < 0) return false;
        Candidate.Inventory[Index] = {static_cast<JadeboundLegacyV1::ItemId>(Source.Slots[Index].ItemId), static_cast<uint32>(Source.Slots[Index].Count)};
    }
    Candidate.NextDropId = static_cast<uint64>(Source.NextDropId);
    Candidate.QuestKillDropId = static_cast<uint64>(Source.QuestKillDropId);
    for (const auto Id : Source.RecordedDropIds)
    {
        if (Id <= 0) return false;
        Candidate.RecordedDropIds.push_back(static_cast<uint64>(Id));
    }
    for (const auto Id : Source.ProcessedDropIds)
    {
        if (Id <= 0) return false;
        Candidate.ProcessedDropIds.push_back(static_cast<uint64>(Id));
    }
    if (!JadeboundLegacyV1::ProgressionCore::ValidateSnapshot(Candidate).Succeeded()) return false;
    Out = std::move(Candidate);
    return true;
}

namespace
{
bool ValidateDropRecord(const FJadeboundSavedDrop& Drop)
{
    if (Drop.DropId <= 0 || Drop.Location.ContainsNaN() || !FMath::IsFinite(Drop.Location.X) ||
        !FMath::IsFinite(Drop.Location.Y) || !FMath::IsFinite(Drop.Location.Z)) return false;
    const FVector Local = FRotator(0,135,0).UnrotateVector(Drop.Location - FVector(150,350,0));
    return FMath::Abs(Local.X) <= 700.0 && FMath::Abs(Local.Y) <= 600.0 && Drop.Location.Z >= 0.0 && Drop.Location.Z <= 300.0;
}
}

bool ValidateDropsV2(const JadeboundLegacyV2::Snapshot& Candidate, const TArray<FJadeboundSavedDrop>& Drops)
{
    if (!JadeboundLegacyV2::ProgressionCore::ValidateSnapshot(Candidate).Succeeded() ||
        Drops.Num() != static_cast<int32>(Candidate.UnprocessedDropIds.size())) return false;
    TSet<int64> Seen;
    for (const auto& Drop : Drops)
    {
        if (!ValidateDropRecord(Drop) || Seen.Contains(Drop.DropId)) return false;
        const uint64 Id = static_cast<uint64>(Drop.DropId);
        if (!std::binary_search(Candidate.UnprocessedDropIds.begin(), Candidate.UnprocessedDropIds.end(), Id)) return false;
        const bool bParcel = Id == Candidate.QuestKillDropId;
        if (Drop.ItemId != (bParcel ? 5 : 3) || Drop.Count != (bParcel ? 1 : 2)) return false;
        Seen.Add(Drop.DropId);
    }
    return true;
}

bool ValidateDropsV1(const JadeboundLegacyV1::Snapshot& Candidate, const TArray<FJadeboundSavedDrop>& Drops)
{
    if (!JadeboundLegacyV1::ProgressionCore::ValidateSnapshot(Candidate).Succeeded() ||
        Drops.Num() != static_cast<int32>(Candidate.RecordedDropIds.size() - Candidate.ProcessedDropIds.size())) return false;
    TSet<int64> Seen;
    for (const auto& Drop : Drops)
    {
        if (!ValidateDropRecord(Drop) || Seen.Contains(Drop.DropId)) return false;
        const uint64 Id = static_cast<uint64>(Drop.DropId);
        if (std::find(Candidate.RecordedDropIds.begin(), Candidate.RecordedDropIds.end(), Id) == Candidate.RecordedDropIds.end() ||
            std::find(Candidate.ProcessedDropIds.begin(), Candidate.ProcessedDropIds.end(), Id) != Candidate.ProcessedDropIds.end()) return false;
        const bool bParcel = Id == Candidate.QuestKillDropId;
        if (Drop.ItemId != (bParcel ? 5 : 3) || Drop.Count != (bParcel ? 1 : 2)) return false;
        Seen.Add(Drop.DropId);
    }
    return true;
}

bool TryJournalPath(bool bDevelopment, const FString& CharacterId, int32 Slot, bool bVersion2, FString& Out)
{
    Out.Reset();
    if (Slot != 0 && Slot != 1) return false;
    if (bDevelopment)
    {
        FString Legacy;
        if (!Development::TryJournalPath(CharacterId, Slot, Legacy)) return false;
        Out = bVersion2 ? FPaths::Combine(FPaths::GetPath(Legacy), Slot == 0 ? TEXT("Progression_V2_A.sav") : TEXT("Progression_V2_B.sav")) : Legacy;
        return true;
    }
    if (CharacterId != TEXT("Traveler01")) return false;
    const TCHAR* Name = bVersion2
        ? (Slot == 0 ? TEXT("Jadebound_Traveler01_Progression_V2_A.sav") : TEXT("Jadebound_Traveler01_Progression_V2_B.sav"))
        : (Slot == 0 ? TEXT("Jadebound_Traveler01_Progression_A.sav") : TEXT("Jadebound_Traveler01_Progression_B.sav"));
    Out = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), Name);
    return true;
}

bool TryForwardV3JournalPath(bool bDevelopment, const FString& CharacterId, int32 Slot, FString& Out)
{
    static_assert(JadeboundLegacyV2::SnapshotVersion == 2, "Historical path policy is bound to frozen V2; actor admission remains separate");
    Out.Reset();
    FString Current;
    // Reuse exact authority-owned current identity/slot validation and directory.
    if (!TryJournalPath(bDevelopment, CharacterId, Slot, true, Current)) return false;
    const TCHAR* Name = bDevelopment
        ? (Slot == 0 ? TEXT("Progression_V3_A.sav") : TEXT("Progression_V3_B.sav"))
        : (Slot == 0 ? TEXT("Jadebound_Traveler01_Progression_V3_A.sav") : TEXT("Jadebound_Traveler01_Progression_V3_B.sav"));
    Out = FPaths::Combine(FPaths::GetPath(Current), Name);
    return true;
}

bool BuildMigrationCandidate(const JadeboundLegacyV1::Snapshot& Legacy, JadeboundLegacyV2::Snapshot& Out)
{
    JadeboundLegacyV2::Snapshot Candidate;
    if (!FrozenV2Migration::ConvertLegacyV1Snapshot(Legacy, Candidate).Succeeded() || Candidate.Revision >= JadeboundLegacyV2::MaximumPersistedCounter) return false;
    ++Candidate.Revision; // One representation transaction; never a reward call.
    if (!JadeboundLegacyV2::ProgressionCore::ValidateSnapshot(Candidate).Succeeded()) return false;
    Out = std::move(Candidate);
    return true;
}
}
