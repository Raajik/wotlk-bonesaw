p = 'modules/mod-playerbots/src/Bot/RandomPlayerbotMgr.h'
s = open(p, 'r', encoding='utf-8', newline='').read()
old = """    void ProcessLfgFillTeleports();
    void UpdateLfgFillTeleports();
"""
new = """    void ProcessLfgFillTeleports();
    void UpdateLfgFillTeleports();
    // Report #157: dissolve groups whose members are all bots (the raid a
    // real player walked away from), so the bots go back to the pool.
    void DisbandBotOnlyGroups();
"""
assert s.count(old) == 1, s.count(old)
s = s.replace(old, new)
open(p, 'w', encoding='utf-8', newline='').write(s)
print('h OK')

p2 = 'modules/mod-playerbots/src/Bot/RandomPlayerbotMgr.cpp'
s2 = open(p2, 'r', encoding='utf-8', newline='').read()
old2 = """    // LFG fill teleports are NOT retried here: this pass re-arms at
    // 30-60s (RandomBotUpdateInterval scaling), which starved the retry
    // window -- live logs showed 48 expired vs 13 delivered. The world-tick
    // hook drives UpdateLfgFillTeleports every update instead (report #165).
"""
new2 = """    // LFG fill teleports are NOT retried here: this pass re-arms at
    // 30-60s (RandomBotUpdateInterval scaling), which starved the retry
    // window -- live logs showed 48 expired vs 13 delivered. The world-tick
    // hook drives UpdateLfgFillTeleports every update instead (report #165).
    // The same pass cadence is right for orphaned bot groups: a raid whose
    // real players left lives minutes, not forever (report #157).
    DisbandBotOnlyGroups();
"""
assert s2.count(old2) == 1, s2.count(old2)
s2 = s2.replace(old2, new2)

impl = """
void RandomPlayerbotMgr::DisbandBotOnlyGroups()
{
    for (auto const& [guid, bot] : playerBots)
    {
        PlayerbotAI* botAI = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
        Group* group = bot ? bot->GetGroup() : nullptr;
        if (!botAI || !group)
            continue;

        // Groups with their own lifecycle are not ours to dissolve: LFG
        // groups fall apart through the dungeon finder, BG/BF raids are
        // created and torn down by their systems.
        if (group->isLFGGroup() || group->isBGGroup() || group->isBFGroup())
            continue;

        // Mirror the login-time groupValid check (OnBotLogin): the group is
        // worth keeping while a real player is in it, or -- when the config
        // asks for it -- while it holds a linked bot that belongs to a real
        // account (alts stay grouped for their master's return).
        bool groupValid = false;
        for (GroupReference const& memberRef : group->GetMembers())
        {
            Player* member = memberRefPlayer(&memberRef);
            (void)member;
        }
    }
}
"""
print('cpp anchor ok' if s2.count(old2) == 0 else 'hmm')
open(p2, 'w', encoding='utf-8', newline='').write(s2)
print('cpp OK (call wired)')
