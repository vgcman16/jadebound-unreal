#include "JadeboundProgressionActor.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "JadeboundDevelopmentGameMode.h"
#include "JadeboundNativeRaider.h"
#include "Combat/PlayerLifeRules.h"
#include "Domain/ProgressionCore.h"
#include "Components/BoxComponent.h"
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
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"

// Trusted automation assembly/window perturbations, never client issuance or login.
struct FJadeboundNativeStrikeIssuanceTestAccess
{
    static bool Attach(AJadeboundDevelopmentGameMode& Mode, AJadeboundNativeRaider& Enemy,
        AJadeboundProgressionActor& Model, APlayerController& Player, ACharacter& Pawn, UStaticMesh* Mesh)
    {
        const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
        if (!Model.ConfigureDevelopmentOwner(&Player, Id, Mesh)) return false;
        Model.Hero = &Pawn; Model.bReady = Model.ReadSave(true); Model.bServerModelInitialized = Model.bReady;
        if (!Model.bReady) return false;
        Model.bRecipientCombatEnabled = true; Mode.bEnableRecipientCombat = true; Mode.RecipientRaider = &Enemy;
        Mode.Models.Add(Id, &Model); Mode.ActiveControllers.Add(&Player, Id);
        Model.RefreshState(); Model.InitializeNativePlayerLife();
        return Mode.OwnsNativeCombatant(&Model, true);
    }
    static const Jadebound::Combat::PlayerLife::State& Life(const AJadeboundProgressionActor& Model)
    { return *Model.NativeLifeState; }
    static void Observation(AJadeboundProgressionActor& Model, double Value) { Model.NativeLifeState->LastObservation = Value; }
    static void SetLife(AJadeboundProgressionActor& Model, const Jadebound::Combat::PlayerLife::State& Value) { *Model.NativeLifeState = Value; }
    static bool PublicAlive(const AJadeboundProgressionActor& Model) { return Model.NativePlayerLife.IsAlive(); }
    static bool Living(const AJadeboundProgressionActor& Model) { return Model.HasLivingPossessedHero(); }
    static bool Owns(const AJadeboundDevelopmentGameMode& Mode, const AJadeboundProgressionActor& Model, bool Living)
    { return Mode.OwnsNativeCombatant(&Model,Living); }
    static bool Ready(AJadeboundProgressionActor& Model) { return Model.AuthorityReady(); }
    static const Jadebound::Snapshot& Core(const AJadeboundProgressionActor& Model) { return Model.Core->GetSnapshot(); }
    static bool Menu(AJadeboundProgressionActor& Model, bool Open)
    { const bool Previous = Model.bAuthorityMenuOpen; Model.bAuthorityMenuOpen = Open; return Previous; }
    static bool CanBuyMoonleaf(const AJadeboundProgressionActor& Model)
    { auto Preview = *Model.Core; return Preview.Buy(Jadebound::ItemId::Moonleaf,1).Succeeded(); }
    static uint64 Sequence(const AJadeboundDevelopmentGameMode& Mode) { return Mode.LastNativeDamageStrike; }
    static void SetSequence(AJadeboundDevelopmentGameMode& Mode, uint64 Value) { Mode.LastNativeDamageStrike = Value; }
    static uint64 LocalId(const AJadeboundNativeRaider& Enemy) { return Enemy.Strike.StrikeId; }
    static bool Windup(const AJadeboundNativeRaider& Enemy) { return Enemy.Strike.bWindup; }
    static bool Pending(const AJadeboundNativeRaider& Enemy) { return Enemy.bStrikeImpactPending; }
    static void Tick(AJadeboundNativeRaider& Enemy) { Enemy.TickNativeStrike(); }
    static void Select(AJadeboundDevelopmentGameMode& Mode, AJadeboundNativeRaider& Enemy) { Mode.RecipientRaider = &Enemy; }
    static void Epoch(AJadeboundNativeRaider& Enemy) { ++Enemy.Life.LifeId; } // Trusted enemy-life transition, not natural respawn.
    static bool Damage(AJadeboundProgressionActor& Model, AJadeboundNativeRaider& Enemy, uint64 Id, uint64 Gen, int64 Epoch)
    { return Model.ApplyNativePlayerStrike(&Enemy, Id, Gen, Epoch); }
    static void Window(AJadeboundProgressionActor& Model, AJadeboundNativeRaider& Enemy, double Time)
    {
        Enemy.Strike.bWindup = false; Enemy.Strike.ImpactServerTime = Time; Enemy.bStrikeImpactPending = true;
        Enemy.StrikeRecipient = &Model; Enemy.StrikePawn = Model.Hero;
        Enemy.StrikePlayerGeneration = Life(Model).Generation; Enemy.StrikeEnemyLife = Enemy.Life.LifeId;
    }
    static void Clear(AJadeboundNativeRaider& Enemy)
    {
        Enemy.bStrikeImpactPending = false; Enemy.Strike.bWindup = false;
        Enemy.StrikeRecipient.Reset(); Enemy.StrikePawn.Reset();
        Enemy.StrikePlayerGeneration = 0; Enemy.StrikeEnemyLife = 0;
    }
    static void WrongPawn(AJadeboundNativeRaider& Enemy, APawn* Pawn) { Enemy.StrikePawn = Pawn; }
    static void NoPending(AJadeboundNativeRaider& Enemy) { Enemy.bStrikeImpactPending = false; }
    static FString Journal(const AJadeboundProgressionActor& Model, int32 Bank) { return Model.GetJournalPath(Bank); }
    static void Opener(AJadeboundProgressionActor& Model, TFunction<IFileHandle*(const FString&)> Open)
    { Model.AutomationJournalOpener = MoveTemp(Open); }
};

namespace JadeboundNativeStrikeIssuanceTests
{
using Access = FJadeboundNativeStrikeIssuanceTestAccess;
using LifeState = Jadebound::Combat::PlayerLife::State;
bool SameLife(const LifeState& A, const LifeState& B)
{
    return A.Generation == B.Generation && A.Health == B.Health && A.DeadAt == B.DeadAt &&
        A.ReviveReadyAt == B.ReviveReadyAt && A.LastStrike == B.LastStrike && A.LastObservation == B.LastObservation;
}
bool SameCore(const Jadebound::Snapshot& A, const Jadebound::Snapshot& B)
{
    if (A.Version!=B.Version || A.Revision!=B.Revision || A.Coins!=B.Coins || A.Quest!=B.Quest ||
        A.EquippedWeapon!=B.EquippedWeapon || A.NextDropId!=B.NextDropId || A.QuestKillDropId!=B.QuestKillDropId ||
        A.UnprocessedDropIds!=B.UnprocessedDropIds) return false;
    for (uint32 I=0; I<Jadebound::InventorySlotCount; ++I)
        if (A.Inventory[I].Id!=B.Inventory[I].Id || A.Inventory[I].Count!=B.Inventory[I].Count) return false;
    return true;
}
struct Bank { bool Exists = false; TArray<uint8> Bytes; };
bool ReadBank(const FString& Path, Bank& Out)
{
    auto& Files = FPlatformFileManager::Get().GetPlatformFile();
    Out.Exists = Files.FileExists(*Path); Out.Bytes.Reset();
    if (!Out.Exists) return true;
    const int64 Size = Files.FileSize(*Path);
    if (Size <= 0 || Size > 1024*1024+32) return false;
    TUniquePtr<IFileHandle> File(Files.OpenRead(*Path));
    if (!File) return false;
    Out.Bytes.SetNumUninitialized(static_cast<int32>(Size));
    return File->Read(Out.Bytes.GetData(), Size);
}
bool SameBank(const Bank& A, const Bank& B) { return A.Exists == B.Exists && A.Bytes == B.Bytes; }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundNativeStrikeIssuanceTest, "Jadebound.Progression.NativeStrikeIssuance",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundNativeStrikeIssuanceTest::RunTest(const FString& Parameters)
{
    using namespace JadeboundNativeStrikeIssuanceTests;
    FString Argument;
    if (!FParse::Param(FCommandLine::Get(), TEXT("JadeboundNativeStrikeIssuanceTests")) ||
        !FParse::Value(FCommandLine::Get(), TEXT("UserDir="), Argument) || FPaths::IsRelative(Argument))
    { AddError(TEXT("Native strike fixture requires opt-in and isolated absolute UserDir.")); return false; }
    FPaths::NormalizeDirectoryName(Argument);
    FString Effective = FPaths::ProjectUserDir(); FPaths::NormalizeDirectoryName(Effective);
    if (!FPaths::IsSamePath(Argument, Effective) || FPaths::GetCleanFilename(Argument) != TEXT("UserDir-v001") ||
        FPaths::GetCleanFilename(FPaths::GetPath(Argument)) != TEXT("Jadebound-P102-A021") ||
        !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()), Argument))
    { AddError(TEXT("Exact fresh A021 namespace required; older fixtures and real saves remain untouched.")); return false; }
    if (!GEngine || !GEngine->WorldSettingsClass || !IsInGameThread())
    { AddError(TEXT("Native strike fixture requires engine settings/game thread.")); return false; }
    UWorld::InitializationValues Values;
    Values.InitializeScenes(true).AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(true)
        .EnableTraceCollision(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false)
        .CreateFXSystem(false).SetTransactional(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
        FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits)), nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!World) { AddError(TEXT("Isolated strike world creation failed.")); return false; }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine)); World->SetGameInstance(Instance.Get());
    struct Cleanup
    {
        UWorld* World; TArray<TWeakObjectPtr<AJadeboundProgressionActor>> Models;
        ~Cleanup()
        {
            for (const auto& Model : Models) if (Model.IsValid()) Access::Opener(*Model.Get(), nullptr);
            GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
        }
    } CleanupScope{World,{}};
    World->GetWorldSettings()->DefaultGameMode = AJadeboundDevelopmentGameMode::StaticClass();
    if (!World->SetGameMode(FURL())) { AddError(TEXT("Isolated strike mode creation failed.")); return false; }
    auto* Mode = Cast<AJadeboundDevelopmentGameMode>(World->GetAuthGameMode());
    FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Original = World->SpawnActor<AJadeboundNativeRaider>(FVector(0,0,100), FRotator::ZeroRotator, Spawn);
    UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Jadebound/OriginalWeapons/JadeGlaiveV001/SM_JadeGlaive_CM_V001.SM_JadeGlaive_CM_V001"));
    if (!Mode || !Original || !Mesh) { AddError(TEXT("Original mesh and native mode/enemy required.")); return false; }
    AJadeboundProgressionActor* Models[2] = {}; APlayerController* Players[2] = {}; ACharacter* Pawns[2] = {};
    FString Paths[2][2]; Bank BeforeBanks[2][2]; FJadeboundProgressionState Progress[2]; Jadebound::Snapshot CoreBefore[2]; int32 Opens = 0;
    for (int32 I=0; I<2; ++I)
    {
        Players[I] = World->SpawnActor<APlayerController>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
        Pawns[I] = World->SpawnActor<ACharacter>(FVector(100+I*400,0,100), FRotator::ZeroRotator, Spawn);
        Models[I] = World->SpawnActor<AJadeboundProgressionActor>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
        if (!Players[I] || !Pawns[I] || !Models[I]) { AddError(TEXT("Native strike owner actors unavailable.")); return false; }
        CleanupScope.Models.Add(Models[I]); Players[I]->SetPlayer(NewObject<ULocalPlayer>(GEngine));
        if (!Players[I]->IsLocalController()) { AddError(TEXT("Engine-owned local fixture player required.")); return false; }
        Pawns[I]->GetCharacterMovement()->SetMovementMode(MOVE_Walking); Players[I]->Possess(Pawns[I]);
        if (!Access::Attach(*Mode,*Original,*Models[I],*Players[I],*Pawns[I],Mesh))
        { AddError(TEXT("Canonical owner, real initial journal, or reciprocal native combatant admission failed.")); return false; }
        Progress[I] = Models[I]->GetProgressionState(); CoreBefore[I] = Access::Core(*Models[I]);
        for (int32 J=0; J<2; ++J)
        {
            Paths[I][J] = Access::Journal(*Models[I],J);
            if (Paths[I][J].IsEmpty() || !FPaths::IsUnderDirectory(Paths[I][J],Argument) || !ReadBank(Paths[I][J],BeforeBanks[I][J]))
            { AddError(TEXT("Exact isolated A021 journal baseline unavailable.")); return false; }
        }
        Access::Opener(*Models[I], [&](const FString&) -> IFileHandle* { ++Opens; return nullptr; });
    }
    auto ProgressUnchanged = [&]()
    {
        for (int32 I=0; I<2; ++I)
        {
            const auto Current = Models[I]->GetProgressionState();
            if (!FJadeboundProgressionState::StaticStruct()->CompareScriptStruct(&Progress[I],&Current,0) ||
                !SameCore(CoreBefore[I],Access::Core(*Models[I]))) return false;
            for (int32 J=0; J<2; ++J) { Bank After; if (!ReadBank(Paths[I][J],After) || !SameBank(BeforeBanks[I][J],After)) return false; }
        }
        return Opens == 0;
    };
    auto Advance = [&](double Seconds)
    {
        const double Before = World->GetTimeSeconds(); const int32 Steps = FMath::FloorToInt(Seconds/.1);
        for (int32 I=0; I<Steps; ++I) World->Tick(LEVELTICK_All,.1f);
        const double Rest = Seconds-Steps*.1; if (Rest>.000001) World->Tick(LEVELTICK_All,static_cast<float>(Rest));
        return TestTrue(TEXT("Measured small world frames advance the real server clock"),FMath::Abs(World->GetTimeSeconds()-Before-Seconds)<.0001);
    };
    auto Hit = [&](AJadeboundNativeRaider& Enemy, int32 Owner, int32 HP, uint64 Global)
    {
        if (!Advance(1.7)) return false;
        Access::Tick(Enemy);
        if (!TestTrue(TEXT("Production tick starts a valid owner windup"),Access::Windup(Enemy)) || !Advance(.75)) return false;
        Access::Tick(Enemy);
        return TestTrue(TEXT("Production impact commits expected HP/global high-water and closes latch"),
            Access::Life(*Models[Owner]).Health == HP && Access::Life(*Models[Owner]).LastStrike == Global &&
            Access::Sequence(*Mode) == Global && !Access::Pending(Enemy) && !Access::Windup(Enemy) && ProgressUnchanged());
    };
    if (!Hit(*Original,0,80,1) || !Hit(*Original,0,60,2)) return false;
    auto* Enemy = World->SpawnActor<AJadeboundNativeRaider>(FVector(0,0,100),FRotator::ZeroRotator,Spawn);
    if (!Enemy || !TestEqual(TEXT("Newly allocated replacement starts local windup ID0"),Access::LocalId(*Enemy),uint64(0))) return false;
    // Remove only the retired enemy's collision, so the replacement trace is meaningful.
    Original->SetActorEnableCollision(false); Access::Select(*Mode,*Enemy);
    if (!Hit(*Enemy,0,40,3) || !TestEqual(TEXT("Replacement's local ID1 receives global damage ID3"),Access::LocalId(*Enemy),uint64(1))) return false;
    const auto BeforeOld = Access::Life(*Models[0]); const uint64 BeforeOldCounter = Access::Sequence(*Mode);
    Access::Window(*Models[0],*Original,World->GetTimeSeconds());
    const bool OldAccepted = Access::Damage(*Models[0],*Original,Access::LocalId(*Original),BeforeOld.Generation,Original->GetLife().LifeId);
    Access::Tick(*Original); Access::Clear(*Original);
    if (!TestTrue(TEXT("Retired original handler/tick cannot damage through replacement admission"),!OldAccepted &&
        SameLife(BeforeOld,Access::Life(*Models[0])) && Access::Sequence(*Mode)==BeforeOldCounter && !Access::Windup(*Original))) return false;
    const int64 PriorEpoch = Enemy->GetLife().LifeId; Access::Epoch(*Enemy);
    if (!Hit(*Enemy,0,20,4) || !TestTrue(TEXT("Trusted same-actor enemy-life transition retains both counters"),
        Enemy->GetLife().LifeId==PriorEpoch+1 && Access::LocalId(*Enemy)==2)) return false;

    // Direct private window perturbations isolate each refusal; positive impacts above use real ticks.
    auto Refuse = [&](const TCHAR* Label, TFunction<void()> Change, uint64 Id, uint64 Gen, int64 Epoch, bool MustConsume = false)
    {
        const auto Before = Access::Life(*Models[0]); const auto Other = Access::Life(*Models[1]);
        const uint64 Counter = Access::Sequence(*Mode);
        Access::Window(*Models[0],*Enemy,World->GetTimeSeconds()); Change();
        const bool Accepted = Access::Damage(*Models[0],*Enemy,Id,Gen,Epoch);
        const bool Consumed = !Access::Pending(*Enemy); Access::Clear(*Enemy);
        return TestTrue(Label,!Accepted && SameLife(Before,Access::Life(*Models[0])) && SameLife(Other,Access::Life(*Models[1])) &&
            Access::Sequence(*Mode)==Counter && (!MustConsume || Consumed) && ProgressUnchanged());
    };
    const uint64 Id = Access::LocalId(*Enemy), Gen = Access::Life(*Models[0]).Generation;
    const int64 Epoch = Enemy->GetLife().LifeId;
    const uint64 CounterBeforePerturbation = Access::Sequence(*Mode);
    Access::SetSequence(*Mode,0); // Trusted inconsistent issuer fixture: never repair it by resetting player high-water.
    const bool Behind = Refuse(TEXT("Issuer behind retained life refuses atomically and consumes pending impact"),[]{},Id,Gen,Epoch,true);
    Access::SetSequence(*Mode,CounterBeforePerturbation); if (!Behind) return false;
    const double Observation = Access::Life(*Models[0]).LastObservation;
    Access::Observation(*Models[0],World->GetTimeSeconds()+1.); // Trusted future observation, not a rewritten world clock.
    const bool Clock = Refuse(TEXT("Nonmonotonic domain clock refuses atomically and consumes pending impact"),[]{},Id,Gen,Epoch,true);
    Access::Observation(*Models[0],Observation); if (!Clock) return false;
    if (!Refuse(TEXT("Stale local windup identity refuses without issuer mutation"),[]{},Id-1,Gen,Epoch) ||
        !Refuse(TEXT("Wrong player generation refuses without issuer mutation"),[]{},Id,Gen+1,Epoch) ||
        !Refuse(TEXT("Old enemy-life epoch refuses without issuer mutation"),[]{},Id,Gen,PriorEpoch) ||
        !Refuse(TEXT("Wrong captured pawn refuses without issuer mutation"),[&]{Access::WrongPawn(*Enemy,Pawns[1]);},Id,Gen,Epoch) ||
        !Refuse(TEXT("Closed private impact latch refuses without issuer mutation"),[&]{Access::NoPending(*Enemy);},Id,Gen,Epoch)) return false;
    Models[0]->SetOwner(Players[1]);
    const bool WrongOwner = Refuse(TEXT("Wrong current owner refuses without issuer mutation"),[]{},Id,Gen,Epoch);
    Models[0]->SetOwner(Players[0]); if (!WrongOwner) return false;
    Pawns[0]->SetActorLocation(FVector(400,0,100));
    const bool Range = Refuse(TEXT("Outside actual native reach refuses without issuer mutation"),[]{},Id,Gen,Epoch);
    Pawns[0]->SetActorLocation(FVector(100,0,100)); if (!Range) return false;
    if (!Refuse(TEXT("Early impact window refuses without issuer mutation"),[&]{Access::Window(*Models[0],*Enemy,World->GetTimeSeconds()+.3);},Id,Gen,Epoch) ||
        !Refuse(TEXT("Late impact window refuses without issuer mutation"),[&]{Access::Window(*Models[0],*Enemy,World->GetTimeSeconds()-.3);},Id,Gen,Epoch)) return false;
    AActor* WallActor = World->SpawnActor<AActor>(FVector::ZeroVector,FRotator::ZeroRotator,Spawn);
    if (!WallActor) { AddError(TEXT("Impact wall actor creation failed.")); return false; }
    UBoxComponent* Wall = NewObject<UBoxComponent>(WallActor); WallActor->SetRootComponent(Wall); WallActor->AddInstanceComponent(Wall);
    Wall->SetBoxExtent(FVector(5,60,60)); Wall->SetCollisionResponseToAllChannels(ECR_Ignore); Wall->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    Wall->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Wall->SetGenerateOverlapEvents(false); Wall->RegisterComponent(); Wall->SetWorldLocation(FVector(50,0,120));
    FHitResult Blocked; FCollisionQueryParams Query(SCENE_QUERY_STAT(JadeboundStrikeIssuerFixtureWall),false); Query.AddIgnoredActor(Enemy);
    if (!TestTrue(TEXT("Wall actually blocks the production visibility trace"),World->LineTraceSingleByChannel(Blocked,FVector(0,0,120),FVector(100,0,120),ECC_Visibility,Query) && Blocked.GetComponent()==Wall)) return false;
    const bool WallRefused = Refuse(TEXT("Actual obstructed impact refuses without issuer mutation"),[]{},Id,Gen,Epoch);
    Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision); WallActor->Destroy(); if (!WallRefused) return false;

    const uint64 SavedCounter = Access::Sequence(*Mode); const auto BeforeExhausted = Access::Life(*Models[0]);
    Access::SetSequence(*Mode,MAX_uint64); // Explicit exhaustion fixture, never a production reset.
    if (!Advance(1.7)) return false; Access::Tick(*Enemy);
    if (!TestTrue(TEXT("Exhaustion still exercises a genuine production windup"),Access::Windup(*Enemy)) || !Advance(.75)) return false;
    Access::Tick(*Enemy);
    if (!TestTrue(TEXT("Real exhausted impact fails closed, spends latch, never wraps or changes life"),
        Access::Sequence(*Mode)==MAX_uint64 && SameLife(BeforeExhausted,Access::Life(*Models[0])) && !Access::Pending(*Enemy) && ProgressUnchanged())) return false;
    Access::SetSequence(*Mode,SavedCounter);
    Pawns[0]->SetActorLocation(FVector(500,0,100)); Pawns[1]->SetActorLocation(FVector(100,0,100));
    const auto Main = Access::Life(*Models[0]);
    if (!Hit(*Enemy,1,80,5) || !TestTrue(TEXT("Independent owner's first accepted global ID does not change main life"),SameLife(Main,Access::Life(*Models[0])))) return false;
    // The exact real handler remains guarded while its caller's recipient/window are still present.
    Access::Window(*Models[1],*Enemy,World->GetTimeSeconds());
    if (!TestTrue(TEXT("Explicit one-call impact fixture invokes real handler"),Access::Damage(*Models[1],*Enemy,Access::LocalId(*Enemy),Access::Life(*Models[1]).Generation,Epoch))) return false;
    const auto AfterOne = Access::Life(*Models[1]); const uint64 AfterOneCounter = Access::Sequence(*Mode);
    const bool Replay = Access::Damage(*Models[1],*Enemy,Access::LocalId(*Enemy),AfterOne.Generation,Epoch); Access::Clear(*Enemy);
    if (!TestTrue(TEXT("Same impact handler replay refuses before caller clears captured window"),!Replay &&
        AfterOne.Health==60 && AfterOne.LastStrike==6 && AfterOneCounter==6 && SameLife(AfterOne,Access::Life(*Models[1])) &&
        Access::Sequence(*Mode)==AfterOneCounter && SameLife(Main,Access::Life(*Models[0])) && ProgressUnchanged())) return false;

    // Model the lethal commit/publication gap without invoking a montage callback.
    const auto SavedLife = Access::Life(*Models[1]);
    const FVector SavedMerchantPoint = Models[1]->MerchantPoint; const float SavedMerchantRange = Models[1]->MerchantRange;
    const bool SavedMenu = Access::Menu(*Models[1],true);
    Models[1]->MerchantPoint = Pawns[1]->GetActorLocation(); Models[1]->MerchantRange = 200.f;
    if (!TestTrue(TEXT("Living owner and current catalog otherwise admit the affordable Moonleaf purchase"),
        Access::PublicAlive(*Models[1]) && Access::Living(*Models[1]) && Access::Owns(*Mode,*Models[1],true) &&
        Access::Owns(*Mode,*Models[1],false) && Access::Ready(*Models[1]) && Access::CanBuyMoonleaf(*Models[1]))) return false;
    auto DeadCandidate = SavedLife; uint64 SyntheticId = DeadCandidate.LastStrike;
    while (DeadCandidate.Health > 0)
    {
        if (SyntheticId == MAX_uint64 || !Jadebound::Combat::PlayerLife::TryDamage(DeadCandidate,++SyntheticId,
            DeadCandidate.Generation,World->GetTimeSeconds()))
        { AddError(TEXT("Valid pure lethal timing-gap candidate creation failed.")); return false; }
    }
    Access::SetLife(*Models[1],DeadCandidate); // Leave replicated/public life alive deliberately.
    const bool DeadAdmissionClosed = Access::PublicAlive(*Models[1]) && !Access::Living(*Models[1]) &&
        !Access::Owns(*Mode,*Models[1],true) && Access::Owns(*Mode,*Models[1],false) &&
        !Models[1]->BuyItemAtRevision(4,1,Progress[1].Revision) && SameLife(DeadCandidate,Access::Life(*Models[1])) &&
        Access::Sequence(*Mode)==AfterOneCounter && SameLife(Main,Access::Life(*Models[0])) && ProgressUnchanged();
    Access::SetLife(*Models[1],SavedLife); Models[1]->MerchantPoint = SavedMerchantPoint; Models[1]->MerchantRange = SavedMerchantRange;
    Access::Menu(*Models[1],SavedMenu);
    if (!TestTrue(TEXT("Committed dead domain blocks living admission/purchase while public life remains alive; nonliving ownership survives"),
        DeadAdmissionClosed && SameLife(SavedLife,Access::Life(*Models[1])) && Access::Owns(*Mode,*Models[1],true) && ProgressUnchanged())) return false;
    AddInfo(TEXT("A021 measures real production strike ticks, replacement/global IDs, exact progression/journal bytes and closed-latch replay. Registry assembly, enemy-life transition, refusal windows, future observation, inconsistent/maximum counters and the lethal domain/publication gap are explicit trusted fixture perturbations. No natural enemy respawn, actual montage/publication callback reentrancy, independent clients, scheduler/gameplay/pixels or cross-session health/issuer proof."));
    return true;
}
#endif
