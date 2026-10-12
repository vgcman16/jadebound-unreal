#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "JadeboundMeleePresentationComponent.generated.h"

class UAnimMontage;
class AJadeboundProgressionActor;

USTRUCT(BlueprintType)
struct FJadeboundMeleePresentation
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Combat") int64 SwingId = 0;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Combat") double ServerStartTime = 0;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Combat") double ServerEndTime = 0;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Combat") bool bPlaying = false;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Combat") TObjectPtr<UAnimMontage> Montage;
};

// Public pawn presentation. The owner-only progression actor cannot multicast
// to peers. This component never applies damage or invokes the hero combo graph.
UCLASS(ClassGroup=(Jadebound))
class JADEBOUNDPROGRESSION_API UJadeboundMeleePresentationComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UJadeboundMeleePresentationComponent();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UFUNCTION(BlueprintPure, Category="Jadebound|Combat") FJadeboundMeleePresentation GetPresentation() const { return Presentation; }
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    friend class AJadeboundProgressionActor;
    UPROPERTY(ReplicatedUsing=OnRepPresentation) FJadeboundMeleePresentation Presentation;
    UPROPERTY(Transient) TObjectPtr<UAnimMontage> PlayingMontage;
    int64 AppliedSwingId = 0;
    void PublishFromAuthority(int64 SwingId, UAnimMontage* Montage, double Start, double End, bool bPlay);
    UFUNCTION() void OnRepPresentation();
    void ApplyPresentation();
    bool HasActivePresentation(int64 SwingId) const;
    void StopPresentation();
    double ServerTime() const;
};
