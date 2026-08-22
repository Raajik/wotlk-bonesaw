// Reagent/quest-item vault storage and autoloot rule engine.
//
// This is a from-scratch reimplementation, not a restore of the backup --
// per project practice (see Bonesaw.md "AI agent practices"), lost
// functionality gets rewritten fresh rather than merged back in from the
// pre-crash archive. The archive (archive/living-gear-backups/LivingGear.cpp.fixed-20260819)
// was read for design reference only (wire protocol, DB schema, the rule
// struct shape) since the client addon (LivingGear.lua) already speaks a
// fixed protocol for this feature (VLT| sync lines, RULEADD/RULEDEL/RULECLR/
// RULERESET/DEPOSITALL) that predates this file and must be matched exactly.
//
// All rule match types are implemented (2026-08-20): Living reuses
// LivingGear.cpp's IsEligible via the IsLivingGearEligibleItem() wrapper
// below; Attuned/Unattuned check `lg_absorb` presence for the item entry;
// Recipe/Food/Potion/Bags are plain ItemTemplate class/subclass checks.
// Disenchant and Learn-recipe *actions* (as opposed to match types) still
// fall back to leaving the item in the bag -- see ApplyLootRule.
#include "Bag.h"
#include "Chat.h"
#include "Creature.h"
#include "CreatureData.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "LootMgr.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "World.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class ItemTemplate;
bool IsLivingGearEligibleItem(ItemTemplate const* proto); // LivingGear.cpp

class Player;
void LivingGear_SendAddonLine(Player* player, std::string const& line); // LivingGear.cpp
bool LivingGear_IsAddonSendInProgress(); // LivingGear.cpp

namespace LivingGearVault
{

uint32 const SPELL_AUTOLOOT = 910008;

enum LgVaultKind : uint8 { VAULT_QUEST = 1, VAULT_REAGENT = 2 };
enum LgMatch : uint8
{
    MATCH_ALL = 0, MATCH_POOR = 1, MATCH_QUEST = 2, MATCH_REAGENT = 3,
    MATCH_LIVING = 4, MATCH_NAME = 5, MATCH_QUALITY = 6,
    MATCH_UNATTUNED = 7, MATCH_ATTUNED = 8, MATCH_RECIPE = 9,
    MATCH_FOOD = 10, MATCH_POTION = 11, MATCH_BAGS = 12, MATCH_SCROLL = 13,
    MATCH_ILVL = 14
};
// These indices are a wire format shared with the client's ACTION_NAMES
// (LivingGear.lua) and with every rule already stored in
// lg_autoloot_rule -- they can be renamed but never renumbered.
//
// Index 2 reads "Hold" in the client and is what DEFAULT_RULES assigns to
// quest items, but it was ACT_QUEST_VAULT here: destroy the bag copy and
// file it under VAULT_QUEST. So the client's own default rule set quietly
// ate every quest item a player looted from the moment they touched the
// rules UI, into a vault that was dropped on 2026-08-21 and has no
// withdraw path (583 items were stranded there). It now means what the
// label says -- leave it alone. Stranded rows are handed back by
// DrainLegacyQuestVault() at login.
enum LgAction : uint8
{
    ACT_BAG = 0, ACT_VENDOR = 1, ACT_HOLD = 2, ACT_REAGENT_VAULT = 3,
    ACT_SKIP = 4, ACT_DISENCHANT = 5, ACT_LEARN = 6
};

struct LgRule
{
    uint8 match = MATCH_ALL;
    uint8 action = ACT_BAG;
    uint8 negate = 0;
    uint8 quality = 0;
    std::string text;
};

struct VaultKey
{
    uint32 ownerGuid = 0;
    uint8 kind = 0;
    uint32 itemEntry = 0;
    bool operator==(VaultKey const& o) const
    {
        return ownerGuid == o.ownerGuid && kind == o.kind && itemEntry == o.itemEntry;
    }
};

struct VaultKeyHash
{
    std::size_t operator()(VaultKey const& k) const
    {
        return std::size_t(k.ownerGuid) ^ (std::size_t(k.kind) << 20) ^ (std::size_t(k.itemEntry) << 1);
    }
};

std::unordered_map<uint32, std::unordered_map<VaultKey, uint32, VaultKeyHash>> g_vaults;
std::unordered_map<uint32, std::vector<LgRule>> g_rules;
std::unordered_set<uint32> g_vaultLoaded;
std::unordered_set<uint32> g_rulesLoaded;

std::unordered_map<uint32, bool> g_autolootOn;
std::unordered_map<uint32, bool> g_autolootDe;
std::unordered_set<uint32> g_autolootLoaded;

// Account-wide Key Ring (2026-08-21): lg_account_key may not exist yet on
// a fresh deploy that hasn't picked up the migration -- probe once and
// degrade gracefully (matches LivingGear_ClassPerks.cpp's DetectSchema
// pattern) rather than hard-failing every login/loot on a missing table.
bool g_accountKeySchemaChecked = false;
bool g_hasAccountKeyTable = false;

void DetectAccountKeySchema()
{
    if (g_accountKeySchemaChecked)
        return;
    g_accountKeySchemaChecked = true;
    if (QueryResult tables = CharacterDatabase.Query(
        "SELECT COUNT(*) FROM `information_schema`.`TABLES` "
        "WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = 'lg_account_key'"))
        g_hasAccountKeyTable = (*tables)[0].Get<uint64>() > 0;
}

void SendLine(Player* player, std::string const& line)
{
    ::LivingGear_SendAddonLine(player, line);
}

void LoadVault(uint32 accountId)
{
    if (!g_vaultLoaded.insert(accountId).second)
        return;
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT `owner_guid`, `kind`, `item_entry`, `item_count` FROM `lg_vault` WHERE `account_id` = {}",
        accountId))
    {
        do
        {
            Field* f = result->Fetch();
            VaultKey key{ f[0].Get<uint32>(), f[1].Get<uint8>(), f[2].Get<uint32>() };
            g_vaults[accountId][key] = f[3].Get<uint32>();
        } while (result->NextRow());
    }
}

void LoadRules(uint32 accountId)
{
    if (!g_rulesLoaded.insert(accountId).second)
        return;
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT `match_type`, `action`, `negate`, `quality`, `match_text` FROM `lg_autoloot_rule` "
        "WHERE `account_id` = {} ORDER BY `sort_idx`", accountId))
    {
        do
        {
            Field* f = result->Fetch();
            LgRule rule;
            rule.match = f[0].Get<uint8>();
            rule.action = f[1].Get<uint8>();
            rule.negate = f[2].Get<uint8>();
            rule.quality = f[3].Get<uint8>();
            rule.text = f[4].Get<std::string>();
            g_rules[accountId].push_back(rule);
        } while (result->NextRow());
    }
}

void LoadAutolootPrefs(uint32 accountId)
{
    if (!g_autolootLoaded.insert(accountId).second)
        return;
    g_autolootOn[accountId] = true;
    g_autolootDe[accountId] = false;
    if (QueryResult q = CharacterDatabase.Query(
        "SELECT `autoloot_on`, `autoloot_de` FROM `lg_account_meta` WHERE `account_id` = {}", accountId))
    {
        g_autolootOn[accountId] = (*q)[0].Get<uint8>() != 0;
        g_autolootDe[accountId] = (*q)[1].Get<uint8>() != 0;
    }
}

bool AutolootOn(uint32 accountId)
{
    LoadAutolootPrefs(accountId);
    return g_autolootOn[accountId];
}

void SendAutolootSync(Player* player)
{
    if (!player || !player->GetSession())
        return;
    uint32 const accountId = player->GetSession()->GetAccountId();
    LoadAutolootPrefs(accountId);
    SendLine(player, Acore::StringFormat("AL|{}|0|10", g_autolootOn[accountId] ? 1 : 0));
    SendLine(player, Acore::StringFormat("ALDE|{}", g_autolootDe[accountId] ? 1 : 0));
}

uint32 VaultCount(uint32 accountId, uint32 ownerGuid, uint8 kind, uint32 itemEntry)
{
    LoadVault(accountId);
    VaultKey const key{ ownerGuid, kind, itemEntry };
    auto const it = g_vaults[accountId].find(key);
    return it == g_vaults[accountId].end() ? 0 : it->second;
}

void VaultAdd(uint32 accountId, uint32 ownerGuid, uint8 kind, uint32 itemEntry, uint32 count)
{
    if (!count)
        return;
    LoadVault(accountId);
    VaultKey const key{ ownerGuid, kind, itemEntry };
    g_vaults[accountId][key] += count;
    CharacterDatabase.DirectExecute(
        "INSERT INTO `lg_vault` (`account_id`, `owner_guid`, `kind`, `item_entry`, `item_count`) "
        "VALUES ({}, {}, {}, {}, {}) ON DUPLICATE KEY UPDATE `item_count` = `item_count` + {}",
        accountId, ownerGuid, kind, itemEntry, count, count);
}

// Removes up to `count` from the vault, capped to what's actually there.
// Returns how much was actually removed.
uint32 VaultRemove(uint32 accountId, uint32 ownerGuid, uint8 kind, uint32 itemEntry, uint32 count)
{
    if (!count)
        return 0;
    LoadVault(accountId);
    VaultKey const key{ ownerGuid, kind, itemEntry };
    auto it = g_vaults[accountId].find(key);
    if (it == g_vaults[accountId].end() || !it->second)
        return 0;
    uint32 const take = std::min(count, it->second);
    it->second -= take;
    CharacterDatabase.DirectExecute(
        "UPDATE `lg_vault` SET `item_count` = `item_count` - {} "
        "WHERE `account_id` = {} AND `owner_guid` = {} AND `kind` = {} AND `item_entry` = {}",
        take, accountId, ownerGuid, kind, itemEntry);
    return take;
}

// Player::AddItem() silently drops whatever doesn't fit and still returns
// true, so anything pulling out of the vault has to do the store by hand
// to learn how much actually landed -- otherwise the overflow is removed
// from the vault and then simply vanishes. Returns the count stored.
uint32 StoreFromVault(Player* player, uint32 itemEntry, uint32 count)
{
    if (!player || !count)
        return 0;
    ItemPosCountVec dest;
    uint32 noSpace = 0;
    player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemEntry, count, &noSpace);
    uint32 const fits = count > noSpace ? count - noSpace : 0;
    if (!fits || dest.empty())
        return 0;
    Item* item = player->StoreNewItem(dest, itemEntry, true);
    if (!item)
        return 0;
    player->SendNewItem(item, fits, true, false);
    return fits;
}

// Called from a core patch in Spell::CheckCast (Spell.cpp), right before
// the engine's own reagent-count check -- reagents/tools that got
// auto-banked (ACT_REAGENT_VAULT) need to still "count as in your
// backpack" for crafting per user request, not require a manual withdraw
// first. Tops the bag up from the account-wide reagent vault just before
// the real HasItemCount check runs, so normal consumption/inventory
// bookkeeping handles everything else completely unmodified.
void TopUpReagentFromVault(Player* player, uint32 itemId, uint32 needed)
{
    if (!player || !player->GetSession() || !needed)
        return;
    uint32 const have = player->GetItemCount(itemId, true);
    if (have >= needed)
        return;
    uint32 const accountId = player->GetSession()->GetAccountId();
    uint32 const shortfall = needed - have;
    uint32 const taken = VaultRemove(accountId, 0, VAULT_REAGENT, itemId, shortfall);
    if (!taken)
        return;
    uint32 const stored = StoreFromVault(player, itemId, taken);
    if (stored < taken)
        VaultAdd(accountId, 0, VAULT_REAGENT, itemId, taken - stored); // bag full -- put the remainder back
}

// Every item id any quest anywhere references: objective items, the source
// items a quest hands out, and quest-starting items. Built once at startup
// (VaultWorld::OnStartup) from the loaded quest templates.
//
// The previous test was "ITEM_CLASS_QUEST / BIND_QUEST_ITEM, or required
// by a quest currently in this player's log". That misses exactly the
// three cases that bite: an item looted *before* its quest is accepted, an
// item that itself starts a quest, and a quest's provided source item. Any
// of those that happens to be ITEM_CLASS_TRADE_GOODS/GEM/REAGENT was
// classed as a reagent by IsReagentItem and vacuumed out of the bag into
// the reagent vault a tick after looting (DefaultLootAction) -- half of
// "quest items don't autoloot". A static index is both broader and
// cheaper: no quest-log walk per looted item.
std::unordered_set<uint32> g_questItemIds;

void BuildQuestItemIndex()
{
    g_questItemIds.clear();
    for (auto const& questPair : sObjectMgr->GetQuestTemplates())
    {
        Quest const* quest = questPair.second;
        if (!quest)
            continue;
        for (uint32 id : quest->RequiredItemId)
            if (id)
                g_questItemIds.insert(id);
        for (uint32 id : quest->ItemDrop)
            if (id)
                g_questItemIds.insert(id);
        if (uint32 const src = quest->GetSrcItemId())
            g_questItemIds.insert(src);
    }
    LOG_INFO("server.loading", "Living Gear vault: indexed {} quest-related item ids", g_questItemIds.size());
}

// `player` is only consulted if the index is empty (module loaded before
// quests, or a bad startup) -- the old quest-log walk is kept purely as
// that fallback so a failed index degrades to the previous behaviour
// instead of to "nothing is a quest item".
bool IsQuestItem(ItemTemplate const* proto, Player* player = nullptr)
{
    if (!proto)
        return false;
    if (proto->Class == ITEM_CLASS_QUEST || proto->Bonding == BIND_QUEST_ITEM)
        return true;
    if (proto->StartQuest)
        return true;
    if (!g_questItemIds.empty())
        return g_questItemIds.count(proto->ItemId) > 0;
    if (!player)
        return false;
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 const questId = player->GetQuestSlotQuestId(slot);
        if (!questId)
            continue;
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
            continue;
        for (uint32 reqItem : quest->RequiredItemId)
            if (reqItem == proto->ItemId)
                return true;
    }
    return false;
}

// The engine only ever disenchants behind a skill check (Group.cpp's
// disenchant roll gates on m_maxEnchantingLevel), so the rule engine has
// to do the same or an auto-disenchant rule would shred greens the player
// cannot actually disenchant.
bool CanDisenchant(Player* player, ItemTemplate const* proto)
{
    if (!player || !proto || !proto->DisenchantID)
        return false;
    uint32 const skill = player->GetSkillValue(SKILL_ENCHANTING);
    return skill && skill >= proto->RequiredDisenchantSkill;
}

bool IsKeyItem(ItemTemplate const* proto)
{
    return proto && proto->Class == ITEM_CLASS_KEY;
}

// Account-wide Key Ring: WotLK already auto-routes key items (BagFamily &
// BAG_FAMILY_MASK_KEYS) into the real, native Key Ring bag on a normal
// loot/StoreNewItem -- no code needed for the looting character itself.
// This only handles the "every OTHER character on the account should also
// have it" half. Following the exact convention lg_account_perk already
// uses elsewhere in this module (LivingGear_Perks.cpp UnlockPerk/
// GrantSubtletyPerks): no code anywhere in this codebase writes directly
// into an offline character's inventory, so this records the key
// account-wide and grants it to each OTHER character on their own next
// login instead (GrantAccountKeys, called from OnPlayerLogin below).
void RecordAccountKey(uint32 accountId, uint32 itemEntry)
{
    DetectAccountKeySchema();
    if (!g_hasAccountKeyTable)
        return;
    CharacterDatabase.DirectExecute(
        "INSERT IGNORE INTO `lg_account_key` (`account_id`, `item_entry`) VALUES ({}, {})",
        accountId, itemEntry);
}

void GrantAccountKeys(Player* player)
{
    if (!player || !player->GetSession())
        return;
    DetectAccountKeySchema();
    if (!g_hasAccountKeyTable)
        return;
    uint32 const accountId = player->GetSession()->GetAccountId();
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT `item_entry` FROM `lg_account_key` WHERE `account_id` = {}", accountId))
    {
        do
        {
            uint32 const itemEntry = (*result)[0].Get<uint32>();
            if (!player->HasItemCount(itemEntry, 1))
                player->AddItem(itemEntry, 1);
        } while (result->NextRow());
    }
}

// Profession tools (Mining Pick, Skinning Knife, Blacksmith Hammer,
// Arclight Spanner, ...) are ITEM_CLASS_WEAPON / ITEM_SUBCLASS_WEAPON_MISC
// in this item data, not ITEM_CLASS_TRADE_GOODS -- confirmed live against
// item_template (2026-08-21) rather than assumed.
bool IsToolItem(ItemTemplate const* proto)
{
    return proto && proto->Class == ITEM_CLASS_WEAPON && proto->SubClass == ITEM_SUBCLASS_WEAPON_MISC;
}

bool IsReagentItem(ItemTemplate const* proto, Player* player = nullptr)
{
    if (!proto || IsQuestItem(proto, player) || IsKeyItem(proto))
        return false;
    return proto->Class == ITEM_CLASS_TRADE_GOODS
        || proto->Class == ITEM_CLASS_GEM
        || proto->Class == ITEM_CLASS_REAGENT
        || IsToolItem(proto);
}

bool IsLockbox(ItemTemplate const* proto)
{
    return proto && proto->LockID && proto->HasFlag(ITEM_FLAG_HAS_LOOT);
}

bool NamesMatch(std::string const& want, std::string const& have)
{
    if (want.empty())
        return false;
    std::string a = want, b = have;
    for (char& c : a) c = char(tolower(c));
    for (char& c : b) c = char(tolower(c));
    return b.find(a) != std::string::npos;
}

bool IsAttuned(uint32 accountId, uint32 itemEntry)
{
    if (QueryResult q = CharacterDatabase.Query(
        "SELECT 1 FROM `lg_absorb` WHERE `account_id` = {} AND `item_entry` = {}", accountId, itemEntry))
        return true;
    return false;
}

bool RuleMatches(LgRule const& rule, ItemTemplate const* proto, uint32 accountId, Player* player = nullptr)
{
    if (rule.match == MATCH_QUALITY)
    {
        // rule.negate is reused here as a 4-value comparison op
        // (0 == , 1 != , 2 >= , 3 <=) instead of a plain negate bit --
        // Type/Name rules below still treat it as a real 0/1 negate. No
        // schema change needed: it was already a uint8 column/field.
        switch (rule.negate)
        {
            case 1: return proto->Quality != rule.quality;
            case 2: return proto->Quality >= rule.quality;
            case 3: return proto->Quality <= rule.quality;
            default: return proto->Quality == rule.quality;
        }
    }
    if (rule.match == MATCH_ILVL)
    {
        // rule.text carries a "min-max" encoded range (reusing the same
        // free-text column MATCH_NAME uses) rather than adding new columns.
        uint32 lo = 0, hi = 0xFFFFFFFF;
        sscanf(rule.text.c_str(), "%u-%u", &lo, &hi);
        bool const result = proto->ItemLevel >= lo && proto->ItemLevel <= hi;
        return rule.negate ? !result : result;
    }
    bool result = false;
    switch (rule.match)
    {
        case MATCH_ALL: result = true; break;
        case MATCH_POOR: result = proto->Quality == ITEM_QUALITY_POOR; break;
        case MATCH_QUEST: result = IsQuestItem(proto, player); break;
        case MATCH_REAGENT: result = IsReagentItem(proto, player); break;
        case MATCH_LIVING: result = IsLivingGearEligibleItem(proto); break;
        case MATCH_NAME: result = NamesMatch(rule.text, proto->Name1); break;
        case MATCH_UNATTUNED:
            result = IsLivingGearEligibleItem(proto) && !IsAttuned(accountId, proto->ItemId);
            break;
        case MATCH_ATTUNED:
            result = IsAttuned(accountId, proto->ItemId);
            break;
        case MATCH_RECIPE: result = proto->Class == ITEM_CLASS_RECIPE; break;
        case MATCH_FOOD: result = proto->Class == ITEM_CLASS_CONSUMABLE && proto->SubClass == ITEM_SUBCLASS_FOOD; break;
        case MATCH_POTION: result = proto->IsPotion(); break;
        case MATCH_BAGS: result = proto->Class == ITEM_CLASS_CONTAINER; break;
        case MATCH_SCROLL: result = proto->Class == ITEM_CLASS_CONSUMABLE && proto->SubClass == ITEM_SUBCLASS_SCROLL; break;
        default: result = false; break;
    }
    return rule.negate ? !result : result;
}

uint8 DefaultLootAction(ItemTemplate const* proto, Player* player = nullptr)
{
    if (IsLockbox(proto))
        return ACT_BAG;
    // 2026-08-21: quest vault dropped entirely per user request ("should no
    // longer even exist") -- it never actually worked (nothing ever
    // restored a vaulted item before a quest turn-in, so items just
    // vanished from the player with no way back). Quest items now simply
    // stay in the bag like anything else -- IsQuestItem(proto, player) is
    // still used (via IsReagentItem's exclusion check below) to keep them
    // out of the reagent vault, just no longer routed anywhere itself.
    if (IsReagentItem(proto, player))
        return ACT_REAGENT_VAULT;
    if (proto->Quality == ITEM_QUALITY_POOR)
        return ACT_VENDOR;
    return ACT_BAG;
}

uint8 ResolveLootAction(Player* player, ItemTemplate const* proto)
{
    if (!player || !proto || !player->GetSession())
        return ACT_BAG;
    uint32 const accountId = player->GetSession()->GetAccountId();
    LoadRules(accountId);
    for (LgRule const& rule : g_rules[accountId])
        if (RuleMatches(rule, proto, accountId, player))
            return rule.action;
    return DefaultLootAction(proto, player);
}

// Destroying an item from inside OnPlayerStoreNewItem is a use-after-free:
// the item is still in ITEM_NEW state at that point (never saved), and
// Item::SetState(ITEM_REMOVED, ...) special-cases brand-new items by
// calling `delete this` immediately instead of the normal deferred
// removal. StoreLootItem (the caller of StoreNewItem, which is what fires
// this hook) still holds that now-freed pointer and uses it right after
// to send the loot notification -- crashing the server (SIGSEGV in
// Player::SendNewItem -> Item::GetCount, 2026-08-20). Only the read-only
// "what should happen to this item" decision runs synchronously here; the
// actual destroy+redirect is deferred a tick, by which point the item has
// been saved and DestroyItem takes its normal (safe) path.
static void ApplyLootRuleAction(ObjectGuid playerGuid, ObjectGuid itemGuid, uint8 action,
    uint32 itemId, uint32 count, uint32 sellPrice)
{
    Player* player = ObjectAccessor::FindPlayer(playerGuid);
    if (!player || !player->IsInWorld())
        return;
    Item* item = player->GetItemByGuid(itemGuid);
    if (!item || item->GetEntry() != itemId)
        return;
    uint32 const accountId = player->GetSession() ? player->GetSession()->GetAccountId() : 0;
    if (!accountId)
        return;

    if (action == ACT_VENDOR)
    {
        uint32 const gold = sellPrice * count;
        player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
        if (gold)
            player->ModifyMoney(int32(gold));
        return;
    }
    if (action == ACT_REAGENT_VAULT)
    {
        VaultAdd(accountId, 0, VAULT_REAGENT, itemId, count);
        player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
        SendLine(player, Acore::StringFormat("VLT|{}|{}|{}|{}",
            uint32(VAULT_REAGENT), itemId, VaultCount(accountId, 0, VAULT_REAGENT, itemId),
            sObjectMgr->GetItemTemplate(itemId) ? sObjectMgr->GetItemTemplate(itemId)->Name1 : "Item"));
        return;
    }
    if (action == ACT_SKIP)
        player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
    if (action == ACT_DISENCHANT)
    {
        // Same path the engine's own group-loot disenchant roll uses
        // (Group.cpp: player->AutoStoreLoot(pProto->DisenchantID,
        // LootTemplates_Disenchant, true)) -- both ACT_DISENCHANT and
        // ACT_LEARN were already selectable in the rule builder's action
        // dropdown (ACTION_NAMES) but had no handler here, so picking
        // either was a silent no-op that just left the item in the bag.
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
        if (proto && proto->DisenchantID)
        {
            player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
            player->AutoStoreLoot(proto->DisenchantID, LootTemplates_Disenchant, true);
        }
        return;
    }
    if (action == ACT_LEARN)
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
        if (proto)
        {
            for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
            {
                if (proto->Spells[i].SpellId <= 0)
                    continue;
                SpellInfo const* useSpell = sSpellMgr->GetSpellInfo(uint32(proto->Spells[i].SpellId));
                if (!useSpell)
                    continue;
                for (uint8 e = 0; e < MAX_SPELL_EFFECTS; ++e)
                {
                    if (useSpell->Effects[e].Effect != SPELL_EFFECT_LEARN_SPELL)
                        continue;
                    uint32 const taught = useSpell->Effects[e].TriggerSpell;
                    if (taught && !player->HasSpell(taught))
                        player->learnSpell(taught);
                    player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
                    return;
                }
            }
        }
    }
}

void ApplyLootRule(Player* player, Item* item)
{
    if (!player || !item || !player->GetSession() || !player->HasSpell(SPELL_AUTOLOOT)
        || !AutolootOn(player->GetSession()->GetAccountId()))
        return;
    ItemTemplate const* proto = item->GetTemplate();
    if (!proto)
        return;
    uint8 action = ResolveLootAction(player, proto);
    if (IsLockbox(proto) && (action == ACT_VENDOR || action == ACT_SKIP))
        action = ACT_BAG;
    // The "not implemented yet, downgrade both to ACT_BAG" guard that used
    // to sit here outlived its reason: ApplyLootRuleAction grew real
    // ACT_DISENCHANT/ACT_LEARN handlers, but this line kept rewriting them
    // to ACT_BAG first, so both stayed dead -- including the Autoloot
    // tab's "disenchant instead of vendor" toggle (ALDE), whose entire
    // effect is to swap rule action 1 for action 5 client-side.
    if (action == ACT_DISENCHANT && !CanDisenchant(player, proto))
        action = ACT_BAG;
    if (action == ACT_BAG || action == ACT_HOLD)
        return;

    ObjectGuid playerGuid = player->GetGUID();
    ObjectGuid itemGuid = item->GetGUID();
    uint32 const itemId = proto->ItemId;
    uint32 const count = item->GetCount();
    uint32 const sellPrice = proto->SellPrice;
    player->m_Events.AddEventAtOffset([playerGuid, itemGuid, action, itemId, count, sellPrice]()
    {
        ApplyLootRuleAction(playerGuid, itemGuid, action, itemId, count, sellPrice);
    }, std::chrono::milliseconds(1));
}

void SendVaultAndRuleSync(Player* player)
{
    if (!player || !player->GetSession())
        return;
    uint32 const accountId = player->GetSession()->GetAccountId();
    LoadVault(accountId);
    LoadRules(accountId);
    for (auto const& [key, count] : g_vaults[accountId])
    {
        if (!count)
            continue;
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(key.itemEntry);
        SendLine(player, Acore::StringFormat("VLT|{}|{}|{}|{}",
            uint32(key.kind), key.itemEntry, count, proto ? proto->Name1 : "Item"));
    }
    uint32 idx = 0;
    for (LgRule const& rule : g_rules[accountId])
    {
        std::string text = rule.text;
        for (char& c : text) if (c == '|') c = ' ';
        SendLine(player, Acore::StringFormat("RULE|{}|{}|{}|{}|{}|{}",
            idx, uint32(rule.match), uint32(rule.action), uint32(rule.negate), uint32(rule.quality), text));
        ++idx;
    }
}

// Called from a small core patch in Spell::EffectPickPocket (SpellEffects.cpp),
// which normally just does player->SendLoot(target, LOOT_PICKPOCKETING) --
// this replaces that with an autoloot grant when enabled. Replicates
// Player::SendLoot's own LOOT_PICKPOCKETING fill logic (CanGeneratePickPocketLoot/
// SetPickPocketLootTime/FillLoot/gold formula) exactly, since bypassing SendLoot
// means that fill never happens otherwise. Returns false (caller falls back to
// the normal SendLoot window) if autoloot is off or the creature was already
// pickpocketed -- SendLoot's own "already pickpocketed" error message is worth
// keeping for that case rather than silently doing nothing.
bool TryAutolootPickpocket(Player* player, Unit* target)
{
    if (!player || !target || !player->GetSession() || !player->HasSpell(SPELL_AUTOLOOT)
        || !AutolootOn(player->GetSession()->GetAccountId()))
        return false;
    Creature* creature = target->ToCreature();
    if (!creature || !creature->IsAlive() || player->IsFriendlyTo(creature))
        return false;
    if (!creature->CanGeneratePickPocketLoot())
        return false;
    creature->SetPickPocketLootTime();
    Loot* loot = &creature->loot;
    loot->clear();
    if (uint32 const lootid = creature->GetCreatureTemplate()->pickpocketLootId)
        loot->FillLoot(lootid, LootTemplates_Pickpocketing, player, true);
    uint32 const a = urand(0, creature->GetLevel() / 2);
    uint32 const b = urand(0, player->GetLevel() / 2);
    loot->gold = uint32(10 * (a + b) * sWorld->getRate(RATE_DROP_MONEY));

    // Spell::EffectPickPocket is still mid-execution on the caster when this
    // runs -- same reentrancy hazard as chest autoloot (see LivingGear_Gather.cpp
    // TryAutolootChest), same fix: defer the actual grant one tick.
    ObjectGuid const playerGuid = player->GetGUID();
    ObjectGuid const creatureGuid = creature->GetGUID();
    player->m_Events.AddEventAtOffset([playerGuid, creatureGuid]()
    {
        Player* p = ObjectAccessor::FindPlayer(playerGuid);
        if (!p || !p->IsInWorld() || !p->GetMap())
            return;
        Creature* c = p->GetMap()->GetCreature(creatureGuid);
        if (!c)
            return;
        Loot* l = &c->loot;
        uint32 const maxSlot = l->GetMaxSlotInLootFor(p);
        for (uint32 slot = 0; slot < maxSlot; ++slot)
        {
            InventoryResult msg = EQUIP_ERR_OK;
            p->StoreLootItem(uint8(slot), l, msg);
        }
        if (l->gold)
        {
            p->ModifyMoney(int32(l->gold));
            l->gold = 0;
        }
    }, std::chrono::milliseconds(1));
    return true;
}

// Skips the manual loot window entirely on a kill: stores every lootable
// item straight into the player's bags via the same StoreLootItem() path
// normal looting uses, and takes any gold. Deliberately does NOT special-
// case quest items or reagents here -- StoreLootItem() puts them in bags
// like anything else, and OnPlayerStoreNewItem (ApplyLootRule, above) fires
// right after and redirects them to the vault/vendor/etc from there. That
// keeps this function from needing to touch Loot's internal quest-item/
// free-for-all slot bookkeeping directly, which is easy to get subtly wrong.
void AutolootCreatureKill(Player* player, Creature* creature)
{
    if (!player || !creature || !player->IsAlive() || !player->GetSession() || !player->HasSpell(SPELL_AUTOLOOT)
        || !AutolootOn(player->GetSession()->GetAccountId()))
        return;
    if (!creature->HasDynamicFlag(UNIT_DYNFLAG_LOOTABLE))
        return;
    if (creature->loot.loot_type == LOOT_SKINNING || creature->loot.loot_type == LOOT_PICKPOCKETING)
        return;
    if (Group* group = creature->GetLootRecipientGroup())
    {
        if (player->GetGroup() != group)
            return;
    }
    else if (creature->GetLootRecipient() != player)
        return;

    Loot* loot = &creature->loot;
    loot->FillNotNormalLootFor(player);
    uint32 const maxSlot = loot->GetMaxSlotInLootFor(player);
    for (uint32 slot = 0; slot < maxSlot; ++slot)
    {
        InventoryResult msg = EQUIP_ERR_OK;
        player->StoreLootItem(uint8(slot), loot, msg);
    }
    if (loot->gold)
    {
        player->ModifyMoney(int32(loot->gold));
        loot->gold = 0;
    }
    if (loot->isLooted())
    {
        creature->RemoveDynamicFlag(UNIT_DYNFLAG_LOOTABLE);
        // The engine only ever sets UNIT_FLAG_SKINNABLE from inside
        // AllLootRemovedFromCorpse() (see Creature.cpp) -- normal looting
        // via the client loot window triggers this automatically, but
        // autoloot bypasses that window entirely, so without this call
        // AutolootSkinKill's very first check (HasUnitFlag(UNIT_FLAG_SKINNABLE))
        // always failed and auto-skinning silently never ran at all.
        if (!creature->IsAlive())
            creature->AllLootRemovedFromCorpse();
        // Autoloot skips the loot window entirely, so corpses never got the
        // normal "all loot removed" decay trigger and just sat there
        // forever. Kept deliberately non-instant (not just despawning right
        // away) so quest items that need to interact with a corpse still
        // have a window to work -- 30s, requested explicitly, overriding
        // whatever decay-rate timer AllLootRemovedFromCorpse() just set. If
        // AutolootSkinKill also runs on this creature and finishes after
        // this, its own DespawnOrUnsummon call further down simply
        // reschedules the same 30s-from-then, which is what we want (30s
        // from the *last* thing that happened to the corpse, not the first).
        if (!creature->IsAlive())
            creature->DespawnOrUnsummon(std::chrono::seconds(30));
    }
}

// Auto-skins a freshly-killed, still-skinnable corpse if the player has
// enough of the required gathering skill, using the same skill-check math
// AzerothCore's own skinning-loot handler uses (see LootMgr/SmartAI usage
// of GetRequiredLootSkill/UpdateGatherSkill elsewhere in core code) so the
// skill-up chance and requirement match normal (manual) skinning exactly.
void AutolootSkinKill(Player* player, Creature* creature)
{
    if (!player || !creature || !player->GetSession() || !player->HasSpell(SPELL_AUTOLOOT)
        || !AutolootOn(player->GetSession()->GetAccountId()))
        return;
    if (!creature->HasUnitFlag(UNIT_FLAG_SKINNABLE))
        return;
    CreatureTemplate const* proto = creature->GetCreatureTemplate();
    if (!proto || !proto->SkinLootId || !LootTemplates_Skinning.HaveLootFor(proto->SkinLootId))
        return;
    SkillType const skill = proto->GetRequiredLootSkill();
    int32 const skillValue = int32(player->GetSkillValue(skill));
    if (skillValue <= 0)
        return;
    int32 const targetLevel = int32(creature->GetLevel());
    int32 const canSkinReq = skillValue < 100 ? (targetLevel - 10) * 10 : targetLevel * 5;
    if (canSkinReq > skillValue)
        return;

    creature->RemoveUnitFlag(UNIT_FLAG_SKINNABLE);
    creature->loot.clear();
    creature->loot.FillLoot(proto->SkinLootId, LootTemplates_Skinning, player, true);
    creature->loot.loot_type = LOOT_SKINNING;
    creature->SetDynamicFlag(UNIT_DYNFLAG_LOOTABLE);

    Loot* loot = &creature->loot;
    uint32 const maxSlot = loot->GetMaxSlotInLootFor(player);
    for (uint32 slot = 0; slot < maxSlot; ++slot)
    {
        InventoryResult msg = EQUIP_ERR_OK;
        player->StoreLootItem(uint8(slot), loot, msg);
    }
    if (loot->isLooted())
    {
        creature->RemoveDynamicFlag(UNIT_DYNFLAG_LOOTABLE);
        creature->DespawnOrUnsummon(std::chrono::seconds(30));
    }

    int32 const reqValue = targetLevel < 10 ? 0 : targetLevel < 20 ? (targetLevel - 10) * 10 : targetLevel * 5;
    if (uint32 const pure = player->GetPureSkillValue(skill))
        player->UpdateGatherSkill(skill, pure, uint32(reqValue), creature->isElite() ? 2 : 1);
}

void DepositAll(Player* player)
{
    if (!player || !player->GetSession() || !player->HasSpell(SPELL_AUTOLOOT))
        return;
    uint32 const accountId = player->GetSession()->GetAccountId();
    std::vector<Item*> toDeposit;
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            if (IsReagentItem(item->GetTemplate(), player))
                toDeposit.push_back(item);
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        Bag* container = player->GetBagByPos(bag);
        if (!container)
            continue;
        for (uint8 slot = 0; slot < container->GetBagSize(); ++slot)
            if (Item* item = container->GetItemByPos(slot))
                if (IsReagentItem(item->GetTemplate(), player))
                    toDeposit.push_back(item);
    }
    for (Item* item : toDeposit)
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (!proto)
            continue;
        uint32 const count = item->GetCount();
        VaultAdd(accountId, 0, VAULT_REAGENT, proto->ItemId, count);
        player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
    }
    SendVaultAndRuleSync(player);
}

// Withdraw one entry from the vault back into the bags. The client has
// always sent "TAKE|<kind>|<entry>" from a vault row's OnClick
// (LivingGear.lua) and nothing has ever parsed it, in any revision -- so
// clicking a row was a silent no-op and the reagent bank was a one-way
// trip. One click takes at most a single stack so a 900-count entry can't
// try to materialise 900 items at once, and anything that doesn't fit
// goes straight back into the vault rather than evaporating.
void VaultWithdraw(Player* player, uint8 kind, uint32 itemEntry)
{
    if (!player || !player->GetSession())
        return;
    if (kind != VAULT_QUEST && kind != VAULT_REAGENT)
        return;
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemEntry);
    if (!proto)
        return;
    uint32 const accountId = player->GetSession()->GetAccountId();
    // The reagent vault is account-wide (owner 0); the retired quest vault
    // was per-character, and its leftovers are still withdrawable.
    uint32 const ownerGuid = kind == VAULT_QUEST ? player->GetGUID().GetCounter() : 0;
    uint32 const stored = VaultCount(accountId, ownerGuid, kind, itemEntry);
    if (!stored)
        return;

    uint32 const want = std::min<uint32>(stored, std::max<uint32>(proto->GetMaxStackSize(), 1));
    uint32 const taken = VaultRemove(accountId, ownerGuid, kind, itemEntry, want);
    if (!taken)
        return;
    uint32 const given = StoreFromVault(player, itemEntry, taken);
    if (given < taken)
        VaultAdd(accountId, ownerGuid, kind, itemEntry, taken - given);
    if (!given)
    {
        ChatHandler(player->GetSession()).SendSysMessage("[Vault] No room in your bags.");
        return;
    }
    SendLine(player, Acore::StringFormat("VLT|{}|{}|{}|{}", uint32(kind), itemEntry,
        VaultCount(accountId, ownerGuid, kind, itemEntry), proto->Name1));
}

// The quest vault was dropped on 2026-08-21 but its rows were left behind
// -- no panel, no withdraw path, and (until the ACT_HOLD fix above) more
// items still being filed into it. Hand them back the next time their
// owner logs in, keeping whatever doesn't fit banked for the login after
// that rather than dropping it on the floor.
void DrainLegacyQuestVault(Player* player)
{
    if (!player || !player->GetSession())
        return;
    uint32 const accountId = player->GetSession()->GetAccountId();
    uint32 const guid = player->GetGUID().GetCounter();
    QueryResult rows = CharacterDatabase.Query(
        "SELECT `item_entry`, `item_count` FROM `lg_vault` "
        "WHERE `account_id` = {} AND `owner_guid` = {} AND `kind` = {} AND `item_count` > 0",
        accountId, guid, uint32(VAULT_QUEST));
    if (!rows)
        return;
    uint32 returned = 0;
    do
    {
        uint32 const entry = (*rows)[0].Get<uint32>();
        uint32 const count = (*rows)[1].Get<uint32>();
        // Remove first, then store, then put back what did not fit --
        // storing first would hand out the items before the vault row was
        // decremented, which is a dupe if anything in between fails.
        uint32 const taken = VaultRemove(accountId, guid, VAULT_QUEST, entry, count);
        if (!taken)
            continue;
        uint32 const given = StoreFromVault(player, entry, taken);
        if (given < taken)
            VaultAdd(accountId, guid, VAULT_QUEST, entry, taken - given);
        returned += given;
    } while (rows->NextRow());
    if (returned)
        ChatHandler(player->GetSession()).PSendSysMessage(
            "[Vault] Returned {} item(s) that were stuck in the retired quest vault.", returned);
}

// How many crafts' worth of reagents to materialise when a recipe is
// selected. One would technically unblock the Create button, but the
// client would then show "1" craftable forever, which reads as broken.
uint32 const CRAFT_PREP_BATCH = 10;

// The 3.3.5 tradeskill window computes "how many can I make" purely from
// bag contents, so a recipe whose reagents live in the reagent vault reads
// as 0 and Create stays greyed out. The server-side top-up already wired
// into Spell::CheckItems (TopUpReagentFromVault, via a core patch) only
// ever runs on a cast -- and the client refuses to send that cast, so it
// never got a chance to help. No amount of server-side work fixes a button
// the client will not enable, which is why this is addon-driven: the addon
// reports the selected recipe (CRAFTPREP|<spellId>) and we put the
// reagents somewhere the client can actually see them.
void PrepareCraftReagents(Player* player, uint32 spellId)
{
    if (!player || !player->GetSession() || !spellId)
        return;
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
    // Only ever act on a recipe this character actually knows -- an addon
    // whisper is not a trusted boundary, and without this any spell id
    // could be used to pull arbitrary reagents out of the vault.
    if (!info || !player->HasSpell(spellId))
        return;
    for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (info->Reagent[i] <= 0 || !info->ReagentCount[i])
            continue;
        TopUpReagentFromVault(player, uint32(info->Reagent[i]),
            info->ReagentCount[i] * CRAFT_PREP_BATCH);
    }
    SendVaultAndRuleSync(player);
}

bool HandleVaultChat(Player* player, std::string msg)
{
    // The "is this actually our own outgoing sync line coming back around"
    // guard used to live here as a file-local flag. It is now
    // LivingGear_IsAddonSendInProgress(), checked once by the dispatcher in
    // LivingGear.cpp before any module sees the message, so every module
    // gets the same protection instead of only this one.
    if (msg.rfind("LG\t", 0) == 0)
        msg = msg.substr(3);
    if (!player || !player->GetSession())
        return false;
    uint32 const accountId = player->GetSession()->GetAccountId();

    if (msg == "DEPOSITALL")
    {
        DepositAll(player);
        return true;
    }
    uint32 craftSpell = 0;
    if (sscanf(msg.c_str(), "CRAFTPREP|%u", &craftSpell) == 1)
    {
        PrepareCraftReagents(player, craftSpell);
        return true;
    }
    uint32 takeKind = 0;
    uint32 takeEntry = 0;
    if (sscanf(msg.c_str(), "TAKE|%u|%u", &takeKind, &takeEntry) == 2)
    {
        VaultWithdraw(player, uint8(takeKind), takeEntry);
        return true;
    }
    if (msg == "RULECLR" || msg == "RULERESET")
    {
        LoadRules(accountId);
        g_rules[accountId].clear();
        CharacterDatabase.DirectExecute("DELETE FROM `lg_autoloot_rule` WHERE `account_id` = {}", accountId);
        SendVaultAndRuleSync(player); // client was never told the rules cleared -- button looked dead
        return true;
    }
    uint32 onOff = 0;
    if (sscanf(msg.c_str(), "ALSET|%u", &onOff) == 1)
    {
        LoadAutolootPrefs(accountId);
        g_autolootOn[accountId] = onOff != 0;
        CharacterDatabase.DirectExecute(
            "INSERT INTO `lg_account_meta` (`account_id`, `autoloot_on`) VALUES ({}, {}) "
            "ON DUPLICATE KEY UPDATE `autoloot_on` = {}",
            accountId, onOff ? 1 : 0, onOff ? 1 : 0);
        SendAutolootSync(player);
        return true;
    }
    if (sscanf(msg.c_str(), "ALDE|%u", &onOff) == 1)
    {
        LoadAutolootPrefs(accountId);
        g_autolootDe[accountId] = onOff != 0;
        CharacterDatabase.DirectExecute(
            "INSERT INTO `lg_account_meta` (`account_id`, `autoloot_de`) VALUES ({}, {}) "
            "ON DUPLICATE KEY UPDATE `autoloot_de` = {}",
            accountId, onOff ? 1 : 0, onOff ? 1 : 0);
        SendAutolootSync(player);
        return true;
    }
    uint32 idx = 0;
    if (sscanf(msg.c_str(), "RULEDEL|%u", &idx) == 1)
    {
        LoadRules(accountId);
        auto& rules = g_rules[accountId];
        if (idx < rules.size())
        {
            rules.erase(rules.begin() + idx);
            CharacterDatabase.DirectExecute("DELETE FROM `lg_autoloot_rule` WHERE `account_id` = {}", accountId);
            for (uint32 i = 0; i < rules.size(); ++i)
                CharacterDatabase.DirectExecute(
                    "INSERT INTO `lg_autoloot_rule` (`account_id`, `sort_idx`, `match_type`, `action`, `negate`, `quality`, `match_text`) "
                    "VALUES ({}, {}, {}, {}, {}, {}, '{}')",
                    accountId, i, rules[i].match, rules[i].action, rules[i].negate, rules[i].quality, rules[i].text);
        }
        return true;
    }
    uint32 match = 0, action = 0, negate = 0, quality = 0;
    char text[128] = "";
    if (sscanf(msg.c_str(), "RULEADD|%u|%u|%u|%u|%127[^\n]", &match, &action, &negate, &quality, text) >= 4)
    {
        LoadRules(accountId);
        auto& rules = g_rules[accountId];
        LgRule rule;
        rule.match = uint8(match);
        rule.action = uint8(action);
        rule.negate = uint8(negate);
        rule.quality = uint8(quality);
        rule.text = text;
        rules.insert(rules.begin(), rule); // new rules take priority, matching the client's expectation of most-recent-first
        CharacterDatabase.DirectExecute("DELETE FROM `lg_autoloot_rule` WHERE `account_id` = {}", accountId);
        for (uint32 i = 0; i < rules.size(); ++i)
            CharacterDatabase.DirectExecute(
                "INSERT INTO `lg_autoloot_rule` (`account_id`, `sort_idx`, `match_type`, `action`, `negate`, `quality`, `match_text`) "
                "VALUES ({}, {}, {}, {}, {}, {}, '{}')",
                accountId, i, rules[i].match, rules[i].action, rules[i].negate, rules[i].quality, rules[i].text);
        return true;
    }
    return false;
}

class VaultPlayer : public PlayerScript
{
public:
    // No PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT here on purpose: every
    // Living Gear addon command is routed through the single dispatcher in
    // LivingGear.cpp (DispatchAddonCommand). See the comment there for why
    // five separate copies of this hook was a trap.
    VaultPlayer() : PlayerScript("LivingGearVaultPlayer", {
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_STORE_NEW_ITEM,
        PLAYERHOOK_ON_CREATURE_KILL
    }) { }

    // *Autoloot (910008) is "on by default" -- there's no unlock condition,
    // every account should just have it. It only ever silently no-ops if
    // sSpellMgr has no spell_dbc row for it (see rev_living_gear_autoloot_dbc.sql).
    void OnPlayerLogin(Player* player) override
    {
        if (!player || !player->GetSession())
            return;
        uint32 const accountId = player->GetSession()->GetAccountId();
        // The missing-spell_dbc check used to guard this whole function, so
        // one absent row took the account key ring and the quest-vault
        // drain down with it too. Scope it to the thing it is actually
        // about.
        if (sSpellMgr->GetSpellInfo(SPELL_AUTOLOOT))
        {
            if (!player->HasSpell(SPELL_AUTOLOOT))
                player->learnSpell(SPELL_AUTOLOOT);
            // Also persist/sync it the same way UnlockPerk() elsewhere does,
            // so the addon's db.perks[910008] (PerkKnown()) actually goes
            // true -- that's what gates the Autoloot toggle being shown.
            CharacterDatabase.DirectExecute(
                "INSERT IGNORE INTO `lg_account_perk` (`account_id`, `spell_id`) VALUES ({}, {})",
                accountId, SPELL_AUTOLOOT);
            SendLine(player, Acore::StringFormat("PK|{}|1", SPELL_AUTOLOOT));
        }
        else
        {
            static bool warnedAutoloot = false;
            if (!warnedAutoloot)
            {
                warnedAutoloot = true;
                LOG_ERROR("module.livinggear",
                    "Living Gear: *Autoloot ({}) has no spell_dbc row -- the entire autoloot "
                    "feature is inert. See rev_living_gear_autoloot_dbc.sql.", SPELL_AUTOLOOT);
            }
        }
        SendAutolootSync(player);
        GrantAccountKeys(player);
        DrainLegacyQuestVault(player);
    }

    void OnPlayerStoreNewItem(Player* player, Item* item, uint32 /*count*/) override
    {
        if (player && player->GetSession() && item)
            if (ItemTemplate const* proto = item->GetTemplate())
                if (IsKeyItem(proto))
                    RecordAccountKey(player->GetSession()->GetAccountId(), proto->ItemId);
        ApplyLootRule(player, item);
    }

    void OnPlayerCreatureKill(Player* killer, Creature* killed) override
    {
        // OnPlayerCreatureKill can fire from deep inside an active spell's
        // own call stack -- e.g. a damage effect that lands a killing blow
        // triggers Unit::Kill synchronously from within Spell::DoAllEffectOnTarget,
        // still nested inside that Spell object's cast(). Running
        // StoreLootItem (which sends packets and touches a lot of player/
        // item state) reentrantly from in there corrupted memory and
        // crashed the server (SIGSEGV in Player::SendNewItem, 2026-08-20,
        // reached via ChainAmbush's Ambush hit killing its target). Defer
        // to the next tick, same fix as ChainAmbush/Avenger's Shield.
        if (!killer || !killed)
            return;
        ObjectGuid killerGuid = killer->GetGUID();
        ObjectGuid killedGuid = killed->GetGUID();
        killer->m_Events.AddEventAtOffset([killerGuid, killedGuid]()
        {
            Player* player = ObjectAccessor::FindPlayer(killerGuid);
            if (!player || !player->IsInWorld())
                return;
            Creature* creature = ObjectAccessor::GetCreature(*player, killedGuid);
            if (!creature)
                return;
            AutolootCreatureKill(player, creature);
            AutolootSkinKill(player, creature);
        }, std::chrono::milliseconds(1));
    }
};

class VaultWorld : public WorldScript
{
public:
    VaultWorld() : WorldScript("LivingGearVaultWorld", { WORLDHOOK_ON_STARTUP }) { }

    void OnStartup() override
    {
        BuildQuestItemIndex();
    }
};

} // namespace LivingGearVault

void SendVaultAndRuleSync(Player* player)
{
    LivingGearVault::SendVaultAndRuleSync(player);
}

void SendAutolootSync(Player* player)
{
    LivingGearVault::SendAutolootSync(player);
}

bool IsAutolootEnabled(Player* player)
{
    if (!player || !player->GetSession() || !player->HasSpell(LivingGearVault::SPELL_AUTOLOOT))
        return false;
    return LivingGearVault::AutolootOn(player->GetSession()->GetAccountId());
}

// Called from a core patch in Spell::EffectPickPocket (SpellEffects.cpp).
bool LivingGear_TryAutolootPickpocket(Player* player, Unit* target)
{
    return LivingGearVault::TryAutolootPickpocket(player, target);
}

// Called from a core patch in Spell::CheckCast (Spell.cpp).
void LivingGear_TopUpReagentFromVault(Player* player, uint32 itemId, uint32 needed)
{
    LivingGearVault::TopUpReagentFromVault(player, itemId, needed);
}

// Addon-command entry point, called by the dispatcher in LivingGear.cpp.
bool LivingGear_HandleVaultCommand(Player* player, std::string const& msg)
{
    return LivingGearVault::HandleVaultChat(player, msg);
}

void AddSC_LivingGearVault()
{
    new LivingGearVault::VaultPlayer();
    new LivingGearVault::VaultWorld();
}
