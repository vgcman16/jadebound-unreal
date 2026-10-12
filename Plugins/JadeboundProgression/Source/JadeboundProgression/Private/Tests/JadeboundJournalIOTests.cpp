#include "Persistence/JournalIO.h"
#include "Presentation/LootVisualMath.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/AutomationTest.h"
#include "Templates/UniquePtr.h"

#include <limits>

namespace
{
struct FJournalTrace
{
    TArray<FString> Events;
    TArray<uint8> Bytes;
    bool bWriteSucceeded = true, bFlushSucceeded = true, bFullFlushRequested = false;
    int32 WriteCalls = 0, FlushCalls = 0;
};

class FJournalHandle final : public IFileHandle
{
public:
    explicit FJournalHandle(FJournalTrace& InTrace) : Trace(InTrace) { Trace.Events.Add(TEXT("open")); }
    virtual ~FJournalHandle() override { Trace.Events.Add(TEXT("close")); }
    virtual int64 Tell() override { return Trace.Bytes.Num(); }
    virtual bool Seek(int64) override { return false; }
    virtual bool SeekFromEnd(int64 = 0) override { return false; }
    virtual bool Read(uint8*, int64) override { return false; }
    virtual bool ReadAt(uint8*, int64, int64) override { return false; }
    virtual bool Truncate(int64) override { return false; }
    virtual bool Write(const uint8* Source, int64 Count) override
    {
        ++Trace.WriteCalls;
        Trace.Events.Add(TEXT("write"));
        if (!Source || Count <= 0 || Count > MAX_int32) return false;
        // A refused write deliberately records a partial prefix, so the test
        // covers refusal after bytes have already reached the handle.
        const int32 Copied = Trace.bWriteSucceeded ? static_cast<int32>(Count) : 1;
        Trace.Bytes.Append(Source, Copied);
        return Trace.bWriteSucceeded;
    }
    virtual bool Flush(const bool bFullFlush = false) override
    {
        ++Trace.FlushCalls;
        Trace.bFullFlushRequested = bFullFlush;
        Trace.Events.Add(bFullFlush ? TEXT("flush-full") : TEXT("flush-soft"));
        return Trace.bFlushSucceeded;
    }
private:
    FJournalTrace& Trace;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundJournalFlushIOTest, "Jadebound.Persistence.JournalFlushIO",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundJournalFlushIOTest::RunTest(const FString& Parameters)
{
    const TArray<uint8> Payload = {0x4a, 0x42, 0x00, 0xff, 0x32};
    for (const bool bWrite : {false, true})
    {
        for (const bool bFlush : {false, true})
        {
            FJournalTrace Trace;
            Trace.bWriteSucceeded = bWrite;
            Trace.bFlushSucceeded = bFlush;
            TUniquePtr<IFileHandle> Handle(new FJournalHandle(Trace));
            const bool Saved = Jadebound::Persistence::WriteAndFlush(*Handle, Payload);
            TestEqual(TEXT("Success requires both write and full flush"), Saved, bWrite && bFlush);
            TestEqual(TEXT("Exactly one write, without helper retry"), Trace.WriteCalls, 1);
            TestEqual(TEXT("No flush after a failed write"), Trace.FlushCalls, bWrite ? 1 : 0);
            TestEqual(TEXT("Full flush is requested after a successful write"), Trace.bFullFlushRequested, bWrite);
            if (bWrite) TestTrue(TEXT("Written bytes exactly match the supplied journal"), Trace.Bytes == Payload);
            else TestTrue(TEXT("A partially written refusal is still refused"), Trace.Bytes.Num() == 1 && Trace.Bytes[0] == Payload[0]);
            TestEqual(TEXT("Helper does not close caller-owned handle"), Trace.Events.Last(), FString(bWrite ? TEXT("flush-full") : TEXT("write")));
            Handle.Reset();
            TestTrue(TEXT("Reset relinquishes ownership"), !Handle);
            TestEqual(TEXT("Handle is closed before any readback"), Trace.Events.Last(), FString(TEXT("close")));
            if (Saved) Trace.Events.Add(TEXT("readback")); // Model caller ordering, not an actor-bank qualification.
            const FString Expected = bWrite ? (bFlush ? TEXT("open|write|flush-full|close|readback") : TEXT("open|write|flush-full|close")) : TEXT("open|write|close");
            TestEqual(TEXT("Exact write/flush/close/readback order"), FString::Join(Trace.Events, TEXT("|")), Expected);
        }
    }
    FJournalTrace EmptyTrace;
    {
        FJournalHandle Handle(EmptyTrace);
        const TArray<uint8> Empty;
        TestFalse(TEXT("Empty journal is refused"), Jadebound::Persistence::WriteAndFlush(Handle, Empty));
        TestEqual(TEXT("Empty journal makes no write call"), EmptyTrace.WriteCalls, 0);
        TestEqual(TEXT("Empty journal makes no flush call"), EmptyTrace.FlushCalls, 0);
    }
    TestEqual(TEXT("Empty journal handle still closes"), FString::Join(EmptyTrace.Events, TEXT("|")), FString(TEXT("open|close")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundLootYawBoundsTest, "Jadebound.Presentation.LootYawBounds",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundLootYawBoundsTest::RunTest(const FString& Parameters)
{
    struct FWitness { int64 Id; float Yaw; };
    // These witnesses use arbitrary-precision integer arithmetic, not the
    // helper under test or overflowing native ID*47 arithmetic.
    const FWitness Witnesses[] = {
        {0, 0.f}, {1, 47.f}, {2, 94.f}, {359, 313.f}, {360, 0.f}, {361, 47.f},
        {196241958230952676LL, 332.f}, // floor(MAX_int64/47): last safe original product.
        {196241958230952677LL, 19.f},  // First overflowing original product.
        {9223372036854775806LL, 282.f}, // Drop-ID representation bound; no reachable state claim.
        {9223372036854775807LL, 329.f}, // Representation boundary, no issuance claim.
        {-1, 0.f}, {std::numeric_limits<int64>::min(), 0.f}
    };
    for (const auto& Witness : Witnesses)
    {
        const float Actual = Jadebound::Presentation::LootYaw(Witness.Id);
        TestEqual(*FString::Printf(TEXT("Exact yaw witness for ID%lld"), static_cast<long long>(Witness.Id)), Actual, Witness.Yaw);
        TestTrue(TEXT("Yaw remains in the finite integral degree range"), Actual >= 0.f && Actual < 360.f);
    }
    return true;
}
#endif
