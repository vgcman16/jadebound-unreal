#include "JadeboundProgressionActor.h"
#include "Domain/ProgressionCore.h"
#include "Domain/RecipientLedger.h"
#include "JadeboundDevelopmentGameMode.h"
#include "JadeboundNativeRaider.h"
#include "JadeboundMeleePresentationComponent.h"
#include "JadeboundPersonalDrop.h"
#include "Combat/RecipientCombatRules.h"
#include "Combat/RecipientAnimationRules.h"
#include "Engine/SkeletalMesh.h"
#include "Persistence/ProgressionSaveV2.h"
#include "Persistence/JournalIO.h"
#include "Presentation/LootVisualMath.h"
#include "Presentation/InventoryReplication.h"
#include "Templates/UniquePtr.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Development/DevelopmentIdentity.h"
#include "JadeboundEquipmentPresentationComponent.h"
#include "UI/SJadeboundProgressionPanel.h"
#include "Capture/JadeboundNativeCapture.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Components/InputComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/LineBatchComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Math/RotationMatrix.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Console.h"
#include "Widgets/SViewport.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Level.h"
#include "UObject/Package.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextRenderActor.h"
#include "Engine/World.h"
#include "Engine/LatentActionManager.h"
#include "CollisionQueryParams.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "EnhancedInputComponent.h"
#include "EnhancedPlayerInput.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "HAL/IConsoleManager.h"
#include "InputAction.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Framework/Application/SlateApplication.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "CoreGlobals.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Guid.h"
#include "Serialization/Archive.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/SecureHash.h"
#include "Net/UnrealNetwork.h"
#include "UObject/UnrealType.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"
#include <algorithm>
#include <limits>
#include <utility>

void Jadebound::ProgressionCoreDeleter::operator()(ProgressionCore* Pointer) const { delete Pointer; }
void FJadeboundNativeCaptureDeleter::operator()(FJadeboundNativeCapture* Pointer) const { delete Pointer; }

namespace
{
const FName WeaponTag(TEXT("JadeGlaiveV001"));
constexpr double RaiderWindupSeconds = .65;
constexpr double RaiderRecoverySeconds = .90;
constexpr double RaiderMaximumLateImpactSeconds = .25;
constexpr double RaiderReachCm = 175.0;
constexpr double RaiderVerticalReachCm = 90.0;
constexpr double RaiderFacingDot = .5; // 60-degree half cone, captured at windup
constexpr float RaiderDamage = 1.f; // existing Hero 10 raw HP, not 100 raw HP

// Original finite jade cue, owned by the actual floor prop. No global debug
// draw, interaction primitive or per-frame line rebuild is introduced.
bool AddLootCollectCue(AJadeboundProgressionActor* Owner, AStaticMeshActor* Prop,
    ATextRenderActor* Caption, int64 DropId)
{
    if (!IsValid(Owner) || Owner->IsActorBeingDestroyed() || DropId <= 0 ||
        !IsValid(Prop) || Prop->IsActorBeingDestroyed() ||
        !IsValid(Caption) || Caption->IsActorBeingDestroyed() ||
        Prop->GetOwner() != Owner || Caption->GetOwner() != Owner ||
        Prop->GetWorld() != Owner->GetWorld() || Caption->GetWorld() != Owner->GetWorld()) return false;
    auto* Mesh = Prop->GetStaticMeshComponent();
    if (!IsValid(Mesh) || !Mesh->IsRegistered() || Prop->GetRootComponent() != Mesh ||
        !IsValid(Mesh->GetStaticMesh()) || Mesh->GetComponentTransform().ContainsNaN() ||
        !Mesh->GetComponentScale().Equals(FVector::OneVector, .0001) || Mesh->GetUpVector().Z < .9999) return false;
    FVector Minimum, Maximum;
    Mesh->GetLocalBounds(Minimum, Maximum);
    if (Minimum.ContainsNaN() || Maximum.ContainsNaN() ||
        Maximum.X <= Minimum.X || Maximum.Y <= Minimum.Y || Maximum.Z <= Minimum.Z) return false;
    const FTransform Transform = Mesh->GetComponentTransform();
    const FVector Middle = (Minimum + Maximum) * .5;
    const FVector Center = Transform.TransformPosition(FVector(Middle.X,Middle.Y,Minimum.Z)) + FVector(0,0,2);
    const FVector Right = Transform.TransformVectorNoScale(FVector(1,0,0));
    const FVector Forward = Transform.TransformVectorNoScale(FVector(0,1,0));
    const FVector LabelEnd = Caption->GetActorLocation() - FVector(0,0,4);
    if (Center.ContainsNaN() || Right.ContainsNaN() || Forward.ContainsNaN() || LabelEnd.ContainsNaN() ||
        FVector::DistSquared(Center,LabelEnd) > FMath::Square(480.0)) return false;
    // Visual margin only. Original query caps and saved anchors are unchanged.
    const double HalfX = FMath::Clamp((Maximum.X-Minimum.X)*.5+3.0,8.0,30.0);
    const double HalfY = FMath::Clamp((Maximum.Y-Minimum.Y)*.5+3.0,8.0,30.0);
    const FLinearColor Jade = FLinearColor::FromSRGBColor(FColor(77,213,171,210));
    TArray<FBatchedLine> Lines; Lines.Reserve(9);
    constexpr int32 Directions[2] = {-1,1};
    for (int32 X : Directions) for (int32 Y : Directions)
    {
        const FVector Corner = Center+Right*(X*HalfX)+Forward*(Y*HalfY);
        Lines.Emplace(Corner,Corner-Right*(X*5.0),Jade,0.f,1.25f,SDPG_World);
        Lines.Emplace(Corner,Corner-Forward*(Y*5.0),Jade,0.f,1.25f,SDPG_World);
    }
    const FVector Delta = LabelEnd-Center;
    const double X = FVector::DotProduct(Delta,Right)<0.0 ? -1.0 : 1.0;
    const double Y = FVector::DotProduct(Delta,Forward)<0.0 ? -1.0 : 1.0;
    Lines.Emplace(Center+Right*(X*HalfX)+Forward*(Y*HalfY),LabelEnd,Jade,0.f,.75f,SDPG_World);
    const TWeakObjectPtr<AJadeboundProgressionActor> WeakOwner(Owner);
    const TWeakObjectPtr<AStaticMeshActor> WeakProp(Prop);
    const TWeakObjectPtr<ATextRenderActor> WeakCaption(Caption);
    auto* Cue = NewObject<ULineBatchComponent>(Prop,
        FName(*FString::Printf(TEXT("JadeboundCollectDrop_%lld"),DropId)),RF_Transient);
    if (!Cue) return false;
    const TWeakObjectPtr<ULineBatchComponent> WeakCue(Cue);
    Prop->AddInstanceComponent(Cue);
    Cue->SetupAttachment(Mesh);
    Cue->SetMobility(EComponentMobility::Movable);
    Cue->PrimaryComponentTick.bCanEverTick = false;
    Cue->bTickInEditor = false;
    Cue->SetComponentTickEnabled(false);
    Cue->SetIsReplicated(false);
    Cue->SetGenerateOverlapEvents(false);
    Cue->SetCanEverAffectNavigation(false);
    Cue->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Cue->SetCollisionResponseToAllChannels(ECR_Ignore);
    Cue->SetCastShadow(false);
    Cue->RegisterComponent();
    Owner=WeakOwner.Get(); Prop=WeakProp.Get(); Caption=WeakCaption.Get(); Cue=WeakCue.Get();
    if (!Owner || Owner->IsActorBeingDestroyed() || !Prop || Prop->IsActorBeingDestroyed() ||
        !Caption || Caption->IsActorBeingDestroyed() || !Cue || !Cue->IsRegistered() ||
        Prop->GetOwner()!=Owner || Caption->GetOwner()!=Owner || Cue->GetOwner()!=Prop ||
        Prop->GetWorld()!=Owner->GetWorld() || Caption->GetWorld()!=Owner->GetWorld())
    {
        if (Cue) Cue->DestroyComponent();
        return false;
    }
    // Tick is disabled: nine world-space lines live only with this actual prop.
    Cue->DrawLines(Lines);
    return true;
}

// Cursor-only exception for the four measured hidden courtyard perimeter walls.
// Names are runtime object names, not editor-only labels. Every identity/property
// check must pass; hidden crates, buildings, targets and all other actors remain.
bool IsQualifiedCursorPerimeter(UWorld* World, const AActor* Candidate)
{
    if (!IsValid(World) || !IsValid(Candidate) ||
        Candidate->GetClass() != AStaticMeshActor::StaticClass() ||
        Candidate->GetLevel() != World->PersistentLevel ||
        !World->PersistentLevel ||
        World->PersistentLevel->GetOutermost()->GetName() != TEXT("/Game/Jadebound/Playable/AreaV002/L_JadeboundProgression_AreaV002") ||
        !Candidate->IsHidden() || !Candidate->GetActorEnableCollision()) return false;
    static const FName Names[4] = {TEXT("StaticMeshActor_10"), TEXT("StaticMeshActor_11"), TEXT("StaticMeshActor_12"), TEXT("StaticMeshActor_13")};
    static const FVector Locations[4] = {
        FVector(-352.045814642448, 852.045814642448, 150.0),
        FVector(652.045814642448, -152.04581464244802, 150.0),
        FVector(-281.33513652379406, -81.33513652379406, 150.0),
        FVector(581.3351365237941, 781.3351365237941, 150.0)};
    static const FVector Scales[4] = {
        FVector(.2, 12.4, 3.0), FVector(.2, 12.4, 3.0),
        FVector(14.4, .2, 3.0), FVector(14.4, .2, 3.0)};
    int32 Index = INDEX_NONE;
    for (int32 Slot = 0; Slot < 4; ++Slot)
        if (Candidate->GetFName() == Names[Slot]) { Index = Slot; break; }
    if (Index == INDEX_NONE) return false;
    const auto* Wall = Cast<AStaticMeshActor>(Candidate);
    const auto* Component = Wall->GetStaticMeshComponent();
    if (!IsValid(Component) || Component != Candidate->GetRootComponent() ||
        Component->GetOwner() != Candidate || Component->GetFName() != TEXT("StaticMeshComponent0") ||
        Component->GetMobility() != EComponentMobility::Static || !Component->IsRegistered() ||
        !Component->IsVisible() || Component->bHiddenInGame || !Component->CanEverAffectNavigation() ||
        Component->GetCollisionEnabled() != ECollisionEnabled::QueryAndPhysics ||
        Component->GetCollisionProfileName() != TEXT("BlockAll") ||
        Component->GetCollisionObjectType() != ECC_WorldStatic ||
        !IsValid(Component->GetStaticMesh()) ||
        Component->GetStaticMesh()->GetPathName() != TEXT("/Engine/BasicShapes/Cube.Cube")) return false;
    const FRotator Rotation(0.0, 135.0, 0.0);
    // 0.0001 scale units on the 100 cm Cube are 0.01 cm. Rotation is
    // checked more tightly than 0.01 cm at either measured long-wall tip.
    return Candidate->GetActorLocation().Equals(Locations[Index], .01) &&
        Candidate->GetActorRotation().Equals(Rotation, .0001) &&
        Candidate->GetActorScale3D().Equals(Scales[Index], .0001) &&
        Component->GetComponentLocation().Equals(Locations[Index], .01) &&
        Component->GetComponentRotation().Equals(Rotation, .0001) &&
        Component->GetComponentScale().Equals(Scales[Index], .0001) &&
        Component->GetStaticMesh()->GetBoundingBox().GetSize().Equals(FVector(100.0), .001);
}

void Append32(TArray<uint8>& Bytes, uint32 Value)
{
    for (int32 Shift = 0; Shift < 32; Shift += 8) Bytes.Add(static_cast<uint8>(Value >> Shift));
}
uint32 Read32(const TArray<uint8>& Bytes, int32 Offset)
{
    uint32 Value = 0;
    for (int32 Index = 0; Index < 4; ++Index) Value |= static_cast<uint32>(Bytes[Offset + Index]) << (Index * 8);
    return Value;
}
bool EncodeJournal(UJadeboundCharacterSave* Save, TArray<uint8>& Bytes)
{
    TArray<uint8> Payload;
    if (!UGameplayStatics::SaveGameToMemory(Save, Payload) || Payload.Num() > 1024 * 1024) return false;
    uint8 Digest[20];
    FSHA1::HashBuffer(Payload.GetData(), Payload.Num(), Digest);
    Bytes.Reset();
    Append32(Bytes, 0x3150424A); // JBP1, fixed little endian header.
    Append32(Bytes, 1);
    Append32(Bytes, static_cast<uint32>(Payload.Num()));
    Bytes.Append(Digest, 20);
    Bytes.Append(Payload);
    return true;
}
UJadeboundCharacterSave* ReadJournal(const FString& Path, TArray<uint8>* Raw = nullptr)
{
    const int64 Size = IFileManager::Get().FileSize(*Path);
    if (Size < 32 || Size > 1024 * 1024 + 32) return nullptr;
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *Path) || Bytes.Num() != Size || Read32(Bytes,0) != 0x3150424A || Read32(Bytes,4) != 1 || Read32(Bytes,8) != static_cast<uint32>(Bytes.Num()-32)) return nullptr;
    uint8 Digest[20];
    FSHA1::HashBuffer(Bytes.GetData()+32, Bytes.Num()-32, Digest);
    if (FMemory::Memcmp(Digest, Bytes.GetData()+12,20) != 0) return nullptr;
    TArray<uint8> Payload;
    Payload.Append(Bytes.GetData()+32,Bytes.Num()-32);
    auto* Save = Cast<UJadeboundCharacterSave>(UGameplayStatics::LoadGameFromMemory(Payload));
    if (Save && Raw) *Raw = MoveTemp(Bytes);
    return Save;
}

template<typename T> T* Field(UObject* Object, const TCHAR* Name)
{
    return Object ? FindFProperty<T>(Object->GetClass(), Name) : nullptr;
}
int32 ReadInt(UObject* Object, const TCHAR* Name)
{
    if (auto* Property = Field<FIntProperty>(Object, Name)) return Property->GetPropertyValue_InContainer(Object);
    return 0;
}
void WriteInt(UObject* Object, const TCHAR* Name, int32 Value)
{
    if (auto* Property = Field<FIntProperty>(Object, Name)) Property->SetPropertyValue_InContainer(Object, Value);
}
UObject* ReadObject(UObject* Object, const TCHAR* Name)
{
    if (auto* Property = Field<FObjectPropertyBase>(Object, Name)) return Property->GetObjectPropertyValue_InContainer(Object);
    return nullptr;
}
FJadeboundHealthSample ReadHealthSample(UObject* Object, const TCHAR* CurrentName, const TCHAR* MaximumName)
{
    FJadeboundHealthSample Sample;
    const auto* Current = Field<FNumericProperty>(Object, CurrentName);
    const auto* Maximum = Field<FNumericProperty>(Object, MaximumName);
    if (!Current || !Maximum || !Current->IsFloatingPoint() || !Maximum->IsFloatingPoint()) return Sample;
    Sample.Current = Current->GetFloatingPointPropertyValue(Current->ContainerPtrToValuePtr<void>(Object));
    Sample.Maximum = Maximum->GetFloatingPointPropertyValue(Maximum->ContainerPtrToValuePtr<void>(Object));
    if (!FMath::IsFinite(Sample.Current) || !FMath::IsFinite(Sample.Maximum) || Sample.Maximum <= 0.0) return Sample;
    Sample.bAvailable = true;
    // Branch before division: finite extreme values cannot overflow the ratio.
    if (Sample.Current <= 0.0) { Sample.Ratio = 0.f; Sample.Percent = 0; }
    else if (Sample.Current >= Sample.Maximum) { Sample.Ratio = 1.f; Sample.Percent = 100; }
    else
    {
        Sample.Ratio = static_cast<float>(FMath::Clamp(Sample.Current / Sample.Maximum, 0.0, 1.0));
        Sample.Percent = FMath::Clamp(FMath::RoundToInt(Sample.Ratio * 100.f), 1, 99);
    }
    return Sample;
}
// Fixed engine-owned paths. v1 remains an immutable migration source.
FString LocalSkillHotbarPath()
{
    return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Config"),TEXT("JadeboundControls-v2.ini")));
}
FString LegacySkillHotbarPath()
{
    return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Config"),TEXT("JadeboundControls-v1.ini")));
}
bool ReadLocalSkillHotbar(const FString& Filename,FJadeboundLocalHotbarBindings& Out,bool AllowSchema1=false)
{
    TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Filename));
    if (!Reader || Reader->TotalSize()<=0 || Reader->TotalSize()>4096) return false;
    FString Text;
    const bool Loaded=FFileHelper::LoadFileToString(Text,*Reader);
    const bool Closed=Reader->Close();
    return Loaded && Closed && !Reader->IsError() && FJadeboundLocalHotbarBindings::Decode(Text,Filename,AllowSchema1,Out);
}

#if PLATFORM_WINDOWS
FString HotbarNativeWindowsPath(const FString& Path)
{
    FString Native=FPaths::ConvertRelativePathToFull(Path).Replace(TEXT("/"),TEXT("\\"));
    if (Native.StartsWith(TEXT("\\\\?\\"))) return Native;
    return Native.StartsWith(TEXT("\\\\")) ? FString(TEXT("\\\\?\\UNC\\"))+Native.Mid(2) : FString(TEXT("\\\\?\\"))+Native;
}
#endif

FText ResultText(Jadebound::ResultCode Code)
{
    using C = Jadebound::ResultCode;
    switch (Code)
    {
    case C::InsufficientCoins: return FText::FromString(TEXT("You do not have enough jade coins."));
    case C::InventoryFull: return FText::FromString(TEXT("Your bag is full. Sell an item or merge a stack first."));
    case C::NotSellable: return FText::FromString(TEXT("This quest item cannot be sold."));
    case C::NotPurchasable: return FText::FromString(TEXT("The merchant does not sell this item."));
    case C::NothingEquipped: return FText::FromString(TEXT("You have no weapon equipped."));
    case C::NotWeapon: return FText::FromString(TEXT("Select a weapon in your bag."));
    case C::NotConsumable: return FText::FromString(TEXT("This item cannot be used. Your pack is unchanged."));
    case C::QuestAlreadyCompleted: return FText::FromString(TEXT("This quest is complete. Its reward has already been paid."));
    case C::QuestNeedsDefeat: return FText::FromString(TEXT("Defeat the courtyard raider first."));
    case C::QuestNeedsParcel: return FText::FromString(TEXT("Recover the stolen parcel before returning."));
    case C::DropAlreadyProcessed: case C::DuplicateDrop: return FText::FromString(TEXT("That loot has already been collected."));
    case C::DropLedgerFull: return FText::FromString(TEXT("Too much uncollected loot. Collect existing drops before earning more."));
    case C::DropIdOverflow: return FText::FromString(TEXT("Loot IDs are exhausted. Existing progress is preserved; character recovery is required."));
    case C::RevisionOverflow: return FText::FromString(TEXT("Your progress counter is exhausted. Existing progress is preserved."));
    default: return FText::FromString(TEXT("That action is no longer valid. Your items and coins are unchanged."));
    }
}
FText Description(Jadebound::ItemId Id)
{
    using I = Jadebound::ItemId;
    switch (Id)
    {
    case I::JadeGlaive: return FText::FromString(TEXT("A jade-edged polearm. Your starting weapon; equipping it visibly replaces the spear."));
    case I::AshwoodSpear: return FText::FromString(TEXT("An original ashwood and iron spear bound with jade thread. A visible alternative to the glaive."));
    case I::IronOre: return FText::FromString(TEXT("Dense iron veined with green mineral. Raiders carry it from the old quarry. The merchant pays one coin per piece."));
    case I::Moonleaf: return FText::FromString(TEXT("A pale, silver-veined leaf traded by courtyard herbalists. The merchant pays one coin per leaf."));
    case I::JadeParcel: return FText::FromString(TEXT("The merchant's stolen parcel. Return it for five coins and one jade shard. Cannot be bought or sold."));
    case I::JadeShard: return FText::FromString(TEXT("A small piece of luminous jade. A useful quest reward worth two coins."));
    case I::Balm: return FText::FromString(TEXT("An original courtyard salve. Use one to recover 25% of maximum health, up to full health. Right-click in your pack."));
    default: return FText::GetEmpty();
    }
}
// Presentation geometry only: original drop anchors never change.
bool ChooseLootLabelPosition(const FVector& Anchor, const FVector& RaiderCenter,
    double RaiderRadius, double RaiderTop, int32 Row, double TextLowerOffset, FVector& Out)
{
    if (Anchor.ContainsNaN() || RaiderCenter.ContainsNaN() ||
        !FMath::IsFinite(RaiderRadius) || RaiderRadius <= 0.0 || RaiderRadius > 128.0 ||
        !FMath::IsFinite(RaiderTop) || RaiderTop < RaiderCenter.Z + RaiderRadius ||
        Row < 0 || Row > 5 || !FMath::IsFinite(TextLowerOffset) || FMath::Abs(TextLowerOffset) > 24.0)
        return false;
    double Base = Anchor.Z;
    const double FootprintDistanceSquared = FVector::DistSquared2D(Anchor, RaiderCenter);
    if (!FMath::IsFinite(FootprintDistanceSquared)) return false;
    if (FootprintDistanceSquared <= FMath::Square(RaiderRadius))
        Base = FMath::Max(Base, RaiderTop + 12.0 - TextLowerOffset);
    const double Rise = Base - Anchor.Z + 24.0 * Row;
    if (!FMath::IsFinite(Rise) || Rise < 0.0 || Rise > 140.0) return false;
    Out = Anchor + FVector(0.0,0.0,Rise);
    return !Out.ContainsNaN();
}
bool MakeLootCameraBasis(const FRotator& Rotation, FVector& Right, FVector& Up, FRotator& Facing)
{
    if (Rotation.ContainsNaN()) return false;
    const FRotationMatrix Camera(Rotation);
    const FVector Forward = Camera.GetUnitAxis(EAxis::X);
    Right = Camera.GetUnitAxis(EAxis::Y); Up = Camera.GetUnitAxis(EAxis::Z);
    if (Forward.ContainsNaN() || Right.ContainsNaN() || Up.ContainsNaN() ||
        FMath::Abs(Forward.SizeSquared() - 1.0) > 1.e-6 ||
        FMath::Abs(Right.SizeSquared() - 1.0) > 1.e-6 || FMath::Abs(Up.SizeSquared() - 1.0) > 1.e-6 ||
        FMath::Abs(FVector::DotProduct(Right, Up)) > 1.e-6) return false;
    // Text face +X, glyph right -Y, up +Z: parallel to the actual image plane.
    Facing = FRotationMatrix::MakeFromXZ(-Forward, Up).Rotator();
    return !Facing.ContainsNaN();
}
bool IsNearLootLabelGroup(const FVector& Anchor, const FVector& Representative, double Radius)
{
    if (Anchor.ContainsNaN() || Representative.ContainsNaN() ||
        !FMath::IsFinite(Radius) || Radius <= 0.0 || Radius > 256.0) return false;
    const double Distance = FVector::DistSquared2D(Anchor, Representative);
    return FMath::IsFinite(Distance) && Distance <= FMath::Square(Radius);
}
bool ChooseGroupedLootLabelPosition(const FVector& Anchor, const FVector& Representative,
    const FVector& Right, const FVector& Up, double HalfWidth, int32 Row, double LowerOffset, FVector& Out)
{
    if (Anchor.ContainsNaN() || Representative.ContainsNaN() || Right.ContainsNaN() || Up.ContainsNaN() ||
        !FMath::IsFinite(HalfWidth) || HalfWidth <= 0.0 || HalfWidth > 125.0 || Row < 0 || Row > 5 ||
        !FMath::IsFinite(LowerOffset) || FMath::Abs(LowerOffset) > 24.0 ||
        FMath::Abs(Right.SizeSquared() - 1.0) > 1.e-6 || FMath::Abs(Up.SizeSquared() - 1.0) > 1.e-6 ||
        FMath::Abs(FVector::DotProduct(Right, Up)) > 1.e-6) return false;
    // Shared image-plane depth, measured half-width plus a 96cm gutter.
    Out = Representative + Right * (HalfWidth + 96.0) + Up * (24.0 * Row - LowerOffset);
    const double Displacement = FVector::DistSquared(Out, Anchor);
    return !Out.ContainsNaN() && FMath::IsFinite(Displacement) && Displacement <= FMath::Square(320.0);
}
bool ValidateDrops(const Jadebound::Snapshot& Candidate, const TArray<FJadeboundSavedDrop>& Drops)
{
    return Jadebound::Persistence::ValidateDropsV2(Candidate, Drops);
}
}

bool FJadeboundLocalHotbarBindings::Decode(const FString& Text,const FString& Hint,bool AllowSchema1,FJadeboundLocalHotbarBindings& Out)
{
    if(Text.IsEmpty() || Text.Len()>4096) return false;
    FConfigFile Config; Config.ProcessInputFileContents(Text,Hint);
    const FConfigSection* Section=Config.FindSection(TEXT("JadeboundHotbar"));
    FString Schema;
    if(Config.Num()!=1 || !Section || Section->Num()!=11 ||
        !Config.GetString(TEXT("JadeboundHotbar"),TEXT("Schema"),Schema)) return false;
    const bool Legacy=Schema.Equals(TEXT("1"),ESearchCase::CaseSensitive);
    if(Legacy!=AllowSchema1 || (!Legacy && !Schema.Equals(TEXT("2"),ESearchCase::CaseSensitive))) return false;
    FJadeboundLocalHotbarBindings Candidate;
    for(int32 Slot=0;Slot<10;++Slot)
    {
        FString Value;
        if(!Config.GetString(TEXT("JadeboundHotbar"),*FString::Printf(TEXT("F%d"),Slot+1),Value)) return false;
        FJadeboundHotbarEntry Entry;
        if(Value.Equals(Legacy ? TEXT("None") : TEXT("Empty"),ESearchCase::CaseSensitive)) Entry={};
        else if(Value.Equals(Legacy ? TEXT("Guard") : TEXT("Skill:Guard"),ESearchCase::CaseSensitive)) Entry=FJadeboundHotbarEntry::Skill(0);
        else if(Value.Equals(Legacy ? TEXT("Current") : TEXT("Skill:Current"),ESearchCase::CaseSensitive)) Entry=FJadeboundHotbarEntry::Skill(1);
        else if(!Legacy && Value.Equals(TEXT("Item:Balm"),ESearchCase::CaseSensitive)) Entry=FJadeboundHotbarEntry::Item(7);
        else return false;
        Candidate.Entries[Slot]=Entry;
    }
    if(!Candidate.IsValid()) return false;
    Out=Candidate; return true; // no partial document escapes
}
FString FJadeboundLocalHotbarBindings::Encode() const
{
    if(!IsValid()) return FString();
    FConfigFile Config; Config.SetString(TEXT("JadeboundHotbar"),TEXT("Schema"),TEXT("2"));
    for(int32 Slot=0;Slot<10;++Slot)
    {
        const auto Entry=Resolve(Slot);
        const TCHAR* Value=Entry.Kind==EJadeboundHotbarKind::Empty ? TEXT("Empty") :
            Entry.Kind==EJadeboundHotbarKind::Item ? TEXT("Item:Balm") : Entry.Identity==0 ? TEXT("Skill:Guard") : TEXT("Skill:Current");
        Config.SetString(TEXT("JadeboundHotbar"),*FString::Printf(TEXT("F%d"),Slot+1),Value);
    }
    FString Text;Config.WriteToString(Text);return Text;
}

AJadeboundProgressionActor::AJadeboundProgressionActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    bReplicates = true;
    bOnlyRelevantToOwner = true;
    InputPriority = 100;
    bBlockInput = false;
    Core = TUniquePtr<Jadebound::ProgressionCore, Jadebound::ProgressionCoreDeleter>(new Jadebound::ProgressionCore());
}
AJadeboundProgressionActor::~AJadeboundProgressionActor() = default;

bool AJadeboundProgressionActor::HasDevelopmentContext() const
{
    return bDevelopmentModel && Jadebound::Development::IsCharacterId(ServerCharacterId);
}
bool AJadeboundProgressionActor::IsDevelopmentReady() const
{
    return bServerModelInitialized && HasDevelopmentContext() && IsValid(Player) && GetOwner() == Player &&
        IsValid(Hero) && Player->GetPawn() == Hero;
}
void AJadeboundProgressionActor::OnRepReadiness()
{
    if (!HasAuthority()) bReady = State.Slots.Num() == static_cast<int32>(Jadebound::InventorySlotCount);
}
FString AJadeboundProgressionActor::GetSaveCharacterId() const
{
    return bDevelopmentModel ? ServerCharacterId : TEXT("Traveler01");
}
FString AJadeboundProgressionActor::GetJournalPath(int32 Slot) const
{
    FString Path;
    Jadebound::Persistence::TryJournalPath(bDevelopmentModel, GetSaveCharacterId(), Slot, true, Path);
    return Path;
}
bool AJadeboundProgressionActor::ConfigureDevelopmentOwner(APlayerController* Controller, const FString& Id, UStaticMesh* Glaive)
{
    if (!HasAuthority() || GetNetMode() == NM_Client || !IsValid(Controller) || Controller->GetWorld() != GetWorld() ||
        !Jadebound::Development::IsCharacterId(Id) || !Glaive) return false;
    if (HasActorBegunPlay() && (!bDevelopmentModel || ServerCharacterId != Id || IsValid(Player))) return false;
    bDevelopmentModel = true;
    ServerCharacterId = Id;
    if (!DevelopmentWriterLease)
    {
        const FString Directory = FPaths::GetPath(GetJournalPath(0));
        IPlatformFile& FileSystem = FPlatformFileManager::Get().GetPlatformFile();
        if (!FileSystem.CreateDirectoryTree(*Directory)) return false;
        // On the scoped Windows backend, OpenWrite never shares write access.
        // Hold this handle for the model's lifetime, including disconnected
        // held candidates. Existing journals are never used as lock files.
        DevelopmentWriterLease.Reset(FileSystem.OpenWrite(*FPaths::Combine(Directory, TEXT("Character.writer")), true, false));
        if (!DevelopmentWriterLease) return false;
    }
    DevelopmentGlaiveMesh = Glaive;
    SetOwner(Controller);
    Player = Controller;
    Hero = nullptr;
    bAuthorityMenuOpen = false;
    bBindingsInstalled = false;
    if (HasActorBegunPlay()) BindGameplay();
    if (bRecipientCombatEnabled && HasActorBegunPlay()) RefreshDropVisuals();
    return true;
}
void AJadeboundProgressionActor::DetachDevelopmentOwner(APlayerController* Controller)
{
    if (!HasAuthority() || !HasDevelopmentContext() || Player != Controller) return;
    CancelRecipientAttack();
    // Character state persists independently of its living pawn. A failed
    // write retains this exact actor/model for server-local retry and rejoin.
    const TWeakObjectPtr<AJadeboundProgressionActor> BeforeSaving(this);
    const bool bSaved = bUnsavedCandidate ? RetryHeldSave() : (bReady && WriteSave());
    if (!BeforeSaving.IsValid() || BeforeSaving->IsActorBeingDestroyed() || bPersistenceEndingPlay) return;
    if (bSaved)
    {
        bReady = true;
        bServerModelInitialized = true;
    }
    ShowMenu(false);
    if (Controller && Controller->IsLocalController())
    {
        DisableInput(Controller);
        if (HudWidget.IsValid() && GetWorld()->GetGameViewport())
            GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(HudWidget.ToSharedRef());
        HudWidget.Reset();
    }
    // A retained listen-host model may later bind another local controller.
    // Remove only this model's listeners before installing them for that owner.
    if (auto* Gate = AttackGateComponent.Get())
        for (uint32 Handle : AttackGateBindingHandles) Gate->RemoveBindingByHandle(Handle);
    AttackGateBindingHandles.Reset();
    AttackGateComponent.Reset();
    AttackGatePawn.Reset();
    if (InputComponent) InputComponent->KeyBindings.Reset();
    AttackBindingIndex = INDEX_NONE;
    bBindingsInstalled = false;
    Player = nullptr;
    Hero = nullptr;
    WeaponComponent = nullptr;
    PublicEquipment.Reset();
    PublicMelee.Reset();
    bAuthorityMenuOpen = false;
    SetOwner(nullptr);
    if (bRecipientCombatEnabled) RefreshDropVisuals();
    ForceNetUpdate();
}
bool AJadeboundProgressionActor::RetryDevelopmentSaveFromServer()
{
    if (!HasAuthority() || !HasDevelopmentContext()) return false;
    return RetryHeldSave();
}

void AJadeboundProgressionActor::BeginPlay()
{
    Super::BeginPlay();
    BindGameplay();
    if (HasAuthority()) SyncLegacyQuestPresentation();
    if (HasAuthority())
    {
        const bool bWriterReady = bDevelopmentModel ? HasJournalWriterLease() : AcquireStandaloneWriterLease();
        bReady = bWriterReady && ReadSave(true);
        bServerModelInitialized = bReady;
        RefreshState();
        if (bReady) Feedback = FText::FromString(TEXT("LMB: walk / target attack   Ctrl+LMB: jump   F1-F10: assigned actions   RMB: loot / selected skill   I: bag   Progress saves automatically"));
    }
}
void AJadeboundProgressionActor::EndPlay(const EEndPlayReason::Type Reason)
{
    bPersistenceEndingPlay = true;
    CancelRecipientAttack();
    if (APawn* Bound = ReviveDamagePawn.Get())
        Bound->OnTakeAnyDamage.RemoveDynamic(this, &AJadeboundProgressionActor::OnReviveDamage);
    ReviveDamagePawn.Reset();
    // Never restore input during world teardown; no legacy delegate was removed.
    RestoreReviveInput(Reason == EEndPlayReason::Destroyed && GetWorld() && !GetWorld()->bIsTearingDown);
    if (Reason == EEndPlayReason::Destroyed && IsValid(ReviveSpirit)) ReviveSpirit->DestroyComponent();
    ReviveSpirit = nullptr;
    if (NativeCapture) NativeCapture->Stop(TEXT("EndPlay"));
    NativeCapture.Reset();
    InteractionHandles.Reset();
    MerchantClickMarker.Reset();
    CancelRaiderStrike(TEXT("EndPlay"), 0.0);
    if (IsValid(RaiderStrikeCue)) RaiderStrikeCue->Destroy();
    RaiderStrikeCue = nullptr;
    ShowMenu(false);
    RemoveClickWalkBindings();
    RestoreWorldCursor();
    if (auto* Gate = AttackGateComponent.Get())
        for (uint32 Handle : AttackGateBindingHandles) Gate->RemoveBindingByHandle(Handle);
    AttackGateBindingHandles.Reset();
    AttackGateComponent.Reset();
    AttackGatePawn.Reset();
    if (HudWidget.IsValid() && GetWorld() && GetWorld()->GetGameViewport()) GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(HudWidget.ToSharedRef());
    HudWidget.Reset();
    for (AActor* Visual : DropVisuals) if (IsValid(Visual)) Visual->Destroy();
    DropVisuals.Empty();
    Super::EndPlay(Reason);
    DevelopmentWriterLease.Reset();
    StandaloneWriterLease.Reset();
}
void AJadeboundProgressionActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(AJadeboundProgressionActor, State, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AJadeboundProgressionActor, Feedback, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AJadeboundProgressionActor, PersistencePresentation, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AJadeboundProgressionActor, bPersistenceBlocked, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AJadeboundProgressionActor, bUnsavedCandidate, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AJadeboundProgressionActor, bDevelopmentModel, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AJadeboundProgressionActor, bRecipientCombatEnabled, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AJadeboundProgressionActor, ServerCharacterId, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AJadeboundProgressionActor, bServerModelInitialized, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AJadeboundProgressionActor, AshwoodSpearMesh, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AJadeboundProgressionActor, MerchantPoint, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AJadeboundProgressionActor, MerchantRange, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AJadeboundProgressionActor, PickupRange, COND_OwnerOnly);
}
void AJadeboundProgressionActor::OnRepState()
{
    bReady = HasAuthority()
        ? State.Slots.Num() == static_cast<int32>(Jadebound::InventorySlotCount)
        : Jadebound::Presentation::ApplyReplicatedInventoryRevision(State);
    ApplyEquipment();
}
void AJadeboundProgressionActor::Tick(float Seconds)
{
    Super::Tick(Seconds);
    // Recover held progress before pawn callbacks can return early. At most one
    // scheduled write here; a recovery tick does not also admit new defeat loot.
    const TWeakObjectPtr<AJadeboundProgressionActor> BeforePersistence(this);
    const bool bPersistenceAttempted = TickPersistenceRetry();
    if (!BeforePersistence.IsValid() || BeforePersistence->IsActorBeingDestroyed() || bPersistenceEndingPlay) return;
    BindGameplay();
    TickRevive();
    // New journals have no pending loot and may not yet have built visuals;
    // a late initial possession still gets one marker, without a save write.
    const TWeakObjectPtr<AJadeboundProgressionActor> BeforeVisualRefresh(this);
    if (bReady && ResolveClickWalkScope() && !bMerchantMarkerAttempted) RefreshDropVisuals();
    if (!BeforeVisualRefresh.IsValid() || BeforeVisualRefresh->IsActorBeingDestroyed()) return;
    const TWeakObjectPtr<AJadeboundProgressionActor> BeforeControls(this);
    TickClickWalk();
    if (!BeforeControls.IsValid() || BeforeControls->IsActorBeingDestroyed()) return;
    const TWeakObjectPtr<AJadeboundProgressionActor> WeakSelf(this);
    TickSkills(Seconds);
    if (!WeakSelf.IsValid() || WeakSelf->IsActorBeingDestroyed()) return;
    TickRaiderStrike();
    AJadeboundProgressionActor* SurvivingSelf = WeakSelf.Get();
    // A synchronous damage callback may have ended this actor's play.
    if (!SurvivingSelf || SurvivingSelf->IsActorBeingDestroyed()) return;
    // A short press/release can be JustPressed while no longer Down. Keep the
    // close gesture blocked until a later input frame has observed its release.
    const double PointerNow = GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0;
    if (bAwaitingPointerRelease && Player && GFrameCounter > LastMenuCloseFrame &&
        FMath::IsFinite(PointerNow) && FMath::IsFinite(RevivePointerGuardUntil) &&
        PointerNow >= RevivePointerGuardUntil && !IsWorldPointerGestureActive())
        bAwaitingPointerRelease = false;
    if (HasAuthority())
    {
        if (!bPersistenceAttempted) TickRecipientCombat();
        if (!WeakSelf.IsValid() || WeakSelf->IsActorBeingDestroyed() || bPersistenceEndingPlay) return;
        if (!bPersistenceAttempted) PollDefeats();
        SyncLegacyQuestPresentation();
    }
    ApplyEquipment();
    EnsureHud();
    if (!NativeCapture) NativeCapture.Reset(new FJadeboundNativeCapture(this));
    NativeCapture->Tick();
    if (IsValid(Player) && IsValid(Player->PlayerCameraManager))
    {
        FVector Right, Up; FRotator Facing;
        if (MakeLootCameraBasis(Player->PlayerCameraManager->GetCameraRotation(), Right, Up, Facing))
            for (AActor* Visual : DropVisuals) if (IsValid(Visual) && Visual->IsA<ATextRenderActor>())
                Visual->SetActorRotation(Facing); // Rotation only; no Tick label reflow.
    }
}
void AJadeboundProgressionActor::BindGameplay()
{
    if (!Player) Player = Cast<APlayerController>(GetOwner());
    if (!Player && HasAuthority() && !bDevelopmentModel)
    {
        Player = UGameplayStatics::GetPlayerController(this, 0);
        if (Player) SetOwner(Player);
    }
    if (Player && Player->GetPawn() != Hero)
    {
        RemoveClickWalkBindings();
        RestoreWorldCursor();
        Hero = Player->GetPawn();
        WeaponComponent = nullptr;
        OriginalGlaiveMesh = nullptr;
        PublicEquipment.Reset();
        // Hide only this machine's locally controlled player's bar. The listen
        // server also owns remote models; their peer bars must remain untouched.
        if (Hero && Player && Player->IsLocalController())
        {
            TArray<USceneComponent*> Components;
            Hero->GetComponents(Components);
            for (USceneComponent* Component : Components)
                if (Component->GetName() == TEXT("Life Bar") &&
                    Component->GetClass()->GetPathName() == TEXT("/Script/UMG.WidgetComponent"))
                {
                    Component->SetVisibility(false, true);
                    Component->SetHiddenInGame(true, true);
                }
        }
    }
    if (Hero && !WeaponComponent)
    {
        TArray<UStaticMeshComponent*> Components;
        Hero->GetComponents(Components);
        for (auto* Component : Components)
            if (Component->ComponentHasTag(WeaponTag) || Component->GetName().Equals(TEXT("JadeGlaive")))
            {
                WeaponComponent = Component;
                OriginalGlaiveMesh = Component->GetStaticMesh();
                break;
            }
    }
    if (!bDevelopmentModel && (!LegacyLoop || !LegacyTarget))
    {
        for (TActorIterator<AActor> It(GetWorld()); It; ++It)
        {
            const FString Name = It->GetClass()->GetName();
            if (Name == TEXT("BP_JadeboundAreaLoop_V001_C") && !LegacyLoop)
            {
                LegacyLoop = *It;
                SeenDefeats = ReadInt(LegacyLoop, TEXT("DefeatCount"));
                AddTickPrerequisiteActor(LegacyLoop);
            }
            if (Name == TEXT("BP_GlaivePracticeTarget_V006_C") && !LegacyTarget) LegacyTarget = *It;
        }
    }
    if (Player && Player->IsLocalController() && !bBindingsInstalled)
    {
        EnableInput(Player);
        if (InputComponent)
        {
            InputComponent->BindKey(EKeys::I, IE_Pressed, this, &AJadeboundProgressionActor::ToggleMenu);
            InputComponent->BindKey(EKeys::E, IE_Pressed, this, &AJadeboundProgressionActor::HandleInteract);
            // F1-F10 are the session skill slots; F11 is engine fullscreen.
            // Progression saves after changes; no manual Save/Reload hotkeys are bound.
            InputComponent->BindKey(EKeys::CapsLock, IE_Pressed, this, &AJadeboundProgressionActor::ToggleConquerWalkRun);
            InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AJadeboundProgressionActor::AttackWithoutWeaponFeedback).bConsumeInput = false;
            AttackBindingIndex = InputComponent->KeyBindings.Num() - 1;
            const FKey ManualKeys[] = {EKeys::W,EKeys::A,EKeys::S,EKeys::D,EKeys::Up,EKeys::Down,EKeys::Left,EKeys::Right,EKeys::SpaceBar};
            for (const FKey& Key : ManualKeys)
                InputComponent->BindKey(Key, IE_Pressed, this, &AJadeboundProgressionActor::HandleManualWalkKey).bConsumeInput = false;
            bBindingsInstalled = true;
        }
    }
    BindAttackActionGate();
    UpdateClickWalkBindings();
    UpdateReviveBinding();
}

// Exact retained V006 event: one AActor input, void return. The existing
// delegate supplies the destroyed pawn; no manual reflected argument buffer.
bool AJadeboundProgressionActor::HasLegacyReviveContract(const APawn* Pawn, const APlayerController* Controller) const
{
    if (!IsValid(Pawn) || !IsValid(Controller) || !Pawn->OnDestroyed.Contains(Controller, FName(TEXT("On Pawn Destroyed")))) return false;
    const UFunction* Function = Controller->FindFunction(FName(TEXT("On Pawn Destroyed")));
    if (!Function || Function->NumParms != 1) return false;
    int32 Inputs = 0;
    for (TFieldIterator<FProperty> It(Function); It; ++It)
        if (It->HasAnyPropertyFlags(CPF_Parm))
        {
            const auto* Object = CastField<FObjectPropertyBase>(*It);
            if (!Object || Object->PropertyClass != AActor::StaticClass() ||
                It->HasAnyPropertyFlags(CPF_ReturnParm | CPF_OutParm | CPF_ReferenceParm)) return false;
            ++Inputs;
        }
    const auto* Delay = Field<FFloatProperty>(const_cast<APawn*>(Pawn), TEXT("Respawn Time"));
    return Inputs == 1 && Delay && FMath::IsFinite(Delay->GetPropertyValue_InContainer(Pawn)) &&
        Delay->GetPropertyValue_InContainer(Pawn) == 3.f;
}

void AJadeboundProgressionActor::UpdateReviveBinding()
{
    APawn* Desired = ResolveClickWalkScope();
    if (ReviveDamagePawn.Get() == Desired) return;
    if (APawn* Bound = ReviveDamagePawn.Get())
        Bound->OnTakeAnyDamage.RemoveDynamic(this, &AJadeboundProgressionActor::OnReviveDamage);
    ReviveDamagePawn = Desired;
    if (Desired) Desired->OnTakeAnyDamage.AddUniqueDynamic(this, &AJadeboundProgressionActor::OnReviveDamage);
}

void AJadeboundProgressionActor::OnReviveDamage(AActor* DamagedActor, float, const UDamageType*, AController*, AActor*)
{
    APawn* Pawn = Cast<APawn>(DamagedActor);
    if (bReviveWaiting || !Pawn || Pawn != ReviveDamagePawn.Get() || Pawn != ResolveClickWalkScope() ||
        !GetWorld() || GetWorld()->IsPaused() || ReviveGeneration == MAX_uint64) return;
    const auto Health = ReadHealthSample(Pawn, TEXT("Current HP"), TEXT("Max HP"));
    if (!Health.bAvailable || Health.Current > 0.0) return;
    const double Now = GetWorld()->GetTimeSeconds();
    const FVector Fall = Pawn->GetActorLocation();
    const int32 LatentCount = GetWorld()->GetLatentActionManager().GetNumActionsForObject(Pawn);
    if (!FMath::IsFinite(Now) || !FMath::IsFinite(Now + 10.0) || Fall.ContainsNaN() ||
        !HasLegacyReviveContract(Pawn, Player) || LatentCount < 1)
    {
        UE_LOG(LogTemp, Error, TEXT("Jadebound SpiritRevive contract unavailable; legacy death path retained."));
        return;
    }
    // ReceiveAnyDamage/HandleDeath has already synchronously set ragdoll/camera
    // and registered RetriggerableDelay. Queue aborts for this exact corpse only.
    // This aborts ALL its pending latent actions; no global timers are cleared.
    GetWorld()->GetLatentActionManager().RemoveActionsForObject(Pawn);
    ShowMenu(false);
    RestoreWorldCursor();
    bReviveWaiting = true;
    bReviveRequested = false;
    bReviveHeld = false;
    bReviveSpiritAttempted = false;
    bReviveReadyAnnounced = false;
    ++ReviveGeneration;
    ReviveCorpse = Pawn;
    ReviveController = Player;
    ReviveDeathAt = Now;
    ReviveReadyAt = Now + 10.0; // game time: pause does not advance this gate
    ReviveFallPoint = Fall;
    ReviveMessage = FText::FromString(TEXT("Your spirit is gathering. Wait for the countdown to finish."));
    Feedback = ReviveMessage;
    StopClickWalk(TEXT("Defeated"), false);
    CancelLegacyAttackPresentation();
    CancelRaiderStrike(TEXT("Defeated"), Now);
    bReviveSavedCursor = Player->ShouldShowMouseCursor();
    bReviveMoveLease = !Player->IsMoveInputIgnored();
    bReviveLookLease = !Player->IsLookInputIgnored();
    if (bReviveMoveLease) Player->SetIgnoreMoveInput(true);
    if (bReviveLookLease) Player->SetIgnoreLookInput(true);
    Player->SetInputMode(FInputModeUIOnly()); // Root modal supplies Slate focus.
    Player->SetShowMouseCursor(true);
    ApplyEquipment();
    UE_LOG(LogTemp, Display, TEXT("Jadebound SpiritRevive waiting generation=%llu pawn=%s latentAbortCount=%d readyAt=%.3f"),
        static_cast<unsigned long long>(ReviveGeneration), *Pawn->GetPathName(), LatentCount, ReviveReadyAt);
}

FJadeboundRevivePresentation AJadeboundProgressionActor::GetRevivePresentation() const
{
    FJadeboundRevivePresentation View;
    View.bWaiting = bReviveWaiting;
    View.Generation = ReviveGeneration;
    View.Message = ReviveMessage;
    if (!bReviveWaiting || !GetWorld()) return View;
    const double Now = GetWorld()->GetTimeSeconds();
    if (!FMath::IsFinite(Now) || !FMath::IsFinite(ReviveReadyAt)) return View;
    View.SecondsRemaining = static_cast<int32>(FMath::CeilToDouble(FMath::Clamp(ReviveReadyAt - Now, 0.0, 10.0)));
    APawn* Corpse = ReviveCorpse.Get();
    View.bCanRevive = !bReviveRequested && !bReviveHeld && Now >= ReviveReadyAt && !GetWorld()->IsPaused() &&
        Corpse && Corpse == ResolveClickWalkScope() && ReviveController.Get() == Player &&
        HasLegacyReviveContract(Corpse, Player) && GetWorld()->GetLatentActionManager().GetNumActionsForObject(Corpse) == 0;
    return View;
}

bool AJadeboundProgressionActor::RequestRevive(uint64 ExpectedGeneration)
{
    const auto View = GetRevivePresentation();
    if (!View.bWaiting || !View.bCanRevive || ExpectedGeneration != View.Generation) return false;
    APawn* Corpse = ReviveCorpse.Get();
    const auto Health = ReadHealthSample(Corpse, TEXT("Current HP"), TEXT("Max HP"));
    if (!Health.bAvailable || Health.Current > 0.0) return false;
    bReviveRequested = true; // latch BEFORE synchronous legacy spawn/possession
    ReviveMessage = FText::FromString(TEXT("Returning to your body..."));
    const TWeakObjectPtr<AJadeboundProgressionActor> Self(this);
    const bool Destroyed = Corpse->Destroy();
    if (!Self.IsValid() || Self->IsActorBeingDestroyed()) return false;
    if (!Destroyed)
    {
        bReviveHeld = true;
        ReviveMessage = FText::FromString(TEXT("Revive could not complete. This attempt is held."));
        UE_LOG(LogTemp, Error, TEXT("Jadebound SpiritRevive Destroy refused; no retry or fallback spawn."));
        return false;
    }
    BindGameplay();
    TickRevive();
    return !bReviveWaiting;
}

void AJadeboundProgressionActor::RestoreReviveInput(bool bApplyToLiveWorld)
{
    APlayerController* Controller = ReviveController.Get();
    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0;
    const double GuardUntil = Now + 0.55;
    if (bApplyToLiveWorld && IsValid(Controller) && Controller == Player && Controller->GetWorld() == GetWorld() &&
        FMath::IsFinite(Now) && Now >= 0.0 && FMath::IsFinite(GuardUntil))
    {
        if (bReviveMoveLease) Controller->SetIgnoreMoveInput(false);
        if (bReviveLookLease) Controller->SetIgnoreLookInput(false);
        Controller->SetInputMode(FInputModeGameOnly());
        Controller->SetShowMouseCursor(bReviveSavedCursor);
        RevivePointerGuardUntil = GuardUntil;
        LastMenuCloseFrame = GFrameCounter;
        bAwaitingPointerRelease = true;
    }
    bReviveMoveLease = false;
    bReviveLookLease = false;
}

void AJadeboundProgressionActor::TickRevive()
{
    if (!bReviveWaiting || !GetWorld()) return;
    if (bReviveRequested)
    {
        APawn* Replacement = ResolveClickWalkScope();
        const auto Health = ReadHealthSample(Replacement, TEXT("Current HP"), TEXT("Max HP"));
        const double RestoreNow = GetWorld()->GetTimeSeconds();
        if (Replacement && Replacement != ReviveCorpse.Get() && ReviveController.Get() == Player &&
            Health.bAvailable && Health.Current > 0.0 && !Replacement->IsActorBeingDestroyed() &&
            FMath::IsFinite(RestoreNow) && RestoreNow >= 0.0 && FMath::IsFinite(RestoreNow + 0.55))
        {
            RestoreReviveInput(true);
            bReviveWaiting = false;
            bReviveHeld = false;
            if (IsValid(ReviveSpirit)) ReviveSpirit->DestroyComponent();
            ReviveSpirit = nullptr;
            ReviveCorpse.Reset(); ReviveController.Reset();
            ReviveMessage = FText::FromString(TEXT("Revived."));
            Feedback = ReviveMessage;
            ApplyEquipment(); UpdateWorldCursor();
            UE_LOG(LogTemp, Display, TEXT("Jadebound SpiritRevive complete generation=%llu pawn=%s"),
                static_cast<unsigned long long>(ReviveGeneration), *Replacement->GetPathName());
        }
        else if (!bReviveHeld)
        {
            bReviveHeld = true;
            ReviveMessage = FText::FromString(TEXT("The replacement body was not verified. Revive is held."));
            UE_LOG(LogTemp, Error, TEXT("Jadebound SpiritRevive replacement not verified; no retry or fallback."));
        }
        return;
    }
    APawn* Corpse = ReviveCorpse.Get();
    if (!Corpse || Corpse != ResolveClickWalkScope() || ReviveController.Get() != Player)
    {
        if (!bReviveHeld) UE_LOG(LogTemp, Error, TEXT("Jadebound SpiritRevive corpse/context lost; held."));
        bReviveHeld = true;
        ReviveMessage = FText::FromString(TEXT("Your fallen body is unavailable. Revive is held."));
        return;
    }
    if (!bReviveReadyAnnounced && GetRevivePresentation().bCanRevive)
    {
        bReviveReadyAnnounced = true;
        ReviveMessage = FText::FromString(TEXT("Choose Revive to return to the courtyard."));
        Feedback = ReviveMessage;
    }
    if (!bReviveSpiritAttempted)
    {
        bReviveSpiritAttempted = true;
        UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr,
            TEXT("/Game/Jadebound/Effects/SpiritReviveV002/SM_JadeSpirit_v002.SM_JadeSpirit_v002"));
        if (IsValid(Mesh))
        {
            ReviveSpirit = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
            AddInstanceComponent(ReviveSpirit);
            ReviveSpirit->SetMobility(EComponentMobility::Movable);
            ReviveSpirit->SetStaticMesh(Mesh); // keep THREE assigned original materials
            ReviveSpirit->SetWorldScale3D(FVector(1.65));
            ReviveSpirit->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            ReviveSpirit->SetCollisionResponseToAllChannels(ECR_Ignore);
            ReviveSpirit->SetGenerateOverlapEvents(false);
            ReviveSpirit->SetCanEverAffectNavigation(false);
            ReviveSpirit->SetCastShadow(false);
            ReviveSpirit->RegisterComponent();
        }
        else UE_LOG(LogTemp, Warning, TEXT("Jadebound SpiritRevive original spirit mesh unavailable; no substituted art."));
    }
    if (IsValid(ReviveSpirit))
    {
        FVector Anchor = ReviveFallPoint + FVector(0.0, 0.0, 57.0);
        if (const auto* Character = Cast<ACharacter>(Corpse))
            if (const auto* Mesh = Character->GetMesh(); IsValid(Mesh) && Mesh->IsRegistered() &&
                !Mesh->Bounds.Origin.ContainsNaN() && !Mesh->Bounds.BoxExtent.ContainsNaN() &&
                Mesh->Bounds.BoxExtent.GetMax() < 1000.0)
                Anchor = Mesh->Bounds.Origin + FVector(0.0, 0.0, Mesh->Bounds.BoxExtent.Z + 24.0);
        const double Now = GetWorld()->GetTimeSeconds();
        if (!Anchor.ContainsNaN() && FMath::IsFinite(Now))
        {
            ReviveSpirit->SetWorldLocation(Anchor + FVector(0.0, 0.0, 2.5 * FMath::Sin((Now - ReviveDeathAt) * 2.0)));
            if (IsValid(Player))
            {
                FVector CameraPosition; FRotator CameraRotation;
                Player->GetPlayerViewPoint(CameraPosition, CameraRotation);
                FVector ToCamera = CameraPosition - Anchor;
                ToCamera.Z = 0.0;
                const double PlanarDistanceSquared = ToCamera.SizeSquared();
                if (!CameraPosition.ContainsNaN() && !ToCamera.ContainsNaN() &&
                    FMath::IsFinite(PlanarDistanceSquared) && PlanarDistanceSquared > 1.0)
                {
                    // Imported spirit front is local +Y; keep it upright and face the camera.
                    const double Yaw = ToCamera.Rotation().Yaw - 90.0;
                    if (FMath::IsFinite(Yaw)) ReviveSpirit->SetWorldRotation(FRotator(0.0, Yaw, 0.0));
                }
            }
        }
    }
}

bool AJadeboundProgressionActor::ResolveRaiderStrikeContext(ACharacter*& Pawn, ACharacter*& Target,
    int32& LifeGeneration, FName& Reason) const
{
    Pawn = nullptr; Target = nullptr; LifeGeneration = -1;
    const UWorld* World = GetWorld();
    if (bReviveWaiting || !World || World->IsPaused() || bDevelopmentModel || !HasAuthority() || GetNetMode() != NM_Standalone)
    { Reason = TEXT("WrongContext"); return false; }
    const AGameModeBase* Mode = World->GetAuthGameMode();
    if (!IsValid(Mode) || Mode->GetClass()->GetPathName() !=
        TEXT("/Game/Jadebound/Tests/GlaiveCombatV006/BP_GlaiveMode_V006.BP_GlaiveMode_V006_C") ||
        !IsValid(Player) || !Player->IsLocalController() || GetOwner() != Player ||
        Player->GetClass()->GetPathName() !=
        TEXT("/Game/Jadebound/Tests/GlaiveCombatV006/BP_GlaiveController_V006.BP_GlaiveController_V006_C"))
    { Reason = TEXT("WrongOwnerOrMode"); return false; }
    Pawn = Cast<ACharacter>(Hero);
    Target = Cast<ACharacter>(LegacyTarget);
    if (!IsValid(Pawn) || Player->GetPawn() != Pawn || Pawn->GetController() != Player ||
        Pawn->GetClass()->GetPathName() !=
        TEXT("/Game/Jadebound/Tests/GlaiveCombatV006/BP_GlaiveHero_V006.BP_GlaiveHero_V006_C"))
    { Reason = TEXT("WrongPawn"); return false; }
    const FJadeboundHealthSample Health = ReadHealthSample(Pawn, TEXT("Current HP"), TEXT("Max HP"));
    if (!Health.bAvailable || Health.Current <= 0.0 || Pawn->IsHidden() || !Pawn->CanBeDamaged())
    { Reason = TEXT("HeroNotLiving"); return false; }
    if (!IsValid(Target) || Target->GetClass()->GetPathName() !=
        TEXT("/Game/Jadebound/Tests/GlaiveCombatV006/BP_GlaivePracticeTarget_V006.BP_GlaivePracticeTarget_V006_C") ||
        !IsValid(LegacyLoop) || LegacyLoop->GetClass()->GetPathName() !=
        TEXT("/Game/Jadebound/Playable/AreaV001/BP_JadeboundAreaLoop_V001.BP_JadeboundAreaLoop_V001_C") ||
        ReadObject(LegacyLoop, TEXT("Target")) != Target)
    { Reason = TEXT("WrongTargetOrLoop"); return false; }
    const auto* Life = Field<FIntProperty>(LegacyLoop, TEXT("LifeState"));
    const auto* Generation = Field<FIntProperty>(LegacyLoop, TEXT("RespawnCount"));
    const auto* Home = Field<FStructProperty>(LegacyLoop, TEXT("TargetHome"));
    const auto* Enabled = Field<FBoolProperty>(Target, TEXT("CombatEnabled"));
    const FJadeboundHealthSample TargetHealth = ReadHealthSample(Target, TEXT("CombatHealth"), TEXT("CombatMaxHealth"));
    if (!Life || !Generation || !Home || Home->Struct != TBaseStructure<FVector>::Get() || !Enabled)
    { Reason = TEXT("LifeFieldsUnavailable"); return false; }
    LifeGeneration = Generation->GetPropertyValue_InContainer(LegacyLoop);
    if (LifeGeneration < 0 || Life->GetPropertyValue_InContainer(LegacyLoop) != 0 ||
        !Enabled->GetPropertyValue_InContainer(Target) || !TargetHealth.bAvailable || TargetHealth.Current <= 0.0 ||
        Target->IsHidden() || !Target->GetActorEnableCollision())
    { Reason = TEXT("TargetNotLiving"); return false; }
    const FVector HomePoint = *Home->ContainerPtrToValuePtr<FVector>(LegacyLoop);
    const FVector TargetPoint = Target->GetActorLocation();
    const FVector PawnPoint = Pawn->GetActorLocation();
    if (HomePoint.ContainsNaN() || TargetPoint.ContainsNaN() || PawnPoint.ContainsNaN() ||
        FVector::Dist2D(HomePoint, TargetPoint) > 185.0 || FVector::Dist2D(TargetPoint, PawnPoint) >= 400.0)
    { Reason = TEXT("LeashRejected"); return false; }
    // Fail closed if a second progression model could schedule this same strike.
    // No shared mutable lease, another chase writer, or target Blueprint marker.
    for (TActorIterator<AJadeboundProgressionActor> It(GetWorld()); It; ++It)
        if (*It != this && !It->bDevelopmentModel && It->Player == Player && It->LegacyTarget == Target)
        { Reason = TEXT("DuplicateStrikeModel"); return false; }
    Reason = NAME_None;
    return true;
}

bool AJadeboundProgressionActor::ValidateRaiderStrikeGeometry(const ACharacter* Pawn,
    const ACharacter* Target, const FVector& Aim, FName& Reason) const
{
    if (!IsValid(Pawn) || !IsValid(Target) || !GetWorld())
    { Reason = TEXT("GeometryContextLost"); return false; }
    const FVector Start = Target->GetActorLocation();
    const FVector End = Pawn->GetActorLocation();
    FVector Offset = End - Start;
    if (Start.ContainsNaN() || End.ContainsNaN() || Aim.ContainsNaN() ||
        FMath::Abs(Offset.Z) > RaiderVerticalReachCm)
    { Reason = TEXT("VerticalReachRejected"); return false; }
    Offset.Z = 0.0;
    const double DistanceSquared = Offset.SizeSquared();
    if (!FMath::IsFinite(DistanceSquared) || DistanceSquared <= 1.0 || DistanceSquared > FMath::Square(RaiderReachCm))
    { Reason = TEXT("ReachRejected"); return false; }
    const FVector Direction = Offset.GetSafeNormal();
    const FVector Facing = Target->GetActorForwardVector().GetSafeNormal2D();
    if (Aim.IsNearlyZero() || Facing.ContainsNaN() || Facing.IsNearlyZero() ||
        FVector::DotProduct(Aim, Direction) < RaiderFacingDot ||
        FVector::DotProduct(Facing, Direction) < RaiderFacingDot)
    { Reason = TEXT("FacingRejected"); return false; }
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_WorldStatic);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(JadeboundRaiderStrike), false);
    Query.AddIgnoredActor(Target);
    Query.AddIgnoredActor(Pawn);
    Query.AddIgnoredActor(this);
    FHitResult Obstruction;
    // Capsule-center line, with the same static-world obstruction class used by
    // the legacy player swing. Dynamic actors are outside this first slice.
    if (GetWorld()->LineTraceSingleByObjectType(Obstruction, Start, End, Objects, Query))
    { Reason = TEXT("ObstructionRejected"); return false; }
    Reason = NAME_None;
    return true;
}

bool AJadeboundProgressionActor::UpdateRaiderStrikeCue(const ACharacter* Target, const TCHAR* Text, FColor Color)
{
    if (!IsValid(Target) || !IsValid(Player) || !Player->PlayerCameraManager || !GetWorld()) return false;
    const auto* Capsule = Target->GetCapsuleComponent();
    if (!Capsule) return false;
    const FVector Location = Target->GetActorLocation() + FVector(0.0, 0.0, Capsule->GetScaledCapsuleHalfHeight() + 70.0);
    const FVector Camera = Player->PlayerCameraManager->GetCameraLocation();
    if (Location.ContainsNaN() || Camera.ContainsNaN()) return false;
    if (!IsValid(RaiderStrikeCue))
    {
        FActorSpawnParameters Spawn;
        Spawn.Owner = this;
        Spawn.ObjectFlags = RF_Transient;
        Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        RaiderStrikeCue = GetWorld()->SpawnActor<ATextRenderActor>(Location, FRotator::ZeroRotator, Spawn);
        if (!IsValid(RaiderStrikeCue)) return false;
        RaiderStrikeCue->SetReplicates(false);
        RaiderStrikeCue->SetActorEnableCollision(false);
        RaiderStrikeCue->SetActorTickEnabled(false);
        auto* Render = RaiderStrikeCue->GetTextRender();
        if (!Render) { RaiderStrikeCue->Destroy(); RaiderStrikeCue = nullptr; return false; }
        Render->SetHorizontalAlignment(EHTA_Center);
        Render->SetVerticalAlignment(EVRTA_TextCenter);
        Render->SetWorldSize(20.f);
    }
    auto* Render = RaiderStrikeCue->GetTextRender();
    if (!Render || RaiderStrikeCue->GetOwner() != this) return false;
    Render->SetText(FText::FromString(Text));
    Render->SetTextRenderColor(Color);
    RaiderStrikeCue->SetActorLocation(Location);
    RaiderStrikeCue->SetActorRotation((Camera - Location).Rotation());
    RaiderStrikeCue->SetActorHiddenInGame(false);
    return true;
}

void AJadeboundProgressionActor::CancelRaiderStrike(FName Reason, double Now)
{
    if (RaiderStrikePhase == 1 && RaiderStrikeCancellations < MAX_int64) ++RaiderStrikeCancellations;
    RaiderStrikePhase = 0;
    RaiderStrikeStopReason = Reason;
    RaiderStrikePawn.Reset(); RaiderStrikeTarget.Reset(); RaiderStrikeLoop.Reset();
    RaiderStrikeLifeGeneration = -1;
    RaiderStrikeAim = FVector::ZeroVector;
    bRaiderImpactLatched = false;
    RaiderStrikePhaseEndsAt = 0.0;
    if (FMath::IsFinite(Now)) RaiderNextStrikeAt = Now + RaiderRecoverySeconds;
    if (IsValid(RaiderStrikeCue) && RaiderStrikeCue->GetOwner() == this) RaiderStrikeCue->SetActorHiddenInGame(true);
}

void AJadeboundProgressionActor::TickRaiderStrike()
{
    // No combat in the development multiplayer models, no client entry point.
    UWorld* World = GetWorld();
    const double Now = World ? World->GetTimeSeconds() : 0.0;
    ACharacter* Pawn = nullptr;
    ACharacter* Target = nullptr;
    int32 LifeGeneration = -1;
    FName Reason;
    if (!FMath::IsFinite(Now) || !ResolveRaiderStrikeContext(Pawn, Target, LifeGeneration, Reason))
    {
        if (RaiderStrikePhase != 0) CancelRaiderStrike(FMath::IsFinite(Now) ? Reason : FName(TEXT("InvalidClock")), Now);
        else RaiderStrikeStopReason = FMath::IsFinite(Now) ? Reason : FName(TEXT("InvalidClock"));
        return;
    }
    if (RaiderStrikePhase != 0 && (RaiderStrikePawn.Get() != Pawn || RaiderStrikeTarget.Get() != Target ||
        RaiderStrikeLoop.Get() != LegacyLoop || RaiderStrikeLifeGeneration != LifeGeneration))
    { CancelRaiderStrike(TEXT("LifeOrPawnChanged"), Now); return; }
    if (RaiderStrikePhase == 2)
    {
        if (IsValid(RaiderStrikeCue) && Now >= RaiderStrikePhaseEndsAt - RaiderRecoverySeconds + .16)
            RaiderStrikeCue->SetActorHiddenInGame(true);
        if (Now >= RaiderStrikePhaseEndsAt)
        {
            RaiderStrikePhase = 0;
            RaiderStrikePawn.Reset(); RaiderStrikeTarget.Reset(); RaiderStrikeLoop.Reset();
            RaiderStrikeLifeGeneration = -1;
        }
        return; // no catch-up attack or second impact in this frame
    }
    if (RaiderStrikePhase == 0)
    {
        if (Now < RaiderNextStrikeAt) return;
        const FVector Aim = Target->GetActorForwardVector().GetSafeNormal2D();
        if (!ValidateRaiderStrikeGeometry(Pawn, Target, Aim, Reason)) { RaiderStrikeStopReason = Reason; return; }
        if (RaiderStrikeId == MAX_int64 || RaiderStrikeDamageAttempts == MAX_int64)
        { RaiderStrikeStopReason = TEXT("StrikeCounterExhausted"); return; }
        // A strike cannot start if its readable provisional cue cannot exist.
        if (!UpdateRaiderStrikeCue(Target, TEXT("STRIKE"), FColor(255, 189, 83)))
        { CancelRaiderStrike(TEXT("CueUnavailable"), Now); return; }
        ++RaiderStrikeId;
        RaiderStrikePawn = Pawn; RaiderStrikeTarget = Target; RaiderStrikeLoop = LegacyLoop;
        RaiderStrikeLifeGeneration = LifeGeneration;
        RaiderStrikeAim = Aim;
        bRaiderImpactLatched = false;
        RaiderStrikePhase = 1;
        RaiderStrikePhaseEndsAt = Now + RaiderWindupSeconds;
        RaiderStrikeStopReason = TEXT("Windup");
        return;
    }
    if (RaiderStrikePhase != 1 || Now > RaiderStrikePhaseEndsAt + RaiderMaximumLateImpactSeconds)
    { CancelRaiderStrike(TEXT("StaleWindup"), Now); return; }
    if (!ValidateRaiderStrikeGeometry(Pawn, Target, RaiderStrikeAim, Reason))
    { CancelRaiderStrike(Reason, Now); return; }
    if (!UpdateRaiderStrikeCue(Target, TEXT("STRIKE"), FColor(255, 189, 83)))
    { CancelRaiderStrike(TEXT("CueUnavailable"), Now); return; }
    if (Now < RaiderStrikePhaseEndsAt) return;
    // Re-read every admission and captured life immediately before synchronous
    // damage; never retarget a windup to a replacement pawn or enemy life.
    if (!ResolveRaiderStrikeContext(Pawn, Target, LifeGeneration, Reason) ||
        RaiderStrikePawn.Get() != Pawn || RaiderStrikeTarget.Get() != Target || RaiderStrikeLoop.Get() != LegacyLoop ||
        RaiderStrikeLifeGeneration != LifeGeneration || !ValidateRaiderStrikeGeometry(Pawn, Target, RaiderStrikeAim, Reason))
    { CancelRaiderStrike(Reason.IsNone() ? FName(TEXT("LifeOrPawnChanged")) : Reason, Now); return; }
    if (bRaiderImpactLatched || RaiderLastImpactStrikeId == RaiderStrikeId || RaiderStrikeDamageAttempts == MAX_int64)
    { CancelRaiderStrike(TEXT("RepeatedImpactRejected"), Now); return; }
    const FJadeboundHealthSample Before = ReadHealthSample(Pawn, TEXT("Current HP"), TEXT("Max HP"));
    if (!Before.bAvailable || Before.Current <= 0.0)
    { CancelRaiderStrike(TEXT("HeroNotLiving"), Now); return; }
    bRaiderImpactLatched = true;
    RaiderLastImpactStrikeId = RaiderStrikeId;
    RaiderLastImpactLifeGeneration = LifeGeneration;
    RaiderLastImpactWorldSeconds = Now;
    ++RaiderStrikeDamageAttempts;
    RaiderLastDamagePawnPath = Pawn->GetPathName();
    RaiderLastDamageTargetPath = Target->GetPathName();
    RaiderLastHeroHealthBefore = Before.Current;
    RaiderStrikePhase = 2; // ledger and cooldown precede reentrant damage delivery
    RaiderStrikePhaseEndsAt = Now + RaiderRecoverySeconds;
    RaiderNextStrikeAt = RaiderStrikePhaseEndsAt;
    const TWeakObjectPtr<AJadeboundProgressionActor> WeakSelf(this);
    const TWeakObjectPtr<ACharacter> WeakPawn(Pawn);
    const TWeakObjectPtr<ACharacter> WeakTarget(Target);
    const float ImpactDamage = GuardDamageForImpact(Pawn, Target, RaiderDamage);
    const float ReturnedDamage = UGameplayStatics::ApplyDamage(Pawn, ImpactDamage, Target->GetController(), Target, nullptr);
    // Damage callbacks may synchronously destroy any participant. No member
    // write or raw Pawn/Target access survives that call without weak rebind.
    AJadeboundProgressionActor* SurvivingSelf = WeakSelf.Get();
    if (!SurvivingSelf || SurvivingSelf->IsActorBeingDestroyed()) return;
    SurvivingSelf->RaiderLastDamageReturned = ReturnedDamage;
    const FJadeboundHealthSample After = ReadHealthSample(WeakPawn.Get(), TEXT("Current HP"), TEXT("Max HP"));
    SurvivingSelf->bRaiderLastHealthAfterAvailable = After.bAvailable;
    SurvivingSelf->RaiderLastHeroHealthAfter = After.bAvailable ? After.Current : 0.0;
    const bool bObservedHit = After.bAvailable && After.Current < Before.Current;
    if (bObservedHit && SurvivingSelf->RaiderStrikeObservedHits < MAX_int64) ++SurvivingSelf->RaiderStrikeObservedHits;
    SurvivingSelf->RaiderStrikeStopReason = bObservedHit ? FName(TEXT("ObservedHit")) : FName(TEXT("DamageUnobserved"));
    SurvivingSelf->UpdateRaiderStrikeCue(WeakTarget.Get(), bObservedHit ? TEXT("HIT") : TEXT("MISS"), FColor(243, 83, 70));
}

// This control slice is deliberately absent from the development/network model.
ACharacter* AJadeboundProgressionActor::ResolveClickWalkScope() const
{
    if (bDevelopmentModel || GetNetMode() != NM_Standalone || !HasAuthority() ||
        !IsValid(Player) || !Player->IsLocalController() || GetOwner() != Player ||
        !IsValid(Hero) || Player->GetPawn() != Hero || Hero->GetController() != Player ||
        Hero->GetClass()->GetPathName() != TEXT("/Game/Jadebound/Tests/GlaiveCombatV006/BP_GlaiveHero_V006.BP_GlaiveHero_V006_C") ||
        !GetWorld() || !GetWorld()->IsGameWorld() || GetWorld()->WorldType == EWorldType::PIE ||
        GetWorld()->GetMapName() != TEXT("L_JadeboundProgression_AreaV002") ||
        Player->GetClass()->GetPathName() != TEXT("/Game/Jadebound/Tests/GlaiveCombatV006/BP_GlaiveController_V006.BP_GlaiveController_V006_C") ||
        !GetWorld()->GetAuthGameMode() || GetWorld()->GetAuthGameMode()->GetClass()->GetPathName() !=
            TEXT("/Game/Jadebound/Tests/GlaiveCombatV006/BP_GlaiveMode_V006.BP_GlaiveMode_V006_C")) return nullptr;
    int32 Models = 0;
    for (TActorIterator<AJadeboundProgressionActor> It(GetWorld()); It; ++It) ++Models;
    if (Models != 1 || GetWorld()->GetNumPlayerControllers() != 1) return nullptr;
    return Cast<ACharacter>(Hero);
}
ACharacter* AJadeboundProgressionActor::ResolveClickWalkCharacter() const
{
    ACharacter* Character = ResolveClickWalkScope();
    if (!Character || !HasLivingPossessedHero() || Character->IsHidden() ||
        !GetWorld() || GetWorld()->IsPaused()) return nullptr;
    const auto* Movement = Character->GetCharacterMovement();
    const auto* Capsule = Character->GetCapsuleComponent();
    const auto* Async = IConsoleManager::Get().FindConsoleVariable(TEXT("p.AsyncCharacterMovement"));
    if (!Movement || !Capsule || !Movement->IsActive() || !Movement->IsComponentTickEnabled() ||
        Movement->bUpdateOnlyIfRendered || !Async || Async->GetInt() != 0 ||
        Movement->MovementMode != MOVE_Walking || !Movement->CurrentFloor.IsWalkableFloor() ||
        Movement->UpdatedComponent != Capsule || Capsule->IsSimulatingPhysics() ||
        !Capsule->IsQueryCollisionEnabled() || Character->IsMoveInputIgnored() ||
        !FMath::IsFinite(Movement->MaxStepHeight) || Movement->MaxStepHeight < 0.f ||
        Character->GetActorLocation().ContainsNaN()) return nullptr;
    return Character;
}
bool AJadeboundProgressionActor::IsClickWalkAvailable() const
{
    if (!ResolveClickWalkCharacter() || bMenuOpen || bAwaitingPointerRelease ||
        IsCloseFrameGuardActive() || !ClickWalkGate.IsValid() || ClickWalkGate.Get() != InputComponent || ClickWalkGatePawn.Get() != Hero ||
        !ClickWalkSubsystem.IsValid() || !ClickWalkAction || !PointerMotionAction || !SelectedSkillAction) return false;
    const auto* Input = Cast<UEnhancedPlayerInput>(Player->PlayerInput);
    if (!Input) return false;
    bool bOwnLeft = false, bOwnRight = false, bOwnMotion = false;
    for (const FEnhancedActionKeyMapping& Mapping : Input->GetEnhancedActionMappingsView())
    {
        if (Mapping.Key == EKeys::LeftMouseButton)
        {
            if (Mapping.Action != ClickWalkAction) return false;
            bOwnLeft = true;
        }
        if (Mapping.Key == EKeys::RightMouseButton)
        {
            if (Mapping.Action != SelectedSkillAction) return false;
            bOwnRight = true;
        }
        if (Mapping.Key == EKeys::Mouse2D)
        {
            if (Mapping.Action != PointerMotionAction) return false;
            bOwnMotion = true;
        }
    }
    return bOwnLeft && bOwnRight && bOwnMotion;
}
bool AJadeboundProgressionActor::IsManualWalkInputActive() const
{
    if (!Player) return true;
    const FKey Keys[] = {EKeys::W,EKeys::A,EKeys::S,EKeys::D,EKeys::Up,EKeys::Down,EKeys::Left,EKeys::Right,
        EKeys::SpaceBar,EKeys::E,EKeys::I};
    for (const FKey& Key : Keys)
        if (Player->IsInputKeyDown(Key) || Player->WasInputKeyJustPressed(Key)) return true;
    return false;
}
void AJadeboundProgressionActor::RemoveClickWalkBindings()
{
    StopClickWalk(TEXT("OwnerChanged"), false);
    RestoreConquerMovementPolicy();
    if (auto* Gate = ClickWalkGate.Get())
        for (uint32 Handle : ClickWalkBindingHandles) Gate->RemoveBindingByHandle(Handle);
    ClickWalkBindingHandles.Reset();
    ClickWalkGate.Reset();
    ClickWalkGatePawn.Reset();
    if (auto* Subsystem = ClickWalkSubsystem.Get())
        if (ClickWalkContext) Subsystem->RemoveMappingContext(ClickWalkContext);
    ClickWalkSubsystem.Reset();
}
void AJadeboundProgressionActor::UpdateClickWalkBindings()
{
    ACharacter* Character = ResolveClickWalkScope();
    auto* Gate = Cast<UEnhancedInputComponent>(InputComponent);
    auto* Local = Player ? Player->GetLocalPlayer() : nullptr;
    auto* Subsystem = Local ? Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
    if (!Character || !Gate || !Subsystem)
    {
        if (ClickWalkSubsystem.IsValid() || ClickWalkGate.IsValid()) RemoveClickWalkBindings();
        RestoreWorldCursor();
        return;
    }
    if (!bSkillHotbarLoaded) LoadLocalSkillHotbar();
    if (ClickWalkGate.Get() == Gate && ClickWalkGatePawn.Get() == Character &&
        ClickWalkSubsystem.Get() == Subsystem) { UpdateWorldCursor(); return; }
    RemoveClickWalkBindings();
    RestoreWorldCursor();
    const auto* HeroInput = Cast<UEnhancedInputComponent>(Character->InputComponent);
    const UInputAction* Move = nullptr;
    const UInputAction* Jump = nullptr;
    const UInputAction* Charged = nullptr;
    if (HeroInput)
        for (const auto& Binding : HeroInput->GetActionEventBindings())
            if (Binding && Binding->IsBoundToObject(Character) && Binding->GetAction())
            {
                const FString Path = Binding->GetAction()->GetPathName();
                if (Path == TEXT("/Game/Input/Actions/IA_Move.IA_Move")) Move = Binding->GetAction();
                if (Path == TEXT("/Game/Input/Actions/IA_Jump.IA_Jump")) Jump = Binding->GetAction();
                if (Path == TEXT("/Game/Variant_Combat/Input/Actions/IA_ChargedAttack.IA_ChargedAttack")) Charged = Binding->GetAction();
            }
    // Manual Enhanced listeners cancel before the lower-priority hero adds
    // movement, even if legacy delegate ordering was changed by a console cvar.
    if (!Move || !HeroInput || Gate->Priority <= HeroInput->Priority) return;
    if (!ClickWalkContext)
    {
        ClickWalkContext = NewObject<UInputMappingContext>(this, NAME_None, RF_Transient);
        ClickWalkAction = NewObject<UInputAction>(this, NAME_None, RF_Transient);
        PointerMotionAction = NewObject<UInputAction>(this, NAME_None, RF_Transient);
        SelectedSkillAction = NewObject<UInputAction>(this, NAME_None, RF_Transient);
        SelectedSkillAction->ValueType = EInputActionValueType::Boolean;
        SelectedSkillAction->bConsumeInput = true;
        SelectedSkillAction->bConsumesActionAndAxisMappings = true;
        ClickWalkAction->ValueType = EInputActionValueType::Boolean;
        PointerMotionAction->ValueType = EInputActionValueType::Axis2D;
        ClickWalkAction->bConsumeInput = true;
        PointerMotionAction->bConsumeInput = true;
        // Conquer owns physical LMB/RMB, including lower legacy key routes.
        // This candidate's attack dispatcher is a PC mouse profile. Template
        // gamepad Combo is gated too; gamepad combat needs a later explicit profile.
        ClickWalkAction->bConsumesActionAndAxisMappings = true;
        PointerMotionAction->bConsumesActionAndAxisMappings = false;
        ClickWalkContext->MapKey(ClickWalkAction, EKeys::LeftMouseButton);
        ClickWalkContext->MapKey(SelectedSkillAction, EKeys::RightMouseButton);
        ClickWalkContext->MapKey(PointerMotionAction, EKeys::Mouse2D);
        const FKey Keys[] = {EKeys::F1,EKeys::F2,EKeys::F3,EKeys::F4,EKeys::F5,EKeys::F6,EKeys::F7,EKeys::F8,EKeys::F9,EKeys::F10};
        for (const FKey& Key : Keys)
        {
            UInputAction* SlotAction = NewObject<UInputAction>(this, NAME_None, RF_Transient);
            SlotAction->ValueType = EInputActionValueType::Boolean;
            SlotAction->bConsumeInput = true;
            SlotAction->bConsumesActionAndAxisMappings = true;
            SkillSlotActions.Add(SlotAction);
            ClickWalkContext->MapKey(SlotAction, Key);
        }
    }
    auto& Walk = Gate->BindAction(ClickWalkAction, ETriggerEvent::Started, this,
        &AJadeboundProgressionActor::HandleClickWalkPressed);
    Walk.SetShouldConsume(false);
    ClickWalkBindingHandles.Add(Walk.GetHandle());
    auto& Skill = Gate->BindAction(SelectedSkillAction, ETriggerEvent::Started, this,
        &AJadeboundProgressionActor::HandleSelectedSkill);
    Skill.SetShouldConsume(false);
    ClickWalkBindingHandles.Add(Skill.GetHandle());
    for (int32 Slot = 0; Slot < SkillSlotActions.Num(); ++Slot)
    {
        auto& Binding = Gate->BindAction(SkillSlotActions[Slot], ETriggerEvent::Started, this,
            &AJadeboundProgressionActor::HandleSkillSlot, Slot);
        Binding.SetShouldConsume(false);
        ClickWalkBindingHandles.Add(Binding.GetHandle());
    }
    // Jump and charged are optional actual hero bindings. Combat IMC has no
    // Jump; do not make that absent template action a walking prerequisite.
    // Charged remains non-consuming, including the original gamepad trigger.
    for (const UInputAction* Action : {Move, Jump, Charged})
    {
        if (!Action) continue;
        for (ETriggerEvent Event : {ETriggerEvent::Started,ETriggerEvent::Triggered,ETriggerEvent::Ongoing})
        {
            auto& Binding = Gate->BindAction(Action, Event, this, &AJadeboundProgressionActor::HandleManualWalkAction);
            Binding.SetShouldConsume(false);
            ClickWalkBindingHandles.Add(Binding.GetHandle());
        }
    }
    // The engine defers this rebuild; IsClickWalkAvailable checks the actual
    // resulting mappings on the next input frame. Never rebuild in a callback.
    Subsystem->AddMappingContext(ClickWalkContext, 1000);
    ClickWalkSubsystem = Subsystem;
    ClickWalkGate = Gate;
    ClickWalkGatePawn = Character;
    ApplyConquerMovementPolicy(Character);
    UpdateWorldCursor();
}
void AJadeboundProgressionActor::UpdateWorldCursor()
{
    if (bReviveWaiting || bMenuOpen || !ResolveClickWalkScope() || !ClickWalkSubsystem.IsValid() || !GetWorld()) return;
    auto* Viewport = GetWorld()->GetGameViewport();
    auto* Local = Player->GetLocalPlayer();
    if (!Viewport || !Local) return;
    if (WorldCursorPlayer.Get() != Player || WorldCursorViewport.Get() != Viewport)
    {
        RestoreWorldCursor();
        WorldCursorPlayer = Player;
        WorldCursorViewport = Viewport;
        bSavedWorldCursorVisible = Player->ShouldShowMouseCursor();
        bSavedWorldCursorHiddenDuringCapture = Viewport->HideCursorDuringCapture();
        SavedWorldMouseCapture = static_cast<uint8>(Viewport->GetMouseCaptureMode());
        SavedWorldMouseLock = static_cast<uint8>(Viewport->GetMouseLockMode());
    }
    if (bWorldCursorPolicyApplied) return;
    FInputModeGameOnly Mode;
    Mode.SetConsumeCaptureMouseDown(false);
    Player->SetInputMode(Mode);
    // GameOnly normally requests hidden high-precision capture and a lock.
    // These public local Slate operations release both, while keeping game focus.
    Local->GetSlateOperations().ReleaseMouseCapture().ReleaseMouseLock();
    Viewport->SetMouseCaptureMode(EMouseCaptureMode::CaptureDuringMouseDown);
    Viewport->SetMouseLockMode(EMouseLockMode::DoNotLock);
    Viewport->SetHideCursorDuringCapture(false);
    Player->SetShowMouseCursor(true);
    bWorldCursorPolicyApplied = true;
}
void AJadeboundProgressionActor::RestoreWorldCursor()
{
    auto* CursorOwner = WorldCursorPlayer.Get();
    auto* Viewport = WorldCursorViewport.Get();
    // Restore only the exact controller/viewport and fields still holding our
    // values. Do not overwrite another UI/input owner's changed policy.
    if (bWorldCursorPolicyApplied && IsValid(CursorOwner) && IsValid(Viewport) &&
        CursorOwner->GetWorld() == GetWorld() && GetWorld() && GetWorld()->GetGameViewport() == Viewport)
    {
        if (CursorOwner->ShouldShowMouseCursor()) CursorOwner->SetShowMouseCursor(bSavedWorldCursorVisible);
        if (Viewport->GetMouseCaptureMode() == EMouseCaptureMode::CaptureDuringMouseDown)
            Viewport->SetMouseCaptureMode(static_cast<EMouseCaptureMode>(SavedWorldMouseCapture));
        if (Viewport->GetMouseLockMode() == EMouseLockMode::DoNotLock)
            Viewport->SetMouseLockMode(static_cast<EMouseLockMode>(SavedWorldMouseLock));
        if (!Viewport->HideCursorDuringCapture()) Viewport->SetHideCursorDuringCapture(bSavedWorldCursorHiddenDuringCapture);
    }
    bWorldCursorPolicyApplied = false;
    WorldCursorPlayer.Reset();
    WorldCursorViewport.Reset();
}
void AJadeboundProgressionActor::HandleClickWalkPressed(const FInputActionInstance&)
{
    ACharacter* Character = ResolveClickWalkCharacter();
    if (!Character || !IsClickWalkAvailable() || IsManualWalkInputActive()) return;
    CancelSkills(TEXT("WorldCommand"), true);
    if (ConquerInteractionIntent.DropId >= 0)
    {
        const TWeakObjectPtr<AJadeboundProgressionActor> WeakSelf(this);
        const TWeakObjectPtr<ACharacter> WeakPawn(Character);
        StopClickWalk(TEXT("InteractionReplaced"), true);
        if (!WeakSelf.IsValid() || WeakSelf->IsActorBeingDestroyed() ||
            !WeakPawn.IsValid() || ResolveClickWalkCharacter() != WeakPawn.Get()) return;
        Character = WeakPawn.Get();
    }
    FHitResult Hit;
    // Classify the actual visible blocking hit before attempting a ground trace.
    // A clicked actor must never become an attack on empty paving.
    TArray<TEnumAsByte<EObjectTypeQuery>> CursorTypes;
    CursorTypes.Add(UEngineTypes::ConvertToObjectType(ECC_WorldStatic));
    CursorTypes.Add(UEngineTypes::ConvertToObjectType(ECC_WorldDynamic));
    CursorTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));
    // Pawn capsules may ignore Visibility. The object query includes the
    // actual query-enabled target capsule rather than tracing through it.
    if (!Player->GetHitResultUnderCursorForObjects(CursorTypes, false, Hit) ||
        !Hit.bBlockingHit || Hit.bStartPenetrating || Hit.ImpactPoint.ContainsNaN())
    { ConquerControlReason = TEXT("NoBlockingCursorHit"); Feedback = FText::FromString(TEXT("Choose visible ground or a living raider.")); return; }
    // Retain the original HUD-hitbox check, cursor coordinates and exact ray.
    // Only a measured perimeter hit permits ONE extra cursor query. These
    // local ignore parameters never affect physical collision/nav/jump/LOS.
    const AActor* CursorPerimeter = Hit.HitObjectHandle.GetCachedActor();
    if (IsQualifiedCursorPerimeter(GetWorld(), CursorPerimeter))
    {
        const FVector TraceStart = Hit.TraceStart;
        const FVector TraceEnd = Hit.TraceEnd;
        FCollisionQueryParams CursorQuery(SCENE_QUERY_STAT(JadeboundPerimeterCursor), false);
        int32 IgnoredPerimeters = 0;
        for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
        {
            if (IsQualifiedCursorPerimeter(GetWorld(), *It))
            { CursorQuery.AddIgnoredActor(*It); ++IgnoredPerimeters; }
        }
        FHitResult FilteredHit;
        const bool bFilteredHit = !TraceStart.ContainsNaN() && !TraceEnd.ContainsNaN() &&
            !TraceStart.Equals(TraceEnd) && IgnoredPerimeters > 0 && IgnoredPerimeters <= 4 &&
            GetWorld()->LineTraceSingleByObjectType(FilteredHit, TraceStart, TraceEnd,
                FCollisionObjectQueryParams(CursorTypes), CursorQuery);
        if (!bFilteredHit || !FilteredHit.bBlockingHit || FilteredHit.bStartPenetrating || FilteredHit.ImpactPoint.ContainsNaN())
        { ConquerControlReason = TEXT("NoBlockingCursorHit"); Feedback = FText::FromString(TEXT("Choose visible ground or a living raider.")); return; }
        Hit = FilteredHit;
        // The existing final-hit diagnostic consumes this same entry below;
        // failed retraces emit no repeated log and cannot bypass its 24 cap.
        if (ConquerGroundDiagnosticEntries < 24)
            UE_LOG(LogTemp, Log, TEXT("JadeboundCursorPerimeterSkipped Entry=%u Actor=%s Ignored=%d TraceStart=%s TraceEnd=%s RetraceHit=1"),
                ConquerGroundDiagnosticEntries + 1, *GetPathNameSafe(CursorPerimeter), IgnoredPerimeters,
                *TraceStart.ToString(), *TraceEnd.ToString());
    }
    // Bounded read-only evidence from the final cursor hit used below.
    if (ConquerGroundDiagnosticEntries < 24)
    {
        ++ConquerGroundDiagnosticEntries;
        const UPrimitiveComponent* DiagnosticComponent = Hit.GetComponent();
        const UCapsuleComponent* DiagnosticCapsule = Character->GetCapsuleComponent();
        const UCharacterMovementComponent* DiagnosticMovement = Character->GetCharacterMovement();
        const FVector DiagnosticPawnPosition = Character->GetActorLocation();
        const double DiagnosticHalfHeight = IsValid(DiagnosticCapsule) ? DiagnosticCapsule->GetScaledCapsuleHalfHeight() : -1.0;
        const double DiagnosticFeetZ = DiagnosticPawnPosition.Z - DiagnosticHalfHeight;
        UE_LOG(LogTemp, Log, TEXT("JadeboundGroundHit %u Actor=%s Component=%s ObjectType=%d Profile=%s ImpactPoint=%s ImpactNormal=%s Blocking=%d Penetrating=%d PawnPosition=%s CapsuleHalfHeight=%.6f FeetZ=%.6f MaxStepHeight=%.6f Walkable=%d PlanarDistance=%.6f"),
            ConquerGroundDiagnosticEntries, *GetPathNameSafe(Hit.HitObjectHandle.GetCachedActor()), *GetPathNameSafe(DiagnosticComponent),
            IsValid(DiagnosticComponent) ? static_cast<int32>(DiagnosticComponent->GetCollisionObjectType()) : -1,
            *(IsValid(DiagnosticComponent) ? DiagnosticComponent->GetCollisionProfileName().ToString() : FString(TEXT("None"))),
            *Hit.ImpactPoint.ToString(), *Hit.ImpactNormal.ToString(), static_cast<int32>(Hit.bBlockingHit), static_cast<int32>(Hit.bStartPenetrating),
            *DiagnosticPawnPosition.ToString(), DiagnosticHalfHeight, DiagnosticFeetZ,
            IsValid(DiagnosticMovement) ? static_cast<double>(DiagnosticMovement->MaxStepHeight) : -1.0,
            static_cast<int32>(IsValid(DiagnosticMovement) && DiagnosticMovement->IsWalkable(Hit)),
            FMath::Sqrt(FVector::DistSquared2D(DiagnosticPawnPosition, Hit.ImpactPoint)));
    }
    const bool bControl = Player->IsInputKeyDown(EKeys::LeftControl) || Player->IsInputKeyDown(EKeys::RightControl);
    if (!bControl && TryHandleConquerInteractionHit(Character, Hit)) return;
    if (!bControl && Hit.GetActor() == LegacyTarget)
    {
        if (State.EquippedWeapon == 0)
        { StopClickWalk(TEXT("Unarmed"), true); Feedback = FText::FromString(TEXT("Equip a weapon from your bag before attacking.")); return; }
        const auto Health = ReadHealthSample(LegacyTarget, TEXT("CombatHealth"), TEXT("CombatMaxHealth"));
        const auto* Enabled = Field<FBoolProperty>(LegacyTarget, TEXT("CombatEnabled"));
        if (!Health.bAvailable || Health.Current <= 0.0 || LegacyTarget->IsHidden() ||
            LegacyTarget->GetClass()->GetPathName() != TEXT("/Game/Jadebound/Tests/GlaiveCombatV006/BP_GlaivePracticeTarget_V006.BP_GlaivePracticeTarget_V006_C") ||
            !IsValid(LegacyLoop) || LegacyLoop->GetClass()->GetPathName() != TEXT("/Game/Jadebound/Playable/AreaV001/BP_JadeboundAreaLoop_V001.BP_JadeboundAreaLoop_V001_C") ||
            ReadObject(LegacyLoop, TEXT("Target")) != LegacyTarget || !Field<FIntProperty>(LegacyLoop, TEXT("LifeState")) ||
            ReadInt(LegacyLoop, TEXT("LifeState")) != 0 || ReadInt(LegacyLoop, TEXT("RespawnCount")) < 0 ||
            !Enabled || !Enabled->GetPropertyValue_InContainer(LegacyTarget) ||
            !Field<FIntProperty>(LegacyLoop, TEXT("RespawnCount")))
        { ConquerControlReason = TEXT("TargetNotLiving"); return; }
        const FVector TargetPosition = LegacyTarget->GetActorLocation();
        if (TargetPosition.ContainsNaN() || FVector::DistSquared2D(Character->GetActorLocation(), TargetPosition) > FMath::Square(800.0))
        { ConquerControlReason = TEXT("TargetTooFar"); Feedback = FText::FromString(TEXT("That raider is too far away. Choose a closer destination first.")); return; }
        StartConquerGoal(Character, TargetPosition, LegacyTarget);
        return;
    }
    const auto* Component = Hit.GetComponent();
    const auto* Movement = Character->GetCharacterMovement();
    const double FeetZ = Character->GetActorLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    if (!IsValid(Component) || Component->GetCollisionObjectType() != ECC_WorldStatic ||
        !Movement->IsWalkable(Hit) || !FMath::IsFinite(FeetZ) ||
        FMath::Abs(Hit.ImpactPoint.Z - FeetZ) > Movement->MaxStepHeight ||
        FVector::DistSquared2D(Character->GetActorLocation(), Hit.ImpactPoint) > FMath::Square(800.0))
    { ConquerControlReason = TEXT("RejectedGround"); Feedback = FText::FromString(TEXT("Choose nearby walkable ground at your current level.")); return; }
    if (bControl) TryConquerJump(Character, Hit);
    else StartConquerGoal(Character, Hit.ImpactPoint, nullptr);
}

bool AJadeboundProgressionActor::BuildConquerPath(ACharacter* Character, const FVector& Destination, bool bTargetDestination)
{
    if (!Character || Destination.ContainsNaN() || !GetWorld()) return false;
    auto* Navigation = UNavigationSystemV1::GetCurrent(GetWorld());
    if (!Navigation) { ConquerControlReason = TEXT("NavigationUnavailable"); Feedback = FText::FromString(TEXT("Ground walking is unavailable here: no navigation data.")); return false; }
    FNavLocation Start, End;
    const FVector Extent(45.0, 45.0, 120.0);
    const auto& Agent = Character->GetCharacterMovement()->GetNavAgentPropertiesRef();
    FVector NavDestination = Destination;
    if (bTargetDestination)
    {
        const auto* TargetCharacter = Cast<ACharacter>(LegacyTarget);
        if (!TargetCharacter || !TargetCharacter->GetCapsuleComponent()) return false;
        NavDestination.Z -= TargetCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    }
    const double FeetZ = Character->GetActorLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    if (!Navigation->ProjectPointToNavigation(Character->GetActorLocation(), Start, Extent, &Agent) ||
        !Navigation->ProjectPointToNavigation(NavDestination, End, Extent, &Agent) ||
        Start.Location.ContainsNaN() || End.Location.ContainsNaN() ||
        FVector::DistSquared2D(NavDestination, End.Location) > FMath::Square(45.0) ||
        FMath::Abs(Start.Location.Z - FeetZ) > Character->GetCharacterMovement()->MaxStepHeight ||
        FMath::Abs(End.Location.Z - NavDestination.Z) > Character->GetCharacterMovement()->MaxStepHeight)
    { ConquerControlReason = TEXT("NavigationProjectionRejected"); Feedback = FText::FromString(TEXT("That destination is outside the walkable route.")); return false; }
    auto* Path = UNavigationSystemV1::FindPathToLocationSynchronously(this, Start.Location, End.Location, Character);
    if (!Path || !Path->IsValid() || Path->IsPartial() || Path->PathPoints.Num() < 2 || Path->PathPoints.Num() > 128 ||
        FVector::DistSquared(Path->PathPoints.Last(), End.Location) > FMath::Square(1.0))
    { ConquerControlReason = TEXT("UnreachableGoal"); Feedback = FText::FromString(TEXT("That destination is unreachable. Choose clear ground.")); return false; }
    double Length = 0.0;
    for (int32 Index = 0; Index < Path->PathPoints.Num(); ++Index)
    {
        if (Path->PathPoints[Index].ContainsNaN()) return false;
        if (Index > 0) Length += FVector::Distance(Path->PathPoints[Index-1], Path->PathPoints[Index]);
    }
    if (!FMath::IsFinite(Length) || Length > 2400.0)
    { ConquerControlReason = TEXT("PathTooLong"); Feedback = FText::FromString(TEXT("That route is too long. Choose a closer destination.")); return false; }
    ConquerPathPoints = Path->PathPoints; // Own value copy; no borrowed UObject/path references.
    ConquerPathIndex = 1;
    ClickWalkGoal = End.Location;
    return true;
}

bool AJadeboundProgressionActor::StartConquerGoal(ACharacter* Character, const FVector& Destination, AActor* Target)
{
    const bool bHadRejectedGroundFeedback = Feedback.ToString() == TEXT("Choose nearby walkable ground at your current level.");
    StopClickWalk(TEXT("Replaced"), true);
    if (!Character || !Character->GetPendingMovementInputVector().IsNearlyZero()) return false;
    const FVector Offset = Destination - Character->GetActorLocation();
    const bool InReach = Target && !Offset.ContainsNaN() && Offset.SizeSquared2D() <= FMath::Square(250.0) && FMath::Abs(Offset.Z) <= 90.0;
    // A target already in strict reach needs no nav projection. Navigation is
    // required only if an actual approach/detour is needed.
    if (!InReach && !BuildConquerPath(Character, Destination, Target != nullptr)) return false;
    const TWeakObjectPtr<AJadeboundProgressionActor> WeakSelf(this);
    const TWeakObjectPtr<ACharacter> WeakPawn(Character);
    const TWeakObjectPtr<AActor> WeakTarget(Target);
    CancelLegacyAttackPresentation();
    if (!WeakSelf.IsValid() || WeakSelf->IsActorBeingDestroyed() || !WeakPawn.IsValid() ||
        ResolveClickWalkCharacter() != WeakPawn.Get() || (Target && !WeakTarget.IsValid())) return false;
    ClickWalkPawn = WeakPawn;
    ClickWalkPlayer = Player;
    ClickWalkStartedAt = ClickWalkProgressAt = GetWorld()->GetTimeSeconds();
    ClickWalkProgressPoint = Character->GetActorLocation();
    ConquerAttackTarget = WeakTarget.Get();
    ConquerTargetLife = Target ? ReadInt(LegacyLoop, TEXT("RespawnCount")) : -1;
    ConquerTargetPathLocation = Destination;
    ConquerNextPathAt = ClickWalkStartedAt + .5;
    // ConquerNextAttackAt survives replacement clicks and cannot shorten cadence.
    bClickWalkManualObserved = false;
    bClickWalkActive = true;
    ClickWalkStopReason = NAME_None;
    ConquerControlReason = Target ? FName(TEXT("ApproachingTarget")) : FName(TEXT("FollowingNavigationPath"));
    if (bHadRejectedGroundFeedback) Feedback = FText::GetEmpty();
    Character->OnCharacterMovementUpdated.AddUniqueDynamic(this, &AJadeboundProgressionActor::OnClickWalkMovementUpdated);
    return true;
}

bool AJadeboundProgressionActor::TryConquerAttack(ACharacter* Character)
{
    AActor* Target = ConquerAttackTarget;
    if (!Character || !IsValid(Target) || Target != LegacyTarget || !IsClickWalkAvailable() ||
        IsAttackBlocked() || !IsProgressionReady()) return false;
    FVector Aim = Target->GetActorLocation() - Character->GetActorLocation();
    if (Aim.ContainsNaN() || FMath::Abs(Aim.Z) > 90.0) return false;
    Aim.Z = 0.0;
    const double Distance = Aim.Size();
    if (!FMath::IsFinite(Distance) || Distance > 250.0 || Distance < 1.0) return false;
    FHitResult Obstruction;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(JadeboundConquerAttackLOS), false, Character);
    Query.AddIgnoredActor(Target);
    if (GetWorld()->LineTraceSingleByObjectType(Obstruction, Character->GetActorLocation(), Target->GetActorLocation(),
        FCollisionObjectQueryParams(ECC_WorldStatic), Query)) return false;
    UObject* Driver = ReadObject(LegacyLoop, TEXT("Driver"));
    const auto* Phase = Field<FIntProperty>(Driver, TEXT("Phase"));
    const auto* Swing = Field<FIntProperty>(Driver, TEXT("SwingId"));
    const auto* Admissions = Field<FIntProperty>(Driver, TEXT("SwingAdmissions"));
    const auto* Attacking = Field<FBoolProperty>(Character, TEXT("Is Attacking"));
    auto* Montage = Cast<UAnimMontage>(ReadObject(Character, TEXT("Combo Attack Montage")));
    auto* Animation = Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
    if (!IsValid(Driver) || Driver->GetClass()->GetPathName() !=
            TEXT("/Game/Jadebound/Tests/GlaiveCombatV006/BP_GlaiveSwingDriver_V006.BP_GlaiveSwingDriver_V006_C") ||
        !Phase || !Swing || !Admissions || !Attacking || !Montage || !Animation)
    { StopClickWalk(TEXT("AttackBindingUnavailable"), true); return true; }
    const double Now = GetWorld()->GetTimeSeconds();
    // Original montage/root motion and driver ledger own an admitted swing.
    // Waiting does not repeatedly cancel it or feed path movement into it.
    ClickWalkProgressAt = Now;
    ClickWalkProgressPoint = Character->GetActorLocation();
    if (Now < ConquerNextAttackAt || Phase->GetPropertyValue_InContainer(Driver) != 0 ||
        Attacking->GetPropertyValue_InContainer(Character) || Animation->Montage_IsActive(Montage)) return true;
    const TWeakObjectPtr<AJadeboundProgressionActor> BeforeFacing(this);
    const TWeakObjectPtr<ACharacter> FacingPawn(Character);
    const TWeakObjectPtr<AActor> FacingTarget(Target);
    const TWeakObjectPtr<UObject> FacingDriver(Driver);
    FRotator BodyFacing = Character->GetActorRotation();
    BodyFacing.Yaw = Aim.Rotation().Yaw;
    const bool Rotated = Character->SetActorRotation(BodyFacing, ETeleportType::None);
    if (!BeforeFacing.IsValid() || BeforeFacing->IsActorBeingDestroyed()) return true;
    if (!FacingPawn.IsValid() || !FacingTarget.IsValid() || !FacingDriver.IsValid() ||
        ResolveClickWalkCharacter() != FacingPawn.Get() || !IsClickWalkAvailable() || IsAttackBlocked() || !IsProgressionReady() ||
        !IsValid(LegacyLoop) || ReadObject(LegacyLoop, TEXT("Driver")) != FacingDriver.Get() ||
        Phase->GetPropertyValue_InContainer(FacingDriver.Get()) != 0 || Attacking->GetPropertyValue_InContainer(FacingPawn.Get()) ||
        !IsValid(Animation) || !IsValid(Montage) || !Character->GetMesh() || Character->GetMesh()->GetAnimInstance() != Animation ||
        Animation->Montage_IsActive(Montage) || ConquerAttackTarget != FacingTarget.Get() || !Rotated)
    { StopClickWalk(TEXT("TargetFacingContextLost"), false); return true; }
    const auto AfterFacingHealth = ReadHealthSample(Target, TEXT("CombatHealth"), TEXT("CombatMaxHealth"));
    Aim = Target->GetActorLocation() - Character->GetActorLocation();
    const auto* AfterFacingEnabled = Field<FBoolProperty>(Target, TEXT("CombatEnabled"));
    if (!AfterFacingHealth.bAvailable || AfterFacingHealth.Current <= 0.0 || Target->IsHidden() ||
        !AfterFacingEnabled || !AfterFacingEnabled->GetPropertyValue_InContainer(Target) || ReadInt(LegacyLoop, TEXT("LifeState")) != 0 ||
        ReadInt(LegacyLoop, TEXT("RespawnCount")) != ConquerTargetLife || Aim.ContainsNaN() ||
        FMath::Abs(Aim.Z) > 90.0 || Aim.SizeSquared2D() < 1.0 || Aim.SizeSquared2D() > FMath::Square(250.0) ||
        FVector::DotProduct(Character->GetActorForwardVector(), Aim.GetSafeNormal2D()) < .5 ||
        GetWorld()->LineTraceSingleByObjectType(Obstruction, Character->GetActorLocation(), Target->GetActorLocation(),
            FCollisionObjectQueryParams(ECC_WorldStatic), Query))
    { StopClickWalk(TEXT("FacingAdmissionChanged"), false); return true; }
    const int32 BeforeSwing = Swing->GetPropertyValue_InContainer(Driver);
    const int32 BeforeAdmissions = Admissions->GetPropertyValue_InContainer(Driver);
    if (BeforeSwing < 0 || BeforeSwing == MAX_int32 || BeforeAdmissions < 0 || BeforeAdmissions == MAX_int32 ||
        ConquerAttackDispatches == MAX_int64 || ConquerObservedAdmissions == MAX_int64)
    { StopClickWalk(TEXT("AttackCounterExhausted"), true); return true; }
    auto* HeroInput = Cast<UEnhancedInputComponent>(Character->InputComponent);
    const FEnhancedInputActionEventBinding* Started = nullptr;
    int32 Matches = 0;
    if (HeroInput)
        for (const auto& Binding : HeroInput->GetActionEventBindings())
            if (Binding && Binding->IsBoundToObject(Character) && Binding->GetAction() &&
                Binding->GetTriggerEvent() == ETriggerEvent::Started && Binding->GetAction()->GetPathName() ==
                    TEXT("/Game/Variant_Combat/Input/Actions/IA_ComboAttack.IA_ComboAttack"))
            { Started = Binding.Get(); ++Matches; }
    if (Matches != 1 || !Started) { StopClickWalk(TEXT("AttackDelegateNotUnique"), true); return true; }
    const UInputAction* ComboAction = Started->GetAction();
    TUniquePtr<FEnhancedInputActionEventBinding> Callback = Started->Clone();
    // Stage14's four data pins are unused. This is controller dispatch through
    // the preserved Started graph, not fabricated hardware input or direct damage.
    const FInputActionInstance Instance(ComboAction);
    ++ConquerAttackDispatches;
    ConquerNextAttackAt = Now + 1.0; // latch before potentially reentrant Blueprint execution
    const TWeakObjectPtr<AJadeboundProgressionActor> WeakSelf(this);
    const TWeakObjectPtr<ACharacter> WeakPawn(Character);
    const TWeakObjectPtr<AActor> WeakTarget(Target);
    const TWeakObjectPtr<UObject> WeakDriver(Driver);
    Character->GetCharacterMovement()->StopMovementImmediately();
    Callback->Execute(Instance);
    auto* Surviving = WeakSelf.Get();
    if (!Surviving || Surviving->IsActorBeingDestroyed()) return true;
    if (!WeakPawn.IsValid() || !WeakTarget.IsValid() || !WeakDriver.IsValid() ||
        Surviving->Hero != WeakPawn.Get() || Surviving->ConquerAttackTarget != WeakTarget.Get())
    { Surviving->StopClickWalk(TEXT("AttackCallbackContextLost"), false); return true; }
    const bool Admitted = ReadInt(WeakDriver.Get(), TEXT("SwingId")) == BeforeSwing + 1 &&
        ReadInt(WeakDriver.Get(), TEXT("SwingAdmissions")) == BeforeAdmissions + 1;
    if (Admitted) ++Surviving->ConquerObservedAdmissions;
    Surviving->ConquerControlReason = Admitted ? FName(TEXT("SelectedSwingAdmitted")) : FName(TEXT("SelectedSwingNotAdmitted"));
    if (!Admitted) Surviving->StopClickWalk(TEXT("SelectedSwingNotAdmitted"), false);
    return true; // Never call BeginGlaiveSwing a second time as a fallback.
}

bool AJadeboundProgressionActor::TryConquerJump(ACharacter* Character, const FHitResult& Ground)
{
    auto* Movement = Character ? Character->GetCharacterMovement() : nullptr;
    auto* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
    if (!Movement || !Capsule || !IsClickWalkAvailable()) return false;
    const TWeakObjectPtr<AJadeboundProgressionActor> BeforeCanJump(this);
    const TWeakObjectPtr<ACharacter> BeforeJumpPawn(Character);
    const bool bCanJump = Character->CanJump(); // BlueprintNativeEvent may be reentrant.
    if (!BeforeCanJump.IsValid() || BeforeCanJump->IsActorBeingDestroyed() || !BeforeJumpPawn.IsValid() ||
        !bCanJump || ResolveClickWalkCharacter() != BeforeJumpPawn.Get() ||
        Character->GetCharacterMovement() != Movement || Character->GetCapsuleComponent() != Capsule) return false;
    const FVector Start = Character->GetActorLocation();
    const FVector End = Ground.ImpactPoint + FVector(0.0,0.0,Capsule->GetScaledCapsuleHalfHeight() + Movement->CurrentFloor.FloorDist);
    const double Gravity = Movement->GetGravityZ();
    const double Up = Movement->JumpZVelocity;
    const double Discriminant = Up * Up + 2.0 * Gravity * (End.Z - Start.Z);
    const double Horizontal = FVector::Dist2D(Start, End);
    if (End.ContainsNaN() || !FMath::IsFinite(Gravity) || Gravity >= -1.0 || Gravity < -5000.0 || !FMath::IsFinite(Up) || Up <= 0.0 ||
        !FMath::IsFinite(Discriminant) || Discriminant <= 0.0 || Horizontal < 20.0 || Horizontal > 500.0)
    { ConquerControlReason = TEXT("JumpRangeRejected"); Feedback = FText::FromString(TEXT("That jump is out of range. Choose closer ground.")); return false; }
    const double Time = (-Up - FMath::Sqrt(Discriminant)) / Gravity;
    if (!FMath::IsFinite(Time) || Time < .2 || Time > 2.5 || Horizontal / Time > Movement->MaxWalkSpeed)
    { ConquerControlReason = TEXT("JumpSpeedRejected"); Feedback = FText::FromString(TEXT("That jump is too far for the current movement speed.")); return false; }
    FVector Velocity = (End - Start) / Time;
    Velocity.Z = Up;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(JadeboundConquerJump), false, Character);
    // An extra 1 cm covers the sub-centimetre chord sag of this <=2.5 s arc.
    const int32 Steps = FMath::Max(24, FMath::CeilToInt(Time / .04));
    const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius()+1.f,
        Capsule->GetScaledCapsuleHalfHeight()+1.f);
    FVector Previous = Start;
    for (int32 Index = 1; Index <= Steps; ++Index)
    {
        const double T = Time * static_cast<double>(Index) / Steps;
        const FVector Point = Start + Velocity * T + FVector(0.0,0.0,.5 * Gravity * T * T);
        FHitResult Block;
        if (GetWorld()->SweepSingleByChannel(Block, Previous, Point, FQuat::Identity, ECC_Pawn, Shape, Query))
        { ConquerControlReason = TEXT("JumpArcBlocked"); Feedback = FText::FromString(TEXT("The jump is blocked by an obstacle.")); return false; }
        Previous = Point;
    }
    if (GetWorld()->OverlapBlockingTestByChannel(End, FQuat::Identity, ECC_Pawn, Shape, Query))
    { ConquerControlReason = TEXT("JumpLandingBlocked"); Feedback = FText::FromString(TEXT("There is no clear space to land there.")); return false; }
    StopClickWalk(TEXT("DirectedJump"), true);
    const TWeakObjectPtr<AJadeboundProgressionActor> WeakSelf(this);
    const TWeakObjectPtr<ACharacter> WeakPawn(Character);
    CancelLegacyAttackPresentation();
    if (!WeakSelf.IsValid() || WeakSelf->IsActorBeingDestroyed() || !WeakPawn.IsValid() ||
        ResolveClickWalkCharacter() != WeakPawn.Get()) return false;
    ConquerControlReason = TEXT("DirectedNativeJump");
    // Normal native launch uses CharacterMovement collision/falling; no actor
    // transform, HP, economic state, save or synthetic damage is assigned.
    Character->LaunchCharacter(Velocity, true, true);
    return true;
}

void AJadeboundProgressionActor::HandleSelectedSkill(const FInputActionInstance&)
{
    if (!ResolveClickWalkScope() || bMenuOpen || bAwaitingPointerRelease || IsCloseFrameGuardActive() || bSkillRightHeld) return;
    bSkillRightHeld = true; // one physical press also covers the loot precedence branch
    ACharacter* Character = ResolveClickWalkCharacter();
    // A real pending drop takes precedence over the selected-skill dispatcher.
    // Empty ground, merchant and foreign hits keep its existing fallback.
    if (Character && IsClickWalkAvailable() && !IsManualWalkInputActive() &&
        TryHandleConquerLootCursor(Character)) return;
    CastSelectedSkill();
}


FJadeboundSkillPresentation AJadeboundProgressionActor::GetSkillPresentation() const
{
    FJadeboundSkillPresentation View;
    View.bAvailable = ResolveClickWalkCharacter() && IsProgressionReady() && !bUnsavedCandidate && State.EquippedWeapon == 1;
    View.Stamina = SkillStamina;
    View.SelectedSlot = LocalSkillBindings.Find(FJadeboundHotbarEntry::Skill(SelectedSkillSlot));
    for (int32 Slot=0; Slot<10; ++Slot) View.SlotEntries[Slot]=LocalSkillBindings.Resolve(Slot);
    if (Core && State.ExactRevision==Core->GetSnapshot().Revision)
        for (const auto& Owned : Core->GetSnapshot().Inventory)
            if (Owned.Id==Jadebound::ItemId::Balm) View.BalmCount+=static_cast<int32>(Owned.Count);
    View.HotbarRevision=SkillHotbarRevision;
    View.bHotbarConfigIssue=bSkillHotbarConfigIssue;
    View.HotbarFeedback=SkillHotbarFeedback;
    View.bRun = bConquerRun;
    View.bGuarding = ActiveSkill == 1;
    View.bStepping = ActiveSkill == 2;
    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    View.GuardCooldown = FMath::Max(0.0, GuardReadyAt - Now);
    View.StepCooldown = FMath::Max(0.0, CurrentReadyAt - Now);
    return View;
}

void AJadeboundProgressionActor::MarkSkillHudGesture()
{
    if (!ResolveClickWalkScope()) return;
    LastMenuCloseFrame = GFrameCounter;
    bAwaitingPointerRelease = true;
}

void AJadeboundProgressionActor::SelectSkillSlot(int32 Slot)
{
    if (!ResolveClickWalkScope() || bMenuOpen || Slot < 0 || Slot >= 10) return;
    const auto Entry = LocalSkillBindings.Resolve(Slot);
    if (Entry.Kind==EJadeboundHotbarKind::Item)
    { Feedback=FText::FromString(TEXT("Balm: use its assigned F-key. RMB keeps your selected skill."));return; }
    SelectedSkillSlot = Entry.Kind==EJadeboundHotbarKind::Skill ? Entry.Identity : INDEX_NONE;
    Feedback = FText::FromString(SelectedSkillSlot == 0 ? TEXT("Rooted Guard selected: RMB to brace; costs 20 stamina.") :
        SelectedSkillSlot == 1 ? TEXT("Passing Current selected: RMB on nearby ground; costs 25 stamina.") :
        TEXT("This quick slot is unbound. Assign a skill or balm in your pack."));
}

bool AJadeboundProgressionActor::HasOwnedHotbarKey(int32 Slot) const
{
    if(Slot<0 || Slot>=10 || !ResolveClickWalkScope() || !ClickWalkGate.IsValid() ||
        ClickWalkGate.Get()!=InputComponent || ClickWalkGatePawn.Get()!=Hero || !ClickWalkSubsystem.IsValid() ||
        !SkillSlotActions.IsValidIndex(Slot) || !IsValid(SkillSlotActions[Slot])) return false;
    const auto* Input=Player ? Cast<UEnhancedPlayerInput>(Player->PlayerInput) : nullptr;
    const FKey Keys[]={EKeys::F1,EKeys::F2,EKeys::F3,EKeys::F4,EKeys::F5,EKeys::F6,EKeys::F7,EKeys::F8,EKeys::F9,EKeys::F10};
    bool Own=false;
    if(Input) for(const auto& Mapping:Input->GetEnhancedActionMappingsView())
        if(Mapping.Key==Keys[Slot]) {if(Mapping.Action!=SkillSlotActions[Slot]) return false;Own=true;}
    return Own;
}
bool AJadeboundProgressionActor::IsHotbarUseContextAvailable() const
{
    if(bReviveWaiting || !ResolveClickWalkScope() || bMenuOpen || bAwaitingPointerRelease || IsCloseFrameGuardActive() ||
        ActiveSkill!=0 || bSkillDispatching || !FSlateApplication::IsInitialized() || !FSlateApplication::Get().IsActive()) return false;
    const auto* Viewport=GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
    if(!Viewport || (Viewport->ViewportConsole && (!IsValid(Viewport->ViewportConsole) || !Viewport->ViewportConsole->ConsoleState.IsNone()))) return false;
    const TSharedPtr<SViewport> Widget=Viewport->GetGameViewportWidget();
    return Widget.IsValid() && Widget->HasKeyboardFocus(); // no text/console/pack focus
}
void AJadeboundProgressionActor::HandleSkillSlot(const FInputActionInstance&,int32 Slot)
{
    if(Slot<0 || Slot>=10 || bSkillKeyHeld[Slot] || !HasOwnedHotbarKey(Slot)) return;
    bSkillKeyHeld[Slot]=true; // failed admissions also require a physical release
    const int64 MappingRevision=SkillHotbarRevision;
    const auto Entry=LocalSkillBindings.Resolve(Slot);
    if(Entry.Kind==EJadeboundHotbarKind::Item)
    { UseHotbarItem(Slot,MappingRevision);return; }
    if(!IsClickWalkAvailable()) return; // original skill eligibility unchanged
    SelectSkillSlot(Slot);
    CastSelectedSkill();
}
void AJadeboundProgressionActor::UseHotbarItem(int32 Slot,int64 ExpectedHotbarRevision)
{
    if(bPersistenceBlocked || bUnsavedCandidate) return;
    if(!IsHotbarUseContextAvailable() || !HasOwnedHotbarKey(Slot) || !bSkillHotbarLoaded ||
        ExpectedHotbarRevision!=SkillHotbarRevision || !LocalSkillBindings.Resolve(Slot).Equals(FJadeboundHotbarEntry::Item(7)) ||
        !AuthorityReady() || !Core || State.ExactRevision>=static_cast<uint64>(MAX_int64)) return;
    const auto& Owned=Core->GetSnapshot();
    if(Owned.Revision!=State.ExactRevision) return;
    for(uint32 Index=0;Index<Jadebound::InventorySlotCount;++Index)
        if(Owned.Inventory[Index].Id==Jadebound::ItemId::Balm && Owned.Inventory[Index].Count>0)
        { UseOwnedItem(static_cast<int32>(Index),7,static_cast<int64>(Owned.Revision),EItemUseContext::Hotbar);return; }
    Feedback=FText::FromString(TEXT("You have no balm. This key stays assigned for your next purchase."));
}
bool AJadeboundProgressionActor::CanConfigureSkillHotbar() const
{
    return ResolveClickWalkScope() && bMenuOpen && HasLivingPossessedHero() && IsProgressionReady() &&
        !bUnsavedCandidate && !bPersistenceBlocked && !bSkillDispatching && GetWorld() && !GetWorld()->IsPaused();
}
void AJadeboundProgressionActor::LoadLocalSkillHotbar()
{
    if(bSkillHotbarLoaded || !ResolveClickWalkScope()) return;
    bSkillHotbarLoaded=true;
    const FString Current=LocalSkillHotbarPath(), Legacy=LegacySkillHotbarPath();
    const bool HasCurrent=IFileManager::Get().FileExists(*Current);
    const bool HasLegacy=IFileManager::Get().FileExists(*Legacy);
    if(!HasCurrent && !HasLegacy)
    { SkillHotbarFeedback=FText::FromString(TEXT("Default keys: F1 Guard, F2 Current. Drag skills or balm to assign."));return; }
    FJadeboundLocalHotbarBindings Loaded;
    if(!ReadLocalSkillHotbar(HasCurrent ? Current : Legacy,Loaded,!HasCurrent))
    {
        LocalSkillBindings=FJadeboundLocalHotbarBindings{};SelectedSkillSlot=INDEX_NONE;bSkillHotbarConfigIssue=true;
        SkillHotbarFeedback=FText::FromString(TEXT("Saved controls could not be read. Defaults are active; assign or reset to save new controls."));return;
    }
    LocalSkillBindings=Loaded;SelectedSkillSlot=INDEX_NONE;bHotbarMigrationPending=!HasCurrent;
    SkillHotbarFeedback=FText::FromString(HasCurrent ? TEXT("Your assigned keys are loaded.") : TEXT("Your existing keys are retained. Your next assignment saves the updated controls."));
}
bool AJadeboundProgressionActor::PersistLocalSkillHotbar() const
{
    if (!CanConfigureSkillHotbar() || !LocalSkillBindings.IsValid()) return false;
#if PLATFORM_WINDOWS
    const FString Filename=LocalSkillHotbarPath();
    if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename),true)) return false;
    const FString Text=LocalSkillBindings.Encode();
    if(Text.IsEmpty() || Text.Len()>4096) return false;
    const FString Temp=Filename+TEXT(".")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".pending");
    if (!FFileHelper::SaveStringToFile(Text,*Temp,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
        &IFileManager::Get(),FILEWRITE_NoReplaceExisting)) return false;
    FJadeboundLocalHotbarBindings Verified;
    if (!ReadLocalSkillHotbar(Temp,Verified) || !Verified.Equals(LocalSkillBindings)) return false;
    // Same-directory Windows rename; generic IFileManager::Move(Replace=true) deletes first.
    // No COPY_ALLOWED, no Delete, no retry. A held rename performs no explicit old-file deletion; pending evidence remains.
    const FString From=HotbarNativeWindowsPath(Temp),To=HotbarNativeWindowsPath(Filename);
    HANDLE Handle=CreateFileW(*From,GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (Handle==INVALID_HANDLE_VALUE) return false;
    const bool Flushed=FlushFileBuffers(Handle)!=0;
    const bool Closed=CloseHandle(Handle)!=0;
    if (!Flushed || !Closed) return false;
    if (!MoveFileExW(*From,*To,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) return false;
    return ReadLocalSkillHotbar(Filename,Verified) && Verified.Equals(LocalSkillBindings);
#else
    return false; // first local-persistence qualification is Windows-only
#endif
}
bool AJadeboundProgressionActor::CommitSkillHotbar(const FJadeboundLocalHotbarBindings& Proposed, int64 ExpectedRevision)
{
    if (!CanConfigureSkillHotbar() || !bSkillHotbarLoaded || SkillHotbarRevision==MAX_int64 || !Proposed.IsValid()) return false;
    if (ExpectedRevision!=SkillHotbarRevision)
    { SkillHotbarFeedback=FText::FromString(TEXT("Assignments changed. Start a new drag or click to use the current slots."));return false; }
    MarkSkillHudGesture();
    if (Proposed.Equals(LocalSkillBindings) && !bSkillHotbarConfigIssue && !bHotbarMigrationPending) return true;
    LocalSkillBindings=Proposed; SelectedSkillSlot=LocalSkillBindings.RetainSelection(SelectedSkillSlot);
    ++SkillHotbarRevision;
    const bool Saved=PersistLocalSkillHotbar(); bSkillHotbarConfigIssue=!Saved;
    if(Saved) bHotbarMigrationPending=false;
    SkillHotbarFeedback=FText::FromString(Saved ?
        TEXT("F1-F10 assignments saved locally. Selection and cooldowns follow each skill, not its position.") :
        TEXT("Assignments work for this session but controls could not be saved. See the Skills page to retry; character progress is unchanged."));
    return true; // session assignment accepted; persistence outcome displayed separately
}
bool AJadeboundProgressionActor::AssignSkillHotbarSlot(int32 Slot, int32 SkillIdentity, int64 ExpectedRevision)
{
    if (!CanConfigureSkillHotbar()) return false;
    if (ExpectedRevision!=SkillHotbarRevision)
    { SkillHotbarFeedback=FText::FromString(TEXT("Assignments changed. Start a new drag or click to use the current slots."));return false; }
    FJadeboundLocalHotbarBindings Proposed=LocalSkillBindings;
    const auto Entry=SkillIdentity==INDEX_NONE ? FJadeboundHotbarEntry{} : FJadeboundHotbarEntry::Skill(SkillIdentity);
    return Proposed.Assign(Slot,Entry) && CommitSkillHotbar(Proposed,ExpectedRevision);
}
bool AJadeboundProgressionActor::AssignItemHotbarSlot(int32 Slot,int32 InventorySlot,int32 ExpectedItemId,int64 ExpectedInventoryRevision,int64 ExpectedHotbarRevision)
{
    if(!CanConfigureSkillHotbar() || !bSkillHotbarLoaded || ExpectedHotbarRevision!=SkillHotbarRevision ||
        ExpectedItemId!=7 || ExpectedInventoryRevision<0 || !Core || !AuthorityReady() ||
        State.ExactRevision>=static_cast<uint64>(MAX_int64) || State.ExactRevision!=static_cast<uint64>(ExpectedInventoryRevision) ||
        State.Revision!=ExpectedInventoryRevision || Core->GetSnapshot().Revision!=State.ExactRevision ||
        InventorySlot<0 || InventorySlot>=static_cast<int32>(Jadebound::InventorySlotCount) ||
        !State.Slots.IsValidIndex(InventorySlot) || State.Slots[InventorySlot].ItemId!=7 || State.Slots[InventorySlot].Count<=0) return false;
    const auto& Owned=Core->GetSnapshot().Inventory[static_cast<uint32>(InventorySlot)];
    if(Owned.Id!=Jadebound::ItemId::Balm || Owned.Count!=static_cast<uint32>(State.Slots[InventorySlot].Count)) return false;
    FJadeboundLocalHotbarBindings Proposed=LocalSkillBindings;
    return Proposed.Assign(Slot,FJadeboundHotbarEntry::Item(7)) && CommitSkillHotbar(Proposed,ExpectedHotbarRevision);
}
bool AJadeboundProgressionActor::ResetSkillHotbar(int64 ExpectedRevision)
{
    return CommitSkillHotbar(FJadeboundLocalHotbarBindings{},ExpectedRevision);
}

bool AJadeboundProgressionActor::BuildPassingCurrent(ACharacter* Character, FVector& Destination) const
{
    if (!Character || Character != ResolveClickWalkCharacter() || !Player || !GetWorld()) return false;
    auto* Movement = Character->GetCharacterMovement();
    auto* Capsule = Character->GetCapsuleComponent();
    if (!Movement || !Capsule || !bConquerMovementPolicy || ConquerMovementPawn.Get() != Character ||
        Movement->MaxWalkSpeed != ConquerOwnedSpeed || !FMath::IsFinite(Movement->MaxAcceleration) ||
        Movement->MaxAcceleration <= 0.f || Character->bIsCrouched || !Character->GetPendingMovementInputVector().IsNearlyZero()) return false;
    TArray<TEnumAsByte<EObjectTypeQuery>> Types;
    Types.Add(UEngineTypes::ConvertToObjectType(ECC_WorldStatic));
    Types.Add(UEngineTypes::ConvertToObjectType(ECC_WorldDynamic));
    Types.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));
    FHitResult Ground;
    if (!Player->GetHitResultUnderCursorForObjects(Types, false, Ground) || !Ground.bBlockingHit) return false;
    // Same measured perimeter-only cursor policy; physical queries below ignore no perimeter.
    if (IsQualifiedCursorPerimeter(GetWorld(), Ground.HitObjectHandle.GetCachedActor()))
    {
        FCollisionQueryParams Query(SCENE_QUERY_STAT(JadeboundCurrentCursor), false);
        for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
            if (IsQualifiedCursorPerimeter(GetWorld(), *It)) Query.AddIgnoredActor(*It);
        FHitResult Filtered;
        if (!GetWorld()->LineTraceSingleByObjectType(Filtered, Ground.TraceStart, Ground.TraceEnd,
            FCollisionObjectQueryParams(Types), Query)) return false;
        Ground = Filtered;
    }
    const FVector Position = Character->GetActorLocation();
    const double Half = Capsule->GetScaledCapsuleHalfHeight();
    const double Feet = Position.Z - Half;
    const double Distance = FVector::Dist2D(Position, Ground.ImpactPoint);
    if (!Ground.bBlockingHit || Ground.bStartPenetrating || Ground.ImpactPoint.ContainsNaN() ||
        !Ground.GetComponent() || Ground.GetComponent()->GetCollisionObjectType() != ECC_WorldStatic ||
        !Movement->IsWalkable(Ground) || !FMath::IsFinite(Distance) || Distance < 30.0 || Distance > 180.0 ||
        FMath::Abs(Ground.ImpactPoint.Z - Feet) > Movement->MaxStepHeight) return false;
    auto* Navigation = UNavigationSystemV1::GetCurrent(GetWorld());
    if (!Navigation) return false;
    FNavLocation Start, End;
    const FVector Extent(45,45,120);
    const auto& Agent = Movement->GetNavAgentPropertiesRef();
    if (!Navigation->ProjectPointToNavigation(Position, Start, Extent, &Agent) ||
        !Navigation->ProjectPointToNavigation(Ground.ImpactPoint, End, Extent, &Agent) ||
        Start.Location.ContainsNaN() || End.Location.ContainsNaN() ||
        FVector::DistSquared2D(End.Location, Ground.ImpactPoint) > FMath::Square(10.0) ||
        FMath::Abs(End.Location.Z - Ground.ImpactPoint.Z) > Movement->MaxStepHeight) return false;
    auto* Path = UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(), Start.Location, End.Location, Character);
    if (!Path || !Path->IsValid() || Path->IsPartial() || Path->PathPoints.Num() < 2 || Path->PathPoints.Num() > 16 ||
        FVector::DistSquared(Path->PathPoints.Last(), End.Location) > 1.0) return false;
    double Length = 0.0;
    for (int32 N = 1; N < Path->PathPoints.Num(); ++N)
    {
        if (Path->PathPoints[N].ContainsNaN()) return false;
        Length += FVector::Distance(Path->PathPoints[N-1], Path->PathPoints[N]);
    }
    if (!FMath::IsFinite(Length) || Length > Distance + 10.0) return false; // no evasive detour
    FCollisionQueryParams Query(SCENE_QUERY_STAT(JadeboundCurrentSweep), false);
    Query.AddIgnoredActor(Character); Query.AddIgnoredActor(this);
    // Physical Pawn-channel sweep matches the existing directed-jump policy.
    // Query-only interaction handles ignore Pawn and are not physical obstacles.
    FVector Previous = Position;
    for (int32 N = 1; N <= 8; ++N)
    {
        const FVector Point = FMath::Lerp(Position, FVector(End.Location.X,End.Location.Y,Position.Z), N / 8.0);
        FHitResult Floor, Wall;
        if (!GetWorld()->LineTraceSingleByObjectType(Floor, Point + FVector(0,0,20),
                Point - FVector(0,0,Half + Movement->MaxStepHeight + 10), FCollisionObjectQueryParams(ECC_WorldStatic), Query) ||
            Floor.bStartPenetrating || !Floor.bBlockingHit || !Movement->IsWalkable(Floor) ||
            FMath::Abs(Floor.ImpactPoint.Z - Feet) > Movement->MaxStepHeight) return false;
        // A 2 cm upward bias prevents supported floor contact from being an obstruction.
        // Existing native floor/ledge movement still owns final collision and motion.
        const FVector Center(Point.X, Point.Y, Floor.ImpactPoint.Z + Half + 2.0);
        if (GetWorld()->SweepSingleByChannel(Wall, Previous + FVector(0,0,2), Center,
            FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Half), Query)) return false;
        Previous = Center - FVector(0,0,2);
    }
    Destination = FVector(End.Location.X,End.Location.Y,Position.Z);
    return !Destination.ContainsNaN() && FVector::Dist2D(Position, Destination) <= 180.0;
}

void AJadeboundProgressionActor::CastSelectedSkill()
{
    // Do not replace held journal/retry feedback with a generic skill error.
    if (bPersistenceBlocked || bUnsavedCandidate) return;
    ACharacter* Character = ResolveClickWalkCharacter();
    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0;
    if (bSkillDispatching || !Character || !IsClickWalkAvailable() || !IsProgressionReady() || bUnsavedCandidate ||
        IsManualWalkInputActive() || !FMath::IsFinite(Now) || State.EquippedWeapon != 1)
    { Feedback = FText::FromString(TEXT("Skills require a living glaive wielder with the pack closed.")); return; }
    if (SelectedSkillSlot < 0 || SelectedSkillSlot > 1)
    { Feedback = FText::FromString(TEXT("Choose an assigned skill; configure F1-F10 in the pack Skills page.")); return; }
    const bool Guard = SelectedSkillSlot == 0;
    const float Cost = Guard ? 20.f : 25.f;
    if (ActiveSkill != 0 || Now < (Guard ? GuardReadyAt : CurrentReadyAt))
    { Feedback = FText::FromString(TEXT("That skill is recovering.")); return; }
    if (!FMath::IsFinite(SkillStamina) || SkillStamina < Cost)
    { Feedback = FText::FromString(TEXT("Not enough stamina.")); return; }
    FVector Destination = FVector::ZeroVector;
    if (!Guard && !BuildPassingCurrent(Character, Destination))
    { Feedback = FText::FromString(TEXT("Current needs clear supported ground 30-180 cm away, without an obstacle or detour.")); return; }
    const TWeakObjectPtr<AJadeboundProgressionActor> WeakSelf(this);
    const TWeakObjectPtr<ACharacter> WeakPawn(Character);
    bSkillDispatching = true; // reject reentrant cast during stop/montage callbacks
    StopClickWalk(TEXT("SkillCast"), true);
    if (!WeakSelf.IsValid() || WeakSelf->IsActorBeingDestroyed()) return;
    CancelLegacyAttackPresentation();
    if (!WeakSelf.IsValid() || WeakSelf->IsActorBeingDestroyed()) return;
    Character = WeakPawn.Get();
    if (!Character || Character != ResolveClickWalkCharacter() || !IsClickWalkAvailable() || State.EquippedWeapon != 1 ||
        !IsProgressionReady() || bUnsavedCandidate || (!Guard && !BuildPassingCurrent(Character, Destination)))
    { bSkillDispatching = false; return; }
    if (SkillResourcePawn.Get() != Character) { SkillResourcePawn = Character; SkillStamina = 100.f; }
    SkillStamina -= Cost;
    if (SkillCastAdmissions < MAX_int64) ++SkillCastAdmissions;
    SkillPawn = Character;
    ActiveSkill = Guard ? 1 : 2;
    SkillEndsAt = Now + (Guard ? .6 : .34);
    SkillRecoveryAt = SkillEndsAt + 1.0;
    if (Guard) GuardReadyAt = Now + 4.0; else CurrentReadyAt = Now + 5.0;
    SkillStopReason = Guard ? FName(TEXT("Guarding")) : FName(TEXT("PassingCurrent"));
    if (!Guard)
    {
        auto* Movement = Character->GetCharacterMovement();
        CurrentStart = CurrentProgressPoint = Character->GetActorLocation();
        CurrentProgressAt = Now;
        CurrentGoal = Destination;
        CurrentDirection = (Destination - CurrentStart).GetSafeNormal2D();
        CurrentBaseSpeed = ConquerOwnedSpeed;
        CurrentSavedAcceleration = Movement->MaxAcceleration;
        bCurrentSavedWalkOffLedges = Movement->bCanWalkOffLedges;
        ConquerOwnedSpeed = 720.f; // same speed owner; no competing lease
        Movement->MaxWalkSpeed = ConquerOwnedSpeed;
        Movement->MaxAcceleration = 3600.f;
        Movement->bCanWalkOffLedges = false;
        bCurrentMovementLease = true;
        Character->OnCharacterMovementUpdated.AddUniqueDynamic(this, &AJadeboundProgressionActor::OnSkillMovementUpdated);
    }
    bSkillDispatching = false;
    Feedback = FText::FromString(Guard ? TEXT("Rooted Guard: frontal damage reduced for 0.6 seconds.") : TEXT("Passing Current: a short ground step."));
}

void AJadeboundProgressionActor::OnSkillMovementUpdated(float, FVector, FVector)
{
    bSkillInputQueued = false;
    SkillQueuedInput = FVector::ZeroVector;
    SkillQueuedFrame = MAX_uint64;
}

void AJadeboundProgressionActor::CancelSkills(FName Reason, bool bStopVelocity)
{
    ACharacter* Character = SkillPawn.Get();
    const bool WasStep = ActiveSkill == 2;
    ActiveSkill = 0; // latch before any native callbacks
    if (bSkillInputQueued && Character && Character == ResolveClickWalkCharacter() &&
        SkillQueuedFrame != MAX_uint64 && GFrameCounter >= SkillQueuedFrame && GFrameCounter - SkillQueuedFrame <= 1 &&
        Character->GetPendingMovementInputVector() == SkillQueuedInput) Character->ConsumeMovementInputVector();
    bSkillInputQueued = false; SkillQueuedFrame = MAX_uint64; SkillQueuedInput = FVector::ZeroVector;
    if (Character)
    {
        Character->OnCharacterMovementUpdated.RemoveDynamic(this, &AJadeboundProgressionActor::OnSkillMovementUpdated);
        auto* Movement = Character->GetCharacterMovement();
        if (Movement && bCurrentMovementLease && ConquerMovementPawn.Get() == Character)
        {
            if (Movement->MaxWalkSpeed == ConquerOwnedSpeed) Movement->MaxWalkSpeed = CurrentBaseSpeed;
            ConquerOwnedSpeed = CurrentBaseSpeed;
            if (Movement->MaxAcceleration == 3600.f) Movement->MaxAcceleration = CurrentSavedAcceleration;
            if (!Movement->bCanWalkOffLedges) Movement->bCanWalkOffLedges = bCurrentSavedWalkOffLedges;
        }
        if (WasStep && bStopVelocity && Movement && Character == ResolveClickWalkCharacter() && !IsManualWalkInputActive())
            Movement->StopMovementImmediately();
    }
    bCurrentMovementLease = false;
    SkillPawn.Reset();
    if (WasStep || Reason == TEXT("ContextLost")) SkillStopReason = Reason;
}

void AJadeboundProgressionActor::TickSkills(float DeltaSeconds)
{
    bSkillInputQueued = false; SkillQueuedFrame = MAX_uint64; SkillQueuedInput = FVector::ZeroVector;
    const FKey Keys[] = {EKeys::F1,EKeys::F2,EKeys::F3,EKeys::F4,EKeys::F5,EKeys::F6,EKeys::F7,EKeys::F8,EKeys::F9,EKeys::F10};
    if (Player)
    {
        if (!Player->IsInputKeyDown(EKeys::RightMouseButton) && !Player->WasInputKeyJustPressed(EKeys::RightMouseButton)) bSkillRightHeld = false;
        for (int32 N = 0; N < 10; ++N)
            if (!Player->IsInputKeyDown(Keys[N]) && !Player->WasInputKeyJustPressed(Keys[N])) bSkillKeyHeld[N] = false;
    }
    ACharacter* Character = ResolveClickWalkCharacter();
    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0;
    if (!Character || !IsProgressionReady() || bUnsavedCandidate || bMenuOpen || State.EquippedWeapon != 1 || !FMath::IsFinite(Now))
    { CancelSkills(TEXT("ContextLost"), false); return; }
    if (SkillResourcePawn.Get() != Character)
    { CancelSkills(TEXT("NewLife"), false); SkillResourcePawn = Character; SkillStamina = 100.f; }
    if (ActiveSkill && (SkillPawn.Get() != Character || IsManualWalkInputActive() || !IsClickWalkAvailable()))
    { CancelSkills(TEXT("Interrupted"), false); return; }
    if (ActiveSkill && Now >= SkillEndsAt) { CancelSkills(TEXT("Completed"), true); return; }
    if (!ActiveSkill)
    {
        if (Now >= SkillRecoveryAt && FMath::IsFinite(DeltaSeconds))
            SkillStamina = FMath::Clamp(SkillStamina + 12.f * FMath::Clamp(DeltaSeconds, 0.f, .1f), 0.f, 100.f);
        return;
    }
    if (ActiveSkill != 2) return;
    auto* Movement = Character->GetCharacterMovement();
    const FVector Position = Character->GetActorLocation();
    const FVector Remaining = CurrentGoal - Position;
    if (!Movement || !bCurrentMovementLease || Movement->MaxWalkSpeed != ConquerOwnedSpeed ||
        Movement->MaxAcceleration != 3600.f || Movement->bCanWalkOffLedges || Position.ContainsNaN())
    { CancelSkills(TEXT("MovementOwnerLost"), false); return; }
    if (Remaining.Size2D() <= 10.0 || FVector::DotProduct(Remaining.GetSafeNormal2D(),CurrentDirection) <= 0.0 ||
        FVector::DistSquared2D(CurrentStart, Position) >= FMath::Square(180.0))
    { CancelSkills(TEXT("Arrived"), true); return; }
    if (FVector::DistSquared2D(Position,CurrentProgressPoint) >= 1.0)
    { CurrentProgressPoint = Position; CurrentProgressAt = Now; }
    if (Now - CurrentProgressAt > .15 || !Character->GetPendingMovementInputVector().IsNearlyZero())
    { CancelSkills(TEXT("BlockedOrOtherInput"), false); return; }
    Character->AddMovementInput(CurrentDirection, 1.f, false);
    SkillQueuedInput = Character->GetPendingMovementInputVector();
    bSkillInputQueued = !SkillQueuedInput.IsNearlyZero(); SkillQueuedFrame = GFrameCounter;
}

float AJadeboundProgressionActor::GuardDamageForImpact(ACharacter* Pawn, ACharacter* Target, float Damage)
{
    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0;
    if (ActiveSkill != 1 || SkillPawn.Get() != Pawn || Pawn != ResolveClickWalkCharacter() ||
        State.EquippedWeapon != 1 || bMenuOpen || !IsProgressionReady() || bUnsavedCandidate ||
        !IsValid(Target) || !FMath::IsFinite(Now) || Now >= SkillEndsAt) return Damage;
    const FVector Toward = (Target->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
    if (Toward.IsNearlyZero() || FVector::DotProduct(Pawn->GetActorForwardVector().GetSafeNormal2D(),Toward) < .5) return Damage;
    if (GuardMitigatedImpacts < MAX_int64) ++GuardMitigatedImpacts;
    return Damage * .3f;
}

bool AJadeboundProgressionActor::IsWorldPointerGestureActive() const
{
    if (Player && (Player->IsInputKeyDown(EKeys::LeftMouseButton) || Player->WasInputKeyJustPressed(EKeys::LeftMouseButton) ||
        Player->IsInputKeyDown(EKeys::RightMouseButton) || Player->WasInputKeyJustPressed(EKeys::RightMouseButton))) return true;
    if (FSlateApplication::IsInitialized())
    {
        const auto& Pressed = FSlateApplication::Get().GetPressedMouseButtons();
        return Pressed.Contains(EKeys::LeftMouseButton) || Pressed.Contains(EKeys::RightMouseButton);
    }
    return false;
}

void AJadeboundProgressionActor::ApplyConquerMovementPolicy(ACharacter* Character)
{
    if (!Character || Character != ResolveClickWalkScope()) return;
    auto* Movement = Character->GetCharacterMovement();
    if (!Movement || !FMath::IsFinite(Movement->MaxWalkSpeed) || Movement->MaxWalkSpeed <= 0.f) return;
    RestoreConquerMovementPolicy();
    ConquerMovementPawn = Character;
    bSavedControllerYaw = Character->bUseControllerRotationYaw;
    bSavedOrientMovement = Movement->bOrientRotationToMovement;
    bSavedDesiredRotation = Movement->bUseControllerDesiredRotation;
    SavedMovementRotationRate = Movement->RotationRate;
    SavedMovementSpeed = Movement->MaxWalkSpeed;
    Character->bUseControllerRotationYaw = false;
    Movement->bUseControllerDesiredRotation = false;
    Movement->bOrientRotationToMovement = true;
    Movement->RotationRate = FRotator(0.f,360.f,0.f);
    ConquerOwnedSpeed = bConquerRun ? SavedMovementSpeed : SavedMovementSpeed * .45f;
    Movement->MaxWalkSpeed = ConquerOwnedSpeed;
    bConquerMovementPolicy = true; // Character facing changes; controller/camera rotation does not.
}

void AJadeboundProgressionActor::RestoreConquerMovementPolicy()
{
    if (auto* Character = ConquerMovementPawn.Get())
        if (auto* Movement = Character->GetCharacterMovement())
            if (bConquerMovementPolicy)
            {
                if (!Character->bUseControllerRotationYaw) Character->bUseControllerRotationYaw = bSavedControllerYaw;
                if (Movement->bOrientRotationToMovement) Movement->bOrientRotationToMovement = bSavedOrientMovement;
                if (!Movement->bUseControllerDesiredRotation) Movement->bUseControllerDesiredRotation = bSavedDesiredRotation;
                if (Movement->RotationRate == FRotator(0.f,360.f,0.f)) Movement->RotationRate = SavedMovementRotationRate;
                if (Movement->MaxWalkSpeed == ConquerOwnedSpeed) Movement->MaxWalkSpeed = SavedMovementSpeed;
            }
    bConquerMovementPolicy = false;
    ConquerMovementPawn.Reset();
}

void AJadeboundProgressionActor::ToggleConquerWalkRun()
{
    CancelSkills(TEXT("WalkRunChanged"), true);
    ACharacter* Character = ResolveClickWalkScope();
    if (!Character || bMenuOpen || !bConquerMovementPolicy || ConquerMovementPawn.Get() != Character) return;
    auto* Movement = Character->GetCharacterMovement();
    if (!Movement || Movement->MaxWalkSpeed != ConquerOwnedSpeed) return;
    bConquerRun = !bConquerRun;
    ConquerOwnedSpeed = bConquerRun ? SavedMovementSpeed : SavedMovementSpeed * .45f;
    Movement->MaxWalkSpeed = ConquerOwnedSpeed;
    Feedback = FText::FromString(bConquerRun ? TEXT("Run selected. CapsLock switches to walking.") : TEXT("Walk selected. CapsLock switches to running."));
}
void AJadeboundProgressionActor::HandleManualWalkAction(const FInputActionInstance&)
{ StopClickWalk(TEXT("ManualInput"), false); }
void AJadeboundProgressionActor::HandleManualWalkKey()
{ StopClickWalk(TEXT("ManualInput"), false); }
void AJadeboundProgressionActor::OnClickWalkMovementUpdated(float, FVector, FVector)
{
    // An actual native movement update consumes the queued contribution. Never
    // infer identity merely because a later manual vector happens to be equal.
    if (bClickWalkInputQueued)
        if (auto* Character = ClickWalkPawn.Get())
            bClickWalkManualObserved |= !Character->GetLastMovementInputVector().Equals(ClickWalkQueuedInput, 0.0001);
    bClickWalkInputQueued = false;
    ClickWalkQueuedInput = FVector::ZeroVector;
    ClickWalkQueuedFrame = MAX_uint64;
}
void AJadeboundProgressionActor::StopClickWalk(FName Reason, bool bStopVelocity)
{
    CancelSkills(Reason, bStopVelocity);
    ConquerInteractionIntent = FJadeboundConquerInteractionIntent(); // latch before any stop callback
    ACharacter* Character = ClickWalkPawn.Get();
    const bool bWasActive = bClickWalkActive;
    // A lease survives only to the next input frame, on the same normal walking
    // context. Changed input is never consumed. Missing movement callbacks do
    // not prove ownership and the lease is expired unconditionally in Tick.
    if (bClickWalkInputQueued && Character && Character == ResolveClickWalkCharacter() &&
        ClickWalkPlayer.Get() == Player && ClickWalkQueuedFrame != MAX_uint64 &&
        GFrameCounter >= ClickWalkQueuedFrame && GFrameCounter - ClickWalkQueuedFrame <= 1 &&
        Character->GetPendingMovementInputVector() == ClickWalkQueuedInput)
        Character->ConsumeMovementInputVector();
    bClickWalkInputQueued = false;
    ClickWalkQueuedInput = FVector::ZeroVector;
    ClickWalkQueuedFrame = MAX_uint64;
    if (Character) Character->OnCharacterMovementUpdated.RemoveDynamic(this, &AJadeboundProgressionActor::OnClickWalkMovementUpdated);
    if (bWasActive && bStopVelocity && Character && Character == ResolveClickWalkCharacter() &&
        !IsManualWalkInputActive()) Character->GetCharacterMovement()->StopMovementImmediately();
    bClickWalkActive = false;
    bClickWalkManualObserved = false;
    ClickWalkPawn.Reset();
    ClickWalkPlayer.Reset();
    ConquerAttackTarget = nullptr;
    ConquerTargetLife = -1;
    ConquerPathPoints.Reset();
    ConquerPathIndex = 0;
    if (bWasActive)
    {
        ClickWalkStopReason = Reason;
        if (Reason == TEXT("Blocked")) Feedback = FText::FromString(TEXT("The route is blocked. Choose a clear destination."));
        else if (Reason == TEXT("Timeout")) Feedback = FText::FromString(TEXT("Walking stopped. Choose a closer destination."));
    }
}
void AJadeboundProgressionActor::TickClickWalk()
{
    // TG_PostUpdateWork: prior-frame input ownership expires whether or not
    // CharacterMovement delivered its normal consumption callback.
    bClickWalkInputQueued = false;
    ClickWalkQueuedInput = FVector::ZeroVector;
    ClickWalkQueuedFrame = MAX_uint64;
    if (!bClickWalkActive) return;
    ACharacter* Character = ResolveClickWalkCharacter();
    if (!Character || Character != ClickWalkPawn.Get() || Player != ClickWalkPlayer.Get() ||
        !IsClickWalkAvailable()) { StopClickWalk(TEXT("ContextLost"), false); return; }
    if (bClickWalkManualObserved || IsManualWalkInputActive()) { StopClickWalk(TEXT("ManualInput"), false); return; }
    const FVector Position = Character->GetActorLocation();
    const double Now = GetWorld()->GetTimeSeconds();
    if (!FMath::IsFinite(Now) || !FMath::IsFinite(ClickWalkStartedAt) || Now < ClickWalkStartedAt ||
        Now - ClickWalkStartedAt >= 8.0) { StopClickWalk(TEXT("Timeout"), true); return; }
    // Arrival can synchronously refresh visuals, open UI or end this actor.
    // No further member access occurs after the one terminal dispatch.
    if (ConquerInteractionIntent.DropId >= 0 && TickConquerInteraction(Character)) return;
    if (ConquerAttackTarget)
    {
        if (State.EquippedWeapon == 0) { StopClickWalk(TEXT("Unarmed"), true); return; }
        if (!IsValid(ConquerAttackTarget)) { StopClickWalk(TEXT("TargetDestroyed"), false); return; }
        const auto Health = ReadHealthSample(ConquerAttackTarget, TEXT("CombatHealth"), TEXT("CombatMaxHealth"));
        const auto* Enabled = Field<FBoolProperty>(ConquerAttackTarget, TEXT("CombatEnabled"));
        if (!IsValid(ConquerAttackTarget) || ConquerAttackTarget != LegacyTarget || ConquerAttackTarget->IsHidden() ||
            !Health.bAvailable || Health.Current <= 0.0 || !Enabled || !Enabled->GetPropertyValue_InContainer(ConquerAttackTarget) ||
            !Field<FIntProperty>(LegacyLoop, TEXT("RespawnCount")) || ReadInt(LegacyLoop, TEXT("RespawnCount")) != ConquerTargetLife ||
            !Field<FIntProperty>(LegacyLoop, TEXT("LifeState")) || ReadInt(LegacyLoop, TEXT("LifeState")) != 0)
        { StopClickWalk(TEXT("TargetLifeEnded"), true); return; }
        if (TryConquerAttack(Character)) return; // may synchronously end this actor; no further access
        const FVector Destination = ConquerAttackTarget->GetActorLocation();
        if (Destination.ContainsNaN() || FVector::DistSquared2D(Position, Destination) > FMath::Square(800.0))
        { StopClickWalk(TEXT("TargetTooFar"), true); return; }
        if (!ConquerPathPoints.IsValidIndex(ConquerPathIndex) ||
            (Now >= ConquerNextPathAt && FVector::DistSquared2D(Destination, ConquerTargetPathLocation) > FMath::Square(40.0)))
        {
            if (!BuildConquerPath(Character, Destination, true)) { StopClickWalk(TEXT("TargetPathLost"), true); return; }
            ConquerTargetPathLocation = Destination;
            ConquerNextPathAt = Now + .5;
        }
    }
    if (!ConquerPathPoints.IsValidIndex(ConquerPathIndex)) { StopClickWalk(TEXT("Arrived"), true); return; }
    FVector Direction = ConquerPathPoints[ConquerPathIndex] - Position;
    Direction.Z = 0.0;
    double Distance = Direction.Size();
    const bool Last = ConquerPathIndex == ConquerPathPoints.Num()-1;
    if (Distance <= (Last ? 10.0 : 25.0))
    {
        if (Last) { StopClickWalk(TEXT("Arrived"), true); return; }
        ++ConquerPathIndex;
        Direction = ConquerPathPoints[ConquerPathIndex] - Position;
        Direction.Z = 0.0;
        Distance = Direction.Size();
    }
    if (!FMath::IsFinite(Distance) || Distance <= UE_SMALL_NUMBER)
    { StopClickWalk(TEXT("InvalidPosition"), false); return; }
    if (FVector::DistSquared2D(Position, ClickWalkProgressPoint) >= FMath::Square(1.0))
    { ClickWalkProgressPoint = Position; ClickWalkProgressAt = Now; }
    if (Now - ClickWalkProgressAt >= .5) { StopClickWalk(TEXT("Blocked"), true); return; }
    if (!Character->GetPendingMovementInputVector().IsNearlyZero())
    { StopClickWalk(TEXT("OtherInput"), false); return; }
    // String-pulled full nav waypoints choose detours; existing native movement
    // supplies acceleration, facing and swept collision, without teleporting.
    Direction /= Distance;
    const float Strength = static_cast<float>(FMath::Clamp(Distance / 120.0, .1, 1.0));
    Character->AddMovementInput(Direction, Strength, false);
    ClickWalkQueuedInput = Character->GetPendingMovementInputVector();
    bClickWalkInputQueued = !ClickWalkQueuedInput.IsNearlyZero();
    ClickWalkQueuedFrame = GFrameCounter;
}
void AJadeboundProgressionActor::BindAttackActionGate()
{
    auto* Gate = Cast<UEnhancedInputComponent>(InputComponent);
    if (!Gate || !Player || !Player->IsLocalController() || !Hero) return;
    if (AttackGateComponent.Get() == Gate && AttackGatePawn.Get() == Hero &&
        AttackGateBindingHandles.Num() == 3) return;
    if (auto* Previous = AttackGateComponent.Get())
        for (uint32 Handle : AttackGateBindingHandles) Previous->RemoveBindingByHandle(Handle);
    AttackGateBindingHandles.Reset();
    AttackGateComponent.Reset();
    AttackGatePawn.Reset();

    const auto* HeroInput = Cast<UEnhancedInputComponent>(Hero->InputComponent);
    if (!HeroInput) return; // Possession may not have installed pawn bindings yet.
    const UInputAction* AttackAction = nullptr;
    for (const auto& Binding : HeroInput->GetActionEventBindings())
        if (Binding && Binding->IsBoundToObject(Hero) && Binding->GetAction() &&
            Binding->GetAction()->GetPathName() ==
                TEXT("/Game/Variant_Combat/Input/Actions/IA_ComboAttack.IA_ComboAttack"))
        {
            AttackAction = Binding->GetAction();
            break;
        }
    if (!AttackAction) return;
    for (ETriggerEvent Event : {ETriggerEvent::Started, ETriggerEvent::Triggered, ETriggerEvent::Ongoing})
    {
        auto& Binding = Gate->BindAction(AttackAction, Event, this,
            &AJadeboundProgressionActor::HandleAttackActionGate);
        Binding.SetShouldConsume(true);
        AttackGateBindingHandles.Add(Binding.GetHandle());
    }
    AttackGateComponent = Gate;
    AttackGatePawn = Hero;
    RefreshAttackActionGate();
}
void AJadeboundProgressionActor::RefreshAttackActionGate()
{
    if (auto* Gate = AttackGateComponent.Get())
        for (const auto& Binding : Gate->GetActionEventBindings())
            if (Binding && AttackGateBindingHandles.Contains(Binding->GetHandle()))
                Binding->SetShouldConsume(ResolveClickWalkScope() != nullptr || IsAttackBlocked());
}
void AJadeboundProgressionActor::HandleAttackActionGate(const FInputActionInstance& Action)
{
    if (bDevelopmentModel)
    {
        if (Action.GetTriggerEvent() == ETriggerEvent::Started && bRecipientCombatEnabled) RequestNativeAttack();
        return; // Preserve the template action consumer; never unblock the old combat graph.
    }
    if (ResolveClickWalkScope()) return; // Only the selected-target dispatcher may start a swing.
    StopClickWalk(TEXT("Attack"), false);
    // This listener consumes the Enhanced Input action, rather than only its
    // legacy key binding. Equipped gameplay still reaches the existing hero.
    if (State.EquippedWeapon == 0) AttackWithoutWeaponFeedback();
}
bool AJadeboundProgressionActor::HasLivingPossessedHero() const
{
    if (bReviveWaiting) return false;
    if (!IsValid(Hero) || !IsValid(Player) || Player->GetPawn() != Hero) return false;
    const auto* Health = Field<FNumericProperty>(Hero, TEXT("Current HP"));
    if (!Health || !Health->IsFloatingPoint()) return false;
    const double Value = Health->GetFloatingPointPropertyValue(Health->ContainerPtrToValuePtr<void>(Hero));
    return FMath::IsFinite(Value) && Value > 0.0;
}
FJadeboundHealthPresentation AJadeboundProgressionActor::GetHealthPresentation() const
{
    FJadeboundHealthPresentation View;
    // Legacy V006 health is not replicated. Never present it as another
    // player's authoritative health, or as the development multiplayer model.
    if (bDevelopmentModel || GetNetMode() != NM_Standalone || !IsValid(Player) || !Player->IsLocalController()) return View;
    View.bContextAvailable = true;
    View.Player = Player;
    View.Pawn = Player->GetPawn();
    if (IsValid(Hero) && View.Pawn.Get() == Hero && Hero->GetClass()->GetPathName() ==
        TEXT("/Game/Jadebound/Tests/GlaiveCombatV006/BP_GlaiveHero_V006.BP_GlaiveHero_V006_C"))
        View.PlayerHealth = ReadHealthSample(Hero, TEXT("Current HP"), TEXT("Max HP"));
    if (!IsValid(LegacyTarget) || LegacyTarget->GetClass()->GetPathName() !=
        TEXT("/Game/Jadebound/Tests/GlaiveCombatV006/BP_GlaivePracticeTarget_V006.BP_GlaivePracticeTarget_V006_C")) return View;
    View.Target = LegacyTarget;
    // Read even when the legacy loop has already hidden the defeated actor.
    // Actual zero is evidence; a missing actor or failed binding is not.
    View.TargetHealth = ReadHealthSample(LegacyTarget, TEXT("CombatHealth"), TEXT("CombatMaxHealth"));
    View.bTargetHidden = LegacyTarget->IsHidden();
    if (const auto* Enabled = Field<FBoolProperty>(LegacyTarget, TEXT("CombatEnabled")))
        View.bTargetCombatEnabled = Enabled->GetPropertyValue_InContainer(LegacyTarget);
    if (auto* Text = Cast<UTextRenderComponent>(ReadObject(LegacyTarget, TEXT("CombatHealthText"))))
        if (Text->GetOwner() == LegacyTarget) View.LegacyTargetText = Text;
    if (const auto* Character = Cast<ACharacter>(LegacyTarget))
        if (const auto* Capsule = Character->GetCapsuleComponent())
        {
            View.TargetAnchor = LegacyTarget->GetActorLocation() + FVector(0.0,0.0,Capsule->GetScaledCapsuleHalfHeight() + 18.0);
            View.bTargetAnchorAvailable = !View.TargetAnchor.ContainsNaN();
        }
    return View;
}
bool AJadeboundProgressionActor::IsCloseFrameGuardActive() const
{
    // Cover the Slate close frame and one following input frame, independently
    // of held-key state. This also covers a press/release delivered in one frame.
    return LastMenuCloseFrame != MAX_uint64 && GFrameCounter - LastMenuCloseFrame <= 1;
}
bool AJadeboundProgressionActor::IsAttackBlocked() const
{
    if (bDevelopmentModel) return true; // Per-recipient native combat is a later slice.
    const bool bMissingLocalGate = Player && Player->IsLocalController() &&
        (!AttackGateComponent.IsValid() || AttackGatePawn.Get() != Hero ||
         AttackGateBindingHandles.Num() != 3);
    return State.EquippedWeapon == 0 || bMenuOpen || bAwaitingPointerRelease ||
        IsCloseFrameGuardActive() || !HasLivingPossessedHero() || bMissingLocalGate;
}
void AJadeboundProgressionActor::CancelLegacyAttackPresentation()
{
    if (!IsValid(Hero)) return;
    const TWeakObjectPtr<AJadeboundProgressionActor> CancelOwner(this);
    const TWeakObjectPtr<APawn> CancelPawn(Hero);
    // This public zero-argument hero function asks the driver to cancel only
    // this admitted pawn's swing; it does not assign driver phase or ledgers.
    if (UFunction* Cancel = Hero->FindFunction(TEXT("CancelGlaiveSwing")))
        if (Cancel->ParmsSize == 0) Hero->ProcessEvent(Cancel, nullptr);
    if (!CancelOwner.IsValid() || CancelOwner->IsActorBeingDestroyed() || !CancelPawn.IsValid() || Hero != CancelPawn.Get()) return;
    if (auto* Character = Cast<ACharacter>(Hero))
    {
        auto* AttackMontage = Cast<UAnimMontage>(ReadObject(Hero, TEXT("Combo Attack Montage")));
        UAnimInstance* Animation = Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
        if (AttackMontage && Animation && Animation->Montage_IsActive(AttackMontage))
        {
            // The inherited OnInterrupted handler checks a buffered timestamp.
            // Expire only that runtime attack input before interrupting, so it
            // cannot start a second combo during the pack-opening transition.
            if (auto* Cached = Field<FNumericProperty>(Hero, TEXT("Cached Attack Input Time")))
                if (Cached->IsFloatingPoint())
                    Cached->SetFloatingPointPropertyValue(Cached->ContainerPtrToValuePtr<void>(Hero), -1.0e9);
            Animation->Montage_Stop(0.f, AttackMontage);
            if (!CancelOwner.IsValid() || CancelOwner->IsActorBeingDestroyed() || !CancelPawn.IsValid() || Hero != CancelPawn.Get()) return;
        }
        if (auto* Movement = Character->GetCharacterMovement()) Movement->StopMovementImmediately();
    }
}
void AJadeboundProgressionActor::ApplyEquipment()
{
    RefreshAttackActionGate();
    if (bDevelopmentModel)
    {
        if (WeaponComponent) WeaponComponent->ComponentTags.Remove(WeaponTag);
        PublishDevelopmentEquipment();
        if (InputComponent && InputComponent->KeyBindings.IsValidIndex(AttackBindingIndex))
            InputComponent->KeyBindings[AttackBindingIndex].bConsumeInput = true;
        return;
    }
    if (!WeaponComponent || !OriginalGlaiveMesh) return;
    const bool bEquipped = State.EquippedWeapon != 0;
    // The tested legacy driver validates this tag before admitting damage.
    // Removing it rejects the preserved driver's explicit swing admission.
    if (bEquipped && !IsAttackBlocked()) WeaponComponent->ComponentTags.AddUnique(WeaponTag);
    else WeaponComponent->ComponentTags.Remove(WeaponTag);
    WeaponComponent->SetVisibility(bEquipped, true);
    WeaponComponent->SetHiddenInGame(!bEquipped, true);
    UStaticMesh* Desired = State.EquippedWeapon == 2 ? AshwoodSpearMesh.Get() : OriginalGlaiveMesh.Get();
    if (bEquipped && Desired && WeaponComponent->GetStaticMesh() != Desired)
    {
        WeaponComponent->SetStaticMesh(Desired);
        // Imported original spear uses the same +Z grip convention as the glaive.
        WeaponComponent->EmptyOverrideMaterials();
    }
    if (InputComponent && InputComponent->KeyBindings.IsValidIndex(AttackBindingIndex))
        InputComponent->KeyBindings[AttackBindingIndex].bConsumeInput = IsAttackBlocked();
}
void AJadeboundProgressionActor::PublishDevelopmentEquipment()
{
    if (!HasAuthority() || !bServerModelInitialized || !HasDevelopmentContext() || !IsValid(Hero) ||
        !DevelopmentGlaiveMesh || State.Slots.Num() != static_cast<int32>(Jadebound::InventorySlotCount)) return;
    UJadeboundEquipmentPresentationComponent* Presentation = PublicEquipment.Get();
    if (!Presentation || Presentation->GetOwner() != Hero)
    {
        Presentation = Hero->FindComponentByClass<UJadeboundEquipmentPresentationComponent>();
        if (!Presentation)
        {
            Presentation = NewObject<UJadeboundEquipmentPresentationComponent>(Hero, FName(TEXT("JadeboundPublicEquipment")));
            Hero->AddInstanceComponent(Presentation);
            Presentation->SetIsReplicated(true);
            Presentation->RegisterComponent();
        }
        PublicEquipment = Presentation;
    }
    Presentation->PublishFromAuthority(State.EquippedWeapon, DevelopmentGlaiveMesh, AshwoodSpearMesh);
}
void AJadeboundProgressionActor::AttackWithoutWeaponFeedback()
{
    if (bDevelopmentModel && bRecipientCombatEnabled) return; // Only consumed Started routes the private attack intent.
    if (ResolveClickWalkScope()) return; // Suppress the old raw LMB route in Conquer mode.
    StopClickWalk(TEXT("Attack"), false);
    if (bDevelopmentModel) { Feedback = FText::FromString(TEXT("Local authority foundation: native per-player combat is not integrated yet.")); return; }
    if (State.EquippedWeapon == 0) Feedback = FText::FromString(TEXT("Equip a weapon from your bag before attacking."));
}
void AJadeboundProgressionActor::SyncLegacyQuestPresentation()
{
    if (bDevelopmentModel) return;
    if (!LegacyLoop) return;
    // Preserve the original asset. Disable its old quest only in this new map's runtime.
    // Its E handler polls PlayerController directly, so consuming E is insufficient.
    WriteInt(LegacyLoop, TEXT("QuestState"), 0);
    WriteInt(LegacyLoop, TEXT("ParcelCount"), 0);
    WriteInt(LegacyLoop, TEXT("RewardCount"), 0);
    WriteInt(LegacyLoop, TEXT("JadeCoins"), static_cast<int32>(FMath::Min(State.Coins, static_cast<int64>(MAX_int32))));
    if (auto* Property = Field<FBoolProperty>(LegacyLoop, TEXT("LootAvailable"))) Property->SetPropertyValue_InContainer(LegacyLoop, false);
    if (auto* Property = Field<FStructProperty>(LegacyLoop, TEXT("MerchantPoint")))
        *Property->ContainerPtrToValuePtr<FVector>(LegacyLoop) = FVector(10000000.0, 10000000.0, 10000000.0);
    if (auto* OldLoot = Cast<AActor>(ReadObject(LegacyLoop, TEXT("LootActor")))) OldLoot->SetActorHiddenInGame(true);
    if (auto* Text = Cast<UTextRenderComponent>(ReadObject(LegacyLoop, TEXT("StatusText")))) Text->SetVisibility(false);
}
bool AJadeboundProgressionActor::IsMerchantInRange() const
{
    return Hero && !Hero->GetActorLocation().ContainsNaN() && FVector::Dist(Hero->GetActorLocation(), MerchantPoint) <= MerchantRange;
}
bool AJadeboundProgressionActor::AuthorityReady()
{
    if (bDevelopmentModel && (!HasDevelopmentContext() || GetOwner() != Player)) return false;
    if (!HasAuthority() || !HasJournalWriterLease() || !IsProgressionReady() || !Hero || !Player || Player->GetPawn() != Hero) return false;
    if (!HasLivingPossessedHero())
    {
        Feedback = FText::FromString(TEXT("You must respawn before using items or talking."));
        return false;
    }
    return true;
}
void AJadeboundProgressionActor::RefreshState()
{
    if (!Jadebound::Persistence::EncodeStateV2(Core->GetSnapshot(), State))
    {
        SetPersistenceFailure(EJadeboundPersistenceFailureKind::RecoveryRequired,
            FText::FromString(TEXT("Your character state needs recovery. Existing files are protected.")));
        return;
    }
    ForceNetUpdate();
}
bool AJadeboundProgressionActor::DecodeState(const FJadeboundProgressionState& Source, Jadebound::Snapshot& Destination)
{
    return Jadebound::Persistence::DecodeStateV2(Source, Destination);
}
bool AJadeboundProgressionActor::FinishTransaction(const Jadebound::Result& Result, const FText& Success)
{
    if (!Result.Succeeded()) { Feedback = ResultText(Result.Code); return false; }
    RefreshState();
    if (Result.Changed && !WriteSave())
    {
        // A failed readback cannot prove that a write did not reach disk.
        // Freeze this exact candidate; automatic retry serializes it, never the reward.
        RefreshDropVisuals();
        ApplyEquipment();
        return false;
    }
    Feedback = Success;
    ApplyEquipment();
    SyncLegacyQuestPresentation();
    return true;
}

bool AJadeboundProgressionActor::SellItem(int32 Slot, int32 Quantity, int64 ExpectedRevision)
{
    bool Ok = false;
    ON_SCOPE_EXIT { if (HasAuthority() && Player && Player->IsLocalController() && !bExecutingDevelopmentRequest) AcknowledgeAction(TEXT("Sell"), Ok); };
    if (!HasAuthority()) { ServerSell(Slot, Quantity, ExpectedRevision < 0 ? State.Revision : ExpectedRevision); return false; }
    if (!AuthorityReady()) return false;
    if (State.ExactRevision > static_cast<uint64>(MAX_int64)) { Feedback = FText::FromString(TEXT("Exact revision exceeds the UI transaction token range. Items are preserved.")); return false; }
    if (ExpectedRevision >= 0 && ExpectedRevision != State.Revision) { Feedback = FText::FromString(TEXT("Your bag changed. Select the item again.")); return false; }
    if (!IsMerchantInRange()) { Feedback = FText::FromString(TEXT("Move closer to the merchant to buy or sell.")); return false; }
    return Ok = FinishTransaction(Core->Sell(static_cast<uint32>(Slot), Quantity), FText::FromString(TEXT("Sold. Coins and items saved.")));
}
bool AJadeboundProgressionActor::SplitItem(int32 Slot, int32 Quantity, int32 ExpectedItemId, int64 ExpectedRevision)
{
    bool Ok = false;
    ON_SCOPE_EXIT { if (HasAuthority() && Player && Player->IsLocalController() && !bExecutingDevelopmentRequest) AcknowledgeAction(TEXT("Split"), Ok); };
    if (!HasAuthority()) { ServerSplit(Slot, Quantity, ExpectedItemId, ExpectedRevision); return false; }
    if (!IsInGameThread() || !GetWorld() || GetWorld()->IsPaused() || GetWorld()->bIsTearingDown ||
        IsActorBeingDestroyed() || bPersistenceEndingPlay || !Core || !IsValid(Player) ||
        GetOwner() != Player || Player->GetWorld() != GetWorld() || !IsValid(Hero) ||
        Hero->GetWorld() != GetWorld() || Hero->IsActorBeingDestroyed() || !AuthorityReady()) return false;
    // Own-inventory requests need the living possessed owner, not merchant
    // range or client-only menu state. AuthorityReady preserves save holds.
    if (ExpectedRevision < 0 || State.ExactRevision >= static_cast<uint64>(MAX_int64) ||
        State.ExactRevision != static_cast<uint64>(ExpectedRevision) || State.Revision != ExpectedRevision ||
        Core->GetSnapshot().Revision != State.ExactRevision || ExpectedItemId <= 0 ||
        Slot < 0 || Slot >= static_cast<int32>(Jadebound::InventorySlotCount) ||
        !State.Slots.IsValidIndex(Slot) || State.Slots[Slot].ItemId != ExpectedItemId || State.Slots[Slot].Count <= 0)
    { Feedback = FText::FromString(TEXT("Your pack changed. Select the original stack again.")); return false; }
    const auto& OwnedSlot = Core->GetSnapshot().Inventory[static_cast<uint32>(Slot)];
    if (static_cast<int32>(OwnedSlot.Id) != ExpectedItemId || OwnedSlot.Count != static_cast<uint32>(State.Slots[Slot].Count))
    { Feedback = FText::FromString(TEXT("The owned stack changed. Your items are preserved.")); return false; }
    const auto* Definition = Jadebound::ProgressionCore::FindDefinition(OwnedSlot.Id);
    if (!Definition || Definition->StackLimit <= 1 || Quantity <= 0 ||
        static_cast<uint32>(Quantity) >= OwnedSlot.Count)
    { Feedback = FText::FromString(TEXT("Split a positive amount smaller than a stack of two or more items.")); return false; }
    const auto Result = Core->SplitStack(static_cast<uint32>(Slot), Quantity);
    if (Result.Code == Jadebound::ResultCode::InventoryFull)
    { Feedback = FText::FromString(TEXT("Make one empty pack slot before splitting. Your stack is unchanged.")); return false; }
    return Ok = FinishTransaction(Result, FText::FromString(FString::Printf(
        TEXT("Split %d into an empty pack slot. Item total unchanged and saved."), Quantity)));
}
bool AJadeboundProgressionActor::IsLegacyWeaponChangeBlocked() const
{
    return ReadInt(ReadObject(LegacyLoop, TEXT("Driver")), TEXT("Phase")) != 0;
}
bool AJadeboundProgressionActor::EquipItem(int32 Slot, int64 ExpectedRevision)
{
    bool Ok = false;
    ON_SCOPE_EXIT { if (HasAuthority() && Player && Player->IsLocalController() && !bExecutingDevelopmentRequest) AcknowledgeAction(TEXT("Equip"), Ok); };
    if (!HasAuthority()) { ServerEquip(Slot, ExpectedRevision < 0 ? State.Revision : ExpectedRevision); return false; }
    if (!AuthorityReady()) return false;
    if (IsRecipientSwingActive()) { Feedback = FText::FromString(TEXT("Finish your swing before changing weapons.")); return false; }
    if (State.ExactRevision > static_cast<uint64>(MAX_int64)) { Feedback = FText::FromString(TEXT("Exact revision exceeds the UI transaction token range. Equipment is preserved.")); return false; }
    if (ExpectedRevision >= 0 && ExpectedRevision != State.Revision) { Feedback = FText::FromString(TEXT("Your bag changed. Select the weapon again.")); return false; }
    if (ReadInt(ReadObject(LegacyLoop, TEXT("Driver")), TEXT("Phase")) != 0) { Feedback = FText::FromString(TEXT("Finish your attack before changing weapons.")); return false; }
    if (State.Slots.IsValidIndex(Slot) && State.Slots[Slot].ItemId == 2 && !AshwoodSpearMesh)
    { Feedback = FText::FromString(TEXT("The spear asset is unavailable. Equipment is unchanged.")); return false; }
    return Ok = FinishTransaction(Core->Equip(static_cast<uint32>(Slot)), FText::FromString(TEXT("Weapon equipped and saved.")));
}
bool AJadeboundProgressionActor::UseItem(int32 Slot, int32 ExpectedItemId, int64 ExpectedRevision)
{
    return UseOwnedItem(Slot,ExpectedItemId,ExpectedRevision,EItemUseContext::Pack);
}
bool AJadeboundProgressionActor::UseOwnedItem(int32 Slot, int32 ExpectedItemId, int64 ExpectedRevision, EItemUseContext Context)
{
    // Legacy Hero health is not replicated. Refuse remote/development use before
    // consumption; no client amount, target, grant or healing RPC exists here.
    if (!IsInGameThread() || bDevelopmentModel || GetNetMode() != NM_Standalone || !HasAuthority())
    { Feedback = FText::FromString(TEXT("Balm use is available only for the local standalone hero.")); return false; }
    if (bBalmUseInProgress) return false;
    if (bPersistenceBlocked || bUnsavedCandidate) return false; // preserve journal failure feedback
    const bool AllowedContext=Context==EItemUseContext::Pack ? bMenuOpen : IsHotbarUseContextAvailable();
    ACharacter* Pawn = ResolveClickWalkScope();
    if (!Pawn || Pawn->IsActorBeingDestroyed() || Pawn->IsHidden() || !GetWorld() || GetWorld()->IsPaused() ||
        !AllowedContext || IsCloseFrameGuardActive() || !AuthorityReady() || !Core || BalmUseAdmissions == MAX_int64)
    { Feedback = FText::FromString(TEXT("Use balm as the living local hero from your pack or assigned key.")); return false; }
    if (ExpectedRevision < 0 || State.ExactRevision >= static_cast<uint64>(MAX_int64) ||
        State.ExactRevision != static_cast<uint64>(ExpectedRevision) || State.Revision != ExpectedRevision ||
        Core->GetSnapshot().Revision != State.ExactRevision ||
        Slot < 0 || Slot >= static_cast<int32>(Jadebound::InventorySlotCount) ||
        !State.Slots.IsValidIndex(Slot) || State.Slots[Slot].ItemId != ExpectedItemId || State.Slots[Slot].Count <= 0)
    { Feedback = FText::FromString(TEXT("Your pack changed. Select the original item again.")); return false; }
    const auto& OwnedSlot = Core->GetSnapshot().Inventory[static_cast<uint32>(Slot)];
    if (static_cast<int32>(OwnedSlot.Id) != ExpectedItemId || OwnedSlot.Count != static_cast<uint32>(State.Slots[Slot].Count))
    { Feedback = FText::FromString(TEXT("The owned item snapshot changed. Balm is preserved.")); return false; }
    const auto* Definition = Jadebound::ProgressionCore::FindDefinition(static_cast<Jadebound::ItemId>(ExpectedItemId));
    if (!Definition || Definition->Id != Jadebound::ItemId::Balm || Definition->Weapon ||
        Definition->HealingPercent != 25)
    { Feedback = FText::FromString(TEXT("This item has no use action.")); return false; }
    // Explicit double storage adapter for the exact V006 Hero checked above.
    // ResetHP and negative damage are not amount-heal APIs and are never called.
    const auto* Current = Field<FDoubleProperty>(Pawn, TEXT("Current HP"));
    const auto* Maximum = Field<FDoubleProperty>(Pawn, TEXT("Max HP"));
    if (!Current || !Maximum)
    { Feedback = FText::FromString(TEXT("This hero's health contract is unavailable. Balm is preserved.")); return false; }
    const double Before = Current->GetFloatingPointPropertyValue(Current->ContainerPtrToValuePtr<void>(Pawn));
    const double Max = Maximum->GetFloatingPointPropertyValue(Maximum->ContainerPtrToValuePtr<void>(Pawn));
    if (!FMath::IsFinite(Before) || !FMath::IsFinite(Max) || Max <= 0.0 || Before <= 0.0 || Before > Max)
    { Feedback = FText::FromString(TEXT("This hero cannot receive healing. Balm is preserved.")); return false; }
    if (Before == Max)
    { Feedback = FText::FromString(TEXT("Your health is full. Balm is preserved.")); return false; }
    // Difference-first clamp avoids overflowing a finite extreme Max HP.
    const double Recovery = FMath::Min(Max * (static_cast<double>(Definition->HealingPercent) / 100.0), Max - Before);
    const double After = FMath::Min(Max, Before + Recovery);
    if (!FMath::IsFinite(After) || After <= Before)
    { Feedback = FText::FromString(TEXT("No measurable healing is available. Balm is preserved.")); return false; }
    TGuardValue<bool> SingleFlight(bBalmUseInProgress, true);
    const Jadebound::Snapshot BeforeSnapshot = Core->GetSnapshot();
    const auto Result = Core->Consume(static_cast<uint32>(Slot));
    if (!Result.Succeeded()) { Feedback = ResultText(Result.Code); return false; }
    // These direct property operations invoke no Blueprint callback or latent
    // work. Cost and runtime effect are paired before the synchronous journal.
    Current->SetFloatingPointPropertyValue(Current->ContainerPtrToValuePtr<void>(Pawn), After);
    const double Observed = Current->GetFloatingPointPropertyValue(Current->ContainerPtrToValuePtr<void>(Pawn));
    if (Observed != After || Maximum->GetFloatingPointPropertyValue(Maximum->ContainerPtrToValuePtr<void>(Pawn)) != Max)
    {
        Current->SetFloatingPointPropertyValue(Current->ContainerPtrToValuePtr<void>(Pawn), Before);
        Core->LoadSnapshot(BeforeSnapshot); // no disk write occurred; original valid snapshot
        Feedback = FText::FromString(TEXT("Healing readback failed. No item transaction was saved."));
        return false;
    }
    ++BalmUseAdmissions;
    LastBalmUseRevision = static_cast<int64>(Result.Revision);
    BalmLastHealthBefore = Before;
    BalmLastHealthAfter = Observed;
    // An uncertain write holds this consumed/healed candidate. Save serializes
    // it only; Save/Reload never call UseItem or replay this effect.
    const bool Saved = FinishTransaction(Result, FText::FromString(TEXT("Balm used. Health recovered and one balm saved as consumed.")));
    if (!Saved && bUnsavedCandidate)
        Feedback = FText::Format(FText::FromString(TEXT("Balm healing was applied once. {0}")), Feedback);
    return Saved;
}

void AJadeboundProgressionActor::PollDefeats()
{
    if (bDevelopmentModel) return; // A shared counter has no player recipient.
    if (!HasJournalWriterLease() || !LegacyLoop || !LegacyTarget || !bReady || bPersistenceBlocked || bUnsavedCandidate) return;
    const int32 Current = ReadInt(LegacyLoop, TEXT("DefeatCount"));
    if (Current <= SeenDefeats) return;
    const auto Id = Core->GetSnapshot().NextDropId;
    const auto Result = Core->RecordRaiderDefeat(Id);
    if (!Result.Succeeded()) { Feedback = ResultText(Result.Code); return; }
    const bool bQuest = Core->GetSnapshot().QuestKillDropId == Id;
    FJadeboundSavedDrop Drop;
    Drop.DropId = static_cast<int64>(Id);
    Drop.ItemId = bQuest ? 5 : 3;
    Drop.Count = bQuest ? 1 : 2;
    Drop.Location = LegacyTarget->GetActorLocation() + FVector(0,0,50);
    PendingDrops.Add(Drop);
    // Acknowledge only this admitted drop. An uncertain save holds this
    // exact candidate; retry must never record the same defeat again.
    ++SeenDefeats;
    FinishTransaction(Result, FText::FromString(bQuest ? TEXT("The stolen parcel dropped. E nearby to collect it.") : TEXT("Raider dropped iron ore x2. E nearby to collect it.")));
    RefreshDropVisuals();
}
bool AJadeboundProgressionActor::RegisterInteractionHandle(AActor* Visual, int64 DropId, float Radius)
{
    if (!ResolveClickWalkScope() || !IsValid(Visual) || Visual->GetOwner() != this ||
        Visual->GetWorld() != GetWorld() || !IsValid(Visual->GetRootComponent()) ||
        DropId < 0 || !FMath::IsFinite(Radius) || Radius <= 0.f || Radius > 48.f) return false;
    USceneComponent* Root = Visual->GetRootComponent();
    UPrimitiveComponent* Pick = nullptr;
    FVector LocalCenter = FVector::ZeroVector;
    if (DropId == 0)
    {
        // Keep the explicit merchant locator's original bounded sphere.
        auto* Sphere = NewObject<USphereComponent>(Visual, NAME_None, RF_Transient);
        if (!Sphere) return false;
        Sphere->InitSphereRadius(Radius);
        Pick = Sphere;
    }
    else
    {
        // Fit one query-only box to the actual visible prop or text rectangle.
        // World-space margins stay small even when a component is scaled.
        const FVector Scale = Root->GetComponentScale();
        if (Root->GetComponentTransform().ContainsNaN() || Scale.ContainsNaN() ||
            Scale.X <= SMALL_NUMBER || Scale.Y <= SMALL_NUMBER || Scale.Z <= SMALL_NUMBER) return false;
        FVector LocalHalf = FVector::ZeroVector;
        FVector WorldCap = FVector::ZeroVector;
        double Margin = 0.0;
        if (auto* Mesh = Cast<UStaticMeshComponent>(Root))
        {
            if (!IsValid(Mesh->GetStaticMesh())) return false;
            FVector Minimum = FVector::ZeroVector, Maximum = FVector::ZeroVector;
            Mesh->GetLocalBounds(Minimum, Maximum);
            if (Minimum.ContainsNaN() || Maximum.ContainsNaN()) return false;
            LocalCenter = (Minimum + Maximum) * 0.5;
            LocalHalf = (Maximum - Minimum) * 0.5;
            if (LocalHalf.X <= 0.0 || LocalHalf.Y <= 0.0 || LocalHalf.Z <= 0.0) return false;
            Margin = 4.0;
            WorldCap = FVector(48.0,48.0,48.0);
        }
        else if (auto* Text = Cast<UTextRenderComponent>(Root))
        {
            const FBoxSphereBounds LocalText = Text->CalcBounds(FTransform::Identity);
            if (LocalText.Origin.ContainsNaN() || LocalText.BoxExtent.ContainsNaN() ||
                LocalText.BoxExtent.X < 0.0 || LocalText.BoxExtent.Y <= 0.0 || LocalText.BoxExtent.Z <= 0.0) return false;
            // GetRenderMatrix includes vertical text alignment; horizontal
            // alignment is already in CalcBounds. Follow the billboard root.
            const FTransform RenderTransform(Text->GetRenderMatrix());
            if (RenderTransform.ContainsNaN()) return false;
            const FVector WorldCenter = RenderTransform.TransformPosition(LocalText.Origin);
            LocalCenter = Root->GetComponentTransform().InverseTransformPosition(WorldCenter);
            LocalHalf = LocalText.BoxExtent;
            Margin = 3.0;
            WorldCap = FVector(4.0,128.0,24.0);
        }
        else return false; // Unknown visuals keep E; never create a broad fallback.
        LocalHalf += FVector(Margin / Scale.X, Margin / Scale.Y, Margin / Scale.Z);
        const FVector WorldHalf = LocalHalf * Scale;
        if (LocalCenter.ContainsNaN() || LocalHalf.ContainsNaN() || WorldHalf.ContainsNaN() ||
            WorldHalf.X <= 0.0 || WorldHalf.Y <= 0.0 || WorldHalf.Z <= 0.0 ||
            WorldHalf.X > WorldCap.X || WorldHalf.Y > WorldCap.Y || WorldHalf.Z > WorldCap.Z) return false;
        auto* Box = NewObject<UBoxComponent>(Visual, NAME_None, RF_Transient);
        if (!Box) return false;
        Box->InitBoxExtent(LocalHalf);
        Pick = Box;
    }
    Visual->AddInstanceComponent(Pick);
    Pick->SetupAttachment(Root);
    Pick->SetRelativeLocation(LocalCenter);
    Pick->SetGenerateOverlapEvents(false);
    Pick->SetCanEverAffectNavigation(false);
    Pick->SetCollisionObjectType(ECC_WorldDynamic);
    Pick->SetCollisionResponseToAllChannels(ECR_Ignore);
    Pick->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    Pick->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Pick->RegisterComponent();
    if (!Pick->IsRegistered()) { Pick->DestroyComponent(); return false; }
    Visual->SetActorEnableCollision(true);
    InteractionHandles.Add(TWeakObjectPtr<UPrimitiveComponent>(Pick), DropId);
    return true;
}

bool AJadeboundProgressionActor::TryHandleConquerLootCursor(ACharacter* Character)
{
    if (!Character || Character != ResolveClickWalkCharacter() || !IsClickWalkAvailable() ||
        IsManualWalkInputActive()) return false;
    TArray<TEnumAsByte<EObjectTypeQuery>> CursorTypes;
    CursorTypes.Add(UEngineTypes::ConvertToObjectType(ECC_WorldStatic));
    CursorTypes.Add(UEngineTypes::ConvertToObjectType(ECC_WorldDynamic));
    CursorTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));
    FHitResult Hit;
    if (!Player->GetHitResultUnderCursorForObjects(CursorTypes, false, Hit) ||
        !Hit.bBlockingHit || Hit.bStartPenetrating || Hit.ImpactPoint.ContainsNaN()) return false;
    // The same exact four-wall qualification used by LMB permits only ONE
    // same-ray cursor retrace. Physical interaction LOS never uses this filter.
    if (IsQualifiedCursorPerimeter(GetWorld(), Hit.HitObjectHandle.GetCachedActor()))
    {
        const FVector TraceStart = Hit.TraceStart, TraceEnd = Hit.TraceEnd;
        FCollisionQueryParams CursorQuery(SCENE_QUERY_STAT(JadeboundLootCursor), false);
        int32 IgnoredPerimeters = 0;
        for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
            if (IsQualifiedCursorPerimeter(GetWorld(), *It))
            { CursorQuery.AddIgnoredActor(*It); ++IgnoredPerimeters; }
        FHitResult FilteredHit;
        if (TraceStart.ContainsNaN() || TraceEnd.ContainsNaN() || TraceStart.Equals(TraceEnd) ||
            IgnoredPerimeters <= 0 || IgnoredPerimeters > 4 ||
            !GetWorld()->LineTraceSingleByObjectType(FilteredHit, TraceStart, TraceEnd,
                FCollisionObjectQueryParams(CursorTypes), CursorQuery) ||
            !FilteredHit.bBlockingHit || FilteredHit.bStartPenetrating || FilteredHit.ImpactPoint.ContainsNaN()) return false;
        Hit = FilteredHit;
    }
    const int64* Identity = InteractionHandles.Find(TWeakObjectPtr<UPrimitiveComponent>(Hit.GetComponent()));
    if (!Identity || *Identity <= 0) return false; // RMB does not activate the merchant.
    // Existing helper copies identity, revalidates weak handles after callbacks,
    // living authority/range/physical LOS, and delegates the idempotent save.
    return TryHandleConquerInteractionHit(Character, Hit);
}

bool AJadeboundProgressionActor::TryHandleConquerInteractionHit(ACharacter* Character, const FHitResult& Hit)
{
    UPrimitiveComponent* Pick = Hit.GetComponent();
    const int64* Identity = InteractionHandles.Find(TWeakObjectPtr<UPrimitiveComponent>(Pick));
    if (!Identity) return false; // Buildings and foreign actors remain non-interactions.
    FJadeboundConquerInteractionIntent Selection;
    Selection.Pawn = Character;
    Selection.Pick = Pick;
    Selection.Visual = IsValid(Pick) ? Pick->GetOwner() : nullptr;
    Selection.DropId = *Identity; // Copy; no borrowed map/record pointer survives callbacks.
    Selection.ExactRevision = State.ExactRevision;
    Selection.Destination = MerchantPoint;
    Selection.Range = Selection.DropId == 0 ? MerchantRange : PickupRange;
    if (auto* Visual = Selection.Visual.Get()) Selection.VisualLocation = Visual->GetActorLocation();
    if (Selection.DropId > 0)
    {
        Selection.RecordIndex = PendingDrops.IndexOfByPredicate([&Selection](const FJadeboundSavedDrop& Row) { return Row.DropId == Selection.DropId; });
        if (PendingDrops.IsValidIndex(Selection.RecordIndex))
        { Selection.Record = PendingDrops[Selection.RecordIndex]; Selection.Destination = Selection.Record.Location; }
    }
    return HandleConquerInteractionSelection(Selection, true);
}

bool AJadeboundProgressionActor::ValidateConquerInteractionIntent(const FJadeboundConquerInteractionIntent& Selection) const
{
    ACharacter* Character = Selection.Pawn.Get();
    UPrimitiveComponent* Pick = Selection.Pick.Get();
    AActor* Visual = Selection.Visual.Get();
    const int64* Identity = InteractionHandles.Find(Selection.Pick);
    if (!Character || Character != ResolveClickWalkCharacter() || !IsClickWalkAvailable() ||
        !HasAuthority() || !IsProgressionReady() || bUnsavedCandidate || !Core ||
        State.ExactRevision != Selection.ExactRevision || Core->GetSnapshot().Revision != Selection.ExactRevision ||
        !IsValid(Pick) || !Pick->IsRegistered() ||
        Pick->GetCollisionEnabled() != ECollisionEnabled::QueryOnly || Pick->GetCollisionObjectType() != ECC_WorldDynamic ||
        !IsValid(Visual) || Visual->IsActorBeingDestroyed() || Visual->IsHidden() || Pick->GetOwner() != Visual ||
        Visual->GetOwner() != this || Visual->GetWorld() != GetWorld() || !DropVisuals.Contains(Visual) ||
        !Identity || *Identity != Selection.DropId || Selection.DropId < 0 ||
        Selection.Destination.ContainsNaN() || Selection.VisualLocation.ContainsNaN() ||
        Visual->GetActorLocation() != Selection.VisualLocation || !FMath::IsFinite(Selection.Range) || Selection.Range <= 0.f)
        return false;
    if (Selection.DropId == 0)
        return MerchantClickMarker.Get() == Visual && MerchantPoint == Selection.Destination && MerchantRange == Selection.Range;
    if (!PendingDrops.IsValidIndex(Selection.RecordIndex)) return false;
    const FJadeboundSavedDrop& Drop = PendingDrops[Selection.RecordIndex];
    return Drop.DropId == Selection.DropId && Drop.DropId == Selection.Record.DropId && Drop.ItemId == Selection.Record.ItemId &&
        Drop.Count == Selection.Record.Count && Drop.Location == Selection.Record.Location &&
        Drop.Location == Selection.Destination && PickupRange == Selection.Range;
}

bool AJadeboundProgressionActor::StartConquerInteraction(const FJadeboundConquerInteractionIntent& Selection)
{
    if (!ValidateConquerInteractionIntent(Selection)) return false;
    ACharacter* Character = Selection.Pawn.Get();
    if (IsManualWalkInputActive() || FVector::DistSquared2D(Character->GetActorLocation(), Selection.Destination) > FMath::Square(800.0))
    { ConquerControlReason = TEXT("InteractionTooFar"); Feedback = FText::FromString(TEXT("That interaction is too far away. Choose nearby ground first.")); return false; }
    // A persisted drop location is above ground, not a nav endpoint. Project via
    // one real static-floor ray, with no cursor-perimeter or wall exemptions.
    FCollisionQueryParams Query(SCENE_QUERY_STAT(JadeboundInteractionFloor), false);
    Query.AddIgnoredActor(this); Query.AddIgnoredActor(Character);
    for (AActor* Row : DropVisuals) if (IsValid(Row)) Query.AddIgnoredActor(Row);
    FHitResult Floor;
    if (!GetWorld()->LineTraceSingleByObjectType(Floor, Selection.Destination,
        Selection.Destination - FVector(0,0,500), FCollisionObjectQueryParams(ECC_WorldStatic), Query) ||
        !Floor.bBlockingHit || Floor.bStartPenetrating || Floor.ImpactPoint.ContainsNaN() ||
        !Character->GetCharacterMovement()->IsWalkable(Floor))
    { ConquerControlReason = TEXT("InteractionFloorRejected"); Feedback = FText::FromString(TEXT("That interaction has no nearby walkable approach.")); return false; }
    const TWeakObjectPtr<AJadeboundProgressionActor> WeakSelf(this);
    // StartConquerGoal owns all existing full-path, timeout and movement leases;
    // null target keeps this intent separate from the raider swing lifecycle.
    const bool Started = StartConquerGoal(Character, Floor.ImpactPoint, nullptr);
    if (!WeakSelf.IsValid() || WeakSelf->IsActorBeingDestroyed()) return false;
    if (!Started) return false; // Preserve the existing specific navigation failure.
    if (!ValidateConquerInteractionIntent(Selection))
    {
        ConquerControlReason = TEXT("InteractionStartChanged");
        if (!bPersistenceBlocked && !bUnsavedCandidate)
            Feedback = FText::FromString(TEXT("That interaction changed. Select the same merchant or loot again."));
        StopClickWalk(TEXT("InteractionStartChanged"), true);
        return false;
    }
    ConquerInteractionIntent = Selection; // Only the exact original weak selection.
    ConquerControlReason = Selection.DropId == 0 ? TEXT("ApproachingMerchant") : TEXT("ApproachingLoot");
    Feedback = FText::FromString(Selection.DropId == 0 ? TEXT("Approaching the merchant.") : TEXT("Approaching the selected loot."));
    return true;
}

bool AJadeboundProgressionActor::TickConquerInteraction(ACharacter* Character)
{
    const FJadeboundConquerInteractionIntent Selection = ConquerInteractionIntent;
    if (Selection.Pawn.Get() != Character || ConquerAttackTarget || !ValidateConquerInteractionIntent(Selection))
    {
        ConquerControlReason = TEXT("InteractionIntentChanged");
        if (!bPersistenceBlocked && !bUnsavedCandidate)
            Feedback = FText::FromString(TEXT("That interaction changed. Select the same merchant or loot again."));
        StopClickWalk(TEXT("InteractionIntentChanged"), true);
        return true; // Stop may end this actor; no post-callback member access.
    }
    if (FVector::Dist(Character->GetActorLocation(), Selection.Destination) > Selection.Range) return false;
    // HandleSelection clears movement/intent BEFORE the synchronous mutation.
    // Failure (full pack, obstruction, save hold) is terminal, never a tick retry.
    HandleConquerInteractionSelection(Selection, false);
    return true;
}

bool AJadeboundProgressionActor::HandleConquerInteractionSelection(const FJadeboundConquerInteractionIntent& Selection, bool bAllowApproach)
{
    const TWeakObjectPtr<AJadeboundProgressionActor> WeakSelf(this);
    bool Ok = false;
    ON_SCOPE_EXIT
    {
        if (auto* Live = WeakSelf.Get())
            if (!Live->IsActorBeingDestroyed() && Live->HasAuthority() && IsValid(Live->Player) &&
                Live->Player->IsLocalController() && !Live->bExecutingDevelopmentRequest)
                Live->AcknowledgeAction(TEXT("Interact"), Ok, true, Live->bAuthorityMenuOpen);
    };
    // Single-use latch: every exit retains no pending interaction, including
    // synchronous refresh/rebind/EndPlay inside the guarded helpers below.
    StopClickWalk(TEXT("InteractionClick"), true);
    if (!WeakSelf.IsValid() || WeakSelf->IsActorBeingDestroyed()) return true;
    if (bPersistenceBlocked || bUnsavedCandidate)
    {
        ConquerControlReason = TEXT("InteractionPersistenceHeld");
        if (Feedback.IsEmpty()) Feedback = FText::FromString(bUnsavedCandidate ?
            TEXT("Unsaved progress is held. Open your bag and finish Save before interacting.") :
            TEXT("Progression is locked. Interaction is unavailable; preserved saves require recovery."));
        return true;
    }
    if (!ValidateConquerInteractionIntent(Selection) || !AuthorityReady())
    { ConquerControlReason = TEXT("InteractionUnavailable"); Feedback = FText::FromString(TEXT("That interaction is no longer available. Close your pack and select it again after respawning.")); return true; }
    ACharacter* Character = Selection.Pawn.Get();
    UPrimitiveComponent* Pick = Selection.Pick.Get();
    if (FVector::Dist(Character->GetActorLocation(), Selection.Destination) > Selection.Range)
    {
        if (bAllowApproach) StartConquerInteraction(Selection);
        else { ConquerControlReason = TEXT("InteractionOutOfRange"); Feedback = FText::FromString(TEXT("That interaction is still outside reach. Select it again from clear ground.")); }
        return true; // Queued walking does not acknowledge an economic success.
    }
    // Physical LOS is the original strict interaction ray, without cursor wall
    // filtering. Owned query-only labels may not obstruct each other.
    // Lifted loot text is presentation; physical LOS ends at its saved anchor.
    // This can reject a ray that previously cleared an obstacle at label height.
    const FVector From = Character->GetPawnViewLocation(), To = Selection.DropId > 0
        ? Selection.Record.Location : Pick->GetComponentLocation();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(JadeboundInteractionSight), false);
    Query.AddIgnoredActor(this); Query.AddIgnoredActor(Character);
    for (AActor* Row : DropVisuals) if (IsValid(Row)) Query.AddIgnoredActor(Row);
    FHitResult Obstruction;
    if (From.ContainsNaN() || To.ContainsNaN() ||
        GetWorld()->LineTraceSingleByChannel(Obstruction, From, To, ECC_Visibility, Query))
    { ConquerControlReason = TEXT("InteractionBlocked"); Feedback = FText::FromString(TEXT("The interaction is blocked. Move into clear reach first.")); return true; }
    ConquerControlReason = Selection.DropId == 0 ? TEXT("MerchantClick") : TEXT("LootClick");
    Ok = Selection.DropId == 0 ? InteractWithMerchant() : PickupPendingDrop(Selection.DropId);
    return true; // No post-callback member access; scope-exit uses surviving weak self.
}

bool AJadeboundProgressionActor::PickupPendingDrop(int64 DropId)
{
    if (bMenuOpen || (HasAuthority() && bDevelopmentModel && bAuthorityMenuOpen))
    { Feedback = FText::FromString(TEXT("Close your pack to talk or collect loot.")); return false; }
    if (!AuthorityReady() || DropId <= 0) return false;
    const int32 Nearest = PendingDrops.IndexOfByPredicate([DropId](const FJadeboundSavedDrop& Row) { return Row.DropId == DropId; });
    if (Nearest == INDEX_NONE)
    { Feedback = FText::FromString(TEXT("That loot has already been collected.")); return false; }
    const auto Drop = PendingDrops[Nearest];
    if (Drop.Location.ContainsNaN() || !FMath::IsFinite(PickupRange) || PickupRange <= 0.f ||
        FVector::Dist(Hero->GetActorLocation(), Drop.Location) > PickupRange)
    { Feedback = FText::FromString(TEXT("Nothing in reach. Move closer to the merchant or a loot drop.")); return false; }
    const auto Result = Drop.ItemId == 5 ? Core->PickupParcel(static_cast<uint64>(Drop.DropId)) :
        Core->GrantLoot(static_cast<Jadebound::ItemId>(Drop.ItemId), Drop.Count, static_cast<uint64>(Drop.DropId));
    if (Result.Succeeded()) PendingDrops.RemoveAt(Nearest);
    const bool Success = FinishTransaction(Result, FText::FromString(Drop.ItemId == 5 ? TEXT("Parcel recovered. Return to the merchant.") : TEXT("Iron ore x2 collected and saved.")));
    if (Success) RefreshDropVisuals();
    return Success;
}

bool AJadeboundProgressionActor::InteractWithMerchant()
{
    if (bMenuOpen || (HasAuthority() && bDevelopmentModel && bAuthorityMenuOpen))
    { Feedback = FText::FromString(TEXT("Close your pack to talk or collect loot.")); return false; }
    if (!AuthorityReady()) return false;
    if (!IsMerchantInRange()) { Feedback = FText::FromString(TEXT("Nothing in reach. Move closer to the merchant or a loot drop.")); return false; }
    using Q = Jadebound::QuestState;
    const Q Quest = Core->GetSnapshot().Quest;
    if (Quest == Q::NotAccepted)
    {
        // Drain earlier standalone defeats before accepting a new quest.
        // The legacy scalar has no kill-time quest metadata.
        if (!bDevelopmentModel && LegacyLoop && ReadInt(LegacyLoop, TEXT("DefeatCount")) > SeenDefeats)
        {
            Feedback = FText::FromString(TEXT("Loot from your last battle is still arriving. Talk again in a moment."));
            return false;
        }
        return FinishTransaction(Core->AcceptQuest(), FText::FromString(TEXT("Quest accepted: defeat the raider and recover the stolen parcel.")));
    }
    if (Quest == Q::ParcelRecovered)
        return FinishTransaction(Core->TurnInQuest(), FText::FromString(TEXT("Quest complete! Five jade coins and one jade shard. Reward saved once.")));
    if (Quest == Q::Completed) Feedback = FText::FromString(TEXT("Your parcel reward has already been paid. Welcome to the shop."));
    else Feedback = FText::FromString(TEXT("Recover the stolen parcel. You can shop while the quest is active."));
    if (bDevelopmentModel && HasAuthority()) bAuthorityMenuOpen = true;
    else ShowMenu(true);
    return true;
}

bool AJadeboundProgressionActor::Interact()
{
    bool Ok = false;
    ON_SCOPE_EXIT { if (HasAuthority() && Player && Player->IsLocalController() && !bExecutingDevelopmentRequest) AcknowledgeAction(TEXT("Interact"), Ok, true, bAuthorityMenuOpen); };
    // Configured remote requests always get the owning server's rejection or
    // acknowledgment, including E while the client pack is already open.
    if (bDevelopmentModel && !HasAuthority()) { ServerInteract(); return false; }
    if (bMenuOpen || (HasAuthority() && bDevelopmentModel && bAuthorityMenuOpen)) { Feedback = FText::FromString(TEXT("Close your pack to talk or collect loot.")); return false; }
    if (!HasAuthority()) { ServerInteract(); return false; }
    if (!AuthorityReady()) return false;
    int32 Nearest = INDEX_NONE;
    double Distance = PickupRange;
    for (int32 Index = 0; Index < PendingDrops.Num(); ++Index)
    {
        const double Candidate = FVector::Dist(Hero->GetActorLocation(), PendingDrops[Index].Location);
        if (Candidate <= Distance) { Distance = Candidate; Nearest = Index; }
    }
    if (Nearest != INDEX_NONE) return Ok = PickupPendingDrop(PendingDrops[Nearest].DropId);
    return Ok = InteractWithMerchant();
}
void AJadeboundProgressionActor::HandleInteract() { StopClickWalk(TEXT("Interact"), false); Interact(); }
#if WITH_DEV_AUTOMATION_TESTS
bool AJadeboundProgressionActor::EncodeJournalForAutomation(UJadeboundCharacterSave* Save, TArray<uint8>& Bytes)
{
    return EncodeJournal(Save, Bytes); // Reuse the production envelope for explicitly synthetic fixtures.
}
#endif

bool AJadeboundProgressionActor::WriteSave()
{
    if (!HasAuthority() || GetNetMode() == NM_Client || bPersistenceEndingPlay ||
        !GetWorld() || GetWorld()->bIsTearingDown || IsActorBeingDestroyed()) return false;
    if (PersistencePresentation.FailureKind == EJadeboundPersistenceFailureKind::RecoveryRequired) return false;
    if (!HasJournalWriterLease()) return false;
    if (PersistencePresentation.FailureKind == EJadeboundPersistenceFailureKind::Retryable)
    {
        const double Now = FPlatformTime::Seconds();
        if (!FMath::IsFinite(Now) || !FMath::IsFinite(PersistenceRetryAt) || Now < PersistenceRetryAt) return false;
    }
    bUnsavedCandidate = true;
    if (!HeldSaveCandidate)
    {
        auto* NewCandidate = Cast<UJadeboundCharacterSave>(UGameplayStatics::CreateSaveGameObject(UJadeboundCharacterSave::StaticClass()));
        if (!NewCandidate)
        {
            SetPersistenceFailure(EJadeboundPersistenceFailureKind::RecoveryRequired,
                FText::FromString(TEXT("Saving needs recovery. Progress is held and existing files are protected. Keep the game open.")));
            return false;
        }
        NewCandidate->CharacterId = GetSaveCharacterId();
        NewCandidate->State = State;
        NewCandidate->PendingDrops = PendingDrops;
        HeldSaveCandidate = NewCandidate;
    }
    auto* Save = HeldSaveCandidate.Get();
    Jadebound::Snapshot Candidate;
    if (Save->CharacterId != GetSaveCharacterId() || !DecodeState(Save->State, Candidate) || !ValidateDrops(Candidate, Save->PendingDrops))
    {
        SetPersistenceFailure(EJadeboundPersistenceFailureKind::RecoveryRequired,
            FText::FromString(TEXT("Saving needs recovery. Progress is held and existing files are protected. Keep the game open.")));
        return false;
    }
    const int32 Slot = 1 - LastSaveSlot;
    TArray<uint8> Written, ReadBack;
    const FString Path = GetJournalPath(Slot);
    if (Path.IsEmpty() || !EncodeJournal(Save, Written))
    {
        SetPersistenceFailure(EJadeboundPersistenceFailureKind::RecoveryRequired,
            FText::FromString(TEXT("Saving needs recovery. Progress is held and existing files are protected. Keep the game open.")));
        return false;
    }
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
#if WITH_DEV_AUTOMATION_TESTS
    TUniquePtr<IFileHandle> JournalHandle(AutomationJournalOpener ? AutomationJournalOpener(Path) :
        FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*Path, false, false));
#else
    TUniquePtr<IFileHandle> JournalHandle(FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*Path, false, false));
#endif
    const bool bWrittenAndFlushed = JournalHandle && Jadebound::Persistence::WriteAndFlush(*JournalHandle, Written);
    // Close on success and failure before ReadJournal; its reader uses another handle.
    JournalHandle.Reset();
    if (!bWrittenAndFlushed)
    {
        SetPersistenceFailure(EJadeboundPersistenceFailureKind::Retryable,
            FText::FromString(TEXT("Progress could not be saved. Retrying automatically; keep the game open.")));
        return false;
    }
    auto* Verify = ReadJournal(Path, &ReadBack);
    Jadebound::Snapshot VerifiedSnapshot;
    if (!Verify || Verify->CharacterId != GetSaveCharacterId() || !DecodeState(Verify->State, VerifiedSnapshot) || !ValidateDrops(VerifiedSnapshot, Verify->PendingDrops) || Written != ReadBack)
    {
        SetPersistenceFailure(EJadeboundPersistenceFailureKind::Retryable,
            FText::FromString(TEXT("Saving could not be confirmed. Retrying automatically; keep the game open.")));
        return false;
    }
    LastSaveSlot = Slot;
    LastCommittedSave = Verify;
    ClearPersistenceFailure();
    return true;
}
void AJadeboundProgressionActor::SetPersistenceFailure(EJadeboundPersistenceFailureKind Kind, const FText& Message)
{
    bPersistenceBlocked = true;
    PersistencePresentation.FailureKind = Kind;
    PersistencePresentation.Message = Message;
    if (Kind == EJadeboundPersistenceFailureKind::Retryable)
    {
        static const double Delays[] = { 2.0, 4.0, 8.0, 16.0, 30.0 };
        const double Now = FPlatformTime::Seconds();
        const uint8 Step = FMath::Min<uint8>(PersistenceRetryStep, 4);
        if (FMath::IsFinite(Now) && FMath::IsFinite(Now + Delays[Step]))
        {
            PersistenceRetryAt = Now + Delays[Step];
            PersistenceRetryStep = Step < 4 ? static_cast<uint8>(Step + 1) : static_cast<uint8>(4);
        }
        else
        {
            PersistencePresentation.FailureKind = EJadeboundPersistenceFailureKind::RecoveryRequired;
            PersistencePresentation.Message = FText::FromString(TEXT("Saving needs recovery. Progress is held; keep the game open."));
            PersistenceRetryAt = 0.0;
        }
    }
    else PersistenceRetryAt = 0.0;
    Feedback = PersistencePresentation.Message;
    ForceNetUpdate();
}
void AJadeboundProgressionActor::ClearPersistenceFailure()
{
    bUnsavedCandidate = false;
    bPersistenceBlocked = false;
    HeldSaveCandidate = nullptr;
    PersistencePresentation = FJadeboundPersistencePresentation();
    PersistenceRetryAt = 0.0;
    PersistenceRetryStep = 0;
    ForceNetUpdate();
}
bool AJadeboundProgressionActor::RetryHeldSave(bool bAcknowledge)
{
    if (!HasAuthority() || GetNetMode() == NM_Client || !bUnsavedCandidate || !bPersistenceBlocked ||
        PersistencePresentation.FailureKind != EJadeboundPersistenceFailureKind::Retryable) return false;
    if (!WriteSave()) return false;
    bReady = true;
    bServerModelInitialized = true;
    Feedback = FText::FromString(TEXT("Character saved and read back successfully."));
    ForceNetUpdate();
    // Owner acknowledgment may synchronously bind gameplay or end this actor.
    // Existing SaveProgress/ServerSave callers own their own acknowledgment.
    if (bAcknowledge) AcknowledgeAction(TEXT("Save"), true);
    return true;
}
bool AJadeboundProgressionActor::TickPersistenceRetry()
{
    if (!HasAuthority() || GetNetMode() == NM_Client || bPersistenceEndingPlay ||
        !GetWorld() || GetWorld()->bIsTearingDown || IsActorBeingDestroyed() ||
        !bUnsavedCandidate || !bPersistenceBlocked ||
        PersistencePresentation.FailureKind != EJadeboundPersistenceFailureKind::Retryable ||
        !HasJournalWriterLease()) return false;
    const double Now = FPlatformTime::Seconds();
    if (!FMath::IsFinite(Now) || !FMath::IsFinite(PersistenceRetryAt) || Now < PersistenceRetryAt) return false;
    RetryHeldSave();
    return true;
}
bool AJadeboundProgressionActor::ReadSave(bool Initial)
{
    if (!HasAuthority() || GetNetMode() == NM_Client || bPersistenceEndingPlay ||
        !GetWorld() || GetWorld()->bIsTearingDown || IsActorBeingDestroyed() ||
        bUnsavedCandidate || bPersistenceBlocked ||
        PersistencePresentation.FailureKind != EJadeboundPersistenceFailureKind::None) return false;
    if (!HasJournalWriterLease()) return false;
    FString V2Paths[2] = {GetJournalPath(0), GetJournalPath(1)};
    if (V2Paths[0].IsEmpty() || V2Paths[1].IsEmpty())
    {
        SetPersistenceFailure(EJadeboundPersistenceFailureKind::RecoveryRequired,
            FText::FromString(TEXT("Your character save needs recovery. Existing files are protected.")));
        return false;
    }
    // Format boundary: even a partial first v2 bank forbids legacy fallback.
    const bool bUseV2 = IFileManager::Get().FileExists(*V2Paths[0]) || IFileManager::Get().FileExists(*V2Paths[1]);
    UJadeboundCharacterSave* Best = nullptr;
    Jadebound::Snapshot BestSnapshot;
    JadeboundLegacyV1::Snapshot BestLegacy;
    uint64 BestRevision = 0;
    TArray<uint8> BestBytes;
    bool bAnyExisting = bUseV2;
    int32 BestSlot = 1;
    for (int32 Slot = 0; Slot < 2; ++Slot)
    {
        FString Path = V2Paths[Slot];
        if (!bUseV2 && !Jadebound::Persistence::TryJournalPath(bDevelopmentModel, GetSaveCharacterId(), Slot, false, Path))
        {
            SetPersistenceFailure(EJadeboundPersistenceFailureKind::RecoveryRequired,
                FText::FromString(TEXT("Your character save needs recovery. Existing files are protected.")));
            return false;
        }
        if (!IFileManager::Get().FileExists(*Path)) continue;
        bAnyExisting = true;
        TArray<uint8> OriginalBytes;
        auto* Save = ReadJournal(Path, &OriginalBytes);
        if (!Save || Save->CharacterId != GetSaveCharacterId()) continue;
        const int32 NamespaceVersion = static_cast<int32>(bUseV2 ? Jadebound::SnapshotVersion : JadeboundLegacyV1::SnapshotVersion);
        if (Save->State.Version > NamespaceVersion)
        {
            SetPersistenceFailure(EJadeboundPersistenceFailureKind::RecoveryRequired,
                FText::FromString(TEXT("Your character save is incompatible with this bank format. Use the matching build; existing files are protected.")));
            return false; // Never skip an owned newer-format bank to migrate an older companion.
        }
        Jadebound::Snapshot Candidate;
        JadeboundLegacyV1::Snapshot Legacy;
        if (bUseV2)
        {
            if (!DecodeState(Save->State, Candidate) || !ValidateDrops(Candidate, Save->PendingDrops)) continue;
        }
        else
        {
            // Rank exact original v1 revisions before applying new signed bounds.
            if (!Jadebound::Persistence::DecodeStateV1(Save->State, Legacy) ||
                !Jadebound::Persistence::ValidateDropsV1(Legacy, Save->PendingDrops)) continue;
        }
        const uint64 CandidateRevision = bUseV2 ? Candidate.Revision : Legacy.Revision;
        if (Best && CandidateRevision == BestRevision)
        {
            // Compare ORIGINAL bank bytes, before any conversion or reserialization.
            if (BestBytes != OriginalBytes)
            {
                SetPersistenceFailure(EJadeboundPersistenceFailureKind::RecoveryRequired,
                    FText::FromString(TEXT("Your character save needs recovery. Existing files are protected.")));
                return false;
            }
        }
        if (!Best || CandidateRevision > BestRevision)
        {
            Best = Save;
            BestRevision = CandidateRevision;
            BestBytes = MoveTemp(OriginalBytes);
            BestSlot = Slot;
            if (bUseV2) BestSnapshot = std::move(Candidate);
            else BestLegacy = std::move(Legacy);
        }
    }
    if (!Best)
    {
        if (bAnyExisting || !Initial)
        {
            SetPersistenceFailure(EJadeboundPersistenceFailureKind::RecoveryRequired,
                FText::FromString(TEXT("Your character save needs recovery. Existing files are protected.")));
            return false;
        }
        LastSaveSlot = 1; // Fresh v2 pair starts with A, not an old schema bank.
        RefreshState();
        return WriteSave();
    }
    if (!bUseV2)
    {
        bReady = false;
        bServerModelInitialized = false;
        Jadebound::Snapshot Migration;
        if (!Jadebound::Persistence::BuildMigrationCandidate(BestLegacy, Migration) ||
            !ValidateDrops(Migration, Best->PendingDrops) || !Core->LoadSnapshot(Migration).Succeeded())
        {
            SetPersistenceFailure(EJadeboundPersistenceFailureKind::RecoveryRequired,
                FText::FromString(TEXT("Your character save cannot be migrated safely. Existing files are protected.")));
            return false;
        }
        // No defeat, pickup or reward API is called. Both original banks survive.
        PendingDrops = Best->PendingDrops;
        LastCommittedSave = Best; // Last verified ORIGINAL state until v2 readback.
        LastSaveSlot = 1; // This v2 pair has no prior committed bank.
        RefreshState();
        const bool Saved = WriteSave(); // Holds this exact revision+1 candidate on uncertainty.
        RefreshDropVisuals();
        return Saved;
    }
    if (!Core->LoadSnapshot(BestSnapshot).Succeeded())
    {
        SetPersistenceFailure(EJadeboundPersistenceFailureKind::RecoveryRequired,
            FText::FromString(TEXT("Your character save needs recovery. Existing files are protected.")));
        return false;
    }
    PendingDrops = Best->PendingDrops;
    LastCommittedSave = Best;
    LastSaveSlot = BestSlot;
    ClearPersistenceFailure();
    RefreshState();
    RefreshDropVisuals();
    return true;
}
bool AJadeboundProgressionActor::SaveProgress()
{
    bool Ok = false;
    ON_SCOPE_EXIT { if (HasAuthority() && Player && Player->IsLocalController() && !bExecutingDevelopmentRequest) AcknowledgeAction(TEXT("Save"), Ok); };
    if (!HasAuthority()) { ServerSave(); return false; }
    if (bUnsavedCandidate || bPersistenceBlocked || PersistencePresentation.FailureKind != EJadeboundPersistenceFailureKind::None)
        return Ok = RetryHeldSave(false);
    if (!AuthorityReady()) return false;
    if (!WriteSave()) return false;
    bReady = true;
    bServerModelInitialized = true;
    Feedback = FText::FromString(TEXT("Character saved and read back successfully."));
    return Ok = true;
}
bool AJadeboundProgressionActor::ReloadProgress()
{
    bool Ok = false;
    ON_SCOPE_EXIT { if (HasAuthority() && Player && Player->IsLocalController() && !bExecutingDevelopmentRequest) AcknowledgeAction(TEXT("Reload"), Ok); };
    if (!HasAuthority()) { ServerReload(); return false; }
    if (bUnsavedCandidate || bPersistenceBlocked || PersistencePresentation.FailureKind != EJadeboundPersistenceFailureKind::None)
    {
        Feedback = GetFeedback();
        return false;
    }
    if (!Player || !Hero) return false;
    if (!ReadSave(false)) return false;
    bReady = true;
    bServerModelInitialized = true;
    SeenDefeats = ReadInt(LegacyLoop, TEXT("DefeatCount"));
    ApplyEquipment();
    SyncLegacyQuestPresentation();
    Feedback = FText::FromString(TEXT("Saved inventory, equipment, coins, quest and uncollected loot restored."));
    return Ok = true;
}
void AJadeboundProgressionActor::AcknowledgeAction(FName Action, bool bSucceeded, bool bApplyMenu, bool bShowMenu)
{
    if (!HasAuthority() || !bDevelopmentModel || !IsValid(Player) || GetOwner() != Player) return;
    if (AcknowledgmentSequence < MAX_int64) ++AcknowledgmentSequence;
    FJadeboundActionReceipt Receipt;
    Receipt.Sequence = AcknowledgmentSequence;
    Receipt.Action = Action;
    Receipt.bSucceeded = bSucceeded;
    Receipt.ObservedRevision = State.Revision;
    Receipt.LastVerifiedJournalRevision = LastCommittedSave ? LastCommittedSave->State.Revision : 0;
    Receipt.bHeldCandidate = bUnsavedCandidate;
    Receipt.bPersistenceBlocked = bPersistenceBlocked;
    Receipt.Message = GetFeedback();
    LastIssuedActionReceipt = Receipt;
    const TWeakObjectPtr<AJadeboundProgressionActor> BeforeAcknowledgment(this);
    ClientActionAcknowledged(Receipt, bApplyMenu, bShowMenu);
    if (BeforeAcknowledgment.IsValid() && !BeforeAcknowledgment->IsActorBeingDestroyed() && !bPersistenceEndingPlay)
        ForceNetUpdate();
}
void AJadeboundProgressionActor::ClientActionAcknowledged_Implementation(const FJadeboundActionReceipt& Receipt, bool bApplyMenu, bool bShowMenu)
{
    if (!bDevelopmentModel || Receipt.Sequence <= LastActionReceipt.Sequence) return;
    LastActionReceipt = Receipt;
    Feedback = Receipt.Message;
    BindGameplay();
    // The acknowledgment does not fabricate or overwrite replicated inventory.
    // Its revision can arrive before the owner-only State property update.
    if (bApplyMenu) ShowMenu(bShowMenu);
}
void AJadeboundProgressionActor::ServerSetMenuOpen_Implementation(bool bShow)
{
    if (!bDevelopmentModel) return;
    if (bShow && !AuthorityReady()) { AcknowledgeAction(TEXT("Menu"), false, true, bAuthorityMenuOpen); return; }
    bAuthorityMenuOpen = bShow;
    if (bShow) CancelRecipientAttack();
    else if (bRecipientCombatEnabled && GetWorld())
    {
        const double Now = GetWorld()->GetTimeSeconds();
        if (FMath::IsFinite(Now)) NativeCloseGuardUntil = Now + Jadebound::Combat::CloseGuardSeconds;
    }
    if (bShow) CancelLegacyAttackPresentation();
    AcknowledgeAction(TEXT("Menu"), true, true, bShow);
}
void AJadeboundProgressionActor::ServerSell_Implementation(int32 Slot, int32 Quantity, int64 ExpectedRevision)
{
    TGuardValue<bool> Request(bExecutingDevelopmentRequest, true);
    const bool Ok = ExpectedRevision >= 0 && SellItem(Slot, Quantity, ExpectedRevision);
    if (bDevelopmentModel && ExpectedRevision < 0) Feedback = FText::FromString(TEXT("A current inventory revision is required."));
    AcknowledgeAction(TEXT("Sell"), Ok);
}
void AJadeboundProgressionActor::ServerSplit_Implementation(int32 Slot, int32 Quantity, int32 ExpectedItemId, int64 ExpectedRevision)
{
    TGuardValue<bool> Request(bExecutingDevelopmentRequest, true);
    const bool Ok = SplitItem(Slot, Quantity, ExpectedItemId, ExpectedRevision);
    AcknowledgeAction(TEXT("Split"), Ok);
}
void AJadeboundProgressionActor::ServerEquip_Implementation(int32 Slot, int64 ExpectedRevision)
{
    TGuardValue<bool> Request(bExecutingDevelopmentRequest, true);
    const bool Ok = ExpectedRevision >= 0 && EquipItem(Slot, ExpectedRevision);
    if (bDevelopmentModel && ExpectedRevision < 0) Feedback = FText::FromString(TEXT("A current inventory revision is required."));
    AcknowledgeAction(TEXT("Equip"), Ok);
}
void AJadeboundProgressionActor::ServerInteract_Implementation() { TGuardValue<bool> Request(bExecutingDevelopmentRequest, true); const bool Ok = Interact(); AcknowledgeAction(TEXT("Interact"), Ok, true, bAuthorityMenuOpen); }
void AJadeboundProgressionActor::ServerSave_Implementation()
{
    TGuardValue<bool> Request(bExecutingDevelopmentRequest, true);
    if (!HasAuthority() || GetNetMode() == NM_Client || bPersistenceEndingPlay || !GetWorld() ||
        GetWorld()->bIsTearingDown || IsActorBeingDestroyed() || !HasDevelopmentContext() || !IsValid(Player) ||
        GetOwner() != Player || Player->GetWorld() != GetWorld()) return;
    // A network request may retry only the exact candidate already held by this model.
    // Do not require a living pawn: persistence recovery also applies while dead.
    const bool bRetryableHeld = bUnsavedCandidate && bPersistenceBlocked &&
        PersistencePresentation.FailureKind == EJadeboundPersistenceFailureKind::Retryable && IsValid(HeldSaveCandidate.Get());
    const bool Ok = bRetryableHeld && RetryHeldSave(false); // WriteSave keeps the existing retry deadline.
    if (!bRetryableHeld)
        Feedback = (bUnsavedCandidate || bPersistenceBlocked || PersistencePresentation.FailureKind != EJadeboundPersistenceFailureKind::None)
            ? GetFeedback() : FText::FromString(TEXT("Progress saves automatically after changes. No manual save was performed."));
    AcknowledgeAction(TEXT("Save"), Ok);
}
void AJadeboundProgressionActor::ServerReload_Implementation()
{
    TGuardValue<bool> Request(bExecutingDevelopmentRequest, true);
    if (!HasAuthority() || GetNetMode() == NM_Client || bPersistenceEndingPlay || !GetWorld() ||
        GetWorld()->bIsTearingDown || IsActorBeingDestroyed() || !HasDevelopmentContext() || !IsValid(Player) ||
        GetOwner() != Player || Player->GetWorld() != GetWorld()) return;
    Feedback = (bUnsavedCandidate || bPersistenceBlocked || PersistencePresentation.FailureKind != EJadeboundPersistenceFailureKind::None)
        ? GetFeedback() : FText::FromString(TEXT("Progress loads automatically when you reconnect. In-session reload is unavailable."));
    AcknowledgeAction(TEXT("Reload"), false);
}

TArray<FJadeboundItemView> AJadeboundProgressionActor::GetItemDefinitions() const
{
    TArray<FJadeboundItemView> Items;
    for (const auto& Definition : Jadebound::ProgressionCore::GetDefinitions())
    {
        FJadeboundItemView Item;
        Item.ItemId = static_cast<int32>(Definition.Id);
        Item.Name = FText::FromString(UTF8_TO_TCHAR(Definition.DisplayName));
        Item.Description = Description(Definition.Id);
        Item.StackLimit = static_cast<int32>(Definition.StackLimit);
        Item.BuyPrice = Definition.BuyPrice;
        Item.SellPrice = Definition.SellPrice;
        Item.bWeapon = Definition.Weapon;
        Item.bPurchasable = Definition.Purchasable;
        Item.bSellable = Definition.Sellable;
        Item.HealingPercent = static_cast<int32>(Definition.HealingPercent);
        Items.Add(Item);
    }
    return Items;
}
FText AJadeboundProgressionActor::GetQuestText() const
{
    if (bDevelopmentModel && !bRecipientCombatEnabled && State.QuestState == 1)
        return FText::FromString(TEXT("A Stolen Spark  -  ACCEPTED\nLocal multiplayer foundation: recipient combat/loot is pending."));
    switch (State.QuestState)
    {
    case 0: return FText::FromString(TEXT("A Stolen Spark\nMerchant [E]  -  Reward: 5 coins + 1 jade shard."));
    case 1: return FText::FromString(TEXT("A Stolen Spark  -  Raider 0/1\nDefeat the courtyard raider."));
    case 2: return FText::FromString(TEXT("A Stolen Spark  -  Raider 1/1\nRecover the dropped jade parcel [E]."));
    case 3: return FText::FromString(TEXT("A Stolen Spark  -  Parcel 1/1\nReturn to the merchant [E]. Reward: 5 coins + 1 jade shard."));
    case 4: return FText::FromString(TEXT("A Stolen Spark  -  COMPLETE\nReward paid once. Trade ore to earn more coins."));
    default: return FText::FromString(TEXT("Character data unavailable."));
    }
}
void AJadeboundProgressionActor::RefreshDropVisuals()
{
    if (ConquerInteractionIntent.DropId >= 0)
    {
        const TWeakObjectPtr<AJadeboundProgressionActor> WeakSelf(this);
        StopClickWalk(TEXT("InteractionVisualRefresh"), true);
        if (!WeakSelf.IsValid() || WeakSelf->IsActorBeingDestroyed()) return;
    }
    InteractionHandles.Reset();
    MerchantClickMarker.Reset();
    bMerchantMarkerAttempted = false;
    for (AActor* Visual : DropVisuals) if (IsValid(Visual)) Visual->Destroy();
    DropVisuals.Empty();
    if (!HasAuthority()) return;
    if (bDevelopmentModel && bRecipientCombatEnabled)
    {
        // A retained disconnected recipient has real pending records but no
        // controller relevance; rebind rebuilds its actual owner-only visuals.
        if (!GetWorld() || GetWorld()->bIsTearingDown || bPersistenceEndingPlay || !IsValid(Player) || GetOwner() != Player) return;
        for (const auto& Drop : PendingDrops)
        {
            UStaticMesh* Mesh = Drop.ItemId == 5 ? JadeParcelMesh.Get() : IronOreMesh.Get();
            if (!Mesh) continue;
            FHitResult Floor;
            FCollisionQueryParams Query(SCENE_QUERY_STAT(JadeboundPersonalLootFloor), false);
            Query.AddIgnoredActor(this);
            if (Hero) Query.AddIgnoredActor(Hero);
            if (auto* Mode = Cast<AJadeboundDevelopmentGameMode>(GetWorld()->GetAuthGameMode()))
                if (auto* Enemy = Mode->GetRecipientRaider()) Query.AddIgnoredActor(Enemy);
            FVector Point = Drop.Location - FVector(0,0,142.15);
            if (GetWorld()->LineTraceSingleByObjectType(Floor, Drop.Location, Drop.Location-FVector(0,0,500),
                FCollisionObjectQueryParams(ECC_WorldStatic), Query)) Point = Floor.ImpactPoint;
            FActorSpawnParameters Spawn;
            Spawn.Owner = this;
            auto* Visual = GetWorld()->SpawnActor<AJadeboundPersonalDrop>(Point+FVector(0,0,2),
                FRotator(0, Jadebound::Presentation::LootYaw(Drop.DropId), 0), Spawn);
            if (!Visual) continue;
            Visual->InitializeFromAuthority(Drop.DropId, Drop.ItemId, Drop.Count, Mesh, static_cast<float>(Drop.Location.Z-Point.Z));
            DropVisuals.Add(Visual);
        }
        return;
    }
    const bool bClickable = ResolveClickWalkScope() != nullptr;
    bMerchantMarkerAttempted = bClickable;
    // Explicit temporary merchant locator. The market building is not a NPC.
    if (bClickable && !MerchantPoint.ContainsNaN())
    {
        FActorSpawnParameters Spawn;
        Spawn.Owner = this;
        auto* Marker = GetWorld()->SpawnActor<ATextRenderActor>(MerchantPoint + FVector(0,0,110), FRotator::ZeroRotator, Spawn);
        if (Marker)
        {
            auto* Text = Marker->GetTextRender();
            Text->SetText(FText::FromString(TEXT("Merchant  [LMB / E]")));
            Text->SetHorizontalAlignment(EHTA_Center);
            Text->SetWorldSize(18.f);
            Text->SetTextRenderColor(FColor(209,184,116));
            Text->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            DropVisuals.Add(Marker);
            MerchantClickMarker = Marker;
            RegisterInteractionHandle(Marker, 0, 48.f);
        }
    }
    // Read only the exact retained target's capsule once per existing refresh.
    FVector RaiderCenter = FVector::ZeroVector;
    double RaiderRadius = 0.0, RaiderTop = 0.0;
    bool bRaiderGeometry = false;
    if (bClickable && IsValid(LegacyTarget) && !LegacyTarget->IsActorBeingDestroyed() &&
        LegacyTarget->GetClass()->GetPathName() == TEXT("/Game/Jadebound/Tests/GlaiveCombatV006/BP_GlaivePracticeTarget_V006.BP_GlaivePracticeTarget_V006_C"))
        if (const UCapsuleComponent* Capsule = LegacyTarget->FindComponentByClass<UCapsuleComponent>())
        {
            RaiderCenter = Capsule->GetComponentLocation();
            RaiderRadius = Capsule->GetScaledCapsuleRadius();
            const double Half = Capsule->GetScaledCapsuleHalfHeight();
            RaiderTop = RaiderCenter.Z + Half;
            bRaiderGeometry = Capsule == LegacyTarget->GetRootComponent() &&
                Capsule->GetOwner() == LegacyTarget && Capsule->GetWorld() == GetWorld() &&
                Capsule->IsRegistered() && !Capsule->GetComponentTransform().ContainsNaN() &&
                Capsule->GetUpVector().Z >= .9999 && !RaiderCenter.ContainsNaN() &&
                FMath::IsFinite(RaiderRadius) && RaiderRadius > 0.0 && RaiderRadius <= 128.0 &&
                FMath::IsFinite(Half) && Half >= RaiderRadius && Half <= 256.0;
        }
    // Plain captured representative: one near group, no chaining or saved mutation.
    const double GroupRadius = bRaiderGeometry ? 2.0 * RaiderRadius : 0.0;
    FVector CameraRight, CameraUp; FRotator LabelFacing;
    const bool bCameraBasis = IsValid(Player) && IsValid(Player->PlayerCameraManager) &&
        MakeLootCameraBasis(Player->PlayerCameraManager->GetCameraRotation(), CameraRight, CameraUp, LabelFacing);
    TArray<FJadeboundSavedDrop> VisualDrops = PendingDrops;
    double GroupBaseline = RaiderTop + 12.0;
    for (const auto& Drop : VisualDrops)
        if (IsNearLootLabelGroup(Drop.Location, RaiderCenter, GroupRadius))
            GroupBaseline = FMath::Max(GroupBaseline, Drop.Location.Z);
    const FVector GroupRepresentative(RaiderCenter.X, RaiderCenter.Y, GroupBaseline);
    VisualDrops.Sort([RaiderCenter, GroupRadius](const FJadeboundSavedDrop& Left, const FJadeboundSavedDrop& Right)
    {
        const bool A = !Left.Location.ContainsNaN(), B = !Right.Location.ContainsNaN();
        if (A != B) return A;
        if (!A) return Left.DropId < Right.DropId;
        const bool AG = IsNearLootLabelGroup(Left.Location, RaiderCenter, GroupRadius);
        const bool BG = IsNearLootLabelGroup(Right.Location, RaiderCenter, GroupRadius);
        if (AG != BG) return AG;
        if (AG) return Left.DropId < Right.DropId;
        if (Left.Location.X != Right.Location.X) return Left.Location.X < Right.Location.X;
        if (Left.Location.Y != Right.Location.Y) return Left.Location.Y < Right.Location.Y;
        if (Left.Location.Z != Right.Location.Z) return Left.Location.Z < Right.Location.Z;
        return Left.DropId < Right.DropId;
    });
    struct FLabelRow
    {
        TWeakObjectPtr<ATextRenderActor> Visual;
        TWeakObjectPtr<UTextRenderComponent> Text;
        TWeakObjectPtr<AStaticMeshActor> FloorProp;
        FVector Anchor;
        int64 DropId = 0;
        int32 ItemId = 0, Row = 0;
        double LowerOffset = 0.0;
        bool bNear = false, bMeasured = false;
    };
    TArray<FLabelRow> Labels; Labels.Reserve(VisualDrops.Num());
    double GroupHalfWidth = 0.0, GroupHalfHeight = 0.0;
    FVector PreviousAnchor = FVector::ZeroVector;
    bool bPreviousAnchor = false;
    int32 LabelRow = 0, NearRow = 0;
    const TWeakObjectPtr<AJadeboundProgressionActor> RefreshSelf(this);
    for (const auto& Drop : VisualDrops)
    {
        const bool bNear = IsNearLootLabelGroup(Drop.Location, RaiderCenter, GroupRadius);
        LabelRow = bNear ? NearRow++ :
            (bPreviousAnchor && Drop.Location == PreviousAnchor ? LabelRow + 1 : 0);
        PreviousAnchor = Drop.Location; bPreviousAnchor = !bNear;
        FActorSpawnParameters Spawn;
        Spawn.Owner = this;
        TWeakObjectPtr<AStaticMeshActor> FloorProp;
        UStaticMesh* LootMesh = Drop.ItemId == 5 ? JadeParcelMesh.Get() : IronOreMesh.Get();
        if (LootMesh)
        {
            FHitResult Floor;
            FCollisionQueryParams Query(SCENE_QUERY_STAT(JadeboundLootFloor), false);
            Query.AddIgnoredActor(this);
            if (Hero) Query.AddIgnoredActor(Hero);
            if (LegacyTarget) Query.AddIgnoredActor(LegacyTarget);
            FVector Point = Drop.Location - FVector(0,0,142.15);
            if (GetWorld()->LineTraceSingleByChannel(Floor,Drop.Location,Drop.Location-FVector(0,0,500),ECC_WorldStatic,Query)) Point = Floor.ImpactPoint;
            auto* Prop = GetWorld()->SpawnActor<AStaticMeshActor>(Point+FVector(0,0,2),FRotator(0,Jadebound::Presentation::LootYaw(Drop.DropId),0),Spawn);
            if (Prop)
            {
                auto* Mesh = Prop->GetStaticMeshComponent();
                Mesh->SetMobility(EComponentMobility::Movable);
                Mesh->SetStaticMesh(LootMesh);
                Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                DropVisuals.Add(Prop);
                if (bClickable) RegisterInteractionHandle(Prop, Drop.DropId, 26.f);
                FloorProp = Prop;
            }
        }
        auto* Visual = GetWorld()->SpawnActor<ATextRenderActor>(Drop.Location, FRotator::ZeroRotator, Spawn);
        if (!Visual) continue;
        auto* Text = Visual->GetTextRender();
        Text->SetText(FText::FromString(bClickable ?
            (Drop.ItemId == 5 ? TEXT("Collect parcel [LMB/RMB/E]") : TEXT("Collect ore x2 [LMB/RMB/E]")) :
            (Drop.ItemId == 5 ? TEXT("Collect parcel [E]") : TEXT("Collect ore x2 [E]"))));
        Text->SetHorizontalAlignment(EHTA_Center);
        Text->SetWorldSize(16.f);
        Text->SetTextRenderColor(Drop.ItemId == 5 ? FColor(95,255,184) : FColor(255,206,124));
        Visual->SetActorEnableCollision(false);
        Text->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        DropVisuals.Add(Visual);
        FLabelRow Row;
        Row.Visual = Visual; Row.Text = Text; Row.Anchor = Drop.Location;
        Row.FloorProp = FloorProp; // Same exact DropId iteration; no nearest-prop matching.
        Row.DropId = Drop.DropId; Row.ItemId = Drop.ItemId;
        Row.Row = LabelRow; Row.bNear = bNear;
        if (bClickable && bRaiderGeometry)
        {
            // Measure the actual configured caption once, before the existing
            // rotation-only billboard loop; never mutate a CDO or allocate a probe.
            const FBoxSphereBounds Bounds = Text->CalcBounds(FTransform(Text->GetRenderMatrix()));
            const FBoxSphereBounds Local = Text->CalcBounds(FTransform::Identity);
            const FVector Scale = Text->GetComponentScale();
            const double HalfWidth = Local.BoxExtent.Y * Scale.Y;
            const double HalfHeight = Local.BoxExtent.Z * Scale.Z;
            Row.LowerOffset = Bounds.Origin.Z - Bounds.BoxExtent.Z - Drop.Location.Z;
            Row.bMeasured = !Bounds.Origin.ContainsNaN() && !Bounds.BoxExtent.ContainsNaN() &&
                !Local.BoxExtent.ContainsNaN() && !Scale.ContainsNaN() &&
                Scale.X > SMALL_NUMBER && Scale.Y > SMALL_NUMBER && Scale.Z > SMALL_NUMBER &&
                FMath::IsFinite(HalfWidth) && HalfWidth > 0.0 && HalfWidth <= 125.0 &&
                FMath::IsFinite(HalfHeight) && HalfHeight > 0.0 && HalfHeight <= 24.0 &&
                FMath::IsFinite(Row.LowerOffset) && FMath::Abs(Row.LowerOffset) <= 24.0;
            if (Row.bNear && Row.bMeasured)
            {
                GroupHalfWidth = FMath::Max(GroupHalfWidth, HalfWidth);
                GroupHalfHeight = FMath::Max(GroupHalfHeight, HalfHeight);
            }
        }
        Labels.Add(Row);
    }
    // One bounded placement pass over the already spawned labels, not a retry.
    int32 CollectCueCount = 0;
    constexpr int32 MaximumCollectCues = 16; // At most 144 lines; every drop retains its caption/pick handling.
    for (const FLabelRow& Row : Labels)
    {
        auto* Visual = Row.Visual.Get(); auto* Text = Row.Text.Get();
        if (!IsValid(Visual) || Visual->IsActorBeingDestroyed() || !IsValid(Text) ||
            Visual->GetTextRender() != Text || Visual->GetOwner() != this) continue;
        FVector LabelPosition;
        const bool bPosition = Row.bMeasured && (Row.bNear ?
            (bCameraBasis && GroupHalfHeight * 2.0 + 6.0 <= 24.0 &&
                ChooseGroupedLootLabelPosition(Row.Anchor, GroupRepresentative, CameraRight, CameraUp,
                    GroupHalfWidth, Row.Row, Row.LowerOffset, LabelPosition)) :
            ChooseLootLabelPosition(Row.Anchor, RaiderCenter, RaiderRadius, RaiderTop, Row.Row, Row.LowerOffset, LabelPosition));
        bool bLabelClickable = bPosition && (LabelPosition == Row.Anchor ||
            Visual->SetActorLocation(LabelPosition, false, nullptr, ETeleportType::None));
        if (!RefreshSelf.IsValid() || RefreshSelf->IsActorBeingDestroyed()) return;
        if (!IsValid(Visual) || Visual->IsActorBeingDestroyed() || !IsValid(Text) || Visual->GetTextRender() != Text) continue;
        if (bLabelClickable) bLabelClickable = RegisterInteractionHandle(Visual, Row.DropId, 32.f);
        // Missing camera/geometry, oversized captions or overflow stay E-only.
        // Original floor props and all physical identity/range/LOS stay unchanged.
        if (!bLabelClickable)
            Text->SetText(FText::FromString(Row.ItemId == 5 ? TEXT("Collect parcel [E]") : TEXT("Collect ore x2 [E]")));
        // Cue only the actual prop/caption pair: no collider, fallback mesh,
        // anchor change, new interaction or persistence write.
        if (CollectCueCount < MaximumCollectCues &&
            AddLootCollectCue(this,Row.FloorProp.Get(),Visual,Row.DropId)) ++CollectCueCount;
        if (!RefreshSelf.IsValid() || RefreshSelf->IsActorBeingDestroyed()) return;
    }
}
void AJadeboundProgressionActor::ToggleMenu()
{
    if (bDevelopmentModel) { ServerSetMenuOpen(!bMenuOpen); return; }
    ShowMenu(!bMenuOpen);
}
void AJadeboundProgressionActor::CloseMenu()
{
    if (bDevelopmentModel)
    {
        if (!HasAuthority()) ShowMenu(false);
        ServerSetMenuOpen(false);
        return;
    }
    ShowMenu(false);
}
void AJadeboundProgressionActor::ShowMenu(bool Show)
{
    if (bReviveWaiting) return;
    if (!Player || !Player->IsLocalController() || !GetWorld() || !GetWorld()->GetGameViewport()) return;
    if (Show == bMenuOpen) return;
    if (Show) CancelRecipientAttack();
    if (Show) StopClickWalk(TEXT("Menu"), true);
    bWorldCursorPolicyApplied = false; // The existing panel now owns cursor policy.
    bMenuOpen = Show;
    if (!Show)
    {
        LastMenuCloseFrame = GFrameCounter;
        const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0;
        const bool bRevivePointerGuardActive = RevivePointerGuardUntil > 0.0 &&
            (!FMath::IsFinite(Now) || !FMath::IsFinite(RevivePointerGuardUntil) || Now < RevivePointerGuardUntil);
        bAwaitingPointerRelease = IsWorldPointerGestureActive() || bRevivePointerGuardActive;
    }
    ApplyEquipment();
    if (Show)
    {
        CancelLegacyAttackPresentation();
        TSharedRef<SJadeboundProgressionPanel> Panel = SNew(SJadeboundProgressionPanel).ProgressionActor(TWeakObjectPtr<AJadeboundProgressionActor>(this));
        MenuWidget = SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor(.002f,.006f,.004f,.20f)).Padding(0.f)
            .HAlign(HAlign_Fill).VAlign(VAlign_Fill)[Panel];
        GetWorld()->GetGameViewport()->AddViewportWidgetContent(MenuWidget.ToSharedRef(), 100);
        FInputModeGameAndUI Mode;
        Mode.SetWidgetToFocus(Panel);
        Mode.SetHideCursorDuringCapture(false);
        Player->SetInputMode(Mode);
        Player->SetShowMouseCursor(true);
        Player->SetIgnoreMoveInput(true);
    }
    else
    {
        if (MenuWidget.IsValid()) GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(MenuWidget.ToSharedRef());
        MenuWidget.Reset();
        Player->SetInputMode(FInputModeGameOnly());
        Player->SetShowMouseCursor(false);
        Player->SetIgnoreMoveInput(false);
        UpdateWorldCursor();
    }
    ApplyEquipment();
}
void AJadeboundProgressionActor::ConfigureRecipientCombat(bool bEnabled, UAnimMontage* Montage)
{
    if (!HasAuthority() || !HasDevelopmentContext() || bPersistenceEndingPlay || IsActorBeingDestroyed() ||
        !GetWorld() || GetWorld()->bIsTearingDown) return;
    bRecipientCombatEnabled = bEnabled && Jadebound::Combat::IsSafeMeleeMontage(Montage);
    NativeAttackMontage = bRecipientCombatEnabled ? Montage : nullptr;
    if (!bRecipientCombatEnabled) CancelRecipientAttack();
    if (HasActorBegunPlay()) RefreshDropVisuals();
    ForceNetUpdate();
}
bool AJadeboundProgressionActor::IsRecipientSwingActive() const
{
    return !bPersistenceEndingPlay && !IsActorBeingDestroyed() && GetWorld() && !GetWorld()->bIsTearingDown &&
        NativeSwingPawn.IsValid() && FMath::IsFinite(GetWorld()->GetTimeSeconds()) && GetWorld()->GetTimeSeconds() < NativeSwingUntil;
}
void AJadeboundProgressionActor::CancelRecipientAttack()
{
    bNativeHitAttempted = true;
    NativeSwingUntil = 0;
    NativeSwingPawn.Reset();
    NativeSwingTarget.Reset();
    NativeSwingComponent.Reset();
    if (HasAuthority())
        if (auto* Presentation = PublicMelee.Get())
            if (NativeSwingId > 0)
                Presentation->PublishFromAuthority(NativeSwingId,NativeAttackMontage,NativeHitAt,0,false);
    if (!HasAuthority() && Hero)
        if (auto* Presentation = Hero->FindComponentByClass<UJadeboundMeleePresentationComponent>())
            Presentation->StopPresentation(); // Immediate local menu close/open response; no server state assignment.
}
bool AJadeboundProgressionActor::RequestNativeAttack()
{
    if (!HasAuthority()) { ServerRequestNativeAttack(); return false; }
    bool Ok = false;
    ON_SCOPE_EXIT { AcknowledgeAction(TEXT("Attack"), Ok); };
    AJadeboundDevelopmentGameMode* Mode = GetWorld() ? Cast<AJadeboundDevelopmentGameMode>(GetWorld()->GetAuthGameMode()) : nullptr;
    if (bPersistenceEndingPlay || IsActorBeingDestroyed() || !GetWorld() || GetWorld()->bIsTearingDown || !bRecipientCombatEnabled || !Jadebound::Combat::IsSafeMeleeMontage(NativeAttackMontage) || !Mode || !Mode->OwnsRecipientModel(this,true) || !AuthorityReady())
    { Feedback = FText::FromString(TEXT("Combat is unavailable until your character is ready.")); return false; }
    const double Now = GetWorld()->GetTimeSeconds();
    if (!FMath::IsFinite(Now) || Now < 0 || !FMath::IsFinite(Now + Jadebound::Combat::CooldownSeconds) || bMenuOpen || bAuthorityMenuOpen || bAwaitingPointerRelease || IsCloseFrameGuardActive() || Now < NativeCloseGuardUntil)
    { Feedback = FText::FromString(TEXT("Close your pack and release the attack button first.")); return false; }
    if (State.EquippedWeapon != 1 && State.EquippedWeapon != 2)
    { Feedback = FText::FromString(TEXT("Equip a weapon before attacking.")); return false; }
    UStaticMesh* RequiredMesh = State.EquippedWeapon == 1 ? DevelopmentGlaiveMesh.Get() : AshwoodSpearMesh.Get();
    if (!WeaponComponent || WeaponComponent->GetOwner() != Hero || !RequiredMesh ||
        WeaponComponent->GetStaticMesh() != RequiredMesh || !WeaponComponent->IsVisible())
    { Feedback = FText::FromString(TEXT("Your weapon is still preparing.")); return false; }
    if (Now < NativeCooldownUntil || IsRecipientSwingActive() || NativeSwingId == MAX_int64)
    { Feedback = FText::FromString(TEXT("Let your swing finish before attacking again.")); return false; }
    AJadeboundNativeRaider* Enemy = Mode->GetRecipientRaider();
    if (!Enemy || !Enemy->GetLife().bAlive || Enemy->GetLife().Health <= 0)
    { Feedback = FText::FromString(TEXT("Wait for the courtyard raider to return.")); return false; }
    ACharacter* Character = Cast<ACharacter>(Hero);
    if (!Character || !Character->GetMesh() || !Character->GetMesh()->GetAnimInstance() ||
        !Character->GetMesh()->GetSkeletalMeshAsset() ||
        Character->GetMesh()->GetSkeletalMeshAsset()->GetSkeleton() != NativeAttackMontage->GetSkeleton())
    { Feedback = FText::FromString(TEXT("Your attack animation is still preparing.")); return false; }
    if (Character->GetMesh()->GetAnimInstance()->GetCurrentActiveMontage())
    { Feedback = FText::FromString(TEXT("Let your current action finish first.")); return false; }
    UJadeboundMeleePresentationComponent* Presentation = PublicMelee.Get();
    if (!Presentation || Presentation->GetOwner() != Hero)
    {
        Presentation = Hero->FindComponentByClass<UJadeboundMeleePresentationComponent>();
        if (!Presentation)
        {
            Presentation = NewObject<UJadeboundMeleePresentationComponent>(Hero,FName(TEXT("JadeboundPublicMelee")));
            Hero->AddInstanceComponent(Presentation);
            Presentation->SetIsReplicated(true);
            Presentation->RegisterComponent();
        }
        PublicMelee = Presentation;
    }
    ++NativeSwingId;
    NativeSwingLife = Enemy->GetLife().LifeId;
    NativeSwingWeapon = State.EquippedWeapon;
    NativeSwingPawn = Hero;
    NativeSwingComponent = WeaponComponent;
    NativeSwingTarget = Enemy;
    NativeHitAt = Now + Jadebound::Combat::WindupSeconds;
    NativeSwingUntil = Now + Jadebound::Combat::PresentationSeconds;
    NativeCooldownUntil = Now + Jadebound::Combat::CooldownSeconds;
    bNativeHitAttempted = false;
    // Expire the template's buffered combo input; no template graph is invoked.
    if (auto* Cached = Field<FNumericProperty>(Hero,TEXT("Cached Attack Input Time")))
        if (Cached->IsFloatingPoint()) Cached->SetFloatingPointPropertyValue(Cached->ContainerPtrToValuePtr<void>(Hero),-1.0e9);
    Presentation->PublishFromAuthority(NativeSwingId,NativeAttackMontage,Now,NativeSwingUntil,true);
    if (!Presentation->HasActivePresentation(NativeSwingId))
    {
        CancelRecipientAttack();
        Feedback = FText::FromString(TEXT("Your attack animation could not start. No hit was applied."));
        return false;
    }
    Feedback = FText::FromString(TEXT("Swing your weapon toward the raider."));
    return Ok = true;
}
void AJadeboundProgressionActor::ServerRequestNativeAttack_Implementation() { RequestNativeAttack(); }
void AJadeboundProgressionActor::TickRecipientCombat()
{
    if (!HasAuthority() || !NativeSwingPawn.IsValid()) return;
    if (bPersistenceEndingPlay || IsActorBeingDestroyed() || !GetWorld() || GetWorld()->bIsTearingDown) { CancelRecipientAttack(); return; }
    const double Now = GetWorld()->GetTimeSeconds();
    AJadeboundDevelopmentGameMode* Mode = Cast<AJadeboundDevelopmentGameMode>(GetWorld()->GetAuthGameMode());
    UStaticMesh* RequiredMesh = NativeSwingWeapon == 1 ? DevelopmentGlaiveMesh.Get() : AshwoodSpearMesh.Get();
    if (!FMath::IsFinite(Now) || !bRecipientCombatEnabled || !Mode || !Mode->OwnsRecipientModel(this,true) ||
        !IsProgressionReady() || !HasLivingPossessedHero() || Hero != NativeSwingPawn.Get() ||
        State.EquippedWeapon != NativeSwingWeapon || !NativeSwingComponent.IsValid() || NativeSwingComponent.Get() != WeaponComponent ||
        NativeSwingComponent->GetOwner() != Hero || !RequiredMesh || NativeSwingComponent->GetStaticMesh() != RequiredMesh || !NativeSwingComponent->IsVisible() ||
        bMenuOpen || bAuthorityMenuOpen || Now >= NativeSwingUntil ||
        !PublicMelee.IsValid() || !PublicMelee->HasActivePresentation(NativeSwingId))
    { CancelRecipientAttack(); return; }
    if (bNativeHitAttempted || Now < NativeHitAt) return;
    bNativeHitAttempted = true; // Even a miss/late tick cannot be retried this swing.
    AJadeboundNativeRaider* Enemy = NativeSwingTarget.Get();
    if (!Jadebound::Combat::IsHitWindow(Now,NativeHitAt) || !Enemy || Enemy != Mode->GetRecipientRaider() ||
        Enemy->GetLife().LifeId != NativeSwingLife || !Enemy->GetLife().bAlive ||
        !Jadebound::Combat::CanReach(Hero->GetActorLocation(),Hero->GetActorForwardVector(),Enemy->GetActorLocation(),NativeSwingWeapon))
    { Feedback = FText::FromString(TEXT("Your swing missed. Move closer and face the raider.")); return; }
    FHitResult Obstruction;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(JadeboundNativeMelee),false);
    Query.AddIgnoredActor(Hero);
    const FVector Start = Hero->GetActorLocation() + FVector(0,0,20);
    const FVector End = Enemy->GetActorLocation() + FVector(0,0,20);
    if (GetWorld()->LineTraceSingleByChannel(Obstruction,Start,End,ECC_Visibility,Query) && Obstruction.GetActor() != Enemy)
    { Feedback = FText::FromString(TEXT("Something blocks your swing.")); return; }
    const bool Hit = Enemy->ApplyValidatedHit(this,Hero,NativeSwingId,NativeSwingLife);
    Feedback = FText::FromString(Hit ? TEXT("Your weapon struck the raider.") : TEXT("Your swing missed."));
}
bool AJadeboundProgressionActor::CommitRecipientDefeat(AJadeboundNativeRaider* Enemy)
{
    // Only the exact admitted native enemy can provide a death. No caller may
    // choose another recipient, life, item, location, amount or allocator ID.
    UWorld* World = GetWorld();
    AJadeboundDevelopmentGameMode* Mode = World ? Cast<AJadeboundDevelopmentGameMode>(World->GetAuthGameMode()) : nullptr;
    if (!HasAuthority() || GetNetMode() == NM_Client || bPersistenceEndingPlay || IsActorBeingDestroyed() ||
        !World || World->bIsTearingDown || !Core || !DevelopmentWriterLease || !HasDevelopmentContext() ||
        !Mode || !Mode->OwnsRecipientModel(this, false) || !IsValid(Enemy) || Enemy != Mode->GetRecipientRaider() ||
        Enemy->GetWorld() != World || Enemy->IsActorBeingDestroyed() || Enemy->Life.bAlive || Enemy->Life.Health != 0 ||
        Enemy->Life.LifeId <= 0 || Enemy->LastValidRecipient.Get() != this) return false;
    const int64 LifeId = Enemy->Life.LifeId;
    if (AwardedEnemy.Get() == Enemy && AwardedLife == LifeId && AwardedDrop > 0)
    {
        // Pickup legitimately removes pending membership. The validated issued
        // prefix, exact owner and revision prove this life was durably awarded.
        Jadebound::Snapshot Committed;
        return LastCommittedSave && LastCommittedSave->CharacterId == ServerCharacterId &&
            Jadebound::Persistence::DecodeStateV2(LastCommittedSave->State, Committed) &&
            Jadebound::Recipient::HasCommittedIssue(Committed, LastCommittedSave->PendingDrops, AwardedDrop, AwardedRevision);
    }
    if (AwardedEnemy.Get() == Enemy && LifeId <= AwardedLife) return false;
    // An accepted death may finish after disconnect/death, but an uninitialized
    // or frozen character cannot allocate another transaction.
    if (!bReady || !bServerModelInitialized || bPersistenceBlocked || bUnsavedCandidate || HeldSaveCandidate) return false;
    const FVector Location = Enemy->GetActorLocation() + FVector(0,0,50);
    Jadebound::Recipient::IssueCandidate Candidate;
    const Jadebound::Result Issued = Jadebound::Recipient::BuildIssue(Core->GetSnapshot(), PendingDrops, Location, Candidate);
    if (!Issued.Succeeded()) { Feedback = ResultText(Issued.Code); return false; }
    const bool bParcel = Candidate.State.QuestKillDropId == static_cast<uint64>(Candidate.DropId);
    if (!Core->LoadSnapshot(Candidate.State).Succeeded()) return false;
    PendingDrops = MoveTemp(Candidate.Drops);
    // Latch before IO. Reentry only checks the committed witness; scheduled
    // retry already serializes the exact held candidate and never reruns issue.
    AwardedEnemy = Enemy;
    AwardedLife = LifeId;
    AwardedDrop = Candidate.DropId;
    AwardedRevision = Core->GetSnapshot().Revision;
    const TWeakObjectPtr<AJadeboundProgressionActor> WeakSelf(this);
    const bool Saved = FinishTransaction(Issued, FText::FromString(bParcel
        ? TEXT("The raider dropped your stolen parcel. Collect it with E.")
        : TEXT("The raider dropped two pieces of iron ore. Collect them with E.")));
    if (!WeakSelf.IsValid() || WeakSelf->IsActorBeingDestroyed() || bPersistenceEndingPlay) return false;
    RefreshDropVisuals(); // Actual presentation path; no test or save bypass.
    if (!WeakSelf.IsValid() || WeakSelf->IsActorBeingDestroyed() || bPersistenceEndingPlay) return false;
    UE_LOG(LogTemp, Log, TEXT("Jadebound recipient death: Life=%lld Recipient=%s Drop=%lld Revision=%llu Verified=%d"),
        LifeId, *ServerCharacterId, AwardedDrop, AwardedRevision, static_cast<int32>(Saved));
    AcknowledgeAction(TEXT("Defeat"), Saved);
    return Saved;
}

void AJadeboundProgressionActor::EnsureHud()
{
    if (HudWidget.IsValid() || !Player || !Player->IsLocalController() || !GetWorld()->GetGameViewport()) return;
    HudWidget = SJadeboundProgressionPanel::BuildGameplayHud(TWeakObjectPtr<AJadeboundProgressionActor>(this));
    GetWorld()->GetGameViewport()->AddViewportWidgetContent(HudWidget.ToSharedRef(), 10);
}
