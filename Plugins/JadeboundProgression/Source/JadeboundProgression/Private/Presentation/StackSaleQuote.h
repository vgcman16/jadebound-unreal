#pragma once

#include <cstdint>
#include <limits>

// Read-only presentation arithmetic. The actor/core still validate every sale.
namespace Jadebound::Presentation {
inline bool TryStackSaleTotal(std::int32_t quantity, std::int64_t unitPrice,
    std::int64_t coins, std::int64_t& total) noexcept
{
    total = 0;
    if (quantity <= 0 || unitPrice < 0 || coins < 0) return false;
    constexpr auto Maximum = std::numeric_limits<std::int64_t>::max();
    if (unitPrice > Maximum / quantity) return false;
    const auto credit = unitPrice * quantity;
    if (coins > Maximum - credit) return false;
    total = credit;
    return true;
}
}
