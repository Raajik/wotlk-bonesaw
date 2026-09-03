"""Infinitely repeatable dailies/weeklies (bug report #164).

PlayerQuest.cpp is LF. Two edits:
  1. #include "Config.h" (sConfigMgr)
  2. after the Set*QuestStatus bookkeeping in RewardQuest, release the
     take-gates (daily slot field, DF set, weekly set) so the quest can be
     accepted again immediately; the single SaveToDB below persists the
     released state.
"""
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
PATH = r"A:\wow-bonesaw\src\server\game\Entities\Player\PlayerQuest.cpp"

data = open(PATH, "rb").read()
if data.count(b"\r\n"):
    raise SystemExit("expected LF file, found CRLF")
text = data.decode("utf-8")

REPLACEMENTS = [
    ("""#include "CreatureAI.h"
#include "DisableMgr.h"
""",
     """#include "Config.h"
#include "CreatureAI.h"
#include "DisableMgr.h"
"""),
    ("""    else if (quest->IsWeekly())
        SetWeeklyQuestStatus(quest_id);
    else if (quest->IsMonthly())
        SetMonthlyQuestStatus(quest_id);
    else if (quest->IsSeasonal())
        SetSeasonalQuestStatus(quest_id);

    RemoveActiveQuest(quest_id, false);
""",
     """    else if (quest->IsWeekly())
        SetWeeklyQuestStatus(quest_id);
    else if (quest->IsMonthly())
        SetMonthlyQuestStatus(quest_id);
    else if (quest->IsSeasonal())
        SetSeasonalQuestStatus(quest_id);

    // Dailies and weeklies are infinitely repeatable (report #164). The
    // bookkeeping above stays intact for saves and achievement criteria,
    // but the take-gates it feeds -- the daily slot field, the DF quest
    // set and the weekly set -- are released immediately, so the quest can
    // be accepted again without waiting for the next reset. The single
    // SaveToDB below then persists the released state, so a relog cannot
    // resurrect the lockout. Daily/weekly templates are auto-flagged
    // QUEST_SPECIAL_FLAGS_REPEATABLE at load, so turn-ins and rewards run
    // the normal path every cycle.
    if (sConfigMgr->GetOption<bool>("LivingGear.Quests.InfiniteDailyWeekly", true))
    {
        if (quest->IsDaily() || quest->IsDFQuest())
        {
            for (uint32 quest_daily_idx = 0; quest_daily_idx < PLAYER_MAX_DAILY_QUESTS; ++quest_daily_idx)
                if (GetUInt32Value(PLAYER_FIELD_DAILY_QUESTS_1 + quest_daily_idx) == quest_id)
                    SetUInt32Value(PLAYER_FIELD_DAILY_QUESTS_1 + quest_daily_idx, 0);
            m_DFQuests.erase(quest_id);
        }

        if (quest->IsWeekly())
        {
            m_weeklyquests.erase(quest_id);
            m_WeeklyQuestChanged = true;
        }
    }

    RemoveActiveQuest(quest_id, false);
"""),
]

for before, after in REPLACEMENTS:
    n = text.count(before)
    if n != 1:
        raise SystemExit("anchor count %d (expected 1): %r" % (n, before[:80]))
    text = text.replace(before, after)

open(PATH, "wb").write(text.encode("utf-8"))
print("ok: 2 replacements applied")
