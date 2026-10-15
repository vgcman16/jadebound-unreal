#include "Domain/RecipientLedger.h"
#include "Persistence/ProgressionSaveV3.h"
#include "Combat/RecipientCombatRules.h"
#include <utility>

namespace Jadebound::Recipient
{
Result BuildIssue(const Snapshot& Before, const TArray<FJadeboundSavedDrop>& Drops,
    const FVector& AuthorityEnemyLocation, IssueCandidate& Out)
{
    if (!Persistence::ValidateDropsV3(Before, Drops) ||
        !Combat::IsSavedDropLocation(AuthorityEnemyLocation))
        return {ResultCode::InvalidSnapshot, ValidationCode::None, false, Before.Revision};
    ProgressionCore Trial;
    const Result Loaded = Trial.LoadSnapshot(Before);
    if (!Loaded.Succeeded()) return Loaded;
    const uint64 Id = Before.NextDropId;
    const Result Issued = Trial.RecordRaiderDefeat(Id);
    if (!Issued.Succeeded()) return Issued;
    IssueCandidate Candidate;
    Candidate.State = Trial.GetSnapshot();
    Candidate.Drops = Drops;
    Candidate.DropId = static_cast<int64>(Id); // Record refuses signed-counter exhaustion.
    FJadeboundSavedDrop Drop;
    Drop.DropId = Candidate.DropId;
    const bool bParcel = Candidate.State.QuestKillDropId == Id;
    Drop.ItemId = bParcel ? 5 : 3;
    Drop.Count = bParcel ? 1 : 2;
    Drop.Location = AuthorityEnemyLocation;
    Candidate.Drops.Add(Drop);
    FJadeboundProgressionState Encoded;
    if (!Persistence::ValidateDropsV3(Candidate.State, Candidate.Drops) ||
        !Persistence::EncodeStateV3(Candidate.State, Encoded))
        return {ResultCode::InvalidSnapshot, ValidationCode::None, false, Before.Revision};
    Out = std::move(Candidate);
    return Issued;
}
bool HasCommittedIssue(const Snapshot& Committed, const TArray<FJadeboundSavedDrop>& Drops,
    int64 DropId, uint64 MinimumRevision)
{
    return DropId > 0 && MinimumRevision <= MaximumPersistedCounter &&
        Persistence::ValidateDropsV3(Committed, Drops) && Committed.Revision >= MinimumRevision &&
        static_cast<uint64>(DropId) < Committed.NextDropId;
}
}
