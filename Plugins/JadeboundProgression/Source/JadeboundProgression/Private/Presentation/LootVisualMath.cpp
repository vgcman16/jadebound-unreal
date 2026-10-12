#include "Presentation/LootVisualMath.h"

namespace Jadebound::Presentation
{
float LootYaw(int64 DropId)
{
    if (DropId <= 0) return 0.f;
    // Reduce first: the multiply is at most359*47=16873, including MAX_int64.
    // For every positive ID this equals the old mathematical (ID*47)%360.
    return static_cast<float>(((DropId % 360) * 47) % 360);
}
}
