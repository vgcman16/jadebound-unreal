#pragma once

#include "../Combat/PlayerLifeRules.h"
#include "../Domain/ProgressionCore.h"

#include <cmath>
#include <cstdint>
#include <type_traits>

// Synchronous domain operation only. The actor validates ownership, world,
// interaction/menu context and persistence readiness before entering this rule.
namespace Jadebound::Transactions::NativeBalm
{
enum class Rejection
{
    None,
    InvalidLife,
    NotAlive,
    FullHealth,
    StaleGeneration,
    InvalidTime,
    InvalidRevision,
    StaleRevision,
    RevisionOverflow,
    InvalidSlot,
    InvalidItem,
    SlotChanged,
    InvalidDefinition,
    CoreRefused
};

struct Outcome
{
    bool Applied = false;
    Rejection Reason = Rejection::None;
    // CoreResult is the actual Consume result only when CoreAttempted is true.
    bool CoreAttempted = false;
    Jadebound::Result CoreResult{};
    std::int32_t HealingApplied = 0;
};

namespace Detail
{
inline Outcome Refuse(Rejection Reason, std::uint64_t Revision) noexcept
{
    Outcome Result;
    Result.Reason = Reason;
    Result.CoreResult.Revision = Revision;
    if (Reason == Rejection::RevisionOverflow)
        Result.CoreResult.Code = ResultCode::RevisionOverflow;
    return Result;
}
}

// Core::Consume may allocate its candidate snapshot and throw before commit.
// Such an exception leaves both caller-owned states unchanged. Do not add
// callbacks, persistence, snapshot copies or allocating output work here.
inline Outcome TryConsume(ProgressionCore& Core, Combat::PlayerLife::State& Life,
                          std::int32_t ExpectedSlot, std::int32_t ExpectedItem,
                          std::int64_t ExpectedRevision,
                          std::uint64_t ExpectedLifeGeneration, double Now)
{
    using namespace Combat::PlayerLife;
    static_assert(std::is_nothrow_copy_assignable<State>::value,
                  "Publishing paired life must not throw after consuming balm");
    static_assert(std::is_trivially_copyable<Outcome>::value,
                  "Returning paired admission must not allocate or throw");
    const auto& Inventory = Core.GetSnapshot();
    const auto Reject = [&Inventory](Rejection Why) noexcept
    { return Detail::Refuse(Why, Inventory.Revision); };

    if (!Valid(Life)) return Reject(Rejection::InvalidLife);
    if (Life.Health == 0) return Reject(Rejection::NotAlive);
    if (Life.Health == MaximumHealth) return Reject(Rejection::FullHealth);
    if (ExpectedLifeGeneration == 0 || ExpectedLifeGeneration != Life.Generation)
        return Reject(Rejection::StaleGeneration);
    if (!std::isfinite(Now) || Now < 0.0 || Now < Life.LastObservation)
        return Reject(Rejection::InvalidTime);
    if (ExpectedRevision < 0) return Reject(Rejection::InvalidRevision);
    if (static_cast<std::uint64_t>(ExpectedRevision) != Inventory.Revision)
        return Reject(Rejection::StaleRevision);
    if (Inventory.Revision >= MaximumPersistedCounter)
        return Reject(Rejection::RevisionOverflow);
    if (ExpectedSlot < 0 || static_cast<std::uint32_t>(ExpectedSlot) >= InventorySlotCount)
        return Reject(Rejection::InvalidSlot);
    if (ExpectedItem != static_cast<std::int32_t>(ItemId::Balm))
        return Reject(Rejection::InvalidItem);
    const auto Slot = static_cast<std::uint32_t>(ExpectedSlot);
    const auto& Owned = Inventory.Inventory[Slot];
    if (Owned.Id != ItemId::Balm || Owned.Count == 0)
        return Reject(Rejection::SlotChanged);
    const auto* Definition = ProgressionCore::FindDefinition(Owned.Id);
    if (!Definition || Definition->Id != ItemId::Balm || Definition->Weapon ||
        Definition->HealingPercent != 25)
        return Reject(Rejection::InvalidDefinition);

    // The catalog supplies the effect; neither damage nor healing comes from
    // request fields. Bounded wide arithmetic precedes the inventory debit.
    const std::int64_t Recovery = static_cast<std::int64_t>(MaximumHealth) *
        static_cast<std::int64_t>(Definition->HealingPercent) / 100;
    const std::int64_t Recovered = static_cast<std::int64_t>(Life.Health) + Recovery;
    State Candidate = Life;
    Candidate.Health = static_cast<std::int32_t>(Recovered > MaximumHealth ? MaximumHealth : Recovered);
    Candidate.LastObservation = Now;
    if (!Valid(Candidate) || Candidate.Health <= Life.Health)
        return Reject(Rejection::InvalidLife);

    Outcome Result;
    Result.CoreAttempted = true;
    Result.HealingApplied = Candidate.Health - Life.Health;
    Result.CoreResult = Core.Consume(Slot);
    if (!Result.CoreResult.Succeeded() || !Result.CoreResult.Changed)
    {
        Result.Reason = Rejection::CoreRefused;
        Result.HealingApplied = 0;
        return Result;
    }
    // Consume already committed. Only no-throw POD assignments follow it.
    Life = Candidate;
    Result.Applied = true;
    return Result;
}
}
