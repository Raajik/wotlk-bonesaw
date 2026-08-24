/*
 * Living Gear
 * Equipped items gain XP and levels. Grown stats apply only while worn.
 *
 * Attunement (2026-08-20 redesign, see Bonesaw.md): no longer a one-time
 * sacrifice. Every level-up of an eligible equipped item (or a Curator-
 * tracked bag/bank/armory item, LivingGear_Perks.cpp TickCurator) banks a
 * live slice of that item's *current* grown stats into the account's
 * lg_absorb record for its item entry -- see BankAttunement/AddItemXpAndBank.
 * The slice starts at 1% at level 1 and ramps to 100% at attuneCapLevel
 * (default 25); items keep growing their own worn stats past that point (up
 * to maxLevel/50) but stop adding anything further to the account. The item
 * is never destroyed. The old destructive SacrificeItem/HandleAttuneMessage
 * path below is kept but effectively dead now that the addon no longer
 * sends ATTUNE| -- not deleted outright since it's inert, not broken.
 */

#include "Bag.h"
#include "Chat.h"
#include "CommandScript.h"
#include "Config.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Random.h"
#include "RBAC.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "WorldSession.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

class Player;
void SendVaultAndRuleSync(Player* player); // LivingGear_Vault.cpp
void SendAutolootSync(Player* player); // LivingGear_Vault.cpp

// Addon command handlers, one per module. Each returns true if it
// recognised and consumed the message. See DispatchAddonCommand below.
bool LivingGear_HandleVaultCommand(Player* player, std::string const& msg); // LivingGear_Vault.cpp
bool LivingGear_HandlePerksCommand(Player* player, std::string const& msg); // LivingGear_Perks.cpp
bool LivingGear_HandleClassPerksCommand(Player* player, std::string const& msg); // LivingGear_ClassPerks.cpp
bool LivingGear_HandleNextCommand(Player* player, std::string const& msg); // LivingGear_Next.cpp
bool LivingGear_HandleSupportCommand(Player* player, std::string const& msg); // LivingGear_Support.cpp
bool LivingGear_HandleAmenitiesCommand(Player* player, std::string const& msg); // LivingGear_Amenities.cpp
void LivingGear_ShowDiagnostics(Player* player); // LivingGear_Support.cpp

// Login-time state pushes, reused to answer a client REQ. See SendAddonSync.
void LivingGear_SendPerksSync(Player* player); // LivingGear_Perks.cpp
void LivingGear_SendClassPerksSync(Player* player); // LivingGear_ClassPerks.cpp
void LivingGear_SendNextSync(Player* player); // LivingGear_Next.cpp
void LivingGear_SendWayfarerSync(Player* player); // LivingGear_Amenities.cpp

// ---------------------------------------------------------------------
// Shared addon transport.
//
// Every server->client line is a self-whisper, and Player::Whisper runs
// the *same* OnPlayerCanUseChat hook that incoming client commands arrive
// on (Player.cpp) -- worse, a hook returning false there does not just
// mean "not handled", it makes Whisper drop the packet before it is ever
// built. So an outgoing line whose text also matches a command pattern is
// both re-executed as a command AND silently never sent. That has already
// cost this module a stack-overflow crash (ALDE| looping into itself) and
// an entire session of "the client never receives corrected state" (every
// Vault sync line being swallowed). One depth counter, checked once by the
// one dispatcher, closes that off for every module at the same time --
// which is why all eight modules now funnel their sends through here
// rather than each keeping a private copy of Whisper plus, at best, a
// private copy of the guard.
// ---------------------------------------------------------------------
namespace
{
thread_local uint32 g_addonSendDepth = 0;
}

bool LivingGear_IsAddonSendInProgress()
{
    return g_addonSendDepth != 0;
}

void LivingGear_SendAddonLine(Player* player, std::string const& line)
{
    if (!player || !player->GetSession())
        return;
    ++g_addonSendDepth;
    player->Whisper(std::string("LG\t") + line, LANG_ADDON, player);
    --g_addonSendDepth;
}

namespace LivingGear
{
struct LgConfig
{
    bool enabled = true;
    uint32 xpPerKill = 1;
    uint32 xpEliteKill = 3;
    uint32 xpBossKill = 10;
    uint16 maxLevel = 50;
    float growthPerLevel = 0.10f;
    // Item level every piece converges on at MaxLevel. 284 is Icecrown
    // 25-heroic, the top of WotLK.
    float ilvlCeiling = 284.0f;
    float absorbPct = 0.10f; // legacy one-time-sacrifice pct; unused by the new continuous-attunement path, kept for the (now-unreachable) SacrificeItem code path
    float rollChance = 25.0f;
    uint8 rollStatCount = 1;

    // Continuous-attunement redesign (2026-08-20, see Bonesaw.md): equipped
    // (and Curator-tracked) gear now banks a live, ever-updating slice of
    // its own current stats into the account's lg_absorb record on every
    // level-up, instead of being destroyed via a one-time "Attune" button.
    // AttuneCapLevel is the living-gear level at which an item's account
    // contribution reaches 100% of its current stats; AttuneIlvlBaseline is
    // the real WoW item level at which the fast 1-AttuneCapLevel XP curve
    // hits its "designed" pace (default: maxes out in ~15-30 min of casual
    // grinding); AttuneIlvlFloorScale keeps very-low-ilvl gear from costing
    // an unrealistic near-zero XP.
    uint16 attuneCapLevel = 25;
    float attuneIlvlBaseline = 70.0f;
    float attuneIlvlFloorScale = 0.15f;

    void Load()
    {
        enabled = sConfigMgr->GetOption<bool>("LivingGear.Enable", true);
        xpPerKill = sConfigMgr->GetOption<uint32>("LivingGear.XpPerKill", 1);
        xpEliteKill = sConfigMgr->GetOption<uint32>("LivingGear.XpEliteKill", 3);
        xpBossKill = sConfigMgr->GetOption<uint32>("LivingGear.XpBossKill", 10);
        maxLevel = static_cast<uint16>(sConfigMgr->GetOption<uint32>("LivingGear.MaxLevel", 50));
        growthPerLevel = sConfigMgr->GetOption<float>("LivingGear.GrowthPerLevel", 0.10f);
        ilvlCeiling = sConfigMgr->GetOption<float>("LivingGear.IlvlCeiling", 284.0f);
        absorbPct = sConfigMgr->GetOption<float>("LivingGear.AbsorbPct", 0.10f);
        rollChance = sConfigMgr->GetOption<float>("LivingGear.RollChance", 25.0f);
        rollStatCount = static_cast<uint8>(sConfigMgr->GetOption<uint32>("LivingGear.RollStatCount", 1));
        if (rollStatCount < 1)
            rollStatCount = 1;
        if (rollStatCount > 5)
            rollStatCount = 5;
        attuneCapLevel = static_cast<uint16>(sConfigMgr->GetOption<uint32>("LivingGear.Attune.CapLevel", 25));
        attuneIlvlBaseline = sConfigMgr->GetOption<float>("LivingGear.Attune.IlvlBaseline", 70.0f);
        attuneIlvlFloorScale = sConfigMgr->GetOption<float>("LivingGear.Attune.IlvlFloorScale", 0.15f);
    }
};

LgConfig g_cfg;

// Client Spell.dbc + SkillLineAbility.dbc ship in patch-Y.MPQ.
// The window is FrameXML UI in patch-enUS-4.MPQ (not a user-installed addon).
uint32 const SPELL_WINDBLOWN = 910001;

struct LgStats
{
    float str = 0.0f;
    float agi = 0.0f;
    float sta = 0.0f;
    float intel = 0.0f;
    float spi = 0.0f;
    float armor = 0.0f;

    float Total() const
    {
        return str + agi + sta + intel + spi + armor;
    }

    LgStats& operator+=(LgStats const& o)
    {
        str += o.str;
        agi += o.agi;
        sta += o.sta;
        intel += o.intel;
        spi += o.spi;
        armor += o.armor;
        return *this;
    }
};

struct LgItemState
{
    uint32 itemGuid = 0;
    uint32 itemEntry = 0;
    uint32 ownerGuid = 0;
    uint32 xp = 0;
    uint16 level = 1;
    int32 rollStr = 0;
    int32 rollAgi = 0;
    int32 rollSta = 0;
    int32 rollInt = 0;
    int32 rollSpi = 0;
};

// Adjacent-tier armor: 1=cloth 2=leather 3=mail 4=plate
struct ArmorRange { int min; int max; };
ArmorRange const CLASS_ARMOR_RANGE[12] =
{
    {0, 0},
    {3, 4}, // Warrior
    {3, 4}, // Paladin
    {2, 3}, // Hunter
    {1, 2}, // Rogue
    {1, 1}, // Priest
    {3, 4}, // Death Knight
    {2, 3}, // Shaman
    {1, 1}, // Mage
    {1, 1}, // Warlock
    {0, 0},
    {1, 2}  // Druid
};

static Stats const PRIMARY_STATS[5] =
{
    STAT_STRENGTH, STAT_AGILITY, STAT_STAMINA, STAT_INTELLECT, STAT_SPIRIT
};

static std::unordered_map<uint32, std::array<float, 6>> g_applied;

static bool IsEligible(ItemTemplate const* proto)
{
    if (!proto)
        return false;

    switch (proto->InventoryType)
    {
    case INVTYPE_NON_EQUIP:
    case INVTYPE_BAG:
    case INVTYPE_AMMO:
    case INVTYPE_QUIVER:
    case INVTYPE_BODY:
    case INVTYPE_TABARD:
        return false;
    default:
        break;
    }

    return proto->Class == ITEM_CLASS_WEAPON || proto->Class == ITEM_CLASS_ARMOR;
}

static bool AccountCanUse(uint8 playerClass, ItemTemplate const* proto)
{
    if (!proto || playerClass == 0 || playerClass >= 12)
        return false;

    if (proto->AllowableClass != 0 && proto->AllowableClass != uint32(-1))
        if (!(proto->AllowableClass & (1u << (playerClass - 1))))
            return false;

    switch (proto->InventoryType)
    {
    case INVTYPE_CLOAK:
    case INVTYPE_NECK:
    case INVTYPE_FINGER:
    case INVTYPE_TRINKET:
        return true;
    default:
        break;
    }

    if (proto->Class == ITEM_CLASS_WEAPON)
        return true;

    if (proto->Class == ITEM_CLASS_ARMOR)
    {
        if (proto->SubClass == 0)
            return true;
        ArmorRange const range = CLASS_ARMOR_RANGE[playerClass];
        int const sub = static_cast<int>(proto->SubClass);
        return sub >= range.min && sub <= range.max;
    }

    return false;
}

// ---------------------------------------------------------------------
// Item level stat budget, measured from the game's own item data.
//
// Levelling an item raises its EFFECTIVE item level, and its stats are then set
// to what an item of that level carries. The obvious shortcut -- multiply the
// base stats by effective_ilvl / base_ilvl -- is wrong and inverts gear value:
// an ilvl 20 green with 8 stamina would reach 23x = 184 while an ilvl 200 epic
// with 60 reaches 2.3x = 138, so the green wins. The budget has to be absolute,
// not relative.
//
// The curve is also not linear. Measured on the chest slot: 2.3 primary stats
// at ilvl 20, 33.4 at 60, 164.6 at 200, 316.1 at 264 -- roughly 0.8 stats per
// ilvl at the bottom and 2.4 at the top. Any hardcoded slope badly undervalues
// high-ilvl gear, so this is built from item_template at startup instead: the
// median primary-stat total per (inventory type, ilvl bucket). Self-calibrating
// and correct per slot, with no magic numbers to drift.
//
// Same shape as BuildQuestItemIndex in LivingGear_Vault.cpp.
// ---------------------------------------------------------------------
struct LgStats;
static LgStats ReadBaseStats(ItemTemplate const* proto);

uint32 const BUDGET_BUCKET = 5;      // ilvl granularity
uint32 const BUDGET_MAX_ILVL = 300;  // measured ceiling; above this we extrapolate

// [inventoryType][ilvl / BUDGET_BUCKET] -> median primary stat total
std::unordered_map<uint32, std::vector<float>> g_statBudget;
float g_budgetTopSlope = 2.4f;       // stats per ilvl past the measured ceiling

static void BuildStatBudget()
{
    g_statBudget.clear();
    // Median rather than mean: a handful of oddities (ilvl 100 sampled at 23.1
    // against 33.4 at ilvl 60) drag an average around badly at sparse levels.
    std::unordered_map<uint32, std::unordered_map<uint32, std::vector<float>>> samples;

    for (auto const& pair : *sObjectMgr->GetItemTemplateStore())
    {
        ItemTemplate const& proto = pair.second;
        if (proto.ItemLevel == 0 || proto.ItemLevel > BUDGET_MAX_ILVL)
            continue;
        if (proto.Class != ITEM_CLASS_ARMOR && proto.Class != ITEM_CLASS_WEAPON)
            continue;
        if (!proto.InventoryType)
            continue;
        LgStats st = ReadBaseStats(&proto);
        float const total = st.str + st.agi + st.sta + st.intel + st.spi;
        if (total <= 0.0f)
            continue;
        samples[proto.InventoryType][proto.ItemLevel / BUDGET_BUCKET].push_back(total);
    }

    uint32 slots = 0, buckets = 0;
    for (auto& slot : samples)
    {
        std::vector<float>& out = g_statBudget[slot.first];
        uint32 const highest = BUDGET_MAX_ILVL / BUDGET_BUCKET;
        out.assign(highest + 1, 0.0f);
        for (auto& bucket : slot.second)
        {
            std::vector<float>& v = bucket.second;
            std::sort(v.begin(), v.end());
            out[bucket.first] = v[v.size() / 2];
            ++buckets;
        }
        // Fill gaps by carrying the last known value forward, so a sparsely
        // populated slot never reports a budget of zero mid-curve, and force
        // the curve to be non-decreasing.
        //
        // Monotonic because a stat budget going DOWN as item level rises is
        // always sampling noise, never real: sparse buckets can have a median
        // dragged low by a couple of odd items. Left alone it produces an item
        // that gets weaker as it levels, which reads as a bug no matter how
        // honest the underlying data is.
        float last = 0.0f;
        for (float& v : out)
        {
            if (v < last)
                v = last;
            else
                last = v;
        }
        ++slots;
    }
    LOG_INFO("server.loading", "Living Gear: stat budget built from {} slot(s), {} ilvl bucket(s)",
        slots, buckets);
}

// Primary-stat budget for a slot at an item level, extrapolated past the
// measured ceiling using the slope at the top of the real data.
static float StatBudgetFor(uint32 inventoryType, float ilvl)
{
    auto const it = g_statBudget.find(inventoryType);
    if (it == g_statBudget.end() || it->second.empty())
        return 0.0f;
    std::vector<float> const& curve = it->second;
    if (ilvl <= 0.0f)
        return 0.0f;
    uint32 const bucket = uint32(ilvl) / BUDGET_BUCKET;
    if (bucket < curve.size())
        return curve[bucket];
    float const top = curve.back();
    float const over = ilvl - float((curve.size() - 1) * BUDGET_BUCKET);
    return top + over * g_budgetTopSlope;
}

static LgStats ReadBaseStats(ItemTemplate const* proto)
{
    LgStats s;
    if (!proto)
        return s;

    s.armor = static_cast<float>(proto->Armor);
    uint32 const count = proto->StatsCount < MAX_ITEM_PROTO_STATS ? proto->StatsCount : MAX_ITEM_PROTO_STATS;
    for (uint32 i = 0; i < count; ++i)
    {
        int32 const v = proto->ItemStat[i].ItemStatValue;
        switch (proto->ItemStat[i].ItemStatType)
        {
        case ITEM_MOD_STRENGTH:  s.str += static_cast<float>(v); break;
        case ITEM_MOD_AGILITY:   s.agi += static_cast<float>(v); break;
        case ITEM_MOD_STAMINA:   s.sta += static_cast<float>(v); break;
        case ITEM_MOD_INTELLECT: s.intel += static_cast<float>(v); break;
        case ITEM_MOD_SPIRIT:    s.spi += static_cast<float>(v); break;
        default: break;
        }
    }
    return s;
}

static LgStats GrownStats(ItemTemplate const* proto, LgItemState const& st)
{
    LgStats base = ReadBaseStats(proto);
    base.str += static_cast<float>(st.rollStr);
    base.agi += static_cast<float>(st.rollAgi);
    base.sta += static_cast<float>(st.rollSta);
    base.intel += static_cast<float>(st.rollInt);
    base.spi += static_cast<float>(st.rollSpi);

    uint16 levels = st.level;
    if (levels < 1)
        levels = 1;

    // Heirloom-style: levelling raises the item's EFFECTIVE item level, and its
    // stats become the budget for that level, kept in the item's own
    // proportions. At the default 5.4 ilvl per level a level-1 quest green
    // reaches ilvl 284 by level 50, and the formula keeps going past that.
    //
    // Convergence is intended: the same gain applies to everything, so a green
    // and a raid epic approach the same ceiling and the epic's advantage is
    // that it starts closer. An item you love stays viable forever.
    // Converge toward a shared ceiling rather than adding a fixed amount per
    // level. A fixed gain is NOT convergence -- I checked the numbers before
    // shipping this and a flat 5.4/level left a level-50 green at 417 stats
    // and a level-50 epic at 850, because the epic stays permanently ~180 ilvl
    // ahead. That is the "epics stay ahead" model, which is not the one
    // chosen.
    //
    // Interpolating toward IlvlCeiling means everything lands on the same
    // number at max level, and better gear simply STARTS closer to it. The
    // loot ladder decides how fast you get there, not where you end up.
    float const span = float(g_cfg.maxLevel > 1 ? g_cfg.maxLevel - 1 : 1);
    float const progress = std::min(1.0f, float(levels - 1) / span);
    float effectiveIlvl = float(proto->ItemLevel);
    if (g_cfg.ilvlCeiling > float(proto->ItemLevel))
        effectiveIlvl += (g_cfg.ilvlCeiling - float(proto->ItemLevel)) * progress;
    float const baseBudget = StatBudgetFor(proto->InventoryType, float(proto->ItemLevel));
    float const targetBudget = StatBudgetFor(proto->InventoryType, effectiveIlvl);

    float mult;
    if (baseBudget > 0.0f && targetBudget > 0.0f)
    {
        mult = targetBudget / baseBudget;
    }
    else
    {
        // No budget for this slot -- tabards, shirts, anything with no stats to
        // measure. Fall back to the old flat curve rather than zeroing the item.
        mult = 1.0f + static_cast<float>(levels - 1) * g_cfg.growthPerLevel;
    }
    if (mult < 1.0f)
        mult = 1.0f;

    base.str *= mult;
    base.agi *= mult;
    base.sta *= mult;
    base.intel *= mult;
    base.spi *= mult;
    base.armor *= mult;
    return base;
}

static LgStats WornDelta(ItemTemplate const* proto, LgItemState const& st)
{
    LgStats grown = GrownStats(proto, st);
    LgStats base = ReadBaseStats(proto);
    LgStats d;
    d.str = grown.str - base.str;
    d.agi = grown.agi - base.agi;
    d.sta = grown.sta - base.sta;
    d.intel = grown.intel - base.intel;
    d.spi = grown.spi - base.spi;
    d.armor = grown.armor - base.armor;
    return d;
}

// Levels 1..attuneCapLevel-1 (default 1-24): fast, item-level-scaled curve
// so attunement (see BankAttunement) reaches its 100% cap quickly -- a
// baseline (itemLevel == attuneIlvlBaseline, default 70) item should hit the
// cap in roughly 15-30 min of casual kill-grinding; lower-ilvl items scale
// down (floor at attuneIlvlFloorScale, default 15% of baseline cost) and
// higher-ilvl items scale up proportionally, so "max out attunement" stays
// a fast target for everyday leveling-through-content gear but a real
// investment for high-ilvl endgame pieces.
// Levels attuneCapLevel..maxLevel-1 (default 25-49): attunement is already
// at 100%, this only governs the item's own continued worn-stat growth, so
// it reverts to the original slower quadratic curve as a long-term chase.
static uint32 XpForNextLevel(uint16 level, uint32 itemLevel)
{
    if (level < g_cfg.attuneCapLevel)
    {
        float const scale = std::max(g_cfg.attuneIlvlFloorScale,
            static_cast<float>(itemLevel) / std::max(1.0f, g_cfg.attuneIlvlBaseline));
        float const base = std::ceil(static_cast<float>(level) / 2.0f);
        uint32 const cost = static_cast<uint32>(std::ceil(base * scale));
        return cost < 1 ? 1 : cost;
    }
    if (level < 10)
        return level;
    return (static_cast<uint32>(level) * static_cast<uint32>(level)) / 2;
}

// 1% at level 1, linear up to 100% at attuneCapLevel (default 25), then
// capped at 100% for any level beyond that.
static float AbsorbPctForLevel(uint16 level)
{
    if (level >= g_cfg.attuneCapLevel)
        return 1.0f;
    if (level < 1)
        level = 1;
    float const span = static_cast<float>(g_cfg.attuneCapLevel - 1);
    float const t = span > 0.0f ? static_cast<float>(level - 1) / span : 1.0f;
    return 0.01f + t * 0.99f;
}

// Non-destructive: mirrors `itemEntry`'s current grown stats (scaled by
// AbsorbPctForLevel) into the account's lg_absorb ratchet for that entry.
// Never destroys anything -- called automatically on every level-up (see
// AddItemXpAndBank) rather than as a manual one-time sacrifice.
// Milestones that raise the attunement rate.
//
// Every id was read out of var/mmap-output/dbc/Achievement.dbc, anchoring on a
// known title to find the name field (index 4 in this build) before trusting
// anything -- the same method that caught a wrong Hemorrhage rank earlier.
// None came from memory or a website.
//
// Two of the intended entries are absent from WotLK and are deliberately not
// faked: "Salty" and "Iron Chef" do not exist as achievements in this client.
// Accomplished Angler and Chef de Cuisine are the closest real equivalents and
// are used in their place.
//
// Faction-split achievements are listed as pairs; earning either counts.
uint32 AttuneRateFor(uint32 accountId);
static void RefreshStats(Player* player, bool includeBags = false);

struct AttuneMilestone { uint32 id; char const* label; };
AttuneMilestone const ATTUNE_MILESTONES[] =
{
    // Tier 1 -- the first week
    // Achievement 13 "Level 80" -- the one every character earns, verified in
    // Achievement.dbc. This was 457 "Realm First! Level 80", which only ONE
    // account on the realm can ever hold, so tier 1 was unreachable for
    // everybody else. Reported as #60, with the achievement link in it.
    {   13, "Level 80" },
    { 1288, "Northrend Dungeonmaster" },          // every WotLK normal dungeon
    { 1516, "Accomplished Angler" },              // stands in for Salty

    // Tier 2 -- committed
    { 1289, "Northrend Dungeon Hero" },           // every WotLK heroic
    { 1010, "Northrend Vanguard" },               // WotLK factions
    { 1799, "Chef de Cuisine" },                  // stands in for Iron Chef
    {  945, "The Argent Champion" },

    // Tier 3 -- dedicated
    { 1285, "Classic Raider" },
    { 1286, "Outland Raider" },
    { 1287, "Outland Dungeon Hero" },
    {  948, "Ambassador of the Alliance" },
    {  762, "Ambassador of the Horde" },
    {   42, "Explore Eastern Kingdoms" },
    {   43, "Explore Kalimdor" },
    {   44, "Explore Outland" },
    {   45, "Explore Northrend" },

    // Tier 4 -- the long haul and the wild ones
    { 2137, "Glory of the Raider (10 player)" },
    { 2138, "Glory of the Raider (25 player)" },
    { 2136, "Glory of the Hero" },
    { 2903, "Champion of Ulduar" },
    { 1658, "Champion of the Frozen Wastes" },
    { 2143, "Leading the Cavalry" },              // 100 mounts
    { 1681, "The Loremaster (Alliance)" },
    { 1682, "The Loremaster (Horde)" },
    { 2336, "Insane in the Membrane" },           // The Insane
    {  230, "Battlemaster" },
    {  907, "The Justicar" },
    {  714, "The Conqueror" },
};

// Award any milestone the account has earned but not yet banked.
//
// Each one raises the rate on everything ALREADY attuned, which is the whole
// snowball: collect broadly early, then every clear retroactively multiplies
// the lot. Checked on login and on achievement earned.
static void CheckAttuneMilestones(Player* player)
{
    if (!player || !player->GetSession())
        return;
    uint32 const accountId = player->GetSession()->GetAccountId();
    uint32 gained = 0;
    for (AttuneMilestone const& m : ATTUNE_MILESTONES)
    {
        if (!player->HasAchieved(m.id))
            continue;
        if (QueryResult have = CharacterDatabase.Query(
            "SELECT 1 FROM `lg_attune_milestone` WHERE `account_id` = {} AND `milestone` = {}",
            accountId, m.id))
            continue;
        CharacterDatabase.DirectExecute(
            "INSERT IGNORE INTO `lg_attune_milestone` (`account_id`, `milestone`, `earned_at`) "
            "VALUES ({}, {}, UNIX_TIMESTAMP())", accountId, m.id);
        ++gained;
        ChatHandler(player->GetSession()).PSendSysMessage(
            "|cff66ccff[Attune]|r Milestone earned: {}. Every attuned item is now worth more.",
            m.label);
    }
    // Sync every banked row to the account's current rate -- always, not only
    // when a milestone was just gained. Cheap, idempotent, and self-healing.
    //
    // This runs unconditionally because of what happened in 0.1.68: the
    // migration parked every existing row at 100 to avoid cutting anyone's
    // stats, and since the update only ever RAISED a row, 96 accounts sat
    // permanently above any achievable rate. Milestones could never move them
    // and new attunements arrived at 5% beside a 100% row. Keeping rows equal
    // to the rate in both directions means that class of drift cannot recur.
    uint32 const rate = AttuneRateFor(accountId);
    CharacterDatabase.DirectExecute(
        "UPDATE `lg_absorb` SET `attune_pct` = {} WHERE `account_id` = {} AND `attune_pct` <> {}",
        rate, accountId, rate);
    if (gained)
        RefreshStats(player, true);
}

// Rate per attuned item, as a percentage of that item's stats.
//
// Base 5, plus 5 for every milestone the account has earned, capped at 100.
// The milestones are the accelerator: each one raises the rate on everything
// ALREADY banked, so clearing content retroactively multiplies the whole
// collection. That is the snowball, and it is why milestone pacing rather than
// the base 5 is the tuning dial.
uint32 AttuneRateFor(uint32 accountId)
{
    uint32 rate = 5;
    if (QueryResult r = CharacterDatabase.Query(
        "SELECT COUNT(*) FROM `lg_attune_milestone` WHERE `account_id` = {}", accountId))
        rate += uint32((*r)[0].Get<uint64>()) * 5;
    return rate > 100 ? 100 : rate;
}

// Attune one item entry for an account. Returns false if the account already
// has it -- each unique item credits exactly once, which is the whole point:
// nothing is ever unfinishable and there is no farming treadmill.
bool AttuneItemEntry(Player* player, uint32 itemEntry)
{
    if (!player || !player->GetSession())
        return false;
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemEntry);
    if (!proto)
        return false;
    uint32 const accountId = player->GetSession()->GetAccountId();

    if (QueryResult prev = CharacterDatabase.Query(
        "SELECT 1 FROM `lg_absorb` WHERE `account_id` = {} AND `item_entry` = {}",
        accountId, itemEntry))
        return false;

    // Full stats are stored; the account receives stats * attune_pct / 100 at
    // apply time. Storing the full value means a milestone raising the rate is
    // one global change rather than a rewrite of every row.
    LgStats full = ReadBaseStats(proto);
    if (full.Total() <= 0.0f)
        return false;

    CharacterDatabase.DirectExecute(
        "REPLACE INTO `lg_absorb` (`account_id`, `item_entry`, `str`, `agi`, `sta`, "
        "`intel`, `spi`, `armor`, `item_level`, `attune_pct`) "
        "VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {}, {})",
        accountId, itemEntry, full.str, full.agi, full.sta, full.intel, full.spi,
        full.armor, proto->ItemLevel, AttuneRateFor(accountId));

    ::LivingGear_SendAddonLine(player, Acore::StringFormat("ATT|{}", itemEntry));
    return true;
}

static void BankAttunement(Player* player, uint32 itemEntry, LgItemState const& st)
{
    if (!player || !player->GetSession())
        return;
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemEntry);
    if (!proto)
        return;

    LgStats grown = GrownStats(proto, st);
    float const pct = AbsorbPctForLevel(st.level);
    LgStats absorb;
    absorb.str = grown.str * pct;
    absorb.agi = grown.agi * pct;
    absorb.sta = grown.sta * pct;
    absorb.intel = grown.intel * pct;
    absorb.spi = grown.spi * pct;
    absorb.armor = grown.armor * pct;

    uint32 const accountId = player->GetSession()->GetAccountId();
    float existingTotal = 0.0f;
    bool firstTime = true;
    if (QueryResult prev = CharacterDatabase.Query(
        "SELECT `str`, `agi`, `sta`, `intel`, `spi`, `armor` FROM `lg_absorb` "
        "WHERE `account_id` = {} AND `item_entry` = {}", accountId, itemEntry))
    {
        firstTime = false;
        Field* f = prev->Fetch();
        existingTotal = f[0].Get<float>() + f[1].Get<float>() + f[2].Get<float>()
            + f[3].Get<float>() + f[4].Get<float>() + f[5].Get<float>();
    }
    // Same ratchet as the old sacrifice path: never regress the account's
    // banked record for this item entry (a lower-level alt copy shouldn't
    // undo a better one already banked).
    if (absorb.Total() <= existingTotal + 0.01f)
        return;

    CharacterDatabase.DirectExecute(
        "REPLACE INTO `lg_absorb` (`account_id`, `item_entry`, `str`, `agi`, `sta`, "
        "`intel`, `spi`, `armor`, `item_level`) VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {})",
        accountId, itemEntry, absorb.str, absorb.agi, absorb.sta,
        absorb.intel, absorb.spi, absorb.armor, st.level);

    // Keep the client's attuned set live so the tooltip's ATTUNED line
    // appears the moment an item first attunes, rather than at next login.
    // Only on the row's first appearance -- every subsequent level-up
    // re-banks the same entry and the client already has it.
    if (firstTime)
        ::LivingGear_SendAddonLine(player, Acore::StringFormat("ATT|{}", itemEntry));
}

// Shared level-up path for both equipped gear (GrantKillXp) and
// Curator-tracked bag/bank/armory pieces (LivingGear_Perks.cpp TickCurator,
// via LivingGear_GrantItemXp below) -- one place that adds XP, rolls over
// levels against XpForNextLevel, and banks attunement on every level
// gained, so both paths behave identically. Returns true if it leveled up
// at least once. Does not save; caller saves after (GrantKillXp saves once
// per equipped slot per kill either way).
static bool AddItemXpAndBank(Player* player, uint32 itemLevel, uint32 xp, LgItemState& st)
{
    st.xp += xp;
    bool leveled = false;
    while (st.level < g_cfg.maxLevel && st.xp >= XpForNextLevel(st.level, itemLevel))
    {
        st.xp -= XpForNextLevel(st.level, itemLevel);
        ++st.level;
        leveled = true;
        // Levelling no longer banks attunement. Item level WAS the attunement
        // clock, which is why "wear it or file it" had no clean answer -- one
        // action did both jobs. Levelling now only makes the worn piece
        // stronger; attunement is earned by spending items. See
        // ATTUNEMENT-REDESIGN.md.
    }
    return leveled;
}

static bool LoadItemState(uint32 itemGuid, LgItemState& out)
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT `item_guid`, `item_entry`, `owner_guid`, `xp`, `level`, "
        "`roll_str`, `roll_agi`, `roll_sta`, `roll_int`, `roll_spi` "
        "FROM `lg_item` WHERE `item_guid` = {}", itemGuid);
    if (!result)
        return false;

    Field* f = result->Fetch();
    out.itemGuid = f[0].Get<uint32>();
    out.itemEntry = f[1].Get<uint32>();
    out.ownerGuid = f[2].Get<uint32>();
    out.xp = f[3].Get<uint32>();
    out.level = f[4].Get<uint16>();
    out.rollStr = f[5].Get<int32>();
    out.rollAgi = f[6].Get<int32>();
    out.rollSta = f[7].Get<int32>();
    out.rollInt = f[8].Get<int32>();
    out.rollSpi = f[9].Get<int32>();
    return true;
}

static void SaveItemState(LgItemState const& st)
{
    // Direct so the next Query in the same tick sees the new level.
    CharacterDatabase.DirectExecute(
        "REPLACE INTO `lg_item` (`item_guid`, `item_entry`, `owner_guid`, `xp`, `level`, "
        "`roll_str`, `roll_agi`, `roll_sta`, `roll_int`, `roll_spi`) "
        "VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {}, {})",
        st.itemGuid, st.itemEntry, st.ownerGuid, st.xp, st.level,
        st.rollStr, st.rollAgi, st.rollSta, st.rollInt, st.rollSpi);
}

static void EnsureItemState(Item* item, Player* player, LgItemState& st)
{
    uint32 const guid = item->GetGUID().GetCounter();
    if (LoadItemState(guid, st))
        return;

    st = {};
    st.itemGuid = guid;
    st.itemEntry = item->GetEntry();
    st.ownerGuid = player->GetGUID().GetCounter();
    st.level = 1;
    SaveItemState(st);
}

static int32 RollAmount(uint32 quality)
{
    switch (quality)
    {
    case ITEM_QUALITY_POOR:
    case ITEM_QUALITY_NORMAL: return static_cast<int32>(urand(1, 2));
    case ITEM_QUALITY_UNCOMMON: return static_cast<int32>(urand(2, 4));
    case ITEM_QUALITY_RARE: return static_cast<int32>(urand(3, 6));
    case ITEM_QUALITY_EPIC: return static_cast<int32>(urand(5, 10));
    default: return static_cast<int32>(urand(8, 15));
    }
}

static void TryRandomRoll(Item* item, Player* player)
{
    if (!g_cfg.enabled || !item || !player)
        return;

    ItemTemplate const* proto = item->GetTemplate();
    if (!IsEligible(proto))
        return;

    uint32 const guid = item->GetGUID().GetCounter();
    LgItemState existing;
    if (LoadItemState(guid, existing))
        return;

    LgItemState st;
    st.itemGuid = guid;
    st.itemEntry = item->GetEntry();
    st.ownerGuid = player->GetGUID().GetCounter();
    st.level = 1;

    if (roll_chance_f(g_cfg.rollChance))
    {
        bool used[5] = {};
        uint8 left = g_cfg.rollStatCount;
        while (left > 0)
        {
            uint8 idx = static_cast<uint8>(urand(0, 4));
            if (used[idx])
                continue;
            used[idx] = true;
            int32 amt = RollAmount(proto->Quality);
            if (idx == 0) st.rollStr = amt;
            else if (idx == 1) st.rollAgi = amt;
            else if (idx == 2) st.rollSta = amt;
            else if (idx == 3) st.rollInt = amt;
            else st.rollSpi = amt;
            --left;
        }

        ChatHandler(player->GetSession()).PSendSysMessage(
            "|cff66ccff[Living Gear]|r {} rolled bonus stats.", proto->Name1);
    }

    SaveItemState(st);
}

static void ApplyPrimary(Player* player, Stats stat, float amount, bool apply)
{
    if (amount <= 0.0f)
        return;
    // BASE_VALUE is what the character sheet green bonus reads (POSSTAT).
    // TOTAL_VALUE can change GetStat() but the paper doll ignores it.
    UnitMods const mod = UnitMods(UNIT_MOD_STAT_START + stat);
    player->HandleStatFlatModifier(mod, BASE_VALUE, amount, apply);
    player->UpdateStatBuffMod(stat);
}

static void ClearApplied(Player* player)
{
    uint32 const guid = player->GetGUID().GetCounter();
    auto it = g_applied.find(guid);
    if (it == g_applied.end())
        return;

    for (int i = 0; i < 5; ++i)
        ApplyPrimary(player, PRIMARY_STATS[i], it->second[i], false);
    if (it->second[5] > 0.0f)
    {
        player->HandleStatFlatModifier(UNIT_MOD_ARMOR, BASE_VALUE, it->second[5], false);
        player->UpdateArmor();
    }
    g_applied.erase(it);
}

static LgStats LoadAbsorbForPlayer(Player* player)
{
    LgStats total;
    uint32 const accountId = player->GetSession()->GetAccountId();
    uint8 const cls = player->getClass();
    uint8 const level = player->GetLevel();

    QueryResult result = CharacterDatabase.Query(
        "SELECT `item_entry`, `str`, `agi`, `sta`, `intel`, `spi`, `armor`, `attune_pct` "
        "FROM `lg_absorb` WHERE `account_id` = {}", accountId);
    if (!result)
        return total;

    do
    {
        Field* f = result->Fetch();
        uint32 const entry = f[0].Get<uint32>();
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
        if (!AccountCanUse(cls, proto))
            continue;
        if (proto && proto->RequiredLevel > level)
            continue;

        // Stored values are the item's FULL stats; the account receives the
        // attuned percentage of them. Keeping the full value in the row is what
        // lets a milestone raise the rate on everything already banked with a
        // single UPDATE instead of a twenty-thousand-row rewrite.
        float const pct = float(f[7].Get<uint16>()) / 100.0f;
        LgStats row;
        row.str = f[1].Get<float>();
        row.agi = f[2].Get<float>();
        row.sta = f[3].Get<float>();
        row.intel = f[4].Get<float>();
        row.spi = f[5].Get<float>();
        row.armor = f[6].Get<float>();
        row.str *= pct;
        row.agi *= pct;
        row.sta *= pct;
        row.intel *= pct;
        row.spi *= pct;
        row.armor *= pct;
        total += row;
    } while (result->NextRow());

    return total;
}

static std::string SanitizeAddonName(std::string name)
{
    for (char& c : name)
    {
        if (c == '|' || c == '\t' || c == '\n' || c == '\r' || c == ';')
            c = ' ';
    }
    if (name.size() > 36)
        name.resize(36);
    return name;
}

static void SendAddonLine(Player* player, std::string const& line)
{
    ::LivingGear_SendAddonLine(player, line);
}

// Account-wide UI scale (85-175%). Client sends SCALESET|<pct> when the
// player picks a size from the addon window's scale menu; this persists it
// so it's the same across every character on the account, matching the
// account-wide speed_cap/riding_skill pattern in LivingGear_Next.cpp.
uint32 const UI_SCALE_MIN = 85;
uint32 const UI_SCALE_MAX = 175;
uint32 const UI_SCALE_DEFAULT = 100;

bool g_scaleSchemaReady = false;
bool g_hasUiScaleCol = false;

void DetectScaleSchema()
{
    if (g_scaleSchemaReady)
        return;
    g_scaleSchemaReady = true;
    if (QueryResult cols = CharacterDatabase.Query(
        "SELECT `COLUMN_NAME` FROM `information_schema`.`COLUMNS` "
        "WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = 'lg_account_meta' AND `COLUMN_NAME` = 'ui_scale'"))
        g_hasUiScaleCol = cols->GetRowCount() > 0;
}

uint32 ClampUiScale(uint32 pct)
{
    if (pct < UI_SCALE_MIN)
        return UI_SCALE_MIN;
    if (pct > UI_SCALE_MAX)
        return UI_SCALE_MAX;
    return pct;
}

uint32 LoadUiScale(uint32 accountId)
{
    DetectScaleSchema();
    if (!g_hasUiScaleCol)
        return UI_SCALE_DEFAULT;
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT `ui_scale` FROM `lg_account_meta` WHERE `account_id` = {}", accountId))
        return ClampUiScale((*result)[0].Get<uint32>());
    return UI_SCALE_DEFAULT;
}

void SaveUiScale(uint32 accountId, uint32 pct)
{
    DetectScaleSchema();
    if (!g_hasUiScaleCol)
        return;
    pct = ClampUiScale(pct);
    CharacterDatabase.DirectExecute(
        "INSERT INTO `lg_account_meta` (`account_id`, `ui_scale`) VALUES ({}, {}) "
        "ON DUPLICATE KEY UPDATE `ui_scale` = {}",
        accountId, pct, pct);
}

// ---------------------------------------------------------------------
// Attuned-item set push (ATL| / ATT|).
//
// The Armory panel already knew which items were attuned, but only as its
// own on-demand ARM| listing -- nothing the tooltip code could consult. So
// the one question a player actually asks while looking at a drop, "do I
// already have this attuned?", had no answer anywhere in the UI and the
// only way to find out was to open the Armory and scroll.
//
// Pushed as the bare entry ids, batched, for the same reason PKALL is
// batched (LivingGear_Perks.cpp SendPerkSync): the WotLK addon-whisper
// channel truncates somewhere around 255 bytes and a mature account has
// well over a thousand attuned entries, so one line would silently lose
// most of them. The client's handler is purely additive, so splitting is
// safe. Names are deliberately not sent -- the client already has the item
// in its own cache by the time it is rendering a tooltip for it.
// ---------------------------------------------------------------------
static void SendAttunedSet(Player* player)
{
    if (!player || !player->GetSession())
        return;
    QueryResult result = CharacterDatabase.Query(
        "SELECT `item_entry` FROM `lg_absorb` WHERE `account_id` = {}",
        player->GetSession()->GetAccountId());
    if (!result)
        return;
    std::string ids;
    do
    {
        std::string next = std::to_string((*result)[0].Get<uint32>());
        if (!ids.empty() && ids.size() + 1 + next.size() > 200)
        {
            SendAddonLine(player, "ATL|" + ids);
            ids.clear();
        }
        if (!ids.empty())
            ids += ',';
        ids += next;
    } while (result->NextRow());
    if (!ids.empty())
        SendAddonLine(player, "ATL|" + ids);
}

static void SendLivingItem(Player* player, Item* item, std::string const& loc)
{
    if (!player || !item)
        return;
    ItemTemplate const* proto = item->GetTemplate();
    if (!IsEligible(proto))
        return;

    LgItemState st;
    EnsureItemState(item, player, st);
    LgStats delta = WornDelta(proto, st);
    uint32 need = st.level >= g_cfg.maxLevel ? 0 : XpForNextLevel(st.level, proto->ItemLevel);
    SendAddonLine(player, Acore::StringFormat(
        "ITM|{}|{}|{}|{}|{}|{:.0f}|{:.0f}|{:.0f}|{:.0f}|{:.0f}|{:.0f}|{}|{}|{}|{}|{}",
        loc, SanitizeAddonName(proto->Name1), st.level, st.xp, need,
        delta.str, delta.agi, delta.sta, delta.intel, delta.spi, delta.armor,
        st.rollStr, st.rollAgi, st.rollSta, st.rollInt, st.rollSpi));
}

static void SendBagLivingItems(Player* player)
{
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
    {
        Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item)
            continue;
        uint32 const clientSlot = static_cast<uint32>(slot - INVENTORY_SLOT_ITEM_START + 1);
        SendLivingItem(player, item, Acore::StringFormat("bag|0|{}", clientSlot));
    }

    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        Bag* container = player->GetBagByPos(bag);
        if (!container)
            continue;
        uint32 const clientBag = static_cast<uint32>(bag - INVENTORY_SLOT_BAG_START + 1);
        for (uint8 slot = 0; slot < container->GetBagSize(); ++slot)
        {
            Item* item = container->GetItemByPos(slot);
            if (!item)
                continue;
            SendLivingItem(player, item, Acore::StringFormat("bag|{}|{}", clientBag, slot + 1));
        }
    }
}

static void SendAddonSync(Player* player, bool includeBags = true)
{
    if (!player || !g_cfg.enabled || !player->GetSession())
        return;

    SendAddonLine(player, "CLR");
    ::SendVaultAndRuleSync(player);
    ::SendAutolootSync(player);
    // These three used to be pushed at login and nowhere else, so a client
    // REQ -- which the addon fires on /reload and on every re-sync --
    // answered with the vault and attunement state only. db.perks,
    // db.classPerks and the toggles stayed empty, and since the addon gates
    // its buttons on PerkKnown(), half the panel simply rendered as locked
    // until the player logged all the way out and back in.
    ::LivingGear_SendPerksSync(player);
    ::LivingGear_SendClassPerksSync(player);
    ::LivingGear_SendNextSync(player);
    ::LivingGear_SendWayfarerSync(player);

    LgStats absorb = LoadAbsorbForPlayer(player);
    uint32 count = 0;
    QueryResult countResult = CharacterDatabase.Query(
        "SELECT COUNT(*) FROM `lg_absorb` WHERE `account_id` = {}",
        player->GetSession()->GetAccountId());
    if (countResult)
        count = (*countResult)[0].Get<uint32>();

    SendAddonLine(player, Acore::StringFormat("ABS|{:.1f}|{:.1f}|{:.1f}|{:.1f}|{:.1f}|{:.1f}|{}",
        absorb.str, absorb.agi, absorb.sta, absorb.intel, absorb.spi, absorb.armor, count));

    {
        uint32 autoOn = 1;
        uint32 autoOff = 0;
        if (QueryResult q = CharacterDatabase.Query(
            "SELECT `auto_attune_on`, `auto_attune_off` FROM `lg_account_meta` WHERE `account_id` = {}",
            player->GetSession()->GetAccountId()))
        {
            autoOn = (*q)[0].Get<uint8>();
            autoOff = (*q)[1].Get<uint32>();
        }
        SendAddonLine(player, Acore::StringFormat("AA|{}|{}|{}", autoOn, count, autoOff));
    }

    SendAttunedSet(player);

    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item)
            continue;
        SendLivingItem(player, item, Acore::StringFormat("inv|{}", slot));
    }

    if (includeBags)
        SendBagLivingItems(player);

    if (player->GetSession())
        SendAddonLine(player, Acore::StringFormat("SCALE|{}",
            LoadUiScale(player->GetSession()->GetAccountId())));

    SendAddonLine(player, "END");
}

static bool HandleTipRequest(Player* player, std::string const& raw)
{
    std::string msg = raw;
    if (msg.rfind("LG\t", 0) == 0)
        msg = msg.substr(3);
    if (msg.rfind("TIPREQ|", 0) != 0)
        return false;

    std::string const rest = msg.substr(7);
    Item* item = nullptr;
    std::string loc;
    if (rest.rfind("inv|", 0) == 0)
    {
        uint32 slot = 0;
        if (sscanf(rest.c_str(), "inv|%u", &slot) != 1 || slot >= EQUIPMENT_SLOT_END)
            return true;
        item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, static_cast<uint8>(slot));
        loc = Acore::StringFormat("inv|{}", slot);
    }
    else if (rest.rfind("bag|", 0) == 0)
    {
        uint32 bag = 0;
        uint32 slot = 0;
        if (sscanf(rest.c_str(), "bag|%u|%u", &bag, &slot) != 2 || slot == 0)
            return true;
        if (bag == 0)
        {
            uint8 const serverSlot = static_cast<uint8>(INVENTORY_SLOT_ITEM_START + slot - 1);
            if (serverSlot >= INVENTORY_SLOT_ITEM_END)
                return true;
            item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, serverSlot);
        }
        else
        {
            uint8 const serverBag = static_cast<uint8>(INVENTORY_SLOT_BAG_START + bag - 1);
            if (serverBag >= INVENTORY_SLOT_BAG_END)
                return true;
            item = player->GetItemByPos(serverBag, static_cast<uint8>(slot - 1));
        }
        loc = Acore::StringFormat("bag|{}|{}", bag, slot);
    }
    else
        return true;

    if (item)
        SendLivingItem(player, item, loc);
    return true;
}

static void RefreshStats(Player* player, bool includeBags)
{
    if (!player || !g_cfg.enabled)
        return;

    ClearApplied(player);

    LgStats sum = LoadAbsorbForPlayer(player);

    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item)
            continue;
        ItemTemplate const* proto = item->GetTemplate();
        if (!IsEligible(proto))
            continue;

        LgItemState st;
        EnsureItemState(item, player, st);
        LgStats delta = WornDelta(proto, st);
        sum += delta;
    }

    std::array<float, 6> stored = {
        sum.str, sum.agi, sum.sta, sum.intel, sum.spi, sum.armor
    };

    for (int i = 0; i < 5; ++i)
        ApplyPrimary(player, PRIMARY_STATS[i], stored[i], true);
    if (stored[5] > 0.0f)
    {
        player->HandleStatFlatModifier(UNIT_MOD_ARMOR, BASE_VALUE, stored[5], true);
        player->UpdateArmor();
    }

    g_applied[player->GetGUID().GetCounter()] = stored;
    player->UpdateAllStats();
    SendAddonSync(player, includeBags);
}

static void SendWornExtras(ChatHandler* handler, Player* player)
{
    LgStats sum;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item || !IsEligible(item->GetTemplate()))
            continue;
        LgItemState st;
        EnsureItemState(item, player, st);
        sum += WornDelta(item->GetTemplate(), st);
    }
    LgStats absorb = LoadAbsorbForPlayer(player);
    handler->PSendSysMessage(
        "Worn extras: +{:.0f} str / +{:.0f} agi / +{:.0f} sta / +{:.0f} int / +{:.0f} spi / +{:.0f} armor",
        sum.str, sum.agi, sum.sta, sum.intel, sum.spi, sum.armor);
    handler->PSendSysMessage(
        "Account absorb: +{:.0f} str / +{:.0f} agi / +{:.0f} sta / +{:.0f} int / +{:.0f} spi / +{:.0f} armor",
        absorb.str, absorb.agi, absorb.sta, absorb.intel, absorb.spi, absorb.armor);
}

static void GrantKillXp(Player* player, Creature* killed)
{
    if (!g_cfg.enabled || !player || !killed)
        return;
    if (killed->IsCritter() || killed->IsPet() || killed->IsTotem())
        return;

    uint32 xp = g_cfg.xpPerKill;
    if (killed->IsDungeonBoss() || killed->isWorldBoss())
        xp = g_cfg.xpBossKill;
    else if (killed->isElite())
        xp = g_cfg.xpEliteKill;

    bool anyLevel = false;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item || !IsEligible(item->GetTemplate()))
            continue;

        LgItemState st;
        EnsureItemState(item, player, st);
        if (st.level >= g_cfg.maxLevel)
            continue;

        if (AddItemXpAndBank(player, item->GetTemplate()->ItemLevel, xp, st))
        {
            anyLevel = true;
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cff66ccff[Living Gear]|r {} reached level {}.",
                item->GetTemplate()->Name1, st.level);
        }
        SaveItemState(st);
    }

    if (anyLevel)
        RefreshStats(player);
    else
        SendAddonSync(player);
}

static uint8 ParseSlot(std::string const& name)
{
    if (name == "head") return EQUIPMENT_SLOT_HEAD;
    if (name == "neck") return EQUIPMENT_SLOT_NECK;
    if (name == "shoulders") return EQUIPMENT_SLOT_SHOULDERS;
    if (name == "chest") return EQUIPMENT_SLOT_CHEST;
    if (name == "waist") return EQUIPMENT_SLOT_WAIST;
    if (name == "legs") return EQUIPMENT_SLOT_LEGS;
    if (name == "feet") return EQUIPMENT_SLOT_FEET;
    if (name == "wrists") return EQUIPMENT_SLOT_WRISTS;
    if (name == "hands") return EQUIPMENT_SLOT_HANDS;
    if (name == "finger1") return EQUIPMENT_SLOT_FINGER1;
    if (name == "finger2") return EQUIPMENT_SLOT_FINGER2;
    if (name == "trinket1") return EQUIPMENT_SLOT_TRINKET1;
    if (name == "trinket2") return EQUIPMENT_SLOT_TRINKET2;
    if (name == "back") return EQUIPMENT_SLOT_BACK;
    if (name == "mainhand") return EQUIPMENT_SLOT_MAINHAND;
    if (name == "offhand") return EQUIPMENT_SLOT_OFFHAND;
    if (name == "ranged") return EQUIPMENT_SLOT_RANGED;
    return EQUIPMENT_SLOT_END;
}

static bool SacrificeItem(Player* player, Item* item, ChatHandler* handler)
{
    if (!player || !item)
        return false;

    ItemTemplate const* proto = item->GetTemplate();
    if (!IsEligible(proto))
    {
        handler->PSendSysMessage("|cffff6666[Living Gear]|r That item cannot be attuned.");
        return false;
    }

    LgItemState st;
    EnsureItemState(item, player, st);
    LgStats grown = GrownStats(proto, st);
    LgStats absorb;
    absorb.str = grown.str * g_cfg.absorbPct;
    absorb.agi = grown.agi * g_cfg.absorbPct;
    absorb.sta = grown.sta * g_cfg.absorbPct;
    absorb.intel = grown.intel * g_cfg.absorbPct;
    absorb.spi = grown.spi * g_cfg.absorbPct;
    absorb.armor = grown.armor * g_cfg.absorbPct;

    uint32 const accountId = player->GetSession()->GetAccountId();
    float existingTotal = 0.0f;
    QueryResult prev = CharacterDatabase.Query(
        "SELECT `str`, `agi`, `sta`, `intel`, `spi`, `armor` FROM `lg_absorb` "
        "WHERE `account_id` = {} AND `item_entry` = {}", accountId, st.itemEntry);
    if (prev)
    {
        Field* f = prev->Fetch();
        existingTotal = f[0].Get<float>() + f[1].Get<float>() + f[2].Get<float>()
            + f[3].Get<float>() + f[4].Get<float>() + f[5].Get<float>();
    }

    // Bail on equal-or-weaker, not just strictly weaker: an Armory-recreated
    // item is by definition the exact same entry the account already has
    // attuned, so without this an item pulled out of the Armory to wear
    // would immediately get auto-attuned (and destroyed) right back if
    // auto-attune is on -- a recreate-then-instantly-lose loop.
    if (absorb.Total() <= existingTotal + 0.01f)
    {
        handler->PSendSysMessage(
            "|cffffcc00[Living Gear]|r A copy of this item at least as strong is already "
            "attuned. Sacrificing this one would do nothing. Item kept.");
        return false;
    }

    CharacterDatabase.DirectExecute(
        "REPLACE INTO `lg_absorb` (`account_id`, `item_entry`, `str`, `agi`, `sta`, "
        "`intel`, `spi`, `armor`, `item_level`) VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {})",
        accountId, st.itemEntry, absorb.str, absorb.agi, absorb.sta,
        absorb.intel, absorb.spi, absorb.armor, st.level);
    CharacterDatabase.DirectExecute("DELETE FROM `lg_item` WHERE `item_guid` = {}", st.itemGuid);

    std::string const name = proto->Name1;
    player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);

    handler->PSendSysMessage(
        "|cff66ccff[Living Gear]|r {} was attuned and destroyed. Account gained "
        "+{:.0f} str / +{:.0f} agi / +{:.0f} sta / +{:.0f} int / +{:.0f} spi / +{:.0f} armor.",
        name, absorb.str, absorb.agi, absorb.sta, absorb.intel, absorb.spi, absorb.armor);
    RefreshStats(player);
    return true;
}

static bool HandleAttuneMessage(Player* player, std::string const& raw)
{
    std::string msg = raw;
    if (msg.rfind("LG\t", 0) == 0)
        msg = msg.substr(3);
    // Bulk attune: spend every eligible item in the bags for a permanent
    // account slice each.
    //
    // Deliberate rather than automatic. Attuning CONSUMES the item, and
    // auto-attuning what you loot would destroy gear before the player had
    // decided whether to wear it -- strictly worse than the armory round-trip
    // it was meant to save. Equipped gear is never touched: you cannot lose
    // what you are wearing to a mis-click.
    if (msg == "ATTUNEALL")
    {
        ChatHandler h(player->GetSession());
        uint32 attuned = 0, skipped = 0;
        // Collect first -- DestroyItem inside a bag walk invalidates it.
        std::vector<ObjectGuid> spend;
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            if (Item* it = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                spend.push_back(it->GetGUID());
        for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
            if (Bag* container = player->GetBagByPos(bag))
                for (uint32 i = 0; i < container->GetBagSize(); ++i)
                    if (Item* it = container->GetItemByPos(uint8(i)))
                        spend.push_back(it->GetGUID());

        for (ObjectGuid guid : spend)
        {
            Item* it = player->GetItemByGuid(guid);
            if (!it)
                continue;
            ItemTemplate const* proto = it->GetTemplate();
            if (!proto || !IsEligible(proto))
                continue;
            if (AttuneItemEntry(player, proto->ItemId))
            {
                player->DestroyItem(it->GetBagSlot(), it->GetSlot(), true);
                ++attuned;
            }
            else
                ++skipped;      // account already has this entry
        }

        if (attuned)
        {
            RefreshStats(player, true);
            h.PSendSysMessage("|cff66ccff[Attune]|r Attuned {} item(s). {} already known.",
                attuned, skipped);
        }
        else
            h.PSendSysMessage("|cff66ccff[Attune]|r Nothing new to attune ({} already known).",
                skipped);
        return true;
    }

    if (msg.rfind("ATTUNE|", 0) != 0)
        return false;

    std::string const slotArg = msg.substr(7);
    uint8 id = EQUIPMENT_SLOT_END;
    if (!slotArg.empty() && slotArg[0] >= '0' && slotArg[0] <= '9')
    {
        uint32 parsed = 0;
        for (char const c : slotArg)
        {
            if (c < '0' || c > '9')
                break;
            parsed = parsed * 10 + static_cast<uint32>(c - '0');
        }
        if (parsed < EQUIPMENT_SLOT_END)
            id = static_cast<uint8>(parsed);
    }
    else
        id = ParseSlot(slotArg);

    ChatHandler handler(player->GetSession());
    if (id >= EQUIPMENT_SLOT_END)
    {
        handler.PSendSysMessage("|cffff6666[Living Gear]|r Unknown slot.");
        return true;
    }

    Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, id);
    if (!item)
    {
        handler.PSendSysMessage("|cffff6666[Living Gear]|r Nothing in that slot.");
        return true;
    }

    SacrificeItem(player, item, &handler);
    return true;
}

// Automatically attunes a newly-acquired item if the account has auto-attune
// on and this item's quality isn't excluded. Safe to be this aggressive
// because attuning isn't a true loss: the Attuned Armory (SPELL_ARMORY,
// 910091) lets the player pull an equippable copy of anything they've
// attuned back out whenever they actually want to wear it -- attuning just
// converts the physical item into permanent account stats up front instead
// of it sitting in a bag. SacrificeItem() itself already declines (with a
// chat message, no destruction) when a stronger copy of the same item is
// already attuned, so this can't make things worse by re-triggering on
// duplicate lower-value drops.
// Deferred the same way as LivingGear_Vault.cpp's autoloot-rule redirect:
// SacrificeItem() destroys the item via Player::DestroyItem(), and calling
// that from inside OnPlayerStoreNewItem is a use-after-free -- the item is
// still ITEM_NEW at that point (never saved), so Item::SetState(ITEM_REMOVED)
// takes its "pretend it never existed" branch and does `delete this`
// immediately, while StoreLootItem (the caller of StoreNewItem, which is
// what fires this hook) still holds that pointer and uses it right after
// to send the loot notification. Crashed the server (SIGSEGV in
// Player::SendNewItem -> Item::GetCount, 2026-08-20). Only the read-only
// eligibility/toggle checks run synchronously here.
// RETIRED 2026-08-23. Attuning consumes the item now, and auto-attuning what
// you loot would destroy gear before the player had decided whether to wear
// it -- strictly worse than the armory round-trip it was invented to avoid.
// Attuning is a deliberate act: the Armory's Attune All button, or nothing.
//
// Left as an early return rather than deleted so the hook, the account
// columns and the client's toggle all keep working harmlessly until they are
// removed in their own change. Deleting it here would mean touching six files
// in a commit that is already large.
static void TryAutoAttune(Player* /*player*/, Item* /*item*/)
{
    return;
}

static void TryAutoAttuneRetired(Player* player, Item* item)
{
    if (!g_cfg.enabled || !player || !item || !player->GetSession())
        return;
    ItemTemplate const* proto = item->GetTemplate();
    if (!IsEligible(proto))
        return;
    uint32 const accountId = player->GetSession()->GetAccountId();
    uint32 autoOn = 1;
    uint32 autoOff = 0;
    if (QueryResult q = CharacterDatabase.Query(
        "SELECT `auto_attune_on`, `auto_attune_off` FROM `lg_account_meta` WHERE `account_id` = {}", accountId))
    {
        autoOn = (*q)[0].Get<uint8>();
        autoOff = (*q)[1].Get<uint32>();
    }
    if (!autoOn)
        return;
    if (proto->Quality < 32 && (autoOff & (1u << proto->Quality)))
        return;

    ObjectGuid playerGuid = player->GetGUID();
    ObjectGuid itemGuid = item->GetGUID();
    uint32 const entry = proto->ItemId;
    player->m_Events.AddEventAtOffset([playerGuid, itemGuid, entry]()
    {
        Player* p = ObjectAccessor::FindPlayer(playerGuid);
        if (!p || !p->IsInWorld() || !p->GetSession())
            return;
        Item* i = p->GetItemByGuid(itemGuid);
        if (!i || i->GetEntry() != entry)
            return;
        ChatHandler handler(p->GetSession());
        SacrificeItem(p, i, &handler);
    }, std::chrono::milliseconds(1));
}

static bool HandleAutoAttuneSet(Player* player, std::string const& raw)
{
    std::string msg = raw;
    if (msg.rfind("LG\t", 0) == 0)
        msg = msg.substr(3);
    if (msg.rfind("AASET|", 0) != 0 || !player || !player->GetSession())
        return false;

    uint32 const accountId = player->GetSession()->GetAccountId();
    std::string const rest = msg.substr(6);
    uint32 v = 0;
    if (sscanf(rest.c_str(), "on|%u", &v) == 1)
    {
        CharacterDatabase.DirectExecute(
            "INSERT INTO `lg_account_meta` (`account_id`, `auto_attune_on`) VALUES ({}, {}) "
            "ON DUPLICATE KEY UPDATE `auto_attune_on` = {}",
            accountId, v ? 1 : 0, v ? 1 : 0);
        SendAddonSync(player, false);
        return true;
    }
    uint32 quality = 0;
    if (sscanf(rest.c_str(), "q|%u|%u", &quality, &v) == 2 && quality < 32)
    {
        uint32 mask = 0;
        if (QueryResult q = CharacterDatabase.Query(
            "SELECT `auto_attune_off` FROM `lg_account_meta` WHERE `account_id` = {}", accountId))
            mask = (*q)[0].Get<uint32>();
        if (v)
            mask |= (1u << quality);
        else
            mask &= ~(1u << quality);
        CharacterDatabase.DirectExecute(
            "INSERT INTO `lg_account_meta` (`account_id`, `auto_attune_off`) VALUES ({}, {}) "
            "ON DUPLICATE KEY UPDATE `auto_attune_off` = {}",
            accountId, mask, mask);
        SendAddonSync(player, false);
        return true;
    }
    return false;
}

// Returns true if some handler recognised and consumed `raw`. Callers
// translate that into "suppress the whisper".
bool DispatchAddonCommand(Player* player, std::string const& raw)
{
    // Our own outgoing lines arrive back through this very hook, because a
    // server->client addon line IS a self-whisper (LivingGear_SendAddonLine).
    // Never parse one as a command, and never report it as handled: the
    // hook returning false makes Player::Whisper drop the packet before it
    // is built, so "handled" here means "the client never gets this line".
    if (::LivingGear_IsAddonSendInProgress())
        return false;

    std::string msg = raw;
    bool const addressed = msg.rfind("LG\t", 0) == 0;
    if (addressed)
        msg = msg.substr(3);

    if (msg == "REQ")
    {
        SendAddonSync(player, true);
        return true;
    }
    uint32 pct = 0;
    if (sscanf(msg.c_str(), "SCALESET|%u", &pct) == 1 && player->GetSession())
    {
        SaveUiScale(player->GetSession()->GetAccountId(), pct);
        SendAddonSync(player, true);
        return true;
    }
    if (HandleTipRequest(player, msg))
        return true;
    if (HandleAttuneMessage(player, msg))
        return true;
    if (HandleAutoAttuneSet(player, msg))
        return true;
    if (::LivingGear_HandleVaultCommand(player, msg))
        return true;
    if (::LivingGear_HandlePerksCommand(player, msg))
        return true;
    if (::LivingGear_HandleClassPerksCommand(player, msg))
        return true;
    if (::LivingGear_HandleNextCommand(player, msg))
        return true;
    if (::LivingGear_HandleSupportCommand(player, msg))
        return true;
    if (::LivingGear_HandleAmenitiesCommand(player, msg))
        return true;

    // Every "this button does nothing" report against this module has
    // turned out to be a client command with no server handler -- TAKE|
    // (vault withdraw, never implemented at all), JMPSET|, SCAP| (gated on
    // an unstripped prefix), CLASS| (listening for a format the client
    // never sent). Not one of them left a trace anywhere. They all do now.
    if (addressed)
        LOG_ERROR("module.livinggear",
            "Living Gear: no handler for addon command '{}' (from {}). "
            "The client sends it and nothing on the server is listening.",
            msg, player->GetName());
    return false;
}

class LivingGearPlayer : public PlayerScript
{
public:
    LivingGearPlayer() : PlayerScript("LivingGearPlayer", {
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_ACHI_COMPLETE,
        PLAYERHOOK_ON_LOGOUT,
        PLAYERHOOK_ON_CREATURE_KILL,
        PLAYERHOOK_ON_CREATURE_KILLED_BY_PET,
        PLAYERHOOK_ON_STORE_NEW_ITEM,
        PLAYERHOOK_ON_CREATE_ITEM,
        PLAYERHOOK_ON_QUEST_REWARD_ITEM,
        PLAYERHOOK_ON_GROUP_ROLL_REWARD_ITEM,
        PLAYERHOOK_ON_EQUIP,
        PLAYERHOOK_ON_UNEQUIP_ITEM,
        PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT,
        PLAYERHOOK_ON_SPELL_CAST
    }) { }

    void OnPlayerLogin(Player* player) override
    {
        if (!g_cfg.enabled)
            return;
        if (!player->HasSpell(SPELL_WINDBLOWN))
            player->learnSpell(SPELL_WINDBLOWN);
        // Before RefreshStats, so a milestone earned offline (or on another
        // character) is already reflected in the stats applied on login.
        CheckAttuneMilestones(player);
        RefreshStats(player, true);
    }

    void OnPlayerAchievementComplete(Player* player, AchievementEntry const* /*achievement*/) override
    {
        if (g_cfg.enabled)
            CheckAttuneMilestones(player);
    }

    void OnPlayerLogout(Player* player) override
    {
        g_applied.erase(player->GetGUID().GetCounter());
    }

    void OnPlayerCreatureKill(Player* killer, Creature* killed) override
    {
        GrantKillXp(killer, killed);
    }

    void OnPlayerCreatureKilledByPet(Player* owner, Creature* killed) override
    {
        GrantKillXp(owner, killed);
    }

    void OnPlayerStoreNewItem(Player* player, Item* item, uint32 /*count*/) override
    {
        TryRandomRoll(item, player);
        TryAutoAttune(player, item);
    }

    void OnPlayerCreateItem(Player* player, Item* item, uint32 /*count*/) override
    {
        TryRandomRoll(item, player);
    }

    void OnPlayerQuestRewardItem(Player* player, Item* item, uint32 /*count*/) override
    {
        TryRandomRoll(item, player);
    }

    void OnPlayerGroupRollRewardItem(Player* player, Item* item, uint32 /*count*/,
        RollVote /*voteType*/, Roll* /*roll*/) override
    {
        TryRandomRoll(item, player);
    }

    void OnPlayerEquip(Player* player, Item* /*it*/, uint8 /*bag*/, uint8 /*slot*/, bool /*update*/) override
    {
        if (g_cfg.enabled)
            RefreshStats(player);
    }

    void OnPlayerUnequip(Player* player, Item* /*it*/) override
    {
        if (g_cfg.enabled)
            RefreshStats(player);
    }

    void OnPlayerSpellCast(Player* player, Spell* spell, bool /*skipCheck*/) override
    {
        if (!g_cfg.enabled || !player || !spell)
            return;
        if (spell->GetSpellInfo()->Id != SPELL_WINDBLOWN)
            return;
        SendAddonLine(player, "OPEN");
        SendAddonSync(player);
    }

    // The single addon-command entry point for the entire module.
    //
    // ScriptMgr's private-chat hook is a boolean hook, and
    // CALL_ENABLED_BOOLEAN_HOOKS (ScriptMgrMacros.h) stops at the FIRST
    // script that returns false -- it never consults the rest. This module
    // used to register that hook five separate times (LivingGear, Next,
    // Perks, ClassPerks, Vault), which meant the routing table for the
    // whole addon protocol was "whatever order AddSC_* happens to run in":
    // any prefix collision silently killed whichever module registered
    // later, and a module returning false for a message it had not really
    // acted on suppressed the whisper for everyone downstream -- which is
    // exactly how every outgoing Vault sync line got dropped for a whole
    // session. One hook, one explicit ordered list, one place that strips
    // the "LG<tab>" prefix.
    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 language, std::string& msg, Player* /*receiver*/) override
    {
        if (!player || language != LANG_ADDON || type != CHAT_MSG_WHISPER)
            return true;
        return !DispatchAddonCommand(player, msg);
    }
};

class LivingGearWorld : public WorldScript
{
public:
    LivingGearWorld() : WorldScript("LivingGearWorld", {
        WORLDHOOK_ON_AFTER_CONFIG_LOAD,
        WORLDHOOK_ON_STARTUP
    }) { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        g_cfg.Load();
    }

    void OnStartup() override
    {
        BuildStatBudget();
        if (g_cfg.enabled)
            LOG_INFO("module", "Living Gear enabled (max level {}, absorb {:.0f}%)",
                g_cfg.maxLevel, g_cfg.absorbPct * 100.0f);
    }
};

using namespace Acore::ChatCommands;

class LivingGearCommands : public CommandScript
{
public:
    LivingGearCommands() : CommandScript("LivingGearCommands") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable lgTable =
        {
            { "status", HandleStatus, rbac::RBAC_PERM_COMMAND_HELP, Console::No },
            { "givexp", HandleGiveXp, rbac::RBAC_PERM_COMMAND_GM, Console::No },
            { "attune", HandleAttune, rbac::RBAC_PERM_COMMAND_HELP, Console::No },
            { "reset",  HandleReset,  rbac::RBAC_PERM_COMMAND_GM, Console::No },
            { "diag",   HandleDiag,   rbac::RBAC_PERM_COMMAND_HELP, Console::No },
        };
        static ChatCommandTable commandTable =
        {
            { "lg", lgTable }
        };
        return commandTable;
    }

    // Answers "did this code actually run for me", which is the one question
    // reading the source cannot answer and the one that three consecutive bug
    // reports turned on. Deliberately available to players, not GM-only: the
    // person who can reproduce the bug is the person who should be able to
    // read the counters.
    static bool HandleDiag(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;
        ::LivingGear_ShowDiagnostics(player);
        return true;
    }

    static bool HandleStatus(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;

        handler->PSendSysMessage("|cff66ccff[Living Gear]|r Equipped:");
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (!item || !IsEligible(item->GetTemplate()))
                continue;

            LgItemState st;
            EnsureItemState(item, player, st);
            uint32 need = st.level >= g_cfg.maxLevel ? 0 : XpForNextLevel(st.level, item->GetTemplate()->ItemLevel);
            handler->PSendSysMessage("  {}  lv {}  xp {}/{}  roll +{}/{}/{}/{}/{}",
                item->GetTemplate()->Name1, st.level, st.xp, need,
                st.rollStr, st.rollAgi, st.rollSta, st.rollInt, st.rollSpi);
        }

        QueryResult result = CharacterDatabase.Query(
            "SELECT COUNT(*), COALESCE(SUM(`str`+`agi`+`sta`+`intel`+`spi`+`armor`), 0) "
            "FROM `lg_absorb` WHERE `account_id` = {}", player->GetSession()->GetAccountId());
        uint32 count = 0;
        float pile = 0.0f;
        if (result)
        {
            count = (*result)[0].Get<uint32>();
            pile = (*result)[1].Get<float>();
        }
        handler->PSendSysMessage("Account attunements: {}  absorb pile: {:.1f}", count, pile);
        RefreshStats(player);
        SendWornExtras(handler, player);
        return true;
    }

    static bool HandleGiveXp(ChatHandler* handler, Optional<uint32> amount)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;

        uint32 xp = amount.value_or(200);
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (!item || !IsEligible(item->GetTemplate()))
                continue;

            LgItemState st;
            EnsureItemState(item, player, st);
            AddItemXpAndBank(player, item->GetTemplate()->ItemLevel, xp, st);
            SaveItemState(st);
        }
        RefreshStats(player);
        handler->PSendSysMessage("|cff66ccff[Living Gear]|r Granted {} XP to equipped living items.", xp);
        SendWornExtras(handler, player);
        return true;
    }

    static bool HandleAttune(ChatHandler* handler, Optional<std::string> slotName, Optional<std::string> confirm)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;

        std::string slot = slotName.value_or("mainhand");
        uint8 id = ParseSlot(slot);
        if (id == EQUIPMENT_SLOT_END)
        {
            handler->PSendSysMessage(
                "Usage: .lg attune <slot> yes   slots: mainhand offhand head chest ...");
            return true;
        }

        if (!confirm || (*confirm != "yes" && *confirm != "YES"))
        {
            handler->PSendSysMessage(
                "This DESTROYS the item and grants 10% of its grown stats to the account. "
                "Type: .lg attune {} yes", slot);
            return true;
        }

        Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, id);
        if (!item)
        {
            handler->PSendSysMessage("|cffff6666[Living Gear]|r Nothing in that slot.");
            return true;
        }

        SacrificeItem(player, item, handler);
        return true;
    }

    static bool HandleReset(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;

        uint32 accountId = player->GetSession()->GetAccountId();
        uint32 guid = player->GetGUID().GetCounter();
        CharacterDatabase.DirectExecute("DELETE FROM `lg_absorb` WHERE `account_id` = {}", accountId);
        CharacterDatabase.DirectExecute("DELETE FROM `lg_item` WHERE `owner_guid` = {}", guid);
        RefreshStats(player);
        handler->PSendSysMessage("|cff66ccff[Living Gear]|r Cleared this account's absorb and this character's item rows.");
        return true;
    }
};
} // namespace LivingGear

// Cross-file wrapper so LivingGear_Vault.cpp's loot-rule "Living gear"
// match type can reuse the same equippable-gear eligibility check the
// attune system uses, without exposing IsEligible's static/file-local
// definition directly.
bool IsLivingGearEligibleItem(ItemTemplate const* proto)
{
    return LivingGear::IsEligible(proto);
}

// Cross-file wrapper: grants XP to a tracked living-gear item by GUID,
// rolling over levels and banking attunement exactly like GrantKillXp does
// for equipped gear. Not tied to an equipped/loaded Item* -- looks the item
// entry's template up directly, so it works for Curator-tracked bag/bank/
// armory pieces too (LivingGear_Perks.cpp TickCurator, "attune 1000 items"
// perk -- see Bonesaw.md, 2026-08-20 attunement redesign).
void LivingGear_GrantItemXp(Player* player, uint32 itemGuid, uint32 xp)
{
    if (!player)
        return;
    LivingGear::LgItemState st;
    if (!LivingGear::LoadItemState(itemGuid, st))
        return;
    if (st.level >= LivingGear::g_cfg.maxLevel)
        return;
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(st.itemEntry);
    if (!proto)
        return;
    LivingGear::AddItemXpAndBank(player, proto->ItemLevel, xp, st);
    LivingGear::SaveItemState(st);
}

void AddSC_LivingGear()
{
    new LivingGear::LivingGearWorld();
    new LivingGear::LivingGearPlayer();
    new LivingGear::LivingGearCommands();
}
