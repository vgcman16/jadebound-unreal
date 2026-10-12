#pragma once
#include "JadeboundProgressionActor.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "JadeboundDevelopmentGameMode.h"
#include "Development/DevelopmentIdentity.h"
#include "Domain/ProgressionCore.h"
#include "Persistence/ProgressionSaveV2.h"
#include "Presentation/StackSaleQuote.h"
#include "UI/SJadeboundProgressionPanel.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SButton.h"
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
#include "HAL/PlatformTime.h"
#include "UObject/StrongObjectPtr.h"

// Distinct global, automation-only friend in actor/mode; no public or RPC hook.
struct FJadeboundMerchantBulkSaleTestAccess
{
    static bool Initialize(AJadeboundProgressionActor& Model, APlayerController& PC, ACharacter& Hero,
        AJadeboundDevelopmentGameMode& Mode, const FString& Id, UStaticMesh* Glaive, bool bInitial)
    {
        if (!Model.ConfigureDevelopmentOwner(&PC, Id, Glaive) || PC.GetPawn() != &Hero || Hero.GetController() != &PC) return false;
        Model.Hero = &Hero; // Trusted possessed fixture; ConfigureDevelopmentOwner clears this reference.
        Model.bReady = Model.ReadSave(bInitial);
        if (!IsValid(&Model) || Model.IsActorBeingDestroyed()) return false;
        Model.bServerModelInitialized = Model.bReady;
        if (!Model.bReady) return false;
        Mode.Models.Add(Id, &Model); Mode.ActiveControllers.Add(&PC, Id); // Registry setup, not PostLogin/StartPlay admission.
        if (!Hero.HasActorBegunPlay()) Hero.DispatchBeginPlay(); // Real saved Blueprint/component lifecycle.
        if (!IsValid(&Hero) || Hero.IsActorBeingDestroyed() || !IsValid(&Model) || Model.IsActorBeingDestroyed() || !Hero.HasActorBegunPlay()) return false;
        Model.BindGameplay();
        if (!IsValid(&Hero) || Hero.IsActorBeingDestroyed() || !IsValid(&Model) || Model.IsActorBeingDestroyed()) return false;
        Model.ApplyEquipment();
        return IsValid(&Hero) && !Hero.IsActorBeingDestroyed() && IsValid(&Model) && !Model.IsActorBeingDestroyed() &&
            Model.DevelopmentWriterLease && Model.IsDevelopmentReady() && Model.IsProgressionReady() && Model.AuthorityReady();
    }
    static const Jadebound::Snapshot& Core(const AJadeboundProgressionActor& Model) { return Model.Core->GetSnapshot(); }
    static const UJadeboundCharacterSave* Saved(const AJadeboundProgressionActor& Model) { return Model.LastCommittedSave.Get(); }
    static const TArray<FJadeboundSavedDrop>& Drops(const AJadeboundProgressionActor& Model) { return Model.PendingDrops; }
    static int32 BankSlot(const AJadeboundProgressionActor& Model) { return Model.LastSaveSlot; }
    static bool Clean(const AJadeboundProgressionActor& Model)
    { return !Model.bPersistenceBlocked && !Model.bUnsavedCandidate && !Model.HeldSaveCandidate &&
        Model.PersistencePresentation.FailureKind == EJadeboundPersistenceFailureKind::None && Model.PendingDrops.IsEmpty(); }
    static void SetOpener(AJadeboundProgressionActor& Model, TFunction<IFileHandle*(const FString&)> Opener)
    { Model.AutomationJournalOpener = MoveTemp(Opener); }
    // Direct authority-handler fixture only; no client or network transport.
    static void BuyRequest(AJadeboundProgressionActor& Model, int32 Id, int32 Quantity, int64 Revision)
    { Model.ServerBuy_Implementation(Id, Quantity, Revision); }
    static void UnequipRequest(AJadeboundProgressionActor& Model, int64 Revision)
    { Model.ServerUnequip_Implementation(Revision); }
    static void SetDisplayedRevision(AJadeboundProgressionActor& Model, int64 Revision, uint64 Exact)
    { Model.State.Revision = Revision; Model.State.ExactRevision = Exact; }
    static void RefreshState(AJadeboundProgressionActor& Model) { Model.RefreshState(); }
    static const UJadeboundCharacterSave* Held(const AJadeboundProgressionActor& Model) { return Model.HeldSaveCandidate.Get(); }
    static bool IsHeld(const AJadeboundProgressionActor& Model)
    { return Model.bUnsavedCandidate && Model.bPersistenceBlocked && Model.HeldSaveCandidate != nullptr; }
    static double Deadline(const AJadeboundProgressionActor& Model) { return Model.PersistenceRetryAt; }
    static bool Retry(AJadeboundProgressionActor& Model) { return Model.RetryHeldSave(false); }
    // Controlled local fixture clock only; never a claim of elapsed production backoff.
    static void SetLocalFixtureFutureDeadline(AJadeboundProgressionActor& Model)
    { Model.PersistenceRetryAt = FPlatformTime::Seconds() + 60.0; }
    static void ExpireLocalFixtureDeadline(AJadeboundProgressionActor& Model)
    { Model.PersistenceRetryAt = FPlatformTime::Seconds() - 1.0; }
    // Explicit synthetic boundary fixture, persisted through the real journal.
    // This is never earned progression or an exposed runtime/client award API.
    static bool SeedLocalFixtureSnapshot(AJadeboundProgressionActor& Model, const Jadebound::Snapshot& Snapshot)
    {
        if (!Model.bDevelopmentModel || !Model.Core || !Model.DevelopmentWriterLease ||
            !Model.IsDevelopmentReady() || !Model.AuthorityReady() || !Clean(Model)) return false;
        if (!Model.Core->LoadSnapshot(Snapshot).Succeeded()) return false;
        Model.RefreshState();
        return Model.WriteSave() && Clean(Model) && Model.IsProgressionReady();
    }
    // Synthetic panel fixture only: no viewport attachment or game input route.
    static void SetFixtureMenu(AJadeboundProgressionActor& Model, bool bOpen) { Model.bMenuOpen = bOpen; }
    static void Select(SJadeboundProgressionPanel& Panel, int32 Slot) { Panel.SelectSlot(Slot); }
    static void Shop(SJadeboundProgressionPanel& Panel) { Panel.ShowContextPage(SJadeboundProgressionPanel::EContextPage::Merchant); }
    static void Gear(SJadeboundProgressionPanel& Panel) { Panel.ShowContextPage(SJadeboundProgressionPanel::EContextPage::Equipment); }
    static void Refresh(SJadeboundProgressionPanel& Panel) { Panel.RefreshFromActor(true); }
    static TSharedPtr<SButton> Button(SJadeboundProgressionPanel& Panel, const TCHAR* FocusKey)
    {
        const TWeakPtr<SWidget>* Target = Panel.FocusTargets.Find(FName(FocusKey));
        return Target ? StaticCastSharedPtr<SButton>(Target->Pin()) : nullptr;
    }
    static void Retire(AJadeboundProgressionActor& Model, AJadeboundDevelopmentGameMode* Mode,
        APlayerController* PC, const FString& Id)
    {
        Model.bPersistenceEndingPlay = true; Model.CancelRecipientAttack();
        Model.AutomationJournalOpener = nullptr; Model.DevelopmentWriterLease.Reset();
        if (IsValid(Mode)) { Mode->Models.Remove(Id); if (IsValid(PC)) Mode->ActiveControllers.Remove(PC); }
    }
};

namespace JadeboundMerchantBulkSale
{
using Access = FJadeboundMerchantBulkSaleTestAccess;
inline bool SameCore(const Jadebound::Snapshot& A, const Jadebound::Snapshot& B)
{
    if (A.Version != B.Version || A.Revision != B.Revision || A.Coins != B.Coins || A.Quest != B.Quest ||
        A.EquippedWeapon != B.EquippedWeapon || A.NextDropId != B.NextDropId || A.QuestKillDropId != B.QuestKillDropId ||
        A.UnprocessedDropIds != B.UnprocessedDropIds) return false;
    for (uint32 I = 0; I < Jadebound::InventorySlotCount; ++I)
        if (A.Inventory[I].Id != B.Inventory[I].Id || A.Inventory[I].Count != B.Inventory[I].Count) return false;
    return true;
}
inline bool SameReflected(const FJadeboundProgressionState& A, const FJadeboundProgressionState& B)
{ return FJadeboundProgressionState::StaticStruct()->CompareScriptStruct(&A, &B, 0); }
inline bool SameDrops(const TArray<FJadeboundSavedDrop>& A, const TArray<FJadeboundSavedDrop>& B)
{
    if (A.Num() != B.Num()) return false;
    for (int32 I = 0; I < A.Num(); ++I)
        if (A[I].DropId != B[I].DropId || A[I].ItemId != B[I].ItemId || A[I].Count != B[I].Count || A[I].Location != B[I].Location) return false;
    return true;
}
struct Bank { bool bExists = false; TArray<uint8> Bytes; };
inline bool ReadBank(const FString& Path, Bank& Out)
{
    IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
    Out.bExists = Files.FileExists(*Path); Out.Bytes.Reset();
    if (!Out.bExists) return true;
    const int64 Size = Files.FileSize(*Path);
    if (Size <= 0 || Size > 1024 * 1024 + 32) return false;
    TUniquePtr<IFileHandle> File(Files.OpenRead(*Path));
    if (!File) return false;
    Out.Bytes.SetNumUninitialized(static_cast<int32>(Size));
    return File->Read(Out.Bytes.GetData(), Size);
}
inline bool SameBank(const Bank& A, const Bank& B) { return A.bExists == B.bExists && A.Bytes == B.Bytes; }
inline bool OwnedUser(FString& User)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("JadeboundMerchantBulkSaleTests")) ||
        !FParse::Value(FCommandLine::Get(), TEXT("UserDir="), User) || User.IsEmpty() || FPaths::IsRelative(User)) return false;
    User.ReplaceInline(TEXT("\\"), TEXT("/"));
    TArray<FString> Parts; User.ParseIntoArray(Parts, TEXT("/"), true);
    for (const auto& Part : Parts) if (Part == TEXT(".") || Part == TEXT("..")) return false;
    FPaths::NormalizeDirectoryName(User);
    FString Effective = FPaths::ProjectUserDir(); FPaths::NormalizeDirectoryName(Effective);
    const FString Parent = FPaths::GetCleanFilename(FPaths::GetPath(User));
    const FString Prefix(TEXT("Jadebound-P102-A"));
    const FString Attempt = Parent.Mid(Prefix.Len());
    if (!Parent.StartsWith(Prefix, ESearchCase::CaseSensitive) || Attempt.Len() != 3 || Attempt == TEXT("000")) return false;
    for (TCHAR Ch : Attempt) if (Ch < TEXT('0') || Ch > TEXT('9')) return false;
    return FPaths::IsSamePath(User, Effective) && FPaths::GetCleanFilename(User) == TEXT("UserDir-v001") &&
        FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()), User);
}
struct Image
{
    Jadebound::Snapshot Core;
    FJadeboundProgressionState Reflected, Committed;
    const UJadeboundCharacterSave* Saved = nullptr;
    FString CharacterId;
    TArray<FJadeboundSavedDrop> Pending, SavedDrops;
    int32 Slot = -1, Opens = 0;
    Bank Banks[4];
};
struct FScope
{
    FAutomationTestBase& Test;
    UWorld* World = nullptr;
    TStrongObjectPtr<UGameInstance> Instance;
    AJadeboundDevelopmentGameMode* Mode = nullptr;
    APlayerController* PC = nullptr;
    ACharacter* Hero = nullptr;
    AJadeboundProgressionActor* Model = nullptr;
    UStaticMesh* Glaive = nullptr;
    UStaticMesh* Spear = nullptr;
    FString Id, Paths[4], LastOpenedPath;
    int32 Opens = 0;
    bool bForeign = false, bValid = false;
    FScope(FAutomationTestBase& InTest) : Test(InTest)
    {
        FString User;
        if (!IsInGameThread() || !GEngine || !GEngine->WorldSettingsClass || !OwnedUser(User))
        { Test.AddError(TEXT("Initialized engine/game thread and owned fresh test UserDir/opt-in required.")); return; }
        auto* HeroClass = LoadClass<ACharacter>(nullptr, TEXT("/Game/Jadebound/Tests/GlaiveCombatV006/BP_GlaiveHero_V006.BP_GlaiveHero_V006_C"));
        Glaive = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Jadebound/OriginalWeapons/JadeGlaiveV001/SM_JadeGlaive_CM_V001.SM_JadeGlaive_CM_V001"));
        Spear = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Jadebound/OriginalWeapons/AshwoodSpearV001/SM_AshwoodSpear_CM_V001.SM_AshwoodSpear_CM_V001"));
        if (!HeroClass || !Glaive || !Spear) { Test.AddError(TEXT("Actual preserved Hero and both original weapon assets required.")); return; }
        Id = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
        if (!Jadebound::Development::IsCharacterId(Id)) { Test.AddError(TEXT("Fresh canonical nonzero GUID required.")); return; }
        for (int32 I = 0; I < 4; ++I)
            if (!Jadebound::Persistence::TryJournalPath(true, Id, I % 2, I >= 2, Paths[I]) ||
                !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Paths[I]), User))
            { Test.AddError(TEXT("All production journal paths must remain under owned UserDir.")); return; }
        if (FPlatformFileManager::Get().GetPlatformFile().DirectoryExists(*FPaths::GetPath(Paths[2])))
        { Test.AddError(TEXT("Fresh identity directory required; no reset or reuse.")); return; }
        UWorld::InitializationValues Values;
        Values.InitializeScenes(true).CreatePhysicsScene(true).EnableTraceCollision(true).ShouldSimulatePhysics(false)
            .AllowAudioPlayback(false).RequiresHitProxies(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false).SetTransactional(false);
        World = UWorld::CreateWorld(EWorldType::Game, false, FName(*FString::Printf(TEXT("MerchantBulk_%s"), *Id)), nullptr, true, ERHIFeatureLevel::Num, &Values);
        if (!World) { Test.AddError(TEXT("World creation failed.")); return; }
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        Instance.Reset(NewObject<UGameInstance>(GEngine)); World->SetGameInstance(Instance.Get());
        if (!Instance || !World->GetWorldSettings() || !World->GetPhysicsScene()) { Test.AddError(TEXT("Actual world prerequisites unavailable.")); return; }
        World->GetWorldSettings()->DefaultGameMode = AJadeboundDevelopmentGameMode::StaticClass();
        if (!World->SetGameMode(FURL())) { Test.AddError(TEXT("Actual authority mode route failed.")); return; }
        World->InitializeActorsForPlay(FURL(), false); // Genuine ProcessEvent/Blueprint lifecycle, never a flag bypass.
        if (!World->AreActorsInitialized() || World->HasBegunPlay())
        { Test.AddError(TEXT("Genuine initialized, nonbegun world required.")); return; }
        Mode = Cast<AJadeboundDevelopmentGameMode>(World->GetAuthGameMode());
        FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        PC = World->SpawnActor<APlayerController>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
        Model = World->SpawnActor<AJadeboundProgressionActor>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
        if (!Mode || !PC || !Model || !Mode->HasAuthority() || !Model->HasAuthority() || World->GetNetMode() != NM_Standalone ||
            !FMath::IsFinite(Model->MerchantRange) || Model->MerchantRange <= 0.f || Model->MerchantPoint.ContainsNaN())
        { Test.AddError(TEXT("Standalone authority and real merchant configuration required.")); return; }
        Model->AshwoodSpearMesh = Spear; // Supply the real map-configured asset in this synthetic actor fixture.
        Hero = World->SpawnActor<ACharacter>(HeroClass, Model->MerchantPoint, FRotator::ZeroRotator, Spawn);
        if (!Hero) { Test.AddError(TEXT("Actual Hero spawn failed.")); return; }
        PC->Possess(Hero); InstallOpener();
        if (!Access::Initialize(*Model, *PC, *Hero, *Mode, Id, Glaive, true) || !Live() || !Model->IsMerchantInRange())
        { Test.AddError(TEXT("Actual possession/living Hero/readiness/lease/range prerequisites failed.")); return; }
        bValid = StateIs(Jadebound::Snapshot{}) && Opens == 1 && Access::BankSlot(*Model) == 0;
        if (!bValid) Test.AddError(TEXT("Whole known r0 and fresh actual initial A-bank required."));
    }
    void InstallOpener()
    {
        Access::SetOpener(*Model, [this](const FString& Path) -> IFileHandle*
        {
            if (Path != Paths[2] && Path != Paths[3]) { bForeign = true; return nullptr; }
            ++Opens; LastOpenedPath = Path;
            return FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*Path, false, false);
        });
    }
    bool Live() const
    {
        return IsInGameThread() && GEngine && IsValid(World) && !World->bIsTearingDown && GEngine->GetWorldContextFromWorld(World) &&
            World->AreActorsInitialized() && !World->HasBegunPlay() && IsValid(Mode) && Mode == World->GetAuthGameMode() &&
            IsValid(Model) && !Model->IsActorBeingDestroyed() && IsValid(Hero) && !Hero->IsActorBeingDestroyed() &&
            IsValid(PC) && !PC->IsActorBeingDestroyed() && PC->GetPawn() == Hero && Hero->GetController() == PC &&
            Hero->HasActorBegunPlay() && Hero->GetWorld() == World && Model->GetWorld() == World;
    }
    bool StateIs(const Jadebound::Snapshot& Expected) const
    {
        if (!Live() || bForeign || !Access::Clean(*Model) || !Model->IsProgressionReady()) return false;
        const auto* Saved = Access::Saved(*Model);
        Jadebound::Snapshot Decoded; FJadeboundProgressionState Encoded;
        return Saved && Saved->CharacterId == Id && Saved->PendingDrops.IsEmpty() &&
            SameCore(Access::Core(*Model), Expected) && Jadebound::Persistence::EncodeStateV2(Expected, Encoded) &&
            SameReflected(Encoded, Model->GetProgressionState()) && SameReflected(Encoded, Saved->State) &&
            Jadebound::Persistence::DecodeStateV2(Saved->State, Decoded) && SameCore(Expected, Decoded);
    }
    bool Capture(Image& Out) const
    {
        if (!Live() || bForeign || !Access::Clean(*Model) || !Access::Saved(*Model)) return false;
        Out.Core = Access::Core(*Model); Out.Reflected = Model->GetProgressionState(); Out.Saved = Access::Saved(*Model);
        Out.Committed = Out.Saved->State; Out.CharacterId = Out.Saved->CharacterId; Out.SavedDrops = Out.Saved->PendingDrops;
        Out.Pending = Access::Drops(*Model); Out.Slot = Access::BankSlot(*Model); Out.Opens = Opens;
        for (int32 I = 0; I < 4; ++I) if (!ReadBank(Paths[I], Out.Banks[I])) return false;
        return !Out.Banks[0].bExists && !Out.Banks[1].bExists && Out.Slot >= 0 && Out.Slot < 2 && Out.Banks[2 + Out.Slot].bExists;
    }
    bool Unchanged(const Image& Before) const
    {
        Image After;
        if (!Capture(After) || After.Saved != Before.Saved || After.CharacterId != Before.CharacterId ||
            !SameCore(After.Core, Before.Core) || !SameReflected(After.Reflected, Before.Reflected) ||
            !SameReflected(After.Committed, Before.Committed) || !SameDrops(After.Pending, Before.Pending) ||
            !SameDrops(After.SavedDrops, Before.SavedDrops) || After.Slot != Before.Slot || After.Opens != Before.Opens) return false;
        for (int32 I = 0; I < 4; ++I) if (!SameBank(After.Banks[I], Before.Banks[I])) return false;
        return true;
    }
    bool Advanced(const Image& Before, const Jadebound::Snapshot& Expected) const
    {
        Image After;
        if (!StateIs(Expected) || !Capture(After) || After.Opens != Before.Opens + 1 || After.Saved == Before.Saved ||
            After.Slot != 1 - Before.Slot || LastOpenedPath != Paths[2 + After.Slot] || SameBank(After.Banks[2 + After.Slot], Before.Banks[2 + After.Slot])) return false;
        for (int32 I = 0; I < 4; ++I) if (I != 2 + After.Slot && !SameBank(After.Banks[I], Before.Banks[I])) return false;
        return true;
    }
    bool Reopen()
    {
        if (!Live()) return false;
        Access::Retire(*Model, Mode, PC, Id);
        if (!World->DestroyActor(Model)) return false;
        Model = nullptr;
        FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Model = World->SpawnActor<AJadeboundProgressionActor>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
        if (!IsValid(Model) || Model->IsActorBeingDestroyed()) return false;
        Model->AshwoodSpearMesh = Spear;
        InstallOpener();
        return Access::Initialize(*Model, *PC, *Hero, *Mode, Id, Glaive, false) && Live() && Model->IsMerchantInRange();
    }
    ~FScope()
    {
        if (!World) return;
        if (IsValid(Model) && !Model->IsActorBeingDestroyed()) Access::Retire(*Model, Mode, PC, Id);
        UEngine* Engine = GEngine;
        if (!Engine || !Engine->GetWorldContextFromWorld(World)) { Test.AddError(TEXT("Engine/context lost; owned lease released, world retained.")); return; }
        if (IsValid(PC)) PC->UnPossess();
        World->DestroyWorld(false); Engine->DestroyWorldContext(World); World = nullptr; Instance.Reset();
    }
};
}

#endif // WITH_DEV_AUTOMATION_TESTS
