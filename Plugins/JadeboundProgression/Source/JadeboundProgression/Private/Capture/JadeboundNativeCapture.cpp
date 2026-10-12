// SOURCE-ONLY additive draft. Merge into the root-selected corrected controls
// candidate; never replace its complete Actor.cpp/h with old source034.
#include "JadeboundNativeCapture.h"
#include "JadeboundProgressionActor.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "UObject/Package.h"
#include "Containers/StringConv.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "Widgets/SViewport.h"
#include "UnrealClient.h"

namespace
{
constexpr uint64 MiB = 1024ull * 1024ull;
constexpr uint64 GiB = 1024ull * MiB;
constexpr uint64 ByteLimit = 256ull * MiB;
constexpr uint64 FrameReserve = 5ull * MiB; // SDR <=1280x720 + PNG overhead.
constexpr uint64 LogReserve = MiB;
constexpr uint64 DiskHeadroom = 512ull * MiB;
constexpr int32 MaxWidth = 1280;
constexpr int32 MaxHeight = 720;
constexpr double Interval = .2; // At most 5 requests/sec; no catch-up loop.

bool IsHex(TCHAR Ch)
{
    return (Ch >= TEXT('0') && Ch <= TEXT('9')) ||
        (Ch >= TEXT('a') && Ch <= TEXT('f')) || (Ch >= TEXT('A') && Ch <= TEXT('F'));
}

bool PlainExistingAncestors(const FString& Path)
{
    // Installed WindowsPlatformFile::IsSymlink rejects ANY reparse point,
    // including junctions. Unimplemented other-platform answers also hold.
    IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
    FString Current = Path;
    while (Current.Len() > 3)
    {
        if (!Files.DirectoryExists(*Current) || Files.IsSymlink(*Current) != ESymlinkResult::NonSymlink) return false;
        const FString Parent = FPaths::GetPath(Current);
        if (Parent == Current) return false;
        Current = Parent;
    }
    return Current.Equals(TEXT("C:"), ESearchCase::IgnoreCase) || Current.Equals(TEXT("C:/"), ESearchCase::IgnoreCase);
}
}

FJadeboundNativeCapture::FJadeboundNativeCapture(AJadeboundProgressionActor* InOwner) : Owner(InOwner) {}
FJadeboundNativeCapture::~FJadeboundNativeCapture() { Stop(TEXT("HelperDestroyed")); }

bool FJadeboundNativeCapture::ReadCommand()
{
    bChecked = true; CheckedAt = FPlatformTime::Seconds();
    if (!FParse::Value(FCommandLine::Get(), TEXT("JadeboundHUDCapture="), Directory)) return false;
#if !PLATFORM_WINDOWS
    return false;
#endif
    if (!FParse::Value(FCommandLine::Get(), TEXT("JadeboundCaptureRun="), RunID) ||
        RunID.Len() != 4 || !RunID.StartsWith(TEXT("M0")) || !FChar::IsDigit(RunID[2]) || !FChar::IsDigit(RunID[3]) || RunID == TEXT("M000")) return false;
    FParse::Value(FCommandLine::Get(), TEXT("JadeboundHUDFrames="), FrameLimit);
    if (FrameLimit < 1 || FrameLimit > 150 || FPaths::IsRelative(Directory)) return false;
    FPaths::NormalizeDirectoryName(Directory);
    if (!FPaths::CollapseRelativeDirectories(Directory)) return false;
    const FString Prefix = TEXT("ActualGameplayFrames-");
    const FString Sequence = FPaths::GetCleanFilename(Directory);
    if (!Sequence.StartsWith(Prefix) || Sequence.Len() != Prefix.Len() + 32) return false;
    for (int32 Index = Prefix.Len(); Index < Sequence.Len(); ++Index) if (!IsHex(Sequence[Index])) return false;
    const FString AllowedRoot = FPaths::Combine(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()), FString::Printf(TEXT("Jadebound-Standalone-Acceptance-%s-v001"), *RunID));
    if (!Directory.Equals(FPaths::Combine(AllowedRoot, Sequence), ESearchCase::IgnoreCase) || !PlainExistingAncestors(Directory)) return false;
    TArray<FString> Existing;
    IFileManager::Get().FindFiles(Existing, *FPaths::Combine(Directory, TEXT("*")), true, true);
    return Existing.IsEmpty(); // Root creates a fresh empty, non-reparse sequence.
}

bool FJadeboundNativeCapture::IsContext() const
{
    const AJadeboundProgressionActor* Actor = Owner.Get();
    const UWorld* World = Actor ? Actor->GetWorld() : nullptr;
    const APlayerController* Player = Actor ? Cast<APlayerController>(Actor->GetOwner()) : nullptr;
    if (!IsValid(Actor) || Actor->IsActorBeingDestroyed() || !World || World->WorldType != EWorldType::Game ||
        World->GetNetMode() != NM_Standalone || !Actor->HasAuthority() || Actor->IsDevelopmentModel() ||
        World->GetNumPlayerControllers() != 1 || World->GetOutermost()->GetName() != TEXT("/Game/Jadebound/Playable/AreaV002/L_JadeboundProgression_AreaV002") ||
        !IsValid(Player) || !Player->IsLocalController() || Player->GetClass()->GetPathName() != TEXT("/Game/Jadebound/Tests/GlaiveCombatV006/BP_GlaiveController_V006.BP_GlaiveController_V006_C")) return false;
    const AGameModeBase* Mode = World->GetAuthGameMode();
    if (!IsValid(Mode) || Mode->GetClass()->GetPathName() != TEXT("/Game/Jadebound/Tests/GlaiveCombatV006/BP_GlaiveMode_V006.BP_GlaiveMode_V006_C")) return false;
    const APawn* Pawn = Player->GetPawn();
    // A missing pawn is allowed to observe actual death/respawn; absence is not0HP.
    if (Pawn && (!IsValid(Pawn) || Pawn->GetController() != Player || Pawn->GetClass()->GetPathName() != TEXT("/Game/Jadebound/Tests/GlaiveCombatV006/BP_GlaiveHero_V006.BP_GlaiveHero_V006_C"))) return false;
    int32 Count = 0;
    for (TActorIterator<AJadeboundProgressionActor> It(World); It; ++It) if (!It->IsActorBeingDestroyed()) ++Count;
    return Count == 1 && World->GetGameViewport() && World->GetGameViewport()->Viewport;
}

bool FJadeboundNativeCapture::CheckRequestBounds(FString& Reason)
{
    if (!PlainExistingAncestors(Directory)) { Reason = TEXT("OutputAncestorChanged"); return false; }
    if (ImageBytes + LogBytes + FrameReserve + LogReserve > ByteLimit) { Reason = TEXT("ImageByteQuota"); return false; }
    uint64 Total = 0;
    if (!FPlatformMisc::GetDiskTotalAndFreeSpace(Directory, Total, LastDiskFree) || LastDiskFree < DiskHeadroom + FrameReserve + LogReserve)
    { Reason = TEXT("DiskHeadroom"); return false; }
    const FPlatformMemoryStats Memory = FPlatformMemory::GetStats();
    LastAvailablePhysical = Memory.AvailablePhysical;
    LastAvailableVirtualOrCommit = Memory.AvailableVirtual; // Windows: min(address-space, commit headroom).
    if (LastAvailablePhysical < GiB || LastAvailableVirtualOrCommit < 4ull * GiB) { Reason = TEXT("MemoryHeadroom"); return false; }
    const AJadeboundProgressionActor* Actor = Owner.Get();
    UGameViewportClient* Viewport = Actor && Actor->GetWorld() ? Actor->GetWorld()->GetGameViewport() : nullptr;
    if (!Viewport || !Viewport->Viewport || !FSlateApplication::IsInitialized()) { Reason = TEXT("ViewportUnavailable"); return false; }
    if (Viewport->Viewport->GetSceneHDREnabled() || Viewport->Viewport->IsHDRViewport()) { Reason = TEXT("HDRUnsupported"); return false; }
    if (UGameViewportClient::OnScreenshotCaptured().IsBound()) { Reason = TEXT("ForeignPixelDelegate"); return false; }
    const FIntPoint NativeSize = Viewport->Viewport->GetSizeXY();
    const TSharedPtr<SViewport> Widget = Viewport->GetGameViewportWidget();
    FWidgetPath Path;
    if (!Widget.IsValid() || !FSlateApplication::Get().GeneratePathToWidgetUnchecked(Widget.ToSharedRef(), Path))
    { Reason = TEXT("SlateViewportUnavailable"); return false; }
    const TOptional<FArrangedWidget> Arranged = Path.FindArrangedWidget(Widget.ToSharedRef());
    if (!Arranged.IsSet()) { Reason = TEXT("SlateViewportUnarranged"); return false; }
    const FVector2f DrawSize = Arranged.GetValue().Geometry.GetDrawSize();
    if (!FMath::IsFinite(DrawSize.X) || !FMath::IsFinite(DrawSize.Y) || DrawSize.X < 1.f || DrawSize.Y < 1.f || DrawSize.X > MaxWidth || DrawSize.Y > MaxHeight)
    { Reason = TEXT("SlatePixelLimit"); return false; }
    const int32 Width = static_cast<int32>(DrawSize.X), Height = static_cast<int32>(DrawSize.Y);
    if (NativeSize.X < 1 || NativeSize.Y < 1 || NativeSize.X > MaxWidth || NativeSize.Y > MaxHeight ||
        Width != NativeSize.X || Height != NativeSize.Y || uint64(Width) * uint64(Height) > uint64(MaxWidth) * uint64(MaxHeight))
    { Reason = TEXT("NativePixelLimitOrDPIMismatch"); return false; }
    if (Completed > 0 && (Width != ExpectedWidth || Height != ExpectedHeight)) { Reason = TEXT("ResolutionChanged"); return false; }
    ExpectedWidth = Width; ExpectedHeight = Height;
    return true;
}

bool FJadeboundNativeCapture::Log(const TCHAR* Event, const TCHAR* Reason, int64 Bytes)
{
    if (!PlainExistingAncestors(Directory)) return false;
    const FString Filename = FPaths::Combine(Directory, TEXT("Capture-v001.jsonl"));
    IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
    if (Files.IsSymlink(*Filename) != ESymlinkResult::NonSymlink ||
        (bLogCreated && Files.FileSize(*Filename) != int64(LogBytes))) return false;
    auto Row = MakeShared<FJsonObject>();
    Row->SetStringField(TEXT("Schema"), TEXT("JadeboundNativeCaptureAdditiveDraft-v001"));
    Row->SetStringField(TEXT("Event"), Event); Row->SetStringField(TEXT("Reason"), Reason);
    Row->SetStringField(TEXT("Run"), RunID); Row->SetStringField(TEXT("UTC"), FDateTime::UtcNow().ToIso8601());
    Row->SetNumberField(TEXT("MonotonicSeconds"), FPlatformTime::Seconds());
    const AJadeboundProgressionActor* Actor = Owner.Get();
    const UWorld* World = Actor ? Actor->GetWorld() : nullptr;
    Row->SetNumberField(TEXT("WorldSeconds"), World ? World->GetTimeSeconds() : 0.0);
    Row->SetNumberField(TEXT("RequestMonotonicSeconds"), RequestAt);
    Row->SetNumberField(TEXT("RequestWorldSeconds"), RequestWorldAt);
    Row->SetStringField(TEXT("Filename"), FinalPath); Row->SetStringField(TEXT("EngineStagingPath"), PendingPath);
    Row->SetNumberField(TEXT("Attempts"), Attempts); Row->SetNumberField(TEXT("Completed"), Completed);
    Row->SetNumberField(TEXT("FrameLimit"), FrameLimit); Row->SetBoolField(TEXT("Pending"), bPending);
    Row->SetNumberField(TEXT("Bytes"), double(Bytes)); Row->SetNumberField(TEXT("CumulativeImageBytes"), double(ImageBytes));
    Row->SetNumberField(TEXT("CaptureByteLimit"), double(ByteLimit));
    Row->SetNumberField(TEXT("LogBytesBeforeThisEvent"), double(LogBytes));
    Row->SetNumberField(TEXT("CaptureBytesBeforeThisEvent"), double(ImageBytes + LogBytes));
    Row->SetNumberField(TEXT("Width"), ExpectedWidth); Row->SetNumberField(TEXT("Height"), ExpectedHeight);
    Row->SetNumberField(TEXT("AvailablePhysicalBytes"), double(LastAvailablePhysical));
    Row->SetNumberField(TEXT("AvailableVirtualOrCommitBytes"), double(LastAvailableVirtualOrCommit));
    Row->SetNumberField(TEXT("AvailableDiskBytes"), double(LastDiskFree));
    Row->SetBoolField(TEXT("ViewportOnlyWithUI"), true); Row->SetBoolField(TEXT("CaptureChangesTimestep"), false);
    Row->SetBoolField(TEXT("FullPNGDecodePassed"), false); Row->SetBoolField(TEXT("VideoOrFeatureAccepted"), false);
    const FJadeboundHealthPresentation Health = Actor ? Actor->GetHealthPresentation() : FJadeboundHealthPresentation();
    Row->SetStringField(TEXT("Pawn"), Health.Pawn.IsValid() ? Health.Pawn->GetPathName() : FString());
    Row->SetBoolField(TEXT("HealthAvailable"), Health.PlayerHealth.bAvailable);
    if (Health.PlayerHealth.bAvailable)
    {
        Row->SetNumberField(TEXT("CurrentHP"), Health.PlayerHealth.Current);
        Row->SetNumberField(TEXT("MaxHP"), Health.PlayerHealth.Maximum);
    }
    else
    {
        Row->SetField(TEXT("CurrentHP"), MakeShared<FJsonValueNull>());
        Row->SetField(TEXT("MaxHP"), MakeShared<FJsonValueNull>());
    }
    FString Line;
    const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Line);
    if (!FJsonSerializer::Serialize(Row, Writer)) return false;
    const FString Record = Line + TEXT("\n");
    const FTCHARToUTF8 Encoded(*Record);
    const uint64 RecordBytes = uint64(Encoded.Length());
    if (RecordBytes > 4096 || LogBytes + RecordBytes > LogReserve || ImageBytes + LogBytes + RecordBytes > ByteLimit) return false;
    if (!FFileHelper::SaveStringToFile(Record, *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
        &IFileManager::Get(), bLogCreated ? FILEWRITE_Append : FILEWRITE_NoReplaceExisting)) return false;
    LogBytes += RecordBytes; bLogCreated = true;
    return true;
}

void FJadeboundNativeCapture::Stop(const TCHAR* Reason)
{
    if (bActive) Log(TEXT("Stopped"), Reason);
    bActive = false; bFinished = true;
    if (ProcessedHandle.IsValid()) FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ProcessedHandle);
    ProcessedHandle.Reset();
    // No globalReset, delete, retry, fake Completed or movie acceptance. An
    // in-flight .pending.png may still finish; preserve it uncounted.
}

void FJadeboundNativeCapture::Tick()
{
    if (bFinished) return;
    if (!bChecked)
    {
        bEnabled = ReadCommand();
        if (!bEnabled) { bFinished = true; return; }
    }
    const double Now = FPlatformTime::Seconds();
    if (!FMath::IsFinite(Now)) { Stop(TEXT("InvalidClock")); return; }
    if (!bActive)
    {
        // Root must be ready to demonstrate ordinary inputs at startup. Up to
        // five seconds only for validowner/context; no reactivation afterstop.
        if (!IsContext())
        {
            if (Now - CheckedAt >= 5.0) Stop(TEXT("InitialContextTimeout"));
            return;
        }
        FString Reason;
        if (!CheckRequestBounds(Reason)) { UE_LOG(LogTemp, Warning, TEXT("Jadebound capture rejected: %s"), *Reason); Stop(*Reason); return; }
        bActive = true; StartedAt = NextAt = Now;
        ProcessedHandle = FScreenshotRequest::OnScreenshotRequestProcessed().AddRaw(this, &FJadeboundNativeCapture::OnProcessed);
        if (!Log(TEXT("Started"), TEXT("OptIn_5Hz_Max150_Max30s_NoInputInjection"))) { Stop(TEXT("LogWriteFailed")); return; }
    }
    if (Now - StartedAt >= 30.0) { Stop(TEXT("WallTimeoutAtTick")); return; }
    if (!IsContext()) { Stop(TEXT("ContextLost")); return; }
    if (GIsDumpingMovie || GIsHighResScreenshot) { Stop(TEXT("ForeignCaptureMode")); return; }
    if (bPending)
    {
        if (FScreenshotRequest::IsScreenshotRequested() && FScreenshotRequest::GetFilename() != PendingPath) Stop(TEXT("ForeignRequestReplacement"));
        else if (Now - RequestAt >= 5.0) Stop(TEXT("PendingTimeoutAtTick"));
        return;
    }
    if (Attempts >= FrameLimit) { Stop(TEXT("AttemptLimit")); return; }
    if (Now < NextAt) return;
    if (FScreenshotRequest::IsScreenshotRequested()) { Stop(TEXT("ForeignRequestPending")); return; }
    FString Reason;
    if (!CheckRequestBounds(Reason)) { Stop(*Reason); return; }
    FinalPath = FPaths::Combine(Directory, FString::Printf(TEXT("ActualHUD.%06d.png"), Attempts));
    PendingPath = FPaths::Combine(Directory, FString::Printf(TEXT("ActualHUD.%06d.pending.png"), Attempts));
    IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
    if (Files.FileExists(*FinalPath) || Files.FileExists(*PendingPath) ||
        Files.IsSymlink(*FinalPath) != ESymlinkResult::NonSymlink || Files.IsSymlink(*PendingPath) != ESymlinkResult::NonSymlink)
    { Stop(TEXT("ExistingOrLinkedFrame")); return; }
    ++Attempts; bPending = true; RequestAt = Now;
    RequestWorldAt = Owner->GetWorld()->GetTimeSeconds(); NextAt = Now + Interval;
    if (!Log(TEXT("Requested"), TEXT("ActualViewportUI_RequestTimeIsLowerBound"))) { Stop(TEXT("LogWriteFailed")); return; }
    FScreenshotRequest::RequestScreenshot(PendingPath, true, false, false, FIntRect(), true);
}

void FJadeboundNativeCapture::OnProcessed()
{
    if (!bActive || !bPending) return;
    if (FPlatformTime::Seconds() - StartedAt >= 30.0) { Stop(TEXT("WallTimeoutAtCallback")); return; }
    if (!IsContext() || !PlainExistingAncestors(Directory)) { Stop(TEXT("CallbackContextOrPathLost")); return; }
    IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
    const int64 Bytes = Files.FileSize(*PendingPath);
    if (Files.IsSymlink(*PendingPath) != ESymlinkResult::NonSymlink || Bytes <= 24 || uint64(Bytes) > FrameReserve || ImageBytes + LogBytes + uint64(Bytes) > ByteLimit)
    { Stop(TEXT("MissingOversizedOrQuotaPNG")); return; }
    TUniquePtr<IFileHandle> Input(Files.OpenRead(*PendingPath));
    uint8 Header[24] = {}; const uint8 Signature[8] = {137,80,78,71,13,10,26,10};
    if (!Input || !Input->Read(Header, 24) || FMemory::Memcmp(Header, Signature, 8) ||
        Header[8] || Header[9] || Header[10] || Header[11] != 13 || FMemory::Memcmp(Header + 12, "IHDR", 4))
    { Stop(TEXT("InvalidPNGHeader")); return; }
    const uint32 Width = (uint32(Header[16]) << 24) | (uint32(Header[17]) << 16) | (uint32(Header[18]) << 8) | Header[19];
    const uint32 Height = (uint32(Header[20]) << 24) | (uint32(Header[21]) << 16) | (uint32(Header[22]) << 8) | Header[23];
    Input.Reset();
    if (Width != uint32(ExpectedWidth) || Height != uint32(ExpectedHeight) || Width > MaxWidth || Height > MaxHeight)
    { Stop(TEXT("UnexpectedPNGDimensions")); return; }
    // IPlatformFile::MoveFile promises no destination overwrite. Engine writes
    // only fresh unique staging names; finals become immutable evidence names.
    if (!Files.MoveFile(*FinalPath, *PendingPath)) { Stop(TEXT("FinalFrameMoveRejected")); return; }
    ImageBytes += uint64(Bytes); ++Completed; bPending = false;
    if (!Log(TEXT("Completed"), TEXT("PNGHeaderChecked_FullDecodeAndPixelReviewPending"), Bytes)) { Stop(TEXT("LogWriteFailed")); return; }
    if (Attempts >= FrameLimit) Stop(TEXT("AttemptLimit"));
}
