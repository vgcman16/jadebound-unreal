#include "JadeboundProgressionActor.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/Source155ActorBankFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundActorJournalFaultRetryTest, "Jadebound.Persistence.ActorJournalFaultRetry",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundActorJournalFaultRetryTest::RunTest(const FString& Parameters)
{
    using namespace JadeboundActorBanks;
    using Access = FJadeboundActorBankTestAccess;
    if (!IsInGameThread()) { AddError(TEXT("Actor-bank tests require the game thread.")); return false; }
    const EFault Faults[] = {EFault::Open, EFault::PartialWrite, EFault::UncertainFlush,
        EFault::CorruptAfterClose, EFault::DifferentValidAfterClose};
    Jadebound::Snapshot ComparatorA, ComparatorB;
    ComparatorB.EquippedTorso = Jadebound::ItemId::WardenCoat;
    TestFalse(TEXT("Complete current-state comparison detects a torso-only difference"), SameCore(ComparatorA, ComparatorB));
    for (const auto Fault : Faults)
    for (const bool bSyntheticTorso : {false, true})
    {
        FScope Scope(*this, bSyntheticTorso ? TEXT("synthetic-torso-write-fault-held-retry") : TEXT("write-fault-held-retry"));
        if (!Scope.bValid) return false;
        Jadebound::Snapshot Before;
        if (bSyntheticTorso)
        {
            Before.EquippedTorso = Jadebound::ItemId::WardenCoat;
            TArray<uint8> Seed;
            if (!MakeV3Fixture(Scope, Before, Seed) || !Scope.PutV3(0, Seed)) return false;
        }
        auto KnownPurchase = Before;
        KnownPurchase.Revision = 1; KnownPurchase.Coins = 8;
        KnownPurchase.Inventory[0] = {Jadebound::ItemId::Moonleaf, 1};
        auto* Actor = Scope.NewActor();
        if (!Actor || !Access::Read(*Actor)) return false;
        TArray<uint8> OldA;
        if (!ReadOwned(Scope.V3Path(0), OldA)) return false;
        const auto* OldCommitted = Access::Committed(*Actor);
        if (!OldCommitted) return false;
        const auto OldCommittedState = OldCommitted->State;
        const auto OldCommittedDrops = OldCommitted->PendingDrops;
        FTrace Trace;
        Trace.Fault = Fault; Trace.ExpectedPath = Scope.V3Path(1); Trace.Substitute = OldA;
        Install(*Actor, Trace);
        TestFalse(TEXT("Actual save rejects the injected OS-facing fault"), Access::BuyMoonleafOnce(*Actor));
        // Test the production deadline before modifying any timing field.
        TestTrue(TEXT("Production backoff deadline lies in the future"), Access::Deadline(*Actor) > FPlatformTime::Seconds());
        TestFalse(TEXT("Unaltered immediate retry is refused"), Access::Retry(*Actor));
        TestEqual(TEXT("Immediate retry did not open again"), Trace.Opens, 1);
        const auto FirstCache = Access::HeldBytes(*Actor);
        TestTrue(TEXT("First complete encoding and current destination survive even a refused open"),
            CacheMatches(*Actor, FirstCache, Scope.V3Path(1), 1));
        const auto ExpectedCore = Access::CoreState(*Actor);
        TestTrue(TEXT("Whole independently known purchase preserves synthetic torso when present"), SameCore(KnownPurchase, ExpectedCore));
        const auto ExpectedState = Actor->GetProgressionState();
        TestTrue(TEXT("Exactly one trusted purchase changed the complete fixture"), ExpectedCore.Revision == 1 && ExpectedCore.Coins == 8 &&
            ExpectedCore.Inventory[0].Id == Jadebound::ItemId::Moonleaf && ExpectedCore.Inventory[0].Count == 1 && ExpectedCore.UnprocessedDropIds.empty());
        const auto* Held = Access::Held(*Actor);
        if (!TestTrue(TEXT("Exact copied candidate remains held"), Held && Access::IsHeld(*Actor))) return false;
        const TArray<FJadeboundSavedDrop> ExpectedDrops = Held->PendingDrops;
        TestTrue(TEXT("Held character and complete reflected state match"), Held->CharacterId == Scope.Id && SameReflected(Held->State, ExpectedState));
        TestTrue(TEXT("Retryable presentation blocks persistence"), Actor->GetPersistencePresentation().FailureKind == EJadeboundPersistenceFailureKind::Retryable);
        TestTrue(TEXT("Old complete commit and slot unchanged"), Access::Committed(*Actor) == OldCommitted && Access::Slot(*Actor) == 0 &&
            SameReflected(OldCommittedState, OldCommitted->State) && SameDrops(OldCommittedDrops, OldCommitted->PendingDrops));
        TArray<uint8> CurrentA;
        TestTrue(TEXT("Old A bytes survive uncertainty"), ReadOwned(Scope.V3Path(0), CurrentA) && CurrentA == OldA);
        TestTrue(TEXT("Owned handle destruction completed or open was refused"), Trace.Closes == (Fault == EFault::Open ? 0 : 1));
        TestTrue(TEXT("Write refusal skips flush; other opened cases request real full flush"),
            Trace.Flushes == (Fault == EFault::Open || Fault == EFault::PartialWrite ? 0 : 1) &&
            (Trace.Flushes == 0 || (Trace.bFullFlush && Trace.bRealFlushSucceeded)));
        TestTrue(TEXT("No path escape and owned corruption succeeded"), !Trace.bPathRefused && Trace.bMutationSucceeded);
        const FString ExpectedEvents = Fault == EFault::Open ? TEXT("open") : Fault == EFault::PartialWrite ? TEXT("open|write|close") :
            (Fault == EFault::CorruptAfterClose || Fault == EFault::DifferentValidAfterClose) ?
                TEXT("open|write|flush|close|owned-after-close-mutation") : TEXT("open|write|flush|close");
        TestEqual(TEXT("Actual real-handle ordering"), FString::Join(Trace.Events, TEXT("|")), ExpectedEvents);
        if (Fault == EFault::PartialWrite)
            TestEqual(TEXT("Refused write left a real eight-byte partial"), FPlatformFileManager::Get().GetPlatformFile().FileSize(*Trace.ExpectedPath), int64(8));
        if (Fault == EFault::Open)
            TestFalse(TEXT("Refused open created no target bank"), FPlatformFileManager::Get().GetPlatformFile().FileExists(*Trace.ExpectedPath));
        const auto FirstPayload = Trace.Payload;
        TestTrue(TEXT("Actual first write received the cached buffer when opening succeeded"),
            Fault == EFault::Open || FirstPayload == FirstCache);
        TestTrue(TEXT("Held state refuses read and second fixture mutation without altering cached wire"),
            !Access::Read(*Actor, false) && !Access::BuyMoonleafOnce(*Actor) && Trace.Opens == 1 &&
            Access::Held(*Actor) == Held && SameCore(ExpectedCore, Access::CoreState(*Actor)) &&
            SameReflected(Held->State, ExpectedState) && SameDrops(Held->PendingDrops, ExpectedDrops) &&
            CacheMatches(*Actor, FirstCache, Scope.V3Path(1), 1));
        // This changes only this test actor's deadline: candidate/commit test,
        // not an elapsed-backoff/scheduler qualification or production bypass.
        Access::ExpireLocalFixtureDeadline(*Actor);
        TestTrue(TEXT("Local deadline fixture preserves the exact held object"), Access::Held(*Actor) == Held && SameReflected(Held->State, ExpectedState) && SameDrops(Held->PendingDrops, ExpectedDrops));
        Trace.Fault = EFault::None;
        TestTrue(TEXT("Retry of exact candidate writes/flushes/reads real B"), Access::Retry(*Actor));
        TestTrue(TEXT("Retry never reruns the transaction"), SameCore(ExpectedCore, Access::CoreState(*Actor)) && SameReflected(ExpectedState, Actor->GetProgressionState()));
        TestTrue(TEXT("Committed complete candidate and drops match"), Access::Committed(*Actor) &&
            SameReflected(Access::Committed(*Actor)->State, ExpectedState) && SameDrops(Access::Committed(*Actor)->PendingDrops, ExpectedDrops));
        TestTrue(TEXT("Retry reused the first cached payload including an initially refused open"), FirstCache == Trace.Payload && (FirstPayload.IsEmpty() || FirstPayload == Trace.Payload));
        TestTrue(TEXT("Readback alone clears the held candidate"), !Access::Held(*Actor) && !Access::IsHeld(*Actor) &&
            Actor->GetPersistencePresentation().FailureKind == EJadeboundPersistenceFailureKind::None && Access::Slot(*Actor) == 1);
        TestTrue(TEXT("Verified commit clears cached destination and leaves both old pairs absent"), CacheCleared(*Actor) && Scope.OlderPairsAbsent());
        Scope.Retire(*Actor);
        auto* Reloaded = Scope.NewActor();
        if (!Reloaded) return false;
        TestTrue(TEXT("Retried exact state really reopens"), Access::Read(*Reloaded) && SameCore(ExpectedCore, Access::CoreState(*Reloaded)));
    }
    AddInfo(TEXT("Synthetic actor-bank files retained; no gameplay/network/power-loss/elapsed-scheduler/genuine-old-tag acceptance."));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundActorJournalReopenTest, "Jadebound.Persistence.ActorJournalReopen",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundActorJournalReopenTest::RunTest(const FString& Parameters)
{
    using namespace JadeboundActorBanks;
    using Access = FJadeboundActorBankTestAccess;
    if (!IsInGameThread()) { AddError(TEXT("Actor-bank tests require the game thread.")); return false; }
    {
        FScope Scope(*this, TEXT("normal-reopen"));
        auto* Actor = Scope.NewActor();
        if (!Actor || !TestTrue(TEXT("Fresh actual A write/readback"), Access::Read(*Actor))) return false;
        TestEqual(TEXT("Fresh A is selected"), Access::Slot(*Actor), 0);
        TestTrue(TEXT("One real Core purchase and actual B commit"), Access::BuyMoonleafOnce(*Actor));
        const auto Expected = Access::CoreState(*Actor);
        TestEqual(TEXT("Exactly one purchase revision"), Expected.Revision, uint64(1));
        TestEqual(TEXT("Catalog Moonleaf price leaves eight coins"), Expected.Coins, int64(8));
        Scope.Retire(*Actor);
        auto* Reloaded = Scope.NewActor();
        if (!Reloaded) return false;
        TestTrue(TEXT("New actor opens real bank pair"), Access::Read(*Reloaded));
        TestTrue(TEXT("Complete Core snapshot survives reopen"), SameCore(Expected, Access::CoreState(*Reloaded)));
        TestEqual(TEXT("Highest revision B selected"), Access::Slot(*Reloaded), 1);
    }
    {
        FScope Scope(*this, TEXT("uncertain-written-bank-reopen"));
        auto* Actor = Scope.NewActor();
        if (!Actor || !Access::Read(*Actor)) return false;
        FTrace Trace; Trace.Fault = EFault::UncertainFlush; Trace.ExpectedPath = Scope.V3Path(1);
        Install(*Actor, Trace);
        TestFalse(TEXT("Reported flush failure holds live model"), Access::BuyMoonleafOnce(*Actor));
        const auto Expected = Access::CoreState(*Actor);
        TestTrue(TEXT("OS write/full flush occurred despite injected uncertainty"), Trace.bRealWriteSucceeded && Trace.bRealFlushSucceeded && Access::IsHeld(*Actor));
        Scope.Retire(*Actor); // Deliberately no retry or second transaction.
        auto* Reloaded = Scope.NewActor();
        if (!Reloaded) return false;
        TestTrue(TEXT("New actor may recover complete higher uncertain bank"), Access::Read(*Reloaded) && SameCore(Expected, Access::CoreState(*Reloaded)) && Access::Slot(*Reloaded) == 1);
    }
    AddInfo(TEXT("Synthetic actor-bank files retained; no gameplay/network/power-loss/elapsed-scheduler/genuine-old-tag acceptance."));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundActorJournalRecoveryTest, "Jadebound.Persistence.ActorJournalRecovery",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundActorJournalRecoveryTest::RunTest(const FString& Parameters)
{
    using namespace JadeboundActorBanks;
    using Access = FJadeboundActorBankTestAccess;
    if (!IsInGameThread()) { AddError(TEXT("Actor-bank tests require the game thread.")); return false; }
    {
        FScope Scope(*this, TEXT("corrupt-higher-v3-recovery"));
        auto* Actor = Scope.NewActor();
        if (!Actor || !Access::Read(*Actor)) return false;
        const auto Lower = Access::CoreState(*Actor);
        if (!Access::BuyMoonleafOnce(*Actor)) return false;
        Scope.Retire(*Actor);
        const TArray<uint8> Corrupt = {1, 2, 3};
        if (!Scope.PutV3(1, Corrupt)) return false;
        auto* Reloaded = Scope.NewActor();
        if (!Reloaded) return false;
        TestTrue(TEXT("Actual reader falls back to valid lower v3"), Access::Read(*Reloaded) && SameCore(Lower, Access::CoreState(*Reloaded)) && Access::Slot(*Reloaded) == 0);
        TArray<uint8> After;
        TestTrue(TEXT("Invalid bank retained as evidence"), ReadOwned(Scope.V3Path(1), After) && After == Corrupt);
    }
    {
        FScope Scope(*this, TEXT("malformed-v2-refuses-legacy"));
        if (!Scope.bValid) return false;
        TArray<uint8> Legacy;
        FJadeboundProgressionState LegacyState;
        if (!MakeV1Fixture(Scope, JadeboundLegacyV1::Snapshot{}, Legacy, LegacyState)) return false;
        const TArray<uint8> Corrupt = {1, 2, 3};
        if (!Scope.PutV1(0, Legacy) || !Scope.PutV1(1, Legacy) || !Scope.PutV2(0, Corrupt)) return false;
        auto* Reloaded = Scope.NewActor();
        if (!Reloaded) return false;
        TestFalse(TEXT("Any existing malformed v2 forbids valid legacy fallback"), Access::Read(*Reloaded));
        TestTrue(TEXT("Format boundary holds RecoveryRequired"), Reloaded->GetPersistencePresentation().FailureKind == EJadeboundPersistenceFailureKind::RecoveryRequired);
        TArray<uint8> A, B, V2;
        TestTrue(TEXT("Both originals and v2 partial are untouched"), ReadOwned(Scope.V1Path(0), A) && A == Legacy &&
            ReadOwned(Scope.V1Path(1), B) && B == Legacy && ReadOwned(Scope.V2Path(0), V2) && V2 == Corrupt);
        TestFalse(TEXT("No replacement v2 B created"), FPlatformFileManager::Get().GetPlatformFile().FileExists(*Scope.V2Path(1)));
        TestTrue(TEXT("Refused historical namespace creates no V3 destination"),
            !Jadebound::Persistence::HasJournalEntry(FPlatformFileManager::Get().GetPlatformFile(), Scope.V3Path(0)) &&
            !Jadebound::Persistence::HasJournalEntry(FPlatformFileManager::Get().GetPlatformFile(), Scope.V3Path(1)));
    }
    {
        FScope Scope(*this, TEXT("malformed-v3-refuses-both-old-pairs"));
        if (!Scope.bValid) return false;
        TArray<uint8> V1, V2;
        FJadeboundProgressionState One, Two;
        if (!MakeV1Fixture(Scope, JadeboundLegacyV1::Snapshot{}, V1, One) ||
            !MakeV2Fixture(Scope, JadeboundLegacyV2::Snapshot{}, V2, Two)) return false;
        const TArray<uint8> Corrupt = {1, 2, 3};
        if (!Scope.PutV1(0, V1) || !Scope.PutV1(1, V1) || !Scope.PutV2(0, V2) || !Scope.PutV2(1, V2) || !Scope.PutV3(0, Corrupt)) return false;
        auto* Actor = Scope.NewActor(); if (!Actor) return false;
        int32 Opens = 0;
        Access::SetOpener(*Actor, [&Opens](const FString&) -> IFileHandle* { ++Opens; return nullptr; });
        TestTrue(TEXT("Malformed current V3 refuses before old history adoption or any write"),
            !Access::Read(*Actor) && !Access::Admitted(*Actor) && !Access::Committed(*Actor) && !Access::Held(*Actor) && Opens == 0 &&
            SameCore(Jadebound::Snapshot{}, Access::CoreState(*Actor)) &&
            Actor->GetPersistencePresentation().FailureKind == EJadeboundPersistenceFailureKind::RecoveryRequired);
        TArray<uint8> A, B;
        for (int32 Bank = 0; Bank < 2; ++Bank)
            TestTrue(TEXT("Both actual historical pairs survive current-format refusal"),
                ReadOwned(Scope.V1Path(Bank), A) && A == V1 && ReadOwned(Scope.V2Path(Bank), B) && B == V2);
        TestTrue(TEXT("Malformed V3 evidence remains and no replacement B is created"),
            ReadOwned(Scope.V3Path(0), A) && A == Corrupt &&
            !Jadebound::Persistence::HasJournalEntry(FPlatformFileManager::Get().GetPlatformFile(), Scope.V3Path(1)));
        Access::SetOpener(*Actor, nullptr);
    }
    {
        FScope Scope(*this, TEXT("equal-revision-original-byte-conflict"));
        auto* Fixture = Scope.NewActor();
        if (!Fixture || !Access::Read(*Fixture)) return false;
        TArray<uint8> A;
        if (!ReadOwned(Scope.V3Path(0), A)) return false;
        auto* Other = Cast<UJadeboundCharacterSave>(UGameplayStatics::CreateSaveGameObject(UJadeboundCharacterSave::StaticClass()));
        if (!Other) return false;
        Other->CharacterId = Scope.Id;
        Other->State = Fixture->GetProgressionState();
        Other->State.Coins = 9; // Synthetic valid equal-revision fixture; not earned progression.
        Jadebound::Snapshot Valid;
        TArray<uint8> B;
        if (!Jadebound::Persistence::DecodeStateV3(Other->State, Valid) || !Access::EncodeSyntheticFixture(Other, B) || A == B) return false;
        Scope.Retire(*Fixture);
        if (!Scope.PutV3(1, B)) return false;
        auto* Conflicted = Scope.NewActor();
        if (!Conflicted) return false;
        TestFalse(TEXT("Equal revision with different original bytes holds"), Access::Read(*Conflicted));
        TestTrue(TEXT("Conflict requests recovery"), Conflicted->GetPersistencePresentation().FailureKind == EJadeboundPersistenceFailureKind::RecoveryRequired);
        TArray<uint8> AfterA, AfterB;
        TestTrue(TEXT("Conflict preserves both actual streams"), ReadOwned(Scope.V3Path(0), AfterA) && A == AfterA && ReadOwned(Scope.V3Path(1), AfterB) && B == AfterB);
    }
    {
        FScope Scope(*this, TEXT("identical-equal-revision-streams"));
        auto* Fixture = Scope.NewActor();
        if (!Fixture || !Access::Read(*Fixture)) return false;
        TArray<uint8> A;
        if (!ReadOwned(Scope.V3Path(0), A)) return false;
        Scope.Retire(*Fixture);
        if (!Scope.PutV3(1, A)) return false;
        auto* Identical = Scope.NewActor();
        if (!Identical) return false;
        TestTrue(TEXT("Identical equal-revision streams are accepted"), Access::Read(*Identical));
    }
    AddInfo(TEXT("Synthetic actor-bank files retained; no gameplay/network/power-loss/elapsed-scheduler/genuine-old-tag acceptance."));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundActorJournalMigrationTest, "Jadebound.Persistence.ActorJournalMigration",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundActorJournalMigrationTest::RunTest(const FString& Parameters)
{
    using namespace JadeboundActorBanks;
    using Access = FJadeboundActorBankTestAccess;
    if (!IsInGameThread()) { AddError(TEXT("Actor-bank tests require the game thread.")); return false; }
    for (const int32 OriginalVersion : {1, 2})
    {
        FScope Scope(*this, OriginalVersion == 1 ? TEXT("synthetic-v1-migration-held-retry") : TEXT("synthetic-v2-migration-held-retry"));
        if (!Scope.bValid) return false;
        // Explicit frozen producer, current class/envelope; not an old archive claim.
        TArray<uint8> Legacy;
        FJadeboundProgressionState ExpectedLegacyState;
        if (OriginalVersion == 1)
        {
            JadeboundLegacyV1::ProgressionCore Setup;
            if (!Setup.Buy(JadeboundLegacyV1::ItemId::Moonleaf, 1).Succeeded() ||
                !MakeV1Fixture(Scope, Setup.GetSnapshot(), Legacy, ExpectedLegacyState) ||
                !Scope.PutV1(0, Legacy) || !Scope.PutV1(1, Legacy)) return false;
        }
        else
        {
            JadeboundLegacyV2::ProgressionCore Setup;
            if (!Setup.Buy(JadeboundLegacyV2::ItemId::Moonleaf, 1).Succeeded() ||
                !MakeV2Fixture(Scope, Setup.GetSnapshot(), Legacy, ExpectedLegacyState) ||
                !Scope.PutV2(0, Legacy) || !Scope.PutV2(1, Legacy)) return false;
        }
        // Independent known fixture oracle: original r1 purchase plus ONE representation revision.
        Jadebound::Snapshot Expected;
        Expected.Revision = 2; Expected.Coins = 8;
        Expected.Inventory[0] = {Jadebound::ItemId::Moonleaf, 1};
        FJadeboundProgressionState ExpectedState;
        if (!Jadebound::Persistence::EncodeStateV3(Expected, ExpectedState)) return false;
        auto* Actor = Scope.NewActor();
        if (!Actor) return false;
        FTrace Trace; Trace.Fault = EFault::UncertainFlush; Trace.ExpectedPath = Scope.V3Path(0);
        Install(*Actor, Trace);
        TestFalse(TEXT("Actual migration remains held after uncertain flush"), Access::Read(*Actor));
        TestTrue(TEXT("Migration preserves every known input field except revision+1"), SameCore(Expected, Access::CoreState(*Actor)) && SameReflected(ExpectedState, Actor->GetProgressionState()));
        TestEqual(TEXT("Exactly one representation revision"), Expected.Revision, uint64(2));
        TestEqual(TEXT("Migration does not award currency"), Expected.Coins, int64(8));
        TestTrue(TEXT("Migration candidate held with complete original still committed"), Access::IsHeld(*Actor) && Access::Committed(*Actor) &&
            Access::Committed(*Actor)->CharacterId == Scope.Id && SameReflected(Access::Committed(*Actor)->State, ExpectedLegacyState) && Access::Committed(*Actor)->PendingDrops.IsEmpty());
        TestTrue(TEXT("Migration stays unready until v3 readback"), Access::MigrationUnready(*Actor));
        const auto* Held = Access::Held(*Actor);
        if (!TestTrue(TEXT("Complete expected migration candidate held"), Held && Held->CharacterId == Scope.Id &&
            SameReflected(Held->State, ExpectedState) && Held->PendingDrops.IsEmpty())) return false;
        const auto HeldDrops = Held->PendingDrops;
        const auto FirstPayload = Trace.Payload;
        const auto FirstCache = Access::HeldBytes(*Actor);
        TestTrue(TEXT("Migration caches the exact first V3 A image under the original witness"),
            CacheMatches(*Actor, FirstCache, Scope.V3Path(0), 0) && FirstPayload == FirstCache);
        TestFalse(TEXT("Migration immediate retry respects original deadline"), Access::Retry(*Actor));
        TestEqual(TEXT("Migration immediate retry does not reopen"), Trace.Opens, 1);
        Access::ExpireLocalFixtureDeadline(*Actor);
        TestTrue(TEXT("Local deadline expiry preserves complete held migration"), Access::Held(*Actor) == Held &&
            SameReflected(Held->State, ExpectedState) && SameDrops(Held->PendingDrops, HeldDrops));
        Trace.Fault = EFault::None;
        TestTrue(TEXT("Actual migration candidate retry succeeds without second increment"), Access::Retry(*Actor) && SameCore(Expected, Access::CoreState(*Actor)));
        TestTrue(TEXT("Migration retry uses identical attempted payload and commits whole state"), !FirstPayload.IsEmpty() && FirstPayload == Trace.Payload &&
            Access::Committed(*Actor) && SameReflected(Access::Committed(*Actor)->State, ExpectedState) && SameDrops(Access::Committed(*Actor)->PendingDrops, HeldDrops) && !Access::Held(*Actor));
        TArray<uint8> A, B;
        const FString OriginalA = OriginalVersion == 1 ? Scope.V1Path(0) : Scope.V2Path(0);
        const FString OriginalB = OriginalVersion == 1 ? Scope.V1Path(1) : Scope.V2Path(1);
        TestTrue(TEXT("Both original streams survive migration and retry"), ReadOwned(OriginalA, A) && A == Legacy && ReadOwned(OriginalB, B) && B == Legacy);
        TestTrue(TEXT("Verified migration clears the first cache and retains the identical attempted bytes"), CacheCleared(*Actor) && Trace.Payload == FirstCache);
        Scope.Retire(*Actor);
        auto* Reloaded = Scope.NewActor();
        if (!Reloaded) return false;
        TestTrue(TEXT("New actor reopens the exact migrated v3 snapshot"), Access::Read(*Reloaded) && SameCore(Expected, Access::CoreState(*Reloaded)));
    }
    AddInfo(TEXT("Synthetic actor-bank files retained; no gameplay/network/power-loss/elapsed-scheduler/genuine-old-tag acceptance."));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundActorJournalQuestRewardTest, "Jadebound.Persistence.ActorJournalQuestReward",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundActorJournalQuestRewardTest::RunTest(const FString& Parameters)
{
    using namespace JadeboundActorBanks;
    using Access = FJadeboundActorBankTestAccess;
    if (!IsInGameThread()) { AddError(TEXT("Actor-bank tests require the game thread.")); return false; }
    // Independent whole-state oracles from the documented fresh character.
    // No expectation is copied from the actor after it awards the reward.
    const Jadebound::Snapshot Initial;
    auto Accepted = Initial;
    Accepted.Revision = 1; Accepted.Quest = Jadebound::QuestState::Accepted;
    auto Recovered = Accepted;
    Recovered.Revision = 3; Recovered.Quest = Jadebound::QuestState::ParcelRecovered;
    Recovered.NextDropId = 2; Recovered.QuestKillDropId = 1;
    Recovered.Inventory[0] = {Jadebound::ItemId::JadeParcel, 1};
    auto Completed = Recovered;
    Completed.Revision = 4; Completed.Quest = Jadebound::QuestState::Completed;
    Completed.Coins = 15; Completed.Inventory[0] = {Jadebound::ItemId::JadeShard, 1};
    FJadeboundProgressionState AcceptedState, RecoveredState, CompletedState;
    if (!Jadebound::Persistence::EncodeStateV3(Accepted, AcceptedState) ||
        !Jadebound::Persistence::EncodeStateV3(Recovered, RecoveredState) ||
        !Jadebound::Persistence::EncodeStateV3(Completed, CompletedState)) return false;
    const bool RetryModes[] = {true, false};
    for (const bool bRetryBeforeReopen : RetryModes)
    {
        FScope Scope(*this, bRetryBeforeReopen ? TEXT("quest-reward-uncertain-retry-reopen") : TEXT("quest-reward-uncertain-direct-reopen"));
        auto* Actor = Scope.NewActor();
        if (!Actor || !TestTrue(TEXT("Fresh character writes actual baseline A"), Access::Read(*Actor)) ||
            !TestTrue(TEXT("Complete initial character matches known oracle"), SameCore(Initial, Access::CoreState(*Actor)))) return false;
        if (!TestTrue(TEXT("Accept quest through real FinishTransaction and save B"), Access::AcceptQuestAndSave(*Actor))) return false;
        TestTrue(TEXT("Acceptance preserves whole known state and commits r1"), SameCore(Accepted, Access::CoreState(*Actor)) &&
            SameReflected(AcceptedState, Actor->GetProgressionState()) && Access::Slot(*Actor) == 1);
        if (!TestTrue(TEXT("Trusted defeat and pickup save recovered state to actual A"), Access::RecoverQuestParcelAndSave(*Actor))) return false;
        TestTrue(TEXT("Recovered parcel matches whole independent r3 oracle"), SameCore(Recovered, Access::CoreState(*Actor)) &&
            SameReflected(RecoveredState, Actor->GetProgressionState()) && Access::Slot(*Actor) == 0);
        const auto* OldCommitted = Access::Committed(*Actor);
        if (!OldCommitted || !TestTrue(TEXT("Real pre-reward commit is recovered and has no outstanding visual drops"),
            SameReflected(RecoveredState, OldCommitted->State) && OldCommitted->PendingDrops.IsEmpty())) return false;
        TArray<uint8> OldA;
        if (!ReadOwned(Scope.V3Path(0), OldA)) return false;
        FTrace Trace;
        Trace.Fault = EFault::UncertainFlush; Trace.ExpectedPath = Scope.V3Path(1);
        Install(*Actor, Trace);
        Jadebound::Result RewardResult;
        TestFalse(TEXT("Reward transaction is held when real flush is reported uncertain"), Access::TurnInQuestAndSave(*Actor, RewardResult));
        // Check the actual deadline before any fixture timing adjustment or bank reads.
        TestTrue(TEXT("Original production backoff deadline is in the future"), Access::Deadline(*Actor) > FPlatformTime::Seconds());
        TestFalse(TEXT("Unaltered immediate retry is refused"), Access::Retry(*Actor));
        TestEqual(TEXT("Immediate refusal opens no second writer"), Trace.Opens, 1);
        TestTrue(TEXT("Exactly one successful reward transaction reached r4"), RewardResult.Succeeded() && RewardResult.Changed && RewardResult.Revision == 4);
        TestTrue(TEXT("Whole rewarded core and reflection match independent oracle"), SameCore(Completed, Access::CoreState(*Actor)) &&
            SameReflected(CompletedState, Actor->GetProgressionState()));
        const auto* Held = Access::Held(*Actor);
        if (!TestTrue(TEXT("Complete reward remains the exact held candidate"), Held && Access::IsHeld(*Actor) &&
            Held->CharacterId == Scope.Id && SameReflected(CompletedState, Held->State) && Held->PendingDrops.IsEmpty())) return false;
        TestTrue(TEXT("Prior recovered commit remains selected during uncertainty"), Access::Committed(*Actor) == OldCommitted &&
            SameReflected(RecoveredState, OldCommitted->State) && OldCommitted->PendingDrops.IsEmpty() && Access::Slot(*Actor) == 0);
        TestTrue(TEXT("Real full write and Flush(true) occurred and handle wrapper closed"), Trace.Opens == 1 && Trace.Writes == 1 &&
            Trace.Flushes == 1 && Trace.Closes == 1 && Trace.bFullFlush && Trace.bRealWriteSucceeded && Trace.bRealFlushSucceeded &&
            !Trace.bPathRefused && FString::Join(Trace.Events, TEXT("|")) == TEXT("open|write|flush|close"));
        const auto FirstPayload = Trace.Payload;
        TestTrue(TEXT("Held reward uses the first cached B image"), CacheMatches(*Actor, FirstPayload, Scope.V3Path(1), 1));
        TArray<uint8> CurrentA, UncertainB;
        if (!TestTrue(TEXT("Old r3 A survives and uncertain B contains the full attempted reward bytes"),
            ReadOwned(Scope.V3Path(0), CurrentA) && CurrentA == OldA && !FirstPayload.IsEmpty() &&
            ReadOwned(Scope.V3Path(1), UncertainB) && UncertainB == FirstPayload)) return false;
        Jadebound::Result HeldRepeat;
        TestFalse(TEXT("Repeated trusted turn-in cannot pay while save is held"), Access::TurnInQuestAndSave(*Actor, HeldRepeat));
        TestTrue(TEXT("Held repeat reports completed and preserves entire candidate without writer"),
            HeldRepeat.Code == Jadebound::ResultCode::QuestAlreadyCompleted && !HeldRepeat.Changed && HeldRepeat.Revision == 4 &&
            SameCore(Completed, Access::CoreState(*Actor)) && SameReflected(CompletedState, Actor->GetProgressionState()) &&
            Access::Held(*Actor) == Held && SameReflected(CompletedState, Held->State) && Held->PendingDrops.IsEmpty() && Trace.Opens == 1);
        if (bRetryBeforeReopen)
        {
            // Candidate/commit fixture only; not elapsed-backoff/scheduler proof.
            Access::ExpireLocalFixtureDeadline(*Actor);
            TestTrue(TEXT("Fixture deadline change preserves the exact reward object"), Access::Held(*Actor) == Held &&
                SameReflected(CompletedState, Held->State) && Held->PendingDrops.IsEmpty());
            Trace.Fault = EFault::None;
            if (!TestTrue(TEXT("Actual held-candidate retry writes, flushes and reads B"), Access::Retry(*Actor))) return false;
            TestTrue(TEXT("Retry commits identical reward bytes without another transaction"), Trace.Payload == FirstPayload && Trace.Opens == 2 &&
                SameCore(Completed, Access::CoreState(*Actor)) && SameReflected(CompletedState, Actor->GetProgressionState()) &&
                Access::Committed(*Actor) && SameReflected(CompletedState, Access::Committed(*Actor)->State) &&
                Access::Committed(*Actor)->PendingDrops.IsEmpty() && !Access::Held(*Actor) && !Access::IsHeld(*Actor) &&
                Actor->GetPersistencePresentation().FailureKind == EJadeboundPersistenceFailureKind::None && Access::Slot(*Actor) == 1);
            TestTrue(TEXT("Reward cache clears only after verified commit"), CacheCleared(*Actor));
        }
        Scope.Retire(*Actor); // Release real lease; direct-reopen branch deliberately never retries.
        auto* Reloaded = Scope.NewActor();
        if (!Reloaded || !TestTrue(TEXT("Second actor reopens actual Completed r4 bank"), Access::Read(*Reloaded))) return false;
        TestTrue(TEXT("Whole reward and consumed parcel history survive real reopen"), SameCore(Completed, Access::CoreState(*Reloaded)) &&
            SameReflected(CompletedState, Reloaded->GetProgressionState()) && Access::Slot(*Reloaded) == 1 &&
            Access::Committed(*Reloaded) && SameReflected(CompletedState, Access::Committed(*Reloaded)->State) &&
            Access::Committed(*Reloaded)->PendingDrops.IsEmpty() && !Access::Held(*Reloaded));
        TArray<uint8> BeforeA, BeforeB;
        if (!TestTrue(TEXT("Reopen leaves both original saved streams unchanged"), ReadOwned(Scope.V3Path(0), BeforeA) && BeforeA == OldA &&
            ReadOwned(Scope.V3Path(1), BeforeB) && BeforeB == FirstPayload)) return false;
        FTrace ReplayTrace; ReplayTrace.ExpectedPath = Scope.V3Path(0);
        Install(*Reloaded, ReplayTrace);
        const auto* ReopenedCommit = Access::Committed(*Reloaded);
        if (!TestTrue(TEXT("Reopened reward commit is available before replay checks"), ReopenedCommit != nullptr)) return false;
        Jadebound::Result Repeated;
        TestFalse(TEXT("Actual repeated turn-in after reopen is refused"), Access::TurnInQuestAndSave(*Reloaded, Repeated));
        TestTrue(TEXT("Completed refusal preserves all progression and cannot write or pay again"),
            Repeated.Code == Jadebound::ResultCode::QuestAlreadyCompleted && !Repeated.Changed && Repeated.Revision == 4 &&
            SameCore(Completed, Access::CoreState(*Reloaded)) && SameReflected(CompletedState, Reloaded->GetProgressionState()) &&
            Access::Committed(*Reloaded) == ReopenedCommit && SameReflected(CompletedState, ReopenedCommit->State) &&
            ReopenedCommit->PendingDrops.IsEmpty() && !Access::Held(*Reloaded) && ReplayTrace.Opens == 0);
        TArray<uint8> AfterA, AfterB;
        TestTrue(TEXT("Repeated reward attempt preserves both actual bank byte streams"), ReadOwned(Scope.V3Path(0), AfterA) && AfterA == BeforeA &&
            ReadOwned(Scope.V3Path(1), AfterB) && AfterB == BeforeB);
    }
    AddInfo(TEXT("Trusted quest transaction/bank regression only; no public interaction, combat, nonempty loot, network, power-loss or elapsed-scheduler acceptance."));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundActorJournalPendingLootTest, "Jadebound.Persistence.ActorJournalPendingLoot",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundActorJournalPendingLootTest::RunTest(const FString& Parameters)
{
    using namespace JadeboundActorBanks;
    using Access = FJadeboundActorBankTestAccess;
    if (!IsInGameThread()) { AddError(TEXT("Actor-bank tests require the game thread.")); return false; }
    FScope Scope(*this, TEXT("nonempty-v3-loot-uncertain-retry-reopen-pickup"));
    if (!Scope.bValid) return false;
    // Whole independent oracles: fixed fresh character, two issuance transactions,
    // two claims, and one quest turn-in. Nothing is copied from awarded output.
    const Jadebound::Snapshot Initial;
    auto Accepted = Initial;
    Accepted.Revision = 1; Accepted.Quest = Jadebound::QuestState::Accepted;
    auto ParcelIssued = Accepted;
    ParcelIssued.Revision = 2; ParcelIssued.Quest = Jadebound::QuestState::RaiderDefeated;
    ParcelIssued.NextDropId = 2; ParcelIssued.QuestKillDropId = 1; ParcelIssued.UnprocessedDropIds = {1};
    auto BothIssued = ParcelIssued;
    BothIssued.Revision = 3; BothIssued.NextDropId = 3; BothIssued.UnprocessedDropIds = {1, 2};
    auto ParcelPicked = BothIssued;
    ParcelPicked.Revision = 4; ParcelPicked.Quest = Jadebound::QuestState::ParcelRecovered;
    ParcelPicked.UnprocessedDropIds = {2}; ParcelPicked.Inventory[0] = {Jadebound::ItemId::JadeParcel, 1};
    auto BothPicked = ParcelPicked;
    BothPicked.Revision = 5; BothPicked.UnprocessedDropIds.clear();
    BothPicked.Inventory[1] = {Jadebound::ItemId::IronOre, 2};
    auto Completed = BothPicked;
    Completed.Revision = 6; Completed.Quest = Jadebound::QuestState::Completed;
    Completed.Coins = 15; Completed.Inventory[0] = {Jadebound::ItemId::JadeShard, 1};
    FJadeboundSavedDrop Parcel, Ore;
    Parcel.DropId = 1; Parcel.ItemId = 5; Parcel.Count = 1; Parcel.Location = FVector(-50.0,400.0,142.15);
    Ore.DropId = 2; Ore.ItemId = 3; Ore.Count = 2; Ore.Location = FVector(170.0,460.0,150.0);
    const TArray<FJadeboundSavedDrop> EmptyDrops, ParcelDrops = {Parcel}, BothDrops = {Parcel, Ore}, OreDrops = {Ore};
    const Jadebound::Snapshot* Oracles[] = {&Initial, &Accepted, &ParcelIssued, &BothIssued, &ParcelPicked, &BothPicked, &Completed};
    const TArray<FJadeboundSavedDrop>* OracleDrops[] = {&EmptyDrops, &EmptyDrops, &ParcelDrops, &BothDrops, &OreDrops, &EmptyDrops, &EmptyDrops};
    FJadeboundProgressionState States[7];
    TArray<uint8> Journals[7];
    for (int32 Stage = 0; Stage < 7; ++Stage)
    {
        auto* Save = Cast<UJadeboundCharacterSave>(UGameplayStatics::CreateSaveGameObject(UJadeboundCharacterSave::StaticClass()));
        if (!Save || !Jadebound::Persistence::EncodeStateV3(*Oracles[Stage], States[Stage]) ||
            !Jadebound::Persistence::ValidateDropsV3(*Oracles[Stage], *OracleDrops[Stage])) return false;
        Save->CharacterId = Scope.Id; Save->State = States[Stage]; Save->PendingDrops = *OracleDrops[Stage];
        if (!Access::EncodeSyntheticFixture(Save, Journals[Stage]) || Journals[Stage].IsEmpty()) return false;
    }
    const auto LivePairMatches = [&](const AJadeboundProgressionActor& Actor, int32 Stage) {
        return SameCore(*Oracles[Stage], Access::CoreState(Actor)) && SameReflected(States[Stage], Actor.GetProgressionState()) &&
            SameDrops(*OracleDrops[Stage], Access::Drops(Actor));
    };
    const auto CommitMatches = [&](const AJadeboundProgressionActor& Actor, int32 Stage) {
        const auto* Save = Access::Committed(Actor);
        return Save && Save->CharacterId == Scope.Id && SameReflected(States[Stage], Save->State) &&
            SameDrops(*OracleDrops[Stage], Save->PendingDrops);
    };
    const auto BankMatches = [&](int32 Bank, int32 Stage) {
        TArray<uint8> ReadBack;
        return ReadOwned(Scope.V3Path(Bank), ReadBack) && ReadBack == Journals[Stage];
    };
    auto* Actor = Scope.NewActor();
    if (!Actor || !TestTrue(TEXT("Fresh actual A is the complete known initial pair"), Access::Read(*Actor) &&
        LivePairMatches(*Actor,0) && CommitMatches(*Actor,0) && Access::Slot(*Actor) == 0 && BankMatches(0,0))) return false;
    if (!TestTrue(TEXT("Quest acceptance saves known complete r1 B"), Access::AcceptQuestAndSave(*Actor) &&
        LivePairMatches(*Actor,1) && CommitMatches(*Actor,1) && BankMatches(1,1) && BankMatches(0,0))) return false;
    Jadebound::Result ParcelResult;
    if (!TestTrue(TEXT("Actual BuildIssue adoption and FinishTransaction save the nonempty parcel r2 A"),
        Access::IssuePendingLootAndSave(*Actor,Parcel.Location,ParcelResult) && ParcelResult.Succeeded() && ParcelResult.Changed &&
        ParcelResult.Revision == 2 && LivePairMatches(*Actor,2) && CommitMatches(*Actor,2) &&
        Access::Slot(*Actor) == 0 && BankMatches(0,2) && BankMatches(1,1))) return false;
    const auto* OldCommitted = Access::Committed(*Actor);
    FTrace Trace; Trace.Fault = EFault::UncertainFlush; Trace.ExpectedPath = Scope.V3Path(1);
    Install(*Actor, Trace);
    Jadebound::Result OreResult;
    TestFalse(TEXT("Real full flush reported uncertain holds the complete two-drop r3 candidate"),
        Access::IssuePendingLootAndSave(*Actor,Ore.Location,OreResult));
    TestTrue(TEXT("Production retry deadline is finite; no future or elapsed timing claim"), FMath::IsFinite(Access::Deadline(*Actor)));
    const auto* Held = Access::Held(*Actor);
    if (!TestTrue(TEXT("Independent complete state and both exact IDs/counts/locations are held"),
        OreResult.Succeeded() && OreResult.Changed && OreResult.Revision == 3 && LivePairMatches(*Actor,3) && Held &&
        Access::IsHeld(*Actor) && Held->CharacterId == Scope.Id && SameReflected(States[3],Held->State) &&
        SameDrops(BothDrops,Held->PendingDrops))) return false;
    TestTrue(TEXT("Both pending drops share the exact first cached B image"), CacheMatches(*Actor, Trace.Payload, Scope.V3Path(1), 1));
    TestTrue(TEXT("Prior whole parcel commit remains selected and both real bank bytes match known oracles"),
        Access::Committed(*Actor) == OldCommitted && CommitMatches(*Actor,2) && Access::Slot(*Actor) == 0 &&
        BankMatches(0,2) && BankMatches(1,3) && Trace.Payload == Journals[3]);
    TestTrue(TEXT("Actual handle writes full bytes, requests Flush(true), closes, and reports only injected uncertainty"),
        Trace.Opens == 1 && Trace.Writes == 1 && Trace.Flushes == 1 && Trace.Closes == 1 && Trace.bFullFlush &&
        Trace.bRealWriteSucceeded && Trace.bRealFlushSucceeded && !Trace.bPathRefused &&
        FString::Join(Trace.Events,TEXT("|")) == TEXT("open|write|flush|close"));
    Jadebound::Result HeldIssue;
    TestFalse(TEXT("Held fixture cannot allocate a second claim"), Access::IssuePendingLootAndSave(*Actor,Ore.Location,HeldIssue));
    TestTrue(TEXT("Held refusal preserves exact object, whole pair, NextDropId and real streams without writer"),
        !HeldIssue.Changed && Access::Held(*Actor) == Held && LivePairMatches(*Actor,3) && SameReflected(States[3],Held->State) &&
        SameDrops(BothDrops,Held->PendingDrops) && Trace.Opens == 1 && BankMatches(0,2) && BankMatches(1,3));
    // Only after complete held/pair/trace checks, control this local fixture guard.
    // Nonempty presentation work may already have consumed the production deadline.
    Access::SetLocalFixtureFutureDeadline(*Actor);
    TestFalse(TEXT("Controlled future fixture deadline refuses held retry without a second open"), Access::Retry(*Actor));
    TestTrue(TEXT("Controlled guard preserves exact held pair, commit, traces and both bank bytes"),
        Access::Held(*Actor) == Held && Access::IsHeld(*Actor) && LivePairMatches(*Actor,3) &&
        Held->CharacterId == Scope.Id && SameReflected(States[3],Held->State) && SameDrops(BothDrops,Held->PendingDrops) &&
        Access::Committed(*Actor) == OldCommitted && CommitMatches(*Actor,2) && Access::Slot(*Actor) == 0 &&
        Trace.Opens == 1 && Trace.Writes == 1 && Trace.Flushes == 1 && Trace.Closes == 1 && Trace.Payload == Journals[3] &&
        FString::Join(Trace.Events,TEXT("|")) == TEXT("open|write|flush|close") && BankMatches(0,2) && BankMatches(1,3));
    // Expire only the same fixture deadline. Candidate/IO behavior follows;
    // this is neither elapsed-backoff nor unaltered production scheduling evidence.
    Access::ExpireLocalFixtureDeadline(*Actor);
    Trace.Fault = EFault::None;
    if (!TestTrue(TEXT("Exact held two-drop retry actually commits and reads back B"), Access::Retry(*Actor))) return false;
    TestTrue(TEXT("Retry reuses exact known r3 bytes and does not issue a third drop"), LivePairMatches(*Actor,3) &&
        CommitMatches(*Actor,3) && Access::Slot(*Actor) == 1 && !Access::Held(*Actor) && !Access::IsHeld(*Actor) &&
        Actor->GetPersistencePresentation().FailureKind == EJadeboundPersistenceFailureKind::None &&
        Trace.Payload == Journals[3] && Trace.Opens == 2 && Trace.Writes == 2 && Trace.Flushes == 2 && Trace.Closes == 2 &&
        Trace.bFullFlush && Trace.bRealWriteSucceeded && Trace.bRealFlushSucceeded && !Trace.bPathRefused &&
        FString::Join(Trace.Events,TEXT("|")) == TEXT("open|write|flush|close|open|write|flush|close") &&
        BankMatches(0,2) && BankMatches(1,3));
    TestTrue(TEXT("Verified nonempty-loot commit clears cached wire and destination"), CacheCleared(*Actor));
    Scope.Retire(*Actor);
    auto* Reloaded = Scope.NewActor();
    if (!Reloaded || !TestTrue(TEXT("A new actor reads both nonempty exact saved records from highest bank B"),
        Access::Read(*Reloaded) && LivePairMatches(*Reloaded,3) && CommitMatches(*Reloaded,3) && Access::Slot(*Reloaded) == 1 &&
        BankMatches(0,2) && BankMatches(1,3))) return false;
    Jadebound::Result ParcelPickup, OrePickup;
    if (!TestTrue(TEXT("Actual parcel domain pickup removes only ID1 and durably saves remaining exact ore record"),
        Access::PickupPendingLootAndSave(*Reloaded,1,ParcelPickup) && ParcelPickup.Succeeded() && ParcelPickup.Changed &&
        ParcelPickup.Revision == 4 && LivePairMatches(*Reloaded,4) && CommitMatches(*Reloaded,4) && Access::Slot(*Reloaded) == 0 &&
        BankMatches(0,4) && BankMatches(1,3))) return false;
    if (!TestTrue(TEXT("Actual ore domain pickup removes ID2 and saves inventory ore x2 with empty outstanding ledger"),
        Access::PickupPendingLootAndSave(*Reloaded,2,OrePickup) && OrePickup.Succeeded() && OrePickup.Changed &&
        OrePickup.Revision == 5 && LivePairMatches(*Reloaded,5) && CommitMatches(*Reloaded,5) && Access::Slot(*Reloaded) == 1 &&
        BankMatches(0,4) && BankMatches(1,5))) return false;
    Jadebound::Result Reward;
    if (!TestTrue(TEXT("One real quest turn-in saves only five coins and one shard while ore and issued prefix remain"),
        Access::TurnInQuestAndSave(*Reloaded,Reward) && Reward.Succeeded() && Reward.Changed && Reward.Revision == 6 &&
        LivePairMatches(*Reloaded,6) && CommitMatches(*Reloaded,6) && Access::Slot(*Reloaded) == 0 &&
        BankMatches(0,6) && BankMatches(1,5))) return false;
    Scope.Retire(*Reloaded);
    auto* Final = Scope.NewActor();
    if (!Final || !TestTrue(TEXT("Third actor reopens durable pickups/reward with no pending records and next ID3"),
        Access::Read(*Final) && LivePairMatches(*Final,6) && CommitMatches(*Final,6) && Access::Slot(*Final) == 0 &&
        BankMatches(0,6) && BankMatches(1,5))) return false;
    const auto* FinalCommitted = Access::Committed(*Final);
    FTrace RepeatTrace; RepeatTrace.ExpectedPath = Scope.V3Path(1);
    Install(*Final, RepeatTrace);
    Jadebound::Result RepeatedParcel, RepeatedOre, RepeatedReward;
    TestFalse(TEXT("Actual domain rejects already-consumed quest claim after reopen"), Access::ReplayConsumedClaimAndSave(*Final,1,true,RepeatedParcel));
    TestFalse(TEXT("Actual domain rejects already-consumed ore claim after reopen"), Access::ReplayConsumedClaimAndSave(*Final,2,false,RepeatedOre));
    TestFalse(TEXT("Actual turn-in cannot pay the completed quest again"), Access::TurnInQuestAndSave(*Final,RepeatedReward));
    TestTrue(TEXT("Duplicate claims/reward preserve full state, drop arrays, exact committed object and both full streams with no open"),
        RepeatedParcel.Code == Jadebound::ResultCode::DropAlreadyProcessed && !RepeatedParcel.Changed && RepeatedParcel.Revision == 6 &&
        RepeatedOre.Code == Jadebound::ResultCode::DropAlreadyProcessed && !RepeatedOre.Changed && RepeatedOre.Revision == 6 &&
        RepeatedReward.Code == Jadebound::ResultCode::QuestAlreadyCompleted && !RepeatedReward.Changed && RepeatedReward.Revision == 6 &&
        LivePairMatches(*Final,6) && CommitMatches(*Final,6) && Access::Committed(*Final) == FinalCommitted &&
        !Access::Held(*Final) && RepeatTrace.Opens == 0 && BankMatches(0,6) && BankMatches(1,5));
    TestFalse(TEXT("No legacy A created in this fresh v3 fixture"), FPlatformFileManager::Get().GetPlatformFile().FileExists(*Scope.V1Path(0)));
    TestFalse(TEXT("No legacy B created in this fresh v3 fixture"), FPlatformFileManager::Get().GetPlatformFile().FileExists(*Scope.V1Path(1)));
    TestTrue(TEXT("Fresh current progression never creates either old-format pair"), Scope.OlderPairsAbsent());
    AddInfo(TEXT("Synthetic nonempty v3 actor-bank pair/real IO regression only; no native enemy/death, final-hit ownership, public pickup/range, animation/visual, game restart, network, power-loss or elapsed-scheduler acceptance."));
    return true;
}
// GuardedV2 registrations remain in the preserved supported-old build.
// Current V3/V4 admission is covered by the separate Source148 tests.

#endif
