#pragma once

#include "CoreTypes.h"

namespace Jadebound::Presentation
{
// Pending IDs are positive signed int64. Invalid IDs get a neutral visual yaw.
float LootYaw(int64 DropId);
}
