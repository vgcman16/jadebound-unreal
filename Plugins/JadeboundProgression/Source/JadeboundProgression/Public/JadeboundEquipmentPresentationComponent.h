#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "JadeboundEquipmentPresentationComponent.generated.h"

class ACharacter;
class UStaticMesh;
class UUserWidget;
class UWidgetComponent;

USTRUCT(BlueprintType)
struct FJadeboundEquipmentAppearance
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Development") int32 WeaponItemId = 0;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Development") int32 Generation = 0;
    UPROPERTY() TObjectPtr<UStaticMesh> GlaiveMesh;
    UPROPERTY() TObjectPtr<UStaticMesh> SpearMesh;
};

// Public appearance belongs to a relevant pawn, not its owner-only inventory.
UCLASS(ClassGroup=Jadebound)
class JADEBOUNDPROGRESSION_API UJadeboundEquipmentPresentationComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UJadeboundEquipmentPresentationComponent();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    void PublishFromAuthority(int32 WeaponItemId, UStaticMesh* GlaiveMesh, UStaticMesh* SpearMesh);
    UFUNCTION(BlueprintPure, Category="Jadebound|Development") FJadeboundEquipmentAppearance GetAppearance() const { return Appearance; }
private:
    UPROPERTY(ReplicatedUsing=OnRepAppearance) FJadeboundEquipmentAppearance Appearance;
    UFUNCTION() void OnRepAppearance();
    void ApplyAppearance();
    void StyleLifeBar(ACharacter* Pawn);
    // Runtime presentation only; never owns or replaces the template widget.
    TWeakObjectPtr<UWidgetComponent> LifeBarComponent;
    TWeakObjectPtr<UUserWidget> LastStyledLifeBarWidget;
    TWeakObjectPtr<UUserWidget> LastRejectedLifeBarWidget;
};
