// Included once by JadeboundStandaloneWriterTests.cpp within its existing
// WITH_DEV_AUTOMATION_TESTS && PLATFORM_WINDOWS conditional and fixture scope.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundGuardedV2StandaloneStartupTest,
    "Jadebound.Persistence.GuardedV2StandaloneStartup", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FJadeboundGuardedV2StandaloneStartupTest::RunTest(const FString&)
{
    using namespace JadeboundStandaloneWriter;
    if (!FParse::Param(FCommandLine::Get(), TEXT("JadeboundGuardedV2AdmissionTests")))
    { AddError(TEXT("Explicit Source118 opt-in and a fresh UserDir are required.")); return false; }
    FScope Scope(*this);
    if (!Scope.bValid) return false;
    FEntry* Original = Scope.Create();
    if (!Original || !Scope.CleanState(*Original, Jadebound::Snapshot{}, 0)) return false;
    auto Expected = Jadebound::Snapshot{};
    Expected.Revision = 1; Expected.Coins = 8; Expected.Inventory[0] = {Jadebound::ItemId::Moonleaf, 1};
    if (!BuyAndAdvance(*this, Scope, *Original, Jadebound::ItemId::Moonleaf, 0, Expected, 1)) return false;
    FBanks Before;
    if (!Scope.Banks(Before) || !Scope.DestroyModel(*Original) || !Scope.CompanionReleased()) return false;
    FString V3[2];
    for (int32 Bank = 0; Bank < 2; ++Bank)
        if (!Jadebound::Persistence::TryForwardV3JournalPath(false, TEXT("Traveler01"), Bank, V3[Bank]) ||
            !FPaths::IsSamePath(FPaths::GetPath(V3[Bank]), FPaths::GetPath(Scope.Paths[2]))) return false;
    IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
    if (Files.FileExists(*V3[0]) || Files.DirectoryExists(*V3[0]) ||
        Files.FileExists(*V3[1]) || Files.DirectoryExists(*V3[1]) || !Files.CreateDirectory(*V3[0])) return false;
    const FString SentinelPath = FPaths::Combine(V3[0], TEXT("keep-evidence.bin"));
    const TArray<uint8> Sentinel = {0x53, 0x31, 0x31, 0x38, 0x4b, 0x45, 0x45, 0x50};
    {
        TUniquePtr<IFileHandle> Handle(Files.OpenWrite(*SentinelPath, false, false));
        if (!Handle || !Handle->Write(Sentinel.GetData(), Sentinel.Num()) || !Handle->Flush(true)) return false;
    }
    {
        TUniquePtr<IFileHandle> Handle(Files.OpenWrite(*V3[1], false, false));
        if (!Handle || !Handle->Flush(true)) return false;
    }
    const auto ForwardUnchanged = [&]() -> bool
    {
        if (!Files.DirectoryExists(*V3[0]) || Files.FileExists(*V3[0]) ||
            !Files.FileExists(*V3[1]) || Files.DirectoryExists(*V3[1]) || Files.FileSize(*V3[1]) != 0 ||
            Files.FileSize(*SentinelPath) != Sentinel.Num()) return false;
        TUniquePtr<IFileHandle> Reader(Files.OpenRead(*SentinelPath));
        TArray<uint8> Bytes; Bytes.SetNumUninitialized(Sentinel.Num());
        return Reader && Reader->Read(Bytes.GetData(), Bytes.Num()) && Bytes == Sentinel && Scope.BanksUnchanged(Before);
    };
    for (int32 Reopen = 0; Reopen < 2; ++Reopen)
    {
        FEntry* Guarded = Scope.Create(); // Production BeginPlay acquires then calls guarded ReadSave.
        FImage Image;
        if (!Guarded || !Scope.Capture(*Guarded, Image) ||
            !TestTrue(TEXT("Production BeginPlay refuses with original lease retained and no old state adoption"),
                Guarded->Model->HasActorBegunPlay() && !Guarded->Model->IsProgressionReady() &&
                !Image.bReady && !Image.bInitialized && Image.bBlocked && !Image.bUnsaved &&
                Image.Failure == EJadeboundPersistenceFailureKind::RecoveryRequired &&
                Image.Lease && !Image.DevelopmentLease && !Image.Saved && !Image.Held &&
                SameCore(Image.Core, Jadebound::Snapshot{}) && Guarded->Opens == 0 && ForwardUnchanged())) return false;
        FEntry* Competing = Scope.Create();
        if (!Competing || !TestTrue(TEXT("Second production startup still excluded by real writer ownership"),
            Scope.Rejected(*Competing) && Scope.Unchanged(*Guarded, Image) && ForwardUnchanged())) return false;
        TestFalse(TEXT("Guarded actor cannot purchase from old history"), Guarded->Model->BuyItemAtRevision(3, 1, 0));
        TestFalse(TEXT("Guarded actor cannot write a manual replacement"), Guarded->Model->SaveProgress());
        TestFalse(TEXT("Guarded actor cannot reload older banks"), Guarded->Model->ReloadProgress());
        if (!TestTrue(TEXT("Refusals preserve unready model, old bytes and both forward entries"),
            Scope.Unchanged(*Guarded, Image) && ForwardUnchanged())) return false;
        if (!TestTrue(TEXT("Actual EndPlay releases only owned lease; repeated startup still refuses"),
            Scope.DestroyModel(*Guarded) && Scope.CompanionReleased() && ForwardUnchanged())) return false;
    }
    AddInfo(TEXT("Viewportless production BeginPlay/EndPlay, V006 pawn, physical directory/zero-file and Windows sharing. No gameplay, process cold restart, V3 migration, arbitrary old executable or MMO proof."));
    return true;
}
