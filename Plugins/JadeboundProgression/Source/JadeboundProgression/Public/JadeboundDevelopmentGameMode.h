#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "JadeboundDevelopmentGameMode.generated.h"

class AJadeboundProgressionActor;
class UStaticMesh;
class UAnimMontage;
class AJadeboundNativeRaider;
namespace Jadebound::Development { class ResumeRegistry; }
struct FJadeboundResumeRegistryDeleter { void operator()(Jadebound::Development::ResumeRegistry* Pointer) const; };

// Opt-in local development only. This is not an authenticated account service.
UCLASS(Blueprintable, Config=JadeboundDevelopment)
class JADEBOUNDPROGRESSION_API AJadeboundDevelopmentGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AJadeboundDevelopmentGameMode();
    virtual ~AJadeboundDevelopmentGameMode() override;
    virtual void StartPlay() override;
    virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
    virtual void PostLogin(APlayerController* NewPlayer) override;
    virtual APlayerController* Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal,
        const FString& Options, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
    virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId,
        const FString& Options, const FString& Portal = TEXT("")) override;
    virtual void Logout(AController* Exiting) override;
    virtual void RestartPlayer(AController* NewPlayer) override;

    UPROPERTY(Config, EditDefaultsOnly, Category="Jadebound|Development") bool bEnableLocalDevelopmentFoundation = false;
    UPROPERTY(Config, EditDefaultsOnly, Category="Jadebound|Development") TArray<FString> DevelopmentCharacterRoster;
    UPROPERTY(EditDefaultsOnly, Category="Jadebound|Development") TObjectPtr<UStaticMesh> GlaiveMesh;
    UPROPERTY(EditDefaultsOnly, Category="Jadebound|Development") TObjectPtr<UStaticMesh> SpearMesh;
    UPROPERTY(EditDefaultsOnly, Category="Jadebound|Development") TObjectPtr<UStaticMesh> IronOreMesh;
    UPROPERTY(EditDefaultsOnly, Category="Jadebound|Development") TObjectPtr<UStaticMesh> ParcelMesh;
    UPROPERTY(EditDefaultsOnly, Category="Jadebound|Development") FVector DevelopmentMerchantPoint = FVector(-344.974746831,477.279220614,92.15);
    UPROPERTY(EditDefaultsOnly, Category="Jadebound|Development") float DevelopmentMerchantRange = 200.f;

    // Inactive local development slice. Assets and independent clients must
    // be qualified before activation; this does not enable public hosting.
    UPROPERTY(EditDefaultsOnly, Category="Jadebound|RecipientCombat") bool bEnableRecipientCombat = false;
    UPROPERTY(EditDefaultsOnly, Category="Jadebound|RecipientCombat") TSubclassOf<AJadeboundNativeRaider> NativeRaiderClass;
    UPROPERTY(EditDefaultsOnly, Category="Jadebound|RecipientCombat") TObjectPtr<UAnimMontage> NativeAttackMontage;
    UPROPERTY(EditDefaultsOnly, Category="Jadebound|RecipientCombat") FTransform NativeRaiderTransform = FTransform(FRotator(0,-45,0),FVector(-26.777,31.802,92.15));

    // Server-local recovery only; no client RPC or caller-supplied award identity.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Jadebound|Development") bool RetryRetainedCharacter(const FString& ServerConfiguredCharacterId);
private:
    friend class AJadeboundProgressionActor;
    friend class AJadeboundNativeRaider;
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FJadeboundMerchantBulkSaleTestAccess;
    friend struct FJadeboundAttackWindowTestAccess;
    friend struct FJadeboundRecipientNativeTestAccess;
#endif
    UPROPERTY(Transient) TObjectPtr<AJadeboundNativeRaider> RecipientRaider;
    AJadeboundNativeRaider* GetRecipientRaider() const;
    bool OwnsRecipientModel(const AJadeboundProgressionActor* Model, bool bRequireLivingOwner) const;
    bool TryCommitRaiderDeath(AJadeboundNativeRaider* Enemy);
    bool AreRecipientSwingsIdle() const;
    UPROPERTY(Transient) TMap<FString, TObjectPtr<AJadeboundProgressionActor>> Models;
    UPROPERTY(Transient) TMap<TObjectPtr<APlayerController>, FString> ActiveControllers;
    // Same-server-session local development identity. Logout releases only the
    // connection, never the ticket's character. This is not account authentication.
    TUniquePtr<Jadebound::Development::ResumeRegistry, FJadeboundResumeRegistryDeleter> ResumeAdmissions;
    FString ResumeSessionId;
    bool EnsureResumeRegistry(FString& Error);
    bool GetPendingResume(APlayerController* Controller, FString& CharacterId) const;
    bool ActivateResume(APlayerController* Controller, FString& Ticket);
    void CancelPendingResume(APlayerController* Controller);
    void DisconnectResume(APlayerController* Controller);
    bool IsConfigurationValid(FString& Error) const;
    bool IsWorldCompatible(FString& Error) const;
    void RejectController(APlayerController* Controller, const FString& Reason);
};
