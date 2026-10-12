#include "JadeboundProgressionActor.h"
#include "Domain/ProgressionCore.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ScopeExit.h"

bool AJadeboundProgressionActor::MatchesInventoryIntentRevision(int64 ExpectedRevision)
{
    if (!IsInGameThread() || !GetWorld() || GetWorld()->IsPaused() || GetWorld()->bIsTearingDown ||
        IsActorBeingDestroyed() || bPersistenceEndingPlay || !Core) return false;
    if (ExpectedRevision < 0 || State.Revision != ExpectedRevision ||
        State.ExactRevision != static_cast<uint64>(ExpectedRevision) ||
        Core->GetSnapshot().Revision != State.ExactRevision)
    {
        Feedback = FText::FromString(TEXT("Your pack changed. Review it before trying again."));
        return false;
    }
    // Revision tokens are bounded by the v2 save format. Do not cross its ceiling.
    if (State.ExactRevision >= Jadebound::MaximumPersistedCounter)
    {
        Feedback = FText::FromString(TEXT("Character progression has reached its revision limit. Items are preserved."));
        return false;
    }
    return true;
}

bool AJadeboundProgressionActor::BuyItem(int32 Id, int32 Quantity)
{
    return BuyItemAtRevision(Id, Quantity, State.Revision);
}

bool AJadeboundProgressionActor::BuyItemAtRevision(int32 Id, int32 Quantity, int64 ExpectedRevision)
{
    bool Ok = false;
    ON_SCOPE_EXIT { if (HasAuthority() && Player && Player->IsLocalController() && !bExecutingDevelopmentRequest) AcknowledgeAction(TEXT("Buy"), Ok); };
    if (!HasAuthority()) { ServerBuy(Id, Quantity, ExpectedRevision); return false; }
    if (Id == static_cast<int32>(Jadebound::ItemId::Balm) &&
        (bDevelopmentModel || GetNetMode() != NM_Standalone || !ResolveClickWalkScope()))
    { Feedback = FText::FromString(TEXT("Balm purchase requires the local standalone V006 hero.")); return false; }
    if (!AuthorityReady() || !MatchesInventoryIntentRevision(ExpectedRevision)) return false;
    if (!IsMerchantInRange()) { Feedback = FText::FromString(TEXT("Move closer to the merchant to buy or sell.")); return false; }
    if (Id == 2 && !AshwoodSpearMesh) { Feedback = FText::FromString(TEXT("The spear asset is unavailable. No coins were spent.")); return false; }
    return Ok = FinishTransaction(Core->Buy(static_cast<Jadebound::ItemId>(Id), Quantity), FText::FromString(TEXT("Purchased. Coins and items saved.")));
}

bool AJadeboundProgressionActor::UnequipWeapon()
{
    return UnequipWeaponAtRevision(State.Revision);
}

bool AJadeboundProgressionActor::UnequipWeaponAtRevision(int64 ExpectedRevision)
{
    bool Ok = false;
    ON_SCOPE_EXIT { if (HasAuthority() && Player && Player->IsLocalController() && !bExecutingDevelopmentRequest) AcknowledgeAction(TEXT("Unequip"), Ok); };
    if (!HasAuthority()) { ServerUnequip(ExpectedRevision); return false; }
    if (!AuthorityReady() || !MatchesInventoryIntentRevision(ExpectedRevision)) return false;
    if (IsRecipientSwingActive()) { Feedback = FText::FromString(TEXT("Finish your swing before changing weapons.")); return false; }
    if (IsLegacyWeaponChangeBlocked()) { Feedback = FText::FromString(TEXT("Finish your attack before changing weapons.")); return false; }
    return Ok = FinishTransaction(Core->Unequip(), FText::FromString(TEXT("Weapon returned to your bag. Equip it to attack.")));
}

void AJadeboundProgressionActor::ServerBuy_Implementation(int32 Id, int32 Quantity, int64 ExpectedRevision)
{
    TGuardValue<bool> Request(bExecutingDevelopmentRequest, true);
    const bool Ok = BuyItemAtRevision(Id, Quantity, ExpectedRevision);
    AcknowledgeAction(TEXT("Buy"), Ok);
}

void AJadeboundProgressionActor::ServerUnequip_Implementation(int64 ExpectedRevision)
{
    TGuardValue<bool> Request(bExecutingDevelopmentRequest, true);
    const bool Ok = UnequipWeaponAtRevision(ExpectedRevision);
    AcknowledgeAction(TEXT("Unequip"), Ok);
}
