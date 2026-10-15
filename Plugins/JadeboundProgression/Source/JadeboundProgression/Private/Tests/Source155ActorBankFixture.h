#pragma once
// Included once by JadeboundActorBankTests.cpp under WITH_DEV_AUTOMATION_TESTS.
#include "Development/DevelopmentIdentity.h"
#include "Domain/ProgressionCore.h"
#include "Domain/RecipientLedger.h"
#include "Persistence/JournalIO.h"
#include "Persistence/ProgressionSaveV2.h"
#include "Persistence/ProgressionSaveV3.h"
#include "Persistence/JournalNamespacePaths.h"
#include "UObject/UnrealType.h"
#include <algorithm>
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
    static const TArray<uint8>& HeldBytes(const AJadeboundProgressionActor& Actor) { return Actor.HeldJournalBytes; }
    static const FString& HeldPath(const AJadeboundProgressionActor& Actor) { return Actor.HeldJournalPath; }
    static int32 HeldSlot(const AJadeboundProgressionActor& Actor) { return Actor.HeldJournalSlot; }
    static bool Admitted(const AJadeboundProgressionActor& Actor) { return Actor.bV3JournalAdmitted; }
    static const IFileHandle* ForwardGuardLease(const AJadeboundProgressionActor& Actor)
    { return Actor.DevelopmentWriterLease.Get(); }
    static bool BuyMoonleafOnce(AJadeboundProgressionActor& Actor)
    {
        if (!Actor.Core || !Actor.bV3JournalAdmitted || !Actor.HasAuthority() || !Actor.HasDevelopmentContext() ||
            !Actor.DevelopmentWriterLease || !Actor.LastCommittedSave || Actor.bPersistenceBlocked ||
            Actor.bUnsavedCandidate || Actor.HeldSaveCandidate || Actor.bPersistenceEndingPlay ||
            !Actor.GetWorld() || Actor.GetWorld()->bIsTearingDown || Actor.IsActorBeingDestroyed()) return false;
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
            !Jadebound::Persistence::ValidateDropsV3(Actor.Core->GetSnapshot(), Actor.PendingDrops)) return false;
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
        if (!Actor.Core || Id <= 0 || !Jadebound::Persistence::ValidateDropsV3(Actor.Core->GetSnapshot(), Actor.PendingDrops)) return false;
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
        A.EquippedWeapon != B.EquippedWeapon || A.EquippedTorso != B.EquippedTorso || A.Quest != B.Quest || A.NextDropId != B.NextDropId ||
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
    if (!FParse::Param(FCommandLine::Get(), TEXT("JadeboundV3ActorBankTests")) ||
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
    const FString Prefix(TEXT("Jadebound-S155-A"));
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
    FString Id, Directory;
    Jadebound::Persistence::JournalNamespacePaths Paths;
    FString V1Path(int32 Bank) const { return Bank >= 0 && Bank < 2 ? Paths.Banks[0][Bank] : FString(); }
    FString V2Path(int32 Bank) const { return Bank >= 0 && Bank < 2 ? Paths.Banks[1][Bank] : FString(); }
    FString V3Path(int32 Bank) const { return Bank >= 0 && Bank < 2 ? Paths.Banks[2][Bank] : FString(); }
    UWorld* World = nullptr;
    TArray<AJadeboundProgressionActor*> Actors;
    bool bValid = false;
    FScope(FAutomationTestBase& InTest, const TCHAR* Case) : Test(InTest)
    {
        FString UserDir;
        if (!IsInGameThread() || !GEngine || !GEngine->WorldSettingsClass)
        { Test.AddError(TEXT("Initialized game-thread engine/world settings required.")); return; }
        if (!IsIsolatedUserDir(UserDir)) { Test.AddError(TEXT("Actor-bank test requires explicit opt-in and a fresh scoped UserDir.")); return; }
        Id = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
        if (!Jadebound::Development::IsCharacterId(Id)) { Test.AddError(TEXT("Canonical test identity failed.")); return; }
        if (!Jadebound::Persistence::BuildJournalNamespacePaths(true, Id, Paths))
        { Test.AddError(TEXT("Canonical three-format fixture paths unavailable.")); return; }
        Directory = FPaths::GetPath(V3Path(0));
        for (int32 Version = 0; Version < 3; ++Version)
            for (int32 Bank = 0; Bank < 2; ++Bank)
                if (!FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Paths.Banks[Version][Bank]), UserDir) ||
                    !FPaths::IsSamePath(FPaths::GetPath(Paths.Banks[Version][Bank]), Directory))
                { Test.AddError(TEXT("Production journal path escaped isolated UserDir/shared directory.")); return; }
        for (const FString& Path : Paths.ForwardV4)
            if (!FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Path), UserDir) ||
                !FPaths::IsSamePath(FPaths::GetPath(Path), Directory))
            { Test.AddError(TEXT("Forward guard path escaped isolated UserDir/shared directory.")); return; }
        IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
        if (Jadebound::Persistence::HasJournalEntry(Files, Directory) || !Files.CreateDirectoryTree(*Directory))
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
    bool PutV1(int32 Bank, const TArray<uint8>& Bytes)
    { return Bank >= 0 && Bank < 2 && WriteOwned(V1Path(Bank), Bytes); }
    bool PutV2(int32 Bank, const TArray<uint8>& Bytes)
    { return Bank >= 0 && Bank < 2 && WriteOwned(V2Path(Bank), Bytes); }
    bool PutV3(int32 Bank, const TArray<uint8>& Bytes)
    { return Bank >= 0 && Bank < 2 && WriteOwned(V3Path(Bank), Bytes); }
    bool OlderPairsAbsent() const
    {
        auto& Files = FPlatformFileManager::Get().GetPlatformFile();
        for (int32 Bank = 0; Bank < 2; ++Bank)
            if (Jadebound::Persistence::HasJournalEntry(Files, V1Path(Bank)) ||
                Jadebound::Persistence::HasJournalEntry(Files, V2Path(Bank))) return false;
        return true;
    }
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
bool MakeV1Fixture(const FScope& Scope, const JadeboundLegacyV1::Snapshot& Source,
    TArray<uint8>& Bytes, FJadeboundProgressionState& Reflected)
{
    // Frozen V1 producer; current reflected class/envelope, never an old archive claim.
    if (!JadeboundLegacyV1::ProgressionCore::ValidateSnapshot(Source).Succeeded() ||
        Source.NextDropId > static_cast<uint64>(MAX_int64) || Source.QuestKillDropId > static_cast<uint64>(MAX_int64)) return false;
    auto* Save = Cast<UJadeboundCharacterSave>(UGameplayStatics::CreateSaveGameObject(UJadeboundCharacterSave::StaticClass()));
    if (!Save) return false;
    Save->CharacterId = Scope.Id;
    auto& State = Save->State;
    State.Version = 1; State.ExactRevision = Source.Revision;
    State.Revision = static_cast<int64>(std::min(Source.Revision, static_cast<uint64>(MAX_int64)));
    State.Coins = Source.Coins; State.EquippedWeapon = static_cast<int32>(Source.EquippedWeapon);
    State.EquippedTorso = 0; State.QuestState = static_cast<int32>(Source.Quest);
    State.NextDropId = static_cast<int64>(Source.NextDropId); State.QuestKillDropId = static_cast<int64>(Source.QuestKillDropId);
    State.Slots.SetNum(static_cast<int32>(JadeboundLegacyV1::InventorySlotCount));
    for (uint32 I = 0; I < JadeboundLegacyV1::InventorySlotCount; ++I)
    { State.Slots[I].ItemId = static_cast<int32>(Source.Inventory[I].Id); State.Slots[I].Count = static_cast<int32>(Source.Inventory[I].Count); }
    for (const auto Id : Source.RecordedDropIds)
    { if (Id > static_cast<uint64>(MAX_int64)) return false; State.RecordedDropIds.Add(static_cast<int64>(Id)); }
    for (const auto Id : Source.ProcessedDropIds)
    { if (Id > static_cast<uint64>(MAX_int64)) return false; State.ProcessedDropIds.Add(static_cast<int64>(Id)); }
    JadeboundLegacyV1::Snapshot Check;
    TArray<uint8> Candidate;
    if (!Jadebound::Persistence::DecodeStateV1(State, Check) || !Jadebound::Persistence::ValidateDropsV1(Check, Save->PendingDrops) ||
        !FJadeboundActorBankTestAccess::EncodeSyntheticFixture(Save, Candidate)) return false;
    Reflected = State; Bytes = MoveTemp(Candidate); return true;
}
bool MakeV2Fixture(const FScope& Scope, const JadeboundLegacyV2::Snapshot& Source,
    TArray<uint8>& Bytes, FJadeboundProgressionState& Reflected)
{
    auto* Save = Cast<UJadeboundCharacterSave>(UGameplayStatics::CreateSaveGameObject(UJadeboundCharacterSave::StaticClass()));
    if (!Save || !Jadebound::Persistence::EncodeStateV2(Source, Save->State) ||
        !Jadebound::Persistence::ValidateDropsV2(Source, Save->PendingDrops)) return false;
    Save->CharacterId = Scope.Id;
    TArray<uint8> Candidate;
    if (!FJadeboundActorBankTestAccess::EncodeSyntheticFixture(Save, Candidate)) return false;
    Reflected = Save->State; Bytes = MoveTemp(Candidate); return true;
}
bool MakeV3Fixture(const FScope& Scope, const Jadebound::Snapshot& Source, TArray<uint8>& Bytes)
{
    auto* Save = Cast<UJadeboundCharacterSave>(UGameplayStatics::CreateSaveGameObject(UJadeboundCharacterSave::StaticClass()));
    if (!Save || !Jadebound::Persistence::EncodeStateV3(Source, Save->State) ||
        !Jadebound::Persistence::ValidateDropsV3(Source, Save->PendingDrops)) return false;
    Save->CharacterId = Scope.Id;
    return FJadeboundActorBankTestAccess::EncodeSyntheticFixture(Save, Bytes);
}
bool CacheMatches(const AJadeboundProgressionActor& Actor, const TArray<uint8>& Bytes, const FString& Path, int32 Slot)
{
    using Access = FJadeboundActorBankTestAccess;
    return !Bytes.IsEmpty() && Access::HeldBytes(Actor) == Bytes && Access::HeldPath(Actor) == Path && Access::HeldSlot(Actor) == Slot;
}
bool CacheCleared(const AJadeboundProgressionActor& Actor)
{
    using Access = FJadeboundActorBankTestAccess;
    return Access::HeldBytes(Actor).IsEmpty() && Access::HeldPath(Actor).IsEmpty() && Access::HeldSlot(Actor) == INDEX_NONE;
}
}
