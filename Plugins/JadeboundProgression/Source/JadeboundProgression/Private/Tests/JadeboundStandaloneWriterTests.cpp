#include "JadeboundProgressionActor.h"

#if WITH_DEV_AUTOMATION_TESTS && PLATFORM_WINDOWS
#include "Domain/ProgressionCore.h"
#include "Persistence/ProgressionSaveV3.h"
#include "Persistence/JournalNamespacePaths.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace JadeboundStandaloneWriter
{
inline constexpr int32 JournalPathCount = 8; // V1, current V3, retained V2, forward V4.
static_assert(Jadebound::SnapshotVersion == 3, "Current standalone fixture requires V3");
struct FImage
{
    Jadebound::Snapshot Core;
    FJadeboundProgressionState Reflected, Committed, Candidate;
    const UJadeboundCharacterSave* Saved = nullptr;
    const UJadeboundCharacterSave* Held = nullptr;
    const IFileHandle* Lease = nullptr;
    const IFileHandle* DevelopmentLease = nullptr;
    const APawn* Hero = nullptr;
    const APlayerController* Player = nullptr;
    const AActor* Owner = nullptr;
    FString SaveId, CommittedId, CandidateId, FailureMessage, Feedback, WirePath;
    TArray<uint8> WireBytes;
    int32 WireSlot = INDEX_NONE;
    TArray<FJadeboundSavedDrop> Pending, CommittedDrops, CandidateDrops;
    int32 Slot = -1, Opens = 0, SuccessfulOpens = 0;
    double Deadline = 0.0;
    uint8 RetryStep = 0;
    EJadeboundPersistenceFailureKind Failure = EJadeboundPersistenceFailureKind::None;
    bool bReady = false, bInitialized = false, bBlocked = false, bUnsaved = false;
    bool bEnding = false, bDevelopment = false, bAdmitted = false;
};
}

// Private automation friend only. Startup/teardown use real actor lifecycle;
// this access never sets readiness/BeginPlay bits or creates/resets a lease.
struct FJadeboundStandaloneWriterTestAccess
{
    static bool Capture(const AJadeboundProgressionActor& Model, JadeboundStandaloneWriter::FImage& Out)
    {
        if (!Model.Core) return false;
        Out = JadeboundStandaloneWriter::FImage{};
        Out.Core = Model.Core->GetSnapshot(); Out.Reflected = Model.State;
        Out.Saved = Model.LastCommittedSave.Get(); Out.Held = Model.HeldSaveCandidate.Get();
        Out.Lease = Model.StandaloneWriterLease.Get(); Out.DevelopmentLease = Model.DevelopmentWriterLease.Get();
        Out.Hero = Model.Hero.Get(); Out.Player = Model.Player.Get(); Out.Owner = Model.GetOwner();
        Out.SaveId = Model.GetSaveCharacterId(); Out.Pending = Model.PendingDrops;
        if (Out.Saved)
        {
            Out.Committed = Out.Saved->State; Out.CommittedId = Out.Saved->CharacterId;
            Out.CommittedDrops = Out.Saved->PendingDrops;
        }
        if (Out.Held)
        {
            Out.Candidate = Out.Held->State; Out.CandidateId = Out.Held->CharacterId;
            Out.CandidateDrops = Out.Held->PendingDrops;
        }
        Out.Slot = Model.LastSaveSlot; Out.Deadline = Model.PersistenceRetryAt;
        Out.RetryStep = Model.PersistenceRetryStep; Out.Failure = Model.PersistencePresentation.FailureKind;
        Out.FailureMessage = Model.PersistencePresentation.Message.ToString(); Out.Feedback = Model.Feedback.ToString();
        Out.bReady = Model.bReady; Out.bInitialized = Model.bServerModelInitialized;
        Out.bBlocked = Model.bPersistenceBlocked; Out.bUnsaved = Model.bUnsavedCandidate;
        Out.bEnding = Model.bPersistenceEndingPlay; Out.bDevelopment = Model.bDevelopmentModel;
        Out.bAdmitted = Model.bV3JournalAdmitted; Out.WireBytes = Model.HeldJournalBytes;
        Out.WirePath = Model.HeldJournalPath; Out.WireSlot = Model.HeldJournalSlot;
        return true;
    }
    static void SetOpener(AJadeboundProgressionActor& Model, TFunction<IFileHandle*(const FString&)> Opener)
    { Model.AutomationJournalOpener = MoveTemp(Opener); }
    static bool Read(AJadeboundProgressionActor& Model) { return Model.ReadSave(false); }
    static bool Write(AJadeboundProgressionActor& Model) { return Model.WriteSave(); }
    static bool Retry(AJadeboundProgressionActor& Model) { return Model.RetryHeldSave(false); }
    static bool Ending(const AJadeboundProgressionActor& Model) { return Model.bPersistenceEndingPlay; }
    static const IFileHandle* Lease(const AJadeboundProgressionActor& Model) { return Model.StandaloneWriterLease.Get(); }
    // Set only a controlled fixture deadline AFTER a real retryable purchase
    // failure. The actual candidate/lease/readiness and production backoff are
    // untouched; this verifies the guard, not elapsed production backoff.
    static bool SetFixtureFutureDeadline(AJadeboundProgressionActor& Model, const IFileHandle* OriginalLease)
    {
        const double Now = FPlatformTime::Seconds();
        if (Model.bDevelopmentModel || !OriginalLease || Model.StandaloneWriterLease.Get() != OriginalLease ||
            !Model.HeldSaveCandidate || !Model.bUnsavedCandidate || !Model.bPersistenceBlocked ||
            Model.PersistencePresentation.FailureKind != EJadeboundPersistenceFailureKind::Retryable ||
            !FMath::IsFinite(Now) || !FMath::IsFinite(Now + 60.0)) return false;
        Model.PersistenceRetryAt = Now + 60.0;
        return true;
    }
    // Only the fixture clock changes; never a claim about elapsed wall time.
    static void ExpireFixtureDeadline(AJadeboundProgressionActor& Model)
    { Model.PersistenceRetryAt = FPlatformTime::Seconds() - 1.0; }
};

namespace JadeboundStandaloneWriter
{
using Access = FJadeboundStandaloneWriterTestAccess;

bool SameCore(const Jadebound::Snapshot& A, const Jadebound::Snapshot& B)
{
    if (A.Version != B.Version || A.Revision != B.Revision || A.Coins != B.Coins || A.Quest != B.Quest ||
        A.EquippedWeapon != B.EquippedWeapon || A.EquippedTorso != B.EquippedTorso || A.NextDropId != B.NextDropId || A.QuestKillDropId != B.QuestKillDropId ||
        A.UnprocessedDropIds != B.UnprocessedDropIds) return false;
    for (uint32 I = 0; I < Jadebound::InventorySlotCount; ++I)
        if (A.Inventory[I].Id != B.Inventory[I].Id || A.Inventory[I].Count != B.Inventory[I].Count) return false;
    return true;
}
bool SameState(const FJadeboundProgressionState& A, const FJadeboundProgressionState& B)
{ return FJadeboundProgressionState::StaticStruct()->CompareScriptStruct(&A, &B, 0); }
bool SameDrops(const TArray<FJadeboundSavedDrop>& A, const TArray<FJadeboundSavedDrop>& B)
{
    if (A.Num() != B.Num()) return false;
    for (int32 I = 0; I < A.Num(); ++I)
        if (A[I].DropId != B[I].DropId || A[I].ItemId != B[I].ItemId || A[I].Count != B[I].Count ||
            A[I].Location != B[I].Location) return false;
    return true;
}
bool SameImage(const FImage& A, const FImage& B)
{
    // Ordinary action feedback/receipts may change on rejection. Durable/model
    // state and the persistence failure presentation must remain exact.
    return SameCore(A.Core, B.Core) && SameState(A.Reflected, B.Reflected) && SameState(A.Committed, B.Committed) &&
        SameState(A.Candidate, B.Candidate) && A.Saved == B.Saved && A.Held == B.Held && A.Lease == B.Lease &&
        A.DevelopmentLease == B.DevelopmentLease && A.Hero == B.Hero && A.Player == B.Player && A.Owner == B.Owner &&
        A.SaveId == B.SaveId && A.CommittedId == B.CommittedId && A.CandidateId == B.CandidateId &&
        A.FailureMessage == B.FailureMessage && SameDrops(A.Pending, B.Pending) &&
        SameDrops(A.CommittedDrops, B.CommittedDrops) && SameDrops(A.CandidateDrops, B.CandidateDrops) &&
        A.Slot == B.Slot && A.Opens == B.Opens && A.SuccessfulOpens == B.SuccessfulOpens &&
        A.Deadline == B.Deadline && A.RetryStep == B.RetryStep && A.Failure == B.Failure &&
        A.bReady == B.bReady && A.bInitialized == B.bInitialized && A.bBlocked == B.bBlocked &&
        A.bUnsaved == B.bUnsaved && A.bEnding == B.bEnding && A.bDevelopment == B.bDevelopment &&
        A.bAdmitted == B.bAdmitted && A.WireBytes == B.WireBytes && A.WirePath == B.WirePath && A.WireSlot == B.WireSlot;
}

struct FBank { bool bExists = false; TArray<uint8> Bytes; };
struct FBanks { FBank Bank[JournalPathCount]; };
bool SameBank(const FBank& A, const FBank& B) { return A.bExists == B.bExists && A.Bytes == B.Bytes; }
bool SameBanks(const FBanks& A, const FBanks& B)
{
    for (int32 I = 0; I < JournalPathCount; ++I) if (!SameBank(A.Bank[I], B.Bank[I])) return false;
    return true;
}
bool OwnedUser(FString& User)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("JadeboundV3StandaloneWriterTests")) ||
        !FParse::Value(FCommandLine::Get(), TEXT("UserDir="), User) || User.IsEmpty() || FPaths::IsRelative(User)) return false;
    User.ReplaceInline(TEXT("\\"), TEXT("/"));
    TArray<FString> Parts; User.ParseIntoArray(Parts, TEXT("/"), true);
    for (const FString& Part : Parts) if (Part == TEXT(".") || Part == TEXT("..")) return false;
    FPaths::NormalizeDirectoryName(User);
    FString Effective = FPaths::ProjectUserDir(); FPaths::NormalizeDirectoryName(Effective);
    const FString Parent = FPaths::GetCleanFilename(FPaths::GetPath(User));
    const FString Prefix(TEXT("Jadebound-S157-A"));
    const FString Attempt = Parent.Mid(Prefix.Len());
    if (!Parent.StartsWith(Prefix, ESearchCase::CaseSensitive) || Attempt.Len() != 3 || Attempt == TEXT("000")) return false;
    for (TCHAR Ch : Attempt) if (Ch < TEXT('0') || Ch > TEXT('9')) return false;
    return FPaths::GetCleanFilename(User) == TEXT("UserDir-v001") && FPaths::IsSamePath(User, Effective) &&
        FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()), User);
}

struct FEntry
{
    TStrongObjectPtr<APlayerController> PC;
    TStrongObjectPtr<ACharacter> Hero;
    TStrongObjectPtr<AJadeboundProgressionActor> Model;
    int32 Opens = 0, SuccessfulOpens = 0;
    FString LastOpenedPath;
    bool bFailOpen = false, bForeign = false;
    TArray<uint8> LastAttemptBytes, LastWrittenBytes;
    FString LastAttemptPath;
    int32 LastAttemptSlot = INDEX_NONE;
};

// Observe the real single WriteAndFlush buffer; all OS calls remain delegated.
class FRecordedJournalHandle final : public IFileHandle
{
    TUniquePtr<IFileHandle> Inner;
    TArray<uint8>& Written;
public:
    FRecordedJournalHandle(IFileHandle* Handle, TArray<uint8>& Buffer) : Inner(Handle), Written(Buffer) {}
    int64 Tell() override { return Inner->Tell(); }
    bool Seek(int64 Position) override { return Inner->Seek(Position); }
    bool SeekFromEnd(int64 Position = 0) override { return Inner->SeekFromEnd(Position); }
    bool Read(uint8* Out, int64 Count) override { return Inner->Read(Out, Count); }
    bool ReadAt(uint8* Out, int64 Count, int64 Offset) override { return Inner->ReadAt(Out, Count, Offset); }
    bool Truncate(int64 Size) override { return Inner->Truncate(Size); }
    bool Write(const uint8* Data, int64 Count) override
    {
        if (!Data || Count <= 0 || Count > 1024 * 1024 + 32) return false;
        Written.Reset(); Written.Append(Data, static_cast<int32>(Count));
        return Inner->Write(Data, Count);
    }
    bool Flush(const bool bFull = false) override { return Inner->Flush(bFull); }
};

struct FScope
{
    FAutomationTestBase& Test;
    UWorld* World = nullptr;
    TStrongObjectPtr<UWorld> WorldKeeper;
    TStrongObjectPtr<UGameInstance> Instance;
    TStrongObjectPtr<UClass> HeroClass;
    TStrongObjectPtr<UStaticMesh> Glaive, Spear;
    TArray<TUniquePtr<FEntry>> Entries;
    FString Paths[JournalPathCount], Companion;
    bool bValid = false;

    explicit FScope(FAutomationTestBase& InTest) : Test(InTest)
    {
        FString User;
        if (!IsInGameThread() || !GEngine || !GEngine->WorldSettingsClass || !OwnedUser(User))
        { Test.AddError(TEXT("Game thread/engine and strict fresh standalone opt-in UserDir required; real profiles untouched.")); return; }
        Jadebound::Persistence::JournalNamespacePaths Namespaces;
        if (!Jadebound::Persistence::BuildJournalNamespacePaths(false, TEXT("Traveler01"), Namespaces))
        { Test.AddError(TEXT("Exact standalone V1/V2/V3/V4 path authority required.")); return; }
        for (int32 Slot = 0; Slot < 2; ++Slot)
        {
            Paths[Slot] = Namespaces.Banks[0][Slot]; Paths[2 + Slot] = Namespaces.Banks[2][Slot];
            Paths[4 + Slot] = Namespaces.Banks[1][Slot]; Paths[6 + Slot] = Namespaces.ForwardV4[Slot];
        }
        for (const FString& Path : Paths)
            if (!FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Path), User))
            { Test.AddError(TEXT("All eight fixed Traveler01 journal paths must be under the owned effective UserDir.")); return; }
        Companion = FPaths::Combine(FPaths::GetPath(Paths[2]), TEXT("Jadebound_Traveler01_Progression.writer"));
        IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
        if (!FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Companion), User) || Jadebound::Persistence::HasJournalEntry(Files, Companion))
        { Test.AddError(TEXT("Fresh absent owned companion required; no reset/reuse.")); return; }
        for (const FString& Path : Paths)
            if (Jadebound::Persistence::HasJournalEntry(Files, Path)) { Test.AddError(TEXT("All eight journals must initially be absent; no reset/reuse.")); return; }
        HeroClass.Reset(LoadClass<ACharacter>(nullptr, TEXT("/Game/Jadebound/Tests/GlaiveCombatV006/BP_GlaiveHero_V006.BP_GlaiveHero_V006_C")));
        Glaive.Reset(LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Jadebound/OriginalWeapons/JadeGlaiveV001/SM_JadeGlaive_CM_V001.SM_JadeGlaive_CM_V001")));
        Spear.Reset(LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Jadebound/OriginalWeapons/AshwoodSpearV001/SM_AshwoodSpear_CM_V001.SM_AshwoodSpear_CM_V001")));
        if (!HeroClass || !Glaive || !Spear) { Test.AddError(TEXT("Actual preserved V006 Hero and original weapons required.")); return; }
        UWorld::InitializationValues Values;
        Values.InitializeScenes(true).CreatePhysicsScene(true).EnableTraceCollision(true).ShouldSimulatePhysics(false)
            .AllowAudioPlayback(false).RequiresHitProxies(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false).SetTransactional(false);
        const FString Name = FString::Printf(TEXT("StandaloneWriter_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
        World = UWorld::CreateWorld(EWorldType::Game, false, FName(*Name), nullptr, true, ERHIFeatureLevel::Num, &Values);
        if (!World) { Test.AddError(TEXT("Initialized standalone world creation failed.")); return; }
        WorldKeeper.Reset(World); GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        Instance.Reset(NewObject<UGameInstance>(GEngine)); World->SetGameInstance(Instance.Get());
        if (!Instance || !World->GetWorldSettings() || !World->GetPhysicsScene())
        { Test.AddError(TEXT("Actual world prerequisites unavailable.")); return; }
        World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
        if (!World->SetGameMode(FURL())) { Test.AddError(TEXT("Generic authority game mode creation failed.")); return; }
        World->InitializeActorsForPlay(FURL(), false);
        bValid = World->AreActorsInitialized() && !World->HasBegunPlay() && !World->GetGameViewport() &&
            World->GetNetMode() == NM_Standalone && World->GetAuthGameMode() && World->GetAuthGameMode()->HasAuthority();
        if (!bValid) Test.AddError(TEXT("Genuine initialized viewportless standalone authority world required."));
    }
    bool Live(const FEntry& Entry) const
    {
        return bValid && IsInGameThread() && GEngine && IsValid(World) && !World->bIsTearingDown &&
            GEngine->GetWorldContextFromWorld(World) && World->AreActorsInitialized() && !World->HasBegunPlay() &&
            !World->GetGameViewport() && IsValid(Entry.Model.Get()) && !Entry.Model->IsActorBeingDestroyed() &&
            IsValid(Entry.PC.Get()) && !Entry.PC->IsActorBeingDestroyed() && IsValid(Entry.Hero.Get()) &&
            !Entry.Hero->IsActorBeingDestroyed() && Entry.Model->GetWorld() == World && Entry.PC->GetWorld() == World &&
            Entry.Hero->GetWorld() == World && Entry.PC->GetPawn() == Entry.Hero.Get() &&
            Entry.Hero->GetController() == Entry.PC.Get() && Entry.Hero->HasActorBegunPlay() &&
            Entry.Model->GetOwner() == Entry.PC.Get() && Entry.Model->HasAuthority() && !Entry.Model->IsDevelopmentModel() && !Entry.bForeign;
    }
    FEntry* Create(bool bBeginModel = true)
    {
        if (!bValid) return nullptr;
        auto Entry = MakeUnique<FEntry>(); FEntry* Out = Entry.Get(); Entries.Add(MoveTemp(Entry));
        FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Out->PC.Reset(World->SpawnActor<APlayerController>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn));
        if (!Out->PC) return nullptr;
        Spawn.Owner = Out->PC.Get();
        Out->Model.Reset(World->SpawnActor<AJadeboundProgressionActor>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn));
        if (!Out->Model || Out->Model->HasActorBegunPlay()) return nullptr;
        Out->Model->AshwoodSpearMesh = Spear.Get(); // Actual map asset, no synthetic equipment.
        Out->Hero.Reset(World->SpawnActor<ACharacter>(HeroClass.Get(), Out->Model->MerchantPoint, FRotator::ZeroRotator, Spawn));
        if (!Out->Hero) return nullptr;
        Out->PC->Possess(Out->Hero.Get());
        if (!Out->PC->HasActorBegunPlay()) Out->PC->DispatchBeginPlay();
        if (!Out->Hero->HasActorBegunPlay()) Out->Hero->DispatchBeginPlay();
        Access::SetOpener(*Out->Model, [this, Out](const FString& Path) -> IFileHandle*
        {
            if (Path != Paths[2] && Path != Paths[3]) { Out->bForeign = true; return nullptr; }
            ++Out->Opens; Out->LastOpenedPath = Path;
            FImage Attempt;
            if (!Access::Capture(*Out->Model, Attempt) || !Attempt.bAdmitted || Attempt.WireBytes.IsEmpty() ||
                Attempt.WirePath != Path || Attempt.WireSlot != 1 - Attempt.Slot)
            { Out->bForeign = true; return nullptr; }
            Out->LastAttemptBytes = Attempt.WireBytes; Out->LastAttemptPath = Attempt.WirePath;
            Out->LastAttemptSlot = Attempt.WireSlot; Out->LastWrittenBytes.Reset();
            if (Out->bFailOpen) return nullptr;
            IFileHandle* Handle = FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*Path, false, false);
            if (Handle) ++Out->SuccessfulOpens;
            return Handle ? new FRecordedJournalHandle(Handle, Out->LastWrittenBytes) : nullptr;
        });
        if (bBeginModel) Out->Model->DispatchBeginPlay(); // Production acquisition BEFORE ReadSave.
        if (!Live(*Out) || (bBeginModel && !Out->Model->HasActorBegunPlay())) return nullptr;
        return Out;
    }
    bool Capture(const FEntry& Entry, FImage& Out) const
    {
        if (!Live(Entry) || !Access::Capture(*Entry.Model, Out)) return false;
        Out.Opens = Entry.Opens; Out.SuccessfulOpens = Entry.SuccessfulOpens;
        return true;
    }
    bool Unchanged(const FEntry& Entry, const FImage& Before) const
    { FImage After; return Capture(Entry, After) && SameImage(Before, After); }
    // Retained-only mode is used solely by the explicit physical V4 guard;
    // it separately inspects both V4 entries and their sentinel contents.
    bool Banks(FBanks& Out, bool bRetainedOnly = false) const
    {
        Out = FBanks{};
        IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
        const int32 Count = bRetainedOnly ? 6 : JournalPathCount;
        for (int32 I = 0; I < Count; ++I)
        {
            auto& Bank = Out.Bank[I]; Bank.bExists = Jadebound::Persistence::HasJournalEntry(Files, Paths[I]); Bank.Bytes.Reset();
            if (!Bank.bExists) continue;
            if (!Files.FileExists(*Paths[I]) || Files.DirectoryExists(*Paths[I])) return false;
            const int64 Size = Files.FileSize(*Paths[I]);
            if (Size <= 0 || Size > 1024 * 1024 + 32) return false;
            TUniquePtr<IFileHandle> File(Files.OpenRead(*Paths[I]));
            if (!File) return false;
            Bank.Bytes.SetNumUninitialized(static_cast<int32>(Size));
            if (!File->Read(Bank.Bytes.GetData(), Size)) return false;
        }
        return !Out.Bank[0].bExists && !Out.Bank[1].bExists && !Out.Bank[4].bExists && !Out.Bank[5].bExists &&
            !Out.Bank[6].bExists && !Out.Bank[7].bExists; // Fresh V3 never creates old/future banks.
    }
    bool BanksUnchanged(const FBanks& Before, bool bRetainedOnly = false) const
    { FBanks After; return Banks(After, bRetainedOnly) && SameBanks(Before, After); }
    bool CleanState(const FEntry& Entry, const Jadebound::Snapshot& Expected, int32 Slot) const
    {
        FImage Image; FJadeboundProgressionState Encoded; Jadebound::Snapshot Decoded;
        return Capture(Entry, Image) && Entry.Model->HasActorBegunPlay() && Entry.Model->IsMerchantInRange() &&
            Entry.Model->IsProgressionReady() && Image.bReady && Image.bInitialized && !Image.bBlocked && !Image.bUnsaved &&
            !Image.bEnding && !Image.bDevelopment && Image.Lease && !Image.DevelopmentLease && Image.Saved && !Image.Held &&
            Image.bAdmitted && Image.WireBytes.IsEmpty() && Image.WirePath.IsEmpty() && Image.WireSlot == INDEX_NONE &&
            Image.Failure == EJadeboundPersistenceFailureKind::None && Image.Deadline == 0.0 && Image.RetryStep == 0 &&
            Image.SaveId == TEXT("Traveler01") && Image.CommittedId == TEXT("Traveler01") && Image.Slot == Slot &&
            Image.Player == Entry.PC.Get() && Image.Hero == Entry.Hero.Get() && Image.Owner == Entry.PC.Get() &&
            Image.Pending.IsEmpty() && Image.CommittedDrops.IsEmpty() && SameCore(Image.Core, Expected) &&
            Jadebound::Persistence::EncodeStateV3(Expected, Encoded) && SameState(Encoded, Image.Reflected) &&
            SameState(Encoded, Image.Committed) && Jadebound::Persistence::DecodeStateV3(Image.Committed, Decoded) &&
            SameCore(Decoded, Expected);
    }
    bool Rejected(const FEntry& Entry) const
    {
        FImage Image; FJadeboundProgressionState Initial;
        return Capture(Entry, Image) && Entry.Model->HasActorBegunPlay() && !Entry.Model->IsProgressionReady() &&
            !Image.bReady && !Image.bInitialized && Image.bBlocked && !Image.bUnsaved && !Image.bEnding &&
            !Image.bDevelopment && !Image.Lease && !Image.DevelopmentLease && !Image.Saved && !Image.Held &&
            !Image.bAdmitted && Image.WireBytes.IsEmpty() && Image.WirePath.IsEmpty() && Image.WireSlot == INDEX_NONE &&
            Image.Failure == EJadeboundPersistenceFailureKind::RecoveryRequired && Image.Deadline == 0.0 && Image.RetryStep == 0 &&
            Image.SaveId == TEXT("Traveler01") && Image.Pending.IsEmpty() && Entry.Opens == 0 && Entry.SuccessfulOpens == 0 &&
            Image.Player == Entry.PC.Get() && Image.Hero == Entry.Hero.Get() && Image.Owner == Entry.PC.Get() &&
            SameCore(Image.Core, Jadebound::Snapshot{}) && Jadebound::Persistence::EncodeStateV3(Jadebound::Snapshot{}, Initial) &&
            SameState(Image.Reflected, Initial);
    }
    bool DestroyModel(FEntry& Entry)
    {
        if (!Live(Entry) || !Entry.Model->HasActorBegunPlay()) return false;
        AJadeboundProgressionActor* Model = Entry.Model.Get();
        const int32 Opens = Entry.Opens, SuccessfulOpens = Entry.SuccessfulOpens;
        return World->DestroyActor(Model) && Access::Ending(*Model) && !Access::Lease(*Model) && !Model->HasActorBegunPlay() &&
            Entry.Opens == Opens && Entry.SuccessfulOpens == SuccessfulOpens;
    }
    bool CompanionReleased() const
    {
        IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
        if (!Files.FileExists(*Companion) || Files.FileSize(*Companion) != 0) return false;
        TUniquePtr<IFileHandle> Probe(Files.OpenWrite(*Companion, true, false));
        return static_cast<bool>(Probe); // Probe closes here; does not write or truncate.
    }
    ~FScope()
    {
        if (!World) return;
        // Real teardown, including failed admissions. Strong actors survive
        // inspection and are retained until this scope's cleanup completes.
        for (const auto& Entry : Entries)
            if (IsValid(Entry->Model.Get()) && !Entry->Model->IsActorBeingDestroyed()) World->DestroyActor(Entry->Model.Get());
        for (const auto& Entry : Entries)
        {
            if (IsValid(Entry->PC.Get())) Entry->PC->UnPossess();
            if (IsValid(Entry->Hero.Get()) && !Entry->Hero->IsActorBeingDestroyed()) World->DestroyActor(Entry->Hero.Get());
            if (IsValid(Entry->PC.Get()) && !Entry->PC->IsActorBeingDestroyed()) World->DestroyActor(Entry->PC.Get());
        }
        UEngine* Engine = GEngine;
        if (!Engine || !Engine->GetWorldContextFromWorld(World))
        { Test.AddError(TEXT("Owned actors were destroyed; engine/context lost, world cleanup unqualified.")); return; }
        World->DestroyWorld(false); Engine->DestroyWorldContext(World); World = nullptr;
        Entries.Empty(); Instance.Reset(); WorldKeeper.Reset();
        // Journals/empty companion and attempt profile are deliberately retained.
    }
};

bool RefuseActions(FAutomationTestBase& Test, FScope& Scope, FEntry& Rejected, FEntry& Owner)
{
    FImage RejectedBefore, OwnerBefore; FBanks Before;
    if (!Test.TestTrue(TEXT("Whole rejected/owning model and all bank baselines available"),
        Scope.Capture(Rejected, RejectedBefore) && Scope.Capture(Owner, OwnerBefore) && Scope.Banks(Before))) return false;
    const bool Buy = Rejected.Model->BuyItemAtRevision(static_cast<int32>(Jadebound::ItemId::IronOre), 1, 0);
    const bool Unequip = Rejected.Model->UnequipWeaponAtRevision(0);
    const bool Save = Rejected.Model->SaveProgress();
    const bool Reload = Rejected.Model->ReloadProgress();
    return Test.TestTrue(TEXT("Rejected model cannot buy/unequip/save/reload, mutate either whole model, or touch any bank"),
        !Buy && !Unequip && !Save && !Reload && Scope.Rejected(Rejected) && Scope.Unchanged(Rejected, RejectedBefore) &&
        Scope.Unchanged(Owner, OwnerBefore) && Scope.BanksUnchanged(Before));
}
bool BuyAndAdvance(FAutomationTestBase& Test, FScope& Scope, FEntry& Entry, Jadebound::ItemId Id,
    int64 Revision, const Jadebound::Snapshot& Expected, int32 Slot)
{
    FImage Before, After; FBanks BanksBefore, BanksAfter;
    if (!Scope.Capture(Entry, Before) || !Scope.Banks(BanksBefore)) return Test.TestTrue(TEXT("Purchase baseline captured"), false);
    const bool Bought = Entry.Model->BuyItemAtRevision(static_cast<int32>(Id), 1, Revision);
    if (!Test.TestTrue(TEXT("Real purchase advances one exact state and writes one alternating bank under the retained lease"),
        Bought && Scope.CleanState(Entry, Expected, Slot) && Scope.Capture(Entry, After) && Scope.Banks(BanksAfter) &&
        After.Lease == Before.Lease && After.Opens == Before.Opens + 1 && After.SuccessfulOpens == Before.SuccessfulOpens + 1 &&
        After.Saved != Before.Saved && Slot == 1 - Before.Slot && Entry.LastOpenedPath == Scope.Paths[2 + Slot] &&
        !SameBank(BanksAfter.Bank[2 + Slot], BanksBefore.Bank[2 + Slot]))) return false;
    for (int32 I = 0; I < JournalPathCount; ++I)
        if (I != 2 + Slot && !Test.TestTrue(TEXT("Purchase preserves every other namespace/bank byte"), SameBank(BanksBefore.Bank[I], BanksAfter.Bank[I]))) return false;
    return true;
}
bool Reopened(FAutomationTestBase& Test, FScope& Scope, const FEntry& Entry,
    const Jadebound::Snapshot& Expected, int32 Slot, const FBanks& Before)
{
    return Test.TestTrue(TEXT("New real BeginPlay reacquires an existing companion and reloads the whole state without a bank writer"),
        Scope.CleanState(Entry, Expected, Slot) && Entry.Opens == 0 && Entry.SuccessfulOpens == 0 && Scope.BanksUnchanged(Before));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundStandaloneWriterLifecycleTest,
    "Jadebound.Progression.StandaloneWriterLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FJadeboundStandaloneWriterLifecycleTest::RunTest(const FString&)
{
    using namespace JadeboundStandaloneWriter;
    FScope Scope(*this); if (!Scope.bValid) return false;
    const Jadebound::Snapshot Initial;
    auto TorsoOnly = Initial; TorsoOnly.EquippedTorso = Jadebound::ItemId::WardenCoat;
    if (!TestFalse(TEXT("Whole-core comparison detects a torso-only current state difference"), SameCore(Initial, TorsoOnly))) return false;
    auto R1 = Initial; R1.Revision = 1; R1.Coins = 8; R1.Inventory[0] = {Jadebound::ItemId::Moonleaf, 1};
    auto R2 = R1; R2.Revision = 2; R2.Coins = 5; R2.Inventory[1] = {Jadebound::ItemId::IronOre, 1};
    auto R3 = R2; R3.Revision = 3; R3.Coins = 3; R3.Inventory[0].Count = 2;

    FEntry* A = Scope.Create(); FBanks InitialBanks;
    if (!TestTrue(TEXT("A admits through production BeginPlay: r0/10 coins/empty bag/Glaive/quest0/drop1, exactly fresh V3 A"),
        A && Scope.CleanState(*A, Initial, 0) && A->Opens == 1 && A->SuccessfulOpens == 1 &&
        Scope.Banks(InitialBanks) && InitialBanks.Bank[2].bExists && !InitialBanks.Bank[3].bExists &&
        FPlatformFileManager::Get().GetPlatformFile().FileExists(*Scope.Companion) &&
        FPlatformFileManager::Get().GetPlatformFile().FileSize(*Scope.Companion) == 0)) return false;
    FImage ABeforeB; if (!Scope.Capture(*A, ABeforeB)) return false;
    FEntry* B = Scope.Create();
    if (!TestTrue(TEXT("B's separate possessed Hero/model is refused before any bank write; A and all bank bytes preserved"),
        B && B->PC.Get() != A->PC.Get() && B->Hero.Get() != A->Hero.Get() && Scope.Rejected(*B) &&
        Scope.Unchanged(*A, ABeforeB) && Scope.BanksUnchanged(InitialBanks)) || !RefuseActions(*this, Scope, *B, *A)) return false;
    if (!BuyAndAdvance(*this, Scope, *A, Jadebound::ItemId::Moonleaf, 0, R1, 1) || !RefuseActions(*this, Scope, *B, *A)) return false;

    // A nonbegun authority actor has no failure flag masking the independent
    // ReadSave/WriteSave lease guards. No readiness or ownership bit is injected.
    FEntry* Probe = Scope.Create(false); FImage ProbeBefore, ABeforeProbe; FBanks BeforeProbe;
    if (!TestTrue(TEXT("Unadmitted real actor has no lease, blocked/unsaved flags, failure, committed save or held candidate"),
        Probe && !Probe->Model->HasActorBegunPlay() && Scope.Capture(*Probe, ProbeBefore) && !ProbeBefore.Lease &&
        !ProbeBefore.bBlocked && !ProbeBefore.bUnsaved && !ProbeBefore.bReady && !ProbeBefore.bInitialized &&
        !ProbeBefore.Saved && !ProbeBefore.Held && ProbeBefore.Failure == EJadeboundPersistenceFailureKind::None &&
        Scope.Capture(*A, ABeforeProbe) && Scope.Banks(BeforeProbe))) return false;
    if (!TestTrue(TEXT("Direct production ReadSave independently refuses unadmitted actor without any state/bank change"),
        !Access::Read(*Probe->Model) && Scope.Unchanged(*Probe, ProbeBefore) && Scope.Unchanged(*A, ABeforeProbe) &&
        Scope.BanksUnchanged(BeforeProbe))) return false;
    if (!TestTrue(TEXT("Direct production WriteSave independently refuses unadmitted actor before candidate/write mutation"),
        !Access::Write(*Probe->Model) && Scope.Unchanged(*Probe, ProbeBefore) && Scope.Unchanged(*A, ABeforeProbe) &&
        Scope.BanksUnchanged(BeforeProbe))) return false;

    FBanks R1Banks; FImage BBeforeRelease;
    if (!Scope.Banks(R1Banks) || !Scope.Capture(*B, BBeforeRelease)) return false;
    if (!TestTrue(TEXT("Actual DestroyActor/EndPlay releases A's original lease; empty companion remains reusable"),
        Scope.DestroyModel(*A) && Scope.CompanionReleased() && Scope.BanksUnchanged(R1Banks) &&
        Scope.Unchanged(*B, BBeforeRelease) && Scope.Rejected(*B))) return false;
    FEntry* C = Scope.Create();
    if (!C || !Reopened(*this, Scope, *C, R1, 1, R1Banks) || !RefuseActions(*this, Scope, *B, *C) ||
        !BuyAndAdvance(*this, Scope, *C, Jadebound::ItemId::IronOre, 1, R2, 0)) return false;
    FBanks R2Banks;
    if (!Scope.Banks(R2Banks) || !TestTrue(TEXT("Actual C EndPlay releases ownership and preserves r2 banks"),
        Scope.DestroyModel(*C) && Scope.CompanionReleased() && Scope.BanksUnchanged(R2Banks))) return false;
    FEntry* D = Scope.Create();
    if (!D || !Reopened(*this, Scope, *D, R2, 0, R2Banks)) return false;

    FImage BeforeFailure, Held; FJadeboundProgressionState EncodedR3;
    if (!Scope.Capture(*D, BeforeFailure) || !Jadebound::Persistence::EncodeStateV3(R3, EncodedR3)) return false;
    D->bFailOpen = true;
    const bool SavedPurchase = D->Model->BuyItemAtRevision(static_cast<int32>(Jadebound::ItemId::Moonleaf), 1, 2);
    if (!TestTrue(TEXT("Real bank-open failure holds one r3 and its original lease under a controlled future fixture deadline; committed r2 and disk survive"),
        !SavedPurchase && Access::SetFixtureFutureDeadline(*D->Model, BeforeFailure.Lease) &&
        Scope.Capture(*D, Held) && SameCore(Held.Core, R3) && SameState(Held.Reflected, EncodedR3) &&
        Held.Lease == BeforeFailure.Lease && Held.Lease && !Held.DevelopmentLease && Held.Held &&
        Held.CandidateId == TEXT("Traveler01") && SameState(Held.Candidate, EncodedR3) && Held.CandidateDrops.IsEmpty() &&
        Held.bAdmitted && !Held.WireBytes.IsEmpty() && Held.WirePath == Scope.Paths[3] && Held.WireSlot == 1 &&
        D->LastAttemptBytes == Held.WireBytes && D->LastAttemptPath == Held.WirePath &&
        D->LastAttemptSlot == Held.WireSlot && D->LastWrittenBytes.IsEmpty() &&
        Held.Saved == BeforeFailure.Saved && Held.CommittedId == BeforeFailure.CommittedId &&
        SameState(Held.Committed, BeforeFailure.Committed) && SameDrops(Held.CommittedDrops, BeforeFailure.CommittedDrops) &&
        SameDrops(Held.Pending, BeforeFailure.Pending) && Held.Slot == BeforeFailure.Slot &&
        Held.bReady && Held.bInitialized && Held.bBlocked && Held.bUnsaved && !Held.bEnding && !Held.bDevelopment &&
        Held.Failure == EJadeboundPersistenceFailureKind::Retryable && Held.RetryStep == 1 &&
        Held.Deadline > FPlatformTime::Seconds() && Held.Opens == BeforeFailure.Opens + 1 &&
        Held.SuccessfulOpens == BeforeFailure.SuccessfulOpens && D->LastOpenedPath == Scope.Paths[3] &&
        !D->Model->IsProgressionReady() && Scope.BanksUnchanged(R2Banks))) return false;
    if (!TestTrue(TEXT("Real RetryHeldSave respects the controlled future fixture deadline without a writer attempt or candidate change"),
        !Access::Retry(*D->Model) && Scope.Unchanged(*D, Held) && Scope.BanksUnchanged(R2Banks))) return false;
    FEntry* E = Scope.Create();
    if (!TestTrue(TEXT("Competing real BeginPlay is refused while D retains a held candidate; all held fields and bytes unchanged"),
        E && Scope.Rejected(*E) && Scope.Unchanged(*D, Held) && Scope.BanksUnchanged(R2Banks)) ||
        !RefuseActions(*this, Scope, *E, *D)) return false;

    D->bFailOpen = false; Access::ExpireFixtureDeadline(*D->Model);
    const bool Retried = Access::Retry(*D->Model); FImage AfterRetry; FBanks R3Banks;
    if (!TestTrue(TEXT("Expired fixture clock permits one write of the identical held r3; no repeated purchase, cost or revision"),
        Retried && Scope.CleanState(*D, R3, 1) && Scope.Capture(*D, AfterRetry) && Scope.Banks(R3Banks) &&
        AfterRetry.Lease == Held.Lease && AfterRetry.Saved != Held.Saved && !AfterRetry.Held &&
        SameState(AfterRetry.Committed, Held.Candidate) && AfterRetry.CommittedId == Held.CandidateId &&
        SameDrops(AfterRetry.CommittedDrops, Held.CandidateDrops) && AfterRetry.Opens == Held.Opens + 1 &&
        AfterRetry.SuccessfulOpens == Held.SuccessfulOpens + 1 && D->LastOpenedPath == Scope.Paths[3] &&
        D->LastAttemptBytes == Held.WireBytes && D->LastAttemptPath == Held.WirePath && D->LastAttemptSlot == Held.WireSlot &&
        D->LastWrittenBytes == Held.WireBytes && R3Banks.Bank[3].Bytes == Held.WireBytes &&
        !SameBank(R3Banks.Bank[3], R2Banks.Bank[3]) && SameBank(R3Banks.Bank[2], R2Banks.Bank[2]) &&
        SameBank(R3Banks.Bank[0], R2Banks.Bank[0]) && SameBank(R3Banks.Bank[1], R2Banks.Bank[1]))) return false;
    for (int32 I = 0; I < JournalPathCount; ++I)
        if (I != 3 && !TestTrue(TEXT("Held retry preserves every other retained/future namespace entry"), SameBank(R3Banks.Bank[I], R2Banks.Bank[I]))) return false;
    if (!TestTrue(TEXT("A repeated held retry is a complete no-op after verified commit"),
        !Access::Retry(*D->Model) && Scope.Unchanged(*D, AfterRetry) && Scope.BanksUnchanged(R3Banks)) ||
        !RefuseActions(*this, Scope, *E, *D)) return false;
    if (!TestTrue(TEXT("Actual D EndPlay releases the lease without touching saved r3"),
        Scope.DestroyModel(*D) && Scope.CompanionReleased() && Scope.BanksUnchanged(R3Banks))) return false;
    FEntry* F = Scope.Create();
    if (!F || !Reopened(*this, Scope, *F, R3, 1, R3Banks) || !RefuseActions(*this, Scope, *E, *F)) return false;
    if (!TestTrue(TEXT("Final actual EndPlay releases F while every evidence bank is retained"),
        Scope.DestroyModel(*F) && Scope.CompanionReleased() && Scope.BanksUnchanged(R3Banks))) return false;
    AddInfo(TEXT("Production standalone lifecycle, real Windows sharing, exact retained bank bytes and controlled fixture-clock retry guard/ownership only; no viewport/input, independent client, network, gameplay or elapsed production-backoff measurement."));
    return true;
}
// Historical guarded V2 registration intentionally remains in the preserved old build only.

// Separately authored current V3 startup guard. This is not the old V2 rollback test.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundV3StandaloneForwardStartupTest,
    "Jadebound.Persistence.V3StandaloneForwardStartup", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FJadeboundV3StandaloneForwardStartupTest::RunTest(const FString&)
{
    using namespace JadeboundStandaloneWriter;
    if (!FParse::Param(FCommandLine::Get(), TEXT("JadeboundV3StandaloneWriterTests")))
    { AddError(TEXT("Explicit Source157 current V3 opt-in and a fresh UserDir are required.")); return false; }
    FScope Scope(*this);
    if (!Scope.bValid) return false;
    FEntry* Original = Scope.Create();
    if (!Original || !Scope.CleanState(*Original, Jadebound::Snapshot{}, 0)) return false;
    auto Expected = Jadebound::Snapshot{};
    Expected.Revision = 1; Expected.Coins = 8; Expected.Inventory[0] = {Jadebound::ItemId::Moonleaf, 1};
    if (!BuyAndAdvance(*this, Scope, *Original, Jadebound::ItemId::Moonleaf, 0, Expected, 1)) return false;
    FBanks Before;
    if (!Scope.Banks(Before) || !Scope.DestroyModel(*Original) || !Scope.CompanionReleased()) return false;
    const FString V4[2] = {Scope.Paths[6], Scope.Paths[7]};
    for (const FString& Path : V4)
        if (!FPaths::IsSamePath(FPaths::GetPath(Path), FPaths::GetPath(Scope.Paths[2]))) return false;
    IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
    if (Files.FileExists(*V4[0]) || Files.DirectoryExists(*V4[0]) ||
        Files.FileExists(*V4[1]) || Files.DirectoryExists(*V4[1]) || !Files.CreateDirectory(*V4[0])) return false;
    const FString SentinelPath = FPaths::Combine(V4[0], TEXT("keep-evidence.bin"));
    const TArray<uint8> Sentinel = {0x53, 0x31, 0x31, 0x38, 0x4b, 0x45, 0x45, 0x50};
    {
        TUniquePtr<IFileHandle> Handle(Files.OpenWrite(*SentinelPath, false, false));
        if (!Handle || !Handle->Write(Sentinel.GetData(), Sentinel.Num()) || !Handle->Flush(true)) return false;
    }
    {
        TUniquePtr<IFileHandle> Handle(Files.OpenWrite(*V4[1], false, false));
        if (!Handle || !Handle->Flush(true)) return false;
    }
    const auto ForwardUnchanged = [&]() -> bool
    {
        if (!Files.DirectoryExists(*V4[0]) || Files.FileExists(*V4[0]) ||
            !Files.FileExists(*V4[1]) || Files.DirectoryExists(*V4[1]) || Files.FileSize(*V4[1]) != 0 ||
            Files.FileSize(*SentinelPath) != Sentinel.Num()) return false;
        TUniquePtr<IFileHandle> Reader(Files.OpenRead(*SentinelPath));
        TArray<uint8> Bytes; Bytes.SetNumUninitialized(Sentinel.Num());
        return Reader && Reader->Read(Bytes.GetData(), Bytes.Num()) && Bytes == Sentinel && Scope.BanksUnchanged(Before, true);
    };
    for (int32 Reopen = 0; Reopen < 2; ++Reopen)
    {
        FEntry* Guarded = Scope.Create(); // Production BeginPlay acquires then calls guarded ReadSave.
        FImage Image;
        if (!Guarded || !Scope.Capture(*Guarded, Image) ||
            !TestTrue(TEXT("Production BeginPlay refuses with original lease retained and no current V3 state adoption"),
                Guarded->Model->HasActorBegunPlay() && !Guarded->Model->IsProgressionReady() &&
                !Image.bReady && !Image.bInitialized && Image.bBlocked && !Image.bUnsaved &&
                Image.Failure == EJadeboundPersistenceFailureKind::RecoveryRequired &&
                Image.Lease && !Image.DevelopmentLease && !Image.Saved && !Image.Held &&
                !Image.bAdmitted && Image.WireBytes.IsEmpty() && Image.WirePath.IsEmpty() && Image.WireSlot == INDEX_NONE &&
                SameCore(Image.Core, Jadebound::Snapshot{}) && Guarded->Opens == 0 && ForwardUnchanged())) return false;
        FEntry* Competing = Scope.Create();
        if (!Competing || !TestTrue(TEXT("Second production startup still excluded by real writer ownership"),
            Scope.Rejected(*Competing) && Scope.Unchanged(*Guarded, Image) && ForwardUnchanged())) return false;
        TestFalse(TEXT("Guarded actor cannot purchase from retained V3 history"), Guarded->Model->BuyItemAtRevision(3, 1, 0));
        TestFalse(TEXT("Guarded actor cannot write a manual replacement"), Guarded->Model->SaveProgress());
        TestFalse(TEXT("Guarded actor cannot reload retained V3 banks"), Guarded->Model->ReloadProgress());
        if (!TestTrue(TEXT("Refusals preserve unready model, retained V3 bytes and both forward entries"),
            Scope.Unchanged(*Guarded, Image) && ForwardUnchanged())) return false;
        if (!TestTrue(TEXT("Actual EndPlay releases only owned lease; repeated startup still refuses"),
            Scope.DestroyModel(*Guarded) && Scope.CompanionReleased() && ForwardUnchanged())) return false;
    }
    AddInfo(TEXT("Viewportless production BeginPlay/EndPlay, V006 pawn, current V3 refusing physical V4 directory/zero-file, preserved V1/V2/V3 entries and Windows sharing. This is not old V2 rollback, a process cold restart, physical V4 decoding, gameplay or MMO proof."));
    return true;
}


#endif // WITH_DEV_AUTOMATION_TESTS && PLATFORM_WINDOWS
