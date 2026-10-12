#pragma once

#include "CoreTypes.h"
#include "Containers/ArrayView.h"

class IFileHandle;

// Server-internal journal IO. The caller owns/closes the handle and performs
// journal decoding/readback afterward. This helper does not open paths.
namespace Jadebound::Persistence
{
bool WriteAndFlush(IFileHandle& Handle, TConstArrayView<uint8> Bytes);
}
