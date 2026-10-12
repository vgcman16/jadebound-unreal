#pragma once
#include "CoreMinimal.h"

namespace Jadebound::Development
{
enum class ReceiptResult { Created, AlreadyMatched, Conflict, WriteFailed, InvalidContents, UnsupportedPlatform };
// Private trusted-path helper. Windows CREATE_NEW; existing files are preserved.
ReceiptResult CreateResumeReceipt(const FString& Path, const FString& Contents);
}
