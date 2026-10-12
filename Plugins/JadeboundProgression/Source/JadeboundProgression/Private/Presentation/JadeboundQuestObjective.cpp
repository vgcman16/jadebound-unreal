#include "JadeboundProgressionActor.h"

#define LOCTEXT_NAMESPACE "JadeboundQuestObjectives"

FText AJadeboundProgressionActor::GetQuestObjectiveText() const
{
    switch (State.QuestState)
    {
    case 0: return LOCTEXT("Offer", "Speak to the merchant about the missing parcel.");
    case 1: return bDevelopmentModel && !bRecipientCombatEnabled
        ? LOCTEXT("CombatPending", "This expedition's combat is not available yet.")
        : LOCTEXT("Raider", "Defeat the courtyard raider.");
    case 2: return LOCTEXT("Parcel", "Recover the jade parcel dropped by the raider.");
    case 3: return LOCTEXT("Return", "Bring the jade parcel back to the merchant.");
    case 4: return LOCTEXT("Complete", "Parcel returned. Your reward is in your pack.");
    default: return LOCTEXT("Unavailable", "Your quest details are unavailable.");
    }
}

#undef LOCTEXT_NAMESPACE
