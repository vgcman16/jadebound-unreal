#include "JadeboundDevelopmentGameMode.h"
#include "Development/DevelopmentIdentity.h"
#include "Development/DevelopmentResumeOptions.h"
#include "Development/DevelopmentResumeRegistry.h"
#include "Engine/NetConnection.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Guid.h"

namespace
{
// This is an engine-assigned controller object identifier, not a client field.
uint64 ResumeConnection(const APlayerController* Controller)
{
    // Logout/cancellation may receive a pending-kill controller before GC.
    // Its engine identifier remains usable during this borrowed call lifetime.
    return Controller ? static_cast<uint64>(Controller->GetUniqueID()) + 1 : 0;
}
bool IsResumeContext(const AJadeboundDevelopmentGameMode* Mode, const APlayerController* Controller)
{
    const UWorld* World = Mode ? Mode->GetWorld() : nullptr;
    if (!IsInGameThread() || !IsValid(Mode) || !Mode->HasAuthority() || Mode->GetNetMode() == NM_Client ||
        !World || !World->IsGameWorld() || World->bIsTearingDown || Mode->IsActorBeingDestroyed() ||
        !IsValid(Controller) || Controller->IsActorBeingDestroyed() || Controller->GetWorld() != World) return false;
    return true;
}
bool IsResumeController(const AJadeboundDevelopmentGameMode* Mode, const APlayerController* Controller)
{
    if (!IsResumeContext(Mode, Controller)) return false;
    if (Controller->IsLocalController()) return Cast<ULocalPlayer>(Controller->Player) != nullptr;
    UNetConnection* Connection = Controller->GetNetConnection();
    return Connection && Jadebound::Development::IsLoopbackAddress(Connection->LowLevelGetRemoteAddress(false));
}
std::string ResumeUtf8(const FString& Value)
{
    const FTCHARToUTF8 Converted(*Value, Value.Len());
    return std::string(Converted.Get(), static_cast<size_t>(Converted.Length()));
}
}

void FJadeboundResumeRegistryDeleter::operator()(Jadebound::Development::ResumeRegistry* Pointer) const
{
    delete Pointer;
}
AJadeboundDevelopmentGameMode::~AJadeboundDevelopmentGameMode() = default;

bool AJadeboundDevelopmentGameMode::EnsureResumeRegistry(FString& Error)
{
    if (!ResumeAdmissions || !Jadebound::Development::IsCharacterId(ResumeSessionId))
    { Error = TEXT("Local development admission state is unavailable."); return false; }
    const auto& Image = ResumeAdmissions->Image();
    if (Image.Configured)
    {
        if (Image.Seats.size() != static_cast<size_t>(DevelopmentCharacterRoster.Num()))
        { Error = TEXT("The development roster cannot change during a server session."); return false; }
        for (int32 Index = 0; Index < DevelopmentCharacterRoster.Num(); ++Index)
            if (Image.Seats[static_cast<size_t>(Index)].CharacterId != ResumeUtf8(DevelopmentCharacterRoster[Index]))
            { Error = TEXT("The development roster cannot change during a server session."); return false; }
        return true;
    }
    std::vector<std::string> Roster;
    Roster.reserve(DevelopmentCharacterRoster.Num());
    for (const FString& Id : DevelopmentCharacterRoster) Roster.push_back(ResumeUtf8(Id));
    if (!ResumeAdmissions->Configure(Roster).Succeeded())
    { Error = TEXT("The local development profile roster is invalid."); return false; }
    return true;
}

APlayerController* AJadeboundDevelopmentGameMode::Login(UPlayer* NewPlayer, ENetRole InRemoteRole,
    const FString& Portal, const FString& Options, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
    ErrorMessage.Reset();
    const UWorld* World = GetWorld();
    if (!IsInGameThread() || !HasAuthority() || GetNetMode() == NM_Client || !World || !World->IsGameWorld() ||
        World->bIsTearingDown || IsActorBeingDestroyed() || !IsValid(NewPlayer))
    { ErrorMessage = TEXT("Local development login context is unavailable."); return nullptr; }
    if (!Cast<ULocalPlayer>(NewPlayer))
    {
        UNetConnection* Connection = Cast<UNetConnection>(NewPlayer);
        if (!Connection || !Jadebound::Development::IsLoopbackAddress(Connection->LowLevelGetRemoteAddress(false)))
        { ErrorMessage = TEXT("Only a loopback development connection is admitted."); return nullptr; }
    }
    if (!IsConfigurationValid(ErrorMessage) || !IsWorldCompatible(ErrorMessage)) return nullptr;
    // At this point UPlayer is supplied by the engine. The new controller's
    // Player/NetConnection is assigned only after Super::Login returns.
    return Super::Login(NewPlayer, InRemoteRole, Portal, Options, UniqueId, ErrorMessage);
}

FString AJadeboundDevelopmentGameMode::InitNewPlayer(APlayerController* Controller,
    const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal)
{
    FString Error;
    if (!IsResumeContext(this, Controller)) return TEXT("Local development admission context is unavailable.");
    if (!IsConfigurationValid(Error) || !IsWorldCompatible(Error) || !EnsureResumeRegistry(Error)) return Error;
    if (Options.Len() > 4096) return TEXT("Invalid local development resume option.");
    const auto Parsed = Jadebound::Development::ParseResumeOption(ResumeUtf8(Options));
    if (!Parsed.Valid) return TEXT("Invalid local development resume option.");
    const std::string Fresh = Parsed.Present ? std::string() : ResumeUtf8(FGuid::NewGuid().ToString(EGuidFormats::Digits));
    const auto Reserved = ResumeAdmissions->Reserve(ResumeConnection(Controller), Parsed.Token, Fresh);
    if (!Reserved.Succeeded())
        return TEXT("Local development admission refused. Use this server session's issued resume ticket; disconnected profiles stay reserved.");
    Error = Super::InitNewPlayer(Controller, UniqueId, Options, Portal);
    if (Error.IsEmpty() && !IsResumeContext(this, Controller)) Error = TEXT("Local development controller was lost during initialization.");
    if (!Error.IsEmpty()) CancelPendingResume(Controller);
    return Error;
}

bool AJadeboundDevelopmentGameMode::GetPendingResume(APlayerController* Controller, FString& CharacterId) const
{
    CharacterId.Reset();
    if (!IsResumeController(this, Controller) || !ResumeAdmissions) return false;
    const uint64 Connection = ResumeConnection(Controller);
    for (const auto& Seat : ResumeAdmissions->Image().Seats)
    {
        if (Seat.Connection != Connection || Seat.State != Jadebound::Development::ResumeSeatState::Pending) continue;
        const FString Id(UTF8_TO_TCHAR(Seat.CharacterId.c_str()));
        if (!DevelopmentCharacterRoster.Contains(Id)) return false;
        CharacterId = Id;
        return true;
    }
    return false;
}
bool AJadeboundDevelopmentGameMode::ActivateResume(APlayerController* Controller, FString& Ticket)
{
    Ticket.Reset();
    FString Error;
    if (!IsResumeController(this, Controller) || !EnsureResumeRegistry(Error)) return false;
    const auto Result = ResumeAdmissions->Activate(ResumeConnection(Controller));
    if (!Result.Succeeded()) return false;
    Ticket = UTF8_TO_TCHAR(Result.Token.c_str());
    return true;
}
void AJadeboundDevelopmentGameMode::CancelPendingResume(APlayerController* Controller)
{
    if (IsInGameThread() && ResumeAdmissions && ResumeConnection(Controller) != 0)
        ResumeAdmissions->CancelPending(ResumeConnection(Controller));
}
void AJadeboundDevelopmentGameMode::DisconnectResume(APlayerController* Controller)
{
    if (IsInGameThread() && ResumeAdmissions && ResumeConnection(Controller) != 0)
        ResumeAdmissions->Disconnect(ResumeConnection(Controller));
}
