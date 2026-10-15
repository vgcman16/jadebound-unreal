#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "JadeboundOriginalHeroAnimInstance.generated.h"

class UAnimSequence;
struct FJadeboundOriginalHeroAnimProxy;

UCLASS(Transient)
class JADEBOUNDPROGRESSION_API UJadeboundOriginalHeroAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    UJadeboundOriginalHeroAnimInstance();
protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
private:
    friend struct FJadeboundOriginalHeroAnimProxy;
    // Native proxy members alone are not GC roots. Keep every played sequence here.
    UPROPERTY(Transient) TArray<TObjectPtr<UAnimSequence>> Clips;
};
