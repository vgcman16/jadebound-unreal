#include "JadeboundProgressionActor.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "JadeboundDevelopmentGameMode.h"
#include "JadeboundNativeRaider.h"
#include "Combat/PlayerLifeRules.h"
#include "CollisionShape.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"

// Trusted synthetic ownership setup; this test does not simulate login or net transport.
struct FJadeboundPlayerLifeTestAccess
{
    static bool Attach(AJadeboundDevelopmentGameMode& Mode, AJadeboundNativeRaider& Enemy,
        AJadeboundProgressionActor& Model, APlayerController& Player, ACharacter& Pawn, UStaticMesh* Mesh, FString& Error)
    {
        const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
        if (!Model.ConfigureDevelopmentOwner(&Player, Id, Mesh))
        { Error = TEXT("Canonical isolated character ownership/writer setup refused."); return false; }
        Model.Hero = &Pawn;
        Model.bReady = Model.ReadSave(true);
        Model.bServerModelInitialized = Model.bReady;
        if (!Model.bReady)
        { Error = TEXT("Isolated initial progression journal setup refused."); return false; }
        Model.bRecipientCombatEnabled = true;
        Mode.bEnableRecipientCombat = true;
        Mode.RecipientRaider = &Enemy;
        Mode.Models.Add(Id, &Model);
        Mode.ActiveControllers.Add(&Player, Id);
        Model.RefreshState();
        Model.InitializeNativePlayerLife();
        if (!Mode.OwnsNativeCombatant(&Model, true))
        { Error = TEXT("Initialized fixture is not the mode's reciprocal owned native combatant."); return false; }
        return true;
    }
    static FJadeboundPlayerLife Life(const AJadeboundProgressionActor& Model) { return Model.NativePlayerLife; }
    static void Strike(AJadeboundNativeRaider& Enemy) { Enemy.TickNativeStrike(); }
    static bool Windup(const AJadeboundNativeRaider& Enemy) { return Enemy.Strike.bWindup; }
    static uint64 StrikeId(const AJadeboundNativeRaider& Enemy) { return Enemy.Strike.StrikeId; }
    static bool Damage(AJadeboundProgressionActor& Model, AJadeboundNativeRaider& Enemy, uint64 Strike)
    { return Model.ApplyNativePlayerStrike(&Enemy, Strike, Model.NativePlayerLife.Generation, Enemy.Life.LifeId); }
    static bool Living(const AJadeboundProgressionActor& Model) { return Model.HasLivingPossessedHero(); }
    static void SaveHeld(AJadeboundProgressionActor& Model) { Model.bPersistenceBlocked = true; }
    static bool Revive(AJadeboundProgressionActor& Model, uint64 Generation) { return Model.RequestNativeRevive(Generation); }
    static void RebindBody(AJadeboundProgressionActor& Model, ACharacter& Pawn)
    { Model.Hero = &Pawn; Model.PublicLife.Reset(); Model.BindNativePlayerLife(); }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundNativePlayerLifeTest, "Jadebound.Combat.NativePlayerLife",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundNativePlayerLifeTest::RunTest(const FString& Parameters)
{
    FString Argument;
    if (!FParse::Param(FCommandLine::Get(), TEXT("JadeboundPlayerLifeTests")) ||
        !FParse::Value(FCommandLine::Get(), TEXT("UserDir="), Argument) || FPaths::IsRelative(Argument))
    { AddError(TEXT("Explicit isolated UserDir and PlayerLife opt-in required.")); return false; }
    FPaths::NormalizeDirectoryName(Argument);
    FString Effective = FPaths::ProjectUserDir();
    FPaths::NormalizeDirectoryName(Effective);
    if (!FPaths::IsSamePath(Argument, Effective) || FPaths::GetCleanFilename(Argument) != TEXT("UserDir-v001") ||
        FPaths::GetCleanFilename(FPaths::GetPath(Argument)) != TEXT("Jadebound-P102-A019") ||
        !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()), Argument))
    { AddError(TEXT("Fresh exact A019 fixture path required; no real save access.")); return false; }
    if (!GEngine || !GEngine->WorldSettingsClass || !IsInGameThread())
    { AddError(TEXT("Engine world settings and game thread are required.")); return false; }
    UWorld::InitializationValues Values;
    Values.InitializeScenes(true).AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(true)
        .EnableTraceCollision(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false)
        .CreateFXSystem(false).SetTransactional(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
        FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits)), nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!World) { AddError(TEXT("Isolated native test world creation failed.")); return false; }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
    World->SetGameInstance(Instance.Get());
    struct Cleanup
    {
        UWorld* World;
        ~Cleanup() { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
    } CleanupScope{World};
    World->GetWorldSettings()->DefaultGameMode = AJadeboundDevelopmentGameMode::StaticClass();
    if (!World->SetGameMode(FURL())) { AddError(TEXT("Native development game mode creation failed.")); return false; }
    auto* Mode = Cast<AJadeboundDevelopmentGameMode>(World->GetAuthGameMode());
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Enemy = World->SpawnActor<AJadeboundNativeRaider>(FVector(0,0,100), FRotator::ZeroRotator, Spawn);
    UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Jadebound/OriginalWeapons/JadeGlaiveV001/SM_JadeGlaive_CM_V001.SM_JadeGlaive_CM_V001"));
    AJadeboundProgressionActor* Models[2] = {};
    APlayerController* Players[2] = {};
    ACharacter* Pawns[2] = {};
    if (!Mode || !Enemy || !Mesh) { AddError(TEXT("Native mode, raider and original glaive asset required.")); return false; }
    for (int32 I=0; I<2; ++I)
    {
        Players[I] = World->SpawnActor<APlayerController>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
        Pawns[I] = World->SpawnActor<ACharacter>(FVector(100 + I*400,0,100), FRotator::ZeroRotator, Spawn);
        Models[I] = World->SpawnActor<AJadeboundProgressionActor>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
        if (!Players[I] || !Pawns[I] || !Models[I])
        { AddError(TEXT("Native fixture controller, pawn and model creation failed.")); return false; }
        // ULocalPlayer declares Within=Engine. This fixture owns no viewport,
        // but uses a real local player/controller pair for input lease behavior.
        Players[I]->SetPlayer(NewObject<ULocalPlayer>(GEngine));
        if (!Players[I]->IsLocalController()) { AddError(TEXT("Explicit engine local player required to test stacked input leases.")); return false; }
        Pawns[I]->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        Players[I]->Possess(Pawns[I]);
        FString Error;
        if (!FJadeboundPlayerLifeTestAccess::Attach(*Mode,*Enemy,*Models[I],*Players[I],*Pawns[I],Mesh,Error))
        { AddError(Error); return false; }
    }
    const auto Progress = Models[0]->GetProgressionState();
    using Access = FJadeboundPlayerLifeTestAccess;
    auto Advance = [&](double Seconds)
    {
        const double Before = World->GetTimeSeconds();
        // Real UWorld ticks clamp long frames through WorldSettings. Advance
        // ordinary small frames; never overwrite time or combat deadlines.
        const int32 Steps = FMath::FloorToInt(Seconds / .1);
        for (int32 Step=0; Step<Steps; ++Step) World->Tick(LEVELTICK_All, .1f);
        const double Remainder = Seconds - Steps * .1;
        if (Remainder > .000001) World->Tick(LEVELTICK_All, static_cast<float>(Remainder));
        return TestTrue(TEXT("Small world frames advance the intended real server-time interval"),
            FMath::Abs(World->GetTimeSeconds() - Before - Seconds) < .0001);
    };
    auto SpawnBlocker = [&](const FVector& Location, const FVector& Extent, ECollisionChannel Channel)
    {
        AActor* Actor = World->SpawnActor<AActor>(Location, FRotator::ZeroRotator, Spawn);
        if (!Actor) return static_cast<UBoxComponent*>(nullptr);
        UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
        Actor->AddInstanceComponent(Box);
        Actor->SetRootComponent(Box);
        Box->SetBoxExtent(Extent);
        Box->SetCollisionObjectType(ECC_WorldStatic);
        Box->SetCollisionResponseToAllChannels(ECR_Ignore);
        Box->SetCollisionResponseToChannel(Channel, ECR_Block);
        Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Box->SetGenerateOverlapEvents(false);
        Box->RegisterComponent();
        Box->SetWorldLocation(Location);
        return Box;
    };
    auto ClearBlocker = [](UBoxComponent* Box)
    {
        Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Box->GetOwner()->Destroy();
    };
    Access::Strike(*Enemy);
    TestTrue(TEXT("Native windup begins on closest owned living body"), Access::Windup(*Enemy));
    if (!Advance(.35)) return false;
    Access::Strike(*Enemy);
    TestEqual(TEXT("Windup does no damage"), Access::Life(*Models[0]).Health, 100);
    // Moving out during the telegraph must make this spent strike miss.
    Pawns[0]->SetActorLocation(FVector(300,0,100));
    if (!Advance(.40)) return false;
    Access::Strike(*Enemy);
    TestFalse(TEXT("A missed impact spends its windup"), Access::Windup(*Enemy));
    TestEqual(TEXT("Leaving reach avoids damage"), Access::Life(*Models[0]).Health, 100);
    Pawns[0]->SetActorLocation(FVector(100,0,100));
    Access::SaveHeld(*Models[0]); // A held journal must not grant combat immunity.
    if (!Advance(1.7)) return false;
    Access::Strike(*Enemy);
    TestTrue(TEXT("Obstruction scenario begins with a valid windup"), Access::Windup(*Enemy));
    UBoxComponent* Wall = SpawnBlocker(FVector(50,0,120), FVector(5,60,60), ECC_Visibility);
    if (!Wall) { AddError(TEXT("Native wall fixture creation failed.")); return false; }
    FHitResult WallHit;
    FCollisionQueryParams TraceQuery(SCENE_QUERY_STAT(JadeboundLifeFixtureWall), false);
    TraceQuery.AddIgnoredActor(Enemy);
    if (!TestTrue(TEXT("Inserted wall actually blocks the native impact trace"),
        World->LineTraceSingleByChannel(WallHit, FVector(0,0,120), FVector(100,0,120), ECC_Visibility, TraceQuery) &&
        WallHit.GetComponent() == Wall)) return false;
    if (!Advance(.75)) return false;
    Access::Strike(*Enemy);
    TestFalse(TEXT("Obstructed impact spends the windup"), Access::Windup(*Enemy));
    TestEqual(TEXT("Inserted wall prevents target damage"), Access::Life(*Models[0]).Health, 100);
    TestEqual(TEXT("Obstructed strike never redirects to the other player"), Access::Life(*Models[1]).Health, 100);
    ClearBlocker(Wall);
    if (!Advance(1.7)) return false;
    Access::Strike(*Enemy);
    TestTrue(TEXT("Ownership scenario begins with a valid windup"), Access::Windup(*Enemy));
    Players[0]->UnPossess();
    if (!Advance(.75)) return false;
    Access::Strike(*Enemy);
    TestFalse(TEXT("Losing reciprocal possession cancels the windup"), Access::Windup(*Enemy));
    TestEqual(TEXT("Unpossessed body receives no damage"), Access::Life(*Models[0]).Health, 100);
    TestEqual(TEXT("Cancelled strike never redirects to the other player"), Access::Life(*Models[1]).Health, 100);
    const auto AfterMisses = Models[0]->GetProgressionState();
    TestTrue(TEXT("Missed and cancelled strikes preserve progression"),
        FJadeboundProgressionState::StaticStruct()->CompareScriptStruct(&Progress,&AfterMisses,0));
    Players[0]->Possess(Pawns[0]);
    for (int32 Hit=0; Hit<5; ++Hit)
    {
        if (!Advance(1.7)) return false;
        Access::Strike(*Enemy);
        TestTrue(TEXT("Each recovery permits a new server windup"), Access::Windup(*Enemy));
        if (!Advance(.75)) return false;
        Access::Strike(*Enemy);
        TestEqual(TEXT("Single impact reduces health by twenty"), Access::Life(*Models[0]).Health, 80-Hit*20);
        TestEqual(TEXT("Owner HUD presents authoritative100-to0 percent"), Models[0]->GetHealthPresentation().PlayerHealth.Percent, 80-Hit*20);
        TestEqual(TEXT("Public pawn component mirrors authority health"), Pawns[0]->FindComponentByClass<UJadeboundPlayerLifeComponent>()->GetLife().Health, 80-Hit*20);
        TestFalse(TEXT("Old strike cannot be re-applied through model"), Access::Damage(*Models[0],*Enemy,Access::StrikeId(*Enemy)));
        TestEqual(TEXT("Other player's life stays independent"), Access::Life(*Models[1]).Health, 100);
    }
    TestFalse(TEXT("Zero HP blocks living action gate"), Access::Living(*Models[0]));
    TestEqual(TEXT("Dead movement is disabled on authority body"), static_cast<int32>(Pawns[0]->GetCharacterMovement()->MovementMode), static_cast<int32>(MOVE_None));
    const uint64 Generation = Access::Life(*Models[0]).Generation;
    TestFalse(TEXT("Immediate revive refuses"), Access::Revive(*Models[0],Generation));
    // Fresh possession emulates the reconnect body's assembly only, not transport.
    ACharacter* Replacement = World->SpawnActor<ACharacter>(FVector(350,200,100), FRotator::ZeroRotator, Spawn);
    if (!Replacement) { AddError(TEXT("Native replacement body creation failed.")); return false; }
    Replacement->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    Players[0]->Possess(Replacement);
    Access::RebindBody(*Models[0], *Replacement);
    TestEqual(TEXT("Fresh possession cannot reset dead retained life"), Access::Life(*Models[0]).Health, 0);
    TestEqual(TEXT("Replacement immediately receives death movement gate"), static_cast<int32>(Replacement->GetCharacterMovement()->MovementMode), static_cast<int32>(MOVE_None));
    if (!Advance(9.8)) return false;
    TestFalse(TEXT("Revive before ten seconds refuses"), Access::Revive(*Models[0],Generation));
    if (!Advance(.25)) return false;
    TestFalse(TEXT("Stale generation refuses"), Access::Revive(*Models[0],Generation+1));
    UBoxComponent* Occupant = SpawnBlocker(FVector(100,0,100), FVector(60,60,120), ECC_Pawn);
    if (!Occupant) { AddError(TEXT("Native home blocker fixture creation failed.")); return false; }
    const UCapsuleComponent* Capsule = Replacement->GetCapsuleComponent();
    FCollisionQueryParams HomeQuery(SCENE_QUERY_STAT(JadeboundLifeFixtureHome), false);
    HomeQuery.AddIgnoredActor(Replacement);
    if (!TestTrue(TEXT("Home blocker actually overlaps the server-selected revive capsule"),
        World->OverlapBlockingTestByChannel(FVector(100,0,100), FQuat::Identity, ECC_Pawn,
        FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), HomeQuery))) return false;
    const FTransform BeforeBlockedRevive = Replacement->GetActorTransform();
    TestFalse(TEXT("Occupied home refuses revive"), Access::Revive(*Models[0],Generation));
    TestEqual(TEXT("Blocked revive keeps zero health"), Access::Life(*Models[0]).Health, 0);
    TestEqual(TEXT("Blocked revive keeps the exact generation"), Access::Life(*Models[0]).Generation, Generation);
    TestTrue(TEXT("Blocked revive leaves the pawn at its death location"), Replacement->GetActorTransform().Equals(BeforeBlockedRevive));
    TestEqual(TEXT("Blocked revive keeps movement disabled"), static_cast<int32>(Replacement->GetCharacterMovement()->MovementMode), static_cast<int32>(MOVE_None));
    TestTrue(TEXT("Blocked revive retains the local dead movement lease"), Players[0]->IsMoveInputIgnored());
    const auto AfterBlockedRevive = Models[0]->GetProgressionState();
    TestTrue(TEXT("Blocked revive leaves progression unchanged"),
        FJadeboundProgressionState::StaticStruct()->CompareScriptStruct(&Progress,&AfterBlockedRevive,0));
    ClearBlocker(Occupant);
    TestTrue(TEXT("Manual revive succeeds after delay, even with held journal"), Access::Revive(*Models[0],Generation));
    TestEqual(TEXT("Revive restores100"), Access::Life(*Models[0]).Health, 100);
    TestFalse(TEXT("Old body's movement lease is released after possession and revive"), Players[0]->IsMoveInputIgnored());
    TestEqual(TEXT("Revive restores the replacement's saved movement mode"), static_cast<int32>(Replacement->GetCharacterMovement()->MovementMode), static_cast<int32>(MOVE_Walking));
    TestEqual(TEXT("Revive advances generation exactly once"), Access::Life(*Models[0]).Generation, Generation+1);
    TestFalse(TEXT("Repeated revive refuses"), Access::Revive(*Models[0],Generation));
    TestTrue(TEXT("Revive keeps the exact same replacement possession"), Players[0]->GetPawn()==Replacement && Replacement->GetController()==Players[0]);
    const auto After = Models[0]->GetProgressionState();
    TestTrue(TEXT("Damage/death/revive do not award or consume progression"), FJadeboundProgressionState::StaticStruct()->CompareScriptStruct(&Progress,&After,0));
    AddInfo(TEXT("Synthetic native authority/physics/life fixture; independent clients and rendered ghost remain unqualified."));
    return true;
}
#endif
