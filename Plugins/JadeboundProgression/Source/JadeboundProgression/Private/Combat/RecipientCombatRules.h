#pragma once

#include "CoreMinimal.h"

// Server-owned values only. These rules neither accept client hit data nor
// mutate character progression. Native automation exercises the boundaries.
namespace Jadebound::Combat
{
    constexpr double WindupSeconds = .16;
    constexpr double HitWindowSeconds = .20;
    constexpr double CooldownSeconds = .85;
    constexpr double PresentationSeconds = .55;
    constexpr double CloseGuardSeconds = .20;
    constexpr double RespawnSeconds = 3.0;

    inline bool IsHitWindow(double Now, double HitAt)
    {
        return FMath::IsFinite(Now) && FMath::IsFinite(HitAt) &&
            Now >= HitAt && Now <= HitAt + HitWindowSeconds;
    }
    inline bool CanReach(const FVector& Origin, const FVector& Forward,
        const FVector& Target, int32 Weapon)
    {
        if (Origin.ContainsNaN() || Forward.ContainsNaN() || Target.ContainsNaN() ||
            (Weapon != 1 && Weapon != 2)) return false;
        const FVector Delta = Target - Origin;
        const double Reach = Weapon == 2 ? 210.0 : 180.0;
        if (FMath::Abs(Delta.Z) > 100.0 || Delta.SizeSquared2D() > Reach * Reach) return false;
        const FVector Facing = Forward.GetSafeNormal2D();
        if (Facing.IsNearlyZero()) return false;
        return Delta.SizeSquared2D() < 1.0 ||
            FVector::DotProduct(Facing, Delta.GetSafeNormal2D()) >= .25;
    }
    inline bool IsSavedDropLocation(const FVector& Location)
    {
        if (Location.ContainsNaN()) return false;
        const FVector Local = FRotator(0,135,0).UnrotateVector(Location - FVector(150,350,0));
        return FMath::Abs(Local.X) <= 700.0 && FMath::Abs(Local.Y) <= 600.0 &&
            Location.Z >= 0.0 && Location.Z <= 300.0;
    }
    inline bool IsNewHit(int64 SwingId, int64 ExpectedLife, int64 CurrentLife,
        int64 LastSwingId)
    {
        return SwingId > 0 && ExpectedLife > 0 && ExpectedLife == CurrentLife &&
            SwingId > LastSwingId;
    }
}
