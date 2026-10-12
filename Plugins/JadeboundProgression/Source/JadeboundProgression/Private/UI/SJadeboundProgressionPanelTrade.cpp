#include "SJadeboundProgressionPanel.h"
#include "HAL/PlatformTime.h"

FReply SJadeboundProgressionPanel::ExecuteActionWithQuantity(EAction Action, int32 Argument, int64 ExpectedRevision,
    int32 Quantity, int32 ExpectedItemId, TSharedPtr<const uint8> RenderToken, uint64 ExpectedLifeGeneration)
{
    AJadeboundProgressionActor* Actor = ProgressionActor.Get();
    if (!IsValid(Actor) || Actor->IsActorBeingDestroyed()) return FReply::Handled();
    if (Action == EAction::Close) { Actor->CloseMenu(); return FReply::Handled(); }
    if (bAwaitingAction) return FReply::Handled();
    const bool bItemAction = Action == EAction::Buy || Action == EAction::Sell || Action == EAction::Equip ||
        Action == EAction::Unequip || Action == EAction::Use || Action == EAction::Split;
    if (bItemAction)
    {
        const FJadeboundProgressionState Live = Actor->GetProgressionState();
        const bool bSelectedAction = Action == EAction::Sell || Action == EAction::Equip ||
            Action == EAction::Use || Action == EAction::Split;
        // Selection and page changes rebuild at the same inventory revision.
        // Bind every item intent to the original rendering, not a later control.
        if (!RenderToken || RenderToken != TradeRenderToken || !Actor->IsMenuOpen() ||
            !bReady || !Actor->IsProgressionReady() || ExpectedRevision < 0 ||
            DisplayState.Revision != ExpectedRevision || DisplayState.ExactRevision != static_cast<uint64>(ExpectedRevision) ||
            Live.Revision != ExpectedRevision || Live.ExactRevision != static_cast<uint64>(ExpectedRevision) ||
            (bSelectedAction && SelectedSlot != Argument))
        { RefreshFromActor(true); return FReply::Handled(); }
    }
    if (Action == EAction::Use && Actor->IsDevelopmentModel() &&
        (ExpectedLifeGeneration == 0 || ExpectedLifeGeneration != Actor->GetItemUseLifeGeneration() ||
            !Actor->IsNativeConsumableContextAvailable()))
    { RefreshFromActor(true); return FReply::Handled(); }
    if (Action == EAction::Sell && Quantity != 1)
    {
        const FJadeboundProgressionState Live = Actor->GetProgressionState();
        // The amount belongs to the displayed button, never a later selection.
        // Stale local controls refresh without issuing a different transaction.
        if (Quantity <= 1 || ExpectedItemId <= 0 || ExpectedRevision < 0 ||
            SelectedSlot != Argument || !Actor->IsMenuOpen() || !RenderToken || RenderToken != TradeRenderToken ||
            Live.Revision != ExpectedRevision || Live.ExactRevision != static_cast<uint64>(ExpectedRevision) ||
            !Live.Slots.IsValidIndex(Argument) || Live.Slots[Argument].ItemId != ExpectedItemId ||
            Live.Slots[Argument].Count != Quantity || DisplayState.Revision != ExpectedRevision ||
            DisplayState.ExactRevision != static_cast<uint64>(ExpectedRevision) ||
            !DisplayState.Slots.IsValidIndex(Argument) || DisplayState.Slots[Argument].ItemId != ExpectedItemId ||
            DisplayState.Slots[Argument].Count != Quantity || !SellRestriction(Quantity).IsEmpty())
        { RefreshFromActor(true); return FReply::Handled(); }
    }
    // Remote callbacks return before receipt/state replication. Never treat
    // that return as success or fabricate an optimistic inventory snapshot.
    bAwaitingAction = Actor->IsDevelopmentModel() && !Actor->HasAuthority();
    PendingReceiptSequence = Actor->GetLastActionReceipt().Sequence;
    PendingSince = FPlatformTime::Seconds();
    bPendingLong = false;
    switch (Action)
    {
    case EAction::Buy: PendingAction = TEXT("Buy"); Actor->BuyItemAtRevision(Argument, 1, ExpectedRevision); break;
    case EAction::Sell: PendingAction = TEXT("Sell"); Actor->SellItem(Argument, Quantity, ExpectedRevision); break;
    case EAction::Equip: PendingAction = TEXT("Equip"); Actor->EquipItem(Argument, ExpectedRevision); break;
    case EAction::Unequip: PendingAction = TEXT("Unequip"); Actor->UnequipWeaponAtRevision(ExpectedRevision); break;
    case EAction::Use: PendingAction = TEXT("Use"); Actor->UseItemAtLifeGeneration(Argument,
        DisplayState.Slots.IsValidIndex(Argument) ? DisplayState.Slots[Argument].ItemId : 0, ExpectedRevision, ExpectedLifeGeneration); break;
    case EAction::Split: PendingAction = TEXT("Split"); Actor->SplitItem(Argument, 1,
        DisplayState.Slots.IsValidIndex(Argument) ? DisplayState.Slots[Argument].ItemId : 0, ExpectedRevision); break;
    case EAction::Save: PendingAction = TEXT("Save"); Actor->SaveProgress(); break;
    case EAction::Reload: PendingAction = TEXT("Reload"); Actor->ReloadProgress(); break;
    case EAction::Close: break;
    }
    RefreshFromActor(true);
    return FReply::Handled();
}
