p = 'modules/mod-playerbots/src/Bot/RandomPlayerbotMgr.cpp'
s = open(p, 'r', encoding='utf-8', newline='').read()

# 1. wire the call (replace the stub call I already inserted)
old_call = """    // The same pass cadence is right for orphaned bot groups: a raid whose
    // real players left lives minutes, not forever (report #157).
    DisbandBotOnlyGroups();
"""
assert s.count(old_call) == 1
print('call already wired')

# 2. replace the broken stub implementation with the real one
i = s.find('void RandomPlayerbotMgr::DisbandBotOnlyGroups()')
assert i >= 0, 'stub not found'
j = s.find('\n}\n', i)
assert j > i
old_impl = s[i:j + 3]
new_impl = """void RandomPlayerbotMgr::DisbandBotOnlyGroups()
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
            if (Player* member = ObjectAccessor::FindPlayer(slot.guid); member && !GET_PLAYERBOT_AI(member))
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
s = s.replace(old_impl, new_impl)
open(p, 'w', encoding='utf-8', newline='').write(s)
print('impl replaced')
