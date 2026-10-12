#include "Combat/RecipientAnimationRules.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Misc/AutomationTest.h"

// Requires the actual full project content, unlike the no-content bank host.
// Calls the production montage admission predicate against real saved assets.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundRecipientAnimationAssetsTest,
    "Jadebound.RecipientCombat.AnimationAssets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJadeboundRecipientAnimationAssetsTest::RunTest(const FString& Parameters)
{
    const auto* Montage = LoadObject<UAnimMontage>(nullptr,
        TEXT("/Game/Jadebound/Development/RecipientAnimationV001/AM_RecipientAttack_Template_v001.AM_RecipientAttack_Template_v001"));
    const auto* Attack = LoadObject<UAnimSequence>(nullptr,
        TEXT("/Game/Jadebound/Development/RecipientAnimationV001/A_RecipientAttack_Template_v001.A_RecipientAttack_Template_v001"));
    const auto* Idle = LoadObject<UAnimSequence>(nullptr,
        TEXT("/Game/Jadebound/Development/RecipientAnimationV001/A_RecipientIdle_Template_v001.A_RecipientIdle_Template_v001"));
    const auto* LegacyMontage = LoadObject<UAnimMontage>(nullptr,
        TEXT("/Game/Variant_Combat/Anims/AM_ComboAttack.AM_ComboAttack"));
    const auto* LegacyAttack = LoadObject<UAnimSequence>(nullptr,
        TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_01.MM_Attack_01"));
    if (!TestTrue(TEXT("Real saved montage, copies and preserved legacy inputs load"),
        Montage && Attack && Idle && LegacyMontage && LegacyAttack)) return false;

    TestTrue(TEXT("Actual new montage passes production native admission"),
        Jadebound::Combat::IsSafeMeleeMontage(Montage));
    TestFalse(TEXT("Preserved legacy combo cannot unlock native combat"),
        Jadebound::Combat::IsSafeMeleeMontage(LegacyMontage));
    TestFalse(TEXT("New montage has no extracted root motion"), Montage->HasRootMotion());
    TestFalse(TEXT("New attack has no extracted root motion"), Attack->HasRootMotion());
    TestFalse(TEXT("New idle has no extracted root motion"), Idle->HasRootMotion());
    TestTrue(TEXT("Preserved original attack still retains its original root-motion setting"),
        LegacyAttack->HasRootMotion());
    TestEqual(TEXT("New montage callbacks absent"), Montage->Notifies.Num(), 0);
    TestEqual(TEXT("New attack callbacks absent"), Attack->Notifies.Num(), 0);
    TestEqual(TEXT("New idle callbacks absent"), Idle->Notifies.Num(), 0);
    TestTrue(TEXT("Actual montage and copied sequences share a real skeleton"),
        Montage->GetSkeleton() && Montage->GetSkeleton() == Attack->GetSkeleton() &&
        Attack->GetSkeleton() == Idle->GetSkeleton());
    if (!TestEqual(TEXT("Actual montage has exactly one slot"), Montage->SlotAnimTracks.Num(), 1)) return false;
    const auto& Track = Montage->SlotAnimTracks[0];
    TestEqual(TEXT("The authored test montage uses the preserved default slot"), Track.SlotName, FName(TEXT("DefaultSlot")));
    if (!TestEqual(TEXT("Actual montage has exactly one segment"), Track.AnimTrack.AnimSegments.Num(), 1)) return false;
    TestTrue(TEXT("Native reference resolves to the actual saved attack copy"),
        Track.AnimTrack.AnimSegments[0].GetAnimReference().Get() == Attack);
    AddInfo(TEXT("Saved-asset admission only: no animation instance, body/weapon pose, timing, input, network or gameplay acceptance."));
    return true;
}
#endif
