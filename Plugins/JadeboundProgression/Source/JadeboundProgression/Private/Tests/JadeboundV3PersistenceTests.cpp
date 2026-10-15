#include "JadeboundProgressionActor.h"
#if WITH_DEV_AUTOMATION_TESTS && PLATFORM_WINDOWS
#include "Tests/Source148V3PersistenceFixture.h"

using namespace JadeboundSource148;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundV3NamespaceAdmissionTest,"Jadebound.Persistence.V3NamespaceAdmission",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundV3NamespaceAdmissionTest::RunTest(const FString&)
{
    // Actual actor paths, synthetic owned journals. No existing profile is used.
    const TCHAR* Cases[]={TEXT("V3-empty"),TEXT("V3-truncated"),TEXT("V3-directory"),TEXT("V2-directory-over-V1"),
        TEXT("V4-empty"),TEXT("V4-directory"),TEXT("future-in-V1"),TEXT("future-in-V2"),TEXT("future-in-V3"),
        TEXT("V1-high-unsigned"),TEXT("V2-high-signed"),TEXT("V1-raw-same-revision-conflict")};
    for(int32 Case=0;Case<UE_ARRAY_COUNT(Cases);++Case)
    {
        Scope S(*this,Cases[Case]); if(!S.Valid) return false;
        auto& F=FPlatformFileManager::Get().GetPlatformFile();
        if(Case<=5)
        {
            if(!S.Seed(1,0,Fixture(S.Id,1))) return false;
            if(Case!=3 && !S.Seed(2,0,Fixture(S.Id,2))) return false;
            if(Case<=2)
            {
                if(Case==2) { if(!F.CreateDirectory(*S.Paths.Banks[2][0])) return false; }
                else if(!Put(S.Paths.Banks[2][0],Case==0 ? TArray<uint8>{} : TArray<uint8>{0x4a,0x42,0x50})) return false;
            }
            else if(Case==3) { if(!F.CreateDirectory(*S.Paths.Banks[1][0])) return false; }
            else
            {
                if(!S.Seed(3,0,Fixture(S.Id,3,20,true))) return false;
                if(Case==4) { if(!Put(S.Paths.ForwardV4[1],{})) return false; }
                else if(!F.CreateDirectory(*S.Paths.ForwardV4[1])) return false;
            }
        }
        else if(Case<=8)
        {
            const int32 Version=Case-5;
            auto* Future=Fixture(S.Id,Version); if(!Future) return false;
            Future->State.Version=Version+1;
            if(!S.Seed(Version,0,Fixture(S.Id,Version)) || !S.Seed(Version,1,Future)) return false;
        }
        else if(Case==9)
        {
            if(!S.Seed(1,0,Fixture(S.Id,1,20)) || !S.Seed(1,1,Fixture(S.Id,1,std::numeric_limits<uint64>::max()))) return false;
        }
        else if(Case==10)
        {
            if(!S.Seed(2,0,Fixture(S.Id,2,20)) || !S.Seed(2,1,Fixture(S.Id,2,MaximumPersistedCounter))) return false;
        }
        else
        {
            auto* A=Fixture(S.Id,1); auto* B=Fixture(S.Id,1); if(!A || !B) return false;
            B->State.ProcessedDropIds={1,4}; // Same owned semantics, different original valid bytes.
            if(!S.Seed(1,0,A) || !S.Seed(1,1,B)) return false;
        }
        auto* A=S.Actor(); if(!A) return false;
        TArray<Image> Before,After; if(!Capture(S.AllPaths(),Before)) return false;
        int32 Opens=0; Access::Opener(*A,[&Opens](const FString&)->IFileHandle* { ++Opens; return nullptr; });
        TestTrue(TEXT("Refuse before current state admission or migration write"),!Access::Read(*A) && !Access::Admitted(*A) &&
            !Access::Held(*A) && !Access::Committed(*A) && Same(Access::Core(*A),Snapshot{}) &&
            A->GetPersistencePresentation().FailureKind==EJadeboundPersistenceFailureKind::RecoveryRequired);
        TestTrue(TEXT("Recovery blocks save/reload, preserves all eight entries and excludes another writer"),
            !A->SaveProgress() && !A->ReloadProgress() && Opens==0 && S.LeaseExcludes(*A) &&
            Capture(S.AllPaths(),After) && SameImages(Before,After));
        Access::Opener(*A,nullptr);
    }
    Scope Fresh(*this,TEXT("fresh-and-unadmitted")); if(!Fresh.Valid) return false;
    auto* A=Fresh.Actor(); if(!A) return false;
    int32 Opens=0; Access::Opener(*A,[&Opens](const FString&)->IFileHandle* { ++Opens; return nullptr; });
    TestTrue(TEXT("WriteSave cannot open any bank before V3 namespace admission"),!Access::Write(*A) && Opens==0 && !Access::Held(*A));
    Access::End(*A); A=Fresh.Actor(); if(!A) return false;
    Trace T; T.Path=Fresh.Paths.Banks[2][0]; Install(*A,T);
    TestTrue(TEXT("No entries creates only V3 A under the shared lease"),Access::Read(*A) && Access::Admitted(*A) && T.Opens==1 &&
        !T.PathRefused && T.RealWrite && T.RealFlush && T.Full && T.Closes==1 && Access::Committed(*A) && Access::Committed(*A)->State.Version==3);
    auto& Files=FPlatformFileManager::Get().GetPlatformFile();
    TestTrue(TEXT("Fresh startup leaves V1/V2 pairs absent"),!HasJournalEntry(Files,Fresh.Paths.Banks[0][0]) &&
        !HasJournalEntry(Files,Fresh.Paths.Banks[0][1]) && !HasJournalEntry(Files,Fresh.Paths.Banks[1][0]) && !HasJournalEntry(Files,Fresh.Paths.Banks[1][1]));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundV3MigrationReadbackTest,"Jadebound.Persistence.V3MigrationReadback",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundV3MigrationReadbackTest::RunTest(const FString&)
{
    for(const int32 Version:{1,2})
    {
        Scope S(*this,Version==1 ? TEXT("V1-direct-V3") : TEXT("V2-direct-V3")); if(!S.Valid) return false;
        auto* Best=Fixture(S.Id,Version,21);
        if(!Best || !S.Seed(Version,0,Fixture(S.Id,Version,20)) || !S.Seed(Version,1,Best)) return false;
        TArray<FString> Older; for(int32 V=0;V<2;++V) for(int32 B=0;B<2;++B) Older.Add(S.Paths.Banks[V][B]);
        TArray<Image> Before,After; if(!Capture(Older,Before)) return false;
        Snapshot Expected;
        if(!Source145BuildLegacyV1TorsoMigrationCandidate(Legacy(21),Expected)) return false;
        auto* A=S.Actor(); if(!A) return false;
        Trace T; T.Path=S.Paths.Banks[2][0]; Install(*A,T);
        if(!TestTrue(TEXT("Highest original converts once and durably reads back V3"),Access::Read(*A) && T.Opens==1 &&
            !T.PathRefused && T.Full && T.RealFlush && T.Closes==1 && T.UnreadyAtEveryOpen && Access::Admitted(*A) && !Access::Held(*A) &&
            Access::Committed(*A) && Access::Committed(*A)->State.Version==3 && Same(Access::Core(*A),Expected) &&
            Expected.Revision==22 && Expected.EquippedTorso==ItemId::None && SameDrops(Access::Drops(*A),OriginalDrops()) &&
            Capture(Older,After) && SameImages(Before,After) && S.LeaseExcludes(*A))) return false;
        const auto Committed=Access::Committed(*A)->State;
        Access::End(*A); A=S.Actor(); if(!A) return false;
        int32 ReopenWrites=0; Access::Opener(*A,[&ReopenWrites](const FString&)->IFileHandle* { ++ReopenWrites; return nullptr; });
        TestTrue(TEXT("Independent actor reopen selects V3 without another increment or write"),Access::Read(*A) &&
            Same(Access::Core(*A),Expected) && SameReflected(A->GetProgressionState(),Committed) && ReopenWrites==0 &&
            SameDrops(Access::Drops(*A),OriginalDrops()) && Capture(Older,After) && SameImages(Before,After));
        Access::Opener(*A,nullptr);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundV3HeldRetryTest,"Jadebound.Persistence.V3HeldRetry",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundV3HeldRetryTest::RunTest(const FString&)
{
    for(const auto Mode:{Fault::Open,Fault::Partial,Fault::UncertainFlush,Fault::CorruptAfterClose,Fault::DifferentAfterClose})
    {
        Scope S(*this,TEXT("V3-torso-held-retry")); if(!S.Valid || !S.Seed(3,0,Fixture(S.Id,3,20,true))) return false;
        auto* A=S.Actor(); if(!A || !Access::Read(*A)) return false;
        const auto Original=Access::Core(*A); const auto* OldCommitted=Access::Committed(*A);
        TArray<uint8> OldBytes; if(!ReadBytes(S.Paths.Banks[2][0],OldBytes)) return false;
        Trace T; T.Mode=Mode; T.Path=S.Paths.Banks[2][1]; T.Substitute=OldBytes; Install(*A,T);
        if(!TestTrue(TEXT("Injected OS-facing failure retains changed candidate"),!Access::BuyLeaf(*A) && Access::Held(*A) &&
            A->GetPersistencePresentation().FailureKind==EJadeboundPersistenceFailureKind::Retryable)) return false;
        const auto* Held=Access::Held(*A); const auto HeldState=Held->State; const auto HeldDrops=Held->PendingDrops;
        const auto Changed=Access::Core(*A); const TArray<uint8> ExpectedBytes=Access::HeldBytes(*A);
        const TArray<uint8> FirstAttemptBuffer=T.Payload;
        const FString FirstTarget=Access::HeldPath(*A); const int32 FirstSlot=Access::HeldSlot(*A);
        if(!TestTrue(TEXT("First encoding and destination were frozen before even a failing open"),!ExpectedBytes.IsEmpty() &&
            FirstTarget==S.Paths.Banks[2][1] && FirstSlot==1)) return false;
        TestTrue(TEXT("One mutation preserves torso and freezes the original witness and slot"),Changed.Revision==Original.Revision+1 &&
            Changed.Coins==Original.Coins-2 && Changed.EquippedTorso==ItemId::WardenCoat && Changed.EquippedWeapon==Original.EquippedWeapon &&
            Access::Committed(*A)==OldCommitted && Access::Slot(*A)==0 && SameDrops(HeldDrops,OriginalDrops()) &&
            S.LeaseExcludes(*A) && T.MutationOk && !T.PathRefused);
        TestTrue(TEXT("Production deadline blocks immediate retry, reload and second fixture mutation"),Access::Deadline(*A)>FPlatformTime::Seconds() &&
            !Access::Retry(*A) && !Access::Read(*A,false) && !Access::BuyLeaf(*A) && T.Opens==1 && Access::Held(*A)==Held &&
            SameReflected(Held->State,HeldState) && SameDrops(Held->PendingDrops,HeldDrops) &&
            Access::HeldBytes(*A)==ExpectedBytes && Access::HeldPath(*A)==FirstTarget && Access::HeldSlot(*A)==FirstSlot);
        if(Mode!=Fault::Open) TestTrue(TEXT("Failed write received the whole exact candidate"),T.Payload==ExpectedBytes && T.Closes==1);
        // Local fixture advances only the deadline; production retry logic stays intact.
        T.Mode=Fault::None; T.Events.Reset(); Access::Expire(*A);
        if(!TestTrue(TEXT("Retry writes identical bytes to the same bank with no second mutation"),Access::Retry(*A) && T.Opens==2 &&
            T.Payload==ExpectedBytes && (Mode==Fault::Open || T.Payload==FirstAttemptBuffer) &&
            T.Events==TArray<FString>{TEXT("open"),TEXT("write"),TEXT("flush"),TEXT("close")} &&
            T.Full && T.RealWrite && T.RealFlush && !Access::Held(*A) && Access::Slot(*A)==1 &&
            Access::HeldBytes(*A).IsEmpty() && Access::HeldPath(*A).IsEmpty() && Access::HeldSlot(*A)==INDEX_NONE &&
            Same(Access::Core(*A),Changed) && SameReflected(Access::Committed(*A)->State,HeldState) &&
            SameDrops(Access::Committed(*A)->PendingDrops,HeldDrops))) return false;
        TArray<uint8> Actual,StillOld;
        TestTrue(TEXT("Full physical retry payload and previous bank remain exact"),ReadBytes(S.Paths.Banks[2][1],Actual) && Actual==ExpectedBytes &&
            ReadBytes(S.Paths.Banks[2][0],StillOld) && StillOld==OldBytes);
        Access::End(*A); A=S.Actor(); if(!A) return false;
        TestTrue(TEXT("Separate actor reopen retains equipped torso and committed purchase once"),Access::Read(*A) && Same(Access::Core(*A),Changed) &&
            SameDrops(Access::Drops(*A),HeldDrops));
    }
    // First migrated bank: partial bytes forbid old-format fallback; a fully
    // written uncertain flush can reopen from V3 without replaying migration.
    for(const auto Mode:{Fault::Partial,Fault::UncertainFlush})
    {
        Scope S(*this,TEXT("migration-uncertain-reopen")); if(!S.Valid || !S.Seed(2,0,Fixture(S.Id,2,21))) return false;
        TArray<uint8> Old,After; if(!ReadBytes(S.Paths.Banks[1][0],Old)) return false;
        auto* A=S.Actor(); if(!A) return false;
        Trace T; T.Mode=Mode; T.Path=S.Paths.Banks[2][0]; Install(*A,T);
        if(!TestTrue(TEXT("Migration failure holds exactly r22 and original V2 witness unready"),!Access::Read(*A) && Access::Held(*A) &&
            Access::Unready(*A) && Access::Core(*A).Revision==22 && Access::Committed(*A) && Access::Committed(*A)->State.Version==2 &&
            Access::Committed(*A)->State.ExactRevision==21 && S.LeaseExcludes(*A))) return false;
        Access::End(*A); A=S.Actor(); if(!A) return false;
        int32 Writes=0; Access::Opener(*A,[&Writes](const FString&)->IFileHandle* { ++Writes; return nullptr; });
        const bool Read=Access::Read(*A);
        if(Mode==Fault::Partial) TestTrue(TEXT("Partial first V3 bank blocks original V2 on reopen"),!Read && !Access::Admitted(*A) && !Access::Committed(*A));
        else TestTrue(TEXT("Complete uncertain V3 is recovered without a second representation increment"),Read && Access::Core(*A).Revision==22 &&
            Access::Committed(*A) && Access::Committed(*A)->State.Version==3);
        TestTrue(TEXT("Neither restart path overwrites the old source or makes a new write"),Writes==0 && ReadBytes(S.Paths.Banks[1][0],After) && Old==After);
        Access::Opener(*A,nullptr);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundV3RecipientTorsoTest,"Jadebound.Persistence.V3RecipientTorso",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundV3RecipientTorsoTest::RunTest(const FString&)
{
    Snapshot Before;
    if(!Source145ConvertLegacyV1ToTorsoSnapshot(Legacy(),Before).Succeeded()) return false;
    Before.EquippedTorso=ItemId::WardenCoat;
    const auto Drops=OriginalDrops(); Recipient::IssueCandidate Issued;
    if(!TestTrue(TEXT("Recipient issues one current V3 claim while retaining torso and weapon identities"),
        Recipient::BuildIssue(Before,Drops,FVector(150,350,50),Issued).Succeeded() && Issued.State.Version==3 &&
        Issued.State.Revision==Before.Revision+1 && Issued.State.NextDropId==Before.NextDropId+1 &&
        Issued.State.EquippedTorso==ItemId::WardenCoat && Issued.State.EquippedWeapon==Before.EquippedWeapon &&
        Issued.State.Coins==Before.Coins && Issued.DropId==5 && Issued.Drops.Num()==3 &&
        Recipient::HasCommittedIssue(Issued.State,Issued.Drops,Issued.DropId,Issued.State.Revision))) return false;
    FJadeboundProgressionState Encoded; Snapshot Decoded;
    if(!TestTrue(TEXT("Entire issued V3 pair roundtrips through current codec"),EncodeStateV3(Issued.State,Encoded) &&
        DecodeStateV3(Encoded,Decoded) && Same(Decoded,Issued.State) && ValidateDropsV3(Decoded,Issued.Drops))) return false;
    ProgressionCore Collected;
    if(!Collected.LoadSnapshot(Decoded).Succeeded() || !Collected.GrantLoot(ItemId::IronOre,2,5).Succeeded()) return false;
    auto Remaining=Issued.Drops; Remaining.RemoveAll([](const FJadeboundSavedDrop& D) { return D.DropId==5; });
    const auto After=Collected.GetSnapshot();
    TestTrue(TEXT("Issued-prefix witness remains after pickup without losing torso or permitting duplicate claim"),
        Recipient::HasCommittedIssue(After,Remaining,5,Issued.State.Revision) && After.EquippedTorso==ItemId::WardenCoat &&
        !Collected.GrantLoot(ItemId::IronOre,2,5).Succeeded() && Same(After,Collected.GetSnapshot()));
    TestFalse(TEXT("A future minimum revision cannot fabricate commit acknowledgment"),
        Recipient::HasCommittedIssue(After,Remaining,5,After.Revision+1));
    return true;
}
#endif
