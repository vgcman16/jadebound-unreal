#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformFile.h"
#include "GameFramework/Actor.h"
#include "GameFramework/SaveGame.h"
#include "Templates/Function.h"
#include "JadeboundPlayerLifeComponent.h"
#include "JadeboundProgressionActor.generated.h"

class APawn;
class ACharacter;
class UGameViewportClient;
class UInputMappingContext;
class UInputAction;
class UEnhancedInputLocalPlayerSubsystem;
class APlayerController;
class UStaticMesh;
class UStaticMeshComponent;
class UEnhancedInputComponent;
class SWidget;
class IFileHandle;
class AJadeboundDevelopmentGameMode;
class AJadeboundNativeRaider;
class UAnimMontage;
class UJadeboundMeleePresentationComponent;
class UJadeboundEquipmentPresentationComponent;
class UTextRenderComponent;
class ATextRenderActor;
class FJadeboundNativeCapture;
struct FJadeboundNativeCaptureDeleter { void operator()(FJadeboundNativeCapture* Pointer) const; };
class UPrimitiveComponent;
namespace Jadebound::Combat::PlayerLife { struct State; }
struct FJadeboundPlayerLifeDeleter { void operator()(Jadebound::Combat::PlayerLife::State* Pointer) const; };
struct FInputActionInstance;
struct FHitResult;
namespace Jadebound {
    class ProgressionCore;
    struct Snapshot;
    struct Result;
    struct ProgressionCoreDeleter { void operator()(ProgressionCore* Pointer) const; };
}

// Read-only, process-local presentation. These are not Blueprint fields,
// replicated combat state, save data, or inputs to gameplay admission.
struct FJadeboundHealthSample
{
    bool bAvailable = false;
    double Current = 0.0;
    double Maximum = 0.0;
    float Ratio = 0.f;
    int32 Percent = 0;
};

// Transient standalone death presentation: no save, replication or client RPC.
struct FJadeboundRevivePresentation
{
    bool bWaiting = false;
    bool bCanRevive = false;
    int32 SecondsRemaining = 0;
    uint64 Generation = 0;
    FText Message;
};

struct FJadeboundHealthPresentation
{
    bool bContextAvailable = false;
    TWeakObjectPtr<APlayerController> Player;
    TWeakObjectPtr<APawn> Pawn;
    FJadeboundHealthSample PlayerHealth;
    TWeakObjectPtr<AActor> Target;
    FJadeboundHealthSample TargetHealth;
    TWeakObjectPtr<UTextRenderComponent> LegacyTargetText;
    FVector TargetAnchor = FVector::ZeroVector;
    bool bTargetAnchorAvailable = false;
    bool bTargetHidden = false;
    bool bTargetCombatEnabled = false;
};

// Typed player-local controls. No entry owns an item, amount or character save.
enum class EJadeboundHotbarKind : uint8 { Empty, Skill, Item };
struct FJadeboundHotbarEntry
{
    EJadeboundHotbarKind Kind = EJadeboundHotbarKind::Empty;
    int32 Identity = INDEX_NONE;
    static constexpr FJadeboundHotbarEntry Skill(int32 Id) { return {EJadeboundHotbarKind::Skill,Id}; }
    static constexpr FJadeboundHotbarEntry Item(int32 Id) { return {EJadeboundHotbarKind::Item,Id}; }
    constexpr bool IsValid() const
    {
        return (Kind==EJadeboundHotbarKind::Empty && Identity==INDEX_NONE) ||
            (Kind==EJadeboundHotbarKind::Skill && (Identity==0 || Identity==1)) ||
            (Kind==EJadeboundHotbarKind::Item && Identity==7);
    }
    constexpr bool Equals(FJadeboundHotbarEntry Other) const { return Kind==Other.Kind && Identity==Other.Identity; }
};
struct FJadeboundLocalHotbarBindings
{
    FJadeboundHotbarEntry Entries[10] = {FJadeboundHotbarEntry::Skill(0),FJadeboundHotbarEntry::Skill(1)};
    constexpr FJadeboundHotbarEntry Resolve(int32 Slot) const { return Slot>=0 && Slot<10 ? Entries[Slot] : FJadeboundHotbarEntry{}; }
    constexpr int32 Find(FJadeboundHotbarEntry Entry) const
    {
        if (!Entry.IsValid() || Entry.Kind==EJadeboundHotbarKind::Empty) return INDEX_NONE;
        for(int32 Slot=0;Slot<10;++Slot) if(Entries[Slot].Equals(Entry)) return Slot;
        return INDEX_NONE;
    }
    constexpr bool IsValid() const
    {
        for(int32 Slot=0;Slot<10;++Slot)
        {
            if(!Entries[Slot].IsValid()) return false;
            if(Entries[Slot].Kind!=EJadeboundHotbarKind::Empty)
                for(int32 Earlier=0;Earlier<Slot;++Earlier) if(Entries[Slot].Equals(Entries[Earlier])) return false;
        }
        return true;
    }
    constexpr bool Assign(int32 Slot,FJadeboundHotbarEntry Entry)
    {
        if(!IsValid() || Slot<0 || Slot>=10 || !Entry.IsValid()) return false;
        if(Entry.Kind!=EJadeboundHotbarKind::Empty)
            for(auto& Existing:Entries) if(Existing.Equals(Entry)) Existing={};
        Entries[Slot]=Entry; return IsValid();
    }
    constexpr int32 RetainSelection(int32 Skill) const
    { return Find(FJadeboundHotbarEntry::Skill(Skill))!=INDEX_NONE ? Skill : INDEX_NONE; }
    constexpr bool Equals(const FJadeboundLocalHotbarBindings& Other) const
    { for(int32 Slot=0;Slot<10;++Slot) if(!Entries[Slot].Equals(Other.Entries[Slot])) return false; return true; }
    static bool Decode(const FString& Text,const FString& Hint,bool AllowSchema1,FJadeboundLocalHotbarBindings& Out);
    FString Encode() const;
};
static_assert([](){FJadeboundLocalHotbarBindings B;return B.IsValid() && B.Resolve(0).Equals(FJadeboundHotbarEntry::Skill(0)) && B.Resolve(9).Kind==EJadeboundHotbarKind::Empty;}(),"Typed defaults");
static_assert([](){FJadeboundLocalHotbarBindings B;return B.Assign(6,FJadeboundHotbarEntry::Skill(0)) && B.Assign(9,FJadeboundHotbarEntry::Skill(1)) && B.RetainSelection(0)==0 && B.Find(FJadeboundHotbarEntry::Skill(1))==9;}(),"Skill identities migrate without reindexing");
static_assert([](){FJadeboundLocalHotbarBindings B;return B.Assign(2,FJadeboundHotbarEntry::Item(7)) && B.Assign(3,FJadeboundHotbarEntry::Item(7)) && B.Resolve(2).Kind==EJadeboundHotbarKind::Empty && B.Resolve(3).Identity==7;}(),"Item identity moves, never duplicates stock");
static_assert([](){FJadeboundLocalHotbarBindings B;return B.Assign(0,FJadeboundHotbarEntry::Item(7)) && B.RetainSelection(0)==INDEX_NONE && B.Resolve(0).Kind==EJadeboundHotbarKind::Item;}(),"Item overwrite cannot remain a selected skill");
static_assert([](){FJadeboundLocalHotbarBindings B;const auto Before=B;return !B.Assign(10,FJadeboundHotbarEntry::Item(7)) && !B.Assign(0,FJadeboundHotbarEntry::Item(4)) && !B.Assign(0,FJadeboundHotbarEntry::Skill(7)) && B.Equals(Before);}(),"Invalid typed assignment is unchanged");

// Standalone skill presentation and local controls, never progression save/RPC data.
struct FJadeboundSkillPresentation
{
    bool bAvailable = false;
    bool bRun = true;
    bool bGuarding = false;
    bool bStepping = false;
    float Stamina = 100.f;
    int32 SelectedSlot = INDEX_NONE; // presented F-key slot, not skill identity
    FJadeboundHotbarEntry SlotEntries[10] = {FJadeboundHotbarEntry::Skill(0),FJadeboundHotbarEntry::Skill(1)};
    int32 BalmCount = 0;
    int64 HotbarRevision = 0;
    bool bHotbarConfigIssue = false;
    FText HotbarFeedback;
    double GuardCooldown = 0.0;
    double StepCooldown = 0.0;
};

USTRUCT(BlueprintType)
struct FJadeboundItemView
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Progression") int32 ItemId = 0;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Progression") FText Name;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Progression") FText Description;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Progression") int32 StackLimit = 1;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Progression") int64 BuyPrice = 0;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Progression") int64 SellPrice = 0;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Progression") bool bWeapon = false;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Progression") bool bPurchasable = false;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Progression") bool bSellable = false;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Progression") int32 HealingPercent = 0;
};

USTRUCT(BlueprintType)
struct FJadeboundInventorySlot
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, SaveGame, Category="Jadebound|Progression") int32 ItemId = 0;
    UPROPERTY(BlueprintReadOnly, SaveGame, Category="Jadebound|Progression") int32 Count = 0;
};

USTRUCT(BlueprintType)
struct FJadeboundProgressionState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, SaveGame, Category="Jadebound|Progression") int32 Version = 1;
    UPROPERTY(BlueprintReadOnly, SaveGame, Category="Jadebound|Progression") int64 Revision = 0;
    UPROPERTY(EditAnywhere, NotReplicated, SaveGame, Category="Jadebound|SaveData") uint64 ExactRevision = 0;
    UPROPERTY(BlueprintReadOnly, SaveGame, Category="Jadebound|Progression") int64 Coins = 10;
    UPROPERTY(BlueprintReadOnly, SaveGame, Category="Jadebound|Progression") TArray<FJadeboundInventorySlot> Slots;
    UPROPERTY(BlueprintReadOnly, SaveGame, Category="Jadebound|Progression") int32 EquippedWeapon = 1;
    UPROPERTY(BlueprintReadOnly, SaveGame, Category="Jadebound|Progression") int32 EquippedTorso = 0;
    UPROPERTY(BlueprintReadOnly, SaveGame, Category="Jadebound|Progression") int32 QuestState = 0;
    UPROPERTY(EditAnywhere, NotReplicated, SaveGame, Category="Jadebound|SaveData") int64 NextDropId = 1;
    UPROPERTY(EditAnywhere, NotReplicated, SaveGame, Category="Jadebound|SaveData") int64 QuestKillDropId = 0;
    UPROPERTY(EditAnywhere, NotReplicated, SaveGame, Category="Jadebound|SaveData") TArray<int64> RecordedDropIds;
    UPROPERTY(EditAnywhere, NotReplicated, SaveGame, Category="Jadebound|SaveData") TArray<int64> ProcessedDropIds;
    // Legacy arrays are retained for schema1 loading and must be empty in schema2.
    UPROPERTY(EditAnywhere, NotReplicated, SaveGame, Category="Jadebound|SaveData") TArray<int64> UnprocessedDropIds;
};

USTRUCT(BlueprintType)
struct FJadeboundSavedDrop
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, SaveGame, Category="Jadebound|SaveData") int64 DropId = 0;
    UPROPERTY(EditAnywhere, SaveGame, Category="Jadebound|SaveData") int32 ItemId = 0;
    UPROPERTY(EditAnywhere, SaveGame, Category="Jadebound|SaveData") int32 Count = 0;
    UPROPERTY(EditAnywhere, SaveGame, Category="Jadebound|SaveData") FVector Location = FVector::ZeroVector;
};

// One copied local selection; no save, replication, RPC or Blueprint identity.
// Weak actor/component witnesses cannot keep destroyed loot or a dead pawn alive.
struct FJadeboundConquerInteractionIntent
{
    TWeakObjectPtr<ACharacter> Pawn;
    TWeakObjectPtr<UPrimitiveComponent> Pick;
    TWeakObjectPtr<AActor> Visual;
    int64 DropId = -1; // inactive; zero is only our exact merchant marker
    uint64 ExactRevision = 0;
    int32 RecordIndex = INDEX_NONE; // stable only under the exact revision witness
    FJadeboundSavedDrop Record;
    FVector Destination = FVector::ZeroVector;
    FVector VisualLocation = FVector::ZeroVector;
    float Range = 0.f;
};

UENUM(BlueprintType)
enum class EJadeboundPersistenceFailureKind : uint8
{
    None,
    Retryable,
    RecoveryRequired
};

USTRUCT(BlueprintType)
struct FJadeboundPersistencePresentation
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Progression") EJadeboundPersistenceFailureKind FailureKind = EJadeboundPersistenceFailureKind::None;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Progression") FText Message;
};

USTRUCT(BlueprintType)
struct FJadeboundActionReceipt
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Development") int64 Sequence = 0;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Development") FName Action;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Development") bool bSucceeded = false;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Development") int64 ObservedRevision = 0;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Development") int64 LastVerifiedJournalRevision = 0;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Development") bool bHeldCandidate = false;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Development") bool bPersistenceBlocked = false;
    UPROPERTY(BlueprintReadOnly, Category="Jadebound|Development") FText Message;
};

UCLASS()
class JADEBOUNDPROGRESSION_API UJadeboundCharacterSave : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, SaveGame, Category="Jadebound|SaveData") FString CharacterId = TEXT("Traveler01");
    UPROPERTY(EditAnywhere, SaveGame, Category="Jadebound|SaveData") FJadeboundProgressionState State;
    UPROPERTY(EditAnywhere, SaveGame, Category="Jadebound|SaveData") TArray<FJadeboundSavedDrop> PendingDrops;
};

UCLASS(BlueprintType)
class JADEBOUNDPROGRESSION_API AJadeboundProgressionActor : public AActor
{
    GENERATED_BODY()
public:
    AJadeboundProgressionActor();
    virtual ~AJadeboundProgressionActor() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    FJadeboundHealthPresentation GetHealthPresentation() const;
    FJadeboundRevivePresentation GetRevivePresentation() const;
    bool RequestRevive(uint64 ExpectedGeneration);
    bool IsClickWalkAvailable() const;
    FJadeboundSkillPresentation GetSkillPresentation() const;
    void SelectSkillSlot(int32 Slot);
    void MarkSkillHudGesture();
    bool CanConfigureSkillHotbar() const;
    bool AssignSkillHotbarSlot(int32 Slot, int32 SkillIdentity, int64 ExpectedRevision);
    bool AssignItemHotbarSlot(int32 Slot, int32 InventorySlot, int32 ExpectedItemId, int64 ExpectedInventoryRevision, int64 ExpectedHotbarRevision);
    bool ResetSkillHotbar(int64 ExpectedRevision);
    void ToggleWalkRunFromHud() { ToggleConquerWalkRun(); }

    UPROPERTY(EditAnywhere, Replicated, Category="Jadebound|Equipment") TObjectPtr<UStaticMesh> AshwoodSpearMesh;
    UPROPERTY(EditAnywhere, Category="Jadebound|Loot") TObjectPtr<UStaticMesh> IronOreMesh;
    UPROPERTY(EditAnywhere, Category="Jadebound|Loot") TObjectPtr<UStaticMesh> JadeParcelMesh;
    UPROPERTY(EditAnywhere, Replicated, Category="Jadebound|Interactions") FVector MerchantPoint = FVector(-344.974746831,477.279220614,92.15);
    UPROPERTY(EditAnywhere, Replicated, Category="Jadebound|Interactions") float MerchantRange = 200.f;
    UPROPERTY(EditAnywhere, Replicated, Category="Jadebound|Interactions") float PickupRange = 180.f;

    UFUNCTION(BlueprintPure, Category="Jadebound|Progression") FJadeboundProgressionState GetProgressionState() const { return State; }
    UFUNCTION(BlueprintPure, Category="Jadebound|Progression") int64 GetRevision() const { return State.Revision; }
    UFUNCTION(BlueprintPure, Category="Jadebound|Progression") int64 GetCoins() const { return State.Coins; }
    UFUNCTION(BlueprintPure, Category="Jadebound|Progression") TArray<FJadeboundItemView> GetItemDefinitions() const;
    FText GetQuestObjectiveText() const;
    UFUNCTION(BlueprintPure, Category="Jadebound|Progression") FText GetQuestText() const;
    UFUNCTION(BlueprintPure, Category="Jadebound|Progression") FText GetFeedback() const { return PersistencePresentation.FailureKind != EJadeboundPersistenceFailureKind::None ? PersistencePresentation.Message : Feedback; }
    UFUNCTION(BlueprintPure, Category="Jadebound|Progression") FJadeboundPersistencePresentation GetPersistencePresentation() const { return PersistencePresentation; }
    UFUNCTION(BlueprintPure, Category="Jadebound|Progression") bool IsMerchantInRange() const;
    UFUNCTION(BlueprintPure, Category="Jadebound|Progression") bool IsProgressionReady() const { return bReady && !bPersistenceBlocked && (!bDevelopmentModel || IsDevelopmentReady()); }
    UFUNCTION(BlueprintPure, Category="Jadebound|Progression") bool IsMenuOpen() const { return bMenuOpen; }
    UFUNCTION(BlueprintPure, Category="Jadebound|Progression") bool CanSaveProgress() const { return (bUnsavedCandidate && bPersistenceBlocked && PersistencePresentation.FailureKind == EJadeboundPersistenceFailureKind::Retryable) || IsProgressionReady(); }
    UFUNCTION(BlueprintPure, Category="Jadebound|Development") bool IsDevelopmentModel() const { return bDevelopmentModel; }
    UFUNCTION(BlueprintPure, Category="Jadebound|Development") FString GetServerCharacterId() const { return ServerCharacterId; }
    UFUNCTION(BlueprintPure, Category="Jadebound|Development") FJadeboundActionReceipt GetLastActionReceipt() const { return HasAuthority() ? LastIssuedActionReceipt : LastActionReceipt; }

    UFUNCTION(BlueprintCallable, Category="Jadebound|Progression") bool BuyItem(int32 ItemId, int32 Quantity = 1);
    // Native controls pass the revision they displayed; Blueprint wrappers capture it at invocation.
    bool BuyItemAtRevision(int32 ItemId, int32 Quantity, int64 ExpectedRevision);
    UFUNCTION(BlueprintCallable, Category="Jadebound|Progression") bool SellItem(int32 SlotIndex, int32 Quantity = 1, int64 ExpectedRevision = -1);
    // Intent only: authority chooses the empty destination and revalidates the owned item.
    UFUNCTION(BlueprintCallable, Category="Jadebound|Progression") bool SplitItem(int32 SlotIndex, int32 Quantity, int32 ExpectedItemId, int64 ExpectedRevision);
    UFUNCTION(BlueprintCallable, Category="Jadebound|Progression") bool EquipItem(int32 SlotIndex, int64 ExpectedRevision = -1);
    UFUNCTION(BlueprintCallable, Category="Jadebound|Progression") bool UnequipWeapon();
    bool UnequipWeaponAtRevision(int64 ExpectedRevision);
    // Blueprint wrappers capture current life; native UI captures life when rendered.
    // Opt-in native owner RPC still needs rendered transport qualification.
    UFUNCTION(BlueprintCallable, Category="Jadebound|Progression") bool UseItem(int32 SlotIndex, int32 ExpectedItemId, int64 ExpectedRevision);
    bool UseItemAtLifeGeneration(int32 SlotIndex, int32 ExpectedItemId, int64 ExpectedRevision, uint64 ExpectedLifeGeneration);
    uint64 GetItemUseLifeGeneration() const;
    bool IsNativeConsumableContextAvailable() const;
    UFUNCTION(BlueprintCallable, Category="Jadebound|Progression") bool Interact();
    UFUNCTION(BlueprintCallable, Category="Jadebound|Progression") bool SaveProgress();
    UFUNCTION(BlueprintCallable, Category="Jadebound|Progression") bool ReloadProgress();
    UFUNCTION(BlueprintCallable, Category="Jadebound|Progression") void ToggleMenu();
    UFUNCTION(BlueprintCallable, Category="Jadebound|Progression") void CloseMenu();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    friend class AJadeboundDevelopmentGameMode;
    friend class AJadeboundNativeRaider;
#if WITH_DEV_AUTOMATION_TESTS
    // Native automation only: no reflected, public, or network entry point.
    friend struct FJadeboundActorBankTestAccess;
    friend struct FJadeboundV3PersistenceTestAccess;
    friend struct FJadeboundMerchantBulkSaleTestAccess;
    friend struct FJadeboundAttackWindowTestAccess;
    friend struct FJadeboundRecipientNativeTestAccess;
    friend struct FJadeboundStandaloneWriterTestAccess;
    friend struct FJadeboundPlayerLifeTestAccess;
    friend struct FJadeboundNativeBalmTestAccess;
    friend struct FJadeboundNativeStrikeIssuanceTestAccess;
    TFunction<IFileHandle*(const FString&)> AutomationJournalOpener;
    static bool EncodeJournalForAutomation(UJadeboundCharacterSave* Save, TArray<uint8>& Bytes);
#endif
    UPROPERTY(Replicated) bool bDevelopmentModel = false;
    UPROPERTY(ReplicatedUsing=OnRepNativePlayerLife) FJadeboundPlayerLife NativePlayerLife;
    TUniquePtr<Jadebound::Combat::PlayerLife::State, FJadeboundPlayerLifeDeleter> NativeLifeState;
    TWeakObjectPtr<UJadeboundPlayerLifeComponent> PublicLife;
    FTransform NativeReviveHome;
    bool bNativeReviveHomeSet = false;
    bool bNativeReviveInProgress = false;
    void InitializeNativePlayerLife();
    void PublishNativePlayerLife();
    void BindNativePlayerLife();
    bool ApplyNativePlayerStrike(AJadeboundNativeRaider* Enemy, uint64 StrikeId, uint64 ExpectedGeneration, int64 EnemyLife);
    bool RequestNativeRevive(uint64 ExpectedGeneration);
    bool CanNativeRevive(uint64 ExpectedGeneration) const;
    UFUNCTION(Server, Reliable) void ServerRequestNativeRevive(uint64 ExpectedGeneration);
    bool UseNativeOwnedItem(int32 Slot, int32 ExpectedItemId, int64 ExpectedRevision, uint64 ExpectedLifeGeneration);
    UFUNCTION(Server, Reliable) void ServerUseNativeItem(int32 Slot, int32 ExpectedItemId, int64 ExpectedRevision, uint64 ExpectedLifeGeneration);
    UFUNCTION() void OnRepNativePlayerLife();
    UPROPERTY(Replicated) FString ServerCharacterId;
    UPROPERTY(ReplicatedUsing=OnRepReadiness) bool bServerModelInitialized = false;
    UPROPERTY(Transient) TObjectPtr<UStaticMesh> DevelopmentGlaiveMesh;
    TWeakObjectPtr<UJadeboundEquipmentPresentationComponent> PublicEquipment;
    TUniquePtr<IFileHandle> DevelopmentWriterLease;
    TUniquePtr<IFileHandle> StandaloneWriterLease;
    TUniquePtr<FJadeboundNativeCapture, FJadeboundNativeCaptureDeleter> NativeCapture;
    bool bAuthorityMenuOpen = false;
    bool bExecutingDevelopmentRequest = false;
    bool bBalmUseInProgress = false;
    UPROPERTY(Transient) int64 BalmUseAdmissions = 0;
    UPROPERTY(Transient) int64 LastBalmUseRevision = -1;
    UPROPERTY(Transient) double BalmLastHealthBefore = 0.0;
    UPROPERTY(Transient) double BalmLastHealthAfter = 0.0;
    int64 AcknowledgmentSequence = 0;
    FJadeboundActionReceipt LastActionReceipt;
    FJadeboundActionReceipt LastIssuedActionReceipt;
    // Private server attack/accepted-death path. The client submits an empty
    // attack intent; no recipient, enemy, life, damage, money or item is supplied.
    UPROPERTY(Replicated) bool bRecipientCombatEnabled = false;
    UPROPERTY(Transient) TObjectPtr<UAnimMontage> NativeAttackMontage;
    TWeakObjectPtr<UJadeboundMeleePresentationComponent> PublicMelee;
    TWeakObjectPtr<APawn> NativeSwingPawn;
    TWeakObjectPtr<AJadeboundNativeRaider> NativeSwingTarget;
    TWeakObjectPtr<UStaticMeshComponent> NativeSwingComponent;
    int64 NativeSwingId = 0;
    int64 NativeSwingLife = 0;
    int32 NativeSwingWeapon = 0;
    double NativeHitAt = 0;
    double NativeSwingUntil = 0;
    double NativeCooldownUntil = 0;
    double NativeCloseGuardUntil = 0;
    bool bNativeHitAttempted = true;
    TWeakObjectPtr<AJadeboundNativeRaider> AwardedEnemy;
    int64 AwardedLife = 0;
    int64 AwardedDrop = 0;
    uint64 AwardedRevision = 0;
    void ConfigureRecipientCombat(bool bEnabled, UAnimMontage* Montage);
    bool IsRecipientSwingActive() const;
    bool RequestNativeAttack();
    void TickRecipientCombat();
    void CancelRecipientAttack();
    bool CommitRecipientDefeat(AJadeboundNativeRaider* Enemy);
    UFUNCTION(Server, Reliable) void ServerRequestNativeAttack();
    bool ConfigureDevelopmentOwner(APlayerController* Controller, const FString& CharacterId, UStaticMesh* Glaive);
    void DetachDevelopmentOwner(APlayerController* Controller);
    bool RetryDevelopmentSaveFromServer();
    bool HasDevelopmentContext() const;
    bool IsDevelopmentReady() const;
    UFUNCTION() void OnRepReadiness();
    FString GetJournalPath(int32 Slot) const;
    FString GetSaveCharacterId() const;
    void PublishDevelopmentEquipment();
    void AcknowledgeAction(FName Action, bool bSucceeded, bool bApplyMenu = false, bool bShowMenu = false);
    UFUNCTION(Server, Reliable) void ServerSetMenuOpen(bool bShowMenu);
    UFUNCTION(Client, Reliable) void ClientActionAcknowledged(const FJadeboundActionReceipt& Receipt, bool bApplyMenu, bool bShowMenu);
    UPROPERTY(ReplicatedUsing=OnRepState) FJadeboundProgressionState State;
    UPROPERTY(Replicated) FText Feedback;
    UPROPERTY(Replicated) FJadeboundPersistencePresentation PersistencePresentation;
    UPROPERTY(Transient) TObjectPtr<UStaticMesh> OriginalGlaiveMesh;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> WeaponComponent;
    UPROPERTY(Transient) TObjectPtr<APawn> Hero;
    UPROPERTY(Transient) TObjectPtr<APlayerController> Player;
    UPROPERTY(Transient) TObjectPtr<AActor> LegacyLoop;
    UPROPERTY(Transient) TObjectPtr<AActor> LegacyTarget;
    UPROPERTY(Transient) TArray<TObjectPtr<AActor>> DropVisuals;
    // Runtime selection aids only. Zero identifies our merchant marker;
    // positive values are exact authority-owned persisted DropIds.
    TMap<TWeakObjectPtr<UPrimitiveComponent>, int64> InteractionHandles;
    TWeakObjectPtr<AActor> MerchantClickMarker;
    bool bMerchantMarkerAttempted = false;
    TArray<FJadeboundSavedDrop> PendingDrops;
    TUniquePtr<Jadebound::ProgressionCore, Jadebound::ProgressionCoreDeleter> Core;
    TSharedPtr<SWidget> MenuWidget;
    TSharedPtr<SWidget> HudWidget;
    UPROPERTY(Transient) TObjectPtr<UJadeboundCharacterSave> LastCommittedSave;
    // A retry serializes this same copied candidate; it never reruns its transaction.
    UPROPERTY(Transient) TObjectPtr<UJadeboundCharacterSave> HeldSaveCandidate;
    TArray<uint8> HeldJournalBytes; // First encoded JBP1 image; never reserialize during retry.
    FString HeldJournalPath;
    int32 HeldJournalSlot = INDEX_NONE;
    double PersistenceRetryAt = 0.0;
    uint8 PersistenceRetryStep = 0;
    bool bPersistenceEndingPlay = false;
    bool bV3JournalAdmitted = false; // Set only by V3-aware ReadSave under the retained writer lease.
    int32 LastSaveSlot = 1;
    int32 SeenDefeats = 0;
    int32 AttackBindingIndex = INDEX_NONE;
    TWeakObjectPtr<UEnhancedInputComponent> AttackGateComponent;
    TWeakObjectPtr<APawn> AttackGatePawn;
    TArray<uint32> AttackGateBindingHandles;
    uint64 LastMenuCloseFrame = MAX_uint64;
    bool bReady = false;
    bool bMenuOpen = false;
    bool bAwaitingPointerRelease = false;
    UPROPERTY(Replicated) bool bPersistenceBlocked = false;
    UPROPERTY(Replicated) bool bUnsavedCandidate = false;
    bool bBindingsInstalled = false;

    // Runtime-only controls: no replicated, economic, save or Blueprint state.
    UPROPERTY(Transient) TObjectPtr<UInputMappingContext> ClickWalkContext;
    UPROPERTY(Transient) TObjectPtr<UInputAction> ClickWalkAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> PointerMotionAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> SelectedSkillAction;
    UPROPERTY(Transient) TArray<TObjectPtr<UInputAction>> SkillSlotActions;
    UPROPERTY(Transient) float SkillStamina = 100.f;
    UPROPERTY(Transient) int32 SelectedSkillSlot = INDEX_NONE; // preserved skill identity: Guard0 / Current1
    FJadeboundLocalHotbarBindings LocalSkillBindings;
    int64 SkillHotbarRevision = 0;
    bool bSkillHotbarLoaded = false;
    bool bSkillHotbarConfigIssue = false;
    bool bHotbarMigrationPending = false;
    FText SkillHotbarFeedback;
    UPROPERTY(Transient) int32 ActiveSkill = 0; // 0 none, 1 guard, 2 current
    UPROPERTY(Transient) int64 SkillCastAdmissions = 0;
    UPROPERTY(Transient) int64 GuardMitigatedImpacts = 0;
    UPROPERTY(Transient) FName SkillStopReason;
    TWeakObjectPtr<ACharacter> SkillResourcePawn;
    TWeakObjectPtr<ACharacter> SkillPawn;
    double SkillEndsAt = 0.0;
    double SkillRecoveryAt = 0.0;
    double GuardReadyAt = 0.0;
    double CurrentReadyAt = 0.0;
    bool bSkillDispatching = false;
    bool bSkillRightHeld = false;
    bool bSkillKeyHeld[10] = {};
    FVector CurrentStart = FVector::ZeroVector;
    FVector CurrentGoal = FVector::ZeroVector;
    FVector CurrentDirection = FVector::ZeroVector;
    FVector CurrentProgressPoint = FVector::ZeroVector;
    double CurrentProgressAt = 0.0;
    FVector SkillQueuedInput = FVector::ZeroVector;
    uint64 SkillQueuedFrame = MAX_uint64;
    bool bSkillInputQueued = false;
    float CurrentSavedAcceleration = 0.f;
    float CurrentBaseSpeed = 0.f;
    bool bCurrentSavedWalkOffLedges = false;
    bool bCurrentMovementLease = false;
    TArray<FVector> ConquerPathPoints;
    int32 ConquerPathIndex = 0;
    UPROPERTY(Transient) TObjectPtr<AActor> ConquerAttackTarget;
    int32 ConquerTargetLife = -1;
    FJadeboundConquerInteractionIntent ConquerInteractionIntent;
    FVector ConquerTargetPathLocation = FVector::ZeroVector;
    double ConquerNextPathAt = 0.0;
    double ConquerNextAttackAt = 0.0;
    UPROPERTY(Transient) int64 ConquerAttackDispatches = 0;
    UPROPERTY(Transient) int64 ConquerObservedAdmissions = 0;
    UPROPERTY(Transient) FName ConquerControlReason;
    // Native actor-lifetime diagnostic only; never reflected, replicated or saved.
    uint32 ConquerGroundDiagnosticEntries = 0;
    UPROPERTY(Transient) bool bConquerRun = true;
    TWeakObjectPtr<ACharacter> ConquerMovementPawn;
    bool bConquerMovementPolicy = false;
    bool bSavedControllerYaw = false;
    bool bSavedOrientMovement = false;
    bool bSavedDesiredRotation = false;
    FRotator SavedMovementRotationRate = FRotator::ZeroRotator;
    float SavedMovementSpeed = 0.f;
    float ConquerOwnedSpeed = 0.f;
    TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> ClickWalkSubsystem;
    TWeakObjectPtr<UEnhancedInputComponent> ClickWalkGate;
    TWeakObjectPtr<APawn> ClickWalkGatePawn;
    TArray<uint32> ClickWalkBindingHandles;
    TWeakObjectPtr<ACharacter> ClickWalkPawn;
    TWeakObjectPtr<APlayerController> ClickWalkPlayer;
    UPROPERTY(Transient) bool bClickWalkActive = false;
    UPROPERTY(Transient) FVector ClickWalkGoal = FVector::ZeroVector;
    UPROPERTY(Transient) FName ClickWalkStopReason;
    double ClickWalkStartedAt = 0.0;
    double ClickWalkProgressAt = 0.0;
    FVector ClickWalkProgressPoint = FVector::ZeroVector;
    FVector ClickWalkQueuedInput = FVector::ZeroVector;
    uint64 ClickWalkQueuedFrame = MAX_uint64;
    bool bClickWalkInputQueued = false;
    bool bClickWalkManualObserved = false;
    TWeakObjectPtr<APlayerController> WorldCursorPlayer;
    TWeakObjectPtr<UGameViewportClient> WorldCursorViewport;
    bool bWorldCursorPolicyApplied = false;
    bool bSavedWorldCursorVisible = false;
    bool bSavedWorldCursorHiddenDuringCapture = true;
    uint8 SavedWorldMouseCapture = 0;
    uint8 SavedWorldMouseLock = 0;

    // One standalone courtyard strike. Runtime witnesses only: not replicated,
    // economic state, save data, client requests, or Blueprint asset edits.
    UPROPERTY(Transient) int32 RaiderStrikePhase = 0; // 0 idle, 1 windup, 2 recovery
    UPROPERTY(Transient) int64 RaiderStrikeId = 0;
    UPROPERTY(Transient) int32 RaiderStrikeLifeGeneration = -1; // AreaLoop RespawnCount
    UPROPERTY(Transient) int64 RaiderStrikeDamageAttempts = 0;
    UPROPERTY(Transient) int64 RaiderStrikeObservedHits = 0;
    UPROPERTY(Transient) int64 RaiderStrikeCancellations = 0;
    UPROPERTY(Transient) int64 RaiderLastImpactStrikeId = 0;
    UPROPERTY(Transient) int32 RaiderLastImpactLifeGeneration = -1;
    UPROPERTY(Transient) double RaiderLastImpactWorldSeconds = 0.0;
    UPROPERTY(Transient) FName RaiderStrikeStopReason;
    UPROPERTY(Transient) float RaiderLastDamageReturned = 0.f;
    UPROPERTY(Transient) double RaiderLastHeroHealthBefore = 0.0;
    UPROPERTY(Transient) double RaiderLastHeroHealthAfter = 0.0;
    UPROPERTY(Transient) bool bRaiderLastHealthAfterAvailable = false;
    UPROPERTY(Transient) FString RaiderLastDamagePawnPath;
    UPROPERTY(Transient) FString RaiderLastDamageTargetPath;
    UPROPERTY(Transient) FVector RaiderStrikeAim = FVector::ZeroVector;
    UPROPERTY(Transient) TObjectPtr<ATextRenderActor> RaiderStrikeCue;
    TWeakObjectPtr<ACharacter> RaiderStrikePawn;
    TWeakObjectPtr<ACharacter> RaiderStrikeTarget;
    TWeakObjectPtr<AActor> RaiderStrikeLoop;
    double RaiderStrikePhaseEndsAt = 0.0;
    double RaiderNextStrikeAt = 0.0;
    bool bRaiderImpactLatched = false;

    UFUNCTION() void OnRepState();
    UFUNCTION(Server, Reliable) void ServerBuy(int32 ItemId, int32 Quantity, int64 ExpectedRevision);
    UFUNCTION(Server, Reliable) void ServerSell(int32 SlotIndex, int32 Quantity, int64 ExpectedRevision);
    UFUNCTION(Server, Reliable) void ServerSplit(int32 SlotIndex, int32 Quantity, int32 ExpectedItemId, int64 ExpectedRevision);
    UFUNCTION(Server, Reliable) void ServerEquip(int32 SlotIndex, int64 ExpectedRevision);
    UFUNCTION(Server, Reliable) void ServerUnequip(int64 ExpectedRevision);
    UFUNCTION(Server, Reliable) void ServerInteract();
    UFUNCTION(Server, Reliable) void ServerSave();
    UFUNCTION(Server, Reliable) void ServerReload();
    // Private loopback development receipt, not character save/RPC award data.
    // Runtime credentials remain under ignored Saved; never print the token.
    UFUNCTION(Client, Reliable) void ClientDevelopmentResumeTicket(const FString& SessionId,
        const FString& CharacterId, const FString& Ticket);
    // The controller's original OnDestroyed delegate is inspected, never replaced.
    TWeakObjectPtr<APawn> ReviveDamagePawn;
    TWeakObjectPtr<APawn> ReviveCorpse;
    TWeakObjectPtr<APlayerController> ReviveController;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> ReviveSpirit;
    bool bReviveWaiting = false;
    bool bReviveRequested = false;
    bool bReviveHeld = false;
    bool bReviveMoveLease = false;
    bool bReviveLookLease = false;
    bool bReviveSavedCursor = false;
    bool bReviveSpiritAttempted = false;
    UPROPERTY(Transient) bool bReviveReadyAnnounced = false;
    uint64 ReviveGeneration = 0;
    double ReviveReadyAt = 0.0;
    double ReviveDeathAt = 0.0;
    double RevivePointerGuardUntil = 0.0;
    FVector ReviveFallPoint = FVector::ZeroVector;
    FText ReviveMessage;
    UFUNCTION() void OnReviveDamage(AActor* DamagedActor, float Damage, const UDamageType* DamageType,
        AController* InstigatedBy, AActor* DamageCauser);
    bool HasLegacyReviveContract(const APawn* Pawn, const APlayerController* Controller) const;
    void UpdateReviveBinding();
    void TickRevive();
    void RestoreReviveInput(bool bApplyToLiveWorld);
    void BindGameplay();
    bool ResolveRaiderStrikeContext(ACharacter*& Pawn, ACharacter*& Target, int32& LifeGeneration, FName& Reason) const;
    bool ValidateRaiderStrikeGeometry(const ACharacter* Pawn, const ACharacter* Target, const FVector& Aim, FName& Reason) const;
    bool UpdateRaiderStrikeCue(const ACharacter* Target, const TCHAR* Text, FColor Color);
    void CancelRaiderStrike(FName Reason, double Now);
    void TickRaiderStrike();

    ACharacter* ResolveClickWalkScope() const;
    ACharacter* ResolveClickWalkCharacter() const;
    bool IsManualWalkInputActive() const;
    void UpdateClickWalkBindings();
    void RemoveClickWalkBindings();
    void UpdateWorldCursor();
    void RestoreWorldCursor();
    void HandleClickWalkPressed(const FInputActionInstance& Action);
    bool BuildConquerPath(ACharacter* Character, const FVector& Destination, bool bTargetDestination = false);
    bool StartConquerGoal(ACharacter* Character, const FVector& Destination, AActor* Target);
    bool TryConquerAttack(ACharacter* Character);
    bool TryConquerJump(ACharacter* Character, const FHitResult& Ground);
    bool IsWorldPointerGestureActive() const;
    void HandleSelectedSkill(const FInputActionInstance& Action);
    void HandleSkillSlot(const FInputActionInstance& Action, int32 Slot);
    void CastSelectedSkill();
    void LoadLocalSkillHotbar();
    bool PersistLocalSkillHotbar() const;
    bool CommitSkillHotbar(const FJadeboundLocalHotbarBindings& Proposed, int64 ExpectedRevision);
    bool HasOwnedHotbarKey(int32 Slot) const;
    bool IsHotbarUseContextAvailable() const;
    void UseHotbarItem(int32 Slot, int64 ExpectedHotbarRevision);
    enum class EItemUseContext : uint8 { Pack, Hotbar };
    bool UseOwnedItem(int32 Slot, int32 ExpectedItemId, int64 ExpectedRevision, EItemUseContext Context);
    bool BuildPassingCurrent(ACharacter* Character, FVector& Destination) const;
    void TickSkills(float DeltaSeconds);
    void CancelSkills(FName Reason, bool bStopVelocity);
    UFUNCTION() void OnSkillMovementUpdated(float DeltaSeconds, FVector OldLocation, FVector OldVelocity);
    float GuardDamageForImpact(ACharacter* Pawn, ACharacter* Target, float Damage);
    void ToggleConquerWalkRun();
    void ApplyConquerMovementPolicy(ACharacter* Character);
    void RestoreConquerMovementPolicy();
    void HandleManualWalkAction(const FInputActionInstance& Action);
    void HandleManualWalkKey();
    void TickClickWalk();
    void StopClickWalk(FName Reason, bool bStopVelocity);
    UFUNCTION() void OnClickWalkMovementUpdated(float DeltaSeconds, FVector OldLocation, FVector OldVelocity);
    void BindAttackActionGate();
    void RefreshAttackActionGate();
    void HandleAttackActionGate(const FInputActionInstance& Action);
    void CancelLegacyAttackPresentation();
    bool HasLivingPossessedHero() const;
    bool IsCloseFrameGuardActive() const;
    bool IsAttackBlocked() const;
    void ApplyEquipment();
    void SyncLegacyQuestPresentation();
    void RefreshState();
    void RefreshDropVisuals();
    bool RegisterInteractionHandle(AActor* Visual, int64 DropId, float Radius);
    bool TryHandleConquerInteractionHit(ACharacter* Character, const FHitResult& Hit);
    bool ValidateConquerInteractionIntent(const FJadeboundConquerInteractionIntent& Selection) const;
    bool StartConquerInteraction(const FJadeboundConquerInteractionIntent& Selection);
    bool TickConquerInteraction(ACharacter* Character);
    bool HandleConquerInteractionSelection(const FJadeboundConquerInteractionIntent& Selection, bool bAllowApproach);
    bool TryHandleConquerLootCursor(ACharacter* Character);
    bool PickupPendingDrop(int64 DropId);
    bool InteractWithMerchant();
    void PollDefeats();
    void AttackWithoutWeaponFeedback();
    void HandleInteract();
    void HandleSave() { SaveProgress(); }
    void HandleReload() { ReloadProgress(); }
    bool AuthorityReady();
    bool MatchesInventoryIntentRevision(int64 ExpectedRevision);
    bool IsLegacyWeaponChangeBlocked() const;
    bool FinishTransaction(const Jadebound::Result& Result, const FText& Success);
    bool AcquireStandaloneWriterLease();
    bool HasJournalWriterLease() const;
    bool WriteSave();
    void SetPersistenceFailure(EJadeboundPersistenceFailureKind Kind, const FText& Message);
    void ClearPersistenceFailure();
    bool RetryHeldSave(bool bAcknowledge = true);
    bool TickPersistenceRetry();
    bool ReadSave(bool bInitialLoad);
    void ShowMenu(bool bShow);
    void EnsureHud();
    static bool DecodeState(const FJadeboundProgressionState& Source, Jadebound::Snapshot& Destination);
};
