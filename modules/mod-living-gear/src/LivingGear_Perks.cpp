/*
 * Living Gear account perks rebuilt as new code on the stub baseline.
 * Zone scale, auto-mount, Subtlety (Shaco kit), poisons, combo, travel,
 * craft speed, cooking regen, dungeon timer, curator, quest helpers, armory,
 * solo queue, instant/uniform mount.
 */

#include "AllMapScript.h"
#include "AllSpellScript.h"
#include "CellImpl.h"
#include "Chat.h"
#include "Config.h"
#include "CreatureAI.h"
#include "CreatureScript.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "InstanceScript.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Random.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellAuraEffects.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "TemporarySummon.h"
#include "Unit.h"
#include "Util.h"
#include "QuestDef.h"
#include "WorldSession.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace LivingGearPerks
{
uint32 const SPELL_FIND_QUESTS = 910088;
uint32 const SPELL_COMBO = 910089;
uint32 const SPELL_AUTO_QUEST = 910090;
uint32 const SPELL_ARMORY = 910091;
uint32 const SPELL_SOLO_QUEUE = 910092;
uint32 const SPELL_CRAFT[] = { 910093, 910094, 910095, 910096, 910097 };
uint32 const SPELL_TRAVEL[] = { 910073, 910074, 910075, 910076, 910077 };
uint32 const SPELL_COOK[] = { 910063, 910064, 910065, 910066, 910067, 910068 };
uint32 const SPELL_SWIM = 910098;
uint32 const SPELL_DUNGEON_SPEED = 910099;
uint32 const SPELL_DUNGEON_PACE = 910100;
uint32 const SPELL_CURATOR = 910101;
uint32 const SPELL_JACK_BOX = 910102;
uint32 const SPELL_SHADOW_CLONE = 910103;
uint32 const SPELL_MOUNTED_OPENER = 910104;
uint32 const SPELL_AUTO_MOUNT = 910105;
uint32 const SPELL_SUBTLETY = 910037;
uint32 const SPELL_ASSASSINATION = 910035;
uint32 const NPC_JACK_BOX = 910200;
uint32 const NPC_SHADOW_CLONE = 910201;
uint32 const SPELL_STEALTH = 1784;
uint32 const SPELL_SHADOWSTEP = 36554;
uint32 const SPELL_AMBUSH = 8676;
uint32 const SPELL_DEADLY_THROW = 26679;
uint32 const SPELL_HOWL = 5484;
uint32 const SPELL_THUNDER_CLAP = 6343;
uint32 const SPELL_CRIPPLING = 3408;
uint32 const SPELL_WOUND = 13218;
uint32 const SPELL_DEADLY_POISON = 2818;
uint32 const COMBO_MAX = 100;
uint32 const COMBO_MS = 180000;
uint32 const COOK_MS = 1000;
float const AMBUSH_MULT = 6.0f;

struct PerkCfg
{
    bool zoneEnable = true;
    uint32 zoneBuffer = 3;
    uint32 zoneMinLevel = 10;
    float zoneFloor = 0.35f;
    float zoneDecay = 12.0f;
    float zoneIncoming = 0.12f;
    bool instantMount = true;
    bool uniformMount = true;
    bool autoMount = true;
    uint32 dungeonPar = 1800;
    uint32 curatorTick = 60000;
};

PerkCfg g_cfg;

struct ComboState
{
    std::vector<uint32> expires;
};

std::unordered_map<uint32, ComboState> g_combo;
std::unordered_map<uint32, ComboState> g_groupCombo;
std::unordered_map<uint32, bool> g_autoMountOn;
std::unordered_map<uint32, bool> g_soloQueue;
std::unordered_map<uint32, bool> g_chatOn;
std::unordered_map<uint32, uint32> g_lastMount;
std::unordered_map<uint32, ObjectGuid> g_cloneGuid;
std::unordered_map<uint32, ObjectGuid> g_boxGuid;
std::unordered_map<uint32, uint32> g_dungeonStart;
std::unordered_map<uint32, bool> g_dungeonDone;
std::unordered_map<uint32, uint32> g_cookAcc;
std::unordered_map<uint32, uint32> g_curatorAcc;
std::unordered_map<uint32, uint32> g_comboTick;
std::unordered_set<uint32> g_perkLoaded;
std::unordered_map<uint32, std::unordered_set<uint32>> g_perks;
bool g_hasAutoMountCol = false;
bool g_hasSoloCol = false;
bool g_schemaReady = false;
bool g_hasZoneScale = false;

void DetectSchema()
{
    if (g_schemaReady)
        return;
    g_schemaReady = true;
    if (QueryResult cols = CharacterDatabase.Query(
        "SELECT `COLUMN_NAME` FROM `information_schema`.`COLUMNS` "
        "WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = 'lg_account_meta'"))
    {
        do
        {
            std::string const name = (*cols)[0].Get<std::string>();
            if (name == "auto_mount")
                g_hasAutoMountCol = true;
            else if (name == "solo_queue")
                g_hasSoloCol = true;
        } while (cols->NextRow());
    }
}

void SendLine(Player* player, std::string const& line)
{
    if (!player || !player->GetSession())
        return;
    player->Whisper(std::string("LG\t") + line, LANG_ADDON, player);
}

void Say(Player* player, char const* msg)
{
    if (!player || !player->GetSession())
        return;
    uint32 const acc = player->GetSession()->GetAccountId();
    auto it = g_chatOn.find(acc);
    if (it != g_chatOn.end() && !it->second)
        return;
    ChatHandler(player->GetSession()).SendSysMessage(msg);
}

void LoadPerks(uint32 accountId)
{
    if (!g_perkLoaded.insert(accountId).second)
        return;
    auto& set = g_perks[accountId];
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT `spell_id` FROM `lg_account_perk` WHERE `account_id` = {}", accountId))
    {
        do
            set.insert((*result)[0].Get<uint32>());
        while (result->NextRow());
    }
}

bool HasPerk(Player* player, uint32 spellId)
{
    if (!player || !player->GetSession())
        return false;
    if (player->HasSpell(spellId))
        return true;
    uint32 const acc = player->GetSession()->GetAccountId();
    LoadPerks(acc);
    return g_perks[acc].count(spellId) > 0;
}

void UnlockPerk(Player* player, uint32 spellId, char const* msg)
{
    if (!player || !player->GetSession() || !sSpellMgr->GetSpellInfo(spellId))
        return;
    uint32 const acc = player->GetSession()->GetAccountId();
    LoadPerks(acc);
    if (!g_perks[acc].insert(spellId).second)
        return;
    CharacterDatabase.DirectExecute(
        "INSERT IGNORE INTO `lg_account_perk` (`account_id`, `spell_id`) VALUES ({}, {})",
        acc, spellId);
    if (!player->HasSpell(spellId))
        player->learnSpell(spellId);
    SendLine(player, Acore::StringFormat("PK|{}|1", spellId));
    if (msg)
        Say(player, msg);
}

uint32 BestOwned(Player* player, uint32 firstId)
{
    if (!player)
        return 0;
    uint32 best = 0;
    for (SpellInfo const* info = sSpellMgr->GetSpellInfo(firstId); info; info = info->GetNextRankSpell())
        if (player->HasSpell(info->Id))
            best = info->Id;
    return best;
}

uint32 AccountMaxLevel(Player* player)
{
    if (!player || !player->GetSession())
        return player ? player->GetLevel() : 1;
    uint32 maxLevel = player->GetLevel();
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT MAX(`level`) FROM `characters` WHERE `account` = {}",
        player->GetSession()->GetAccountId()))
        maxLevel = std::max(maxLevel, (*result)[0].Get<uint32>());
    return maxLevel;
}

uint32 AccountMaxSkill(Player* player, uint32 skillId)
{
    uint32 best = player ? player->GetSkillValue(skillId) : 0;
    if (!player || !player->GetSession())
        return best;
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT MAX(`value`) FROM `character_skills` cs "
        "INNER JOIN `characters` c ON c.`guid` = cs.`guid` "
        "WHERE c.`account` = {} AND cs.`skill` = {}",
        player->GetSession()->GetAccountId(), skillId))
        if (!result->Fetch()->IsNull())
            best = std::max(best, (*result)[0].Get<uint32>());
    return best;
}

uint32 ZoneLevel(Player* player)
{
    if (!player)
        return 1;
    uint32 areaId = player->GetAreaId();
    AreaTableEntry const* area = sAreaTableStore.LookupEntry(areaId);
    if (!area)
        return 1;
    if (area->zone)
        if (AreaTableEntry const* parent = sAreaTableStore.LookupEntry(area->zone))
            area = parent;
    int32 level = area->area_level;
    uint32 zoneId = area->zone ? area->zone : area->ID;
    if (g_hasZoneScale)
        if (QueryResult ov = WorldDatabase.Query(
            "SELECT `max_level` FROM `lg_zone_scale` WHERE `zone_id` = {}", zoneId))
            level = int32((*ov)[0].Get<uint32>());
    if (level < 1)
        level = 1;
    return uint32(level);
}

bool OpenWorld(Player* player)
{
    if (!player || !player->GetMap())
        return false;
    Map* map = player->GetMap();
    return !map->IsDungeon() && !map->IsBattlegroundOrArena();
}

float ZoneRewardMult(Player* player)
{
    if (!g_cfg.zoneEnable || !OpenWorld(player))
        return 1.0f;
    uint32 const real = player->GetLevel();
    if (real < g_cfg.zoneMinLevel)
        return 1.0f;
    uint32 const z = ZoneLevel(player);
    uint32 const cap = z + g_cfg.zoneBuffer;
    if (real <= cap)
        return 1.0f;
    float gap = float(real - cap);
    return g_cfg.zoneFloor + (1.0f - g_cfg.zoneFloor) * std::exp(-gap / g_cfg.zoneDecay);
}

float ZoneCombatRatio(Player* player)
{
    if (!g_cfg.zoneEnable || !OpenWorld(player))
        return 1.0f;
    uint32 const real = player->GetLevel();
    if (real < g_cfg.zoneMinLevel)
        return 1.0f;
    uint32 const z = ZoneLevel(player);
    uint32 const cap = z + g_cfg.zoneBuffer;
    if (real <= cap)
        return 1.0f;
    return float(cap) / float(real);
}

void NotifyZoneScale(Player* player)
{
    if (!player || !g_cfg.zoneEnable || !OpenWorld(player))
        return;
    uint32 const real = player->GetLevel();
    uint32 const z = ZoneLevel(player);
    uint32 eff = std::min(real, z + g_cfg.zoneBuffer);
    SendLine(player, Acore::StringFormat("ZSCALE|{}|{}|{}", eff, real, z));
}

uint32 ComboCount(Player* player)
{
    if (!player)
        return 0;
    uint32 now = getMSTime();
    ComboState* st = nullptr;
    if (player->GetGroup())
        st = &g_groupCombo[player->GetGroup()->GetGUID().GetCounter()];
    else
        st = &g_combo[player->GetGUID().GetCounter()];
    st->expires.erase(std::remove_if(st->expires.begin(), st->expires.end(),
        [now](uint32 t) { return t <= now; }),
        st->expires.end());
    return uint32(st->expires.size());
}

void RecastCombo(Player* player)
{
    if (!player || !HasPerk(player, SPELL_COMBO) || !sSpellMgr->GetSpellInfo(SPELL_COMBO))
        return;
    uint32 stacks = ComboCount(player);
    if (!stacks)
    {
        player->RemoveAurasDueToSpell(SPELL_COMBO);
        SendLine(player, "COMBO|0|0|0");
        return;
    }
    int32 bp = int32(stacks) - 1;
    player->CastCustomSpell(player, SPELL_COMBO, &bp, &bp, nullptr, true);
    SendLine(player, Acore::StringFormat("COMBO|{}|{}|{}", stacks, COMBO_MAX, COMBO_MS / 1000));
}

void AddCombo(Player* player)
{
    if (!player || !HasPerk(player, SPELL_COMBO))
        return;
    uint32 now = getMSTime();
    ComboState* st = nullptr;
    if (Group* group = player->GetGroup())
        st = &g_groupCombo[group->GetGUID().GetCounter()];
    else
        st = &g_combo[player->GetGUID().GetCounter()];
    if (st->expires.size() >= COMBO_MAX)
        st->expires.erase(st->expires.begin());
    st->expires.push_back(now + COMBO_MS);
    if (Group* group = player->GetGroup())
    {
        for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
            if (Player* m = itr->GetSource())
                RecastCombo(m);
    }
    else
        RecastCombo(player);
}

uint32 RandomPoison(Player* player)
{
    uint32 ids[3] = {
        BestOwned(player, SPELL_CRIPPLING),
        BestOwned(player, SPELL_WOUND),
        BestOwned(player, SPELL_DEADLY_POISON)
    };
    std::vector<uint32> have;
    for (uint32 id : ids)
        if (id)
            have.push_back(id);
    if (have.empty())
        return 0;
    return have[urand(0, uint32(have.size() - 1))];
}

void ApplyRandomPoison(Player* caster, Unit* target)
{
    if (!caster || !target)
        return;
    uint32 id = RandomPoison(caster);
    if (id)
        caster->CastSpell(target, id, true);
}

uint32 PickMount(Player* player)
{
    if (!player)
        return 0;
    uint32 last = g_lastMount[player->GetGUID().GetCounter()];
    if (last && player->HasSpell(last))
        if (SpellInfo const* info = sSpellMgr->GetSpellInfo(last))
            if (info->HasAura(SPELL_AURA_MOUNTED))
                return last;
    std::vector<uint32> mounts;
    for (auto const& sp : player->GetSpellMap())
    {
        if (!sp.second || sp.second->State == PLAYERSPELL_REMOVED)
            continue;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(sp.first);
        if (info && info->HasAura(SPELL_AURA_MOUNTED))
            mounts.push_back(sp.first);
    }
    if (mounts.empty())
        return 0;
    return mounts[urand(0, uint32(mounts.size() - 1))];
}

void TryAutoMount(Player* player)
{
    if (!g_cfg.autoMount || !player || !HasPerk(player, SPELL_AUTO_MOUNT))
        return;
    uint32 acc = player->GetSession()->GetAccountId();
    if (!g_autoMountOn[acc])
        return;
    if (player->IsInCombat() || player->IsMounted() || player->isDead())
        return;
    if (player->GetVehicle() || player->isMoving())
        return;
    uint32 mount = PickMount(player);
    if (mount)
        player->CastSpell(player, mount, true);
}

void InstantAndMoveMount(Spell* spell, SpellInfo const* info)
{
    if (!g_cfg.instantMount || !spell || !info)
        return;
    if (!info->HasAura(SPELL_AURA_MOUNTED))
        return;
    spell->SetCastTime(0);
}

void UniformMount(Unit* unit, Aura* aura)
{
    if (!g_cfg.uniformMount || !unit || !aura || !unit->IsPlayer())
        return;
    SpellInfo const* info = aura->GetSpellInfo();
    if (!info || !info->HasAura(SPELL_AURA_MOUNTED))
        return;
    Player* player = unit->ToPlayer();
    float ground = 0.0f;
    float flight = 0.0f;
    for (auto const& sp : player->GetSpellMap())
    {
        if (!sp.second || sp.second->State == PLAYERSPELL_REMOVED)
            continue;
        SpellInfo const* si = sSpellMgr->GetSpellInfo(sp.first);
        if (!si)
            continue;
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            if (si->Effects[i].ApplyAuraName == SPELL_AURA_MOD_INCREASE_MOUNTED_SPEED)
                ground = std::max(ground, float(si->Effects[i].CalcValue()));
            if (si->Effects[i].ApplyAuraName == SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED)
                flight = std::max(flight, float(si->Effects[i].CalcValue()));
        }
    }
    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
    {
        AuraEffect* eff = aura->GetEffect(i);
        if (!eff)
            continue;
        if (eff->GetAuraType() == SPELL_AURA_MOD_INCREASE_MOUNTED_SPEED && ground > 0.0f)
            eff->ChangeAmount(int32(ground));
        if (eff->GetAuraType() == SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED && flight > 0.0f)
            eff->ChangeAmount(int32(flight));
    }
}

void ChainAmbush(Player* player, Unit* first)
{
    if (!player || !first || !HasPerk(player, SPELL_SUBTLETY))
        return;
    player->CastSpell(player, SPELL_STEALTH, true);
    uint32 ambush = BestOwned(player, SPELL_AMBUSH);
    if (!ambush)
        ambush = SPELL_AMBUSH;
    std::vector<Unit*> targets;
    targets.push_back(first);
    std::list<Unit*> around;
    Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck check(player, player, 15.0f);
    Acore::UnitListSearcher<Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck> searcher(player, around, check);
    Cell::VisitObjects(player, searcher, 15.0f);
    for (Unit* u : around)
        if (u && u != first && u->IsAlive() && player->IsValidAttackTarget(u))
            targets.push_back(u);
    if (targets.empty())
        return;
    for (uint32 i = 0; i < 5; ++i)
    {
        Unit* t = targets[i % targets.size()];
        if (!t || !t->IsAlive())
            continue;
        player->NearTeleportTo(t->GetPositionX(), t->GetPositionY(), t->GetPositionZ(),
            player->GetOrientation());
        int32 dmg = int32(player->GetLevel() * 12.0f * AMBUSH_MULT);
        if (SpellInfo const* ai = sSpellMgr->GetSpellInfo(ambush))
            dmg = int32(float(std::max(1, ai->Effects[EFFECT_0].CalcValue(player))) * AMBUSH_MULT);
        player->CastCustomSpell(t, ambush, &dmg, nullptr, nullptr, true);
    }
}

void SummonJackBox(Player* player)
{
    if (!player || !HasPerk(player, SPELL_SUBTLETY))
        return;
    auto it = g_boxGuid.find(player->GetGUID().GetCounter());
    if (it != g_boxGuid.end())
        if (Creature* c = player->GetMap()->GetCreature(it->second))
            c->DespawnOrUnsummon();
    Position pos = player->GetPosition();
    if (TempSummon* s = player->SummonCreature(NPC_JACK_BOX, pos, TEMPSUMMON_TIMED_DESPAWN, 60000))
    {
        g_boxGuid[player->GetGUID().GetCounter()] = s->GetGUID();
        s->SetFaction(player->GetFaction());
        s->SetOwnerGUID(player->GetGUID());
        s->SetVisible(false);
    }
}

void SummonClone(Player* player)
{
    if (!player || !HasPerk(player, SPELL_SUBTLETY))
        return;
    auto it = g_cloneGuid.find(player->GetGUID().GetCounter());
    if (it != g_cloneGuid.end())
        if (Creature* c = player->GetMap()->GetCreature(it->second))
            c->DespawnOrUnsummon();
    if (TempSummon* s = player->SummonCreature(NPC_SHADOW_CLONE, player->GetPosition(),
        TEMPSUMMON_MANUAL_DESPAWN))
    {
        g_cloneGuid[player->GetGUID().GetCounter()] = s->GetGUID();
        s->SetOwnerGUID(player->GetGUID());
        s->SetFaction(player->GetFaction());
        s->SetLevel(player->GetLevel());
        s->SetDisplayId(player->GetDisplayId());
        s->SetMaxHealth(player->GetMaxHealth());
        s->SetHealth(player->GetMaxHealth());
        s->SetPower(POWER_ENERGY, player->GetMaxPower(POWER_ENERGY));
    }
}

void TickCooking(Player* player, uint32 diff)
{
    if (!player || player->IsInCombat())
        return;
    uint32 acc = player->GetGUID().GetCounter();
    g_cookAcc[acc] += diff;
    if (g_cookAcc[acc] < COOK_MS)
        return;
    g_cookAcc[acc] = 0;
    uint32 skill = AccountMaxSkill(player, SKILL_COOKING);
    int pct = 0;
    if (skill >= 450 && HasPerk(player, SPELL_COOK[5]))
        pct = 6;
    else if (skill >= 375 && HasPerk(player, SPELL_COOK[4]))
        pct = 5;
    else if (skill >= 300 && HasPerk(player, SPELL_COOK[3]))
        pct = 4;
    else if (skill >= 225 && HasPerk(player, SPELL_COOK[2]))
        pct = 3;
    else if (skill >= 150 && HasPerk(player, SPELL_COOK[1]))
        pct = 2;
    else if (skill >= 75 && HasPerk(player, SPELL_COOK[0]))
        pct = 1;
    if (!pct)
        return;
    uint32 hp = player->CountPctFromMaxHealth(pct);
    if (player->GetHealth() < player->GetMaxHealth())
        player->ModifyHealth(int32(hp));
    uint32 maxMana = player->GetMaxPower(POWER_MANA);
    if (maxMana)
    {
        int32 mana = int32(CalculatePct(maxMana, pct));
        if (player->GetPower(POWER_MANA) < int32(maxMana))
            player->ModifyPower(POWER_MANA, mana);
    }
}

void CatchUpProfession(Player* player)
{
    if (!player || !player->GetSession())
        return;
    uint32 craft = 0;
    uint32 const skills[] = {
        SKILL_ALCHEMY, SKILL_BLACKSMITHING, SKILL_LEATHERWORKING, SKILL_TAILORING,
        SKILL_ENGINEERING, SKILL_ENCHANTING, SKILL_JEWELCRAFTING, SKILL_INSCRIPTION,
        SKILL_COOKING
    };
    for (uint32 sk : skills)
        craft = std::max(craft, AccountMaxSkill(player, sk));
    uint32 need[] = { 75, 150, 225, 300, 375 };
    for (uint32 i = 0; i < 5; ++i)
        if (craft >= need[i])
            UnlockPerk(player, SPELL_CRAFT[i], nullptr);
    uint32 cook = AccountMaxSkill(player, SKILL_COOKING);
    uint32 cookNeed[] = { 75, 150, 225, 300, 375, 450 };
    for (uint32 i = 0; i < 6; ++i)
        if (cook >= cookNeed[i])
            UnlockPerk(player, SPELL_COOK[i], nullptr);
    uint32 maxLv = AccountMaxLevel(player);
    if (maxLv >= 10)
        UnlockPerk(player, SPELL_SWIM, nullptr);
    uint32 travelNeed[] = { 20, 40, 60, 70, 80 };
    for (uint32 i = 0; i < 5; ++i)
        if (maxLv >= travelNeed[i])
            UnlockPerk(player, SPELL_TRAVEL[i], nullptr);
    if (maxLv >= 40)
        UnlockPerk(player, SPELL_MOUNTED_OPENER, nullptr);
    if (PickMount(player))
        UnlockPerk(player, SPELL_AUTO_MOUNT, nullptr);
    if (QueryResult att = CharacterDatabase.Query(
        "SELECT COUNT(*) FROM `lg_absorb` WHERE `account_id` = {}",
        player->GetSession()->GetAccountId()))
        if ((*att)[0].Get<uint64>() >= 1000)
            UnlockPerk(player, SPELL_CURATOR, nullptr);
}

void TickCurator(Player* player, uint32 diff)
{
    if (!player || !HasPerk(player, SPELL_CURATOR))
        return;
    uint32 id = player->GetGUID().GetCounter();
    g_curatorAcc[id] += diff;
    if (g_curatorAcc[id] < g_cfg.curatorTick)
        return;
    g_curatorAcc[id] = 0;
    CharacterDatabase.Execute(
        "UPDATE `lg_item` i INNER JOIN ("
        " SELECT `item_guid` FROM `lg_item` WHERE `owner_guid` = {} AND `level` < 50 "
        " ORDER BY `level` ASC, `xp` ASC LIMIT 5) x ON i.`item_guid` = x.`item_guid` "
        " SET i.`xp` = i.`xp` + 1",
        player->GetGUID().GetCounter());
}

void CheckDungeonClear(Player* player, Creature* killed)
{
    if (!player || !killed || !player->GetMap() || !player->GetMap()->IsDungeon())
        return;
    if (!killed->IsDungeonBoss())
        return;
    Map* map = player->GetMap();
    uint32 inst = map->GetInstanceId();
    if (!g_dungeonStart[inst])
        g_dungeonStart[inst] = getMSTime();
    bool allDead = true;
    if (InstanceMap* im = map->ToInstanceMap())
        if (InstanceScript* instScr = im->GetInstanceScript())
            allDead = instScr->AllBossesDone();
    if (!allDead || g_dungeonDone[inst])
        return;
    g_dungeonDone[inst] = true;
    uint32 elapsed = getMSTimeDiff(g_dungeonStart[inst], getMSTime()) / 1000;
    uint32 par = g_cfg.dungeonPar;
    if (QueryResult q = WorldDatabase.Query(
        "SELECT `par_sec` FROM `lg_dungeon_par` WHERE `map_id` = {} AND `difficulty` = {}",
        map->GetId(), map->GetDifficulty()))
        par = (*q)[0].Get<uint32>();
    int32 speedBp = 0;
    int32 paceBp = 0;
    uint32 dur = 0;
    if (elapsed * 2 <= par)
    {
        speedBp = 29;
        paceBp = 49;
        dur = 1800000;
    }
    else if (elapsed * 4 <= par * 3)
    {
        speedBp = 19;
        paceBp = 29;
        dur = 1200000;
    }
    else if (elapsed <= par)
    {
        speedBp = 9;
        paceBp = 14;
        dur = 900000;
    }
    if (!speedBp)
        return;
    Map::PlayerList const& plist = map->GetPlayers();
    for (auto const& ref : plist)
    {
        Player* m = ref.GetSource();
        if (!m)
            continue;
        m->CastCustomSpell(m, SPELL_DUNGEON_SPEED, &speedBp, &speedBp, nullptr, true);
        m->CastCustomSpell(m, SPELL_DUNGEON_PACE, &paceBp, nullptr, nullptr, true);
        if (Aura* a = m->GetAura(SPELL_DUNGEON_SPEED))
            a->SetDuration(int32(dur));
        if (Aura* a = m->GetAura(SPELL_DUNGEON_PACE))
            a->SetDuration(int32(dur));
        SendLine(m, Acore::StringFormat("DTIMER|clear|{}|{}|{}|{}|{}",
            elapsed, speedBp, speedBp + 1, paceBp + 1, dur / 1000));
    }
}

void SendArmory(Player* player)
{
    if (!player || !player->GetSession())
        return;
    SendLine(player, "ARMCLR");
    if (QueryResult q = CharacterDatabase.Query(
        "SELECT `item_entry`, `item_level`, `str`, `agi`, `sta`, `intel`, `spi`, `armor` "
        "FROM `lg_absorb` WHERE `account_id` = {} LIMIT 80",
        player->GetSession()->GetAccountId()))
    {
        do
        {
            Field* f = q->Fetch();
            uint32 entry = f[0].Get<uint32>();
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
            char const* name = proto ? proto->Name1.c_str() : "Item";
            uint32 slot = proto ? proto->InventoryType : 0;
            SendLine(player, Acore::StringFormat("ARM|{}|{}|{}|{}|{}|{}|{}|{}|{}",
                slot, entry, f[1].Get<uint32>(), int32(f[2].Get<float>()), int32(f[3].Get<float>()),
                int32(f[4].Get<float>()), int32(f[5].Get<float>()), int32(f[6].Get<float>()), name));
        } while (q->NextRow());
    }
    SendLine(player, "ARMEND");
}

void FindQuests(Player* player)
{
    if (!player)
        return;
    std::list<Creature*> list;
    Acore::AllFriendlyCreaturesInGrid check(player);
    Acore::CreatureListSearcher<Acore::AllFriendlyCreaturesInGrid> searcher(player, list, check);
    Cell::VisitObjects(player, searcher, 60.0f);
    uint32 n = 0;
    for (Creature* c : list)
    {
        if (!c || !c->IsQuestGiver())
            continue;
        QuestRelationBounds bounds = sObjectMgr->GetCreatureQuestRelationBounds(c->GetEntry());
        for (auto i = bounds.first; i != bounds.second && n < 8; ++i)
        {
            Quest const* q = sObjectMgr->GetQuestTemplate(i->second);
            if (!q || player->GetQuestStatus(q->GetQuestId()) != QUEST_STATUS_NONE)
                continue;
            if (!player->CanTakeQuest(q, false))
                continue;
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cff66ccff[Quests]|r {} -> {}", c->GetName(), q->GetTitle());
            ++n;
        }
    }
    if (!n)
        ChatHandler(player->GetSession()).SendSysMessage("|cff66ccff[Quests]|r No nearby quests.");
}

void AutoQuestFinish(Player* player)
{
    if (!player)
        return;
    uint32 spawned = 0;
    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE && spawned < 10; ++slot)
    {
        uint32 qid = player->GetQuestSlotQuestId(slot);
        if (!qid || player->GetQuestStatus(qid) != QUEST_STATUS_COMPLETE)
            continue;
        uint32 npc = 0;
        if (QueryResult q = WorldDatabase.Query(
            "SELECT `id` FROM `creature_questender` WHERE `quest` = {} LIMIT 1", qid))
            npc = (*q)[0].Get<uint32>();
        if (!npc)
            continue;
        if (player->SummonCreature(npc, player->GetPosition(), TEMPSUMMON_TIMED_DESPAWN, 60000))
            ++spawned;
    }
    Say(player, Acore::StringFormat("[Quests] Summoned {} turn-in NPC(s).", spawned).c_str());
}

bool HandleLgChat(Player* player, std::string msg)
{
    if (msg.rfind("LG\t", 0) == 0)
        msg = msg.substr(3);
    uint32 acc = player->GetSession()->GetAccountId();
    uint32 v = 0;
    uint32 slot = 0;
    uint32 entry = 0;
    if (sscanf(msg.c_str(), "CHATSET|%u", &v) == 1)
    {
        g_chatOn[acc] = v != 0;
        return true;
    }
    if (sscanf(msg.c_str(), "AMSET|%u", &v) == 1)
    {
        g_autoMountOn[acc] = v != 0;
        DetectSchema();
        if (g_hasAutoMountCol)
            CharacterDatabase.DirectExecute(
                "INSERT INTO `lg_account_meta` (`account_id`, `auto_mount`) VALUES ({}, {}) "
                "ON DUPLICATE KEY UPDATE `auto_mount` = {}",
                acc, v ? 1 : 0, v ? 1 : 0);
        SendLine(player, Acore::StringFormat("AM|{}", v ? 1 : 0));
        return true;
    }
    if (sscanf(msg.c_str(), "SOLOSET|%u", &v) == 1)
    {
        g_soloQueue[acc] = v != 0;
        DetectSchema();
        if (g_hasSoloCol)
            CharacterDatabase.DirectExecute(
                "INSERT INTO `lg_account_meta` (`account_id`, `solo_queue`) VALUES ({}, {}) "
                "ON DUPLICATE KEY UPDATE `solo_queue` = {}",
                acc, v ? 1 : 0, v ? 1 : 0);
        SendLine(player, Acore::StringFormat("SQ|{}", v ? 1 : 0));
        return true;
    }
    if (msg == "ARMOPEN")
    {
        SendArmory(player);
        return true;
    }
    if (sscanf(msg.c_str(), "ARMEQUIP|%u|%u", &slot, &entry) == 2)
    {
        uint16 dest = 0;
        if (player->CanEquipNewItem(uint8(slot), dest, entry, true) == EQUIP_ERR_OK)
            player->EquipNewItem(dest, entry, true);
        else
            player->StoreNewItemInBestSlots(entry, 1);
        return true;
    }
    return false;
}

class PerksPlayer : public PlayerScript
{
public:
    PerksPlayer() : PlayerScript("LivingGearPerksPlayer", {
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_LOGOUT,
        PLAYERHOOK_ON_UPDATE,
        PLAYERHOOK_ON_UPDATE_ZONE,
        PLAYERHOOK_ON_MAP_CHANGED,
        PLAYERHOOK_ON_CREATURE_KILL,
        PLAYERHOOK_ON_CREATURE_KILLED_BY_PET,
        PLAYERHOOK_ON_GIVE_EXP,
        PLAYERHOOK_ON_SPELL_CAST,
        PLAYERHOOK_ON_LEARN_SPELL,
        PLAYERHOOK_ON_PLAYER_LEAVE_COMBAT,
        PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT,
        PLAYERHOOK_ON_UPDATE_CRAFTING_SKILL
    }) { }

    void OnPlayerLogin(Player* player) override
    {
        if (!player || !player->GetSession())
            return;
        DetectSchema();
        uint32 acc = player->GetSession()->GetAccountId();
        LoadPerks(acc);
        CatchUpProfession(player);
        if (g_hasAutoMountCol)
        {
            g_autoMountOn[acc] = false;
            if (QueryResult q = CharacterDatabase.Query(
                "SELECT `auto_mount` FROM `lg_account_meta` WHERE `account_id` = {}", acc))
                g_autoMountOn[acc] = (*q)[0].Get<uint32>() != 0;
        }
        else if (g_autoMountOn.find(acc) == g_autoMountOn.end())
            g_autoMountOn[acc] = true;
        if (HasPerk(player, SPELL_SWIM))
            player->CastSpell(player, SPELL_SWIM, true);
        if (HasPerk(player, SPELL_SUBTLETY))
        {
            player->learnSpell(SPELL_SHADOWSTEP);
            player->learnSpell(SPELL_JACK_BOX);
            player->learnSpell(SPELL_SHADOW_CLONE);
        }
        RecastCombo(player);
        NotifyZoneScale(player);
        SendLine(player, Acore::StringFormat("AM|{}", g_autoMountOn[acc] ? 1 : 0));
    }

    void OnPlayerLogout(Player* player) override
    {
        if (!player)
            return;
        uint32 g = player->GetGUID().GetCounter();
        g_combo.erase(g);
        g_comboTick.erase(g);
        g_cloneGuid.erase(g);
        g_boxGuid.erase(g);
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        TickCooking(player, diff);
        TickCurator(player, diff);
        if (HasPerk(player, SPELL_COMBO))
        {
            uint32 id = player->GetGUID().GetCounter();
            g_comboTick[id] += diff;
            if (g_comboTick[id] >= 1000)
            {
                g_comboTick[id] = 0;
                RecastCombo(player);
            }
        }
    }

    void OnPlayerUpdateZone(Player* player, uint32 /*newZone*/, uint32 /*newArea*/) override
    {
        NotifyZoneScale(player);
    }

    void OnPlayerMapChanged(Player* player) override
    {
        if (!player || !player->GetMap())
            return;
        NotifyZoneScale(player);
        if (player->GetMap()->IsDungeon())
        {
            uint32 inst = player->GetMap()->GetInstanceId();
            if (!g_dungeonStart[inst])
            {
                g_dungeonStart[inst] = getMSTime();
                SendLine(player, Acore::StringFormat("DTIMER|start|{}", g_cfg.dungeonPar));
            }
        }
        else
        {
            SendLine(player, "DTIMER|stop");
        }
    }

    void OnPlayerCreatureKill(Player* killer, Creature* killed) override
    {
        AddCombo(killer);
        CheckDungeonClear(killer, killed);
    }

    void OnPlayerCreatureKilledByPet(Player* owner, Creature* killed) override
    {
        AddCombo(owner);
        CheckDungeonClear(owner, killed);
    }

    void OnPlayerGiveXP(Player* player, uint32& amount, Unit* /*victim*/, uint8 /*xpSource*/) override
    {
        if (!player || !amount)
            return;
        float mult = ZoneRewardMult(player);
        uint32 stacks = ComboCount(player);
        if (HasPerk(player, SPELL_COMBO) && stacks)
            mult *= 1.0f + 0.03f * float(stacks);
        if (Aura* pace = player->GetAura(SPELL_DUNGEON_PACE))
            if (AuraEffect* e = pace->GetEffect(EFFECT_0))
                mult *= 1.0f + float(e->GetAmount() + 1) / 100.0f;
        amount = uint32(float(amount) * mult);
        if (!amount)
            amount = 1;
    }

    void OnPlayerSpellCast(Player* player, Spell* spell, bool skip) override
    {
        if (skip || !player || !spell)
            return;
        SpellInfo const* info = spell->GetSpellInfo();
        if (!info)
            return;
        if (info->HasAura(SPELL_AURA_MOUNTED))
            g_lastMount[player->GetGUID().GetCounter()] = info->Id;
        if (info->Id == SPELL_SHADOWSTEP && HasPerk(player, SPELL_SUBTLETY))
        {
            player->RemoveSpellCooldown(SPELL_SHADOWSTEP, true);
            Unit* t = spell->m_targets.GetUnitTarget();
            if (t)
                ChainAmbush(player, t);
        }
        if (info->Id == SPELL_JACK_BOX)
            SummonJackBox(player);
        if (info->Id == SPELL_SHADOW_CLONE)
            SummonClone(player);
        if (info->Id == SPELL_FIND_QUESTS)
            FindQuests(player);
        if (info->Id == SPELL_AUTO_QUEST)
            AutoQuestFinish(player);
        if (info->Id == SPELL_ARMORY)
            SendArmory(player);
        if (info->Id == SPELL_SOLO_QUEUE)
        {
            uint32 acc = player->GetSession()->GetAccountId();
            g_soloQueue[acc] = !g_soloQueue[acc];
            SendLine(player, Acore::StringFormat("SQ|{}", g_soloQueue[acc] ? 1 : 0));
        }
        if (info->Id == SPELL_AUTO_MOUNT)
        {
            uint32 acc = player->GetSession()->GetAccountId();
            g_autoMountOn[acc] = !g_autoMountOn[acc];
            SendLine(player, Acore::StringFormat("AM|{}", g_autoMountOn[acc] ? 1 : 0));
        }
        if (info->Id == SPELL_MOUNTED_OPENER && player->IsMounted())
        {
            std::list<Unit*> list;
            Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck check(player, player, 20.0f);
            Acore::UnitListSearcher<Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck> searcher(
                player, list, check);
            Cell::VisitObjects(player, searcher, 20.0f);
            for (Unit* u : list)
                if (u && u->IsAlive())
                    u->GetMotionMaster()->MoveJump(player->GetPositionX(), player->GetPositionY(),
                        player->GetPositionZ() + 0.5f, 20.0f, 8.0f);
            player->CastSpell(player, SPELL_THUNDER_CLAP, true);
        }
        if (HasPerk(player, SPELL_SUBTLETY) && info->SpellFamilyName == SPELLFAMILY_ROGUE
            && info->Id != SPELL_SHADOW_CLONE)
        {
            auto it = g_cloneGuid.find(player->GetGUID().GetCounter());
            if (it != g_cloneGuid.end())
                if (Creature* clone = player->GetMap()->GetCreature(it->second))
                    if (Unit* t = spell->m_targets.GetUnitTarget())
                        clone->CastSpell(t, info->Id, true);
        }
    }

    void OnPlayerLearnSpell(Player* player, uint32 spellId) override
    {
        if (SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId))
            if (info->HasAura(SPELL_AURA_MOUNTED))
                UnlockPerk(player, SPELL_AUTO_MOUNT, "[Account Perks] Auto-Mount unlocked.");
    }

    void OnPlayerLeaveCombat(Player* player) override
    {
        TryAutoMount(player);
    }

    void OnPlayerUpdateCraftingSkill(Player* player, SkillLineAbilityEntry const* /*skill*/,
        uint32 /*current_level*/, uint32& /*gain*/) override
    {
        CatchUpProfession(player);
    }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 language, std::string& msg,
        Player* /*receiver*/) override
    {
        if (!player || language != LANG_ADDON || type != CHAT_MSG_WHISPER)
            return true;
        if (HandleLgChat(player, msg))
            return false;
        return true;
    }
};

class PerksSpell : public AllSpellScript
{
public:
    PerksSpell() : AllSpellScript("LivingGearPerksSpell", {
        ALLSPELLHOOK_ON_PREPARE
    }) { }

    void OnSpellPrepare(Spell* spell, Unit* caster, SpellInfo const* info) override
    {
        InstantAndMoveMount(spell, info);
        if (!caster || !caster->IsPlayer() || !info)
            return;
        Player* player = caster->ToPlayer();
        uint32 travel = 0;
        for (uint32 id : SPELL_TRAVEL)
            if (HasPerk(player, id))
                ++travel;
        if (travel && (info->Id == 8690 || info->Id == 556))
            spell->SetCastTime(int32(float(spell->GetCastTime()) * std::pow(0.80f, float(travel))));
        uint32 ranks = 0;
        for (uint32 id : SPELL_CRAFT)
            if (HasPerk(player, id))
                ++ranks;
        if (!ranks)
            return;
        bool craft = info->IsAbilityOfSkillType(SKILL_ALCHEMY)
            || info->IsAbilityOfSkillType(SKILL_BLACKSMITHING)
            || info->IsAbilityOfSkillType(SKILL_LEATHERWORKING)
            || info->IsAbilityOfSkillType(SKILL_TAILORING)
            || info->IsAbilityOfSkillType(SKILL_ENGINEERING)
            || info->IsAbilityOfSkillType(SKILL_ENCHANTING)
            || info->IsAbilityOfSkillType(SKILL_JEWELCRAFTING)
            || info->IsAbilityOfSkillType(SKILL_INSCRIPTION)
            || info->IsAbilityOfSkillType(SKILL_COOKING);
        if (craft)
            spell->SetCastTime(int32(float(spell->GetCastTime()) * std::pow(0.80f, float(ranks))));
    }
};

class PerksUnit : public UnitScript
{
public:
    PerksUnit() : UnitScript("LivingGearPerksUnit", true, {
        UNITHOOK_ON_DAMAGE,
        UNITHOOK_ON_AURA_APPLY
    }) { }

    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        if (!attacker || !victim || !damage)
            return;
        if (Player* p = attacker->ToPlayer())
        {
            damage = uint32(float(damage) * ZoneCombatRatio(p));
            if (!damage)
                damage = 1;
            if (HasPerk(p, SPELL_ASSASSINATION) && roll_chance_i(20))
                ApplyRandomPoison(p, victim);
        }
        if (Player* v = victim->ToPlayer())
        {
            float extra = 1.0f;
            if (g_cfg.zoneEnable && OpenWorld(v))
            {
                uint32 real = v->GetLevel();
                uint32 cap = ZoneLevel(v) + g_cfg.zoneBuffer;
                if (real > cap && real >= g_cfg.zoneMinLevel)
                    extra += float(real - cap) * g_cfg.zoneIncoming;
            }
            damage = uint32(float(damage) * extra);
        }
    }

    void OnAuraApply(Unit* unit, Aura* aura) override
    {
        UniformMount(unit, aura);
    }
};

class PerksMap : public AllMapScript
{
public:
    PerksMap() : AllMapScript("LivingGearPerksMap", { ALLMAPHOOK_ON_PLAYER_ENTER_ALL }) { }

    void OnPlayerEnterAll(Map* map, Player* player) override
    {
        if (!map || !player || !map->IsDungeon())
            return;
        uint32 inst = map->GetInstanceId();
        if (!g_dungeonStart[inst])
            g_dungeonStart[inst] = getMSTime();
    }
};

struct npc_lg_jack_boxAI : public ScriptedAI
{
    npc_lg_jack_boxAI(Creature* c) : ScriptedAI(c), _ticks(0), _sprung(false) { }

    void Reset() override
    {
        me->SetReactState(REACT_PASSIVE);
        me->SetVisible(false);
    }

    void UpdateAI(uint32 diff) override
    {
        _wait += diff;
        if (!_sprung)
        {
            if (_wait < 400)
                return;
            _wait = 0;
            std::list<Unit*> list;
            Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck check(me, me, 3.0f);
            Acore::UnitListSearcher<Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck> searcher(
                me, list, check);
            Cell::VisitObjects(me, searcher, 3.0f);
            if (list.empty())
                return;
            _sprung = true;
            me->SetVisible(true);
            uint32 howl = SPELL_HOWL;
            if (Unit* owner = me->GetOwner())
                if (Player* p = owner->ToPlayer())
                    if (uint32 h = BestOwned(p, SPELL_HOWL))
                        howl = h;
            me->CastSpell(me, howl, true);
            return;
        }
        if (_wait < 2000)
            return;
        _wait = 0;
        if (++_ticks > 5)
        {
            me->DespawnOrUnsummon();
            return;
        }
        Player* owner = nullptr;
        if (Unit* o = me->GetOwner())
            owner = o->ToPlayer();
        uint32 thr = owner ? BestOwned(owner, SPELL_DEADLY_THROW) : SPELL_DEADLY_THROW;
        if (!thr)
            thr = SPELL_DEADLY_THROW;
        std::list<Unit*> list;
        Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck check(me, me, 20.0f);
        Acore::UnitListSearcher<Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck> searcher(
            me, list, check);
        Cell::VisitObjects(me, searcher, 20.0f);
        for (Unit* u : list)
        {
            if (!u || !u->IsAlive())
                continue;
            me->CastSpell(u, thr, true);
            if (owner)
                ApplyRandomPoison(owner, u);
        }
    }

private:
    uint32 _wait = 0;
    uint32 _ticks = 0;
    bool _sprung = false;
};

class npc_lg_jack_box : public CreatureScript
{
public:
    npc_lg_jack_box() : CreatureScript("npc_lg_jack_box") { }
    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_lg_jack_boxAI(creature);
    }
};

struct npc_lg_shadow_cloneAI : public ScriptedAI
{
    npc_lg_shadow_cloneAI(Creature* c) : ScriptedAI(c) { }

    void UpdateAI(uint32 /*diff*/) override
    {
        if (!UpdateVictim())
            return;
        DoMeleeAttackIfReady();
    }
};

class npc_lg_shadow_clone : public CreatureScript
{
public:
    npc_lg_shadow_clone() : CreatureScript("npc_lg_shadow_clone") { }
    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_lg_shadow_cloneAI(creature);
    }
};

void LoadPerkConfig()
{
    g_cfg.zoneEnable = sConfigMgr->GetOption<bool>("LivingGear.ZoneScale.Enable", true);
    g_cfg.zoneBuffer = sConfigMgr->GetOption<uint32>("LivingGear.ZoneScale.CombatBuffer", 3);
    g_cfg.zoneMinLevel = sConfigMgr->GetOption<uint32>("LivingGear.ZoneScale.MinPlayerLevel", 10);
    g_cfg.zoneFloor = sConfigMgr->GetOption<float>("LivingGear.ZoneScale.RewardFloor", 0.35f);
    g_cfg.zoneDecay = sConfigMgr->GetOption<float>("LivingGear.ZoneScale.RewardGapDecay", 12.0f);
    g_cfg.zoneIncoming = sConfigMgr->GetOption<float>("LivingGear.ZoneScale.IncomingPerLevel", 0.12f);
    g_cfg.instantMount = sConfigMgr->GetOption<bool>("LivingGear.InstantMount", true);
    g_cfg.uniformMount = sConfigMgr->GetOption<bool>("LivingGear.UniformMountSpeed", true);
    g_cfg.autoMount = sConfigMgr->GetOption<bool>("LivingGear.AutoMount", true);
    g_cfg.dungeonPar = sConfigMgr->GetOption<uint32>("LivingGear.DungeonTimer.DefaultParSec", 1800);
    g_cfg.curatorTick = sConfigMgr->GetOption<uint32>("LivingGear.CollectionPassive.TickMs", 60000);
    g_hasZoneScale = false;
    if (QueryResult t = WorldDatabase.Query(
        "SELECT 1 FROM `information_schema`.`TABLES` "
        "WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = 'lg_zone_scale'"))
        g_hasZoneScale = true;
}

class PerksWorld : public WorldScript
{
public:
    PerksWorld() : WorldScript("LivingGearPerksWorld", {
        WORLDHOOK_ON_AFTER_CONFIG_LOAD,
        WORLDHOOK_ON_STARTUP
    }) { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        LoadPerkConfig();
    }

    void OnStartup() override
    {
        LoadPerkConfig();
        LOG_INFO("server.loading", "Living Gear perks module loaded (zone scale, auto-mount, Subtlety, poisons)");
    }
};

} // namespace LivingGearPerks

void AddSC_LivingGearPerks()
{
    new LivingGearPerks::PerksWorld();
    new LivingGearPerks::PerksPlayer();
    new LivingGearPerks::PerksSpell();
    new LivingGearPerks::PerksUnit();
    new LivingGearPerks::PerksMap();
    new LivingGearPerks::npc_lg_jack_box();
    new LivingGearPerks::npc_lg_shadow_clone();
}
