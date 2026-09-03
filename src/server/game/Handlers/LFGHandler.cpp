/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "DBCStores.h"
#include "GameTime.h"
#include "Group.h"
#include "LFGMgr.h"
#include "LFGPackets.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <sstream>

void BuildPlayerLockDungeonBlock(WorldPacket& data, lfg::LfgLockMap const& lock)
{
    data << uint32(lock.size());                           // Size of lock dungeons
    for (lfg::LfgLockMap::const_iterator it = lock.begin(); it != lock.end(); ++it)
    {
        data << uint32(it->first);                         // Dungeon entry (id + type)
        data << uint32(it->second);                        // Lock status
    }
}

void BuildPartyLockDungeonBlock(WorldPacket& data, lfg::LfgLockPartyMap const& lockMap)
{
    data << uint8(lockMap.size());
    for (lfg::LfgLockPartyMap::const_iterator it = lockMap.begin(); it != lockMap.end(); ++it)
    {
        data << it->first;                                 // Player guid
        BuildPlayerLockDungeonBlock(data, it->second);
    }
}

void WorldSession::HandleLfgJoinOpcode(WorldPackets::LFG::LFGJoin& packet)
{
    if (!sLFGMgr->isOptionEnabled(lfg::LFG_OPTION_ENABLE_DUNGEON_FINDER | lfg::LFG_OPTION_ENABLE_RAID_BROWSER | lfg::LFG_OPTION_ENABLE_SEASONAL_BOSSES) ||
        (GetPlayer()->GetGroup() && GetPlayer()->GetGroup()->GetLeaderGUID() != GetPlayer()->GetGUID() &&
         (GetPlayer()->GetGroup()->GetMembersCount() == MAXGROUPSIZE || !GetPlayer()->GetGroup()->isLFGGroup())))
        return;

    if (packet.Slots.empty())
    {
        LOG_DEBUG("lfg", "CMSG_LFG_JOIN {} no dungeons selected", GetPlayerInfo());
        return;
    }

    lfg::LfgDungeonSet newDungeons;
    for (uint32 slot : packet.Slots)
    {
        uint32 dungeon = slot & 0x00FFFFFF;                             // remove the type from the dungeon entry
        if (sLFGDungeonStore.LookupEntry(dungeon))
            newDungeons.insert(dungeon);
    }

    LOG_DEBUG("network", "CMSG_LFG_JOIN [{}] roles: {}, Dungeons: {}, Comment: {}",
                 GetPlayerInfo(), packet.Roles, newDungeons.size(), packet.Comment);

    sLFGMgr->JoinLfg(GetPlayer(), uint8(packet.Roles), newDungeons, packet.Comment);
    GetPlayer()->UpdateLFGChannel();
}

void WorldSession::HandleLfgLeaveOpcode(WorldPackets::LFG::LFGLeave& /*packet*/)
{
    Group* group = GetPlayer()->GetGroup();
    ObjectGuid guid = GetPlayer()->GetGUID();
    ObjectGuid gguid = group ? group->GetGUID() : guid;

    LOG_DEBUG("network", "CMSG_LFG_LEAVE [{}] in group: {}", guid.ToString(), group ? 1 : 0);

    // Check cheating - only leader can leave the queue
    if (!group || group->GetLeaderGUID() == guid)
    {
        sLFGMgr->LeaveLfg(sLFGMgr->GetState(guid) == lfg::LFG_STATE_RAIDBROWSER ? guid : gguid);
        sLFGMgr->LeaveAllLfgQueues(guid, true, group ? group->GetGUID() : ObjectGuid::Empty);
    }
    GetPlayer()->UpdateLFGChannel();
}

void WorldSession::HandleLfgProposalResultOpcode(WorldPacket& recvData)
{
    uint32 proposalID;                                      // Internal lfgGroupID
    bool accept;                                           // Accept to join?
    recvData >> proposalID;
    recvData >> accept;

    LOG_DEBUG("network", "CMSG_LFG_PROPOSAL_RESULT [{}] proposal: {} accept: {}", GetPlayer()->GetGUID().ToString(), proposalID, accept ? 1 : 0);
    sLFGMgr->UpdateProposal(proposalID, GetPlayer()->GetGUID(), accept);
}

void WorldSession::HandleLfgSetRolesOpcode(WorldPacket& recvData)
{
    uint8 roles;
    recvData >> roles;                                    // Player Group Roles
    ObjectGuid guid = GetPlayer()->GetGUID();
    Group* group = GetPlayer()->GetGroup();
    if (!group)
    {
        LOG_DEBUG("network", "CMSG_LFG_SET_ROLES [{}] Not in group", guid.ToString());
        return;
    }
    ObjectGuid gguid = group->GetGUID();
    LOG_DEBUG("network", "CMSG_LFG_SET_ROLES: Group [{}], Player [{}], Roles: {}", gguid.ToString(), guid.ToString(), roles);
    sLFGMgr->UpdateRoleCheck(gguid, guid, roles);
}

void WorldSession::HandleLfgSetCommentOpcode(WorldPacket&  recvData)
{
    std::string comment;
    recvData >> comment;
    ObjectGuid guid = GetPlayer()->GetGUID();
    LOG_DEBUG("network", "CMSG_LFG_SET_COMMENT [{}] comment: {}", guid.ToString(), comment);

    sLFGMgr->SetComment(GetPlayer()->GetGUID(), comment);
    sLFGMgr->LfrSetComment(GetPlayer(), comment);
}

void WorldSession::HandleLfgSetBootVoteOpcode(WorldPacket& recvData)
{
    bool agree;                                            // Agree to kick player
    recvData >> agree;

    ObjectGuid guid = GetPlayer()->GetGUID();
    LOG_DEBUG("network", "CMSG_LFG_SET_BOOT_VOTE [{}] agree: {}", guid.ToString(), agree ? 1 : 0);
    sLFGMgr->UpdateBoot(guid, agree);
}

void WorldSession::HandleLfgTeleportOpcode(WorldPacket& recvData)
{
    bool out;
    recvData >> out;

    LOG_DEBUG("network", "CMSG_LFG_TELEPORT [{}] out: {}", GetPlayer()->GetGUID().ToString(), out ? 1 : 0);
    sLFGMgr->TeleportPlayer(GetPlayer(), out);
}

void WorldSession::HandleLfgPlayerLockInfoRequestOpcode(WorldPacket& /*recvData*/)
{
    ObjectGuid guid = GetPlayer()->GetGUID();
    LOG_DEBUG("network", "CMSG_LFG_PLAYER_LOCK_INFO_REQUEST [{}]", guid.ToString());

    // Get Random dungeons that can be done at a certain level and expansion
    uint8 level = GetPlayer()->GetLevel();
    lfg::LfgDungeonSet const& randomDungeons =
        sLFGMgr->GetRandomAndSeasonalDungeons(level, GetPlayer()->GetSession()->Expansion());

    // Get player locked Dungeons
    sLFGMgr->InitializeLockedDungeons(GetPlayer(), GetPlayer()->GetGroup()); // pussywizard
    lfg::LfgLockMap const& lock = sLFGMgr->GetLockedDungeons(guid);
    uint32 rsize = uint32(randomDungeons.size());
    uint32 lsize = uint32(lock.size());

    LOG_DEBUG("network", "SMSG_LFG_PLAYER_INFO [{}]", guid.ToString());
    WorldPacket data(SMSG_LFG_PLAYER_INFO, 1 + rsize * (4 + 1 + 4 + 4 + 4 + 4 + 1 + 4 + 4 + 4) + 4 + lsize * (1 + 4 + 4 + 4 + 4 + 1 + 4 + 4 + 4));

    data << uint8(randomDungeons.size());                  // Random Dungeon count
    for (lfg::LfgDungeonSet::const_iterator it = randomDungeons.begin(); it != randomDungeons.end(); ++it)
    {
        data << uint32(*it);                               // Dungeon Entry (id + type)
        lfg::LfgReward const* reward = sLFGMgr->GetRandomDungeonReward(*it, level);
        Quest const* quest = nullptr;
        bool done = false;
        if (reward)
        {
            quest = sObjectMgr->GetQuestTemplate(reward->firstQuest);
            if (quest)
            {
                done = !GetPlayer()->CanRewardQuest(quest, false);
                if (done)
                    quest = sObjectMgr->GetQuestTemplate(reward->otherQuest);
            }
        }

        if (quest)
        {
            uint8 playerLevel = GetPlayer() ? GetPlayer()->GetLevel() : 0;
            uint8 playerLevelForXP = playerLevel;
            sScriptMgr->OnPlayerBeforeGetLevelForXPGain(GetPlayer(), playerLevelForXP);

            data << uint8(done);
            data << uint32(quest->GetRewOrReqMoney(playerLevel));
            if (playerLevelForXP < GetPlayer()->GetUInt32Value(PLAYER_FIELD_MAX_LEVEL))
                data << uint32(quest->XPValue(playerLevelForXP));
            else
                data << uint32(0);
            data << uint32(0);
            data << uint32(0);
            data << uint8(quest->GetRewItemsCount());
            if (quest->GetRewItemsCount())
            {
                for (uint8 i = 0; i < QUEST_REWARDS_COUNT; ++i)
                    if (uint32 itemId = quest->RewardItemId[i])
                    {
                        ItemTemplate const* item = sObjectMgr->GetItemTemplate(itemId);
                        data << uint32(itemId);
                        data << uint32(item ? item->DisplayInfoID : 0);
                        data << uint32(quest->RewardItemIdCount[i]);
                    }
            }
        }
        else
        {
            data << uint8(0);
            data << uint32(0);
            data << uint32(0);
            data << uint32(0);
            data << uint32(0);
            data << uint8(0);
        }
    }
    BuildPlayerLockDungeonBlock(data, lock);
    SendPacket(&data);
}

void WorldSession::HandleLfgPartyLockInfoRequestOpcode(WorldPacket&  /*recvData*/)
{
    ObjectGuid guid = GetPlayer()->GetGUID();
    LOG_DEBUG("network", "CMSG_LFG_PARTY_LOCK_INFO_REQUEST [{}]", guid.ToString());

    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    // Get the locked dungeons of the other party members
    lfg::LfgLockPartyMap lockMap;
    for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        Player* plrg = itr->GetSource();
        if (!plrg)
            continue;

        ObjectGuid pguid = plrg->GetGUID();
        if (pguid == guid)
            continue;

        sLFGMgr->InitializeLockedDungeons(plrg, group); // pussywizard
        lockMap[pguid] = sLFGMgr->GetLockedDungeons(pguid);
    }

    uint32 size = 0;
    for (lfg::LfgLockPartyMap::const_iterator it = lockMap.begin(); it != lockMap.end(); ++it)
        size += 8 + 4 + uint32(it->second.size()) * (4 + 4);

    LOG_DEBUG("network", "SMSG_LFG_PARTY_INFO [{}]", guid.ToString());
    WorldPacket data(SMSG_LFG_PARTY_INFO, 1 + size);
    BuildPartyLockDungeonBlock(data, lockMap);
    SendPacket(&data);
}

void WorldSession::HandleLfrSearchJoinOpcode(WorldPacket& recvData)
{
    uint32 dungeonId;
    recvData >> dungeonId;
    dungeonId = (dungeonId & 0x00FFFFFF); // remove the type from the dungeon entry
    sLFGMgr->LfrSearchAdd(GetPlayer(), dungeonId);
    sLFGMgr->SendRaidBrowserCachedList(GetPlayer(), dungeonId);
}

void WorldSession::HandleLfrSearchLeaveOpcode(WorldPacket& recvData)
{
    uint32 dungeonId;
    recvData >> dungeonId;
    sLFGMgr->LfrSearchRemove(GetPlayer());
}

void WorldSession::HandleLfgGetStatus(WorldPacket& /*recvData*/)
{
    LOG_DEBUG("lfg", "CMSG_LFG_GET_STATUS {}", GetPlayerInfo());

    ObjectGuid guid = GetPlayer()->GetGUID();
    lfg::LfgUpdateData updateData = sLFGMgr->GetLfgStatus(guid);

    if (GetPlayer()->GetGroup())
    {
        SendLfgUpdateParty(updateData);
        updateData.dungeons.clear();
        SendLfgUpdatePlayer(updateData);
    }
    else
    {
        SendLfgUpdatePlayer(updateData);
        updateData.dungeons.clear();
        SendLfgUpdateParty(updateData);
    }
}

void WorldSession::SendLfgUpdatePlayer(lfg::LfgUpdateData const& updateData)
{
    bool queued = false;
    uint8 size = uint8(updateData.dungeons.size());

    switch (updateData.updateType)
    {
        case lfg::LFG_UPDATETYPE_JOIN_QUEUE:
        case lfg::LFG_UPDATETYPE_ADDED_TO_QUEUE:
            queued = true;
            break;
        case lfg::LFG_UPDATETYPE_UPDATE_STATUS:
            queued = updateData.state == lfg::LFG_STATE_QUEUED;
            break;
        default:
            break;
    }

    LOG_DEBUG("lfg", "SMSG_LFG_UPDATE_PLAYER {} updatetype: {}",
                   GetPlayerInfo(), updateData.updateType);
    WorldPacket data(SMSG_LFG_UPDATE_PLAYER, 1 + 1 + (size > 0 ? 1 : 0) * (1 + 1 + 1 + 1 + size * 4 + updateData.comment.length()));
    data << uint8(updateData.updateType);                  // Lfg Update type
    data << uint8(size > 0);                               // Extra info
    if (size)
    {
        data << uint8(queued);                             // Join the queue
        data << uint8(0);                                  // unk - Always 0
        data << uint8(0);                                  // unk - Always 0

        data << uint8(size);
        for (lfg::LfgDungeonSet::const_iterator it = updateData.dungeons.begin(); it != updateData.dungeons.end(); ++it)
            data << uint32(*it);
        data << updateData.comment;
    }
    SendPacket(&data);
}

void WorldSession::SendLfgUpdateParty(lfg::LfgUpdateData const& updateData)
{
    bool join = false;
    bool queued = false;
    uint8 size = uint8(updateData.dungeons.size());

    switch (updateData.updateType)
    {
        case lfg::LFG_UPDATETYPE_ADDED_TO_QUEUE:                // Rolecheck Success
            queued = true;
            [[fallthrough]];
        case lfg::LFG_UPDATETYPE_PROPOSAL_BEGIN:
            join = true;
            break;
        case lfg::LFG_UPDATETYPE_UPDATE_STATUS:
            join = updateData.state != lfg::LFG_STATE_ROLECHECK && updateData.state != lfg::LFG_STATE_NONE;
            queued = updateData.state == lfg::LFG_STATE_QUEUED;
            break;
        default:
            break;
    }

    LOG_DEBUG("lfg", "SMSG_LFG_UPDATE_PARTY {} updatetype: {}",
                   GetPlayerInfo(), updateData.updateType);
    WorldPacket data(SMSG_LFG_UPDATE_PARTY, 1 + 1 + (size > 0 ? 1 : 0) * (1 + 1 + 1 + 1 + 1 + size * 4 + updateData.comment.length()));
    data << uint8(updateData.updateType);                  // Lfg Update type
    data << uint8(size > 0);                               // Extra info
    if (size)
    {
        data << uint8(join);                               // LFG Join
        data << uint8(queued);                             // Join the queue
        data << uint8(0);                                  // unk - Always 0
        data << uint8(0);                                  // unk - Always 0
        for (uint8 i = 0; i < 3; ++i)
            data << uint8(0);                              // unk - Always 0

        data << uint8(size);
        for (lfg::LfgDungeonSet::const_iterator it = updateData.dungeons.begin(); it != updateData.dungeons.end(); ++it)
            data << uint32(*it);
        data << updateData.comment;
    }
    SendPacket(&data);
}

void WorldSession::SendLfgRoleChosen(ObjectGuid guid, uint8 roles)
{
    LOG_DEBUG("network", "SMSG_LFG_ROLE_CHOSEN [{}] guid: [{}] roles: {}", GetPlayer()->GetGUID().ToString(), guid.ToString(), roles);

    WorldPacket data(SMSG_LFG_ROLE_CHOSEN, 8 + 1 + 4);
    data << guid;                                          // Guid
    data << uint8(roles > 0);                              // Ready
    data << uint32(roles);                                 // Roles
    SendPacket(&data);
}

void WorldSession::SendLfgRoleCheckUpdate(lfg::LfgRoleCheck const& roleCheck)
{
    lfg::LfgDungeonSet dungeons;
    if (roleCheck.rDungeonId)
        dungeons.insert(roleCheck.rDungeonId);
    else
        dungeons = roleCheck.dungeons;

    LOG_DEBUG("network", "SMSG_LFG_ROLE_CHECK_UPDATE [{}]", GetPlayer()->GetGUID().ToString());
    WorldPacket data(SMSG_LFG_ROLE_CHECK_UPDATE, 4 + 1 + 1 + dungeons.size() * 4 + 1 + roleCheck.roles.size() * (8 + 1 + 4 + 1));

    data << uint32(roleCheck.state);                       // Check result
    data << uint8(roleCheck.state == lfg::LFG_ROLECHECK_INITIALITING);
    data << uint8(dungeons.size());                        // Number of dungeons
    if (!dungeons.empty())
        for (lfg::LfgDungeonSet::iterator it = dungeons.begin(); it != dungeons.end(); ++it)
            data << uint32(sLFGMgr->GetLFGDungeonEntry(*it)); // Dungeon

    data << uint8(roleCheck.roles.size());                 // Players in group
    if (!roleCheck.roles.empty())
    {
        // Leader info MUST be sent 1st :S
        ObjectGuid guid = roleCheck.leader;
        uint8 roles = roleCheck.roles.find(guid)->second;
        data << guid;                                      // Guid
        data << uint8(roles > 0);                          // Ready
        data << uint32(roles);                             // Roles
        Player* player = ObjectAccessor::FindConnectedPlayer(guid);
        data << uint8(player ? player->GetLevel() : 0);    // Level

        for (lfg::LfgRolesMap::const_iterator it = roleCheck.roles.begin(); it != roleCheck.roles.end(); ++it)
        {
            if (it->first == roleCheck.leader)
                continue;

            guid = it->first;
            roles = it->second;
            data << guid;                                  // Guid
            data << uint8(roles > 0);                      // Ready
            data << uint32(roles);                         // Roles
            player = ObjectAccessor::FindConnectedPlayer(guid);
            data << uint8(player ? player->GetLevel() : 0);// Level
        }
    }
    SendPacket(&data);
}

void WorldSession::SendLfgJoinResult(lfg::LfgJoinResultData const& joinData)
{
    uint32 size = 0;
    for (lfg::LfgLockPartyMap::const_iterator it = joinData.lockmap.begin(); it != joinData.lockmap.end(); ++it)
        size += 8 + 4 + uint32(it->second.size()) * (4 + 4);

    LOG_DEBUG("network", "SMSG_LFG_JOIN_RESULT [{}] checkResult: {} checkValue: {}", GetPlayer()->GetGUID().ToString(), joinData.result, joinData.state);
    WorldPacket data(SMSG_LFG_JOIN_RESULT, 4 + 4 + size);
    data << uint32(joinData.result);                       // Check Result
    data << uint32(joinData.state);                        // Check Value
    if (!joinData.lockmap.empty())
        BuildPartyLockDungeonBlock(data, joinData.lockmap);
    SendPacket(&data);
}

void WorldSession::SendLfgQueueStatus(lfg::LfgQueueStatusData const& queueData)
{
    LOG_DEBUG("network", "SMSG_LFG_QUEUE_STATUS [{}] dungeon: {} - waitTime: {} - avgWaitTime: {} - waitTimeTanks: {} - waitTimeHealer: {} - waitTimeDps: {} - queuedTime: {} - tanks: {} - healers: {} - dps: {}",
                   GetPlayer()->GetGUID().ToString(), queueData.dungeonId, queueData.waitTime, queueData.waitTimeAvg, queueData.waitTimeTank,
                   queueData.waitTimeHealer, queueData.waitTimeDps, queueData.queuedTime, queueData.tanks, queueData.healers, queueData.dps);
    WorldPacket data(SMSG_LFG_QUEUE_STATUS, 4 + 4 + 4 + 4 + 4 + 4 + 1 + 1 + 1 + 4);
    data << uint32(queueData.dungeonId);                   // Dungeon
    data << int32(queueData.waitTimeAvg);                  // Average Wait time
    data << int32(queueData.waitTime);                     // Wait Time
    data << int32(queueData.waitTimeTank);                 // Wait Tanks
    data << int32(queueData.waitTimeHealer);               // Wait Healers
    data << int32(queueData.waitTimeDps);                  // Wait Dps
    data << uint8(queueData.tanks);                        // Tanks needed
    data << uint8(queueData.healers);                      // Healers needed
    data << uint8(queueData.dps);                          // Dps needed
    data << uint32(queueData.queuedTime);                  // Player wait time in queue
    SendPacket(&data);
}

void WorldSession::SendLfgPlayerReward(lfg::LfgPlayerRewardData const& rewardData)
{
    if (!rewardData.rdungeonEntry || !rewardData.sdungeonEntry || !rewardData.quest)
        return;

    LOG_DEBUG("lfg", "SMSG_LFG_PLAYER_REWARD {} rdungeonEntry: {}, sdungeonEntry: {}, done: {}",
                   GetPlayerInfo(), rewardData.rdungeonEntry, rewardData.sdungeonEntry, rewardData.done);

    uint8 itemNum = rewardData.quest->GetRewItemsCount();

    uint8 playerLevel = GetPlayer() ? GetPlayer()->GetLevel() : 0;
    uint8 playerLevelForXP = playerLevel;
    sScriptMgr->OnPlayerBeforeGetLevelForXPGain(GetPlayer(), playerLevelForXP);

    WorldPacket data(SMSG_LFG_PLAYER_REWARD, 4 + 4 + 1 + 4 + 4 + 4 + 4 + 4 + 1 + itemNum * (4 + 4 + 4));
    data << uint32(rewardData.rdungeonEntry);              // Random Dungeon Finished
    data << uint32(rewardData.sdungeonEntry);              // Dungeon Finished
    data << uint8(rewardData.done);
    data << uint32(1);
    data << uint32(rewardData.quest->GetRewOrReqMoney(playerLevel));
    data << uint32(rewardData.quest->XPValue(playerLevelForXP));
    data << uint32(0);
    data << uint32(0);
    data << uint8(itemNum);
    if (itemNum)
    {
        for (uint8 i = 0; i < QUEST_REWARDS_COUNT; ++i)
            if (uint32 itemId = rewardData.quest->RewardItemId[i])
            {
                ItemTemplate const* item = sObjectMgr->GetItemTemplate(itemId);
                data << uint32(itemId);
                data << uint32(item ? item->DisplayInfoID : 0);
                data << uint32(rewardData.quest->RewardItemIdCount[i]);
            }
    }
    SendPacket(&data);
}

void WorldSession::SendLfgBootProposalUpdate(lfg::LfgPlayerBoot const& boot)
{
    ObjectGuid guid = GetPlayer()->GetGUID();
    lfg::LfgAnswer playerVote = boot.votes.find(guid)->second;
    uint8 votesNum = 0;
    uint8 agreeNum = 0;
    uint32 secsleft = boot.cancelTime - GameTime::GetGameTime().count();
    for (lfg::LfgAnswerContainer::const_iterator it = boot.votes.begin(); it != boot.votes.end(); ++it)
    {
        if (it->second != lfg::LFG_ANSWER_PENDING)
        {
            ++votesNum;
            if (it->second == lfg::LFG_ANSWER_AGREE)
                ++agreeNum;
        }
    }
    LOG_DEBUG("network", "SMSG_LFG_BOOT_PROPOSAL_UPDATE [{}] inProgress: {} - didVote: {} - agree: {} - victim: [{}] votes: {} - agrees: {} - left: {} - needed: {} - reason {}",
                   guid.ToString(), uint8(boot.inProgress), uint8(playerVote != lfg::LFG_ANSWER_PENDING), uint8(playerVote == lfg::LFG_ANSWER_AGREE),
                   boot.victim.ToString(), votesNum, agreeNum, secsleft, lfg::LFG_GROUP_KICK_VOTES_NEEDED, boot.reason);
    WorldPacket data(SMSG_LFG_BOOT_PROPOSAL_UPDATE, 1 + 1 + 1 + 8 + 4 + 4 + 4 + 4 + boot.reason.length());
    data << uint8(boot.inProgress);                        // Vote in progress
    data << uint8(playerVote != lfg::LFG_ANSWER_PENDING);  // Did Vote
    data << uint8(playerVote == lfg::LFG_ANSWER_AGREE);    // Agree
    data << boot.victim;                                   // Victim GUID
    data << uint32(votesNum);                              // Total Votes
    data << uint32(agreeNum);                              // Agree Count
    data << uint32(secsleft);                              // Time Left
    data << uint32(lfg::LFG_GROUP_KICK_VOTES_NEEDED);      // Needed Votes
    data << boot.reason.c_str();                           // Kick reason
    SendPacket(&data);
}

// What goes in SMSG_LFG_PROPOSAL_UPDATE's role field: the AzerothCore role
// bitmask, unchanged apart from guaranteeing that at least one role bit is set.
//
// This was guessed at four times before anyone read the client. It is now
// settled by disassembly, so do not re-theorise it. The 3.3.5a 12340 Wow.exe
// turns that field into the Lua string at two call sites -- 0x00553093
// (GetLFGProposal, your own role) and 0x00554255 (GetLFGProposalMember) -- and
// both run the identical sequence:
//
//     a8 02  test al, 2      -> [0x00ad87b0] "TANK"
//     a8 04  test al, 4      -> [0x00ad87b4] "HEALER"
//     a8 08  test al, 8      -> [0x00ad87b8] "DAMAGER"
//     otherwise                 push 0x009e4110 "UNKNOWN"
//
// It is a BIT TEST, not an index, and the bit values are exactly AzerothCore's.
// Bit 0 is read separately (and eax, 1) as the isLeader return, which is why
// the leader bit belongs in the field rather than being stripped -- stripping
// it is what cost the ready popup its leader crown.
//
// "UNKNOWN" is the fallback for a value with none of bits 2/4/8, and it is the
// only way GetTexCoordsForRole (LFGFrame.lua:337) can be handed it, since that
// function error()s on anything but GUIDE/TANK/HEALER/DAMAGER. So the Lua error
//
//     Interface\FrameXML\LFGFrame.lua:337: Unknown role: UNKNOWN
//
// means, and can only mean, that a proposal member reached the client with a
// role mask holding none of those three bits. In practice that was never a
// member we SENT -- it was the slots we did not send, which the client reads
// anyway. See the padding loop at the end of SendLfgUpdateProposal.
//
// That also explains the one observation that got a correct fix reverted:
// 0.1.74 sent 0/1/2 as a table index, and index 2 (our "damage") is the TANK
// bit, so rogues were drawn as tanks while indices 0 and 1 -- carrying no role
// bit at all -- threw this very error. The index theory was self-refuting.
//
// tools/client-patch/dump_lfg_role_table.py reprints the bytes above.
static uint8 ProposalRoleForClient(uint8 role)
{
    // Keep the leader bit and whatever role bits the queue recorded; only
    // substitute when there is no role at all, which the client cannot draw.
    if (role & (lfg::PLAYER_ROLE_TANK | lfg::PLAYER_ROLE_HEALER | lfg::PLAYER_ROLE_DAMAGE))
        return role;

    return uint8((role & lfg::PLAYER_ROLE_LEADER) | lfg::PLAYER_ROLE_DAMAGE);
}

void WorldSession::SendLfgUpdateProposal(lfg::LfgProposal const& proposal)
{
    ObjectGuid guid = GetPlayer()->GetGUID();
    ObjectGuid gguid = proposal.players.find(guid)->second.group;
    bool silent = !proposal.isNew && gguid == proposal.group;
    uint32 dungeonEntry = proposal.dungeonId;

    LOG_DEBUG("network", "SMSG_LFG_PROPOSAL_UPDATE [{} state: {}", guid.ToString(), proposal.state);

    // show random dungeon if player selected random dungeon and it's not lfg group
    if (!silent)
    {
        lfg::LfgDungeonSet const& playerDungeons = sLFGMgr->GetSelectedDungeons(guid);
        if (playerDungeons.find(proposal.dungeonId) == playerDungeons.end())
            dungeonEntry = (*playerDungeons.begin());
    }

    dungeonEntry = sLFGMgr->GetLFGDungeonEntry(dungeonEntry);

    WorldPacket data(SMSG_LFG_PROPOSAL_UPDATE, 4 + 1 + 4 + 4 + 1 + 1 + MAXGROUPSIZE * (4 + 1 + 1 + 1 + 1 + 1));
    data << uint32(dungeonEntry);                          // Dungeon
    data << uint8(proposal.state);                         // Proposal state
    data << uint32(proposal.id);                           // Proposal ID
    data << uint32(proposal.encounters);                   // encounters done
    data << uint8(silent);                                 // Show proposal window
    // ALWAYS MAXGROUPSIZE, however many members the proposal really has. See
    // the padding loop at the end of this function for why.
    data << uint8(MAXGROUPSIZE);                           // Group size

    // Sort by roles: tank, healer, dps
    std::vector<ObjectGuid> ordered;
    ordered.reserve(proposal.players.size());

    std::vector<ObjectGuid> tanks, healers, dps;

    for (auto const& [pguid, player] : proposal.players)
    {
        // Sort on the mask actually sent, tested in the same order the client
        // tests it, so the row order matches the icon each row will draw.
        uint8 const sortRole = ProposalRoleForClient(player.role);
        if (sortRole & lfg::PLAYER_ROLE_TANK)
            tanks.push_back(pguid);
        else if (sortRole & lfg::PLAYER_ROLE_HEALER)
            healers.push_back(pguid);
        else
            dps.push_back(pguid);
    }

    ordered.insert(ordered.end(), tanks.begin(), tanks.end());
    ordered.insert(ordered.end(), healers.begin(), healers.end());
    ordered.insert(ordered.end(), dps.begin(), dps.end());

    // The client keeps exactly MAXGROUPSIZE member slots and GetLFGProposalMember
    // refuses an index outside 1..5 (cmp eax, 5 / jae bail at 0x0055306b), so
    // anything past the fifth member is unreachable by the UI. Drop it rather
    // than hand the client a count it cannot represent -- but never drop the
    // recipient, because GetLFGProposal locates "you" by scanning those same
    // five slots for the self flag and returns nothing useful if it is absent.
    if (ordered.size() > size_t(MAXGROUPSIZE))
    {
        auto const self = std::find(ordered.begin(), ordered.end(), guid);
        if (self != ordered.end() && std::distance(ordered.begin(), self) >= MAXGROUPSIZE)
            std::iter_swap(ordered.begin() + (MAXGROUPSIZE - 1), self);

        ordered.resize(MAXGROUPSIZE);
    }

    uint32 slot = 0;
    for (auto const& pguid : ordered)
    {
        lfg::LfgProposalPlayer const& player = proposal.players.find(pguid)->second;
        ++slot; // 1-based, and it is what the client draws as its Nth icon

        uint8 const roleOut = ProposalRoleForClient(player.role);

        // A member with no role bits is a genuine upstream bug -- a bot placed
        // into the group outside the queue, or a proposal formed without
        // CheckGroupRoles running. ProposalRoleForClient covers for it so the
        // client still has something to draw; say so rather than hide it.
        if (!(player.role & (lfg::PLAYER_ROLE_TANK | lfg::PLAYER_ROLE_HEALER | lfg::PLAYER_ROLE_DAMAGE)))
            LOG_ERROR("lfg", "SMSG_LFG_PROPOSAL_UPDATE: proposal {} slot {} member [{}] has no LFG role at all (mask {}), sending damage",
                proposal.id, slot, pguid.ToString(), uint32(player.role));

        data << uint32(roleOut);                           // Role
        data << uint8(pguid == guid);                      // Self player
        if (!player.group)                                 // Player not in a group
        {
            data << uint8(0);                              // Not in dungeon
            data << uint8(0);                              // Not same group
        }
        else
        {
            data << uint8(player.group == proposal.group); // In dungeon (silent)
            data << uint8(player.group == gguid);          // Same Group than player
        }
        data << uint8(player.accept != lfg::LFG_ANSWER_PENDING);// Answered
        data << uint8(player.accept == lfg::LFG_ANSWER_AGREE);  // Accepted
    }
    // PAD TO MAXGROUPSIZE. This is the fix for
    //
    //     Interface\FrameXML\LFGFrame.lua:337: Unknown role: UNKNOWN
    //
    // and it is a server-side fix for a hard-coded client assumption.
    //
    // GetLFGProposal does NOT return the member count this packet carries. It
    // pushes a constant: `fld qword [0x009f5988]` at 0x005542fd, and that
    // qword is 5.0. So LFDDungeonReadyPopup_Update always runs
    //
    //     for i=1, numMembers do LFDDungeonReadyStatus_UpdateIcon(...Player"..i)
    //
    // five times, and the following `for i=numMembers+1, NUM_LFD_MEMBERS` loop
    // that would hide unused rows is 6..5 and never runs. Every slot this
    // packet did not fill is read out of stale or zeroed client memory, comes
    // back with no role bit, and throws the Lua error above -- once per
    // unfilled slot.
    //
    // Retail never hit this because a proposal there is always exactly five.
    // Bonesaw's queue deliberately pops incomplete ones -- LFGQueue.cpp:339,
    // allowIncomplete = hasSoloQueue() || raidQueue || allowBotFill -- and
    // fills the group with bots on accept, so two-member proposals are normal
    // here and cost the player three Lua errors per pop.
    //
    // The padding describes what the group is about to become: the bot fill
    // brings it to five, and the fill produces dps once the tank and healer
    // slots are covered. Marking them answered+accepted keeps the popup from
    // showing three seats waiting forever on players who do not exist yet.
    for (size_t filled = ordered.size(); filled < size_t(MAXGROUPSIZE); ++filled)
    {
        data << uint32(lfg::PLAYER_ROLE_DAMAGE);           // Role
        data << uint8(0);                                  // Self player
        data << uint8(0);                                  // Not in dungeon
        data << uint8(0);                                  // Not same group
        data << uint8(1);                                  // Answered
        data << uint8(1);                                  // Accepted
    }

    // Layout, confirmed against a byte capture of a live 2-member proposal:
    //   uint32 dungeonEntry, uint8 state, uint32 proposalId, uint32 encounters,
    //   uint8 silent, uint8 memberCount,
    //   then per member: uint32 role, uint8 self, uint8 inDungeon,
    //                    uint8 sameGroup, uint8 answered, uint8 accepted
    // i.e. 15 + 9 * memberCount, so 60 bytes now that the count is always five.
    SendPacket(&data);
}

void WorldSession::SendLfgLfrList(bool update)
{
    LOG_DEBUG("network", "SMSG_LFG_LFR_LIST [{}] update: {}", GetPlayer()->GetGUID().ToString(), update ? 1 : 0);
    WorldPacket data(SMSG_LFG_UPDATE_SEARCH, 1);
    data << uint8(update);                                 // In Lfg Queue?
    SendPacket(&data);
}

void WorldSession::SendLfgDisabled()
{
    LOG_DEBUG("network", "SMSG_LFG_DISABLED [{}]", GetPlayer()->GetGUID().ToString());
    WorldPacket data(SMSG_LFG_DISABLED, 0);
    SendPacket(&data);
}

void WorldSession::SendLfgOfferContinue(uint32 dungeonEntry)
{
    LOG_DEBUG("network", "SMSG_LFG_OFFER_CONTINUE [{}] dungeon entry: {}", GetPlayer()->GetGUID().ToString(), dungeonEntry);
    WorldPacket data(SMSG_LFG_OFFER_CONTINUE, 4);
    data << uint32(dungeonEntry);
    SendPacket(&data);
}

void WorldSession::SendLfgTeleportError(uint8 err)
{
    LOG_DEBUG("network", "SMSG_LFG_TELEPORT_DENIED [{}] reason: {}", GetPlayer()->GetGUID().ToString(), err);
    WorldPacket data(SMSG_LFG_TELEPORT_DENIED, 4);
    data << uint32(err);                                   // Error
    SendPacket(&data);
}
