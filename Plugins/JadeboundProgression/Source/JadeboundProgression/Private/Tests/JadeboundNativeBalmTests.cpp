#include "JadeboundProgressionActor.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "JadeboundDevelopmentGameMode.h"
#include "JadeboundNativeRaider.h"
#include "Combat/PlayerLifeRules.h"
#include "Domain/ProgressionCore.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
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

// Automation-only trusted registry assembly, not login, a public award API or RPC transport.
struct FJadeboundNativeBalmTestAccess
{
    static bool Attach(AJadeboundDevelopmentGameMode& Mode, AJadeboundNativeRaider& Enemy,
        AJadeboundProgressionActor& Model, APlayerController& Player, ACharacter& Pawn,
        UStaticMesh* Mesh, const FString& Id, bool Initial, FString& Error)
    {
        if (!Model.ConfigureDevelopmentOwner(&Player, Id, Mesh))
        { Error = TEXT("Canonical isolated Balm owner/writer admission refused."); return false; }
        Model.Hero = &Pawn;
        Model.bReady = Model.ReadSave(Initial);
        Model.bServerModelInitialized = Model.bReady;
        if (!Model.bReady)
        { Error = TEXT("Real isolated Balm journal initialization/load failed."); return false; }
        Model.bRecipientCombatEnabled = true;
        Mode.bEnableRecipientCombat = true;
        Mode.RecipientRaider = &Enemy;
        Mode.Models.Add(Id, &Model);
        Mode.ActiveControllers.Add(&Player, Id);
        Model.RefreshState();
        Model.InitializeNativePlayerLife();
        Model.MerchantPoint = Pawn.GetActorLocation();
        Model.MerchantRange = 200.f;
        Model.bAuthorityMenuOpen = true;
        if (!Mode.OwnsNativeCombatant(&Model, true) || !Model.AuthorityReady())
        { Error = TEXT("Initialized Balm fixture is not a ready reciprocally owned native combatant."); return false; }
        return true;
    }
    static const Jadebound::Snapshot& Core(const AJadeboundProgressionActor& Model) { return Model.Core->GetSnapshot(); }
    static const Jadebound::Combat::PlayerLife::State& Life(const AJadeboundProgressionActor& Model) { return *Model.NativeLifeState; }
    static bool Use(AJadeboundProgressionActor& Model, int32 Slot, int32 Item, int64 Revision, uint64 Generation)
    { return Model.UseNativeOwnedItem(Slot, Item, Revision, Generation); }
    static void Request(AJadeboundProgressionActor& Model, int32 Slot, int32 Item, int64 Revision, uint64 Generation)
    { Model.ServerUseNativeItem_Implementation(Slot, Item, Revision, Generation); }
    static void Strike(AJadeboundNativeRaider& Enemy) { Enemy.TickNativeStrike(); }
    static bool Windup(const AJadeboundNativeRaider& Enemy) { return Enemy.Strike.bWindup; }
    static bool Revive(AJadeboundProgressionActor& Model, uint64 Generation) { return Model.RequestNativeRevive(Generation); }
    static void OpenPack(AJadeboundProgressionActor& Model) { Model.bAuthorityMenuOpen = true; }
    static void SetOpener(AJadeboundProgressionActor& Model, TFunction<IFileHandle*(const FString&)> Opener)
    { Model.AutomationJournalOpener = MoveTemp(Opener); }
    static FString Journal(const AJadeboundProgressionActor& Model, int32 Slot) { return Model.GetJournalPath(Slot); }
    static bool Retry(AJadeboundProgressionActor& Model) { return Model.RetryHeldSave(false); }
    static void FutureRetry(AJadeboundProgressionActor& Model) { Model.PersistenceRetryAt = FPlatformTime::Seconds() + 60.; }
    static void ExpireRetry(AJadeboundProgressionActor& Model) { Model.PersistenceRetryAt = FPlatformTime::Seconds() - 1.; }
    static const UJadeboundCharacterSave* Held(const AJadeboundProgressionActor& Model) { return Model.HeldSaveCandidate.Get(); }
    static const UJadeboundCharacterSave* Saved(const AJadeboundProgressionActor& Model) { return Model.LastCommittedSave.Get(); }
    static bool Blocked(const AJadeboundProgressionActor& Model) { return Model.bPersistenceBlocked; }
    static bool Unsaved(const AJadeboundProgressionActor& Model) { return Model.bUnsavedCandidate; }
    static int32 BankSlot(const AJadeboundProgressionActor& Model) { return Model.LastSaveSlot; }
    static double Deadline(const AJadeboundProgressionActor& Model) { return Model.PersistenceRetryAt; }
    static uint8 RetryStep(const AJadeboundProgressionActor& Model) { return Model.PersistenceRetryStep; }
    static int64 Admissions(const AJadeboundProgressionActor& Model) { return Model.BalmUseAdmissions; }
    static void Retire(AJadeboundProgressionActor& Model, AJadeboundDevelopmentGameMode& Mode, APlayerController& Player, const FString& Id)
    {
        Model.bPersistenceEndingPlay = true;
        Model.AutomationJournalOpener = nullptr;
        Model.DevelopmentWriterLease.Reset();
        Mode.Models.Remove(Id);
        Mode.ActiveControllers.Remove(&Player);
    }
};

namespace JadeboundNativeBalmTests
{
using Access = FJadeboundNativeBalmTestAccess;
using LifeState = Jadebound::Combat::PlayerLife::State;
bool SameCore(const Jadebound::Snapshot& A, const Jadebound::Snapshot& B)
{
    if (A.Version != B.Version || A.Revision != B.Revision || A.Coins != B.Coins || A.Quest != B.Quest ||
        A.EquippedWeapon != B.EquippedWeapon || A.NextDropId != B.NextDropId || A.QuestKillDropId != B.QuestKillDropId ||
        A.UnprocessedDropIds != B.UnprocessedDropIds) return false;
    for (uint32 I = 0; I < Jadebound::InventorySlotCount; ++I)
        if (A.Inventory[I].Id != B.Inventory[I].Id || A.Inventory[I].Count != B.Inventory[I].Count) return false;
    return true;
}
bool SameLife(const LifeState& A, const LifeState& B)
{
    return A.Generation == B.Generation && A.Health == B.Health && A.DeadAt == B.DeadAt &&
        A.ReviveReadyAt == B.ReviveReadyAt && A.LastStrike == B.LastStrike && A.LastObservation == B.LastObservation;
}
bool SameState(const FJadeboundProgressionState& A, const FJadeboundProgressionState& B)
{ return FJadeboundProgressionState::StaticStruct()->CompareScriptStruct(&A, &B, 0); }
struct Bank { bool Exists = false; TArray<uint8> Bytes; };
bool ReadBank(const FString& Path, Bank& Out)
{
    auto& Files = FPlatformFileManager::Get().GetPlatformFile();
    Out.Exists = Files.FileExists(*Path);
    Out.Bytes.Reset();
    if (!Out.Exists) return true;
    const int64 Size = Files.FileSize(*Path);
    if (Size <= 0 || Size > 1024 * 1024 + 32) return false;
    TUniquePtr<IFileHandle> File(Files.OpenRead(*Path));
    if (!File) return false;
    Out.Bytes.SetNumUninitialized(static_cast<int32>(Size));
    return File->Read(Out.Bytes.GetData(), Size);
}
bool SameBank(const Bank& A, const Bank& B) { return A.Exists == B.Exists && A.Bytes == B.Bytes; }
struct Image
{
    Jadebound::Snapshot Core;
    LifeState Life;
    FJadeboundProgressionState State, Committed, HeldState;
    const UJadeboundCharacterSave* Saved = nullptr;
    const UJadeboundCharacterSave* Held = nullptr;
    Bank Banks[2];
    int32 Slot = -1;
    int32 Opens = 0;
    int64 Admissions = 0;
    bool Blocked = false, Unsaved = false;
    double Deadline = 0;
    uint8 RetryStep = 0;
};
bool Capture(const AJadeboundProgressionActor& Model, int32 Opens, Image& Out)
{
    Out.Core = Access::Core(Model); Out.Life = Access::Life(Model); Out.State = Model.GetProgressionState();
    Out.Saved = Access::Saved(Model); Out.Held = Access::Held(Model);
    if (!Out.Saved) return false;
    Out.Committed = Out.Saved->State;
    Out.HeldState = Out.Held ? Out.Held->State : FJadeboundProgressionState();
    Out.Slot = Access::BankSlot(Model); Out.Opens = Opens; Out.Admissions = Access::Admissions(Model);
    Out.Blocked = Access::Blocked(Model); Out.Unsaved = Access::Unsaved(Model);
    Out.Deadline = Access::Deadline(Model); Out.RetryStep = Access::RetryStep(Model);
    return ReadBank(Access::Journal(Model, 0), Out.Banks[0]) && ReadBank(Access::Journal(Model, 1), Out.Banks[1]);
}
bool SameImage(const Image& A, const Image& B, bool IncludeLife = true)
{
    return SameCore(A.Core, B.Core) && (!IncludeLife || SameLife(A.Life, B.Life)) && SameState(A.State, B.State) &&
        A.Saved == B.Saved && A.Held == B.Held && SameState(A.Committed, B.Committed) && SameState(A.HeldState, B.HeldState) &&
        A.Slot == B.Slot && A.Opens == B.Opens && A.Admissions == B.Admissions && A.Blocked == B.Blocked && A.Unsaved == B.Unsaved &&
        A.Deadline == B.Deadline && A.RetryStep == B.RetryStep && SameBank(A.Banks[0], B.Banks[0]) && SameBank(A.Banks[1], B.Banks[1]);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundNativeBalmTest, "Jadebound.Progression.NativeBalmHeldSave",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundNativeBalmTest::RunTest(const FString& Parameters)
{
    using namespace JadeboundNativeBalmTests;
    FString Argument;
    if (!FParse::Param(FCommandLine::Get(), TEXT("JadeboundNativeBalmTests")) ||
        !FParse::Value(FCommandLine::Get(), TEXT("UserDir="), Argument) || FPaths::IsRelative(Argument))
    { AddError(TEXT("NativeBalm opt-in and explicit isolated absolute UserDir required.")); return false; }
    FPaths::NormalizeDirectoryName(Argument);
    FString Effective = FPaths::ProjectUserDir(); FPaths::NormalizeDirectoryName(Effective);
    if (!FPaths::IsSamePath(Argument, Effective) || FPaths::GetCleanFilename(Argument) != TEXT("UserDir-v001") ||
        (FPaths::GetCleanFilename(FPaths::GetPath(Argument)) != TEXT("Jadebound-P102-A020") &&
         FPaths::GetCleanFilename(FPaths::GetPath(Argument)) != TEXT("Jadebound-P102-A021")) ||
        !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()), Argument))
    { AddError(TEXT("Fresh admitted native Balm namespace required; no previous fixture or real character save access.")); return false; }
    if (!GEngine || !GEngine->WorldSettingsClass || !IsInGameThread())
    { AddError(TEXT("NativeBalm requires engine settings and the game thread.")); return false; }
    UWorld::InitializationValues Values;
    Values.InitializeScenes(true).AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(true)
        .EnableTraceCollision(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false)
        .CreateFXSystem(false).SetTransactional(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
        FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits)), nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!World) { AddError(TEXT("Isolated native Balm world creation failed.")); return false; }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
    World->SetGameInstance(Instance.Get());
    struct Cleanup
    {
        UWorld* World;
        TArray<TWeakObjectPtr<AJadeboundProgressionActor>> Models;
        ~Cleanup()
        {
            // Opener lambdas capture fixture locals. Unbind before world teardown,
            // including early failures, rather than exposing expired references.
            for (const auto& Model : Models) if (Model.IsValid()) Access::SetOpener(*Model.Get(), nullptr);
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
    } CleanupScope{World, {}};
    World->GetWorldSettings()->DefaultGameMode = AJadeboundDevelopmentGameMode::StaticClass();
    if (!World->SetGameMode(FURL())) { AddError(TEXT("Isolated Balm native game mode creation failed.")); return false; }
    auto* Mode = Cast<AJadeboundDevelopmentGameMode>(World->GetAuthGameMode());
    FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Enemy = World->SpawnActor<AJadeboundNativeRaider>(FVector(0,0,100), FRotator::ZeroRotator, Spawn);
    UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Jadebound/OriginalWeapons/JadeGlaiveV001/SM_JadeGlaive_CM_V001.SM_JadeGlaive_CM_V001"));
    if (!Mode || !Enemy || !Mesh) { AddError(TEXT("Original glaive and native mode/raider required.")); return false; }
    AJadeboundProgressionActor* Models[2] = {};
    APlayerController* Players[2] = {};
    ACharacter* Pawns[2] = {};
    FString Ids[2], Paths[2][2];
    int32 Opens[2] = {};
    bool ForeignPath = false;
    for (int32 I=0; I<2; ++I)
    {
        Players[I] = World->SpawnActor<APlayerController>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
        Pawns[I] = World->SpawnActor<ACharacter>(FVector(100 + I*400,0,100), FRotator::ZeroRotator, Spawn);
        Models[I] = World->SpawnActor<AJadeboundProgressionActor>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
        if (!Players[I] || !Pawns[I] || !Models[I]) { AddError(TEXT("Native Balm fixture actor creation failed.")); return false; }
        CleanupScope.Models.Add(Models[I]);
        Players[I]->SetPlayer(NewObject<ULocalPlayer>(GEngine));
        if (!Players[I]->IsLocalController()) { AddError(TEXT("Engine-owned local fixture player required.")); return false; }
        Pawns[I]->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        Players[I]->Possess(Pawns[I]);
        Ids[I] = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
        FString Error;
        if (!Access::Attach(*Mode, *Enemy, *Models[I], *Players[I], *Pawns[I], Mesh, Ids[I], true, Error))
        { AddError(Error); return false; }
        for (int32 BankIndex=0; BankIndex<2; ++BankIndex)
        {
            Paths[I][BankIndex] = Access::Journal(*Models[I], BankIndex);
            if (Paths[I][BankIndex].IsEmpty() || !FPaths::IsUnderDirectory(Paths[I][BankIndex], Argument))
            { AddError(TEXT("Exact native fixture journal must remain beneath the isolated admitted fixture.")); return false; }
        }
    }
    auto InstallWriter = [&](int32 I)
    {
        Access::SetOpener(*Models[I], [&, I](const FString& Path) -> IFileHandle*
        {
            if (Path != Paths[I][0] && Path != Paths[I][1]) { ForeignPath = true; return nullptr; }
            ++Opens[I];
            return FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*Path, false, false);
        });
    };
    auto Advance = [&](double Seconds)
    {
        const double Before = World->GetTimeSeconds();
        const int32 Steps = FMath::FloorToInt(Seconds / .1);
        for (int32 Step=0; Step<Steps; ++Step) World->Tick(LEVELTICK_All, .1f);
        const double Remainder = Seconds - Steps * .1;
        if (Remainder > .000001) World->Tick(LEVELTICK_All, static_cast<float>(Remainder));
        return TestTrue(TEXT("Small native frames advance measured server clock, without rewriting deadlines"),
            FMath::Abs(World->GetTimeSeconds() - Before - Seconds) < .0001);
    };
    auto Hit = [&](int32 ExpectedHealth)
    {
        if (!Advance(1.7)) return false;
        Access::Strike(*Enemy);
        if (!TestTrue(TEXT("Real server raider recovery starts a fresh windup"), Access::Windup(*Enemy)) || !Advance(.75)) return false;
        Access::Strike(*Enemy);
        return TestEqual(TEXT("Real native raider impact changes expected target HP"), Access::Life(*Models[0]).Health, ExpectedHealth);
    };
    for (int32 I=0; I<2; ++I)
    {
        InstallWriter(I);
        if (!TestTrue(TEXT("Real merchant purchase admits three original Balm"), Models[I]->BuyItemAtRevision(7,3,0)) ||
            !TestTrue(TEXT("Catalog purchase spends nine coins and saves one revision"), Access::Core(*Models[I]).Coins == 1 &&
                Access::Core(*Models[I]).Revision == 1 && Access::Core(*Models[I]).Inventory[0].Id == Jadebound::ItemId::Balm &&
                Access::Core(*Models[I]).Inventory[0].Count == 3 && Opens[I] == 1)) return false;
    }
    Image OtherBaseline;
    if (!Capture(*Models[1], Opens[1], OtherBaseline)) { AddError(TEXT("Other owner baseline unavailable.")); return false; }
    auto OtherUnchanged = [&]() { Image After; return Capture(*Models[1], Opens[1], After) && SameImage(OtherBaseline, After); };
    auto Refuse = [&](int32 Slot, int32 Item, int64 Revision, uint64 Generation, const TCHAR* Label)
    {
        Image Before, After;
        if (!Capture(*Models[0], Opens[0], Before)) { AddError(TEXT("Native use refusal baseline unavailable.")); return false; }
        const bool Accepted = Access::Use(*Models[0], Slot, Item, Revision, Generation);
        return TestFalse(Label, Accepted) && TestTrue(TEXT("Refusal retains full life, inventory, held metadata and actual banks without an opener"),
            Capture(*Models[0], Opens[0], After) && SameImage(Before, After) && OtherUnchanged());
    };
    const uint64 Generation = Access::Life(*Models[0]).Generation;
    Image BeforeRpcRefusal, AfterRpcRefusal;
    const int64 RefusedSequence = Models[0]->GetLastActionReceipt().Sequence;
    if (!Capture(*Models[0], Opens[0], BeforeRpcRefusal)) { AddError(TEXT("RPC refusal baseline unavailable.")); return false; }
    Access::Request(*Models[0],0,7,1,Generation+1);
    const auto RefusedReceipt = Models[0]->GetLastActionReceipt();
    if (!TestTrue(TEXT("Direct server-handler refusal issues exactly one matching Use acknowledgment"),
        RefusedReceipt.Sequence == RefusedSequence+1 && RefusedReceipt.Action == TEXT("Use") && !RefusedReceipt.bSucceeded &&
        RefusedReceipt.ObservedRevision == 1 && RefusedReceipt.LastVerifiedJournalRevision == 1 &&
        !RefusedReceipt.bHeldCandidate && !RefusedReceipt.bPersistenceBlocked &&
        Capture(*Models[0], Opens[0], AfterRpcRefusal) && SameImage(BeforeRpcRefusal, AfterRpcRefusal))) return false;
    if (!Refuse(0,7,1,Generation,TEXT("Full HP preserves owned Balm")) || !Hit(80) || !Hit(60)) return false;
    if (!Refuse(-1,7,1,Generation,TEXT("Negative slot refuses")) || !Refuse(12,7,1,Generation,TEXT("Out-of-range slot refuses")) ||
        !Refuse(1,7,1,Generation,TEXT("Empty stale slot refuses")) || !Refuse(0,4,1,Generation,TEXT("Wrong item identity refuses")) ||
        !Refuse(0,7,0,Generation,TEXT("Stale inventory revision refuses")) || !Refuse(0,7,1,Generation+1,TEXT("Wrong life generation refuses"))) return false;
    Models[0]->SetOwner(Players[1]);
    const bool WrongOwnerRefused = Refuse(0,7,1,Generation,TEXT("Wrong network owner refuses"));
    Models[0]->SetOwner(Players[0]);
    if (!WrongOwnerRefused) return false;
    Image BeforeHeld;
    if (!Capture(*Models[0], Opens[0], BeforeHeld)) { AddError(TEXT("Pre-hold native journal baseline unavailable.")); return false; }
    bool NestedTransactionsRefused = false;
    Access::SetOpener(*Models[0], [&](const FString& Path) -> IFileHandle*
    {
        if (Path != Paths[0][0] && Path != Paths[0][1]) { ForeignPath = true; return nullptr; }
        ++Opens[0];
        const int32 BeforeNestedOpens = Opens[0];
        const auto BeforeNestedCore = Access::Core(*Models[0]);
        const auto BeforeNestedLife = Access::Life(*Models[0]);
        const int64 CurrentRevision = static_cast<int64>(BeforeNestedCore.Revision);
        const bool Bought = Models[0]->BuyItemAtRevision(4,1,CurrentRevision);
        const bool Sold = Models[0]->SellItem(0,1,CurrentRevision);
        const bool Unequipped = Models[0]->UnequipWeaponAtRevision(CurrentRevision);
        // Sell and Unequip would otherwise be affordable/valid; Buy alone has
        // insufficient coins and therefore cannot establish the callback gate.
        NestedTransactionsRefused = !Bought && !Sold && !Unequipped && Opens[0] == BeforeNestedOpens &&
            SameCore(BeforeNestedCore, Access::Core(*Models[0])) && SameLife(BeforeNestedLife, Access::Life(*Models[0]));
        return nullptr; // Controlled failure of the real WriteSave opener, before any file opens.
    });
    if (!TestFalse(TEXT("Failed real journal writer reports no durable-use success"), Access::Use(*Models[0],0,7,1,Generation))) return false;
    Image Held;
    if (!TestTrue(TEXT("One consumed/healed candidate is retained, old verified r1 and both bank bytes unchanged"),
        Capture(*Models[0], Opens[0], Held) && Held.Core.Revision == 2 && Held.Core.Inventory[0].Count == 2 && Held.Core.Coins == 1 &&
        Held.Life.Health == 85 && Held.Life.Generation == Generation && Held.Held && Held.HeldState.Revision == 2 && NestedTransactionsRefused &&
        Held.Blocked && Held.Unsaved && Held.Admissions == BeforeHeld.Admissions+1 && Held.Opens == BeforeHeld.Opens+1 &&
        Held.Saved == BeforeHeld.Saved && Held.Committed.Revision == 1 && Held.Slot == BeforeHeld.Slot &&
        SameBank(Held.Banks[0], BeforeHeld.Banks[0]) && SameBank(Held.Banks[1], BeforeHeld.Banks[1]) && OtherUnchanged())) return false;
    if (!Refuse(0,7,1,Generation,TEXT("Pre-use token cannot replay during hold")) ||
        !Refuse(0,7,2,Generation,TEXT("Even current held token cannot consume or heal again")) || !Hit(65)) return false;
    Image DamagedHeld;
    if (!TestTrue(TEXT("Held journal does not grant damage immunity or change the consumed candidate"),
        Capture(*Models[0], Opens[0], DamagedHeld) && DamagedHeld.Life.Health == 65 && SameImage(Held, DamagedHeld, false))) return false;
    const int64 HeldSequence = Models[0]->GetLastActionReceipt().Sequence;
    Access::Request(*Models[0],0,7,2,Generation);
    const auto HeldReceipt = Models[0]->GetLastActionReceipt();
    Image AfterHeldRpc;
    if (!TestTrue(TEXT("Held RPC acknowledges refusal and the original verified/consumed revisions without IO or healing"),
        HeldReceipt.Sequence == HeldSequence+1 && HeldReceipt.Action == TEXT("Use") && !HeldReceipt.bSucceeded &&
        HeldReceipt.ObservedRevision == 2 && HeldReceipt.LastVerifiedJournalRevision == 1 &&
        HeldReceipt.bHeldCandidate && HeldReceipt.bPersistenceBlocked &&
        Capture(*Models[0], Opens[0], AfterHeldRpc) && SameImage(DamagedHeld, AfterHeldRpc))) return false;
    InstallWriter(0);
    Access::FutureRetry(*Models[0]); // Explicit fixture deadline, not elapsed production-backoff proof.
    Image FutureHeld, AfterEarlyRetry;
    if (!Capture(*Models[0], Opens[0], FutureHeld) || !TestFalse(TEXT("Retry before its controlled deadline does no IO"), Access::Retry(*Models[0])) ||
        !TestTrue(TEXT("Early retry preserves damaged life and every held field"), Capture(*Models[0], Opens[0], AfterEarlyRetry) && SameImage(FutureHeld, AfterEarlyRetry))) return false;
    Access::ExpireRetry(*Models[0]);
    Image RetryBefore, RetryAfter;
    if (!Capture(*Models[0], Opens[0], RetryBefore) || !TestTrue(TEXT("Real retry saves the identical consumed r2"), Access::Retry(*Models[0])) ||
        !TestTrue(TEXT("Retry leaves HP65 and all life clocks/strike/generation unchanged; one verified bank flips"),
            Capture(*Models[0], Opens[0], RetryAfter) && SameLife(RetryBefore.Life, RetryAfter.Life) &&
            SameCore(RetryBefore.Core, RetryAfter.Core) && RetryAfter.Committed.Revision == 2 && !RetryAfter.Held &&
            !RetryAfter.Blocked && !RetryAfter.Unsaved && RetryAfter.Admissions == RetryBefore.Admissions &&
            RetryAfter.Opens == RetryBefore.Opens+1 && RetryAfter.Slot == 1-RetryBefore.Slot &&
            SameBank(RetryBefore.Banks[RetryBefore.Slot], RetryAfter.Banks[RetryBefore.Slot]) && OtherUnchanged())) return false;
    if (!Refuse(0,7,1,Generation,TEXT("Old successful-use request stays stale after retry"))) return false;

    // Exercise a synchronous writer callback after paired consumption/healing, before publication.
    if (!Advance(1.7)) return false;
    Access::Strike(*Enemy);
    if (!TestTrue(TEXT("Second use overlaps a genuine ready native impact"), Access::Windup(*Enemy)) || !Advance(.75)) return false;
    bool CallbackSawPairedCandidate = false;
    Access::SetOpener(*Models[0], [&](const FString& Path) -> IFileHandle*
    {
        if (Path != Paths[0][0] && Path != Paths[0][1]) { ForeignPath = true; return nullptr; }
        ++Opens[0];
        CallbackSawPairedCandidate = Access::Core(*Models[0]).Revision == 3 && Access::Core(*Models[0]).Inventory[0].Count == 1 &&
            Access::Life(*Models[0]).Health == 90;
        Access::Strike(*Enemy); // Real incoming impact reduces already-committed90 to70.
        return nullptr;
    });
    if (!TestFalse(TEXT("Second real writer failure holds another single candidate"), Access::Use(*Models[0],0,7,2,Generation)) ||
        !TestTrue(TEXT("No post-write publication overwrites damage received during the writer callback"), CallbackSawPairedCandidate &&
            Access::Life(*Models[0]).Health == 70 && Models[0]->GetHealthPresentation().PlayerHealth.Percent == 70 &&
            Access::Core(*Models[0]).Revision == 3 && Access::Core(*Models[0]).Inventory[0].Count == 1 && Access::Held(*Models[0]))) return false;
    if (!Refuse(0,7,2,Generation,TEXT("Old second-use token cannot replay")) ||
        !Refuse(0,7,3,Generation,TEXT("Current second held token cannot replay")) || !Hit(50) || !Hit(30) || !Hit(10) || !Hit(0)) return false;
    if (!Refuse(0,7,3,Generation,TEXT("Dead owner cannot consume its remaining owned Balm")) ||
        !TestFalse(TEXT("Held journal does not bypass the ten-second revive delay"), Access::Revive(*Models[0],Generation)) || !Advance(9.8) ||
        !TestFalse(TEXT("Real clock still refuses revive before ten seconds"), Access::Revive(*Models[0],Generation)) || !Advance(.25) ||
        !TestTrue(TEXT("Manual native revive remains available during a real held item journal"), Access::Revive(*Models[0],Generation))) return false;
    Access::OpenPack(*Models[0]);
    if (!Refuse(0,7,3,Generation,TEXT("Pre-death life token refuses after actual manual revive")) ||
        !Refuse(0,7,3,Generation+1,TEXT("Revived full health and held journal preserve remaining Balm"))) return false;
    InstallWriter(0); Access::ExpireRetry(*Models[0]);
    Image RevivedHeld, Verified;
    if (!Capture(*Models[0], Opens[0], RevivedHeld) || !TestTrue(TEXT("Held consumption saves after death and revive"), Access::Retry(*Models[0])) ||
        !TestTrue(TEXT("Retry never replays earlier HP90 over revived HP100"), Capture(*Models[0], Opens[0], Verified) &&
            SameLife(RevivedHeld.Life, Verified.Life) && Verified.Life.Health == 100 && Verified.Life.Generation == Generation+1 &&
            SameCore(RevivedHeld.Core, Verified.Core) && Verified.Committed.Revision == 3 && Verified.Core.Inventory[0].Count == 1 &&
            Verified.Opens == RevivedHeld.Opens+1 && !Verified.Held && !Verified.Blocked && !Verified.Unsaved && OtherUnchanged())) return false;
    if (!Hit(80) || !Refuse(0,7,3,Generation,TEXT("Actual pre-death generation is stale for the injured revived owner"))) return false;

    // The other real owner receives one impact, then a clamped20-point heal from the25-point catalog dose.
    Pawns[0]->SetActorLocation(FVector(500,0,100)); Pawns[1]->SetActorLocation(FVector(100,0,100));
    Image MainBeforeOther;
    if (!Capture(*Models[0], Opens[0], MainBeforeOther) || !Advance(1.7)) return false;
    Access::Strike(*Enemy);
    if (!TestTrue(TEXT("Independent owner becomes the real nearest native target"), Access::Windup(*Enemy)) || !Advance(.75)) return false;
    Access::Strike(*Enemy);
    const uint64 OtherGeneration = Access::Life(*Models[1]).Generation;
    if (!TestEqual(TEXT("Independent owner really has HP80 before clamp"), Access::Life(*Models[1]).Health, 80)) return false;
    const int64 OtherSequence = Models[1]->GetLastActionReceipt().Sequence;
    Access::Request(*Models[1],0,7,1,OtherGeneration);
    const auto OtherReceipt = Models[1]->GetLastActionReceipt();
    if (!TestTrue(TEXT("Successful direct server use issues one verified Use acknowledgment"),
            OtherReceipt.Sequence == OtherSequence+1 && OtherReceipt.Action == TEXT("Use") && OtherReceipt.bSucceeded &&
            OtherReceipt.ObservedRevision == 2 && OtherReceipt.LastVerifiedJournalRevision == 2 &&
            !OtherReceipt.bHeldCandidate && !OtherReceipt.bPersistenceBlocked) ||
        !TestTrue(TEXT("Clamp retains100, two Balm, coins1 and exactly r2"), Access::Life(*Models[1]).Health == 100 &&
            Access::Core(*Models[1]).Inventory[0].Count == 2 && Access::Core(*Models[1]).Coins == 1 && Access::Core(*Models[1]).Revision == 2 && Opens[1] == 2)) return false;
    Image MainAfterOther;
    if (!TestTrue(TEXT("Other owner's incoming damage/use/journal preserves the complete main owner state and banks"),
        Capture(*Models[0], Opens[0], MainAfterOther) && SameImage(MainBeforeOther, MainAfterOther))) return false;
    Image OtherBeforeReplay, OtherAfterReplay;
    if (!Capture(*Models[1], Opens[1], OtherBeforeReplay) || !TestFalse(TEXT("Repeated successful clamp request is stale"), Access::Use(*Models[1],0,7,1,OtherGeneration)) ||
        !TestTrue(TEXT("Clamp replay does no IO or repeated healing"), Capture(*Models[1], Opens[1], OtherAfterReplay) && SameImage(OtherBeforeReplay, OtherAfterReplay))) return false;

    // New actor/new real writer lease reloads only durable progression. HP persistence is not claimed.
    const auto DurableCore = Access::Core(*Models[0]);
    const auto DurableState = Models[0]->GetProgressionState();
    const int32 OpensBeforeReopen = Opens[0];
    const int32 BankBeforeReopen = Access::BankSlot(*Models[0]);
    Bank DurableBanks[2];
    if (!ReadBank(Paths[0][0], DurableBanks[0]) || !ReadBank(Paths[0][1], DurableBanks[1]))
    { AddError(TEXT("Real durable pre-reopen banks unavailable.")); return false; }
    Access::Retire(*Models[0], *Mode, *Players[0], Ids[0]);
    Models[0] = World->SpawnActor<AJadeboundProgressionActor>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
    if (!Models[0]) { AddError(TEXT("Fresh Balm reload actor creation failed.")); return false; }
    CleanupScope.Models.Add(Models[0]);
    InstallWriter(0); // Count any unexpected write during the actual ReadSave(false).
    FString ReloadError;
    if (!Access::Attach(*Mode,*Enemy,*Models[0],*Players[0],*Pawns[0],Mesh,Ids[0],false,ReloadError))
    { AddError(ReloadError); return false; }
    Bank LoadedBanks[2];
    if (!TestTrue(TEXT("Fresh lease/reload restores consumed r3/count1 and leaves actual journal bytes unchanged"),
        SameCore(DurableCore, Access::Core(*Models[0])) && SameState(DurableState, Models[0]->GetProgressionState()) &&
        Access::BankSlot(*Models[0]) == BankBeforeReopen && Opens[0] == OpensBeforeReopen &&
        ReadBank(Paths[0][0], LoadedBanks[0]) && ReadBank(Paths[0][1], LoadedBanks[1]) &&
        SameBank(DurableBanks[0], LoadedBanks[0]) && SameBank(DurableBanks[1], LoadedBanks[1]) && !ForeignPath)) return false;
    AddInfo(TEXT("Native Balm uses real native handlers, direct server-handler acknowledgments, measured small world frames, catalog purchases, actual journal opener failures/retries/readbacks and new-lease reload. Registry, menu and retry deadlines are explicit trusted fixture setup; no RPC transport, independent-client, actor-scheduler, gameplay/pixels, persistent-health or power-loss proof."));
    return true;
}
#endif
