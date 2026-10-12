#include "JadeboundEquipmentPresentationComponent.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateNoResource.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/ProgressBar.h"
#include "Components/WidgetComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"

UJadeboundEquipmentPresentationComponent::UJadeboundEquipmentPresentationComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
    SetIsReplicatedByDefault(true);
}
void UJadeboundEquipmentPresentationComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UJadeboundEquipmentPresentationComponent, Appearance);
}
void UJadeboundEquipmentPresentationComponent::PublishFromAuthority(int32 Id, UStaticMesh* Glaive, UStaticMesh* Spear)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || Id < 0 || Id > 2) return;
    if (Appearance.WeaponItemId != Id || Appearance.GlaiveMesh != Glaive || Appearance.SpearMesh != Spear)
    {
        Appearance.WeaponItemId = Id;
        Appearance.GlaiveMesh = Glaive;
        Appearance.SpearMesh = Spear;
        Appearance.Generation = Appearance.Generation == MAX_int32 ? 1 : Appearance.Generation + 1;
        GetOwner()->ForceNetUpdate();
    }
    ApplyAppearance();
}
void UJadeboundEquipmentPresentationComponent::OnRepAppearance() { ApplyAppearance(); }
void UJadeboundEquipmentPresentationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime, TickType, TickFunction);
    // Retry after replicated mesh references or inherited pawn components arrive.
    ApplyAppearance();
}
void UJadeboundEquipmentPresentationComponent::ApplyAppearance()
{
    ACharacter* Pawn = Cast<ACharacter>(GetOwner());
    // Initial component rep-notifies may precede the pawn's Blueprint BeginPlay.
    // Let its tag-based InitializeGlaive run before touching the weapon tag.
    if (!Pawn || !Pawn->HasActorBegunPlay()) return;
    StyleLifeBar(Pawn);
    USkeletalMeshComponent* BodyMesh = Pawn->GetMesh();
    const FName GripSocket(TEXT("hand_r"));
    if (!BodyMesh || !BodyMesh->DoesSocketExist(GripSocket)) return;
    TArray<UStaticMeshComponent*> Components;
    Pawn->GetComponents(Components);
    for (UStaticMeshComponent* Weapon : Components)
    {
        if (Weapon->GetName() != TEXT("JadeGlaive")) continue;
        // Repair the existing inherited component on every observing machine.
        // Both original polearms share the saved +Z grip convention and TRS.
        if (Weapon->GetAttachParent() != BodyMesh || Weapon->GetAttachSocketName() != GripSocket)
        {
            if (!Weapon->AttachToComponent(BodyMesh, FAttachmentTransformRules::KeepRelativeTransform, GripSocket)) return;
        }
        const FTransform GripTransform(FRotator(-90.0, 0.0, 0.0), FVector::ZeroVector, FVector::OneVector);
        if (!Weapon->GetRelativeTransform().Equals(GripTransform, KINDA_SMALL_NUMBER))
        {
            Weapon->SetRelativeTransform(GripTransform);
        }
        // Foundation combat is deliberately unavailable. Never admit the PC0
        // legacy driver through a public visual component's equipped tag.
        Weapon->ComponentTags.Remove(FName(TEXT("JadeGlaiveV001")));
        Weapon->SetOnlyOwnerSee(false);
        Weapon->SetOwnerNoSee(false);
        const bool bVisible = Appearance.WeaponItemId != 0;
        UStaticMesh* Desired = Appearance.WeaponItemId == 2 ? Appearance.SpearMesh.Get() : Appearance.GlaiveMesh.Get();
        if (bVisible && !Desired) return;
        if (bVisible && Weapon->GetStaticMesh() != Desired)
        {
            Weapon->SetStaticMesh(Desired);
            Weapon->EmptyOverrideMaterials();
        }
        Weapon->SetVisibility(bVisible, true);
        Weapon->SetHiddenInGame(!bVisible, true);
        return;
    }
}

void UJadeboundEquipmentPresentationComponent::StyleLifeBar(ACharacter* Pawn)
{
    // The protected Hero caches/calls UI_LifeBar_C; preserve that exact object
    // and its health graph. This helper never replaces it or writes Percent.
    if (!LifeBarComponent.IsValid())
    {
        TArray<UWidgetComponent*> Components;
        Pawn->GetComponents(Components);
        for (UWidgetComponent* Component : Components)
            if (Component->GetName() == TEXT("Life Bar") &&
                Component->GetClass()->GetPathName() == TEXT("/Script/UMG.WidgetComponent"))
            {
                LifeBarComponent = Component;
                break;
            }
    }
    UWidgetComponent* Component = LifeBarComponent.Get();
    if (!Component) return;

    // BindGameplay already hides the owned bar. Also cover the interval before
    // its owner-only progression model binds; never unhide a peer/dead bar.
    if (Pawn->IsLocallyControlled())
    {
        if (Component->GetVisibleFlag()) Component->SetVisibility(false, true);
        if (!Component->bHiddenInGame) Component->SetHiddenInGame(true, true);
        return;
    }
    UUserWidget* Widget = Component->GetUserWidgetObject();
    if (!Widget || !Widget->WidgetTree || Widget == LastStyledLifeBarWidget.Get()) return;
    // Only successfully validated/styled instances are cached. Component or
    // tree initialization can finish later on the same widget object.
    if (Widget->GetClass()->GetPathName() != TEXT("/Game/Variant_Combat/UI/UI_LifeBar.UI_LifeBar_C")) return;
    UProgressBar* Bar = nullptr;
    int32 ProgressBars = 0;
    Widget->WidgetTree->ForEachWidget([&Bar, &ProgressBars](UWidget* Child)
    {
        if (UProgressBar* Candidate = Cast<UProgressBar>(Child))
        {
            Bar = Candidate;
            ++ProgressBars;
        }
    });
    UOverlay* RootOverlay = Cast<UOverlay>(Widget->WidgetTree->RootWidget);
    const int32 OverlayChildren = RootOverlay ? RootOverlay->GetChildrenCount() : 0;
    UBorder* OuterBorder = OverlayChildren == 1 ? Cast<UBorder>(RootOverlay->GetChildAt(0)) : nullptr;
    const bool bKnownChrome = RootOverlay && RootOverlay->GetName() == TEXT("Overlay_0") &&
        OuterBorder && OuterBorder->GetName() == TEXT("Border_0") && Bar &&
        Bar->GetName() == TEXT("Bar") && OuterBorder->GetContent() == Bar;
    if (ProgressBars != 1 || !bKnownChrome)
    {
        if (LastRejectedLifeBarWidget.Get() != Widget)
        {
            LastRejectedLifeBarWidget = Widget; // Diagnostic throttle, never an admission cache.
            UE_LOG(LogTemp, Warning, TEXT("Jadebound peer Life Bar style deferred: Pawn=%s ProgressBars=%d OverlayChildren=%d ExactChrome=%d"),
                *GetNameSafe(Pawn), ProgressBars, OverlayChildren, static_cast<int32>(bKnownChrome));
        }
        return;
    }

    // Quiet only this known instance's outer chrome. The real ProgressBar,
    // health value, Percent/bindings, widget class and visibility are retained.
    OuterBorder->SetPadding(FMargin(0.f));
    OuterBorder->SetBrushColor(FLinearColor::Transparent);

    // Procedural brush values carry no texture/material UObject references.
    // SetWidgetStyle copies them into the UProgressBar-owned style; its Slate
    // widget keeps that owned style pointer, not these temporary values.
    const FLinearColor DeepJade(0.018f, 0.043f, 0.037f, 1.0f);
    const FLinearColor Bronze(0.55f, 0.38f, 0.18f, 1.0f);
    const FLinearColor Jade(0.14f, 0.55f, 0.39f, 1.0f);
    FProgressBarStyle Style = Bar->GetWidgetStyle();
    Style.SetBackgroundImage(FSlateRoundedBoxBrush(DeepJade, 3.0f, Bronze, 1.0f));
    Style.SetFillImage(FSlateRoundedBoxBrush(FLinearColor::White, 2.0f));
    Style.SetMarqueeImage(FSlateNoResource());
    Style.SetEnableFillAnimation(false);
    Bar->SetWidgetStyle(Style);
    Bar->SetFillColorAndOpacity(Jade);
    Component->SetDrawAtDesiredSize(false);
    Component->SetDrawSize(FVector2D(96.0, 14.0));
    LastStyledLifeBarWidget = Widget;
    LastRejectedLifeBarWidget.Reset();
    // Preserve the saved Z30 offset, pivot, screen space and health percentage.
    UE_LOG(LogTemp, Log, TEXT("Jadebound peer Life Bar styled: Pawn=%s Widget=%s ProgressBars=1 DrawSize=96x14"), *GetNameSafe(Pawn), *GetNameSafe(Widget));
}
