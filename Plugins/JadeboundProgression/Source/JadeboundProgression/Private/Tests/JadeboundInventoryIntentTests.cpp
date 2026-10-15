#include "JadeboundProgressionActor.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/JadeboundMerchantFixture.h"
#include "Presentation/InventoryReplication.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

namespace JadeboundInventoryIntentTests
{
using namespace JadeboundMerchantBulkSale;

Jadebound::Snapshot InitialState()
{
    Jadebound::Snapshot State;
    State.Version = 3; State.Revision = 0; State.Coins = 10;
    State.EquippedWeapon = Jadebound::ItemId::JadeGlaive; State.EquippedTorso = Jadebound::ItemId::None;
    State.Quest = Jadebound::QuestState::NotAccepted;
    State.NextDropId = 1; State.QuestKillDropId = 0; State.UnprocessedDropIds.clear();
    for (auto& Slot : State.Inventory) Slot = {Jadebound::ItemId::None, 0};
    return State;
}

bool ReceiptIs(FAutomationTestBase& Test, const AJadeboundProgressionActor& Model, int64 PreviousSequence,
    FName Action, bool bSucceeded, int64 Observed, int64 Verified, const TCHAR* Context, bool bHeld = false)
{
    const auto Receipt = Model.GetLastActionReceipt();
    return Test.TestTrue(FString::Printf(TEXT("%s: exactly one matching authority receipt"), Context),
        Receipt.Sequence == PreviousSequence + 1 && Receipt.Action == Action && Receipt.bSucceeded == bSucceeded &&
        Receipt.ObservedRevision == Observed && Receipt.LastVerifiedJournalRevision == Verified &&
        Receipt.bHeldCandidate == bHeld && Receipt.bPersistenceBlocked == bHeld);
}

bool RefuseBuy(FAutomationTestBase& Test, FScope& Scope, const Jadebound::Snapshot& Expected,
    int32 Id, int32 Quantity, int64 Revision, const TCHAR* Context)
{
    Image Before;
    if (!Test.TestTrue(TEXT("Clean complete refusal baseline exists"), Scope.StateIs(Expected) && Scope.Capture(Before))) return false;
    const int64 Sequence = Scope.Model->GetLastActionReceipt().Sequence;
    Access::BuyRequest(*Scope.Model, Id, Quantity, Revision);
    return ReceiptIs(Test, *Scope.Model, Sequence, TEXT("Buy"), false, static_cast<int64>(Expected.Revision), static_cast<int64>(Expected.Revision), Context) &&
        Test.TestTrue(Context, Scope.Unchanged(Before) && Scope.StateIs(Expected));
}

bool RefuseUnequip(FAutomationTestBase& Test, FScope& Scope, const Jadebound::Snapshot& Expected,
    int64 Revision, const TCHAR* Context)
{
    Image Before;
    if (!Test.TestTrue(TEXT("Clean complete equipment refusal baseline exists"), Scope.StateIs(Expected) && Scope.Capture(Before))) return false;
    const int64 Sequence = Scope.Model->GetLastActionReceipt().Sequence;
    Access::UnequipRequest(*Scope.Model, Revision);
    return ReceiptIs(Test, *Scope.Model, Sequence, TEXT("Unequip"), false, static_cast<int64>(Expected.Revision), static_cast<int64>(Expected.Revision), Context) &&
        Test.TestTrue(Context, Scope.Unchanged(Before) && Scope.StateIs(Expected));
}

bool BuyAndAdvance(FAutomationTestBase& Test, FScope& Scope, const Jadebound::Snapshot& Expected,
    int32 Id, int32 Quantity, int64 Revision, const TCHAR* Context)
{
    Image Before;
    if (!Scope.Capture(Before)) { Test.AddError(TEXT("Actual pre-purchase bank image unavailable.")); return false; }
    const int64 Sequence = Scope.Model->GetLastActionReceipt().Sequence;
    Access::BuyRequest(*Scope.Model, Id, Quantity, Revision);
    return ReceiptIs(Test, *Scope.Model, Sequence, TEXT("Buy"), true, static_cast<int64>(Expected.Revision), static_cast<int64>(Expected.Revision), Context) &&
        Test.TestTrue(Context, Scope.Advanced(Before, Expected));
}

bool ReopenAs(FAutomationTestBase& Test, FScope& Scope, const Jadebound::Snapshot& Expected)
{
    Image Before, After;
    if (!Scope.Capture(Before)) { Test.AddError(TEXT("Actual pre-replacement banks unavailable.")); return false; }
    if (!Test.TestTrue(TEXT("Replacement actor with a new writer lease reloads the complete expected state without a write"),
        Scope.Reopen() && Scope.StateIs(Expected) && Scope.Capture(After) && After.Opens == Before.Opens &&
        After.Slot == Before.Slot && After.CharacterId == Before.CharacterId && SameCore(After.Core, Before.Core) &&
        SameReflected(After.Reflected, Before.Reflected) && SameReflected(After.Committed, Before.Committed) &&
        SameDrops(After.Pending, Before.Pending) && SameDrops(After.SavedDrops, Before.SavedDrops))) return false;
    for (int32 I = 0; I < JournalPathCount; ++I)
        if (!Test.TestTrue(TEXT("Replacement load preserves every actual journal namespace and byte"), SameBank(After.Banks[I], Before.Banks[I]))) return false;
    return true;
}

struct FZeroHealth
{
    ACharacter* Hero;
    FNumericProperty* Property = nullptr;
    double Previous = 0;
    bool bValid = false;
    explicit FZeroHealth(ACharacter* InHero) : Hero(InHero)
    {
        if (!IsValid(Hero)) return;
        Property = FindFProperty<FNumericProperty>(Hero->GetClass(), TEXT("Current HP"));
        if (!Property || !Property->IsFloatingPoint()) return;
        void* Address = Property->ContainerPtrToValuePtr<void>(Hero);
        Previous = Property->GetFloatingPointPropertyValue(Address);
        if (!FMath::IsFinite(Previous) || Previous <= 0.0) return;
        Property->SetFloatingPointPropertyValue(Address, 0.0); // Owned real Hero field; no death/respawn simulation claim.
        bValid = Property->GetFloatingPointPropertyValue(Address) == 0.0;
    }
    ~FZeroHealth()
    {
        if (Property && FMath::IsFinite(Previous) && Previous > 0.0 && IsValid(Hero))
            Property->SetFloatingPointPropertyValue(Property->ContainerPtrToValuePtr<void>(Hero), Previous);
    }
};

struct HeldImage
{
    Image Current;
    const UJadeboundCharacterSave* Held = nullptr;
    FJadeboundProgressionState Candidate;
    FString CandidateId, Message;
    TArray<FJadeboundSavedDrop> CandidateDrops;
    double Deadline = 0;
};

bool CaptureHeld(const FScope& Scope, HeldImage& Out)
{
    if (!Scope.Live() || Scope.bForeign || !Access::IsHeld(*Scope.Model) || !Access::Held(*Scope.Model) ||
        !Access::Saved(*Scope.Model) || Scope.Model->IsProgressionReady() ||
        Scope.Model->GetPersistencePresentation().FailureKind != EJadeboundPersistenceFailureKind::Retryable) return false;
    Out.Current.Core = Access::Core(*Scope.Model); Out.Current.Reflected = Scope.Model->GetProgressionState();
    Out.Current.Saved = Access::Saved(*Scope.Model); Out.Current.Committed = Out.Current.Saved->State;
    Out.Current.CharacterId = Out.Current.Saved->CharacterId; Out.Current.SavedDrops = Out.Current.Saved->PendingDrops;
    Out.Current.Pending = Access::Drops(*Scope.Model); Out.Current.Slot = Access::BankSlot(*Scope.Model); Out.Current.Opens = Scope.Opens;
    Out.Held = Access::Held(*Scope.Model); Out.Candidate = Out.Held->State;
    Out.CandidateId = Out.Held->CharacterId; Out.CandidateDrops = Out.Held->PendingDrops;
    Out.Message = Scope.Model->GetPersistencePresentation().Message.ToString(); Out.Deadline = Access::Deadline(*Scope.Model);
    for (int32 I = 0; I < JournalPathCount; ++I) if (!ReadBank(Scope.Paths[I], Out.Current.Banks[I])) return false;
    for (int32 I = 0; I < JournalPathCount; ++I)
        if (I != 2 && I != 3 && Out.Current.Banks[I].bExists) return false;
    return FMath::IsFinite(Out.Deadline);
}

bool HeldUnchanged(const FScope& Scope, const HeldImage& Before)
{
    HeldImage After;
    if (!CaptureHeld(Scope, After) || After.Held != Before.Held || After.CandidateId != Before.CandidateId ||
        !SameReflected(After.Candidate, Before.Candidate) || !SameDrops(After.CandidateDrops, Before.CandidateDrops) ||
        After.Message != Before.Message || After.Deadline != Before.Deadline || After.Current.Saved != Before.Current.Saved ||
        After.Current.CharacterId != Before.Current.CharacterId || !SameCore(After.Current.Core, Before.Current.Core) ||
        !SameReflected(After.Current.Reflected, Before.Current.Reflected) || !SameReflected(After.Current.Committed, Before.Current.Committed) ||
        !SameDrops(After.Current.Pending, Before.Current.Pending) || !SameDrops(After.Current.SavedDrops, Before.Current.SavedDrops) ||
        After.Current.Slot != Before.Current.Slot || After.Current.Opens != Before.Current.Opens) return false;
    for (int32 I = 0; I < JournalPathCount; ++I) if (!SameBank(After.Current.Banks[I], Before.Current.Banks[I])) return false;
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundInventoryRevisionIntentTest,
    "Jadebound.Progression.InventoryRevisionIntents", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FJadeboundInventoryRevisionIntentTest::RunTest(const FString& Parameters)
{
    using namespace JadeboundInventoryIntentTests;
    const auto Initial = InitialState();
    {
        FScope Scope(*this);
        if (!Scope.bValid || !TestTrue(TEXT("Independent complete r0 fixture"), Scope.StateIs(Initial))) return false;
        struct InvalidBuy { int32 Id, Quantity; int64 Revision; };
        const InvalidBuy Refusals[] = {{4, 1, -1}, {4, 1, 1}, {0, 1, 0}, {-1, 1, 0}, {999, 1, 0},
            {5, 1, 0}, {4, 0, 0}, {4, -1, 0}, {4, 6, 0}, {4, MAX_int32, 0}};
        for (const auto& Case : Refusals)
            if (!RefuseBuy(*this, Scope, Initial, Case.Id, Case.Quantity, Case.Revision,
                TEXT("Missing/future token or invalid item/quantity/funds preserves full state and banks"))) return false;

        // Reflected-shadow corruption must not manufacture authority equality with Core.
        const struct { int64 Signed; uint64 Exact; int64 Intent; } Shadows[] = {{0, 1, 0}, {1, 1, 1}};
        for (const auto& Shadow : Shadows)
        {
            Access::SetDisplayedRevision(*Scope.Model, Shadow.Signed, Shadow.Exact);
            ON_SCOPE_EXIT { Access::RefreshState(*Scope.Model); };
            Image Before;
            if (!Scope.Capture(Before)) return false;
            const int64 Sequence = Scope.Model->GetLastActionReceipt().Sequence;
            Access::BuyRequest(*Scope.Model, 4, 1, Shadow.Intent);
            if (!ReceiptIs(*this, *Scope.Model, Sequence, TEXT("Buy"), false, Shadow.Signed, 0, TEXT("Authority revision correspondence")) ||
                !TestTrue(TEXT("Mismatched signed/exact/Core revision never spends or writes"), Scope.Unchanged(Before))) return false;
        }
        if (!TestTrue(TEXT("Authority reflection restored from unchanged Core"), Scope.StateIs(Initial))) return false;
        const FVector Home = Scope.Hero->GetActorLocation();
        Scope.Hero->SetActorLocation(Scope.Model->MerchantPoint + FVector(Scope.Model->MerchantRange + 200.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
        if (!TestFalse(TEXT("Actual owned Hero is outside merchant range"), Scope.Model->IsMerchantInRange()) ||
            !RefuseBuy(*this, Scope, Initial, 4, 1, 0, TEXT("Current-token remote-range purchase refuses without a write"))) return false;
        Scope.Hero->SetActorLocation(Home, false, nullptr, ETeleportType::TeleportPhysics);
        {
            FZeroHealth Dead(Scope.Hero);
            if (!TestTrue(TEXT("Actual floating Hero health is zero for bounded admission probe"), Dead.bValid) ||
                !RefuseBuy(*this, Scope, Initial, 4, 1, 0, TEXT("Zero-health purchase refuses at the current revision")) ||
                !RefuseUnequip(*this, Scope, Initial, 0, TEXT("Zero-health unequip refuses at the current revision"))) return false;
        }
        auto First = InitialState(); First.Revision = 1; First.Coins = 8; First.Inventory[0] = {Jadebound::ItemId::Moonleaf, 1};
        if (!BuyAndAdvance(*this, Scope, First, 4, 1, 0, TEXT("Real current r0 purchase establishes r1 once")) ||
            !RefuseBuy(*this, Scope, First, 4, 1, 0, TEXT("Affordable duplicate old Buy cannot consume another two coins"))) return false;
        auto Second = InitialState(); Second.Revision = 2; Second.Coins = 6; Second.Inventory[0] = {Jadebound::ItemId::Moonleaf, 2};
        if (!BuyAndAdvance(*this, Scope, Second, 4, 1, 1, TEXT("Fresh r1 request advances complete r2 exactly once")) ||
            !RefuseBuy(*this, Scope, Second, 4, 1, 1, TEXT("Repeated prior successful token refuses at r2")) || !ReopenAs(*this, Scope, Second) ||
            !RefuseBuy(*this, Scope, Second, 4, 1, 0, TEXT("Replacement actor rejects original r0 purchase replay"))) return false;
        auto Third = InitialState(); Third.Revision = 3; Third.Coins = 4; Third.Inventory[0] = {Jadebound::ItemId::Moonleaf, 3};
        if (!BuyAndAdvance(*this, Scope, Third, 4, 1, 2, TEXT("Fresh post-replacement current token remains usable"))) return false;
    }
    {
        FScope Scope(*this);
        if (!Scope.bValid) return false;
        if (!RefuseUnequip(*this, Scope, Initial, -1, TEXT("Missing equipment token preserves the real default glaive")) ||
            !RefuseUnequip(*this, Scope, Initial, 1, TEXT("Future equipment token preserves the real default glaive"))) return false;
        auto Bought = InitialState(); Bought.Revision = 1; Bought.Coins = 2; Bought.Inventory[0] = {Jadebound::ItemId::AshwoodSpear, 1};
        if (!BuyAndAdvance(*this, Scope, Bought, 2, 1, 0, TEXT("Actual original spear establishes independent gear fixture"))) return false;
        Image Before;
        if (!Scope.Capture(Before)) return false;
        auto Equipped = InitialState(); Equipped.Revision = 2; Equipped.Coins = 2; Equipped.EquippedWeapon = Jadebound::ItemId::AshwoodSpear;
        Equipped.Inventory[0] = {Jadebound::ItemId::JadeGlaive, 1};
        if (!TestTrue(TEXT("Actual equip swaps glaive for spear and persists only r2"), Scope.Model->EquipItem(0, 1) && Scope.Advanced(Before, Equipped)) ||
            !RefuseUnequip(*this, Scope, Equipped, 1, TEXT("Delayed r1 Unequip cannot remove the newer spear although bag has room")) ||
            !RefuseUnequip(*this, Scope, Equipped, 3, TEXT("Future r3 Unequip cannot remove current r2 gear"))) return false;
        const FVector Home = Scope.Hero->GetActorLocation();
        Scope.Hero->SetActorLocation(Scope.Model->MerchantPoint + FVector(Scope.Model->MerchantRange + 200.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
        if (!TestFalse(TEXT("Gear fixture is actually away from merchant"), Scope.Model->IsMerchantInRange()) || !Scope.Capture(Before)) return false;
        const int64 Sequence = Scope.Model->GetLastActionReceipt().Sequence;
        Access::UnequipRequest(*Scope.Model, 2);
        auto Unequipped = InitialState(); Unequipped.Revision = 3; Unequipped.Coins = 2; Unequipped.EquippedWeapon = Jadebound::ItemId::None;
        Unequipped.Inventory[0] = {Jadebound::ItemId::JadeGlaive, 1}; Unequipped.Inventory[1] = {Jadebound::ItemId::AshwoodSpear, 1};
        if (!ReceiptIs(*this, *Scope.Model, Sequence, TEXT("Unequip"), true, 3, 3, TEXT("Fresh equipment handler")) ||
            !TestTrue(TEXT("Fresh Unequip succeeds away from merchant with precisely one returned spear/write"), Scope.Advanced(Before, Unequipped)) ||
            !RefuseUnequip(*this, Scope, Unequipped, 2, TEXT("Repeated successful old Unequip token preserves both weapons")) ||
            !RefuseUnequip(*this, Scope, Unequipped, 3, TEXT("Current-token NothingEquipped refuses without another write"))) return false;
        Scope.Hero->SetActorLocation(Home, false, nullptr, ETeleportType::TeleportPhysics);
        if (!ReopenAs(*this, Scope, Unequipped) || !RefuseUnequip(*this, Scope, Unequipped, 2, TEXT("Replacement actor refuses old equipment intent"))) return false;
        if (!Scope.Capture(Before)) return false;
        auto Reequipped = InitialState(); Reequipped.Revision = 4; Reequipped.Coins = 2; Reequipped.Inventory[1] = {Jadebound::ItemId::AshwoodSpear, 1};
        if (!TestTrue(TEXT("Fresh replacement equipment revision equips only the owned glaive"), Scope.Model->EquipItem(0, 3) && Scope.Advanced(Before, Reequipped))) return false;
        if (!Scope.Capture(Before)) return false;
        auto WrapperUnequipped = InitialState(); WrapperUnequipped.Revision = 5; WrapperUnequipped.Coins = 2; WrapperUnequipped.EquippedWeapon = Jadebound::ItemId::None;
        WrapperUnequipped.Inventory[0] = {Jadebound::ItemId::JadeGlaive, 1}; WrapperUnequipped.Inventory[1] = {Jadebound::ItemId::AshwoodSpear, 1};
        if (!TestTrue(TEXT("Unchanged Blueprint-facing Unequip wrapper binds current r4 and persists complete r5"),
            Scope.Model->UnequipWeapon() && Scope.Advanced(Before, WrapperUnequipped))) return false;
    }
    {
        FScope Scope(*this);
        if (!Scope.bValid) return false;
        Image Before, Seeded;
        if (!Scope.StateIs(Initial) || !Scope.Capture(Before)) return false;
        auto Maximum = InitialState(); Maximum.Revision = Jadebound::MaximumPersistedCounter;
        // Explicit synthetic ceiling seed, not a claimed chain of gameplay transactions.
        if (!TestTrue(TEXT("Trusted maximum-counter fixture loads a valid whole snapshot and verifies one actual journal write"),
            Access::SeedLocalFixtureSnapshot(*Scope.Model, Maximum) && Scope.StateIs(Maximum) && Scope.Capture(Seeded) &&
            Seeded.Opens == Before.Opens + 1 && Seeded.Slot == 1 - Before.Slot && Seeded.Saved != Before.Saved &&
            Scope.LastOpenedPath == Scope.Paths[2 + Seeded.Slot] &&
            !SameBank(Seeded.Banks[2 + Seeded.Slot], Before.Banks[2 + Seeded.Slot]))) return false;
        for (int32 I = 0; I < JournalPathCount; ++I)
            if (I != 2 + Seeded.Slot && !TestTrue(TEXT("Synthetic ceiling seed preserves all other real journal banks"), SameBank(Seeded.Banks[I], Before.Banks[I]))) return false;
        if (!RefuseBuy(*this, Scope, Maximum, 4, 1, MAX_int64, TEXT("Exact current ceiling cannot wrap a purchase revision or consume currency")) ||
            !RefuseUnequip(*this, Scope, Maximum, MAX_int64, TEXT("Exact current ceiling cannot wrap gear revision or add a weapon")) ||
            !ReopenAs(*this, Scope, Maximum) ||
            !RefuseBuy(*this, Scope, Maximum, 4, 1, MAX_int64, TEXT("Replacement ceiling snapshot remains safely terminal"))) return false;
    }
    AddInfo(TEXT("Actual server-handler implementations, genuine Hero/weapons, complete states, real owned journal bytes and replacement actor readback. Trusted fixture registry; zero-health and maximum-counter synthetic fixture perturbations. No RPC transport/client ordering, normal login, gameplay death/respawn, viewport or game input proof."));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundInventoryReplicationShadowTest,
    "Jadebound.Presentation.InventoryReplicationShadow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FJadeboundInventoryReplicationShadowTest::RunTest(const FString& Parameters)
{
    using namespace JadeboundInventoryIntentTests;
    auto Known = InitialState(); Known.Revision = 7; Known.Coins = 4; Known.Inventory[0] = {Jadebound::ItemId::Moonleaf, 3};
    Known.EquippedTorso = Jadebound::ItemId::WardenCoat; // Nonzero current wire witness; no earned/equip claim.
    FJadeboundProgressionState Expected;
    if (!TestTrue(TEXT("Independent v3 presentation payload encodes"), Jadebound::Persistence::EncodeStateV3(Known, Expected))) return false;
    auto Wire = Expected; Wire.ExactRevision = 0;
    if (!TestTrue(TEXT("Supported signed wire revision repairs stale save-only shadow while preserving complete payload"),
        Jadebound::Presentation::ApplyReplicatedInventoryRevision(Wire) && SameReflected(Wire, Expected))) return false;
    auto Highest = Expected; Highest.Revision = MAX_int64; Highest.ExactRevision = 1;
    if (!TestTrue(TEXT("Signed ceiling remains an exact presentation value without truncation"),
        Jadebound::Presentation::ApplyReplicatedInventoryRevision(Highest) && Highest.ExactRevision == Jadebound::MaximumPersistedCounter)) return false;
    for (int32 Case = 0; Case < 4; ++Case)
    {
        auto Invalid = Expected; Invalid.ExactRevision = 99;
        if (Case == 0) Invalid.Version = 1;
        if (Case == 1) Invalid.Slots.RemoveAt(0);
        if (Case == 2) Invalid.Slots.AddDefaulted();
        if (Case == 3) Invalid.Revision = -1;
        auto Preserved = Invalid; Preserved.ExactRevision = Case == 3 ? 0 : 7;
        if (!TestTrue(TEXT("Unsupported version/short or oversized pack/negative wire revision cannot admit readiness or alter items"),
            !Jadebound::Presentation::ApplyReplicatedInventoryRevision(Invalid) && SameReflected(Invalid, Preserved))) return false;
    }
    AddInfo(TEXT("Actual bounded presentation helper with complete reflected payload comparisons; no received packet/replication transport or actor readiness event proof."));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundInventoryHeldIntentTest,
    "Jadebound.Progression.InventoryHeldIntents", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FJadeboundInventoryHeldIntentTest::RunTest(const FString& Parameters)
{
    using namespace JadeboundInventoryIntentTests;
    FScope Scope(*this);
    if (!Scope.bValid) return false;
    const auto Initial = InitialState();
    auto Candidate = InitialState(); Candidate.Revision = 1; Candidate.Coins = 8; Candidate.Inventory[0] = {Jadebound::ItemId::Moonleaf, 1};
    Image InitialBanks;
    if (!Scope.StateIs(Initial) || !Scope.Capture(InitialBanks)) return false;
    Access::SetOpener(*Scope.Model, [&Scope](const FString& Path) -> IFileHandle*
    {
        if (Path != Scope.Paths[2] && Path != Scope.Paths[3]) { Scope.bForeign = true; return nullptr; }
        ++Scope.Opens; Scope.LastOpenedPath = Path;
        return nullptr; // Controlled real WriteSave opener failure, before any file is opened.
    });
    int64 Sequence = Scope.Model->GetLastActionReceipt().Sequence;
    Access::BuyRequest(*Scope.Model, 4, 1, 0);
    HeldImage Held;
    FJadeboundProgressionState Encoded;
    if (!ReceiptIs(*this, *Scope.Model, Sequence, TEXT("Buy"), false, 1, 0, TEXT("Purchase reaches held candidate once"), true) ||
        !TestTrue(TEXT("Failed persistence holds complete r1 while verified payload and bank slot remain r0"),
            CaptureHeld(Scope, Held) && Jadebound::Persistence::EncodeStateV3(Candidate, Encoded) &&
            SameCore(Held.Current.Core, Candidate) && SameReflected(Held.Current.Reflected, Encoded) && SameReflected(Held.Candidate, Encoded) &&
            Held.CandidateId == Scope.Id && Held.CandidateDrops.IsEmpty() && Held.Current.Pending.IsEmpty() &&
            Held.Current.Saved == InitialBanks.Saved && Held.Current.CharacterId == InitialBanks.CharacterId &&
            SameReflected(Held.Current.Committed, InitialBanks.Committed) && SameDrops(Held.Current.SavedDrops, InitialBanks.SavedDrops) &&
            Held.Current.Slot == InitialBanks.Slot && Held.Current.Opens == InitialBanks.Opens + 1)) return false;
    for (int32 I = 0; I < JournalPathCount; ++I)
        if (!TestTrue(TEXT("Failed opener did not rewrite any actual journal bank"), SameBank(Held.Current.Banks[I], InitialBanks.Banks[I]))) return false;
    const int64 HeldRevisions[] = {0, 1};
    for (const int64 Revision : HeldRevisions)
    {
        Sequence = Scope.Model->GetLastActionReceipt().Sequence;
        Access::BuyRequest(*Scope.Model, 4, 1, Revision);
        if (!ReceiptIs(*this, *Scope.Model, Sequence, TEXT("Buy"), false, 1, 0, TEXT("Buy during hold"), true) ||
            !TestTrue(TEXT("Old and current Buy tokens preserve exact held candidate, flags, deadline and banks"), HeldUnchanged(Scope, Held))) return false;
        Sequence = Scope.Model->GetLastActionReceipt().Sequence;
        Access::UnequipRequest(*Scope.Model, Revision);
        if (!ReceiptIs(*this, *Scope.Model, Sequence, TEXT("Unequip"), false, 1, 0, TEXT("Unequip during hold"), true) ||
            !TestTrue(TEXT("Old and current Unequip tokens cannot alter held gear or open a journal"), HeldUnchanged(Scope, Held))) return false;
    }
    Scope.InstallOpener();
    Access::SetLocalFixtureFutureDeadline(*Scope.Model); // Controlled deadline, no elapsed production-backoff claim.
    if (!CaptureHeld(Scope, Held)) return false;
    Sequence = Scope.Model->GetLastActionReceipt().Sequence;
    if (!TestTrue(TEXT("Retry before controlled future deadline leaves all held evidence and acknowledgment unchanged"),
        !Access::Retry(*Scope.Model) && HeldUnchanged(Scope, Held) && Scope.Model->GetLastActionReceipt().Sequence == Sequence)) return false;
    Access::ExpireLocalFixtureDeadline(*Scope.Model); // Explicit fixture setup; never clears or reconstructs candidate.
    if (!CaptureHeld(Scope, Held)) return false;
    if (!TestTrue(TEXT("Retry writes the exact held r1 once without a second transaction or acknowledgment"),
        Access::Retry(*Scope.Model) && Scope.StateIs(Candidate) && Scope.Model->GetLastActionReceipt().Sequence == Sequence)) return false;
    Image Verified;
    if (!TestTrue(TEXT("Verified held retry flips one real bank with exactly one additional opener"), Scope.Capture(Verified) &&
        Verified.Opens == Held.Current.Opens + 1 && Verified.Slot == 1 - Held.Current.Slot &&
        Scope.LastOpenedPath == Scope.Paths[2 + Verified.Slot] && Verified.Saved != Held.Current.Saved &&
        !SameBank(Verified.Banks[2 + Verified.Slot], Held.Current.Banks[2 + Verified.Slot]))) return false;
    for (int32 I = 0; I < JournalPathCount; ++I)
        if (I != 2 + Verified.Slot && !TestTrue(TEXT("Held retry preserves all other actual bank bytes"), SameBank(Verified.Banks[I], Held.Current.Banks[I]))) return false;
    if (!RefuseBuy(*this, Scope, Candidate, 4, 1, 0, TEXT("Original pre-hold token remains stale after candidate verification"))) return false;
    auto Second = InitialState(); Second.Revision = 2; Second.Coins = 6; Second.Inventory[0] = {Jadebound::ItemId::Moonleaf, 2};
    if (!BuyAndAdvance(*this, Scope, Second, 4, 1, 1, TEXT("Only a fresh post-hold token purchases the second leaf")) || !ReopenAs(*this, Scope, Second)) return false;
    AddInfo(TEXT("Controlled journal opener failure plus actual held-candidate retry/readback and replacement actor. Future/expired retry deadlines are explicit fixture perturbations; no elapsed backoff, power-loss/crash recovery, clients or gameplay proof."));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundInventoryTorsoHeldTest,
    "Jadebound.Progression.InventoryTorsoHeld", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FJadeboundInventoryTorsoHeldTest::RunTest(const FString& Parameters)
{
    using namespace JadeboundInventoryIntentTests;
    FScope Scope(*this);
    if (!Scope.bValid) return false;
    Image Fresh, Seeded;
    auto Seed = InitialState(); Seed.Revision = 1; Seed.EquippedTorso = Jadebound::ItemId::WardenCoat;
    // Synthetic owned armor seed at a new revision; no earned/equip or appearance claim.
    if (!Scope.Capture(Fresh) || !TestTrue(TEXT("Trusted nonzero torso seed commits a complete V3 r1 with one actual write"),
        Access::SeedLocalFixtureSnapshot(*Scope.Model, Seed) && Scope.Advanced(Fresh, Seed) && Scope.Capture(Seeded))) return false;
    auto Candidate = Seed; Candidate.Revision = 2; Candidate.Coins = 8; Candidate.Inventory[0] = {Jadebound::ItemId::Moonleaf, 1};
    Access::SetOpener(*Scope.Model, [&Scope](const FString& Path) -> IFileHandle*
    {
        if (Path != Scope.Paths[2] && Path != Scope.Paths[3]) { Scope.bForeign = true; return nullptr; }
        ++Scope.Opens; Scope.LastOpenedPath = Path;
        return nullptr; // Controlled first-opener failure; no file was opened.
    });
    const int64 Sequence = Scope.Model->GetLastActionReceipt().Sequence;
    Access::BuyRequest(*Scope.Model, 4, 1, 1);
    HeldImage Held;
    FJadeboundProgressionState Encoded;
    if (!ReceiptIs(*this, *Scope.Model, Sequence, TEXT("Buy"), false, 2, 1, TEXT("Nonzero torso purchase reaches held r2 once"), true) ||
        !TestTrue(TEXT("Held core/reflection/candidate preserve armor while committed V3 r1 retains its full original payload"),
            CaptureHeld(Scope, Held) && Jadebound::Persistence::EncodeStateV3(Candidate, Encoded) &&
            SameCore(Held.Current.Core, Candidate) && SameReflected(Held.Current.Reflected, Encoded) && SameReflected(Held.Candidate, Encoded) &&
            Held.Current.Core.EquippedTorso == Jadebound::ItemId::WardenCoat && Held.Candidate.EquippedTorso == 8 &&
            Held.CandidateId == Scope.Id && Held.CandidateDrops.IsEmpty() && Held.Current.Pending.IsEmpty() &&
            Held.Current.Saved == Seeded.Saved && Held.Current.CharacterId == Seeded.CharacterId &&
            SameReflected(Held.Current.Committed, Seeded.Committed) && SameDrops(Held.Current.SavedDrops, Seeded.SavedDrops) &&
            Held.Current.Committed.EquippedTorso == 8 && Held.Current.Slot == Seeded.Slot && Held.Current.Opens == Seeded.Opens + 1)) return false;
    for (int32 I = 0; I < JournalPathCount; ++I)
        if (!TestTrue(TEXT("Failed armored purchase preserves all eight actual namespace banks"), SameBank(Held.Current.Banks[I], Seeded.Banks[I]))) return false;
    Scope.InstallOpener();
    Access::ExpireLocalFixtureDeadline(*Scope.Model); // Explicit fixture deadline, no elapsed-backoff claim.
    if (!CaptureHeld(Scope, Held) || !TestTrue(TEXT("One retry verifies the same armored r2 with no second transaction or acknowledgment"),
        Access::Retry(*Scope.Model) && Scope.StateIs(Candidate) && Scope.Model->GetLastActionReceipt().Sequence == Sequence + 1)) return false;
    Image Verified;
    if (!TestTrue(TEXT("Armored retry commits one bank and clears held state/cache through the complete clean fixture check"),
        Scope.Capture(Verified) && Verified.Opens == Held.Current.Opens + 1 && Verified.Slot == 1 - Held.Current.Slot &&
        Scope.LastOpenedPath == Scope.Paths[2 + Verified.Slot] && Verified.Saved != Held.Current.Saved &&
        !SameBank(Verified.Banks[2 + Verified.Slot], Held.Current.Banks[2 + Verified.Slot]))) return false;
    for (int32 I = 0; I < JournalPathCount; ++I)
        if (I != 2 + Verified.Slot && !TestTrue(TEXT("Armored retry changes no other namespace bank"), SameBank(Verified.Banks[I], Held.Current.Banks[I]))) return false;
    if (!ReopenAs(*this, Scope, Candidate)) return false;
    AddInfo(TEXT("Synthetic nonzero armor fixture, actual buy handler, failed opener, held retry and replacement actor full-state readback. No armor earning, visible equip, gameplay, clients, networking, elapsed backoff or crash recovery proof."));
    return true;
}
#endif
