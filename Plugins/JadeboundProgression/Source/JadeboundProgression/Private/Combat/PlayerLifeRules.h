#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

// Pure server-owned player life. Identity, persistence, geometry, ownership and
// strike issuance are the caller's responsibility; no client health/time is read.
namespace Jadebound::Combat::PlayerLife
{
inline constexpr std::int32_t MaximumHealth = 100;
inline constexpr std::int32_t StrikeDamage = 20;
inline constexpr double ReviveDelaySeconds = 10.0;

struct State
{
    std::uint64_t Generation = 1;
    std::int32_t Health = MaximumHealth;
    double DeadAt = 0.0;
    double ReviveReadyAt = 0.0;
    std::uint64_t LastStrike = 0;
    double LastObservation = 0.0;
};

namespace Detail
{
inline bool NonnegativeFinite(double Value) noexcept
{
    return std::isfinite(Value) && Value >= 0.0;
}

inline bool DeathInterval(double Start, double Ready) noexcept
{
    // Refuse overflow or clocks too coarse to represent the ten-second delay.
    // Addition/subtraction round at the timestamp's precision, not at 10.0's.
    if (!NonnegativeFinite(Start) || !NonnegativeFinite(Ready) ||
        Ready != Start + ReviveDelaySeconds || Ready <= Start) return false;
    const double Infinity = std::numeric_limits<double>::infinity();
    const double StartUlp = std::nextafter(Start, Infinity) - Start;
    const double ReadyUlp = std::nextafter(Ready, Infinity) - Ready;
    const double ClockUlp = StartUlp > ReadyUlp ? StartUlp : ReadyUlp;
    if (!std::isfinite(ClockUlp) || ClockUlp > 1e-6) return false;
    const double Tolerance = ClockUlp * 2.0 + ReviveDelaySeconds * std::numeric_limits<double>::epsilon();
    return std::abs((Ready - Start) - ReviveDelaySeconds) <= Tolerance;
}

inline bool Observation(const State& Life, double Now) noexcept
{
    return NonnegativeFinite(Now) && Now >= Life.LastObservation;
}
}

inline bool Valid(const State& Life) noexcept
{
    if (Life.Generation == 0 || Life.Health < 0 || Life.Health > MaximumHealth ||
        !Detail::NonnegativeFinite(Life.DeadAt) || !Detail::NonnegativeFinite(Life.ReviveReadyAt) ||
        !Detail::NonnegativeFinite(Life.LastObservation)) return false;
    if (Life.Health > 0) return Life.DeadAt == 0.0 && Life.ReviveReadyAt == 0.0;
    return Life.LastStrike != 0 && Detail::DeathInterval(Life.DeadAt, Life.ReviveReadyAt) &&
        Life.LastObservation >= Life.DeadAt;
}

inline bool IsAlive(const State& Life) noexcept
{
    return Valid(Life) && Life.Health > 0;
}

inline bool TryDamage(State& Life, std::uint64_t Strike, std::uint64_t ExpectedGeneration,
                      double Now) noexcept
{
    if (!IsAlive(Life) || ExpectedGeneration == 0 || ExpectedGeneration != Life.Generation ||
        Strike == 0 || Strike <= Life.LastStrike || !Detail::Observation(Life, Now)) return false;
    State Next = Life;
    Next.Health = Life.Health > StrikeDamage ? Life.Health - StrikeDamage : 0;
    Next.LastStrike = Strike;
    Next.LastObservation = Now;
    if (Next.Health == 0)
    {
        Next.DeadAt = Now;
        Next.ReviveReadyAt = Now + ReviveDelaySeconds;
    }
    if (!Valid(Next)) return false;
    Life = Next;
    return true;
}

inline bool CanRevive(const State& Life, std::uint64_t ExpectedGeneration, double Now) noexcept
{
    return Valid(Life) && Life.Health == 0 && ExpectedGeneration != 0 &&
        ExpectedGeneration == Life.Generation && Life.Generation != std::numeric_limits<std::uint64_t>::max() &&
        Detail::Observation(Life, Now) && Now >= Life.ReviveReadyAt;
}

// Presentation only: zero also denotes an invalid state/clock. Use CanRevive to
// admit a manual revive; this query neither observes nor changes server state.
inline double RemainingSeconds(const State& Life, double Now) noexcept
{
    if (!Valid(Life) || Life.Health > 0 || !Detail::Observation(Life, Now) || Now >= Life.ReviveReadyAt)
        return 0.0;
    return Life.ReviveReadyAt - Now;
}

inline bool TryRevive(State& Life, std::uint64_t ExpectedGeneration, double Now) noexcept
{
    if (!CanRevive(Life, ExpectedGeneration, Now)) return false;
    State Next = Life;
    ++Next.Generation;
    Next.Health = MaximumHealth;
    Next.DeadAt = 0.0;
    Next.ReviveReadyAt = 0.0;
    Next.LastObservation = Now;
    // LastStrike deliberately persists: an old enemy strike cannot damage the
    // revived player even if it is presented with the new generation.
    Life = Next;
    return true;
}
}
