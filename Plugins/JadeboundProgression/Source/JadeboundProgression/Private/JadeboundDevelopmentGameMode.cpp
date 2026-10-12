#include "JadeboundDevelopmentGameMode.h"
#include "JadeboundProgressionActor.h"
#include "JadeboundNativeRaider.h"
#include "Combat/RecipientAnimationRules.h"
#include "Combat/RecipientCombatRules.h"
#include "Development/DevelopmentIdentity.h"
#include "Development/DevelopmentResumeRegistry.h"
#include "Misc/Guid.h"
#include "Engine/NetConnection.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UnrealType.h"

AJadeboundDevelopmentGameMode::AJadeboundDevelopmentGameMode()
{
    // Fixed server test profiles; a server-issued ticket binds a reconnect to
    // its original profile. Clients never choose a bare character identifier.
    DevelopmentCharacterRoster = {TEXT("27b0cbce01df4b6b8f9cf927acaecf11"), TEXT("4a372b5a838f42be9d9a12e04cb24422")};
    ResumeAdmissions.Reset(new Jadebound::Development::ResumeRegistry());
    ResumeSessionId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
}
bool AJadeboundDevelopmentGameMode::IsConfigurationValid(FString& Error) const
{
#if UE_BUILD_SHIPPING
    Error = TEXT("The local development progression mode is unavailable in Shipping.");
    return false;
#else
    if (!bEnableLocalDevelopmentFoundation || DevelopmentCharacterRoster.Num() < 2 ||
        DevelopmentCharacterRoster.Num() > Jadebound::Development::MaxCharacters || !GlaiveMesh || !SpearMesh ||
        DevelopmentMerchantPoint.ContainsNaN() || !FMath::IsFinite(DevelopmentMerchantRange) || DevelopmentMerchantRange <= 0 || DevelopmentMerchantRange > 500)
    { Error = TEXT("Explicit local development configuration, 2-4 profiles and both weapon meshes are required."); return false; }
    if (bEnableRecipientCombat)
    {
        const AJadeboundNativeRaider* Prototype = NativeRaiderClass ? NativeRaiderClass->GetDefaultObject<AJadeboundNativeRaider>() : nullptr;
        if (!Prototype || !Prototype->HasValidPresentation() || !Jadebound::Combat::IsSafeMeleeMontage(NativeAttackMontage) ||
            NativeRaiderTransform.ContainsNaN() || !NativeRaiderTransform.GetScale3D().Equals(FVector::OneVector) ||
            !Jadebound::Combat::IsSavedDropLocation(NativeRaiderTransform.GetLocation()+FVector(0,0,50)) || !IronOreMesh || !ParcelMesh)
        { Error = TEXT("Recipient combat requires reviewed native enemy presentation, safe attack montage, finite arena transform and both loot meshes."); return false; }
    }
    TSet<FString> Seen;
    const FNumericProperty* Health = DefaultPawnClass ? FindFProperty<FNumericProperty>(DefaultPawnClass.Get(), TEXT("Current HP")) : nullptr;
    if (!Health || !Health->IsFloatingPoint())
    { Error = TEXT("Configure a development pawn class with the reviewed floating-point Current HP contract; generic APawn is insufficient."); return false; }
    for (const FString& Id : DevelopmentCharacterRoster)
    {
        if (!Jadebound::Development::IsCharacterId(Id) || Seen.Contains(Id))
        { Error = TEXT("Server development profiles must be distinct canonical 32-character lowercase GUIDs."); return false; }
        Seen.Add(Id);
    }
    return true;
#endif
}
bool AJadeboundDevelopmentGameMode::IsWorldCompatible(FString& Error) const
{
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        if (Cast<AJadeboundProgressionActor>(*It))
        {
            const auto* Model = Cast<AJadeboundProgressionActor>(*It);
            if (!Model->IsDevelopmentModel()) { Error = TEXT("Remove the placed standalone progression actor from the new test-map copy."); return false; }
        }
        if (It->IsA<AJadeboundNativeRaider>() && *It != RecipientRaider.Get())
        { Error = TEXT("Remove placed native raiders; this mode owns one admitted enemy."); return false; }
        const FString Name = It->GetClass()->GetName();
        if (Name == TEXT("BP_JadeboundAreaLoop_V001_C") || Name == TEXT("BP_GlaiveSwingDriver_V006_C"))
        { Error = TEXT("The local multiplayer test copy must omit the PC0 legacy AreaLoop and swing driver."); return false; }
    }
    return true;
}
void AJadeboundDevelopmentGameMode::StartPlay()
{
    FString Error;
    const bool bAdmitted = IsConfigurationValid(Error) && IsWorldCompatible(Error);
    if (!bAdmitted)
        UE_LOG(LogTemp, Error, TEXT("Jadebound development foundation admission blocked: %s"), *Error);
    Super::StartPlay();
    if (!bAdmitted || !HasAuthority() || !bEnableRecipientCombat || !GetWorld() || GetWorld()->bIsTearingDown || IsActorBeingDestroyed()) return;
    RecipientRaider = GetWorld()->SpawnActor<AJadeboundNativeRaider>(NativeRaiderClass, NativeRaiderTransform);
    if (!IsValid(RecipientRaider) || RecipientRaider->IsActorBeingDestroyed())
    { RecipientRaider = nullptr; UE_LOG(LogTemp, Error, TEXT("Jadebound recipient enemy creation failed.")); return; }
    for (const auto& Pair : Models)
        if (IsValid(Pair.Value)) Pair.Value->ConfigureRecipientCombat(true, NativeAttackMontage);
}
void AJadeboundDevelopmentGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
    Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
    if (!ErrorMessage.IsEmpty()) return;
    if (!IsConfigurationValid(ErrorMessage) || !IsWorldCompatible(ErrorMessage)) return;
    if (!Jadebound::Development::IsLoopbackAddress(Address)) ErrorMessage = TEXT("This unauthenticated development mode accepts loopback connections only.");
    else if (ActiveControllers.Num() >= DevelopmentCharacterRoster.Num()) ErrorMessage = TEXT("All server development profiles are in use.");
}
void AJadeboundDevelopmentGameMode::RejectController(APlayerController* Controller, const FString& Reason)
{
    CancelPendingResume(Controller);
    UE_LOG(LogTemp, Error, TEXT("Jadebound development admission rejected: %s"), *Reason);
    if (!IsValid(Controller) || Controller->IsActorBeingDestroyed()) return;
    if (!GameSession || !GameSession->KickPlayer(Controller, FText::FromString(Reason)))
        Controller->ClientReturnToMainMenuWithTextReason(FText::FromString(Reason));
}
void AJadeboundDevelopmentGameMode::PostLogin(APlayerController* NewPlayer)
{
    FString Error;
    if (!IsValid(NewPlayer) || NewPlayer->IsActorBeingDestroyed()) { CancelPendingResume(NewPlayer); return; }
    if (ActiveControllers.Contains(NewPlayer)) return;
    if (!IsConfigurationValid(Error) || !IsWorldCompatible(Error)) { RejectController(NewPlayer, Error); return; }
    if (NewPlayer->IsLocalController())
    {
        if (!Cast<ULocalPlayer>(NewPlayer->Player))
        { RejectController(NewPlayer, TEXT("An engine local player is required for the host.")); return; }
    }
    else
    {
        UNetConnection* Connection = NewPlayer->GetNetConnection();
        if (!Connection || !Jadebound::Development::IsLoopbackAddress(Connection->LowLevelGetRemoteAddress(false)))
        { RejectController(NewPlayer, TEXT("Only a loopback development connection is admitted.")); return; }
    }
    FString Selected;
    if (!GetPendingResume(NewPlayer, Selected))
    { RejectController(NewPlayer, TEXT("A reserved local development admission is required.")); return; }
    Super::PostLogin(NewPlayer);
    // Base PostLogin invokes gameplay/Blueprint callbacks. Revalidate their result.
    if (!GetPendingResume(NewPlayer, Selected))
    { RejectController(NewPlayer, TEXT("Local development admission was interrupted.")); return; }
    AJadeboundProgressionActor* Model = Models.FindRef(Selected).Get();
    if (!IsValid(Model))
    {
        Model = GetWorld()->SpawnActorDeferred<AJadeboundProgressionActor>(AJadeboundProgressionActor::StaticClass(),
            FTransform::Identity, NewPlayer, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Model) { RejectController(NewPlayer, TEXT("Progression model creation failed.")); return; }
        Model->AshwoodSpearMesh = SpearMesh;
        Model->IronOreMesh = IronOreMesh;
        Model->JadeParcelMesh = ParcelMesh;
        Model->MerchantPoint = DevelopmentMerchantPoint;
        Model->MerchantRange = DevelopmentMerchantRange;
        if (!Model->ConfigureDevelopmentOwner(NewPlayer, Selected, GlaiveMesh))
        { Model->Destroy(); RejectController(NewPlayer, TEXT("Progression ownership initialization failed.")); return; }
        Models.Add(Selected, Model);
        UGameplayStatics::FinishSpawningActor(Model, FTransform::Identity);
    }
    else if (!Model->ConfigureDevelopmentOwner(NewPlayer, Selected, GlaiveMesh))
    { RejectController(NewPlayer, TEXT("This character already has a writer or ownership cannot be rebound.")); return; }
    Model->ConfigureRecipientCombat(bEnableRecipientCombat && IsValid(GetRecipientRaider()), NativeAttackMontage);
    FString Ticket;
    if (!ActivateResume(NewPlayer, Ticket))
    {
        Model->DetachDevelopmentOwner(NewPlayer);
        RejectController(NewPlayer, TEXT("Local development admission could not be completed."));
        return;
    }
    ActiveControllers.Add(NewPlayer, Selected);
    Model->ForceNetUpdate();
    Model->ClientDevelopmentResumeTicket(ResumeSessionId, Selected, Ticket);
}
void AJadeboundDevelopmentGameMode::Logout(AController* Exiting)
{
    if (APlayerController* Player = Cast<APlayerController>(Exiting))
    {
        if (const FString* Id = ActiveControllers.Find(Player))
        {
            if (AJadeboundProgressionActor* Model = Models.FindRef(*Id).Get()) Model->DetachDevelopmentOwner(Player);
            ActiveControllers.Remove(Player);
        }
        // An inactive character remains bound to its ticket. Reconnecting in a
        // different order cannot inherit another client's inventory or rewards.
        DisconnectResume(Player);
    }
    Super::Logout(Exiting);
}
void AJadeboundDevelopmentGameMode::RestartPlayer(AController* NewPlayer)
{
    Super::RestartPlayer(NewPlayer);
    if (NewPlayer && NewPlayer->GetPawn())
    {
        // The preserved Blueprint event name includes spaces; the graph DSL omits them.
        APawn* const Pawn = NewPlayer->GetPawn();
        const FName LegacyEvent(TEXT("On Pawn Destroyed"));
        const bool bBoundBefore = Pawn->OnDestroyed.Contains(NewPlayer, LegacyEvent);
        Pawn->OnDestroyed.Remove(NewPlayer, LegacyEvent);
        const bool bBoundAfter = Pawn->OnDestroyed.Contains(NewPlayer, LegacyEvent);
        UE_LOG(LogTemp, Log, TEXT("Jadebound development legacy respawn binding: Controller=%s Pawn=%s Function=%s BoundBefore=%d BoundAfter=%d RemovalQualified=%s"),
            *GetNameSafe(NewPlayer), *GetNameSafe(Pawn), *LegacyEvent.ToString(),
            static_cast<int32>(bBoundBefore), static_cast<int32>(bBoundAfter),
            bBoundBefore && !bBoundAfter ? TEXT("true") : TEXT("false"));
        if (!bBoundBefore)
            UE_LOG(LogTemp, Warning, TEXT("Jadebound development legacy respawn removal unqualified: exact callback was missing before removal."));
        if (bBoundAfter)
            UE_LOG(LogTemp, Error, TEXT("Jadebound development legacy respawn removal failed: exact callback remains bound."));
        NewPlayer->GetPawn()->bOnlyRelevantToOwner = false;
        NewPlayer->GetPawn()->bNetUseOwnerRelevancy = false;
        NewPlayer->GetPawn()->SetReplicates(true);
        NewPlayer->GetPawn()->SetReplicateMovement(true);
    }
}
bool AJadeboundDevelopmentGameMode::RetryRetainedCharacter(const FString& Id)
{
    if (!HasAuthority() || GetNetMode() == NM_Client || !DevelopmentCharacterRoster.Contains(Id)) return false;
    AJadeboundProgressionActor* Model = Models.FindRef(Id).Get();
    return IsValid(Model) && Model->RetryDevelopmentSaveFromServer();
}

AJadeboundNativeRaider* AJadeboundDevelopmentGameMode::GetRecipientRaider() const
{
    return HasAuthority() && GetNetMode() != NM_Client && bEnableRecipientCombat && GetWorld() &&
        !GetWorld()->bIsTearingDown && !IsActorBeingDestroyed() && IsValid(RecipientRaider) &&
        !RecipientRaider->IsActorBeingDestroyed() && RecipientRaider->GetWorld() == GetWorld()
        ? RecipientRaider.Get() : nullptr;
}
bool AJadeboundDevelopmentGameMode::OwnsRecipientModel(const AJadeboundProgressionActor* Model, bool Living) const
{
    if (!GetRecipientRaider() || !IsValid(Model) || Model->IsActorBeingDestroyed() || !Model->HasAuthority() ||
        Model->GetWorld() != GetWorld() || Model->bPersistenceEndingPlay || !Model->HasDevelopmentContext() ||
        !Model->DevelopmentWriterLease || Models.FindRef(Model->ServerCharacterId).Get() != Model) return false;
    if (!Living) return true; // A previously accepted death retains its recipient after pawn loss/logout.
    const APlayerController* Controller = Model->Player;
    const FString* Id = IsValid(Controller) ? ActiveControllers.Find(const_cast<APlayerController*>(Controller)) : nullptr;
    return Id && *Id == Model->ServerCharacterId && Model->GetOwner() == Controller &&
        Model->IsProgressionReady() && Model->HasLivingPossessedHero();
}
bool AJadeboundDevelopmentGameMode::TryCommitRaiderDeath(AJadeboundNativeRaider* Enemy)
{
    if (!IsValid(Enemy) || Enemy != GetRecipientRaider() || Enemy->Life.bAlive || Enemy->Life.Health != 0 || Enemy->Life.LifeId <= 0) return false;
    AJadeboundProgressionActor* Recipient = Enemy->LastValidRecipient.Get();
    if (!OwnsRecipientModel(Recipient, false)) return false;
    return Recipient->CommitRecipientDefeat(Enemy);
}
bool AJadeboundDevelopmentGameMode::AreRecipientSwingsIdle() const
{
    if (!GetRecipientRaider()) return false;
    for (const auto& Pair : Models)
        if (IsValid(Pair.Value) && Pair.Value->IsRecipientSwingActive()) return false;
    return true;
}
