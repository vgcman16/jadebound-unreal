#include "JadeboundProgressionActor.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Development/DevelopmentIdentity.h"
#include "Domain/ProgressionCore.h"
#include "Domain/RecipientLedger.h"
#include "Persistence/JournalIO.h"
#include "Persistence/ProgressionSaveV2.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

// Only this native test translation unit defines the actor's private friend.
// No UFUNCTION/reflection/RPC/client or production fixture entry point exists.
struct FJadeboundActorBankTestAccess
{
    static void SetIdentity(AJadeboundProgressionActor& Actor, const FString& Id, TUniquePtr<IFileHandle> Lease)
    {
        Actor.bDevelopmentModel = true;
        Actor.ServerCharacterId = Id;
        Actor.DevelopmentWriterLease = MoveTemp(Lease);
    }
    static void SetOpener(AJadeboundProgressionActor& Actor, TFunction<IFileHandle*(const FString&)> Opener)
    { Actor.AutomationJournalOpener = MoveTemp(Opener); }
    static bool Read(AJadeboundProgressionActor& Actor, bool bInitial = true) { return Actor.ReadSave(bInitial); }
    static bool BuyMoonleafOnce(AJadeboundProgressionActor& Actor)
    {
        const auto Result = Actor.Core->Buy(Jadebound::ItemId::Moonleaf, 1);
        if (!Result.Succeeded() || !Result.Changed) return false;
        Actor.RefreshState();
        return Actor.WriteSave();
    }
    static bool AcceptQuestAndSave(AJadeboundProgressionActor& Actor)
    { return Actor.FinishTransaction(Actor.Core->AcceptQuest(), FText::GetEmpty()); }
    static bool RecoverQuestParcelAndSave(AJadeboundProgressionActor& Actor)
    {
        // Trusted fixture progression only. Never save or present outstanding loot.
        if (!Actor.PendingDrops.IsEmpty() || !Actor.Core->GetSnapshot().UnprocessedDropIds.empty()) return false;
        const auto Defeat = Actor.Core->RecordRaiderDefeat(1);
        if (!Defeat.Succeeded() || !Defeat.Changed) return false;
        const auto Pickup = Actor.Core->PickupParcel(1);
        if (!Pickup.Succeeded() || !Pickup.Changed || !Actor.Core->GetSnapshot().UnprocessedDropIds.empty()) return false;
        return Actor.FinishTransaction(Pickup, FText::GetEmpty());
    }
    static bool TurnInQuestAndSave(AJadeboundProgressionActor& Actor, Jadebound::Result& Result)
    {
        Result = Actor.Core->TurnInQuest();
        return Actor.FinishTransaction(Result, FText::GetEmpty());
    }
    static bool IssuePendingLootAndSave(AJadeboundProgressionActor& Actor, const FVector& Location, Jadebound::Result& Result)
    {
        // Synthetic pair fixture only; no enemy/death/provider or public award API.
        Result = {Jadebound::ResultCode::InvalidSnapshot, Jadebound::ValidationCode::None, false, Actor.State.ExactRevision};
        if (!Actor.HasAuthority() || !Actor.HasDevelopmentContext() || !Actor.DevelopmentWriterLease || !Actor.Core ||
            !Actor.GetWorld() || Actor.GetWorld()->bIsTearingDown || Actor.IsActorBeingDestroyed() || Actor.bPersistenceEndingPlay ||
            !Actor.LastCommittedSave || Actor.bPersistenceBlocked || Actor.bUnsavedCandidate || Actor.HeldSaveCandidate) return false;
        Jadebound::Recipient::IssueCandidate Candidate;
        Result = Jadebound::Recipient::BuildIssue(Actor.Core->GetSnapshot(), Actor.PendingDrops, Location, Candidate);
        if (!Result.Succeeded() || !Result.Changed) return false;
        const auto Adopted = Actor.Core->LoadSnapshot(Candidate.State);
        if (!Adopted.Succeeded()) { Result = Adopted; return false; }
        Actor.PendingDrops = MoveTemp(Candidate.Drops);
        return Actor.FinishTransaction(Result, FText::GetEmpty());
    }
    static bool PickupPendingLootAndSave(AJadeboundProgressionActor& Actor, int64 Id, Jadebound::Result& Result)
    {
        // Actual domain claim/removal/save, without public range/possession admission.
        Result = {Jadebound::ResultCode::InvalidSnapshot, Jadebound::ValidationCode::None, false, Actor.State.ExactRevision};
        if (!Actor.HasAuthority() || !Actor.HasDevelopmentContext() || !Actor.DevelopmentWriterLease || !Actor.Core ||
            !Actor.GetWorld() || Actor.GetWorld()->bIsTearingDown || Actor.IsActorBeingDestroyed() || Actor.bPersistenceEndingPlay ||
            !Actor.LastCommittedSave || Actor.bPersistenceBlocked || Actor.bUnsavedCandidate || Actor.HeldSaveCandidate ||
            !Jadebound::Persistence::ValidateDropsV2(Actor.Core->GetSnapshot(), Actor.PendingDrops)) return false;
        const int32 Index = Actor.PendingDrops.IndexOfByPredicate([Id](const FJadeboundSavedDrop& Drop) { return Drop.DropId == Id; });
        if (Index == INDEX_NONE) return false;
        const auto Drop = Actor.PendingDrops[Index];
        Result = Drop.ItemId == 5 ? Actor.Core->PickupParcel(static_cast<uint64>(Id)) :
            Actor.Core->GrantLoot(static_cast<Jadebound::ItemId>(Drop.ItemId), Drop.Count, static_cast<uint64>(Id));
        if (Result.Succeeded()) Actor.PendingDrops.RemoveAt(Index);
        return Actor.FinishTransaction(Result, FText::GetEmpty());
    }
    static bool ReplayConsumedClaimAndSave(AJadeboundProgressionActor& Actor, int64 Id, bool bParcel, Jadebound::Result& Result)
    {
        // Only a known already-issued/processed fixture claim may reach this probe.
        Result = {Jadebound::ResultCode::InvalidSnapshot, Jadebound::ValidationCode::None, false, Actor.State.ExactRevision};
        if (!Actor.Core || Id <= 0 || !Jadebound::Persistence::ValidateDropsV2(Actor.Core->GetSnapshot(), Actor.PendingDrops)) return false;
        const auto& Before = Actor.Core->GetSnapshot();
        if (static_cast<uint64>(Id) >= Before.NextDropId ||
            Actor.PendingDrops.ContainsByPredicate([Id](const FJadeboundSavedDrop& Drop) { return Drop.DropId == Id; })) return false;
        Result = bParcel ? Actor.Core->PickupParcel(static_cast<uint64>(Id)) :
            Actor.Core->GrantLoot(Jadebound::ItemId::IronOre, 2, static_cast<uint64>(Id));
        return Actor.FinishTransaction(Result, FText::GetEmpty());
    }
    static void SetLocalFixtureFutureDeadline(AJadeboundProgressionActor& Actor)
    {
        // Controlled fixture guard only; not unaltered or elapsed production timing.
        Actor.PersistenceRetryAt = FPlatformTime::Seconds() + 60.0;
    }
    static const TArray<FJadeboundSavedDrop>& Drops(const AJadeboundProgressionActor& Actor) { return Actor.PendingDrops; }
    static Jadebound::Snapshot CoreState(const AJadeboundProgressionActor& Actor) { return Actor.Core->GetSnapshot(); }
    static const UJadeboundCharacterSave* Held(const AJadeboundProgressionActor& Actor) { return Actor.HeldSaveCandidate.Get(); }
    static const UJadeboundCharacterSave* Committed(const AJadeboundProgressionActor& Actor) { return Actor.LastCommittedSave.Get(); }
    static int32 Slot(const AJadeboundProgressionActor& Actor) { return Actor.LastSaveSlot; }
    static bool IsHeld(const AJadeboundProgressionActor& Actor)
    { return Actor.bUnsavedCandidate && Actor.bPersistenceBlocked && Actor.HeldSaveCandidate != nullptr; }
    static bool MigrationUnready(const AJadeboundProgressionActor& Actor)
    { return !Actor.bReady && !Actor.bServerModelInitialized; }
    static double Deadline(const AJadeboundProgressionActor& Actor) { return Actor.PersistenceRetryAt; }
    static bool Retry(AJadeboundProgressionActor& Actor) { return Actor.RetryHeldSave(false); }
    static void ExpireLocalFixtureDeadline(AJadeboundProgressionActor& Actor)
    {
        // Fixture setup only, after testing the unaltered immediate refusal.
        // Candidate/commit behavior follows; no elapsed backoff timing claim.
        Actor.PersistenceRetryAt = FPlatformTime::Seconds() - 1.0;
    }
    static bool EncodeSyntheticFixture(UJadeboundCharacterSave* Save, TArray<uint8>& Bytes)
    { return AJadeboundProgressionActor::EncodeJournalForAutomation(Save, Bytes); }
    static void End(AJadeboundProgressionActor& Actor, bool bDestroyActor = true)
    {
        Actor.bPersistenceEndingPlay = true; // Before lease release or world teardown.
        Actor.AutomationJournalOpener = nullptr;
        Actor.DevelopmentWriterLease.Reset();
        if (bDestroyActor) Actor.Destroy();
    }
};

namespace JadeboundActorBanks
{
bool SameCore(const Jadebound::Snapshot& A, const Jadebound::Snapshot& B)
{
    if (A.Version != B.Version || A.Revision != B.Revision || A.Coins != B.Coins ||
        A.EquippedWeapon != B.EquippedWeapon || A.Quest != B.Quest || A.NextDropId != B.NextDropId ||
        A.QuestKillDropId != B.QuestKillDropId || A.UnprocessedDropIds != B.UnprocessedDropIds) return false;
    for (uint32 Index = 0; Index < Jadebound::InventorySlotCount; ++Index)
        if (A.Inventory[Index].Id != B.Inventory[Index].Id || A.Inventory[Index].Count != B.Inventory[Index].Count) return false;
    return true;
}
bool SameReflected(const FJadeboundProgressionState& A, const FJadeboundProgressionState& B)
{ return FJadeboundProgressionState::StaticStruct()->CompareScriptStruct(&A, &B, 0); }
bool SameDrops(const TArray<FJadeboundSavedDrop>& A, const TArray<FJadeboundSavedDrop>& B)
{
    if (A.Num() != B.Num()) return false;
    for (int32 Index = 0; Index < A.Num(); ++Index)
        if (!FJadeboundSavedDrop::StaticStruct()->CompareScriptStruct(&A[Index], &B[Index], 0)) return false;
    return true;
}
bool ReadOwned(const FString& Path, TArray<uint8>& Bytes)
{
    IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
    const int64 Size = Files.FileSize(*Path);
    if (Size <= 0 || Size > 1024 * 1024 + 32) return false;
    TUniquePtr<IFileHandle> Handle(Files.OpenRead(*Path));
    if (!Handle) return false;
    Bytes.SetNumUninitialized(static_cast<int32>(Size));
    return Handle->Read(Bytes.GetData(), Size);
}
bool WriteOwned(const FString& Path, const TArray<uint8>& Bytes)
{
    TUniquePtr<IFileHandle> Handle(FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*Path, false, false));
    const bool bSaved = Handle && Jadebound::Persistence::WriteAndFlush(*Handle, Bytes);
    Handle.Reset();
    return bSaved;
}
bool IsIsolatedUserDir(FString& Out)
{
    FString Argument;
    if (!FParse::Param(FCommandLine::Get(), TEXT("JadeboundActorBankTests")) ||
        !FParse::Value(FCommandLine::Get(), TEXT("UserDir="), Argument) ||
        Argument.IsEmpty() || FPaths::IsRelative(Argument)) return false;
    Argument.ReplaceInline(TEXT("\\"), TEXT("/"));
    TArray<FString> Parts;
    Argument.ParseIntoArray(Parts, TEXT("/"), true);
    for (const auto& Part : Parts) if (Part == TEXT(".") || Part == TEXT("..")) return false;
    FPaths::NormalizeDirectoryName(Argument);
    FString Effective = FPaths::ProjectUserDir();
    FPaths::NormalizeDirectoryName(Effective);
    const FString Parent = FPaths::GetCleanFilename(FPaths::GetPath(Argument));
    const FString Prefix(TEXT("Jadebound-P102-A"));
    const FString Attempt = Parent.Mid(Prefix.Len());
    if (!FPaths::IsSamePath(Argument, Effective) || FPaths::GetCleanFilename(Argument) != TEXT("UserDir-v001") ||
        !Parent.StartsWith(Prefix, ESearchCase::CaseSensitive) || Attempt.Len() != 3) return false;
    for (TCHAR Ch : Attempt) if (Ch < TEXT('0') || Ch > TEXT('9')) return false;
    if (Attempt == TEXT("000")) return false;
    Out = Argument;
    return FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()), Out);
}

// All files are fresh GUID children of the explicit test UserDir. Nothing is
// deleted, seeded into another profile, or installed as a global filesystem.
struct FScope
{
    FAutomationTestBase& Test;
    FString Id, Directory, Paths[2][2]; // [v1/v2][A/B]
    UWorld* World = nullptr;
    TArray<AJadeboundProgressionActor*> Actors;
    bool bValid = false;
    FScope(FAutomationTestBase& InTest, const TCHAR* Case) : Test(InTest)
    {
        FString UserDir;
        if (!IsInGameThread() || !GEngine || !GEngine->WorldSettingsClass)
        { Test.AddError(TEXT("Initialized game-thread engine/world settings required.")); return; }
        if (!IsIsolatedUserDir(UserDir)) { Test.AddError(TEXT("Actor-bank test requires explicit opt-in and fresh Source102 UserDir.")); return; }
        Id = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
        if (!Jadebound::Development::IsCharacterId(Id)) { Test.AddError(TEXT("Canonical test identity failed.")); return; }
        for (int32 Version = 0; Version < 2; ++Version)
            for (int32 Bank = 0; Bank < 2; ++Bank)
                if (!Jadebound::Persistence::TryJournalPath(true, Id, Bank, Version == 1, Paths[Version][Bank]) ||
                    !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Paths[Version][Bank]), UserDir))
                { Test.AddError(TEXT("Production journal path escaped isolated UserDir.")); return; }
        Directory = FPaths::GetPath(Paths[1][0]);
        IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
        if (Files.DirectoryExists(*Directory) || !Files.CreateDirectoryTree(*Directory))
        { Test.AddError(TEXT("Fresh owned GUID directory required; no reset/replay.")); return; }
        UWorld::InitializationValues Values;
        Values.InitializeScenes(false).AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
            .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false).SetTransactional(false);
        World = UWorld::CreateWorld(EWorldType::Game, false, FName(*FString::Printf(TEXT("ActorBank_%s"), *Id)),
            nullptr, true, ERHIFeatureLevel::Num, &Values);
        if (World) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        bValid = World && GEngine->GetWorldContextFromWorld(World) && !World->HasBegunPlay() &&
            !World->bIsTearingDown && World->GetNetMode() == NM_Standalone;
        if (!bValid) Test.AddError(TEXT("Minimal non-BeginPlay authority world unavailable."));
        Test.AddInfo(FString::Printf(TEXT("SYNTHETIC actor-bank %s evidence retained: %s"), Case, *Directory));
    }
    ~FScope()
    {
        if (!World) return;
        UEngine* Engine = GEngine;
        const bool bCanDestroy = Engine && Engine->GetWorldContextFromWorld(World);
        for (auto* Actor : Actors)
            if (IsValid(Actor) && !Actor->IsActorBeingDestroyed())
                FJadeboundActorBankTestAccess::End(*Actor, bCanDestroy);
        if (!bCanDestroy)
        {
            Test.AddError(TEXT("Actor-bank teardown lost engine/world context; leases released, world/context retained."));
            return;
        }
        World->DestroyWorld(false);
        Engine->DestroyWorldContext(World);
        World = nullptr;
    }
    AJadeboundProgressionActor* NewActor()
    {
        if (!bValid) return nullptr;
        TUniquePtr<IFileHandle> Lease(FPlatformFileManager::Get().GetPlatformFile().OpenWrite(
            *FPaths::Combine(Directory, TEXT("Character.writer")), true, false));
        if (!Lease) { Test.AddError(TEXT("Exclusive real Character.writer lease unavailable.")); return nullptr; }
        FActorSpawnParameters Spawn;
        Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Actor = World->SpawnActor<AJadeboundProgressionActor>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
        if (!Actor || !Actor->HasAuthority() || Actor->GetWorld() != World || Actor->GetNetMode() != NM_Standalone || Actor->HasActorBegunPlay())
        { Test.AddError(TEXT("Native non-BeginPlay authority actor unavailable.")); return nullptr; }
        Actors.Add(Actor);
        FJadeboundActorBankTestAccess::SetIdentity(*Actor, Id, MoveTemp(Lease));
        return Actor;
    }
    void Retire(AJadeboundProgressionActor& Actor) { FJadeboundActorBankTestAccess::End(Actor); }
    bool Put(int32 Version, int32 Bank, const TArray<uint8>& Bytes)
    { return Version >= 0 && Version < 2 && Bank >= 0 && Bank < 2 && WriteOwned(Paths[Version][Bank], Bytes); }
};

enum class EFault { Open, PartialWrite, UncertainFlush, CorruptAfterClose, DifferentValidAfterClose, None };
struct FTrace
{
    EFault Fault = EFault::None;
    FString ExpectedPath;
    TArray<uint8> Payload, Substitute;
    TArray<FString> Events;
    int32 Opens = 0, Writes = 0, Flushes = 0, Closes = 0;
    bool bFullFlush = false, bRealFlushSucceeded = false, bRealWriteSucceeded = false, bMutationSucceeded = true, bPathRefused = false;
};
class FForwardHandle final : public IFileHandle
{
    TUniquePtr<IFileHandle> Inner;
    FTrace& Trace;
    FString Path;
public:
    FForwardHandle(IFileHandle* InHandle, FTrace& InTrace, const FString& InPath) : Inner(InHandle), Trace(InTrace), Path(InPath) {}
    ~FForwardHandle() override
    {
        Inner.Reset(); // Destroy owned native handle before mutation; no OS close-result audit.
        ++Trace.Closes;
        Trace.Events.Add(TEXT("close"));
        if (Trace.Fault == EFault::CorruptAfterClose || Trace.Fault == EFault::DifferentValidAfterClose)
        {
            const TArray<uint8> Corrupt = {0x4a, 0x42, 0x50};
            Trace.bMutationSucceeded = FPaths::IsSamePath(Path, Trace.ExpectedPath) &&
                WriteOwned(Path, Trace.Fault == EFault::CorruptAfterClose ? Corrupt : Trace.Substitute);
            Trace.Events.Add(TEXT("owned-after-close-mutation"));
        }
    }
    int64 Tell() override { return Inner->Tell(); }
    bool Seek(int64 Position) override { return Inner->Seek(Position); }
    bool SeekFromEnd(int64 Position = 0) override { return Inner->SeekFromEnd(Position); }
    bool Read(uint8* Out, int64 Count) override { return Inner->Read(Out, Count); }
    bool ReadAt(uint8* Out, int64 Count, int64 Offset) override { return Inner->ReadAt(Out, Count, Offset); }
    bool Truncate(int64 Size) override { return Inner->Truncate(Size); }
    bool Write(const uint8* Data, int64 Count) override
    {
        ++Trace.Writes;
        Trace.Events.Add(TEXT("write"));
        if (!Data || Count <= 0 || Count > 1024 * 1024 + 32) return false;
        Trace.Payload.Reset();
        Trace.Payload.Append(Data, static_cast<int32>(Count));
        Trace.bRealWriteSucceeded = Inner->Write(Data, Trace.Fault == EFault::PartialWrite ? FMath::Min<int64>(8, Count) : Count);
        return Trace.bRealWriteSucceeded && Trace.Fault != EFault::PartialWrite;
    }
    bool Flush(const bool bFull = false) override
    {
        ++Trace.Flushes;
        Trace.bFullFlush = bFull;
        Trace.Events.Add(TEXT("flush"));
        Trace.bRealFlushSucceeded = Inner->Flush(bFull);
        return Trace.bRealFlushSucceeded && Trace.Fault != EFault::UncertainFlush;
    }
};
void Install(AJadeboundProgressionActor& Actor, FTrace& Trace)
{
    FJadeboundActorBankTestAccess::SetOpener(Actor, [&Trace](const FString& Path) -> IFileHandle* {
        ++Trace.Opens;
        Trace.Events.Add(TEXT("open"));
        if (!FPaths::IsSamePath(Path, Trace.ExpectedPath)) { Trace.bPathRefused = true; return nullptr; }
        if (Trace.Fault == EFault::Open) return nullptr;
        IFileHandle* Real = FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*Path, false, false);
        return Real ? new FForwardHandle(Real, Trace, Path) : nullptr;
    });
}
bool MakeLegacyFixture(const FScope& Scope, const FJadeboundProgressionState& State, TArray<uint8>& Bytes)
{
    auto* Save = Cast<UJadeboundCharacterSave>(UGameplayStatics::CreateSaveGameObject(UJadeboundCharacterSave::StaticClass()));
    if (!Save) return false;
    Save->CharacterId = Scope.Id;
    Save->State = State;
    Save->State.Version = 1;
    Save->State.UnprocessedDropIds.Reset();
    JadeboundLegacyV1::Snapshot Legacy;
    return Jadebound::Persistence::DecodeStateV1(Save->State, Legacy) &&
        Jadebound::Persistence::ValidateDropsV1(Legacy, Save->PendingDrops) &&
        FJadeboundActorBankTestAccess::EncodeSyntheticFixture(Save, Bytes);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundActorJournalFaultRetryTest, "Jadebound.Persistence.ActorJournalFaultRetry",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundActorJournalFaultRetryTest::RunTest(const FString& Parameters)
{
    using namespace JadeboundActorBanks;
    using Access = FJadeboundActorBankTestAccess;
    if (!IsInGameThread()) { AddError(TEXT("Actor-bank tests require the game thread.")); return false; }
    const EFault Faults[] = {EFault::Open, EFault::PartialWrite, EFault::UncertainFlush,
        EFault::CorruptAfterClose, EFault::DifferentValidAfterClose};
    for (const auto Fault : Faults)
    {
        FScope Scope(*this, TEXT("write-fault-held-retry"));
        auto* Actor = Scope.NewActor();
        if (!Actor || !Access::Read(*Actor)) return false;
        TArray<uint8> OldA;
        if (!ReadOwned(Scope.Paths[1][0], OldA)) return false;
        const auto* OldCommitted = Access::Committed(*Actor);
        if (!OldCommitted) return false;
        const auto OldCommittedState = OldCommitted->State;
        const auto OldCommittedDrops = OldCommitted->PendingDrops;
        FTrace Trace;
        Trace.Fault = Fault; Trace.ExpectedPath = Scope.Paths[1][1]; Trace.Substitute = OldA;
        Install(*Actor, Trace);
        TestFalse(TEXT("Actual save rejects the injected OS-facing fault"), Access::BuyMoonleafOnce(*Actor));
        // Test the production deadline before modifying any timing field.
        TestTrue(TEXT("Production backoff deadline lies in the future"), Access::Deadline(*Actor) > FPlatformTime::Seconds());
        TestFalse(TEXT("Unaltered immediate retry is refused"), Access::Retry(*Actor));
        TestEqual(TEXT("Immediate retry did not open again"), Trace.Opens, 1);
        const auto ExpectedCore = Access::CoreState(*Actor);
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
        TestTrue(TEXT("Old A bytes survive uncertainty"), ReadOwned(Scope.Paths[1][0], CurrentA) && CurrentA == OldA);
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
        // This changes only this test actor's deadline: candidate/commit test,
        // not an elapsed-backoff/scheduler qualification or production bypass.
        Access::ExpireLocalFixtureDeadline(*Actor);
        TestTrue(TEXT("Local deadline fixture preserves the exact held object"), Access::Held(*Actor) == Held && SameReflected(Held->State, ExpectedState) && SameDrops(Held->PendingDrops, ExpectedDrops));
        Trace.Fault = EFault::None;
        TestTrue(TEXT("Retry of exact candidate writes/flushes/reads real B"), Access::Retry(*Actor));
        TestTrue(TEXT("Retry never reruns the transaction"), SameCore(ExpectedCore, Access::CoreState(*Actor)) && SameReflected(ExpectedState, Actor->GetProgressionState()));
        TestTrue(TEXT("Committed complete candidate and drops match"), Access::Committed(*Actor) &&
            SameReflected(Access::Committed(*Actor)->State, ExpectedState) && SameDrops(Access::Committed(*Actor)->PendingDrops, ExpectedDrops));
        TestTrue(TEXT("Retry reserialized the same payload when first attempt reached Write"), FirstPayload.IsEmpty() || FirstPayload == Trace.Payload);
        TestTrue(TEXT("Readback alone clears the held candidate"), !Access::Held(*Actor) && !Access::IsHeld(*Actor) &&
            Actor->GetPersistencePresentation().FailureKind == EJadeboundPersistenceFailureKind::None && Access::Slot(*Actor) == 1);
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
        FTrace Trace; Trace.Fault = EFault::UncertainFlush; Trace.ExpectedPath = Scope.Paths[1][1];
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
        FScope Scope(*this, TEXT("corrupt-higher-v2-recovery"));
        auto* Actor = Scope.NewActor();
        if (!Actor || !Access::Read(*Actor)) return false;
        const auto Lower = Access::CoreState(*Actor);
        if (!Access::BuyMoonleafOnce(*Actor)) return false;
        Scope.Retire(*Actor);
        const TArray<uint8> Corrupt = {1, 2, 3};
        if (!Scope.Put(1, 1, Corrupt)) return false;
        auto* Reloaded = Scope.NewActor();
        if (!Reloaded) return false;
        TestTrue(TEXT("Actual reader falls back to valid lower v2"), Access::Read(*Reloaded) && SameCore(Lower, Access::CoreState(*Reloaded)) && Access::Slot(*Reloaded) == 0);
        TArray<uint8> After;
        TestTrue(TEXT("Invalid bank retained as evidence"), ReadOwned(Scope.Paths[1][1], After) && After == Corrupt);
    }
    {
        FScope Scope(*this, TEXT("malformed-v2-refuses-legacy"));
        auto* Fixture = Scope.NewActor();
        if (!Fixture || !Access::Read(*Fixture)) return false;
        TArray<uint8> Legacy;
        if (!MakeLegacyFixture(Scope, Fixture->GetProgressionState(), Legacy)) return false;
        Scope.Retire(*Fixture);
        const TArray<uint8> Corrupt = {1, 2, 3};
        if (!Scope.Put(0, 0, Legacy) || !Scope.Put(0, 1, Legacy) || !Scope.Put(1, 0, Corrupt)) return false;
        auto* Reloaded = Scope.NewActor();
        if (!Reloaded) return false;
        TestFalse(TEXT("Any existing malformed v2 forbids valid legacy fallback"), Access::Read(*Reloaded));
        TestTrue(TEXT("Format boundary holds RecoveryRequired"), Reloaded->GetPersistencePresentation().FailureKind == EJadeboundPersistenceFailureKind::RecoveryRequired);
        TArray<uint8> A, B, V2;
        TestTrue(TEXT("Both originals and v2 partial are untouched"), ReadOwned(Scope.Paths[0][0], A) && A == Legacy &&
            ReadOwned(Scope.Paths[0][1], B) && B == Legacy && ReadOwned(Scope.Paths[1][0], V2) && V2 == Corrupt);
        TestFalse(TEXT("No replacement v2 B created"), FPlatformFileManager::Get().GetPlatformFile().FileExists(*Scope.Paths[1][1]));
    }
    {
        FScope Scope(*this, TEXT("equal-revision-original-byte-conflict"));
        auto* Fixture = Scope.NewActor();
        if (!Fixture || !Access::Read(*Fixture)) return false;
        TArray<uint8> A;
        if (!ReadOwned(Scope.Paths[1][0], A)) return false;
        auto* Other = Cast<UJadeboundCharacterSave>(UGameplayStatics::CreateSaveGameObject(UJadeboundCharacterSave::StaticClass()));
        if (!Other) return false;
        Other->CharacterId = Scope.Id;
        Other->State = Fixture->GetProgressionState();
        Other->State.Coins = 9; // Synthetic valid equal-revision fixture; not earned progression.
        Jadebound::Snapshot Valid;
        TArray<uint8> B;
        if (!Jadebound::Persistence::DecodeStateV2(Other->State, Valid) || !Access::EncodeSyntheticFixture(Other, B) || A == B) return false;
        Scope.Retire(*Fixture);
        if (!Scope.Put(1, 1, B)) return false;
        auto* Conflicted = Scope.NewActor();
        if (!Conflicted) return false;
        TestFalse(TEXT("Equal revision with different original bytes holds"), Access::Read(*Conflicted));
        TestTrue(TEXT("Conflict requests recovery"), Conflicted->GetPersistencePresentation().FailureKind == EJadeboundPersistenceFailureKind::RecoveryRequired);
        TArray<uint8> AfterA, AfterB;
        TestTrue(TEXT("Conflict preserves both actual streams"), ReadOwned(Scope.Paths[1][0], AfterA) && A == AfterA && ReadOwned(Scope.Paths[1][1], AfterB) && B == AfterB);
    }
    {
        FScope Scope(*this, TEXT("identical-equal-revision-streams"));
        auto* Fixture = Scope.NewActor();
        if (!Fixture || !Access::Read(*Fixture)) return false;
        TArray<uint8> A;
        if (!ReadOwned(Scope.Paths[1][0], A)) return false;
        Scope.Retire(*Fixture);
        if (!Scope.Put(1, 1, A)) return false;
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
    {
        FScope Scope(*this, TEXT("synthetic-v1-migration-held-retry"));
        // CreateSaveGameObject + actual production envelope, not an old archive claim.
        FJadeboundProgressionState LegacyState;
        Jadebound::ProgressionCore LegacySetup;
        if (!LegacySetup.Buy(Jadebound::ItemId::Moonleaf, 1).Succeeded() ||
            !Jadebound::Persistence::EncodeStateV2(LegacySetup.GetSnapshot(), LegacyState)) return false;
        auto ExpectedLegacyState = LegacyState;
        ExpectedLegacyState.Version = 1;
        auto Expected = LegacySetup.GetSnapshot(); // Oracle from known input, not migrated output.
        Expected.Version = Jadebound::SnapshotVersion;
        ++Expected.Revision; // Only the representation transaction may change.
        FJadeboundProgressionState ExpectedState;
        if (!Jadebound::Persistence::EncodeStateV2(Expected, ExpectedState)) return false;
        TArray<uint8> Legacy;
        if (!MakeLegacyFixture(Scope, LegacyState, Legacy) || !Scope.Put(0, 0, Legacy) || !Scope.Put(0, 1, Legacy)) return false;
        auto* Actor = Scope.NewActor();
        if (!Actor) return false;
        FTrace Trace; Trace.Fault = EFault::UncertainFlush; Trace.ExpectedPath = Scope.Paths[1][0];
        Install(*Actor, Trace);
        TestFalse(TEXT("Actual migration remains held after uncertain flush"), Access::Read(*Actor));
        TestTrue(TEXT("Migration preserves every known input field except revision+1"), SameCore(Expected, Access::CoreState(*Actor)) && SameReflected(ExpectedState, Actor->GetProgressionState()));
        TestEqual(TEXT("Exactly one representation revision"), Expected.Revision, uint64(2));
        TestEqual(TEXT("Migration does not award currency"), Expected.Coins, int64(8));
        TestTrue(TEXT("Migration candidate held with complete original still committed"), Access::IsHeld(*Actor) && Access::Committed(*Actor) &&
            Access::Committed(*Actor)->CharacterId == Scope.Id && SameReflected(Access::Committed(*Actor)->State, ExpectedLegacyState) && Access::Committed(*Actor)->PendingDrops.IsEmpty());
        TestTrue(TEXT("Migration stays unready until v2 readback"), Access::MigrationUnready(*Actor));
        const auto* Held = Access::Held(*Actor);
        if (!TestTrue(TEXT("Complete expected migration candidate held"), Held && Held->CharacterId == Scope.Id &&
            SameReflected(Held->State, ExpectedState) && Held->PendingDrops.IsEmpty())) return false;
        const auto HeldDrops = Held->PendingDrops;
        const auto FirstPayload = Trace.Payload;
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
        TestTrue(TEXT("Both v1 streams survive migration and retry"), ReadOwned(Scope.Paths[0][0], A) && A == Legacy && ReadOwned(Scope.Paths[0][1], B) && B == Legacy);
        Scope.Retire(*Actor);
        auto* Reloaded = Scope.NewActor();
        if (!Reloaded) return false;
        TestTrue(TEXT("New actor reopens the exact migrated v2 snapshot"), Access::Read(*Reloaded) && SameCore(Expected, Access::CoreState(*Reloaded)));
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
    if (!Jadebound::Persistence::EncodeStateV2(Accepted, AcceptedState) ||
        !Jadebound::Persistence::EncodeStateV2(Recovered, RecoveredState) ||
        !Jadebound::Persistence::EncodeStateV2(Completed, CompletedState)) return false;
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
        if (!ReadOwned(Scope.Paths[1][0], OldA)) return false;
        FTrace Trace;
        Trace.Fault = EFault::UncertainFlush; Trace.ExpectedPath = Scope.Paths[1][1];
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
        TArray<uint8> CurrentA, UncertainB;
        if (!TestTrue(TEXT("Old r3 A survives and uncertain B contains the full attempted reward bytes"),
            ReadOwned(Scope.Paths[1][0], CurrentA) && CurrentA == OldA && !FirstPayload.IsEmpty() &&
            ReadOwned(Scope.Paths[1][1], UncertainB) && UncertainB == FirstPayload)) return false;
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
        }
        Scope.Retire(*Actor); // Release real lease; direct-reopen branch deliberately never retries.
        auto* Reloaded = Scope.NewActor();
        if (!Reloaded || !TestTrue(TEXT("Second actor reopens actual Completed r4 bank"), Access::Read(*Reloaded))) return false;
        TestTrue(TEXT("Whole reward and consumed parcel history survive real reopen"), SameCore(Completed, Access::CoreState(*Reloaded)) &&
            SameReflected(CompletedState, Reloaded->GetProgressionState()) && Access::Slot(*Reloaded) == 1 &&
            Access::Committed(*Reloaded) && SameReflected(CompletedState, Access::Committed(*Reloaded)->State) &&
            Access::Committed(*Reloaded)->PendingDrops.IsEmpty() && !Access::Held(*Reloaded));
        TArray<uint8> BeforeA, BeforeB;
        if (!TestTrue(TEXT("Reopen leaves both original saved streams unchanged"), ReadOwned(Scope.Paths[1][0], BeforeA) && BeforeA == OldA &&
            ReadOwned(Scope.Paths[1][1], BeforeB) && BeforeB == FirstPayload)) return false;
        FTrace ReplayTrace; ReplayTrace.ExpectedPath = Scope.Paths[1][0];
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
        TestTrue(TEXT("Repeated reward attempt preserves both actual bank byte streams"), ReadOwned(Scope.Paths[1][0], AfterA) && AfterA == BeforeA &&
            ReadOwned(Scope.Paths[1][1], AfterB) && AfterB == BeforeB);
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
    FScope Scope(*this, TEXT("nonempty-v2-loot-uncertain-retry-reopen-pickup"));
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
        if (!Save || !Jadebound::Persistence::EncodeStateV2(*Oracles[Stage], States[Stage]) ||
            !Jadebound::Persistence::ValidateDropsV2(*Oracles[Stage], *OracleDrops[Stage])) return false;
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
        return ReadOwned(Scope.Paths[1][Bank], ReadBack) && ReadBack == Journals[Stage];
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
    FTrace Trace; Trace.Fault = EFault::UncertainFlush; Trace.ExpectedPath = Scope.Paths[1][1];
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
    TestTrue(TEXT("Retry serializes exact known r3 bytes and does not issue a third drop"), LivePairMatches(*Actor,3) &&
        CommitMatches(*Actor,3) && Access::Slot(*Actor) == 1 && !Access::Held(*Actor) && !Access::IsHeld(*Actor) &&
        Actor->GetPersistencePresentation().FailureKind == EJadeboundPersistenceFailureKind::None &&
        Trace.Payload == Journals[3] && Trace.Opens == 2 && Trace.Writes == 2 && Trace.Flushes == 2 && Trace.Closes == 2 &&
        Trace.bFullFlush && Trace.bRealWriteSucceeded && Trace.bRealFlushSucceeded && !Trace.bPathRefused &&
        FString::Join(Trace.Events,TEXT("|")) == TEXT("open|write|flush|close|open|write|flush|close") &&
        BankMatches(0,2) && BankMatches(1,3));
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
    FTrace RepeatTrace; RepeatTrace.ExpectedPath = Scope.Paths[1][1];
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
    TestFalse(TEXT("No legacy A created in this fresh v2 fixture"), FPlatformFileManager::Get().GetPlatformFile().FileExists(*Scope.Paths[0][0]));
    TestFalse(TEXT("No legacy B created in this fresh v2 fixture"), FPlatformFileManager::Get().GetPlatformFile().FileExists(*Scope.Paths[0][1]));
    AddInfo(TEXT("Synthetic nonempty v2 actor-bank pair/real IO regression only; no native enemy/death, final-hit ownership, public pickup/range, animation/visual, game restart, network, power-loss or elapsed-scheduler acceptance."));
    return true;
}
#endif
