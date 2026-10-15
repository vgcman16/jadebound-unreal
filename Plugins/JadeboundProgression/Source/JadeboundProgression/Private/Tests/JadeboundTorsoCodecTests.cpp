#include "Persistence/ProgressionSaveV2.h"
#include "Persistence/ProgressionSaveV3.h"
#include "Domain/LegacyToTorsoMigration.h"
#include "Domain/FrozenV2Migration.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

#include <algorithm>
#include <limits>

namespace
{
namespace V1 = JadeboundLegacyV1;
namespace V2 = JadeboundLegacyV2;
using namespace Jadebound;
using namespace Jadebound::Persistence;

// Synthetic memory-only fixtures. No profile, physical journal, reward or RPC.
template<typename S> bool SameCommon(const S& A, const S& B)
{
    if (A.Version != B.Version || A.Revision != B.Revision || A.Coins != B.Coins ||
        A.EquippedWeapon != B.EquippedWeapon || A.Quest != B.Quest ||
        A.NextDropId != B.NextDropId || A.QuestKillDropId != B.QuestKillDropId) return false;
    for (uint32 I = 0; I < InventorySlotCount; ++I)
        if (A.Inventory[I].Id != B.Inventory[I].Id || A.Inventory[I].Count != B.Inventory[I].Count) return false;
    return true;
}
bool Same(const Snapshot& A, const Snapshot& B)
{
    return SameCommon(A,B) && A.EquippedTorso == B.EquippedTorso && A.UnprocessedDropIds == B.UnprocessedDropIds;
}
bool Same(const V2::Snapshot& A, const V2::Snapshot& B)
{
    return SameCommon(A,B) && A.UnprocessedDropIds == B.UnprocessedDropIds;
}
bool Same(const V1::Snapshot& A, const V1::Snapshot& B)
{
    return SameCommon(A,B) && A.RecordedDropIds == B.RecordedDropIds && A.ProcessedDropIds == B.ProcessedDropIds;
}
bool SameReflected(const FJadeboundProgressionState& A, const FJadeboundProgressionState& B)
{
    return FJadeboundProgressionState::StaticStruct()->CompareScriptStruct(&A,&B,0);
}
V1::Snapshot OldFixture(uint32 Stage)
{
    V1::Snapshot S;
    S.Revision = 30; S.Coins = 17; S.EquippedWeapon = V1::ItemId::AshwoodSpear;
    for (auto& Slot : S.Inventory) Slot = {V1::ItemId::Moonleaf,1};
    S.Inventory[0] = {V1::ItemId::JadeGlaive,1};
    S.Inventory[1] = {V1::ItemId::IronOre,3};
    S.Inventory[11] = {V1::ItemId::JadeShard,1};
    S.Quest = static_cast<V1::QuestState>(Stage);
    S.NextDropId = 5; S.RecordedDropIds = {1,2,3,4}; S.ProcessedDropIds = {4,1};
    if (Stage >= 2) S.QuestKillDropId = 2;
    if (Stage >= 3) S.ProcessedDropIds = {4,2,1};
    if (Stage == 3) S.Inventory[2] = {V1::ItemId::JadeParcel,1};
    return S;
}
FJadeboundProgressionState OldReflected(const V1::Snapshot& S)
{
    FJadeboundProgressionState R;
    R.Version = 1; R.ExactRevision = S.Revision;
    R.Revision = static_cast<int64>(std::min(S.Revision,static_cast<uint64>(MAX_int64)));
    R.Coins = S.Coins; R.EquippedWeapon = static_cast<int32>(S.EquippedWeapon);
    R.QuestState = static_cast<int32>(S.Quest);
    R.NextDropId = static_cast<int64>(S.NextDropId); R.QuestKillDropId = static_cast<int64>(S.QuestKillDropId);
    R.Slots.SetNum(static_cast<int32>(InventorySlotCount));
    for (uint32 I = 0; I < InventorySlotCount; ++I)
    {
        R.Slots[I].ItemId = static_cast<int32>(S.Inventory[I].Id);
        R.Slots[I].Count = static_cast<int32>(S.Inventory[I].Count);
    }
    for (const auto Id : S.RecordedDropIds) R.RecordedDropIds.Add(static_cast<int64>(Id));
    for (const auto Id : S.ProcessedDropIds) R.ProcessedDropIds.Add(static_cast<int64>(Id));
    return R;
}
TArray<FJadeboundSavedDrop> DropsFor(const Snapshot& S)
{
    TArray<FJadeboundSavedDrop> Out;
    // Reversed physical-record order must remain valid; codec does not sort it.
    for (auto It = S.UnprocessedDropIds.rbegin(); It != S.UnprocessedDropIds.rend(); ++It)
    {
        FJadeboundSavedDrop D;
        D.DropId = static_cast<int64>(*It);
        const bool Parcel = *It == S.QuestKillDropId;
        D.ItemId = Parcel ? 5 : 3; D.Count = Parcel ? 1 : 2;
        D.Location = FVector(151.125 + Out.Num(),349.875 - Out.Num(),50.0625 + Out.Num());
        Out.Add(D);
    }
    return Out;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundTorsoV3CodecTest, "Jadebound.Persistence.TorsoV3Codec",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundTorsoV3CodecTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Old absent Version tag default remains one"), FJadeboundProgressionState().Version,1);
    TestEqual(TEXT("Absent torso tag defaults to none"), FJadeboundProgressionState().EquippedTorso,0);
    for (uint32 Stage = 0; Stage <= 4; ++Stage)
    for (const bool Equipped : {false,true})
    {
        Snapshot Original;
        if (!TestTrue(TEXT("Pure historical fixture conversion"), Source145ConvertLegacyV1ToTorsoSnapshot(OldFixture(Stage),Original).Succeeded())) return false;
        if (Equipped) Original.EquippedTorso = ItemId::WardenCoat;
        else Original.Inventory[5] = {ItemId::WardenCoat,1};
        const auto SourceBefore = Original;
        FJadeboundProgressionState Encoded;
        Encoded.RecordedDropIds.Add(999); Encoded.ProcessedDropIds.Add(999); // Must be cleared by successful publication.
        if (!TestTrue(TEXT("V3 encode validates full pack and torso"), EncodeStateV3(Original,Encoded))) return false;
        TestTrue(TEXT("One V3 ledger and independent weapon/torso"), Encoded.Version == 3 && Encoded.RecordedDropIds.IsEmpty() &&
            Encoded.ProcessedDropIds.IsEmpty() && Encoded.EquippedWeapon == 2 && Encoded.EquippedTorso == (Equipped ? 8 : 0));
        Snapshot Decoded; Decoded.Coins = 999;
        TestTrue(TEXT("Every field roundtrips without source mutation"), DecodeStateV3(Encoded,Decoded) && Same(Original,Decoded) && Same(Original,SourceBefore));
        TestTrue(TEXT("Exact pending identity and position policy"), ValidateDropsV3(Decoded,DropsFor(Decoded)));
    }

    Snapshot Valid;
    if (!TestTrue(TEXT("Prepare independent failure sentinel"), Source145ConvertLegacyV1ToTorsoSnapshot(OldFixture(2),Valid).Succeeded())) return false;
    Valid.EquippedTorso = ItemId::WardenCoat;
    FJadeboundProgressionState Good;
    if (!TestTrue(TEXT("Prepare valid reflected V3"), EncodeStateV3(Valid,Good))) return false;
    Snapshot Out; Out.Revision = 70; Out.Coins = 123; Out.Inventory[7] = {ItemId::Balm,2};
    const auto Before = Out;
    auto Refuse = [&](const TCHAR* Label,const FJadeboundProgressionState& Bad)
    { return TestTrue(Label,!DecodeStateV3(Bad,Out) && Same(Before,Out)); };
    auto Bad = Good; Bad.Version = 1; Refuse(TEXT("V1 cannot masquerade as V3"),Bad);
    Bad = Good; Bad.Version = 2; Refuse(TEXT("V2 cannot masquerade as V3"),Bad);
    Bad = Good; Bad.Version = 4; Refuse(TEXT("Future payload cannot be read as V3"),Bad);
    Bad = Good; Bad.Revision = -1; Refuse(TEXT("Negative signed revision"),Bad);
    Bad = Good; ++Bad.Revision; Refuse(TEXT("Signed/exact revision mismatch"),Bad);
    Bad = Good; Bad.ExactRevision = MaximumPersistedCounter + 1; Bad.Revision = MAX_int64;
    Refuse(TEXT("No unsigned revision truncation"),Bad);
    Bad = Good; Bad.Coins = -1; Refuse(TEXT("Negative currency"),Bad);
    Bad = Good; Bad.Slots.Pop(); Refuse(TEXT("Missing slot"),Bad);
    Bad = Good; Bad.Slots.AddDefaulted(); Refuse(TEXT("Extra slot"),Bad);
    Bad = Good; Bad.Slots[0].ItemId = -1; Refuse(TEXT("Negative item"),Bad);
    Bad = Good; Bad.Slots[0].ItemId = 9; Refuse(TEXT("Genuinely unknown V3 item"),Bad);
    Bad = Good; Bad.Slots[0].Count = -1; Refuse(TEXT("Negative count"),Bad);
    Bad = Good; Bad.Slots[0].Count = 0; Refuse(TEXT("Occupied zero count"),Bad);
    Bad = Good; Bad.Slots[0].ItemId = 0; Refuse(TEXT("Empty item with count"),Bad);
    Bad = Good; Bad.Slots[0].ItemId = 8; Bad.Slots[0].Count = 2; Refuse(TEXT("Torso stack limit one"),Bad);
    Bad = Good; Bad.EquippedWeapon = 8; Refuse(TEXT("Torso cannot occupy weapon slot"),Bad);
    for (const int32 Id : {-1,1,7,9,MAX_int32})
    { Bad = Good; Bad.EquippedTorso = Id; Refuse(TEXT("Non-torso appearance identity"),Bad); }
    Bad = Good; Bad.QuestState = 5; Refuse(TEXT("Unknown quest"),Bad);
    Bad = Good; Bad.RecordedDropIds.Add(1); Refuse(TEXT("No recorded legacy ledger in V3"),Bad);
    Bad = Good; Bad.ProcessedDropIds.Add(1); Refuse(TEXT("No processed legacy ledger in V3"),Bad);
    Bad = Good; Bad.NextDropId = 0; Refuse(TEXT("Invalid next ID"),Bad);
    Bad = Good; Bad.QuestKillDropId = -1; Refuse(TEXT("Negative quest ID"),Bad);
    Bad = Good; Bad.QuestKillDropId = 5; Refuse(TEXT("Unissued quest ID"),Bad);
    Bad = Good; Bad.UnprocessedDropIds = {3,2}; Refuse(TEXT("Unsorted pending ledger"),Bad);
    Bad = Good; Bad.UnprocessedDropIds = {2,2}; Refuse(TEXT("Duplicate pending ledger"),Bad);
    Bad = Good; Bad.UnprocessedDropIds = {0,3}; Refuse(TEXT("Zero outstanding ID"),Bad);
    Bad = Good; Bad.UnprocessedDropIds = {-1,3}; Refuse(TEXT("Negative outstanding ID"),Bad);
    Bad = Good; Bad.UnprocessedDropIds = {2,5}; Refuse(TEXT("Unissued outstanding ID"),Bad);
    Bad = Good; Bad.UnprocessedDropIds = {3}; Refuse(TEXT("Defeated quest ID must remain outstanding"),Bad);
    Bad = Good; Bad.UnprocessedDropIds.SetNum(static_cast<int32>(MaximumUnprocessedDrops)+1);
    Refuse(TEXT("Ledger bound checked before conversion"),Bad);
    FJadeboundProgressionState Preserve = Good;
    auto Invalid = Valid; Invalid.EquippedTorso = ItemId::IronOre;
    TestTrue(TEXT("Invalid encode leaves every reflected field intact"),!EncodeStateV3(Invalid,Preserve) && SameReflected(Preserve,Good));
    Valid.Revision = MaximumPersistedCounter;
    TestTrue(TEXT("Signed ceiling roundtrips without increment"),EncodeStateV3(Valid,Preserve) && Preserve.Revision == MAX_int64 &&
        DecodeStateV3(Preserve,Out) && Same(Valid,Out));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundTorsoLegacyCodecIsolationTest, "Jadebound.Persistence.TorsoLegacyCodecIsolation",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundTorsoLegacyCodecIsolationTest::RunTest(const FString& Parameters)
{
    for (uint32 Stage = 0; Stage <= 4; ++Stage)
    {
        const auto Original = OldFixture(Stage); const auto Reflected = OldReflected(Original);
        V1::Snapshot One;
        if (!TestTrue(TEXT("V1 is decoded by frozen V1"),DecodeStateV1(Reflected,One) && Same(One,Original))) return false;
        V2::Snapshot Two;
        if (!TestTrue(TEXT("Historical pure converter outputs frozen V2"),FrozenV2Migration::ConvertLegacyV1Snapshot(One,Two).Succeeded())) return false;
        FJadeboundProgressionState Encoded; Encoded.EquippedTorso = 8;
        if (!TestTrue(TEXT("Historical encoder explicitly emits V2 and no torso"),EncodeStateV2(Two,Encoded) && Encoded.Version == 2 && Encoded.EquippedTorso == 0)) return false;
        V2::Snapshot TwoOut;
        if (!TestTrue(TEXT("Frozen V2 full roundtrip"),DecodeStateV2(Encoded,TwoOut) && Same(Two,TwoOut))) return false;
        Snapshot Three;
        if (!TestTrue(TEXT("Pure V3 counterpart preserves no-torso state"),Source145ConvertLegacyV1ToTorsoSnapshot(One,Three).Succeeded())) return false;
        const auto Drops = DropsFor(Three);
        TestTrue(TEXT("All formats preserve same outstanding record identity"),ValidateDropsV1(One,Drops) && ValidateDropsV2(Two,Drops) && ValidateDropsV3(Three,Drops));
        const auto BeforeOne = One; const auto BeforeTwo = TwoOut;
        auto Bad = Reflected; Bad.EquippedTorso = 8;
        TestTrue(TEXT("V1 cannot ignore torso data"),!DecodeStateV1(Bad,One) && Same(One,BeforeOne));
        Bad = Reflected; Bad.UnprocessedDropIds.Add(2);
        TestTrue(TEXT("V1 cannot ignore new ledger"),!DecodeStateV1(Bad,One) && Same(One,BeforeOne));
        Bad = Reflected; Bad.Slots[0].ItemId = 8;
        TestTrue(TEXT("Frozen V1 rejects new inventory SKU8"),!DecodeStateV1(Bad,One) && Same(One,BeforeOne));
        Bad = Encoded; Bad.EquippedTorso = 8;
        TestTrue(TEXT("V2 cannot ignore torso data"),!DecodeStateV2(Bad,TwoOut) && Same(TwoOut,BeforeTwo));
        Bad = Encoded; Bad.Slots[0].ItemId = 8;
        TestTrue(TEXT("Frozen V2 rejects new inventory SKU8"),!DecodeStateV2(Bad,TwoOut) && Same(TwoOut,BeforeTwo));
        Bad = Encoded; Bad.RecordedDropIds.Add(1);
        TestTrue(TEXT("V2 rejects mixed ledger representation"),!DecodeStateV2(Bad,TwoOut) && Same(TwoOut,BeforeTwo));
        Bad = Encoded; Bad.ProcessedDropIds.Add(1);
        TestTrue(TEXT("V2 rejects retired legacy representation"),!DecodeStateV2(Bad,TwoOut) && Same(TwoOut,BeforeTwo));
        Bad = Encoded; Bad.Version = 3;
        TestTrue(TEXT("Historical V2 refuses V3 even without torso"),!DecodeStateV2(Bad,TwoOut) && Same(TwoOut,BeforeTwo));
        V2::Snapshot OldWrite; Snapshot NewWrite;
        TestTrue(TEXT("Historical V2 and current V3 write candidates each advance once"),BuildMigrationCandidate(One,OldWrite) &&
            OldWrite.Version == 2 && OldWrite.Revision == One.Revision+1 &&
            Source145BuildLegacyV1TorsoMigrationCandidate(One,NewWrite) && NewWrite.Version == 3 && NewWrite.Revision == One.Revision+1);
    }
    auto TooHigh = OldFixture(4); TooHigh.Revision = std::numeric_limits<uint64>::max();
    V1::Snapshot One;
    TestTrue(TEXT("Exact old uint64 revision remains rankable before eligibility"),DecodeStateV1(OldReflected(TooHigh),One) && One.Revision == TooHigh.Revision);
    Snapshot Three; Three.Coins = 123; const auto Before = Three;
    TestTrue(TEXT("Unrepresentable old revision cannot silently migrate"),!Source145BuildLegacyV1TorsoMigrationCandidate(One,Three) && Same(Three,Before));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundTorsoV3PendingTest, "Jadebound.Persistence.TorsoV3PendingDrops",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundTorsoV3PendingTest::RunTest(const FString& Parameters)
{
    Snapshot S;
    if (!TestTrue(TEXT("Prepare mixed quest/ore ledger"),Source145ConvertLegacyV1ToTorsoSnapshot(OldFixture(2),S).Succeeded())) return false;
    S.EquippedTorso = ItemId::WardenCoat;
    const auto Drops = DropsFor(S);
    if (!TestTrue(TEXT("Two exact records in arbitrary physical order"),Drops.Num() == 2 && Drops[0].DropId == 3 && Drops[1].DropId == 2 && ValidateDropsV3(S,Drops))) return false;
    auto Bad = Drops; Bad.Pop(); TestFalse(TEXT("Missing record"),ValidateDropsV3(S,Bad));
    Bad = Drops; Bad.Add(Drops[0]); TestFalse(TEXT("Extra record"),ValidateDropsV3(S,Bad));
    Bad = Drops; Bad[1] = Bad[0]; TestFalse(TEXT("Duplicate record"),ValidateDropsV3(S,Bad));
    Bad = Drops; Bad[0].DropId = 4; TestFalse(TEXT("Already retired record"),ValidateDropsV3(S,Bad));
    Bad = Drops; Bad[0].DropId = 5; TestFalse(TEXT("Unissued record"),ValidateDropsV3(S,Bad));
    Bad = Drops; Bad[0].Count = 1; TestFalse(TEXT("Ordinary count altered"),ValidateDropsV3(S,Bad));
    Bad = Drops; Bad[1].ItemId = 3; TestFalse(TEXT("Quest parcel replaced by ore"),ValidateDropsV3(S,Bad));
    Bad = Drops; Bad[0].ItemId = 8; TestFalse(TEXT("Codec does not invent armor loot"),ValidateDropsV3(S,Bad));
    Bad = Drops; Bad[0].Location.X = std::numeric_limits<double>::infinity(); TestFalse(TEXT("Infinite position"),ValidateDropsV3(S,Bad));
    Bad = Drops; Bad[0].Location.Y = std::numeric_limits<double>::quiet_NaN(); TestFalse(TEXT("NaN position"),ValidateDropsV3(S,Bad));
    Bad = Drops; Bad[0].Location.Z = -1; TestFalse(TEXT("Below-world position"),ValidateDropsV3(S,Bad));
    Bad = Drops; Bad[0].Location.Z = 301; TestFalse(TEXT("Above-world position"),ValidateDropsV3(S,Bad));
    Bad = Drops; Bad[0].Location.X = 9999; TestFalse(TEXT("Outside original courtyard"),ValidateDropsV3(S,Bad));
    Bad = Drops; Bad[0].Location = FVector(150,350,0); Bad[1].Location = FVector(150,350,300);
    TestTrue(TEXT("Original vertical endpoints remain inclusive"),ValidateDropsV3(S,Bad));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundTorsoV3MemoryArchiveTest, "Jadebound.Persistence.TorsoV3MemoryArchive",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundTorsoV3MemoryArchiveTest::RunTest(const FString& Parameters)
{
    // Same current producer/consumer only. Genuine old producer bytes and absent
    // property tags require separate native qualification; no physical IO here.
    const auto* Torso = FindFProperty<FIntProperty>(FJadeboundProgressionState::StaticStruct(),TEXT("EquippedTorso"));
    if (!TestTrue(TEXT("Torso is the exact integer SaveGame field"),Torso && Torso->HasAnyPropertyFlags(CPF_SaveGame))) return false;
    const auto* ExactRevision = FindFProperty<FUInt64Property>(FJadeboundProgressionState::StaticStruct(),TEXT("ExactRevision"));
    if (!TestTrue(TEXT("ExactRevision retains its unsigned wire property and FName"),ExactRevision && ExactRevision->HasAnyPropertyFlags(CPF_SaveGame))) return false;
    TestEqual(TEXT("Save class path unchanged"),UJadeboundCharacterSave::StaticClass()->GetPathName(),FString(TEXT("/Script/JadeboundProgression.JadeboundCharacterSave")));
    Snapshot Original;
    if (!TestTrue(TEXT("Prepare outstanding quest fixture"),Source145ConvertLegacyV1ToTorsoSnapshot(OldFixture(2),Original).Succeeded())) return false;
    Original.EquippedTorso = ItemId::WardenCoat;
    auto* Save = Cast<UJadeboundCharacterSave>(UGameplayStatics::CreateSaveGameObject(UJadeboundCharacterSave::StaticClass()));
    if (!TestNotNull(TEXT("Synthetic memory-only V3 save"),Save)) return false;
    Save->CharacterId = TEXT("Traveler01");
    if (!TestTrue(TEXT("Encode current-class V3"),EncodeStateV3(Original,Save->State))) return false;
    Save->PendingDrops = DropsFor(Original);
    TArray<uint8> Bytes;
    if (!TestTrue(TEXT("Serialize current-class memory payload"),UGameplayStatics::SaveGameToMemory(Save,Bytes))) return false;
    auto* Reloaded = Cast<UJadeboundCharacterSave>(UGameplayStatics::LoadGameFromMemory(Bytes));
    if (!TestNotNull(TEXT("Reload current-class memory payload"),Reloaded)) return false;
    Snapshot Decoded;
    TestTrue(TEXT("Full torso/inventory/currency/quest/ledger retained"),Reloaded->CharacterId == Save->CharacterId &&
        SameReflected(Save->State,Reloaded->State) && DecodeStateV3(Reloaded->State,Decoded) && Same(Original,Decoded) &&
        ValidateDropsV3(Decoded,Reloaded->PendingDrops));
    if (!TestEqual(TEXT("Pending count preserved"),Save->PendingDrops.Num(),Reloaded->PendingDrops.Num())) return false;
    for (int32 I = 0; I < Save->PendingDrops.Num(); ++I)
        TestTrue(TEXT("Exact record fields/location/order preserved"),FJadeboundSavedDrop::StaticStruct()->CompareScriptStruct(&Save->PendingDrops[I],&Reloaded->PendingDrops[I],0));

    // Default-valued properties may be omitted by tagged delta serialization.
    // Valid V3 None is accepted by values; this does not assert tag presence.
    Original.EquippedTorso = ItemId::None;
    if (!TestTrue(TEXT("Encode explicit V3 with no torso"),EncodeStateV3(Original,Save->State))) return false;
    Bytes.Reset();
    if (!TestTrue(TEXT("Serialize V3 default torso"),UGameplayStatics::SaveGameToMemory(Save,Bytes))) return false;
    Reloaded = Cast<UJadeboundCharacterSave>(UGameplayStatics::LoadGameFromMemory(Bytes));
    if (!TestNotNull(TEXT("Load V3 default torso"),Reloaded)) return false;
    TestTrue(TEXT("Version3 survives while None remains valid"),Reloaded->State.Version == 3 && Reloaded->State.EquippedTorso == 0 &&
        DecodeStateV3(Reloaded->State,Decoded) && Same(Original,Decoded));

    // Current-producer malicious mixtures survive serialization as values and
    // are refused by the explicit historical codecs, not silently truncated.
    for (const int32 Version : {1,2})
    {
        Save->State.Version = Version; Save->State.EquippedTorso = 8;
        Bytes.Reset();
        if (!TestTrue(TEXT("Serialize intentional mixed-version negative fixture"),UGameplayStatics::SaveGameToMemory(Save,Bytes))) return false;
        Reloaded = Cast<UJadeboundCharacterSave>(UGameplayStatics::LoadGameFromMemory(Bytes));
        if (!TestNotNull(TEXT("Load mixed-version negative fixture"),Reloaded)) return false;
        V1::Snapshot One; const auto BeforeOne = One;
        V2::Snapshot Two; const auto BeforeTwo = Two;
        TestTrue(TEXT("Tagged mixed-version payload retains torso then legacy decoder refuses"),Reloaded->State.EquippedTorso == 8 &&
            (Version == 1 ? (!DecodeStateV1(Reloaded->State,One) && Same(One,BeforeOne)) :
                            (!DecodeStateV2(Reloaded->State,Two) && Same(Two,BeforeTwo))));
    }
    Save->State.Version = 4; Save->State.EquippedTorso = 0;
    Bytes.Reset();
    if (!TestTrue(TEXT("Serialize future-version negative fixture"),UGameplayStatics::SaveGameToMemory(Save,Bytes))) return false;
    Reloaded = Cast<UJadeboundCharacterSave>(UGameplayStatics::LoadGameFromMemory(Bytes));
    if (!TestNotNull(TEXT("Load future-version negative fixture"),Reloaded)) return false;
    const auto BeforeFutureRefusal = Decoded;
    TestTrue(TEXT("Serialized future version is refused without changing any destination field"),Reloaded->State.Version == 4 &&
        !DecodeStateV3(Reloaded->State,Decoded) && Same(Decoded,BeforeFutureRefusal));
    return true;
}
#endif
