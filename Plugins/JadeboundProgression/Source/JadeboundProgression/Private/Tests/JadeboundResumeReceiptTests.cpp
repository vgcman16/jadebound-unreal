#include "Development/ResumeReceiptIO.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include <thread>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeboundResumeReceiptTest, "Jadebound.Development.ResumeReceiptExclusiveCreate",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJadeboundResumeReceiptTest::RunTest(const FString& Parameters)
{
    using namespace Jadebound::Development;
    FString User;
    if (!FParse::Param(FCommandLine::Get(), TEXT("JadeboundResumeTests")) ||
        !FParse::Value(FCommandLine::Get(), TEXT("UserDir="), User) || User.IsEmpty() || FPaths::IsRelative(User))
    { AddError(TEXT("Explicit resume-test opt-in and isolated effective UserDir required.")); return false; }
    User.ReplaceInline(TEXT("\\"), TEXT("/"));
    FPaths::NormalizeDirectoryName(User);
    FString Effective = FPaths::ProjectUserDir(); FPaths::NormalizeDirectoryName(Effective);
    if (!FPaths::IsSamePath(User, Effective) ||
        !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()), User))
    { AddError(TEXT("Explicit resume-test opt-in and isolated effective UserDir required.")); return false; }
#if PLATFORM_WINDOWS
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("ResumeReceipt"),
        FGuid::NewGuid().ToString(EGuidFormats::Digits));
    const FString Path = FPaths::Combine(Directory, TEXT("Receipt.ini"));
    const FString First(TEXT("Synthetic receipt A\n")), Second(TEXT("Synthetic receipt B\n"));
    TestTrue(TEXT("Fresh receipt created"), CreateResumeReceipt(Path, First) == ReceiptResult::Created);
    TestTrue(TEXT("Exact retry is idempotent"), CreateResumeReceipt(Path, First) == ReceiptResult::AlreadyMatched);
    TestTrue(TEXT("Different receipt rejected"), CreateResumeReceipt(Path, Second) == ReceiptResult::Conflict);
    FString Readback;
    TestTrue(TEXT("Conflicting retry preserves bytes"), FFileHelper::LoadFileToString(Readback, *Path) && Readback == First);
    TestTrue(TEXT("Oversized receipt rejected"), CreateResumeReceipt(Path, FString::ChrN(257, TEXT('x'))) == ReceiptResult::InvalidContents);
    const FString RacePath = FPaths::Combine(Directory, TEXT("Race.ini"));
    ReceiptResult A = ReceiptResult::WriteFailed, B = ReceiptResult::WriteFailed;
    std::thread One([&] { A = CreateResumeReceipt(RacePath, First); });
    std::thread Two([&] { B = CreateResumeReceipt(RacePath, Second); });
    One.join(); Two.join();
    TestTrue(TEXT("Competing writers create exactly one receipt"),
        (A == ReceiptResult::Created && B == ReceiptResult::Conflict) || (B == ReceiptResult::Created && A == ReceiptResult::Conflict));
    TestTrue(TEXT("Race keeps winning bytes"), FFileHelper::LoadFileToString(Readback, *RacePath) &&
        Readback == (A == ReceiptResult::Created ? First : Second));
    // Keep this uniquely named synthetic evidence; no real character files touched.
#else
    AddError(TEXT("This milestone requires the Windows receipt implementation.")); return false;
#endif
    return true;
}
#endif
