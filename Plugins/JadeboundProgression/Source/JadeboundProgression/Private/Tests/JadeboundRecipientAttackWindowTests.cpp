#include "JadeboundProgressionActor.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "JadeboundDevelopmentGameMode.h"
#include "JadeboundNativeRaider.h"
#include "JadeboundMeleePresentationComponent.h"
#include "Combat/RecipientAnimationRules.h"
#include "Combat/RecipientCombatRules.h"
#include "Development/DevelopmentIdentity.h"
#include "Domain/ProgressionCore.h"
#include "Persistence/ProgressionSaveV3.h"
#include "Persistence/JournalNamespacePaths.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"

// Distinct automation-only native friend; no reflection or network entry point.
struct FJadeboundAttackWindowTestAccess
{
    static bool Initialize(AJadeboundProgressionActor& Model, APlayerController& PC, ACharacter& Hero,
        AJadeboundDevelopmentGameMode& Mode, AJadeboundNativeRaider& Enemy,
        const FString& Id, UStaticMesh* Glaive, UAnimMontage* Montage)
    {
        if (!Model.ConfigureDevelopmentOwner(&PC, Id, Glaive)) return false;
        if (PC.GetPawn() != &Hero || Hero.GetController() != &PC) return false;
        Model.Hero = &Hero; // ConfigureDevelopmentOwner clears Hero; actual possession supplied.
        Model.bReady = Model.ReadSave(true);
        Model.bServerModelInitialized = Model.bReady;
        if (!Model.bReady) return false;
        Model.RefreshState();
        Mode.bEnableRecipientCombat = true; // Trusted fixture instance registry, not StartPlay/PostLogin.
        Mode.RecipientRaider = &Enemy;
        Mode.Models.Add(Id, &Model);
        Mode.ActiveControllers.Add(&PC, Id);
        Hero.DispatchBeginPlay(); // Actual registered components and preserved Blueprint callback.
        if (!IsValid(&Hero) || Hero.IsActorBeingDestroyed() || !IsValid(&Model) || Model.IsActorBeingDestroyed() || !Hero.HasActorBegunPlay()) return false;
        Model.BindGameplay();
        if (!IsValid(&Hero) || Hero.IsActorBeingDestroyed() || !IsValid(&Model) || Model.IsActorBeingDestroyed()) return false;
        Model.ApplyEquipment(); // Actual weapon discovery, attachment and authority appearance; no assignments.
        if (!IsValid(&Hero) || Hero.IsActorBeingDestroyed() || !IsValid(&Model) || Model.IsActorBeingDestroyed()) return false;
        Model.ConfigureRecipientCombat(true, Montage);
        return Mode.OwnsRecipientModel(&Model, true) && Model.IsDevelopmentReady() && Model.IsProgressionReady() &&
            Model.WeaponComponent && Model.WeaponComponent->GetOwner() == &Hero &&
            Model.WeaponComponent->GetStaticMesh() == Glaive && Model.WeaponComponent->IsVisible();
    }
    static bool Request(AJadeboundProgressionActor& Model) { return Model.RequestNativeAttack(); }
    static void Poll(AJadeboundProgressionActor& Model) { Model.TickRecipientCombat(); }
    static void Menu(AJadeboundProgressionActor& Model, bool bOpen) { Model.ServerSetMenuOpen(bOpen); }
    static UJadeboundMeleePresentationComponent* Presentation(const AJadeboundProgressionActor& Model) { return Model.PublicMelee.Get(); }
    static const Jadebound::Snapshot& Core(const AJadeboundProgressionActor& Model) { return Model.Core->GetSnapshot(); }
    static const UJadeboundCharacterSave* Saved(const AJadeboundProgressionActor& Model) { return Model.LastCommittedSave.Get(); }
    static bool Clean(const AJadeboundProgressionActor& Model)
    { return !Model.bPersistenceBlocked && !Model.bUnsavedCandidate && !Model.HeldSaveCandidate && Model.PendingDrops.IsEmpty() && Model.bV3JournalAdmitted &&
        Model.HeldJournalBytes.IsEmpty() && Model.HeldJournalPath.IsEmpty() && Model.HeldJournalSlot == INDEX_NONE; }
    struct Swing
    {
        int64 Id, Life;
        double Hit, End, Cooldown;
        bool bAttempted;
        TWeakObjectPtr<APawn> Pawn;
        TWeakObjectPtr<AJadeboundNativeRaider> Target;
        TWeakObjectPtr<UStaticMeshComponent> Weapon;
    };
    static Swing ReadSwing(const AJadeboundProgressionActor& Model)
    { return {Model.NativeSwingId, Model.NativeSwingLife, Model.NativeHitAt, Model.NativeSwingUntil,
        Model.NativeCooldownUntil, Model.bNativeHitAttempted, Model.NativeSwingPawn, Model.NativeSwingTarget, Model.NativeSwingComponent}; }
    static bool SameSwing(const Swing& A, const Swing& B)
    { return A.Id == B.Id && A.Life == B.Life && A.Hit == B.Hit && A.End == B.End && A.Cooldown == B.Cooldown &&
        A.bAttempted == B.bAttempted && A.Pawn == B.Pawn && A.Target == B.Target && A.Weapon == B.Weapon; }
    static bool MenuCancelled(const AJadeboundProgressionActor& Model)
    { return Model.bAuthorityMenuOpen && Model.bNativeHitAttempted && !Model.NativeSwingPawn.IsValid() &&
        !Model.NativeSwingTarget.IsValid() && !Model.NativeSwingComponent.IsValid() && Model.NativeSwingUntil == 0; }
    static bool HitExactlyOnce(const AJadeboundNativeRaider& Enemy, const AJadeboundProgressionActor& Model)
    { return Enemy.Life.LifeId == 1 && Enemy.Life.bAlive && Enemy.Life.Health == 2 && Enemy.LastHitSwing.Num() == 1 &&
        Enemy.LastHitSwing.FindRef(TWeakObjectPtr<AJadeboundProgressionActor>(const_cast<AJadeboundProgressionActor*>(&Model))) == 1 &&
        Enemy.LastValidRecipient.Get() == &Model && !Enemy.bDeathAwardCommitted; }
    static bool NoHit(const AJadeboundNativeRaider& Enemy)
    { return Enemy.Life.LifeId == 1 && Enemy.Life.bAlive && Enemy.Life.Health == 3 &&
        Enemy.LastHitSwing.IsEmpty() && !Enemy.LastValidRecipient.IsValid() && !Enemy.bDeathAwardCommitted; }
    static void End(AJadeboundProgressionActor& Model)
    { Model.bPersistenceEndingPlay = true; Model.CancelRecipientAttack(); Model.AutomationJournalOpener = nullptr; Model.DevelopmentWriterLease.Reset(); }
    static void SetOpener(AJadeboundProgressionActor& Model, TFunction<IFileHandle*(const FString&)> Opener)
    { Model.AutomationJournalOpener = MoveTemp(Opener); }
};

namespace JadeboundAttackWindow
{
// 0/1 V1, 2/3 current V3, 4/5 retained V2, 6/7 forward V4.
inline constexpr int32 JournalPathCount = 8;
static_assert(Jadebound::SnapshotVersion == 3, "Current recipient fixture requires the V3 domain");
using Access = FJadeboundAttackWindowTestAccess;
const FVector EnemyPoint(-26.777, 31.802, 92.15);
constexpr float Step = .04f;

bool SameCore(const Jadebound::Snapshot& A, const Jadebound::Snapshot& B)
{
    if (A.Version != B.Version || A.Revision != B.Revision || A.Coins != B.Coins || A.Quest != B.Quest ||
        A.EquippedWeapon != B.EquippedWeapon || A.EquippedTorso != B.EquippedTorso || A.NextDropId != B.NextDropId || A.QuestKillDropId != B.QuestKillDropId ||
        A.UnprocessedDropIds != B.UnprocessedDropIds) return false;
    for (uint32 I = 0; I < Jadebound::InventorySlotCount; ++I)
        if (A.Inventory[I].Id != B.Inventory[I].Id || A.Inventory[I].Count != B.Inventory[I].Count) return false;
    return true;
}
struct Bank { bool bExists = false; TArray<uint8> Bytes; };
bool ReadBank(const FString& Path, Bank& Out)
{
    IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
    Out.bExists = Jadebound::Persistence::HasJournalEntry(Files, Path); Out.Bytes.Reset();
    if (!Out.bExists) return true;
    if (!Files.FileExists(*Path) || Files.DirectoryExists(*Path)) return false;
    const int64 Size = Files.FileSize(*Path);
    if (Size <= 0 || Size > 1024 * 1024 + 32) return false;
    TUniquePtr<IFileHandle> File(Files.OpenRead(*Path));
    if (!File) return false;
    Out.Bytes.SetNumUninitialized(static_cast<int32>(Size));
    return File->Read(Out.Bytes.GetData(), Size);
}
bool OwnedUser(FString& User)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("JadeboundRecipientAttackWindowTests")) ||
        !FParse::Value(FCommandLine::Get(), TEXT("UserDir="), User) || User.IsEmpty() || FPaths::IsRelative(User)) return false;
    User.ReplaceInline(TEXT("\\"), TEXT("/"));
    TArray<FString> Parts; User.ParseIntoArray(Parts, TEXT("/"), true);
    for (const auto& Part : Parts) if (Part == TEXT(".") || Part == TEXT("..")) return false;
    FPaths::NormalizeDirectoryName(User);
    FString Effective = FPaths::ProjectUserDir(); FPaths::NormalizeDirectoryName(Effective);
    return FPaths::IsSamePath(User, Effective) && FPaths::GetCleanFilename(User) == TEXT("UserDir-v001") &&
        FPaths::GetCleanFilename(FPaths::GetPath(User)) == TEXT("Jadebound-P102-A008") &&
        FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()), User);
}

struct FScope
{
    FAutomationTestBase& Test;
    UWorld* World = nullptr;
    TStrongObjectPtr<UGameInstance> Instance;
    APlayerController* PC = nullptr;
    ACharacter* Hero = nullptr;
    AJadeboundProgressionActor* Model = nullptr;
    AJadeboundNativeRaider* Enemy = nullptr;
    UAnimMontage* Montage = nullptr;
    FString Id, Paths[JournalPathCount];
    Bank Before[JournalPathCount];
    const UJadeboundCharacterSave* OriginalSaved = nullptr;
    int32 Opens = 0, BaselineOpens = 0;
    bool bForeign = false, bValid = false;
    FScope(FAutomationTestBase& InTest) : Test(InTest)
    {
        FString User;
        if (!IsInGameThread() || !GEngine || !GEngine->WorldSettingsClass || !OwnedUser(User))
        { Test.AddError(TEXT("Initialized engine/game thread and owned A008 UserDir/opt-in required.")); return; }
        auto* HeroClass = LoadClass<ACharacter>(nullptr, TEXT("/Game/Jadebound/Tests/GlaiveCombatV006/BP_GlaiveHero_V006.BP_GlaiveHero_V006_C"));
        auto* Glaive = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Jadebound/OriginalWeapons/JadeGlaiveV001/SM_JadeGlaive_CM_V001.SM_JadeGlaive_CM_V001"));
        Montage = LoadObject<UAnimMontage>(nullptr, TEXT("/Game/Jadebound/Development/RecipientAnimationV001/AM_RecipientAttack_Template_v001.AM_RecipientAttack_Template_v001"));
        if (!HeroClass || !Glaive || !Jadebound::Combat::IsSafeMeleeMontage(Montage) || Montage->SlotAnimTracks[0].SlotName != TEXT("DefaultSlot"))
        { Test.AddError(TEXT("Actual preserved Hero/glaive and admitted DefaultSlot montage required.")); return; }
        Id = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
        if (!Jadebound::Development::IsCharacterId(Id)) { Test.AddError(TEXT("Fresh canonical nonzero GUID required.")); return; }
        Jadebound::Persistence::JournalNamespacePaths Namespaces;
        if (!Jadebound::Persistence::BuildJournalNamespacePaths(true, Id, Namespaces))
        { Test.AddError(TEXT("Current and retained journal namespace paths must be valid.")); return; }
        for (int32 Slot = 0; Slot < 2; ++Slot)
        {
            Paths[Slot] = Namespaces.Banks[0][Slot]; Paths[2 + Slot] = Namespaces.Banks[2][Slot];
            Paths[4 + Slot] = Namespaces.Banks[1][Slot]; Paths[6 + Slot] = Namespaces.ForwardV4[Slot];
        }
        for (int32 I = 0; I < JournalPathCount; ++I)
            if (!FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Paths[I]), User))
            { Test.AddError(TEXT("Actual journal paths must remain under owned UserDir.")); return; }
        if (FPlatformFileManager::Get().GetPlatformFile().DirectoryExists(*FPaths::GetPath(Paths[2])))
        { Test.AddError(TEXT("Fresh identity directory required; no reset/reuse.")); return; }
        UWorld::InitializationValues Values;
        Values.InitializeScenes(true).CreatePhysicsScene(true).EnableTraceCollision(true).ShouldSimulatePhysics(false)
            .AllowAudioPlayback(false).RequiresHitProxies(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false).SetTransactional(false);
        World = UWorld::CreateWorld(EWorldType::Game, false, FName(*FString::Printf(TEXT("AttackWindow_%s"), *Id)), nullptr, true, ERHIFeatureLevel::Num, &Values);
        if (!World) { Test.AddError(TEXT("World creation failed.")); return; }
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        Instance.Reset(NewObject<UGameInstance>(GEngine)); World->SetGameInstance(Instance.Get());
        if (!Instance || !World->GetWorldSettings() || !World->GetPhysicsScene()) { Test.AddError(TEXT("Real world prerequisites unavailable.")); return; }
        World->GetWorldSettings()->DefaultGameMode = AJadeboundDevelopmentGameMode::StaticClass();
        if (!World->SetGameMode(FURL())) { Test.AddError(TEXT("Actual authority mode route failed.")); return; }
        // Actor::ProcessEvent gates generated RPCs and ReceiveBeginPlay on real
        // world initialization. Route the public lifecycle before spawning the
        // fixture actors; do not force its flag or bypass the RPC wrapper.
        World->InitializeActorsForPlay(FURL(), false);
        if (!World->AreActorsInitialized() || World->HasBegunPlay())
        { Test.AddError(TEXT("Genuine initialized, nonbegun world required for actual Blueprint/RPC dispatch.")); return; }
        auto* Mode = Cast<AJadeboundDevelopmentGameMode>(World->GetAuthGameMode());
        FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Enemy = World->SpawnActor<AJadeboundNativeRaider>(EnemyPoint, FRotator::ZeroRotator, Spawn);
        PC = World->SpawnActor<APlayerController>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
        Hero = World->SpawnActor<ACharacter>(HeroClass, EnemyPoint - FVector(100, 0, 0), FRotator::ZeroRotator, Spawn);
        Model = World->SpawnActor<AJadeboundProgressionActor>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
        if (!Mode || !Enemy || !PC || !Hero || !Model || !Mode->HasAuthority() || !Model->HasAuthority() || World->GetNetMode() != NM_Standalone || !World->AreActorsInitialized() || World->HasBegunPlay())
        { Test.AddError(TEXT("Exact standalone authority actors required.")); return; }
        PC->Possess(Hero);
        Access::SetOpener(*Model, [this](const FString& Path) -> IFileHandle*
        {
            if (Path != Paths[2] && Path != Paths[3]) { bForeign = true; return nullptr; }
            ++Opens;
            return FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*Path, false, false);
        });
        if (!Access::Initialize(*Model, *PC, *Hero, *Mode, *Enemy, Id, Glaive, Montage))
        { Test.AddError(TEXT("Actual possession, Hero BeginPlay, readiness, equipment and recipient guards must pass.")); return; }
        auto* Mesh = Hero->GetMesh(); auto* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
        if (!Mesh || !Mesh->GetSkeletalMeshAsset() || !Anim || Mesh->GetSkeletalMeshAsset()->GetSkeleton() != Montage->GetSkeleton() ||
            Anim->GetClass()->GetPathName() != TEXT("/Game/Variant_Combat/Anims/ABP_Manny_Combat.ABP_Manny_Combat_C") ||
            Mesh->bPauseAnims || Mesh->bNoSkeletonUpdate || !Mesh->bEnableAnimation || Anim->GetCurrentActiveMontage())
        { Test.AddError(TEXT("Exact actual ABP/skeleton and enabled idle animation required; no replacement instance.")); return; }
        Test.AddInfo(FString::Printf(TEXT("Actual headless mesh tick option=%d; explicit public animation evaluation, not render visibility proof. Files retained: %s"),
            static_cast<int32>(Mesh->VisibilityBasedAnimTickOption), *FPaths::GetPath(Paths[2])));
        OriginalSaved = Access::Saved(*Model); BaselineOpens = Opens;
        for (int32 I = 0; I < JournalPathCount; ++I) if (!ReadBank(Paths[I], Before[I])) { Test.AddError(TEXT("Owned IO baseline read failed.")); return; }
        bValid = EconomyUnchanged() && Access::NoHit(*Enemy) && Jadebound::Combat::CanReach(Hero->GetActorLocation(), Hero->GetActorForwardVector(), Enemy->GetActorLocation(), 1);
        if (!bValid) Test.AddError(TEXT("Independent complete r0/IO/known enemy/range baseline unavailable."));
    }
    ~FScope()
    {
        if (!World) return;
        if (IsValid(Model) && !Model->IsActorBeingDestroyed()) Access::End(*Model);
        UEngine* Engine = GEngine;
        if (!Engine || !Engine->GetWorldContextFromWorld(World)) { Test.AddError(TEXT("Engine/context lost; owned lease released, world retained.")); return; }
        if (IsValid(PC)) PC->UnPossess();
        World->DestroyWorld(false); Engine->DestroyWorldContext(World); World = nullptr; Instance.Reset();
    }
    bool EconomyUnchanged() const
    {
        const Jadebound::Snapshot Expected;
        Jadebound::Snapshot Decoded; FJadeboundProgressionState Reflected;
        const auto Actual = Model->GetProgressionState(); const auto* Saved = Access::Saved(*Model);
        if (bForeign || Opens != BaselineOpens || !Access::Clean(*Model) || !Saved || Saved != OriginalSaved || Saved->CharacterId != Id || !Saved->PendingDrops.IsEmpty() ||
            !SameCore(Access::Core(*Model), Expected) || !Jadebound::Persistence::DecodeStateV3(Saved->State, Decoded) || !SameCore(Decoded, Expected) ||
            !Jadebound::Persistence::EncodeStateV3(Expected, Reflected) || !FJadeboundProgressionState::StaticStruct()->CompareScriptStruct(&Actual, &Reflected, 0)) return false;
        for (int32 I = 0; I < JournalPathCount; ++I) { Bank Current; if (!ReadBank(Paths[I], Current) || Current.bExists != Before[I].bExists || Current.Bytes != Before[I].Bytes) return false; }
        return true;
    }
    bool Live() const
    {
        return IsInGameThread() && GEngine && IsValid(World) && !World->bIsTearingDown && GEngine->GetWorldContextFromWorld(World) &&
            IsValid(Model) && !Model->IsActorBeingDestroyed() && IsValid(Hero) && !Hero->IsActorBeingDestroyed() &&
            IsValid(Enemy) && !Enemy->IsActorBeingDestroyed() && IsValid(PC) && !PC->IsActorBeingDestroyed() &&
            PC->GetPawn() == Hero && Hero->GetController() == PC && Hero->GetWorld() == World && Model->GetWorld() == World &&
            IsValid(Hero->GetMesh()) && IsValid(Hero->GetMesh()->GetAnimInstance());
    }
    bool Advance()
    {
        if (!Live()) return false;
        auto* Mesh = Hero->GetMesh(); auto* Anim = Mesh->GetAnimInstance();
        const double BeforeTime = World->GetTimeSeconds();
        World->Tick(LEVELTICK_TimeOnly, Step); // Real clock; excludes engine actor ticks and TimerManager.
        if (!Live() || Hero->GetMesh() != Mesh || Mesh->GetAnimInstance() != Anim) return false;
        const double Delta = World->GetTimeSeconds() - BeforeTime;
        if (!FMath::IsFinite(Delta) || Delta <= 0 || Delta > Step + .001 || !World->AreActorsInitialized() || World->HasBegunPlay()) return false;
        Mesh->TickAnimation(static_cast<float>(Delta), false);
        if (!Live() || Hero->GetMesh() != Mesh || Mesh->GetAnimInstance() != Anim) return false;
        Mesh->RefreshBoneTransforms(); // Complete real graph update before double-buffered slot-weight reads.
        if (!Live() || Hero->GetMesh() != Mesh || Mesh->GetAnimInstance() != Anim) return false;
        if (auto* Presentation = Access::Presentation(*Model)) Presentation->TickComponent(static_cast<float>(Delta), LEVELTICK_All, nullptr);
        if (!Live() || Hero->GetMesh() != Mesh || Mesh->GetAnimInstance() != Anim) return false;
        Access::Poll(*Model); // Actual production clock polling; no full actor Tick or internal assignments.
        return Live() && Hero->GetMesh() == Mesh && Mesh->GetAnimInstance() == Anim && EconomyUnchanged();
    }
};

class FAttackCommand final : public IAutomationLatentCommand
{
    TSharedPtr<FScope> Scope;
    int32 Frames = 0;
    uint64 LastFrame = MAX_uint64;
    enum class EPhase { Windup, Cooldown, Cancelled } Phase = EPhase::Windup;
    Access::Swing First{};
    double SecondHit = 0;
    float LastPosition = 0;
    bool bSlotContributed = false;
    bool Fail(const TCHAR* Message) { Scope->Test.AddError(Message); Scope.Reset(); return true; }
public:
    explicit FAttackCommand(TSharedPtr<FScope> InScope) : Scope(MoveTemp(InScope))
    {
        Scope->bValid = false;
        const double Now = Scope->World->GetTimeSeconds();
        if (!Access::Request(*Scope->Model)) { Scope->Test.AddError(TEXT("First real attack request failed actual presentation guards.")); return; }
        if (!Scope->Live()) { Scope->Test.AddError(TEXT("Real request callback ended a required fixture object.")); return; }
        First = Access::ReadSwing(*Scope->Model);
        auto* Presentation = Access::Presentation(*Scope->Model);
        auto* Anim = Scope->Hero->GetMesh()->GetAnimInstance();
        Scope->bValid = First.Id == 1 && First.Life == 1 && !First.bAttempted && First.Pawn.Get() == Scope->Hero && First.Target.Get() == Scope->Enemy && First.Weapon.IsValid() &&
            FMath::IsNearlyEqual(First.Hit, Now + Jadebound::Combat::WindupSeconds, .000001) &&
            FMath::IsNearlyEqual(First.End, Now + Jadebound::Combat::PresentationSeconds, .000001) &&
            FMath::IsNearlyEqual(First.Cooldown, Now + Jadebound::Combat::CooldownSeconds, .000001) &&
            Presentation && Presentation->GetPresentation().SwingId == 1 && Presentation->GetPresentation().bPlaying &&
            Presentation->GetPresentation().Montage == Scope->Montage && Anim->Montage_IsActive(Scope->Montage) && Scope->EconomyUnchanged() && Access::NoHit(*Scope->Enemy);
        if (!Scope->bValid) { Scope->Test.AddError(TEXT("Real request must establish exact known swing/times/actual montage, without damage or IO.")); return; }
        if (Access::Request(*Scope->Model) || !Scope->Live() || !Access::SameSwing(First, Access::ReadSwing(*Scope->Model)) || !Scope->EconomyUnchanged() || !Access::NoHit(*Scope->Enemy))
        { Scope->bValid = false; Scope->Test.AddError(TEXT("Immediate cooldown refusal must preserve admitted swing/enemy/economy/banks.")); }
    }
    bool Update() override
    {
        if (!Scope->bValid) { Scope.Reset(); return true; }
        if (LastFrame == GFrameCounter) return false; // No global frame-counter assignment.
        LastFrame = GFrameCounter;
        if (++Frames > 80) return Fail(TEXT("Bounded headless attack evaluation exceeded 80 host frames."));
        if (!Scope->Advance()) return Fail(TEXT("Real clock/animation/poll progression or complete IO preservation failed."));
        const double Now = Scope->World->GetTimeSeconds();
        auto* Anim = Scope->Hero->GetMesh()->GetAnimInstance();
        if (Phase == EPhase::Windup)
        {
            const float Position = Anim->Montage_GetPosition(Scope->Montage);
            const float NodeWeight = Anim->GetSlotNodeGlobalWeight(TEXT("DefaultSlot"));
            const float MontageWeight = Anim->GetSlotMontageGlobalWeight(TEXT("DefaultSlot"));
            if (!Anim->Montage_IsActive(Scope->Montage) || !FMath::IsFinite(Position) || Position <= LastPosition ||
                !FMath::IsFinite(NodeWeight) || !FMath::IsFinite(MontageWeight)) return Fail(TEXT("Actual montage playback/slot measurements failed."));
            LastPosition = Position;
            if (NodeWeight > 0.f && MontageWeight > 0.f) bSlotContributed = true;
            if (Now < First.Hit)
            { if (!Access::NoHit(*Scope->Enemy) || Access::ReadSwing(*Scope->Model).bAttempted) return Fail(TEXT("Hit or attempt occurred before actual windup deadline.")); return false; }
            if (Now > First.Hit + Jadebound::Combat::HitWindowSeconds || !bSlotContributed || !Access::HitExactlyOnce(*Scope->Enemy, *Scope->Model) || !Access::ReadSwing(*Scope->Model).bAttempted)
                return Fail(TEXT("Interior actual DefaultSlot contribution and exactly one native hit required."));
            Access::Poll(*Scope->Model);
            if (!Scope->Live()) return Fail(TEXT("Repeated combat callback ended a required fixture object."));
            Access::Poll(*Scope->Model);
            if (!Scope->Live() || !Access::HitExactlyOnce(*Scope->Enemy, *Scope->Model) || !Scope->EconomyUnchanged()) return Fail(TEXT("Repeated production polling changed hit count or progression."));
            Phase = EPhase::Cooldown;
            return false;
        }
        if (Phase == EPhase::Cooldown)
        {
            if (!Access::HitExactlyOnce(*Scope->Enemy, *Scope->Model)) return Fail(TEXT("Additional same-swing damage occurred during cooldown."));
            if (Now < First.Cooldown)
            { const auto Before = Access::ReadSwing(*Scope->Model); if (Access::Request(*Scope->Model) || !Scope->Live() || !Access::SameSwing(Before, Access::ReadSwing(*Scope->Model)) || !Scope->EconomyUnchanged()) return Fail(TEXT("Real cooldown refusal changed admitted transaction.")); return false; }
            if (Anim->GetCurrentActiveMontage() || !Access::Request(*Scope->Model)) return Fail(TEXT("A second genuine attack must start after actual cooldown and presentation expiry."));
            if (!Scope->Live() || Scope->Hero->GetMesh()->GetAnimInstance() != Anim) return Fail(TEXT("Second real request replaced or ended a required animation instance."));
            const auto Second = Access::ReadSwing(*Scope->Model);
            if (Second.Id != 2 || Second.Life != 1 || Second.bAttempted || Second.Hit <= Now || !Anim->Montage_IsActive(Scope->Montage)) return Fail(TEXT("Second positive request did not create its own real windup/presentation."));
            SecondHit = Second.Hit;
            Access::Menu(*Scope->Model, true); // Actual server menu route invokes production cancellation.
            if (!Scope->Live() || !Access::MenuCancelled(*Scope->Model) || Access::Request(*Scope->Model) || !Scope->Live() || !Access::HitExactlyOnce(*Scope->Enemy, *Scope->Model) || !Scope->EconomyUnchanged()) return Fail(TEXT("Real menu cancellation/refusal failed before second windup."));
            Phase = EPhase::Cancelled;
            return false;
        }
        if (!Access::MenuCancelled(*Scope->Model) || !Access::HitExactlyOnce(*Scope->Enemy, *Scope->Model)) return Fail(TEXT("Cancelled real second swing applied damage or reactivated."));
        if (Now < SecondHit + Jadebound::Combat::HitWindowSeconds) return false;
        Scope->Test.AddInfo(TEXT("Actual positive request, montage position/DefaultSlot contribution, one native timed hit, cooldown and real-menu cancellation verified by this fixture. Explicit headless clock/animation/polling; no engine scheduler, input, rendered pose, clients or MMO proof."));
        Scope.Reset(); return true;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundRecipientAttackWindowTest,
    "Jadebound.RecipientCombat.NativeAttackWindow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FJadeboundRecipientAttackWindowTest::RunTest(const FString& Parameters)
{
    auto Scope = MakeShared<JadeboundAttackWindow::FScope>(*this);
    if (!Scope->bValid) return false;
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<JadeboundAttackWindow::FAttackCommand>(Scope));
    return true;
}
#endif
