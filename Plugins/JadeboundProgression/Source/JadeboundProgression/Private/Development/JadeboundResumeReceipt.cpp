#include "JadeboundProgressionActor.h"
#include "Development/DevelopmentIdentity.h"
#include "Development/ResumeReceiptIO.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"

void AJadeboundProgressionActor::ClientDevelopmentResumeTicket_Implementation(
    const FString& SessionId, const FString& CharacterId, const FString& Ticket)
{
#if !UE_BUILD_SHIPPING
    // Owner RPC arrives before all initial model properties necessarily do.
    // Check the actual local owner/world, not readiness or a replicated HP value.
    const UWorld* World = GetWorld();
    const APlayerController* Controller = Cast<APlayerController>(GetOwner());
    if (!IsInGameThread() || !World || !World->IsGameWorld() || World->bIsTearingDown || IsActorBeingDestroyed() ||
        !IsValid(Controller) || !Controller->IsLocalController() || Controller->GetWorld() != World ||
        !Jadebound::Development::IsCharacterId(SessionId) || !Jadebound::Development::IsCharacterId(CharacterId) ||
        !Jadebound::Development::IsCharacterId(Ticket) || Ticket == CharacterId) return;
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("JadeboundDevelopmentResume"));
    const FString Path = FPaths::Combine(Directory, SessionId + TEXT("_") + CharacterId + TEXT(".ini"));
    const FString Receipt = TEXT("[LocalDevelopmentResume]\nVersion=1\nSession=") + SessionId +
        TEXT("\nCharacter=") + CharacterId + TEXT("\nTicket=") + Ticket + TEXT("\n");
    const auto Result = Jadebound::Development::CreateResumeReceipt(Path, Receipt);
    if (Result != Jadebound::Development::ReceiptResult::Created && Result != Jadebound::Development::ReceiptResult::AlreadyMatched)
        UE_LOG(LogTemp, Warning, TEXT("Jadebound local resume receipt could not be confirmed; existing files were preserved and reconnect qualification is required."));
#endif
}
