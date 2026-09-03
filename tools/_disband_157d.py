# ---- header: add declaration after UpdateLfgFillTeleports
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
n = s.count(old)
assert n == 1, f'header anchor: {n}'
s = s.replace(old, new)
open(p, 'w', encoding='utf-8', newline='').write(s)
print('h OK')

# ---- cpp: wire the call into UpdateAIInternal
p2 = 'modules/mod-playerbots/src/Bot/RandomPlayerbotMgr.cpp'
s2 = open(p2, 'r', encoding='utf-8', newline='').read()
old2 = """    // hook drives UpdateLfgFillTeleports every update instead (report #165).
"""
new2 = """    // hook drives UpdateLfgFillTeleports every update instead (report #165).
    // The same pass cadence is right for orphaned bot groups: a raid whose
    // real players left lives minutes, not forever (report #157).
    DisbandBotOnlyGroups();
"""
n2 = s2.count(old2)
assert n2 == 1, f'cpp call anchor: {n2}'
s2 = s2.replace(old2, new2)

impl = """void RandomPlayerbotMgr::DisbandBotOnlyGroups()
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
        // asks for it -- while it holds a member on a real account (alts
        // stay grouped for their master's return).
        bool groupValid = false;
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
        {
            Player* member = ObjectAccessor::FindPlayer(slot.guid);
            if (member && !GET_PLAYERBOT_AI(member))
            {
                groupValid = true;
                break;
            }
            if (sPlayerbotAIConfig.KeepAltsInGroup())
            {
                uint32 const account = sCharacterCache->GetCharacterAccountIdByGuid(slot.guid);
                if (!sPlayerbotAIConfig.IsInRandomAccountList(account))
                {
                    groupValid = true;
                    break;
                }
            }
        }

        // Only the leader acts: one disband packet frees the whole raid at
        // once and sends every bot back to the pool. Grouped bots never
        // reach ProcessBot's own leave logic (it early-returns for grouped
        // bots), which is why these groups could outlive their players
        // indefinitely.
        if (!groupValid && group->GetLeaderGUID() == bot->GetGUID())
        {
            botAI->LeaveOrDisbandGroup();
            LOG_INFO("playerbots", "Disbanded bot-only group, leader {} back to the pool", bot->GetName().c_str());
        }
    }
}

"""
# insert implementation right before ProcessLfgFillTeleports definition
anchor = 'void RandomPlayerbotMgr::ProcessLfgFillTeleports()'
n3 = s2.count(anchor)
assert n3 == 1, f'impl anchor: {n3}'
s2 = s2.replace(anchor, impl + anchor)
open(p2, 'w', encoding='utf-8', newline='').write(s2)
print('cpp OK')
