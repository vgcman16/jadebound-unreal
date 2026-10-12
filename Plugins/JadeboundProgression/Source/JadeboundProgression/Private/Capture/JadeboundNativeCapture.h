#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class AJadeboundProgressionActor;

// Optional local evidence capture only. No input bindings, replicated fields,
// saves, gameplay requests or ownership of the actor. SOURCE DRAFT: native unrun.
class FJadeboundNativeCapture final
{
public:
    explicit FJadeboundNativeCapture(AJadeboundProgressionActor* InOwner);
    ~FJadeboundNativeCapture();
    FJadeboundNativeCapture(const FJadeboundNativeCapture&) = delete;
    FJadeboundNativeCapture& operator=(const FJadeboundNativeCapture&) = delete;
    void Tick();
    void Stop(const TCHAR* Reason);

private:
    TWeakObjectPtr<AJadeboundProgressionActor> Owner;
    bool bChecked = false;
    bool bEnabled = false;
    bool bActive = false;
    bool bFinished = false;
    bool bPending = false;
    bool bLogCreated = false;
    int32 FrameLimit = 150;
    int32 Attempts = 0;
    int32 Completed = 0;
    int32 ExpectedWidth = 0;
    int32 ExpectedHeight = 0;
    uint64 ImageBytes = 0;
    uint64 LogBytes = 0;
    uint64 LastAvailablePhysical = 0;
    uint64 LastAvailableVirtualOrCommit = 0;
    uint64 LastDiskFree = 0;
    double CheckedAt = 0.0;
    double StartedAt = 0.0;
    double NextAt = 0.0;
    double RequestAt = 0.0;
    double RequestWorldAt = 0.0;
    FString RunID;
    FString Directory;
    FString PendingPath;
    FString FinalPath;
    FDelegateHandle ProcessedHandle;

    bool ReadCommand();
    bool IsContext() const;
    bool CheckRequestBounds(FString& Reason);
    bool Log(const TCHAR* Event, const TCHAR* Reason, int64 Bytes = 0);
    void OnProcessed();
};
