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
#include "DBCStores.h"
#include "Group.h"
#include "Item.h"
#include "SpellInfo.h"
#include "WorldSessionMgr.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
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
uint32 LivingGear_AccountLockpickSkill(Player* player); // LivingGear_Gather.cpp
void LivingGear_SendAddonLine(Player* player, std::string const& line); // LivingGear.cpp
bool LivingGear_IsAddonSendInProgress(); // LivingGear.cpp

// Canonical account-aware perk check (LivingGear_Perks.cpp). Autoloot was
// gated on player->HasSpell, which is a per-character fact, while the perk
// itself is owned by the account -- so an alt that never learned the spell
// had autoloot silently switched off. Four characters were in that state.
bool LivingGear_HasPerk(Player* player, uint32 spellId);
void LivingGear_RefundIfPurchased(Player* player, uint32 spellId); // LivingGear_Perks.cpp

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
//
// ACT_DESTROY (7) added 2026-08-22 for bug report #18 ("auto delete <item>").
// ACT_SKIP already destroyed the item, but "Skip" reads as "leave it alone",
// so nobody would ever have guessed it was the destroy action. Rather than
// renumber a wire format, the honest label gets its own value.
enum LgAction : uint8
{
    ACT_BAG = 0, ACT_VENDOR = 1, ACT_HOLD = 2, ACT_REAGENT_VAULT = 3,
    ACT_SKIP = 4, ACT_DISENCHANT = 5, ACT_LEARN = 6, ACT_DESTROY = 7,
    // Report #182: currency tokens pool to the ACCOUNT, not the character.
    // The action value rides the autoloot wire format, so it gets the next
    // free slot rather than renumbering anything the addon knows.
    ACT_ACCOUNT_CURRENCY = 8
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

uint32 VaultCount(uint32 accountId, uint32 ownerGuid, uint8 kind, uint32 itemEntry);
void BroadcastVaultLine(uint32 accountId, uint32 itemEntry);

void SendLine(Player* player, std::string const& line)
{
    ::LivingGear_SendAddonLine(player, line);
}

// Broadcast one VLT| line to every online character of the account.
//
// Bug report #178 (recurring; also #137): the reagent vault is account-wide,
// but every VLT| sync line was sent only to the player who caused the
// change. A second character on the same account (a playerbot crafting
// First Aid, say) can drain the vault while someone else is online, and
// that client's db.vault keeps the pre-craft counts -- the profession
// window keeps saying "enough to make 124" while the server truthfully
// answers "missing reagent: runecloth" on every cast. The counts only
// corrected themselves on relog.
//
// Only the reagent vault is broadcast: it is the kind with owner 0 that
// every character of the account shares. The quest vault is per-owner, and
// its callers already know exactly which player to update.
void BroadcastVaultLine(uint32 accountId, uint32 itemEntry)
{
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemEntry);
    if (!proto)
        return;
    std::string const line = Acore::StringFormat("VLT|{}|{}|{}|{}",
        uint32(VAULT_REAGENT), itemEntry, VaultCount(accountId, 0, VAULT_REAGENT, itemEntry), proto->Name1);
    for (auto const& [guid, session] : sWorldSessionMgr->GetAllSessions())
        if (session && session->GetPlayer() && session->GetAccountId() == accountId)
            SendLine(session->GetPlayer(), line);
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
    if (kind == VAULT_REAGENT && ownerGuid == 0)
        BroadcastVaultLine(accountId, itemEntry);
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
    if (kind == VAULT_REAGENT && ownerGuid == 0)
        BroadcastVaultLine(accountId, itemEntry);
    return take;
}

// Set while a vault withdraw is materialising items into the player's bags.
//
// Player::StoreNewItem() fires OnPlayerStoreNewItem, which is the very hook
// the autoloot rule engine runs on (ApplyLootRule) -- and a reagent coming
// OUT of the reagent vault matches the same rule that put it in there, so
// every withdraw was immediately re-deposited a tick later. Clicking a
// Reagents row appeared to do nothing; DEPOSITALL looked like the only
// direction that worked. This flag is the "these items were deliberately
// handed to the player, do not re-file them" signal, checked once in
// ApplyLootRule.
//
// A depth counter rather than a bool: materialising a withdraw reaches
// ApplyLootRule, which routes further items, and any of those paths may
// already be holding the flag.
uint32 g_vaultGrantDepth = 0;

// -------------------------------------------------------------------------
// Shared account currency pool (report #182). One balance per
// (account, currency item) across every character -- the honor-style model
// the user asked for. Currency tokens route here on loot; vendor purchases
// that cost those tokens pay from the pool first (core-patch 0049 hooks in
// Player.cpp via the two LivingGear_AccountCurrency* callbacks below).
// -------------------------------------------------------------------------

struct AccountCurrencyState
{
    std::unordered_map<uint32, int64> balances; // item id -> pooled count
};
std::unordered_map<uint32, AccountCurrencyState> g_accountCurrency;

void LoadAccountCurrency(uint32 accountId)
{
    if (g_accountCurrency.find(accountId) != g_accountCurrency.end())
        return;
    AccountCurrencyState& st = g_accountCurrency[accountId];
    QueryResult result = CharacterDatabase.Query(
        "SELECT `item_id`, `count` FROM `lg_account_currency` WHERE `account_id` = {}", accountId);
    if (result)
    {
        do
            st.balances[(*result)[0].Get<uint32>()] += (*result)[1].Get<int64>();
        while (result->NextRow());
    }
}

int64 AccountCurrencyBalance(uint32 accountId, uint32 itemId)
{
    LoadAccountCurrency(accountId);
    auto it = g_accountCurrency[accountId].balances.find(itemId);
    return it == g_accountCurrency[accountId].balances.end() ? 0 : it->second;
}

void AccountCurrencyAdd(uint32 accountId, uint32 itemId, int64 count)
{
    if (!accountId || !itemId || !count)
        return;
    LoadAccountCurrency(accountId);
    g_accountCurrency[accountId].balances[itemId] += count;
    CharacterDatabase.DirectExecute(
        "INSERT INTO `lg_account_currency` (`account_id`, `item_id`, `count`) VALUES ({}, {}, {}) "
        "ON DUPLICATE KEY UPDATE `count` = `count` + ({})",
        accountId, itemId, count, count);
}

void AccountCurrencyAddChat(Player* player, uint32 itemId, int64 count);

// Remove up to `count` from the pool (bags must have been drained first by
// the caller). Returns how much was actually removed.
int64 AccountCurrencyRemove(Player* player, uint32 accountId, uint32 itemId, int64 count)
{
    if (!accountId || !itemId || count <= 0)
        return 0;
    LoadAccountCurrency(accountId);
    auto& st = g_accountCurrency[accountId];
    auto it = st.balances.find(itemId);
    if (it == st.balances.end() || it->second <= 0)
        return 0;
    int64 const take = std::min<int64>(count, it->second);
    it->second -= take;
    CharacterDatabase.DirectExecute(
        "UPDATE `lg_account_currency` SET `count` = `count` - {} WHERE `account_id` = {} AND `item_id` = {}",
        take, accountId, itemId);
    if (player)
        AccountCurrencyAddChat(player, itemId, -take);
    return take;
}

// Tell the addon the pool balance changed: CUR|itemId|balanceDelta|poolTotal
void AccountCurrencyAddChat(Player* player, uint32 itemId, int64 count)
{
    if (!player || !player->GetSession() || !itemId)
        return;
    uint32 const acc = player->GetSession()->GetAccountId();
    SendLine(player, Acore::StringFormat("CUR|{}|{}|{}",
        itemId, count, AccountCurrencyBalance(acc, itemId)));
}

// Core callbacks (core-patch 0049, Player.cpp extended-cost path).
bool LivingGear_AccountCurrencyCovers(Player* player, uint32 itemId, uint32 count)
{
    if (!player || !player->GetSession() || !itemId)
        return false;
    return AccountCurrencyBalance(player->GetSession()->GetAccountId(), itemId) >= int64(count);
}

void LivingGear_AccountCurrencyPay(Player* player, uint32 itemId, uint32 count)
{
    if (!player || !player->GetSession() || !itemId || !count)
        return;
    uint32 const acc = player->GetSession()->GetAccountId();
    // Bags first (DestroyItemCount), pool for the remainder.
    uint32 const inBags = player->GetItemCount(itemId, true);
    uint32 const fromBags = std::min(count, inBags);
    if (fromBags)
        player->DestroyItemCount(itemId, fromBags, true);
    if (int64 const rest = int64(count) - int64(fromBags))
    {
        int64 const removed = AccountCurrencyRemove(player, acc, itemId, rest);
        if (removed < rest)
            LOG_WARN("module.livinggear",
                "account currency: account {} paid {} of {} x{} -- pool short by {} (validation missed?)",
                acc, removed, itemId, count, rest - removed);
    }
    else
        AccountCurrencyAddChat(player, itemId, 0); // balance changed only if pool used; bags-only pays silently
}

struct VaultGrantScope
{
    VaultGrantScope() { ++g_vaultGrantDepth; }
    ~VaultGrantScope() { --g_vaultGrantDepth; }
};

// Player::AddItem() silently drops whatever doesn't fit and still returns
// true, so anything pulling out of the vault has to do the store by hand
// to learn how much actually landed -- otherwise the overflow is removed
// from the vault and then simply vanishes. Returns the count stored.
// `stored` optionally hands back the item that was created, so a caller that
// needs to do something to it (VaultWithdraw picking a lockbox open) does not
// have to go hunting through the bags for it afterwards.
uint32 StoreFromVault(Player* player, uint32 itemEntry, uint32 count, Item** stored = nullptr)
{
    if (stored)
        *stored = nullptr;
    if (!player || !count)
        return 0;
    ItemPosCountVec dest;
    uint32 noSpace = 0;
    player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemEntry, count, &noSpace);
    uint32 const fits = count > noSpace ? count - noSpace : 0;
    if (!fits || dest.empty())
        return 0;
    VaultGrantScope const guard;
    Item* item = player->StoreNewItem(dest, itemEntry, true);
    if (!item)
        return 0;
    player->SendNewItem(item, fits, true, false);
    if (stored)
        *stored = item;
    return fits;
}

// Applies the account's Lockpicking to a locked item on the spot: no cast, no
// channel, no finding a Rogue. Returns true if the item came away unlocked.
//
// This is the withdraw-side half of routing junkboxes into the reagent vault.
// Deliberately the same three things Spell::EffectOpenLock does on a
// successful manual pick -- set ITEM_FIELD_FLAG_UNLOCKED, mark the item
// changed so the unlock is saved, and take the skill-up roll -- so a box
// opened this way is worth exactly as much skill as one opened by hand, no
// more and no less.
//
// Skill is read account-wide, matching core-patch 0005: any character can
// open what the account's best lockpicker could.
bool InstantPickLock(Player* player, Item* item)
{
    if (!player || !item || !item->IsLocked())
        return false;
    ItemTemplate const* proto = item->GetTemplate();
    if (!proto || !proto->LockID)
        return false;
    LockEntry const* lockInfo = sLockStore.LookupEntry(proto->LockID);
    if (!lockInfo)
        return false;

    for (int j = 0; j < MAX_LOCK_CASE; ++j)
    {
        if (lockInfo->Type[j] != LOCK_KEY_SKILL)
            continue;
        if (SkillByLockType(LockType(lockInfo->Index[j])) != SKILL_LOCKPICKING)
            continue;

        uint32 const required = lockInfo->Skill[j];
        if (LivingGear_AccountLockpickSkill(player) < required)
            return false; // not skilled enough yet -- it stays locked, as it would in hand

        item->SetFlag(ITEM_FIELD_FLAGS, ITEM_FIELD_FLAG_UNLOCKED);
        item->SetState(ITEM_CHANGED, player);
        if (uint32 const pure = player->GetPureSkillValue(SKILL_LOCKPICKING))
            player->UpdateGatherSkill(SKILL_LOCKPICKING, pure, required);
        return true;
    }
    return false;
}

// Called from a core patch in Spell::CheckItems (Spell.cpp), right before the
// engine's own reagent-count check -- reagents that got auto-banked
// (ACT_REAGENT_VAULT) need to still "count as in your backpack" for crafting
// per user request, not require a manual withdraw first.
//
// This ANSWERS the check in place. The old answer moved items: core-patch
// 0011's first cut withdrew the shortfall into the bag right before
// HasItemCount ran, so every craft -- and every craft whose later checks
// failed, and every craft-all batch that was cancelled halfway -- shovelled
// materials out of the reagent bank and into the backpack. The bank was
// behaving as a queue into the bags instead of the store it is. The tool
// checks already answered in place (core-patch 0022); this is the same shape
// for Reagent[]. Consumption is handled separately by
// ConsumeReagentBagThenVault, called from Spell::TakeReagents.
bool VaultCoversReagent(Player* player, uint32 itemId, uint32 needed)
{
    if (!player || !player->GetSession() || !needed)
        return false;
    uint32 const have = player->GetItemCount(itemId, true); // bags + bank
    if (have >= needed)
        return true;
    uint32 const accountId = player->GetSession()->GetAccountId();
    LoadVault(accountId);
    return have + VaultCount(accountId, 0, VAULT_REAGENT, itemId) >= needed;
}

// Called from a core patch in Spell::TakeReagents (Spell.cpp), where the
// engine consumes spell reagents. The craft gate above now accepts vault
// stock, so consumption has to reach the vault too: pay from the bags and
// bank first (normal DestroyItemCount bookkeeping), then burn the shortfall
// straight out of the reagent vault. Nothing passes through the bag on its
// way to being consumed -- the player asked for banked materials to stay
// banked, tools and materials alike.
void ConsumeReagentBagThenVault(Player* player, uint32 itemId, uint32 count)
{
    if (!player || !count)
        return;
    uint32 const have = player->GetItemCount(itemId, true);
    uint32 const fromBag = std::min(have, count);
    if (fromBag)
        player->DestroyItemCount(itemId, fromBag, true);
    uint32 const fromVault = count - fromBag;
    if (!fromVault || !player->GetSession())
        return;
    uint32 const accountId = player->GetSession()->GetAccountId();
    VaultRemove(accountId, 0, VAULT_REAGENT, itemId, fromVault);

    // Tell the client the vault got smaller. Reported 2026-08-24: cooking from
    // reagent-bank materials worked twice, then said "missing reagent: Gooey
    // Spider Leg" while the recipe still claimed three more were craftable.
    //
    // The server was right and the client was stale. The addon adds vault
    // contents to the craftable count on purpose (GetTradeSkillInfo counts bags
    // only, and the whole point of the reagent vault is that bag contents are
    // not the whole story), but VaultRemove updates the database and the
    // in-memory map and sends NOTHING -- so db.vault kept the pre-craft counts
    // and kept promising reagents that had already been spent. The manual
    // withdraw path has always sent this line; consumption never did.
    //
    // Sending it here also refreshes the Reagents panel, which had the same
    // stale-count problem after every craft.
    if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId))
        SendLine(player, Acore::StringFormat("VLT|{}|{}|{}|{}", uint32(VAULT_REAGENT), itemId,
            VaultCount(accountId, 0, VAULT_REAGENT, itemId), proto->Name1));
}

// Called from a core patch in Spell::CheckItems (Spell.cpp), from inside the
// engine's own tool checks.
//
// The other half of bug report #29. Tool requirements are NOT reagents: they
// live in SpellInfo::Totem[] (a named item) and SpellInfo::TotemCategory[] (a
// kind of tool), and are tested with HasItemCount / HasItemTotemCategory,
// both of which walk real inventory only. VaultCoversReagent covers
// Reagent[] and nothing else, so a Blacksmith Hammer sitting in the reagent
// bank left every blacksmithing recipe insisting you needed a hammer -- the
// item was banked, visible, and useless.
//
// These ANSWER the question rather than moving anything. The first cut of
// this withdrew the tool into the player's bags, which worked but quietly
// emptied the bank the player had deliberately filed it into. A tool is not
// consumed, so there is nothing to supply -- the only thing the check needs
// is a truthful "yes, you have one".
//
// Using the engine's own IsTotemCategoryCompatiableWith is what makes the
// Gnomish Army Knife work with no special case: its totem category mask is a
// superset of the individual tools', so one banked Army Knife satisfies
// blacksmithing, engineering and the rest exactly as it would in a bag.
bool VaultHasToolCategory(Player* player, uint32 totemCategory)
{
    if (!player || !player->GetSession() || !totemCategory)
        return false;
    uint32 const accountId = player->GetSession()->GetAccountId();
    LoadVault(accountId);
    for (auto const& [key, count] : g_vaults[accountId])
    {
        if (key.kind != VAULT_REAGENT || !count)
            continue;
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(key.itemEntry);
        if (proto && player->IsTotemCategoryCompatiableWith(proto, totemCategory))
            return true;
    }
    return false;
}

bool VaultHasItem(Player* player, uint32 itemId)
{
    if (!player || !player->GetSession() || !itemId)
        return false;
    return VaultCount(player->GetSession()->GetAccountId(), 0, VAULT_REAGENT, itemId) > 0;
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

// The same index the other way round: which quests each item id belongs to.
// Needed by PurgeCompletedQuestItems -- "is this item still wanted by
// anything" is only answerable if you can enumerate every quest that
// references it, and walking all ~10k quest templates per bag slot is not
// an option.
std::unordered_map<uint32, std::vector<uint32>> g_questItemQuests;

void NoteQuestItem(uint32 itemId, uint32 questId)
{
    if (!itemId)
        return;
    g_questItemIds.insert(itemId);
    std::vector<uint32>& quests = g_questItemQuests[itemId];
    if (std::find(quests.begin(), quests.end(), questId) == quests.end())
        quests.push_back(questId);
}

void BuildQuestItemIndex()
{
    g_questItemIds.clear();
    g_questItemQuests.clear();
    for (auto const& questPair : sObjectMgr->GetQuestTemplates())
    {
        Quest const* quest = questPair.second;
        if (!quest)
            continue;
        uint32 const questId = quest->GetQuestId();
        for (uint32 id : quest->RequiredItemId)
            NoteQuestItem(id, questId);
        for (uint32 id : quest->ItemDrop)
            NoteQuestItem(id, questId);
        NoteQuestItem(quest->GetSrcItemId(), questId);
    }
    LOG_INFO("server.loading", "Living Gear vault: indexed {} quest-related item ids", g_questItemIds.size());
}

// 2026-08-22, bug reports #2 and #7: this used to answer "is this item wanted
// by ANY quest in the game", which is 5147 item ids -- including Wool Cloth
// (10 quests), Runecloth (30) and Star Ruby (2). Since IsReagentItem() excludes
// quest items, that quietly barred a large slice of ordinary trade goods from
// the reagent vault for every player permanently, whether or not they had ever
// seen the quest. Both halves of the complaint -- autoloot not filing reagents,
// and the Deposit All button appearing to do nothing -- were the same cause:
// the items were being correctly identified as "not reagents".
//
// The question that actually matters is narrower: does THIS player need this
// item for a quest they are on right now. Anything else belongs in the vault.
//
// g_questItemQuests makes that cheap -- it already maps item id to the quests
// referencing it, so this is a handful of GetQuestStatus calls rather than a
// walk over ~10k quest templates.
//
// QUEST_STATUS_COMPLETE counts as "still needed": the objectives are met but
// the items must physically be in the player's bags at turn-in, so vaulting
// them there would strand the quest just as surely.
//
// Without a player (no context to judge against) it still falls back to the
// global index, which is the conservative answer.
bool IsQuestItem(ItemTemplate const* proto, Player* player = nullptr)
{
    if (!proto)
        return false;
    // An item that hands out a quest is always kept: banking it would hide the
    // quest itself.
    if (proto->StartQuest)
        return true;

    // Bug report #35, 2026-08-23: Dark Iron Residue, Dark Iron Scraps, Core of
    // Elements and Savage Frond should file into the reagent bank. All four are
    // ITEM_CLASS_QUEST, and this used to answer "yes, quest item" for that
    // class unconditionally -- before ever asking whether the player has the
    // quest. They are turn-ins for REPEATABLE quests (9129, 9132, 9137 all
    // carry SpecialFlags 1), so they accumulate indefinitely and spend most of
    // their life as inventory clutter.
    //
    // With a player to ask, the active-quest check below is the honest answer
    // for quest-class items too. Without one -- the rule-engine's static
    // queries -- keep the conservative class test.
    if (!player && (proto->Class == ITEM_CLASS_QUEST || proto->Bonding == BIND_QUEST_ITEM))
        return true;

    if (player)
    {
        auto const known = g_questItemQuests.find(proto->ItemId);
        if (known == g_questItemQuests.end())
            return false;
        for (uint32 questId : known->second)
        {
            QuestStatus const status = player->GetQuestStatus(questId);
            if (status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_COMPLETE)
                return true;
        }
        return false;
    }

    return !g_questItemIds.empty() && g_questItemIds.count(proto->ItemId) > 0;
}

// Quest leftovers: an item is only worth deleting if it is quest-only gear
// (ITEM_CLASS_QUEST or BIND_QUEST_ITEM) AND every quest that references it
// has already been turned in by this character.
//
// Both halves matter. Plenty of ordinary trade goods and consumables are
// listed as some quest's RequiredItemId -- Thick Leather, Runecloth, Refreshing
// Spring Water -- and deleting a stack of those because one obscure quest that
// wants them happens to be complete would be a disaster, so the class/bind
// test is a hard gate, not a heuristic. And an item shared between a done
// quest and an undone one (very common in quest chains and in the "collect N,
// two different NPCs want them" pattern) has to survive.
bool IsSpentQuestItem(Player* player, ItemTemplate const* proto)
{
    if (!player || !proto)
        return false;
    if (proto->Class != ITEM_CLASS_QUEST && proto->Bonding != BIND_QUEST_ITEM)
        return false;
    // An item that starts a quest is only spent once that quest is done --
    // otherwise this would eat quest-starting drops before they're used.
    if (proto->StartQuest && !player->IsQuestRewarded(proto->StartQuest))
        return false;

    auto const it = g_questItemQuests.find(proto->ItemId);
    if (it == g_questItemQuests.end() || it->second.empty())
        return false; // referenced by no quest at all -- not ours to judge
    for (uint32 questId : it->second)
    {
        if (!player->IsQuestRewarded(questId))
            return false;
        // "Rewarded" is not "done" for a quest you can hand in again.
        // Dailies, weeklies and plain repeatables are rewarded and available
        // at the same time, and stockpiling their turn-in items between runs
        // is normal play -- deleting that stockpile would be the single most
        // destructive thing this function could do.
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest || quest->IsRepeatable() || quest->IsDaily() || quest->IsWeekly()
            || quest->IsMonthly() || quest->IsSeasonal())
            return false;
    }
    return true;
}

// Deletes quest items whose quests are all already turned in. Runs at login
// and again after every turn-in, which is where they actually accumulate:
// Player::RewardQuest only removes the exact RequiredItemCount the quest
// asked for, so overflow from a shared drop, items from a quest that never
// takes them back, and anything looted after the turn-in all just sit in the
// bags forever.
void PurgeCompletedQuestItems(Player* player)
{
    if (!player || !player->GetSession())
        return;
    // Never run against a half-built quest state: the index is what decides
    // "no quest wants this", and an empty one would make that true of
    // everything.
    if (g_questItemQuests.empty())
        return;

    std::vector<Item*> doomed;
    auto consider = [&doomed, player](Item* item)
    {
        if (!item)
            return;
        if (ItemTemplate const* proto = item->GetTemplate())
            if (IsSpentQuestItem(player, proto))
                doomed.push_back(item);
    };

    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        consider(player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        Bag* container = player->GetBagByPos(bag);
        if (!container)
            continue;
        for (uint8 slot = 0; slot < container->GetBagSize(); ++slot)
            consider(container->GetItemByPos(slot));
    }

    if (doomed.empty())
        return;

    // Drop anything still wanted by an active log quest. Done as a second
    // pass over the (small) candidate list rather than inside IsSpentQuestItem
    // so the quest-log walk happens at most once per candidate item, not once
    // per bag slot.
    std::unordered_set<uint32> activeItems;
    for (uint16 logSlot = 0; logSlot < MAX_QUEST_LOG_SIZE; ++logSlot)
    {
        uint32 const questId = player->GetQuestSlotQuestId(logSlot);
        if (!questId)
            continue;
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
            continue;
        for (uint32 id : quest->RequiredItemId)
            if (id)
                activeItems.insert(id);
        for (uint32 id : quest->ItemDrop)
            if (id)
                activeItems.insert(id);
        if (uint32 const src = quest->GetSrcItemId())
            activeItems.insert(src);
    }

    uint32 removed = 0;
    for (Item* item : doomed)
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (!proto || activeItems.count(proto->ItemId))
            continue;
        removed += item->GetCount();
        player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
    }
    if (removed)
        ChatHandler(player->GetSession()).PSendSysMessage(
            "[Bags] Cleared {} quest item(s) left over from quests you have already completed.", removed);
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
//
// That class/subclass pair is not the whole story, though. The Gnomish Army
// Knife is ITEM_CLASS_TRADE_GOODS / 3 and stands in for several tools at once
// (totem category 161, whose mask is a superset of the individual tools'), so
// the narrow test missed it entirely. Anything the game itself tags with a
// totem category and that we already treat as trade goods is a tool: that
// reaches the Army Knife, enchanting rods, the Gyromatic Micro-Adjustor and
// the Jeweler's Kit.
//
// Deliberately NOT reached: ITEM_CLASS_ARMOR / relics (72 equippable "Totem
// of ..." items) and the shaman totem items in ITEM_CLASS_MISC. Those carry
// totem categories too but are gear a player wears or carries, not crafting
// tools, and banking them would be a nasty surprise.
bool IsToolItem(ItemTemplate const* proto)
{
    if (!proto)
        return false;
    if (proto->Class == ITEM_CLASS_WEAPON && proto->SubClass == ITEM_SUBCLASS_WEAPON_MISC)
        return true;
    return proto->TotemCategory != 0 && proto->Class == ITEM_CLASS_TRADE_GOODS;
}

// Item ids the class check cannot reach, from lg_vault_reagent.
//
// The repeatable turn-in currencies -- Dark Iron Residue, Core of Elements,
// darkmoon cards, bijous, Relics of Ulduar, Argent Dawn tokens -- are all
// ITEM_CLASS_QUEST (12) in item_template, not trade goods. Admitting class 12
// wholesale is not safe: the quest guard above only covers quests the player is
// currently ON, so a quest-starter item looted before its quest was accepted
// would be filed away where they cannot reach it. An explicit list keeps the
// named oddities working without putting every quest item at risk, and can be
// extended with a SQL row rather than a rebuild.
std::unordered_set<uint32> g_vaultReagentIds;

bool IsLockbox(ItemTemplate const* proto);   // defined below; IsReagentItem needs it

bool IsReagentItem(ItemTemplate const* proto, Player* player = nullptr)
{
    if (!proto || IsQuestItem(proto, player) || IsKeyItem(proto))
        return false;
    // After the quest guard on purpose: if this player is actually on a quest
    // that wants it, it stays in their bags whatever the list says.
    if (g_vaultReagentIds.count(proto->ItemId))
        return true;
    // Reports #72/#76: the automated loot path already files lockboxes into
    // the vault (ResolveLootAction), but a box sitting in a bag could not be
    // sent there by hand -- DepositAll asks this function, and a locked
    // container is none of trade goods/gem/reagent/tool. Admit them here, so
    // "deposit all" and the automated path agree on where a box belongs.
    // Withdrawal still unlocks it (InstantPickLock) and the vendor/skip guard
    // in ApplyLootRule keeps a locked box from ever being destroyed.
    if (IsLockbox(proto))
        return true;
    return proto->Class == ITEM_CLASS_TRADE_GOODS
        || proto->Class == ITEM_CLASS_GEM
        || proto->Class == ITEM_CLASS_REAGENT
        || IsToolItem(proto);
}

void LoadVaultReagentIds()
{
    g_vaultReagentIds.clear();
    if (!CharacterDatabase.Query(
        "SELECT 1 FROM `information_schema`.`TABLES` "
        "WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = 'lg_vault_reagent'"))
        return;
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT `item_entry` FROM `lg_vault_reagent`"))
    {
        do
            g_vaultReagentIds.insert((*result)[0].Get<uint32>());
        while (result->NextRow());
    }
    LOG_INFO("server.loading", "Living Gear vault: {} extra reagent item id(s)",
        g_vaultReagentIds.size());
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
    // Bug report #21, 2026-08-22, and a regression introduced by #13 the same
    // day: an item the player needs for a quest they are ON never gets routed
    // anywhere. It stays in the bag.
    //
    // #13 added a rule sending food, potions and scrolls to the vendor by
    // default. Smoked Desert Dumplings are ITEM_CLASS_CONSUMABLE /
    // ITEM_SUBCLASS_FOOD and are the objective of "Kitchen Assistance", so
    // every one the player cooked was sold out from under them. The rule was
    // right; putting it after IsReagentItem -- the only branch that happened
    // to check quest items -- was not.
    //
    // Guarding here rather than inside the food branch, because the same trap
    // is one careless "return ACT_VENDOR;" away on any future branch. Quest
    // items are now the first thing this function decides, so nothing added
    // below can quietly eat one.
    if (IsQuestItem(proto, player))
        return ACT_BAG;
    // Report #35: a quest-class item the player has no active quest for is
    // something they are stockpiling for a repeatable turn-in, so file it.
    // IsQuestItem above already returned ACT_BAG for anything a live quest
    // wants, so this cannot swallow an item that is currently needed -- and
    // unlike the retired quest vault, the reagent panel shows it and hands it
    // back on demand.
    if (proto->Class == ITEM_CLASS_QUEST || proto->Bonding == BIND_QUEST_ITEM)
        return ACT_REAGENT_VAULT;
    // 2026-08-22: locked containers file into the reagent vault instead of
    // sitting in bags. Pickpocketing in particular produces junkboxes far
    // faster than anyone opens them, and a Rogue working a crowd would fill
    // their bags with boxes inside a couple of minutes. Taking one back out
    // now picks its lock on the way (see InstantPickLock), so the vault is a
    // holding pen rather than somewhere they go to be forgotten.
    if (IsLockbox(proto))
        return ACT_REAGENT_VAULT;
    // 2026-08-21: quest vault dropped entirely per user request ("should no
    // longer even exist") -- it never actually worked (nothing ever
    // restored a vaulted item before a quest turn-in, so items just
    // vanished from the player with no way back). Quest items now simply
    // stay in the bag like anything else -- IsQuestItem(proto, player) is
    // still used (via IsReagentItem's exclusion check below) to keep them
    // out of the reagent vault, just no longer routed anywhere itself.
    // Bug report #29, 2026-08-22: "Bought a blacksmith hammer, didn't go into
    // inventory or into reagent bank, need it for crafting."
    //
    // Tools are reagent-vault material by IsReagentItem, so every one a player
    // acquired was pulled straight out of the bag the instant it arrived. On
    // the bot accounts that produced 30-75 Blacksmith Hammers apiece in a
    // single vault -- the bank was acting as an unbounded tool sink rather
    // than a convenience.
    //
    // Bank the first one, because a tool you own on one character should be
    // reachable from all of them. Leave every copy after that in the bag,
    // where it is visibly yours and immediately usable. Combined with
    // TopUpToolFromVault (which hands the banked one back the moment a recipe
    // needs it), the player always ends up holding a tool rather than
    // wondering where it went.
    if (IsToolItem(proto))
    {
        uint32 const accountId =
            player && player->GetSession() ? player->GetSession()->GetAccountId() : 0;
        if (accountId && VaultCount(accountId, 0, VAULT_REAGENT, proto->ItemId) > 0)
            return ACT_BAG;
        return ACT_REAGENT_VAULT;
    }
    // Reports #143/#148/#150: dungeon currencies ([Emblem of Triumph] and the
    // other emblems) were being filed into the reagent vault, where the
    // client's currency tab cannot see them -- players read the tab as "the
    // emblem never dropped". f9aa4449f deliberately routed turn-in
    // currencies to the vault, but that design loses to how the tab actually
    // works: class-10 currency items must stay in bags to be counted there.
    // The emblem rows in lg_vault_reagent are handed back by the migration
    // that accompanies this guard, so nothing is lost.
    if (proto->Class == ITEM_CLASS_MONEY)
        return ACT_BAG;
    // Report #182: currency TOKENS (shards, emblems, marks -- BagFamily
    // currency tokens) pool to the account. This supersedes the 0044
    // "currency stays in bags" guard: the user asked for shared account
    // currency storage, and pooled tokens are spent straight from the pool
    // on vendor purchases, so the currency-tab visibility problem 0044 solved
    // no longer applies (the balance follows the player account-wide via the
    // CUR| sync line). Gold itself was already pooled account-wide by the
    // SharedCurrencies system in LivingGear_Next.cpp -- ACT_BAG here just
    // leaves it to that path.
    if (proto->IsCurrencyToken())
        return ACT_ACCOUNT_CURRENCY;
    if (IsReagentItem(proto, player))
        return ACT_REAGENT_VAULT;
    if (proto->Quality == ITEM_QUALITY_POOR)
        return ACT_VENDOR;
    // Bug report #13, 2026-08-22: "still looting food/potions/consumables --
    // automatically vendor/destroy please."
    //
    // The client's DEFAULT_RULES has vendored these since 2026-08-21, but those
    // defaults only exist once a player opens and saves the autoloot rules UI.
    // Anyone who never touched that screen -- i.e. almost everyone -- fell
    // through to this function, which had no opinion on consumables and put
    // them in the bags. Matching the client's documented defaults here means
    // the behaviour is the same whether or not the UI has ever been opened.
    //
    // An explicit rule still wins: ResolveLootAction only calls this when no
    // rule matched, so anyone who wants to keep their food just says so.
    // Bug report #31, 2026-08-23: "my conjured food is getting auto deleted."
    //
    // Conjured food is ITEM_CLASS_CONSUMABLE / FOOD, so it fell into the rule
    // below and was destroyed -- and since conjured items have no sell value,
    // the player did not even get coin for it. A mage conjuring a stack watched
    // it evaporate. Same shape as #21 (cooked quest food auto-vendored), which
    // is why the guard goes here, above the rule, rather than inside it.
    //
    // The core already asks this exact question for its own purposes.
    if (proto->IsConjuredConsumable())
        return ACT_BAG;
    if (proto->Class == ITEM_CLASS_CONSUMABLE
        && (proto->SubClass == ITEM_SUBCLASS_FOOD
            || proto->SubClass == ITEM_SUBCLASS_SCROLL
            || proto->IsPotion()))
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
    if (action == ACT_ACCOUNT_CURRENCY)
    {
        // Report #182: token currencies pool to the account instead of the
        // character's currency tab. The CUR| sync line tells the addon the
        // new balance so the UI can display it anywhere in the account.
        AccountCurrencyAdd(accountId, itemId, int64(count));
        player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
        SendLine(player, Acore::StringFormat("CUR|{}|{}|{}",
            itemId, int64(count), AccountCurrencyBalance(accountId, itemId)));
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
    if (action == ACT_SKIP || action == ACT_DESTROY)
    {
        player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
        return;
    }
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
    // A withdraw is not a loot. Without this the rule engine files the item
    // straight back into the vault it just came out of -- see
    // g_vaultGrantDepth above.
    if (g_vaultGrantDepth)
        return;
    if (!player || !item || !player->GetSession() || !LivingGear_HasPerk(player, SPELL_AUTOLOOT)
        || !AutolootOn(player->GetSession()->GetAccountId()))
        return;
    ItemTemplate const* proto = item->GetTemplate();
    if (!proto)
        return;
    uint8 action = ResolveLootAction(player, proto);
    // Never vendor or discard something still locked -- the contents have not
    // been seen yet. Vaulting it is the non-destructive choice.
    if (IsLockbox(proto) && (action == ACT_VENDOR || action == ACT_SKIP))
        action = ACT_REAGENT_VAULT;
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
    if (!player || !target || !player->GetSession() || !LivingGear_HasPerk(player, SPELL_AUTOLOOT)
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
        uint32 granted = 0, refused = 0;
        for (uint32 slot = 0; slot < maxSlot; ++slot)
        {
            InventoryResult msg = EQUIP_ERR_OK;
            if (p->StoreLootItem(uint8(slot), l, msg))
                ++granted;
            else
                ++refused;
        }
        uint32 const gold = l->gold;
        if (l->gold)
        {
            p->ModifyMoney(int32(l->gold));
            l->gold = 0;
        }
        // Report #42: the Shadowstep sweep logs 4 humanoids pickpocketed and
        // the player sees no loot, so the casts are fine and the grant is
        // where it goes wrong. Same counters the chest path already prints,
        // for the same reason -- "did it fire" and "did anything land" are
        // different questions.
        LOG_INFO("module.livinggear",
            "pickpocket autoloot: '{}' -- {} slot(s), granted {}, refused {}, {} copper",
            c->GetName(), maxSlot, granted, refused, gold);
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
    if (!player || !creature || !player->IsAlive() || !player->GetSession() || !LivingGear_HasPerk(player, SPELL_AUTOLOOT)
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
        // Bug report #32, 2026-08-23: "bots autoloot is bypassing the group
        // loot rolling." Correct, and it was not bot-specific -- grabbing every
        // slot the moment a corpse becomes lootable skips the roll for anyone
        // with autoloot on. It just shows up as bots because they are the ones
        // reaching the corpse first.
        //
        // Under any method that rolls, the engine hands the item out after the
        // roll resolves; taking it here is what steals it. Free-for-all is the
        // one method with nothing to wait for, so autoloot stays on there.
        //
        // #139/#150: this must ask the EFFECTIVE method, not the stored one --
        // personal loot (#114) leaves the stored method at GROUP_LOOT/etc while
        // the engine actually loots every corpse FFA (GetEffectiveLootMethod).
        // Comparing against the stored method refused corpse autoloot for
        // every grouped player with autoloot on. Same fix the personal-loot
        // core patch (0029) applied to LootHandler's permission switch.
        if (group->GetEffectiveLootMethod() != FREE_FOR_ALL)
            return;
    }
    else if (creature->GetLootRecipient() != player)
        return;

    Loot* loot = &creature->loot;
    loot->FillNotNormalLootFor(player);
    uint32 const maxSlot = loot->GetMaxSlotInLootFor(player);
    uint32 granted = 0, refused = 0;
    for (uint32 slot = 0; slot < maxSlot; ++slot)
    {
        InventoryResult msg = EQUIP_ERR_OK;
        if (player->StoreLootItem(uint8(slot), loot, msg))
            ++granted;
        else
            ++refused;
    }
    uint32 const gold = loot->gold;
    if (loot->gold)
    {
        player->ModifyMoney(int32(loot->gold));
        loot->gold = 0;
    }
    // Same counters the chest and pickpocket paths print -- "did it fire" and
    // "did anything land" are different questions (#139/#150 had no line here
    // at all, so a silent early return was indistinguishable from a grant).
    LOG_INFO("module.livinggear",
        "corpse autoloot: '{}' -- {} slot(s), granted {}, refused {}, {} copper",
        creature->GetName(), maxSlot, granted, refused, gold);
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
    if (!player || !creature || !player->GetSession() || !LivingGear_HasPerk(player, SPELL_AUTOLOOT)
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
    if (!player || !player->GetSession() || !LivingGear_HasPerk(player, SPELL_AUTOLOOT))
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

// Withdraw one entry from the vault back into the bags. The client sends
// "TAKE|<kind>|<entry>" from a vault row's OnClick (LivingGear.lua) -- one
// stack per click -- and "TAKE|<kind>|<entry>|<count>" from the craft
// staging path, which wants an exact shortfall, not a stack. Anything that
// doesn't fit goes straight back into the vault rather than evaporating.
void VaultWithdraw(Player* player, uint8 kind, uint32 itemEntry, uint32 count = 0)
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

    uint32 const want = std::min<uint32>(stored,
        count ? count : std::max<uint32>(proto->GetMaxStackSize(), 1));
    uint32 const taken = VaultRemove(accountId, ownerGuid, kind, itemEntry, want);
    if (!taken)
        return;
    Item* withdrawn = nullptr;
    uint32 const given = StoreFromVault(player, itemEntry, taken, &withdrawn);
    if (given < taken)
        VaultAdd(accountId, ownerGuid, kind, itemEntry, taken - given);
    if (!given)
    {
        ChatHandler(player->GetSession()).SendSysMessage("[Vault] No room in your bags.");
        return;
    }
    if (withdrawn && IsLockbox(proto) && InstantPickLock(player, withdrawn))
        ChatHandler(player->GetSession()).PSendSysMessage(
            "[Vault] Picked the lock on {}.", proto->Name1);
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

// The craft-prep fallback that used to live here (CRAFTPREP /
// PrepareCraftReagents) is retired. It staged one craft's worth of reagents
// into the backpack whenever a craft came back refused, on the theory that
// the client might refuse to send a cast the bag could not pay for. It
// cannot: the 3.3.5 client validates nothing about reagents on send, the
// Create button already counts the vault (GetTradeSkillInfo /
// GetCraftInfo hooks in the addon), and the server answers the reagent gate
// from the vault in place. Anything the fallback staged was materials being
// pulled out of the bank for no reason -- the exact behaviour the reagent
// bank exists to prevent.

// Report #219 (reopened): crafts whose reagents sit only in the account
// reagent vault must run server-side and pay the vault DIRECTLY. The old
// staging path pulled the shortfall into the backpack first -- exactly the
// carrying-around the reagent bank exists to prevent. A CRAFTCAST runs the
// recipe through the player's own normal cast path: the Spell::CheckItems
// gate already counts the vault, and TakeReagents pays bags-then-vault in
// place, so nothing is ever withdrawn into the bags. The crafted item still
// lands in the bags like any other craft.
//
// Guards: the spell must be known to this character, create an item, and
// actually list reagents, so the channel cannot become a generic "cast
// anything" primitive. Batches are staggered (longer than a craft's cast
// time) so each cast's reagent payment lands before the next one checks
// stock -- that is what keeps a wide batch from overdrawing a thin vault.
// A failed cast (stock ran dry) zeroes the batch so the staggered tail is
// skipped instead of spamming refusals.
std::unordered_map<uint32, uint32> g_craftCastRemaining; // char guid counter -> casts left

void HandleCraftCast(Player* player, uint32 spellId, uint32 count)
{
    if (!player || !player->IsInWorld())
        return;
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
    if (!info || count == 0)
        return;
    if (count > 200)
        count = 200;
    if (!player->HasSpell(spellId) || info->Reagent[0] == 0 ||
        !info->HasEffect(SPELL_EFFECT_CREATE_ITEM))
    {
        LOG_INFO("module.livinggear",
            "craft cast refused: spell {} is not a known craft of this character", spellId);
        return;
    }
    ObjectGuid const ownerGuid = player->GetGUID();
    uint32 const staggerMs = info->CastTimeEntry ? 600u : 50u;
    g_craftCastRemaining[ownerGuid.GetCounter()] = count;
    for (uint32 i = 0; i < count; ++i)
    {
        player->m_Events.AddEventAtOffset([ownerGuid, spellId]()
        {
            auto found = g_craftCastRemaining.find(ownerGuid.GetCounter());
            if (found == g_craftCastRemaining.end())
                return;
            Player* p = ObjectAccessor::FindPlayer(ownerGuid);
            if (!p || !p->IsInWorld())
            {
                g_craftCastRemaining.erase(ownerGuid.GetCounter());
                return;
            }
            --found->second;
            if (!p->CastSpell(p, spellId, false))
                g_craftCastRemaining.erase(ownerGuid.GetCounter());
        }, std::chrono::milliseconds(staggerMs * i));
    }
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
    uint32 craftCount = 0;
    if (sscanf(msg.c_str(), "CRAFTCAST|%u|%u", &craftSpell, &craftCount) == 2)
    {
        HandleCraftCast(player, craftSpell, craftCount);
        return true;
    }
    uint32 takeKind = 0;
    uint32 takeEntry = 0;
    uint32 takeCount = 0;
    // Third field (exact count) is optional; the vault panel never sends it.
    if (sscanf(msg.c_str(), "TAKE|%u|%u|%u", &takeKind, &takeEntry, &takeCount) >= 2)
    {
        VaultWithdraw(player, uint8(takeKind), takeEntry, takeCount);
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

// Report #191: proximity re-poll cadence and scratch list for the 5s sweep.
uint32 const AUTOLOOT_POLL_MS = 5000;
float const AUTLOOT_POLL_RANGE = 30.0f;
std::unordered_map<uint32, uint32> g_autolootPollAcc;

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
        PLAYERHOOK_ON_CREATURE_KILL,
        PLAYERHOOK_ON_PLAYER_COMPLETE_QUEST,
        PLAYERHOOK_ON_UPDATE
    }) { }

    // *Autoloot (910008) is "on by default" -- there's no unlock condition,
    // every account should just have it. It only ever silently no-ops if
    // sSpellMgr has no spell_dbc row for it (see rev_living_gear_autoloot_dbc.sql).
    void OnPlayerLogin(Player* player) override
    {
        if (!player || !player->GetSession())
            return;
        uint32 const accountId = player->GetSession()->GetAccountId();
        // Report #182: sync the shared account currency pool to the client so
        // every character sees the same pooled balances on arrival.
        LoadAccountCurrency(accountId);
        for (auto const& [itemId, balance] : g_accountCurrency[accountId].balances)
            if (balance > 0)
                SendLine(player, Acore::StringFormat("CUR|{}|0|{}", itemId, balance));
        // The missing-spell_dbc check used to guard this whole function, so
        // one absent row took the account key ring and the quest-vault
        // drain down with it too. Scope it to the thing it is actually
        // about.
        if (sSpellMgr->GetSpellInfo(SPELL_AUTOLOOT))
        {
            if (!LivingGear_HasPerk(player, SPELL_AUTOLOOT))
                player->learnSpell(SPELL_AUTOLOOT);
            // Also persist/sync it the same way UnlockPerk() elsewhere does,
            // so the addon's db.perks[910008] (PerkKnown()) actually goes
            // true -- that's what gates the Autoloot toggle being shown.
            CharacterDatabase.DirectExecute(
                "INSERT IGNORE INTO `lg_account_perk` (`account_id`, `spell_id`) VALUES ({}, {})",
                accountId, SPELL_AUTOLOOT);
            SendLine(player, Acore::StringFormat("PK|{}|1", SPELL_AUTOLOOT));
            // Looting 10 corpses earns this outright, so refund anyone who
            // had already bought it.
            LivingGear_RefundIfPurchased(player, SPELL_AUTOLOOT);
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
        PurgeCompletedQuestItems(player);
    }

    // Fired at the tail of Player::RewardQuest (PlayerQuest.cpp), so the
    // quest is already in m_RewardedQuests by the time this runs -- which is
    // exactly what makes its own leftovers collectable here. Deferred a tick
    // anyway: this is still inside the turn-in call stack, and destroying
    // items from in there is the same hazard ApplyLootRule documents above.
    void OnPlayerCompleteQuest(Player* player, Quest const* /*quest*/) override
    {
        if (!player)
            return;
        ObjectGuid const playerGuid = player->GetGUID();
        player->m_Events.AddEventAtOffset([playerGuid]()
        {
            if (Player* p = ObjectAccessor::FindPlayer(playerGuid))
                if (p->IsInWorld())
                    PurgeCompletedQuestItems(p);
        }, std::chrono::milliseconds(1));
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

    // Report #191: autoloot was purely event-driven (kill, chest open, loot
    // window), so lootable corpses left behind by other players -- or ones a
    // late-arriving group member filled -- sat unlooted until the player
    // walked away. Re-poll nearby lootable corpses on a 5s cadence and run
    // them through the same guarded autoloot path (which still enforces the
    // recipient/loot-method rules, so this never takes someone else's roll).
    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        if (!player || !player->IsAlive() || !player->GetSession())
            return;
        if (!LivingGear_HasPerk(player, SPELL_AUTOLOOT) || !AutolootOn(player->GetSession()->GetAccountId()))
            return;
        uint32& acc = g_autolootPollAcc[player->GetGUID().GetCounter()];
        acc += diff;
        if (acc < AUTOLOOT_POLL_MS)
            return;
        acc = 0;

        std::list<Unit*> nearby;
        Acore::AnyUnitInObjectRangeCheck check(player, AUTLOOT_POLL_RANGE);
        Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(player, nearby, check);
        Cell::VisitObjects(player, searcher, AUTLOOT_POLL_RANGE);
        for (Unit* unit : nearby)
            if (Creature* c = unit->ToCreature())
                if (c->IsAlive() && c->HasDynamicFlag(UNIT_DYNFLAG_LOOTABLE))
                    AutolootCreatureKill(player, c);
    }
};

class VaultWorld : public WorldScript
{
public:
    VaultWorld() : WorldScript("LivingGearVaultWorld", { WORLDHOOK_ON_STARTUP }) { }

    void OnStartup() override
    {
        BuildQuestItemIndex();
        LoadVaultReagentIds();
    }
};

} // namespace LivingGearVault

// Global-scope bindings for the core callbacks (core-patch 0049, report
// #182). Player.cpp links these by unqualified name; the implementations
// live inside LivingGearVault above.
bool LivingGear_AccountCurrencyCovers(Player* player, uint32 itemId, uint32 count)
{
    return LivingGearVault::LivingGear_AccountCurrencyCovers(player, itemId, count);
}

void LivingGear_AccountCurrencyPay(Player* player, uint32 itemId, uint32 count)
{
    LivingGearVault::LivingGear_AccountCurrencyPay(player, itemId, count);
}

void SendVaultAndRuleSync(Player* player)
{
    LivingGearVault::SendVaultAndRuleSync(player);
}

void SendAutolootSync(Player* player)
{
    LivingGearVault::SendAutolootSync(player);
}

// Bug reports #15 and #55 (and the autoloot half of #46), 2026-08-24.
//
// This asked whether the CHARACTER had learned spell 910008. It never can:
// Autoloot was deliberately taken out of CASTABLE_SPELLS when its redundant
// spellbook button was removed, and UnlockPerk only learns a spell
// `if (PerkIsCastable(spellId))`. So the gate demanded something the design
// guarantees will not exist, and every autoloot path -- chests, quest objects,
// fishing, corpses -- silently declined for anyone whose character had not
// learned it back when it still was castable.
//
// Measured on the live realm before changing it: 96 accounts own the perk, and
// of 16 real characters 5 did not know the spell -- including Aela, the
// character that filed #55 and #51. Those five had autoloot switched off
// permanently with no way to tell.
//
// The other four call sites in this very file (ApplyLootRule, pickpocket,
// corpse autoloot) already ask LivingGear_HasPerk, i.e. the ACCOUNT. This one
// was the odd one out -- exactly the "two copies of a helper disagree" hazard
// ARCHITECTURE.md warns about, and the account/character split from invariant 6.
bool IsAutolootEnabled(Player* player)
{
    if (!player || !player->GetSession() || !LivingGear_HasPerk(player, LivingGearVault::SPELL_AUTOLOOT))
        return false;
    return LivingGearVault::AutolootOn(player->GetSession()->GetAccountId());
}

// Called from a core patch in Spell::EffectPickPocket (SpellEffects.cpp).
// Called from the loot-roll hook in LivingGear_Support.cpp (bug report #12,
// quest items at 100% drop rate). Exported from here because this file already
// owns g_questItemQuests, the item-id -> quests index -- answering this by
// walking the player's 25 quest slots and their required-item arrays on every
// single loot roll would be far more expensive than one hash lookup.
//
// "Needs" means an active, unfinished quest. A quest already marked complete is
// deliberately excluded: its objectives are met, so forcing further copies to
// drop would just be noise.
bool LivingGear_PlayerNeedsItemForQuest(Player const* player, uint32 itemId)
{
    if (!player || !itemId)
        return false;
    auto const known = LivingGearVault::g_questItemQuests.find(itemId);
    if (known == LivingGearVault::g_questItemQuests.end())
        return false;
    for (uint32 questId : known->second)
        if (player->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE)
            return true;
    return false;
}

bool LivingGear_TryAutolootPickpocket(Player* player, Unit* target)
{
    return LivingGearVault::TryAutolootPickpocket(player, target);
}

// Called from core patches in Spell::CheckItems / Spell::TakeReagents
// (Spell.cpp). The reagent gate answers in place and consumption pays the
// vault shortfall directly -- nothing is ever withdrawn into the bags to
// satisfy a craft.
bool LivingGear_VaultCoversReagent(Player* player, uint32 itemId, uint32 needed)
{
    return LivingGearVault::VaultCoversReagent(player, itemId, needed);
}

// Report #137/#138 instrumentation: vault stock for one reagent, for the
// refusal log line in Spell::CheckItems. Read-only.
uint32 LivingGear_VaultCountForDiag(Player* player, uint32 itemId)
{
    if (!player || !player->GetSession())
        return 0;
    return LivingGearVault::VaultCount(player->GetSession()->GetAccountId(), 0,
        LivingGearVault::VAULT_REAGENT, itemId);
}

void LivingGear_ConsumeReagent(Player* player, uint32 itemId, uint32 count)
{
    LivingGearVault::ConsumeReagentBagThenVault(player, itemId, count);
}

// Bug report #29: profession tools live in SpellInfo::Totem[] and
// TotemCategory[], not Reagent[], so the reagent top-up above never covered
// them and a banked hammer could not satisfy a blacksmithing recipe. These
// answer in place -- nothing leaves the reagent bank.
bool LivingGear_VaultHasToolCategory(Player* player, uint32 totemCategory)
{
    return LivingGearVault::VaultHasToolCategory(player, totemCategory);
}

bool LivingGear_VaultHasItem(Player* player, uint32 itemId)
{
    return LivingGearVault::VaultHasItem(player, itemId);
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
