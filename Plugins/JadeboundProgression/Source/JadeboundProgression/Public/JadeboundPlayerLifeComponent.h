#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "JadeboundPlayerLifeComponent.generated.h"

class AJadeboundProgressionActor;
class UStaticMeshComponent;
class APlayerController;
class APawn;
class AController;

USTRUCT(BlueprintType)
struct FJadeboundPlayerLife
{
    GENERATED_BODY()
    UPROPERTY() bool bInitialized = false;
    UPROPERTY() uint64 Generation = 0;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Combat") int32 Health = 0;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Combat") double DeathServerTime = 0;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Combat") double ReviveServerTime = 0;
    bool IsAlive() const { return bInitialized && Generation > 0 && Health > 0 && Health <= 100; }
};

// Public pawn presentation for server-owned life. No damage/heal RPC exists.
UCLASS(ClassGroup=(Jadebound))
class JADEBOUNDPROGRESSION_API UJadeboundPlayerLifeComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UJadeboundPlayerLifeComponent();
    virtual void OnRegister() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UFUNCTION(BlueprintPure, Category="Jadebound|Combat") FJadeboundPlayerLife GetLife() const { return Life; }
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    friend class AJadeboundProgressionActor;
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FJadeboundPlayerLifeTestAccess;
#endif
    UPROPERTY(ReplicatedUsing=OnRepLife) FJadeboundPlayerLife Life;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Spirit;
    bool bDeadApplied = false;
    bool bSpiritAttempted = false;
    bool bMoveLease = false;
    bool bCursorSaved = false;
    bool bMeshVisible = true;
    uint8 SavedMovementMode = 0;
    uint8 SavedCustomMovementMode = 0;
    uint8 SavedCollisionMode = 0;
    TWeakObjectPtr<APlayerController> InputOwner;
    void PublishFromAuthority(const FJadeboundPlayerLife& Value);
    UFUNCTION() void OnRepLife();
    UFUNCTION() void OnControllerChanged(APawn* Pawn, AController* Previous, AController* Current);
    void UpdateNativePeerBar(class ACharacter* Pawn);
    void ApplyLife();
    void ReleaseInput(bool bLiveWorld);
};
