// Standalone C++17 executable, linked to the actual ProgressionCore.cpp.
// Allocation injection must never be linked into the Unreal module.
#include "../../Source/JadeboundProgression/Private/Transactions/NativeBalmRules.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

namespace AllocationFixture
{
long Remaining = -1;
void* Allocate(std::size_t Size)
{
    if (Remaining == 0) throw std::bad_alloc();
    if (Remaining > 0) --Remaining;
    if (void* Memory = std::malloc(Size == 0 ? 1 : Size)) return Memory;
    throw std::bad_alloc();
}
}
void* operator new(std::size_t Size) { return AllocationFixture::Allocate(Size); }
void* operator new[](std::size_t Size) { return AllocationFixture::Allocate(Size); }
void operator delete(void* Memory) noexcept { std::free(Memory); }
void operator delete[](void* Memory) noexcept { std::free(Memory); }
void operator delete(void* Memory, std::size_t) noexcept { std::free(Memory); }
void operator delete[](void* Memory, std::size_t) noexcept { std::free(Memory); }

namespace
{
using namespace Jadebound;
namespace LifeRules = Combat::PlayerLife;
namespace BalmRules = Transactions::NativeBalm;
std::uint32_t Checks = 0;
#define CHECK(Expression) do { ++Checks; if (!(Expression)) { \
    std::printf("FAIL line %d: %s\n", __LINE__, #Expression); return false; } } while (false)

bool SameLife(const LifeRules::State& A, const LifeRules::State& B) noexcept
{
    return A.Generation == B.Generation && A.Health == B.Health && A.LastStrike == B.LastStrike &&
        std::memcmp(&A.DeadAt, &B.DeadAt, sizeof(double)) == 0 &&
        std::memcmp(&A.ReviveReadyAt, &B.ReviveReadyAt, sizeof(double)) == 0 &&
        std::memcmp(&A.LastObservation, &B.LastObservation, sizeof(double)) == 0;
}
bool SameCore(const Snapshot& A, const Snapshot& B) noexcept
{
    if (A.Version != B.Version || A.Revision != B.Revision || A.Coins != B.Coins ||
        A.EquippedWeapon != B.EquippedWeapon || A.Quest != B.Quest || A.NextDropId != B.NextDropId ||
        A.QuestKillDropId != B.QuestKillDropId || A.UnprocessedDropIds != B.UnprocessedDropIds) return false;
    for (std::uint32_t I = 0; I < InventorySlotCount; ++I)
        if (A.Inventory[I].Id != B.Inventory[I].Id || A.Inventory[I].Count != B.Inventory[I].Count) return false;
    return true;
}
LifeRules::State Hurt(std::int32_t Health = 50) noexcept
{
    LifeRules::State Life;
    Life.Generation = 3;
    Life.Health = Health;
    Life.LastStrike = 8;
    Life.LastObservation = 4.25;
    return Life;
}
bool PrepareCore(ProgressionCore& Core, std::uint64_t Revision = 40, std::uint32_t Balms = 3)
{
    Snapshot Image;
    Image.Revision = Revision;
    Image.Coins = 17;
    Image.Inventory[3] = {ItemId::Balm, Balms};
    Image.Inventory[8] = {ItemId::IronOre, 4};
    Image.NextDropId = 3;
    Image.UnprocessedDropIds = {1, 2};
    Image.Quest = QuestState::Accepted;
    CHECK(Core.LoadSnapshot(Image).Succeeded());
    return true;
}
template<class Operation>
bool RefusesUnchanged(ProgressionCore& Core, LifeRules::State& Life,
                      BalmRules::Rejection Reason, Operation Invoke)
{
    const Snapshot Before = Core.GetSnapshot();
    const auto BeforeLife = Life;
    const auto Result = Invoke();
    CHECK(!Result.Applied && Result.Reason == Reason && Result.HealingApplied == 0);
    CHECK(!Result.CoreAttempted && !Result.CoreResult.Changed && Result.CoreResult.Revision == Before.Revision);
    CHECK(SameCore(Core.GetSnapshot(), Before) && SameLife(Life, BeforeLife));
    return true;
}

bool ExactPairAndClamp()
{
    ProgressionCore Core;
    CHECK(PrepareCore(Core));
    auto Life = Hurt();
    const Snapshot Before = Core.GetSnapshot();
    const auto BeforeLife = Life;
    const auto Result = BalmRules::TryConsume(Core, Life, 3, 7, 40, 3, 5.0);
    CHECK(Result.Applied && Result.Reason == BalmRules::Rejection::None && Result.CoreAttempted);
    CHECK(Result.CoreResult.Succeeded() && Result.CoreResult.Changed && Result.CoreResult.Revision == 41);
    CHECK(Result.HealingApplied == 25 && Life.Health == 75 && Life.LastObservation == 5.0);
    CHECK(Life.Generation == BeforeLife.Generation && Life.LastStrike == BeforeLife.LastStrike &&
          Life.DeadAt == BeforeLife.DeadAt && Life.ReviveReadyAt == BeforeLife.ReviveReadyAt);
    Snapshot Expected = Before;
    Expected.Revision = 41;
    --Expected.Inventory[3].Count;
    CHECK(SameCore(Core.GetSnapshot(), Expected));
    CHECK(ProgressionCore::ValidateSnapshot(Core.GetSnapshot()).Succeeded());
    CHECK(!LifeRules::TryDamage(Life, BeforeLife.LastStrike, Life.Generation, 6.0));
    CHECK(LifeRules::TryDamage(Life, BeforeLife.LastStrike + 1, Life.Generation, 6.0) && Life.Health == 55);

    ProgressionCore ClampCore;
    CHECK(PrepareCore(ClampCore, 40, 1));
    auto ClampLife = Hurt(80);
    const auto Clamped = BalmRules::TryConsume(ClampCore, ClampLife, 3, 7, 40, 3, 4.25);
    CHECK(Clamped.Applied && Clamped.HealingApplied == 20 && ClampLife.Health == 100);
    CHECK(ClampCore.GetSnapshot().Inventory[3].Id == ItemId::None && ClampCore.GetSnapshot().Inventory[3].Count == 0);
    auto BarelyAlive = Hurt(1);
    ProgressionCore LowCore;
    CHECK(PrepareCore(LowCore));
    CHECK(BalmRules::TryConsume(LowCore, BarelyAlive, 3, 7, 40, 3, 5).Applied && BarelyAlive.Health == 26);
    return true;
}

bool LifeAndClockRefusals()
{
    ProgressionCore Core;
    CHECK(PrepareCore(Core));
    auto Life = Hurt(100);
    CHECK(RefusesUnchanged(Core, Life, BalmRules::Rejection::FullHealth,
        [&]() { return BalmRules::TryConsume(Core, Life, 3, 7, 40, 3, 5); }));
    Life = Hurt();
    for (std::uint64_t Strike = 9; Strike <= 11; ++Strike)
        CHECK(LifeRules::TryDamage(Life, Strike, 3, 5));
    CHECK(LifeRules::Valid(Life) && Life.Health == 0);
    CHECK(RefusesUnchanged(Core, Life, BalmRules::Rejection::NotAlive,
        [&]() { return BalmRules::TryConsume(Core, Life, 3, 7, 40, 3, 100); }));

    const std::array<double, 6> InvalidTimes = {{
        -1.0, std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN(), 4.0, std::nextafter(4.25, 0.0)}};
    Life = Hurt();
    for (const double Now : InvalidTimes)
        CHECK(RefusesUnchanged(Core, Life, BalmRules::Rejection::InvalidTime,
            [&]() { return BalmRules::TryConsume(Core, Life, 3, 7, 40, 3, Now); }));
    CHECK(RefusesUnchanged(Core, Life, BalmRules::Rejection::StaleGeneration,
        [&]() { return BalmRules::TryConsume(Core, Life, 3, 7, 40, 0, 5); }));
    CHECK(RefusesUnchanged(Core, Life, BalmRules::Rejection::StaleGeneration,
        [&]() { return BalmRules::TryConsume(Core, Life, 3, 7, 40, 2, 5); }));

    std::array<LifeRules::State, 10> InvalidLives;
    InvalidLives.fill(Hurt());
    InvalidLives[0].Generation = 0;
    InvalidLives[1].Health = -1;
    InvalidLives[2].Health = 101;
    InvalidLives[3].Health = std::numeric_limits<std::int32_t>::max();
    InvalidLives[4].DeadAt = 1;
    InvalidLives[5].ReviveReadyAt = 10;
    InvalidLives[6].LastObservation = -1;
    InvalidLives[7].LastObservation = std::numeric_limits<double>::quiet_NaN();
    InvalidLives[8].LastObservation = std::numeric_limits<double>::infinity();
    InvalidLives[9].Health = 0; // No valid death timer/strike history.
    for (auto& InvalidLife : InvalidLives)
        CHECK(RefusesUnchanged(Core, InvalidLife, BalmRules::Rejection::InvalidLife,
            [&]() { return BalmRules::TryConsume(Core, InvalidLife, 3, 7, 40, 3, 5); }));
    return true;
}

bool InventoryIntentRefusals()
{
    ProgressionCore Core;
    CHECK(PrepareCore(Core));
    auto Life = Hurt();
    for (const std::int64_t Revision : {std::int64_t{-1}, std::numeric_limits<std::int64_t>::min()})
        CHECK(RefusesUnchanged(Core, Life, BalmRules::Rejection::InvalidRevision,
            [&]() { return BalmRules::TryConsume(Core, Life, 3, 7, Revision, 3, 5); }));
    for (const std::int64_t Revision : {std::int64_t{39}, std::int64_t{41}, std::numeric_limits<std::int64_t>::max()})
        CHECK(RefusesUnchanged(Core, Life, BalmRules::Rejection::StaleRevision,
            [&]() { return BalmRules::TryConsume(Core, Life, 3, 7, Revision, 3, 5); }));
    for (const std::int32_t Slot : {-1, 12, std::numeric_limits<std::int32_t>::max()})
        CHECK(RefusesUnchanged(Core, Life, BalmRules::Rejection::InvalidSlot,
            [&]() { return BalmRules::TryConsume(Core, Life, Slot, 7, 40, 3, 5); }));
    for (const std::int32_t Item : {-1, 0, 1, 3, 6, 8, std::numeric_limits<std::int32_t>::max()})
        CHECK(RefusesUnchanged(Core, Life, BalmRules::Rejection::InvalidItem,
            [&]() { return BalmRules::TryConsume(Core, Life, 3, Item, 40, 3, 5); }));
    CHECK(RefusesUnchanged(Core, Life, BalmRules::Rejection::SlotChanged,
        [&]() { return BalmRules::TryConsume(Core, Life, 8, 7, 40, 3, 5); }));
    CHECK(RefusesUnchanged(Core, Life, BalmRules::Rejection::SlotChanged,
        [&]() { return BalmRules::TryConsume(Core, Life, 0, 7, 40, 3, 5); }));
    return true;
}

bool RevisionReplayAndBoundaries()
{
    ProgressionCore Core;
    CHECK(PrepareCore(Core));
    auto Life = Hurt();
    CHECK(BalmRules::TryConsume(Core, Life, 3, 7, 40, 3, 5).Applied);
    CHECK(RefusesUnchanged(Core, Life, BalmRules::Rejection::StaleRevision,
        [&]() { return BalmRules::TryConsume(Core, Life, 3, 7, 40, 3, 5); }));
    CHECK(BalmRules::TryConsume(Core, Life, 3, 7, 41, 3, 6).Applied && Life.Health == 100);
    CHECK(RefusesUnchanged(Core, Life, BalmRules::Rejection::FullHealth,
        [&]() { return BalmRules::TryConsume(Core, Life, 3, 7, 42, 3, 7); }));

    ProgressionCore EdgeCore;
    CHECK(PrepareCore(EdgeCore, MaximumPersistedCounter - 1));
    auto EdgeLife = Hurt();
    const auto Last = BalmRules::TryConsume(EdgeCore, EdgeLife, 3, 7,
        static_cast<std::int64_t>(MaximumPersistedCounter - 1), 3, 5);
    CHECK(Last.Applied && Last.CoreResult.Revision == MaximumPersistedCounter && EdgeLife.Health == 75);
    CHECK(RefusesUnchanged(EdgeCore, EdgeLife, BalmRules::Rejection::RevisionOverflow,
        [&]() { return BalmRules::TryConsume(EdgeCore, EdgeLife, 3, 7,
            static_cast<std::int64_t>(MaximumPersistedCounter), 3, 6); }));

    // Healing increments neither generation nor strike ID; their terminal
    // values remain unchanged rather than wrapping or resetting replay guards.
    ProgressionCore CounterCore;
    CHECK(PrepareCore(CounterCore));
    auto CounterLife = Hurt();
    CounterLife.Generation = std::numeric_limits<std::uint64_t>::max();
    CounterLife.LastStrike = std::numeric_limits<std::uint64_t>::max();
    CHECK(BalmRules::TryConsume(CounterCore, CounterLife, 3, 7, 40, CounterLife.Generation, 5).Applied);
    CHECK(CounterLife.Generation == std::numeric_limits<std::uint64_t>::max() &&
          CounterLife.LastStrike == std::numeric_limits<std::uint64_t>::max());
    return true;
}

bool AllocationAtomicity()
{
    ProgressionCore Core;
    CHECK(PrepareCore(Core));
    auto Life = Hurt();
    const Snapshot Before = Core.GetSnapshot();
    const auto BeforeLife = Life;
    bool Threw = false;
    AllocationFixture::Remaining = 0;
    try { (void)BalmRules::TryConsume(Core, Life, 3, 7, 40, 3, 5); }
    catch (const std::bad_alloc&) { Threw = true; }
    AllocationFixture::Remaining = -1;
    CHECK(Threw && SameCore(Core.GetSnapshot(), Before) && SameLife(Life, BeforeLife));
    // Permit exactly the candidate vector allocation. Any allocating return
    // work after debit would throw and fail this second actual-Core invocation.
    AllocationFixture::Remaining = 1;
    BalmRules::Outcome Result;
    Threw = false;
    try { Result = BalmRules::TryConsume(Core, Life, 3, 7, 40, 3, 5); }
    catch (const std::bad_alloc&) { Threw = true; }
    const long Remaining = AllocationFixture::Remaining;
    AllocationFixture::Remaining = -1;
    CHECK(!Threw && Remaining == 0 && Result.Applied && Life.Health == 75);
    CHECK(Core.GetSnapshot().Revision == 41 && Core.GetSnapshot().Inventory[3].Count == 2);
    return true;
}

bool CharacterIsolation()
{
    ProgressionCore A, B;
    CHECK(PrepareCore(A) && PrepareCore(B, 45, 4));
    auto LifeA = Hurt();
    auto LifeB = Hurt(40);
    const Snapshot BeforeB = B.GetSnapshot();
    const auto BeforeLifeB = LifeB;
    CHECK(BalmRules::TryConsume(A, LifeA, 3, 7, 40, 3, 5).Applied);
    CHECK(SameCore(B.GetSnapshot(), BeforeB) && SameLife(LifeB, BeforeLifeB));
    CHECK(RefusesUnchanged(B, LifeB, BalmRules::Rejection::StaleRevision,
        [&]() { return BalmRules::TryConsume(B, LifeB, 3, 7, 40, 3, 5); }));
    const Snapshot AfterA = A.GetSnapshot();
    const auto AfterLifeA = LifeA;
    CHECK(BalmRules::TryConsume(B, LifeB, 3, 7, 45, 3, 6).Applied && LifeB.Health == 65);
    CHECK(SameCore(A.GetSnapshot(), AfterA) && SameLife(LifeA, AfterLifeA));
    return true;
}
}

int main()
{
    if (!ExactPairAndClamp() || !LifeAndClockRefusals() || !InventoryIntentRefusals() ||
        !RevisionReplayAndBoundaries() || !AllocationAtomicity() || !CharacterIsolation()) return 1;
    std::printf("PASS: 6 native-balm groups, checks=%u; actual Core consume, healing, clamp, refusals, replay, allocation atomicity and isolation\n", Checks);
    return 0;
}
