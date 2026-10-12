#include "Development/ResumeReceiptIO.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace Jadebound::Development
{
ReceiptResult CreateResumeReceipt(const FString& Path, const FString& Contents)
{
    const FTCHARToUTF8 Bytes(*Contents, Contents.Len());
    if (Bytes.Length() <= 0 || Bytes.Length() > 256) return ReceiptResult::InvalidContents;
#if PLATFORM_WINDOWS
    const auto Existing = [&]()
    {
        const HANDLE Reader = CreateFileW(*Path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (Reader == INVALID_HANDLE_VALUE) return ReceiptResult::Conflict;
        LARGE_INTEGER Size{};
        uint8 Readback[256]{};
        DWORD Read = 0;
        const bool bMatched = GetFileSizeEx(Reader, &Size) && Size.QuadPart == Bytes.Length() &&
            ReadFile(Reader, Readback, static_cast<DWORD>(Bytes.Length()), &Read, nullptr) &&
            Read == static_cast<DWORD>(Bytes.Length()) && FMemory::Memcmp(Readback, Bytes.Get(), Bytes.Length()) == 0;
        const bool bClosed = CloseHandle(Reader);
        return bMatched && bClosed ? ReceiptResult::AlreadyMatched : ReceiptResult::Conflict;
    };
    if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true)) return ReceiptResult::WriteFailed;
    const HANDLE File = CreateFileW(*Path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (File == INVALID_HANDLE_VALUE)
    {
        const DWORD Error = GetLastError();
        return Error == ERROR_FILE_EXISTS || Error == ERROR_ALREADY_EXISTS ? Existing() : ReceiptResult::WriteFailed;
    }
    DWORD Written = 0;
    const bool bWritten = WriteFile(File, Bytes.Get(), static_cast<DWORD>(Bytes.Length()), &Written, nullptr) &&
        Written == static_cast<DWORD>(Bytes.Length());
    const bool bFlushed = bWritten && FlushFileBuffers(File);
    const bool bClosed = CloseHandle(File);
    return bWritten && bFlushed && bClosed ? ReceiptResult::Created : ReceiptResult::WriteFailed;
#else
    return ReceiptResult::UnsupportedPlatform;
#endif
}
}
