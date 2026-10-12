#include "../../Source/JadeboundProgression/Private/Development/DevelopmentResumeOptions.h"
#include "../../Source/JadeboundProgression/Private/Development/DevelopmentResumeRegistry.h"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
using namespace Jadebound::Development;
int Checks = 0;
void Require(bool Value, const char* Message)
{
    ++Checks;
    if (!Value) throw std::runtime_error(Message);
}
const std::string Ticket = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
void ParseChecks()
{
    for (const std::string& Input : std::vector<std::string>{"", "?name=Tester", "?listen??name=Local", std::string(4096, 'x')})
    {
        const auto Result = ParseResumeOption(Input);
        Require(Result.Valid && !Result.Present && Result.Token.empty(), "absent option should be a fresh admission");
    }
    for (const std::string& Key : std::vector<std::string>{"JadeboundResume", "jadeboundresume", "JADEBOUNDRESUME"})
    {
        const auto Result = ParseResumeOption("?name=Tester?" + Key + "=" + Ticket + "?other=allowed");
        Require(Result.Valid && Result.Present && Result.Token == Ticket, "canonical option failed");
    }
    const std::vector<std::string> Invalid = {
        "?JadeboundResume", "?JadeboundResume=", "?JadeboundResume=short",
        "?JadeboundResume=" + std::string(32, '0'), "?JadeboundResume=" + std::string(32, 'A'),
        "?JadeboundResume=" + Ticket + "=extra", "?JadeboundResume=%61" + Ticket.substr(1),
        "?JadeboundResume=../" + Ticket, "?JadeboundResume=" + Ticket + "?jadeboundresume=" + Ticket,
        "?JadeboundResume=" + Ticket + "?JADEBOUNDRESUME", std::string(4097, 'x')
    };
    for (const auto& Input : Invalid) Require(!ParseResumeOption(Input).Valid, "malformed/duplicate option admitted");
    Require(!ParseResumeOption(std::string("?JadeboundResume=") + Ticket.substr(0, 16) + '\0' + Ticket.substr(17)).Valid,
        "embedded nul ticket admitted");
    Require(!ParseResumeOption(std::string(1, '\0') + "?JadeboundResume=" + Ticket).Valid, "nul prefix became a new admission");
    Require(!ParseResumeOption(std::string("?name=Local") + '\0').Valid, "nul suffix admitted");
}
void AdmissionChecks()
{
    ResumeRegistry Registry;
    Require(Registry.Configure({"27b0cbce01df4b6b8f9cf927acaecf11", "4a372b5a838f42be9d9a12e04cb24422"}).Succeeded(), "configure failed");
    Require(Registry.Reserve(1, {}, Ticket).Succeeded() && Registry.Activate(1).Succeeded(), "first player failed");
    Require(Registry.Disconnect(1).Succeeded(), "logout failed");
    const auto Before = Registry.Image();
    for (const auto& Input : std::vector<std::string>{"?JadeboundResume=", "?JadeboundResume=garbage", "?JadeboundResume=" + Ticket + "?JadeboundResume=" + Ticket})
    {
        const auto Parsed = ParseResumeOption(Input);
        // This is the native adapter contract: a failed parse never calls Reserve.
        Require(!Parsed.Valid && Registry.Image() == Before, "invalid parse became a new admission");
    }
    const auto Unknown = ParseResumeOption("?JadeboundResume=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    Require(Unknown.Valid && Registry.Reserve(2, Unknown.Token, {}).Code == ResumeCode::UnknownToken && Registry.Image() == Before,
        "unknown resume fell back to a free character");
    const auto Resume = ParseResumeOption("?JadeboundResume=" + Ticket);
    const auto Admitted = Registry.Reserve(3, Resume.Token, {});
    Require(Admitted.Succeeded() && Admitted.ProfileIndex == 0 && Registry.Activate(3).Succeeded(), "original owner failed to resume");
}
}
int main()
{
    try { ParseChecks(); AdmissionChecks(); std::cout << "PASS DevelopmentResumeOptions checks=" << Checks << '\n'; return 0; }
    catch (const std::exception& Error) { std::cerr << "FAIL " << Error.what() << '\n'; return 1; }
}
