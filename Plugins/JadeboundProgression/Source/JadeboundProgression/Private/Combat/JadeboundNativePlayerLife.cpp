#include "JadeboundProgressionActor.h"
#include "JadeboundDevelopmentGameMode.h"
#include "JadeboundNativeRaider.h"
#include "Combat/PlayerLifeRules.h"
#include "Combat/NativeStrikeSequence.h"
#include "CollisionShape.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"

void FJadeboundPlayerLifeDeleter::operator()(Jadebound::Combat::PlayerLife::State* Pointer) const { delete Pointer; }

void AJadeboundProgressionActor::InitializeNativePlayerLife()
{
    if (!HasAuthority() || !HasDevelopmentContext() || !bRecipientCombatEnabled || bPersistenceEndingPlay) return;
    if (!NativeLifeState) NativeLifeState.Reset(new Jadebound::Combat::PlayerLife::State());
    // Re-enabling combat or reconnecting never resets an existing life.
    PublishNativePlayerLife();
}
void AJadeboundProgressionActor::PublishNativePlayerLife()
{
    if (!HasAuthority() || !NativeLifeState || !Jadebound::Combat::PlayerLife::Valid(*NativeLifeState)) return;
    NativePlayerLife.bInitialized = true;
    NativePlayerLife.Generation = NativeLifeState->Generation;
    NativePlayerLife.Health = NativeLifeState->Health;
    NativePlayerLife.DeathServerTime = NativeLifeState->DeadAt;
    NativePlayerLife.ReviveServerTime = NativeLifeState->ReviveReadyAt;
    BindNativePlayerLife();
    ForceNetUpdate();
}
void AJadeboundProgressionActor::BindNativePlayerLife()
{
    if (!HasAuthority() || !bDevelopmentModel || !NativePlayerLife.bInitialized || !IsValid(Hero) ||
        Hero->IsActorBeingDestroyed() || !IsValid(Player) || Player->GetPawn() != Hero || Hero->GetController() != Player ||
        !GetWorld() || GetWorld()->bIsTearingDown) return;
    ACharacter* Character = Cast<ACharacter>(Hero);
    if (!Character || Character->GetActorTransform().ContainsNaN()) return;
    if (!bNativeReviveHomeSet)
    {
        NativeReviveHome = Character->GetActorTransform();
        bNativeReviveHomeSet = true;
    }
    UJadeboundPlayerLifeComponent* Component = PublicLife.Get();
    if (!Component || Component->GetOwner() != Hero)
    {
        Component = Hero->FindComponentByClass<UJadeboundPlayerLifeComponent>();
        if (!Component)
        {
            Component = NewObject<UJadeboundPlayerLifeComponent>(Hero, FName(TEXT("JadeboundPublicLife")));
            Hero->AddInstanceComponent(Component);
            Component->SetIsReplicated(true);
            Component->RegisterComponent();
        }
        PublicLife = Component;
    }
    Component->PublishFromAuthority(NativePlayerLife);
}
void AJadeboundProgressionActor::OnRepNativePlayerLife()
{
    if (!NativePlayerLife.IsAlive() && NativePlayerLife.bInitialized)
    {
        ShowMenu(false);
        CancelRecipientAttack();
    }
}
bool AJadeboundDevelopmentGameMode::OwnsNativeCombatant(const AJadeboundProgressionActor* Model, bool Living) const
{
    if (!OwnsRecipientModel(Model, false) || !Model->bServerModelInitialized || !Model->NativePlayerLife.bInitialized ||
        !IsValid(Model->Player) || !IsValid(Model->Hero) || Model->Hero->IsActorBeingDestroyed() ||
        Model->Hero->GetWorld() != GetWorld() || Model->Player->GetPawn() != Model->Hero ||
        Model->Hero->GetController() != Model->Player || Model->GetOwner() != Model->Player) return false;
    const FString* Id = ActiveControllers.Find(Model->Player);
    return Id && *Id == Model->ServerCharacterId && (!Living || Model->HasLivingPossessedHero());
}
bool AJadeboundProgressionActor::ApplyNativePlayerStrike(AJadeboundNativeRaider* Enemy, uint64 StrikeId,
    uint64 ExpectedGeneration, int64 EnemyLife)
{
    UWorld* World = GetWorld();
    auto* Mode = World ? Cast<AJadeboundDevelopmentGameMode>(World->GetAuthGameMode()) : nullptr;
    if (!IsInGameThread() || !HasAuthority() || bPersistenceEndingPlay || IsActorBeingDestroyed() ||
        !World || World->bIsTearingDown || World->IsPaused() || !Mode || !Mode->OwnsNativeCombatant(this, true) ||
        !NativeLifeState || !IsValid(Enemy) || Enemy != Mode->GetRecipientRaider() || !Enemy->HasAuthority() ||
        Enemy->GetWorld() != World || Enemy->IsActorBeingDestroyed() || !Enemy->Life.bAlive || Enemy->Life.LifeId != EnemyLife ||
        Enemy->StrikeRecipient.Get() != this || Enemy->StrikePawn.Get() != Hero || Enemy->Strike.StrikeId != StrikeId ||
        Enemy->Strike.bWindup || !Enemy->bStrikeImpactPending || Enemy->StrikePlayerGeneration != ExpectedGeneration ||
        !Enemy->IsNativeStrikeReachable(Hero)) return false;
    const double Now = World->GetTimeSeconds();
    if (!FMath::IsFinite(Enemy->Strike.ImpactServerTime) || Enemy->Strike.ImpactServerTime < 0 ||
        Now < Enemy->Strike.ImpactServerTime || Now > Enemy->Strike.ImpactServerTime + .20) return false;
    // Spend this synchronous impact before any presentation callback. A domain
    // refusal consumes no health or world sequence; the windup still cannot replay.
    Enemy->bStrikeImpactPending = false;
    uint64 CandidateSequence = Mode->LastNativeDamageStrike;
    const uint64 DamageId = Jadebound::Combat::NativeStrikeSequence::TryIssue(CandidateSequence);
    auto CandidateLife = *NativeLifeState;
    if (DamageId == 0 || !Jadebound::Combat::PlayerLife::TryDamage(CandidateLife, DamageId, ExpectedGeneration, Now)) return false;
    // Both POD writes precede callbacks. Issue at impact, so overlapping windups
    // landing in either order cannot suppress one another through LastStrike.
    Mode->LastNativeDamageStrike = CandidateSequence;
    *NativeLifeState = CandidateLife;
    const TWeakObjectPtr<AJadeboundProgressionActor> Self(this);
    auto CanContinue = [&]()
    {
        return Self.IsValid() && !Self->IsActorBeingDestroyed() && !bPersistenceEndingPlay &&
            IsValid(World) && GetWorld() == World && !World->bIsTearingDown && NativeLifeState;
    };
    if (!Jadebound::Combat::PlayerLife::IsAlive(*NativeLifeState))
    {
        CancelRecipientAttack();
        if (!CanContinue()) return true; // Damage already committed before callback teardown.
        ShowMenu(false);
        if (!CanContinue()) return true;
        bAuthorityMenuOpen = false;
    }
    Feedback = FText::FromString(NativeLifeState->Health == 0 ?
        TEXT("You have fallen. Your spirit needs ten seconds before you can revive.") : TEXT("The raider struck you. Move out of its windup."));
    PublishNativePlayerLife();
    return true;
}
bool AJadeboundProgressionActor::CanNativeRevive(uint64 ExpectedGeneration) const
{
    const UWorld* World = GetWorld();
    if (!World || World->bIsTearingDown || World->IsPaused() || bPersistenceEndingPlay || IsActorBeingDestroyed() || bNativeReviveInProgress ||
        !bDevelopmentModel || !NativePlayerLife.bInitialized || NativePlayerLife.Health != 0 ||
        NativePlayerLife.Generation != ExpectedGeneration || !IsValid(Player) || !IsValid(Hero) ||
        Player->GetPawn() != Hero || Hero->GetController() != Player) return false;
    const AGameStateBase* GameState = World->GetGameState();
    const double Now = HasAuthority() ? World->GetTimeSeconds() :
        (GameState ? GameState->GetServerWorldTimeSeconds() : -1);
    if (!FMath::IsFinite(Now) || !FMath::IsFinite(NativePlayerLife.ReviveServerTime) || Now < NativePlayerLife.ReviveServerTime) return false;
    if (!HasAuthority()) return true; // presentation only; RPC is revalidated
    const auto* Mode = Cast<AJadeboundDevelopmentGameMode>(World->GetAuthGameMode());
    return Mode && Mode->OwnsNativeCombatant(this, false) && NativeLifeState && bNativeReviveHomeSet &&
        !NativeReviveHome.ContainsNaN() && Jadebound::Combat::PlayerLife::CanRevive(*NativeLifeState, ExpectedGeneration, Now);
}
bool AJadeboundProgressionActor::RequestNativeRevive(uint64 ExpectedGeneration)
{
    if (!CanNativeRevive(ExpectedGeneration)) return false;
    if (!HasAuthority()) { ServerRequestNativeRevive(ExpectedGeneration); return false; }
    ACharacter* Character = Cast<ACharacter>(Hero);
    if (!Character || !Character->GetCapsuleComponent()) return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(JadeboundPlayerRevive), false);
    Query.AddIgnoredActor(Character);
    const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
    if (GetWorld()->OverlapBlockingTestByChannel(NativeReviveHome.GetLocation(), NativeReviveHome.GetRotation(), ECC_Pawn,
        FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Query))
    { Feedback = FText::FromString(TEXT("Your return point is occupied. Try again when it is clear.")); return false; }
    auto Candidate = *NativeLifeState;
    if (!Jadebound::Combat::PlayerLife::TryRevive(Candidate, ExpectedGeneration, GetWorld()->GetTimeSeconds())) return false;
    TGuardValue<bool> Reviving(bNativeReviveInProgress, true);
    const TWeakObjectPtr<AJadeboundProgressionActor> Self(this);
    const TWeakObjectPtr<ACharacter> Pawn(Character);
    // Keep the same possessed body: no Blueprint destroy callback, no additional
    // profile, and no inventory/reward transaction is created by resurrection.
    if (!Character->TeleportTo(NativeReviveHome.GetLocation(), NativeReviveHome.Rotator(), false, true)) return false;
    if (!Self.IsValid() || Self->IsActorBeingDestroyed() || !Pawn.IsValid() || Pawn->IsActorBeingDestroyed() ||
        !GetWorld() || GetWorld()->bIsTearingDown || Hero != Pawn.Get() || !IsValid(Player) ||
        Player->GetPawn() != Pawn.Get() || Pawn->GetController() != Player || !NativeLifeState ||
        NativeLifeState->Generation != ExpectedGeneration || NativeLifeState->Health != 0) return false;
    *NativeLifeState = Candidate;
    bAwaitingPointerRelease = true;
    NativeCloseGuardUntil = GetWorld()->GetTimeSeconds() + .20;
    Feedback = FText::FromString(TEXT("You have revived at the courtyard. Your equipment and progress are preserved."));
    PublishNativePlayerLife();
    ApplyEquipment();
    return true;
}
void AJadeboundProgressionActor::ServerRequestNativeRevive_Implementation(uint64 ExpectedGeneration)
{
    const bool Ok = RequestNativeRevive(ExpectedGeneration);
    AcknowledgeAction(TEXT("Revive"), Ok);
}
