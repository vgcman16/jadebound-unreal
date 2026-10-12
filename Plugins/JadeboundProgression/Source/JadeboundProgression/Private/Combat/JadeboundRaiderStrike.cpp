#include "JadeboundNativeRaider.h"
#include "JadeboundProgressionActor.h"
#include "JadeboundDevelopmentGameMode.h"
#include "Combat/RecipientCombatRules.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

namespace
{
constexpr double WindupSeconds = .70;
constexpr double RecoverySeconds = 1.60;
constexpr double ImpactWindowSeconds = .20;
}
bool AJadeboundNativeRaider::IsNativeStrikeReachable(const APawn* Pawn) const
{
    if (!IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || Pawn->GetWorld() != GetWorld() || !GetWorld() ||
        !Jadebound::Combat::CanReach(GetActorLocation(),GetActorForwardVector(),Pawn->GetActorLocation(),1)) return false;
    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(JadeboundRaiderStrike), false);
    Query.AddIgnoredActor(this);
    const bool Blocked = GetWorld()->LineTraceSingleByChannel(Hit, GetActorLocation()+FVector(0,0,20),
        Pawn->GetActorLocation()+FVector(0,0,20), ECC_Visibility, Query);
    return !Blocked || Hit.GetActor() == Pawn;
}
AJadeboundProgressionActor* AJadeboundDevelopmentGameMode::SelectNativeStrikeTarget(const AJadeboundNativeRaider* Enemy) const
{
    if (!Enemy || Enemy != GetRecipientRaider() || !Enemy->Life.bAlive) return nullptr;
    AJadeboundProgressionActor* Chosen = nullptr;
    double Closest = 180. * 180.;
    for (const auto& Pair : Models)
    {
        AJadeboundProgressionActor* Model = Pair.Value.Get();
        if (!OwnsNativeCombatant(Model, true) || Model->Hero->GetActorLocation().ContainsNaN()) continue;
        const FVector Delta = Model->Hero->GetActorLocation() - Enemy->GetActorLocation();
        if (FMath::Abs(Delta.Z) > 100) continue;
        const double Distance = Delta.SizeSquared2D();
        if (!FMath::IsFinite(Distance) || Distance > Closest) continue;
        // Stable server choice for ties, independent of map iteration order.
        if (Chosen && Distance == Closest && Model->ServerCharacterId >= Chosen->ServerCharacterId) continue;
        Closest = Distance;
        Chosen = Model;
    }
    return Chosen;
}
void AJadeboundNativeRaider::CancelNativeStrike(double Now)
{
    Strike.bWindup = false;
    bStrikeImpactPending = false;
    StrikeRecipient.Reset();
    StrikePawn.Reset();
    StrikePlayerGeneration = 0;
    StrikeEnemyLife = 0;
    if (FMath::IsFinite(Now) && Now >= 0) NextStrikeAt = Now + RecoverySeconds;
    OnRepLife();
    ForceNetUpdate();
}
void AJadeboundNativeRaider::TickNativeStrike()
{
    UWorld* World = GetWorld();
    if (!HasAuthority() || !World || World->bIsTearingDown || World->IsPaused() || IsActorBeingDestroyed()) return;
    const double Now = World->GetTimeSeconds();
    if (!FMath::IsFinite(Now) || Now < 0 || !FMath::IsFinite(Now + RecoverySeconds + WindupSeconds)) return;
    auto* Mode = Cast<AJadeboundDevelopmentGameMode>(World->GetAuthGameMode());
    if (!Life.bAlive || Life.Health <= 0 || !Mode || Mode->GetRecipientRaider() != this)
    {
        if (Strike.bWindup || StrikeRecipient.IsValid()) CancelNativeStrike(Now);
        return;
    }
    if (Strike.bWindup)
    {
        AJadeboundProgressionActor* Model = StrikeRecipient.Get();
        if (!Mode->OwnsNativeCombatant(Model, true) || StrikePawn.Get() != Model->Hero ||
            Model->NativePlayerLife.Generation != StrikePlayerGeneration || Life.LifeId != StrikeEnemyLife)
        { CancelNativeStrike(Now); return; }
        if (Now < Strike.ImpactServerTime) return;
        // Latch before calling the recipient. Even misses/late ticks spend this
        // attack; it can never reacquire a different player during this windup.
        Strike.bWindup = false;
        NextStrikeAt = Now + RecoverySeconds;
        const TWeakObjectPtr<AJadeboundNativeRaider> Self(this);
        if (Now <= Strike.ImpactServerTime + ImpactWindowSeconds)
        {
            bStrikeImpactPending = true;
            Model->ApplyNativePlayerStrike(this, Strike.StrikeId, StrikePlayerGeneration, StrikeEnemyLife);
        }
        if (!Self.IsValid() || Self->IsActorBeingDestroyed() || World->bIsTearingDown) return;
        bStrikeImpactPending = false;
        StrikeRecipient.Reset();
        StrikePawn.Reset();
        OnRepLife();
        ForceNetUpdate();
        return;
    }
    if (Now < NextStrikeAt || Strike.StrikeId == MAX_uint64) return;
    AJadeboundProgressionActor* Target = Mode->SelectNativeStrikeTarget(this);
    if (!Target) return;
    FVector Direction = Target->Hero->GetActorLocation() - GetActorLocation();
    Direction.Z = 0;
    if (!Direction.IsNearlyZero()) SetActorRotation(Direction.Rotation());
    if (!IsNativeStrikeReachable(Target->Hero)) return;
    ++Strike.StrikeId;
    Strike.bWindup = true;
    Strike.ImpactServerTime = Now + WindupSeconds;
    StrikeRecipient = Target;
    StrikePawn = Target->Hero;
    StrikePlayerGeneration = Target->NativePlayerLife.Generation;
    StrikeEnemyLife = Life.LifeId;
    OnRepLife();
    ForceNetUpdate();
}
