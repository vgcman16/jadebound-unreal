#include "JadeboundProgressionActor.h"
#include "JadeboundDevelopmentGameMode.h"
#include "Combat/PlayerLifeRules.h"
#include "Transactions/NativeBalmRules.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

uint64 AJadeboundProgressionActor::GetItemUseLifeGeneration() const
{
    return bDevelopmentModel && NativePlayerLife.IsAlive() ? NativePlayerLife.Generation : 0;
}

bool AJadeboundProgressionActor::IsNativeConsumableContextAvailable() const
{
    const UWorld* World = GetWorld();
    if (!IsInGameThread() || !bDevelopmentModel || !IsDevelopmentReady() || !IsProgressionReady() ||
        !World || World->bIsTearingDown || World->IsPaused() || IsActorBeingDestroyed() || bPersistenceEndingPlay ||
        bUnsavedCandidate || !NativePlayerLife.IsAlive() || !IsValid(Player) || !IsValid(Hero) ||
        Hero->IsActorBeingDestroyed() || Hero->GetWorld() != World || Player->GetWorld() != World ||
        Player->GetPawn() != Hero || Hero->GetController() != Player || GetOwner() != Player) return false;
    if (!HasAuthority()) return Player->IsLocalController(); // presentation only
    const auto* Mode = Cast<AJadeboundDevelopmentGameMode>(World->GetAuthGameMode());
    return Mode && Mode->OwnsNativeCombatant(this, true) && NativeLifeState &&
        Jadebound::Combat::PlayerLife::IsAlive(*NativeLifeState) &&
        NativeLifeState->Generation == NativePlayerLife.Generation;
}

bool AJadeboundProgressionActor::UseItemAtLifeGeneration(int32 Slot, int32 ExpectedItemId,
    int64 ExpectedRevision, uint64 ExpectedLifeGeneration)
{
    if (!bDevelopmentModel) return UseOwnedItem(Slot, ExpectedItemId, ExpectedRevision, EItemUseContext::Pack);
    if (!IsInGameThread() || !IsNativeConsumableContextAvailable() || !bMenuOpen ||
        ExpectedLifeGeneration == 0 || ExpectedLifeGeneration != GetItemUseLifeGeneration()) return false;
    if (!HasAuthority())
    {
        ServerUseNativeItem(Slot, ExpectedItemId, ExpectedRevision, ExpectedLifeGeneration);
        return false; // wait for receipt; never grant optimistic healing/items
    }
    const TWeakObjectPtr<AJadeboundProgressionActor> Self(this);
    const bool Saved = UseNativeOwnedItem(Slot, ExpectedItemId, ExpectedRevision, ExpectedLifeGeneration);
    if (Self.IsValid() && !Self->IsActorBeingDestroyed() && !Self->bPersistenceEndingPlay && !Self->bExecutingDevelopmentRequest)
        Self->AcknowledgeAction(TEXT("Use"), Saved);
    return Saved;
}

bool AJadeboundProgressionActor::UseNativeOwnedItem(int32 Slot, int32 ExpectedItemId,
    int64 ExpectedRevision, uint64 ExpectedLifeGeneration)
{
    // A held write freezes inventory. Damage/revive use their own life admission
    // and remain available; repeating a consume never does.
    if (!IsInGameThread() || !HasAuthority() || !bDevelopmentModel || bBalmUseInProgress ||
        bPersistenceBlocked || bUnsavedCandidate || bNativeReviveInProgress) return false;
    if (!IsNativeConsumableContextAvailable() || !AuthorityReady() || !Core || !NativeLifeState ||
        !bAuthorityMenuOpen || BalmUseAdmissions == MAX_int64 ||
        GetWorld()->GetTimeSeconds() < NativeCloseGuardUntil)
    { Feedback = FText::FromString(TEXT("Use balm from the pack of your living, ready hero.")); return false; }
    if (ExpectedLifeGeneration == 0 || ExpectedLifeGeneration != NativeLifeState->Generation ||
        !MatchesInventoryIntentRevision(ExpectedRevision) || !State.Slots.IsValidIndex(Slot) ||
        State.Slots[Slot].ItemId != ExpectedItemId || State.Slots[Slot].Count <= 0)
    { Feedback = FText::FromString(TEXT("Your life or pack changed. Select the original balm again.")); return false; }
    if (NativeLifeState->Health == 100)
    { Feedback = FText::FromString(TEXT("Your health is full. Balm is preserved.")); return false; }
    TGuardValue<bool> SingleFlight(bBalmUseInProgress, true);
    const TWeakObjectPtr<AJadeboundProgressionActor> Self(this);
    const int32 BeforeHealth = NativeLifeState->Health;
    const auto Outcome = Jadebound::Transactions::NativeBalm::TryConsume(*Core, *NativeLifeState,
        Slot, ExpectedItemId, ExpectedRevision, ExpectedLifeGeneration, GetWorld()->GetTimeSeconds());
    if (!Outcome.Applied)
    {
        Feedback = FText::FromString(TEXT("That balm intent is no longer available. Your item is preserved."));
        return false;
    }
    // Core and life are committed before writer/presentation callbacks.
    // FinishTransaction freezes the exact inventory candidate first.
    ++BalmUseAdmissions;
    LastBalmUseRevision = static_cast<int64>(Core->GetSnapshot().Revision);
    BalmLastHealthBefore = BeforeHealth;
    BalmLastHealthAfter = NativeLifeState->Health;
    const bool Saved = FinishTransaction(Outcome.CoreResult,
        FText::FromString(TEXT("Used one balm. Health restored and inventory saved.")));
    if (!Self.IsValid() || Self->IsActorBeingDestroyed() || Self->bPersistenceEndingPlay) return false;
    if (!Saved && Self->bUnsavedCandidate)
    {
        Self->PersistencePresentation.Message = FText::Format(
            NSLOCTEXT("Jadebound", "NativeBalmSaveHeld", "Healing applied once. {0}"), Self->PersistencePresentation.Message);
    }
    // Publish CURRENT life, including damage admitted during a writer callback.
    // Retry never replays the earlier healed life or the consumption operation.
    Self->PublishNativePlayerLife();
    return Saved;
}

void AJadeboundProgressionActor::ServerUseNativeItem_Implementation(int32 Slot, int32 ExpectedItemId,
    int64 ExpectedRevision, uint64 ExpectedLifeGeneration)
{
    TGuardValue<bool> Request(bExecutingDevelopmentRequest, true);
    const TWeakObjectPtr<AJadeboundProgressionActor> Self(this);
    const bool Saved = UseNativeOwnedItem(Slot, ExpectedItemId, ExpectedRevision, ExpectedLifeGeneration);
    if (Self.IsValid() && !Self->IsActorBeingDestroyed() && !Self->bPersistenceEndingPlay)
        Self->AcknowledgeAction(TEXT("Use"), Saved);
}
