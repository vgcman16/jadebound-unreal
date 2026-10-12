// Standalone C++17 executable; deliberately outside Source so UnrealBuildTool
// does not link its main or allocation-failure fixture into the game.
#include "../../Source/JadeboundProgression/Private/Development/DevelopmentResumeRegistry.h"

#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>

namespace AllocationFixture
{
    // Single-threaded fixture: fail exactly the selected allocation in a mutation.
    long Remaining = -1;
    void* Allocate(std::size_t Size)
    {
        if (Remaining == 0) throw std::bad_alloc();
        if (Remaining > 0) --Remaining;
        if (void* Memory = std::malloc(Size == 0 ? 1 : Size)) return Memory;
        throw std::bad_alloc();
    }
}
void* operator new(std::size_t Size) { return AllocationFixture::Allocate(Size); }
void* operator new[](std::size_t Size) { return AllocationFixture::Allocate(Size); }
void operator delete(void* Memory) noexcept { std::free(Memory); }
void operator delete[](void* Memory) noexcept { std::free(Memory); }
void operator delete(void* Memory, std::size_t) noexcept { std::free(Memory); }
void operator delete[](void* Memory, std::size_t) noexcept { std::free(Memory); }

namespace
{
using namespace Jadebound::Development;

const std::vector<std::string> Ids = {
    "27b0cbce01df4b6b8f9cf927acaecf11", "4a372b5a838f42be9d9a12e04cb24422",
    "730d69e057c448bb8634b52c72e13133", "91bba52052104c6f890d23f193a87244"
};
const std::vector<std::string> Tokens = {
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
    "cccccccccccccccccccccccccccccccc", "dddddddddddddddddddddddddddddddd"
};
const std::string Spare = "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";

void Require(bool Condition, const char* Message)
{
    if (!Condition) throw std::runtime_error(Message);
}

std::vector<std::string> Roster(std::size_t Count)
{
    return {Ids.begin(), Ids.begin() + Count};
}

ResumeRegistry Configured(std::size_t Count = 2)
{
    ResumeRegistry Registry;
    Require(Registry.Configure(Roster(Count)).Succeeded(), "valid roster refused");
    return Registry;
}

void ExpectSeat(const ResumeResult& Result, std::size_t Index, const std::string& Token)
{
    Require(Result.Succeeded(), "expected successful admission transition");
    Require(Result.ProfileIndex == Index && Result.Token == Token, "wrong profile or token");
}

template<class Operation>
void RefusesUnchanged(ResumeRegistry& Registry, ResumeCode Code, Operation Invoke)
{
    const ResumeImage Before = Registry.Image();
    const ResumeResult Result = Invoke();
    Require(Result.Code == Code && !Result.Succeeded(), "wrong refusal code");
    Require(Result.ProfileIndex == ResumeResult::InvalidProfile && Result.Token.empty(),
            "refusal disclosed a profile or credential");
    Require(Registry.Image() == Before, "refusal changed registry image");
}

void BindActive(ResumeRegistry& Registry, std::size_t Index, std::uint64_t Connection)
{
    ExpectSeat(Registry.Reserve(Connection, {}, Tokens[Index]), Index, Tokens[Index]);
    ExpectSeat(Registry.Activate(Connection), Index, Tokens[Index]);
}

void Configuration()
{
    ResumeRegistry Registry;
    RefusesUnchanged(Registry, ResumeCode::NotConfigured,
        [&] { return Registry.Reserve(1, {}, Tokens[0]); });
    RefusesUnchanged(Registry, ResumeCode::NotConfigured, [&] { return Registry.Activate(1); });
    RefusesUnchanged(Registry, ResumeCode::NotConfigured, [&] { return Registry.Disconnect(1); });
    RefusesUnchanged(Registry, ResumeCode::NotConfigured, [&] { return Registry.CancelPending(1); });
    std::vector<std::vector<std::string>> Invalid = {
        {}, {Ids[0]}, {Ids[0], Ids[1], Ids[2], Ids[3], Spare},
        {Ids[0], Ids[0]}, {Ids[0], std::string(32, '0')}, {Ids[0], "short"},
        {Ids[0], "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"},
        {Ids[0], "27b0cbce-01df-4b6b-8f9c-f927acaecf11"}
    };
    for (const auto& Input : Invalid)
        RefusesUnchanged(Registry, ResumeCode::InvalidRoster, [&] { return Registry.Configure(Input); });
    Require(Registry.Configure(Roster(3)).Succeeded(), "invalid configure poisoned later valid configure");
    Require(Registry.Image().Configured && Registry.Image().Seats.size() == 3, "wrong roster image");
    for (std::size_t I = 0; I < 3; ++I)
    {
        const auto& Seat = Registry.Image().Seats[I];
        Require(Seat.CharacterId == Ids[I] && Seat.Token.empty() && Seat.Connection == 0 &&
            Seat.State == ResumeSeatState::NeverBound && !Seat.EverActivated && !Seat.PendingNew,
            "configured seat was already bound");
    }
    RefusesUnchanged(Registry, ResumeCode::AlreadyConfigured, [&] { return Registry.Configure(Roster(2)); });
}

void ReversedRejoins(std::size_t Count)
{
    auto Registry = Configured(Count);
    for (std::size_t I = 0; I < Count; ++I) BindActive(Registry, I, 100 + I);
    for (std::size_t I = 0; I < Count; ++I)
        ExpectSeat(Registry.Disconnect(100 + I), I, Tokens[I]);
    RefusesUnchanged(Registry, ResumeCode::RosterExhausted,
        [&] { return Registry.Reserve(700, {}, Spare); });
    for (std::size_t I = Count; I-- > 0;)
    {
        const std::uint64_t Connection = 800 + I;
        ExpectSeat(Registry.Reserve(Connection, Tokens[I], {}), I, Tokens[I]);
        ExpectSeat(Registry.Activate(Connection), I, Tokens[I]);
        const auto& Seat = Registry.Image().Seats[I];
        Require(Seat.CharacterId == Ids[I] && Seat.Connection == Connection &&
            Seat.State == ResumeSeatState::Active && Seat.EverActivated && !Seat.PendingNew,
            "reverse reconnect reassigned persistent identity");
    }
}

void TokenRefusals()
{
    auto Registry = Configured();
    BindActive(Registry, 0, 10);
    ExpectSeat(Registry.Disconnect(10), 0, Tokens[0]);
    RefusesUnchanged(Registry, ResumeCode::UnknownToken, [&] { return Registry.Reserve(11, Spare, {}); });
    const std::vector<std::string> Malformed = {
        "short", std::string(32, '0'), std::string(31, 'a'), std::string(33, 'a'), std::string(32, 'A'),
        std::string(32, 'g'), std::string(31, 'a') + ' ', std::string(31, 'a') + '\0'
    };
    for (const auto& Token : Malformed)
    {
        RefusesUnchanged(Registry, ResumeCode::InvalidPresentedToken,
            [&] { return Registry.Reserve(11, Token, {}); });
        RefusesUnchanged(Registry, ResumeCode::InvalidFreshToken,
            [&] { return Registry.Reserve(11, {}, Token); });
    }
    for (const auto& Id : Roster(2))
    {
        RefusesUnchanged(Registry, ResumeCode::CharacterIdPresented,
            [&] { return Registry.Reserve(11, Id, {}); });
        RefusesUnchanged(Registry, ResumeCode::TokenCollision,
            [&] { return Registry.Reserve(11, {}, Id); });
    }
    RefusesUnchanged(Registry, ResumeCode::UnexpectedFreshToken,
        [&] { return Registry.Reserve(11, Tokens[0], Spare); });
    RefusesUnchanged(Registry, ResumeCode::InvalidFreshToken,
        [&] { return Registry.Reserve(11, {}, {}); });
    RefusesUnchanged(Registry, ResumeCode::TokenCollision,
        [&] { return Registry.Reserve(11, {}, Tokens[0]); });
    // Refused credentials never consume the still-never-bound second profile.
    ExpectSeat(Registry.Reserve(12, {}, Tokens[1]), 1, Tokens[1]);
    ExpectSeat(Registry.Reserve(13, Tokens[0], {}), 0, Tokens[0]);
}

void DuplicateConnectionsAndProfiles()
{
    auto Registry = Configured();
    ExpectSeat(Registry.Reserve(10, {}, Tokens[0]), 0, Tokens[0]);
    RefusesUnchanged(Registry, ResumeCode::ConnectionInUse,
        [&] { return Registry.Reserve(10, {}, Tokens[1]); });
    RefusesUnchanged(Registry, ResumeCode::ConnectionInUse,
        [&] { return Registry.Reserve(10, Tokens[0], {}); });
    RefusesUnchanged(Registry, ResumeCode::ProfileInUse,
        [&] { return Registry.Reserve(20, Tokens[0], {}); });
    RefusesUnchanged(Registry, ResumeCode::TokenCollision,
        [&] { return Registry.Reserve(20, {}, Tokens[0]); });
    ExpectSeat(Registry.Activate(10), 0, Tokens[0]);
    RefusesUnchanged(Registry, ResumeCode::ProfileInUse,
        [&] { return Registry.Reserve(20, Tokens[0], {}); });
    RefusesUnchanged(Registry, ResumeCode::NotPending, [&] { return Registry.Activate(10); });
    RefusesUnchanged(Registry, ResumeCode::NotPending, [&] { return Registry.CancelPending(10); });
    for (const std::uint64_t Connection : {std::uint64_t(0), std::uint64_t(999)})
    {
        const ResumeCode Code = Connection == 0 ? ResumeCode::InvalidConnection : ResumeCode::UnknownConnection;
        RefusesUnchanged(Registry, Code, [&] { return Registry.Activate(Connection); });
        RefusesUnchanged(Registry, Code, [&] { return Registry.Disconnect(Connection); });
        RefusesUnchanged(Registry, Code, [&] { return Registry.CancelPending(Connection); });
    }
    RefusesUnchanged(Registry, ResumeCode::InvalidConnection,
        [&] { return Registry.Reserve(0, {}, Tokens[1]); });
    ExpectSeat(Registry.Disconnect(10), 0, Tokens[0]);
    RefusesUnchanged(Registry, ResumeCode::UnknownConnection, [&] { return Registry.Disconnect(10); });
    // A connection key can be reused only after disconnect; its token still selects profile zero.
    ExpectSeat(Registry.Reserve(10, Tokens[0], {}), 0, Tokens[0]);
}

void PendingCancellation()
{
    auto Registry = Configured();
    ExpectSeat(Registry.Reserve(10, {}, Tokens[0]), 0, Tokens[0]);
    ExpectSeat(Registry.CancelPending(10), 0, {});
    Require(Registry.Image().Seats[0].State == ResumeSeatState::NeverBound &&
        Registry.Image().Seats[0].Token.empty() && !Registry.Image().Seats[0].EverActivated,
        "cancelled never-active new admission retained ticket");
    RefusesUnchanged(Registry, ResumeCode::UnknownToken,
        [&] { return Registry.Reserve(11, Tokens[0], {}); });
    ExpectSeat(Registry.Reserve(12, {}, Tokens[1]), 0, Tokens[1]);
    ExpectSeat(Registry.Activate(12), 0, Tokens[1]);
    ExpectSeat(Registry.Disconnect(12), 0, Tokens[1]);
    ExpectSeat(Registry.Reserve(13, Tokens[1], {}), 0, Tokens[1]);
    ExpectSeat(Registry.CancelPending(13), 0, Tokens[1]);
    Require(Registry.Image().Seats[0].State == ResumeSeatState::Disconnected &&
        Registry.Image().Seats[0].EverActivated, "cancelled resume released activated identity");
    ExpectSeat(Registry.Reserve(14, {}, Tokens[0]), 1, Tokens[0]);
    // Ordinary pending disconnect is not cancellation and never releases identity.
    ExpectSeat(Registry.Disconnect(14), 1, Tokens[0]);
    ExpectSeat(Registry.Reserve(15, Tokens[0], {}), 1, Tokens[0]);
    ExpectSeat(Registry.CancelPending(15), 1, Tokens[0]);
    Require(!Registry.Image().Seats[1].EverActivated &&
        Registry.Image().Seats[1].State == ResumeSeatState::Disconnected &&
        Registry.Image().Seats[1].Token == Tokens[0], "cancelled preactivation resume released identity");
    RefusesUnchanged(Registry, ResumeCode::RosterExhausted,
        [&] { return Registry.Reserve(16, {}, Spare); });
    ExpectSeat(Registry.Reserve(17, Tokens[1], {}), 0, Tokens[1]);
}

template<class Factory, class Operation>
void AllocationAtomic(Factory Make, Operation Invoke)
{
    std::size_t Throws = 0;
    bool ReachedSuccess = false;
    for (long Ordinal = 0; Ordinal < 128; ++Ordinal)
    {
        auto Registry = Make();
        const ResumeImage Before = Registry.Image();
        bool Threw = false;
        ResumeResult Result;
        AllocationFixture::Remaining = Ordinal;
        try { Result = Invoke(Registry); }
        catch (const std::bad_alloc&) { Threw = true; }
        catch (...) { AllocationFixture::Remaining = -1; throw; }
        AllocationFixture::Remaining = -1;
        if (Threw)
        {
            ++Throws;
            Require(Registry.Image() == Before, "allocation exception partially committed registry");
            Require(Invoke(Registry).Succeeded(), "exception prevented later successful retry");
        }
        else
        {
            Require(Result.Succeeded(), "allocation fixture reached logical refusal");
            ReachedSuccess = true;
            break;
        }
    }
    Require(Throws > 0 && ReachedSuccess, "allocation fixture did not cover throws and success");
}

void ExceptionAtomicity()
{
    const auto Four = Roster(4);
    AllocationAtomic([] { return ResumeRegistry{}; },
        [&](ResumeRegistry& R) { return R.Configure(Four); });
    AllocationAtomic([] { return Configured(4); },
        [](ResumeRegistry& R) { return R.Reserve(10, {}, Tokens[0]); });
    const auto Pending = [] {
        auto R = Configured(4); ExpectSeat(R.Reserve(10, {}, Tokens[0]), 0, Tokens[0]); return R;
    };
    AllocationAtomic(Pending, [](ResumeRegistry& R) { return R.Activate(10); });
    AllocationAtomic(Pending, [](ResumeRegistry& R) { return R.CancelPending(10); });
    AllocationAtomic(Pending, [](ResumeRegistry& R) { return R.Disconnect(10); });
    const auto Disconnected = [] {
        auto R = Configured(4); BindActive(R, 0, 10); R.Disconnect(10); return R;
    };
    AllocationAtomic(Disconnected, [](ResumeRegistry& R) { return R.Reserve(20, Tokens[0], {}); });
    const auto ResumePending = [&] {
        auto R = Disconnected(); R.Reserve(20, Tokens[0], {}); return R;
    };
    AllocationAtomic(ResumePending, [](ResumeRegistry& R) { return R.CancelPending(20); });
}
}

int main()
{
    try
    {
        Configuration();
        ReversedRejoins(2);
        ReversedRejoins(4);
        TokenRefusals();
        DuplicateConnectionsAndProfiles();
        PendingCancellation();
        ExceptionAtomicity();
        std::cout << "PASS: 7 resume admission groups, including allocation-failure atomicity\n";
        return 0;
    }
    catch (const std::exception& Error)
    {
        AllocationFixture::Remaining = -1;
        std::cerr << "FAIL: " << Error.what() << '\n';
        return 1;
    }
}
