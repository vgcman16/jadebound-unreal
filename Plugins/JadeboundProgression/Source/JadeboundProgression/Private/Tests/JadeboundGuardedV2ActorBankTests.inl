// Included once by JadeboundActorBankTests.cpp inside WITH_DEV_AUTOMATION_TESTS.
// Reuses that translation unit's existing, explicitly isolated native fixture.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundGuardedV2ForwardPathsTest, "Jadebound.Persistence.GuardedV2ForwardPaths",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundGuardedV2ForwardPathsTest::RunTest(const FString&)
{
    FString V2, V3;
    const FString Id(TEXT("27b0cbce01df4b6b8f9cf927acaecf11"));
    for (const bool bDevelopment : {false, true})
        for (int32 Slot = 0; Slot < 2; ++Slot)
        {
            const FString Character = bDevelopment ? Id : FString(TEXT("Traveler01"));
            if (!TestTrue(TEXT("Guard path validates exact current identity and shares its directory"),
                Jadebound::Persistence::TryJournalPath(bDevelopment, Character, Slot, true, V2) &&
                Jadebound::Persistence::TryForwardV3JournalPath(bDevelopment, Character, Slot, V3) &&
                FPaths::IsSamePath(FPaths::GetPath(V2), FPaths::GetPath(V3)))) return false;
            const FString Expected = bDevelopment
                ? (Slot == 0 ? TEXT("Progression_V3_A.sav") : TEXT("Progression_V3_B.sav"))
                : (Slot == 0 ? TEXT("Jadebound_Traveler01_Progression_V3_A.sav") : TEXT("Jadebound_Traveler01_Progression_V3_B.sav"));
            TestEqual(TEXT("Only the exact canonical forward filename is queried"), FPaths::GetCleanFilename(V3), Expected);
        }
    V3 = TEXT("stale");
    TestTrue(TEXT("Invalid standalone character clears output and refuses"),
        !Jadebound::Persistence::TryForwardV3JournalPath(false, Id, 0, V3) && V3.IsEmpty());
    V3 = TEXT("stale");
    TestTrue(TEXT("Invalid development character clears output and refuses"),
        !Jadebound::Persistence::TryForwardV3JournalPath(true, TEXT("../Traveler01"), 0, V3) && V3.IsEmpty());
    for (const int32 Slot : {-1, 2})
    {
        V3 = TEXT("stale");
        TestTrue(TEXT("Invalid slot clears output and refuses"),
            !Jadebound::Persistence::TryForwardV3JournalPath(true, Id, Slot, V3) && V3.IsEmpty());
    }
    AddInfo(TEXT("Pure canonical guard path queries only; no V3 codec or migration."));
    return true;
}

#if PLATFORM_WINDOWS
namespace JadeboundForwardAdmission
{
struct FEntryImage
{
    bool bFile = false, bDirectory = false, bStatValid = false;
    int64 Size = -1;
    TArray<uint8> Bytes;
};
bool Capture(const TArray<FString>& Paths, TArray<FEntryImage>& Out)
{
    Out.Reset();
    IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
    for (const FString& Path : Paths)
    {
        FEntryImage Image;
        Image.bFile = Files.FileExists(*Path);
        Image.bDirectory = Files.DirectoryExists(*Path);
        const FFileStatData Stat = Files.GetStatData(*Path);
        Image.bStatValid = Stat.bIsValid;
        Image.Size = Stat.bIsValid ? Stat.FileSize : -1;
        if (Image.bFile)
        {
            const int64 Size = Files.FileSize(*Path);
            if (Image.bDirectory || Size < 0 || Size > 1024 * 1024 + 32) return false;
            Image.Bytes.SetNumUninitialized(static_cast<int32>(Size));
            if (Size > 0)
            {
                TUniquePtr<IFileHandle> Reader(Files.OpenRead(*Path));
                if (!Reader || !Reader->Read(Image.Bytes.GetData(), Size)) return false;
            }
            // Exact empty-byte image needs no read handle. The real empty writer
            // companion may be exclusively open; preserve that lease unchanged.
        }
        Out.Add(MoveTemp(Image));
    }
    return true;
}
bool Same(const TArray<FEntryImage>& A, const TArray<FEntryImage>& B)
{
    if (A.Num() != B.Num()) return false;
    for (int32 I = 0; I < A.Num(); ++I)
        if (A[I].bFile != B[I].bFile || A[I].bDirectory != B[I].bDirectory ||
            A[I].bStatValid != B[I].bStatValid || A[I].Size != B[I].Size || A[I].Bytes != B[I].Bytes) return false;
    return true;
}
bool PutEmpty(const FString& Path)
{
    TUniquePtr<IFileHandle> Handle(FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*Path, false, false));
    const bool Ok = Handle && Handle->Flush(true);
    Handle.Reset();
    return Ok && FPlatformFileManager::Get().GetPlatformFile().FileExists(*Path) &&
        FPlatformFileManager::Get().GetPlatformFile().FileSize(*Path) == 0;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundGuardedV2ForwardPhysicalTest, "Jadebound.Persistence.GuardedV2ForwardPhysical",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundGuardedV2ForwardPhysicalTest::RunTest(const FString&)
{
    using namespace JadeboundActorBanks;
    using Access = FJadeboundActorBankTestAccess;
    if (!FParse::Param(FCommandLine::Get(), TEXT("JadeboundGuardedV2AdmissionTests")))
    { AddError(TEXT("Explicit Source118 isolated-profile opt-in required.")); return false; }
    // Every case gets a fresh physical GUID directory. No entry is removed.
    const TCHAR* Cases[] = {TEXT("ordinary-v2"), TEXT("zero-a"), TEXT("zero-b"), TEXT("malformed-a"),
        TEXT("malformed-b"), TEXT("directory-a"), TEXT("directory-b"), TEXT("synthetic-future-a"),
        TEXT("synthetic-future-b"), TEXT("both-files")};
    for (int32 Case = 0; Case < UE_ARRAY_COUNT(Cases); ++Case)
    {
        FScope Scope(*this, Cases[Case]);
        if (!Scope.bValid) return false;
        auto* Fixture = Scope.NewActor();
        if (!Fixture || !Access::Read(*Fixture) || !Access::BuyMoonleafOnce(*Fixture)) return false;
        const auto Expected = Access::CoreState(*Fixture); // r1, 8 coins, one real-domain Moonleaf.
        TArray<uint8> Legacy, Future;
        if (!MakeLegacyFixture(Scope, Fixture->GetProgressionState(), Legacy) ||
            !Scope.Put(0, 0, Legacy) || !Scope.Put(0, 1, Legacy)) return false;
        auto* Synthetic = Cast<UJadeboundCharacterSave>(UGameplayStatics::CreateSaveGameObject(UJadeboundCharacterSave::StaticClass()));
        if (!Synthetic) return false;
        Synthetic->CharacterId = Scope.Id;
        Synthetic->State = Fixture->GetProgressionState();
        Synthetic->State.Version = 3; // Old reflected class, synthetic future envelope only.
        if (!Access::EncodeSyntheticFixture(Synthetic, Future)) return false;
        Scope.Retire(*Fixture);
        FString V3[2];
        for (int32 Bank = 0; Bank < 2; ++Bank)
            if (!Jadebound::Persistence::TryForwardV3JournalPath(true, Scope.Id, Bank, V3[Bank]) ||
                !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(V3[Bank]), Scope.Directory)) return false;
        IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
        const int32 Bank = (Case == 2 || Case == 4 || Case == 6 || Case == 8) ? 1 : 0;
        const TArray<uint8> Malformed = {0x4a, 0x42, 0x00};
        const TArray<uint8> Sentinel = {0x53, 0x31, 0x31, 0x38, 0x4b, 0x45, 0x45, 0x50};
        if (Case == 1 || Case == 2)
        { if (!JadeboundForwardAdmission::PutEmpty(V3[Bank])) return false; }
        else if (Case == 3 || Case == 4)
        { if (!WriteOwned(V3[Bank], Malformed)) return false; }
        else if (Case == 5 || Case == 6)
        {
            if (!Files.CreateDirectory(*V3[Bank]) ||
                !WriteOwned(FPaths::Combine(V3[Bank], TEXT("keep-evidence.bin")), Sentinel)) return false;
        }
        else if (Case == 7 || Case == 8)
        { if (!WriteOwned(V3[Bank], Future)) return false; }
        else if (Case == 9)
        { if (!JadeboundForwardAdmission::PutEmpty(V3[0]) || !WriteOwned(V3[1], Malformed)) return false; }
        TArray<FString> Paths;
        for (int32 Version = 0; Version < 2; ++Version)
            for (int32 Slot = 0; Slot < 2; ++Slot) Paths.Add(Scope.Paths[Version][Slot]);
        Paths.Add(V3[0]); Paths.Add(V3[1]); Paths.Add(FPaths::Combine(Scope.Directory, TEXT("Character.writer")));
        Paths.Add(FPaths::Combine(V3[0], TEXT("keep-evidence.bin")));
        Paths.Add(FPaths::Combine(V3[1], TEXT("keep-evidence.bin")));
        TArray<JadeboundForwardAdmission::FEntryImage> Before, After;
        if (!JadeboundForwardAdmission::Capture(Paths, Before)) return false;
        for (int32 Reopen = 0; Reopen < 2; ++Reopen)
        {
            auto* Reopened = Scope.NewActor();
            if (!Reopened) return false;
            const IFileHandle* Lease = Access::ForwardGuardLease(*Reopened);
            int32 Opens = 0;
            Access::SetOpener(*Reopened, [&Opens](const FString&) -> IFileHandle* { ++Opens; return nullptr; });
            const bool Read = Access::Read(*Reopened);
            if (Case == 0)
            {
                if (!TestTrue(TEXT("Absent forward entries preserve ordinary whole V2 read and both original banks"),
                    Read && SameCore(Expected, Access::CoreState(*Reopened)) && Access::Committed(*Reopened))) return false;
            }
            else
            {
                if (!TestTrue(TEXT("Any forward entry refuses before an old state is loaded or committed"),
                    !Read && !Reopened->IsProgressionReady() &&
                    Reopened->GetPersistencePresentation().FailureKind == EJadeboundPersistenceFailureKind::RecoveryRequired &&
                    !Access::Committed(*Reopened) && !Access::Held(*Reopened) &&
                    SameCore(Jadebound::Snapshot{}, Access::CoreState(*Reopened)))) return false;
                TestFalse(TEXT("Recovery refusal cannot manually save old history"), Reopened->SaveProgress());
                TestFalse(TEXT("Recovery refusal cannot reload old history"), Reopened->ReloadProgress());
            }
            {
                TUniquePtr<IFileHandle> Competitor(Files.OpenWrite(
                    *FPaths::Combine(Scope.Directory, TEXT("Character.writer")), true, false));
                if (!TestTrue(TEXT("Actual Windows companion lease still excludes a second writer"),
                    Lease && Lease == Access::ForwardGuardLease(*Reopened) && !Competitor)) return false;
            }
            if (!TestTrue(TEXT("No bank writer; six bank entries, two sentinels and writer bytes preserved"),
                Opens == 0 && JadeboundForwardAdmission::Capture(Paths, After) &&
                JadeboundForwardAdmission::Same(Before, After))) return false;
            Access::SetOpener(*Reopened, nullptr);
            Scope.Retire(*Reopened); // Next iteration proves admission after legitimate lease release.
        }
        AddInfo(FString::Printf(TEXT("Source118 physical case retained: %s, %s"), Cases[Case], *Scope.Directory));
    }
    AddInfo(TEXT("Physical actor ReadSave/Windows lease only. Synthetic future envelopes are not V3 codecs, migration, gameplay, process restart, unmodified old build or MMO proof."));
    return true;
}
#endif // PLATFORM_WINDOWS
