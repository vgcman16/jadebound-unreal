#include "Persistence/JournalIO.h"
#include "GenericPlatform/GenericPlatformFile.h"

namespace Jadebound::Persistence
{
bool WriteAndFlush(IFileHandle& Handle, TConstArrayView<uint8> Bytes)
{
    // Encoded journals are nonempty. A write failure can leave partial bytes;
    // it must never become a successful save, even if later readable.
    if (Bytes.IsEmpty()) return false;
    return Handle.Write(Bytes.GetData(), Bytes.Num()) && Handle.Flush(true);
}
}
