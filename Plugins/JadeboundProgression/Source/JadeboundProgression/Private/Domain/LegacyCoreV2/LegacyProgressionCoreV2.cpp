#include "ProgressionCore.h"

#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>

namespace JadeboundLegacyV2 {
namespace {

constexpr std::array<ItemDefinition, DefinitionCount> Catalog{{
    {ItemId::JadeGlaive, "jade_glaive", "Jade Glaive", 1, 12, 4, true, true, true},
    {ItemId::AshwoodSpear, "ashwood_spear", "Ashwood Spear", 1, 8, 3, true, true, true},
    {ItemId::IronOre, "iron_ore", "Iron Ore", 20, 3, 1, true, true, false},
    {ItemId::Moonleaf, "moonleaf", "Moonleaf", 20, 2, 1, true, true, false},
    {ItemId::JadeParcel, "jade_parcel", "Jade Parcel", 1, 0, 0, false, false, false},
    {ItemId::JadeShard, "jade_shard", "Jade Shard", 50, 0, 2, false, true, false},
    {ItemId::Balm, "balm", "Balm", 10, 3, 1, true, true, false, 25}
}};

bool CheckedMultiply(std::int64_t quantity, std::int64_t price, std::int64_t& value) noexcept {
    if (quantity < 0 || price < 0 ||
        (price != 0 && quantity > std::numeric_limits<std::int64_t>::max() / price)) {
        return false;
    }
    value = quantity * price;
    return true;
}

bool CheckedAdd(std::int64_t coins, std::int64_t credit, std::int64_t& value) noexcept {
    if (coins < 0 || credit < 0 || credit > std::numeric_limits<std::int64_t>::max() - coins) {
        return false;
    }
    value = coins + credit;
    return true;
}

bool HasId(const std::vector<std::uint64_t>& ids, std::uint64_t id) noexcept {
    return std::binary_search(ids.begin(), ids.end(), id);
}

bool EqualSnapshots(const Snapshot& left, const Snapshot& right) noexcept {
    if (left.Version != right.Version || left.Revision != right.Revision ||
        left.Coins != right.Coins || left.EquippedWeapon != right.EquippedWeapon ||
        left.Quest != right.Quest || left.NextDropId != right.NextDropId ||
        left.QuestKillDropId != right.QuestKillDropId ||
        left.UnprocessedDropIds != right.UnprocessedDropIds) {
        return false;
    }
    for (std::uint32_t i = 0; i < InventorySlotCount; ++i) {
        if (left.Inventory[i].Id != right.Inventory[i].Id ||
            left.Inventory[i].Count != right.Inventory[i].Count) {
            return false;
        }
    }
    return true;
}

Result Invalid(const Snapshot& candidate, ValidationCode reason) noexcept {
    return {ResultCode::InvalidSnapshot, reason, false, candidate.Revision};
}

} // namespace

const std::array<ItemDefinition, DefinitionCount>& ProgressionCore::GetDefinitions() noexcept {
    return Catalog;
}

const ItemDefinition* ProgressionCore::FindDefinition(ItemId id) noexcept {
    for (const auto& definition : Catalog) {
        if (definition.Id == id) {
            return &definition;
        }
    }
    return nullptr;
}

Result ProgressionCore::Failure(ResultCode code) const noexcept {
    return {code, ValidationCode::None, false, State.Revision};
}

Result ProgressionCore::Commit(Snapshot&& candidate) {
    if (State.Revision == MaximumPersistedCounter) {
        return Failure(ResultCode::RevisionOverflow);
    }
    candidate.Revision = State.Revision + 1;
    const auto valid = ValidateSnapshot(candidate);
    if (!valid.Succeeded()) {
        return {valid.Code, valid.Validation, false, State.Revision};
    }
    static_assert(std::is_nothrow_move_assignable<Snapshot>::value,
                  "Snapshot commit must not partially replace live state");
    State = std::move(candidate);
    return {ResultCode::Ok, ValidationCode::None, true, State.Revision};
}

bool ProgressionCore::AddItems(Snapshot& candidate, ItemId id, std::uint64_t quantity) {
    const auto* definition = FindDefinition(id);
    if (definition == nullptr || quantity == 0) {
        return false;
    }
    // Capacity preflight prevents even the candidate from being partly filled.
    std::uint64_t capacity = 0;
    for (const auto& slot : candidate.Inventory) {
        if (slot.Id == ItemId::None) {
            capacity += definition->StackLimit;
        } else if (slot.Id == id) {
            capacity += definition->StackLimit - slot.Count;
        }
    }
    if (quantity > capacity) {
        return false;
    }
    for (auto& slot : candidate.Inventory) {
        if (slot.Id == id && quantity != 0) {
            const auto add = std::min<std::uint64_t>(quantity, definition->StackLimit - slot.Count);
            slot.Count += static_cast<std::uint32_t>(add);
            quantity -= add;
        }
    }
    for (auto& slot : candidate.Inventory) {
        if (slot.Id == ItemId::None && quantity != 0) {
            const auto add = std::min<std::uint64_t>(quantity, definition->StackLimit);
            slot = {id, static_cast<std::uint32_t>(add)};
            quantity -= add;
        }
    }
    return quantity == 0;
}

Result ProgressionCore::Buy(ItemId sku, std::int64_t quantity) {
    if (quantity <= 0) {
        return Failure(ResultCode::InvalidQuantity);
    }
    const auto* definition = FindDefinition(sku);
    if (definition == nullptr) {
        return Failure(ResultCode::InvalidItem);
    }
    if (!definition->Purchasable) {
        return Failure(ResultCode::NotPurchasable);
    }
    std::int64_t cost = 0;
    if (!CheckedMultiply(quantity, definition->BuyPrice, cost)) {
        return Failure(ResultCode::CurrencyOverflow);
    }
    if (State.Coins < cost) {
        return Failure(ResultCode::InsufficientCoins);
    }
    Snapshot candidate = State;
    if (!AddItems(candidate, sku, static_cast<std::uint64_t>(quantity))) {
        return Failure(ResultCode::InventoryFull);
    }
    candidate.Coins -= cost;
    return Commit(std::move(candidate));
}

Result ProgressionCore::Sell(std::uint32_t inventorySlot, std::int64_t quantity) {
    if (quantity <= 0) {
        return Failure(ResultCode::InvalidQuantity);
    }
    if (inventorySlot >= InventorySlotCount) {
        return Failure(ResultCode::InvalidSlot);
    }
    const auto& slot = State.Inventory[inventorySlot];
    if (static_cast<std::uint64_t>(quantity) > slot.Count) {
        return Failure(ResultCode::InsufficientItems);
    }
    const auto* definition = FindDefinition(slot.Id);
    if (definition == nullptr) {
        return Failure(ResultCode::InsufficientItems);
    }
    if (!definition->Sellable) {
        return Failure(ResultCode::NotSellable);
    }
    std::int64_t credit = 0;
    std::int64_t coins = 0;
    if (!CheckedMultiply(quantity, definition->SellPrice, credit) ||
        !CheckedAdd(State.Coins, credit, coins)) {
        return Failure(ResultCode::CurrencyOverflow);
    }
    Snapshot candidate = State;
    auto& sold = candidate.Inventory[inventorySlot];
    sold.Count -= static_cast<std::uint32_t>(quantity);
    if (sold.Count == 0) {
        sold = {};
    }
    candidate.Coins = coins;
    return Commit(std::move(candidate));
}

Result ProgressionCore::SplitStack(std::uint32_t inventorySlot, std::int64_t quantity) {
    if (quantity <= 0) {
        return Failure(ResultCode::InvalidQuantity);
    }
    if (inventorySlot >= InventorySlotCount) {
        return Failure(ResultCode::InvalidSlot);
    }
    const auto& source = State.Inventory[inventorySlot];
    if (source.Count == 0 || source.Id == ItemId::None) {
        return Failure(ResultCode::InsufficientItems);
    }
    const auto* definition = FindDefinition(source.Id);
    if (definition == nullptr) {
        return Failure(ResultCode::InvalidItem);
    }
    if (definition->StackLimit <= 1 ||
        static_cast<std::uint64_t>(quantity) >= source.Count) {
        return Failure(ResultCode::InvalidQuantity);
    }
    const auto empty = std::find_if(State.Inventory.begin(), State.Inventory.end(),
        [](const ItemStack& slot) { return slot.Id == ItemId::None && slot.Count == 0; });
    if (empty == State.Inventory.end()) {
        return Failure(ResultCode::InventoryFull);
    }
    const auto destination = static_cast<std::uint32_t>(empty - State.Inventory.begin());
    Snapshot candidate = State;
    const auto moved = static_cast<std::uint32_t>(quantity); // Bounded below source.Count.
    candidate.Inventory[inventorySlot].Count -= moved;
    candidate.Inventory[destination] = {source.Id, moved};
    return Commit(std::move(candidate));
}

Result ProgressionCore::Equip(std::uint32_t inventorySlot) {
    if (inventorySlot >= InventorySlotCount) {
        return Failure(ResultCode::InvalidSlot);
    }
    const auto& slot = State.Inventory[inventorySlot];
    if (slot.Count == 0) {
        return Failure(ResultCode::InsufficientItems);
    }
    const auto* definition = FindDefinition(slot.Id);
    if (definition == nullptr || !definition->Weapon) {
        return Failure(ResultCode::NotWeapon);
    }
    if (slot.Id == State.EquippedWeapon) {
        return Failure(ResultCode::AlreadyEquipped);
    }
    Snapshot candidate = State;
    // Weapon stacks are exactly one. Removing the new weapon frees the slot
    // used for the old weapon, even when every inventory slot was occupied.
    candidate.EquippedWeapon = slot.Id;
    candidate.Inventory[inventorySlot] = State.EquippedWeapon == ItemId::None
        ? ItemStack{} : ItemStack{State.EquippedWeapon, 1};
    return Commit(std::move(candidate));
}

Result ProgressionCore::Unequip() {
    if (State.EquippedWeapon == ItemId::None) {
        return Failure(ResultCode::NothingEquipped);
    }
    Snapshot candidate = State;
    if (!AddItems(candidate, candidate.EquippedWeapon, 1)) {
        return Failure(ResultCode::InventoryFull);
    }
    candidate.EquippedWeapon = ItemId::None;
    return Commit(std::move(candidate));
}

Result ProgressionCore::AcceptQuest() {
    if (State.Quest == QuestState::Completed) {
        return Failure(ResultCode::QuestAlreadyCompleted);
    }
    if (State.Quest != QuestState::NotAccepted) {
        return Failure(ResultCode::QuestAlreadyAccepted);
    }
    Snapshot candidate = State;
    candidate.Quest = QuestState::Accepted;
    return Commit(std::move(candidate));
}

Result ProgressionCore::RecordRaiderDefeat(std::uint64_t dropId) {
    if (dropId == 0) {
        return Failure(ResultCode::InvalidDropId);
    }
    if (dropId < State.NextDropId) {
        return Failure(ResultCode::DuplicateDrop);
    }
    if (dropId != State.NextDropId) {
        return Failure(ResultCode::InvalidDropId);
    }
    if (State.NextDropId == MaximumPersistedCounter) {
        return Failure(ResultCode::DropIdOverflow);
    }
    if (State.UnprocessedDropIds.size() >= MaximumUnprocessedDrops) {
        return Failure(ResultCode::DropLedgerFull);
    }
    Snapshot candidate = State;
    candidate.UnprocessedDropIds.push_back(dropId);
    ++candidate.NextDropId; // Checked signed-storage bound above; monotonic issuance.
    if (candidate.Quest == QuestState::Accepted) {
        candidate.Quest = QuestState::RaiderDefeated;
        candidate.QuestKillDropId = dropId;
    }
    return Commit(std::move(candidate));
}

Result ProgressionCore::GrantLoot(ItemId id, std::int64_t quantity, std::uint64_t dropId) {
    if (quantity <= 0) {
        return Failure(ResultCode::InvalidQuantity);
    }
    if (FindDefinition(id) == nullptr) {
        return Failure(ResultCode::InvalidItem);
    }
    if (id == ItemId::JadeParcel ||
        (dropId != 0 && dropId == State.QuestKillDropId)) {
        return Failure(ResultCode::DropReservedForQuest);
    }
    if (dropId == 0) {
        return Failure(ResultCode::InvalidDropId);
    }
    if (dropId >= State.NextDropId) {
        return Failure(ResultCode::UnknownDrop);
    }
    if (!HasId(State.UnprocessedDropIds, dropId)) {
        return Failure(ResultCode::DropAlreadyProcessed);
    }
    Snapshot candidate = State;
    if (!AddItems(candidate, id, static_cast<std::uint64_t>(quantity))) {
        return Failure(ResultCode::InventoryFull);
    }
    candidate.UnprocessedDropIds.erase(std::lower_bound(
        candidate.UnprocessedDropIds.begin(), candidate.UnprocessedDropIds.end(), dropId));
    return Commit(std::move(candidate));
}

Result ProgressionCore::PickupParcel(std::uint64_t dropId) {
    if (dropId == 0) {
        return Failure(ResultCode::InvalidDropId);
    }
    if (dropId >= State.NextDropId) {
        return Failure(ResultCode::UnknownDrop);
    }
    if (!HasId(State.UnprocessedDropIds, dropId)) {
        return Failure(ResultCode::DropAlreadyProcessed);
    }
    if (State.Quest == QuestState::NotAccepted) {
        return Failure(ResultCode::QuestNotAccepted);
    }
    if (State.Quest == QuestState::Accepted) {
        return Failure(ResultCode::QuestNeedsDefeat);
    }
    if (State.Quest == QuestState::Completed) {
        return Failure(ResultCode::QuestAlreadyCompleted);
    }
    if (State.Quest == QuestState::ParcelRecovered) {
        return Failure(ResultCode::ParcelAlreadyRecovered);
    }
    if (dropId != State.QuestKillDropId) {
        return Failure(ResultCode::UnknownDrop);
    }
    Snapshot candidate = State;
    if (!AddItems(candidate, ItemId::JadeParcel, 1)) {
        return Failure(ResultCode::InventoryFull);
    }
    candidate.UnprocessedDropIds.erase(std::lower_bound(
        candidate.UnprocessedDropIds.begin(), candidate.UnprocessedDropIds.end(), dropId));
    candidate.Quest = QuestState::ParcelRecovered;
    return Commit(std::move(candidate));
}

Result ProgressionCore::TurnInQuest() {
    switch (State.Quest) {
    case QuestState::NotAccepted: return Failure(ResultCode::QuestNotAccepted);
    case QuestState::Accepted: return Failure(ResultCode::QuestNeedsDefeat);
    case QuestState::RaiderDefeated: return Failure(ResultCode::QuestNeedsParcel);
    case QuestState::Completed: return Failure(ResultCode::QuestAlreadyCompleted);
    case QuestState::ParcelRecovered: break;
    }
    std::int64_t coins = 0;
    if (!CheckedAdd(State.Coins, QuestCoinReward, coins)) {
        return Failure(ResultCode::CurrencyOverflow);
    }
    Snapshot candidate = State;
    // Consuming the parcel releases its slot before inserting the item reward.
    const auto parcel = std::find_if(candidate.Inventory.begin(), candidate.Inventory.end(),
        [](const ItemStack& slot) { return slot.Id == ItemId::JadeParcel; });
    if (parcel == candidate.Inventory.end()) {
        return Failure(ResultCode::QuestNeedsParcel);
    }
    *parcel = {};
    if (!AddItems(candidate, ItemId::JadeShard, 1)) {
        return Failure(ResultCode::InventoryFull);
    }
    candidate.Coins = coins;
    candidate.Quest = QuestState::Completed;
    return Commit(std::move(candidate));
}

Result ProgressionCore::Consume(std::uint32_t inventorySlot) {
    if (inventorySlot >= InventorySlotCount) return Failure(ResultCode::InvalidSlot);
    const auto& source = State.Inventory[inventorySlot];
    const auto* definition = FindDefinition(source.Id);
    if (!definition || source.Count == 0) return Failure(ResultCode::InsufficientItems);
    if (definition->Weapon || definition->HealingPercent == 0 || definition->HealingPercent > 100)
        return Failure(ResultCode::NotConsumable);
    Snapshot candidate = State;
    auto& slot = candidate.Inventory[inventorySlot];
    if (--slot.Count == 0) slot = {};
    return Commit(std::move(candidate));
}

Result ProgressionCore::ValidateSnapshot(const Snapshot& candidate) {
    if (candidate.Version != SnapshotVersion) {
        return Invalid(candidate, ValidationCode::UnsupportedVersion);
    }
    if (candidate.Coins < 0) {
        return Invalid(candidate, ValidationCode::NegativeCurrency);
    }
    std::uint32_t parcels = 0;
    for (const auto& slot : candidate.Inventory) {
        if (slot.Id == ItemId::None) {
            if (slot.Count != 0) {
                return Invalid(candidate, ValidationCode::EmptySlotCountMismatch);
            }
            continue;
        }
        const auto* definition = FindDefinition(slot.Id);
        if (definition == nullptr) {
            return Invalid(candidate, ValidationCode::UnknownInventoryItem);
        }
        if (slot.Count == 0) {
            return Invalid(candidate, ValidationCode::EmptySlotCountMismatch);
        }
        if (slot.Count > definition->StackLimit) {
            return Invalid(candidate, ValidationCode::StackLimitExceeded);
        }
        if (slot.Id == ItemId::JadeParcel) {
            parcels += slot.Count;
        }
    }
    if (candidate.EquippedWeapon != ItemId::None) {
        const auto* equipped = FindDefinition(candidate.EquippedWeapon);
        if (equipped == nullptr || !equipped->Weapon) {
            return Invalid(candidate, ValidationCode::InvalidEquippedWeapon);
        }
    }
    if (static_cast<std::uint32_t>(candidate.Quest) >
        static_cast<std::uint32_t>(QuestState::Completed)) {
        return Invalid(candidate, ValidationCode::UnknownQuestState);
    }
    if (candidate.Revision > MaximumPersistedCounter ||
        candidate.NextDropId > MaximumPersistedCounter ||
        candidate.QuestKillDropId > MaximumPersistedCounter) {
        return Invalid(candidate, ValidationCode::CounterOutOfRange);
    }
    if (candidate.NextDropId == 0) {
        return Invalid(candidate, ValidationCode::InvalidDropSequence);
    }
    if (candidate.UnprocessedDropIds.size() > MaximumUnprocessedDrops) {
        return Invalid(candidate, ValidationCode::DropLedgerTooLarge);
    }
    std::uint64_t previous = 0;
    for (const auto id : candidate.UnprocessedDropIds) {
        if (id == 0 || id >= candidate.NextDropId) {
            return Invalid(candidate, ValidationCode::InvalidUnprocessedDropId);
        }
        if (id == previous) {
            return Invalid(candidate, ValidationCode::DuplicateUnprocessedDropId);
        }
        if (id < previous) {
            return Invalid(candidate, ValidationCode::UnsortedUnprocessedDropIds);
        }
        previous = id;
    }
    const bool questDefeated = candidate.Quest == QuestState::RaiderDefeated ||
        candidate.Quest == QuestState::ParcelRecovered || candidate.Quest == QuestState::Completed;
    if (!questDefeated) {
        if (candidate.QuestKillDropId != 0) {
            return Invalid(candidate, ValidationCode::QuestDropMismatch);
        }
    } else if (candidate.QuestKillDropId == 0 ||
               candidate.QuestKillDropId >= candidate.NextDropId) {
        return Invalid(candidate, ValidationCode::QuestDropMismatch);
    }
    // Retain the quest ID after claim/reward: it must never become ordinary loot.
    if (questDefeated && HasId(candidate.UnprocessedDropIds, candidate.QuestKillDropId) !=
        (candidate.Quest == QuestState::RaiderDefeated)) {
        return Invalid(candidate, ValidationCode::QuestLedgerMismatch);
    }
    if (parcels != (candidate.Quest == QuestState::ParcelRecovered ? 1u : 0u)) {
        return Invalid(candidate, ValidationCode::QuestParcelMismatch);
    }
    const std::uint64_t issued = candidate.NextDropId - 1;
    // Strictly increasing IDs below NextDropId guarantee size <= issued.
    const std::uint64_t processed = issued - candidate.UnprocessedDropIds.size();
    const std::uint64_t questStages = (candidate.Quest == QuestState::NotAccepted ? 0u : 1u) +
        (candidate.Quest == QuestState::Completed ? 1u : 0u);
    if (processed > MaximumPersistedCounter - issued ||
        questStages > MaximumPersistedCounter - issued - processed) {
        return Invalid(candidate, ValidationCode::CounterOutOfRange);
    }
    const std::uint64_t minimumRevision = issued + processed + questStages;
    if (candidate.Revision < minimumRevision) {
        return Invalid(candidate, ValidationCode::RevisionBelowRecordedTransactions);
    }
    return {ResultCode::Ok, ValidationCode::None, false, candidate.Revision};
}

Result ProgressionCore::LoadSnapshot(const Snapshot& candidate) {
    const auto valid = ValidateSnapshot(candidate);
    if (!valid.Succeeded()) {
        return {valid.Code, valid.Validation, false, State.Revision};
    }
    const bool changed = !EqualSnapshots(State, candidate);
    // Copy all allocation-bearing fields before replacing any live field.
    Snapshot replacement = candidate;
    State = std::move(replacement);
    return {ResultCode::Ok, ValidationCode::None, changed, State.Revision};
}

} // namespace JadeboundLegacyV2
