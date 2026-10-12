#include "JadeboundProgressionActor.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/JadeboundMerchantFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundMerchantBulkSaleTest,
    "Jadebound.Progression.MerchantBulkSale", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FJadeboundMerchantBulkSaleTest::RunTest(const FString& Parameters)
{
    using namespace JadeboundMerchantBulkSale;
    FScope Scope(*this);
    if (!Scope.bValid) return false;
    const Jadebound::Snapshot Initial;
    auto Bought = Initial; Bought.Revision = 1; Bought.Coins = 0; Bought.Inventory[0] = {Jadebound::ItemId::Moonleaf, 5};
    auto SoldOne = Bought; SoldOne.Revision = 2; SoldOne.Coins = 1; SoldOne.Inventory[0].Count = 4;
    auto SoldStack = SoldOne; SoldStack.Revision = 3; SoldStack.Coins = 5; SoldStack.Inventory[0] = {};
    Image Before;
    if (!TestTrue(TEXT("Known r0 and actual IO baseline"), Scope.StateIs(Initial) && Scope.Capture(Before))) return false;
    if (!TestTrue(TEXT("Public buy five commits exact r1/zero coins/five moonleaf and one journal write"),
        Scope.Model->BuyItem(static_cast<int32>(Jadebound::ItemId::Moonleaf), 5) && Scope.Advanced(Before, Bought))) return false;
    if (!Scope.Capture(Before)) { AddError(TEXT("Post-buy actual IO baseline unavailable.")); return false; }
    if (!TestTrue(TEXT("Public sell one commits exact r2/one coin/four moonleaf and one journal write"),
        Scope.Model->SellItem(0, 1, 1) && Scope.Advanced(Before, SoldOne))) return false;
    if (!Scope.Capture(Before)) { AddError(TEXT("Pre-bulk-sale full actual IO baseline unavailable.")); return false; }
    const int32 InvalidQuantities[] = {0, -1, 5};
    for (const int32 Quantity : InvalidQuantities)
        if (!TestTrue(TEXT("Invalid sell quantity preserves whole core/reflection/committed payload/banks/open count"),
            !Scope.Model->SellItem(0, Quantity, 2) && Scope.Unchanged(Before) && Scope.StateIs(SoldOne))) return false;
    if (!TestTrue(TEXT("Stale r1 bulk intent cannot sell the newer r2 stack or write a bank"),
        !Scope.Model->SellItem(0, 4, 1) && Scope.Unchanged(Before) && Scope.StateIs(SoldOne))) return false;
    if (!TestTrue(TEXT("Public bulk sell four commits exact r3/five coins/empty slot and one journal write"),
        Scope.Model->SellItem(0, 4, 2) && Scope.Advanced(Before, SoldStack))) return false;
    Image Final;
    if (!Scope.Capture(Final)) { AddError(TEXT("Final actual r3 banks unavailable.")); return false; }
    if (!TestTrue(TEXT("A fresh replacement actor with real new lease reopens the complete actual r3"),
        Scope.Reopen() && Scope.StateIs(SoldStack) && Access::BankSlot(*Scope.Model) == Final.Slot && Scope.Opens == Final.Opens)) return false;
    Image Reopened;
    if (!Scope.Capture(Reopened)) { AddError(TEXT("Reopened actual IO baseline unavailable.")); return false; }
    for (int32 I = 0; I < 4; ++I)
        if (!TestTrue(TEXT("Replacement ReadSave does not rewrite or discard either journal namespace"), SameBank(Final.Banks[I], Reopened.Banks[I]))) return false;
    if (!TestTrue(TEXT("Replayed stale bulk sale after reopen cannot pay again or write"),
        !Scope.Model->SellItem(0, 4, 2) && Scope.Unchanged(Reopened) && Scope.StateIs(SoldStack))) return false;
    AddInfo(FString::Printf(TEXT("Actual public merchant buy5/sell1/sell4/refusals and replacement r3 readback verified by this fixture; files retained: %s. Trusted registry/readiness, no UI input/admission/network/client proof."), *FPaths::GetPath(Scope.Paths[2])));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundStackSaleQuoteTest,
    "Jadebound.Presentation.StackSaleQuote", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FJadeboundStackSaleQuoteTest::RunTest(const FString& Parameters)
{
    constexpr std::int64_t Maximum = 9223372036854775807LL;
    std::int64_t Total = 99;
    if (!TestTrue(TEXT("Four at one quotes four with one existing coin"),
        Jadebound::Presentation::TryStackSaleTotal(4, 1, 1, Total) && Total == 4)) return false;
    struct Refusal { std::int32_t Quantity; std::int64_t Price; std::int64_t Coins; };
    const Refusal Refusals[] = {{0, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {1, 1, -1}, {2, Maximum, 0}, {1, 1, Maximum}};
    for (const Refusal& Case : Refusals)
    {
        Total = 99;
        if (!TestTrue(TEXT("Invalid or overflowing quote refuses with zero output"),
            !Jadebound::Presentation::TryStackSaleTotal(Case.Quantity, Case.Price, Case.Coins, Total) && Total == 0)) return false;
    }
    if (!TestTrue(TEXT("Exact multiplication ceiling quotes the full signed maximum"),
        Jadebound::Presentation::TryStackSaleTotal(1, Maximum, 0, Total) && Total == Maximum)) return false;
    if (!TestTrue(TEXT("Exact coin addition ceiling permits four credit"),
        Jadebound::Presentation::TryStackSaleTotal(4, 1, Maximum - 4, Total) && Total == 4)) return false;
    if (!TestTrue(TEXT("Zero-price positive quantity remains valid at maximum coins"),
        Jadebound::Presentation::TryStackSaleTotal(2147483647, 0, Maximum, Total) && Total == 0)) return false;
    AddInfo(TEXT("Read-only quote arithmetic only; no UI callback, authority transaction, journal, network or rendered control proof."));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundPanelRetainedActionTest,
    "Jadebound.Presentation.PanelRetainedActions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FJadeboundPanelRetainedActionTest::RunTest(const FString& Parameters)
{
    using namespace JadeboundMerchantBulkSale;
    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Initialized native Slate required.")); return false; }
    {
        FScope Scope(*this);
        if (!Scope.bValid) return false;
        const Jadebound::Snapshot Initial;
        auto Prepared = Initial;
        Prepared.Revision = 2; Prepared.Coins = 3;
        Prepared.Inventory[0] = {Jadebound::ItemId::Moonleaf, 2};
        Prepared.Inventory[1] = {Jadebound::ItemId::IronOre, 1};
        if (!TestTrue(TEXT("Actual public purchases establish two independently known stacks"),
            Scope.Model->BuyItem(4, 2) && Scope.Model->BuyItem(3, 1) && Scope.StateIs(Prepared))) return false;
        Access::SetFixtureMenu(*Scope.Model, true);
        TSharedPtr<SJadeboundProgressionPanel> Panel = SNew(SJadeboundProgressionPanel).ProgressionActor(Scope.Model);
        Access::Select(*Panel, 0);
        auto OldSell = Access::Button(*Panel, TEXT("Action-1-0-1"));
        Access::Select(*Panel, 1); // Actual same-revision panel rebuild, not a fabricated token.
        Image Before;
        if (!OldSell || !Scope.Capture(Before)) { AddError(TEXT("Real retained Sell 1 and bank baseline required.")); return false; }
        OldSell->SimulateClick(); // Only this isolated native widget; never the running game or a viewport.
        if (!TestTrue(TEXT("Retained Sell 1 after selection change preserves both stacks, coins, commit and exact banks"),
            Scope.Unchanged(Before) && Scope.StateIs(Prepared))) return false;
        Access::Shop(*Panel);
        auto SameStateOldBuy = Access::Button(*Panel, TEXT("Action-0-4-1"));
        Access::Shop(*Panel); // Same page, selection and revision; only the render generation changes.
        if (!SameStateOldBuy || !Scope.Capture(Before)) return false;
        SameStateOldBuy->SimulateClick();
        if (!TestTrue(TEXT("Obsolete render alone refuses purchase with unchanged selection and exact revision"), Scope.Unchanged(Before))) return false;
        auto OldBuy = Access::Button(*Panel, TEXT("Action-0-4-1"));
        auto FreshSell = Access::Button(*Panel, TEXT("Action-1-1-1"));
        if (!OldBuy || !FreshSell || !Scope.Capture(Before)) { AddError(TEXT("Actual current merchant controls required.")); return false; }
        auto Sold = Prepared; Sold.Revision = 3; Sold.Coins = 4; Sold.Inventory[1] = {};
        FreshSell->SimulateClick();
        if (!TestTrue(TEXT("Fresh Sell 1 pays only the displayed ore and writes exactly one bank"), Scope.Advanced(Before, Sold))) return false;
        if (!Scope.Capture(Before)) return false;
        OldBuy->SimulateClick();
        if (!TestTrue(TEXT("Retained Buy 1 after inventory refresh cannot spend again or write"), Scope.Unchanged(Before))) return false;
        auto FreshBuy = Access::Button(*Panel, TEXT("Action-0-4-1"));
        if (!FreshBuy || !Scope.Capture(Before)) return false;
        auto Bought = Sold; Bought.Revision = 4; Bought.Coins = 2; Bought.Inventory[0].Count = 3;
        FreshBuy->SimulateClick();
        if (!TestTrue(TEXT("Fresh Buy 1 spends the catalog price and saves exactly one additional leaf"), Scope.Advanced(Before, Bought))) return false;
        auto ClosedBuy = Access::Button(*Panel, TEXT("Action-0-4-1"));
        Access::SetFixtureMenu(*Scope.Model, false);
        if (!ClosedBuy || !Scope.Capture(Before)) return false;
        ClosedBuy->SimulateClick();
        if (!TestTrue(TEXT("A detached closed-menu purchase cannot spend or write"), Scope.Unchanged(Before))) return false;
        Access::SetFixtureMenu(*Scope.Model, true);
        Access::Refresh(*Panel);
        Access::Select(*Panel, 0);
        auto OldSplit = Access::Button(*Panel, TEXT("Action-8-0-1"));
        Access::Select(*Panel, 1);
        if (!OldSplit || !Scope.Capture(Before)) return false;
        OldSplit->SimulateClick();
        if (!TestTrue(TEXT("Retained Split after same-revision selection change preserves the original stack and banks"), Scope.Unchanged(Before))) return false;
        OldSell.Reset(); SameStateOldBuy.Reset(); OldBuy.Reset(); FreshSell.Reset(); FreshBuy.Reset(); ClosedBuy.Reset(); OldSplit.Reset();
        Panel.Reset(); // Drop all panel/style references before retiring its isolated actor.
        if (!TestTrue(TEXT("Replacement authority actor reopens the exact final merchant state"), Scope.Reopen() && Scope.StateIs(Bought))) return false;
    }
    {
        FScope Scope(*this);
        if (!Scope.bValid) return false;
        const Jadebound::Snapshot Initial;
        auto Prepared = Initial; Prepared.Revision = 1; Prepared.Coins = 2;
        Prepared.Inventory[0] = {Jadebound::ItemId::AshwoodSpear, 1};
        if (!TestTrue(TEXT("Actual spear purchase establishes the gear fixture"), Scope.Model->BuyItem(2, 1) && Scope.StateIs(Prepared))) return false;
        Access::SetFixtureMenu(*Scope.Model, true);
        TSharedPtr<SJadeboundProgressionPanel> Panel = SNew(SJadeboundProgressionPanel).ProgressionActor(Scope.Model);
        Access::Gear(*Panel);
        auto OldUnequip = Access::Button(*Panel, TEXT("Action-3--1-1"));
        auto Equipped = Prepared; Equipped.Revision = 2; Equipped.EquippedWeapon = Jadebound::ItemId::AshwoodSpear;
        Equipped.Inventory[0] = {Jadebound::ItemId::JadeGlaive, 1};
        if (!OldUnequip || !TestTrue(TEXT("Authority equips spear before the panel polls"),
            Scope.Model->EquipItem(0, 1) && Scope.StateIs(Equipped))) return false;
        Image Before;
        if (!Scope.Capture(Before)) return false;
        OldUnequip->SimulateClick();
        if (!TestTrue(TEXT("Old unequip cannot remove the new weapon during live/display revision drift"), Scope.Unchanged(Before))) return false;
        auto FreshUnequip = Access::Button(*Panel, TEXT("Action-3--1-1"));
        if (!FreshUnequip || !Scope.Capture(Before)) return false;
        auto Unequipped = Equipped; Unequipped.Revision = 3; Unequipped.EquippedWeapon = Jadebound::ItemId::None;
        Unequipped.Inventory[1] = {Jadebound::ItemId::AshwoodSpear, 1};
        FreshUnequip->SimulateClick();
        if (!TestTrue(TEXT("Fresh unequip returns only the displayed spear and saves exactly once"), Scope.Advanced(Before, Unequipped))) return false;
        OldUnequip.Reset(); FreshUnequip.Reset(); Panel.Reset();
        if (!TestTrue(TEXT("Replacement actor reopens the exact final gear state"), Scope.Reopen() && Scope.StateIs(Unequipped))) return false;
    }
    AddInfo(TEXT("Actual isolated Slate buttons and captured callbacks, public authority transactions and real journal bytes/reopen. Synthetic menu/registry setup; no viewport, OS/game input, networking, normal admission, gameplay or visual acceptance."));
    return true;
}
#endif
