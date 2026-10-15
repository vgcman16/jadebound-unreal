#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "JadeboundOriginalHeroMovementComponent.generated.h"

class ACharacter;
class APlayerController;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UJadeboundOriginalHeroMovementProfile;
class UWorld;

// Transient presentation owned by the exact possessed V006 pawn in the copied map.
// This component never creates items, awards XP, modifies HP, or saves progression.
UCLASS()
class JADEBOUNDPROGRESSION_API UJadeboundOriginalHeroMovementComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    static bool IsMovementWorld(const UWorld* World);
    static bool IsReady(const ACharacter* Character);
    static bool Install(ACharacter* Character, APlayerController* Player, FString& Reason);
    UJadeboundOriginalHeroMovementProfile* GetProfile() const { return Profile; }
    float GetRunSpeed() const { return RunSpeed; }
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    bool Configure(ACharacter* Character, FString& Reason);
    bool HasOwnedPresentation(const ACharacter* Character) const;
    UPROPERTY(Transient) TObjectPtr<UJadeboundOriginalHeroMovementProfile> Profile;
    UPROPERTY(Transient) TArray<TObjectPtr<USkeletalMeshComponent>> Followers;
    UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> BodyComponent;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> WeaponComponent;
    bool bReady = false;
    float RunSpeed = 0.f;
};
