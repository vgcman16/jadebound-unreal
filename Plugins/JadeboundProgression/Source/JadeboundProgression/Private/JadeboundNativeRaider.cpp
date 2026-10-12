#include "JadeboundNativeRaider.h"
#include "JadeboundDevelopmentGameMode.h"
#include "JadeboundProgressionActor.h"
#include "Camera/PlayerCameraManager.h"
#include "CollisionShape.h"
#include "Combat/RecipientCombatRules.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"

AJadeboundNativeRaider::AJadeboundNativeRaider()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    bReplicates = true;
    bAlwaysRelevant = true; // Dead/hidden life transitions must reach both clients.
    SetReplicateMovement(true);
    AutoPossessAI = EAutoPossessAI::Disabled;
    GetCapsuleComponent()->InitCapsuleSize(42.f,92.f);
    GetMesh()->SetRelativeLocation(FVector(0,0,-92));
    GetMesh()->SetRelativeRotation(FRotator(0,-90,0));
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    HealthLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("RaiderHealth"));
    HealthLabel->SetupAttachment(GetCapsuleComponent());
    HealthLabel->SetRelativeLocation(FVector(0,0,130));
    HealthLabel->SetWorldSize(18.f);
    HealthLabel->SetHorizontalAlignment(EHTA_Center);
    HealthLabel->SetTextRenderColor(FColor(240,212,167));
    HealthLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
bool AJadeboundNativeRaider::HasValidPresentation() const
{
    const UAnimSequence* Sequence = Cast<UAnimSequence>(IdleAnimation);
    return RaiderMesh && Sequence && Sequence->Notifies.Num() == 0 && !Sequence->HasRootMotion() && RaiderMesh->GetSkeleton() &&
        RaiderMesh->GetSkeleton() == IdleAnimation->GetSkeleton();
}
void AJadeboundNativeRaider::BeginPlay()
{
    Super::BeginPlay();
    Home = GetActorTransform();
    GetCharacterMovement()->DisableMovement();
    if (HasValidPresentation())
    {
        GetMesh()->SetSkeletalMeshAsset(RaiderMesh);
        GetMesh()->PlayAnimation(IdleAnimation, true);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Jadebound native raider requires a reviewed mesh and compatible real idle animation."));
        if (HasAuthority()) Destroy();
        return;
    }
    OnRepLife();
}
void AJadeboundNativeRaider::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AJadeboundNativeRaider, Life);
}
float AJadeboundNativeRaider::TakeDamage(float, const FDamageEvent&, AController*, AActor*)
{
    // Generic ApplyDamage/legacy Blueprint causers cannot bypass native attack
    // identity, timing, facing, obstruction, equipment and swing/life checks.
    return 0.f;
}
bool AJadeboundNativeRaider::ApplyValidatedHit(AJadeboundProgressionActor* Recipient,
    APawn* Attacker, int64 SwingId, int64 ExpectedLife)
{
    UWorld* World = GetWorld();
    if (!HasAuthority() || !IsValid(World) || World->bIsTearingDown || IsActorBeingDestroyed()) return false;
    const double Now = World->GetTimeSeconds();
    if (!FMath::IsFinite(Now) || Now < 0.0) return false;
    AJadeboundDevelopmentGameMode* Mode = Cast<AJadeboundDevelopmentGameMode>(World->GetAuthGameMode());
    if (!IsValid(Mode) || Mode->GetRecipientRaider() != this || !Mode->OwnsRecipientModel(Recipient, true) ||
        !IsValid(Attacker) || Attacker->IsActorBeingDestroyed() || Attacker->GetWorld() != World ||
        !Life.bAlive || Life.Health <= 0) return false;
    const APlayerController* AttackerController = Cast<APlayerController>(Recipient->GetOwner());
    if (!AttackerController || AttackerController->GetPawn() != Attacker || Attacker->GetController() != AttackerController) return false;
    const TWeakObjectPtr<AJadeboundProgressionActor> Key(Recipient);
    const int64 Previous = LastHitSwing.FindRef(Key);
    if (!Jadebound::Combat::IsNewHit(SwingId,ExpectedLife,Life.LifeId,Previous)) return false;
    if (!LastHitSwing.Contains(Key) && LastHitSwing.Num() >= 4) return false;
    // Commit dedup before synchronous damage/death/award callbacks.
    LastHitSwing.Add(Key,SwingId);
    LastValidRecipient = Recipient;
    --Life.Health;
    UE_LOG(LogTemp, Log, TEXT("Jadebound native hit: Enemy=%s Life=%lld Swing=%lld Recipient=%s Health=%d"),
        *GetName(),Life.LifeId,SwingId,*Recipient->GetServerCharacterId(),Life.Health);
    if (Life.Health == 0)
    {
        Life.bAlive = false;
        Life.DeathServerTime = Now;
        bDeathAwardCommitted = false;
        NextAwardCheck = 0;
    }
    OnRepLife();
    ForceNetUpdate();
    return true;
}
void AJadeboundNativeRaider::OnRepLife()
{
    GetMesh()->SetVisibility(Life.bAlive, true);
    HealthLabel->SetVisibility(Life.bAlive);
    GetCapsuleComponent()->SetCollisionEnabled(Life.bAlive ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
    HealthLabel->SetText(FText::FromString(FString::Printf(TEXT("Courtyard raider  %d/3"),Life.Health)));
}
void AJadeboundNativeRaider::Tick(float Seconds)
{
    Super::Tick(Seconds);
    UWorld* World = GetWorld();
    if (!IsValid(World) || World->bIsTearingDown || IsActorBeingDestroyed()) return;
    if (const APlayerController* Local = World->GetFirstPlayerController())
        if (Local->IsLocalController() && Local->PlayerCameraManager)
            HealthLabel->SetWorldRotation((Local->PlayerCameraManager->GetCameraLocation() - HealthLabel->GetComponentLocation()).Rotation());
    if (!HasAuthority() || Life.bAlive) return;
    const double Now = World->GetTimeSeconds();
    if (!FMath::IsFinite(Now) || Now < 0.0 || !FMath::IsFinite(Life.DeathServerTime) ||
        Life.DeathServerTime < 0.0 || Now < Life.DeathServerTime || !FMath::IsFinite(NextAwardCheck) ||
        Life.Health != 0 || Life.LifeId <= 0 || Home.ContainsNaN()) return;
    AJadeboundDevelopmentGameMode* Mode = Cast<AJadeboundDevelopmentGameMode>(World->GetAuthGameMode());
    if (!bDeathAwardCommitted && Now >= NextAwardCheck)
    {
        NextAwardCheck = Now + .5;
        if (Mode) bDeathAwardCommitted = Mode->TryCommitRaiderDeath(this);
    }
    // A readback failure keeps this exact dead life/recipient. No new life or
    // second allocator call until the held candidate is durably verified.
    if (!bDeathAwardCommitted || Now < Life.DeathServerTime + Jadebound::Combat::RespawnSeconds || Life.LifeId == MAX_int64) return;
    if (!Mode || !Mode->AreRecipientSwingsIdle()) return;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(JadeboundNativeRespawn),false);
    Query.AddIgnoredActor(this);
    if (GetWorld()->OverlapBlockingTestByChannel(Home.GetLocation(),Home.GetRotation(),ECC_Pawn,
        FCollisionShape::MakeCapsule(GetCapsuleComponent()->GetScaledCapsuleRadius(),GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),Query)) return;
    SetActorTransform(Home,false,nullptr,ETeleportType::TeleportPhysics);
    ++Life.LifeId;
    Life.Health = 3; Life.bAlive = true; Life.DeathServerTime = 0;
    LastHitSwing.Empty(); LastValidRecipient.Reset(); bDeathAwardCommitted = false;
    OnRepLife(); ForceNetUpdate();
}
