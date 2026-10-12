#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "JadeboundNativeRaider.generated.h"

class AJadeboundProgressionActor;
class AJadeboundDevelopmentGameMode;
class UAnimationAsset;
class USkeletalMesh;
class UTextRenderComponent;

USTRUCT(BlueprintType)
struct FJadeboundRaiderLife
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Combat") int64 LifeId = 1;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Combat") int32 Health = 3;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Combat") bool bAlive = true;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Combat") double DeathServerTime = 0;
};

// Stationary native enemy for the first recipient combat slice. No legacy
// target cast, PC0 input polling, automatic player damage, or fake health field.
UCLASS(Blueprintable)
class JADEBOUNDPROGRESSION_API AJadeboundNativeRaider : public ACharacter
{
    GENERATED_BODY()
public:
    AJadeboundNativeRaider();
    virtual void Tick(float DeltaSeconds) override;
    virtual float TakeDamage(float Damage, const FDamageEvent& Event, AController* Instigator, AActor* Causer) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UPROPERTY(EditDefaultsOnly, Category="Jadebound|Presentation") TObjectPtr<USkeletalMesh> RaiderMesh;
    UPROPERTY(EditDefaultsOnly, Category="Jadebound|Presentation") TObjectPtr<UAnimationAsset> IdleAnimation;
    UFUNCTION(BlueprintPure, Category="Jadebound|Combat") FJadeboundRaiderLife GetLife() const { return Life; }
    bool HasValidPresentation() const;
protected:
    virtual void BeginPlay() override;
private:
    friend class AJadeboundProgressionActor;
    friend class AJadeboundDevelopmentGameMode;
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FJadeboundAttackWindowTestAccess;
    friend struct FJadeboundRecipientNativeTestAccess;
#endif
    UPROPERTY(VisibleAnywhere, Category="Jadebound|Presentation") TObjectPtr<UTextRenderComponent> HealthLabel;
    UPROPERTY(ReplicatedUsing=OnRepLife) FJadeboundRaiderLife Life;
    TMap<TWeakObjectPtr<AJadeboundProgressionActor>, int64> LastHitSwing;
    TWeakObjectPtr<AJadeboundProgressionActor> LastValidRecipient;
    FTransform Home;
    double NextAwardCheck = 0;
    bool bDeathAwardCommitted = false;
    bool ApplyValidatedHit(AJadeboundProgressionActor* Recipient, APawn* Attacker, int64 SwingId, int64 ExpectedLife);
    UFUNCTION() void OnRepLife();
};
