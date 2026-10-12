#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "JadeboundPersonalDrop.generated.h"

class AJadeboundProgressionActor;
class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;

USTRUCT(BlueprintType)
struct FJadeboundPersonalDropView
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Loot") int64 DropId = 0;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Loot") int32 ItemId = 0;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Loot") int32 Count = 0;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Loot") float LabelHeight = 35.f;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Loot") TObjectPtr<UStaticMesh> Mesh;
};

// Visual only: no overlap award and no client pickup/drop-id RPC. The owned
// progression actor chooses a saved nearby pending drop on ServerInteract.
UCLASS()
class JADEBOUNDPROGRESSION_API AJadeboundPersonalDrop : public AActor
{
    GENERATED_BODY()
public:
    AJadeboundPersonalDrop();
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UFUNCTION(BlueprintPure, Category="Jadebound|Loot") FJadeboundPersonalDropView GetDropView() const { return View; }
protected:
    virtual void BeginPlay() override;
private:
    friend class AJadeboundProgressionActor;
    UPROPERTY(VisibleAnywhere, Category="Jadebound|Presentation") TObjectPtr<UStaticMeshComponent> Prop;
    UPROPERTY(VisibleAnywhere, Category="Jadebound|Presentation") TObjectPtr<UTextRenderComponent> Label;
    UPROPERTY(ReplicatedUsing=OnRepView) FJadeboundPersonalDropView View;
    void InitializeFromAuthority(int64 Id, int32 Item, int32 Quantity, UStaticMesh* Mesh, float LabelHeight);
    UFUNCTION() void OnRepView();
};
