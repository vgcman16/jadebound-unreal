#include "JadeboundOriginalHeroMovementComponent.h"
#include "JadeboundOriginalHeroMovementProfile.h"
#include "JadeboundOriginalHeroAnimInstance.h"
#include "JadeboundProgressionActor.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "UObject/Package.h"

namespace
{
constexpr const TCHAR* MapPackage = TEXT("/Game/Jadebound/Development/OriginalHeroPlayable137_v001/L_JB137_OriginalHeroMovement");
constexpr const TCHAR* ProfilePath = TEXT("/Game/Jadebound/Development/OriginalHeroPlayable137_v001/DA_JB137_Movement.DA_JB137_Movement");
constexpr const TCHAR* BodyPath = TEXT("/Game/Jadebound/Development/OriginalHeroV002/SK_JB132_Body.SK_JB132_Body");
constexpr const TCHAR* SkeletonPath = TEXT("/Game/Jadebound/Development/OriginalHeroV002/SK_JB132_Body_Skeleton.SK_JB132_Body_Skeleton");
constexpr const TCHAR* HeroClass = TEXT("/Game/Jadebound/Tests/GlaiveCombatV006/BP_GlaiveHero_V006.BP_GlaiveHero_V006_C");
constexpr const TCHAR* ControllerClass = TEXT("/Game/Jadebound/Tests/GlaiveCombatV006/BP_GlaiveController_V006.BP_GlaiveController_V006_C");
constexpr const TCHAR* ModeClass = TEXT("/Game/Jadebound/Tests/GlaiveCombatV006/BP_GlaiveMode_V006.BP_GlaiveMode_V006_C");

bool UnitTransform(const FTransform& Transform, double MaxDistance)
{
    return !Transform.ContainsNaN() && Transform.GetRotation().IsNormalized() &&
        Transform.GetScale3D().Equals(FVector::OneVector, .00001) &&
        Transform.GetTranslation().SizeSquared() <= MaxDistance*MaxDistance;
}
bool SameReferenceSkeleton(const USkeletalMesh* A, const USkeletalMesh* B)
{
    if (!A || !B || A->GetSkeleton()!=B->GetSkeleton()) return false;
    const auto& X=A->GetRefSkeleton(); const auto& Y=B->GetRefSkeleton();
    if (X.GetNum()!=53 || Y.GetNum()!=X.GetNum()) return false;
    for (int32 I=0; I<X.GetNum(); ++I)
        if (X.GetBoneName(I)!=Y.GetBoneName(I) || X.GetParentIndex(I)!=Y.GetParentIndex(I) ||
            !X.GetRefBonePose()[I].Equals(Y.GetRefBonePose()[I], .0001)) return false;
    return true;
}
}

bool UJadeboundOriginalHeroMovementComponent::IsMovementWorld(const UWorld* World)
{
    return IsValid(World) && World->IsGameWorld() && World->WorldType!=EWorldType::PIE &&
        World->GetOutermost()->GetName()==MapPackage;
}

bool UJadeboundOriginalHeroMovementComponent::IsReady(const ACharacter* Character)
{
    const auto* Component = IsValid(Character) ? Character->FindComponentByClass<UJadeboundOriginalHeroMovementComponent>() : nullptr;
    return Component && Component->HasOwnedPresentation(Character);
}

bool UJadeboundOriginalHeroMovementComponent::Install(ACharacter* Character, APlayerController* Player, FString& Reason)
{
    UWorld* World = Character ? Character->GetWorld() : nullptr;
    if (!IsMovementWorld(World) || World->GetNetMode()!=NM_Standalone || !IsValid(Character) || !Character->HasAuthority() ||
        !IsValid(Player) || !Player->IsLocalController() || Player->GetPawn()!=Character || Character->GetController()!=Player ||
        Character->GetClass()->GetPathName()!=HeroClass || Player->GetClass()->GetPathName()!=ControllerClass ||
        !World->GetAuthGameMode() || World->GetAuthGameMode()->GetClass()->GetPathName()!=ModeClass || World->GetNumPlayerControllers()!=1)
    { Reason=TEXT("Source137 exact standalone V006 possession contract failed."); return false; }
    int32 Models=0;
    for (TActorIterator<AJadeboundProgressionActor> It(World); It; ++It)
    {
        ++Models;
        if (It->GetOwner()!=Player) { Reason=TEXT("Source137 progression model owner mismatch."); return false; }
    }
    if (Models!=1) { Reason=TEXT("Source137 requires exactly one existing progression model."); return false; }
    TArray<UJadeboundOriginalHeroMovementComponent*> Existing;
    Character->GetComponents(Existing);
    if (Existing.Num())
    {
        if (Existing.Num()==1 && Existing[0]->HasOwnedPresentation(Character)) return true;
        Reason=TEXT("Source137 presentation was not ready or changed. Restart this test after fixing the cause.");
        return false;
    }
    auto* Component=NewObject<UJadeboundOriginalHeroMovementComponent>(Character, TEXT("JadeboundOriginalHeroMovement137"), RF_Transient);
    Character->AddInstanceComponent(Component);
    Component->RegisterComponent();
    return Component->Configure(Character, Reason);
}

bool UJadeboundOriginalHeroMovementComponent::Configure(ACharacter* Character, FString& Reason)
{
    // Validation precedes the mesh swap. A failed installation is latched; it is never retried in-place.
    Profile=LoadObject<UJadeboundOriginalHeroMovementProfile>(nullptr, ProfilePath);
    BodyComponent=Character->GetMesh();
    const auto* Movement=Character->GetCharacterMovement();
    if (!Profile || !Profile->bMeasuredBindings || !Profile->Body || !BodyComponent ||
        !Movement || !FMath::IsFinite(Movement->MaxWalkSpeed) || Movement->MaxWalkSpeed<=0.f || Movement->MaxWalkSpeed>1500.f ||
        BodyComponent->IsSimulatingPhysics() || !BodyComponent->IsRegistered() || BodyComponent->GetOwner()!=Character ||
        !Profile->Body->GetSkeleton() || Profile->Body->GetPathName()!=BodyPath ||
        Profile->Body->GetSkeleton()->GetPathName()!=SkeletonPath || Profile->Body->GetRefSkeleton().GetNum()!=53 ||
        Profile->Body->GetRefSkeleton().FindBoneIndex(TEXT("hand_r"))==INDEX_NONE ||
        Profile->CoveredBase.Num()!=6 || Profile->Clips.Num()!=6 ||
        !UnitTransform(Profile->BodyRelativeTransform, 250.0) || !UnitTransform(Profile->WeaponHandTransform, 100.0))
    { Reason=TEXT("Source137 profile, measured transforms, body, movement or six-part/clip contract failed."); return false; }
    for (int32 I=0; I<6; ++I)
    {
        const FString Name=FString::Printf(TEXT("SK_JB132_CoveredBase_Part%02d"), I);
        const FString Expected=TEXT("/Game/Jadebound/Development/OriginalHeroV002/")+Name+TEXT(".")+Name;
        if (!Profile->CoveredBase[I] || Profile->CoveredBase[I]->GetPathName()!=Expected ||
            !SameReferenceSkeleton(Profile->Body, Profile->CoveredBase[I]))
        { Reason=TEXT("Source137 covered-base skeleton or exact part identity failed."); return false; }
    }
    const TCHAR* Names[6]={TEXT("A_JB134_Idle_Template"),TEXT("A_JB136_Walk_Template"),TEXT("A_JB136_Jog_Template"),
        TEXT("A_JB136_Jump_Template"),TEXT("A_JB136_Fall_Template"),TEXT("A_JB136_Land_Template")};
    FString LocomotionPackageFolder;
    for (int32 I=0; I<6; ++I)
    {
        const auto* Clip=Profile->Clips[I].Get();
        if (!Clip || Clip->GetClass()!=UAnimSequence::StaticClass() || Clip->GetName()!=Names[I] ||
            Clip->GetSkeleton()!=Profile->Body->GetSkeleton() || Clip->GetAdditiveAnimType()!=AAT_None ||
            Clip->HasRootMotion() || (I>0 && !Clip->bForceRootLock) || Clip->RateScale!=1.f || Clip->Notifies.Num()!=0 ||
            !FMath::IsFinite(Clip->GetPlayLength()) || Clip->GetPlayLength()<.1f || Clip->GetPlayLength()>20.f)
        { Reason=TEXT("Source137 clip class, skeleton, role, base pose, notify or root-motion contract failed."); return false; }
        if (I==0 && Clip->GetPathName()!=TEXT("/Game/Jadebound/Development/OriginalHeroIdle134_v003/A_JB134_Idle_Template.A_JB134_Idle_Template"))
        { Reason=TEXT("Source137 idle must remain the qualified immutable Source134 v003 asset."); return false; }
        if (I>0)
        {
            FString Folder, Leaf; Clip->GetOutermost()->GetName().Split(TEXT("/"), &Folder, &Leaf, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
            if (!Folder.StartsWith(TEXT("/Game/Jadebound/Development/OriginalHeroLocomotion136_")) ||
                (I>1 && Folder!=LocomotionPackageFolder))
            { Reason=TEXT("Source137 locomotion must be one complete qualified Source136 output set."); return false; }
            LocomotionPackageFolder=Folder;
        }
    }
    TArray<UStaticMeshComponent*> Weapons; Character->GetComponents(Weapons);
    int32 WeaponCount=0;
    for (auto* Item : Weapons)
        if (Item->GetName()==TEXT("JadeGlaive")) { WeaponComponent=Item; ++WeaponCount; }
    if (WeaponCount!=1 || !WeaponComponent || WeaponComponent->GetOwner()!=Character ||
        !WeaponComponent->GetStaticMesh() || WeaponComponent->GetStaticMesh()->GetPathName()!=
            TEXT("/Game/Jadebound/OriginalWeapons/JadeGlaiveV001/SM_JadeGlaive_CM_V001.SM_JadeGlaive_CM_V001"))
    { Reason=TEXT("Source137 requires the exact existing JadeGlaive component and original mesh before equipment binding."); return false; }

    RunSpeed=Movement->MaxWalkSpeed; // Capture BEFORE the existing Conquer policy applies its .45 walk cap.
    // No qualified original-body ragdoll exists. Refuse damage rather than invoking the legacy HandleDeath graph.
    Character->SetCanBeDamaged(false);
    WeaponComponent->ComponentTags.Remove(TEXT("JadeGlaiveV001"));
    BodyComponent->SetAnimInstanceClass(nullptr);
    BodyComponent->EmptyOverrideMaterials();
    BodyComponent->SetSkeletalMesh(Profile->Body, true);
    BodyComponent->SetPhysicsAsset(nullptr);
    BodyComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BodyComponent->SetGenerateOverlapEvents(false);
    BodyComponent->SetRelativeTransform(Profile->BodyRelativeTransform);
    BodyComponent->SetVisibility(true);
    BodyComponent->SetHiddenInGame(false);
    BodyComponent->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    BodyComponent->SetAnimInstanceClass(UJadeboundOriginalHeroAnimInstance::StaticClass());

    for (int32 I=0; I<6; ++I)
    {
        auto* Part=NewObject<USkeletalMeshComponent>(Character, *FString::Printf(TEXT("JB137CoveredBase%02d"), I), RF_Transient);
        Character->AddInstanceComponent(Part);
        Part->SetupAttachment(BodyComponent);
        Part->SetRelativeTransform(FTransform::Identity);
        Part->SetSkeletalMesh(Profile->CoveredBase[I], true);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetGenerateOverlapEvents(false);
        Part->SetCanEverAffectNavigation(false);
        Part->SetLeaderPoseComponent(BodyComponent, true, false);
        Part->RegisterComponent();
        Followers.Add(Part);
    }
    const bool Attached=WeaponComponent->AttachToComponent(BodyComponent, FAttachmentTransformRules::KeepRelativeTransform, TEXT("hand_r"));
    WeaponComponent->SetRelativeTransform(Profile->WeaponHandTransform);
    bReady=Attached;
    if (!HasOwnedPresentation(Character))
    { bReady=false; Reason=TEXT("Source137 mesh/AnimInstance/leader-pose/weapon readback failed; this test must remain paused."); return false; }
    UE_LOG(LogTemp, Display, TEXT("Source137 movement/equipment preview ready: original body, six leader-pose parts, DefaultSlot and hand_r. Combat/death unavailable."));
    return true;
}

bool UJadeboundOriginalHeroMovementComponent::HasOwnedPresentation(const ACharacter* Character) const
{
    if (!bReady || !IsValid(Character) || Character!=GetOwner() || !IsMovementWorld(Character->GetWorld()) ||
        !Profile || !BodyComponent || Character->GetMesh()!=BodyComponent || BodyComponent->GetSkeletalMeshAsset()!=Profile->Body ||
        BodyComponent->GetAnimClass()!=UJadeboundOriginalHeroAnimInstance::StaticClass() ||
        !Cast<UJadeboundOriginalHeroAnimInstance>(BodyComponent->GetAnimInstance()) || BodyComponent->IsSimulatingPhysics() ||
        BodyComponent->GetCollisionEnabled()!=ECollisionEnabled::NoCollision || Character->CanBeDamaged() ||
        !BodyComponent->GetRelativeTransform().Equals(Profile->BodyRelativeTransform, .0001) || Followers.Num()!=6 ||
        !WeaponComponent || WeaponComponent->GetOwner()!=Character || WeaponComponent->GetAttachParent()!=BodyComponent ||
        WeaponComponent->GetAttachSocketName()!=TEXT("hand_r") ||
        !WeaponComponent->GetRelativeTransform().Equals(Profile->WeaponHandTransform, .0001)) return false;
    for (int32 I=0; I<6; ++I)
    {
        const auto* Part=Followers[I].Get();
        if (!IsValid(Part) || !Part->IsRegistered() || Part->GetOwner()!=Character || Part->GetAttachParent()!=BodyComponent ||
            Part->GetSkeletalMeshAsset()!=Profile->CoveredBase[I] || Part->LeaderPoseComponent.Get()!=BodyComponent ||
            !Part->GetRelativeTransform().Equals(FTransform::Identity, .0001) || Part->IsSimulatingPhysics() ||
            Part->GetCollisionEnabled()!=ECollisionEnabled::NoCollision) return false;
    }
    return true;
}

void UJadeboundOriginalHeroMovementComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    bReady=false;
    // The pawn/world owns every transient mesh; normal actor teardown releases them.
    // Hot-removing this component is unsupported and the map admission will fail closed.
    Super::EndPlay(Reason);
}
