#include "JadeboundMeleePresentationComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"

UJadeboundMeleePresentationComponent::UJadeboundMeleePresentationComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
    SetIsReplicatedByDefault(true);
}
void UJadeboundMeleePresentationComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UJadeboundMeleePresentationComponent, Presentation);
}
double UJadeboundMeleePresentationComponent::ServerTime() const
{
    if (!GetWorld()) return 0;
    if (const AGameStateBase* State = GetWorld()->GetGameState()) return State->GetServerWorldTimeSeconds();
    return GetWorld()->GetTimeSeconds();
}
void UJadeboundMeleePresentationComponent::PublishFromAuthority(int64 Id, UAnimMontage* Montage,
    double Start, double End, bool bPlay)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || Id <= 0) return;
    Presentation.SwingId = Id;
    Presentation.Montage = Montage;
    Presentation.ServerStartTime = Start;
    Presentation.ServerEndTime = End;
    Presentation.bPlaying = bPlay;
    ApplyPresentation();
    GetOwner()->ForceNetUpdate();
}
void UJadeboundMeleePresentationComponent::OnRepPresentation() { ApplyPresentation(); }
void UJadeboundMeleePresentationComponent::TickComponent(float Seconds, ELevelTick Type, FActorComponentTickFunction* Function)
{
    Super::TickComponent(Seconds, Type, Function);
    ApplyPresentation(); // Also retries after pawn Blueprint BeginPlay / AnimInstance creation.
}
void UJadeboundMeleePresentationComponent::ApplyPresentation()
{
    ACharacter* Character = Cast<ACharacter>(GetOwner());
    if (!Character || !Character->HasActorBegunPlay()) return;
    UAnimInstance* Animation = Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
    if (!Animation) return;
    const double Now = ServerTime();
    if (!FMath::IsFinite(Now) || !FMath::IsFinite(Presentation.ServerStartTime) ||
        !FMath::IsFinite(Presentation.ServerEndTime)) { StopPresentation(); return; }
    if (!Presentation.bPlaying || !Presentation.Montage || Now >= Presentation.ServerEndTime)
    { StopPresentation(); return; }
    if (Now < Presentation.ServerStartTime) return; // Await client server-time synchronization.
    if (Presentation.SwingId <= AppliedSwingId) return;
    const float Position = static_cast<float>(FMath::Max(0.0, Now - Presentation.ServerStartTime));
    // No PlayMontage Blueprint proxy, buffered combo callback, or stop-all flag.
    // Authored notifies/root motion still require native network qualification.
    if (Animation->Montage_Play(Presentation.Montage, 1.f,
        EMontagePlayReturnType::MontageLength, Position, false) > 0.f)
    {
        PlayingMontage = Presentation.Montage;
        AppliedSwingId = Presentation.SwingId;
    }
}
bool UJadeboundMeleePresentationComponent::HasActivePresentation(int64 SwingId) const
{
    const ACharacter* Character = Cast<ACharacter>(GetOwner());
    const UAnimInstance* Animation = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
    return Animation && Presentation.bPlaying && Presentation.SwingId == SwingId &&
        AppliedSwingId == SwingId && PlayingMontage && PlayingMontage == Presentation.Montage &&
        Animation->Montage_IsActive(PlayingMontage);
}
void UJadeboundMeleePresentationComponent::StopPresentation()
{
    if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
        if (UAnimInstance* Animation = Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr)
            if (PlayingMontage && Animation->Montage_IsActive(PlayingMontage))
                Animation->Montage_Stop(.08f, PlayingMontage);
    PlayingMontage = nullptr;
}
void UJadeboundMeleePresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    StopPresentation();
    Super::EndPlay(Reason);
}
