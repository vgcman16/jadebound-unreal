#pragma once

#include "CoreMinimal.h"
#include "JadeboundProgressionActor.h"
#include "Styling/SlateTypes.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SScrollBox;
class FGCObject;

// Runtime skill quickbar is implemented as a separate local HUD sibling;
// it does not own the economic panel or change its transaction callbacks.

// This panel presents committed state and sends intent through the actor API.
// It never owns the actor or changes inventory/progression state itself.
class SJadeboundProgressionPanel final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SJadeboundProgressionPanel) {}
        SLATE_ARGUMENT(TWeakObjectPtr<AJadeboundProgressionActor>, ProgressionActor)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    static TSharedRef<SWidget> BuildGameplayHud(TWeakObjectPtr<AJadeboundProgressionActor> Actor);
    virtual void Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float InDeltaTime) override;
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
    virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
    virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
    virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FJadeboundMerchantBulkSaleTestAccess;
#endif
    enum class EAction : uint8 { Buy, Sell, Equip, Unequip, Save, Reload, Close, Use, Split };

    static constexpr int32 DisplaySlotCount = 12;
    static constexpr int32 MaximumCatalogItems = 7;
    TWeakObjectPtr<AJadeboundProgressionActor> ProgressionActor;
    FJadeboundProgressionState DisplayState;
    TArray<FJadeboundItemView> Definitions;
    FText QuestText;
    FText FeedbackText;
    FJadeboundPersistencePresentation PersistencePresentation;
    FText RenderedSelectedUseRestriction;
    FText RenderedSelectedSplitRestriction;
    int32 SelectedSlot = INDEX_NONE;
    bool bSkillsPage = false;
    enum class EContextPage : uint8 { Equipment, Merchant, Journey };
    EContextPage ContextPage = EContextPage::Equipment;
    float RetainedCatalogOffset = 0.f;
    float RetainedInventoryOffset = 0.f;
    bool bReady = false;
    bool bCanSave = false;
    bool bMerchantInRange = false;
    bool bHasSnapshot = false;
    bool bActorWasValid = false;
    bool bLastReportedReady = false;
    double NextPollTime = 0.0;
    FButtonStyle ActionStyle;
    FButtonStyle MerchantStyle;
    // Original brush textures remain referenced for the panel's full lifetime.
    TSharedPtr<FGCObject> MerchantArtResources;
    FButtonStyle SecondaryStyle;
    FButtonStyle SlotStyle;
    FButtonStyle EmptySlotStyle;
    FButtonStyle SelectedSlotStyle;
    TSharedPtr<SScrollBox> CatalogScroll;
    TSharedPtr<SScrollBox> InventoryScroll;
    TMap<FName, TWeakPtr<SWidget>> FocusTargets;
    TSharedPtr<const uint8> TradeRenderToken;
    bool bAwaitingAction = false;
    bool bPendingLong = false;
    int64 PendingReceiptSequence = 0;
    int64 LastReceiptSequence = 0;
    double PendingSince = 0.0;
    FName PendingAction;

    void RefreshFromActor(bool bForce);
    void RebuildContents();
    TSharedRef<SWidget> BuildInventory();
    TSharedRef<SWidget> BuildSkills();
    TSharedRef<SWidget> BuildHotbarAssignmentRow();
    FReply ShowPackPage(bool Skills);
    FReply ShowContextPage(EContextPage Page);
    TSharedRef<SWidget> BuildMerchant();
    TSharedRef<SWidget> BuildSelection();
    TSharedRef<SWidget> BuildEquipment();
    TSharedRef<SWidget> BuildQuest();
    FReply ExecuteAction(EAction Action, int32 Argument, int64 ExpectedRevision, TSharedPtr<const uint8> RenderToken);
    FReply ExecuteActionWithQuantity(EAction Action, int32 Argument, int64 ExpectedRevision,
        int32 Quantity, int32 ExpectedItemId, TSharedPtr<const uint8> RenderToken);
    TSharedRef<SWidget> ActionButton(const FText& Label, const FText& Tooltip,
        bool bEnabled, EAction Action, int32 Argument = INDEX_NONE,
        int32 Quantity = 1, int32 ExpectedItemId = 0);
    FReply SelectSlot(int32 SlotIndex);
    FReply HandleInventoryMouseDown(const FGeometry& Geometry, const FPointerEvent& Event,
        int32 SlotIndex, int32 RenderedItemId, int64 RenderedRevision, TSharedPtr<const uint8> RenderToken);
    FReply HandleMerchantMouseDown(const FGeometry& Geometry, const FPointerEvent& Event,
        int32 RenderedItemId, int64 RenderedRevision, int64 RenderedBuyPrice, TSharedPtr<const uint8> RenderToken);
    FReply HandleMerchantDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event);
    FReply HandleInventoryDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event);
    FReply HandleEquippedDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event,
        int32 RenderedWeapon, int64 RenderedRevision, TSharedPtr<const uint8> RenderToken);

    const FJadeboundItemView* FindDefinition(int32 ItemId) const;
    const FJadeboundInventorySlot* GetSelectedSlot() const;
    bool HasCapacityForOne(int32 ItemId, int32 IgnoredSlot = INDEX_NONE) const;
    FText ItemTooltip(const FJadeboundItemView& Item) const;
    FText BuyRestriction(const FJadeboundItemView& Item) const;
    FText SellRestriction(int32 Quantity = 1) const;
    FText EquipRestriction() const;
    FText EquipRestrictionForSlot(int32 SlotIndex) const;
    FText UnequipRestriction() const;
    FText UseRestrictionForSlot(int32 SlotIndex) const;
    FText SplitRestrictionForSlot(int32 SlotIndex) const;
};
