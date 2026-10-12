#include "Persistence/ProgressionSaveV2.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Development/DevelopmentIdentity.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#include <algorithm>
#include <limits>

namespace
{
bool SameSnapshot(const Jadebound::Snapshot& A, const Jadebound::Snapshot& B)
{
    if (A.Version != B.Version || A.Revision != B.Revision || A.Coins != B.Coins ||
        A.EquippedWeapon != B.EquippedWeapon || A.Quest != B.Quest || A.NextDropId != B.NextDropId ||
        A.QuestKillDropId != B.QuestKillDropId || A.UnprocessedDropIds != B.UnprocessedDropIds) return false;
    for (uint32 Index = 0; Index < Jadebound::InventorySlotCount; ++Index)
        if (A.Inventory[Index].Id != B.Inventory[Index].Id || A.Inventory[Index].Count != B.Inventory[Index].Count) return false;
    return true;
}
bool SameReflectedState(const FJadeboundProgressionState& A, const FJadeboundProgressionState& B)
{
    return FJadeboundProgressionState::StaticStruct()->CompareScriptStruct(&A, &B, 0);
}
FJadeboundProgressionState LegacyFixture(const JadeboundLegacyV1::Snapshot& S)
{
    // SYNTHETIC memory-only v1 representation, never an old-profile disk writer.
    FJadeboundProgressionState Out;
    Out.Version = 1;
    Out.ExactRevision = S.Revision;
    Out.Revision = static_cast<int64>(std::min(S.Revision, static_cast<uint64>(MAX_int64)));
    Out.Coins = S.Coins;
    Out.EquippedWeapon = static_cast<int32>(S.EquippedWeapon);
    Out.QuestState = static_cast<int32>(S.Quest);
    Out.NextDropId = static_cast<int64>(S.NextDropId);
    Out.QuestKillDropId = static_cast<int64>(S.QuestKillDropId);
    Out.Slots.SetNum(static_cast<int32>(Jadebound::InventorySlotCount));
    for (uint32 Index = 0; Index < Jadebound::InventorySlotCount; ++Index)
    {
        Out.Slots[Index].ItemId = static_cast<int32>(S.Inventory[Index].Id);
        Out.Slots[Index].Count = static_cast<int32>(S.Inventory[Index].Count);
    }
    for (const auto Id : S.RecordedDropIds) Out.RecordedDropIds.Add(static_cast<int64>(Id));
    for (const auto Id : S.ProcessedDropIds) Out.ProcessedDropIds.Add(static_cast<int64>(Id));
    return Out;
}
TArray<FJadeboundSavedDrop> PendingFixture(const JadeboundLegacyV1::Snapshot& S)
{
    TArray<FJadeboundSavedDrop> Out;
    // Intentionally reversed world-record order; ledger sorting must not move them.
    for (auto It = S.RecordedDropIds.rbegin(); It != S.RecordedDropIds.rend(); ++It)
    {
        if (std::find(S.ProcessedDropIds.begin(), S.ProcessedDropIds.end(), *It) != S.ProcessedDropIds.end()) continue;
        FJadeboundSavedDrop D;
        D.DropId = static_cast<int64>(*It);
        D.ItemId = *It == S.QuestKillDropId ? 5 : 3;
        D.Count = *It == S.QuestKillDropId ? 1 : 2;
        D.Location = FVector(151.125 + Out.Num(), 349.875 - Out.Num(), 50.0625 + Out.Num());
        Out.Add(D);
    }
    return Out;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundV2JournalPathsTest, "Jadebound.Persistence.V2JournalPaths",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundV2JournalPathsTest::RunTest(const FString& Parameters)
{
    using namespace Jadebound::Persistence;
    const FString Id(TEXT("27b0cbce01df4b6b8f9cf927acaecf11"));
    FString Old[2], Current[2], Expected;
    for (int32 Slot = 0; Slot < 2; ++Slot)
    {
        TestTrue(TEXT("Old canonical development path"), TryJournalPath(true, Id, Slot, false, Old[Slot]));
        TestTrue(TEXT("New canonical development path"), TryJournalPath(true, Id, Slot, true, Current[Slot]));
        TestTrue(TEXT("Existing v1 helper retained"), Jadebound::Development::TryJournalPath(Id, Slot, Expected) && FPaths::IsSamePath(Old[Slot], Expected));
        TestTrue(TEXT("Versions have different bank filenames"), !FPaths::IsSamePath(Old[Slot], Current[Slot]));
        TestTrue(TEXT("Versions share the same character directory"), FPaths::IsSamePath(FPaths::GetPath(Old[Slot]), FPaths::GetPath(Current[Slot])));
        TestTrue(TEXT("Writer lease path stays identical"), FPaths::IsSamePath(
            FPaths::Combine(FPaths::GetPath(Old[Slot]), TEXT("Character.writer")),
            FPaths::Combine(FPaths::GetPath(Current[Slot]), TEXT("Character.writer"))));
    }
    TestTrue(TEXT("New banks are distinct"), Current[0] != Current[1]);
    for (const bool bV2 : {false, true})
    {
        FString A, B;
        TestTrue(TEXT("Legacy standalone paths are bounded"), TryJournalPath(false, TEXT("Traveler01"), 0, bV2, A) && TryJournalPath(false, TEXT("Traveler01"), 1, bV2, B) && A != B);
        TestEqual(TEXT("Explicit legacy filename"), FPaths::GetCleanFilename(A), FString(bV2 ? TEXT("Jadebound_Traveler01_Progression_V2_A.sav") : TEXT("Jadebound_Traveler01_Progression_A.sav")));
        for (const int32 Slot : {-1, MAX_int32})
        {
            FString Rejected(TEXT("stale"));
            TestTrue(TEXT("Invalid slot produces no stale path"), !TryJournalPath(true, Id, Slot, bV2, Rejected) && Rejected.IsEmpty());
        }
        for (const FString& Invalid : {FString(TEXT("../Traveler01")), Id.ToUpper(), FString(TEXT("00000000000000000000000000000000"))})
        {
            FString Rejected(TEXT("stale"));
            TestTrue(TEXT("Invalid development identity produces no path"), !TryJournalPath(true, Invalid, 0, bV2, Rejected) && Rejected.IsEmpty());
        }
        FString Rejected(TEXT("stale"));
        TestTrue(TEXT("Standalone identity cannot choose another character"), !TryJournalPath(false, Id, 0, bV2, Rejected) && Rejected.IsEmpty());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundV2CodecMigrationTest, "Jadebound.Persistence.V2CodecMigration",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundV2CodecMigrationTest::RunTest(const FString& Parameters)
{
    using namespace Jadebound;
    using namespace Jadebound::Persistence;
    TestEqual(TEXT("Reflected missing-tag compatibility default remains1"), FJadeboundProgressionState().Version, 1);
    for (uint32 Stage = 0; Stage <= 4; ++Stage)
    {
        JadeboundLegacyV1::ProgressionCore Old;
        if (!TestTrue(TEXT("Synthetic old inventory/equipment fixture"),
            Old.Buy(JadeboundLegacyV1::ItemId::AshwoodSpear, 1).Succeeded() && Old.Equip(0).Succeeded() &&
            Old.RecordRaiderDefeat(1).Succeeded() && Old.RecordRaiderDefeat(2).Succeeded() &&
            Old.GrantLoot(JadeboundLegacyV1::ItemId::IronOre, 2, 2).Succeeded())) return false;
        if (Stage >= 1 && !TestTrue(TEXT("Old accepted stage"), Old.AcceptQuest().Succeeded())) return false;
        if (Stage >= 2 && !TestTrue(TEXT("Old defeated stage"), Old.RecordRaiderDefeat(3).Succeeded())) return false;
        if (Stage >= 3 && !TestTrue(TEXT("Old recovered stage"), Old.PickupParcel(3).Succeeded())) return false;
        if (Stage >= 4 && !TestTrue(TEXT("Old completed stage"), Old.TurnInQuest().Succeeded())) return false;
        const auto Original = Old.GetSnapshot();
        const auto OriginalState = LegacyFixture(Original);
        const auto Drops = PendingFixture(Original);
        JadeboundLegacyV1::Snapshot DecodedLegacy;
        if (!TestTrue(TEXT("Original v1 decode and outstanding record bijection"), DecodeStateV1(OriginalState, DecodedLegacy) &&
            SameReflectedState(OriginalState, LegacyFixture(DecodedLegacy)) && ValidateDropsV1(DecodedLegacy, Drops))) return false;
        // SYNTHETIC current-class v1 SaveGame roundtrip. Real old omitted tags remain a separate native fixture.
        auto* LegacySave = Cast<UJadeboundCharacterSave>(UGameplayStatics::CreateSaveGameObject(UJadeboundCharacterSave::StaticClass()));
        if (!TestNotNull(TEXT("Synthetic memory-only legacy save"), LegacySave)) return false;
        LegacySave->CharacterId = TEXT("Traveler01");
        LegacySave->State = OriginalState;
        LegacySave->PendingDrops = Drops;
        TArray<uint8> LegacyBytes;
        if (!TestTrue(TEXT("Synthetic v1 reflected SaveGame serialization"), UGameplayStatics::SaveGameToMemory(LegacySave, LegacyBytes))) return false;
        auto* LegacyReloaded = Cast<UJadeboundCharacterSave>(UGameplayStatics::LoadGameFromMemory(LegacyBytes));
        if (!TestNotNull(TEXT("Synthetic v1 memory reload"), LegacyReloaded)) return false;
        TestTrue(TEXT("Synthetic v1 character/state preserved and strict legacy decode"), LegacyReloaded->CharacterId == LegacySave->CharacterId &&
            SameReflectedState(OriginalState, LegacyReloaded->State) && DecodeStateV1(LegacyReloaded->State, DecodedLegacy) &&
            SameReflectedState(OriginalState, LegacyFixture(DecodedLegacy)) && ValidateDropsV1(DecodedLegacy, LegacyReloaded->PendingDrops));
        if (!TestEqual(TEXT("Synthetic v1 pending record count preserved"), LegacyReloaded->PendingDrops.Num(), Drops.Num())) return false;
        for (int32 Index = 0; Index < Drops.Num(); ++Index)
            TestTrue(TEXT("Synthetic v1 pending locations/order preserved"), FJadeboundSavedDrop::StaticStruct()->CompareScriptStruct(&Drops[Index], &LegacyReloaded->PendingDrops[Index], 0));
        Snapshot Pure, Migration;
        if (!TestTrue(TEXT("Exact pure conversion and one representation increment"), ConvertLegacyV1Snapshot(DecodedLegacy, Pure).Succeeded() &&
            Pure.Revision == Original.Revision && BuildMigrationCandidate(DecodedLegacy, Migration) && Migration.Revision == Original.Revision + 1)) return false;
        auto Expected = Pure;
        ++Expected.Revision;
        TestTrue(TEXT("Migration changes only representation revision"), SameSnapshot(Expected, Migration));
        TestTrue(TEXT("Both ledger forms accept the same pending positions/order"), ValidateDropsV2(Migration, Drops));
        FJadeboundProgressionState Encoded;
        if (!TestTrue(TEXT("Strict v2 encode"), EncodeStateV2(Migration, Encoded))) return false;
        TestTrue(TEXT("Exactly one serialized ledger form"), Encoded.Version == 2 && Encoded.RecordedDropIds.IsEmpty() && Encoded.ProcessedDropIds.IsEmpty());
        Snapshot Decoded;
        TestTrue(TEXT("Strict v2 codec preserves every field"), DecodeStateV2(Encoded, Decoded) && SameSnapshot(Migration, Decoded));
        TestTrue(TEXT("Old original fixture stays unchanged"), SameReflectedState(OriginalState, LegacyFixture(Old.GetSnapshot())));

        const auto Preserve = Decoded;
        auto Bad = Encoded;
        Bad.RecordedDropIds.Add(1);
        TestTrue(TEXT("v2 refuses legacy recorded ambiguity unchanged"), !DecodeStateV2(Bad, Decoded) && SameSnapshot(Preserve, Decoded));
        Bad = Encoded; Bad.ProcessedDropIds.Add(2);
        TestTrue(TEXT("v2 refuses legacy processed ambiguity unchanged"), !DecodeStateV2(Bad, Decoded) && SameSnapshot(Preserve, Decoded));
        Bad = Encoded; Bad.Version = 1;
        TestTrue(TEXT("v1 cannot be decoded as v2"), !DecodeStateV2(Bad, Decoded) && SameSnapshot(Preserve, Decoded));
        Bad = Encoded; Bad.Version = 3;
        TestTrue(TEXT("Future schema refuses strict decode unchanged"), !DecodeStateV2(Bad, Decoded) && SameSnapshot(Preserve, Decoded));
        Bad = Encoded; ++Bad.Revision;
        TestTrue(TEXT("Public/exact revision mismatch rejects unchanged"), !DecodeStateV2(Bad, Decoded) && SameSnapshot(Preserve, Decoded));
        Bad = Encoded; Bad.Slots.Pop();
        TestTrue(TEXT("Missing inventory slot rejects unchanged"), !DecodeStateV2(Bad, Decoded) && SameSnapshot(Preserve, Decoded));
        Bad = OriginalState; Bad.UnprocessedDropIds.Add(1);
        TestTrue(TEXT("v1 refuses new-ledger ambiguity unchanged"), !DecodeStateV1(Bad, DecodedLegacy) && SameReflectedState(OriginalState, LegacyFixture(DecodedLegacy)));

        auto* Save = Cast<UJadeboundCharacterSave>(UGameplayStatics::CreateSaveGameObject(UJadeboundCharacterSave::StaticClass()));
        if (!TestNotNull(TEXT("Synthetic memory-only current save"), Save)) return false;
        Save->CharacterId = TEXT("Traveler01");
        Save->State = Encoded;
        Save->PendingDrops = Drops;
        TArray<uint8> Bytes;
        if (!TestTrue(TEXT("Current reflected SaveGame serialization"), UGameplayStatics::SaveGameToMemory(Save, Bytes))) return false;
        auto* Reloaded = Cast<UJadeboundCharacterSave>(UGameplayStatics::LoadGameFromMemory(Bytes));
        if (!TestNotNull(TEXT("Current reflected memory reload"), Reloaded)) return false;
        TestTrue(TEXT("Character and full reflected state preserved"), Reloaded->CharacterId == Save->CharacterId && SameReflectedState(Encoded, Reloaded->State));
        if (!TestEqual(TEXT("Pending record count preserved"), Reloaded->PendingDrops.Num(), Drops.Num())) return false;
        for (int32 Index = 0; Index < Drops.Num(); ++Index)
            TestTrue(TEXT("Every pending record and exact location/order preserved"), FJadeboundSavedDrop::StaticStruct()->CompareScriptStruct(&Drops[Index], &Reloaded->PendingDrops[Index], 0));
        TestTrue(TEXT("Serialized payload decodes and validates pending bijection"), DecodeStateV2(Reloaded->State, Decoded) && SameSnapshot(Migration, Decoded) && ValidateDropsV2(Decoded, Reloaded->PendingDrops));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundV2PendingValidationTest, "Jadebound.Persistence.V2PendingValidation",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundV2PendingValidationTest::RunTest(const FString& Parameters)
{
    using namespace Jadebound;
    using namespace Jadebound::Persistence;
    JadeboundLegacyV1::ProgressionCore Old;
    if (!TestTrue(TEXT("Mixed ordinary/quest outstanding fixture"), Old.RecordRaiderDefeat(1).Succeeded() &&
        Old.AcceptQuest().Succeeded() && Old.RecordRaiderDefeat(2).Succeeded() && Old.RecordRaiderDefeat(3).Succeeded())) return false;
    const auto Drops = PendingFixture(Old.GetSnapshot());
    Snapshot Candidate;
    if (!TestTrue(TEXT("Original pending conversion validates"), BuildMigrationCandidate(Old.GetSnapshot(), Candidate) && ValidateDropsV2(Candidate, Drops))) return false;
    auto Bad = Drops; Bad.Pop();
    TestFalse(TEXT("Missing pending record"), ValidateDropsV2(Candidate, Bad));
    Bad = Drops; Bad[1] = Bad[0];
    TestFalse(TEXT("Duplicate pending record"), ValidateDropsV2(Candidate, Bad));
    Bad = Drops; Bad[0].DropId = static_cast<int64>(Candidate.NextDropId);
    TestFalse(TEXT("Never-issued pending ID"), ValidateDropsV2(Candidate, Bad));
    Bad = Drops; Bad[0].Count = 1;
    TestFalse(TEXT("Ordinary count cannot be altered"), ValidateDropsV2(Candidate, Bad));
    Bad = Drops; Bad[1].ItemId = 3;
    TestFalse(TEXT("Reserved quest parcel cannot become ordinary ore"), ValidateDropsV2(Candidate, Bad));
    Bad = Drops; Bad[0].Location.Z = -1;
    TestFalse(TEXT("Below-world pending position"), ValidateDropsV2(Candidate, Bad));
    Bad = Drops; Bad[0].Location.X = 9999;
    TestFalse(TEXT("Out-of-area pending position"), ValidateDropsV2(Candidate, Bad));
    Bad = Drops; Bad[0].Location.Y = std::numeric_limits<double>::quiet_NaN();
    TestFalse(TEXT("NaN pending position"), ValidateDropsV2(Candidate, Bad));
    Bad = Drops; Bad[0].Location.X = std::numeric_limits<double>::infinity();
    TestFalse(TEXT("Infinite pending position"), ValidateDropsV2(Candidate, Bad));
    ProgressionCore Current;
    TestTrue(TEXT("Load candidate and consume one ordinary ID"), Current.LoadSnapshot(Candidate).Succeeded() && Current.GrantLoot(ItemId::IronOre, 2, 3).Succeeded());
    TestFalse(TEXT("Consumed ID cannot remain in pending records"), ValidateDropsV2(Current.GetSnapshot(), Drops));
    Bad = Drops; Bad.RemoveAt(0);
    TestTrue(TEXT("Only retired record removed; quest position retained"), ValidateDropsV2(Current.GetSnapshot(), Bad));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundV2MigrationExhaustionTest, "Jadebound.Persistence.V2MigrationExhaustion",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundV2MigrationExhaustionTest::RunTest(const FString& Parameters)
{
    using namespace Jadebound;
    using namespace Jadebound::Persistence;
    JadeboundLegacyV1::Snapshot Old;
    Old.Revision = MaximumPersistedCounter + 1;
    const auto OldState = LegacyFixture(Old);
    JadeboundLegacyV1::Snapshot Decoded;
    TestTrue(TEXT("Highest original unsigned revision remains rankable before migration eligibility"), DecodeStateV1(OldState, Decoded) && Decoded.Revision == Old.Revision);
    Snapshot Output; Output.Coins = 123; Output.Inventory[4] = {ItemId::Balm, 2};
    const auto Before = Output;
    TestTrue(TEXT("Unsigned original cannot be migrated or truncated"), !BuildMigrationCandidate(Decoded, Output) && SameSnapshot(Before, Output));
    Old.Revision = MaximumPersistedCounter;
    TestTrue(TEXT("Representable but exhausted migration refuses unchanged"), !BuildMigrationCandidate(Old, Output) && SameSnapshot(Before, Output));
    Old.Revision = MaximumPersistedCounter - 1;
    TestTrue(TEXT("Last representation increment preserves exact bounded revision"), BuildMigrationCandidate(Old, Output) && Output.Revision == MaximumPersistedCounter);
    ProgressionCore Exhausted;
    TestTrue(TEXT("Bounded converted candidate validates but no award can commit"), Exhausted.LoadSnapshot(Output).Succeeded() &&
        Exhausted.AcceptQuest().Code == ResultCode::RevisionOverflow && SameSnapshot(Output, Exhausted.GetSnapshot()));
    FJadeboundProgressionState Encoded;
    Snapshot Reloaded;
    TestTrue(TEXT("Signed limit serializes without a cast wrap"), EncodeStateV2(Output, Encoded) && Encoded.Revision == MAX_int64 && DecodeStateV2(Encoded, Reloaded) && SameSnapshot(Output, Reloaded));
    return true;
}
#endif
