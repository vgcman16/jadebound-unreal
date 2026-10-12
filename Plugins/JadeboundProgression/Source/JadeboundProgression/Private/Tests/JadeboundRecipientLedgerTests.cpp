#include "Domain/RecipientLedger.h"
#include "Persistence/ProgressionSaveV2.h"
#include "Misc/AutomationTest.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
bool SameState(const Jadebound::Snapshot& A, const Jadebound::Snapshot& B)
{
    if (A.Version != B.Version || A.Revision != B.Revision || A.Coins != B.Coins ||
        A.EquippedWeapon != B.EquippedWeapon || A.Quest != B.Quest || A.NextDropId != B.NextDropId ||
        A.QuestKillDropId != B.QuestKillDropId || A.UnprocessedDropIds != B.UnprocessedDropIds) return false;
    for (uint32 Index = 0; Index < Jadebound::InventorySlotCount; ++Index)
        if (A.Inventory[Index].Id != B.Inventory[Index].Id || A.Inventory[Index].Count != B.Inventory[Index].Count) return false;
    return true;
}
bool SameDrops(const TArray<FJadeboundSavedDrop>& A, const TArray<FJadeboundSavedDrop>& B)
{
    if (A.Num() != B.Num()) return false;
    for (int32 Index = 0; Index < A.Num(); ++Index)
        if (A[Index].DropId != B[Index].DropId || A[Index].ItemId != B[Index].ItemId ||
            A[Index].Count != B[Index].Count || A[Index].Location != B[Index].Location) return false;
    return true;
}
bool SameCandidate(const Jadebound::Recipient::IssueCandidate& A, const Jadebound::Recipient::IssueCandidate& B)
{
    return A.DropId == B.DropId && SameState(A.State, B.State) && SameDrops(A.Drops, B.Drops);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundRecipientPairTest, "Jadebound.RecipientCombat.V2IssuePair",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FJadeboundRecipientPairTest::RunTest(const FString&)
{
    using namespace Jadebound;
    ProgressionCore Core;
    if (!TestTrue(TEXT("Known accepted quest"), Core.AcceptQuest().Succeeded())) return false;
    const Snapshot Before = Core.GetSnapshot();
    const TArray<FJadeboundSavedDrop> Empty;
    const FVector Location(150, 350, 142.15);
    Recipient::IssueCandidate Issued;
    const Result Result = Recipient::BuildIssue(Before, Empty, Location, Issued);
    if (!TestTrue(TEXT("Parcel pair issued once"), Result.Succeeded() && Result.Changed)) return false;
    Snapshot Expected = Before;
    Expected.Revision = 2;
    Expected.Quest = QuestState::RaiderDefeated;
    Expected.NextDropId = 2;
    Expected.QuestKillDropId = 1;
    Expected.UnprocessedDropIds = {1};
    TestTrue(TEXT("Whole known-input state oracle"), SameState(Expected, Issued.State));
    TestTrue(TEXT("Input state not mutated"), SameState(Before, Core.GetSnapshot()) && Empty.IsEmpty());
    TestEqual(TEXT("Allocated own next ID"), Issued.DropId, int64(1));
    if (!TestEqual(TEXT("One matching pending record"), Issued.Drops.Num(), 1)) return false;
    TestTrue(TEXT("Exact parcel definition/location"), Issued.Drops[0].DropId == 1 && Issued.Drops[0].ItemId == 5 &&
        Issued.Drops[0].Count == 1 && Issued.Drops[0].Location == Location);
    TestTrue(TEXT("Pair validates for actual actor codec"), Persistence::ValidateDropsV2(Issued.State, Issued.Drops));
    Recipient::IssueCandidate Second;
    TestTrue(TEXT("Next ordinary defeat pair"), Recipient::BuildIssue(Issued.State, Issued.Drops, Location, Second).Succeeded());
    if (!TestEqual(TEXT("Both outstanding records retained"), Second.Drops.Num(), 2)) return false;
    Snapshot ExpectedSecond = Expected;
    ExpectedSecond.Revision = 3;
    ExpectedSecond.NextDropId = 3;
    ExpectedSecond.UnprocessedDropIds = {1, 2};
    TestTrue(TEXT("Second whole state oracle"), SameState(ExpectedSecond, Second.State));
    TestTrue(TEXT("Original parcel unchanged; ordinary ore definition"), SameDrops(Issued.Drops, {Second.Drops[0]}) &&
        Second.DropId == 2 && Second.Drops[1].DropId == 2 && Second.Drops[1].ItemId == 3 && Second.Drops[1].Count == 2);
    FJadeboundProgressionState Encoded;
    Snapshot Decoded;
    TestTrue(TEXT("Pure current codec roundtrip preserves full pair"), Persistence::EncodeStateV2(Second.State, Encoded) &&
        Persistence::DecodeStateV2(Encoded, Decoded) && SameState(Second.State, Decoded) && Persistence::ValidateDropsV2(Decoded, Second.Drops));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundRecipientRefusalTest, "Jadebound.RecipientCombat.V2IssueRefusals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FJadeboundRecipientRefusalTest::RunTest(const FString&)
{
    using namespace Jadebound;
    const FVector Location(150, 350, 142.15);
    Snapshot Empty;
    TArray<FJadeboundSavedDrop> NoDrops;
    Recipient::IssueCandidate Output;
    if (!TestTrue(TEXT("Create complete sentinel output"), Recipient::BuildIssue(Empty, NoDrops, Location, Output).Succeeded())) return false;
    const Recipient::IssueCandidate Sentinel = Output;
    auto Refused = [&](const TCHAR* Name, const Snapshot& Input, const TArray<FJadeboundSavedDrop>& Drops,
        const FVector& Point, ResultCode Code)
    {
        const Snapshot Prior = Input;
        const TArray<FJadeboundSavedDrop> PriorDrops = Drops;
        const Result Result = Recipient::BuildIssue(Input, Drops, Point, Output);
        TestTrue(Name, !Result.Succeeded() && Result.Code == Code && !Result.Changed &&
            SameState(Input, Prior) && SameDrops(Drops, PriorDrops) && SameCandidate(Output, Sentinel));
    };
    Refused(TEXT("Missing corresponding pending record"), Sentinel.State, NoDrops, Location, ResultCode::InvalidSnapshot);
    TArray<FJadeboundSavedDrop> Wrong = Sentinel.Drops;
    Wrong[0].Count = 3;
    Refused(TEXT("Wrong fixed loot count"), Sentinel.State, Wrong, Location, ResultCode::InvalidSnapshot);
    Refused(TEXT("Nonfinite provider location"), Empty, NoDrops,
        FVector(std::numeric_limits<double>::quiet_NaN(), 350, 142.15), ResultCode::InvalidSnapshot);
    Refused(TEXT("Out-of-arena location"), Empty, NoDrops, FVector(150,350,301), ResultCode::InvalidSnapshot);
    Snapshot Exhausted = Empty;
    Exhausted.Revision = MaximumPersistedCounter;
    Refused(TEXT("Revision exhaustion unchanged"), Exhausted, NoDrops, Location, ResultCode::RevisionOverflow);
    Snapshot Invalid = Empty;
    Invalid.NextDropId = 0;
    Refused(TEXT("Allocator reset refused"), Invalid, NoDrops, Location, ResultCode::InvalidSnapshot);
    Snapshot Full = Empty;
    Full.Revision = MaximumUnprocessedDrops;
    Full.NextDropId = MaximumUnprocessedDrops + 1ULL;
    TArray<FJadeboundSavedDrop> FullDrops;
    for (uint64 Id = 1; Id < Full.NextDropId; ++Id)
    {
        Full.UnprocessedDropIds.push_back(Id);
        FJadeboundSavedDrop Drop;
        Drop.DropId = static_cast<int64>(Id); Drop.ItemId = 3; Drop.Count = 2; Drop.Location = Location;
        FullDrops.Add(Drop);
    }
    TestTrue(TEXT("Known full pending pair is valid"), Persistence::ValidateDropsV2(Full, FullDrops));
    Refused(TEXT("Pending cap failure preserves every output/input field"), Full, FullDrops, Location, ResultCode::DropLedgerFull);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundRecipientWitnessTest, "Jadebound.RecipientCombat.V2CommittedWitness",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FJadeboundRecipientWitnessTest::RunTest(const FString&)
{
    using namespace Jadebound;
    const FVector Location(150,350,142.15);
    Recipient::IssueCandidate First;
    Snapshot Initial;
    if (!TestTrue(TEXT("First ordinary issue"), Recipient::BuildIssue(Initial, {}, Location, First).Succeeded())) return false;
    TestTrue(TEXT("Pending issue witness"), Recipient::HasCommittedIssue(First.State, First.Drops, 1, 1));
    ProgressionCore Core;
    if (!TestTrue(TEXT("Adopt/pickup own issued drop"), Core.LoadSnapshot(First.State).Succeeded() &&
        Core.GrantLoot(ItemId::IronOre, 2, 1).Succeeded())) return false;
    const TArray<FJadeboundSavedDrop> None;
    const Snapshot Collected = Core.GetSnapshot();
    TestTrue(TEXT("Collected issued ID remains a committed witness"), Recipient::HasCommittedIssue(Collected, None, 1, 1));
    const Result Replay = Core.GrantLoot(ItemId::IronOre, 2, 1);
    TestTrue(TEXT("Old ID cannot pay again after compaction"), Replay.Code == ResultCode::DropAlreadyProcessed && SameState(Collected, Core.GetSnapshot()));
    FJadeboundProgressionState Encoded;
    Snapshot Reopened;
    if (!TestTrue(TEXT("Actual v2 codec full-state roundtrip"), Persistence::EncodeStateV2(Collected, Encoded) &&
        Persistence::DecodeStateV2(Encoded, Reopened) && SameState(Collected, Reopened))) return false;
    TestTrue(TEXT("Reopened collected prefix remains witnessed"), Recipient::HasCommittedIssue(Reopened, None, 1, 1));
    TestFalse(TEXT("Future unissued ID"), Recipient::HasCommittedIssue(Reopened, None, 2, 1));
    TestFalse(TEXT("Negative/zero ID"), Recipient::HasCommittedIssue(Reopened, None, -1, 1) || Recipient::HasCommittedIssue(Reopened, None, 0, 1));
    TestFalse(TEXT("Uncommitted revision"), Recipient::HasCommittedIssue(Reopened, None, 1, 3));
    TestFalse(TEXT("Unexpected leftover collected pending"), Recipient::HasCommittedIssue(Reopened, First.Drops, 1, 1));
    Recipient::IssueCandidate Next;
    TestTrue(TEXT("Next life gets the next allocator ID"), Recipient::BuildIssue(Reopened, None, Location, Next).Succeeded() && Next.DropId == 2 && Next.State.NextDropId == 3);
    TestTrue(TEXT("Old issued witness survives later allocation"), Recipient::HasCommittedIssue(Next.State, Next.Drops, 1, 1));
    Snapshot Corrupt = Next.State;
    Corrupt.UnprocessedDropIds.push_back(3);
    TestFalse(TEXT("Malformed ledger cannot be duplicate proof"), Recipient::HasCommittedIssue(Corrupt, Next.Drops, 1, 1));
    return true;
}
#endif
