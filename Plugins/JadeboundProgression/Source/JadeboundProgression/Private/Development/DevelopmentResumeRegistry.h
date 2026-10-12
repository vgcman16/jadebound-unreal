#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace Jadebound::Development
{
// Game-thread-owned, in-memory identity admission for loopback development only.
// The trusted server supplies fresh opaque tokens; this class generates no entropy.
// Restarting the server discards this registry. A token is a bearer credential.
enum class ResumeCode
{
    Success,
    NotConfigured,
    AlreadyConfigured,
    InvalidRoster,
    InvalidConnection,
    ConnectionInUse,
    InvalidPresentedToken,
    CharacterIdPresented,
    UnknownToken,
    UnexpectedFreshToken,
    InvalidFreshToken,
    TokenCollision,
    ProfileInUse,
    RosterExhausted,
    UnknownConnection,
    NotPending
};

enum class ResumeSeatState { NeverBound, Pending, Active, Disconnected };

struct ResumeResult
{
    static constexpr std::size_t InvalidProfile = std::numeric_limits<std::size_t>::max();
    ResumeCode Code = ResumeCode::NotConfigured;
    std::size_t ProfileIndex = InvalidProfile;
    std::string Token;
    bool Succeeded() const noexcept { return Code == ResumeCode::Success; }
};

struct ResumeSeat
{
    std::string CharacterId;
    std::string Token;
    std::uint64_t Connection = 0;
    ResumeSeatState State = ResumeSeatState::NeverBound;
    bool EverActivated = false;
    bool PendingNew = false;

    bool operator==(const ResumeSeat& Other) const noexcept
    {
        return CharacterId == Other.CharacterId && Token == Other.Token &&
            Connection == Other.Connection && State == Other.State &&
            EverActivated == Other.EverActivated && PendingNew == Other.PendingNew;
    }
};

struct ResumeImage
{
    bool Configured = false;
    std::vector<ResumeSeat> Seats;
    bool operator==(const ResumeImage& Other) const noexcept
    {
        return Configured == Other.Configured && Seats == Other.Seats;
    }
};

class ResumeRegistry
{
public:
    static constexpr std::size_t MinimumProfiles = 2;
    static constexpr std::size_t MaximumProfiles = 4;

    const ResumeImage& Image() const noexcept { return Current; }

    ResumeResult Configure(const std::vector<std::string>& Roster)
    {
        if (Current.Configured) return Refuse(ResumeCode::AlreadyConfigured);
        if (Roster.size() < MinimumProfiles || Roster.size() > MaximumProfiles)
            return Refuse(ResumeCode::InvalidRoster);
        for (std::size_t I = 0; I < Roster.size(); ++I)
        {
            if (!LowerHex32(Roster[I], true)) return Refuse(ResumeCode::InvalidRoster);
            for (std::size_t J = 0; J < I; ++J)
                if (Roster[J] == Roster[I]) return Refuse(ResumeCode::InvalidRoster);
        }
        ResumeImage Next;
        Next.Configured = true;
        Next.Seats.reserve(Roster.size());
        for (const std::string& Id : Roster)
        {
            ResumeSeat Seat;
            Seat.CharacterId = Id;
            Next.Seats.push_back(std::move(Seat));
        }
        ResumeResult Result;
        Result.Code = ResumeCode::Success;
        Swap(Next);
        return Result;
    }

    // Empty PresentedToken means a new player and requires FreshServerToken.
    // A reconnect supplies only PresentedToken. Unknown tokens never become new joins.
    ResumeResult Reserve(std::uint64_t Connection, const std::string& PresentedToken,
                         const std::string& FreshServerToken)
    {
        if (!Current.Configured) return Refuse(ResumeCode::NotConfigured);
        if (Connection == 0) return Refuse(ResumeCode::InvalidConnection);
        if (FindConnection(Connection) != ResumeResult::InvalidProfile)
            return Refuse(ResumeCode::ConnectionInUse);

        std::size_t Index = ResumeResult::InvalidProfile;
        const bool New = PresentedToken.empty();
        if (!New)
        {
            if (!LowerHex32(PresentedToken, true))
                return Refuse(ResumeCode::InvalidPresentedToken);
            if (IsCharacterId(PresentedToken)) return Refuse(ResumeCode::CharacterIdPresented);
            if (!FreshServerToken.empty()) return Refuse(ResumeCode::UnexpectedFreshToken);
            Index = FindToken(PresentedToken);
            if (Index == ResumeResult::InvalidProfile) return Refuse(ResumeCode::UnknownToken);
            if (Current.Seats[Index].State != ResumeSeatState::Disconnected)
                return Refuse(ResumeCode::ProfileInUse);
        }
        else
        {
            if (!LowerHex32(FreshServerToken, true)) return Refuse(ResumeCode::InvalidFreshToken);
            if (IsCharacterId(FreshServerToken) ||
                FindToken(FreshServerToken) != ResumeResult::InvalidProfile)
                return Refuse(ResumeCode::TokenCollision);
            for (std::size_t I = 0; I < Current.Seats.size(); ++I)
                if (Current.Seats[I].State == ResumeSeatState::NeverBound) { Index = I; break; }
            if (Index == ResumeResult::InvalidProfile) return Refuse(ResumeCode::RosterExhausted);
        }

        ResumeImage Next = Current;
        ResumeSeat& Seat = Next.Seats[Index];
        if (New) Seat.Token = FreshServerToken;
        Seat.Connection = Connection;
        Seat.State = ResumeSeatState::Pending;
        Seat.PendingNew = New;
        return Commit(Next, Index);
    }

    ResumeResult Activate(std::uint64_t Connection)
    {
        const ResumeResult Failure = ConnectionAdmission(Connection);
        if (!Failure.Succeeded()) return Failure;
        const std::size_t Index = FindConnection(Connection);
        if (Current.Seats[Index].State != ResumeSeatState::Pending)
            return Refuse(ResumeCode::NotPending);
        ResumeImage Next = Current;
        Next.Seats[Index].State = ResumeSeatState::Active;
        Next.Seats[Index].EverActivated = true;
        Next.Seats[Index].PendingNew = false;
        return Commit(Next, Index);
    }

    // Logout/disconnect never releases a bound identity, including pending admissions.
    ResumeResult Disconnect(std::uint64_t Connection)
    {
        const ResumeResult Failure = ConnectionAdmission(Connection);
        if (!Failure.Succeeded()) return Failure;
        const std::size_t Index = FindConnection(Connection);
        ResumeImage Next = Current;
        Next.Seats[Index].Connection = 0;
        Next.Seats[Index].State = ResumeSeatState::Disconnected;
        Next.Seats[Index].PendingNew = false;
        return Commit(Next, Index);
    }

    // Only call before model creation/token delivery. A failed first admission can
    // release its never-activated new ticket; cancelling a resume retains the ticket.
    ResumeResult CancelPending(std::uint64_t Connection)
    {
        const ResumeResult Failure = ConnectionAdmission(Connection);
        if (!Failure.Succeeded()) return Failure;
        const std::size_t Index = FindConnection(Connection);
        const ResumeSeat& Original = Current.Seats[Index];
        if (Original.State != ResumeSeatState::Pending) return Refuse(ResumeCode::NotPending);
        ResumeImage Next = Current;
        ResumeSeat& Seat = Next.Seats[Index];
        Seat.Connection = 0;
        Seat.State = ResumeSeatState::Disconnected;
        if (Original.PendingNew && !Original.EverActivated)
        {
            Seat.Token.clear();
            Seat.State = ResumeSeatState::NeverBound;
        }
        Seat.PendingNew = false;
        return Commit(Next, Index);
    }

private:
    ResumeImage Current;

    static bool LowerHex32(const std::string& Value, bool RequireNonzero) noexcept
    {
        if (Value.size() != 32) return false;
        bool Nonzero = false;
        for (char Ch : Value)
        {
            if (!((Ch >= '0' && Ch <= '9') || (Ch >= 'a' && Ch <= 'f'))) return false;
            Nonzero |= Ch != '0';
        }
        return !RequireNonzero || Nonzero;
    }

    bool IsCharacterId(const std::string& Token) const noexcept
    {
        for (const ResumeSeat& Seat : Current.Seats)
            if (Seat.CharacterId == Token) return true;
        return false;
    }

    std::size_t FindToken(const std::string& Token) const noexcept
    {
        for (std::size_t I = 0; I < Current.Seats.size(); ++I)
            if (!Current.Seats[I].Token.empty() && Current.Seats[I].Token == Token) return I;
        return ResumeResult::InvalidProfile;
    }

    std::size_t FindConnection(std::uint64_t Connection) const noexcept
    {
        for (std::size_t I = 0; I < Current.Seats.size(); ++I)
            if (Current.Seats[I].Connection == Connection) return I;
        return ResumeResult::InvalidProfile;
    }

    ResumeResult ConnectionAdmission(std::uint64_t Connection) const
    {
        if (!Current.Configured) return Refuse(ResumeCode::NotConfigured);
        if (Connection == 0) return Refuse(ResumeCode::InvalidConnection);
        if (FindConnection(Connection) == ResumeResult::InvalidProfile)
            return Refuse(ResumeCode::UnknownConnection);
        return Refuse(ResumeCode::Success);
    }

    static ResumeResult Refuse(ResumeCode Code)
    {
        ResumeResult Result;
        Result.Code = Code;
        return Result;
    }

    void Swap(ResumeImage& Next) noexcept
    {
        std::swap(Current.Configured, Next.Configured);
        Current.Seats.swap(Next.Seats);
    }

    ResumeResult Commit(ResumeImage& Next, std::size_t Index)
    {
        // All allocations (including the returned credential) precede the no-throw
        // commit. A bad_alloc leaves the full current image unchanged.
        ResumeResult Result;
        Result.Code = ResumeCode::Success;
        Result.ProfileIndex = Index;
        Result.Token = Next.Seats[Index].Token;
        Swap(Next);
        return Result;
    }
};
}
