#include "JadeboundPersonalDrop.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Net/UnrealNetwork.h"

AJadeboundPersonalDrop::AJadeboundPersonalDrop()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    bOnlyRelevantToOwner = true;
    bNetUseOwnerRelevancy = true; // Model -> owning PC, never another profile.
    SetReplicateMovement(true);
    Prop = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Loot"));
    SetRootComponent(Prop);
    Prop->SetMobility(EComponentMobility::Movable);
    Prop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("LootLabel"));
    Label->SetupAttachment(Prop);
    Label->SetRelativeLocation(FVector(0,0,35));
    Label->SetHorizontalAlignment(EHTA_Center);
    Label->SetWorldSize(16.f);
    Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
void AJadeboundPersonalDrop::BeginPlay() { Super::BeginPlay(); OnRepView(); }
void AJadeboundPersonalDrop::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AJadeboundPersonalDrop, View);
}
void AJadeboundPersonalDrop::InitializeFromAuthority(int64 Id, int32 Item, int32 Quantity,
    UStaticMesh* Mesh, float Height)
{
    if (!HasAuthority() || Id <= 0 || !Mesh ||
        !((Item == 5 && Quantity == 1) || (Item == 3 && Quantity == 2))) return;
    View.DropId = Id; View.ItemId = Item; View.Count = Quantity; View.Mesh = Mesh;
    // This affects only readability, not the saved pickup range/position.
    View.LabelHeight = FMath::Clamp(Height,20.f,160.f);
    OnRepView();
    ForceNetUpdate();
}
void AJadeboundPersonalDrop::OnRepView()
{
    const bool Valid = View.DropId > 0 && View.Mesh &&
        ((View.ItemId == 5 && View.Count == 1) || (View.ItemId == 3 && View.Count == 2));
    Prop->SetStaticMesh(Valid ? View.Mesh.Get() : nullptr);
    Prop->SetVisibility(Valid);
    Label->SetVisibility(Valid);
    Label->SetRelativeLocation(FVector(0,0,View.LabelHeight));
    Label->SetText(FText::FromString(View.ItemId == 5 ? TEXT("Stolen parcel  [E]") : TEXT("Iron ore x2  [E]")));
    Label->SetTextRenderColor(View.ItemId == 5 ? FColor(95,255,184) : FColor(255,206,124));
}
void AJadeboundPersonalDrop::Tick(float Seconds)
{
    Super::Tick(Seconds);
    // GetNetOwner returns the immediate model, not its PC. Resolve the known
    // two-hop model -> PC chain, with no local PC0 shortcut/recipient.
    const AActor* Model = GetOwner();
    const APlayerController* Controller = Model ? Cast<APlayerController>(Model->GetOwner()) : nullptr;
    if (Controller && Controller->IsLocalController() && Controller->PlayerCameraManager)
        Label->SetWorldRotation((Controller->PlayerCameraManager->GetCameraLocation() - Label->GetComponentLocation()).Rotation());
}
