#pragma once

#include "Domain/ProgressionCore.h"
#include "JadeboundProgressionActor.h"

namespace Jadebound::Recipient
{
// Pure pair construction only. The caller must first prove the exact native
// enemy life and retained authority recipient. This API is not a kill provider.
struct IssueCandidate
{
    Snapshot State;
    TArray<FJadeboundSavedDrop> Drops;
    int64 DropId = 0;
};
Result BuildIssue(const Snapshot& Before, const TArray<FJadeboundSavedDrop>& Drops,
    const FVector& AuthorityEnemyLocation, IssueCandidate& Out);
// The issued prefix remains true after pickup removes an outstanding claim.
// Character identity and actual journal ownership are checked by the actor.
bool HasCommittedIssue(const Snapshot& Committed, const TArray<FJadeboundSavedDrop>& Drops,
    int64 DropId, uint64 MinimumRevision);
}
