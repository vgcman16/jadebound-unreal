#include "JadeboundProgressionActor.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "JadeboundDevelopmentGameMode.h"
#include "JadeboundNativeRaider.h"
#include "JadeboundPersonalDrop.h"
#include "Combat/RecipientAnimationRules.h"
#include "Combat/PlayerLifeRules.h"
#include "Development/DevelopmentIdentity.h"
#include "Domain/ProgressionCore.h"
#include "Persistence/ProgressionSaveV3.h"
#include "Persistence/JournalNamespacePaths.h"
#include "Animation/AnimMontage.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
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
#include "UObject/UnrealType.h"

// Only this CPP defines this automation-only, unreflected friend. Registry and
// readiness assembly below are trusted fixture setup, not login admission.
struct FJadeboundRecipientNativeTestAccess
{
    static Jadebound::Combat::PlayerLife::State LifeImage(const AJadeboundProgressionActor& Model) { return *Model.NativeLifeState; }
    static void SetLife(AJadeboundProgressionActor& Model, const Jadebound::Combat::PlayerLife::State& Value)
    { *Model.NativeLifeState = Value; Model.PublishNativePlayerLife(); }
    static bool Initialize(AJadeboundProgressionActor& Model, APlayerController& Controller,
        ACharacter& Pawn, const FString& Id, UStaticMesh* Glaive, UStaticMesh* Ore,
        UStaticMesh* Parcel, UAnimMontage* Montage)
    {
        if (!Model.ConfigureDevelopmentOwner(&Controller, Id, Glaive)) return false;
        if (Controller.GetPawn() != &Pawn || Pawn.GetController() != &Controller) return false;
        Model.Hero = &Pawn; // ConfigureDevelopmentOwner deliberately clears Hero.
        Model.IronOreMesh = Ore;
        Model.JadeParcelMesh = Parcel;
        Model.bReady = Model.ReadSave(true);
        Model.bServerModelInitialized = Model.bReady; // Same rule as production BeginPlay.
        if (!Model.bReady) return false;
        Model.RefreshState();
        Model.ConfigureRecipientCombat(true, Montage);
        return Model.bRecipientCombatEnabled && Model.IsDevelopmentReady() && Model.IsProgressionReady();
    }
    static bool Register(AJadeboundDevelopmentGameMode& Mode, AJadeboundNativeRaider& Enemy,
        AJadeboundProgressionActor& Model, APlayerController& Controller)
    {
        if (const auto Existing = Mode.Models.FindRef(Model.ServerCharacterId))
            if (Existing.Get() != &Model) return false;
        if (Mode.ActiveControllers.Contains(&Controller)) return false;
        Mode.bEnableRecipientCombat = true; // Instance only; class/project defaults stay false.
        Mode.RecipientRaider = &Enemy;
        Mode.Models.Add(Model.ServerCharacterId, &Model);
        Mode.ActiveControllers.Add(&Controller, Model.ServerCharacterId);
        return Mode.GetRecipientRaider() == &Enemy && Mode.OwnsRecipientModel(&Model, true);
    }
    static bool Rebind(AJadeboundDevelopmentGameMode& Mode, AJadeboundNativeRaider& Enemy,
        AJadeboundProgressionActor& Model, APlayerController& Controller, ACharacter& Pawn, UStaticMesh* Glaive)
    {
        if (!Model.ConfigureDevelopmentOwner(&Controller, Model.ServerCharacterId, Glaive) ||
            Controller.GetPawn() != &Pawn || Pawn.GetController() != &Controller) return false;
        Model.Hero = &Pawn;
        if (!Register(Mode, Enemy, Model, Controller)) return false;
        Model.RefreshDropVisuals(); // Real connected presentation path; no bypass.
        return Model.IsDevelopmentReady() && Model.IsProgressionReady();
    }
    static bool Living(const AJadeboundDevelopmentGameMode& Mode, const AJadeboundProgressionActor& Model)
    { return Mode.OwnsRecipientModel(&Model, true); }
    static bool Retained(const AJadeboundDevelopmentGameMode& Mode, const AJadeboundProgressionActor& Model)
    { return Mode.OwnsRecipientModel(&Model, false); }
    static bool Detached(const AJadeboundDevelopmentGameMode& Mode, const AJadeboundProgressionActor& Model,
        const APlayerController& Controller)
    {
        return !Mode.ActiveControllers.Contains(const_cast<APlayerController*>(&Controller)) &&
            Mode.Models.FindRef(Model.ServerCharacterId).Get() == &Model && Model.DevelopmentWriterLease &&
            !Model.Player && !Model.Hero && !Model.GetOwner();
    }
    static bool Hit(AJadeboundNativeRaider& Enemy, AJadeboundProgressionActor& Model,
        APawn& Pawn, int64 Swing, int64 Life)
    { return Enemy.ApplyValidatedHit(&Model, &Pawn, Swing, Life); }
    static bool Request(AJadeboundProgressionActor& Model) { return Model.RequestNativeAttack(); }
    static bool Observe(AJadeboundDevelopmentGameMode& Mode, AJadeboundNativeRaider& Enemy)
    { return Mode.TryCommitRaiderDeath(&Enemy); } // Enemy only; no recipient/item/ID argument.
    static AJadeboundProgressionActor* Recipient(const AJadeboundNativeRaider& Enemy)
    { return Enemy.LastValidRecipient.Get(); }
    static int64 LastSwing(const AJadeboundNativeRaider& Enemy, const AJadeboundProgressionActor& Model)
    { return Enemy.LastHitSwing.FindRef(TWeakObjectPtr<AJadeboundProgressionActor>(const_cast<AJadeboundProgressionActor*>(&Model))); }
    static bool Awarded(const AJadeboundNativeRaider& Enemy) { return Enemy.bDeathAwardCommitted; }
    static bool AwardLatch(const AJadeboundProgressionActor& Model, const AJadeboundNativeRaider& Enemy)
    { return Model.AwardedEnemy.Get() == &Enemy && Model.AwardedLife == 1 && Model.AwardedDrop == 1 && Model.AwardedRevision == 1; }
    static Jadebound::Snapshot Core(const AJadeboundProgressionActor& Model) { return Model.Core->GetSnapshot(); }
    static const TArray<FJadeboundSavedDrop>& Drops(const AJadeboundProgressionActor& Model) { return Model.PendingDrops; }
    static const UJadeboundCharacterSave* Committed(const AJadeboundProgressionActor& Model) { return Model.LastCommittedSave.Get(); }
    static const UJadeboundCharacterSave* Held(const AJadeboundProgressionActor& Model) { return Model.HeldSaveCandidate.Get(); }
    static int32 Slot(const AJadeboundProgressionActor& Model) { return Model.LastSaveSlot; }
    static bool Clean(const AJadeboundProgressionActor& Model)
    { return !Model.bPersistenceBlocked && !Model.bUnsavedCandidate && !Model.HeldSaveCandidate &&
        Model.bV3JournalAdmitted && Model.HeldJournalBytes.IsEmpty() && Model.HeldJournalPath.IsEmpty() && Model.HeldJournalSlot == INDEX_NONE; }
    static bool OneOwnedVisual(const AJadeboundProgressionActor& Model)
    {
        return Model.DropVisuals.Num() == 1 && IsValid(Model.DropVisuals[0]) &&
            Model.DropVisuals[0]->IsA<AJadeboundPersonalDrop>() && Model.DropVisuals[0]->GetOwner() == &Model;
    }
    static void SetOpener(AJadeboundProgressionActor& Model, TFunction<IFileHandle*(const FString&)> Opener)
    { Model.AutomationJournalOpener = MoveTemp(Opener); }
    static void RemoveFixtureEntry(AJadeboundDevelopmentGameMode& Mode, const AJadeboundProgressionActor& Model,
        APlayerController& Controller)
    { Mode.ActiveControllers.Remove(&Controller); Mode.Models.Remove(Model.ServerCharacterId); }
    static void End(AJadeboundProgressionActor& Model, bool bDestroy)
    {
        Model.bPersistenceEndingPlay = true;
        Model.AutomationJournalOpener = nullptr;
        Model.DevelopmentWriterLease.Reset();
        if (bDestroy) Model.Destroy();
    }
    struct EnemyImage
    {
        FJadeboundRaiderLife Life;
        TMap<TWeakObjectPtr<AJadeboundProgressionActor>, int64> Hits;
        TWeakObjectPtr<AJadeboundProgressionActor> LastRecipient;
        bool bAwarded = false;
        double NextAwardCheck = 0;
    };
    static EnemyImage Capture(const AJadeboundNativeRaider& Enemy)
    { return {Enemy.Life, Enemy.LastHitSwing, Enemy.LastValidRecipient, Enemy.bDeathAwardCommitted, Enemy.NextAwardCheck}; }
    static bool Unchanged(const AJadeboundNativeRaider& Enemy, const EnemyImage& Before)
    {
        if (!FJadeboundRaiderLife::StaticStruct()->CompareScriptStruct(&Enemy.Life, &Before.Life, 0) ||
            Enemy.LastHitSwing.Num() != Before.Hits.Num() || Enemy.LastValidRecipient != Before.LastRecipient ||
            Enemy.bDeathAwardCommitted != Before.bAwarded || Enemy.NextAwardCheck != Before.NextAwardCheck) return false;
        for (const auto& Entry : Before.Hits)
        {
            const int64* Value = Enemy.LastHitSwing.Find(Entry.Key);
            if (!Value || *Value != Entry.Value) return false;
        }
        return true;
    }
};

namespace JadeboundRecipientNative
{
// 0/1 V1, 2/3 current V3, 4/5 retained V2, 6/7 forward V4.
inline constexpr int32 JournalPathCount = 8;
static_assert(Jadebound::SnapshotVersion == 3, "Current recipient fixture requires the V3 domain");
using Access = FJadeboundRecipientNativeTestAccess;
const FVector EnemyPoint(-26.777, 31.802, 92.15);

bool SameCore(const Jadebound::Snapshot& A, const Jadebound::Snapshot& B)
{
    if (A.Version != B.Version || A.Revision != B.Revision || A.Coins != B.Coins ||
        A.EquippedWeapon != B.EquippedWeapon || A.EquippedTorso != B.EquippedTorso || A.Quest != B.Quest || A.NextDropId != B.NextDropId ||
        A.QuestKillDropId != B.QuestKillDropId || A.UnprocessedDropIds != B.UnprocessedDropIds) return false;
    for (uint32 Index = 0; Index < Jadebound::InventorySlotCount; ++Index)
        if (A.Inventory[Index].Id != B.Inventory[Index].Id || A.Inventory[Index].Count != B.Inventory[Index].Count) return false;
    return true;
}
bool SameDrops(const TArray<FJadeboundSavedDrop>& A, const TArray<FJadeboundSavedDrop>& B)
{
    if (A.Num() != B.Num()) return false;
    for (int32 Index = 0; Index < A.Num(); ++Index)
        if (!FJadeboundSavedDrop::StaticStruct()->CompareScriptStruct(&A[Index], &B[Index], 0)) return false;
    return true;
}
bool Known(const AJadeboundProgressionActor& Model, const Jadebound::Snapshot& Expected,
    const TArray<FJadeboundSavedDrop>& ExpectedDrops)
{
    FJadeboundProgressionState Reflected;
    const FJadeboundProgressionState ActualReflected = Model.GetProgressionState();
    Jadebound::Snapshot Disk;
    const UJadeboundCharacterSave* Saved = Access::Committed(Model);
    return Jadebound::Persistence::EncodeStateV3(Expected, Reflected) &&
        SameCore(Access::Core(Model), Expected) && SameDrops(Access::Drops(Model), ExpectedDrops) &&
        FJadeboundProgressionState::StaticStruct()->CompareScriptStruct(&ActualReflected, &Reflected, 0) &&
        Saved && Saved->CharacterId == Model.GetServerCharacterId() &&
        Jadebound::Persistence::DecodeStateV3(Saved->State, Disk) && SameCore(Disk, Expected) &&
        SameDrops(Saved->PendingDrops, ExpectedDrops) && Access::Clean(Model);
}
struct BankImage { bool bExists = false; TArray<uint8> Bytes; };
struct OpenTrace { int32 Count = 0; bool bForeign = false; TArray<FString> Paths; };

bool ReadBank(const FString& Path, BankImage& Out)
{
    IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
    Out.bExists = Jadebound::Persistence::HasJournalEntry(Files, Path); Out.Bytes.Reset();
    if (!Out.bExists) return true;
    if (!Files.FileExists(*Path) || Files.DirectoryExists(*Path)) return false;
    const int64 Bytes = Files.FileSize(*Path);
    if (Bytes <= 0 || Bytes > 1024 * 1024 + 32) return false;
    TUniquePtr<IFileHandle> Handle(Files.OpenRead(*Path));
    if (!Handle) return false;
    Out.Bytes.SetNumUninitialized(static_cast<int32>(Bytes));
    return Handle->Read(Out.Bytes.GetData(), Bytes);
}
bool EqualBank(const BankImage& A, const BankImage& B)
{ return A.bExists == B.bExists && A.Bytes == B.Bytes; }

struct ModelImage
{
    Jadebound::Snapshot State;
    FJadeboundProgressionState Reflected;
    TArray<FJadeboundSavedDrop> Drops;
    const UJadeboundCharacterSave* Committed = nullptr;
    const UJadeboundCharacterSave* Held = nullptr;
    int32 Slot = 0, Opens = 0;
    BankImage Banks[JournalPathCount];
};

bool IsOwnedUserDir(FString& Out)
{
    FString Argument;
    if (!FParse::Param(FCommandLine::Get(), TEXT("JadeboundRecipientNativeTests")) ||
        !FParse::Value(FCommandLine::Get(), TEXT("UserDir="), Argument) ||
        Argument.IsEmpty() || FPaths::IsRelative(Argument)) return false;
    Argument.ReplaceInline(TEXT("\\"), TEXT("/"));
    TArray<FString> Parts;
    Argument.ParseIntoArray(Parts, TEXT("/"), true);
    for (const auto& Part : Parts) if (Part == TEXT(".") || Part == TEXT("..")) return false;
    FPaths::NormalizeDirectoryName(Argument);
    FString Effective = FPaths::ProjectUserDir();
    FPaths::NormalizeDirectoryName(Effective);
    if (!FPaths::IsSamePath(Argument, Effective) || FPaths::GetCleanFilename(Argument) != TEXT("UserDir-v001") ||
        FPaths::GetCleanFilename(FPaths::GetPath(Argument)) != TEXT("Jadebound-P102-A006")) return false;
    Out = Argument;
    return FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()), Out);
}

struct FScope
{
    FAutomationTestBase& Test;
    UWorld* World = nullptr;
    TStrongObjectPtr<UGameInstance> Instance;
    AJadeboundDevelopmentGameMode* Mode = nullptr;
    AJadeboundNativeRaider* Enemy = nullptr;
    AJadeboundProgressionActor* Models[2] = {};
    APlayerController* Controllers[2] = {};
    ACharacter* Heroes[2] = {};
    UStaticMesh* Glaive = nullptr;
    UStaticMesh* Ore = nullptr;
    UStaticMesh* Parcel = nullptr;
    UAnimMontage* Montage = nullptr;
    FString Ids[2], Paths[2][JournalPathCount]; // Each identity: V1, current V3, retained V2, forward V4.
    OpenTrace Traces[2];
    TArray<AJadeboundProgressionActor*> AllModels;
    bool bValid = false;

    FScope(FAutomationTestBase& InTest) : Test(InTest)
    {
        FString User;
        if (!IsInGameThread() || !GEngine || !GEngine->WorldSettingsClass || !IsOwnedUserDir(User))
        { Test.AddError(TEXT("Initialized engine/game thread and explicit fresh A006 UserDir/opt-in required.")); return; }
        UClass* HeroClass = LoadClass<ACharacter>(nullptr,
            TEXT("/Game/Jadebound/Tests/GlaiveCombatV006/BP_GlaiveHero_V006.BP_GlaiveHero_V006_C"));
        Glaive = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Jadebound/OriginalWeapons/JadeGlaiveV001/SM_JadeGlaive_CM_V001.SM_JadeGlaive_CM_V001"));
        Ore = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Jadebound/OriginalLoot/ProgressionPBRV003/SM_IronOre_CM_PBRV003.SM_IronOre_CM_PBRV003"));
        Parcel = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Jadebound/OriginalLoot/ProgressionPBRV003/SM_CourierParcel_CM_PBRV003.SM_CourierParcel_CM_PBRV003"));
        Montage = LoadObject<UAnimMontage>(nullptr, TEXT("/Game/Jadebound/Development/RecipientAnimationV001/AM_RecipientAttack_Template_v001.AM_RecipientAttack_Template_v001"));
        if (!HeroClass || !Glaive || !Ore || !Parcel || !Jadebound::Combat::IsSafeMeleeMontage(Montage))
        { Test.AddError(TEXT("Preserved Hero, actual original meshes and admitted saved montage must load.")); return; }
        const FNumericProperty* Health = FindFProperty<FNumericProperty>(HeroClass, TEXT("Current HP"));
        if (!Health || !Health->IsFloatingPoint())
        { Test.AddError(TEXT("Actual Hero requires its existing floating Current HP field; no fake health class.")); return; }
        for (int32 Index = 0; Index < 2; ++Index)
        {
            Ids[Index] = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
            if (!Jadebound::Development::IsCharacterId(Ids[Index]) || (Index == 1 && Ids[1] == Ids[0]))
            { Test.AddError(TEXT("Distinct canonical fresh GUID identities required.")); return; }
            Jadebound::Persistence::JournalNamespacePaths Namespaces;
            if (!Jadebound::Persistence::BuildJournalNamespacePaths(true, Ids[Index], Namespaces))
            { Test.AddError(TEXT("Current and retained journal namespace paths must be valid.")); return; }
            for (int32 Slot = 0; Slot < 2; ++Slot)
            {
                Paths[Index][Slot] = Namespaces.Banks[0][Slot]; Paths[Index][2 + Slot] = Namespaces.Banks[2][Slot];
                Paths[Index][4 + Slot] = Namespaces.Banks[1][Slot]; Paths[Index][6 + Slot] = Namespaces.ForwardV4[Slot];
            }
            for (int32 Bank = 0; Bank < JournalPathCount; ++Bank)
                if (!FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Paths[Index][Bank]), User))
                { Test.AddError(TEXT("Production journal path escaped owned UserDir.")); return; }
            if (FPlatformFileManager::Get().GetPlatformFile().DirectoryExists(*FPaths::GetPath(Paths[Index][2])))
            { Test.AddError(TEXT("GUID directory already exists; no reuse/reset.")); return; }
        }
        UWorld::InitializationValues Values;
        Values.InitializeScenes(true).AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(true)
            .EnableTraceCollision(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false)
            .CreateFXSystem(false).SetTransactional(false);
        World = UWorld::CreateWorld(EWorldType::Game, false, FName(*FString::Printf(TEXT("RecipientNative_%s"), *Ids[0])),
            nullptr, true, ERHIFeatureLevel::Num, &Values);
        if (!World) { Test.AddError(TEXT("Fresh authority world creation failed.")); return; }
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        Instance.Reset(NewObject<UGameInstance>(GEngine));
        World->SetGameInstance(Instance.Get());
        if (!Instance || !World->GetWorldSettings() || !World->GetPhysicsScene() || World->HasBegunPlay() ||
            World->GetNetMode() != NM_Standalone || !GEngine->GetWorldContextFromWorld(World))
        { Test.AddError(TEXT("Real isolated context/game-instance/physics prerequisites unavailable.")); return; }
        // Explicit native default avoids GameInstance's map-prefix WorldContext path.
        World->GetWorldSettings()->DefaultGameMode = AJadeboundDevelopmentGameMode::StaticClass();
        if (!World->SetGameMode(FURL())) { Test.AddError(TEXT("Actual SetGameMode authority attachment failed.")); return; }
        Mode = Cast<AJadeboundDevelopmentGameMode>(World->GetAuthGameMode());
        if (!Mode || Mode->HasActorBegunPlay() || !Mode->HasAuthority())
        { Test.AddError(TEXT("Exact nonbegun native authority mode required.")); return; }
        FActorSpawnParameters Spawn;
        Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Enemy = World->SpawnActor<AJadeboundNativeRaider>(EnemyPoint, FRotator::ZeroRotator, Spawn);
        if (!Enemy || Enemy->HasActorBegunPlay() || !Enemy->GetActorLocation().Equals(EnemyPoint, 0.0))
        { Test.AddError(TEXT("Exact native nonbegun enemy at the known arena point required.")); return; }
        for (int32 Index = 0; Index < 2; ++Index)
        {
            Controllers[Index] = World->SpawnActor<APlayerController>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
            Heroes[Index] = World->SpawnActor<ACharacter>(HeroClass, EnemyPoint + FVector(100 + 200 * Index, 0, 0), FRotator::ZeroRotator, Spawn);
            if (!Controllers[Index] || !Heroes[Index] || Heroes[Index]->HasActorBegunPlay())
            { Test.AddError(TEXT("Actual nonbegun Hero/controller creation failed.")); return; }
            Controllers[Index]->Possess(Heroes[Index]);
            const double HP = Health->GetFloatingPointPropertyValue(Health->ContainerPtrToValuePtr<void>(Heroes[Index]));
            if (Controllers[Index]->GetPawn() != Heroes[Index] || Heroes[Index]->GetController() != Controllers[Index] ||
                !Heroes[Index]->HasAuthority() || !Controllers[Index]->HasAuthority() || !FMath::IsFinite(HP) || HP <= 0.0)
            { Test.AddError(TEXT("Real reciprocal authority possession and finite living Hero required.")); return; }
            Models[Index] = NewModel(Index);
            if (!Models[Index] || !Access::Register(*Mode, *Enemy, *Models[Index], *Controllers[Index]))
            { Test.AddError(TEXT("Trusted fixture registry/readiness setup failed its actual production guards.")); return; }
            Test.AddInfo(FString::Printf(TEXT("Native recipient fixture files retained: %s"), *FPaths::GetPath(Paths[Index][2])));
        }
        bValid = true;
    }
    ~FScope()
    {
        if (!World) return;
        UEngine* Engine = GEngine;
        const bool bCanDestroy = Engine && Engine->GetWorldContextFromWorld(World);
        for (auto* Model : AllModels)
            if (IsValid(Model) && !Model->IsActorBeingDestroyed()) Access::End(*Model, false);
        if (!bCanDestroy)
        { Test.AddError(TEXT("Engine/context lost: leases released; world retained, cleanup unqualified.")); return; }
        for (auto* Controller : Controllers) if (IsValid(Controller)) Controller->UnPossess();
        World->DestroyWorld(false);
        Engine->DestroyWorldContext(World);
        World = nullptr;
        Instance.Reset();
    }
    AJadeboundProgressionActor* NewModel(int32 Index)
    {
        FActorSpawnParameters Spawn;
        Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Model = World->SpawnActor<AJadeboundProgressionActor>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
        if (!Model || !Model->HasAuthority() || Model->HasActorBegunPlay()) return nullptr;
        AllModels.Add(Model);
        Access::SetOpener(*Model, [this, Index](const FString& Path) -> IFileHandle*
        {
            auto& Trace = Traces[Index];
            if (Path != Paths[Index][2] && Path != Paths[Index][3]) { Trace.bForeign = true; return nullptr; }
            ++Trace.Count;
            Trace.Paths.Add(Path);
            return FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*Path, false, false);
        }); // Instrument opens only; real Write/Flush/close/readback remain unchanged.
        if (!Access::Initialize(*Model, *Controllers[Index], *Heroes[Index], Ids[Index], Glaive, Ore, Parcel, Montage)) return nullptr;
        return Model;
    }
    bool Capture(int32 Index, ModelImage& Out) const
    {
        const auto& Model = *Models[Index];
        Out.State = Access::Core(Model); Out.Reflected = Model.GetProgressionState(); Out.Drops = Access::Drops(Model);
        Out.Committed = Access::Committed(Model); Out.Held = Access::Held(Model); Out.Slot = Access::Slot(Model); Out.Opens = Traces[Index].Count;
        for (int32 Bank = 0; Bank < JournalPathCount; ++Bank) if (!ReadBank(Paths[Index][Bank], Out.Banks[Bank])) return false;
        return !Traces[Index].bForeign;
    }
    bool Unchanged(int32 Index, const ModelImage& Before) const
    {
        ModelImage After;
        if (!Capture(Index, After) || !SameCore(Before.State, After.State) || !SameDrops(Before.Drops, After.Drops) ||
            !FJadeboundProgressionState::StaticStruct()->CompareScriptStruct(&Before.Reflected, &After.Reflected, 0) ||
            Before.Committed != After.Committed || Before.Held != After.Held || Before.Slot != After.Slot || Before.Opens != After.Opens) return false;
        for (int32 Bank = 0; Bank < JournalPathCount; ++Bank) if (!EqualBank(Before.Banks[Bank], After.Banks[Bank])) return false;
        return true;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundRecipientNativeHitTest,
    "Jadebound.RecipientCombat.NativeHitAttribution", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJadeboundRecipientNativeHitTest::RunTest(const FString& Parameters)
{
    using namespace JadeboundRecipientNative;
    FScope Scope(*this);
    if (!Scope.bValid) return false;
    auto& A = *Scope.Models[0]; auto& B = *Scope.Models[1]; auto& Enemy = *Scope.Enemy; auto& Mode = *Scope.Mode;
    Jadebound::Snapshot Initial; // Independent complete known r0 oracle, not copied from actual state.
    const TArray<FJadeboundSavedDrop> Empty;
    if (!TestTrue(TEXT("Both actual journals/readiness start at the complete known r0"),
        Known(A, Initial, Empty) && Known(B, Initial, Empty) && Access::Living(Mode, A) && Access::Living(Mode, B))) return false;
    ModelImage BeforeA, BeforeB;
    auto NoMutation = [&](const TCHAR* Label, const TFunction<bool()>& Operation) -> bool
    {
        if (!Scope.Capture(0, BeforeA) || !Scope.Capture(1, BeforeB)) { AddError(TEXT("Owned baseline read failed.")); return false; }
        const auto BeforeEnemy = Access::Capture(Enemy);
        const bool Result = Operation();
        return TestTrue(Label, !Result && Access::Unchanged(Enemy, BeforeEnemy) && Scope.Unchanged(0, BeforeA) && Scope.Unchanged(1, BeforeB));
    };
    if (!NoMutation(TEXT("Wrong controller/pawn cannot strike for A"), [&]() { return Access::Hit(Enemy, A, *Scope.Heroes[1], 1, 1); })) return false;
    A.SetOwner(Scope.Controllers[1]);
    const bool WrongOwner = NoMutation(TEXT("Wrong owner refuses actual native hit"), [&]() { return Access::Hit(Enemy, A, *Scope.Heroes[0], 1, 1); }) &&
        NoMutation(TEXT("Wrong owner refuses actual early attack admission"), [&]() { return Access::Request(A); });
    A.SetOwner(Scope.Controllers[0]);
    if (!WrongOwner || !TestTrue(TEXT("Restored actual living owner is admitted"), Access::Living(Mode, A))) return false;
    const auto SavedLife = Access::LifeImage(A);
    auto DeadLife = SavedLife;
    for (uint64 Strike = 1; Strike <= 5; ++Strike)
        if (!Jadebound::Combat::PlayerLife::TryDamage(DeadLife, SavedLife.LastStrike + Strike, SavedLife.Generation, Scope.World->GetTimeSeconds())) return false;
    Access::SetLife(A, DeadLife); // Valid canonical native life; never trust Blueprint HP.
    const bool DeadRefused = NoMutation(TEXT("Zero-health model refuses actual native hit"), [&]() { return Access::Hit(Enemy, A, *Scope.Heroes[0], 1, 1); }) &&
        NoMutation(TEXT("Zero-health model refuses actual early attack admission"), [&]() { return Access::Request(A); });
    Access::SetLife(A, SavedLife);
    if (!DeadRefused || !TestTrue(TEXT("Restored actual health is positively admitted"), Access::Living(Mode, A))) return false;
    if (!NoMutation(TEXT("Stale enemy life cannot mutate any state"), [&]() { return Access::Hit(Enemy, A, *Scope.Heroes[0], 1, 2); })) return false;
    if (!Scope.Capture(0, BeforeA) || !Scope.Capture(1, BeforeB)) return false;
    if (!TestTrue(TEXT("B's real first hit latches B and only changes native enemy"),
        Access::Hit(Enemy, B, *Scope.Heroes[1], 1, 1) && Enemy.GetLife().Health == 2 && Enemy.GetLife().bAlive &&
        Enemy.GetLife().LifeId == 1 && Access::Recipient(Enemy) == &B && Access::LastSwing(Enemy, B) == 1 &&
        Scope.Unchanged(0, BeforeA) && Scope.Unchanged(1, BeforeB))) return false;
    if (!NoMutation(TEXT("Repeated native B swing cannot hit twice"), [&]() { return Access::Hit(Enemy, B, *Scope.Heroes[1], 1, 1); })) return false;
    Mode.Logout(Scope.Controllers[1]); // Real unregister also legitimately saves prior state.
    if (!TestTrue(TEXT("Actual Logout retains B's lease/model but clears active owner"),
        Access::Detached(Mode, B, *Scope.Controllers[1]) && !Access::Living(Mode, B) && Access::Retained(Mode, B) && Known(B, Initial, Empty))) return false;
    if (!NoMutation(TEXT("Unregistered B cannot strike still-living enemy"), [&]() { return Access::Hit(Enemy, B, *Scope.Heroes[1], 2, 1); }) ||
        !NoMutation(TEXT("Unregistered B refuses actual early attack admission"), [&]() { return Access::Request(B); })) return false;
    if (!Scope.Capture(0, BeforeA) || !Scope.Capture(1, BeforeB)) return false; // Post-Logout bytes/pointers are the baseline.
    if (!TestTrue(TEXT("A's real first hit replaces final-recipient candidate"),
        Access::Hit(Enemy, A, *Scope.Heroes[0], 1, 1) && Enemy.GetLife().Health == 1 && Enemy.GetLife().bAlive &&
        Access::Recipient(Enemy) == &A && Access::LastSwing(Enemy, A) == 1 && Scope.Unchanged(0, BeforeA) && Scope.Unchanged(1, BeforeB))) return false;
    if (!TestTrue(TEXT("A's lethal native hit latches A without issuing progression"),
        Access::Hit(Enemy, A, *Scope.Heroes[0], 2, 1) && Enemy.GetLife().Health == 0 && !Enemy.GetLife().bAlive &&
        Enemy.GetLife().LifeId == 1 && FMath::IsFinite(Enemy.GetLife().DeathServerTime) && Access::Recipient(Enemy) == &A &&
        Access::LastSwing(Enemy, A) == 2 && Scope.Unchanged(0, BeforeA) && Scope.Unchanged(1, BeforeB))) return false;
    Mode.Logout(Scope.Controllers[0]);
    if (!TestTrue(TEXT("Final recipient survives real Logout before death observation"),
        Access::Detached(Mode, A, *Scope.Controllers[0]) && !Access::Living(Mode, A) && Access::Retained(Mode, A) &&
        Access::Recipient(Enemy) == &A && Known(A, Initial, Empty))) return false;
    if (!Scope.Capture(0, BeforeA) || !Scope.Capture(1, BeforeB)) return false;
    Jadebound::Snapshot Issued = Initial; Issued.Revision = 1; Issued.NextDropId = 2; Issued.UnprocessedDropIds = {1};
    FJadeboundSavedDrop Drop; Drop.DropId = 1; Drop.ItemId = 3; Drop.Count = 2; Drop.Location = EnemyPoint + FVector(0, 0, 50);
    const TArray<FJadeboundSavedDrop> Pending{Drop};
    Enemy.Tick(0.f); // Actual production Tick -> mode -> final recipient -> issue/FinishTransaction/real IO.
    ModelImage AfterAward;
    if (!Scope.Capture(0, AfterAward)) return false;
    if (!TestTrue(TEXT("Actual death Tick awards only A exactly once with known complete r1 pair"),
        Access::Awarded(Enemy) && Access::AwardLatch(A, Enemy) && Known(A, Issued, Pending) &&
        Scope.Traces[0].Count == BeforeA.Opens + 1 && Scope.Unchanged(1, BeforeB) && Known(B, Initial, Empty) &&
        AfterAward.Slot == 1 - BeforeA.Slot && AfterAward.Committed != BeforeA.Committed &&
        EqualBank(AfterAward.Banks[BeforeA.Slot + 2], BeforeA.Banks[BeforeA.Slot + 2]) &&
        !EqualBank(AfterAward.Banks[AfterAward.Slot + 2], BeforeA.Banks[AfterAward.Slot + 2]))) return false;
    if (!Scope.Capture(0, BeforeA) || !Scope.Capture(1, BeforeB)) return false;
    const auto AwardedEnemy = Access::Capture(Enemy);
    if (!TestTrue(TEXT("Repeated real death observation uses committed witness with no reissue or IO"),
        Access::Observe(Mode, Enemy) && Access::Observe(Mode, Enemy) && Access::Unchanged(Enemy, AwardedEnemy) &&
        Scope.Unchanged(0, BeforeA) && Scope.Unchanged(1, BeforeB))) return false;
    if (!TestTrue(TEXT("Rebind admits the exact actually possessed A and builds actual personal drop"),
        Access::Rebind(Mode, Enemy, A, *Scope.Controllers[0], *Scope.Heroes[0], Scope.Glaive) &&
        Access::Living(Mode, A) && Access::OneOwnedVisual(A) && Known(A, Issued, Pending))) return false;
    if (!Scope.Heroes[0]->SetActorLocation(Drop.Location, false, nullptr, ETeleportType::TeleportPhysics) ||
        !TestTrue(TEXT("Trusted fixture placement is in actual pickup range"),
            Scope.Heroes[0]->GetActorLocation().Equals(Drop.Location, 0.0) &&
            FVector::Dist(Scope.Heroes[0]->GetActorLocation(), Drop.Location) <= A.PickupRange)) return false;
    if (!Scope.Capture(0, BeforeA) || !Scope.Capture(1, BeforeB)) return false;
    Jadebound::Snapshot Picked = Initial; Picked.Revision = 2; Picked.NextDropId = 2;
    Picked.Inventory[0] = {Jadebound::ItemId::IronOre, 2};
    if (!TestTrue(TEXT("Real public Interact consumes the owned pending drop and saves whole r2 oracle"),
        A.Interact() && Known(A, Picked, Empty) && Scope.Traces[0].Count == BeforeA.Opens + 1 &&
        Scope.Unchanged(1, BeforeB) && Access::Unchanged(Enemy, AwardedEnemy))) return false;
    ModelImage AfterPickup;
    if (!Scope.Capture(0, AfterPickup)) return false;
    if (!TestTrue(TEXT("Pickup changes only the next owned bank, preserving the prior r1 bank"),
        AfterPickup.Slot == 1 - BeforeA.Slot && EqualBank(AfterPickup.Banks[BeforeA.Slot + 2], BeforeA.Banks[BeforeA.Slot + 2]) &&
        !EqualBank(AfterPickup.Banks[AfterPickup.Slot + 2], BeforeA.Banks[AfterPickup.Slot + 2]))) return false;
    if (!Scope.Capture(0, BeforeA) || !Scope.Capture(1, BeforeB)) return false;
    if (!TestTrue(TEXT("Same death remains committed after actual pickup removed pending membership"),
        Access::Observe(Mode, Enemy) && Access::Observe(Mode, Enemy) && Known(A, Picked, Empty) &&
        Access::Unchanged(Enemy, AwardedEnemy) && Scope.Unchanged(0, BeforeA) && Scope.Unchanged(1, BeforeB))) return false;
    // Reopen only after duplicate checks: native world death latches are not
    // persisted identities and cannot be transferred to a replacement model.
    Access::RemoveFixtureEntry(Mode, A, *Scope.Controllers[0]);
    Access::End(A, true); // Release original writer lease before replacement.
    Scope.Models[0] = Scope.NewModel(0);
    if (!Scope.Models[0] || !TestTrue(TEXT("A new actor really reopens the complete r2 snapshot without another write"),
        Known(*Scope.Models[0], Picked, Empty) && Scope.Traces[0].Count == BeforeA.Opens)) return false;
    ModelImage Reopened;
    if (!Scope.Capture(0, Reopened)) return false;
    for (int32 Bank = 0; Bank < JournalPathCount; ++Bank)
        if (!TestTrue(TEXT("Reopen preserves all retained journal namespace bank bytes"), EqualBank(Reopened.Banks[Bank], BeforeA.Banks[Bank]))) return false;
    if (!TestTrue(TEXT("Nonrecipient B and its actual banks remain at the post-Logout baseline"),
        Scope.Unchanged(1, BeforeB) && Known(B, Initial, Empty))) return false;
    AddInfo(TEXT("Native trusted-hit/death/Logout/journal/public-pickup integration only. No positive attack dispatch, input, timing, pose, login, network, visibility, scheduling, respawn or persistent-world kill identity proof."));
    return true;
}
#endif
