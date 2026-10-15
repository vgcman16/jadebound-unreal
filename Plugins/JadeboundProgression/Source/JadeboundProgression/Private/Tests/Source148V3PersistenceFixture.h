#pragma once
// Included only by the new native test CPP under WITH_DEV_AUTOMATION_TESTS.
#include "Development/DevelopmentIdentity.h"
#include "Domain/FrozenV2Migration.h"
#include "Domain/LegacyToTorsoMigration.h"
#include "Domain/TorsoMigration.h"
#include "Domain/RecipientLedger.h"
#include "Persistence/ProgressionSaveV2.h"
#include "Persistence/ProgressionSaveV3.h"
#include "Persistence/JournalNamespacePaths.h"
#include "Persistence/JournalIO.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include <algorithm>
#include <limits>

struct FJadeboundV3PersistenceTestAccess
{
    static void Identity(AJadeboundProgressionActor& A,const FString& Id,TUniquePtr<IFileHandle> Lease)
    { A.bDevelopmentModel=true; A.ServerCharacterId=Id; A.DevelopmentWriterLease=MoveTemp(Lease); }
    static bool Read(AJadeboundProgressionActor& A,bool Initial=true) { return A.ReadSave(Initial); }
    static bool Write(AJadeboundProgressionActor& A) { return A.WriteSave(); }
    static bool Retry(AJadeboundProgressionActor& A) { return A.RetryHeldSave(false); }
    static void Expire(AJadeboundProgressionActor& A) { A.PersistenceRetryAt=FPlatformTime::Seconds()-1.0; }
    static double Deadline(const AJadeboundProgressionActor& A) { return A.PersistenceRetryAt; }
    static void Opener(AJadeboundProgressionActor& A,TFunction<IFileHandle*(const FString&)> Fn) { A.AutomationJournalOpener=MoveTemp(Fn); }
    static const UJadeboundCharacterSave* Held(const AJadeboundProgressionActor& A) { return A.HeldSaveCandidate.Get(); }
    static const UJadeboundCharacterSave* Committed(const AJadeboundProgressionActor& A) { return A.LastCommittedSave.Get(); }
    static const IFileHandle* Lease(const AJadeboundProgressionActor& A) { return A.DevelopmentWriterLease.Get(); }
    static Jadebound::Snapshot Core(const AJadeboundProgressionActor& A) { return A.Core->GetSnapshot(); }
    static const TArray<FJadeboundSavedDrop>& Drops(const AJadeboundProgressionActor& A) { return A.PendingDrops; }
    static bool Admitted(const AJadeboundProgressionActor& A) { return A.bV3JournalAdmitted; }
    static bool Unready(const AJadeboundProgressionActor& A) { return !A.bReady && !A.bServerModelInitialized; }
    static int32 Slot(const AJadeboundProgressionActor& A) { return A.LastSaveSlot; }
    static const TArray<uint8>& HeldBytes(const AJadeboundProgressionActor& A) { return A.HeldJournalBytes; }
    static const FString& HeldPath(const AJadeboundProgressionActor& A) { return A.HeldJournalPath; }
    static int32 HeldSlot(const AJadeboundProgressionActor& A) { return A.HeldJournalSlot; }
    static bool Encode(UJadeboundCharacterSave* A,TArray<uint8>& Bytes) { return AJadeboundProgressionActor::EncodeJournalForAutomation(A,Bytes); }
    static bool BuyLeaf(AJadeboundProgressionActor& A)
    {
        if (A.bUnsavedCandidate || A.HeldSaveCandidate || A.bPersistenceBlocked || !A.bV3JournalAdmitted) return false;
        // Trusted fixture drives the actual core once; no public gameplay claim.
        const auto R=A.Core->Buy(Jadebound::ItemId::Moonleaf,1);
        return R.Succeeded() && R.Changed && A.FinishTransaction(R,FText::GetEmpty());
    }
    static void End(AJadeboundProgressionActor& A,bool Destroy=true)
    { A.bPersistenceEndingPlay=true; A.AutomationJournalOpener=nullptr; A.DevelopmentWriterLease.Reset(); if(Destroy) A.Destroy(); }
};

namespace JadeboundSource148
{
using Access=FJadeboundV3PersistenceTestAccess;
namespace V1=JadeboundLegacyV1;
namespace V2=JadeboundLegacyV2;
using namespace Jadebound;
using namespace Jadebound::Persistence;
inline bool Same(const Snapshot& A,const Snapshot& B)
{
    if(A.Version!=B.Version || A.Revision!=B.Revision || A.Coins!=B.Coins || A.EquippedWeapon!=B.EquippedWeapon ||
       A.EquippedTorso!=B.EquippedTorso || A.Quest!=B.Quest || A.NextDropId!=B.NextDropId ||
       A.QuestKillDropId!=B.QuestKillDropId || A.UnprocessedDropIds!=B.UnprocessedDropIds) return false;
    for(uint32 I=0;I<InventorySlotCount;++I) if(A.Inventory[I].Id!=B.Inventory[I].Id || A.Inventory[I].Count!=B.Inventory[I].Count) return false;
    return true;
}
inline bool SameDrops(const TArray<FJadeboundSavedDrop>& A,const TArray<FJadeboundSavedDrop>& B)
{
    if(A.Num()!=B.Num()) return false;
    for(int32 I=0;I<A.Num();++I) if(!FJadeboundSavedDrop::StaticStruct()->CompareScriptStruct(&A[I],&B[I],0)) return false;
    return true;
}
inline bool SameReflected(const FJadeboundProgressionState& A,const FJadeboundProgressionState& B)
{ return FJadeboundProgressionState::StaticStruct()->CompareScriptStruct(&A,&B,0); }
inline bool ReadBytes(const FString& Path,TArray<uint8>& Out)
{
    auto& F=FPlatformFileManager::Get().GetPlatformFile(); const int64 Size=F.FileSize(*Path);
    if(Size<0 || Size>1024*1024+32) return false;
    Out.SetNumUninitialized(static_cast<int32>(Size));
    if(Size==0) return true;
    TUniquePtr<IFileHandle> H(F.OpenRead(*Path)); return H && H->Read(Out.GetData(),Size);
}
inline bool Put(const FString& Path,const TArray<uint8>& Bytes)
{
    TUniquePtr<IFileHandle> H(FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*Path,false,false));
    const bool Ok=H && (Bytes.IsEmpty() ? H->Flush(true) : WriteAndFlush(*H,Bytes)); H.Reset(); return Ok;
}
inline V1::Snapshot Legacy(uint64 Revision=20)
{
    V1::Snapshot S; S.Revision=Revision; S.Coins=17; S.EquippedWeapon=V1::ItemId::AshwoodSpear;
    for(auto& Slot:S.Inventory) Slot={V1::ItemId::Moonleaf,1};
    S.Inventory[0]={V1::ItemId::JadeGlaive,1}; S.Inventory[1]={V1::ItemId::IronOre,3}; S.Inventory[11]={V1::ItemId::JadeShard,1};
    S.Quest=V1::QuestState::RaiderDefeated; S.NextDropId=5; S.QuestKillDropId=2;
    S.RecordedDropIds={1,2,3,4}; S.ProcessedDropIds={4,1}; return S;
}
inline TArray<FJadeboundSavedDrop> OriginalDrops()
{
    FJadeboundSavedDrop Ore; Ore.DropId=3; Ore.ItemId=3; Ore.Count=2; Ore.Location=FVector(155,352,51);
    FJadeboundSavedDrop Parcel; Parcel.DropId=2; Parcel.ItemId=5; Parcel.Count=1; Parcel.Location=FVector(152,349,50);
    return {Ore,Parcel};
}
inline bool EncodeLegacy(const V1::Snapshot& S,FJadeboundProgressionState& R)
{
    if(!V1::ProgressionCore::ValidateSnapshot(S).Succeeded()) return false;
    R={}; R.Version=1; R.ExactRevision=S.Revision; R.Revision=static_cast<int64>(std::min(S.Revision,static_cast<uint64>(MAX_int64)));
    R.Coins=S.Coins; R.EquippedWeapon=static_cast<int32>(S.EquippedWeapon); R.QuestState=static_cast<int32>(S.Quest);
    R.NextDropId=S.NextDropId; R.QuestKillDropId=S.QuestKillDropId; R.Slots.SetNum(InventorySlotCount);
    for(uint32 I=0;I<InventorySlotCount;++I) { R.Slots[I].ItemId=static_cast<int32>(S.Inventory[I].Id); R.Slots[I].Count=S.Inventory[I].Count; }
    for(auto Id:S.RecordedDropIds) R.RecordedDropIds.Add(Id);
    for(auto Id:S.ProcessedDropIds) R.ProcessedDropIds.Add(Id);
    V1::Snapshot Check; return DecodeStateV1(R,Check);
}
inline UJadeboundCharacterSave* Fixture(const FString& Id,int32 Version,uint64 Revision=20,bool Torso=false)
{
    auto* Save=Cast<UJadeboundCharacterSave>(UGameplayStatics::CreateSaveGameObject(UJadeboundCharacterSave::StaticClass()));
    if(!Save) return nullptr;
    Save->CharacterId=Id; Save->PendingDrops=OriginalDrops(); const auto Old=Legacy(Revision);
    if(Version==1) return EncodeLegacy(Old,Save->State) ? Save : nullptr;
    V2::Snapshot Two; if(!FrozenV2Migration::ConvertLegacyV1Snapshot(Old,Two).Succeeded()) return nullptr;
    if(Version==2) return EncodeStateV2(Two,Save->State) ? Save : nullptr;
    Snapshot Three; if(Version!=3 || !ConvertLegacyV2Snapshot(Two,Three).Succeeded()) return nullptr;
    if(Torso) Three.EquippedTorso=ItemId::WardenCoat;
    return EncodeStateV3(Three,Save->State) ? Save : nullptr;
}
struct Image
{
    bool File=false,Directory=false,Stat=false; TArray<uint8> Bytes;
};
inline bool Capture(const TArray<FString>& Paths,TArray<Image>& Out)
{
    auto& F=FPlatformFileManager::Get().GetPlatformFile(); Out.Reset();
    for(const auto& P:Paths)
    {
        Image I; I.File=F.FileExists(*P); I.Directory=F.DirectoryExists(*P); I.Stat=F.GetStatData(*P).bIsValid;
        if(I.File && !ReadBytes(P,I.Bytes)) return false; Out.Add(MoveTemp(I));
    }
    return true;
}
inline bool SameImages(const TArray<Image>& A,const TArray<Image>& B)
{
    if(A.Num()!=B.Num()) return false;
    for(int32 I=0;I<A.Num();++I) if(A[I].File!=B[I].File || A[I].Directory!=B[I].Directory || A[I].Stat!=B[I].Stat || A[I].Bytes!=B[I].Bytes) return false;
    return true;
}
inline bool IsIsolatedUserDir(FString& Out)
{
    FString Arg;
    if(!FParse::Param(FCommandLine::Get(),TEXT("JadeboundV3PersistenceTests")) ||
       !FParse::Value(FCommandLine::Get(),TEXT("UserDir="),Arg) || Arg.IsEmpty() || FPaths::IsRelative(Arg)) return false;
    Arg.ReplaceInline(TEXT("\\"),TEXT("/")); TArray<FString> Parts; Arg.ParseIntoArray(Parts,TEXT("/"),true);
    for(const auto& Part:Parts) if(Part==TEXT(".") || Part==TEXT("..")) return false;
    FPaths::NormalizeDirectoryName(Arg); FString Effective=FPaths::ProjectUserDir(); FPaths::NormalizeDirectoryName(Effective);
    const FString Parent=FPaths::GetCleanFilename(FPaths::GetPath(Arg)); const FString Prefix(TEXT("Jadebound-S148-A"));
    const FString Attempt=Parent.Mid(Prefix.Len());
    if(!FPaths::IsSamePath(Arg,Effective) || FPaths::GetCleanFilename(Arg)!=TEXT("UserDir-v001") ||
       !Parent.StartsWith(Prefix,ESearchCase::CaseSensitive) || Attempt.Len()!=3 || Attempt==TEXT("000")) return false;
    for(TCHAR C:Attempt) if(C<TEXT('0') || C>TEXT('9')) return false;
    Out=Arg; return FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()),Out);
}
struct Scope
{
    FAutomationTestBase& Test; FString Id,Directory; JournalNamespacePaths Paths;
    UWorld* World=nullptr; TArray<AJadeboundProgressionActor*> Actors; bool Valid=false;
    Scope(FAutomationTestBase& In,const TCHAR* Name):Test(In)
    {
        FString UserDir;
        if(!IsInGameThread() || !GEngine || !GEngine->WorldSettingsClass || !IsIsolatedUserDir(UserDir))
        { Test.AddError(TEXT("Explicit Source148 flag and fresh isolated UserDir/game thread required.")); return; }
        Id=FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
        if(!BuildJournalNamespacePaths(true,Id,Paths)) { Test.AddError(TEXT("Canonical fixture identity refused.")); return; }
        for(const auto& Pair:Paths.Banks) for(const auto& P:Pair)
            if(!FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(P),UserDir)) { Test.AddError(TEXT("Fixture escaped UserDir.")); return; }
        Directory=FPaths::GetPath(Paths.Banks[2][0]); auto& F=FPlatformFileManager::Get().GetPlatformFile();
        if(F.DirectoryExists(*Directory) || !F.CreateDirectoryTree(*Directory)) { Test.AddError(TEXT("Fresh GUID directory required.")); return; }
        UWorld::InitializationValues V;
        V.InitializeScenes(false).AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
         .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false).SetTransactional(false);
        World=UWorld::CreateWorld(EWorldType::Game,false,FName(*FString::Printf(TEXT("Source148_%s"),*Id)),nullptr,true,ERHIFeatureLevel::Num,&V);
        if(World) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        Valid=World && GEngine->GetWorldContextFromWorld(World) && !World->HasBegunPlay() && !World->bIsTearingDown && World->GetNetMode()==NM_Standalone;
        if(!Valid) Test.AddError(TEXT("Minimal non-BeginPlay authority world unavailable."));
        Test.AddInfo(FString::Printf(TEXT("Synthetic Source148 %s retained: %s"),Name,*Directory));
    }
    ~Scope()
    {
        if(!World) return; UEngine* E=GEngine; const bool CanDestroy=E && E->GetWorldContextFromWorld(World);
        for(auto* A:Actors) if(IsValid(A) && !A->IsActorBeingDestroyed()) Access::End(*A,CanDestroy);
        if(!CanDestroy) { Test.AddError(TEXT("World context lost; leases released, evidence retained.")); return; }
        World->DestroyWorld(false); E->DestroyWorldContext(World);
    }
    AJadeboundProgressionActor* Actor()
    {
        if(!Valid) return nullptr;
        TUniquePtr<IFileHandle> Lease(FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*FPaths::Combine(Directory,TEXT("Character.writer")),true,false));
        if(!Lease) { Test.AddError(TEXT("Exclusive real Windows writer lease unavailable.")); return nullptr; }
        FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* A=World->SpawnActor<AJadeboundProgressionActor>(FVector::ZeroVector,FRotator::ZeroRotator,Spawn);
        if(!A || !A->HasAuthority() || A->GetWorld()!=World || A->GetNetMode()!=NM_Standalone || A->HasActorBegunPlay())
        { Test.AddError(TEXT("Non-BeginPlay native authority actor unavailable.")); return nullptr; }
        Actors.Add(A); Access::Identity(*A,Id,MoveTemp(Lease)); return A;
    }
    bool Seed(int32 Version,int32 Slot,UJadeboundCharacterSave* Save)
    { TArray<uint8> Bytes; return Save && Version>=1 && Version<=3 && Slot>=0 && Slot<=1 && Access::Encode(Save,Bytes) && Put(Paths.Banks[Version-1][Slot],Bytes); }
    TArray<FString> AllPaths() const
    {
        TArray<FString> Out; for(const auto& Pair:Paths.Banks) for(const auto& P:Pair) Out.Add(P);
        Out.Add(Paths.ForwardV4[0]); Out.Add(Paths.ForwardV4[1]); Out.Add(FPaths::Combine(Directory,TEXT("Character.writer"))); return Out;
    }
    bool LeaseExcludes(const AJadeboundProgressionActor& A)
    {
        TUniquePtr<IFileHandle> Other(FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*FPaths::Combine(Directory,TEXT("Character.writer")),true,false));
        return Access::Lease(A) && !Other;
    }
};
enum class Fault { None,Open,Partial,UncertainFlush,CorruptAfterClose,DifferentAfterClose };
struct Trace
{
    Fault Mode=Fault::None; FString Path; TArray<uint8> Payload,Substitute; TArray<FString> Events;
    int32 Opens=0,Closes=0; bool Full=false,RealWrite=false,RealFlush=false,PathRefused=false,MutationOk=true,UnreadyAtEveryOpen=true;
};
class Handle final:public IFileHandle
{
    TUniquePtr<IFileHandle> Inner; Trace& T;
public:
    Handle(IFileHandle* H,Trace& In):Inner(H),T(In) {}
    ~Handle() override
    {
        Inner.Reset(); ++T.Closes; T.Events.Add(TEXT("close"));
        if(T.Mode==Fault::CorruptAfterClose || T.Mode==Fault::DifferentAfterClose)
            T.MutationOk=Put(T.Path,T.Mode==Fault::CorruptAfterClose ? TArray<uint8>{0x4a,0x42,0x50} : T.Substitute);
    }
    int64 Tell() override { return Inner->Tell(); }
    bool Seek(int64 P) override { return Inner->Seek(P); }
    bool SeekFromEnd(int64 P=0) override { return Inner->SeekFromEnd(P); }
    bool Read(uint8* P,int64 N) override { return Inner->Read(P,N); }
    bool ReadAt(uint8* P,int64 N,int64 O) override { return Inner->ReadAt(P,N,O); }
    bool Truncate(int64 N) override { return Inner->Truncate(N); }
    bool Write(const uint8* Data,int64 Count) override
    {
        T.Events.Add(TEXT("write")); if(!Data || Count<=0 || Count>1024*1024+32) return false;
        T.Payload.Reset(); T.Payload.Append(Data,static_cast<int32>(Count));
        T.RealWrite=Inner->Write(Data,T.Mode==Fault::Partial ? FMath::Min<int64>(8,Count) : Count);
        return T.RealWrite && T.Mode!=Fault::Partial;
    }
    bool Flush(const bool Full=false) override
    { T.Events.Add(TEXT("flush")); T.Full=Full; T.RealFlush=Inner->Flush(Full); return T.RealFlush && T.Mode!=Fault::UncertainFlush; }
};
inline void Install(AJadeboundProgressionActor& A,Trace& T)
{
    Access::Opener(A,[&T,&A](const FString& Path)->IFileHandle* {
        ++T.Opens; T.Events.Add(TEXT("open")); T.UnreadyAtEveryOpen=T.UnreadyAtEveryOpen && Access::Unready(A);
        if(!FPaths::IsSamePath(Path,T.Path)) { T.PathRefused=true; return nullptr; }
        if(T.Mode==Fault::Open) return nullptr;
        auto* Real=FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*Path,false,false); return Real ? new Handle(Real,T) : nullptr;
    });
}
}
