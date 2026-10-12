#include "JadeboundPlayerLifeComponent.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/ProgressBar.h"
#include "Components/WidgetComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"

UJadeboundPlayerLifeComponent::UJadeboundPlayerLifeComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
    SetIsReplicatedByDefault(true);
}
void UJadeboundPlayerLifeComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UJadeboundPlayerLifeComponent, Life);
}
void UJadeboundPlayerLifeComponent::PublishFromAuthority(const FJadeboundPlayerLife& Value)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !Value.bInitialized || Value.Generation == 0 ||
        Value.Health < 0 || Value.Health > 100) return;
    const bool Changed = Life.bInitialized != Value.bInitialized || Life.Generation != Value.Generation ||
        Life.Health != Value.Health || Life.DeathServerTime != Value.DeathServerTime || Life.ReviveServerTime != Value.ReviveServerTime;
    Life = Value;
    ApplyLife();
    if (Changed) GetOwner()->ForceNetUpdate();
}
void UJadeboundPlayerLifeComponent::OnRepLife() { ApplyLife(); }
void UJadeboundPlayerLifeComponent::OnRegister()
{
    Super::OnRegister();
    if (APawn* Pawn = Cast<APawn>(GetOwner()))
        Pawn->ReceiveControllerChangedDelegate.AddUniqueDynamic(this, &UJadeboundPlayerLifeComponent::OnControllerChanged);
}
void UJadeboundPlayerLifeComponent::OnControllerChanged(APawn*, AController*, AController*) { ApplyLife(); }
void UJadeboundPlayerLifeComponent::UpdateNativePeerBar(ACharacter* Pawn)
{
    TArray<UWidgetComponent*> Components;
    Pawn->GetComponents(Components);
    for (auto* Component : Components)
    {
        if (Component->GetName() != TEXT("Life Bar") || Component->GetClass()->GetPathName() != TEXT("/Script/UMG.WidgetComponent")) continue;
        UUserWidget* Widget = Component->GetUserWidgetObject();
        UProgressBar* Bar = nullptr;
        if (Widget && Widget->GetClass()->GetPathName() == TEXT("/Game/Variant_Combat/UI/UI_LifeBar.UI_LifeBar_C") && Widget->WidgetTree)
        {
            UOverlay* Root = Cast<UOverlay>(Widget->WidgetTree->RootWidget);
            UBorder* Border = Root && Root->GetName() == TEXT("Overlay_0") && Root->GetChildrenCount() == 1 ? Cast<UBorder>(Root->GetChildAt(0)) : nullptr;
            Bar = Border && Border->GetName() == TEXT("Border_0") ? Cast<UProgressBar>(Border->GetContent()) : nullptr;
            if (Bar && Bar->GetName() != TEXT("Bar")) Bar = nullptr;
        }
        const bool Visible = Bar && Life.IsAlive() && !Pawn->IsLocallyControlled();
        Component->SetVisibility(Visible, true);
        Component->SetHiddenInGame(!Visible, true);
        if (Bar)
        {
            // This native instance takes its value from public replicated life.
            // Keep the reviewed widget/frame; never show stale Blueprint HP.
            Bar->PercentDelegate.Unbind();
            Bar->SetPercent(Life.Health / 100.f);
        }
    }
}
void UJadeboundPlayerLifeComponent::ReleaseInput(bool bLiveWorld)
{
    if (bLiveWorld)
        if (APlayerController* Player = InputOwner.Get())
        {
            if (bMoveLease) Player->SetIgnoreMoveInput(false);
            Player->SetShowMouseCursor(bCursorSaved);
            if (Player->GetPawn() == GetOwner() && Player->IsLocalController() && GetWorld() && GetWorld()->GetGameViewport())
                Player->SetInputMode(FInputModeGameOnly()); // leave the modal's keyboard focus at completed revive
        }
    InputOwner.Reset();
    bMoveLease = false;
}
void UJadeboundPlayerLifeComponent::ApplyLife()
{
    ACharacter* Pawn = Cast<ACharacter>(GetOwner());
    if (!IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || !GetWorld() || GetWorld()->bIsTearingDown ||
        !Life.bInitialized || !Life.Generation || Life.Health < 0 || Life.Health > 100) return;
    UCharacterMovementComponent* Movement = Pawn->GetCharacterMovement();
    UCapsuleComponent* Capsule = Pawn->GetCapsuleComponent();
    USkeletalMeshComponent* Mesh = Pawn->GetMesh();
    if (!Movement || !Capsule || !Mesh) return;
    APlayerController* Player = Cast<APlayerController>(Pawn->GetController());
    const bool LocalOwner = Player && Player->IsLocalController();
    if (InputOwner.IsValid() && (!LocalOwner || InputOwner.Get() != Player)) ReleaseInput(true);
    UpdateNativePeerBar(Pawn);
    if (Life.IsAlive())
    {
        if (bDeadApplied)
        {
            Capsule->SetCollisionEnabled(static_cast<ECollisionEnabled::Type>(SavedCollisionMode));
            Mesh->SetVisibility(bMeshVisible, true);
            Movement->SetMovementMode(static_cast<EMovementMode>(SavedMovementMode), SavedCustomMovementMode);
            ReleaseInput(true);
            bDeadApplied = false;
        }
        if (Spirit) Spirit->SetVisibility(false);
        return;
    }
    if (!bDeadApplied)
    {
        bMeshVisible = Mesh->IsVisible();
        SavedMovementMode = Movement->MovementMode;
        SavedCustomMovementMode = Movement->CustomMovementMode;
        SavedCollisionMode = Capsule->GetCollisionEnabled();
        bDeadApplied = true;
    }
    // Server movement gate also rejects remote client movement while dead.
    Movement->StopMovementImmediately();
    Movement->DisableMovement();
    Pawn->ConsumeMovementInputVector();
    Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetVisibility(false, true);
    if (LocalOwner && InputOwner.Get() != Player)
    {
        ReleaseInput(true);
        InputOwner = Player;
        bMoveLease = !Player->IsMoveInputIgnored();
        bCursorSaved = Player->ShouldShowMouseCursor();
        if (bMoveLease) Player->SetIgnoreMoveInput(true);
        Player->SetShowMouseCursor(true); // existing Slate modal consumes input
    }
    if (!bSpiritAttempted)
    {
        bSpiritAttempted = true;
        UStaticMesh* Asset = LoadObject<UStaticMesh>(nullptr,
            TEXT("/Game/Jadebound/Effects/SpiritReviveV002/SM_JadeSpirit_v002.SM_JadeSpirit_v002"));
        if (Asset)
        {
            Spirit = NewObject<UStaticMeshComponent>(Pawn, NAME_None, RF_Transient);
            Pawn->AddInstanceComponent(Spirit);
            Spirit->SetupAttachment(Capsule);
            Spirit->SetStaticMesh(Asset);
            Spirit->SetRelativeScale3D(FVector(1.65));
            Spirit->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Spirit->SetGenerateOverlapEvents(false);
            Spirit->SetCanEverAffectNavigation(false);
            Spirit->SetCastShadow(false);
            Spirit->RegisterComponent();
        }
        else UE_LOG(LogTemp, Warning, TEXT("Jadebound native ghost art unavailable; original spirit mesh is required."));
    }
    if (Spirit)
    {
        const AGameStateBase* GameState = GetWorld()->GetGameState();
        const double Now = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
        if (FMath::IsFinite(Now)) Spirit->SetRelativeLocation(FVector(0,0,60 + 2.5 * FMath::Sin((Now - Life.DeathServerTime) * 2)));
        Spirit->SetVisibility(true);
    }
}
void UJadeboundPlayerLifeComponent::TickComponent(float Seconds, ELevelTick Type, FActorComponentTickFunction* Function)
{
    Super::TickComponent(Seconds, Type, Function);
    ApplyLife();
}
void UJadeboundPlayerLifeComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (APawn* Pawn = Cast<APawn>(GetOwner()))
        Pawn->ReceiveControllerChangedDelegate.RemoveDynamic(this, &UJadeboundPlayerLifeComponent::OnControllerChanged);
    ReleaseInput(GetWorld() && !GetWorld()->bIsTearingDown && Reason == EEndPlayReason::Destroyed);
    if (Spirit && Reason == EEndPlayReason::Destroyed) Spirit->DestroyComponent();
    Spirit = nullptr;
    Super::EndPlay(Reason);
}
