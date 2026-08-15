/*
 * Living Gear
 * Equipped items gain XP and levels. Grown stats apply only while worn.
 * Sacrificing an item destroys it and stores 10% of its grown stats on the
 * account (best copy per item entry). Random bonus stats may roll on loot.
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
#include "ObjectMgr.h"
#include "Player.h"
#include "Random.h"
#include "RBAC.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "WorldSession.h"

#include <array>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

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
    float absorbPct = 0.10f;
    float rollChance = 25.0f;
    uint8 rollStatCount = 1;

    void Load()
    {
        enabled = sConfigMgr->GetOption<bool>("LivingGear.Enable", true);
        xpPerKill = sConfigMgr->GetOption<uint32>("LivingGear.XpPerKill", 1);
        xpEliteKill = sConfigMgr->GetOption<uint32>("LivingGear.XpEliteKill", 3);
        xpBossKill = sConfigMgr->GetOption<uint32>("LivingGear.XpBossKill", 10);
        maxLevel = static_cast<uint16>(sConfigMgr->GetOption<uint32>("LivingGear.MaxLevel", 50));
        growthPerLevel = sConfigMgr->GetOption<float>("LivingGear.GrowthPerLevel", 0.10f);
        absorbPct = sConfigMgr->GetOption<float>("LivingGear.AbsorbPct", 0.10f);
        rollChance = sConfigMgr->GetOption<float>("LivingGear.RollChance", 25.0f);
        rollStatCount = static_cast<uint8>(sConfigMgr->GetOption<uint32>("LivingGear.RollStatCount", 1));
        if (rollStatCount < 1)
            rollStatCount = 1;
        if (rollStatCount > 5)
            rollStatCount = 5;
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
    float const mult = 1.0f + static_cast<float>(levels - 1) * g_cfg.growthPerLevel;
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

// Fast to 10 (45 white kills), then quadratic so later levels take real work.
static uint32 XpForNextLevel(uint16 level)
{
    if (level < 10)
        return level;
    return (static_cast<uint32>(level) * static_cast<uint32>(level)) / 2;
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
        "SELECT `item_entry`, `str`, `agi`, `sta`, `intel`, `spi`, `armor` "
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

        LgStats row;
        row.str = f[1].Get<float>();
        row.agi = f[2].Get<float>();
        row.sta = f[3].Get<float>();
        row.intel = f[4].Get<float>();
        row.spi = f[5].Get<float>();
        row.armor = f[6].Get<float>();
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
    if (!player || !player->GetSession())
        return;
    player->Whisper(std::string("LG\t") + line, LANG_ADDON, player);
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
    uint32 need = st.level >= g_cfg.maxLevel ? 0 : XpForNextLevel(st.level);
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

    LgStats absorb = LoadAbsorbForPlayer(player);
    uint32 count = 0;
    QueryResult countResult = CharacterDatabase.Query(
        "SELECT COUNT(*) FROM `lg_absorb` WHERE `account_id` = {}",
        player->GetSession()->GetAccountId());
    if (countResult)
        count = (*countResult)[0].Get<uint32>();

    SendAddonLine(player, Acore::StringFormat("ABS|{:.1f}|{:.1f}|{:.1f}|{:.1f}|{:.1f}|{:.1f}|{}",
        absorb.str, absorb.agi, absorb.sta, absorb.intel, absorb.spi, absorb.armor, count));

    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item)
            continue;
        SendLivingItem(player, item, Acore::StringFormat("inv|{}", slot));
    }

    if (includeBags)
        SendBagLivingItems(player);

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

static void RefreshStats(Player* player, bool includeBags = false)
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

        st.xp += xp;
        while (st.level < g_cfg.maxLevel && st.xp >= XpForNextLevel(st.level))
        {
            st.xp -= XpForNextLevel(st.level);
            ++st.level;
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

    if (absorb.Total() + 0.01f < existingTotal)
    {
        handler->PSendSysMessage(
            "|cffffcc00[Living Gear]|r A stronger copy of this item is already attuned. "
            "Sacrificing this one would do nothing. Item kept.");
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

class LivingGearPlayer : public PlayerScript
{
public:
    LivingGearPlayer() : PlayerScript("LivingGearPlayer", {
        PLAYERHOOK_ON_LOGIN,
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
        RefreshStats(player, true);
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

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 language, std::string& msg, Player* /*receiver*/) override
    {
        if (language != LANG_ADDON || type != CHAT_MSG_WHISPER)
            return true;
        if (msg == "LG\tREQ" || msg == "REQ")
        {
            SendAddonSync(player, true);
            return false;
        }
        if (HandleTipRequest(player, msg))
            return false;
        if (HandleAttuneMessage(player, msg))
            return false;
        return true;
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
        };
        static ChatCommandTable commandTable =
        {
            { "lg", lgTable }
        };
        return commandTable;
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
            uint32 need = st.level >= g_cfg.maxLevel ? 0 : XpForNextLevel(st.level);
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
            st.xp += xp;
            while (st.level < g_cfg.maxLevel && st.xp >= XpForNextLevel(st.level))
            {
                st.xp -= XpForNextLevel(st.level);
                ++st.level;
            }
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

void AddSC_LivingGear()
{
    new LivingGear::LivingGearWorld();
    new LivingGear::LivingGearPlayer();
    new LivingGear::LivingGearCommands();
}
