// Standalone C++17 test; outside Source and never linked into the Unreal module.
#include "../../Source/JadeboundProgression/Private/Combat/PlayerLifeRules.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

namespace
{
using namespace Jadebound::Combat::PlayerLife;
#define CHECK(Expression) do { if (!(Expression)) { \
    std::printf("FAIL line %d: %s\n", __LINE__, #Expression); return false; } } while (false)

bool Same(const State& A, const State& B) noexcept
{
    // Compare each double's bits so unchanged NaNs and signed zero remain testable.
    // Ignore implementation-defined padding between the plain state fields.
    return A.Generation == B.Generation && A.Health == B.Health && A.LastStrike == B.LastStrike &&
        std::memcmp(&A.DeadAt, &B.DeadAt, sizeof(double)) == 0 &&
        std::memcmp(&A.ReviveReadyAt, &B.ReviveReadyAt, sizeof(double)) == 0 &&
        std::memcmp(&A.LastObservation, &B.LastObservation, sizeof(double)) == 0;
}

State Dead(std::uint64_t Generation = 1)
{
    State Life;
    Life.Generation = Generation;
    for (std::uint64_t Strike = 1; Strike <= 5; ++Strike)
        TryDamage(Life, Strike, Generation, static_cast<double>(Strike));
    return Life;
}

bool DefaultAndDamage()
{
    State Life;
    CHECK(Valid(Life) && IsAlive(Life));
    CHECK(Life.Generation == 1 && Life.Health == 100 && Life.LastStrike == 0 && Life.LastObservation == 0);
    CHECK(!CanRevive(Life, 1, 100) && RemainingSeconds(Life, 100) == 0);
    const std::array<std::int32_t, 5> Health = {80, 60, 40, 20, 0};
    for (std::size_t I = 0; I < Health.size(); ++I)
    {
        const double Now = 1.25 + static_cast<double>(I) * .125;
        CHECK(TryDamage(Life, 100 + I, 1, Now));
        CHECK(Valid(Life) && Life.Health == Health[I] && Life.Generation == 1);
        CHECK(Life.LastStrike == 100 + I && Life.LastObservation == Now);
        CHECK(IsAlive(Life) == (I < 4));
        if (I < 4) CHECK(Life.DeadAt == 0 && Life.ReviveReadyAt == 0);
    }
    CHECK(Life.DeadAt == 1.75 && Life.ReviveReadyAt == 11.75);
    const State Before = Life;
    CHECK(!TryDamage(Life, 105, 1, 2) && Same(Life, Before));
    CHECK(RemainingSeconds(Life, 1.75) == 10 && RemainingSeconds(Life, 6.75) == 5);
    CHECK(!CanRevive(Life, 1, 1.75));
    // Saturation is safe for any admitted positive health, including below damage.
    State Low;
    Low.Health = 1;
    CHECK(TryDamage(Low, 1, 1, 0));
    CHECK(Low.Health == 0 && Low.DeadAt == 0 && Low.ReviveReadyAt == 10 && Valid(Low));
    return true;
}

bool ReviveBoundaryAndReplay()
{
    State Life = Dead();
    CHECK(Valid(Life) && !IsAlive(Life) && Life.DeadAt == 5 && Life.ReviveReadyAt == 15);
    const State Before = Life;
    const double Early = std::nextafter(15.0, 0.0);
    CHECK(RemainingSeconds(Life, Early) > 0 && !CanRevive(Life, 1, Early));
    CHECK(!TryRevive(Life, 1, Early) && Same(Life, Before));
    CHECK(!TryRevive(Life, 0, 15) && Same(Life, Before));
    CHECK(!TryRevive(Life, 2, 15) && Same(Life, Before));
    CHECK(CanRevive(Life, 1, 15) && CanRevive(Life, 1, std::nextafter(15.0, 16.0)));
    CHECK(RemainingSeconds(Life, 15) == 0 && Same(Life, Before));
    CHECK(TryRevive(Life, 1, 15));
    CHECK(Valid(Life) && IsAlive(Life) && Life.Generation == 2 && Life.Health == 100);
    CHECK(Life.DeadAt == 0 && Life.ReviveReadyAt == 0 && Life.LastObservation == 15 && Life.LastStrike == 5);
    const State Revived = Life;
    CHECK(!TryRevive(Life, 1, 16) && Same(Life, Revived));
    CHECK(!TryRevive(Life, 2, 16) && Same(Life, Revived));
    CHECK(!TryDamage(Life, 5, 2, 15) && Same(Life, Revived));
    CHECK(!TryDamage(Life, 100, 1, 15) && Same(Life, Revived));
    CHECK(!TryDamage(Life, 6, 2, std::nextafter(15.0, 0.0)) && Same(Life, Revived));
    CHECK(TryDamage(Life, 6, 2, 15) && Life.Health == 80 && Life.LastStrike == 6);
    // A new life needs its own full timer; the previous generation is stale.
    for (std::uint64_t Strike = 7; Strike <= 10; ++Strike)
        CHECK(TryDamage(Life, Strike, 2, static_cast<double>(Strike) + 10));
    CHECK(Life.Health == 0 && Life.DeadAt == 20 && Life.ReviveReadyAt == 30);
    const State SecondDeath = Life;
    CHECK(!TryRevive(Life, 1, 30) && Same(Life, SecondDeath));
    CHECK(TryRevive(Life, 2, 30) && Life.Generation == 3 && Life.LastStrike == 10);
    return true;
}

bool StaleStrikesAndClockReversal()
{
    State Life;
    CHECK(TryDamage(Life, 20, 1, 4));
    const State Before = Life;
    for (const std::uint64_t Strike : {std::uint64_t(0), std::uint64_t(19), std::uint64_t(20)})
        CHECK(!TryDamage(Life, Strike, 1, 5) && Same(Life, Before));
    CHECK(!TryDamage(Life, 21, 0, 5) && Same(Life, Before));
    CHECK(!TryDamage(Life, 21, 2, 5) && Same(Life, Before));
    CHECK(!TryDamage(Life, 21, 1, 3) && Same(Life, Before));
    CHECK(!TryDamage(Life, 21, 1, -1) && Same(Life, Before));
    CHECK(TryDamage(Life, 21, 1, 4) && Life.Health == 60); // Same game-time instant is valid.
    State Ghost = Dead();
    const State GhostBefore = Ghost;
    CHECK(!CanRevive(Ghost, 1, 4) && RemainingSeconds(Ghost, 4) == 0);
    CHECK(!TryRevive(Ghost, 1, 4) && Same(Ghost, GhostBefore));
    CHECK(!TryDamage(Ghost, 6, 1, 16) && Same(Ghost, GhostBefore));
    // Even queries far beyond the deadline do not automatically revive or change clocks.
    CHECK(CanRevive(Ghost, 1, 1000) && RemainingSeconds(Ghost, 1000) == 0 && Same(Ghost, GhostBefore));
    return true;
}

bool MalformedStatesAndTimes()
{
    const double NaN = std::numeric_limits<double>::quiet_NaN();
    const double Infinity = std::numeric_limits<double>::infinity();
    for (const double Now : {NaN, Infinity, -Infinity, -1.0})
    {
        State Life;
        const State Before = Life;
        CHECK(!TryDamage(Life, 1, 1, Now) && Same(Life, Before));
        CHECK(!CanRevive(Life, 1, Now) && RemainingSeconds(Life, Now) == 0);
        CHECK(!TryRevive(Life, 1, Now) && Same(Life, Before));
        State Ghost = Dead();
        const State GhostBefore = Ghost;
        CHECK(!CanRevive(Ghost, 1, Now) && RemainingSeconds(Ghost, Now) == 0);
        CHECK(!TryRevive(Ghost, 1, Now) && Same(Ghost, GhostBefore));
    }
    const State Default;
    std::array<State, 16> Bad;
    Bad.fill(Default);
    Bad[0].Generation = 0;
    Bad[1].Health = -1;
    Bad[2].Health = 101;
    Bad[3].Health = std::numeric_limits<std::int32_t>::min();
    Bad[4].Health = std::numeric_limits<std::int32_t>::max();
    Bad[5].DeadAt = 1;
    Bad[6].ReviveReadyAt = 10;
    Bad[7].LastObservation = -1;
    Bad[8].LastObservation = NaN;
    Bad[9].DeadAt = NaN;
    Bad[10].ReviveReadyAt = Infinity;
    Bad[11].Health = 0; // Dead life lacks its strike and timer.
    for (std::size_t I = 12; I < Bad.size(); ++I) Bad[I] = Dead();
    Bad[12].LastStrike = 0;
    Bad[13].ReviveReadyAt = std::nextafter(Bad[13].ReviveReadyAt, Infinity);
    Bad[14].LastObservation = 4;
    Bad[15].DeadAt = -1;
    for (auto& Life : Bad)
    {
        const State Before = Life;
        CHECK(!Valid(Life) && !IsAlive(Life));
        CHECK(!TryDamage(Life, 100, 1, 100) && Same(Life, Before));
        CHECK(!CanRevive(Life, 1, 100) && RemainingSeconds(Life, 100) == 0);
        CHECK(!TryRevive(Life, 1, 100) && Same(Life, Before));
    }
    return true;
}

bool OverflowAndRepresentability()
{
    const auto Max = std::numeric_limits<std::uint64_t>::max();
    State Life = Dead(Max);
    CHECK(Valid(Life) && Life.Generation == Max && Life.Health == 0);
    const State Before = Life;
    CHECK(!CanRevive(Life, Max, 15) && RemainingSeconds(Life, 15) == 0);
    CHECK(!TryRevive(Life, Max, 15) && Same(Life, Before));
    State Near = Dead(Max - 1);
    CHECK(TryRevive(Near, Max - 1, 15) && Near.Generation == Max && Near.Health == 100);
    State Last;
    CHECK(TryDamage(Last, Max, 1, 0) && Last.LastStrike == Max);
    const State LastBefore = Last;
    CHECK(!TryDamage(Last, 0, 1, 1) && Same(Last, LastBefore));
    CHECK(!TryDamage(Last, Max, 1, 1) && Same(Last, LastBefore));
    for (const double Now : {std::numeric_limits<double>::max(), std::ldexp(1.0, 56)})
    {
        State Low;
        Low.Health = 20;
        const State LowBefore = Low;
        CHECK(!TryDamage(Low, 1, 1, Now) && Same(Low, LowBefore));
    }
    // Even an exact ten-second subtraction is refused when the clock itself
    // has more than one microsecond of resolution uncertainty.
    State Coarse;
    Coarse.Health = 20;
    const State CoarseBefore = Coarse;
    CHECK(!TryDamage(Coarse, 1, 1, std::ldexp(1.0, 50)) && Same(Coarse, CoarseBefore));
    // Non-integer ordinary timestamps retain the fixed delay despite binary rounding.
    State Fraction;
    Fraction.Health = 20;
    CHECK(TryDamage(Fraction, 1, 1, 6.843934985) && Valid(Fraction));
    CHECK(!CanRevive(Fraction, 1, std::nextafter(Fraction.ReviveReadyAt, 0.0)));
    CHECK(TryRevive(Fraction, 1, Fraction.ReviveReadyAt));
    return true;
}

bool OrdinaryLongSessionClocks()
{
    // Minutes, hours and multiple days, including an exponent transition where
    // the stored ten-second addition has a different ULP from the death time.
    for (const double DeathAt : {100.1, 1000.2, 3599.937, 123456.789, 604800.123456, 1023.99999})
    {
        State Life;
        for (std::uint64_t Strike = 1; Strike <= 4; ++Strike)
            CHECK(TryDamage(Life, Strike, 1, DeathAt - static_cast<double>(5 - Strike)));
        CHECK(TryDamage(Life, 5, 1, DeathAt));
        CHECK(Valid(Life) && Life.Health == 0 && Life.DeadAt == DeathAt);
        CHECK(Life.ReviveReadyAt == DeathAt + 10.0 && Life.LastObservation == DeathAt);
        const State Ghost = Life;
        const double Early = std::nextafter(Life.ReviveReadyAt, 0.0);
        CHECK(RemainingSeconds(Life, Early) > 0 && !CanRevive(Life, 1, Early));
        CHECK(!TryRevive(Life, 1, Early) && Same(Life, Ghost));
        CHECK(CanRevive(Life, 1, Life.ReviveReadyAt));
        CHECK(TryRevive(Life, 1, Life.ReviveReadyAt));
        CHECK(Valid(Life) && IsAlive(Life) && Life.Generation == 2 && Life.Health == 100 && Life.LastStrike == 5);
    }
    return true;
}

bool Isolation()
{
    State A;
    State B;
    const State OriginalB = B;
    for (std::uint64_t Strike = 1; Strike <= 5; ++Strike)
        CHECK(TryDamage(A, Strike, 1, static_cast<double>(Strike)) && Same(B, OriginalB));
    const State GhostA = A;
    CHECK(TryDamage(B, 5, 1, 5) && B.Health == 80 && Same(A, GhostA));
    const State DamagedB = B;
    CHECK(TryRevive(A, 1, 15) && A.Generation == 2 && Same(B, DamagedB));
    CHECK(!TryDamage(A, 6, 1, 16) && Same(B, DamagedB));
    CHECK(TryDamage(B, 6, 1, 16) && B.Generation == 1 && B.Health == 60);
    return true;
}

static_assert(noexcept(Valid(std::declval<const State&>())));
static_assert(noexcept(IsAlive(std::declval<const State&>())));
static_assert(noexcept(TryDamage(std::declval<State&>(), 1, 1, 0)));
static_assert(noexcept(CanRevive(std::declval<const State&>(), 1, 0)));
static_assert(noexcept(RemainingSeconds(std::declval<const State&>(), 0)));
static_assert(noexcept(TryRevive(std::declval<State&>(), 1, 0)));
}

int main()
{
    if (!DefaultAndDamage() || !ReviveBoundaryAndReplay() || !StaleStrikesAndClockReversal() ||
        !MalformedStatesAndTimes() || !OverflowAndRepresentability() || !OrdinaryLongSessionClocks() || !Isolation()) return 1;
    std::puts("PASS: 7 player-life groups; damage, ten-second manual revive, replay, long-session clocks, invalid-state and isolation boundaries");
    return 0;
}
