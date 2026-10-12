#pragma once

#include <array>
#include <cstdint>
#include <vector>

// Portable C++17 domain contract. No Unreal types, IO, clocks or random values.
namespace Jadebound {

inline constexpr std::uint32_t SnapshotVersion = 2;
inline constexpr std::uint32_t InventorySlotCount = 12;
inline constexpr std::uint32_t DefinitionCount = 7;
inline constexpr std::uint32_t MaximumUnprocessedDrops = 4096;
// Actor persistence uses signed int64. Keep the existing uint64 API while
// refusing any value or increment that would be truncated by that storage.
inline constexpr std::uint64_t MaximumPersistedCounter = 9223372036854775807ULL;
inline constexpr std::int64_t QuestCoinReward = 5;

// These IDs are persisted. Never renumber or reuse them for another item.
enum class ItemId : std::uint32_t {
    None = 0,
    JadeGlaive = 1,
    AshwoodSpear = 2,
    IronOre = 3,
    Moonleaf = 4,
    JadeParcel = 5,
    JadeShard = 6,
    Balm = 7
};

enum class QuestState : std::uint32_t {
    NotAccepted = 0,
    Accepted = 1,
    RaiderDefeated = 2,
    ParcelRecovered = 3,
    Completed = 4
};

struct ItemDefinition {
    ItemId Id;
    const char* StableKey;
    const char* DisplayName;
    std::uint32_t StackLimit;
    std::int64_t BuyPrice;
    std::int64_t SellPrice;
    bool Purchasable;
    bool Sellable;
    bool Weapon;
    // Zero means not consumable; recovery is a percentage of authority-owned Max HP.
    std::uint32_t HealingPercent = 0;
};

struct ItemStack {
    ItemId Id = ItemId::None;
    std::uint32_t Count = 0;
};

struct Snapshot {
    std::uint32_t Version = SnapshotVersion;
    std::uint64_t Revision = 0;
    std::int64_t Coins = 10;
    std::array<ItemStack, InventorySlotCount> Inventory{};
    ItemId EquippedWeapon = ItemId::JadeGlaive;
    QuestState Quest = QuestState::NotAccepted;
    // The authority assigns this ID when recording the next defeated raider.
    // It persists across reopen, so an old drop cannot become a new drop.
    std::uint64_t NextDropId = 1;
    std::uint64_t QuestKillDropId = 0;
    // Every ID in [1, NextDropId) has been issued, with no gaps or reset.
    // Only outstanding claims are stored, strictly increasing and bounded.
    // An issued ID absent here is permanently processed, even after reload.
    std::vector<std::uint64_t> UnprocessedDropIds;
};

enum class ResultCode : std::uint32_t {
    Ok = 0,
    InvalidItem,
    InvalidQuantity,
    InvalidSlot,
    NotPurchasable,
    NotSellable,
    InsufficientCoins,
    InsufficientItems,
    InventoryFull,
    NotWeapon,
    AlreadyEquipped,
    NothingEquipped,
    QuestNotAccepted,
    QuestAlreadyAccepted,
    QuestAlreadyCompleted,
    QuestNeedsDefeat,
    QuestNeedsParcel,
    ParcelAlreadyRecovered,
    InvalidDropId,
    DuplicateDrop,
    UnknownDrop,
    DropAlreadyProcessed,
    DropLedgerFull,
    CurrencyOverflow,
    RevisionOverflow,
    InvalidSnapshot,
    DropReservedForQuest,
    NotConsumable,
    DropIdOverflow
};

enum class ValidationCode : std::uint32_t {
    None = 0,
    UnsupportedVersion,
    NegativeCurrency,
    UnknownInventoryItem,
    EmptySlotCountMismatch,
    StackLimitExceeded,
    InvalidEquippedWeapon,
    UnknownQuestState,
    DropLedgerTooLarge,
    InvalidRecordedDropId,
    DuplicateRecordedDropId,
    InvalidProcessedDropId,
    DuplicateProcessedDropId,
    ProcessedDropNotRecorded,
    QuestLedgerMismatch,
    QuestParcelMismatch,
    RevisionBelowRecordedTransactions,
    InvalidDropSequence,
    QuestDropMismatch,
    CounterOutOfRange,
    InvalidUnprocessedDropId,
    DuplicateUnprocessedDropId,
    UnsortedUnprocessedDropIds
};

struct Result {
    ResultCode Code = ResultCode::Ok;
    ValidationCode Validation = ValidationCode::None;
    bool Changed = false;
    std::uint64_t Revision = 0;

    bool Succeeded() const noexcept { return Code == ResultCode::Ok; }
};

class ProgressionCore {
public:
    ProgressionCore() = default;

    static const std::array<ItemDefinition, DefinitionCount>& GetDefinitions() noexcept;
    static const ItemDefinition* FindDefinition(ItemId id) noexcept;
    const Snapshot& GetSnapshot() const noexcept { return State; }

    // Prices are looked up internally from the fixed catalog, never supplied by
    // a client. Quantity is signed so zero and negative requests are rejected.
    Result Buy(ItemId sku, std::int64_t quantity);
    Result Sell(std::uint32_t inventorySlot, std::int64_t quantity);
    // Move existing items to the first empty cell, never merge or award items.
    // Leave at least one item in the source; a successful split commits once.
    Result SplitStack(std::uint32_t inventorySlot, std::int64_t quantity);
    Result Equip(std::uint32_t inventorySlot);
    Result Unequip();
    // Exactly one owned consumable; no health, IO or client-supplied effect amount.
    Result Consume(std::uint32_t inventorySlot);

    // This prototype has one nonrepeatable quest. Defeats can produce ordinary
    // loot in any quest state. Only the first defeat after acceptance is the
    // quest parcel drop; that ID cannot be consumed by ordinary GrantLoot.
    Result AcceptQuest();
    Result RecordRaiderDefeat(std::uint64_t dropId);
    // Server-authority API: id/quantity come from trusted drop definitions,
    // never from merchant UI requests. JadeParcel uses PickupParcel instead.
    Result GrantLoot(ItemId id, std::int64_t quantity, std::uint64_t dropId);
    Result PickupParcel(std::uint64_t dropId);
    Result TurnInQuest();

    static Result ValidateSnapshot(const Snapshot& candidate);
    // Loading adopts the persisted revision exactly; it is not a new gameplay
    // transaction. Failure preserves every field of the current snapshot.
    Result LoadSnapshot(const Snapshot& candidate);

private:
    Snapshot State;
    Result Failure(ResultCode code) const noexcept;
    Result Commit(Snapshot&& candidate);
    static bool AddItems(Snapshot& candidate, ItemId id, std::uint64_t quantity);
};

} // namespace Jadebound
