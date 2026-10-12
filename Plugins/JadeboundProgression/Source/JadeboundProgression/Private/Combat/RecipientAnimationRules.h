#pragma once

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"

namespace Jadebound::Combat
{
    // Strict first-slice contract: a separately reviewed real sequence copy,
    // no legacy owner-interface notify callbacks and no root-motion lunge.
    // The protected AM_ComboAttack does not satisfy this contract.
    inline bool IsSafeMeleeMontage(const UAnimMontage* Montage)
    {
        if (!Montage || Montage->Notifies.Num() != 0 || Montage->HasRootMotion() ||
            !Montage->GetSkeleton() || Montage->SlotAnimTracks.Num() != 1 ||
            !FMath::IsFinite(Montage->RateScale) || Montage->RateScale != 1.f ||
            !FMath::IsFinite(Montage->GetPlayLength()) || Montage->GetPlayLength() < .55f ||
            Montage->GetPlayLength() > 2.f) return false;
        const auto& Segments = Montage->SlotAnimTracks[0].AnimTrack.AnimSegments;
        if (Segments.Num() != 1) return false;
        const FAnimSegment& Segment = Segments[0];
        const UAnimSequence* Sequence = Cast<UAnimSequence>(Segment.GetAnimReference().Get());
        return Sequence && Sequence->Notifies.Num() == 0 && !Sequence->HasRootMotion() &&
            Sequence->GetSkeleton() == Montage->GetSkeleton() && Sequence->RateScale == 1.f &&
            Segment.AnimPlayRate == 1.f && Segment.LoopingCount == 1 && Segment.StartPos == 0.f;
    }
}
