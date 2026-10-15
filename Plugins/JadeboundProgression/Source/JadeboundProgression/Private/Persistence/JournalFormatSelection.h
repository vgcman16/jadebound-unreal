#pragma once

#include <cstdint>

namespace Jadebound::Persistence
{
enum class JournalFormat : std::uint8_t { None = 0, V1 = 1, V2 = 2, V3 = 3 };
struct JournalPresence
{
    bool V1A = false, V1B = false, V2A = false, V2B = false, V3A = false, V3B = false;
};

// Existence only, NEVER validity. A partial new-format file must not permit
// replay/migration of older history. Caller obtains presence under its one
// retained writer lease and still validates/ranks ORIGINAL bytes in this pair.
// This pure function does not read any file, acquire a lease or prevent an old
// executable from opening the retained older namespace after lease release.
inline JournalFormat SelectJournalFormat(const JournalPresence& Presence) noexcept
{
    if (Presence.V3A || Presence.V3B) return JournalFormat::V3;
    if (Presence.V2A || Presence.V2B) return JournalFormat::V2;
    if (Presence.V1A || Presence.V1B) return JournalFormat::V1;
    return JournalFormat::None;
}
}
