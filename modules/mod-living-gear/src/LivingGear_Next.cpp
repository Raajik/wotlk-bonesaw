#include "AllCreatureScript.h"
#include "AllGameObjectScript.h"
#include "AllSpellScript.h"
#include "CellImpl.h"
#include "Chat.h"
#include "Config.h"
#include "DBCEnums.h"
#include "DatabaseEnv.h"
#include "DynamicObject.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "Item.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "Spell.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "Timer.h"
#include "Unit.h"
#include "WorldSession.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace LivingGearNext
{
uint32 const SPELL_CLASS_BUFFS = 910106;
uint32 const SPELL_RIDING_SHARE = 910107;
uint32 const SPELL_AUTO_ACCEPT = 910108;
uint32 const SPELL_AUTOLOOT = 910008;
uint32 const SPELL_PALADIN_HOLY = 910069;
uint32 const SPELL_PALADIN_RETRIBUTION = 910071;
uint32 const SPELL_HAND_OF_FREEDOM = 1044;
uint32 const SPELL_CONSECRATION = 26573;
uint32 const SPELL_AVENGERS_SHIELD = 31935;
uint32 const NPC_KELTHUZAD = 15990;
uint32 const MAP_NAXXRAMAS = 533;
uint32 const QUEST_RESPAWN_CAP = 10;
float const CLASS_BUFF_PCT = 10.0f;
float const MENTOR_XP = 2.0f;
float const AS_DAMAGE_MULT = 4.0f;
uint32 const AS_CD_MS = 5000;
uint32 const SPEED_CAP_DEFAULT = 500;

struct NextState
{
    uint32 speedCapPct = SPEED_CAP_DEFAULT;
    uint32 ridingSkill = 0;
    uint32 ridingMax = 0;
    uint32 ridingStep = 0;
    uint32 hofTarget = 0;
    uint32 lastTickMs = 0;
    bool classBuffOn = false;
    uint8 botOrigLevel = 0;
    bool weaponPeakOn = false;
    float peakStr = 0.f;
    float peakAgi = 0.f;
    float peakSta = 0.f;
    float peakInt = 0.f;
    float peakSpi = 0.f;
};

std::unordered_map<uint32, NextState> g_state;
std::unordered_map<uint32, std::unordered_set<uint8>> g_classBuffUnlock;
std::unordered_map<uint32, uint32> g_accountRiding;
bool g_metaReady = false;
bool g_hasSpeedCapCol = false;
bool g_hasRidingCol = false;
bool g_hasClassBuffTable = false;

void DetectNextSchema()
{
    if (g_metaReady)
        return;
    g_metaReady = true;
    if (QueryResult cols = CharacterDatabase.Query(
        "SELECT `COLUMN_NAME` FROM `information_schema`.`COLUMNS` "
        "WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = 'lg_account_meta'"))
    {
        do
        {
            std::string const name = (*cols)[0].Get<std::string>();
            if (name == "speed_cap")
                g_hasSpeedCapCol = true;
            else if (name == "riding_skill")
                g_hasRidingCol = true;
        } while (cols->NextRow());
    }
    if (QueryResult tables = CharacterDatabase.Query(
        "SELECT COUNT(*) FROM `information_schema`.`TABLES` "
        "WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = 'lg_class_buff_unlock'"))
        g_hasClassBuffTable = (*tables)[0].Get<uint64>() > 0;
}

void SendLine(Player* player, std::string const& line)
{
    if (!player || !player->GetSession())
        return;
    player->Whisper(std::string("LG\t") + line, LANG_ADDON, player);
}

bool HasPerk(Player* player, uint32 spellId)
{
    return player && player->HasSpell(spellId);
}

// 2026-08-21: every caller in this file (Class Buffs, Riding, Auto-Accept)
// is a pure account-perk flag with no CASTABLE_SPELLS/SkillLineAbility
// entry -- player->learnSpell() marked them "known" with nothing to
// categorize them, so the client dumped them into the General spellbook
// tab regardless of being excluded from CASTABLE_SPELLS client-side.
// HasPerk()'s g_perks[acc]/DB fallback (LivingGear_Perks.cpp) works fine
// without ever calling learnSpell, so this no longer does.
void UnlockPerk(Player* player, uint32 spellId)
{
    if (!player || !player->GetSession() || !sSpellMgr->GetSpellInfo(spellId))
        return;
    uint32 const accountId = player->GetSession()->GetAccountId();
    CharacterDatabase.DirectExecute(
        "INSERT IGNORE INTO `lg_account_perk` (`account_id`, `spell_id`) VALUES ({}, {})",
        accountId, spellId);
}

bool RankOf(SpellInfo const* info, uint32 firstId)
{
    if (!info)
        return false;
    uint32 const first = sSpellMgr->GetFirstSpellInChain(firstId);
    return first && sSpellMgr->GetFirstSpellInChain(info->Id) == first;
}

NextState& StateFor(Player* player)
{
    return g_state[player->GetGUID().GetCounter()];
}

uint32 SpeedCapPct(Player* player)
{
    if (!player)
        return SPEED_CAP_DEFAULT;
    uint32 pct = StateFor(player).speedCapPct;
    if (pct < 100)
        pct = 100;
    if (pct > SPEED_CAP_DEFAULT)
        pct = SPEED_CAP_DEFAULT;
    return pct;
}

void ApplySpeedCap(Player* player)
{
    if (!player || !player->IsInWorld())
        return;
    float const cap = float(SpeedCapPct(player)) / 100.0f;
    if (player->GetSpeedRate(MOVE_RUN) > cap)
        player->SetSpeed(MOVE_RUN, cap, true);
    if (player->GetSpeedRate(MOVE_SWIM) > cap)
        player->SetSpeed(MOVE_SWIM, cap, true);
    if (player->GetSpeedRate(MOVE_FLIGHT) > cap)
        player->SetSpeed(MOVE_FLIGHT, cap, true);
}

void LoadClassBuffUnlock(uint32 accountId)
{
    if (g_classBuffUnlock.find(accountId) != g_classBuffUnlock.end())
        return;
    DetectNextSchema();
    auto& set = g_classBuffUnlock[accountId];
    if (!g_hasClassBuffTable)
        return;
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT `class` FROM `lg_class_buff_unlock` WHERE `account_id` = {}", accountId))
    {
        do
            set.insert((*result)[0].Get<uint8>());
        while (result->NextRow());
    }
}

void NoteClassBuffUnlock(Player* player)
{
    if (!player || !player->GetSession())
        return;
    uint32 const accountId = player->GetSession()->GetAccountId();
    uint8 const cls = player->getClass();
    LoadClassBuffUnlock(accountId);
    if (g_classBuffUnlock[accountId].count(cls))
        return;
    g_classBuffUnlock[accountId].insert(cls);
    DetectNextSchema();
    if (g_hasClassBuffTable)
        CharacterDatabase.DirectExecute(
            "INSERT IGNORE INTO `lg_class_buff_unlock` (`account_id`, `class`) VALUES ({}, {})",
            accountId, cls);
    UnlockPerk(player, SPELL_CLASS_BUFFS);
    ChatHandler(player->GetSession()).SendSysMessage(
        "|cff66ccff[Account Perks]|r Class Buffs unlocked for this class (Naxxramas 25).");
}

void ApplyClassBuffs(Player* player, bool apply)
{
    if (!player)
        return;
    NextState& st = StateFor(player);
    if (st.classBuffOn == apply)
        return;
    st.classBuffOn = apply;
    float const amt = apply ? CLASS_BUFF_PCT : -CLASS_BUFF_PCT;
    player->ApplyStatPctModifier(UNIT_MOD_STAT_STRENGTH, TOTAL_PCT, amt);
    player->ApplyStatPctModifier(UNIT_MOD_STAT_AGILITY, TOTAL_PCT, amt);
    player->ApplyStatPctModifier(UNIT_MOD_STAT_STAMINA, TOTAL_PCT, amt);
    player->ApplyStatPctModifier(UNIT_MOD_STAT_INTELLECT, TOTAL_PCT, amt);
    player->ApplyStatPctModifier(UNIT_MOD_STAT_SPIRIT, TOTAL_PCT, amt);
    player->UpdateAllStats();
}

bool PlayerHasClassBuffUnlock(Player* player)
{
    if (!player || !player->GetSession())
        return false;
    uint32 const accountId = player->GetSession()->GetAccountId();
    LoadClassBuffUnlock(accountId);
    return g_classBuffUnlock[accountId].count(player->getClass()) > 0
        && HasPerk(player, SPELL_CLASS_BUFFS);
}

bool ShouldHaveClassBuff(Player* player)
{
    if (!player || !player->IsAlive())
        return false;
    if (PlayerHasClassBuffUnlock(player))
        return true;
    Group* group = player->GetGroup();
    if (!group)
        return false;
    for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
    {
        Player* member = itr->GetSource();
        if (!member || member == player || !member->IsInWorld())
            continue;
        if (!player->IsInMap(member) || !player->IsWithinDistInMap(member, 100.0f))
            continue;
        if (PlayerHasClassBuffUnlock(member))
            return true;
    }
    return false;
}

void TickClassBuffs(Player* player)
{
    if (!player || !player->GetSession())
        return;
    ApplyClassBuffs(player, ShouldHaveClassBuff(player));
}

void LoadAccountRiding(uint32 accountId)
{
    if (g_accountRiding.find(accountId) != g_accountRiding.end())
        return;
    DetectNextSchema();
    uint32 skill = 0;
    if (g_hasRidingCol)
        if (QueryResult result = CharacterDatabase.Query(
            "SELECT `riding_skill` FROM `lg_account_meta` WHERE `account_id` = {}", accountId))
            skill = (*result)[0].Get<uint32>();
    g_accountRiding[accountId] = skill;
}

void SaveAccountRiding(uint32 accountId, uint32 skill)
{
    if (skill > g_accountRiding[accountId])
        g_accountRiding[accountId] = skill;
    DetectNextSchema();
    if (!g_hasRidingCol)
        return;
    CharacterDatabase.DirectExecute(
        "INSERT INTO `lg_account_meta` (`account_id`, `riding_skill`) VALUES ({}, {}) "
        "ON DUPLICATE KEY UPDATE `riding_skill` = GREATEST(`riding_skill`, {})",
        accountId, skill, skill);
}

void ApplyAccountRiding(Player* player)
{
    if (!player || !player->GetSession())
        return;
    uint32 const accountId = player->GetSession()->GetAccountId();
    LoadAccountRiding(accountId);
    uint32 const have = g_accountRiding[accountId];
    if (!have)
        return;
    UnlockPerk(player, SPELL_RIDING_SHARE);
    uint16 const cur = player->GetSkillValue(SKILL_RIDING);
    if (cur >= have)
        return;
    uint32 step = 1;
    uint32 maxv = 75;
    if (have >= 300)
    {
        step = 4;
        maxv = 300;
    }
    else if (have >= 225)
    {
        step = 3;
        maxv = 225;
    }
    else if (have >= 150)
    {
        step = 2;
        maxv = 150;
    }
    player->SetSkill(SKILL_RIDING, uint16(step), uint16(have), uint16(maxv));
}

void NoteRiding(Player* player)
{
    if (!player || !player->GetSession())
        return;
    uint16 const val = player->GetSkillValue(SKILL_RIDING);
    if (!val)
        return;
    SaveAccountRiding(player->GetSession()->GetAccountId(), val);
    UnlockPerk(player, SPELL_RIDING_SHARE);
}

uint8 GroupMedianLevel(Group* group, bool humansOnly)
{
    std::vector<uint8> levels;
    if (!group)
        return 0;
    for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
    {
        Player* member = itr->GetSource();
        if (!member)
            continue;
        bool const bot = member->GetSession() && member->GetSession()->IsBot();
        if (humansOnly && bot)
            continue;
        levels.push_back(member->GetLevel());
    }
    if (levels.empty())
        return 0;
    std::sort(levels.begin(), levels.end());
    return levels[levels.size() / 2];
}

bool GroupIsMentored(Player* player)
{
    if (!player || !player->GetGroup())
        return false;
    Group* group = player->GetGroup();
    uint8 const humanMed = GroupMedianLevel(group, true);
    if (!humanMed)
        return false;
    for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
    {
        Player* member = itr->GetSource();
        if (!member || !member->GetSession() || !member->GetSession()->IsBot())
            continue;
        uint8 orig = g_state[member->GetGUID().GetCounter()].botOrigLevel;
        if (!orig)
            orig = member->GetLevel();
        if (orig > humanMed)
            return true;
    }
    return false;
}

void ScaleMentorBots(Player* player)
{
    if (!player || !player->GetGroup() || !player->GetMap() || !player->GetMap()->IsDungeon())
        return;
    Group* group = player->GetGroup();
    uint8 const median = GroupMedianLevel(group, true);
    if (!median)
        return;
    for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
    {
        Player* bot = itr->GetSource();
        if (!bot || !bot->GetSession() || !bot->GetSession()->IsBot())
            continue;
        NextState& st = g_state[bot->GetGUID().GetCounter()];
        if (!st.botOrigLevel)
            st.botOrigLevel = bot->GetLevel();
        if (st.botOrigLevel <= median)
            continue;
        if (bot->GetLevel() != median)
            bot->SetLevel(median);
    }
}

void RestoreMentorBots(Player* player)
{
    if (!player || !player->GetGroup())
        return;
    for (GroupReference* itr = player->GetGroup()->GetFirstMember(); itr; itr = itr->next())
    {
        Player* bot = itr->GetSource();
        if (!bot || !bot->GetSession() || !bot->GetSession()->IsBot())
            continue;
        NextState& st = g_state[bot->GetGUID().GetCounter()];
        if (st.botOrigLevel && bot->GetLevel() != st.botOrigLevel)
            bot->SetLevel(st.botOrigLevel);
    }
}

bool QuestRelatedCreature(Creature* creature)
{
    if (!creature)
        return false;
    CreatureQuestItemList const* items = sObjectMgr->GetCreatureQuestItemList(creature->GetEntry());
    if (items && !items->empty())
        return true;
    return false;
}

void CapQuestRespawn(Creature* creature)
{
    if (!creature || !QuestRelatedCreature(creature))
        return;
    if (creature->GetRespawnDelay() > QUEST_RESPAWN_CAP)
        creature->SetRespawnDelay(QUEST_RESPAWN_CAP);
}

void CapQuestGoRespawn(GameObject* go)
{
    if (!go || go->GetGoType() != GAMEOBJECT_TYPE_CHEST)
        return;
    GameObjectTemplate const* info = go->GetGOInfo();
    if (!info)
        return;
    bool quest = info->chest.questId != 0;
    GameObjectQuestItemList const* items = sObjectMgr->GetGameObjectQuestItemList(go->GetEntry());
    if (items && !items->empty())
        quest = true;
    if (!quest)
        return;
    if (go->GetRespawnDelay() > QUEST_RESPAWN_CAP)
        go->SetRespawnDelay(QUEST_RESPAWN_CAP);
}

void RelocateConsecration(Player* player)
{
    if (!player || !player->GetMap() || !HasPerk(player, SPELL_PALADIN_HOLY))
        return;
    for (SpellInfo const* info = sSpellMgr->GetSpellInfo(SPELL_CONSECRATION); info; info = info->GetNextRankSpell())
    {
        DynamicObject* dyn = player->GetDynObject(info->Id);
        if (!dyn)
            continue;
        player->GetMap()->DynamicObjectRelocation(dyn,
            player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetOrientation());
        if (dyn->GetDuration() < 2000)
            dyn->SetDuration(8000);
    }
}

void ThrowExtraAvengers(Player* player, Spell* spell)
{
    if (!player || !spell || player->getClass() != CLASS_PALADIN)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!RankOf(info, SPELL_AVENGERS_SHIELD))
        return;
    Unit* first = spell->m_targets.GetUnitTarget();
    if (!first)
        return;
    std::list<Unit*> targets;
    Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck check(player, player, 30.0f);
    Acore::UnitListSearcher<Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck> searcher(player, targets, check);
    Cell::VisitObjects(player, searcher, 30.0f);
    uint32 extra = 0;
    for (Unit* target : targets)
    {
        if (!target || target == first || !target->IsAlive() || !player->IsValidAttackTarget(target))
            continue;
        player->CastSpell(target, info->Id, true);
        if (++extra >= 2)
            break;
    }
    int32 cd = int32(AS_CD_MS);
    float haste = player->GetRatingBonusValue(CR_HASTE_SPELL);
    if (haste < 0.0f)
        haste = 0.0f;
    cd = int32(float(cd) / (1.0f + haste / 100.0f));
    if (cd < 1000)
        cd = 1000;
    player->AddSpellCooldown(info->Id, 0, getMSTime() + uint32(cd), true);
}

void KeepHofOneTarget(Player* caster, Unit* target)
{
    if (!caster || !target || !HasPerk(caster, SPELL_PALADIN_RETRIBUTION))
        return;
    NextState& st = StateFor(caster);
    uint32 const newGuid = target->GetGUID().GetCounter();
    if (st.hofTarget && st.hofTarget != newGuid)
        if (Player* prev = ObjectAccessor::FindPlayerByLowGUID(st.hofTarget))
            prev->RemoveAurasDueToSpell(SPELL_HAND_OF_FREEDOM);
    st.hofTarget = newGuid;
    if (Aura* aura = target->GetAura(SPELL_HAND_OF_FREEDOM, caster->GetGUID()))
    {
        aura->SetMaxDuration(-1);
        aura->SetDuration(-1);
    }
    caster->RemoveSpellCooldown(SPELL_HAND_OF_FREEDOM, true);
}

void ApplyWeaponPeak(Player* player)
{
    if (!player || !player->GetSession())
        return;
    uint32 const accountId = player->GetSession()->GetAccountId();
    NextState& st = StateFor(player);
    if (st.weaponPeakOn)
    {
        player->HandleStatFlatModifier(UNIT_MOD_STAT_STRENGTH, TOTAL_VALUE, st.peakStr, false);
        player->HandleStatFlatModifier(UNIT_MOD_STAT_AGILITY, TOTAL_VALUE, st.peakAgi, false);
        player->HandleStatFlatModifier(UNIT_MOD_STAT_STAMINA, TOTAL_VALUE, st.peakSta, false);
        player->HandleStatFlatModifier(UNIT_MOD_STAT_INTELLECT, TOTAL_VALUE, st.peakInt, false);
        player->HandleStatFlatModifier(UNIT_MOD_STAT_SPIRIT, TOTAL_VALUE, st.peakSpi, false);
        st.weaponPeakOn = false;
        st.peakStr = st.peakAgi = st.peakSta = st.peakInt = st.peakSpi = 0.f;
    }
    Item* weapon = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
    if (!weapon)
        return;
    ItemTemplate const* proto = weapon->GetTemplate();
    if (!proto || proto->Class != ITEM_CLASS_WEAPON)
        return;
    float peak[5] = {};
    float mine[5] = {};
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT `item_entry`, `str`, `agi`, `sta`, `intel`, `spi` FROM `lg_absorb` WHERE `account_id` = {}",
        accountId))
    {
        do
        {
            uint32 const entry = (*result)[0].Get<uint32>();
            ItemTemplate const* other = sObjectMgr->GetItemTemplate(entry);
            if (!other || other->Class != proto->Class || other->SubClass != proto->SubClass)
                continue;
            float vals[5] = {
                (*result)[1].Get<float>(), (*result)[2].Get<float>(), (*result)[3].Get<float>(),
                (*result)[4].Get<float>(), (*result)[5].Get<float>()
            };
            for (int i = 0; i < 5; ++i)
                if (vals[i] > peak[i])
                    peak[i] = vals[i];
            if (entry == proto->ItemId)
            {
                for (int i = 0; i < 5; ++i)
                    mine[i] = vals[i];
            }
        } while (result->NextRow());
    }
    st.peakStr = std::max(0.f, peak[0] - mine[0]);
    st.peakAgi = std::max(0.f, peak[1] - mine[1]);
    st.peakSta = std::max(0.f, peak[2] - mine[2]);
    st.peakInt = std::max(0.f, peak[3] - mine[3]);
    st.peakSpi = std::max(0.f, peak[4] - mine[4]);
    if (st.peakStr + st.peakAgi + st.peakSta + st.peakInt + st.peakSpi <= 0.01f)
        return;
    player->HandleStatFlatModifier(UNIT_MOD_STAT_STRENGTH, TOTAL_VALUE, st.peakStr, true);
    player->HandleStatFlatModifier(UNIT_MOD_STAT_AGILITY, TOTAL_VALUE, st.peakAgi, true);
    player->HandleStatFlatModifier(UNIT_MOD_STAT_STAMINA, TOTAL_VALUE, st.peakSta, true);
    player->HandleStatFlatModifier(UNIT_MOD_STAT_INTELLECT, TOTAL_VALUE, st.peakInt, true);
    player->HandleStatFlatModifier(UNIT_MOD_STAT_SPIRIT, TOTAL_VALUE, st.peakSpi, true);
    st.weaponPeakOn = true;
    player->UpdateAllStats();
}

void HandleNextMessage(Player* player, std::string const& raw)
{
    std::string msg = raw;
    if (msg.rfind("LG\t", 0) == 0)
        msg = msg.substr(3);
    uint32 cap = 0;
    if (sscanf(msg.c_str(), "SCAP|%u", &cap) == 1)
    {
        if (cap < 100)
            cap = 100;
        if (cap > SPEED_CAP_DEFAULT)
            cap = SPEED_CAP_DEFAULT;
        StateFor(player).speedCapPct = cap;
        DetectNextSchema();
        if (g_hasSpeedCapCol && player->GetSession())
            CharacterDatabase.DirectExecute(
                "INSERT INTO `lg_account_meta` (`account_id`, `speed_cap`) VALUES ({}, {}) "
                "ON DUPLICATE KEY UPDATE `speed_cap` = {}",
                player->GetSession()->GetAccountId(), cap, cap);
        ApplySpeedCap(player);
        SendLine(player, Acore::StringFormat("SCAP|{}", cap));
    }
}

class NextPlayer : public PlayerScript
{
public:
    NextPlayer() : PlayerScript("LivingGearNextPlayer", {
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_LOGOUT,
        PLAYERHOOK_ON_UPDATE,
        PLAYERHOOK_ON_MAP_CHANGED,
        PLAYERHOOK_ON_CREATURE_KILL,
        PLAYERHOOK_ON_LEARN_SPELL,
        PLAYERHOOK_ON_UPDATE_SKILL,
        PLAYERHOOK_ON_GIVE_EXP,
        PLAYERHOOK_ON_EQUIP,
        PLAYERHOOK_ON_UNEQUIP_ITEM,
        PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT,
        PLAYERHOOK_ON_PLAYER_QUEST_ACCEPT
    }) { }

    void OnPlayerLogin(Player* player) override
    {
        if (!player || !player->GetSession())
            return;
        uint32 const accountId = player->GetSession()->GetAccountId();
        DetectNextSchema();
        LoadClassBuffUnlock(accountId);
        LoadAccountRiding(accountId);
        if (g_hasSpeedCapCol)
        {
            if (QueryResult result = CharacterDatabase.Query(
                "SELECT `speed_cap` FROM `lg_account_meta` WHERE `account_id` = {}", accountId))
            {
                uint32 cap = (*result)[0].Get<uint32>();
                if (cap)
                    StateFor(player).speedCapPct = cap;
            }
        }
        ApplyAccountRiding(player);
        if (g_classBuffUnlock[accountId].count(player->getClass()))
            UnlockPerk(player, SPELL_CLASS_BUFFS);
        ApplyWeaponPeak(player);
        SendLine(player, Acore::StringFormat("SCAP|{}", SpeedCapPct(player)));
    }

    void OnPlayerLogout(Player* player) override
    {
        if (!player)
            return;
        ApplyClassBuffs(player, false);
        NextState& st = StateFor(player);
        if (st.weaponPeakOn)
            ApplyWeaponPeak(player);
        RestoreMentorBots(player);
        g_state.erase(player->GetGUID().GetCounter());
    }

    void OnPlayerUpdate(Player* player, uint32 /*diff*/) override
    {
        if (!player || !player->IsInWorld())
            return;
        uint32 const now = getMSTime();
        NextState& st = StateFor(player);
        if (!st.lastTickMs || getMSTimeDiff(st.lastTickMs, now) >= 400)
        {
            st.lastTickMs = now;
            TickClassBuffs(player);
            ApplySpeedCap(player);
        }
        RelocateConsecration(player);
    }

    void OnPlayerQuestAccept(Player* player, Quest const* /*quest*/) override
    {
        UnlockPerk(player, SPELL_AUTO_ACCEPT);
    }

    void OnPlayerMapChanged(Player* player) override
    {
        if (!player)
            return;
        if (player->GetMap() && player->GetMap()->IsDungeon())
            ScaleMentorBots(player);
        else
            RestoreMentorBots(player);
    }

    void OnPlayerCreatureKill(Player* killer, Creature* killed) override
    {
        if (!killer || !killed || killed->GetEntry() != NPC_KELTHUZAD)
            return;
        Map* map = killer->GetMap();
        if (!map || map->GetId() != MAP_NAXXRAMAS)
            return;
        if (map->GetDifficulty() != RAID_DIFFICULTY_25MAN_NORMAL
            && map->GetDifficulty() != RAID_DIFFICULTY_25MAN_HEROIC)
            return;
        NoteClassBuffUnlock(killer);
        if (Group* group = killer->GetGroup())
        {
            for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
                if (Player* member = itr->GetSource())
                    if (member->IsInMap(killer) && member->getClass() == killer->getClass())
                        NoteClassBuffUnlock(member);
        }
    }

    void OnPlayerLearnSpell(Player* player, uint32 /*spellId*/) override
    {
        NoteRiding(player);
    }

    void OnPlayerUpdateSkill(Player* player, uint32 skillId, uint32 /*value*/, uint32 /*max*/,
        uint32 /*step*/, uint32 /*newValue*/) override
    {
        if (skillId == SKILL_RIDING)
            NoteRiding(player);
    }

    void OnPlayerGiveXP(Player* player, uint32& amount, Unit* /*victim*/, uint8 /*xpSource*/) override
    {
        if (!player || !amount)
            return;
        if (GroupIsMentored(player))
            amount = uint32(float(amount) * MENTOR_XP);
    }

    void OnPlayerEquip(Player* player, Item* /*it*/, uint8 /*bag*/, uint8 /*slot*/, bool /*update*/) override
    {
        ApplyWeaponPeak(player);
    }

    void OnPlayerUnequip(Player* player, Item* /*it*/) override
    {
        ApplyWeaponPeak(player);
    }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 language, std::string& msg, Player* /*receiver*/) override
    {
        if (language != LANG_ADDON || type != CHAT_MSG_WHISPER)
            return true;
        if (msg.rfind("SCAP|", 0) == 0)
        {
            HandleNextMessage(player, msg);
            return false;
        }
        return true;
    }
};

class NextSpell : public AllSpellScript
{
public:
    NextSpell() : AllSpellScript("LivingGearNextSpell", {
        ALLSPELLHOOK_ON_PREPARE,
        ALLSPELLHOOK_ON_SPELL_CHECK_CAST,
        ALLSPELLHOOK_ON_CAST
    }) { }

    void OnSpellPrepare(Spell* spell, Unit* caster, SpellInfo const* spellInfo) override
    {
        if (!spell || !caster || !spellInfo || !caster->IsPlayer())
            return;
        if (spellInfo->HasAura(SPELL_AURA_MOUNTED))
            spell->SetCastTime(0);
    }

    void OnSpellCheckCast(Spell* spell, bool /*strict*/, SpellCastResult& res) override
    {
        if (!spell)
            return;
        Unit* caster = spell->GetCaster();
        if (!caster || !caster->IsPlayer())
            return;
        Player* player = caster->ToPlayer();
        SpellInfo const* info = spell->GetSpellInfo();
        if (!info)
            return;
        if (info->HasAura(SPELL_AURA_MOUNTED)
            && (res == SPELL_FAILED_MOVING || res == SPELL_FAILED_LEVEL_REQUIREMENT
                || res == SPELL_FAILED_LOWLEVEL))
            res = SPELL_CAST_OK;
        if (HasPerk(player, SPELL_PALADIN_HOLY) && RankOf(info, SPELL_CONSECRATION)
            && player->GetDynObject(info->Id))
        {
            player->RemoveDynObject(info->Id);
            res = SPELL_FAILED_DONT_REPORT;
        }
        if (HasPerk(player, SPELL_PALADIN_RETRIBUTION) && info->Id == SPELL_HAND_OF_FREEDOM
            && res == SPELL_FAILED_NOT_READY)
            res = SPELL_CAST_OK;
    }

    void OnSpellCast(Spell* spell, Unit* caster, SpellInfo const* spellInfo, bool /*skipCheck*/) override
    {
        if (!spell || !caster || !spellInfo || !caster->IsPlayer())
            return;
        Player* player = caster->ToPlayer();
        if (RankOf(spellInfo, SPELL_AVENGERS_SHIELD) && !spell->IsTriggered())
            ThrowExtraAvengers(player, spell);
        if (spellInfo->Id == SPELL_HAND_OF_FREEDOM)
        {
            Unit* target = spell->m_targets.GetUnitTarget();
            if (!target)
                target = player;
            KeepHofOneTarget(player, target);
        }
    }
};

class NextUnit : public UnitScript
{
public:
    NextUnit() : UnitScript("LivingGearNextUnit", true, {
        UNITHOOK_MODIFY_SPELL_DAMAGE_TAKEN,
        UNITHOOK_ON_AURA_APPLY
    }) { }

    void ModifySpellDamageTaken(Unit* /*target*/, Unit* attacker, int32& damage, SpellInfo const* spellInfo) override
    {
        if (damage <= 0 || !attacker || !spellInfo)
            return;
        if (RankOf(spellInfo, SPELL_AVENGERS_SHIELD) && attacker->IsPlayer())
            damage = int32(float(damage) * AS_DAMAGE_MULT);
    }

    void OnAuraApply(Unit* unit, Aura* aura) override
    {
        if (!unit || !aura || aura->GetId() != SPELL_HAND_OF_FREEDOM)
            return;
        Unit* caster = aura->GetCaster();
        if (!caster || !caster->IsPlayer())
            return;
        KeepHofOneTarget(caster->ToPlayer(), unit);
    }
};

class NextCreature : public AllCreatureScript
{
public:
    NextCreature() : AllCreatureScript("LivingGearNextCreature") { }

    void OnCreatureAddWorld(Creature* creature) override
    {
        CapQuestRespawn(creature);
    }
};

class NextGameObject : public AllGameObjectScript
{
public:
    NextGameObject() : AllGameObjectScript("LivingGearNextGameObject") { }

    void OnGameObjectAddWorld(GameObject* go) override
    {
        CapQuestGoRespawn(go);
    }
};

} // namespace LivingGearNext

void AddSC_LivingGearNext()
{
    new LivingGearNext::NextPlayer();
    new LivingGearNext::NextSpell();
    new LivingGearNext::NextUnit();
    new LivingGearNext::NextCreature();
    new LivingGearNext::NextGameObject();
}
