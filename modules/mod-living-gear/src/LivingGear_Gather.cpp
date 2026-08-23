#include "AllCreatureScript.h"
#include "AllGameObjectScript.h"
#include "CellImpl.h"
#include "Chat.h"
#include "Creature.h"
#include "CreatureData.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "GameTime.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "LootMgr.h"
#include "Map.h"
#include "MiscScript.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Castable perks only get learned; badges do not. LivingGear_Perks.cpp.
bool LivingGear_PerkIsCastable(uint32 spellId);

class Player;
bool IsAutolootEnabled(Player* player); // LivingGear_Vault.cpp

class Player;
void LivingGear_SendAddonLine(Player* player, std::string const& line); // LivingGear.cpp
bool LivingGear_SafeToCastOn(Player* player); // LivingGear_Support.cpp
void LivingGear_DiagBump(Player* player, char const* key); // LivingGear_Support.cpp
bool LivingGear_IsAddonSendInProgress(); // LivingGear.cpp

namespace LivingGearGather
{
uint32 const SPELL_MINE_YIELD[] = { 910109, 910110, 910111 };
uint32 const SPELL_MINE_REACH[] = { 910112, 910113, 910114 };
uint32 const SPELL_HERB_YIELD[] = { 910115, 910116, 910117 };
uint32 const SPELL_HERB_REACH[] = { 910118, 910119, 910120 };
uint32 const SPELL_SKIN_YIELD[] = { 910121, 910122, 910123 };
uint32 const SPELL_SKIN_REACH[] = { 910124, 910125, 910126 };
uint32 const SPELL_FISH_YIELD[] = { 910127, 910128, 910129 };
uint32 const SPELL_FISH_REACH[] = { 910130, 910131, 910132 };
uint32 const SPELL_ENG_YIELD[] = { 910133, 910134, 910135 };
uint32 const SPELL_ENG_REACH[] = { 910136, 910137, 910138 };
// Fishing track, 2026-08-22. These three were advertised in the panel and
// earnable since the track was written, and read by nothing -- found by
// tools/perk_audit.py rather than by another player report.
//
// Auto-CATCH already worked and was deliberately ungated (see TryAutoCatchFish).
// What "Cast" actually promises is the other half: that fishing re-casts itself.
uint32 const SPELL_FISH_AUTOCAST = 910043;
uint32 const SPELL_FISH_POOLS = 910044;
// "Speed" (910045) is implemented in LivingGear_Perks.cpp as
// SPELL_FISH_BITE_SPEED, because cast time is set in OnSpellPrepare and that
// file owns the spell hook. Deliberately not declared twice.
float const FISH_POOL_RANGE = 25.0f;
// How long to wait after a catch before casting again. Long enough to read as
// a person deciding to cast, short enough not to feel like a hang.
uint32 const FISH_RECAST_MS = 1200;

uint32 const SPELL_SPARKLE_HERB = 910139;
uint32 const SPELL_SPARKLE_MINE = 910140;
uint32 const SPELL_SPARKLE_FISH = 910141;
uint32 const SPELL_RARE_PULSE = 910142;
uint32 const YIELD_NEED[] = { 150, 300, 450 };
uint32 const REACH_NEED[] = { 75, 225, 375 };
uint32 const RARE_RESPAWN_CAP = 180;
uint32 const RAID_ICON_SKULL = 7;
float const GATHER_BASE_RANGE = 10.0f;
float const REACH_PER_RANK = 3.0f;
float const SPARKLE_RANGE = 60.0f;
float const RARE_MARK_RANGE = 150.0f;

uint32 const ALL_GATHER_PERKS[] = {
    910109, 910110, 910111, 910112, 910113, 910114,
    910115, 910116, 910117, 910118, 910119, 910120,
    910121, 910122, 910123, 910124, 910125, 910126,
    910127, 910128, 910129, 910130, 910131, 910132,
    910133, 910134, 910135, 910136, 910137, 910138
};

std::unordered_map<uint32, std::unordered_set<uint32>> g_perks;
std::unordered_map<uint32, uint32> g_lastPulse;
std::unordered_map<uint32, uint32> g_pulseWait;
std::unordered_map<uint32, uint32> g_lastSparkle;
std::unordered_map<uint32, uint32> g_lastGatherScan;
std::unordered_set<uint32> g_perkLoaded;

void SendLine(Player* player, std::string const& line)
{
    ::LivingGear_SendAddonLine(player, line);
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

// The mirror of the Next.cpp problem: this asked only the account set, so a
// perk learned as a spell but missing its lg_account_perk row read false.
// Same answer as the other five copies now.
bool HasPerk(Player* player, uint32 spellId)
{
    if (!player || !player->GetSession())
        return false;
    if (player->HasSpell(spellId))
        return true;
    uint32 const accountId = player->GetSession()->GetAccountId();
    LoadPerks(accountId);
    return g_perks[accountId].count(spellId) > 0;
}

void UnlockPerk(Player* player, uint32 spellId, std::string const& msg)
{
    if (!player || !player->GetSession() || !sSpellMgr->GetSpellInfo(spellId))
        return;
    uint32 const accountId = player->GetSession()->GetAccountId();
    LoadPerks(accountId);
    // This never learned the spell at all, so every gather perk read false to
    // any HasSpell-flavoured check -- the same split that produced bug #25.
    bool const firstTime = g_perks[accountId].insert(spellId).second;
    if (firstTime)
        CharacterDatabase.DirectExecute(
            "INSERT IGNORE INTO `lg_account_perk` (`account_id`, `spell_id`) VALUES ({}, {})",
            accountId, spellId);
    // Castable perks only -- learning a badge spams chat and puts nothing in
    // the spellbook. See LivingGear_PerkIsCastable in LivingGear_Perks.cpp.
    if (LivingGear_PerkIsCastable(spellId) && !player->HasSpell(spellId))
        player->learnSpell(spellId);
    if (!firstTime)
        return;
    SendLine(player, Acore::StringFormat("PK|{}|1", spellId));
    if (!msg.empty())
        ChatHandler(player->GetSession()).SendSysMessage(msg.c_str());
}

uint32 AccountSkill(Player* player, uint32 skillId)
{
    if (!player || !player->GetSession())
        return 0;
    uint32 skill = player->GetSkillValue(skillId);
    uint32 const accountId = player->GetSession()->GetAccountId();
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT MAX(cs.`value`) FROM `character_skills` cs "
        "INNER JOIN `characters` c ON c.`guid` = cs.`guid` "
        "WHERE c.`account` = {} AND cs.`skill` = {}",
        accountId, skillId))
    {
        if (!(*result)[0].IsNull())
        {
            uint32 const acc = (*result)[0].Get<uint32>();
            if (acc > skill)
                skill = acc;
        }
    }
    return skill;
}

void CheckSkillTrack(Player* player, uint32 skillId, uint32 const* yieldIds, uint32 const* reachIds, char const* label)
{
    if (!player)
        return;
    uint32 const skill = AccountSkill(player, skillId);
    if (!skill)
        return;
    for (uint32 i = 0; i < 3; ++i)
    {
        if (skill < YIELD_NEED[i])
            continue;
        UnlockPerk(player, yieldIds[i], Acore::StringFormat(
            "|cff66ccff[Account Perks]|r {} yield {}x unlocked (skill {}).",
            label, 1u << (i + 1), YIELD_NEED[i]));
    }
    for (uint32 i = 0; i < 3; ++i)
    {
        if (skill < REACH_NEED[i])
            continue;
        UnlockPerk(player, reachIds[i], Acore::StringFormat(
            "|cff66ccff[Account Perks]|r {} auto-gather +{} yd unlocked (skill {}).",
            label, uint32(REACH_PER_RANK * (i + 1)), REACH_NEED[i]));
    }
}

void CheckAllTracks(Player* player)
{
    CheckSkillTrack(player, SKILL_MINING, SPELL_MINE_YIELD, SPELL_MINE_REACH, "Mining");
    CheckSkillTrack(player, SKILL_HERBALISM, SPELL_HERB_YIELD, SPELL_HERB_REACH, "Herbalism");
    CheckSkillTrack(player, SKILL_SKINNING, SPELL_SKIN_YIELD, SPELL_SKIN_REACH, "Skinning");
    CheckSkillTrack(player, SKILL_FISHING, SPELL_FISH_YIELD, SPELL_FISH_REACH, "Fishing");
    CheckSkillTrack(player, SKILL_ENGINEERING, SPELL_ENG_YIELD, SPELL_ENG_REACH, "Engineering");
}

void SyncPerkTicks(Player* player)
{
    if (!player || !player->GetSession())
        return;
    uint32 const accountId = player->GetSession()->GetAccountId();
    LoadPerks(accountId);
    for (uint32 spellId : ALL_GATHER_PERKS)
        SendLine(player, Acore::StringFormat("PK|{}|{}", spellId,
            g_perks[accountId].count(spellId) ? 1 : 0));
}

uint32 YieldMult(Player* player, uint32 const* yieldIds)
{
    uint32 ranks = 0;
    for (uint32 i = 0; i < 3; ++i)
        if (HasPerk(player, yieldIds[i]))
            ranks = i + 1;
    if (!ranks)
        return 1;
    return 1u << ranks;
}

uint32 const GATHER_SKILL_IDS[] = { SKILL_MINING, SKILL_HERBALISM, SKILL_SKINNING, SKILL_FISHING, SKILL_ENGINEERING };

// Cross-profession skill-up acceleration: leveling any ONE gathering
// profession to a 75-point milestone speeds up how fast every OTHER
// gathering profession gains skill points (not itself) -- e.g. Fishing 300
// grants +4 bonus skill points per gather action to Mining/Herbalism/etc.
// Deliberately separate from YieldMult (own-skill-gated material count per
// gather) and ExtraReach (own-skill-gated auto-loot range) -- three
// complementary but independent systems.
uint32 CrossProfessionTier(Player* player, uint32 selfSkillId)
{
    uint32 best = 0;
    for (uint32 skillId : GATHER_SKILL_IDS)
    {
        if (skillId == selfSkillId)
            continue;
        uint32 const skill = AccountSkill(player, skillId);
        if (skill > best)
            best = skill;
    }
    return best / 75;
}

float ExtraReach(Player* player, uint32 const* reachIds)
{
    uint32 ranks = 0;
    for (uint32 i = 0; i < 3; ++i)
        if (HasPerk(player, reachIds[i]))
            ranks = i + 1;
    return REACH_PER_RANK * float(ranks);
}

bool IsRareCreature(Creature* creature)
{
    if (!creature || creature->IsPet() || creature->IsSummon() || creature->isWorldBoss())
        return false;
    if (!creature->GetSpawnId())
        return false;
    uint32 const rank = creature->GetCreatureTemplate()->rank;
    return rank == CREATURE_ELITE_RARE || rank == CREATURE_ELITE_RAREELITE;
}

void CapRareRespawn(Creature* creature)
{
    if (!IsRareCreature(creature))
        return;
    if (creature->GetRespawnDelay() > RARE_RESPAWN_CAP)
        creature->SetRespawnDelay(RARE_RESPAWN_CAP);
    time_t const now = GameTime::GetGameTime().count();
    time_t const respawn = creature->GetRespawnTime();
    if (respawn > now + time_t(RARE_RESPAWN_CAP))
    {
        creature->SetRespawnTime(RARE_RESPAWN_CAP);
        creature->SaveRespawnTime();
    }
}

void SendSkullIcon(Player* player, Creature* creature)
{
    if (!player || !player->GetSession() || !creature)
        return;
    WorldPacket data(MSG_RAID_TARGET_UPDATE, 1 + 8 + 1 + 8);
    data << uint8(0);
    data << player->GetGUID();
    data << uint8(RAID_ICON_SKULL);
    data << creature->GetGUID();
    player->GetSession()->SendPacket(&data);
}

void PulseRare(Creature* creature)
{
    if (!creature || !creature->IsAlive() || !creature->IsInWorld())
        return;
    CapRareRespawn(creature);
    uint32 const low = creature->GetGUID().GetCounter();
    uint32 const now = getMSTime();
    uint32& wait = g_pulseWait[low];
    if (!wait)
        wait = urand(6000, 10000);
    uint32& last = g_lastPulse[low];
    if (last && getMSTimeDiff(last, now) < wait)
        return;
    last = now;
    wait = urand(6000, 10000);

    Player* watcher = creature->SelectNearestPlayer(RARE_MARK_RANGE);
    if (!watcher)
        return;

    if (sSpellMgr->GetSpellInfo(SPELL_RARE_PULSE))
        creature->CastSpell(creature, SPELL_RARE_PULSE, true);
    else
        creature->SendPlaySpellVisual(1162);

    Map::PlayerList const& players = creature->GetMap()->GetPlayers();
    for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
    {
        Player* player = itr->GetSource();
        if (!player || !player->IsInWorld())
            continue;
        if (!creature->IsWithinDistInMap(player, RARE_MARK_RANGE, false))
            continue;
        SendSkullIcon(player, creature);
    }
}

bool GatherLockType(uint32 lockId, uint32& skillId, uint32& reqSkill)
{
    LockEntry const* lock = sLockStore.LookupEntry(lockId);
    if (!lock)
        return false;
    for (int i = 0; i < MAX_LOCK_CASE; ++i)
    {
        if (lock->Type[i] != LOCK_KEY_SKILL)
            continue;
        uint32 const lockType = lock->Index[i];
        if (lockType == LOCKTYPE_HERBALISM)
            skillId = SKILL_HERBALISM;
        else if (lockType == LOCKTYPE_MINING)
            skillId = SKILL_MINING;
        else if (lockType == LOCKTYPE_BLASTING)
            skillId = SKILL_ENGINEERING;
        else if (lockType == LOCKTYPE_FISHING)
            skillId = SKILL_FISHING;
        else
            continue;
        reqSkill = lock->Skill[i];
        return true;
    }
    return false;
}

uint32 SparkleForSkill(uint32 skillId)
{
    if (skillId == SKILL_HERBALISM)
        return SPELL_SPARKLE_HERB;
    if (skillId == SKILL_MINING)
        return SPELL_SPARKLE_MINE;
    if (skillId == SKILL_FISHING)
        return SPELL_SPARKLE_FISH;
    return SPELL_SPARKLE_MINE;
}

void SparkleGo(GameObject* go, uint32 skillId)
{
    if (!go || !go->isSpawned())
        return;
    uint32 const key = go->GetSpawnId() ? go->GetSpawnId() : go->GetGUID().GetCounter();
    uint32 const now = getMSTime();
    uint32& last = g_lastSparkle[key];
    if (last && getMSTimeDiff(last, now) < 8000)
        return;
    Player* near = go->SelectNearestPlayer(SPARKLE_RANGE);
    if (!near)
        return;
    last = now;
    uint32 kit = 1162;
    if (skillId == SKILL_MINING)
        kit = 406;
    else if (skillId == SKILL_FISHING)
        kit = 179;
    uint32 const spellId = SparkleForSkill(skillId);
    if (sSpellMgr->GetSpellInfo(spellId))
        go->CastSpell(near, spellId);
    near->SendPlaySpellVisual(go->GetGUID(), kit);
}

void ScaleLootCounts(Loot* loot, uint32 mult)
{
    if (!loot || mult <= 1)
        return;
    auto scale = [mult](LootItem& item)
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(item.itemid);
        uint32 cap = proto ? proto->GetMaxStackSize() : 255;
        if (cap > 255)
            cap = 255;
        if (!cap)
            cap = 1;
        uint32 want = uint32(item.count) * mult;
        if (want > cap)
            want = cap;
        item.count = uint8(want);
    };
    for (LootItem& item : loot->items)
        scale(item);
    for (LootItem& item : loot->quest_items)
        scale(item);
}

uint32 YieldForStore(LootStore const& store, Player* player, Loot* loot)
{
    if (!player)
        return 1;
    if (&store == &LootTemplates_Skinning)
        return YieldMult(player, SPELL_SKIN_YIELD);
    if (&store == &LootTemplates_Fishing)
        return YieldMult(player, SPELL_FISH_YIELD);
    if (&store != &LootTemplates_Gameobject || !loot)
        return 1;
    GameObject* go = loot->sourceGameObject;
    if (!go && loot->sourceWorldObjectGUID && player->GetMap())
        go = player->GetMap()->GetGameObject(loot->sourceWorldObjectGUID);
    if (!go || !go->GetGOInfo())
        return 1;
    uint32 skillId = 0;
    uint32 req = 0;
    if (!GatherLockType(go->GetGOInfo()->GetLockId(), skillId, req))
        return 1;
    if (skillId == SKILL_MINING)
        return YieldMult(player, SPELL_MINE_YIELD);
    if (skillId == SKILL_HERBALISM)
        return YieldMult(player, SPELL_HERB_YIELD);
    if (skillId == SKILL_ENGINEERING)
        return YieldMult(player, SPELL_ENG_YIELD);
    if (skillId == SKILL_FISHING)
        return YieldMult(player, SPELL_FISH_YIELD);
    return 1;
}

void AutoStoreLoot(Player* player, Loot* loot)
{
    if (!player || !loot)
        return;
    uint32 const maxSlot = loot->GetMaxSlotInLootFor(player);
    for (uint32 slot = 0; slot < maxSlot; ++slot)
    {
        InventoryResult msg = EQUIP_ERR_OK;
        player->StoreLootItem(uint8(slot), loot, msg);
    }
    if (loot->gold)
    {
        player->ModifyMoney(loot->gold);
        loot->gold = 0;
    }
}

void TryGatherChest(Player* player, GameObject* go, uint32 skillId, uint32 reqSkill)
{
    if (!player || !go || !go->isSpawned() || go->GetGoType() != GAMEOBJECT_TYPE_CHEST)
        return;
    uint16 const skillValue = player->GetSkillValue(skillId);
    if (!skillValue || skillValue < reqSkill)
        return;
    Loot* loot = &go->loot;
    if (go->getLootState() == GO_READY)
    {
        loot->clear();
        if (uint32 lootId = go->GetGOInfo()->GetLootId())
            loot->FillLoot(lootId, LootTemplates_Gameobject, player, true, false, go->GetLootMode(), go);
        if (GameObjectTemplateAddon const* addon = go->GetTemplateAddon())
            loot->generateMoneyLoot(addon->mingold, addon->maxgold);
        go->SetLootGenerationTime();
        go->SetLootState(GO_ACTIVATED, player);
        if (uint32 pure = player->GetPureSkillValue(skillId))
            player->UpdateGatherSkill(skillId, pure, reqSkill);
    }
    AutoStoreLoot(player, loot);
    if (loot->isLooted())
        go->SetLootState(GO_JUST_DEACTIVATED);
}

// Autoloot never covered chests -- only PLAYERHOOK_ON_CREATURE_KILL
// (LivingGear_Vault.cpp). Chests are GameObjects, a completely separate
// loot path. This mirrors GameObject.cpp's own GAMEOBJECT_TYPE_CHEST
// handling exactly (same lock-type checks) so it only ever fires for the
// same chests that would otherwise go straight to player->SendLoot() --
// key- and gathering-skill-gated chests are left alone, same as before.
// Bug report #15, 2026-08-22: "solid chests STILL not autolooting, whether in
// or out of combat" -- the second report of this against a path patch 0007 was
// written to fix.
//
// Static analysis has ruled out everything checkable: Solid Chest is
// GAMEOBJECT_TYPE_CHEST (type 3) as required; its lock (57) is OPEN/TREASURE
// with a skill requirement of 0, so it goes through GameObject::Use() where the
// hook is invoked before anything else; GatherLockType does not claim it; and
// the reporter's account has autoloot enabled. So the failure is somewhere this
// code cannot be reasoned about from the outside.
//
// These are log lines, NOT chat prints -- nothing reaches a player, so unlike
// the TEMP DEBUG spam removed on this same day there is no reason to rush them
// back out. They record which stage gives up, so the next occurrence names the
// cause instead of costing another round of guessing.
void TryAutolootChest(Player* player, GameObject* go, bool& handled)
{
    if (!player || !go)
        return;
    if (go->GetGoType() != GAMEOBJECT_TYPE_CHEST)
        return;
    if (!IsAutolootEnabled(player))
    {
        LOG_INFO("module.livinggear", "chest autoloot: {} declined for '{}' -- autoloot disabled",
            player->GetName(), go->GetGOInfo()->name);
        return;
    }
    uint32 skillId = 0, reqSkill = 0;
    if (GatherLockType(go->GetGOInfo()->GetLockId(), skillId, reqSkill))
    {
        LOG_INFO("module.livinggear", "chest autoloot: '{}' left to the gathering path (skill {})",
            go->GetGOInfo()->name, skillId);
        return; // gathering-skill chest (herbalism/mining/etc) -- leave to TryGatherChest
    }
    // 2026-08-21: used to also bail here for any chest whose static
    // template lock is LOCK_KEY_ITEM, "leave it for the manual spell
    // path." That was correct back when this only ever ran from
    // GameObject::Use()'s CHEST case (unlocked chests only) -- but it's
    // also now reached from Spell::EffectOpenLock AFTER a lock has already
    // been successfully opened (key matched or skill sufficient), where
    // GetLockId() still reports the chest's unchanging static lock
    // regardless. That made this unconditionally bounce every locked
    // chest reaching autoloot via the new path, key or skill alike --
    // exactly why "Solid Chest" kept refusing to autoloot even after
    // being successfully unlocked.
    LivingGear_DiagBump(player, "chest.try");
    Loot* loot = &go->loot;
    loot->clear();
    if (uint32 lootId = go->GetGOInfo()->GetLootId())
        loot->FillLoot(lootId, LootTemplates_Gameobject, player, true, false, go->GetLootMode(), go);
    if (GameObjectTemplateAddon const* addon = go->GetTemplateAddon())
        loot->generateMoneyLoot(addon->mingold, addon->maxgold);
    go->SetLootGenerationTime();
    handled = true;
    LOG_INFO("module.livinggear", "chest autoloot: '{}' (entry {}, lock {}) filled lootId {} -- {} slot(s), {} gold",
        go->GetGOInfo()->name, go->GetEntry(), go->GetGOInfo()->GetLockId(),
        go->GetGOInfo()->GetLootId(), loot->GetMaxSlotInLootFor(player), loot->gold);

    // 2026-08-21: this is reached both from a plain right-click (GameObject::
    // Use(), safe) AND from Spell::EffectOpenLock -- which is still mid-
    // execution on the caster when it fires OnPlayerUseGameObject. Calling
    // player->StoreLootItem() synchronously from inside another Spell's own
    // effect processing is the exact reentrancy hazard documented in the
    // wiki's "Reentrant hook execution" section (same API, same crash
    // signature previously hit via OnPlayerCreatureKill's autoloot). Defer
    // the actual grant one tick and re-resolve both player and chest by
    // GUID -- either may be gone by the time this runs. Deferring on the
    // safe (right-click) path too is harmless, just one tick later.
    ObjectGuid const playerGuid = player->GetGUID();
    ObjectGuid const goGuid = go->GetGUID();
    player->m_Events.AddEventAtOffset([playerGuid, goGuid]()
    {
        Player* p = ObjectAccessor::FindPlayer(playerGuid);
        if (!p || !p->IsInWorld() || !p->GetMap())
            return;
        GameObject* obj = p->GetMap()->GetGameObject(goGuid);
        if (!obj)
            return;
        Loot* l = &obj->loot;
        uint32 const maxSlot = l->GetMaxSlotInLootFor(p);
        uint32 taken = 0, refused = 0;
        for (uint32 slot = 0; slot < maxSlot; ++slot)
        {
            InventoryResult msg = EQUIP_ERR_OK;
            p->StoreLootItem(uint8(slot), l, msg);
            if (msg == EQUIP_ERR_OK)
                ++taken;
            else
                ++refused;
        }
        LOG_INFO("module.livinggear", "chest autoloot: granted {}/{} slot(s) to {} ({} refused)",
            taken, maxSlot, p->GetName(), refused);
        if (l->gold)
        {
            p->ModifyMoney(int32(l->gold));
            l->gold = 0;
        }
        obj->SetLootState(GO_JUST_DEACTIVATED);
    }, std::chrono::milliseconds(1));
}

void TryGatherFishHole(Player* player, GameObject* go)
{
    if (!player || !go || !go->isSpawned() || go->GetGoType() != GAMEOBJECT_TYPE_FISHINGHOLE)
        return;
    if (!player->GetSkillValue(SKILL_FISHING))
        return;
    Loot* loot = &go->loot;
    if (go->getLootState() == GO_READY || loot->empty())
    {
        go->GetFishLoot(loot, player, false);
        go->SetLootState(GO_ACTIVATED, player);
    }
    AutoStoreLoot(player, loot);
    if (loot->isLooted())
        go->SetLootState(GO_JUST_DEACTIVATED);
}

void TrySkin(Player* player, Creature* creature)
{
    if (!player || !creature || creature->IsAlive())
        return;
    SkillType const skill = creature->GetCreatureTemplate()->GetRequiredLootSkill();
    uint32 const* reachIds = SPELL_SKIN_REACH;
    if (skill == SKILL_ENGINEERING)
        reachIds = SPELL_ENG_REACH;
    else if (skill == SKILL_HERBALISM)
        reachIds = SPELL_HERB_REACH;
    else if (skill == SKILL_MINING)
        reachIds = SPELL_MINE_REACH;
    else if (skill != SKILL_SKINNING)
        return;
    float const extra = ExtraReach(player, reachIds);
    if (extra <= 0.0f)
        return;
    if (!player->IsWithinDistInMap(creature, GATHER_BASE_RANGE + extra))
        return;
    if (!creature->HasUnitFlag(UNIT_FLAG_SKINNABLE))
    {
        if (creature->loot.loot_type == LOOT_SKINNING && !creature->loot.isLooted())
            AutoStoreLoot(player, &creature->loot);
        return;
    }
    int32 const skillValue = int32(player->GetSkillValue(skill));
    if (skillValue <= 0)
        return;
    int32 const targetLevel = int32(creature->GetLevel());
    int32 const canSkinReq = skillValue < 100 ? (targetLevel - 10) * 10 : targetLevel * 5;
    if (canSkinReq > skillValue)
        return;
    uint32 const skinId = creature->GetCreatureTemplate()->SkinLootId;
    if (!skinId || !LootTemplates_Skinning.HaveLootFor(skinId))
        return;
    creature->RemoveUnitFlag(UNIT_FLAG_SKINNABLE);
    creature->loot.clear();
    creature->loot.FillLoot(skinId, LootTemplates_Skinning, player, true);
    creature->loot.loot_type = LOOT_SKINNING;
    int32 reqValue = targetLevel < 10 ? 0 : targetLevel < 20 ? (targetLevel - 10) * 10 : targetLevel * 5;
    if (uint32 pure = player->GetPureSkillValue(skill))
        player->UpdateGatherSkill(skill, pure, uint32(reqValue), creature->isElite() ? 2 : 1);
    AutoStoreLoot(player, &creature->loot);
}

// A normal rod-cast catch was never implemented at all (found in the
// 2026-08-21 perk audit) -- only the FISHINGHOLE pool-node case above
// worked. The bobber is the player's own channel object, not something
// found by a nearby-object scan, so this reads UNIT_FIELD_CHANNEL_OBJECT
// directly rather than going through the range-scan loop in ScanGather.
GameObject* FindFishingBobber(Player* player)
{
    if (!player || !player->GetMap())
        return nullptr;
    ObjectGuid const guid = player->GetGuidValue(UNIT_FIELD_CHANNEL_OBJECT);
    if (!guid)
        return nullptr;
    GameObject* go = player->GetMap()->GetGameObject(guid);
    if (!go || go->GetGoType() != GAMEOBJECT_TYPE_FISHINGNODE)
        return nullptr;
    if (go->GetOwnerGUID() != player->GetGUID())
        return nullptr;
    return go;
}

// The fishing ability the player last cast themselves. Recasting THAT is more
// reliable than guessing a spell id: the castable Fishing spell differs by rank
// and by what the character has actually trained, and a wrong id would silently
// do nothing.
std::unordered_map<uint32, uint32> g_lastFishSpell;

void NoteFishingCast(Player* player, uint32 spellId)
{
    if (player && spellId)
        g_lastFishSpell[player->GetGUID().GetCounter()] = spellId;
}

// Fishing track "Cast" (910043): after the catch, throw the line again.
// Deferred rather than cast inline -- this runs from inside the bobber's own
// Use(), and starting another cast on a player mid-way through one is the
// reentrancy hazard the rest of this module already guards against.
void QueueFishingRecast(Player* player)
{
    if (!player || !HasPerk(player, SPELL_FISH_AUTOCAST))
        return;
    uint32 const spellId = g_lastFishSpell[player->GetGUID().GetCounter()];
    if (!spellId)
        return;
    ObjectGuid const guid = player->GetGUID();
    player->m_Events.AddEventAtOffset([guid, spellId]()
    {
        Player* p = ObjectAccessor::FindPlayer(guid);
        if (!LivingGear_SafeToCastOn(p) || p->IsInCombat())
            return;
        // Do not stack casts: if they already started fishing again by hand,
        // or are casting anything else, leave them alone.
        if (p->IsNonMeleeSpellCast(false, false, true))
            return;
        p->CastSpell(p, spellId, false);
    }, std::chrono::milliseconds(FISH_RECAST_MS));
}

void TryAutoCatchFish(Player* player)
{
    // 2026-08-21: was incorrectly gated on ExtraReach(SPELL_FISH_REACH) > 0
    // -- that's a RANGE-extension perk (how far away a node can be to
    // interact with), not a prerequisite for auto-catching a bobber the
    // player is already standing at and actively channeling themselves.
    // Every other gather function in this file uses reach perks to extend
    // range, never as an on/off gate for the feature working at all --
    // this was the one inconsistent with its own established pattern,
    // silently no-opping autofishing for anyone without a Fish Reach rank
    // unlocked. FindFishingBobber's UNIT_FIELD_CHANNEL_OBJECT check already
    // naturally scopes this to "only does something while actually fishing".
    if (!player)
        return;
    GameObject* bobber = FindFishingBobber(player);
    if (!bobber || bobber->getLootState() != GO_READY)
        return;
    bobber->Use(player); // same as a manual right-click on a biting bobber
    QueueFishingRecast(player);
}

void ScanGather(Player* player)
{
    if (!player || !player->IsAlive() || !player->IsInWorld() || player->IsInFlight())
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    uint32 const now = getMSTime();
    uint32& last = g_lastGatherScan[guid];
    if (last && getMSTimeDiff(last, now) < 400)
        return;
    last = now;

    TryAutoCatchFish(player);

    float range = GATHER_BASE_RANGE;
    range = std::max(range, GATHER_BASE_RANGE + ExtraReach(player, SPELL_MINE_REACH));
    range = std::max(range, GATHER_BASE_RANGE + ExtraReach(player, SPELL_HERB_REACH));
    range = std::max(range, GATHER_BASE_RANGE + ExtraReach(player, SPELL_FISH_REACH));
    range = std::max(range, GATHER_BASE_RANGE + ExtraReach(player, SPELL_ENG_REACH));
    range = std::max(range, GATHER_BASE_RANGE + ExtraReach(player, SPELL_SKIN_REACH));
    // Fishing track "Pools" (910044) reaches further than any reach perk, and
    // has to widen the scan itself -- otherwise the early return below skips
    // the whole sweep for someone whose only fishing perk is this one.
    bool const pools = HasPerk(player, SPELL_FISH_POOLS);
    if (pools)
        range = std::max(range, FISH_POOL_RANGE);
    if (range <= GATHER_BASE_RANGE + 0.01f)
        return;

    std::list<Creature*> corpses;
    player->GetDeadCreatureListInGrid(corpses, range, true);
    for (Creature* creature : corpses)
        TrySkin(player, creature);

    std::list<GameObject*> objects;
    auto collectGo = [&](GameObject* go)
    {
        if (!go || !go->isSpawned())
            return;
        uint8 const type = go->GetGoType();
        if (type != GAMEOBJECT_TYPE_CHEST && type != GAMEOBJECT_TYPE_FISHINGHOLE)
            return;
        if (!player->IsWithinDistInMap(go, range, true, false, false))
            return;
        objects.push_back(go);
    };
    Acore::GameObjectWorker<decltype(collectGo)> goWorker(player, collectGo);
    Cell::VisitObjects(player, goWorker, range);
    for (GameObject* go : objects)
    {
        if (go->GetGoType() == GAMEOBJECT_TYPE_FISHINGHOLE)
        {
            // Two ways to reach a pool now: the Fish Reach perks extend the
            // normal gather radius, and Pools grants a flat 25 yards.
            float reach = GATHER_BASE_RANGE + ExtraReach(player, SPELL_FISH_REACH);
            if (pools)
                reach = std::max(reach, FISH_POOL_RANGE);
            if (reach > GATHER_BASE_RANGE
                && player->IsWithinDistInMap(go, reach, true, false, false))
                TryGatherFishHole(player, go);
            continue;
        }
        uint32 skillId = 0;
        uint32 req = 0;
        if (!GatherLockType(go->GetGOInfo()->GetLockId(), skillId, req))
            continue;
        uint32 const* reachIds = nullptr;
        if (skillId == SKILL_MINING)
            reachIds = SPELL_MINE_REACH;
        else if (skillId == SKILL_HERBALISM)
            reachIds = SPELL_HERB_REACH;
        else if (skillId == SKILL_ENGINEERING)
            reachIds = SPELL_ENG_REACH;
        else if (skillId == SKILL_FISHING)
            reachIds = SPELL_FISH_REACH;
        if (!reachIds)
            continue;
        float const extra = ExtraReach(player, reachIds);
        if (extra <= 0.0f)
            continue;
        if (!player->IsWithinDistInMap(go, GATHER_BASE_RANGE + extra, true, false, false))
            continue;
        TryGatherChest(player, go, skillId, req);
    }
}

bool IsEngineeringCraftSpell(uint32 spellId)
{
    SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
    for (SkillLineAbilityMap::const_iterator itr = bounds.first; itr != bounds.second; ++itr)
        if (itr->second && itr->second->SkillLine == SKILL_ENGINEERING)
            return true;
    return false;
}

class GatherPlayer : public PlayerScript
{
public:
    GatherPlayer() : PlayerScript("LivingGearGatherPlayer", {
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_LOGOUT,
        PLAYERHOOK_ON_UPDATE,
        PLAYERHOOK_ON_UPDATE_SKILL,
        PLAYERHOOK_ON_CREATE_ITEM,
        PLAYERHOOK_ON_USE_GAMEOBJECT,
        PLAYERHOOK_ON_UPDATE_GATHERING_SKILL,
        PLAYERHOOK_ON_UPDATE_FISHING_SKILL,
        PLAYERHOOK_ON_SPELL_CAST
    }) { }

    // Remember which fishing ability the player actually used, so the Fishing
    // "Cast" perk can throw the same line again rather than guessing a rank.
    void OnPlayerSpellCast(Player* player, Spell* spell, bool /*skip*/) override
    {
        if (!player || !spell)
            return;
        SpellInfo const* info = spell->GetSpellInfo();
        if (info && info->IsAbilityOfSkillType(SKILL_FISHING))
            NoteFishingCast(player, info->Id);
    }

    // Skill-up rate is boosted by CrossProfessionTier (see above) -- how far
    // your OTHER gathering professions are leveled, not this one. This is
    // intentionally independent of YieldMult (material count per gather,
    // gated on this profession's own skill).
    void OnPlayerUpdateGatheringSkill(Player* player, uint32 skillId, uint32 /*currentLevel*/,
        uint32 /*gray*/, uint32 /*green*/, uint32 /*yellow*/, uint32& gain) override
    {
        bool const tracked = skillId == SKILL_MINING || skillId == SKILL_HERBALISM ||
            skillId == SKILL_SKINNING || skillId == SKILL_ENGINEERING;
        if (tracked)
            gain += CrossProfessionTier(player, skillId);
    }

    bool OnPlayerUpdateFishingSkill(Player* player, int32 /*skill*/, int32 /*zoneSkill*/,
        int32 /*chance*/, int32 /*roll*/) override
    {
        // Unlike the gathering hook above, this one has no gain-amount
        // parameter to add to -- it's a plain "does this attempt succeed"
        // gate, and UpdateFishingSkill() itself always adds exactly 1 point.
        // Simulate the bonus by calling it extra times ourselves, then
        // letting the engine's own call (return true) add the last one.
        uint32 const extra = CrossProfessionTier(player, SKILL_FISHING);
        for (uint32 i = 0; i < extra; ++i)
            player->UpdateFishingSkill();
        return true;
    }

    void OnPlayerUseGameObject(Player* player, GameObject* go, bool& handled) override
    {
        TryAutolootChest(player, go, handled);
    }

    void OnPlayerLogin(Player* player) override
    {
        if (!player || !player->GetSession())
            return;
        LoadPerks(player->GetSession()->GetAccountId());
        CheckAllTracks(player);
        SyncPerkTicks(player);
    }

    void OnPlayerLogout(Player* player) override
    {
        if (!player)
            return;
        g_lastGatherScan.erase(player->GetGUID().GetCounter());
        g_lastFishSpell.erase(player->GetGUID().GetCounter());
    }

    void OnPlayerUpdate(Player* player, uint32 /*diff*/) override
    {
        // Crash guard: several ticks below cast auras on the player, and doing
        // that after Player::CleanupsBeforeDelete asserts on !m_cleanupDone and
        // takes the realm down. See LivingGear_SafeToCastOn.
        if (!LivingGear_SafeToCastOn(player))
            return;
        ScanGather(player);
    }

    void OnPlayerUpdateSkill(Player* player, uint32 skillId, uint32 /*value*/, uint32 /*max*/,
        uint32 /*step*/, uint32 /*newValue*/) override
    {
        if (skillId == SKILL_MINING)
            CheckSkillTrack(player, SKILL_MINING, SPELL_MINE_YIELD, SPELL_MINE_REACH, "Mining");
        else if (skillId == SKILL_HERBALISM)
            CheckSkillTrack(player, SKILL_HERBALISM, SPELL_HERB_YIELD, SPELL_HERB_REACH, "Herbalism");
        else if (skillId == SKILL_SKINNING)
            CheckSkillTrack(player, SKILL_SKINNING, SPELL_SKIN_YIELD, SPELL_SKIN_REACH, "Skinning");
        else if (skillId == SKILL_FISHING)
            CheckSkillTrack(player, SKILL_FISHING, SPELL_FISH_YIELD, SPELL_FISH_REACH, "Fishing");
        else if (skillId == SKILL_ENGINEERING)
            CheckSkillTrack(player, SKILL_ENGINEERING, SPELL_ENG_YIELD, SPELL_ENG_REACH, "Engineering");
    }

    void OnPlayerCreateItem(Player* player, Item* item, uint32 count) override
    {
        if (!player || !item || !count)
            return;
        uint32 const mult = YieldMult(player, SPELL_ENG_YIELD);
        if (mult <= 1)
            return;
        bool engineering = false;
        for (uint8 i = CURRENT_MELEE_SPELL; i < CURRENT_MAX_SPELL; ++i)
        {
            Spell* spell = player->GetCurrentSpell(CurrentSpellTypes(i));
            if (spell && spell->GetSpellInfo() && IsEngineeringCraftSpell(spell->GetSpellInfo()->Id))
            {
                engineering = true;
                break;
            }
        }
        if (!engineering)
            return;
        uint32 extra = count * (mult - 1);
        if (extra)
            player->AddItem(item->GetEntry(), extra);
    }
};

class GatherLoot : public MiscScript
{
public:
    GatherLoot() : MiscScript("LivingGearGatherLoot", { MISCHOOK_ON_AFTER_LOOT_TEMPLATE_PROCESS }) { }

    void OnAfterLootTemplateProcess(Loot* loot, LootTemplate const* /*tab*/, LootStore const& store,
        Player* lootOwner, bool /*personal*/, bool /*noEmptyError*/, uint16 /*lootMode*/) override
    {
        if (!loot || !lootOwner)
            return;
        uint32 const mult = YieldForStore(store, lootOwner, loot);
        ScaleLootCounts(loot, mult);
    }
};

class GatherCreature : public AllCreatureScript
{
public:
    GatherCreature() : AllCreatureScript("LivingGearGatherCreature") { }

    void OnCreatureAddWorld(Creature* creature) override
    {
        CapRareRespawn(creature);
    }

    void OnCreatureRemoveWorld(Creature* creature) override
    {
        if (!creature)
            return;
        uint32 const low = creature->GetGUID().GetCounter();
        g_lastPulse.erase(low);
        g_pulseWait.erase(low);
    }

    void OnAllCreatureUpdate(Creature* creature, uint32 /*diff*/) override
    {
        if (!IsRareCreature(creature))
            return;
        PulseRare(creature);
    }
};

class GatherGameObject : public AllGameObjectScript
{
public:
    GatherGameObject() : AllGameObjectScript("LivingGearGatherGameObject") { }

    void OnGameObjectAddWorld(GameObject* go) override
    {
        if (!go)
            return;
        if (go->GetGoType() == GAMEOBJECT_TYPE_FISHINGHOLE)
        {
            SparkleGo(go, SKILL_FISHING);
            return;
        }
        if (go->GetGoType() != GAMEOBJECT_TYPE_CHEST)
            return;
        uint32 skillId = 0;
        uint32 req = 0;
        if (GatherLockType(go->GetGOInfo()->GetLockId(), skillId, req))
            SparkleGo(go, skillId);
    }

    void OnGameObjectRemoveWorld(GameObject* go) override
    {
        if (!go)
            return;
        uint32 const key = go->GetSpawnId() ? go->GetSpawnId() : go->GetGUID().GetCounter();
        g_lastSparkle.erase(key);
    }

    void OnGameObjectUpdate(GameObject* go, uint32 /*diff*/) override
    {
        if (!go || !go->isSpawned())
            return;
        if (go->GetGoType() == GAMEOBJECT_TYPE_FISHINGHOLE)
        {
            SparkleGo(go, SKILL_FISHING);
            return;
        }
        if (go->GetGoType() != GAMEOBJECT_TYPE_CHEST)
            return;
        uint32 skillId = 0;
        uint32 req = 0;
        if (GatherLockType(go->GetGOInfo()->GetLockId(), skillId, req))
            SparkleGo(go, skillId);
    }
};

} // namespace LivingGearGather

// Called from a small core-patch in Spell::CanOpenLock (SpellEffects.cpp)
// so Lockpicking is account-wide like Riding already is -- an alt with 0
// Lockpicking can open anything a higher-skill character on the same
// account could. Only used for SKILL_LOCKPICKING specifically; the other
// LOCK_KEY_SKILL types (Herbalism/Mining/Blasting/etc.) are gathering
// locks already handled entirely by this module's own scan-based system
// and never reach the engine's native skill check for that purpose.
uint32 LivingGear_AccountLockpickSkill(Player* player)
{
    return LivingGearGather::AccountSkill(player, SKILL_LOCKPICKING);
}

void AddSC_LivingGearGather()
{
    new LivingGearGather::GatherPlayer();
    new LivingGearGather::GatherLoot();
    new LivingGearGather::GatherCreature();
    new LivingGearGather::GatherGameObject();
}
