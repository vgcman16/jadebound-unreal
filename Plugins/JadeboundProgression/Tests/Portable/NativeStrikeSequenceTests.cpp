// Standalone C++17 test; outside Source and never linked into the Unreal module.
#include "../../Source/JadeboundProgression/Private/Combat/NativeStrikeSequence.h"
#include "../../Source/JadeboundProgression/Private/Combat/PlayerLifeRules.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

namespace
{
namespace Sequence = Jadebound::Combat::NativeStrikeSequence;
namespace Life = Jadebound::Combat::PlayerLife;
using Life::State;

std::uint32_t Checks = 0;
#define CHECK(Expression) do { ++Checks; if (!(Expression)) { \
    std::printf("FAIL line %d: %s\n", __LINE__, #Expression); return false; } } while (false)

static_assert(noexcept(Sequence::TryIssue(std::declval<std::uint64_t&>())),
              "Issuance must not throw.");
static_assert(std::is_same_v<decltype(Sequence::TryIssue(std::declval<std::uint64_t&>())), std::uint64_t>,
              "Zero and nonzero tickets must retain the full unsigned ID width.");

bool Same(const State& A, const State& B) noexcept
{
    // Compare meaningful fields and clock bits, never struct padding.
    return A.Generation == B.Generation && A.Health == B.Health && A.LastStrike == B.LastStrike &&
        std::memcmp(&A.DeadAt, &B.DeadAt, sizeof(double)) == 0 &&
        std::memcmp(&A.ReviveReadyAt, &B.ReviveReadyAt, sizeof(double)) == 0 &&
        std::memcmp(&A.LastObservation, &B.LastObservation, sizeof(double)) == 0;
}

bool BasicIssuanceAndSpentAdmission()
{
    std::uint64_t LastIssued = 0;
    CHECK(Sequence::TryIssue(LastIssued) == 1 && LastIssued == 1);
    CHECK(Sequence::TryIssue(LastIssued) == 2 && LastIssued == 2);
    CHECK(Sequence::TryIssue(LastIssued) == 3 && LastIssued == 3);

    State Player;
    const State Before = Player;
    // A failed downstream admission never recycles its issued world ID.
    const std::uint64_t WrongGenerationTicket = Sequence::TryIssue(LastIssued);
    CHECK(WrongGenerationTicket == 4 && LastIssued == 4);
    CHECK(!Life::TryDamage(Player, WrongGenerationTicket, 2, 1.0) && Same(Player, Before));
    CHECK(LastIssued == 4);
    const std::uint64_t NextTicket = Sequence::TryIssue(LastIssued);
    CHECK(NextTicket == 5 && LastIssued == 5);
    CHECK(Life::TryDamage(Player, NextTicket, 1, 1.0));
    CHECK(Player.Health == 80 && Player.LastStrike == 5 && Life::Valid(Player));
    return true;
}

bool EnemyReplacementRetainsWorldSequence()
{
    State Player;
    std::uint64_t WorldLastIssued = 0;
    CHECK(Life::TryDamage(Player, Sequence::TryIssue(WorldLastIssued), 1, 1.0));
    CHECK(Life::TryDamage(Player, Sequence::TryIssue(WorldLastIssued), 1, 2.0));
    CHECK(WorldLastIssued == 2 && Player.LastStrike == 2 && Player.Health == 60);
    const State Retained = Player;

    // Negative control: a replacement enemy's fresh local counter is too old.
    std::uint64_t ReplacementLocalCounter = 0;
    const std::uint64_t LocalFirst = Sequence::TryIssue(ReplacementLocalCounter);
    CHECK(LocalFirst == 1 && ReplacementLocalCounter == 1);
    CHECK(!Life::TryDamage(Player, LocalFirst, 1, 3.0) && Same(Player, Retained));
    CHECK(WorldLastIssued == 2);

    const std::uint64_t ReplacementImpact = Sequence::TryIssue(WorldLastIssued);
    CHECK(ReplacementImpact == 3 && WorldLastIssued == 3);
    CHECK(Life::TryDamage(Player, ReplacementImpact, 1, 3.0));
    CHECK(Player.Health == 40 && Player.LastStrike == 3 && Player.Generation == 1);
    const State After = Player;
    CHECK(!Life::TryDamage(Player, ReplacementImpact, 1, 4.0) && Same(Player, After));
    CHECK(WorldLastIssued == 3);
    return true;
}

bool ReverseWindupOrderAllocatesAtImpact()
{
    // Model two already validated enemy impacts. This is a pure ordering test,
    // not a native windup, collision or recipient-validation qualification.
    std::uint64_t AtWindup = 0;
    const std::uint64_t EarlierWindupTicket = Sequence::TryIssue(AtWindup);
    const std::uint64_t LaterWindupTicket = Sequence::TryIssue(AtWindup);
    State PrematureTickets;
    CHECK(EarlierWindupTicket == 1 && LaterWindupTicket == 2);
    CHECK(Life::TryDamage(PrematureTickets, LaterWindupTicket, 1, 4.0));
    const State PrematureBefore = PrematureTickets;
    CHECK(!Life::TryDamage(PrematureTickets, EarlierWindupTicket, 1, 5.0) &&
          Same(PrematureTickets, PrematureBefore));

    // No IDs are reserved during the earlier or later windup. The later
    // windup's impact completes first and receives the first world ID.
    std::uint64_t AtImpact = 0;
    State CorrectTickets;
    CHECK(AtImpact == 0);
    const std::uint64_t LaterEnemyImpact = Sequence::TryIssue(AtImpact);
    CHECK(LaterEnemyImpact == 1 && AtImpact == 1);
    CHECK(Life::TryDamage(CorrectTickets, LaterEnemyImpact, 1, 4.0));
    CHECK(CorrectTickets.Health == 80 && CorrectTickets.LastStrike == 1);
    const std::uint64_t EarlierEnemyImpact = Sequence::TryIssue(AtImpact);
    CHECK(EarlierEnemyImpact == 2 && AtImpact == 2);
    CHECK(Life::TryDamage(CorrectTickets, EarlierEnemyImpact, 1, 5.0));
    CHECK(CorrectTickets.Health == 60 && CorrectTickets.LastStrike == 2 && Life::Valid(CorrectTickets));
    const State After = CorrectTickets;
    CHECK(!Life::TryDamage(CorrectTickets, LaterEnemyImpact, 1, 6.0) && Same(CorrectTickets, After));
    CHECK(!Life::TryDamage(CorrectTickets, EarlierEnemyImpact, 1, 6.0) && Same(CorrectTickets, After));
    return true;
}

bool PlayerHistoriesRemainIndependent()
{
    std::uint64_t WorldLastIssued = 0;
    State A;
    State B;
    const State BInitial = B;
    const std::uint64_t FirstA = Sequence::TryIssue(WorldLastIssued);
    CHECK(Life::TryDamage(A, FirstA, 1, 1.0));
    CHECK(A.Health == 80 && A.LastStrike == 1 && Same(B, BInitial));

    const State ABeforeB = A;
    const std::uint64_t FirstB = Sequence::TryIssue(WorldLastIssued);
    CHECK(Life::TryDamage(B, FirstB, 1, 2.0));
    CHECK(B.Health == 80 && B.LastStrike == 2 && Same(A, ABeforeB));
    const State BBeforeA = B;
    CHECK(!Life::TryDamage(A, FirstA, 1, 3.0) && Same(A, ABeforeB));
    const std::uint64_t NextA = Sequence::TryIssue(WorldLastIssued);
    CHECK(NextA == 3 && Life::TryDamage(A, NextA, 1, 3.0));
    CHECK(A.Health == 60 && A.LastStrike == 3 && Same(B, BBeforeA));

    const State AAfter = A;
    CHECK(!Life::TryDamage(B, FirstB, 1, 4.0) && Same(B, BBeforeA));
    const std::uint64_t NextB = Sequence::TryIssue(WorldLastIssued);
    CHECK(NextB == 4 && Life::TryDamage(B, NextB, 1, 4.0));
    CHECK(B.Health == 60 && B.LastStrike == 4 && Same(A, AAfter));
    const State BAfter = B;
    CHECK(!Life::TryDamage(A, NextA, 1, 5.0) && Same(A, AAfter));
    CHECK(!Life::TryDamage(B, NextB, 1, 5.0) && Same(B, BAfter));
    CHECK(WorldLastIssued == 4);
    // An ID is not a recipient credential. The native authority must bind the
    // validated impact to its exact player; PlayerLife only owns local replay.
    return true;
}

bool RealReviveRetainsStrikeHistory()
{
    std::uint64_t WorldLastIssued = 0;
    State Player;
    for (std::uint64_t Index = 0; Index < 5; ++Index)
    {
        const std::uint64_t Ticket = Sequence::TryIssue(WorldLastIssued);
        CHECK(Ticket == Index + 1);
        CHECK(Life::TryDamage(Player, Ticket, 1, 100.1 + static_cast<double>(Index) * .2));
    }
    CHECK(Life::Valid(Player) && Player.Health == 0 && Player.LastStrike == 5 && WorldLastIssued == 5);
    CHECK(Player.ReviveReadyAt == Player.DeadAt + Life::ReviveDelaySeconds);
    const State Dead = Player;
    const double Early = std::nextafter(Player.ReviveReadyAt, Player.DeadAt);
    CHECK(!Life::CanRevive(Player, 1, Early));
    CHECK(!Life::TryRevive(Player, 1, Early) && Same(Player, Dead));
    CHECK(Life::CanRevive(Player, 1, Player.ReviveReadyAt));
    CHECK(Life::TryRevive(Player, 1, Player.ReviveReadyAt));
    CHECK(Player.Health == 100 && Player.Generation == 2 && Player.LastStrike == 5 &&
          Player.DeadAt == 0.0 && Player.ReviveReadyAt == 0.0 && WorldLastIssued == 5);
    const State Revived = Player;
    const double Now = Player.LastObservation;
    CHECK(!Life::TryDamage(Player, 5, 2, Now) && Same(Player, Revived));
    CHECK(!Life::TryRevive(Player, 2, Now) && Same(Player, Revived));
    const std::uint64_t NewImpact = Sequence::TryIssue(WorldLastIssued);
    CHECK(NewImpact == 6 && Life::TryDamage(Player, NewImpact, 2, Now));
    CHECK(Player.Health == 80 && Player.LastStrike == 6 && Player.Generation == 2);
    const State After = Player;
    const std::uint64_t StaleGenerationImpact = Sequence::TryIssue(WorldLastIssued);
    CHECK(StaleGenerationImpact == 7 && !Life::TryDamage(Player, StaleGenerationImpact, 1, Now) &&
          Same(Player, After));
    CHECK(WorldLastIssued == 7);
    CHECK(Sequence::TryIssue(WorldLastIssued) == 8 && WorldLastIssued == 8);
    CHECK(Life::TryDamage(Player, 8, 2, Now) && Player.Health == 60 && Player.LastStrike == 8);
    return true;
}

bool ExhaustionNeverWrapsOrReuses()
{
    constexpr std::uint64_t Maximum = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t WorldLastIssued = Maximum - 2;
    State Player;
    Player.LastStrike = Maximum - 2;
    CHECK(Life::Valid(Player));
    const std::uint64_t Penultimate = Sequence::TryIssue(WorldLastIssued);
    CHECK(Penultimate == Maximum - 1 && WorldLastIssued == Maximum - 1);
    CHECK(Life::TryDamage(Player, Penultimate, 1, 1.0));
    CHECK(Player.Health == 80 && Player.LastStrike == Maximum - 1);
    const std::uint64_t LastTicket = Sequence::TryIssue(WorldLastIssued);
    CHECK(LastTicket == Maximum && WorldLastIssued == Maximum);
    CHECK(Life::TryDamage(Player, LastTicket, 1, 2.0));
    CHECK(Player.Health == 60 && Player.LastStrike == Maximum);
    const State Before = Player;
    for (std::uint32_t Attempt = 0; Attempt < 3; ++Attempt)
    {
        const std::uint64_t Refused = Sequence::TryIssue(WorldLastIssued);
        CHECK(Refused == 0 && WorldLastIssued == Maximum);
        CHECK(!Life::TryDamage(Player, Refused, 1, 3.0) && Same(Player, Before));
    }
    CHECK(!Life::TryDamage(Player, LastTicket, 1, 3.0) && Same(Player, Before));
    CHECK(!Life::TryDamage(Player, Penultimate, 1, 3.0) && Same(Player, Before));
    CHECK(WorldLastIssued == Maximum && Life::Valid(Player));
    return true;
}

struct Test
{
    const char* Name;
    bool (*Run)();
};
}

int main()
{
    const std::array<Test, 6> Tests = {{
        {"BasicIssuanceAndSpentAdmission", BasicIssuanceAndSpentAdmission},
        {"EnemyReplacementRetainsWorldSequence", EnemyReplacementRetainsWorldSequence},
        {"ReverseWindupOrderAllocatesAtImpact", ReverseWindupOrderAllocatesAtImpact},
        {"PlayerHistoriesRemainIndependent", PlayerHistoriesRemainIndependent},
        {"RealReviveRetainsStrikeHistory", RealReviveRetainsStrikeHistory},
        {"ExhaustionNeverWrapsOrReuses", ExhaustionNeverWrapsOrReuses}
    }};
    std::uint32_t Groups = 0;
    for (const Test& Current : Tests)
    {
        const std::uint32_t Before = Checks;
        if (!Current.Run()) return 1;
        ++Groups;
        std::printf("PASS %s (%u checks)\n", Current.Name, static_cast<unsigned>(Checks - Before));
    }
    std::printf("PASS NativeStrikeSequenceTests: %u groups, %u checks\n",
                static_cast<unsigned>(Groups), static_cast<unsigned>(Checks));
    return 0;
}
