#include "JadeboundOriginalHeroAnimInstance.h"
#include "JadeboundOriginalHeroMovementComponent.h"
#include "JadeboundOriginalHeroMovementProfile.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "AnimNodes/AnimNode_Slot.h"
#include "AnimationRuntime.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
enum EClip { Idle, Walk, Jog, Jump, Fall, Land, ClipCount };

// Plain native node: no generated Blueprint node data or constant-folded properties.
struct FOriginalHeroLocomotionNode final : FAnimNode_Base
{
    FAnimNode_SequencePlayer_Standalone Players[ClipCount];
    float Weights[ClipCount] = {1.f, 0.f, 0.f, 0.f, 0.f, 0.f};
    float Speed = 0.f;
    float VerticalSpeed = 0.f;
    float RunSpeed = 0.f;
    float LandingRemaining = 0.f;
    bool bFalling = false;
    bool bWasFalling = false;
    bool bHaveFirstSample = false;
    bool bInputValid = false;

    void Initialize_AnyThread(const FAnimationInitializeContext& Context) override
    {
        FAnimNode_Base::Initialize_AnyThread(Context);
        for (auto& Player : Players) Player.Initialize_AnyThread(Context);
        for (int32 I=0; I<ClipCount; ++I) Weights[I] = I==Idle ? 1.f : 0.f;
        LandingRemaining = 0.f;
        bWasFalling = false;
        bHaveFirstSample = false;
    }
    void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override
    {
        for (auto& Player : Players) Player.CacheBones_AnyThread(Context);
    }
    void Update_AnyThread(const FAnimationUpdateContext& Context) override
    {
        const float Dt = FMath::IsFinite(Context.GetDeltaTime()) ? FMath::Clamp(Context.GetDeltaTime(), 0.f, .1f) : 0.f;
        float Desired[ClipCount] = {};
        if (!bInputValid) Desired[Idle] = 1.f;
        else
        {
            if (bFalling && (!bHaveFirstSample || !bWasFalling))
            {
                Players[Jump].SetAccumulatedTime(0.f);
                Players[Fall].SetAccumulatedTime(0.f);
                LandingRemaining = 0.f;
            }
            if (bHaveFirstSample && bWasFalling && !bFalling)
            {
                Players[Land].SetAccumulatedTime(0.f);
                // Presentation blend only; it neither delays nor locks actual movement.
                LandingRemaining = FMath::Min(.22f, Players[Land].GetCurrentAssetLength());
            }
            if (bFalling) Desired[VerticalSpeed > 5.f ? Jump : Fall] = 1.f;
            else if (LandingRemaining > 0.f)
            {
                Desired[Land] = 1.f;
                LandingRemaining = FMath::Max(0.f, LandingRemaining-Dt);
            }
            else
            {
                // Existing Conquer policy: walk cap is exactly 45% of the admitted run cap.
                const float WalkSpeed = RunSpeed*.45f;
                const float Moving = FMath::Clamp(Speed/FMath::Max(1.f, WalkSpeed), 0.f, 1.f);
                const float Running = FMath::Clamp((Speed-WalkSpeed)/FMath::Max(1.f, RunSpeed-WalkSpeed), 0.f, 1.f);
                Desired[Idle] = 1.f-Moving;
                Desired[Walk] = Moving*(1.f-Running);
                Desired[Jog] = Moving*Running;
            }
            bWasFalling = bFalling;
            bHaveFirstSample = true;
        }
        const float Alpha = 1.f-FMath::Exp(-Dt/0.08f);
        float Sum = 0.f;
        for (int32 I=0; I<ClipCount; ++I)
        {
            Weights[I] = FMath::Lerp(Weights[I], Desired[I], Alpha);
            if (Weights[I] < .0001f) Weights[I] = 0.f;
            Sum += Weights[I];
        }
        if (Sum <= 0.f) { Weights[Idle] = 1.f; Sum = 1.f; }
        for (int32 I=0; I<ClipCount; ++I)
        {
            Weights[I] /= Sum;
            if (Weights[I] > 0.f) Players[I].Update_AnyThread(Context.FractionalWeight(Weights[I]));
        }
    }
    void Evaluate_AnyThread(FPoseContext& Output) override
    {
        float Accumulated = 0.f;
        for (int32 I=0; I<ClipCount; ++I)
        {
            if (Weights[I] <= 0.f) continue;
            if (Accumulated == 0.f) Players[I].Evaluate_AnyThread(Output);
            else
            {
                FPoseContext Next(Output);
                Players[I].Evaluate_AnyThread(Next);
                FAnimationPoseData OutputData(Output), NextData(Next);
                FAnimationRuntime::BlendTwoPosesTogetherInPlace(OutputData, NextData, Accumulated/(Accumulated+Weights[I]));
            }
            Accumulated += Weights[I];
        }
        if (Accumulated == 0.f) Output.ResetToRefPose();
    }
};
}

struct FJadeboundOriginalHeroAnimProxy final : FAnimInstanceProxy
{
    explicit FJadeboundOriginalHeroAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}
    FOriginalHeroLocomotionNode Locomotion;
    FAnimNode_Slot Slot;

    void Initialize(UAnimInstance* Instance) override
    {
        auto* Typed = CastChecked<UJadeboundOriginalHeroAnimInstance>(Instance);
        auto* Character = Cast<ACharacter>(Instance->GetOwningActor());
        auto* Presentation = Character ? Character->FindComponentByClass<UJadeboundOriginalHeroMovementComponent>() : nullptr;
        const auto* Profile = Presentation ? Presentation->GetProfile() : nullptr;
        Typed->Clips.Reset();
        if (Profile && Profile->Clips.Num()==ClipCount) Typed->Clips = Profile->Clips;
        for (int32 I=0; I<ClipCount; ++I)
        {
            auto& Player = Locomotion.Players[I];
            Player.SetSequence(Typed->Clips.IsValidIndex(I) ? Typed->Clips[I].Get() : nullptr);
            Player.SetLoopAnimation(I==Idle || I==Walk || I==Jog || I==Fall);
            Player.SetPlayRate(1.f);
            Player.SetStartPosition(0.f);
            if (I==Walk || I==Jog)
            {
                Player.SetGroupName(TEXT("JB137Ground"));
                Player.SetGroupMethod(EAnimSyncMethod::SyncGroup);
                Player.SetGroupRole(EAnimGroupRole::CanBeLeader);
            }
        }
        Locomotion.RunSpeed = Presentation ? Presentation->GetRunSpeed() : 0.f;
        Slot.SlotName = TEXT("DefaultSlot");
        Slot.bAlwaysUpdateSourcePose = true;
        Slot.Source.SetLinkNode(&Locomotion);
        FAnimInstanceProxy::Initialize(Instance);
    }
    FAnimNode_Base* GetCustomRootNode() override { return &Slot; }
    void GetCustomNodes(TArray<FAnimNode_Base*>& Nodes) override
    {
        Nodes.Add(&Slot); Nodes.Add(&Locomotion);
        for (auto& Player : Locomotion.Players) Nodes.Add(&Player);
    }
    void PreUpdate(UAnimInstance* Instance, float Dt) override
    {
        FAnimInstanceProxy::PreUpdate(Instance, Dt);
        // Game-thread read once. Worker Update/Evaluate only consume copied scalar state.
        const auto* Character = Cast<ACharacter>(Instance->GetOwningActor());
        const auto* Movement = Character ? Character->GetCharacterMovement() : nullptr;
        const FVector Velocity = Character ? Character->GetVelocity() : FVector::ZeroVector;
        Locomotion.bInputValid = Movement && !Velocity.ContainsNaN() && FMath::IsFinite(Dt) &&
            FMath::IsFinite(Locomotion.RunSpeed) && Locomotion.RunSpeed>0.f &&
            (Movement->IsMovingOnGround() || Movement->IsFalling()) && Velocity.SizeSquared()<FMath::Square(2500.f);
        Locomotion.Speed = Locomotion.bInputValid ? Velocity.Size2D() : 0.f;
        Locomotion.VerticalSpeed = Locomotion.bInputValid ? Velocity.Z : 0.f;
        Locomotion.bFalling = Locomotion.bInputValid && Movement->IsFalling();
    }
};

UJadeboundOriginalHeroAnimInstance::UJadeboundOriginalHeroAnimInstance()
{
    RootMotionMode = ERootMotionMode::IgnoreRootMotion;
}
FAnimInstanceProxy* UJadeboundOriginalHeroAnimInstance::CreateAnimInstanceProxy()
{
    return new FJadeboundOriginalHeroAnimProxy(this);
}
void UJadeboundOriginalHeroAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy)
{
    delete static_cast<FJadeboundOriginalHeroAnimProxy*>(Proxy);
}
