#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "JadeboundOriginalHeroMovementProfile.generated.h"

class USkeletalMesh;
class UAnimSequence;

// Only the separate Source137 courtyard loads this asset. No template CDO edits.
// Order is part of this small presentation contract, not inventory/save data.
UCLASS()
class JADEBOUNDPROGRESSION_API UJadeboundOriginalHeroMovementProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category="Source137") TObjectPtr<USkeletalMesh> Body;
    UPROPERTY(EditAnywhere, Category="Source137") TArray<TObjectPtr<USkeletalMesh>> CoveredBase;
    // Idle, Walk, Jog, Jump, Fall, Land; Source134 idle + qualified Source136 outputs.
    UPROPERTY(EditAnywhere, Category="Source137") TArray<TObjectPtr<UAnimSequence>> Clips;
    UPROPERTY(EditAnywhere, Category="Source137") FTransform BodyRelativeTransform;
    UPROPERTY(EditAnywhere, Category="Source137") FTransform WeaponHandTransform;
    // ROOT fills measured transforms after inspecting the new body and actual polearms.
    // False by default: neither an invented grip nor the old mannequin offset is accepted.
    UPROPERTY(EditAnywhere, Category="Source137") bool bMeasuredBindings = false;
};
