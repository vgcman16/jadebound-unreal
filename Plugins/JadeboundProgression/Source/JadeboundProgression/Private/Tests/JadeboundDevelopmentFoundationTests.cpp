#include "Development/DevelopmentIdentity.h"
#include "Domain/ProgressionCore.h"
#include "JadeboundProgressionActor.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundCharacterNamespaceTest, "Jadebound.Development.CharacterNamespaces",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundCharacterNamespaceTest::RunTest(const FString& Parameters)
{
    const FString A(TEXT("27b0cbce01df4b6b8f9cf927acaecf11"));
    const FString B(TEXT("4a372b5a838f42be9d9a12e04cb24422"));
    FString A0, A1, B0;
    TestTrue(TEXT("Valid server profile"), Jadebound::Development::IsCharacterId(A));
    TestTrue(TEXT("A journal"), Jadebound::Development::TryJournalPath(A, 0, A0));
    TestTrue(TEXT("B journal"), Jadebound::Development::TryJournalPath(A, 1, A1));
    TestTrue(TEXT("Second character"), Jadebound::Development::TryJournalPath(B, 0, B0));
    TestTrue(TEXT("Each character and bank has a distinct file"), A0 != A1 && A0 != B0 && A1 != B0);
    TestTrue(TEXT("Fixed development root"), A0.Contains(TEXT("JadeboundDevelopment")) && !A0.Contains(TEXT("Traveler01")));
    const TArray<FString> Invalid = {TEXT(""), TEXT("Traveler01"), TEXT("../Traveler01"),
        A.ToUpper(), A + TEXT("/.."), TEXT("00000000000000000000000000000000"), FString::ChrN(4096, TEXT('a'))};
    for (const FString& Id : Invalid)
    {
        FString Rejected(TEXT("stale path"));
        TestFalse(TEXT("Reject invalid identity before producing a path"), Jadebound::Development::TryJournalPath(Id, 0, Rejected));
        TestTrue(TEXT("No stale path survives rejection"), Rejected.IsEmpty());
    }
    FString InvalidSlot;
    TestFalse(TEXT("Negative journal bank"), Jadebound::Development::TryJournalPath(A, -1, InvalidSlot));
    TestFalse(TEXT("Huge journal bank"), Jadebound::Development::TryJournalPath(A, MAX_int32, InvalidSlot));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundLocalAdmissionTest, "Jadebound.Development.LoopbackAdmission",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundLocalAdmissionTest::RunTest(const FString& Parameters)
{
    for (const FString& Address : {FString(TEXT("127.0.0.1")), FString(TEXT("::1")), FString(TEXT("[::1]"))})
        TestTrue(TEXT("Exact loopback"), Jadebound::Development::IsLoopbackAddress(Address));
    for (const FString& Address : {FString(TEXT("")), FString(TEXT("192.168.1.4")), FString(TEXT("127.0.0.1.attacker")), FString(TEXT("127.0.0.1:7777"))})
        TestFalse(TEXT("No prefix/hostname/address-option admission"), Jadebound::Development::IsLoopbackAddress(Address));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundIndependentModelsTest, "Jadebound.Development.IndependentCoreModels",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundIndependentModelsTest::RunTest(const FString& Parameters)
{
    Jadebound::ProgressionCore A;
    Jadebound::ProgressionCore B;
    const auto BeforeB = B.GetSnapshot();
    TestTrue(TEXT("A buys its spear"), A.Buy(Jadebound::ItemId::AshwoodSpear, 1).Succeeded());
    TestTrue(TEXT("A equips its spear"), A.Equip(0).Succeeded());
    TestEqual(TEXT("B coins remain independent"), B.GetSnapshot().Coins, BeforeB.Coins);
    TestEqual(TEXT("B revision remains independent"), B.GetSnapshot().Revision, BeforeB.Revision);
    TestTrue(TEXT("B equipment remains independent"), B.GetSnapshot().EquippedWeapon == BeforeB.EquippedWeapon);
    TestTrue(TEXT("B's first slot remains empty"), B.GetSnapshot().Inventory[0].Count == 0);
    const auto BeforeA = A.GetSnapshot();
    TestTrue(TEXT("B separately accepts a quest"), B.AcceptQuest().Succeeded());
    TestEqual(TEXT("A revision is unchanged by B"), A.GetSnapshot().Revision, BeforeA.Revision);
    TestTrue(TEXT("A quest is unchanged by B"), A.GetSnapshot().Quest == BeforeA.Quest);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundWindowsWriterLeaseTest, "Jadebound.Development.WindowsWriterLease",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundWindowsWriterLeaseTest::RunTest(const FString& Parameters)
{
#if PLATFORM_WINDOWS
    // Unique synthetic fixture only. Keep both files; never touch real journals.
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"),
        TEXT("JadeboundWriterLease"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
    IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
    if (!TestTrue(TEXT("Unique fixture directory"), Files.CreateDirectoryTree(*Directory))) return false;
    const FString APath = FPaths::Combine(Directory, TEXT("CharacterA.writer"));
    const FString BPath = FPaths::Combine(Directory, TEXT("CharacterB.writer"));
    TUniquePtr<IFileHandle> A(Files.OpenWrite(*APath, true, false));
    TUniquePtr<IFileHandle> Duplicate(Files.OpenWrite(*APath, true, false));
    TUniquePtr<IFileHandle> B(Files.OpenWrite(*BPath, true, false));
    TestNotNull(TEXT("First character writer"), A.Get());
    TestNull(TEXT("Second writer to the same character rejects"), Duplicate.Get());
    TestNotNull(TEXT("Another character is not blocked"), B.Get());
    A.Reset();
    TUniquePtr<IFileHandle> Reopened(Files.OpenWrite(*APath, true, false));
    TestNotNull(TEXT("Closing the handle releases the lease"), Reopened.Get());
#else
    AddInfo(TEXT("This source candidate qualifies Windows writer semantics only; platform-specific proof is pending elsewhere."));
#endif
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundBalmTransactionsTest, "Jadebound.Progression.BalmTransactions",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundBalmTransactionsTest::RunTest(const FString& Parameters)
{
    using namespace Jadebound;
    const auto Equal = [](const Snapshot& A, const Snapshot& B) {
        if (A.Version != B.Version || A.Revision != B.Revision || A.Coins != B.Coins ||
            A.EquippedWeapon != B.EquippedWeapon || A.Quest != B.Quest || A.NextDropId != B.NextDropId ||
            A.QuestKillDropId != B.QuestKillDropId || A.UnprocessedDropIds != B.UnprocessedDropIds) return false;
        for (std::uint32_t I=0; I<InventorySlotCount; ++I)
            if (A.Inventory[I].Id != B.Inventory[I].Id || A.Inventory[I].Count != B.Inventory[I].Count) return false;
        return true;
    };
    ProgressionCore Bought;
    TestTrue(TEXT("Merchant buys two owned balm"), Bought.Buy(ItemId::Balm,2).Succeeded());
    TestEqual(TEXT("Fixed price charged"), Bought.GetSnapshot().Coins, static_cast<std::int64_t>(4));
    Snapshot Expected=Bought.GetSnapshot();
    --Expected.Inventory[0].Count; ++Expected.Revision;
    const auto First=Bought.Consume(0);
    TestTrue(TEXT("One admitted consume exact full snapshot"), First.Succeeded() && First.Changed && Equal(Expected,Bought.GetSnapshot()));
    Expected.Inventory[0]={}; ++Expected.Revision;
    TestTrue(TEXT("Last item clears slot exact full snapshot"), Bought.Consume(0).Succeeded() && Equal(Expected,Bought.GetSnapshot()));
    const auto EmptyBefore=Bought.GetSnapshot();
    TestTrue(TEXT("Empty rejected unchanged"), Bought.Consume(0).Code==ResultCode::InsufficientItems && Equal(EmptyBefore,Bought.GetSnapshot()));
    TestTrue(TEXT("Out-of-range rejected unchanged"), Bought.Consume(InventorySlotCount).Code==ResultCode::InvalidSlot && Equal(EmptyBefore,Bought.GetSnapshot()));
    TestTrue(TEXT("Huge slot rejected unchanged"), Bought.Consume(MAX_uint32).Code==ResultCode::InvalidSlot && Equal(EmptyBefore,Bought.GetSnapshot()));
    for (const ItemId Id : {ItemId::JadeGlaive,ItemId::AshwoodSpear,ItemId::IronOre,ItemId::Moonleaf,ItemId::JadeShard})
    {
        Snapshot S; S.Inventory[0]={Id,1}; ProgressionCore Rejected;
        TestTrue(TEXT("Valid nonconsumable fixture"), Rejected.LoadSnapshot(S).Succeeded());
        TestTrue(TEXT("Nonconsumable rejected complete snapshot unchanged"), Rejected.Consume(0).Code==ResultCode::NotConsumable && Equal(S,Rejected.GetSnapshot()));
    }
    ProgressionCore Quest;
    TestTrue(TEXT("Quest fixture"), Quest.AcceptQuest().Succeeded() && Quest.RecordRaiderDefeat(1).Succeeded() && Quest.PickupParcel(1).Succeeded());
    const auto ParcelBefore=Quest.GetSnapshot();
    TestTrue(TEXT("Parcel cannot be used or alter ledgers"), Quest.Consume(0).Code==ResultCode::NotConsumable && Equal(ParcelBefore,Quest.GetSnapshot()));
    Snapshot Full;
    for (auto& Slot : Full.Inventory) Slot={ItemId::IronOre,20};
    Full.Inventory[5]={ItemId::Balm,2};
    ProgressionCore FullBag;
    TestTrue(TEXT("Full bag fixture"), FullBag.LoadSnapshot(Full).Succeeded());
    --Full.Inventory[5].Count; ++Full.Revision;
    TestTrue(TEXT("Full bag use needs no empty slot"), FullBag.Consume(5).Succeeded() && Equal(Full,FullBag.GetSnapshot()));
    const auto FullBefore=FullBag.GetSnapshot();
    TestTrue(TEXT("Full bag buy rejected unchanged"), FullBag.Buy(ItemId::IronOre,1).Code==ResultCode::InventoryFull && Equal(FullBefore,FullBag.GetSnapshot()));
    TestTrue(TEXT("Consumable never equips"), FullBag.Equip(5).Code==ResultCode::NotWeapon && Equal(FullBefore,FullBag.GetSnapshot()));
    Snapshot Overflow=FullBefore; Overflow.Revision=MaximumPersistedCounter;
    ProgressionCore Last;
    TestTrue(TEXT("Overflow fixture"), Last.LoadSnapshot(Overflow).Succeeded());
    TestTrue(TEXT("Overflow rejects consume unchanged"), Last.Consume(5).Code==ResultCode::RevisionOverflow && Equal(Overflow,Last.GetSnapshot()));
    Snapshot Bad; Bad.Inventory[0]={ItemId::Balm,11};
    TestTrue(TEXT("Balm stack bound"), ProgressionCore::ValidateSnapshot(Bad).Validation==ValidationCode::StackLimitExceeded);
    Bad.Inventory[0]={static_cast<ItemId>(8),1};
    TestTrue(TEXT("Unknown future item rejects"), ProgressionCore::ValidateSnapshot(Bad).Validation==ValidationCode::UnknownInventoryItem);
    Bad.Inventory[0]={ItemId::Balm,0};
    TestTrue(TEXT("Zero occupied stack rejects"), ProgressionCore::ValidateSnapshot(Bad).Validation==ValidationCode::EmptySlotCountMismatch);
    Bad.Inventory[0]={}; Bad.EquippedWeapon=ItemId::Balm;
    TestTrue(TEXT("Balm equipped snapshot rejects"), ProgressionCore::ValidateSnapshot(Bad).Validation==ValidationCode::InvalidEquippedWeapon);
    const auto* Leaf=ProgressionCore::FindDefinition(ItemId::Moonleaf);
    const auto* Balm=ProgressionCore::FindDefinition(ItemId::Balm);
    TestTrue(TEXT("Moonleaf trade semantics unchanged"), Leaf && Leaf->HealingPercent==0 && Leaf->BuyPrice==2 && Leaf->SellPrice==1 && !Leaf->Weapon);
    TestTrue(TEXT("Fixed original balm catalog"), Balm && Balm->StackLimit==10 && Balm->BuyPrice==3 && Balm->SellPrice==1 && Balm->HealingPercent==25 && !Balm->Weapon);
    ProgressionCore Sold;
    TestTrue(TEXT("Balm sale fixture"), Sold.Buy(ItemId::Balm,1).Succeeded());
    const auto SaleBefore=Sold.GetSnapshot();
    TestTrue(TEXT("Balm has ordinary sale transaction"), Sold.Sell(0,1).Succeeded() && Sold.GetSnapshot().Coins==SaleBefore.Coins+1 && Sold.GetSnapshot().Revision==SaleBefore.Revision+1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundTypedHotbarTest,"Jadebound.Controls.TypedItemHotbar",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundTypedHotbarTest::RunTest(const FString& Parameters)
{
    FJadeboundLocalHotbarBindings B;
    TestTrue(TEXT("Typed defaults are valid"),B.IsValid());
    TestTrue(TEXT("Guard moves to F7"),B.Assign(6,FJadeboundHotbarEntry::Skill(0)));
    TestTrue(TEXT("Current moves to F10"),B.Assign(9,FJadeboundHotbarEntry::Skill(1)));
    TestTrue(TEXT("Balm assigned by identity"),B.Assign(2,FJadeboundHotbarEntry::Item(7)));
    TestTrue(TEXT("Duplicate Balm assignment moves instead of inventing stock"),B.Assign(3,FJadeboundHotbarEntry::Item(7)) && B.Resolve(2).Kind==EJadeboundHotbarKind::Empty);
    const auto Before=B;
    TestTrue(TEXT("Unknown material or skill payload rejects unchanged"),!B.Assign(4,FJadeboundHotbarEntry::Item(4)) && !B.Assign(4,FJadeboundHotbarEntry::Skill(7)) && B.Equals(Before));
    TestTrue(TEXT("Out of bounds rejects unchanged"),!B.Assign(-1,FJadeboundHotbarEntry::Item(7)) && !B.Assign(10,FJadeboundHotbarEntry::Item(7)) && B.Equals(Before));
    FJadeboundLocalHotbarBindings Parsed;
    const FString Encoded=B.Encode();
    TestTrue(TEXT("Production schema2 roundtrip preserves all ten typed entries"),FJadeboundLocalHotbarBindings::Decode(Encoded,TEXT("Memory"),false,Parsed) && Parsed.Equals(B));
    const FString Legacy=TEXT("[JadeboundHotbar]\nSchema=1\nF1=None\nF2=None\nF3=None\nF4=None\nF5=None\nF6=None\nF7=Guard\nF8=None\nF9=None\nF10=Current\n");
    TestTrue(TEXT("Schema1 migrates F7/F10 in place"),FJadeboundLocalHotbarBindings::Decode(Legacy,TEXT("Memory"),true,Parsed) && Parsed.Find(FJadeboundHotbarEntry::Skill(0))==6 && Parsed.Find(FJadeboundHotbarEntry::Skill(1))==9);
    const auto Migrated=Parsed;
    TestTrue(TEXT("Schema2 cannot masquerade as retained v1"),!FJadeboundLocalHotbarBindings::Decode(Encoded,TEXT("Memory"),true,Parsed) && Parsed.Equals(Migrated));
    TestTrue(TEXT("Schema1 cannot masquerade as existing v2"),!FJadeboundLocalHotbarBindings::Decode(Legacy,TEXT("Memory"),false,Parsed) && Parsed.Equals(Migrated));
    const FString Invalid[]={Encoded.Replace(TEXT("Item:Balm"),TEXT("Item:Moonleaf")),Encoded.Replace(TEXT("Schema=2"),TEXT("Schema=3")),
        Encoded+TEXT("F4=Item:Balm\n"),Encoded+TEXT("[Other]\nF1=Skill:Guard\n"),Encoded.Replace(TEXT("F1=Empty"),TEXT("F1=Item:Balm")),
        Encoded.Replace(TEXT("Item:Balm"),TEXT("Skill:Balm")),Encoded.Replace(TEXT("F10=Skill:Current"),TEXT("F10=skill:Current")),FString::ChrN(4097,TEXT('x'))};
    for(const auto& Text:Invalid)
        TestTrue(TEXT("Whole malformed document rejects without partial output"),!FJadeboundLocalHotbarBindings::Decode(Text,TEXT("Memory"),false,Parsed) && Parsed.Equals(Migrated));
    TestTrue(TEXT("Item overwrite removes selected skill identity"),B.Assign(6,FJadeboundHotbarEntry::Item(7)) && B.RetainSelection(0)==INDEX_NONE && B.RetainSelection(1)==1);
    TestTrue(TEXT("Empty canonical payload clears"),B.Assign(6,{}) && B.Resolve(6).Kind==EJadeboundHotbarKind::Empty);
    return true;
}


#endif

// Deliberately editor/test-only. This is a labelled boundary fixture, never earned progression.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"

namespace JadeboundQAFullCapacity
{
bool GetIsolatedUserDir(FString& Out)
{
    FString Argument;
    if (!FParse::Value(FCommandLine::Get(), TEXT("UserDir="), Argument) ||
        Argument.IsEmpty() || FPaths::IsRelative(Argument)) return false;
    Argument.ReplaceInline(TEXT("\\"), TEXT("/"));
    TArray<FString> Parts;
    Argument.ParseIntoArray(Parts, TEXT("/"), true);
    for (const FString& Part : Parts) if (Part == TEXT(".") || Part == TEXT("..")) return false;
    FPaths::NormalizeDirectoryName(Argument);
    FString EffectiveUserDir = FPaths::ProjectUserDir();
    FPaths::NormalizeDirectoryName(EffectiveUserDir);
    const FString Parent = FPaths::GetCleanFilename(FPaths::GetPath(Argument));
    if (FPaths::GetCleanFilename(Argument) != TEXT("UserDir-QAFullCapacity-v001") ||
        !Parent.StartsWith(TEXT("Jadebound-QA-FullCapacity-"), ESearchCase::CaseSensitive) ||
        Parent.Len() <= FString(TEXT("Jadebound-QA-FullCapacity-")).Len() ||
        !FPaths::IsSamePath(Argument, EffectiveUserDir)) return false;
    Out = Argument;
    return true;
}

struct FStartupCensus
{
    bool bOptIn = false;
    bool bBeforeBeginPlay = false;
    bool bEmpty = false;
    bool bAttempted = false;
    FString UserDir;
    FStartupCensus()
    {
        bOptIn = FParse::Param(FCommandLine::Get(), TEXT("JadeboundQAFixtureFullBag"));
        if (!bOptIn || !GetIsolatedUserDir(UserDir)) return;
        bBeforeBeginPlay = true;
        if (GEngine)
            for (const FWorldContext& Context : GEngine->GetWorldContexts())
                if (Context.World() && Context.World()->IsGameWorld() && Context.World()->HasBegunPlay())
                    bBeforeBeginPlay = false;
        TArray<FString> Saves;
        IFileManager::Get().FindFilesRecursive(Saves, *UserDir, TEXT("*.sav"), true, false);
        bEmpty = Saves.IsEmpty();
        UE_LOG(LogTemp, Display, TEXT("QA FIXTURE - NOT EARNED: startup UserDir=%s BeforeBeginPlay=%d ExistingSav=%d"),
            *UserDir, bBeforeBeginPlay, Saves.Num());
    }
};
// Default-phase plugin loading records the census before the normal actor's BeginPlay save.
static FStartupCensus Startup;

bool SameState(const FJadeboundProgressionState& A, const FJadeboundProgressionState& B)
{
    return FJadeboundProgressionState::StaticStruct()->CompareScriptStruct(&A, &B, 0);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundQAFullCapacityFixtureTest, "Jadebound.QA.FullCapacityFixture",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundQAFullCapacityFixtureTest::RunTest(const FString& Parameters)
{
    using namespace JadeboundQAFullCapacity;
    if (!FParse::Param(FCommandLine::Get(), TEXT("JadeboundQAFixtureFullBag")))
    {
        AddInfo(TEXT("SKIPPED: QA full-capacity fixture needs explicit -JadeboundQAFixtureFullBag and isolated QA UserDir; no state or files changed."));
        return true;
    }
    const auto Hold = [this](const TCHAR* Why) {
        AddError(FString::Printf(TEXT("QA FIXTURE HOLD: %s. Preserve all QA state/files; do not reset or automatically retry."), Why));
        return false;
    };
    if (!IsInGameThread() || !GEngine) return Hold(TEXT("game-thread engine context unavailable"));
    if (!GEngine->bEnableOnScreenDebugMessages || !GEngine->bEnableOnScreenDebugMessagesDisplay)
        return Hold(TEXT("on-screen QA fixture labelling is disabled"));
    if (Startup.bAttempted) return Hold(TEXT("this process already attempted the fixture"));
    Startup.bAttempted = true;
    FString UserDir;
    if (!Startup.bOptIn || !Startup.bBeforeBeginPlay || !Startup.bEmpty ||
        !GetIsolatedUserDir(UserDir) || !FPaths::IsSamePath(UserDir, Startup.UserDir))
        return Hold(TEXT("opt-in path or empty pre-BeginPlay save census did not match"));

    UWorld* World = nullptr;
    AJadeboundProgressionActor* Actor = nullptr;
    int32 GameWorlds = 0, Actors = 0;
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        UWorld* Candidate = Context.World();
        if (!Candidate || Context.WorldType != EWorldType::Game) continue;
        ++GameWorlds;
        World = Candidate;
        for (TActorIterator<AJadeboundProgressionActor> It(Candidate); It; ++It) { ++Actors; Actor = *It; }
    }
    if (GameWorlds != 1 || Actors != 1 || !IsValid(World) || !World->HasBegunPlay() ||
        World->GetNetMode() != NM_Standalone || !World->GetGameViewport() ||
        GEngine->GameViewport != World->GetGameViewport() || !IsValid(Actor) ||
        Actor->GetClass() != AJadeboundProgressionActor::StaticClass() || !Actor->HasAuthority() ||
        Actor->IsDevelopmentModel() || !Actor->IsProgressionReady())
        return Hold(TEXT("exactly one ready authority standalone nondevelopment actor/game viewport required"));
    const FJadeboundHealthPresentation Health = Actor->GetHealthPresentation();
    if (!Health.bContextAvailable || !Health.Player.IsValid() || !Health.Player->IsLocalController() ||
        !Health.Pawn.IsValid() || Health.Player->GetPawn() != Health.Pawn.Get() ||
        Health.Pawn->GetWorld() != World || !Health.PlayerHealth.bAvailable ||
        !FMath::IsFinite(Health.PlayerHealth.Current) || !FMath::IsFinite(Health.PlayerHealth.Maximum) ||
        Health.PlayerHealth.Current <= 0.0 || Health.PlayerHealth.Maximum <= 0.0 ||
        Health.PlayerHealth.Current > Health.PlayerHealth.Maximum || !Actor->IsMerchantInRange() ||
        !IsValid(Actor->AshwoodSpearMesh))
        return Hold(TEXT("living possessed local V006 hero, merchant range and real spear asset required"));

    FStructProperty* StateProperty = FindFProperty<FStructProperty>(Actor->GetClass(), TEXT("State"));
    FObjectPropertyBase* SaveProperty = FindFProperty<FObjectPropertyBase>(Actor->GetClass(), TEXT("LastCommittedSave"));
    FBoolProperty* BlockedProperty = FindFProperty<FBoolProperty>(Actor->GetClass(), TEXT("bPersistenceBlocked"));
    FBoolProperty* UnsavedProperty = FindFProperty<FBoolProperty>(Actor->GetClass(), TEXT("bUnsavedCandidate"));
    if (!StateProperty || StateProperty->Struct != FJadeboundProgressionState::StaticStruct() ||
        !SaveProperty || !BlockedProperty || !UnsavedProperty)
        return Hold(TEXT("expected typed reflected fields unavailable"));
    const auto Unheld = [&]() {
        return !BlockedProperty->GetPropertyValue_InContainer(Actor) && !UnsavedProperty->GetPropertyValue_InContainer(Actor);
    };
    const auto CommittedMatches = [&](const FJadeboundProgressionState& Expected) {
        const auto* Save = Cast<UJadeboundCharacterSave>(SaveProperty->GetObjectPropertyValue_InContainer(Actor));
        return IsValid(Save) && Save->CharacterId == TEXT("Traveler01") && Save->PendingDrops.IsEmpty() && SameState(Save->State, Expected);
    };
    FJadeboundProgressionState Fresh;
    Fresh.Version = static_cast<int32>(Jadebound::SnapshotVersion);
    Fresh.Slots.SetNum(static_cast<int32>(Jadebound::InventorySlotCount));
    const FJadeboundProgressionState Before = Actor->GetProgressionState();
    if (!Unheld() || !SameState(Before, Fresh) || !CommittedMatches(Fresh))
        return Hold(TEXT("actor and production LastCommittedSave must be complete fresh0/10-coin/Glaive/quest0 states with no pending drops"));
    const FString SavedDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
    const FString InitialBank = FPaths::Combine(SavedDir, TEXT("SaveGames/Jadebound_Traveler01_Progression_V2_A.sav"));
    TArray<FString> Saves;
    IFileManager::Get().FindFilesRecursive(Saves, *UserDir, TEXT("*.sav"), true, false);
    if (!FPaths::IsUnderDirectory(SavedDir, UserDir) || Saves.Num() != 1 || !FPaths::IsSamePath(Saves[0], InitialBank))
        return Hold(TEXT("only the one normal production-created initial A bank may exist inside this QA UserDir"));

    FJadeboundProgressionState Fixture = Fresh;
    Fixture.Revision = 1;
    Fixture.ExactRevision = 1;
    Fixture.Coins = 8;
    for (FJadeboundInventorySlot& Slot : Fixture.Slots) { Slot.ItemId = 2; Slot.Count = 1; }
    GEngine->AddOnScreenDebugMessage(uint64(0x4A425141), 86400.f, FColor::Yellow,
        TEXT("QA FIXTURE - NOT EARNED | Full-capacity boundary test"));
    UE_LOG(LogTemp, Display, TEXT("QA FIXTURE - NOT EARNED: seeding isolated typed state UserDir=%s r1 coins8 twelve Spears equipped Glaive; production save/readback follows"), *UserDir);
    // Supported typed reflection only in this opt-in editor test, never a runtime/client award API.
    StateProperty->SetValue_InContainer(Actor, &Fixture);
    if (!Actor->SaveProgress()) return Hold(TEXT("production SaveProgress failed; candidate/partials retained"));
    if (!Actor->ReloadProgress()) return Hold(TEXT("production ReloadProgress failed; candidate/partials retained"));
    if (!SameState(Actor->GetProgressionState(), Fixture) || !CommittedMatches(Fixture) || !Unheld())
        return Hold(TEXT("complete production roundtrip differs from the typed fixture"));

    // UI buttons preflight capacity. These guarded authority calls separately test the actor/Core rejection.
    const FString FullBagReason(TEXT("Your bag is full. Sell an item or merge a stack first."));
    if (Actor->BuyItem(2, 1) || Actor->GetFeedback().ToString() != FullBagReason ||
        !SameState(Actor->GetProgressionState(), Fixture) || !CommittedMatches(Fixture) || !Unheld())
        return Hold(TEXT("affordable buy did not reject with exact full-bag reason and whole-state rollback"));
    if (Actor->UnequipWeapon() || Actor->GetFeedback().ToString() != FullBagReason ||
        !SameState(Actor->GetProgressionState(), Fixture) || !CommittedMatches(Fixture) || !Unheld())
        return Hold(TEXT("unequip did not reject with exact full-bag reason and whole-state rollback"));
    const FJadeboundHealthPresentation AfterHealth = Actor->GetHealthPresentation();
    if (AfterHealth.Pawn != Health.Pawn || !AfterHealth.PlayerHealth.bAvailable ||
        AfterHealth.PlayerHealth.Current != Health.PlayerHealth.Current || AfterHealth.PlayerHealth.Maximum != Health.PlayerHealth.Maximum)
        return Hold(TEXT("health/possession changed during synchronous fixture setup"));
    AddInfo(FString::Printf(TEXT("QA FIXTURE - NOT EARNED: production roundtrip and two exact full-capacity rollbacks passed in %s. Ordinary one-raider defeat/pending-loot refusal/free-slot/retry and cold reopening remain native tests."), *UserDir));
    return true;
}
#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
