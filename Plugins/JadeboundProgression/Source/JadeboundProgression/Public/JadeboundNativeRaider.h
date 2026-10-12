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

USTRUCT()
struct FJadeboundRaiderStrike
{
    GENERATED_BODY()
    UPROPERTY() uint64 StrikeId = 0;
    UPROPERTY() bool bWindup = false;
    UPROPERTY() double ImpactServerTime = 0;
};

// Stationary native enemy for the first recipient combat slice. No legacy
// target cast, PC0 input polling, client damage amount, or fake health field.
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
    friend struct FJadeboundPlayerLifeTestAccess;
    friend struct FJadeboundNativeBalmTestAccess;
    friend struct FJadeboundNativeStrikeIssuanceTestAccess;
#endif
    UPROPERTY(VisibleAnywhere, Category="Jadebound|Presentation") TObjectPtr<UTextRenderComponent> HealthLabel;
    UPROPERTY(ReplicatedUsing=OnRepLife) FJadeboundRaiderLife Life;
    UPROPERTY(ReplicatedUsing=OnRepLife) FJadeboundRaiderStrike Strike;
    TWeakObjectPtr<AJadeboundProgressionActor> StrikeRecipient;
    TWeakObjectPtr<APawn> StrikePawn;
    uint64 StrikePlayerGeneration = 0;
    int64 StrikeEnemyLife = 0;
    double NextStrikeAt = 0;
    // Open only around one synchronous server impact, consumed before callbacks.
    bool bStrikeImpactPending = false;
    void TickNativeStrike();
    void CancelNativeStrike(double Now);
    bool IsNativeStrikeReachable(const APawn* Pawn) const;
    TMap<TWeakObjectPtr<AJadeboundProgressionActor>, int64> LastHitSwing;
    TWeakObjectPtr<AJadeboundProgressionActor> LastValidRecipient;
    FTransform Home;
    double NextAwardCheck = 0;
    bool bDeathAwardCommitted = false;
    bool ApplyValidatedHit(AJadeboundProgressionActor* Recipient, APawn* Attacker, int64 SwingId, int64 ExpectedLife);
    UFUNCTION() void OnRepLife();
};
