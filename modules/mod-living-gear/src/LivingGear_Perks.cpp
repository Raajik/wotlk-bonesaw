/*
 * Living Gear account perks rebuilt as new code on the stub baseline.
 * Zone scale, auto-mount, Subtlety (Shaco kit), poisons, combo, travel,
 * craft speed, cooking regen, dungeon timer, curator, quest helpers, armory,
 * solo queue, instant/uniform mount.
 */

#include "AchievementMgr.h"
#include "AllMapScript.h"
#include "AllSpellScript.h"
#include "CellImpl.h"
#include "Chat.h"
#include "Config.h"
#include "CreatureAI.h"
#include "CreatureData.h"
#include "CreatureScript.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "Formulas.h"
#include "GameTime.h"
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
#include "Pet.h"
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
#include "World.h"
#include "QuestDef.h"
#include "WorldSession.h"

#include <limits>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Player;
void LivingGear_GrantItemXp(Player* player, uint32 itemGuid, uint32 xp); // LivingGear.cpp
void LivingGear_BankCollection(Player* player, uint32 pct); // LivingGear.cpp
float LivingGear_LevelingXpMultiplier(Player* player); // LivingGear_Progression.cpp
uint32 GetClassPerk(Player* player); // LivingGear_ClassPerks.cpp
void LivingGear_SendAddonLine(Player* player, std::string const& line); // LivingGear.cpp
bool LivingGear_SafeToCastOn(Player* player); // LivingGear_Support.cpp
void LivingGear_DiagBump(Player* player, char const* key); // LivingGear_Support.cpp
bool LivingGear_IsAddonSendInProgress(); // LivingGear.cpp

namespace LivingGearPerks
{
uint32 const SPELL_FIND_QUESTS = 910088;
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
// Shadow Dance (2026-08-21, replaces the dropped Jack in the Box totem,
// reuses its freed spell ID): permanent Subtlety perk, two effects --
// lets stealth-only openers (Ambush/Garrote/Cheap Shot/etc.) be used
// without actually being stealthed (LivingGear_BypassStealthRequirement,
// a small core patch in Spell::CheckCast), and a +10% attack power buff
// to the whole party/raid (ShouldHaveShadowDanceBuff/TickShadowDanceBuff
// below, mirroring the existing Class Buffs pattern in LivingGear_Next.cpp).
uint32 const SPELL_SHADOW_DANCE = 910102;
uint32 const SPELL_SHADOW_CLONE = 910103;
uint32 const SPELL_MOUNTED_OPENER = 910104;
uint32 const SPELL_AUTO_MOUNT = 910105;
// Quadruples aggro/detection radius while toggled on -- native
// SPELL_AURA_MOD_DETECTED_RANGE (152), flat +60 yards, which roughly 4x's
// the ~20-yard same-level baseline in Creature::GetAttackDistance(). No
// core changes needed; this is exactly what that aura type is for.
uint32 const SPELL_PULL_RADIUS = 910168;
// Track Ore / Track Herbs -- toggle badges gating the native tracking
// spells (Find Minerals / Find Herbs, stock Blizzard spells not in our
// spell_dbc table since they're resolved from the compiled client DBC).
// Casting the native spell puts real nodes on the minimap; refreshed
// periodically the same way SPELL_PULL_RADIUS is, since we
// can't check the native spell's real duration from our sparse table.
uint32 const SPELL_TRACK_ORE = 910170;
uint32 const SPELL_TRACK_HERB = 910171;
// CC Reduction (2026-08-22): passive account perk, cuts the duration of any
// crowd control landing on you by 95%. Deliberately not a real castable
// spell (learnSpellToo = false at its UnlockPerk call) -- there is nothing
// to press, and a spell with no SkillLineAbility row would just land in the
// General spellbook tab. Unlocks the first time anything crowd controls
// you, which for most characters is the first pull that goes wrong.
uint32 const SPELL_CC_REDUCTION = 910172;
float const CC_REDUCTION_PCT = 0.95f;
// Shadow Dance's visible half (2026-08-22). SPELL_SHADOW_DANCE above stays
// the account perk flag; this is the real aura carrying the +10% attack
// power, cast on the Rogue and their party so it shows up in the buff bar
// like any other raid buff instead of being an invisible stat modifier.
uint32 const SPELL_SHADOW_DANCE_BUFF = 910173;
// First Aid track, 2026-08-22. Advertised and earnable since the track was
// written, implemented by nothing -- found by tools/perk_audit.py. Nothing in
// the module so much as contained the word "bandage" before this.
uint32 const SPELL_AID_INSTANT = 910046;
uint32 const SPELL_AID_RESTORE = 910047;
uint32 const SPELL_AID_CLEANSE = 910048;
// Fishing "Speed" (910045) lives here rather than in LivingGear_Gather.cpp
// because cast time is set in OnSpellPrepare, and this file owns that hook.
uint32 const SPELL_FISH_BITE_SPEED = 910045;
// One debuff per second while bandaged, so Cleanse is a steady scrub rather
// than an instant full dispel.
uint32 const AID_CLEANSE_MS = 1000;
// Cooking regen (2026-08-22): real aura feeding MOD_REGEN/MOD_POWER_REGEN,
// base points set per-cast from the player's max health/mana. See TickCooking.
uint32 const SPELL_COOK_REGEN = 910174;
uint32 const NATIVE_FIND_MINERALS = 2580;
uint32 const NATIVE_FIND_HERBS = 2383;
uint32 const NATIVE_FIND_FISH = 43308;
// Native tracking auras ultimately only toggle these PLAYER_TRACK_RESOURCES
// bits (misc values 3, 2 and 19 respectively). Set the bits directly so ore,
// herbs and fishing pools stay visible without repeatedly casting Blizzard
// tracking spells and stealing the player's tracking-dropdown selection.
uint32 const TRACK_RESOURCE_ORE = 1u << (3 - 1);
uint32 const TRACK_RESOURCE_HERB = 1u << (2 - 1);
uint32 const TRACK_RESOURCE_FISH = 1u << (19 - 1);
uint32 const SPELL_SUBTLETY = 910037;
uint32 const SPELL_ASSASSINATION = 910035;
uint32 const NPC_SHADOW_CLONE = 910201;
uint32 const SPELL_STEALTH = 1784;
uint32 const SPELL_SHADOWSTEP = 36554;
// The real Shadow Dance spell (report #61: "turn Shadow Dance into a permanent
// spell for sub rogues"). The perk flag SPELL_SHADOW_DANCE (910102) above is a
// badge; this is the actual button. Granted alongside Shadowstep so it shows
// in the spellbook -- the BypassStealthRequirement core hook (Spell::CheckCast)
// is what makes its effect permanent, so the spell is a real usable button
// whose underlying benefit never expires.
uint32 const SPELL_SHADOW_DANCE_NATIVE = 51713;
uint32 const SPELL_AMBUSH = 8676;
uint32 const SPELL_PICKPOCKET = 921;
// Hemorrhage AoE (2026-08-21, replaces Jack in the Box): on cast, applies
// Garrote + Pickpocket to every enemy within 10 yards, via BestOwned() to
// resolve each spell to the player's actual trained rank -- same pattern
// ChainAmbush already uses for Ambush. Cheap Shot was in the original ask
// but dropped (mass-stun on a whole pack agreed to be too strong).
uint32 const SPELL_HEMORRHAGE = 16511;
uint32 const SPELL_GARROTE = 703;
uint32 const SPELL_DEADLY_THROW = 26679;
uint32 const SPELL_SINISTER_STRIKE = 1752;
uint32 const SPELL_EVISCERATE = 2098;
uint32 const SPELL_KICK = 1766;
uint32 const SPELL_RUPTURE = 1943;
uint32 const SPELL_HOWL = 5484;
uint32 const SPELL_THUNDER_CLAP = 6343;
uint32 const SPELL_CRIPPLING = 3408;
uint32 const SPELL_WOUND = 13218;
uint32 const SPELL_DEADLY_POISON = 2818;
// How often to re-evaluate the cooking regen aura's base points. Not a
// restore interval any more -- the engine does the restoring -- just how
// promptly the aura tracks a max-health/mana change.
uint32 const COOK_REFRESH_MS = 3000;
uint32 const COOK_BREAKS[] = { 75, 150, 225, 300, 375, 450 };
// Bug report #8: Garrote's bleed carries a 1000% damage multiplier, i.e. 11x
// the tick it would otherwise do. Applied once, in ModifyPeriodicDamageAurasTick,
// so every Garrote benefits -- hand-cast, or applied in bulk by Hemorrhage --
// rather than only the copies one particular code path happens to create.
float const GARROTE_BLEED_MULT = 11.0f;
// Bug report #10: the Ambush that Hemorrhage spreads lands at +500%, i.e. 6x.
float const HEMO_AMBUSH_MULT = 6.0f;
// Shadowstep's cooldown after the landing-pickpocket was removed (2026-08-26,
// report #9): with the pickpocket gone it is a pure on-demand speed boost, so
// a short cooldown is the buff. 2 seconds -- 3x the old 6s -- keeps it a
// mobility tool you lean on rather than a button you spam.
uint32 const SHADOWSTEP_COOLDOWN_MS = 2000;
// Report #73 (redesign 2026-08-26): using the Pickpocket skill directly
// auto-pickpockets every humanoid within this radius. User chose this over
// a once-per-stealth aura -- a real larceny button instead of a stealth
// stance.
float const PICKPOCKET_AOE_RADIUS = 10.0f;
// Bug report #10: how far Hemorrhage spreads.
// Bug report #41: "Hemorrhage's extra effects need to apply to all enemies
// within 15 yards (up from 10)". Matches the Assassination detonator and the
// Combat flurry spread, so all three rogue specs now reach the same distance.
float const HEMO_RADIUS = 15.0f;

// Report #44: "Garrote and Rupture damage should both be increased by 2000%".
// 2000% more is twenty-one times the base.
uint32 const SUBTLETY_BLEED_MULT = 21;
uint32 const SPELL_RUPTURE_R1 = 1943;    // Spell.dbc, 12 ranks
uint32 const SPELL_SLICE_AND_DICE_R1 = 5171;
uint32 const SPELL_EVISCERATE_R1 = 2098;

struct PerkCfg
{
    bool zoneEnable = true;
    uint32 zoneBuffer = 3;
    uint32 zoneMinLevel = 2;
    float zoneFloor = 0.35f;
    float zoneDecay = 12.0f;
    float zoneIncoming = 0.12f;
    bool instantMount = true;
    bool uniformMount = true;
    bool autoMount = true;
    uint32 dungeonPar = 1800;
    uint32 curatorTick = 60000;
    bool dungeonScale = true;
    bool questScale = true;
    uint32 questFloorPct = 10;
    // Every kill is worth at least this percentage of the killer's current
    // level bar. See KillXpFor.
    bool killFloor = true;
    uint32 killFloorPct = 2;
    uint32 killFloorElitePct = 4;
    // Share a kill the engine paid nobody for with the whole party, not just
    // whoever landed the blow. See GrantUnrewardedKillXp.
    bool groupKillXp = true;
    bool softenImmunity = true;
    bool ignoreSpellReqs = true;
    // See ReconcilePerkSpells. Castable perks only, so the cost is bounded.
    bool reconcilePerkSpells = true;
    // Achievement.dbc points divided by this become spendable skill points,
    // rounded up. 10 is deliberate: 84% of achievements are worth exactly 10
    // points, so at this rate one achievement is one skill point and the
    // conversion needs no explaining. Above 10 the typical achievement pays
    // nothing at all.
    uint32 skillPointDivisor = 10;
    uint32 perkRespecCooldown = 300;
    // Bump when lg_perk_cost prices change: every account is refunded on its
    // next login and re-spends at the new rates. Without it, accounts keep
    // whatever price they bought in at and veterans drift steadily richer.
    uint32 perkCostEpoch = 1;
};

PerkCfg g_cfg;

std::unordered_map<uint32, bool> g_autoMountOn;
std::unordered_map<uint32, bool> g_soloQueue;
std::unordered_map<uint32, bool> g_chatOn;
std::unordered_map<uint32, uint32> g_lastMount;
std::unordered_map<uint32, ObjectGuid> g_cloneGuid;
std::unordered_map<uint32, uint32> g_dungeonStart;
std::unordered_map<uint32, bool> g_dungeonDone;
std::unordered_map<uint32, uint32> g_cookAcc;
// hp-per-5 in the high 32 bits, mana-per-5 in the low: the last amounts
// actually cast, so an unchanged tier doesn't re-flash the buff icon.
std::unordered_map<uint32, uint64> g_cookAmount;
std::unordered_map<uint32, uint32> g_aidCleanseTick;
std::unordered_map<uint32, uint32> g_curatorAcc;
// Shadow Dance's +10% attack power half -- keyed by player GUID (not
// account), since it's a real per-character stat modifier applied via
// Player::ApplyStatPctModifier, not a persisted account toggle.
std::unordered_map<uint32, bool> g_shadowDanceBuffOn;
std::unordered_map<uint32, uint32> g_shadowDanceTick;
std::unordered_map<uint32, uint32> g_pullRadiusTick;
std::unordered_map<uint32, uint32> g_trackOreTick;   // retired with the 10s recast (report #83) -- kept for save compat
std::unordered_map<uint32, uint32> g_trackHerbTick;
std::unordered_set<uint32> g_perkLoaded;

// Achievement-funded perk purchases. `prereq` is what orders a track (Cooking
// 75 -> 150 -> 225); 0 means the node stands alone. Not named `requires`:
// that is a keyword in C++20, which this module is built as.
struct PerkPrice
{
    uint32 cost = 0;
    uint32 prereq = 0;
};

std::unordered_map<uint32, PerkPrice> g_perkPrice;
std::unordered_map<uint32, uint32> g_achievementValue;
// Earned points are a join across every character on the account, so they are
// computed once per account per uptime and invalidated on purchase, respec,
// and newly completed achievements rather than being re-queried per click.
std::unordered_map<uint32, uint32> g_earnedCache;
// spellId -> what was paid, per account. Cached because the buy-back check runs
// on every condition grant (CatchUpProfession alone fires ~20 per login), and
// that must not be 20 queries.
std::unordered_map<uint32, std::unordered_map<uint32, uint32>> g_purchased;
std::unordered_set<uint32> g_purchaseLoaded;
bool g_hasPerkPriceSchema = false;
bool g_hasLastRespecCol = false;
bool g_hasPerkEpochCol = false;
std::unordered_map<uint32, std::unordered_set<uint32>> g_perks;
bool g_hasAutoMountCol = false;
bool g_hasSoloCol = false;
bool g_hasPullRadiusCol = false;
bool g_hasTrackOreCol = false;
bool g_hasTrackHerbCol = false;
bool g_schemaReady = false;
bool g_hasZoneScale = false;
std::unordered_map<uint32, bool> g_pullRadiusOn;
std::unordered_map<uint32, bool> g_trackOreOn;
std::unordered_map<uint32, bool> g_trackHerbOn;
std::unordered_map<uint32, uint32> g_jumpMode;
bool g_hasJumpCol = false;
// Which accounts have had their lg_account_meta toggles read in this
// uptime. SendPerkSync now answers a client REQ as well as a login, and
// REQ fires on /reload, on entering the world and every time a bank window
// opens -- re-running six synchronous CharacterDatabase queries on the
// world thread each time is not free. Every writer below updates the
// in-memory maps too, so the cache cannot go stale.
std::unordered_set<uint32> g_metaLoaded;

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
            else if (name == "pull_radius")
                g_hasPullRadiusCol = true;
            else if (name == "track_ore")
                g_hasTrackOreCol = true;
            else if (name == "track_herb")
                g_hasTrackHerbCol = true;
            else if (name == "jump_mode")
                g_hasJumpCol = true;
        } while (cols->NextRow());
    }
}

void SendLine(Player* player, std::string const& line)
{
    ::LivingGear_SendAddonLine(player, line);
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

// 2026-08-21: learnSpellToo defaults true (most perks unlocked here ARE
// meant to be real, castable spells) but must be false for pure account-
// perk flags with no CASTABLE_SPELLS/SkillLineAbility entry (Shadow Dance,
// Shadow Clone) -- player->learnSpell() marks them "known" with nothing to
// categorize them, so the client dumps them into the General spellbook tab
// regardless of being excluded from CASTABLE_SPELLS client-side. HasPerk()'s
// g_perks[acc]/DB fallback works fine without ever calling learnSpell.
bool PerkIsCastable(uint32 spellId);   // defined just below; UnlockPerk needs it
void RefundIfPurchased(Player* player, uint32 spellId);   // buy-back, defined further down

void UnlockPerk(Player* player, uint32 spellId, char const* msg, bool learnSpellToo = true)
{
    if (!player || !player->GetSession())
        return;
    if (!sSpellMgr->GetSpellInfo(spellId))
    {
        // A perk whose spell_dbc row never made it into the world DB used
        // to disappear right here without a word: the feature simply
        // "stopped working" and there was nothing in any log to grep for.
        // Once per spell id per uptime is enough to make it obvious.
        static std::unordered_set<uint32> warned;
        if (warned.insert(spellId).second)
            LOG_ERROR("module.livinggear",
                "Living Gear: perk spell {} has no spell_dbc row, so it can never unlock. "
                "The migration that adds it has not been applied to the world DB.", spellId);
        return;
    }
    uint32 const acc = player->GetSession()->GetAccountId();
    LoadPerks(acc);
    // Perks are owned by the ACCOUNT but spells are learned per CHARACTER, and
    // this used to return right here the moment the account already owned the
    // perk -- before learning anything. So the first character on an account
    // got the spell and no later one ever did, and a perk whose spell was
    // added after the fact could never be learned by anyone who already held
    // the row. Both were live: 61 accounts owned Fishing 910043 with zero
    // characters having learned it, and one real account owned 70 perks whose
    // five characters knew 20, 14, 11, 14 and 2 of them.
    //
    // Only the announcement is once-per-account now. Learning is idempotent
    // and runs whenever it is missing.
    bool const firstTime = g_perks[acc].insert(spellId).second;
    if (firstTime)
        CharacterDatabase.DirectExecute(
            "INSERT IGNORE INTO `lg_account_perk` (`account_id`, `spell_id`) VALUES ({}, {})",
            acc, spellId);
    // Badges are never learned. They have no SkillLineAbility row so they
    // appear nowhere, HasPerk reads the account set regardless, and learning
    // one only spams "You have learned a new ability" into chat.
    if (learnSpellToo && PerkIsCastable(spellId) && !player->HasSpell(spellId))
        player->learnSpell(spellId);
    if (!firstTime)
    {
        // Already owned. If it was BOUGHT and a condition has now granted it
        // anyway, the points went on something that would have been free.
        RefundIfPurchased(player, spellId);
        return;
    }
    SendLine(player, Acore::StringFormat("PK|{}|1", spellId));
    if (msg)
        Say(player, msg);
}

// The perks that are real, pressable abilities.
//
// Mirrors CASTABLE_SPELLS in tools/client-patch/build_patch.py, which is what
// decides the matter: only these get a SkillLineAbility.dbc row, and without
// one a learned spell does not appear in the spellbook at all. Everything else
// in the 910xxx range is a badge -- a flag the module reads, with nothing to
// press and nothing to see.
//
// That distinction is the whole reason reconciliation is worth doing at all.
// Learning a badge produces a "you have learnt" packet and changes nothing a
// player can observe, because HasPerk consults the account set anyway. Learning
// one of these restores a button that is genuinely missing: *Quests - Finish
// was absent from 894 characters, Auto-Mount from 733, Solo Queue from 723.
bool PerkIsCastable(uint32 spellId)
{
    switch (spellId)
    {
        case 910001: // *Windblown -- opens the Account Perks window
        case 910002: // *Mailbox
        case 910003: // *Auction House
        case 910004: // *Class Trainer
        case 910005: // *Bank
        case 910006: // *Stable
        case 910007: // *Bind
        case 910009: // *Flight Master
        case 910088: // *Quests - Find
        case 910090: // *Quests - Finish
        case 910091: // *Attuned Armory
            return true;
        // Autoloot (910008), Solo Queue (910092) and Auto-Mount (910105) are
        // deliberately absent. They are STATE, not actions: casting one only
        // flipped an account boolean that the Account Perks window already
        // toggles, via ALSET / SOLOSET / AMSET, each of which has a server
        // handler. A spellbook button duplicating a checkbox is a second
        // source of truth for one switch, and they topped the missing-button
        // list -- 733 characters had no Auto-Mount, 723 no Solo Queue --
        // exactly because nobody needed to notice they were gone.
        //
        // They remain OWNED perks. HasPerk reads the account set, so every
        // gate depending on them is untouched.
        default:
            return false;
    }
}

// Repair pass, run at every login and re-sync.
//
// UnlockPerk only fires when a perk's unlock CONDITION is re-evaluated, and
// most conditions are events (train a skill, win a battleground) that never
// happen twice. That left two permanent holes: a new character on an
// established account, and any perk whose spell_dbc row was added after the
// account already owned the perk -- exactly what happened to the 40 badge
// spells added in 0.1.61, which nobody had learned.
//
// Reconciling here means the character's spellbook converges on what the
// account owns no matter which of those paths got it there.
//
// Restricted to castable perks on purpose, and that restriction is what makes
// it safe to have on by default.
//
// The first cut of this learned everything the account owned, which meant ~100
// learnSpell calls on a character's first login. Player::learnSpell sends the
// "you have learnt" packet whenever IsInWorld(), and OnPlayerLogin runs after
// AddPlayerToMap (CharacterHandler.cpp:911 vs :1131), so every one of those
// would have fired. For badges that buys nothing at all -- they have no
// SkillLineAbility row, so they never appear in the spellbook, and HasPerk
// consults the account set regardless.
//
// Bounded to the 15 castable perks, the cost is at most 15 packets once per
// character, and the benefit is a button the player earned and could not see.
void ReconcilePerkSpells(Player* player)
{
    if (!player || !player->GetSession() || !g_cfg.reconcilePerkSpells)
        return;
    uint32 const acc = player->GetSession()->GetAccountId();
    LoadPerks(acc);

    // Copy first: learnSpell can run scripts, and this must not be iterating
    // g_perks if any of them reach back into the perk system.
    std::vector<uint32> owned(g_perks[acc].begin(), g_perks[acc].end());
    uint32 learned = 0;
    uint32 retired = 0;
    for (uint32 spellId : owned)
    {
        // Older builds learned every account perk, including passive badges
        // and retired toggles. Ownership must remain in g_perks, but the stale
        // spellbook entry must not: otherwise spells such as Auto-Mount,
        // Craft ranks, tracking badges and Mounted Opener survive forever
        // merely because the account still owns their underlying perk
        // (report #100).
        if (!PerkIsCastable(spellId))
        {
            if (player->HasSpell(spellId))
            {
                player->removeSpell(spellId, SPEC_MASK_ALL, false);
                ++retired;
            }
            continue;
        }
        if (player->HasSpell(spellId))
            continue;
        if (!sSpellMgr->GetSpellInfo(spellId))
            continue;   // already logged loudly by UnlockPerk
        player->learnSpell(spellId);
        ++learned;
    }
    if (learned)
        LOG_INFO("module.livinggear",
            "Living Gear: {} learned {} account perk spell(s) it was missing.",
            player->GetName(), learned);
    if (retired)
        LOG_INFO("module.livinggear",
            "Living Gear: {} removed {} retired/passive perk spell(s).",
            player->GetName(), retired);

    // The other direction, and the reason a respec can revoke anything at all.
    // Spells cannot be stripped from an offline alt (same constraint the
    // account key ring hit), so a respec deletes the rows immediately and each
    // character sheds its buttons here on next login. Bounded to the priced
    // set, which is the only thing a respec is allowed to take back.
    uint32 removed = 0;
    for (auto const& entry : g_perkPrice)
    {
        uint32 const spellId = entry.first;
        if (!player->HasSpell(spellId) || g_perks[acc].count(spellId))
            continue;
        player->removeSpell(spellId, SPEC_MASK_ALL, false);
        ++removed;
    }
    if (removed)
        LOG_INFO("module.livinggear",
            "Living Gear: {} lost {} perk spell(s) its account no longer owns.",
            player->GetName(), removed);
}

// ---------------------------------------------------------------------------
// Achievement-funded perk purchases (2026-08-24).
//
// Balance is DERIVED, never stored:
//     balance = EarnedSkillPoints() - SpentSkillPoints()
// so there is no counter that can drift out of step with the purchase set,
// double-spend is impossible, and a respec is a DELETE rather than arithmetic.
//
// Ownership stays in `lg_account_perk`, untouched -- every rank is already its
// own spell id, so all existing HasPerk() reads keep working. A perk owned
// there with no row in `lg_account_perk_purchase` was granted, not bought,
// which is how every pre-existing unlock grandfathers in at zero cost.
// ---------------------------------------------------------------------------

void SendPerkPoints(Player* player);   // defined below; the buy-back path re-sends
void ApplyCuratorCoverage(Player* player);   // defined below; PurchaseRank applies it at once

void DetectPerkPriceSchema()
{
    g_hasPerkPriceSchema = false;
    g_hasLastRespecCol = false;
    if (QueryResult t = CharacterDatabase.Query(
        "SELECT 1 FROM `information_schema`.`TABLES` "
        "WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = 'lg_perk_cost'"))
        g_hasPerkPriceSchema = t->GetRowCount() > 0;
    if (QueryResult c = CharacterDatabase.Query(
        "SELECT `COLUMN_NAME` FROM `information_schema`.`COLUMNS` "
        "WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = 'lg_account_meta' "
        "AND `COLUMN_NAME` IN ('last_respec', 'perk_epoch')"))
    {
        do
        {
            std::string const col = (*c)[0].Get<std::string>();
            if (col == "last_respec")
                g_hasLastRespecCol = true;
            else if (col == "perk_epoch")
                g_hasPerkEpochCol = true;
        }
        while (c->NextRow());
    }
}

void LoadPerkPrices()
{
    g_perkPrice.clear();
    g_achievementValue.clear();
    g_earnedCache.clear();
    if (!g_hasPerkPriceSchema)
        return;
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT `spell_id`, `cost`, `requires_spell_id` FROM `lg_perk_cost`"))
    {
        do
        {
            PerkPrice price;
            price.cost = (*result)[1].Get<uint32>();
            price.prereq = (*result)[2].Get<uint32>();
            g_perkPrice[(*result)[0].Get<uint32>()] = price;
        }
        while (result->NextRow());
    }
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT `achievement_id`, `skill_points` FROM `lg_achievement_value`"))
    {
        do
            g_achievementValue[(*result)[0].Get<uint32>()] = (*result)[1].Get<uint32>();
        while (result->NextRow());
    }
    LOG_INFO("module.livinggear",
        "Living Gear: {} perk prices, {} achievement value overrides.",
        g_perkPrice.size(), g_achievementValue.size());
}

// Achievement categories are a tree -- Battlegrounds and Arena both roll up to
// Player vs. Player -- and the multiplier gates want the root.
uint32 RootAchievementCategory(uint32 categoryId)
{
    for (uint8 depth = 0; depth < 10; ++depth)
    {
        AchievementCategoryEntry const* cat = sAchievementCategoryStore.LookupEntry(categoryId);
        if (!cat || cat->parentCategory < 0)
            break;
        categoryId = uint32(cat->parentCategory);
    }
    return categoryId;
}

uint32 AchievementSkillValue(AchievementEntry const* entry)
{
    if (!entry)
        return 0;
    // Overrides first: this is where Feats of Strength and Realm Firsts get a
    // value, since every one of them awards 0 points in Achievement.dbc.
    auto const itr = g_achievementValue.find(entry->ID);
    if (itr != g_achievementValue.end())
        return itr->second;
    uint32 const divisor = g_cfg.skillPointDivisor ? g_cfg.skillPointDivisor : 10;
    return (entry->points + divisor - 1) / divisor;   // round up
}

// Account-wide and DISTINCT: two characters that both earned "Level 10" pay
// once. Counting per character instead would make levelling alts the cheapest
// way to farm the currency.
uint32 EarnedSkillPoints(uint32 accountId, std::unordered_map<uint32, uint32>* byCategory = nullptr)
{
    if (!byCategory)
    {
        auto const cached = g_earnedCache.find(accountId);
        if (cached != g_earnedCache.end())
            return cached->second;
    }

    uint32 earned = 0;
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT DISTINCT ca.`achievement` FROM `character_achievement` ca "
        "JOIN `characters` c ON c.`guid` = ca.`guid` WHERE c.`account` = {}", accountId))
    {
        do
        {
            AchievementEntry const* entry = sAchievementStore.LookupEntry((*result)[0].Get<uint16>());
            uint32 const value = AchievementSkillValue(entry);
            if (!value)
                continue;
            earned += value;
            if (byCategory)
                (*byCategory)[RootAchievementCategory(entry->categoryId)] += value;
        }
        while (result->NextRow());
    }
    g_earnedCache[accountId] = earned;
    return earned;
}

uint32 SpentSkillPoints(uint32 accountId)
{
    if (!g_hasPerkPriceSchema)
        return 0;
    if (QueryResult result = CharacterDatabase.Query(
        // CAST so the column comes back as an integer type. A bare SUM() is
        // DECIMAL, which Field::Get<uint64> has no business reading.
        "SELECT CAST(COALESCE(SUM(`paid`), 0) AS UNSIGNED) "
        "FROM `lg_account_perk_purchase` WHERE `account_id` = {}",
        accountId))
        return uint32((*result)[0].Get<uint64>());
    return 0;
}

void LoadPurchases(uint32 accountId)
{
    if (!g_hasPerkPriceSchema || !g_purchaseLoaded.insert(accountId).second)
        return;
    auto& owned = g_purchased[accountId];
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT `spell_id`, `paid` FROM `lg_account_perk_purchase` WHERE `account_id` = {}",
        accountId))
    {
        do
            owned[(*result)[0].Get<uint32>()] = (*result)[1].Get<uint32>();
        while (result->NextRow());
    }
}

uint32 SkillPointBalance(uint32 accountId)
{
    uint32 const earned = EarnedSkillPoints(accountId);
    uint32 const spent = SpentSkillPoints(accountId);
    return earned > spent ? earned - spent : 0;
}

bool PurchaseRank(Player* player, uint32 spellId)
{
    if (!player || !player->GetSession() || !g_hasPerkPriceSchema)
        return false;
    uint32 const acc = player->GetSession()->GetAccountId();

    auto const itr = g_perkPrice.find(spellId);
    if (itr == g_perkPrice.end() || HasPerk(player, spellId))
        return false;
    if (itr->second.prereq && !HasPerk(player, itr->second.prereq))
        return false;
    if (SkillPointBalance(acc) < itr->second.cost)
        return false;

    // Storing what was actually paid, rather than re-reading lg_perk_cost at
    // refund time, is what lets prices be retuned later without retroactively
    // overdrawing accounts that bought in at the old rate.
    CharacterDatabase.DirectExecute(
        "INSERT INTO `lg_account_perk_purchase` (`account_id`, `spell_id`, `paid`) "
        "VALUES ({}, {}, {})", acc, spellId, itr->second.cost);
    LoadPurchases(acc);
    g_purchased[acc][spellId] = itr->second.cost;

    UnlockPerk(player, spellId, nullptr);
    // Buying a Curator rank has to bank the collection immediately, or the perk
    // would appear to do nothing until the next login.
    if (spellId == SPELL_CURATOR || spellId == 910178 || spellId == 910179 || spellId == 910180)
        ApplyCuratorCoverage(player);
    return true;
}

// A condition has just granted a perk this account had already PAID for -- the
// points went on something that would have arrived free anyway, so hand them
// back. Deleting the purchase row is the entire refund, because balance is
// derived; the perk itself stays owned.
void RefundIfPurchased(Player* player, uint32 spellId)
{
    if (!player || !player->GetSession() || !g_hasPerkPriceSchema)
        return;
    uint32 const acc = player->GetSession()->GetAccountId();
    LoadPurchases(acc);
    auto& owned = g_purchased[acc];
    auto const itr = owned.find(spellId);
    if (itr == owned.end())
        return;
    uint32 const paid = itr->second;
    owned.erase(itr);
    CharacterDatabase.DirectExecute(
        "DELETE FROM `lg_account_perk_purchase` WHERE `account_id` = {} AND `spell_id` = {}",
        acc, spellId);
    if (paid)
        Say(player, Acore::StringFormat(
            "|cff66ccff[Account Perks]|r You earned that perk the hard way - {} point(s) refunded.",
            paid).c_str());
    SendPerkPoints(player);
}

uint32 LoadLastRespec(uint32 accountId)
{
    if (!g_hasLastRespecCol)
        return 0;
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT `last_respec` FROM `lg_account_meta` WHERE `account_id` = {}", accountId))
        return (*result)[0].Get<uint32>();
    return 0;
}

// Account-wide, and persisted rather than held in memory. An in-memory
// cooldown would reset on every worldserver restart, and this realm restarts
// on every ship -- which would make it trivially bypassable on patch day.
bool CanRespec(uint32 accountId, uint32& secondsLeft)
{
    secondsLeft = 0;
    uint32 const now = uint32(GameTime::GetGameTime().count());
    uint32 const last = LoadLastRespec(accountId);
    if (last && now < last + g_cfg.perkRespecCooldown)
    {
        secondsLeft = (last + g_cfg.perkRespecCooldown) - now;
        return false;
    }
    return true;
}

// Full-only, never per-track. With prereq-ordered tracks a partial refund
// would have to walk the dependency graph and cascade dependents; refunding
// Cooking 150 while 225 is still owned leaves the track corrupt. All-or-
// nothing sidesteps that entire class of bug.
void RespecPerks(uint32 accountId)
{
    if (!g_hasPerkPriceSchema)
        return;

    auto trans = CharacterDatabase.BeginTransaction();
    // Revoke only what was bought. Condition-granted and grandfathered rows
    // have no purchase row and must survive untouched -- that set difference
    // is the whole reason purchases live in their own table.
    trans->Append(
        "DELETE p FROM `lg_account_perk` p "
        "JOIN `lg_account_perk_purchase` q "
        "  ON q.`account_id` = p.`account_id` AND q.`spell_id` = p.`spell_id` "
        "WHERE p.`account_id` = {}", accountId);
    trans->Append("DELETE FROM `lg_account_perk_purchase` WHERE `account_id` = {}", accountId);
    CharacterDatabase.CommitTransaction(trans);

    g_perkLoaded.erase(accountId);
    g_perks.erase(accountId);
    g_earnedCache.erase(accountId);
    g_purchaseLoaded.erase(accountId);
    g_purchased.erase(accountId);
}

// Mirror a completed achievement onto every other character on the account.
//
// Rows go straight into character_achievement rather than through
// CompletedAchievement, which is what the core's own offline-update queue
// calls. That path re-runs the reward logic -- it grants the title and mails
// the item -- so an account with five alts would be sent five Albino Drakes.
// The record is what wants to be account-wide; the loot does not.
//
// This changes no skill point total. EarnedSkillPoints already counts DISTINCT
// achievement ids across the account, so two characters earning Level 10 was
// always worth one point. This is so the achievement panel agrees with itself
// on every character.
//
// Realm First is excluded deliberately: it carries realm-unique bookkeeping and
// is a one-off honour that belongs to the character that actually did it.
void SyncAchievementToAccount(Player* player, AchievementEntry const* entry)
{
    if (!player || !player->GetSession() || !entry)
        return;
    if (entry->flags & (ACHIEVEMENT_FLAG_COUNTER | ACHIEVEMENT_FLAG_REALM_FIRST_REACH
        | ACHIEVEMENT_FLAG_REALM_FIRST_KILL))
        return;
    CharacterDatabase.Execute(
        "INSERT IGNORE INTO `character_achievement` (`guid`, `achievement`, `date`) "
        "SELECT `guid`, {}, {} FROM `characters` WHERE `account` = {} AND `guid` <> {}",
        entry->ID, uint32(GameTime::GetGameTime().count()),
        player->GetSession()->GetAccountId(), player->GetGUID().GetCounter());
}

// Report #45: achievements should be account-wide. SyncAchievementToAccount
// above only covers completions that happen WHILE someone is logged in -- the
// INSERT fires from OnPlayerAchievementComplete. Every achievement earned
// before that sync existed was a hole on every alt forever: account 2 holds
// 74 distinct achievements but Muckfuppet had 73 of them and his alts ~34
// each, purely by age.
//
// The catch-up runs at login and backfills both directions in one statement:
// anything any character on this account has completed that THIS character
// lacks is inserted here (INSERT IGNORE, dated to now), so alts see the full
// account history without touching criteria progress -- which stays
// per-character on purpose, since progress counters feed statistics.
//
// Feeds the perk-points cache too: g_earnedCache counts DISTINCT completed
// achievements per account through these very rows, so after one login per
// alt every character sees the same earned count.
void CatchUpAchievements(Player* player)
{
    if (!player || !player->GetSession())
        return;
    CharacterDatabase.Execute(
        "INSERT IGNORE INTO `character_achievement` (`guid`, `achievement`, `date`) "
        "SELECT me.`guid`, theirs.`achievement`, theirs.`date` "
        "FROM `character_achievement` theirs "
        "JOIN `characters` other ON other.`guid` = theirs.`guid` AND other.`account` = {} "
        "CROSS JOIN `characters` me ON me.`account` = other.`account` AND me.`guid` <> theirs.`guid` "
        "WHERE NOT EXISTS (SELECT 1 FROM `character_achievement` mine "
        "WHERE mine.`guid` = me.`guid` AND mine.`achievement` = theirs.`achievement`)",
        player->GetSession()->GetAccountId());
}

// Achievement titles, on every character on the account.
//
// SyncAchievementToAccount deliberately records the completion and nothing
// else, because the core's reward path also mails the item and five alts would
// mean five Albino Drakes. Titles carry no such hazard -- they are a
// per-character bitmask with nothing to duplicate -- so they are granted here
// instead, at login, over whatever the sync has already recorded.
//
// Runs over the completed set rather than reacting to the sync, so a character
// created long after the achievement was earned still picks the title up.
void GrantAchievementTitles(Player* player)
{
    if (!player)
        return;
    AchievementMgr* mgr = player->GetAchievementMgr();
    if (!mgr)
        return;
    uint32 granted = 0;
    for (auto const& completed : mgr->GetCompletedAchievements())
    {
        AchievementEntry const* achievement = sAchievementStore.LookupEntry(completed.first);
        if (!achievement)
            continue;
        AchievementReward const* reward = sAchievementMgr->GetAchievementReward(achievement);
        if (!reward)
            continue;
        // Only achievement 1793 indexes its titles by gender; every other one
        // is by faction. Same rule the core's own reward path applies.
        uint32 const titleId = reward->titleId[achievement->ID == 1793
            ? player->getGender() : uint8(player->GetTeamId())];
        if (!titleId)
            continue;
        CharTitlesEntry const* title = sCharTitlesStore.LookupEntry(titleId);
        if (!title || player->HasTitle(title))
            continue;
        player->SetTitle(title);
        ++granted;
    }
    if (granted)
        LOG_INFO("module.livinggear",
            "Living Gear: {} was granted {} account achievement title(s).",
            player->GetName(), granted);
}

void SaveLastRespec(uint32 accountId, uint32 when)
{
    if (!g_hasLastRespecCol)
        return;
    CharacterDatabase.DirectExecute(
        "INSERT INTO `lg_account_meta` (`account_id`, `last_respec`) VALUES ({}, {}) "
        "ON DUPLICATE KEY UPDATE `last_respec` = {}", accountId, when, when);
}

uint32 LoadPerkEpoch(uint32 accountId)
{
    if (!g_hasPerkEpochCol)
        return 0;
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT `perk_epoch` FROM `lg_account_meta` WHERE `account_id` = {}", accountId))
        return (*result)[0].Get<uint32>();
    return 0;
}

void SavePerkEpoch(uint32 accountId, uint32 epoch)
{
    if (!g_hasPerkEpochCol)
        return;
    CharacterDatabase.DirectExecute(
        "INSERT INTO `lg_account_meta` (`account_id`, `perk_epoch`) VALUES ({}, {}) "
        "ON DUPLICATE KEY UPDATE `perk_epoch` = {}", accountId, epoch, epoch);
}

// The client cannot price anything on its own -- lg_perk_cost is the only
// source of truth, and it is meant to be retuned without shipping a new addon.
// Batched for the same reason PKALL is: the addon-whisper channel silently
// truncates around 255 bytes.
void SendPerkCosts(Player* player)
{
    if (!player || !player->GetSession() || g_perkPrice.empty())
        return;
    std::string batch;
    for (auto const& entry : g_perkPrice)
    {
        std::string const next = Acore::StringFormat("{}:{}:{}",
            entry.first, entry.second.cost, entry.second.prereq);
        if (!batch.empty() && batch.size() + 1 + next.size() > 200)
        {
            SendLine(player, "PKCOST|" + batch);
            batch.clear();
        }
        if (!batch.empty())
            batch += ',';
        batch += next;
    }
    if (!batch.empty())
        SendLine(player, "PKCOST|" + batch);
}

void SendPerkPoints(Player* player)
{
    if (!player || !player->GetSession())
        return;
    uint32 const acc = player->GetSession()->GetAccountId();
    uint32 const earned = EarnedSkillPoints(acc);
    uint32 const spent = SpentSkillPoints(acc);
    SendLine(player, Acore::StringFormat("PKPTS|{}|{}|{}",
        earned > spent ? earned - spent : 0, earned, spent));
}

// Raising LivingGear.Perks.CostEpoch past what an account last saw refunds it
// in full on next login. Deliberately bypasses the respec cooldown: this is
// not the player's choice and must not burn their own respec.
void ApplyPerkEpoch(Player* player)
{
    if (!player || !player->GetSession() || !g_hasPerkPriceSchema || !g_hasPerkEpochCol)
        return;
    uint32 const acc = player->GetSession()->GetAccountId();
    if (LoadPerkEpoch(acc) >= g_cfg.perkCostEpoch)
        return;
    uint32 const spent = SpentSkillPoints(acc);
    RespecPerks(acc);
    SavePerkEpoch(acc, g_cfg.perkCostEpoch);
    if (spent)
        Say(player, "|cff66ccff[Account Perks]|r Perk costs changed - your points have been refunded.");
}

// A player's own DisplayId only carries the bare racial body model --
// skin/face/hair customization lives in PLAYER_BYTES-family fields that
// only exist on Player objects (a Creature's update-field array doesn't
// have room for them at all), so copying it onto a Creature-based clone
// renders as a blank, untextured white mesh -- confirmed live 2026-08-20.
// There is no way to get a pixel-exact match on a Creature; this picks a
// same-race/gender NPC model that actually has real textures instead, so
// it at least looks like a textured person of the right race/gender.
// Only Human is populated with any confidence right now (the only race
// actually tested); everything else falls back to the raw (broken) player
// DisplayId until someone tests a different race and it can be filled in.
uint32 LookAlikeDisplayId(Player* player)
{
    if (!player)
        return 0;
    if (player->getRace() == RACE_HUMAN)
        return player->getGender() == GENDER_FEMALE ? 3344 /* Priestess Anetta, Stormwind */
            : 3167 /* Stormwind City Guard */;
    return player->GetDisplayId();
}

// Highest rank owned, falling back to the rank a perk granted -- the rule from
// report #38: a perk casts what the player has actually learned, and a granted
// spell starts at rank 1 until they train better.
uint32 BestOwnedOr(Player* player, uint32 firstId);
bool IsRankOf(SpellInfo const* info, uint32 firstId);

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

uint32 BestOwnedOr(Player* player, uint32 firstId)
{
    uint32 const owned = BestOwned(player, firstId);
    return owned ? owned : firstId;
}

bool IsRankOf(SpellInfo const* info, uint32 firstId)
{
    if (!info)
        return false;
    uint32 const first = sSpellMgr->GetFirstSpellInChain(firstId);
    return first && sSpellMgr->GetFirstSpellInChain(info->Id) == first;
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

// Bug report #6, 2026-08-22: "level scaling didn't work in dungeon, sent me to
// deadmines at level 50+ and everything was still level 18/grey."
//
// Correct as written -- EffectiveCreatureLevel bailed on !OpenWorld(), so
// scaling had never applied inside an instance at all. That was survivable
// while the Dungeon Finder only offered content in your own bracket; once
// every outlevelled dungeon became queueable (core-patch 0019) it turned into
// the common case.
//
// Battlegrounds and arenas stay excluded. Rescaling players against each other
// is a different problem with a different answer, and nothing here is designed
// for it.
bool ScalableDungeon(Player* player)
{
    if (!player || !player->GetMap())
        return false;
    Map* map = player->GetMap();
    return map->IsDungeon() && !map->IsBattlegroundOrArena();
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

// Shared "is this creature eligible for zone scaling, and what level
// should it effectively be for this viewer" -- single source of truth for
// the displayed level (below), real combat damage exchanged (PerksUnit::
// OnDamage), and grey-kill XP (GrantScaledGreyKillXP), so all three always
// agree on the same number. Always roughly a rank above the viewer --
// "yellow, not trivial, not overwhelming" -- both up AND down from the
// creature's real level, so open-world zones stay relevant no matter how
// overleveled a player is, and mixed-level groups each get an
// appropriately-scaled fight on the same mob instead of the higher level
// just carrying. Returns 0 for "not eligible, use the real level."
uint32 EffectiveCreatureLevel(Creature const* creature, Player* viewer)
{
    if (!g_cfg.zoneEnable || !creature || !viewer || !creature->IsInWorld())
        return 0;

    bool const openWorld = OpenWorld(viewer);
    bool const dungeon = !openWorld && g_cfg.dungeonScale && ScalableDungeon(viewer);
    if (!openWorld && !dungeon)
        return 0;
    if (creature->IsPet() || creature->IsTotem() || creature->IsGuardian() || creature->IsSummon())
        return 0;
    if (CreatureTemplate const* tmpl = creature->GetCreatureTemplate())
        if (tmpl->type == CREATURE_TYPE_CRITTER)
            return 0;
    uint32 const viewerLevel = viewer->GetLevel();
    if (viewerLevel < g_cfg.zoneMinLevel)
        return 0;
    // Dungeons scale UP only, matching the Dungeon Finder rule: content you
    // have outlevelled is brought up to you, content above you is left exactly
    // as it is. Scaling a raid down to a level 20 who queued for it would be
    // the opposite of what was asked for. Open world keeps its existing
    // both-ways behaviour -- that is long-shipped and deliberate.
    if (dungeon && creature->GetLevel() >= viewerLevel)
        return 0;
    uint32 effective = viewerLevel + 1;
    if (effective > 80)
        effective = 80;
    return effective;
}

// Per-viewer displayed creature level (called from PerksUnit's
// ShouldTrackValuesUpdatePosByIndex/OnPatchValuesUpdate UnitScript hooks --
// Unit::PatchValuesUpdate already takes a per-target Player, so different
// players looking at the SAME creature simultaneously can each see a
// different level scaled to their own). Same eligibility as
// EffectiveCreatureLevel, plus display-only requires the creature actually
// be a valid attack target (a friendly town NPC keeps showing its real
// level).
uint32 DisplayLevelOverride(Unit const* unit, Player* viewer)
{
    Creature const* creature = unit ? unit->ToCreature() : nullptr;
    if (!creature)
        return 0;
    uint32 const effective = EffectiveCreatureLevel(creature, viewer);
    if (!effective)
        return 0;
    if (!viewer->IsValidAttackTarget(creature))
        return 0;
    return effective;
}

// Combat halves of zone scaling: rather than faking a creature's shared
// (not per-viewer) health pool, scale the damage exchanged in both
// directions by the ratio between its real-level stats and its
// effective-level stats -- computed via the same engine formulas
// Creature::SelectLevel uses at spawn (CreatureBaseStats::GenerateHealth/
// GenerateBaseDamage, Creature.cpp/CreatureData.h). Hits-to-kill and
// damage-taken both converge toward what they'd genuinely be at the
// displayed level, without ever touching UNIT_FIELD_MAXHEALTH itself, so
// there's no per-viewer desync on the shared health bar.
float OutgoingScaleRatio(Creature* creature, uint32 effectiveLevel)
{
    CreatureTemplate const* info = creature->GetCreatureTemplate();
    CreatureBaseStats const* realStats = sObjectMgr->GetCreatureBaseStats(creature->GetLevel(), info->unit_class);
    CreatureBaseStats const* effStats = sObjectMgr->GetCreatureBaseStats(effectiveLevel, info->unit_class);
    if (!realStats || !effStats)
        return 1.0f;
    float const effHp = float(effStats->GenerateHealth(info));
    if (effHp <= 0.0f)
        return 1.0f;
    return std::clamp(float(realStats->GenerateHealth(info)) / effHp, 0.05f, 4.0f);
}

float IncomingScaleRatio(Creature* creature, uint32 effectiveLevel)
{
    CreatureTemplate const* info = creature->GetCreatureTemplate();
    CreatureBaseStats const* realStats = sObjectMgr->GetCreatureBaseStats(creature->GetLevel(), info->unit_class);
    CreatureBaseStats const* effStats = sObjectMgr->GetCreatureBaseStats(effectiveLevel, info->unit_class);
    if (!realStats || !effStats)
        return 1.0f;
    float const realDmg = realStats->GenerateBaseDamage(info);
    if (realDmg <= 0.0f)
        return 1.0f;
    return std::clamp(effStats->GenerateBaseDamage(info) / realDmg, 0.2f, 4.0f);
}

// AzerothCore picks the base-XP constant from the content tier, not from
// the level: 45 for 1-60, 235 for 61-70, 580 for 71-80 (Formulas.h).
ContentLevels ContentForLevel(uint32 level)
{
    if (level > 70)
        return CONTENT_71_80;
    if (level > 60)
        return CONTENT_61_70;
    return CONTENT_1_60;
}

// What a kill should be worth at the level the killer is actually being
// SHOWN, which is the whole point of zone scaling. Returns 0 when scaling
// does not apply to this pair, in which case the engine's own number
// stands untouched.
//
// This used to be inlined into the grey-kill path with CONTENT_1_60
// hardcoded, so a level 71 killing an effective-72 mob was paid off a base
// of 45 instead of 580 -- under a third of the intended amount, before
// ZoneRewardMult then took another ~65% off in a starter zone. That is the
// "mob shows yellow and gives no XP" half of the complaint; the other half
// was that only fully-grey kills went through this at all (see
// PerksPlayer::OnPlayerGiveXP).
uint32 ScaledKillXP(Player* killer, Creature* killed)
{
    if (!killer || !killed)
        return 0;
    uint32 const eff = EffectiveCreatureLevel(killed, killer);
    if (!eff)
        return 0;
    // Everything Acore::XP::Gain applies on top of BaseGain and BaseGain
    // itself does not. This used to return the bare BaseGain, so a scaled
    // kill silently paid no elite bonus, ignored the creature's
    // ModExperience, and ignored Rate.XP.Kill -- an elite paid exactly what
    // the trash beside it paid.
    float mod = 1.0f;
    if (killed->isElite())
        mod *= 2.0f;
    if (CreatureTemplate const* tmpl = killed->GetCreatureTemplate())
        mod *= tmpl->ModExperience;
    mod *= sWorld->getRate(RATE_XP_KILL);
    uint32 const base = Acore::XP::BaseGain(uint8(killer->GetLevel()), uint8(eff), ContentForLevel(killer->GetLevel()));
    return uint32(float(base) * mod);
}

// Creatures that must never pay XP, no matter which path asks. Critters,
// anything owned by a player, and the NO_XP flag (training dummies and the
// like) -- the flag in particular is checked by Acore::XP::Gain but NOT by
// EffectiveCreatureLevel, so the scaled path has to exclude it itself.
bool CreatureNeverPaysXp(Creature* killed)
{
    if (!killed)
        return true;
    if (killed->IsPet() || killed->IsTotem() || killed->IsGuardian() || killed->IsSummon() || killed->IsCritter())
        return true;
    return killed->HasFlagsExtra(CREATURE_FLAG_EXTRA_NO_XP);
}

// THE single answer to "what is this kill worth to this player", used by both
// halves of kill XP: the engine's own reward (LivingGearPerks::OnPlayerGiveXP)
// and the grants the engine declined to make at all (GrantUnrewardedKillXp).
//
// Before this existed the two halves each had a piece of the rules and neither
// had all of them. The engine path got zone rescaling but not the floor; the
// grey path got neither the floor, the elite bonus, nor Rate.XP.Kill, because
// Player::GiveXP does not fire OnPlayerGiveXP (only KillRewarder, quests,
// exploration and BG bonus honor do), so every hook-based rule silently
// skipped the exact kills that needed it most. A level 60 in a starter zone
// was being paid 131 XP against a 290,000 bar -- 0.045%, or 2,200 kills per
// level -- for a mob the same module had already scaled to level 61 in both
// display and damage. That is what "the scaling is a facade" meant.
//
// engineAmount is what AzerothCore itself calculated, or 0 when it never ran.
uint32 KillXpFor(Player* killer, Creature* killed, uint32 engineAmount)
{
    if (!killer || !killed)
        return engineAmount;
    if (killer->GetLevel() >= sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
        return 0;
    if (CreatureNeverPaysXp(killed))
        return engineAmount;

    uint32 xp = engineAmount;
    // Rebase onto the level the player is actually being SHOWN. Replaces
    // rather than maxes: scaling runs both ways in the open world, and a
    // creature scaled DOWN to the viewer is supposed to pay less.
    if (uint32 const scaled = ScaledKillXP(killer, killed))
        xp = scaled;
    xp = uint32(float(xp) * ZoneRewardMult(killer));

    // The floor. Expressed against PLAYER_NEXT_LEVEL_XP so it stays
    // meaningful at every level -- a flat number would be a fortune at 5 and
    // a rounding error at 75. Elites are worth double a trash mob here for
    // the same reason Acore::XP::Gain doubles them.
    if (g_cfg.killFloor)
    {
        uint32 const pct = killed->isElite() ? g_cfg.killFloorElitePct : g_cfg.killFloorPct;
        if (pct)
        {
            uint64 const bar = killer->GetUInt32Value(PLAYER_NEXT_LEVEL_XP);
            uint32 const floor = uint32(bar * pct / 100);
            if (xp < floor)
                xp = floor;
        }
    }
    return xp;
}

// The module's own XP multipliers, for the grant path that never reaches
// OnPlayerGiveXP. Every one of these is applied to a normal kill by some
// script's hook; a direct Player::GiveXP fires none of them, so they have to be
// applied by hand here or the grey half of the world quietly loses them.
uint32 ApplyOffHookXpMultipliers(Player* player, uint32 xp)
{
    if (!player || !xp)
        return xp;
    float mult = ::LivingGear_LevelingXpMultiplier(player);
    if (Aura* pace = player->GetAura(SPELL_DUNGEON_PACE))
        if (AuraEffect* e = pace->GetEffect(EFFECT_0))
            mult *= 1.0f + float(e->GetAmount() + 1) / 100.0f;
    if (mult <= 1.0f)
        return xp;
    uint32 const scaled = uint32(float(xp) * mult);
    return scaled ? scaled : xp;
}

// Did AzerothCore's own KillRewarder already pay this player for this kill?
//
// Mirrors KillRewarder::_RewardGroup/_RewardXP exactly, because the answer
// decides whether the module grants on top (double XP if this is wrong) or
// stays out of the way (no XP at all if this is wrong). The rules are:
//   * solo   -- paid unless Acore::XP::Gain returned zero, i.e. the victim
//               was grey to the killer;
//   * group  -- the engine picks the highest-level ALIVE member within reward
//               distance for whom the victim is not grey, pays off that
//               member's level, and pays nobody at all if there is no such
//               member. Individual members are then skipped if they are dead
//               or out-level that reference member.
bool CoreRewardedKillXp(Player* member, Creature* killed, Player* maxNotGray, uint32 maxNotGrayLevel)
{
    if (!member || !killed)
        return true;
    if (!member->GetGroup())
        return Acore::XP::Gain(member, killed) != 0;
    if (!maxNotGray)
        return false;
    if (!member->IsAlive())
        return false;
    return maxNotGrayLevel >= member->GetLevel();
}

// XP for every kill the engine declined to pay for.
//
// AzerothCore's kill-XP formula hard-zeroes a grey kill (mob_level <=
// GetGrayLevel(playerLevel)) and KillRewarder then skips the OnPlayerGiveXP
// hook entirely (`if (xp)`), so no hook-based rule can ever reach those
// kills. In a low-level zone EVERY mob is grey -- GetGrayLevel(60) is 51 --
// which is precisely where zone scaling is supposed to be doing its work.
//
// Group-aware on purpose. This used to grant to the killing blow only, while
// KillRewarder zeroes a grey kill for the WHOLE group (_maxNotGrayMember is
// null, so every member gets nothing). With playerbots in the party that
// meant every kill a bot finished paid the player exactly zero -- the
// "I don't get XP from half the mobs I kill" report.
void GrantUnrewardedKillXp(Player* killer, Creature* killed)
{
    if (!killer || !killed || CreatureNeverPaysXp(killed))
        return;

    Group* group = g_cfg.groupKillXp ? killer->GetGroup() : nullptr;
    if (!group)
    {
        if (!killer->IsAlive() || CoreRewardedKillXp(killer, killed, nullptr, 0))
            return;
        if (uint32 const xp = ApplyOffHookXpMultipliers(killer, KillXpFor(killer, killed, 0)))
            killer->GiveXP(xp, killed);
        return;
    }

    // Same reference member the engine would have used.
    Player* maxNotGray = nullptr;
    uint32 maxNotGrayLevel = 0;
    for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        Player* member = itr->GetSource();
        if (!member || !member->IsAlive())
            continue;
        if (member != killer && !member->IsAtGroupRewardDistance(killed))
            continue;
        if (killed->GetLevel() <= Acore::XP::GetGrayLevel(uint8(member->GetLevel())))
            continue;
        if (!maxNotGray || maxNotGrayLevel < member->GetLevel())
        {
            maxNotGray = member;
            maxNotGrayLevel = member->GetLevel();
        }
    }

    for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        Player* member = itr->GetSource();
        if (!member || !member->IsAlive())
            continue;
        if (member != killer && !member->IsAtGroupRewardDistance(killed))
            continue;
        if (CoreRewardedKillXp(member, killed, maxNotGray, maxNotGrayLevel))
            continue;
        // Deliberately NOT split across the group. Every member's share is
        // computed against their own level and their own zone multiplier,
        // which is the same answer they would get soloing the mob. Grouping
        // up is meant to be rewarded here, not taxed.
        if (uint32 const xp = ApplyOffHookXpMultipliers(member, KillXpFor(member, killed, 0)))
            member->GiveXP(xp, killed);
    }
}

// Kill Combo (910089) was removed on 2026-08-23. It was two rewards wearing
// one coat: a stacking kill-XP bonus, now made redundant by the flat kill
// floor in KillXpFor, and a stacking movement-speed bonus, now the speed half
// of the Wayfarer balance perk (LivingGear_Amenities.cpp). What is gone with
// it: g_combo/g_groupCombo/g_comboShown, the per-second RecastCombo tick, and
// the `lg_combo` round-trip across logout. The table and any existing
// lg_account_perk rows are left alone -- nothing reads them now.

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
    // 2026-08-21: isDead() is the death-state flag, not ghost status -- a
    // released spirit is alive-but-ghost in engine terms, so none of these
    // guards caught it. OnPlayerLeaveCombat firing on a post-death combat
    // exit let this cast a mount spell on a ghost, a state the client
    // never expects -- suspected cause of "stuck, needs relog" after dying.
    if (player->IsInCombat() || player->IsMounted() || player->isDead() || player->HasPlayerFlag(PLAYER_FLAGS_GHOST))
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

// Stealth's built-in movement-speed penalty is zeroed out for Subtlety
// perk holders (requested: match, and exceed via real talents, normal
// movement speed while stealthed). Neutralizes whatever negative speed
// effect(s) the Stealth aura carries rather than hardcoding a percentage,
// so it stays correct regardless of rank/exact DBC value. Leaves every
// other speed source (talents, other auras) untouched, so real speed
// talents still stack on top normally.
void NeutralizeStealthSpeed(Unit* unit, Aura* aura)
{
    if (!unit || !aura || !unit->IsPlayer())
        return;
    if (aura->GetSpellInfo()->Id != SPELL_STEALTH)
        return;
    if (GetClassPerk(unit->ToPlayer()) != SPELL_SUBTLETY)
        return;
    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
    {
        AuraEffect* eff = aura->GetEffect(i);
        if (!eff)
            continue;
        AuraType const type = eff->GetAuraType();
        if ((type == SPELL_AURA_MOD_DECREASE_SPEED || type == SPELL_AURA_MOD_SPEED_ALWAYS
            || type == SPELL_AURA_MOD_INCREASE_SPEED) && eff->GetAmount() < 0)
            eff->ChangeAmount(0);
    }
}

// True for anything a player would call crowd control: stuns, roots, fears,
// charms, sleeps, polymorphs, snares and silences.
//
// Two tests, because neither alone covers the field. The mechanic mask is
// the authoritative one and is exactly the set the engine itself calls
// "movement impairment and loss of control", but a fair number of 3.3.5
// spells carry MECHANIC_NONE and express the CC purely through the aura
// type, so those are matched by effect as well.
bool IsCrowdControlAura(Aura const* aura)
{
    SpellInfo const* info = aura ? aura->GetSpellInfo() : nullptr;
    if (!info || info->IsPositive())
        return false;
    if (info->GetAllEffectsMechanicMask() & IMMUNE_TO_MOVEMENT_IMPAIRMENT_AND_LOSS_CONTROL_MASK)
        return true;
    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
    {
        AuraEffect const* eff = aura->GetEffect(i);
        if (!eff)
            continue;
        switch (eff->GetAuraType())
        {
            case SPELL_AURA_MOD_STUN:
            case SPELL_AURA_MOD_ROOT:
            case SPELL_AURA_MOD_FEAR:
            case SPELL_AURA_MOD_CONFUSE:
            case SPELL_AURA_MOD_CHARM:
            case SPELL_AURA_MOD_POSSESS:
            case SPELL_AURA_MOD_PACIFY:
            case SPELL_AURA_MOD_SILENCE:
            case SPELL_AURA_MOD_PACIFY_SILENCE:
                return true;
            case SPELL_AURA_MOD_DECREASE_SPEED:
                if (eff->GetAmount() < 0)
                    return true;
                break;
            default:
                break;
        }
    }
    return false;
}

// *CC Reduction (910172): 95% off the duration of every crowd control effect
// that lands on the player. Done here rather than as a pile of
// SPELL_AURA_MECHANIC_DURATION_MOD effects in spell_dbc because that aura is
// one mechanic per effect and a spell only has three effects -- covering the
// ~14 control mechanics that way is not possible at all.
//
// Only ever shortens: an aura that already resolved to less than 5% of its
// base (diminishing returns did most of the work, or a resist shortened it)
// is left alone rather than being lengthened back up.
void ReduceCrowdControl(Unit* unit, Aura* aura)
{
    if (!unit || !aura || !unit->IsPlayer())
        return;
    if (!IsCrowdControlAura(aura))
        return;
    Player* player = unit->ToPlayer();
    // Unlock on the way through: the first thing that ever crowd controls
    // you is both the unlock condition and the first effect it applies to.
    UnlockPerk(player, SPELL_CC_REDUCTION,
        "|cff66ccff[Account Perks]|r *CC Reduction unlocked -- crowd control lasts 95% less on you.", false);
    if (!HasPerk(player, SPELL_CC_REDUCTION))
        return;

    int32 const maxDuration = aura->GetMaxDuration();
    if (maxDuration <= 0) // permanent (-1) or already instant -- nothing to cut
        return;
    // Floor at 1ms: 0 means "permanent" to Aura, so rounding a very short
    // stun down to nothing would turn it into a forever-stun.
    int32 const reduced = std::max<int32>(1, int32(float(maxDuration) * (1.0f - CC_REDUCTION_PCT)));
    if (reduced >= maxDuration)
        return;
    aura->SetMaxDuration(reduced);
    if (aura->GetDuration() > reduced)
        aura->SetDuration(reduced);
}

// SummonCloneWithDisplay() and ChainAmbushHit() were removed 2026-08-22 with
// the chain-Ambush they existed for (bug report #9 -- Shadowstep no longer
// deals damage). Between them they summoned up to eight short-lived clones per
// cast, mirrored the player's weapons onto each so a weapon-damage ability had
// something to swing, forced the damage through by hand, and hand-fed the
// threat table so targets did not evade a summon that vanished a moment later.
// The Shadow Clone PET is unrelated and still very much alive further down --
// it is a real SUMMON_PET, not one of these throwaway TempSummons.

// 2026-08-21: the 6s cooldown override used to live inside ChainAmbushImpl,
// which only runs when a chain-ambush target actually resolves -- if the
// player casts Shadowstep with no target, or the target guid stops
// resolving one tick later, that whole path was skipped and the real
// client-side Shadowstep cooldown (not present in our spell_dbc table at
// all -- it's a stock spell resolved from the compiled DBC, ~30s) was left
// standing. Split out into its own function that always runs, independent
// of whether the chain-ambush effect itself fires.
static void ApplyShadowstepCooldown(Player* player)
{
    if (!player || GetClassPerk(player) != SPELL_SUBTLETY)
        return;
    ObjectGuid playerGuid = player->GetGUID();
    // Still has to be deferred rather than set from OnPlayerSpellCast
    // directly -- that fires mid-way through Shadowstep's own cast(), before
    // it's applied its own cooldown, so setting one there just gets
    // overwritten once cast() finishes. 300ms gives ample margin for the
    // engine's own real cooldown-set to have already happened.
    //
    // 2026-08-21: AddSpellCooldown's needSendToClient flag does NOT push a
    // live packet -- _AddSpellCooldown (Player.cpp) only ever *stores* it on
    // the cooldown entry, and the only place that flag is ever read is
    // SendInitialSpells, i.e. login/spec-change. So the server's own
    // castability was correctly set to 6s, but the client's action-bar
    // swirl never got corrected -- it kept counting down from the real
    // ~30s it originally predicted from the compiled DBC, and the client
    // blocks *sending* another cast attempt locally while it thinks a
    // spell is still on cooldown. Switched to ModifySpellCooldown, which
    // DOES send SMSG_MODIFY_COOLDOWN live -- read the real remaining
    // cooldown first and apply the exact delta needed to land on
    // SHADOWSTEP_COOLDOWN_MS.
    player->m_Events.AddEventAtOffset([playerGuid]()
    {
        Player* p = ObjectAccessor::FindPlayer(playerGuid);
        if (!p || !p->IsInWorld())
            return;
        int32 const remaining = int32(p->GetSpellCooldownDelay(SPELL_SHADOWSTEP));
        int32 const delta = int32(SHADOWSTEP_COOLDOWN_MS) - remaining;
        if (delta != 0)
            p->ModifySpellCooldown(SPELL_SHADOWSTEP, delta);
    }, std::chrono::milliseconds(300));
}

// Report #73 (redesign 2026-08-26): "just make the pickpocket skill
// automatically pickpocket all mobs within 10 yards when used." A real
// button beat a stealth aura: casting Pickpocket (921) hits every humanoid
// within PICKPOCKET_AOE_RADIUS at once. Humanoids only -- that is what
// Pickpocket can target at all. Reuses the Shadowstep pickpocket shape
// (junkboxes autoloot to the reagent vault via core-patch 0010).
static void PickpocketAoE(Player* player)
{
    if (!LivingGear_SafeToCastOn(player) || GetClassPerk(player) != SPELL_SUBTLETY)
        return;

    std::list<Unit*> around;
    Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck check(player, player, PICKPOCKET_AOE_RADIUS);
    Acore::UnitListSearcher<Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck> searcher(player, around, check);
    Cell::VisitObjects(player, searcher, PICKPOCKET_AOE_RADIUS);

    uint32 cast = 0;
    for (Unit* u : around)
    {
        if (!u || !u->IsAlive() || !player->IsValidAttackTarget(u))
            continue;
        if (u->GetCreatureType() != CREATURE_TYPE_HUMANOID)
            continue;
        player->CastSpell(u, SPELL_PICKPOCKET, true);
        ++cast;
    }
    LOG_DEBUG("module.livinggear", "pickpocket aoe: {} humanoid(s) in {} yards pickpocketed",
        cast, uint32(PICKPOCKET_AOE_RADIUS));
}

// Report #43, a new Subtlety effect: "Eviscerate applies a 5 point Slice and
// Dice and Rupture to all enemies within 15 yards."
//
// Slice and Dice is a self-buff and Rupture is the bleed, so the finisher
// blankets the pull with the bleed and leaves the rogue swinging faster. Cast
// at the combo-point value the finisher was actually spent at where the engine
// allows it; the spread copies land at full value via CastSpell.
static void EviscerateSpreadImpl(ObjectGuid playerGuid)
{
    Player* player = ObjectAccessor::FindPlayer(playerGuid);
    if (!LivingGear_SafeToCastOn(player) || GetClassPerk(player) != SPELL_SUBTLETY)
        return;

    uint32 const rupture = BestOwnedOr(player, SPELL_RUPTURE_R1);
    uint32 const snd = BestOwnedOr(player, SPELL_SLICE_AND_DICE_R1);

    // Slice and Dice is on the rogue, not the pull.
    if (snd)
        player->CastSpell(player, snd, true);

    std::list<Unit*> around;
    Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck check(player, player, HEMO_RADIUS);
    Acore::UnitListSearcher<Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck> searcher(player, around, check);
    Cell::VisitObjects(player, searcher, HEMO_RADIUS);
    for (Unit* u : around)
    {
        if (!u || !u->IsAlive() || !player->IsValidAttackTarget(u))
            continue;
        if (rupture)
            player->CastSpell(u, rupture, true);
    }
}

void EviscerateSpread(Player* player)
{
    if (!player || GetClassPerk(player) != SPELL_SUBTLETY)
        return;
    ObjectGuid const playerGuid = player->GetGUID();
    player->m_Events.AddEventAtOffset([playerGuid]()
    {
        EviscerateSpreadImpl(playerGuid);
    }, std::chrono::milliseconds(1));
}

// Report #44: Garrote and Rupture hit for twenty-one times as much, and tick
// faster with haste WITHOUT the duration shrinking.
//
// The second half is the fiddly one. SPELL_ATTR5_SPELL_HASTE_AFFECTS_PERIODIC
// is WotLK's built-in answer and it does shorten the duration, which is
// explicitly not what was asked for. AuraEffect has no amplitude setter, but it
// does expose SetPeriodicTimer -- so after each tick we pull the next one
// forward. Duration is never touched, so the bleed simply gets more ticks.
void ApplySubtletyBleed(Unit* target, Unit* attacker, uint32& damage, SpellInfo const* info)
{
    if (!target || !attacker || !damage || !info)
        return;
    if (!IsRankOf(info, SPELL_GARROTE) && !IsRankOf(info, SPELL_RUPTURE_R1))
        return;
    Player* player = attacker->ToPlayer();
    if (!player || GetClassPerk(player) != SPELL_SUBTLETY)
        return;

    damage *= SUBTLETY_BLEED_MULT;

    float const haste = player->GetRatingBonusValue(CR_HASTE_MELEE);
    if (haste <= 0.0f)
        return;
    if (AuraEffect* eff = target->GetAuraEffect(info->Id, EFFECT_0, player->GetGUID()))
    {
        int32 const amplitude = eff->GetAmplitude();
        if (amplitude > 0)
        {
            int32 const faster = int32(float(amplitude) / (1.0f + haste / 100.0f));
            eff->SetPeriodicTimer(std::max(200, faster));
        }
    }
}

static void HemorrhageAoEImpl(ObjectGuid playerGuid)
{
    Player* player = ObjectAccessor::FindPlayer(playerGuid);
    if (!LivingGear_SafeToCastOn(player) || GetClassPerk(player) != SPELL_SUBTLETY)
        return;
    // Bug report #10, 2026-08-22: Hemorrhage applies a +500% Ambush and the
    // Garrote bleed to everything within 10 yards. Previously it spread Garrote
    // and Pickpocket; Pickpocket moved to Shadowstep (report #9) and Ambush
    // takes its place here.
    uint32 garrote = BestOwned(player, SPELL_GARROTE);
    if (!garrote)
        garrote = SPELL_GARROTE;
    uint32 ambush = BestOwned(player, SPELL_AMBUSH);
    if (!ambush)
        ambush = SPELL_AMBUSH;

    // Cast from the PLAYER, not from a summoned clone. Ambush is weapon-damage
    // based, and the old chain-Ambush code had to mirror the player's weapons
    // onto each clone and then force the damage through by hand precisely
    // because a clone has nothing to swing. The player is already holding the
    // right weapons, so the engine's own damage calculation just works.
    int32 dmg = int32(player->GetLevel() * 12.0f * HEMO_AMBUSH_MULT);
    if (SpellInfo const* ambushInfo = sSpellMgr->GetSpellInfo(ambush))
        dmg = int32(float(std::max(1, ambushInfo->Effects[EFFECT_0].CalcValue(player))) * HEMO_AMBUSH_MULT);

    LivingGear_DiagBump(player, "hemo.impl");
    Unit* const original = player->GetVictim();
    std::list<Unit*> around;
    Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck check(player, player, HEMO_RADIUS);
    Acore::UnitListSearcher<Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck> searcher(player, around, check);
    Cell::VisitObjects(player, searcher, HEMO_RADIUS);
    for (Unit* u : around)
    {
        if (!u || !u->IsAlive() || !player->IsValidAttackTarget(u))
            continue;

        // Bug reports #28/#42/#90, log forensics 2026-08-26. The failure log
        // was dominated by three results with distinct, fixable causes:
        //
        //   result 30 EQUIPPED_ITEM_CLASS_MAINHAND (~532k) -- Ambush demands a
        //     dagger and the ATTR3 weapon check ignored the waive flag. Fixed
        //     in the core (core-patch 0026), so this spread now works from any
        //     weapon.
        //   result 97 OUT_OF_RANGE / 134 UNIT_NOT_INFRONT (~115k) -- Ambush
        //     and Garrote are melee-range, in-front spells but this loop cast
        //     them at everything within HEMO_RADIUS regardless of geometry.
        //     Now gated to targets actually inside melee range AND inside the
        //     player's front arc; Hemorrhage itself still spreads to the full
        //     radius.
        //   result 12 BAD_TARGETS on undead/mechanical/elemental (~1.7k) --
        //     bleeds cannot land on creatures immune to bleed mechanics;
        //     skipping those instead of spamming failures.
        if (!player->IsWithinMeleeRange(u))
            continue;
        if (!player->HasInArc(static_cast<float>(M_PI), u))
            continue;
        switch (u->GetCreatureType())
        {
            case CREATURE_TYPE_UNDEAD:
            case CREATURE_TYPE_MECHANICAL:
            case CREATURE_TYPE_ELEMENTAL:
                continue;
            default:
                break;
        }
        LivingGear_DiagBump(player, "hemo.hit");

        // Ambush is dagger-only (EquippedItemSubClassMask 0x8000), and
        // TRIGGERED_IGNORE_EQUIPPED_ITEM_REQUIREMENT (0x00080000) sits OUTSIDE
        // TRIGGERED_FULL_MASK (0x0007FFFF) -- so `triggered = true` does NOT
        // waive it. Every Ambush this spread was silently failing with
        // SPELL_FAILED_EQUIPPED_ITEM_CLASS for any Rogue not holding a dagger
        // in the main hand, and silently because TRIGGERED_DONT_REPORT_CAST_ERROR
        // IS inside the mask. Diagnostics showed 13 casts, 57 targets hit and
        // nothing landing, which is what sent us looking here.
        //
        // Hemorrhage itself takes axes, maces, swords, staves and fists, so
        // "spread an Ambush" cannot sensibly mean "only if you brought a
        // dagger". The requirement is waived; the damage still comes from
        // whatever weapon the player is actually holding. Waiving it is now
        // also honoured by CheckItems' weapon-presence check -- core-patch
        // 0026 -- which is what finally let these casts through.
        TriggerCastFlags const spreadFlags =
            TriggerCastFlags(TRIGGERED_FULL_MASK | TRIGGERED_IGNORE_EQUIPPED_ITEM_REQUIREMENT);

        CustomSpellValues ambushValues;
        ambushValues.AddSpellMod(SPELLVALUE_BASE_POINT0, dmg);
        SpellCastResult const ambushResult =
            player->CastCustomSpell(ambush, ambushValues, u, spreadFlags);
        if (ambushResult != SPELL_CAST_OK)
            LOG_INFO("module.livinggear", "hemo: ambush {} on {} failed, result {}",
                ambush, u->GetName(), uint32(ambushResult));

        // The bleed's 1000% multiplier is NOT applied here -- it lives in
        // ModifyPeriodicDamageAurasTick so it covers every Garrote equally.
        SpellCastResult const garroteResult = player->CastSpell(u, garrote, spreadFlags);
        if (garroteResult != SPELL_CAST_OK)
            LOG_INFO("module.livinggear", "hemo: garrote {} on {} failed, result {}",
                garrote, u->GetName(), uint32(garroteResult));

        // Report #41: "...and also apply Hemorrhage itself to all enemies."
        // Skip the original target, which already has it from the cast that
        // triggered this spread.
        if (u != original)
        {
            uint32 const hemo = BestOwnedOr(player, SPELL_HEMORRHAGE);
            if (hemo)
                player->CastSpell(u, hemo, spreadFlags);
        }
    }
}

// Deferred the same way ChainAmbush is -- OnPlayerSpellCast fires mid-way
// through Hemorrhage's own Spell::cast(), and casting more spells on the
// same player from in there is a reentrant call into the aura/spell
// system on a unit that's still "in progress" (the established
// Unit::_AddAura assert crash this file's other deferred casts all guard
// against the same way).
void ApplyHemorrhageAoE(Player* player)
{
    if (!player || GetClassPerk(player) != SPELL_SUBTLETY)
        return;
    ObjectGuid playerGuid = player->GetGUID();
    player->m_Events.AddEventAtOffset([playerGuid]()
    {
        HemorrhageAoEImpl(playerGuid);
    }, std::chrono::milliseconds(1));
}

// Shadow Dance's +10% attack power half. Mirrors LivingGear_Next.cpp's
// existing Class Buffs pattern exactly (ShouldHaveClassBuff/
// ApplyClassBuffs/TickClassBuffs) -- self or any group member within 100
// yards having the perk grants it to this player, applied via a native
// stat-percent modifier rather than a real spell/aura.
bool ShouldHaveShadowDanceBuff(Player* player)
{
    if (!player || !player->IsAlive())
        return false;
    if (GetClassPerk(player) == SPELL_SUBTLETY && HasPerk(player, SPELL_SHADOW_DANCE))
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
        if (GetClassPerk(member) == SPELL_SUBTLETY && HasPerk(member, SPELL_SHADOW_DANCE))
            return true;
    }
    return false;
}

// 2026-08-22: this used to call Player::ApplyStatPctModifier directly. The
// stat change was real, but a raw modifier has no icon, no tooltip and no
// timer -- from the player's chair an invisible +10% is indistinguishable
// from a perk that never turned on, which is exactly how it kept getting
// reported ("no buff, nothing in the spellbook"). SPELL_SHADOW_DANCE_BUFF
// is a real aura carrying MOD_ATTACK_POWER_PCT, so the engine applies the
// stat AND the client draws it in the buff bar. g_shadowDanceBuffOn is kept
// as the edge detector so refreshing every tick doesn't re-flash the icon.
void ApplyShadowDanceBuff(Player* player, bool apply)
{
    if (!player)
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    bool const wasOn = g_shadowDanceBuffOn[guid];
    if (wasOn == apply && (!apply || player->HasAura(SPELL_SHADOW_DANCE_BUFF)))
        return;
    g_shadowDanceBuffOn[guid] = apply;
    if (apply)
    {
        if (sSpellMgr->GetSpellInfo(SPELL_SHADOW_DANCE_BUFF))
            player->CastSpell(player, SPELL_SHADOW_DANCE_BUFF, true);
    }
    else
        player->RemoveAurasDueToSpell(SPELL_SHADOW_DANCE_BUFF);
}

void TickShadowDanceBuff(Player* player)
{
    if (!player || !player->GetSession())
        return;
    ApplyShadowDanceBuff(player, ShouldHaveShadowDanceBuff(player));
}

// Shadow Dance's "openers usable without stealth" half. Called from a
// small core patch in Spell::CheckCast (Spell.cpp) -- self only, does NOT
// extend to the whole party like the attack power buff above, since this
// is about the caster's own ability to act, not a shared buff.
bool BypassStealthRequirement(Unit* caster)
{
    if (!caster)
        return false;
    Player* player = caster->ToPlayer();
    if (!player)
        return false;
    return GetClassPerk(player) == SPELL_SUBTLETY && HasPerk(player, SPELL_SHADOW_DANCE);
}

// Rebuilt 2026-08-20/21: was a bare TempSummon + ScriptedAI ("not a real
// playerbot" -- mod-playerbots hard-requires a real Player+WorldSession, so
// that door is closed). This is a genuinely different approach: a real
// SUMMON_PET-type Pet (same engine object model as a Warlock demon --
// Spell::EffectSummonPet is the reference this mirrors), which gets the
// native pet frame with real Aggressive/Defensive/Passive stance buttons
// for free -- that's what actually gives the player aggro-level control,
// no custom UI needed. npc_lg_shadow_cloneAI (below) still drives target
// selection and which Rogue ability to cast, but now gates its own
// eagerness to engage on me->GetReactState() so the stance buttons
// actually mean something.
void SummonClone(Player* player)
{
    if (!player || GetClassPerk(player) != SPELL_SUBTLETY)
        return;
    auto it = g_cloneGuid.find(player->GetGUID().GetCounter());
    if (it != g_cloneGuid.end())
        if (Creature* c = player->GetMap()->GetCreature(it->second))
            c->DespawnOrUnsummon();
    if (Pet* existing = player->GetPet())
        existing->Remove(PET_SAVE_AS_DELETED);

    Pet* pet = player->SummonPet(NPC_SHADOW_CLONE, player->GetPositionX(), player->GetPositionY(),
        player->GetPositionZ(), player->GetOrientation(), SUMMON_PET);
    if (!pet)
        return;

    // Same model-swap-glitch note as before this rework: set the display
    // before anything else touches the pet, while it's still brand new.
    pet->SetDisplayId(LookAlikeDisplayId(player));
    pet->SetNativeDisplayId(LookAlikeDisplayId(player));
    pet->SetUInt32Value(UNIT_CREATED_BY_SPELL, SPELL_SHADOW_CLONE);
    pet->SetReactState(REACT_DEFENSIVE);
    pet->SetFaction(player->GetFaction());
    pet->SetLevel(player->GetLevel());
    pet->SetMaxHealth(player->GetMaxHealth());
    pet->SetHealth(player->GetMaxHealth());
    if (Item* mh = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND))
        pet->SetVirtualItem(0, mh->GetEntry());
    if (Item* oh = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND))
        pet->SetVirtualItem(1, oh->GetEntry());
    if (Item* rh = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED))
        pet->SetVirtualItem(2, rh->GetEntry());
    pet->SetPower(POWER_ENERGY, player->GetMaxPower(POWER_ENERGY));
    // SetVirtualItem above is purely cosmetic (what weapon model it holds) --
    // a Creature/Pet's actual melee damage comes from its own weapon-damage
    // stat, not from any item, so without this it hits for whatever the
    // 910201 creature_template's near-nothing base damage is regardless of
    // what it's visibly holding. Copy the owner's real current weapon
    // damage range directly.
    pet->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, player->GetWeaponDamageRange(BASE_ATTACK, MINDAMAGE));
    pet->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, player->GetWeaponDamageRange(BASE_ATTACK, MAXDAMAGE));
    pet->UpdateDamagePhysical(BASE_ATTACK);
    player->PetSpellInitialize(); // native pet frame: stance + follow/stay/attack commands
    g_cloneGuid[player->GetGUID().GetCounter()] = pet->GetGUID();
}

// Which cooking tier this account has earned: 1% of max health and mana per
// second per tier, at 75/150/225/300/375/450 skill. Returns 0 for "no tier".
uint32 CookingTier(Player* player)
{
    if (!player)
        return 0;
    uint32 const skill = AccountMaxSkill(player, SKILL_COOKING);
    for (uint32 i = 6; i-- > 0; )
        if (skill >= COOK_BREAKS[i] && HasPerk(player, SPELL_COOK[i]))
            return i + 1;
    return 0;
}

// 2026-08-22: reworked from a hand-rolled ModifyHealth/ModifyPower tick to a
// real aura feeding the engine's own regeneration. The old version restored
// the right amount but was invisible -- no icon, no tooltip, nothing to
// check -- so "is cooking regen even on?" was unanswerable without the debug
// prints this replaces.
//
// SPELL_AURA_MOD_REGEN and SPELL_AURA_MOD_POWER_REGEN are the two aura types
// Player::RegenerateHealth/Regenerate already read for natural regen, and
// both are denominated per 5 seconds -- hence the x5 below to express a
// per-second percentage. Letting the engine own it also means the health
// half correctly stops in combat on its own (RegenerateHealth only adds
// MOD_REGEN when !IsInCombat()), which the old manual combat bail was
// approximating by hand.
//
// Base points depend on max health/mana, which move with gear and level, so
// the aura is recast whenever the computed amounts drift or the aura has
// gone missing -- not blindly every tick, which would re-flash the icon.
// First Aid "Cleanse" (910048): "While bandaged, remove debuffs every second."
//
// Scoped to what a bandage plausibly helps with -- disease, poison and bleeds.
// Deliberately NOT a blanket harmful-aura strip: that would shrug off boss
// mechanics, crowd control and every scripted debuff in the game, which is a
// far bigger change than the description promises.
//
// One per tick, oldest first, so it reads as steadily cleaning up rather than
// wiping everything the instant a bandage lands.
void TickFirstAidCleanse(Player* player, uint32& acc, uint32 diff)
{
    if (!player || !player->IsAlive())
        return;
    acc += diff;
    if (acc < AID_CLEANSE_MS)
        return;
    acc = 0;
    if (!player->HasAuraWithMechanic(1 << MECHANIC_BANDAGE))
        return;
    if (!HasPerk(player, SPELL_AID_CLEANSE))
        return;

    uint32 const cleansable = (1 << DISPEL_DISEASE) | (1 << DISPEL_POISON);
    Unit::AuraApplicationMap const& applications = player->GetAppliedAuras();
    for (auto const& pair : applications)
    {
        AuraApplication const* app = pair.second;
        if (!app || app->IsPositive())
            continue;
        Aura* aura = app->GetBase();
        if (!aura)
            continue;
        SpellInfo const* info = aura->GetSpellInfo();
        if (!info)
            continue;
        bool const bleed = info->GetAllEffectsMechanicMask() & (1 << MECHANIC_BLEED);
        if (!bleed && !((1 << info->Dispel) & cleansable))
            continue;
        player->RemoveAura(pair.first);
        return; // one per second
    }
}

void TickCooking(Player* player, uint32 diff)
{
    if (!player)
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    g_cookAcc[guid] += diff;
    if (g_cookAcc[guid] < COOK_REFRESH_MS)
        return;
    g_cookAcc[guid] = 0;

    uint32 const tier = CookingTier(player);
    if (!tier)
    {
        if (player->HasAura(SPELL_COOK_REGEN))
            player->RemoveAurasDueToSpell(SPELL_COOK_REGEN);
        g_cookAmount.erase(guid);
        return;
    }
    if (!sSpellMgr->GetSpellInfo(SPELL_COOK_REGEN))
        return;

    int32 const hpPer5 = int32(player->CountPctFromMaxHealth(tier)) * 5;
    int32 const manaPer5 = int32(CalculatePct(player->GetMaxPower(POWER_MANA), tier)) * 5;
    uint64 const want = (uint64(uint32(hpPer5)) << 32) | uint32(manaPer5);
    auto const cached = g_cookAmount.find(guid);
    if (cached != g_cookAmount.end() && cached->second == want && player->HasAura(SPELL_COOK_REGEN))
        return;
    g_cookAmount[guid] = want;
    int32 hp = hpPer5;
    int32 mana = manaPer5;
    player->CastCustomSpell(player, SPELL_COOK_REGEN, &hp, &mana, nullptr, true);
}

void CatchUpProfession(Player* player)
{
    if (!player || !player->GetSession())
        return;
    // These three were fully implemented and mechanically sound but never
    // actually granted to anyone (found in a 2026-08-21 audit after the
    // Autoloot/Quests-Finish 910008/910090 instances of this same bug) --
    // no documented unlock condition for any of them, so grant unconditionally.
    UnlockPerk(player, SPELL_ARMORY, nullptr);
    UnlockPerk(player, SPELL_SOLO_QUEUE, nullptr);
    UnlockPerk(player, SPELL_PULL_RADIUS, nullptr);
    UnlockPerk(player, SPELL_TRACK_ORE, nullptr);
    UnlockPerk(player, SPELL_TRACK_HERB, nullptr);
    if (player->GetRewardedQuestCount() >= 50)
        UnlockPerk(player, SPELL_FIND_QUESTS, "|cff66ccff[Account Perks]|r *Quests - Find unlocked!");
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
    for (uint32 i = 0; i < 6; ++i)
        if (cook >= COOK_BREAKS[i])
            UnlockPerk(player, SPELL_COOK[i], nullptr);
    uint32 maxLv = AccountMaxLevel(player);
    if (maxLv >= 10)
        UnlockPerk(player, SPELL_SWIM, nullptr);
    uint32 travelNeed[] = { 20, 40, 60, 70, 80 };
    for (uint32 i = 0; i < 5; ++i)
        if (maxLv >= travelNeed[i])
            UnlockPerk(player, SPELL_TRAVEL[i], nullptr);
    // Mounted Opener (910104) is no longer granted -- scrapped 2026-08-24.
    // The non-class perk audit found it could never fire: the only trigger is
    // the cast branch below, and the spell is neither castable nor learned. It
    // is off the panel too. Existing lg_account_perk rows are left alone;
    // nothing reads them.
    if (PickMount(player))
        UnlockPerk(player, SPELL_AUTO_MOUNT, nullptr);
    if (QueryResult att = CharacterDatabase.Query(
        "SELECT COUNT(*) FROM `lg_absorb` WHERE `account_id` = {}",
        player->GetSession()->GetAccountId()))
        if ((*att)[0].Get<uint64>() >= 1000)
            UnlockPerk(player, SPELL_CURATOR, nullptr);
}

// Curator's 5 lowest-attunement bag/bank/armory pieces are treated as if
// equipped for attunement purposes (2026-08-20 redesign, see Bonesaw.md):
// each gets +1 xp per tick, rolled over and banked through the exact same
// LivingGear_GrantItemXp path an equipped item's kill XP goes through. The
// selection filter is `level < 25`, not `< 50` -- once a piece reaches
// attunement's cap level it stops adding anything further to the account
// (see LivingGear.cpp AbsorbPctForLevel), so there's no reason for
// Curator's limited tick budget to keep growing it past that instead of
// moving on to the next least-attuned piece. 25 must match LivingGear.cpp's
// `LivingGear.Attune.CapLevel` config (default 25) if that's ever changed.
// Curator is coverage, not a drip.
//
// It used to feed 1 item XP a minute to the five lowest-level pieces, pushing
// them up the 1%-to-100% attune ramp -- roughly 7.6 hours of being logged in to
// finish one endgame item. That made "my tabard is gaining levels in the bank"
// something the design had to explain, and made the perk a timer rather than a
// choice.
//
// Each rank now simply states what share of your collection counts, and the
// work happens once when it is bought and once at login. See
// LivingGear_BankCollection in LivingGear.cpp for why it banks BASE stats --
// short version: worn gear banks GROWN stats, grown always exceeds base, so
// wearing a piece still beats leaving it in the bank.
uint32 const CURATOR_RANKS[] = { SPELL_CURATOR, 910178, 910179, 910180 };
uint32 const CURATOR_PCT[]   = { 25, 50, 75, 100 };

uint32 CuratorCoverage(Player* player)
{
    uint32 pct = 0;
    for (uint8 i = 0; i < 4; ++i)
        if (HasPerk(player, CURATOR_RANKS[i]))
            pct = CURATOR_PCT[i];
    return pct;
}

void ApplyCuratorCoverage(Player* player)
{
    if (uint32 const pct = CuratorCoverage(player))
        LivingGear_BankCollection(player, pct);
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
    std::unordered_set<uint32> summonedNpcs;   // report #94: one NPC per entry, not per quest
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
        // Several complete quests routinely share ONE turn-in NPC (a quest hub
        // like the Sunstrider trainer or an innkeeper). Summoning per quest
        // stacked a copy of the same NPC on top of itself for every quest --
        // report #94. Skip entries already standing from this press.
        if (!summonedNpcs.insert(npc).second)
            continue;
        if (player->SummonCreature(npc, player->GetPosition(), TEMPSUMMON_TIMED_DESPAWN, 60000))
            ++spawned;
    }
    Say(player, Acore::StringFormat("[Quests] Summoned {} turn-in NPC(s).", spawned).c_str());
}

void SendPerkSync(Player* player);   // defined below; the respec path re-syncs

bool HandleLgChat(Player* player, std::string msg)
{
    if (!player || !player->GetSession())
        return false;
    if (msg.rfind("LG\t", 0) == 0)
        msg = msg.substr(3);
    uint32 const acc = player->GetSession()->GetAccountId();
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
    if (sscanf(msg.c_str(), "PULLSET|%u", &v) == 1)
    {
        g_pullRadiusOn[acc] = v != 0;
        DetectSchema();
        if (g_hasPullRadiusCol)
            CharacterDatabase.DirectExecute(
                "INSERT INTO `lg_account_meta` (`account_id`, `pull_radius`) VALUES ({}, {}) "
                "ON DUPLICATE KEY UPDATE `pull_radius` = {}",
                acc, v ? 1 : 0, v ? 1 : 0);
        if (!v)
            player->RemoveAurasDueToSpell(SPELL_PULL_RADIUS);
        SendLine(player, Acore::StringFormat("PULL|{}", v ? 1 : 0));
        return true;
    }
    if (sscanf(msg.c_str(), "TRACKORESET|%u", &v) == 1)
    {
        g_trackOreOn[acc] = v != 0;
        DetectSchema();
        if (g_hasTrackOreCol)
            CharacterDatabase.DirectExecute(
                "INSERT INTO `lg_account_meta` (`account_id`, `track_ore`) VALUES ({}, {}) "
                "ON DUPLICATE KEY UPDATE `track_ore` = {}",
                acc, v ? 1 : 0, v ? 1 : 0);
        SendLine(player, Acore::StringFormat("TRACKORE|{}", v ? 1 : 0));
        return true;
    }
    if (sscanf(msg.c_str(), "TRACKHERBSET|%u", &v) == 1)
    {
        g_trackHerbOn[acc] = v != 0;
        DetectSchema();
        if (g_hasTrackHerbCol)
            CharacterDatabase.DirectExecute(
                "INSERT INTO `lg_account_meta` (`account_id`, `track_herb`) VALUES ({}, {}) "
                "ON DUPLICATE KEY UPDATE `track_herb` = {}",
                acc, v ? 1 : 0, v ? 1 : 0);
        SendLine(player, Acore::StringFormat("TRACKHERB|{}", v ? 1 : 0));
        return true;
    }
    if (sscanf(msg.c_str(), "JMPSET|%u", &v) == 1)
    {
        // Extra jump is deliberately off server-side (AGENTS.md: "Extra
        // jump is disabled; do not advertise it"), so this remembers the
        // selection and echoes it -- the client's own db.jump.max gate is
        // what keeps the boosted modes unselectable. It matters because
        // before this the client sent JMPSET| and absolutely nothing
        // anywhere parsed it, so the panel never even acknowledged a click.
        g_jumpMode[acc] = v;
        DetectSchema();
        if (g_hasJumpCol)
            CharacterDatabase.DirectExecute(
                "INSERT INTO `lg_account_meta` (`account_id`, `jump_mode`) VALUES ({}, {}) "
                "ON DUPLICATE KEY UPDATE `jump_mode` = {}",
                acc, v, v);
        SendLine(player, Acore::StringFormat("JMP|{}|0", v));
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
    if (msg == "PERKPTS")
    {
        SendPerkPoints(player);
        return true;
    }
    if (sscanf(msg.c_str(), "PERKBUY|%u", &v) == 1)
    {
        // An addon whisper is not a trusted boundary. PurchaseRank re-checks
        // the price table, the prerequisite and the balance server-side, so a
        // crafted PERKBUY can neither conjure a perk nor overdraw an account.
        if (!PurchaseRank(player, v))
            Say(player, "|cff66ccff[Account Perks]|r You cannot buy that yet.");
        SendPerkPoints(player);
        return true;
    }
    if (msg == "PERKRESPEC")
    {
        uint32 left = 0;
        if (!CanRespec(acc, left))
        {
            Say(player, Acore::StringFormat(
                "|cff66ccff[Account Perks]|r You can respec again in {} second(s).", left).c_str());
            return true;
        }
        RespecPerks(acc);
        SaveLastRespec(acc, uint32(GameTime::GetGameTime().count()));
        // Strips the buttons for anything just revoked on this character;
        // other characters on the account shed theirs at their next login.
        ReconcilePerkSpells(player);
        Say(player, "|cff66ccff[Account Perks]|r Perks refunded.");
        SendPerkSync(player);
        SendPerkPoints(player);
        return true;
    }
    if (msg == "ARMOPEN")
    {
        SendArmory(player);
        return true;
    }
    if (sscanf(msg.c_str(), "ARMEQUIP|%u|%u", &slot, &entry) == 2)
    {
        // The client only ever offers entries from the account's own
        // attuned list, but a whisper addon message is not a trusted
        // boundary -- without this check, a crafted ARMEQUIP message could
        // conjure any item entry for free. Require the account to actually
        // have it attuned first.
        if (QueryResult q = CharacterDatabase.Query(
            "SELECT 1 FROM `lg_absorb` WHERE `account_id` = {} AND `item_entry` = {}", acc, entry))
        {
            Item* created = nullptr;
            uint16 dest = 0;
            if (player->CanEquipNewItem(uint8(slot), dest, entry, true) == EQUIP_ERR_OK)
                created = player->EquipNewItem(dest, entry, true);
            else
            {
                ItemPosCountVec destVec;
                if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, destVec, entry, 1) == EQUIP_ERR_OK)
                    created = player->StoreNewItem(destVec, entry, true);
            }
            // Recreated copies are soulbound -- they're a free re-materialization
            // of something already permanently converted to account stats, not
            // a tradeable item, so this closes off selling/trading duplicates.
            if (created)
                created->SetBinding(true);
        }
        return true;
    }
    return false;
}

// One row, one round trip, once per account -- this was six separate
// single-column SELECTs against the same row. solo_queue in particular had
// a column and a SOLOSET writer for as long as the others but nothing ever
// read it back, so that toggle silently reset itself on every relog.
void LoadAccountToggles(uint32 accountId)
{
    DetectSchema();
    if (!g_metaLoaded.insert(accountId).second)
        return;
    g_autoMountOn[accountId] = !g_hasAutoMountCol; // no column: default on
    g_pullRadiusOn[accountId] = false;
    g_trackOreOn[accountId] = false;
    g_trackHerbOn[accountId] = false;
    g_soloQueue[accountId] = false;
    g_jumpMode[accountId] = 0;
    if (QueryResult q = CharacterDatabase.Query(
        "SELECT {}, {}, {}, {}, {}, {} FROM `lg_account_meta` WHERE `account_id` = {}",
        g_hasAutoMountCol ? "`auto_mount`" : "0",
        g_hasPullRadiusCol ? "`pull_radius`" : "0",
        g_hasTrackOreCol ? "`track_ore`" : "0",
        g_hasTrackHerbCol ? "`track_herb`" : "0",
        g_hasSoloCol ? "`solo_queue`" : "0",
        g_hasJumpCol ? "`jump_mode`" : "0",
        accountId))
    {
        Field* f = q->Fetch();
        if (g_hasAutoMountCol)
            g_autoMountOn[accountId] = f[0].Get<uint32>() != 0;
        g_pullRadiusOn[accountId] = f[1].Get<uint32>() != 0;
        g_trackOreOn[accountId] = f[2].Get<uint32>() != 0;
        g_trackHerbOn[accountId] = f[3].Get<uint32>() != 0;
        g_soloQueue[accountId] = f[4].Get<uint32>() != 0;
        g_jumpMode[accountId] = f[5].Get<uint32>();
    }
}

// Everything the client needs to render the World Perks / toggle UI.
// Split out of OnPlayerLogin because it was ONLY ever sent at login: a
// client REQ (which the addon fires on /reload and whenever it re-syncs)
// was answered by LivingGear.cpp alone, so db.perks, the four toggles and
// the solo-queue flag stayed empty until a full relog -- and since the
// addon gates the buttons on PerkKnown(), half the panel simply looked
// locked. LivingGear.cpp now calls this from its REQ path too.
void SendPerkSync(Player* player)
{
    if (!player || !player->GetSession())
        return;
    DetectSchema();
    uint32 const acc = player->GetSession()->GetAccountId();
    LoadPerks(acc);
    LoadAccountToggles(acc);
    ReconcilePerkSpells(player);
    if (!g_perks[acc].empty())
    {
        // WotLK's addon-whisper channel silently truncates messages
        // around ~255 bytes -- a single account can have 50+ unlocked
        // perks, so one giant "PKALL|910002,910003,..." line can blow
        // past that and drop whatever IDs land after the cutoff (e.g.
        // Auto-Mount, 910105, always unlocking server-side but never
        // reaching db.perks client-side, so its World Perks toggle
        // looked locked and clicking it silently did nothing). The
        // client's PKALL handler is purely additive (no db.perks = {}
        // reset), so it's safe to split across multiple PKALL sends.
        std::string ids;
        for (uint32 spellId : g_perks[acc])
        {
            std::string next = std::to_string(spellId);
            if (!ids.empty() && ids.size() + 1 + next.size() > 200)
            {
                SendLine(player, "PKALL|" + ids);
                ids.clear();
            }
            if (!ids.empty())
                ids += ',';
            ids += next;
        }
        if (!ids.empty())
            SendLine(player, "PKALL|" + ids);
    }
    SendPerkCosts(player);
    SendPerkPoints(player);
    SendLine(player, Acore::StringFormat("JMP|{}|0", g_jumpMode[acc]));
    SendLine(player, Acore::StringFormat("AM|{}", g_autoMountOn[acc] ? 1 : 0));
    SendLine(player, Acore::StringFormat("PULL|{}", g_pullRadiusOn[acc] ? 1 : 0));
    SendLine(player, Acore::StringFormat("TRACKORE|{}", g_trackOreOn[acc] ? 1 : 0));
    SendLine(player, Acore::StringFormat("TRACKHERB|{}", g_trackHerbOn[acc] ? 1 : 0));
    SendLine(player, Acore::StringFormat("SQ|{}", g_soloQueue[acc] ? 1 : 0));
}

class PerksPlayer : public PlayerScript
{
public:
    PerksPlayer() : PlayerScript("LivingGearPerksPlayer", {
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_LOGOUT,
        PLAYERHOOK_ON_UPDATE,
        PLAYERHOOK_ON_CREATURE_KILL,
        PLAYERHOOK_ON_CREATURE_KILLED_BY_PET,
        PLAYERHOOK_ON_GIVE_EXP,
        PLAYERHOOK_ON_SPELL_CAST,
        PLAYERHOOK_ON_LEARN_SPELL,
        PLAYERHOOK_ON_PLAYER_LEAVE_COMBAT,
        PLAYERHOOK_ON_UPDATE_CRAFTING_SKILL,
        PLAYERHOOK_ON_QUEST_COMPUTE_EXP,
        PLAYERHOOK_CAN_SOLO_QUEUE,
        PLAYERHOOK_ON_AFTER_SPEC_SLOT_CHANGED,
        PLAYERHOOK_ON_ACHI_COMPLETE
    }) { }

    void OnPlayerAchievementComplete(Player* player, AchievementEntry const* achievement) override
    {
        if (!player || !player->GetSession() || !achievement)
            return;
        // Earned points are cached per account for the whole uptime, and a new
        // achievement is precisely the event that makes that cache wrong. Left
        // stale, a player earned points and saw no change until they relogged.
        g_earnedCache.erase(player->GetSession()->GetAccountId());
        SyncAchievementToAccount(player, achievement);
        SendPerkPoints(player);
    }

    // Bug report #17, 2026-08-22, partial. The Solo Queue perk (910092) has
    // always been a DEAD SWITCH: toggling it wrote a bool, synced it to the
    // client, and was then read by absolutely nothing. The core has offered
    // OnPlayerCanSoloQueue the whole time -- LFGMgr consults it in three
    // places -- and this module simply never answered. Exactly the failure
    // CLAUDE.md warns about, one layer further in than usual: the command had a
    // handler, but the state that handler wrote had no reader.
    //
    // Answering it makes the toggle mean something: it waives the Deserter
    // check when queueing, and skips the playerbot fill for RAID finder groups.
    //
    // 2026-08-24, non-class perk audit: this comment used to say a lone player
    // could not cause a proposal to form, so the queue "would never pop for
    // them at all". That stopped being true and nobody updated it.
    // LFGQueue::CheckCompatibility now computes
    //     allowIncomplete = hasSoloQueue() || raidQueue || allowBotFill
    // so hasSoloQueue() ALONE lets an incomplete proposal form. Verified end to
    // end: Deserter waived (LFGMgr.cpp:737), random-dungeon cooldown waived
    // (:784), incomplete proposals allowed (LFGQueue.cpp:339), and the raid
    // branch skips bot fill for a solo queuer while dungeons still get filled
    // (LFGMgr.cpp:1839). The perk delivers what it advertises.
    //
    // Left as-is on purpose: dungeons DO still get bots, which is the point --
    // a solo queuer wants a group that can clear the place, not four empty
    // slots. Raids skip the fill instead.
    bool OnPlayerCanSoloQueue(Player* player) override
    {
        if (!player || !player->GetSession())
            return false;
        return g_soloQueue[player->GetSession()->GetAccountId()];
    }

    // Subtlety-gated grants (Shadowstep + Shadow Dance + Shadow Clone).
    // Called at login AND on a live dual-spec swap -- these used to only
    // run at login, so swapping into Subtlety mid-session without relogging
    // left Shadow Dance ungranted (HasPerk stayed false forever, even
    // though the AP-buff tick already re-checks spec live every second).
    static void GrantSubtletyPerks(Player* player)
    {
        if (GetClassPerk(player) != SPELL_SUBTLETY)
            return;
        // Not a class check, deliberately: this asks "will the core keep this
        // spell", not "is this a Rogue". Player::CheckSkillLearnedBySpell is
        // what deletes cross-class spells at login (dozens of "Will be deleted"
        // lines per startup, and the grant never stuck anyway), and it is
        // itself switched off by ValidateSkillLearnedBySpells. So the day that
        // config is turned off for multi-classing, this opens up on its own
        // with no code change and no class hardcoded here.
        if (player->CheckSkillLearnedBySpell(SPELL_SHADOWSTEP))
            player->learnSpell(SPELL_SHADOWSTEP);
        if (player->CheckSkillLearnedBySpell(SPELL_SHADOW_DANCE_NATIVE))
            player->learnSpell(SPELL_SHADOW_DANCE_NATIVE);
        // UnlockPerk (not raw learnSpell) so the client's db.perks
        // actually gets a PK|id|1 for these -- otherwise the addon UI
        // can never show them as known even though the character has
        // them, since nothing else ever tells the client. learnSpellToo=false:
        // pure flags, no CASTABLE_SPELLS entry -- see UnlockPerk's comment.
        UnlockPerk(player, SPELL_SHADOW_DANCE, nullptr, false);
        UnlockPerk(player, SPELL_SHADOW_CLONE, nullptr, false);
    }

    void OnPlayerAfterSpecSlotChanged(Player* player, uint8 /*newSlot*/) override
    {
        if (player)
            GrantSubtletyPerks(player);
    }

    void OnPlayerLogin(Player* player) override
    {
        if (!player || !player->GetSession())
            return;
        // Before the sync: a price-epoch refund revokes perks, and the sync
        // is what tells the client what is actually owned afterwards.
        ApplyPerkEpoch(player);
        SendPerkSync(player);   // sends PKCOST + PKPTS too
        ApplyCuratorCoverage(player);
        // After the sync rows are in: titles for achievements this character
        // inherited from the account rather than earned itself.
        CatchUpAchievements(player);
        GrantAchievementTitles(player);
        CatchUpProfession(player);
        if (HasPerk(player, SPELL_SWIM))
            player->CastSpell(player, SPELL_SWIM, true);
        GrantSubtletyPerks(player);
    }

    void OnPlayerLogout(Player* player) override
    {
        if (!player)
            return;
        uint32 g = player->GetGUID().GetCounter();
        g_cookAcc.erase(g);
        g_cookAmount.erase(g);
        g_aidCleanseTick.erase(g);
        g_pullRadiusTick.erase(g);
        g_cloneGuid.erase(g);
        g_shadowDanceBuffOn.erase(g);
        g_shadowDanceTick.erase(g);
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        // Nothing in this tick is worth a crash. Several of these cast auras on
        // the player, and doing that after Player::CleanupsBeforeDelete asserts
        // and takes the realm down.
        if (!LivingGear_SafeToCastOn(player))
            return;
        TickCooking(player, diff);
        TickFirstAidCleanse(player, g_aidCleanseTick[player->GetGUID().GetCounter()], diff);
        // Curator is applied at login and on purchase now, not on a tick.
        {
            uint32 const id = player->GetGUID().GetCounter();
            g_shadowDanceTick[id] += diff;
            if (g_shadowDanceTick[id] >= 1000)
            {
                g_shadowDanceTick[id] = 0;
                TickShadowDanceBuff(player);
            }
        }
        if (player->GetSession() && g_pullRadiusOn[player->GetSession()->GetAccountId()]
            && HasPerk(player, SPELL_PULL_RADIUS))
        {
            uint32 id = player->GetGUID().GetCounter();
            g_pullRadiusTick[id] += diff;
            // SPELL_PULL_RADIUS's own duration is short (it matched Kill Combo's
            // DurationIndex) -- refresh well inside that window so the aura
            // never actually lapses between ticks.
            if (g_pullRadiusTick[id] >= 10000)
            {
                g_pullRadiusTick[id] = 0;
                player->CastSpell(player, SPELL_PULL_RADIUS, true);
            }
        }
        if (player->GetSession())
        {
            uint32 const acc = player->GetSession()->GetAccountId();
            bool const ore = g_trackOreOn[acc] && HasPerk(player, SPELL_TRACK_ORE);
            bool const herb = g_trackHerbOn[acc] && HasPerk(player, SPELL_TRACK_HERB);
            if (ore)
                player->SetFlag(PLAYER_TRACK_RESOURCES, TRACK_RESOURCE_ORE);
            else if (!player->HasAura(NATIVE_FIND_MINERALS))
                player->RemoveFlag(PLAYER_TRACK_RESOURCES, TRACK_RESOURCE_ORE);
            if (herb)
                player->SetFlag(PLAYER_TRACK_RESOURCES, TRACK_RESOURCE_HERB);
            else if (!player->HasAura(NATIVE_FIND_HERBS))
                player->RemoveFlag(PLAYER_TRACK_RESOURCES, TRACK_RESOURCE_HERB);
            if (ore || herb)
                player->SetFlag(PLAYER_TRACK_RESOURCES, TRACK_RESOURCE_FISH);
            else if (!player->HasAura(NATIVE_FIND_FISH))
                player->RemoveFlag(PLAYER_TRACK_RESOURCES, TRACK_RESOURCE_FISH);
        }
    }

    void OnPlayerMapChanged(Player* player) override
    {
        if (!player || !player->GetMap())
            return;
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
        CheckDungeonClear(killer, killed);
        GrantUnrewardedKillXp(killer, killed);
    }

    void OnPlayerCreatureKilledByPet(Player* owner, Creature* killed) override
    {
        CheckDungeonClear(owner, killed);
        GrantUnrewardedKillXp(owner, killed);
    }

    void OnPlayerGiveXP(Player* player, uint32& amount, Unit* victim, uint8 xpSource) override
    {
        if (!player || !amount)
            return;
        float mult = 1.0f;
        // Zone scaling is a rule about kills. It used to be applied to
        // every XP source indiscriminately, so turning in a quest in a
        // starter zone at 70 was also cut to ~35% -- the displayed level of
        // some creature has nothing to say about quest, exploration or
        // battleground XP.
        if (xpSource == XPSOURCE_KILL)
        {
            // One funnel for creature kills: rescale onto the level the
            // player is actually shown, re-apply what BaseGain drops, apply
            // the zone multiplier, then the floor -- see KillXpFor. A player
            // victim (battleground honorable kill) has none of that to do
            // and only wants the floor, which SupportKillXp applies.
            if (Creature* creature = victim ? victim->ToCreature() : nullptr)
                amount = KillXpFor(player, creature, amount);
        }
        if (Aura* pace = player->GetAura(SPELL_DUNGEON_PACE))
            if (AuraEffect* e = pace->GetEffect(EFFECT_0))
                mult *= 1.0f + float(e->GetAmount() + 1) / 100.0f;
        amount = uint32(float(amount) * mult);
        if (!amount)
            amount = 1;
    }

    // Zone scaling already re-bases KILL xp onto the level the player is
    // actually shown (ScaledKillXP). Quest XP had no equivalent, so the
    // moment you outlevelled a zone its quests paid in scraps -- the mobs
    // beside them stayed worth killing while the quest chain running through
    // them did not. Two-part fix, matching what was agreed:
    //
    //   1. Rescale: recompute the reward as though the quest had been
    //      authored for this player's level. Applied as a RATIO against the
    //      engine's own number rather than as a replacement, so the quest
    //      rate config, SPELL_AURA_MOD_XP_QUEST_PCT and everything else
    //      CalculateQuestRewardXP folded in survive untouched.
    //   2. Floor: never pay less than QUEST_XP_FLOOR_PCT of the current
    //      level's bar, which catches quests whose rescaled value still
    //      reads as noise.
    //
    // Deliberately does NOT fire for a repeat turn-in. The engine zeroes
    // those on purpose (rewarded && !DF && !daily/weekly/monthly, see
    // Player::RewardQuest) and a floor applied there would turn any
    // repeatable quest into an infinite XP tap.
    void OnPlayerQuestComputeXP(Player* player, Quest const* quest, uint32& xpValue) override
    {
        if (!player || !quest || !g_cfg.questScale)
            return;
        if (player->GetLevel() >= sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
            return;
        bool const repeatTurnIn = player->IsQuestRewarded(quest->GetQuestId())
            && !quest->IsDFQuest()
            && !(quest->IsDaily() || quest->IsWeekly() || quest->IsMonthly());
        if (repeatTurnIn)
            return;

        uint8 const level = uint8(player->GetLevel());
        // What the quest is worth at its own level vs. at the player's. The
        // second is just the QuestXP row for the player's level: XPValue's
        // diffFactor saturates at 10 when quest level == player level, and
        // the /10 cancels it out.
        uint32 const rawAtQuestLevel = quest->XPValue(level);
        QuestXPEntry const* xpEntry = sQuestXPStore.LookupEntry(level);
        // QuestXPEntry::Exp is a fixed 10-wide array and RewardXPDifficulty
        // comes straight out of quest_template, so bound it rather than
        // trusting the data -- the core indexes this unchecked, but a bad row
        // reading off the end of a DBC record is not a bug worth inheriting.
        uint32 const difficulty = quest->GetXPId();
        uint32 const rawAtPlayerLevel = (xpEntry && difficulty < 10) ? xpEntry->Exp[difficulty] : 0;
        if (rawAtPlayerLevel > rawAtQuestLevel && rawAtQuestLevel > 0)
            xpValue = uint32(uint64(xpValue) * rawAtPlayerLevel / rawAtQuestLevel);

        uint32 const forNextLevel = player->GetUInt32Value(PLAYER_NEXT_LEVEL_XP);
        uint32 const floor = uint32(uint64(forNextLevel) * g_cfg.questFloorPct / 100);
        if (xpValue < floor)
            xpValue = floor;
    }

    void OnPlayerSpellCast(Player* player, Spell* spell, bool /*skip*/) override
    {
        if (!player || !spell)
            return;
        SpellInfo const* info = spell->GetSpellInfo();
        if (!info)
            return;
        if (info->HasAura(SPELL_AURA_MOUNTED))
            g_lastMount[player->GetGUID().GetCounter()] = info->Id;
        if (info->Id == SPELL_SHADOWSTEP && GetClassPerk(player) == SPELL_SUBTLETY)
        {
            LivingGear_DiagBump(player, "sstep.hook");
            ApplyShadowstepCooldown(player);
        }
        // Report #73: using the Pickpocket skill hits every humanoid within
        // 10 yards. Runs off the actual button press, not stealth.
        if (info->Id == SPELL_PICKPOCKET && GetClassPerk(player) == SPELL_SUBTLETY)
            PickpocketAoE(player);
        if (sSpellMgr->GetFirstSpellInChain(info->Id) == SPELL_HEMORRHAGE)
            LivingGear_DiagBump(player, "hemo.cast");
        if (GetClassPerk(player) == SPELL_SUBTLETY
            && sSpellMgr->GetFirstSpellInChain(info->Id) == SPELL_HEMORRHAGE)
        {
            LivingGear_DiagBump(player, "hemo.hook");
            ApplyHemorrhageAoE(player);
        if (IsRankOf(info, SPELL_EVISCERATE_R1))
            EviscerateSpread(player);
        }
        // Shadow Clone pet dropped 2026-08-21 -- the SUMMON_PET rework
        // (real pet frame, stance buttons) didn't play the way the user
        // wanted. SummonClone()/npc_lg_shadow_clone are kept but now
        // unreachable, same "inert isn't broken" precedent as the old
        // SacrificeItem path. Casting *Shadow Clone now does nothing;
        // Shadowstep's own chain-ambush effect (ChainAmbush, unrelated
        // code path, its own short-lived per-hit visual clone) is
        // untouched and is where the real improvements went instead.
        if (info->Id == 8690 || info->Id == 556) // Hearthstone / Astral Recall
        {
            // 2026-08-21: Travel used to make Hearthstone a fully instant,
            // no-cooldown teleport once unlocked -- a real "teleportation
            // stone," not a gradual reduction. Restoring that instead of
            // the partial per-rank scaling this got rewritten into earlier
            // this session. Cast time is zeroed unconditionally in
            // OnSpellPrepare above; this clears the cooldown entirely.
            uint32 travelRanks = 0;
            for (uint32 id : SPELL_TRAVEL)
                if (HasPerk(player, id))
                    ++travelRanks;
            if (travelRanks)
            {
                uint32 const spellId = info->Id;
                ObjectGuid playerGuid = player->GetGUID();
                // Still deferred -- OnPlayerSpellCast fires mid-way through
                // the triggering Spell::cast(), before the engine has
                // written its own real cooldown entry, so clearing it here
                // synchronously would just get overwritten once cast()
                // finishes and sets the real ~30 min cooldown.
                player->m_Events.AddEventAtOffset([playerGuid, spellId]()
                {
                    Player* p = ObjectAccessor::FindPlayer(playerGuid);
                    if (p && p->IsInWorld())
                        p->RemoveSpellCooldown(spellId, true);
                }, std::chrono::milliseconds(300));
            }
        }
        // Bug report #58: "make quest items have no cooldown, some of them are
        // absurdly long for no reason". Measured on this realm: 599 items of
        // ITEM_CLASS_QUEST carry one, and the tail is genuinely silly -- three
        // hours on one, an hour on another, 45 and 30 minutes on ten more.
        //
        // Cleared at runtime rather than by editing item_template: the data
        // under data/sql is immutable, a migration would be a one-way door on
        // 599 rows, and doing it here means it applies to the player who used
        // the item and nothing else.
        //
        // Deferred by a tick for the reason invariant 2 exists -- cooldowns are
        // applied when a cast FINISHES, so clearing one from OnPlayerSpellCast
        // directly clears something that does not exist yet. Same shape as the
        // Travel hearthstone clear above.
        if (Item* castItem = spell->m_CastItem)
        {
            if (ItemTemplate const* proto = castItem->GetTemplate())
            {
                if (proto->Class == ITEM_CLASS_QUEST)
                {
                    ObjectGuid const questCaster = player->GetGUID();
                    uint32 const questSpell = info->Id;
                    uint32 const questCategory = info->GetCategory();
                    player->m_Events.AddEventAtOffset([questCaster, questSpell, questCategory]()
                    {
                        Player* p = ObjectAccessor::FindPlayer(questCaster);
                        if (!p || !p->IsInWorld())
                            return;
                        p->RemoveSpellCooldown(questSpell, true);
                        if (questCategory)
                            p->RemoveCategoryCooldown(questCategory);
                    }, std::chrono::milliseconds(300));
                }
            }
        }
        if (info->Id == SPELL_FIND_QUESTS)
            FindQuests(player);
        if (info->Id == SPELL_AUTO_QUEST)
            AutoQuestFinish(player);
        // SendArmory alone only refreshes data. The addon deliberately does not
        // reveal anything on ARMEND, because that same feed rides every login
        // sync and would otherwise pop the window open on each one -- so from
        // the point the armory joined that sync until now, casting this perk
        // did nothing visible at all (report #104). The reveal has to be its
        // own message, sent only when the player actually asked.
        if (info->Id == SPELL_ARMORY)
        {
            SendArmory(player);
            SendLine(player, "OPENARM");
        }
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
        if (GetClassPerk(player) == SPELL_SUBTLETY && info->SpellFamilyName == SPELLFAMILY_ROGUE
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
        // THE logout crash, finally. Unit::CleanupsBeforeDelete sets
        // m_cleanupDone and THEN calls CombatStop() two lines later
        // (Unit.cpp:12745, :12747), and CombatStop fires this hook. So logging
        // out while in combat runs TryAutoMount -- which casts a mount -- on a
        // player the engine has already finished tearing down, and _AddAura
        // asserts.
        //
        // That is why it needed no second player nearby and why it kept
        // happening in the same spot: it is "log out during or just after a
        // fight", not anything to do with who else was around. Three earlier
        // passes guarded ticks and spread targets and never touched this,
        // because OnPlayerLogout genuinely IS safe -- it runs at
        // WorldSession.cpp:857, before the cleanup at :873.
        if (!LivingGear_SafeToCastOn(player))
            return;
        TryAutoMount(player);
    }

    void OnPlayerUpdateCraftingSkill(Player* player, SkillLineAbilityEntry const* /*skill*/,
        uint32 /*current_level*/, uint32& /*gain*/) override
    {
        CatchUpProfession(player);
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
        // 2026-08-21: full instant cast once Travel is unlocked at all
        // ("teleportation stone"), not a gradual per-rank reduction.
        if (travel && (info->Id == 8690 || info->Id == 556))
            spell->SetCastTime(0);
        // First Aid "Instant" (910046): bandages apply with no channel. The
        // heal itself is unchanged -- it is still the periodic the bandage
        // always applied, which is what makes this an instant HoT rather than
        // an instant heal.
        if (info->Mechanic == MECHANIC_BANDAGE && HasPerk(player, SPELL_AID_INSTANT))
            spell->SetCastTime(0);
        // Fishing "Speed" (910045): bites come twice as fast.
        if (info->IsAbilityOfSkillType(SKILL_FISHING) && HasPerk(player, SPELL_FISH_BITE_SPEED))
            spell->SetCastTime(spell->GetCastTime() / 2);
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
        // Bug report #30: "Crafting speed perks are not applying to
        // Blacksmithing or Leatherworking." Both skills ARE in the list above
        // and both have hundreds of entries in SkillLineAbility.dbc, so
        // IsAbilityOfSkillType should match. Rather than guess at why it does
        // not, say what actually happened on the next craft.
        if (!craft)
        {
            static std::unordered_set<uint32> warned;
            if (info->IsAbilityOfSkillType(SKILL_BLACKSMITHING)
                || info->IsAbilityOfSkillType(SKILL_LEATHERWORKING)
                || warned.size() < 20)
                if (warned.insert(info->Id).second)
                    LOG_INFO("module.livinggear",
                        "craft speed: spell {} ({}) did not match any crafting skill line, "
                        "no boost applied ({} rank(s) owned)", info->Id, info->SpellName[0], ranks);
            return;
        }
        int32 const before = spell->GetCastTime();
        spell->SetCastTime(int32(float(before) * std::pow(0.80f, float(ranks))));
        {
            static std::unordered_set<uint32> seen;
            if (seen.insert(info->Id).second)
                LOG_INFO("module.livinggear",
                    "craft speed: spell {} cast time {} -> {} with {} rank(s)",
                    info->Id, before, spell->GetCastTime(), ranks);
        }
    }
};

class PerksUnit : public UnitScript
{
public:
    PerksUnit() : UnitScript("LivingGearPerksUnit", true, {
        UNITHOOK_ON_DAMAGE,
        UNITHOOK_ON_AURA_APPLY,
        UNITHOOK_MODIFY_PERIODIC_DAMAGE_AURAS_TICK,
        UNITHOOK_MODIFY_HEAL_RECEIVED,
        UNITHOOK_SHOULD_TRACK_VALUES_UPDATE_POS_BY_INDEX,
        UNITHOOK_ON_PATCH_VALUES_UPDATE,
        UNITHOOK_ON_BEFORE_ROLL_MELEE_OUTCOME_AGAINST
    }) { }

    // Bug reports #88/#89: a level 7 in Shadowglen could not land a hit on
    // zone-scaled mobs. The scaling is deliberately display-and-damage-only
    // (UNIT_FIELD_LEVEL keeps the mob's real level so the shared health bar
    // never desyncs), but the engine's hit rolls read the REAL level: melee
    // compares the player's weapon skill to victim->GetMaxSkillValueForLevel()
    // = real level * 5, and MagicSpellHitResult takes levelDiff from the real
    // level too. Against an effective-8 mob whose real level is 80, that is a
    // 400-vs-35 skill gap and +72 levels of spell-miss -- nearly every swing
    // and spell misses regardless of what the damage hooks do.
    //
    // Fix: wherever the engine asks "what level is this victim for hit
    // purposes", answer with the level this viewer is being SHOWN. Melee goes
    // through OnBeforeRollMeleeOutcomeAgainst (victimDefenseSkill derives from
    // GetUnitMeleeSkill -> getLevelForTarget, so overriding it here covers
    // miss/dodge/parry/block/crit together). Spells need no hook -- they read
    // Creature::getLevelForTarget directly, which core-patch 0025 overrides.
    void OnBeforeRollMeleeOutcomeAgainst(Unit const* attacker, Unit const* victim,
        WeaponAttackType /*attType*/, int32& /*attackerMaxSkillValueForLevel*/,
        int32& victimMaxSkillValueForLevel, int32& /*attackerWeaponSkill*/,
        int32& victimDefenseSkill, int32& /*crit_chance*/, int32& /*miss_chance*/,
        int32& /*dodge_chance*/, int32& /*parry_chance*/, int32& /*block_chance*/) override
    {
        Creature const* c = victim ? victim->ToCreature() : nullptr;
        Player const* p = attacker ? attacker->ToPlayer() : nullptr;
        if (!c || !p)
            return;
        uint32 const eff = EffectiveCreatureLevel(c, const_cast<Player*>(p));
        if (!eff || eff >= c->GetLevel())
            return;
        int32 const scaled = int32(eff) * 5;
        victimMaxSkillValueForLevel = scaled;
        // GetDefenseSkillValue on a creature is GetUnitMeleeSkill = level*5;
        // only rewrite it when it still holds that stock value.
        if (victimDefenseSkill == int32(c->GetUnitMeleeSkill(nullptr)))
            victimDefenseSkill = scaled;
    }

    // Spell-hit half of the same fix: MagicSpellHitResult takes levelDiff from
    // Creature::getLevelForTarget (core-patch 0025 routes it through here).
    // Answering with the displayed level keeps spells on zone-scaled mobs
    // landing at the same rate melee now does.
    void OnCreatureLevelForTarget(Unit const* creature, WorldObject const* target, uint8& outLevel) override
    {
        Creature const* c = creature ? creature->ToCreature() : nullptr;
        Player const* p = target ? target->ToPlayer() : nullptr;
        if (!c || !p)
            return;
        uint32 const eff = EffectiveCreatureLevel(c, const_cast<Player*>(p));
        if (!eff || eff >= c->GetLevel())
            return;
        outLevel = uint8(eff);
    }

    // First Aid "Restore" (910047): "Bandages restore 1% HP per second at
    // 1-75, 2% at 76-150, and so on." Expressed as a multiplier on the
    // bandage's own healing rather than a flat percentage, so higher-rank
    // bandages stay better than low ones instead of every bandage collapsing
    // to the same number.
    //
    // Tier comes from the healer's First Aid skill in 75-point bands, matching
    // how the description reads and how the Cooking tiers already work.
    void ModifyHealReceived(Unit* /*target*/, Unit* healer, uint32& heal,
        SpellInfo const* spellInfo) override
    {
        if (!healer || !spellInfo || !heal || !healer->IsPlayer())
            return;
        if (spellInfo->Mechanic != MECHANIC_BANDAGE)
            return;
        Player* player = healer->ToPlayer();
        if (!HasPerk(player, SPELL_AID_RESTORE))
            return;
        uint32 const skill = AccountMaxSkill(player, SKILL_FIRST_AID);
        uint32 const tier = std::max<uint32>(1, (skill + 74) / 75);
        uint64 const boosted = uint64(heal) * tier;
        heal = boosted > uint64(std::numeric_limits<uint32>::max())
            ? std::numeric_limits<uint32>::max() : uint32(boosted);
    }

    // Bug report #8, 2026-08-22: "add 1000% damage multiplier to garrote bleed
    // effect." Done here rather than at the cast site so it holds for every
    // Garrote a player applies, however it got there -- hand-cast on one target,
    // or spread across a pack by Hemorrhage (report #10). Doing it by passing
    // scaled base points instead would have meant remembering to scale at each
    // call site, and would have double-applied the moment two of them met.
    //
    // GetFirstSpellInChain so every rank counts, not just rank 1.
    //
    // Player-applied only: a mob's own bleed is not a Rogue perk. The clamp is
    // there because damage is a uint32 and 11x a large tick on a boss-level
    // Rogue is not a number worth trusting to wrap quietly.
    void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage,
        SpellInfo const* spellInfo) override
    {
        if (!attacker || !spellInfo || !damage || !attacker->IsPlayer())
            return;
        // Report #44 gives Subtlety its own, larger multiplier on BOTH Garrote
        // and Rupture, and makes them tick faster with haste. It replaces the
        // generic Garrote boost rather than stacking with it -- 11x on top of
        // 21x is 231x, which is not what anyone asked for.
        if (GetClassPerk(attacker->ToPlayer()) == SPELL_SUBTLETY)
        {
            ApplySubtletyBleed(target, attacker, damage, spellInfo);
            return;
        }
        if (sSpellMgr->GetFirstSpellInChain(spellInfo->Id) != SPELL_GARROTE)
            return;
        double const boosted = double(damage) * double(GARROTE_BLEED_MULT);
        damage = boosted >= double(std::numeric_limits<uint32>::max())
            ? std::numeric_limits<uint32>::max() : uint32(boosted);
    }

    bool ShouldTrackValuesUpdatePosByIndex(Unit const* unit, uint8 /*updateType*/, uint16 index) override
    {
        return unit && unit->IsCreature() && index == UNIT_FIELD_LEVEL;
    }

    void OnPatchValuesUpdate(Unit const* unit, ByteBuffer& valuesUpdateBuf, BuildValuesCachePosPointers& posPointers, Player* target) override
    {
        if (!unit || !target)
            return;
        auto it = posPointers.other.find(UNIT_FIELD_LEVEL);
        if (it == posPointers.other.end())
            return;
        if (uint32 const displayLevel = DisplayLevelOverride(unit, target))
            valuesUpdateBuf.put(it->second, displayLevel);
    }

    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        if (!attacker || !victim || !damage)
            return;
        if (Player* p = attacker->ToPlayer())
        {
            if (Creature* c = victim->ToCreature())
                if (uint32 const eff = EffectiveCreatureLevel(c, p))
                    damage = uint32(float(damage) * OutgoingScaleRatio(c, eff));
            if (!damage)
                damage = 1;
            if (GetClassPerk(p) == SPELL_ASSASSINATION && roll_chance_i(20))
                ApplyRandomPoison(p, victim);
        }
        if (Player* v = victim->ToPlayer())
        {
            if (Creature* c = attacker->ToCreature())
                if (uint32 const eff = EffectiveCreatureLevel(c, v))
                    damage = uint32(float(damage) * IncomingScaleRatio(c, eff));
        }
    }

    void OnAuraApply(Unit* unit, Aura* aura) override
    {
        UniformMount(unit, aura);
        NeutralizeStealthSpeed(unit, aura);
        ReduceCrowdControl(unit, aura);
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

// Not a real playerbot (see the 2026-08-20 wiki note on why that's not
// feasible for a temporary summon -- mod-playerbots hard-requires a real
// Player+WorldSession). Instead this reads the owning player's actual
// known Rogue spells each decision and applies a simple priority rotation
// with them, so it fights using the owner's real kit rather than a
// hand-picked fixed ability. Follows the owner out of combat; auto-engages
// whatever the owner is fighting.
struct npc_lg_shadow_cloneAI : public ScriptedAI
{
    npc_lg_shadow_cloneAI(Creature* c) : ScriptedAI(c) { }

    void Reset() override
    {
        // REACT_DEFENSIVE so being attacked actually registers as combat
        // at the engine level; the immediate-response part (was taking
        // ~10s to react) is handled explicitly below since this AI doesn't
        // go through the standard UpdateVictim()/threat-list path at all.
        // No forced SetReactState here anymore -- this is a real SUMMON_PET
        // now (2026-08-21 rework), and the player's Aggressive/Defensive/
        // Passive choice from the native pet frame is what should govern
        // this, not a hardcoded reset every time Reset() fires.
    }

    void UpdateAI(uint32 /*diff*/) override
    {
        Unit* ownerUnit = me->GetOwner();
        Player* owner = ownerUnit ? ownerUnit->ToPlayer() : nullptr;
        if (!owner || !owner->IsInWorld() || !me->IsWithinDistInMap(owner, 60.0f))
        {
            me->DespawnOrUnsummon();
            return;
        }

        ReactStates const stance = me->GetReactState();

        // Passive: never auto-engage, just follow. Matches how a passive
        // pet behaves everywhere else in the game.
        if (stance == REACT_PASSIVE)
        {
            if (me->GetVictim())
                me->AttackStop();
            if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
                me->GetMotionMaster()->MoveFollow(owner, 2.0f, frand(0.0f, 2.0f * float(M_PI)));
            return;
        }

        // Pet-aggressive: if something is actively hitting the clone and it
        // isn't already fighting back, engage immediately -- this is the
        // fix for "took ~10 seconds to react", since without it the clone
        // only ever picked a target from the owner's current selection.
        // Applies at Defensive too (fighting back when attacked is not the
        // same as picking fights), only true Passive skips this.
        if (!me->GetVictim())
        {
            if (Unit* attacker = me->getAttackerForHelper())
            {
                AttackStart(attacker);
                RunRotation(owner, attacker);
                DoMeleeAttackIfReady();
                return;
            }
        }

        // Aggressive: also proactively pick a fight with whatever's nearby,
        // same as the native stance means for any other controllable pet.
        if (stance == REACT_AGGRESSIVE && !me->GetVictim())
        {
            std::list<Unit*> nearby;
            Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck check(me, me, 20.0f);
            Acore::UnitListSearcher<Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck> searcher(me, nearby, check);
            Cell::VisitObjects(me, searcher, 20.0f);
            for (Unit* u : nearby)
            {
                if (u && u->IsAlive() && owner->IsValidAttackTarget(u))
                {
                    AttackStart(u);
                    RunRotation(owner, u);
                    DoMeleeAttackIfReady();
                    return;
                }
            }
        }

        Unit* ownerTarget = owner->GetSelectedUnit();
        bool const ownerFighting = owner->IsInCombat() && ownerTarget && ownerTarget->IsAlive()
            && owner->IsValidAttackTarget(ownerTarget);

        if (!ownerFighting)
        {
            if (me->GetVictim())
                me->AttackStop();
            if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
                me->GetMotionMaster()->MoveFollow(owner, 2.0f, frand(0.0f, 2.0f * float(M_PI)));
            return;
        }

        if (me->GetVictim() != ownerTarget)
            AttackStart(ownerTarget);

        if (!me->IsWithinMeleeRange(ownerTarget))
        {
            TryShadowstepTo(owner, ownerTarget);
            return;
        }

        RunRotation(owner, ownerTarget);
        DoMeleeAttackIfReady();
    }

private:
    void TryShadowstepTo(Player* owner, Unit* target)
    {
        uint32 const step = BestOwned(owner, SPELL_SHADOWSTEP);
        if (!step || me->HasSpellCooldown(step))
            return;
        me->CastSpell(target, step, false);
    }

    void RunRotation(Player* owner, Unit* target)
    {
        if (target->HasUnitState(UNIT_STATE_CASTING))
        {
            uint32 const kick = BestOwned(owner, SPELL_KICK);
            if (kick && !me->HasSpellCooldown(kick))
            {
                me->CastSpell(target, kick, false);
                return;
            }
        }
        if (target->GetHealthPct() < 35.0f)
        {
            uint32 const finisher = BestOwned(owner, SPELL_EVISCERATE);
            if (finisher && !me->HasSpellCooldown(finisher))
            {
                me->CastSpell(target, finisher, false);
                return;
            }
        }
        uint32 const rupture = BestOwned(owner, SPELL_RUPTURE);
        if (rupture && !me->HasSpellCooldown(rupture) && !target->HasAura(rupture, me->GetGUID()))
        {
            me->CastSpell(target, rupture, false);
            return;
        }
        uint32 const builder = BestOwned(owner, SPELL_SINISTER_STRIKE);
        if (builder && !me->HasSpellCooldown(builder))
            me->CastSpell(target, builder, false);
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
    g_cfg.zoneMinLevel = sConfigMgr->GetOption<uint32>("LivingGear.ZoneScale.MinPlayerLevel", 2);
    g_cfg.zoneFloor = sConfigMgr->GetOption<float>("LivingGear.ZoneScale.RewardFloor", 0.35f);
    g_cfg.zoneDecay = sConfigMgr->GetOption<float>("LivingGear.ZoneScale.RewardGapDecay", 12.0f);
    g_cfg.zoneIncoming = sConfigMgr->GetOption<float>("LivingGear.ZoneScale.IncomingPerLevel", 0.12f);
    g_cfg.dungeonScale = sConfigMgr->GetOption<bool>("LivingGear.ZoneScale.Dungeons", true);
    g_cfg.softenImmunity = sConfigMgr->GetOption<bool>("LivingGear.SoftenCreatureImmunity", true);
    g_cfg.ignoreSpellReqs = sConfigMgr->GetOption<bool>("LivingGear.IgnoreSpellRequirements", true);
    g_cfg.reconcilePerkSpells = sConfigMgr->GetOption<bool>("LivingGear.ReconcilePerkSpells", true);
    g_cfg.questScale = sConfigMgr->GetOption<bool>("LivingGear.QuestScale.Enable", true);
    // 10, not the 4 this shipped with. The kill floor pays 2% of the level
    // bar per mob and 4% per elite, so a 4% quest was worth exactly two mobs
    // -- which does not complement questing, it replaces it. At 10% a quest
    // is worth five mobs and both halves of levelling stay worth doing.
    g_cfg.questFloorPct = sConfigMgr->GetOption<uint32>("LivingGear.QuestScale.FloorPct", 10);
    if (g_cfg.questFloorPct > 100)
        g_cfg.questFloorPct = 100;
    g_cfg.killFloor = sConfigMgr->GetOption<bool>("LivingGear.KillXpFloor.Enable", true);
    g_cfg.killFloorPct = sConfigMgr->GetOption<uint32>("LivingGear.KillXpFloor.Pct", 2);
    g_cfg.killFloorElitePct = sConfigMgr->GetOption<uint32>("LivingGear.KillXpFloor.ElitePct", 4);
    if (g_cfg.killFloorPct > 100)
        g_cfg.killFloorPct = 100;
    if (g_cfg.killFloorElitePct > 100)
        g_cfg.killFloorElitePct = 100;
    g_cfg.groupKillXp = sConfigMgr->GetOption<bool>("LivingGear.GroupKillXp", true);
    g_cfg.instantMount = sConfigMgr->GetOption<bool>("LivingGear.InstantMount", true);
    g_cfg.uniformMount = sConfigMgr->GetOption<bool>("LivingGear.UniformMountSpeed", true);
    g_cfg.autoMount = sConfigMgr->GetOption<bool>("LivingGear.AutoMount", true);
    g_cfg.dungeonPar = sConfigMgr->GetOption<uint32>("LivingGear.DungeonTimer.DefaultParSec", 1800);
    g_cfg.curatorTick = sConfigMgr->GetOption<uint32>("LivingGear.CollectionPassive.TickMs", 60000);
    g_cfg.skillPointDivisor = sConfigMgr->GetOption<uint32>("LivingGear.Perks.SkillPointDivisor", 10);
    if (!g_cfg.skillPointDivisor)
        g_cfg.skillPointDivisor = 10;
    g_cfg.perkRespecCooldown = sConfigMgr->GetOption<uint32>("LivingGear.Perks.RespecCooldown", 300);
    g_cfg.perkCostEpoch = sConfigMgr->GetOption<uint32>("LivingGear.Perks.CostEpoch", 1);
    DetectPerkPriceSchema();
    LoadPerkPrices();
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

// Called from LivingGear_ClassPerks.cpp's SelectClassPerk() when a player
// actively picks Rogue: Subtlety as their class perk -- the login-only
// grant doesn't cover a live perk switch, so without this call Shadow
// Dance/Shadowstep/Shadow Clone stayed ungranted until the player's next
// login even after picking Subtlety.
void LivingGear_GrantSubtletyPerks(Player* player)
{
    LivingGearPerks::PerksPlayer::GrantSubtletyPerks(player);
}

// Config gate for the creature-immunity core patch in Unit.cpp. A creature
// that would be outright immune to a damage school takes a flat 80% resist
// instead, so "this mob cannot be hurt by Frost at all" stops existing while
// the mob still shrugs off most of the hit. Player immunities (Divine
// Shield, Ice Block, Anti-Magic Shell) are untouched -- the core patch
// checks IsCreature() before ever asking this.
// The canonical "does this player have this perk", exported so other module
// files stop keeping their own copy. Six files had six versions of this and
// two of them disagreed: Next.cpp asked only HasSpell (bug #25's mechanism)
// and Gather.cpp asked only the account set, so the same perk could read
// owned in one file and missing in another.
bool LivingGear_HasPerk(Player* player, uint32 spellId)
{
    return LivingGearPerks::HasPerk(player, spellId);
}

// Is this perk a real, pressable ability? See PerkIsCastable.
//
// Exported because every UnlockPerk has to ask it before learning. Learning a
// badge produces "You have learned a new ability: *Mine: 150" in chat and puts
// nothing anywhere -- badges have no SkillLineAbility row -- so it is pure
// noise. Removing the early return in UnlockPerk yesterday fixed alts not
// getting their perks and simultaneously turned every login into a wall of
// those messages, because a badge whose condition still holds gets re-unlocked
// each time.
bool LivingGear_PerkIsCastable(uint32 spellId)
{
    return LivingGearPerks::PerkIsCastable(spellId);
}

// Every UnlockPerk calls this when the perk was already owned, so an account
// that BOUGHT a rank and then met its condition anyway gets the points back
// instead of having paid for something free. Exported for the same reason
// LivingGear_PerkIsCastable is: there are six copies of UnlockPerk across this
// module plus a direct grant in LivingGear_Vault.cpp, and every one of them is
// a place a purchase can be made redundant.
void LivingGear_RefundIfPurchased(Player* player, uint32 spellId)
{
    LivingGearPerks::RefundIfPurchased(player, spellId);
}

bool LivingGear_SoftenCreatureImmunity()
{
    return LivingGearPerks::g_cfg.softenImmunity;
}

// Config gate for the core patch that waives weapon, positioning and reagent
// requirements on player casts. All three are the same kind of rule -- WotLK
// telling a player "not with that weapon / not from there / not without the
// component" -- and none of them are wanted here. Creature casts keep every
// check; the patch sites test IsPlayer() before consulting this.
bool LivingGear_IgnoreSpellRequirements()
{
    return LivingGearPerks::g_cfg.ignoreSpellReqs;
}

// Called from a core patch in Spell::CheckCast (Spell.cpp).
bool LivingGear_BypassStealthRequirement(Unit* caster)
{
    return LivingGearPerks::BypassStealthRequirement(caster);
}

// Addon-command entry point, called by the dispatcher in LivingGear.cpp.
bool LivingGear_HandlePerksCommand(Player* player, std::string const& msg)
{
    return LivingGearPerks::HandleLgChat(player, msg);
}

void LivingGear_SendPerksSync(Player* player)
{
    LivingGearPerks::SendPerkSync(player);
}

// Pushed as part of the ordinary sync now, not only when the Armory is opened.
// The Items panel shows real gear and attuned entitlements in one list, and two
// feeds arriving at different times meant the list visibly rewrote itself a
// moment after opening.
void LivingGear_SendArmorySync(Player* player)
{
    LivingGearPerks::SendArmory(player);
}

void AddSC_LivingGearPerks()
{
    new LivingGearPerks::PerksWorld();
    new LivingGearPerks::PerksPlayer();
    new LivingGearPerks::PerksSpell();
    new LivingGearPerks::PerksUnit();
    new LivingGearPerks::PerksMap();
    new LivingGearPerks::npc_lg_shadow_clone();
}
