#pragma once

#include <cstdint>
#include <limits>

// One server-private sequence for the running authoritative world, shared by
// enemy instances. Its owner retains it across enemy replacement and player
// revive; never reset it from an individual player's accepted strike history.
// Access belongs to the server game thread; no client supplies a seed or reset.
// Call only at a validated impact, immediately before damage admission, rather
// than at windup. An issued ID is spent even if the subsequent admission fails.
// This helper does not validate recipient identity, geometry or server ownership.
namespace Jadebound::Combat::NativeStrikeSequence
{
inline std::uint64_t TryIssue(std::uint64_t& LastIssued) noexcept
{
    if (LastIssued == std::numeric_limits<std::uint64_t>::max()) return 0;
    return ++LastIssued;
}
}
