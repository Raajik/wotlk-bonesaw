/*
 * Living Gear class-spec combat perks. All 10 classes, all 3 specs each,
 * are implemented here except Paladin Holy/Retribution (LivingGear_Next.cpp)
 * and Rogue Assassination/Subtlety (LivingGear_Perks.cpp).
 *
 * Paladin Holy (910069) and Retribution (910071) already live in
 * LivingGear_Next.cpp; Rogue Assassination (910035) and Subtlety (910037)
 * already live in LivingGear_Perks.cpp. This file does not touch those.
 *
 * Fresh implementation informed by (not copied from) the pre-split
 * LivingGear.cpp.backup-20260818 "frankenfile" -- that file never compiled
 * cleanly and is not restored here. See A:\obsidian\jeremy\wiki\Bonesaw.md
 * "Crashes / never again" before changing anything in this file.
 *
 * Safety notes:
 *  - No SetStackAmount/ModStackAmount on any custom dummy aura anywhere in
 *    this file. Stacking state (Fury haste stacks) lives in a server-side
 *    map, same pattern as Kill Combo in LivingGear_Perks.cpp.
 *  - Bladestorm (Warrior Arms) has documented crash history in this
 *    codebase (effect-3/CheckCast-after-aura-removal). This file does NOT
 *    implement "never ends, recast to stop" -- only "learn it, no rage
 *    cost, no cooldown". See TryWarriorArmsOnCast for details.
 *  - No player->m_Events.AddEventAtOffset delayed-event scheduling. That
 *    pattern only exists in the abandoned backup, never in the live
 *    4-file codebase, so it is treated as unproven here. Multi-target
 *    effects (Avenger's Shield bounce, Thunder Clap cleave) run
 *    synchronously inside the triggering hook instead.
 */

#include "AllSpellScript.h"
#include "CellImpl.h"
#include "ObjectAccessor.h"
#include "Chat.h"
#include "CreatureAI.h"
#include "CreatureScript.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "DynamicObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "GameTime.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
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
#include "SpellDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "TemporarySummon.h"
#include "Timer.h"
#include "Unit.h"
#include "Util.h"
#include "WorldSession.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Castable perks only get learned; badges do not. LivingGear_Perks.cpp.
bool LivingGear_PerkIsCastable(uint32 spellId);
void LivingGear_RefundIfPurchased(Player* player, uint32 spellId);

// LivingGear_Perks.cpp -- owns Rogue Subtlety. Declared at global scope
// (not inside namespace LivingGearClassPerks below) so the unqualified
// call in SelectClassPerk() actually binds to the real global-scope
// definition instead of an unresolved namespace-local declaration.
void LivingGear_GrantSubtletyPerks(Player* player);
bool LivingGear_SafeToCastOn(Player* player); // LivingGear_Support.cpp

class Player;
void LivingGear_SendAddonLine(Player* player, std::string const& line); // LivingGear.cpp
bool LivingGear_IsAddonSendInProgress(); // LivingGear.cpp

namespace LivingGearClassPerks
{
// -------------------------------------------------------------------------
// Class-perk pick spells (client name/icon/tooltip already shipped via
// tools/client-patch/build_patch.py's CUSTOM_SPELLS dict).
// -------------------------------------------------------------------------
uint32 const SPELL_MAGE_ARCANE = 910032;
uint32 const SPELL_MAGE_FIRE = 910033;
uint32 const SPELL_MAGE_FROST = 910034;
uint32 const SPELL_ROGUE_ASSASSINATION = 910035; // owned by LivingGear_Perks.cpp; only used here for selection bookkeeping
uint32 const SPELL_ROGUE_COMBAT = 910036;
uint32 const SPELL_ROGUE_SUBTLETY = 910037; // owned by LivingGear_Perks.cpp
uint32 const SPELL_PALADIN_HOLY = 910069; // owned by LivingGear_Next.cpp
uint32 const SPELL_PALADIN_PROTECTION = 910070;
uint32 const SPELL_PALADIN_RETRIBUTION = 910071; // owned by LivingGear_Next.cpp
uint32 const SPELL_WARRIOR_ARMS = 910083;
uint32 const SPELL_WARRIOR_FURY = 910084;
uint32 const SPELL_WARRIOR_PROTECTION = 910085;
uint32 const SPELL_HUNTER_MARKSMANSHIP = 910150;
uint32 const SPELL_SHAMAN_ELEMENTAL = 910151;
uint32 const SPELL_DK_UNHOLY = 910152;
uint32 const SPELL_HUNTER_BEAST_MASTERY = 910153;
uint32 const SPELL_HUNTER_SURVIVAL = 910154;
uint32 const SPELL_SHAMAN_ENHANCEMENT = 910155;
uint32 const SPELL_SHAMAN_RESTORATION = 910156;
uint32 const SPELL_WARLOCK_AFFLICTION = 910157;
uint32 const SPELL_WARLOCK_DEMONOLOGY = 910158;
uint32 const SPELL_WARLOCK_DESTRUCTION = 910159;
uint32 const SPELL_DRUID_BALANCE = 910160;
uint32 const SPELL_DRUID_FERAL = 910161;
uint32 const SPELL_DRUID_RESTORATION = 910162;
uint32 const SPELL_PRIEST_DISCIPLINE = 910163;
uint32 const SPELL_PRIEST_HOLY = 910164;
uint32 const SPELL_PRIEST_SHADOW = 910165;
uint32 const SPELL_DK_BLOOD = 910166;
uint32 const SPELL_DK_FROST = 910167;

uint32 const MAGE_CLASS_PERKS[] = { SPELL_MAGE_ARCANE, SPELL_MAGE_FIRE, SPELL_MAGE_FROST };
uint32 const ROGUE_CLASS_PERKS[] = { SPELL_ROGUE_ASSASSINATION, SPELL_ROGUE_COMBAT, SPELL_ROGUE_SUBTLETY };
uint32 const PALADIN_CLASS_PERKS[] = { SPELL_PALADIN_HOLY, SPELL_PALADIN_PROTECTION, SPELL_PALADIN_RETRIBUTION };
uint32 const WARRIOR_CLASS_PERKS[] = { SPELL_WARRIOR_ARMS, SPELL_WARRIOR_FURY, SPELL_WARRIOR_PROTECTION };
uint32 const HUNTER_CLASS_PERKS[] = { SPELL_HUNTER_MARKSMANSHIP, SPELL_HUNTER_BEAST_MASTERY, SPELL_HUNTER_SURVIVAL };
uint32 const SHAMAN_CLASS_PERKS[] = { SPELL_SHAMAN_ELEMENTAL, SPELL_SHAMAN_ENHANCEMENT, SPELL_SHAMAN_RESTORATION };
uint32 const DK_CLASS_PERKS[] = { SPELL_DK_UNHOLY, SPELL_DK_BLOOD, SPELL_DK_FROST };
uint32 const WARLOCK_CLASS_PERKS[] = { SPELL_WARLOCK_AFFLICTION, SPELL_WARLOCK_DEMONOLOGY, SPELL_WARLOCK_DESTRUCTION };
uint32 const DRUID_CLASS_PERKS[] = { SPELL_DRUID_BALANCE, SPELL_DRUID_FERAL, SPELL_DRUID_RESTORATION };
uint32 const PRIEST_CLASS_PERKS[] = { SPELL_PRIEST_DISCIPLINE, SPELL_PRIEST_HOLY, SPELL_PRIEST_SHADOW };

// -------------------------------------------------------------------------
// Real WotLK spells this module casts or reacts to.
// -------------------------------------------------------------------------
uint32 const SPELL_MIRROR_IMAGE = 55342;
uint32 const SPELL_LIVING_BOMB = 44457;
uint32 const SPELL_MAGE_COMBUSTION = 11129;
uint32 const SPELL_ICE_LANCE_R1 = 30455;
// Ice Lance ranks 2/3 (42913/42914) used to be named here for a hand-written
// level table. BestOwnedOrFirst walks the chain from rank 1 instead.
uint32 const SPELL_BLIZZARD_RANKS[] = { 10, 6141, 8427, 10185, 10186, 10187, 27085, 42939, 42940 };
uint32 const SPELL_FIRE_BLAST_R1 = 2136;
uint32 const SPELL_ARCANE_POWER = 12042;
// Living Bomb's explosion is a separate spell per rank. The aura effect
// carries its own explosion id in GetAmount() (see spell_mage_living_bomb),
// so nothing here has to map ranks by hand -- this list exists only so the
// damage multiplier can recognise an explosion when it lands.
uint32 const SPELL_LIVING_BOMB_BLASTS[] = { 44461, 55359, 55360, 55361, 55362 };
// +300%, matching the Assassination poison and Fury bleed perks.
uint32 const MAGE_DAMAGE_MULT = 4;
uint32 const MAGE_BLIZZARD_TICK_MS = 1000;
uint32 const SPELL_BLADESTORM = 46924;
// Report #120: Hurricane ranks, rank-1 first (16914,17401,17402,27012,48467).
// De-channeled server-side so the free Hurricane from Moonfire behaves like
// Blizzard: the circle is placed and keeps damaging until it expires, with no
// channel holding the druid in place.
uint32 const SPELL_HURRICANE_RANKS[] = { 16914, 17401, 17402, 27012, 48467 };
uint32 const DRUID_HURRICANE_TICK_MS = 1000;
// Report #130: while Starfall is up, each Starfall damage hit on a target has
// a 30% chance to fire a free Moonfire at that target. Declared here because
// TickDruidBalanceStarfall's hook (~:3018) reads it and function bodies are
// ordered before the druid constants block.
uint32 const DRUID_STARFALL_MOONFIRE_CHANCE = 30;
uint32 const SPELL_SHOCKWAVE = 46968;
uint32 const SPELL_THUNDER_CLAP = 6343;
uint32 const SPELL_WHIRLWIND = 1680;
// Bug report #19: while Bladestorm is spinning, Whirlwind and Thunder Clap fire
// on their own on this cadence, shortened by haste.
// Report #146: cadence tightened from 6s to 1s (the 1s floor in the tick keeps
// haste from pushing it below that).
uint32 const BLADESTORM_AUTOCAST_MS = 1000;
uint32 const SPELL_REND = 772;
uint32 const SPELL_DEEP_WOUNDS_DOT = 12721;
uint32 const SPELL_DEEP_WOUNDS_TALENT[] = { 12834, 12849, 12867 };
// Kit 2 "Lord of Shields and Thunder" (910085). Ranks verified from
// acore_world.spell_ranks / Spell.dbc: Shield Slam chain 23922 -> 47488
// (rank 8, the level-80 rank), Thunder Clap 6343 -> 47502, Hamstring 1715,
// Deep Wounds DoT 12721.
uint32 const SPELL_SHIELD_SLAM_R1 = 23922;
// "Shield Slams hit 3 additional times" (Divine Storm multi-hit shape).
uint32 const WARRIOR_PROT_SS_EXTRA_HITS = 3;
uint32 const WARRIOR_PROT_SS_HIT_DELAY_MS = 120;
// Avalanche reuses Death and Decay 43265 (single rank, verified) as its
// ground-target vehicle: the engine places the 12-yard persistent circle at
// the target point and the module tick drives damage/slows on top of it.
uint32 const SPELL_DEATH_AND_DECAY = 43265;
uint32 const SPELL_HAMSTRING = 1715;
// Defensive Tactics (29559): single-effect self buff, Aura1 = 112
// (SPELL_AURA_MOD_BLOCK_PERCENT), self target, ~30s duration. CastCustomSpell
// overrides the amount so the passive reads exactly +25% block chance while
// Avalanche is on the ground.
uint32 const SPELL_AVALANCHE_BLOCK_AURA = 29559;
int32 const AVALANCHE_BLOCK_PCT = 25;
uint32 const AVALANCHE_TICK_MS = 1000;
// Recasting ON TOP of the live circle (within this distance of its centre)
// toggles it off; casting further away moves it.
float const AVALANCHE_TOGGLE_RANGE = 5.0f;
// RadiusMod is a direct multiplier (Spell.cpp: radius * RadiusMod), so x3 is
// 30000 -- a second RADIUS_MOD_DOUBLE (20000) would give x4, not x3.
uint32 const RADIUS_MOD_TRIPLE = 30000;
uint32 const SPELL_AVENGERS_SHIELD = 31935;
uint32 const SPELL_DEVOTION_AURA = 465;
uint32 const SPELL_BLADE_FLURRY = 13877;
uint32 const SPELL_KILLING_SPREE = 51690;
uint32 const SPELL_CHIMERA_SHOT = 53209; // single rank
uint32 const SPELL_SERPENT_STING_R1 = 1978;
uint32 const SPELL_AIMED_SHOT_R1 = 19434;
uint32 const SPELL_THUNDERSTORM = 51490; // rank 1 of 4 -- use RankOf
uint32 const SPELL_LAVA_BURST_R1 = 51505;
uint32 const SPELL_CHAIN_LIGHTNING_R1 = 421;
uint32 const SPELL_ARMY_OF_THE_DEAD = 42650;
uint32 const SPELL_SUMMON_GARGOYLE = 49206;
uint32 const NPC_GHOUL_TANK = 910202;
uint32 const NPC_GHOUL_HEALER = 910203;
uint32 const NPC_GHOUL_DPS = 910204;

// Kit 7 (Unholy 910152, user-revised definition). Disease ids verified from
// Spell.dbc + acore_world.spell_ranks at level-80 rank:
// - Blood Plague 55078 and Frost Fever 55095 are SINGLE ids for all ranks in
//   3.3.5 (Plague Strike 45462/49917-49921 all trigger 55078; Icy Touch
//   45477..49909 all trigger 55095 -- verified in the DBC trigger columns),
//   so there is no rank chain to walk and spell_ranks (which lacks both) is
//   irrelevant for them.
// - Ebon Plaguebringer 51726->51734->51735 (spell_ranks chain, rank 3 =
//   level 80). Talent-only: BestOwned() may legitimately return 0.
// - Unholy Blight 49194->50536 has no spell_ranks chain (not loaded), so the
//   rank is picked explicitly via HasSpell below.
// - 65142 (the plan's Desecration-visual candidate) does not exist in this
//   DBC. Visual marker instead: Unholy Blight itself, the canonical blight
//   aura wrapped around the DK (green swirl, client-visible), re-cast on the
//   DK every tick so it reads as a permanent shroud.
uint32 const SPELL_BLOOD_PLAGUE_DISEASE = 55078;
uint32 const SPELL_FROST_FEVER_DISEASE = 55095;
uint32 const SPELL_EBON_PLAGUE_R1 = 51726;
uint32 const SPELL_UNHOLY_BLIGHT_R1 = 49194;
uint32 const SPELL_UNHOLY_BLIGHT_R2 = 50536;
uint32 const NPC_EBON_GARGOYLE = 27829; // summoned by 49206/61777 (DBC basepoints)
uint32 const DK_UNHOLY_BLIGHT_TICK_MS = 2000;
float const DK_UNHOLY_BLIGHT_RANGE = 10.0f;
// x5, matching the MAGE_DAMAGE_MULT/WARLOCK_AFFLICTION_DOT_MULT precedent
// (hook-applied multiplier in the periodic-damage-tick hook).
uint32 const DK_UNHOLY_DISEASE_MULT = 5;
// Refreshed into the gargoyle's TempSummon timer every blight tick while the
// creature is alive, so it only ever dies to damage, never to its own timer.
uint32 const DK_GARGOYLE_REFRESH_MS = 30000;

// Hunter
uint32 const SPELL_BESTIAL_WRATH = 19574;
uint32 const SPELL_EXPLOSIVE_SHOT_R1 = 53301;
// The spell the core casts to deal Explosive Shot's damage, for all ranks.
uint32 const SPELL_EXPLOSIVE_SHOT_DAMAGE = 53352;
uint32 const SPELL_HUNTER_TRAP_FIRST_RANKS[] = { 13795 /*Immolation*/, 1499 /*Freezing*/, 13809 /*Frost*/, 13813 /*Explosive*/ };
uint32 const SPELL_SNAKE_TRAP = 34600; // no rank chain
// Survival "Call of the Wilds" -- a new castable (spell_dbc row lives in
// rev_living_gear_call_of_the_wilds; client DBC entry in
// tools/client-patch/build_patch.py). Declared here, above CLASS_PERK_GRANTS,
// which hands it out.
uint32 const SPELL_CALL_OF_WILDS = 910181;
// Shaman
uint32 const SPELL_FERAL_SPIRIT = 51533;
uint32 const SPELL_STORMSTRIKE = 17364;
uint32 const NPC_SPIRIT_WOLF = 29264;
uint32 const SPELL_RIPTIDE = 61295;
uint32 const SPELL_CHAIN_HEAL_R1 = 1064;
// Warlock
uint32 const SPELL_METAMORPHOSIS = 47241;
// Fel Domination (18708) is Demonology's Imp Legion toggle button. Present in
// var/mmap-output/dbc/Spell.dbc. Declared here, ABOVE CLASS_PERK_GRANTS,
// which grants it (a talent spell, so it needs the explicit grant -- #123).
uint32 const SPELL_FEL_DOMINATION = 18708;
uint32 const SPELL_CHAOS_BOLT_R1 = 50796;
uint32 const SPELL_CONFLAGRATE_R1 = 17962;
// Reports #105/#116: Haunt is the Affliction perk's button. The auto-applied
// debuffs are listed as rank-1 chain heads; BestOwned() picks the highest one
// the warlock actually learned.
uint32 const SPELL_HAUNT = 48181;
uint32 const SPELL_UNSTABLE_AFFLICTION_R1 = 30108;
uint32 const SPELL_CORRUPTION_R1 = 172;
uint32 const SPELL_CURSE_OF_AGONY_R1 = 980;
uint32 const SPELL_CURSE_OF_ELEMENTS_R1 = 1490;
// Druid
uint32 const SPELL_STARFALL = 48505;
uint32 const SPELL_ECLIPSE_SOLAR = 48517;
uint32 const SPELL_ECLIPSE_LUNAR = 48518;
uint32 const SPELL_BERSERK_DRUID = 50334;
uint32 const SPELL_WILD_GROWTH_R1 = 48438;
uint32 const SPELL_REJUVENATION_R1 = 774;
// Reports #85/#86: Insect Swarm spreads to everything within 25 yards on
// cast and auto-casts on struck enemies; Moonkin Form is granted so the
// shape-shift exists. Thorns (#87) goes through TickDruidBalanceThorns.
uint32 const SPELL_INSECT_SWARM_R1 = 5570;
uint32 const SPELL_MOONKIN_FORM = 24858;
uint32 const SPELL_THORNS_R1 = 467;
// Enhancement "The Storm Caller" echo bolts + wolf procs need the Lightning
// Bolt chain head; verified from acore_world.spell_ranks (403 = rank 1,
// 49238 = rank 14, the level-80 rank).
uint32 const SPELL_LIGHTNING_BOLT_R1 = 403;
// Report #120: Moonfire/Hurricane and the Wrath/Starfire pair. Rank chains
// verified in acore_world.spell_ranks (ranks in comment).
uint32 const SPELL_MOONFIRE_R1 = 8921;    // 8921..48463, 14 ranks
uint32 const SPELL_HURRICANE_R1 = 16914;  // 16914,17401,17402,27012,48467
uint32 const SPELL_WRATH_R1 = 5176;       // 5176..48461, 12 ranks
uint32 const SPELL_STARFIRE_R1 = 2912;    // 2912..48465, 10 ranks
// Priest
uint32 const SPELL_PENANCE_R1 = 47540;
// Penance's damage half, ranked in lockstep with the channel above
// (spell_priest.cpp SPELL_PRIEST_PENANCE_R1_DAMAGE). Used by the ricochet.
uint32 const SPELL_PENANCE_R1_DAMAGE = 47758;
uint32 const SPELL_POWER_WORD_SHIELD_R1 = 17;
uint32 const SPELL_GUARDIAN_SPIRIT = 47788;
uint32 const SPELL_SHADOWFIEND = 34433;
uint32 const SPELL_MIND_FLAY_R1 = 15407;
// The spell that actually carries Mind Flay's damage, for all nine ranks.
uint32 const SPELL_MIND_FLAY_DAMAGE = 58381;
// Shadow Priest "The Void Choir" (910165). Rank chains from acore_world.spell_ranks:
// Mind Blast 8092 -> 48127 (13 ranks), Shadow Word: Pain 589 -> 48125 (12 ranks).
// Shadowform (15473, single rank) is the Voidform toggle vehicle -- a permanent
// aura-bearing castable the priest already knows, same trick Starfall's toggle uses.
uint32 const SPELL_MIND_BLAST_R1 = 8092;
uint32 const SPELL_SW_PAIN_R1 = 589;
uint32 const SPELL_SHADOWFORM = 15473;
// What Shadowfiend (34433) actually summons: Spell.dbc row 34433, Effect_2 = 28
// (SPELL_EFFECT_SUMMON), EffectMiscValue = 19668 -- creature_template 19668
// "Shadowfiend". The tendrils clone that entry so they reuse its model/stats.
uint32 const PRIEST_SHADOWFIEND_ENTRY = 19668;
uint32 const PRIEST_VOID_TENDRIL_COUNT = 2;        // spawned per Mind Flay channel
uint32 const PRIEST_VOID_TENDRIL_MAX_ALIVE = 6;    // oldest despawned beyond this
uint32 const PRIEST_VOID_TENDRIL_LIFETIME_MS = 9000; // 3s flay channel + 6s linger
uint32 const PRIEST_VOID_TICK_MS = 3000;           // Voidform free Mind Blast cadence
// Death Knight
uint32 const SPELL_DANCING_RUNE_WEAPON = 49028;
uint32 const SPELL_HUNGERING_COLD = 49203;
// Declared here rather than below CLASS_PERK_GRANTS: DK Frost grants both, so
// the table needs them in scope.
uint32 const SPELL_FROST_STRIKE_R1 = 49143;
uint32 const SPELL_OBLITERATE_R1 = 49020;

// ---------------------------------------------------------------------
// What each spec hands you when you pick it.
//
// Bug reports #33, #36, #37, #39, #40 are one hole: picking a spec did not
// teach the abilities that spec is entirely about. Three specs out of thirty
// granted anything; one revoked anything. #36 states the rule the system was
// missing -- "should be given even at level 1 so they can be used while
// levelling" -- so these are handed over regardless of level, at the best rank
// the character qualifies for.
//
// Rank-1 ids only. The core already knows every chain (spell_ranks), so
// BestRankForLevel walks it; hardcoding per-rank ids is how Living Bomb ended
// up stuck at rank 1 in report #38.
//
// Every id here was verified: from AzerothCore's own script constants, from
// the spell_ranks table, or from the spell links players pasted into their
// reports. None were recalled from memory.
// ---------------------------------------------------------------------
// Penance, Wild Growth and Explosive Shot already had constants above;
// my independent lookups matched them, which is a useful cross-check.
uint32 const SPELL_ENVENOM_R1 = 32645;          // Spell.dbc, 8 ranks
uint32 const SPELL_ADRENALINE_RUSH_R1 = 13750;  // Spell.dbc, 4 ranks
uint32 const ROGUE_DETONATE_RANGE = 15.0f;
uint32 const SPELL_CRUSADER_STRIKE_G = 35395;   // report #37's own link
uint32 const SPELL_CONSECRATION_R1 = 26573;     // spell_ranks, 8 ranks
uint32 const SPELL_HOLY_SHOCK_R1 = 20473;       // spell_paladin.cpp
uint32 const SPELL_DIVINE_STORM_G = 53385;      // spell_paladin.cpp
uint32 const SPELL_HEMORRHAGE_G = 16511;        // RANK 1 -- verified in Spell.dbc.
// Report #40 linked 17347, which is a later rank; granting that to a level 1
// rogue would hand over something unusable. Take rank ids from the DBC, not
// from a player's link.
uint32 const SPELL_SHADOWSTEP_G = 36554;        // report #42's own link
// Retribution's Exorcism splash lives in LivingGear_Next.cpp (SPELL_EXORCISM),
// which is a separate translation unit, so the id is repeated here rather than
// shared. Verified against Spell.dbc (name "Exorcism") and spell_ranks (rank 1
// of its own chain) rather than copied from the abandoned backup file.
uint32 const SPELL_EXORCISM_R1 = 879;

// Widened from 3 to 8 when the grant rule changed to "everything the spec
// needs" (below). Survival alone hands over Explosive Shot plus five traps.
uint32 const MAX_CLASS_PERK_GRANT_SPELLS = 8;

struct ClassPerkGrant
{
    uint32 perk;
    uint32 spells[MAX_CLASS_PERK_GRANT_SPELLS];   // rank-1 ids, 0-terminated
};

// THE RULE (set 2026-08-23): picking a spec grants every ability its
// description names AND every ability its implementation reaches for.
//
// The point is that you can play the spec from level 1 instead of spending 50+
// levels waiting for it to become the thing the tooltip described. A perk that
// silently does nothing because the player has not trained its trigger yet is
// indistinguishable from a broken perk -- and the audit found six specs in
// exactly that state (Fire had no Fire Blast to detonate with, Destruction
// granted nothing at all while promising two talent spells).
//
// So "amplifier" specs still grant what they amplify: Fury multiplies Rend, so
// Fury hands you Rend. The only spells deliberately absent are ones every
// character of that class already has at level 1 (Fireball, Power Word: Shield)
// and pure passives with no spell to learn (Titan's Grip, the Eclipse auras,
// which are cast rather than trained).
//
// Rank-1 ids only -- ApplyClassPerkSpells runs BestRankForLevel over each, so
// the character always receives the best rank they qualify for and is topped up
// on every login as they level.
ClassPerkGrant const CLASS_PERK_GRANTS[] =
{
    { SPELL_PALADIN_HOLY,        { SPELL_CONSECRATION_R1, SPELL_HOLY_SHOCK_R1, 0 } },
    { SPELL_PALADIN_PROTECTION,  { SPELL_AVENGERS_SHIELD, SPELL_DEVOTION_AURA, 0 } },
    // Retribution's third clause is "Crusader Strike also casts Exorcism".
    { SPELL_PALADIN_RETRIBUTION, { SPELL_CRUSADER_STRIKE_G, SPELL_DIVINE_STORM_G,
                                   SPELL_EXORCISM_R1, 0 } },
    // Arms autocasts Whirlwind and Thunder Clap while Bladestorm spins
    // (TickWarriorArmsBladestorm reaches for both via BestOwned, which returns
    // 0 -- silently doing nothing -- for a warrior who has not trained them).
    { SPELL_WARRIOR_ARMS,        { SPELL_BLADESTORM, SPELL_WHIRLWIND,
                                   SPELL_THUNDER_CLAP, 0 } },
    // Protection: TC radius x3 and applies 3x Rend + Deep Wounds to every
    // enemy hit; Shield Slam launches 3 extra hits, each rupturing for 8 yards;
    // Death and Decay is the Avalanche toggle's castable ground-target vehicle
    // (recast to move, recast on the circle to dismiss).
    { SPELL_WARRIOR_PROTECTION,  { SPELL_SHOCKWAVE, SPELL_THUNDER_CLAP,
                                   SPELL_REND, SPELL_DEATH_AND_DECAY, 0 } },
    { SPELL_ROGUE_ASSASSINATION, { SPELL_ENVENOM_R1, 0, 0 } },
    { SPELL_ROGUE_COMBAT,        { SPELL_BLADE_FLURRY, SPELL_KILLING_SPREE,
                                   SPELL_ADRENALINE_RUSH_R1, 0 } },
    { SPELL_ROGUE_SUBTLETY,      { SPELL_HEMORRHAGE_G, SPELL_SHADOWSTEP_G, 0 } },
    { SPELL_MAGE_ARCANE,         { SPELL_ARCANE_POWER, SPELL_MIRROR_IMAGE, 0 } },
    // Fire Blast is the detonator (TryMageFireDetonate) and was the best half
    // of the perk. Ungranted, it did not exist below the level it is trained.
    { SPELL_MAGE_FIRE,           { SPELL_LIVING_BOMB, SPELL_FIRE_BLAST_R1, 0 } },
    // Report #54: "Blizzard was not un-learned when i swapped specs." Frost had
    // no entry here because GrantMageFrostBlizzard already existed -- and that
    // legacy path hands the spell over WITHOUT recording it in
    // lg_char_class_grant, so the revoke had nothing to act on. Anything a perk
    // grants has to come through this table or it can never be taken back.
    // Ice Lance is the cleave half of the perk (TickMageFrostIceLance).
    { SPELL_MAGE_FROST,          { SPELL_BLIZZARD_RANKS[0], SPELL_ICE_LANCE_R1, 0 } },
    // Chimera Shot refreshes Serpent Sting; the proc casts Aimed Shot.
    { SPELL_HUNTER_MARKSMANSHIP, { SPELL_CHIMERA_SHOT, SPELL_SERPENT_STING_R1,
                                   SPELL_AIMED_SHOT_R1, 0 } },
    { SPELL_HUNTER_BEAST_MASTERY,{ SPELL_BESTIAL_WRATH, 0, 0 } },
    // "Traps lose their cooldown and get a bigger blast radius" -- all five,
    // or the perk only applies to whichever ones happened to be trained.
    { SPELL_HUNTER_SURVIVAL,     { SPELL_EXPLOSIVE_SHOT_R1,
                                   SPELL_HUNTER_TRAP_FIRST_RANKS[0],
                                   SPELL_HUNTER_TRAP_FIRST_RANKS[1],
                                   SPELL_HUNTER_TRAP_FIRST_RANKS[2],
                                   SPELL_HUNTER_TRAP_FIRST_RANKS[3],
                                   SPELL_SNAKE_TRAP, SPELL_CALL_OF_WILDS, 0 } },
    // Lava Burst is doubled and Chain Lightning loses its target cap.
    { SPELL_SHAMAN_ELEMENTAL,    { SPELL_THUNDERSTORM, SPELL_LAVA_BURST_R1,
                                   SPELL_CHAIN_LIGHTNING_R1, 0 } },
    // Storm Caller: Thorns is the Static Field toggle vehicle (see the state
    // block comment above) -- granted so a level-1 spec pick can toggle it.
    { SPELL_SHAMAN_ENHANCEMENT,  { SPELL_FERAL_SPIRIT, SPELL_STORMSTRIKE,
                                   SPELL_THORNS_R1, 0 } },
    // "Chain Heal has no bounce cap."
    { SPELL_SHAMAN_RESTORATION,  { SPELL_RIPTIDE, SPELL_CHAIN_HEAL_R1, 0 } },
    { SPELL_DK_UNHOLY,           { SPELL_SUMMON_GARGOYLE, SPELL_ARMY_OF_THE_DEAD, 0 } },
    { SPELL_DK_BLOOD,            { SPELL_DANCING_RUNE_WEAPON, 0, 0 } },
    // "Frost Strike and Obliterate deal double damage."
    { SPELL_DK_FROST,            { SPELL_HUNGERING_COLD, SPELL_FROST_STRIKE_R1,
                                   SPELL_OBLITERATE_R1, 0 } },
    // Fel Domination is Demonology's Imp Legion toggle button (talent spell,
    // so it needs the explicit grant per report #123's lesson).
    { SPELL_WARLOCK_DEMONOLOGY,  { SPELL_METAMORPHOSIS, SPELL_FEL_DOMINATION, 0 } },
    // Was empty. The entire perk is Chaos Bolt + Conflagrate, both talents, so
    // an untalented Destruction warlock had a perk that did literally nothing.
    { SPELL_WARLOCK_DESTRUCTION, { SPELL_CHAOS_BOLT_R1, SPELL_CONFLAGRATE_R1, 0 } },
    // Was the only spec with no entry until reports #105/#116 gave Affliction
    // a named ability: Haunt is the perk's button (instant, no cooldown, seeds
    // the whole DoT set on the target). Report #123: Unstable Affliction is
    // granted here too -- it is a TALENT (30108 chain, trainable via the
    // Affliction tree, not a trainer spell), so BestOwned() returned 0 for an
    // untalented warlock and Haunt silently skipped it. Anything a perk grants
    // has to come through this table or it can never be taken back on spec
    // switch (see the Frost/Blizzard note above).
    { SPELL_WARLOCK_AFFLICTION,  { SPELL_HAUNT, SPELL_UNSTABLE_AFFLICTION_R1, 0 } },
    // Fury multiplies Rend and Deep Wounds. Deep Wounds is a pure talent with
    // no castable spell to learn, so only Rend can be handed over.
    { SPELL_WARRIOR_FURY,        { SPELL_REND, 0, 0 } },
    { SPELL_DRUID_BALANCE,       { SPELL_STARFALL, SPELL_MOONKIN_FORM, 0 } },
    { SPELL_DRUID_FERAL,         { SPELL_BERSERK_DRUID, 0, 0 } },
    // "Rejuvenation spreads to injured allies within 15 yards every 3 sec."
    { SPELL_DRUID_RESTORATION,   { SPELL_WILD_GROWTH_R1, SPELL_REJUVENATION_R1, 0 } },
    { SPELL_PRIEST_DISCIPLINE,   { SPELL_PENANCE_R1, 0, 0 } },
    { SPELL_PRIEST_HOLY,         { SPELL_GUARDIAN_SPIRIT, 0, 0 } },
    // "Mind Flay deals quadruple damage." Shadowform is granted as the Voidform
    // toggle vehicle: a level-1 priest gets the button too, and spec-switch
    // revokes it with the rest (the report #54 rule).
    { SPELL_PRIEST_SHADOW,       { SPELL_SHADOWFIEND, SPELL_MIND_FLAY_R1,
                                   SPELL_SHADOWFORM, 0 } },
};

float const CLASS_PERK_RANGE = 15.0f;
uint32 const MAGE_ARCANE_LINGER_MS = 60000;
uint32 const MAGE_FIRE_TICK_MS = 1000;
// How far from the mage we look for enemies already carrying Living Bomb to
// spread FROM (#128). The hop itself is MAGE_FIRE_SPREAD_RANGE; this only
// bounds the search, so the chain can creep past the mage's own range one hop
// at a time -- same shape as the warlock affliction contagion constants.
float const MAGE_FIRE_CONTAGION_RANGE = 60.0f;
// Feature #202: "spread to all enemies within 40 yards (like Affliction dots)"
float const MAGE_FIRE_SPREAD_RANGE = 40.0f;
// Feature #202: Living Bomb ticks deal 2000% more (x21), same ladder as the
// Affliction DoT multiplier. Fires from ModifyPeriodicDamageAurasTick.
float const MAGE_LIVING_BOMB_DMG_MULT = 21.0f;
uint32 const MAGE_FROST_ICE_TICK_MS = 2000;
uint32 const FURY_HASTE_CAP = 20;
uint32 const FURY_HASTE_PCT_PER_STACK = 5;
uint32 const FURY_HASTE_LINGER_MS = 60000;
uint32 const ROGUE_ENERGY_TICK_MS = 2000;
int32 const ROGUE_ENERGY_TICK_BONUS = 10; // +50% of the ~20-per-2s baseline energy tick
uint32 const ROGUE_KS_CHANCE = 30;
uint32 const PALADIN_AS_BOUNCES = 30;
// Report #81: "Avenger's Shield needs a much shorter cooldown or a chance to
// proc off other abilities like Judgement." Report #132 raises the ask:
// Judgement AND Hammer of the Righteous at 50%, AS's own cooldown at 6s.
uint32 const PALADIN_AS_PROC_CHANCE = 50;
uint32 const SPELL_JUDGEMENT_R1 = 20271;
// Report #132: the prot rework ask -- AS bounces "a bunch of times" with a
// 6s cooldown, and Judgement/Hammer of the Righteous each roll a 50% chance
// to fire the shield. The bounce count and the Judgement proc already exist
// (#81); this pass raises the proc to 50% (overriding the #81 value), adds
// HotR as a second proc source, and cuts the shield's own cooldown to 6s
// via the existing deferred clear (PALADIN_AS_COOLDOWN_MS).
uint32 const PALADIN_AS_COOLDOWN_MS = 6000;
uint32 const SPELL_HAMMER_OF_THE_RIGHTEOUS_R1 = 53595;
float const PALADIN_AS_HOP_RANGE = 10.0f;
float const PALADIN_DEVO_ALLY_RANGE = 40.0f;
float const PALADIN_DEVO_DR = 0.90f; // 10% incoming damage reduction
float const PALADIN_THORNS_PCT = 0.5f;
// Report (Thorns +2000%): the Prot paladin's Thorns aura deals 20x the base
// spell damage, and the aura is re-cast on every party/raid member on a tick
// (same shape as the Druid Balance party tick -- observe only, never touch an
// aura from inside its own application hook).
float const PALADIN_THORNS_DMG_MULT = 20.0f;
uint32 const PALADIN_THORNS_PARTY_TICK_MS = 5000;
int32 const RADIUS_MOD_DOUBLE = 20000; // SetSpellValue(SPELLVALUE_RADIUS_MOD, x) -> RadiusMod = x/10000
uint32 const HUNTER_MM_AIMED_PROC_CHANCE = 20;
int32 const CHAIN_LIGHTNING_MAX_TARGETS = 99; // SetSpellValue(SPELLVALUE_MAX_TARGETS, x) removes the normal 3-target cap
uint32 const ARMY_GROUP_DESPAWN_MS = 60000;
uint32 const ARMY_HEALER_TICK_MS = 2000;
uint32 const ARMY_HEALER_HEAL_PCT = 15;
uint32 const ARMY_HEALER_HEAL_THRESHOLD_PCT = 90;
uint32 const BM_PACK_SIZE = 4;
uint32 const BM_PACK_DESPAWN_MS = 20000;
uint32 const HUNTER_TRAP_RADIUS_MOD = 20000; // SetSpellValue(SPELLVALUE_RADIUS_MOD, x) -> RadiusMod = x/10000
// Survival "The Trap Engineer" (910154) additions. Call of the Wilds is a new
// castable, so it exists in both spell_dbc (rev_living_gear_call_of_the_wilds)
// and the client DBC patch (tools/client-patch/build_patch.py) like every
// other 9101xx button.
uint32 const SPELL_TAUNT = 355;
// No-pet fallback body: Shardtooth Bear (acore_world.creature_template 7444).
uint32 const NPC_WILDS_FALLBACK_BEAR = 7444;
uint32 const WILDS_BEAR_COUNT = 2;
uint32 const WILDS_BEAR_DESPAWN_MS = 60000;
float const WILDS_BEAR_TAUNT_RANGE = 10.0f;
uint32 const WILDS_BEAR_THREAT_TICK_MS = 2000;
uint32 const SHRAPNEL_MAX_STACKS = 5;
uint32 const SHRAPNEL_LIFETIME_S = 8; // refreshed by every Explosive Shot tick
float const SHRAPNEL_BLAST_RANGE = 8.0f;
uint32 const TRAP_ZONE_REARM_MS = 10000;
uint32 const TRAP_ZONE_MAX_REARMS = 3;
uint32 const TRAP_ZONE_MAX_LIVE = 6;   // live re-arm zones per hunter
float const HUNTER_TRAP_RADIUS_MULT = 2.0f; // matches HUNTER_TRAP_RADIUS_MOD
uint32 const WARLOCK_AFFLICTION_SPREAD_TICK_MS = 1000;
float const WARLOCK_AFFLICTION_SPREAD_RANGE = 40.0f; // Feature #194: was 15.0f -- "affliction dots jump up to 40 yards" (the Haunt-carried DoTs)
// How far from the warlock we look for already-infected enemies to spread FROM.
// The hop itself is still SPREAD_RANGE; this only bounds the search, so the
// plague can creep well past the warlock's own casting range one hop at a time.
float const WARLOCK_AFFLICTION_CONTAGION_RANGE = 60.0f;
// One tick is one hop: carriers are snapshotted before anything is applied, so
// an enemy infected this second only starts infecting others on the next one.
// That is what makes it read as a spreading plague rather than an instant
// map-wide application, and it is also what bounds the cost.
uint32 const WARLOCK_AFFLICTION_MAX_SPREADS_PER_TICK = 60;
// Report #106: every Affliction DoT periodic tick deals increased damage.
// Report #122: bumped from x4 to 2000% (x21) -- "bump affliction dot damage
// up to 2000%", verbatim.
float const WARLOCK_AFFLICTION_DOT_MULT = 21.0f;
uint32 const DRUID_ECLIPSE_TICK_MS = 3000;
int32 const DRUID_ECLIPSE_REFRESH_DURATION = 15000;
uint32 const DRUID_REJUV_SPREAD_TICK_MS = 3000;
float const DRUID_REJUV_SPREAD_RANGE = 15.0f;
int32 const WILD_GROWTH_MAX_TARGETS = 10;
float const ALLY_SPREAD_INJURED_PCT = 95.0f;
float const PRIEST_GS_SPREAD_RANGE = 20.0f;
float const SHAMAN_RIPTIDE_SPREAD_RANGE = 15.0f;
uint32 const SHAMAN_RIPTIDE_SPREAD_COUNT = 2;
uint32 const PRIEST_GS_SPREAD_COUNT = 2;

// -------------------------------------------------------------------------
// State
// -------------------------------------------------------------------------
struct MageState
{
    uint32 fireTickAcc = 0;
    uint32 frostTickAcc = 0;
    uint32 blizzardTickAcc = 0;
};

struct WarriorFuryState
{
    uint32 stacks = 0;
    uint32 appliedPct = 0;
    uint32 lastHitMs = 0;
};

struct RogueState
{
    uint32 energyTickAcc = 0;
};

std::unordered_map<uint32, MageState> g_mage;
std::unordered_map<uint32, WarriorFuryState> g_fury;
std::unordered_map<uint32, RogueState> g_rogue;
std::unordered_map<uint32, uint32> g_classPerk;      // char guid -> selected class-perk spell id
std::unordered_set<uint32> g_classPerkLoaded;         // char guids already loaded from DB
std::unordered_set<uint32> g_reentryGuard;            // char guid -> currently applying a perk-triggered cast

// Unholy Death Knight "one-man raid group" (Army of the Dead redesign).
// char guid -> the 5 ghoul GUIDs currently alive for that owner, so the
// healer ghoul's AI can find its own group without a global creature scan.
std::unordered_map<uint32, std::vector<ObjectGuid>> g_armyGroup;

// Beast Mastery Hunter "call the pack" -- char guid -> the extra beast GUIDs
// cloned from the hunter's active pet. Same one-slot-per-owner dedupe shape
// as g_armyGroup.
std::unordered_map<uint32, std::vector<ObjectGuid>> g_bmPack;

// Survival Hunter "The Trap Engineer": persistent trap zones (a trap placement
// is remembered by position and re-cast in place on a 10s cadence while an
// enemy stands inside its radius), Shrapnel stack counters (per target, banked
// tick damage until the detonating hit), and the Call of the Wilds bears.
// All keyed by char guid; stored GUIDs are re-resolved at use time.
struct HunterTrapZone
{
    uint32 spellId = 0;
    uint32 mapId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    uint32 rearmAcc = 0;
    uint8 rearms = 0;
};

struct HunterShrapnel
{
    uint8 stacks = 0;
    int32 accDamage = 0;
    uint32 expireS = 0;
};

std::unordered_map<uint32, std::vector<HunterTrapZone>> g_trapZones;
std::unordered_map<uint32, std::unordered_map<uint32, HunterShrapnel>> g_shrapnel;
std::unordered_map<uint32, std::vector<ObjectGuid>> g_wildsBears;

// Warlock Demonology (910158) "The Imp Lord" -- char guid -> the imp-legion
// pack + whether Meta currently has it upgraded to felguards. Same
// one-slot-per-owner dedupe shape as g_bmPack.
// Spell ids verified in var/mmap-output/dbc/Spell.dbc (field col 110 =
// EffectMiscValue_1): spell 688 Summon Imp -> creature 416 "Imp",
// spell 30146 Summon Felguard -> creature 17252 "Felguard" (both rows
// confirmed in the DBC, creature names confirmed in acore_world).
// 18708 Fel Domination confirmed present in the same DBC; it is the
// toggle button. 47241 Metamorphosis is already SPELL_METAMORPHOSIS above.
uint32 const DEMO_IMP_ENTRY = 416;
uint32 const DEMO_FELGUARD_ENTRY = 17252;
uint32 const DEMO_LEGION_SIZE = 8;
uint32 const DEMO_LEGION_DESPAWN_MS = 30000;
uint32 const DEMO_LEGION_HP_PCT = 35;
uint32 const DEMO_IMP_FIREBOLT_COOLDOWN_MS = 2000;
struct WarlockDemoLegion
{
    std::vector<ObjectGuid> pack;
    bool metaForm = false;
};
std::unordered_map<uint32, WarlockDemoLegion> g_demoLegion;

// Enhancement Shaman (910155) "The Storm Caller".
// Spell ids verified: Stormstrike 17364 (single rank, present in
// var/mmap-output/dbc/Spell.dbc), Chain Lightning 421 -> 49271 and Lightning
// Bolt 403 -> 49238 (acore_world.spell_ranks), Feral Spirit 51533.
// Static Field's toggle vehicle is Thorns (467 -> 53307): an existing
// aura-bearing NATURE-school castable the shaman does not natively train,
// same vehicle trick as Starfall's toggle (Spell 48505) and Voidform's
// (Shadowform 15473). Granted through CLASS_PERK_GRANTS so spec-switch
// reclaims it. Gated on own-cast only (aura caster == the shaman), so a
// druid's Thorns buff can never silently switch the field on.
uint32 const SHAMAN_CHARGE_DURATION_MS = 8000;   // Stormstrike "Charged" window
float const SHAMAN_ECHO_PCT = 0.5f;              // echo bolt at 50% damage
float const SHAMAN_STATIC_FIELD_AP_PCT = 0.25f;  // tick damage = 25% of AP
uint32 const SHAMAN_STATIC_FIELD_TICK_MS = 2000; // shock cadence
float const SHAMAN_STATIC_FIELD_RANGE = 10.0f;   // storm-circle radius
uint32 const SHAMAN_WOLF_BOLT_CHANCE = 25;       // wolf strike -> free bolt
uint32 const SHAMAN_WOLF_BOLT_DELAY_MS = 1;      // deferred out of the damage hook
// char guid -> (target low guid -> "Charged" expiry, ms timer). Module-tracked
// instead of an aura because there is no existing 8s shaman debuff to reuse.
std::unordered_map<uint32, std::unordered_map<uint32, uint32>> g_shamanCharged;

// Shaman Elemental tick accumulator reused for a couple of other per-tick
// perks below (Warlock Affliction spread, Druid Eclipse refresh, Druid
// Rejuvenation spread) -- one small struct per class instead of yet more
// single-purpose maps.
struct TickState
{
    uint32 acc = 0;
};
std::unordered_map<uint32, TickState> g_afflictionTick;
// Bladestorm autocast accumulator, keyed by character guid. See
// TickWarriorArmsBladestorm (bug report #19).
std::unordered_map<uint32, uint32> g_bladestormTick;
// Avalanche's per-second tick accumulator, keyed like g_bladestormTick.
std::unordered_map<uint32, uint32> g_protAvalancheTick;
std::unordered_map<uint32, TickState> g_eclipseTick;
std::unordered_map<uint32, TickState> g_rejuvTick;
std::unordered_map<uint32, TickState> g_thornsTick;
std::unordered_map<uint32, TickState> g_insectTick;
// Unholy DK blight tick accumulator (Kit 7). Same TickState shape as above.
std::unordered_map<uint32, TickState> g_dkUnholyTick;

std::unordered_set<uint32> g_perkLoaded;              // account ids already loaded
std::unordered_map<uint32, std::unordered_set<uint32>> g_perks; // account id -> unlocked spell ids

// Shadow Priest "Void Choir" -- char guid -> void-tendril GUIDs, oldest first
// (despawn the front when the cap is hit). Same one-slot-per-owner shape as
// g_armyGroup / g_bmPack.
std::unordered_map<uint32, std::vector<ObjectGuid>> g_voidTendrils;
// Voidform free-Mind-Blast cadence accumulator (TickPriestShadowVoidform).
std::unordered_map<uint32, TickState> g_voidTick;
// Static Field damage accumulator, same shape as g_voidTick.
std::unordered_map<uint32, TickState> g_shamanStaticTick;

bool g_schemaReady = false;
bool g_hasClassPerkTable = false;
bool g_hasClassGrantTable = false;

void DetectSchema()
{
    if (g_schemaReady)
        return;
    g_schemaReady = true;
    if (QueryResult tables = CharacterDatabase.Query(
        "SELECT COUNT(*) FROM `information_schema`.`TABLES` "
        "WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = 'lg_char_class_perk'"))
        g_hasClassPerkTable = (*tables)[0].Get<uint64>() > 0;
    // Probed the same defensive way, so a missing migration degrades to
    // "grants are not remembered across a restart" instead of aborting.
    if (QueryResult tables = CharacterDatabase.Query(
        "SELECT COUNT(*) FROM `information_schema`.`TABLES` "
        "WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = 'lg_char_class_grant'"))
        g_hasClassGrantTable = (*tables)[0].Get<uint64>() > 0;
}

// -------------------------------------------------------------------------
// Local copies of the three shared helpers (exact behavior matched to
// LivingGear_Perks.cpp lines ~150-210, per this module's self-contained
// per-file convention -- see LivingGear_Next.cpp/_Gather.cpp for the same
// pattern).
// -------------------------------------------------------------------------
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

// 2026-08-21: this file's only caller (GrantAndBroadcastClassPerks, below)
// unlocks the three class-spec pick badges -- pure account-perk flags,
// never meant to be castable/spellbook-visible (unlike LivingGear_Perks.cpp's
// UnlockPerk, which also grants some genuinely castable perks and keeps
// learnSpell). Calling player->learnSpell() here marked them "known" with
// no SkillLineAbility entry to categorize them, so the client dumped them
// into the General spellbook tab regardless of CASTABLE_SPELLS excluding
// them -- exactly why "*Rogue: Subtlety" etc. kept showing up there even
// after that client-side fix. HasPerk()'s g_perks[acc]/DB fallback still
// works fine without ever calling learnSpell.
void UnlockPerk(Player* player, uint32 spellId, char const* msg)
{
    if (!player || !player->GetSession() || !sSpellMgr->GetSpellInfo(spellId))
        return;
    uint32 const acc = player->GetSession()->GetAccountId();
    LoadPerks(acc);
    // Bug #25 was exactly this: recorded against the account, never learned as
    // a spell, so HasPerk-by-HasSpell said no while HasPerk-by-account said
    // yes. Class perks are selected through the account set, but the class
    // spec badges are real spells and other files do ask HasSpell.
    bool const firstTime = g_perks[acc].insert(spellId).second;
    if (firstTime)
        CharacterDatabase.DirectExecute(
            "INSERT IGNORE INTO `lg_account_perk` (`account_id`, `spell_id`) VALUES ({}, {})",
            acc, spellId);
    // Castable perks only -- learning a badge spams chat and puts nothing in
    // the spellbook. See LivingGear_PerkIsCastable in LivingGear_Perks.cpp.
    if (LivingGear_PerkIsCastable(spellId) && !player->HasSpell(spellId))
        player->learnSpell(spellId);
    if (!firstTime)
    {
        // Already owned. If it was BOUGHT and a condition has now granted it
        // anyway, the points went on something that would have been free.
        LivingGear_RefundIfPurchased(player, spellId);
        return;
    }
    SendLine(player, Acore::StringFormat("PK|{}|1", spellId));
    if (msg)
        ChatHandler(player->GetSession()).SendSysMessage(msg);
}

// -------------------------------------------------------------------------
// Small local helpers shared by several perks below.
// -------------------------------------------------------------------------
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

// The rank a perk should actually cast.
//
// Report #38: "the automatic living bomb from the mage fire spec only casts
// rank 1, it should scale with level up to rank 3 at 80." Hardcoding the rank-1
// constant is the bug, and it was in five places.
//
// The rule, per the user: always use the highest rank the player has genuinely
// LEARNED. A perk that granted the spell hands over rank 1, and that is what
// gets cast until the player trains something better -- at which point the
// perk starts using it automatically. BestOwned answers exactly that; this
// just supplies the granted rank as the floor for the case where the perk gave
// it and nothing is trained yet.
// Clear a spell's cooldown AFTER the cast that triggered it has finished.
//
// Report #59: "Penance has a cooldown when it shouldn't according to the
// Discipline spec description." Six perks promise "no cooldown" and all six
// had the same bug -- they called RemoveSpellCooldown from OnPlayerSpellCast,
// which fires when the cast STARTS. AzerothCore applies the cooldown when the
// cast finishes, so every one of them was clearing a cooldown that did not
// exist yet and the real one landed a moment later.
//
// Deferring by a tick puts the removal after Spell::finish(). Same deferral
// this module already uses for every cast that must not run reentrantly, for
// a different reason but with the same mechanism.
void ClearCooldownAfterCast(Player* player, uint32 spellId, uint32 category)
{
    if (!player)
        return;
    ObjectGuid const guid = player->GetGUID();
    player->m_Events.AddEventAtOffset([guid, spellId, category]()
    {
        Player* p = ObjectAccessor::FindPlayer(guid);
        if (!p || !p->IsInWorld())
            return;
        p->RemoveSpellCooldown(spellId, true);
        if (category)
            p->RemoveCategoryCooldown(category);
    }, std::chrono::milliseconds(1));
}

uint32 BestOwnedOrFirst(Player* player, uint32 firstId)
{
    uint32 const owned = BestOwned(player, firstId);
    return owned ? owned : firstId;
}

bool RankOf(SpellInfo const* info, uint32 firstId)
{
    if (!info)
        return false;
    uint32 const first = sSpellMgr->GetFirstSpellInChain(firstId);
    return first && sSpellMgr->GetFirstSpellInChain(info->Id) == first;
}

bool HasAuraRankOf(Unit* unit, uint32 firstId)
{
    if (!unit)
        return false;
    for (SpellInfo const* info = sSpellMgr->GetSpellInfo(firstId); info; info = info->GetNextRankSpell())
        if (unit->HasAura(info->Id))
            return true;
    return false;
}

// Can this unit safely RECEIVE an aura from us right now?
//
// LivingGear_SafeToCastOn answers that for the caster. Nothing answered it for
// the target, and that is the hole the fourth _AddAura crash went through:
// Muckfuppet asserted on !m_cleanupDone while logging out, and the caster was
// somebody else entirely. Player::CleanupsBeforeDelete sets m_cleanupDone at
// WorldSession.cpp:873, but RemoveFromWorld does not run until the next line,
// so in that window the target still reports IsInWorld() and IsAlive() and is
// a perfectly plausible thing to buff. Only the session's PlayerLogout() flag
// tells the truth, and none of these iterators were asking.
//
// The spread perks make this easy to hit: TickDruidRejuvSpread sweeps 60 yards
// for injured allies, so any bot resto druid standing in Stormwind can reach
// a player who is halfway through logging out.
//
// Checked here, in the two iterators every perk goes through, rather than at
// each of the dozen call sites -- same reasoning as the single addon-command
// dispatcher.
inline bool SafeAuraTarget(Unit* unit)
{
    if (!unit || !unit->IsInWorld() || !unit->IsAlive())
        return false;
    if (unit->IsDuringRemoveFromWorld())
        return false;
    if (Player* target = unit->ToPlayer())
        return LivingGear_SafeToCastOn(target);
    return true;
}

template <typename Fn>
void ForEachHostileNear(Player* player, WorldObject* center, float range, Fn&& fn)
{
    if (!player || !center)
        return;
    std::list<Unit*> targets;
    Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck check(center, player, range);
    Acore::UnitListSearcher<Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck> searcher(player, targets, check);
    Cell::VisitObjects(center, searcher, range);
    for (Unit* target : targets)
        if (SafeAuraTarget(target))
            fn(target);
}

template <typename Fn>
void ForEachHostileInRange(Player* player, float range, Fn&& fn)
{
    ForEachHostileNear(player, player, range, fn);
}

// Friendly, alive, within range, and below ALLY_SPREAD_INJURED_PCT health --
// shared by every "spreads to nearby injured allies" perk below (Riptide,
// Guardian Spirit, Rejuvenation).
template <typename Fn>
void ForEachInjuredAllyNear(Player* player, WorldObject* center, float range, Fn&& fn)
{
    if (!player || !center)
        return;
    std::list<Unit*> targets;
    Acore::AnyFriendlyNotSelfUnitInObjectRangeCheck check(center, player, range);
    Acore::UnitListSearcher<Acore::AnyFriendlyNotSelfUnitInObjectRangeCheck> searcher(player, targets, check);
    Cell::VisitObjects(center, searcher, range);
    for (Unit* target : targets)
        if (SafeAuraTarget(target) && target->GetHealthPct() < ALLY_SPREAD_INJURED_PCT)
            fn(target);
}

// -------------------------------------------------------------------------
// Class-perk selection bookkeeping (the "pick one spec" system that was
// missing entirely -- see LivingGear.cpp.backup-20260818 lines ~1171-1334
// for the pre-split intent this reimplements). Selection is per-character
// (a class-perk pick can differ per alt), stored in lg_char_class_perk.
//
// Note: this table only governs the four specs implemented in this file
// (Mage x3, Warrior x3, Paladin Protection, Rogue Combat). Paladin
// Holy/Retribution and Rogue Assassination/Subtlety are gated purely by
// HasPerk() in their own files and are intentionally left untouched, so a
// character could in principle have e.g. Rogue Assassination active
// (via HasPerk there) at the same time as Rogue Combat selected here.
// That's a known limitation of not editing the other files; see report.
// -------------------------------------------------------------------------
bool IsMageClassPerk(uint32 spellId)
{
    for (uint32 id : MAGE_CLASS_PERKS)
        if (id == spellId)
            return true;
    return false;
}

bool IsRogueClassPerk(uint32 spellId)
{
    for (uint32 id : ROGUE_CLASS_PERKS)
        if (id == spellId)
            return true;
    return false;
}

bool IsPaladinClassPerk(uint32 spellId)
{
    for (uint32 id : PALADIN_CLASS_PERKS)
        if (id == spellId)
            return true;
    return false;
}

bool IsWarriorClassPerk(uint32 spellId)
{
    for (uint32 id : WARRIOR_CLASS_PERKS)
        if (id == spellId)
            return true;
    return false;
}

bool IsHunterClassPerk(uint32 spellId)
{
    for (uint32 id : HUNTER_CLASS_PERKS)
        if (id == spellId)
            return true;
    return false;
}

bool IsShamanClassPerk(uint32 spellId)
{
    for (uint32 id : SHAMAN_CLASS_PERKS)
        if (id == spellId)
            return true;
    return false;
}

bool IsDkClassPerk(uint32 spellId)
{
    for (uint32 id : DK_CLASS_PERKS)
        if (id == spellId)
            return true;
    return false;
}

bool IsWarlockClassPerk(uint32 spellId)
{
    for (uint32 id : WARLOCK_CLASS_PERKS)
        if (id == spellId)
            return true;
    return false;
}

bool IsDruidClassPerk(uint32 spellId)
{
    for (uint32 id : DRUID_CLASS_PERKS)
        if (id == spellId)
            return true;
    return false;
}

bool IsPriestClassPerk(uint32 spellId)
{
    for (uint32 id : PRIEST_CLASS_PERKS)
        if (id == spellId)
            return true;
    return false;
}

void LoadClassPerk(uint32 guid)
{
    if (!g_classPerkLoaded.insert(guid).second)
        return;
    DetectSchema();
    if (!g_hasClassPerkTable)
        return;
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT `spell_id` FROM `lg_char_class_perk` WHERE `guid` = {}", guid))
        g_classPerk[guid] = (*result)[0].Get<uint32>();
}

uint32 GetClassPerk(Player* player)
{
    if (!player)
        return 0;
    uint32 const guid = player->GetGUID().GetCounter();
    LoadClassPerk(guid);
    auto it = g_classPerk.find(guid);
    return it != g_classPerk.end() ? it->second : 0;
}

uint32 const* GrantsFor(uint32 perk)
{
    for (ClassPerkGrant const& g : CLASS_PERK_GRANTS)
        if (g.perk == perk)
            return g.spells;
    return nullptr;
}

// Highest rank of a chain the character qualifies for, or rank 1 if they
// qualify for nothing yet -- report #36 wants these usable while levelling, so
// a level 1 character still gets something castable rather than nothing.
//
// Walking the chain is also the fix for the shape of report #38 (Living Bomb
// stuck at rank 1 because the id was hardcoded).
uint32 BestRankForLevel(uint32 firstRank, uint8 level)
{
    uint32 best = firstRank;
    uint32 id = firstRank;
    while (id)
    {
        SpellInfo const* info = sSpellMgr->GetSpellInfo(id);
        if (!info)
            break;
        if (info->SpellLevel && info->SpellLevel > level)
            break;
        best = id;
        id = sSpellMgr->GetNextSpellInChain(id);
    }
    return best;
}

// Spells this character was given BY a class perk, so switching specs can take
// back exactly those and nothing else.
//
// Report #37: "Crusader Strike was taught on login, but does not go away when
// switching to another class perk". The naive fix -- remove everything the old
// spec grants -- is wrong, because some of those are trainable anyway. A
// paladin can train Consecration; removing it because they tried Holy and moved
// on would take something they earned. So we only ever hand back what we
// handed out.
std::unordered_map<uint32, std::unordered_set<uint32>> g_classGrantLog;

void LoadClassGrants(uint32 guid)
{
    if (!g_hasClassGrantTable || g_classGrantLog.count(guid))
        return;
    auto& set = g_classGrantLog[guid];
    if (QueryResult r = CharacterDatabase.Query(
        "SELECT `spell_id` FROM `lg_char_class_grant` WHERE `guid` = {}", guid))
        do { set.insert((*r)[0].Get<uint32>()); } while (r->NextRow());
}

void NoteGranted(Player* player, uint32 spellId)
{
    uint32 const guid = player->GetGUID().GetCounter();
    LoadClassGrants(guid);
    if (!g_classGrantLog[guid].insert(spellId).second || !g_hasClassGrantTable)
        return;
    CharacterDatabase.DirectExecute(
        "INSERT IGNORE INTO `lg_char_class_grant` (`guid`, `spell_id`) VALUES ({}, {})",
        guid, spellId);
}

void ForgetGranted(Player* player, uint32 spellId)
{
    uint32 const guid = player->GetGUID().GetCounter();
    g_classGrantLog[guid].erase(spellId);
    if (g_hasClassGrantTable)
        CharacterDatabase.DirectExecute(
            "DELETE FROM `lg_char_class_grant` WHERE `guid` = {} AND `spell_id` = {}",
            guid, spellId);
}

// Hand over the new spec's abilities and take back the old spec's, skipping
// anything both share so a switch never flickers a spell the player keeps.
void ApplyClassPerkSpells(Player* player, uint32 prevPerk, uint32 newPerk)
{
    if (!player || !player->GetSession())
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    LoadClassGrants(guid);
    uint8 const level = player->GetLevel();

    uint32 const* incoming = GrantsFor(newPerk);
    std::unordered_set<uint32> keep;
    if (incoming)
        for (uint8 i = 0; i < MAX_CLASS_PERK_GRANT_SPELLS && incoming[i]; ++i)
            keep.insert(BestRankForLevel(incoming[i], level));

    if (uint32 const* outgoing = GrantsFor(prevPerk))
    {
        for (uint8 i = 0; i < MAX_CLASS_PERK_GRANT_SPELLS && outgoing[i]; ++i)
        {
            // Every rank, because the character may have levelled since.
            for (uint32 id = outgoing[i]; id; id = sSpellMgr->GetNextSpellInChain(id))
            {
                if (keep.count(id) || !g_classGrantLog[guid].count(id))
                    continue;
                if (player->HasSpell(id))
                    player->removeSpell(id, SPEC_MASK_ALL, false);
                ForgetGranted(player, id);
            }
        }
    }

    for (uint32 id : keep)
    {
        if (!sSpellMgr->GetSpellInfo(id))
            continue;
        if (!player->HasSpell(id))
            player->learnSpell(id);
        NoteGranted(player, id);
    }
}

bool CanSelectClassPerk(Player* player, uint32 spellId)
{
    if (!player || !HasPerk(player, spellId))
        return false;
    switch (player->getClass())
    {
        case CLASS_MAGE:
            return IsMageClassPerk(spellId);
        case CLASS_ROGUE:
            return IsRogueClassPerk(spellId);
        case CLASS_PALADIN:
            return IsPaladinClassPerk(spellId);
        case CLASS_WARRIOR:
            return IsWarriorClassPerk(spellId);
        case CLASS_HUNTER:
            return IsHunterClassPerk(spellId);
        case CLASS_SHAMAN:
            return IsShamanClassPerk(spellId);
        case CLASS_DEATH_KNIGHT:
            return IsDkClassPerk(spellId);
        case CLASS_WARLOCK:
            return IsWarlockClassPerk(spellId);
        case CLASS_DRUID:
            return IsDruidClassPerk(spellId);
        case CLASS_PRIEST:
            return IsPriestClassPerk(spellId);
        default:
            return false;
    }
}

// Grant the 3 spec spells for the player's class (so there is something to
// pick from) and tell the client which ones exist + which is selected.
// Nothing anywhere -- not even the pre-existing Paladin/Rogue Subtlety
// specs owned by other files -- ever did this, which is why the Class tab
// always showed "No class perk for your class yet." regardless of spec.
uint32 const* ClassPerkListFor(uint8 cls, uint32& count)
{
    switch (cls)
    {
        case CLASS_MAGE:    count = 3; return MAGE_CLASS_PERKS;
        case CLASS_ROGUE:   count = 3; return ROGUE_CLASS_PERKS;
        case CLASS_PALADIN: count = 3; return PALADIN_CLASS_PERKS;
        case CLASS_WARRIOR: count = 3; return WARRIOR_CLASS_PERKS;
        case CLASS_HUNTER:  count = 3; return HUNTER_CLASS_PERKS;
        case CLASS_SHAMAN:  count = 3; return SHAMAN_CLASS_PERKS;
        case CLASS_DEATH_KNIGHT: count = 3; return DK_CLASS_PERKS;
        case CLASS_WARLOCK: count = 3; return WARLOCK_CLASS_PERKS;
        case CLASS_DRUID:   count = 3; return DRUID_CLASS_PERKS;
        case CLASS_PRIEST:  count = 3; return PRIEST_CLASS_PERKS;
        default:            count = 0; return nullptr;
    }
}

void GrantAndBroadcastClassPerks(Player* player)
{
    if (!player || !player->GetSession())
        return;
    uint32 count = 0;
    uint32 const* list = ClassPerkListFor(player->getClass(), count);
    if (!list)
        return;
    uint32 const selected = GetClassPerk(player);
    std::string pairs;
    for (uint32 i = 0; i < count; ++i)
    {
        UnlockPerk(player, list[i], nullptr);
        if (!pairs.empty())
            pairs += ',';
        pairs += Acore::StringFormat("{}:{}", list[i], list[i] == selected ? 1 : 0);
    }
    SendLine(player, "CPKALL|" + pairs);
}

void ApplyRogueCombatBladeFlurry(Player* player); // defined below, near TickRogueCombat

void SelectClassPerk(Player* player, uint32 spellId)
{
    if (!player || !player->GetSession())
        return;
    if (!CanSelectClassPerk(player, spellId))
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    uint32 const prev = GetClassPerk(player);
    if (prev == spellId)
        return;
    g_classPerk[guid] = spellId;
    DetectSchema();
    if (g_hasClassPerkTable)
        CharacterDatabase.DirectExecute(
            "REPLACE INTO `lg_char_class_perk` (`guid`, `spell_id`) VALUES ({}, {})",
            guid, spellId);
    // CRASH FIX 2026-08-22. A class perk is selected by CASTING its spell, so
    // this whole function runs from inside OnPlayerSpellCast -- part-way
    // through the triggering Spell::cast(), before its effects have finished
    // applying. Everything below adds auras or teaches spells on that same
    // player, which is the reentrant path into Unit::_AddAura this module has
    // been bitten by repeatedly.
    //
    // It bit again: "ASSERTION FAILED ... Function: _AddAura ... Condition:
    // !m_cleanupDone" on a Paladin switching spec, which took the realm down.
    // Every other cast in this module is already deferred for exactly this
    // reason; this path was the one that never was.
    //
    // The DB write and the in-memory selection above stay synchronous -- they
    // touch no spell state, and the player should not be able to double-select
    // in the gap.
    ObjectGuid const playerGuid = player->GetGUID();
    player->m_Events.AddEventAtOffset([playerGuid, prev, spellId]()
    {
        Player* p = ObjectAccessor::FindPlayer(playerGuid);
        if (!p || !p->IsInWorld() || !p->GetSession())
            return;
        if (prev == SPELL_ROGUE_COMBAT && spellId != SPELL_ROGUE_COMBAT)
            p->RemoveAurasDueToSpell(SPELL_BLADE_FLURRY);
        if (spellId == SPELL_ROGUE_COMBAT)
            ApplyRogueCombatBladeFlurry(p);
        // GrantMageFrostBlizzard is deliberately NOT called here any more --
        // the grant table above covers Blizzard and, unlike this, records it
        // so switching away can revoke it. See report #54.
        if (spellId == SPELL_ROGUE_SUBTLETY)
            LivingGear_GrantSubtletyPerks(p);
        // Reports #33, #36, #37, #39, #40. Everything above this line is the
        // old ad-hoc approach -- three specs out of thirty, hand-written. This
        // covers all of them from one table, and takes the previous spec's
        // spells back.
        ApplyClassPerkSpells(p, prev, spellId);
        GrantAndBroadcastClassPerks(p); // sends CPKALL, which the client actually handles
    }, std::chrono::milliseconds(1));
    if (SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId))
        ChatHandler(player->GetSession()).PSendSysMessage(
            "|cff66ccff[Account Perks]|r {} is now your active class perk.", info->SpellName[0]);
}

// -------------------------------------------------------------------------
// Mage: Arcane (910032)
// "While in combat, Mirror Images appear and chain-cast. They linger 60
// sec after combat."
//
// Chain-casting/attacking is the engine's own Mirror Image AI (already
// implemented server-side for the stock spell); this only keeps the
// images up during combat and lets the aura run out naturally 60s after
// combat ends.
// -------------------------------------------------------------------------
// Arcane's damage buff and its button.
//
// The old perk was "in combat, Mirror Images appear and chain-cast" -- one
// more thing that happens on its own while the player watches. Arcane Power
// is a free permanent toggle instead, the same shape as Arms' Bladestorm and
// Feral's Berserk, both of which already work here: cast it and it stays,
// recast to drop it. All Arcane damage is multiplied while the mage is the
// one dealing it.
void ApplyMageArcaneDamage(Unit* attacker, int32& damage, SpellInfo const* info)
{
    if (!attacker || damage <= 0 || !info)
        return;
    Player* player = attacker->ToPlayer();
    if (!player || GetClassPerk(player) != SPELL_MAGE_ARCANE)
        return;
    if (!(info->GetSchoolMask() & SPELL_SCHOOL_MASK_ARCANE))
        return;
    if (!player->HasAura(SPELL_ARCANE_POWER))
        return;
    damage *= int32(MAGE_DAMAGE_MULT);
}

void TryMageArcaneOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_MAGE_ARCANE)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || info->Id != SPELL_ARCANE_POWER)
        return;
    if (int32 const cost = spell->GetPowerCost())
        player->ModifyPower(POWER_MANA, cost);
    ClearCooldownAfterCast(player, SPELL_ARCANE_POWER, info->GetCategory());
}

// Observed from the update loop rather than by touching the aura's lifetime
// during its own application -- the same reason TickWarriorArmsBladestorm
// works that way.
void TickMageArcanePower(Player* player)
{
    if (!player || GetClassPerk(player) != SPELL_MAGE_ARCANE || !player->IsAlive())
        return;
    if (Aura* aura = player->GetAura(SPELL_ARCANE_POWER))
        if (aura->GetDuration() >= 0 && aura->GetDuration() < 5000)
            aura->SetDuration(aura->GetMaxDuration() > 0 ? aura->GetMaxDuration() : 15000);
}

void TickMageArcane(Player* player)
{
    if (!player || GetClassPerk(player) != SPELL_MAGE_ARCANE || !player->IsAlive() || !player->IsInCombat())
        return;
    if (!player->HasAura(SPELL_MIRROR_IMAGE))
    {
        player->CastSpell(player, SPELL_MIRROR_IMAGE, true);
        return;
    }
    if (Aura* aura = player->GetAura(SPELL_MIRROR_IMAGE))
        if (aura->GetDuration() >= 0 && aura->GetDuration() < 10000)
            aura->SetDuration(30000);
}

void OnLeaveCombatMage(Player* player)
{
    if (!player || GetClassPerk(player) != SPELL_MAGE_ARCANE)
        return;
    if (Aura* aura = player->GetAura(SPELL_MIRROR_IMAGE))
        if (aura->GetDuration() < int32(MAGE_ARCANE_LINGER_MS))
            aura->SetDuration(int32(MAGE_ARCANE_LINGER_MS));
}

// -------------------------------------------------------------------------
// Mage: Fire (910033)
// "Fire spells apply Living Bomb. That effect spreads to enemies within
// 15 yards every 1 sec."
// -------------------------------------------------------------------------
bool IsHarmfulMageFireSpell(SpellInfo const* info)
{
    if (!info || info->SpellFamilyName != SPELLFAMILY_MAGE)
        return false;
    if (!(info->GetSchoolMask() & SPELL_SCHOOL_MASK_FIRE))
        return false;
    switch (info->Id)
    {
        case SPELL_LIVING_BOMB:
        case 44461: // Living Bomb explosion
        case 55359: // Living Bomb explosion (rank)
        case 55360:
        case 55361:
        case 55362:
        case SPELL_MAGE_COMBUSTION:
        case 28682: // Combustion crit-stack aura
            return false;
        default:
            break;
    }
    return true;
}

void ApplyMageCombustion(Player* player)
{
    if (!player->HasAura(SPELL_MAGE_COMBUSTION))
        player->CastSpell(player, SPELL_MAGE_COMBUSTION, true);
}

// Chain-aware "carries MY Living Bomb" check. The perk casts the player's
// best owned rank (BestOwnedOrFirst -> 55360 for a rank-3 mage), but the old
// checks tested HasAura(SPELL_LIVING_BOMB) -- the rank-1 chain head id only.
// AzerothCore auras are keyed by exact spell id, so every rank-3 mage read as
// "0 carriers" and the spread/detonate pipeline silently no-op'd (report #183:
// "living bomb still does not spread for swayss"; the log showed applies
// succeeding with result 255 = SPELL_CAST_OK while the spread tick counted
// zero carriers). Walk the rank chain and match the caster guid.
bool HasLivingBombFrom(Unit* target, Player* caster)
{
    if (!target || !caster)
        return false;
    for (uint32 id = SPELL_LIVING_BOMB; id; id = sSpellMgr->GetNextSpellInChain(id))
        if (target->HasAura(id, caster->GetGUID()))
            return true;
    return false;
}

void TryMageFireOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_MAGE_FIRE)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!IsHarmfulMageFireSpell(info))
        return;
    Unit* target = spell->m_targets.GetUnitTarget();
    if (!target || target == player || !player->IsValidAttackTarget(target))
        return;
    if (!HasLivingBombFrom(target, player))
    {
        // Input-side counterpart of the spread counter: if carriers are always
        // 0 in the spread tick, this is the half of the pipeline that failed.
        uint32 const lb = BestOwnedOrFirst(player, SPELL_LIVING_BOMB);
        SpellCastResult const res = player->CastSpell(target, lb, true);
        LOG_INFO("module.livinggear",
            "living bomb apply: spell {} on target {} result {}",
            lb, target->GetName(), uint32(res));
    }
    ApplyMageCombustion(player);
}

// Is this Living Bomb, in any of its forms -- the DoT, or one of the five
// per-rank explosions?
bool IsLivingBombDamage(SpellInfo const* info)
{
    if (!info)
        return false;
    if (RankOf(info, SPELL_LIVING_BOMB))
        return true;
    for (uint32 id : SPELL_LIVING_BOMB_BLASTS)
        if (info->Id == id)
            return true;
    return false;
}

// Fire's damage buff. Spreading Living Bomb to eight enemies was spreading
// noise: base Living Bomb at 80 is small, and unlike Assassination (+300%
// poisons), DK Frost (x2) or Priest Shadow (x4 Mind Flay), Fire carried a
// multiplier of exactly one.
void ApplyMageFireDamage(Unit* attacker, int32& damage, SpellInfo const* info)
{
    if (!attacker || damage <= 0 || !IsLivingBombDamage(info))
        return;
    Player* player = attacker->ToPlayer();
    if (!player || GetClassPerk(player) != SPELL_MAGE_FIRE)
        return;
    damage *= int32(MAGE_DAMAGE_MULT > MAGE_LIVING_BOMB_DMG_MULT ? MAGE_DAMAGE_MULT : MAGE_LIVING_BOMB_DMG_MULT);
}

void ApplyMageFirePeriodic(Unit* attacker, uint32& damage, SpellInfo const* info)
{
    if (!attacker || !damage || !IsLivingBombDamage(info))
        return;
    Player* player = attacker->ToPlayer();
    if (!player || GetClassPerk(player) != SPELL_MAGE_FIRE)
        return;
    damage *= MAGE_DAMAGE_MULT > MAGE_LIVING_BOMB_DMG_MULT ? MAGE_DAMAGE_MULT : MAGE_LIVING_BOMB_DMG_MULT;
}

// Fire Blast becomes a detonator.
//
// The spread on its own is entirely passive -- a 1 sec timer copying Living
// Bomb onto everything nearby while the player does nothing. Compare
// Subtlety, which hands the player a button with a dramatic payoff.
// Detonating is that button, and it reuses one Fire already has.
//
// Every Living Bomb the player owns within range is expired on the spot,
// which is what makes it explode (spell_mage_living_bomb only fires its
// blast for AURA_REMOVE_BY_EXPIRE or _BY_ENEMY_SPELL), and each detonated
// target re-seeds Living Bomb around itself -- so a big pull chain-reacts
// outward instead of ticking politely.
void TryMageFireDetonate(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_MAGE_FIRE)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || !RankOf(info, SPELL_FIRE_BLAST_R1))
        return;

    uint32 const guid = player->GetGUID().GetCounter();
    if (!g_reentryGuard.insert(guid).second)
        return;

    // Collect first. Removing the aura casts the explosion, which can reach
    // straight back into this hook and into the hostile iterator.
    std::vector<ObjectGuid> bombed;
    ForEachHostileInRange(player, CLASS_PERK_RANGE, [player, &bombed](Unit* target)
    {
        if (HasLivingBombFrom(target, player))
            bombed.push_back(target->GetGUID());
    });

    for (ObjectGuid targetGuid : bombed)
    {
        Unit* target = ObjectAccessor::GetUnit(*player, targetGuid);
        if (!target || !target->IsAlive())
            continue;
        // RemoveAura by exact id can miss a rank-3 bomb; walk the chain.
        for (uint32 id = SPELL_LIVING_BOMB; id; id = sSpellMgr->GetNextSpellInChain(id))
            if (target->HasAura(id, player->GetGUID()))
            {
                target->RemoveAura(id, player->GetGUID(), 0, AURA_REMOVE_BY_EXPIRE);
                break;
            }
        // Re-seed around the corpse-to-be so the reaction travels.
        ForEachHostileNear(player, target, CLASS_PERK_RANGE, [player](Unit* next)
        {
            if (!HasLivingBombFrom(next, player))
                player->CastSpell(next, BestOwnedOrFirst(player, SPELL_LIVING_BOMB), true);
        });
    }

    g_reentryGuard.erase(guid);
}

void TickMageFire(Player* player, MageState& st, uint32 diff)
{
    if (!player || GetClassPerk(player) != SPELL_MAGE_FIRE || !player->IsAlive() || !player->IsInCombat())
        return;
    st.fireTickAcc += diff;
    if (st.fireTickAcc < MAGE_FIRE_TICK_MS)
        return;
    st.fireTickAcc = 0;
    // Report #128: "Living Bomb only spreads from me directly to things within
    // range, not from the bomb itself." The old loop searched around the PLAYER
    // and cast on every clean enemy in that radius, so the bomb never hopped
    // enemy-to-enemy beyond the mage's own reach. Mirror TickWarlockAffliction's
    // carrier-based contagion: collect every enemy carrying MY Living Bomb
    // (carriers, searched out to MAGE_FIRE_CONTAGION_RANGE so the chain can
    // creep well past casting range one hop at a time), then apply Living Bomb
    // to every clean enemy within MAGE_FIRE_SPREAD_RANGE of any carrier. The
    // mage is only the cast source, never the search center. Detonation (the
    // Fire Blast hook above) is untouched, and carriers are snapshotted before
    // anything is applied so one tick is one hop.
    uint32 const guid = player->GetGUID().GetCounter();
    if (!g_reentryGuard.insert(guid).second)
        return;

    // Report #217: one hop per second made a big pull crawl outward one ring
    // at a time. Keep sweeping within the same tick -- every target infected
    // this pass joins the carrier set for the next pass, so the reaction
    // chain-reacts to its natural edge instead of waiting for the next tick.
    uint32 spread = 0, failed = 0;
    SpellCastResult lastResult = SPELL_CAST_OK;
    for (uint32 hop = 0; hop < 16; ++hop)
    {
        std::vector<Unit*> carriers;
        std::vector<Unit*> clean;
        ForEachHostileInRange(player, MAGE_FIRE_CONTAGION_RANGE, [player, &carriers, &clean](Unit* target)
        {
            if (HasLivingBombFrom(target, player))
                carriers.push_back(target);
            else
                clean.push_back(target);
        });

        uint32 thisHop = 0;
        for (Unit* target : clean)
        {
            Unit* nearCarrier = nullptr;
            for (Unit* carrier : carriers)
                if (target->IsWithinDist(carrier, MAGE_FIRE_SPREAD_RANGE))
                {
                    nearCarrier = carrier;
                    break;
                }

            if (!nearCarrier)
                continue;

            // Report #52 instrumentation kept: count cast results per tick instead
            // of trusting the silent CastSpell. BestOwnedOrFirst guarantees a spell
            // ID, so a failure is a Spell::cast rejection (range, facing, target
            // invalid) that would otherwise be swallowed.
            // Report #156 parity with the affliction spread: the new infection is
            // cast BY the carrier so it visibly jumps bomb to bomb, while
            // originalCaster stays the mage so HasLivingBombFrom (caster-guid
            // keyed) and the explosion damage keep keying off the player.
            SpellCastResult const res =
                nearCarrier->CastSpell(target, BestOwnedOrFirst(player, SPELL_LIVING_BOMB), true,
                    nullptr, nullptr, player->GetGUID());
            if (res == SPELL_CAST_OK)
            {
                ++spread;
                ++thisHop;
            }
            else
            {
                ++failed;
                lastResult = res;
            }
        }

        if (!thisHop)
            break;
    }

    // Reports #145/#146 (Bonesaw #147/#148): this counter was LOG_DEBUG on
    // module.livinggear and live Logger.module=4 never surfaced it, so both
    // retests were debugged blind. Log at INFO on EVERY ticked cycle, not just
    // cycles that cast -- a zero-carrier tick is the input-side signature
    // (auto-apply never landed) and must be visible, not folded into silence.
    LOG_INFO("module.livinggear",
        "living bomb spread: {} cast, {} failed (last result {})",
        spread, failed, uint32(lastResult));
    g_reentryGuard.erase(guid);
}

// -------------------------------------------------------------------------
// Mage: Frost (910034)
// "Blizzard is instant, no cooldown, and lingers like Death and Decay. In
// combat, Ice Lance hits enemies within 15 yards every 2 sec."
//
// Instant cast + no cooldown for every Blizzard rank is already applied
// globally by build_patch.py's Spell.dbc patch (BLIZZARD_RANKS), so this
// only implements the "lingers" and "Ice Lance cleave" parts of the perk.
// -------------------------------------------------------------------------
bool IsBlizzardRank(uint32 spellId)
{
    for (uint32 id : SPELL_BLIZZARD_RANKS)
        if (id == spellId)
            return true;
    return false;
}

// GrantMageFrostBlizzard and BlizzardRankForLevel used to live here. They were
// the legacy grant path from report #54 -- they handed Blizzard over WITHOUT
// recording it in lg_char_class_grant, so switching spec could never take it
// back. CLASS_PERK_GRANTS replaced them and both had already lost every call
// site; deleted so nobody wires a second, unrecorded grant path back up.
// BlizzardRankForLevel was also a hardcoded rank table (invariant 3);
// BestRankForLevel walks spell_ranks properly and is what the table uses.

void TryMageFrostBlizzardLinger(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_MAGE_FROST)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || !IsBlizzardRank(info->Id))
        return;
    if (DynamicObject* dyn = player->GetDynObject(info->Id))
        if (dyn->GetDuration() < 8000)
            dyn->SetDuration(8000);
}

void TickMageFrostBlizzard(Player* player)
{
    if (!player || GetClassPerk(player) != SPELL_MAGE_FROST || !player->IsAlive() || !player->IsInCombat())
        return;
    for (uint32 id : SPELL_BLIZZARD_RANKS)
        if (DynamicObject* dyn = player->GetDynObject(id))
            if (dyn->GetDuration() < 4000)
                dyn->SetDuration(8000);
}

// Blizzard's damage, driven by us rather than by the spell's own channel.
//
// Swayss reported that Blizzard places its circle and then does nothing.
// That is a direct consequence of how it was made to linger: Blizzard is a
// channeled persistent area aura, and PatchBlizzardServerSide clears
// SPELL_ATTR1_IS_CHANNELED so it behaves like Death and Decay. The engine
// only ever syncs a persistent-area-aura's lifetime to the channel inside
// `if (m_spellInfo->IsChanneled() && ...)` (Spell.cpp, handle_immediate),
// so once that flag is gone the ticking depends on machinery we deliberately
// switched off.
//
// Rather than keep fighting that, the perk does the damage itself: while a
// Blizzard dynamic object of ours is on the ground, every second, everything
// inside its radius takes the rank-appropriate Blizzard damage. That is what
// "lingers like Death and Decay" meant in the first place, it cannot be
// broken again by the channel flag, and it gets Frost the damage multiplier
// the other specs already had.
//
// The damage spell is read from the rank's own effect rather than hardcoded,
// so every rank hits for its correct amount.
void TickMageFrostBlizzardDamage(Player* player, MageState& st, uint32 diff)
{
    if (!player || GetClassPerk(player) != SPELL_MAGE_FROST || !player->IsAlive())
        return;
    st.blizzardTickAcc += diff;
    if (st.blizzardTickAcc < MAGE_BLIZZARD_TICK_MS)
        return;
    st.blizzardTickAcc = 0;

    uint32 const guid = player->GetGUID().GetCounter();
    if (!g_reentryGuard.insert(guid).second)
        return;

    uint32 found = 0, hit = 0;
    for (uint32 id : SPELL_BLIZZARD_RANKS)
    {
        DynamicObject* dyn = player->GetDynObject(id);
        if (!dyn)
            continue;
        ++found;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(id);
        if (!info)
            continue;
        uint32 tick = 0;
        for (uint8 i = EFFECT_0; i < MAX_SPELL_EFFECTS && !tick; ++i)
            tick = info->Effects[i].TriggerSpell;
        if (!tick)
            continue;
        float radius = dyn->GetRadius();
        if (radius <= 0.0f)
            radius = CLASS_PERK_RANGE;
        ForEachHostileNear(player, dyn, radius, [player, tick, &hit](Unit* target)
        {
            player->CastSpell(target, tick, true);
            ++hit;
        });
    }
    // Report #53: "Blizzard does no damage", still, after 0.1.62 supposedly
    // fixed it. Rather than assume which half is wrong, say whether we found a
    // Blizzard on the ground at all and whether anything was in it.
    if (found)
        LOG_INFO("module.livinggear", "blizzard tick: {} object(s) on the ground, {} target(s) hit",
            found, hit);

    g_reentryGuard.erase(guid);
}

// Frost's damage buff, applied to the Blizzard ticks above and to the
// Ice Lance volley below.
void ApplyMageFrostDamage(Unit* attacker, int32& damage, SpellInfo const* info)
{
    if (!attacker || damage <= 0 || !info)
        return;
    Player* player = attacker->ToPlayer();
    if (!player || GetClassPerk(player) != SPELL_MAGE_FROST)
        return;
    if (!(info->GetSchoolMask() & SPELL_SCHOOL_MASK_FROST))
        return;
    damage *= int32(MAGE_DAMAGE_MULT);
}

// Was a hand-written level table (78 -> r3, 72 -> r2, else r1), which is
// invariant 3's "never hardcode a spell rank" and the exact shape that pinned
// Living Bomb to rank 1 in report #38. Frost now grants Ice Lance through
// CLASS_PERK_GRANTS, so BestOwnedOrFirst walks the real chain and always
// returns the best rank the mage actually knows.

void TickMageFrostIceLance(Player* player, MageState& st, uint32 diff)
{
    if (!player || GetClassPerk(player) != SPELL_MAGE_FROST || !player->IsAlive() || !player->IsInCombat())
        return;
    st.frostTickAcc += diff;
    if (st.frostTickAcc < MAGE_FROST_ICE_TICK_MS)
        return;
    st.frostTickAcc = 0;
    uint32 const guid = player->GetGUID().GetCounter();
    if (!g_reentryGuard.insert(guid).second)
        return;
    uint32 const ice = BestOwnedOrFirst(player, SPELL_ICE_LANCE_R1);
    ForEachHostileInRange(player, CLASS_PERK_RANGE, [player, ice](Unit* target)
    {
        player->CastSpell(target, ice, true);
    });
    g_reentryGuard.erase(guid);
}

// -------------------------------------------------------------------------
// Rogue: Assassination (910035) -- Envenom is a detonator.
//
// The old perk was "poisons deal +300%, DoTs spread within 10 yards", which is
// two passive numbers and no moment. Subtlety works because Shadowstep is a
// button that visibly does something; this gives Assassination the same shape
// without losing the poison damage it already had.
//
// Envenom now consumes every damage-over-time the rogue owns, on the target
// and on everything within 15 yards: each one's entire remaining duration is
// dealt at once as instant damage, and then it is put back at full duration.
// So the play is to build bleeds and poisons wide, then cash them all in.
// -------------------------------------------------------------------------
void TryRogueAssassinationDetonate(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_ROGUE_ASSASSINATION)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || !RankOf(info, SPELL_ENVENOM_R1))
        return;

    uint32 const guid = player->GetGUID().GetCounter();
    if (!g_reentryGuard.insert(guid).second)
        return;

    ObjectGuid const owner = player->GetGUID();
    ForEachHostileInRange(player, ROGUE_DETONATE_RANGE, [player, owner](Unit* target)
    {
        // Collect first: dealing damage can kill the target and invalidate the
        // aura list mid-walk, which is the same discipline the Hemorrhage
        // spread and the Fire detonator needed.
        struct Pending { uint32 spellId; int32 damage; SpellSchoolMask school; };
        std::vector<Pending> pending;

        for (AuraEffect* eff : target->GetAuraEffectsByType(SPELL_AURA_PERIODIC_DAMAGE))
        {
            Aura* aura = eff->GetBase();
            if (!aura || aura->GetCasterGUID() != owner)
                continue;
            int32 const period = eff->GetAmplitude();
            if (period <= 0)
                continue;
            int32 const left = aura->GetDuration();
            if (left <= 0)
                continue;
            int32 const ticks = left / period;
            if (ticks <= 0)
                continue;
            pending.push_back({ aura->GetId(), eff->GetAmount() * ticks,
                                aura->GetSpellInfo()->GetSchoolMask() });
        }

        for (Pending const& p : pending)
        {
            if (!target->IsAlive())
                break;
            SpellInfo const* dotInfo = sSpellMgr->GetSpellInfo(p.spellId);
            Unit::DealDamage(player, target, uint32(std::max(0, p.damage)), nullptr,
                SPELL_DIRECT_DAMAGE, p.school, dotInfo, false);
            // Put it back at full duration rather than leaving the target
            // clean -- the fantasy is a detonation, not a dispel.
            if (Aura* again = target->GetAura(p.spellId, owner))
                again->RefreshDuration();
        }
    });

    g_reentryGuard.erase(guid);
}

// -------------------------------------------------------------------------
// Rogue: Combat (910036)
// "Blade Flurry is always active. Energy regeneration increased by 50%.
// Combo builders have a 30% chance to cast free Killing Spree."
//
// Blade Flurry is a permanent buff applied once when Combat is selected
// (and re-applied on login if missing), not re-checked every server tick.
// The original tick-based "recast if !HasAura" approach cast Blade Flurry
// on every single OnPlayerUpdate call whenever the aura wasn't present --
// which included while dead (an aura can't apply to a dead unit), so a
// player who died spent every tick forever re-attempting the cast.
// -------------------------------------------------------------------------
void ApplyRogueCombatBladeFlurry(Player* player)
{
    if (!player || !player->IsAlive() || GetClassPerk(player) != SPELL_ROGUE_COMBAT)
        return;
    if (!player->HasAura(SPELL_BLADE_FLURRY))
        if (Aura* aura = player->AddAura(SPELL_BLADE_FLURRY, player))
        {
            aura->SetDuration(-1);
            aura->SetMaxDuration(-1);
        }
}

void TickRogueCombat(Player* player, RogueState& st, uint32 diff)
{
    if (!player || !player->IsAlive() || GetClassPerk(player) != SPELL_ROGUE_COMBAT)
        return;
    st.energyTickAcc += diff;
    if (st.energyTickAcc < ROGUE_ENERGY_TICK_MS)
        return;
    st.energyTickAcc = 0;
    if (player->GetPower(POWER_ENERGY) < player->GetMaxPower(POWER_ENERGY))
        player->ModifyPower(POWER_ENERGY, ROGUE_ENERGY_TICK_BONUS);
}

bool IsComboPointBuilder(SpellInfo const* info)
{
    if (!info || info->SpellFamilyName != SPELLFAMILY_ROGUE)
        return false;
    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        if (info->Effects[i].Effect == SPELL_EFFECT_ADD_COMBO_POINTS)
            return true;
    return false;
}

// Rogue: Combat (910036) -- Adrenaline Rush is a free permanent toggle.
//
// The old perk was three passive numbers. This is the toggle shape that is
// already proven four times over in this module (Arms' Bladestorm, Feral's
// Berserk, Arcane Power, Enhancement's Feral Spirit): the power cost is
// refunded and the cooldown cleared on cast, and the aura's lifetime is
// observed from the update loop rather than touched during its own
// application -- which is what keeps it out of Unit::_AddAura.
//
// While it is up: abilities cost no energy, Blade Flurry reaches everything
// within 15 yards instead of one extra target, and Killing Spree is free of
// its cooldown.
void TryRogueCombatAdrenalineRush(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_ROGUE_COMBAT)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || !RankOf(info, SPELL_ADRENALINE_RUSH_R1))
        return;
    if (int32 const cost = spell->GetPowerCost())
        player->ModifyPower(POWER_ENERGY, cost);
    ClearCooldownAfterCast(player, info->Id, info->GetCategory());
}

bool RogueCombatRushUp(Player* player)
{
    if (!player || GetClassPerk(player) != SPELL_ROGUE_COMBAT)
        return false;
    for (uint32 id = SPELL_ADRENALINE_RUSH_R1; id; id = sSpellMgr->GetNextSpellInChain(id))
        if (player->HasAura(id))
            return true;
    return false;
}

void TickRogueCombatAdrenalineRush(Player* player)
{
    if (!player || !player->IsAlive() || GetClassPerk(player) != SPELL_ROGUE_COMBAT)
        return;
    for (uint32 id = SPELL_ADRENALINE_RUSH_R1; id; id = sSpellMgr->GetNextSpellInChain(id))
        if (Aura* aura = player->GetAura(id))
            if (aura->GetDuration() >= 0 && aura->GetDuration() < 5000)
                aura->SetDuration(aura->GetMaxDuration() > 0 ? aura->GetMaxDuration() : 15000);
}

// The "costs no energy" half. Refunding after the fact is how the other
// toggles do it, and it avoids fighting Spell::CheckPower before the cast is
// allowed at all.
void RefundRogueCombatEnergy(Player* player, Spell* spell)
{
    if (!spell || !RogueCombatRushUp(player))
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || info->PowerType != POWER_ENERGY)
        return;
    if (int32 const cost = spell->GetPowerCost())
        player->ModifyPower(POWER_ENERGY, cost);
}

// The "Blade Flurry reaches 15 yards" half: while the rush is up, every melee
// swing echoes onto everything else in range.
void TryRogueCombatFlurrySpread(Unit* attacker, uint32 damage)
{
    Player* player = attacker ? attacker->ToPlayer() : nullptr;
    if (!player || !damage || !RogueCombatRushUp(player))
        return;
    if (!player->HasAura(SPELL_BLADE_FLURRY))
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    if (!g_reentryGuard.insert(guid).second)
        return;
    Unit* current = player->GetVictim();
    ForEachHostileInRange(player, ROGUE_DETONATE_RANGE, [player, current, damage](Unit* target)
    {
        if (target == current)
            return;
        Unit::DealDamage(player, target, damage, nullptr, DIRECT_DAMAGE,
            SPELL_SCHOOL_MASK_NORMAL, nullptr, false);
    });
    g_reentryGuard.erase(guid);
}

void TryRogueCombatKillingSpree(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_ROGUE_COMBAT)
        return;
    if (player->HasAura(SPELL_KILLING_SPREE))
        return;
    if (!IsComboPointBuilder(spell->GetSpellInfo()))
        return;
    if (!roll_chance_i(int32(ROGUE_KS_CHANCE)))
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    if (!g_reentryGuard.insert(guid).second)
        return;
    player->RemoveSpellCooldown(SPELL_KILLING_SPREE, true);
    Unit* dest = spell->m_targets.GetUnitTarget();
    if (!dest || !player->IsValidAttackTarget(dest))
        dest = player->GetVictim();
    if (dest && player->IsValidAttackTarget(dest))
        player->CastSpell(dest, SPELL_KILLING_SPREE, true);
    else
        player->CastSpell(player, SPELL_KILLING_SPREE, true);
    g_reentryGuard.erase(guid);
}

// -------------------------------------------------------------------------
// Warrior: Arms (910083)
// "Learn Bladestorm. No rage cost, no cooldown, and it does not end.
// Recast to stop. You can use other abilities while spinning."
//
// The permanent toggle IS implemented (report #77, 2026-08-26), using the
// observe-only pattern proven by Starfall/Arcane Power/Feral Berserk: the
// update tick refreshes the aura's duration without touching its lifetime
// during application, and the off-switch runs in CheckCast's strict pass so
// the cast never starts. The historical crash (effect-3/CheckCast-after-
// aura-removal) is on the OLD implementation and is not reached by this
// shape. "Use other abilities while spinning" is handled by build_patch.py
// zeroing Bladestorm's ALLOW_ONLY_ABILITY effect (see Bonesaw.md).
// -------------------------------------------------------------------------
// Bug report #19, 2026-08-22: "should also autocast whirlwind and thunderclap
// every 6 seconds (affected by haste)" while Bladestorm is active.
//
// Driven from the per-player update tick rather than from any spell hook, and
// deliberately so. Bonesaw.md records a real crash history on this exact spell
// around effect-3/CheckCast-after-aura-removal, which is why the "does not end"
// toggle was never implemented. Ticking from the update loop means we only ever
// observe the aura, never interfere with its lifetime -- and every cast is
// triggered, so nothing re-enters Bladestorm's own CheckCast.
//
// Haste uses UNIT_MOD_CAST_SPEED, where values below 1 mean faster. Floored at
// one second so no amount of haste can turn this into a per-tick cast loop.
void TickWarriorArmsBladestorm(Player* player, uint32& acc, uint32 diff)
{
    if (!player || GetClassPerk(player) != SPELL_WARRIOR_ARMS)
        return;
    if (!player->HasAura(SPELL_BLADESTORM))
    {
        acc = 0;
        return;
    }

    // Report #77: "Bladestorm is now free to cast, but isn't a permanent buff."
    // The safe observe-only toggle (proven by Starfall/Arcane Power/Feral
    // Berserk -- wiki "free-permanent-toggle pattern") keeps the aura alive by
    // refreshing its duration from the update loop. We never touch its
    // lifetime during its own application, so the old CheckCast-after-removal
    // crash path is never reached. Recast-to-stop lives in the strict
    // CheckCast pass (TryWarriorArmsBladestormToggleOff).
    if (Aura* aura = player->GetAura(SPELL_BLADESTORM))
        if (aura->GetDuration() >= 0 && aura->GetDuration() < 5000)
            aura->SetDuration(aura->GetMaxDuration() > 0 ? aura->GetMaxDuration() : 30000);

    float haste = player->GetFloatValue(UNIT_MOD_CAST_SPEED);
    if (haste <= 0.0f)
        haste = 1.0f;
    uint32 interval = uint32(float(BLADESTORM_AUTOCAST_MS) * haste);
    if (interval < 1000)
        interval = 1000;

    acc += diff;
    if (acc < interval)
        return;
    acc = 0;

    ObjectGuid const guid = player->GetGUID();
    player->m_Events.AddEventAtOffset([guid]()
    {
        Player* p = ObjectAccessor::FindPlayer(guid);
        if (!p || !p->IsInWorld() || !p->IsAlive() || !p->HasAura(SPELL_BLADESTORM))
            return;
        if (uint32 const ww = BestOwned(p, SPELL_WHIRLWIND))
            p->CastSpell(p, ww, true);
        if (uint32 const tc = BestOwned(p, SPELL_THUNDER_CLAP))
            p->CastSpell(p, tc, true);
    }, std::chrono::milliseconds(1));
}

// Report #77: the off-switch. Runs in CheckCast's strict pass (same ordering
// discipline as TryDruidBalanceStarfallToggleOff -- removing the aura there
// means the cast never starts, so nothing re-applies it).
void TryWarriorArmsBladestormToggleOff(Spell* spell, bool strict, SpellCastResult& res)
{
    if (!strict || res != SPELL_CAST_OK || !spell)
        return;
    Unit* caster = spell->GetCaster();
    Player* player = caster ? caster->ToPlayer() : nullptr;
    if (!player || GetClassPerk(player) != SPELL_WARRIOR_ARMS)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || !RankOf(info, SPELL_BLADESTORM))
        return;
    if (!player->HasAura(SPELL_BLADESTORM))
        return;                     // not spinning -> let the cast through, toggle ON
    player->RemoveAurasDueToSpell(SPELL_BLADESTORM);
    res = SPELL_FAILED_DONT_REPORT; // toggle OFF, and the cast never happens
}

void TryWarriorArmsOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_WARRIOR_ARMS)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || info->Id != SPELL_BLADESTORM)
        return;
    // No refund here any more. Handing the rage back after the fact only works
    // for a caster who had it to spend in the first place -- with an empty rage
    // bar the cast was refused outright with "Not enough rage" (reports #39 and
    // #74) and the refund never ran. Worse, once CheckPower was satisfied the
    // refund paid out the full cost whether or not that much was actually
    // taken, which on a near-empty bar was a rage generator.
    //
    // The cost is zeroed at source instead, in Spell::prepare, via
    // LivingGear_SpellIsFreeCast below.
    ClearCooldownAfterCast(player, SPELL_BLADESTORM, info->GetCategory());
}

// -------------------------------------------------------------------------
// Warrior: Fury (910084)
// "Titan's Grip. Each melee hit: +5% attack speed (20 stacks) and heal 1%
// of max health in combat. Attack speed lingers 60 sec after combat.
// Rend and Deep Wounds deal +300% damage."
// -------------------------------------------------------------------------
void SetFuryHastePct(Player* player, WarriorFuryState& st, uint32 pct)
{
    if (!player || pct == st.appliedPct)
        return;
    if (st.appliedPct)
    {
        player->ApplyAttackTimePercentMod(BASE_ATTACK, -float(st.appliedPct), false);
        player->ApplyAttackTimePercentMod(OFF_ATTACK, -float(st.appliedPct), false);
    }
    if (pct)
    {
        player->ApplyAttackTimePercentMod(BASE_ATTACK, -float(pct), true);
        player->ApplyAttackTimePercentMod(OFF_ATTACK, -float(pct), true);
    }
    st.appliedPct = pct;
}

void NoteFuryMeleeHit(Player* player)
{
    if (!player || GetClassPerk(player) != SPELL_WARRIOR_FURY)
        return;
    if (!player->CanTitanGrip())
        player->SetCanTitanGrip(true);
    WarriorFuryState& st = g_fury[player->GetGUID().GetCounter()];
    if (st.stacks < FURY_HASTE_CAP)
        ++st.stacks;
    st.lastHitMs = getMSTime();
    SetFuryHastePct(player, st, st.stacks * FURY_HASTE_PCT_PER_STACK);
    if (player->IsInCombat() && player->GetHealth() < player->GetMaxHealth())
    {
        uint32 const heal = player->CountPctFromMaxHealth(1);
        if (heal)
            player->ModifyHealth(int32(heal));
    }
}

void TickWarriorFury(Player* player)
{
    if (!player)
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    auto it = g_fury.find(guid);
    if (it == g_fury.end() || !it->second.stacks)
        return;
    if (GetClassPerk(player) != SPELL_WARRIOR_FURY)
    {
        SetFuryHastePct(player, it->second, 0);
        it->second.stacks = 0;
        return;
    }
    if (player->IsInCombat())
        return;
    if (getMSTimeDiff(it->second.lastHitMs, getMSTime()) >= FURY_HASTE_LINGER_MS)
    {
        SetFuryHastePct(player, it->second, 0);
        it->second.stacks = 0;
    }
}

// Rend and Deep Wounds are pure SPELL_AURA_PERIODIC_DAMAGE auras (verified in
// Spell.dbc: both are Effect_1 = APPLY_AURA, Aura = 3). Periodic ticks are
// dealt in SpellAuraEffects.cpp and reach ModifyPeriodicDamageAurasTick;
// ModifySpellDamageTaken is only ever called from
// Unit::CalculateSpellDamageTaken, which handles DIRECT spell damage. This
// multiplier was registered on the direct hook alone, so it never fired once.
//
// Mage Fire is the pattern to copy: it registers both halves, because Living
// Bomb has a periodic tick AND a direct explosion. Rend and Deep Wounds have
// only the tick, so only the periodic form is needed here.
bool IsFuryBleed(Player* player, SpellInfo const* info)
{
    if (!player || !info || GetClassPerk(player) != SPELL_WARRIOR_FURY)
        return false;
    return RankOf(info, SPELL_REND) || info->Id == SPELL_DEEP_WOUNDS_DOT;
}

void ApplyFuryBleedPeriodic(Unit* attacker, uint32& damage, SpellInfo const* info)
{
    if (!attacker || !damage)
        return;
    if (IsFuryBleed(attacker->ToPlayer(), info))
        damage *= 4;
}

// -------------------------------------------------------------------------
// Warrior: Protection (910085) -- "Lord of Shields and Thunder"
// "Shield Slam hits 3 additional times, each hit rupturing for 8 yards.
// Thunder Clap radius x3 and applies your Rend and Deep Wounds at 3x to
// every enemy hit. Avalanche: a castable toggle that drops a 12-yard
// Death and Decay-style circle at your target -- shield-slam-grade damage
// and a 50% slow inside every second; recast to move it, recast on the
// circle to dismiss it. +25% block chance while it is on the ground."
//
// Avalanche deliberately reuses Death and Decay (43265) instead of a new
// spell_dbc row: the DBC row is verified single-rank and ground-target, so
// the engine already owns "place a persistent circle at the clicked point",
// and CLASS_PERK_GRANTS handles un-learning it on spec switch for free.
// -------------------------------------------------------------------------
void ThunderClapApplyBleeds(Player* player)
{
    if (!player)
        return;
    uint32 const rend = BestOwned(player, SPELL_REND);
    bool deep = false;
    for (uint32 id : SPELL_DEEP_WOUNDS_TALENT)
        if (player->HasSpell(id))
        {
            deep = true;
            break;
        }
    if (!rend && !deep)
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    if (!g_reentryGuard.insert(guid).second)
        return;
    ForEachHostileInRange(player, CLASS_PERK_RANGE, [player, rend, deep](Unit* target)
    {
        if (rend)
            player->CastSpell(target, rend, true);
        if (deep)
        {
            int32 const bp = int32((player->GetFloatValue(UNIT_FIELD_MAXDAMAGE)
                + player->GetFloatValue(UNIT_FIELD_MINDAMAGE)) / 2.0f);
            player->CastCustomSpell(target, SPELL_DEEP_WOUNDS_DOT, &bp, nullptr, nullptr, true);
        }
    });
    g_reentryGuard.erase(guid);
}

// The 8-yard rupture behind every Shield Slam hit. Thunder Clap's own damage
// component is the existing cheap AoE with the right footprint (radius index
// 14 = 8yd at rank 8, verified from Spell.dbc), so the rupture simply casts
// the player's TC rank triggered on themselves. Triggered casts never re-enter
// OnPlayerSpellCast (the hook returns early on IsTriggered), so there is no
// bleed-recursion path.
void WarriorProtRupture(Player* player)
{
    if (!player || !player->IsAlive())
        return;
    if (uint32 const tc = BestOwned(player, SPELL_THUNDER_CLAP))
        player->CastSpell(player, tc, true);
}

// "Shield Slams hit 3 additional times": three extra triggered copies of the
// player's Shield Slam rank on top of the one they cast (Divine Storm shape),
// each carrying its own rupture.
void QueueProtShieldSlamExtraHits(Player* player)
{
    if (!player)
        return;
    ObjectGuid const guid = player->GetGUID();
    for (uint32 i = 0; i < WARRIOR_PROT_SS_EXTRA_HITS; ++i)
    {
        player->m_Events.AddEventAtOffset([guid]()
        {
            Player* p = ObjectAccessor::FindPlayer(guid);
            if (!LivingGear_SafeToCastOn(p))
                return;
            Unit* victim = p->GetVictim();
            if (uint32 const ss = BestOwned(p, SPELL_SHIELD_SLAM_R1))
                if (victim && p->IsValidAttackTarget(victim))
                    p->CastSpell(victim, ss, true);
            WarriorProtRupture(p);
        }, std::chrono::milliseconds(WARRIOR_PROT_SS_HIT_DELAY_MS * (i + 1)));
    }
}

void TryWarriorProtOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_WARRIOR_PROTECTION)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info)
        return;
    if (info->Id == SPELL_SHOCKWAVE)
    {
        ClearCooldownAfterCast(player, SPELL_SHOCKWAVE, info->GetCategory());
        return;
    }
    if (RankOf(info, SPELL_SHIELD_SLAM_R1))
    {
        QueueProtShieldSlamExtraHits(player);
        WarriorProtRupture(player);
        return;
    }
    if (RankOf(info, SPELL_DEATH_AND_DECAY))
    {
        ClearCooldownAfterCast(player, SPELL_DEATH_AND_DECAY, info->GetCategory());
        // Recast while a circle is live = MOVE it: drop the old dynobj so the
        // engine places the fresh one at the new target point. (Toggling off
        // lives in TryWarriorProtAvalancheToggleOff, strict CheckCast pass.)
        player->RemoveDynObject(SPELL_DEATH_AND_DECAY);
        return;
    }
    if (RankOf(info, SPELL_THUNDER_CLAP))
        ThunderClapApplyBleeds(player);
}

// "3x their normal damage": ThunderClapApplyBleeds applies the real Rend /
// Deep Wounds auras, whose per-tick damage reaches
// ModifyPeriodicDamageAurasTick (the Fury multiplier's hook, for the same
// reason -- these are pure PERIODIC_DAMAGE auras, never direct damage).
void ApplyProtTCBleedPeriodic(Unit* attacker, uint32& damage, SpellInfo const* info)
{
    if (!attacker || !damage || !info)
        return;
    Player* player = attacker->ToPlayer();
    if (!player || GetClassPerk(player) != SPELL_WARRIOR_PROTECTION)
        return;
    if (RankOf(info, SPELL_REND) || info->Id == SPELL_DEEP_WOUNDS_DOT)
        damage *= 3;
}

// Avalanche's off-switch, strict CheckCast pass (Bladestorm toggle-off
// discipline: removing the circle here means the cast never starts). Casting
// ON TOP of the live circle dismisses it; casting anywhere else falls through
// and TryWarriorProtOnCast's Death and Decay branch turns the cast into a move.
void TryWarriorProtAvalancheToggleOff(Spell* spell, bool strict, SpellCastResult& res)
{
    if (!strict || res != SPELL_CAST_OK || !spell)
        return;
    Unit* caster = spell->GetCaster();
    Player* player = caster ? caster->ToPlayer() : nullptr;
    if (!player || GetClassPerk(player) != SPELL_WARRIOR_PROTECTION)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || !RankOf(info, SPELL_DEATH_AND_DECAY))
        return;
    DynamicObject* dyn = player->GetDynObject(SPELL_DEATH_AND_DECAY);
    if (!dyn)
        return;                     // no circle on the ground -> toggle ON
    WorldLocation const* dest = spell->m_targets.GetDstPos();
    if (!dest || dyn->GetExactDist(*dest) > AVALANCHE_TOGGLE_RANGE)
        return;                     // casting elsewhere -> this cast moves it
    player->RemoveDynObject(SPELL_DEATH_AND_DECAY);
    player->RemoveAurasDueToSpell(SPELL_AVALANCHE_BLOCK_AURA);
    res = SPELL_FAILED_DONT_REPORT; // toggle OFF, and the cast never happens
}

// Avalanche's ground: while the Death and Decay dynobj is live, every second
// everything inside takes the player's Shield Slam (shield-slam-grade, rank-
// correct via BestOwned) and the player's Hamstring (50% slow, rank-agnostic).
// The +25% block chance passive is a Defensive Tactics (29559) aura with the
// amount overridden to 25, refreshed from here while the circle lives.
void TickWarriorProtAvalanche(Player* player, uint32& acc, uint32 diff)
{
    if (!player)
        return;
    bool const isProt = GetClassPerk(player) == SPELL_WARRIOR_PROTECTION && player->IsAlive();
    DynamicObject* dyn = player->GetDynObject(SPELL_DEATH_AND_DECAY);
    if (!isProt || !dyn)
    {
        // Expired, dismissed, or the perk was switched away: the passive goes
        // with it. The aura's own ~30s duration bounds the worst case.
        if (player->HasAura(SPELL_AVALANCHE_BLOCK_AURA))
            player->RemoveAurasDueToSpell(SPELL_AVALANCHE_BLOCK_AURA);
        acc = 0;
        return;
    }

    acc += diff;
    if (acc < AVALANCHE_TICK_MS)
        return;
    acc = 0;

    if (!player->HasAura(SPELL_AVALANCHE_BLOCK_AURA))
    {
        int32 const blockPct = AVALANCHE_BLOCK_PCT;
        player->CastCustomSpell(player, SPELL_AVALANCHE_BLOCK_AURA, &blockPct, nullptr, nullptr, true);
    }

    float radius = dyn->GetRadius();
    if (radius <= 0.0f)
        radius = CLASS_PERK_RANGE;
    uint32 const hamstring = BestOwned(player, SPELL_HAMSTRING);
    uint32 const ss = BestOwned(player, SPELL_SHIELD_SLAM_R1);
    ForEachHostileNear(player, dyn, radius, [player, hamstring, ss](Unit* target)
    {
        if (hamstring)
            player->CastSpell(target, hamstring, true);
        if (ss)
            player->CastSpell(target, ss, true);
    });
}

void ApplyProtThunderClapRadius(Spell* spell, Player* player, SpellInfo const* info)
{
    if (!spell || !player || !info || GetClassPerk(player) != SPELL_WARRIOR_PROTECTION)
        return;
    if (RankOf(info, SPELL_THUNDER_CLAP))
        spell->SetSpellValue(SPELLVALUE_RADIUS_MOD, RADIUS_MOD_TRIPLE);
}

void ApplyProtShockwaveDamage(Unit* attacker, int32& damage, SpellInfo const* info)
{
    if (!attacker || damage <= 0 || !info || info->Id != SPELL_SHOCKWAVE)
        return;
    Player* player = attacker->ToPlayer();
    if (!player || GetClassPerk(player) != SPELL_WARRIOR_PROTECTION)
        return;
    damage *= 4;
}

// -------------------------------------------------------------------------
// Hunter: Marksmanship (910150)
// "Chimera Shot has no cooldown and refreshes Serpent Sting to full
// duration. Ranged shots have a chance to grant a free, instant Aimed
// Shot."
//
// SIMPLIFICATION: "ranged shots have a chance" procs off any non-triggered
// Hunter ranged-damage spell cast (SpellFamilyName Hunter + DmgClass
// Ranged), not specifically off a crit -- there is no crit-outcome hook
// exposed to script code anywhere in this codebase (checked: UnitScript
// only exposes post-hoc damage-modify hooks, not the crit roll itself), so
// "on crit" would mean adding new engine plumbing for one perk. Same
// risk/reward tradeoff already made for Warrior Fury/Protection's flat
// damage multipliers elsewhere in this file.
// -------------------------------------------------------------------------
void TryHunterMMOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_HUNTER_MARKSMANSHIP)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info)
        return;
    if (info->Id == SPELL_CHIMERA_SHOT)
    {
        ClearCooldownAfterCast(player, SPELL_CHIMERA_SHOT, info->GetCategory());
        if (Unit* target = spell->m_targets.GetUnitTarget())
        {
            uint32 const sting = BestOwned(player, SPELL_SERPENT_STING_R1);
            if (sting && player->IsValidAttackTarget(target))
                player->CastSpell(target, sting, true);
        }
        return;
    }
    if (info->SpellFamilyName != SPELLFAMILY_HUNTER || info->DmgClass != SPELL_DAMAGE_CLASS_RANGED)
        return;
    Unit* target = spell->m_targets.GetUnitTarget();
    if (!target || !player->IsValidAttackTarget(target))
        return;
    if (!roll_chance_i(int32(HUNTER_MM_AIMED_PROC_CHANCE)))
        return;
    uint32 const aimed = BestOwned(player, SPELL_AIMED_SHOT_R1);
    if (aimed)
        player->CastSpell(target, aimed, true);
}

// -------------------------------------------------------------------------
// Shaman: Elemental guardians (#167)
// "add 2 permanent fire elementals (scaled down to 25% of regular size) as
// guardians--also make them do 2000% extra damage"
//
// SummonGuardian re-owners the totem's cast to the PLAYER, so both the
// vanilla elemental and the twin are player-owned temp summons and pass
// through OnPlayerBeforeTempSummonInitStats -- which is where they are
// shrunk and made permanent (duration 0 turns the summon into
// TEMPSUMMON_DEAD_DESPAWN: it lives until something kills it). The twin is
// summoned straight through Map::SummonCreature with the vanilla summon
// properties, so it is the same creature class with the same AI as the
// vanilla elemental. x21, not x20: "2000% extra", the same reading every
// other 2000% kit on this realm uses (Living Bomb, Affliction ladder).
// -------------------------------------------------------------------------
uint32 const NPC_GREATER_FIRE_ELEMENTAL = 15438;
uint32 const SPELL_FIRE_ELEMENTAL_TOTEM = 2894;
uint32 const SPELL_SUMMON_GREATER_FIRE_ELEMENTAL = 32982;  // what the totem casts
uint32 const SUMMON_PROPS_GREATER_FIRE_ELEMENTAL = 61;     // slot 0: guardians coexist
float const FIRE_ELEMENTAL_GUARDIAN_SCALE = 0.25f;
uint32 const FIRE_ELEMENTAL_GUARDIAN_DAMAGE_MULT = 21;     // +2000%

// One pack per Elemental shaman. Summon properties 61 has slot 0, so a new
// totem would otherwise stack a third and fourth guardian instead of
// replacing the old pair.
std::unordered_map<uint32, std::vector<ObjectGuid>> g_feGuardianPacks;

void DespawnShamanFireElementals(Player* player)
{
    auto itr = g_feGuardianPacks.find(player->GetGUID().GetCounter());
    if (itr == g_feGuardianPacks.end())
        return;
    for (ObjectGuid const& guid : itr->second)
        if (Creature* fe = ObjectAccessor::GetCreature(*player, guid))
            if (fe->IsSummon())
                fe->ToTempSummon()->UnSummon();
    g_feGuardianPacks.erase(itr);
}

Player* FireElementalGuardianOwner(Unit const* summon)
{
    if (!summon || summon->GetEntry() != NPC_GREATER_FIRE_ELEMENTAL)
        return nullptr;
    Unit* owner = summon->GetOwner();
    Player* player = owner ? owner->ToPlayer() : nullptr;
    if (!player || GetClassPerk(player) != SPELL_SHAMAN_ELEMENTAL)
        return nullptr;
    return player;
}

// Called from ClassPerksPlayer::OnPlayerBeforeTempSummonInitStats for every
// player-owned temp summon.
void ApplyShamanElementalGuardianSummon(Player* player, TempSummon* summon, uint32& duration)
{
    if (!player || !summon || summon->GetEntry() != NPC_GREATER_FIRE_ELEMENTAL)
        return;
    if (summon->GetUInt32Value(UNIT_CREATED_BY_SPELL) != SPELL_SUMMON_GREATER_FIRE_ELEMENTAL)
        return;
    if (GetClassPerk(player) != SPELL_SHAMAN_ELEMENTAL)
        return;
    summon->SetObjectScale(FIRE_ELEMENTAL_GUARDIAN_SCALE);
    duration = 0;
    g_feGuardianPacks[player->GetGUID().GetCounter()].push_back(summon->GetGUID());
}

// Called from ClassPerksUnit::OnDamage -- the one funnel every direct damage
// number passes through (melee, spell hits and periodic ticks alike).
void ApplyShamanElementalGuardianDamage(Unit* attacker, uint32& damage)
{
    if (!damage || !FireElementalGuardianOwner(attacker))
        return;
    damage *= FIRE_ELEMENTAL_GUARDIAN_DAMAGE_MULT;
}

// -------------------------------------------------------------------------
// Shaman: Elemental (910151)
// "Thunderstorm has no cooldown. Lava Burst deals double damage. Chain
// Lightning has no target cap."
// -------------------------------------------------------------------------
void TryShamanElementalOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_SHAMAN_ELEMENTAL)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info)
        return;

    // Report #167: the totem itself summons the first guardian through the
    // vanilla path four seconds from now (the summon resolves to the player,
    // so the InitStats hook below scales and permanizes it); the twin goes
    // out right here with the same summon properties and the same 32982
    // spell stamp, which makes it the same creature class with the same AI.
    // A fresh totem despawns the previous pack first, since slot 0 means
    // nothing would ever replace it.
    if (info->Id == SPELL_FIRE_ELEMENTAL_TOTEM)
    {
        DespawnShamanFireElementals(player);
        if (SummonPropertiesEntry const* props = sSummonPropertiesStore.LookupEntry(SUMMON_PROPS_GREATER_FIRE_ELEMENTAL))
            player->GetMap()->SummonCreature(NPC_GREATER_FIRE_ELEMENTAL, player->GetPosition(), props,
                0, player, SPELL_SUMMON_GREATER_FIRE_ELEMENTAL);
        return;
    }

    // RankOf, not an exact id: Thunderstorm is rank 1 of 4 (51490 -> 51502,
    // 51503, 51504). The old comment here claimed single rank and was wrong,
    // so an Elemental shaman past rank 1 got no cooldown removal at all.
    if (!RankOf(info, SPELL_THUNDERSTORM))
        return;
    // info->Id, not SPELL_THUNDERSTORM: the player may be casting a higher rank,
    // and clearing rank 1's cooldown entry would leave theirs untouched.
    ClearCooldownAfterCast(player, info->Id, info->GetCategory());
}

void ApplyShamanElementalLavaBurstDamage(Unit* attacker, int32& damage, SpellInfo const* info)
{
    if (!attacker || damage <= 0 || !info || !RankOf(info, SPELL_LAVA_BURST_R1))
        return;
    Player* player = attacker->ToPlayer();
    if (!player || GetClassPerk(player) != SPELL_SHAMAN_ELEMENTAL)
        return;
    damage *= 2;
}

void ApplyShamanElementalChainLightningTargets(Spell* spell, Player* player, SpellInfo const* info)
{
    if (!spell || !player || !info || GetClassPerk(player) != SPELL_SHAMAN_ELEMENTAL)
        return;
    if (RankOf(info, SPELL_CHAIN_LIGHTNING_R1))
        spell->SetSpellValue(SPELLVALUE_MAX_TARGETS, CHAIN_LIGHTNING_MAX_TARGETS);
}

// -------------------------------------------------------------------------
// Death Knight: Unholy (910152) -- Kit 7, user-revised definition
// "A permanent blight aura follows you: every 2 seconds it applies all of
// your diseases to every enemy within 10 yards and refreshes any it already
// infected. Your diseases deal 5x damage. Your Army of the Dead group
// persists while you are in combat and despawns 60s after you leave it.
// Your Gargoyle stays until it dies."
// (Replaces the plan's original Desecration circle: 65142 does not exist in
// this DBC, and the user's revision defines the kit as a disease aura.)
// Existing, unchanged: Summon Gargoyle and Army of the Dead have no cooldown;
// the Army group is 1 tank + 1 healer + 3 dps.
//
// ARMY PERSISTENCE is native, not tick-driven: the ghouls are summoned as
// TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, whose TempSummon::Update resets
// m_timer back to m_lifetime (60s) on every tick while in combat and only
// counts down out of combat. Exactly the requested behavior -- indefinite in
// combat, 60s after leaving -- with zero module-side timer management.
//
// GARGOYLE is refreshed from the blight tick below while the creature is
// alive (its TempSummon timer is topped back up every 2s), so its vanilla
// despawn timer can never run out. The timer drive uses the public
// SetTimer() setter only (m_lifetime untouched).
//
// The vanilla ghoul swarm from Army of the Dead is left completely alone
// (its own engine-side aura/summon logic in spell_dk.cpp is not touched --
// safer than trying to suppress it). This perk summons its own separate
// 5-ghoul group alongside it, so the net effect is "the usual swarm, plus
// an actual mini raid group."
// -------------------------------------------------------------------------
// Both roles reuse the Army of the Dead ghoul's own model (26079, see
// rev_living_gear_army_of_the_dead.sql) for this first pass -- distinct
// per-role models (bulkier tank, ghostly healer) are a follow-up, same as
// how the Shadow Clone's look-alike model started Human-only and grew from
// there. All 5 are distinguishable by nameplate/behavior even while they
// share one look.
static TempSummon* SpawnArmyGhoul(Player* player, uint32 entry, Position const& pos, uint32 healthPct)
{
    // OUT_OF_COMBAT: TempSummon::Update holds m_timer at m_lifetime while in
    // combat and counts the 60s down only after combat ends (Kit 7 army
    // persistence). No tick-side refresh needed for the group.
    TempSummon* s = player->SummonCreature(entry, pos, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, ARMY_GROUP_DESPAWN_MS);
    if (!s)
        return nullptr;
    s->SetOwnerGUID(player->GetGUID());
    s->SetFaction(player->GetFaction());
    s->SetLevel(player->GetLevel());
    uint32 const hp = std::max<uint32>(1, player->GetMaxHealth() * healthPct / 100);
    s->SetMaxHealth(hp);
    s->SetHealth(hp);
    return s;
}

void SummonArmyGroup(Player* player)
{
    if (!player || !player->IsInWorld())
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    // Despawn any previous group still standing (e.g. Army of the Dead
    // recast before the old group's timer ran out) rather than letting
    // groups pile up -- same one-slot-per-owner rule as the Shadow Clone.
    auto old = g_armyGroup.find(guid);
    if (old != g_armyGroup.end())
    {
        for (ObjectGuid const& g : old->second)
            if (Creature* c = ObjectAccessor::GetCreature(*player, g))
                c->DespawnOrUnsummon();
        old->second.clear();
    }
    std::vector<ObjectGuid>& group = g_armyGroup[guid];
    Position center = player->GetPosition();
    if (TempSummon* tank = SpawnArmyGhoul(player, NPC_GHOUL_TANK, center, 300))
        group.push_back(tank->GetGUID());
    if (TempSummon* healer = SpawnArmyGhoul(player, NPC_GHOUL_HEALER, center, 100))
        group.push_back(healer->GetGUID());
    for (uint32 i = 0; i < 3; ++i)
    {
        Position p = center;
        p.RelocateOffset({ frand(-2.5f, 2.5f), frand(-2.5f, 2.5f), 0.0f, 0.0f });
        if (TempSummon* dps = SpawnArmyGhoul(player, NPC_GHOUL_DPS, p, 100))
            group.push_back(dps->GetGUID());
    }
}

void TryDkUnholyOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_DK_UNHOLY)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info)
        return;
    if (info->Id == SPELL_SUMMON_GARGOYLE)
    {
        ClearCooldownAfterCast(player, SPELL_SUMMON_GARGOYLE, info->GetCategory());
        return;
    }
    if (info->Id != SPELL_ARMY_OF_THE_DEAD)
        return;
    ClearCooldownAfterCast(player, SPELL_ARMY_OF_THE_DEAD, info->GetCategory());
    // Deferred: OnPlayerSpellCast fires mid-way through the triggering
    // spell's own Spell::cast() (see the reentrancy notes elsewhere in this
    // codebase / Bonesaw.md "Reentrant hook execution"), and summoning 5
    // creatures is exactly the kind of heavier reentrant call that pattern
    // warns about. Re-resolves the player by GUID a tick later instead of
    // summoning straight from inside this callback.
    ObjectGuid playerGuid = player->GetGUID();
    player->m_Events.AddEventAtOffset([playerGuid]()
    {
        if (Player* p = ObjectAccessor::FindPlayer(playerGuid))
            if (p->IsInWorld() && GetClassPerk(p) == SPELL_DK_UNHOLY)
                SummonArmyGroup(p);
    }, std::chrono::milliseconds(1));
}

// Kit 7: the permanent blight aura, on the shared 2s module tick (same
// cadence as the warlock/druid spread ticks; reentry guard like the rest).
// Every tick, while the perk is selected and the DK is alive:
// 1. Unholy Blight is re-cast on the DK as the client-visible blight marker
//    (it IS the canonical blight shroud; re-casting also refreshes its own
//    short duration, so the visual never drops).
// 2. Every hostile within 10 yards gets all the DK's diseases cast on them.
//    A target already carrying a disease gets it refreshed by the recast
//    (the Insect Swarm refresh pattern in TickDruidBalanceInsectContagion:
//    same-caster reapplication refreshes duration).
// 3. A living Ebon Gargoyle (Kit 7 toggle persistence) has its TempSummon
//    despawn timer topped back up, so it only ever dies to damage.
void TickDkUnholyBlight(Player* player, TickState& st, uint32 diff)
{
    if (!player || GetClassPerk(player) != SPELL_DK_UNHOLY || !player->IsAlive())
        return;
    st.acc += diff;
    if (st.acc < DK_UNHOLY_BLIGHT_TICK_MS)
        return;
    st.acc = 0;

    uint32 const guid = player->GetGUID().GetCounter();
    if (!g_reentryGuard.insert(guid).second)
        return;

    // Blood Plague/Frost Fever are single ids all ranks (see constants
    // block); Ebon Plague is talent-only and may be unowned.
    player->CastSpell(player,
        player->HasSpell(SPELL_UNHOLY_BLIGHT_R2) ? SPELL_UNHOLY_BLIGHT_R2 : SPELL_UNHOLY_BLIGHT_R1, true);
    uint32 const ebon = BestOwned(player, SPELL_EBON_PLAGUE_R1);
    ForEachHostileInRange(player, DK_UNHOLY_BLIGHT_RANGE, [&](Unit* target)
    {
        player->CastSpell(target, SPELL_BLOOD_PLAGUE_DISEASE, true);
        player->CastSpell(target, SPELL_FROST_FEVER_DISEASE, true);
        if (ebon)
            player->CastSpell(target, ebon, true);
    });

    // Gargoyle persistence: refresh the vanilla despawn timer of any living
    // Ebon Gargoyle this player controls. Timer-only (SetTimer), so the
    // summon type and lifetime are untouched; once the gargoyle is dead it
    // stays dead, and a dead/not-summoned one is never resurrected here.
    for (Unit* controlled : player->m_Controlled)
        if (Creature* c = controlled->ToCreature())
            if (c->GetEntry() == NPC_EBON_GARGOYLE)
                if (TempSummon* s = c->ToTempSummon())
                    s->SetTimer(DK_GARGOYLE_REFRESH_MS);

    g_reentryGuard.erase(guid);
}

// Kit 7: disease damage x5. Fires from ModifyPeriodicDamageAurasTick, same
// hook the Affliction DoT multiplier uses. The disease set is exactly the
// one the blight aura applies -- Blood Plague, Frost Fever, Ebon Plague
// (Unholy Blight's own pulse is the marker visual, not a disease, and is
// deliberately left at its base damage).
void ApplyDkUnholyDiseaseDamage(Unit* attacker, uint32& damage, SpellInfo const* info)
{
    if (!attacker || damage <= 0 || !info)
        return;
    Player* player = attacker->ToPlayer();
    if (!player || GetClassPerk(player) != SPELL_DK_UNHOLY)
        return;
    if (info->Id != SPELL_BLOOD_PLAGUE_DISEASE && info->Id != SPELL_FROST_FEVER_DISEASE
        && !RankOf(info, SPELL_EBON_PLAGUE_R1))
        return;
    damage *= DK_UNHOLY_DISEASE_MULT;
}

// -------------------------------------------------------------------------
// Army ghoul AI: shared struct, branches on me->GetEntry() for role. Not a
// real playerbot -- see the 2026-08-20 wiki note on why that's infeasible
// for a temporary summon (mod-playerbots hard-requires a real
// Player+WorldSession). Same "read the owner, follow/engage, despawn if
// abandoned" shape as npc_lg_shadow_cloneAI in LivingGear_Perks.cpp.
// -------------------------------------------------------------------------
struct npc_lg_army_ghoulAI : public ScriptedAI
{
    npc_lg_army_ghoulAI(Creature* c) : ScriptedAI(c) { }

    void Reset() override
    {
        me->SetReactState(REACT_DEFENSIVE);
        _healTickMs = 0;
    }

    void UpdateAI(uint32 diff) override
    {
        Unit* ownerUnit = me->GetOwner();
        Player* owner = ownerUnit ? ownerUnit->ToPlayer() : nullptr;
        if (!owner || !owner->IsInWorld() || !me->IsWithinDistInMap(owner, 60.0f))
        {
            me->DespawnOrUnsummon();
            return;
        }

        if (me->GetEntry() == NPC_GHOUL_HEALER)
        {
            UpdateHealer(owner, diff);
            return;
        }

        // Tank/dps: pet-aggressive self-defense first, same fix as the
        // Shadow Clone for "took ~10 seconds to react".
        if (!me->GetVictim())
        {
            if (Unit* attacker = me->getAttackerForHelper())
            {
                AttackStart(attacker);
                if (me->GetEntry() == NPC_GHOUL_TANK)
                    HoldThreat(attacker);
                DoMeleeAttackIfReady();
                return;
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
                me->GetMotionMaster()->MoveFollow(owner, 3.0f, frand(0.0f, 2.0f * float(M_PI)));
            return;
        }

        if (me->GetVictim() != ownerTarget)
            AttackStart(ownerTarget);
        if (me->GetEntry() == NPC_GHOUL_TANK)
            HoldThreat(ownerTarget);
        DoMeleeAttackIfReady();
    }

private:
    uint32 _healTickMs = 0;

    // Not a real Taunt cast (bosses can be taunt-immune, and a resisted
    // Taunt would leave the tank ghoul doing nothing useful). Just makes
    // sure it always outranks the other 4 summons on this target's threat
    // table, which is all "hold aggro for the group" actually needs here.
    void HoldThreat(Unit* target)
    {
        if (target && target->IsAlive())
            target->GetThreatMgr().AddThreat(me, 500.0f, nullptr, true, true);
    }

    void UpdateHealer(Player* owner, uint32 diff)
    {
        if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
            me->GetMotionMaster()->MoveFollow(owner, 8.0f, frand(0.0f, 2.0f * float(M_PI)));

        if (_healTickMs > diff)
        {
            _healTickMs -= diff;
            return;
        }
        _healTickMs = ARMY_HEALER_TICK_MS;

        Unit* lowest = nullptr;
        float lowestPct = 100.0f;
        auto consider = [&lowest, &lowestPct](Unit* u)
        {
            if (!u || !u->IsAlive())
                return;
            float const pct = u->GetHealthPct();
            if (pct < lowestPct)
            {
                lowestPct = pct;
                lowest = u;
            }
        };
        consider(owner);
        auto it = g_armyGroup.find(owner->GetGUID().GetCounter());
        if (it != g_armyGroup.end())
            for (ObjectGuid const& g : it->second)
                if (Creature* c = ObjectAccessor::GetCreature(*owner, g))
                    consider(c);

        if (!lowest || lowestPct >= float(ARMY_HEALER_HEAL_THRESHOLD_PCT))
            return;
        uint32 const heal = std::max<uint32>(1, lowest->GetMaxHealth() * ARMY_HEALER_HEAL_PCT / 100);
        lowest->ModifyHealth(int32(heal));
    }
};

class npc_lg_army_ghoul_tank : public CreatureScript
{
public:
    npc_lg_army_ghoul_tank() : CreatureScript("npc_lg_army_ghoul_tank") { }
    CreatureAI* GetAI(Creature* creature) const override { return new npc_lg_army_ghoulAI(creature); }
};

class npc_lg_army_ghoul_healer : public CreatureScript
{
public:
    npc_lg_army_ghoul_healer() : CreatureScript("npc_lg_army_ghoul_healer") { }
    CreatureAI* GetAI(Creature* creature) const override { return new npc_lg_army_ghoulAI(creature); }
};

class npc_lg_army_ghoul_dps : public CreatureScript
{
public:
    npc_lg_army_ghoul_dps() : CreatureScript("npc_lg_army_ghoul_dps") { }
    CreatureAI* GetAI(Creature* creature) const override { return new npc_lg_army_ghoulAI(creature); }
};

// -------------------------------------------------------------------------
// Shared "temp pet" AI: follow owner, attack owner's target, pet-aggressive
// self-defense, despawn if abandoned. Used for the Beast Mastery pack below
// -- unlike the Army ghouls this is attached directly via AIM_Initialize()
// on an already-existing creature entry (the hunter's own pet's entry), so
// it needs no creature_template/ScriptName row of its own.
// -------------------------------------------------------------------------
struct npc_lg_temp_petAI : public ScriptedAI
{
    npc_lg_temp_petAI(Creature* c) : ScriptedAI(c) { }

    void Reset() override
    {
        me->SetReactState(REACT_DEFENSIVE);
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
        if (!me->GetVictim())
        {
            if (Unit* attacker = me->getAttackerForHelper())
            {
                AttackStart(attacker);
                DoMeleeAttackIfReady();
                return;
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
                me->GetMotionMaster()->MoveFollow(owner, 4.0f, frand(0.0f, 2.0f * float(M_PI)));
            return;
        }
        if (me->GetVictim() != ownerTarget)
            AttackStart(ownerTarget);
        DoMeleeAttackIfReady();
    }
};

// -------------------------------------------------------------------------
// Hunter: Beast Mastery (910153)
// "The Beast Within (Bestial Wrath) has no cooldown/focus cost. Call up to
// 4 more beasts from your stable to fight alongside your pet, each at 50%
// of its stats."
//
// SIMPLIFICATION: "from your stable" is literal in the design discussion
// but reading the account's actual stabled pets (character_pet table,
// empty-stable edge case, cross-account rules) is a lot of extra surface
// for a first pass. This clones the hunter's currently active pet instead
// (same entry/model, just more of them) -- reads the same in practice
// ("more beasts show up") without touching pet-stable data at all.
// -------------------------------------------------------------------------
void SummonBmPack(Player* player)
{
    if (!player || !player->IsInWorld())
        return;
    Pet* pet = player->GetPet();
    if (!pet)
        return;
    uint32 const entry = pet->GetEntry();
    uint32 const guid = player->GetGUID().GetCounter();
    auto old = g_bmPack.find(guid);
    if (old != g_bmPack.end())
    {
        for (ObjectGuid const& g : old->second)
            if (Creature* c = ObjectAccessor::GetCreature(*player, g))
                c->DespawnOrUnsummon();
        old->second.clear();
    }
    std::vector<ObjectGuid>& pack = g_bmPack[guid];
    uint32 const hp = std::max<uint32>(1, pet->GetMaxHealth() / 2);
    for (uint32 i = 0; i < BM_PACK_SIZE; ++i)
    {
        Position p = player->GetPosition();
        p.RelocateOffset({ frand(-3.0f, 3.0f), frand(-3.0f, 3.0f), 0.0f, 0.0f });
        TempSummon* s = player->SummonCreature(entry, p, TEMPSUMMON_TIMED_DESPAWN, BM_PACK_DESPAWN_MS);
        if (!s)
            continue;
        s->SetOwnerGUID(player->GetGUID());
        s->SetFaction(player->GetFaction());
        s->SetLevel(player->GetLevel());
        s->SetMaxHealth(hp);
        s->SetHealth(hp);
        s->AIM_Initialize(new npc_lg_temp_petAI(s));
        pack.push_back(s->GetGUID());
    }
}

void TryHunterBmOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_HUNTER_BEAST_MASTERY)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || info->Id != SPELL_BESTIAL_WRATH)
        return;
    if (int32 const cost = spell->GetPowerCost())
        player->ModifyPower(POWER_FOCUS, cost);
    ClearCooldownAfterCast(player, SPELL_BESTIAL_WRATH, info->GetCategory());
    // Deferred for the same reason as the Army of the Dead group summon --
    // see TryDkUnholyOnCast below.
    ObjectGuid playerGuid = player->GetGUID();
    player->m_Events.AddEventAtOffset([playerGuid]()
    {
        if (Player* p = ObjectAccessor::FindPlayer(playerGuid))
            if (p->IsInWorld() && GetClassPerk(p) == SPELL_HUNTER_BEAST_MASTERY)
                SummonBmPack(p);
    }, std::chrono::milliseconds(1));
}

// -------------------------------------------------------------------------
// Hunter: Survival (910154)
// "Explosive Shot deals double damage. Traps lose their cooldown and get a
// bigger blast radius. You are immune to your own trap damage."
// -------------------------------------------------------------------------
// Explosive Shot does not deal its own damage. Its PERIODIC_DUMMY aura casts
// spell 53352 instead -- hardcoded in the core for every rank
// (SpellAuraEffects.cpp:5953) -- and 53352 is NOT in 53301's rank chain
// (spell_ranks: 53301 -> 60051, 60052, 60053). Matching only on RankOf meant
// this multiplier never saw a single damage event.
void ApplyHunterSurvivalExplosiveShotDamage(Unit* attacker, int32& damage, SpellInfo const* info)
{
    if (!attacker || damage <= 0 || !info)
        return;
    if (info->Id != SPELL_EXPLOSIVE_SHOT_DAMAGE && !RankOf(info, SPELL_EXPLOSIVE_SHOT_R1))
        return;
    Player* player = attacker->ToPlayer();
    if (!player || GetClassPerk(player) != SPELL_HUNTER_SURVIVAL)
        return;
    damage *= 2;
}

// Shrapnel: every Explosive Shot hit on a target banks its (doubled) damage
// and a stack -- 5 max, lifetime refreshed to 8s by every hit. The hit that
// arrives while the target already carries 5 stacks detonates instead:
// everything within 8 yards of the target takes damage equal to the five
// banked ticks, then the counter clears and the cycle restarts. Same
// rank/blast recognition as the x2 above (53352 is not in 53301's chain).
void ApplyHunterSurvivalShrapnel(Unit* attacker, Unit* victim, int32& damage, SpellInfo const* info)
{
    if (!attacker || !victim || damage <= 0 || !info)
        return;
    if (info->Id != SPELL_EXPLOSIVE_SHOT_DAMAGE && !RankOf(info, SPELL_EXPLOSIVE_SHOT_R1))
        return;
    Player* player = attacker->ToPlayer();
    if (!player || GetClassPerk(player) != SPELL_HUNTER_SURVIVAL)
        return;
    uint32 const playerGuid = player->GetGUID().GetCounter();
    uint32 const targetGuid = victim->GetGUID().GetCounter();
    HunterShrapnel& track = g_shrapnel[playerGuid][targetGuid];
    uint32 const now = uint32(GameTime::GetGameTime().count());
    if (now >= track.expireS)
    {
        track.stacks = 0;
        track.accDamage = 0;
    }
    if (track.stacks < SHRAPNEL_MAX_STACKS)
    {
        ++track.stacks;
        track.accDamage += damage;
        track.expireS = now + SHRAPNEL_LIFETIME_S;
        return;
    }
    // Detonate. Clear BEFORE dealing the blast so a reentrant Explosive Shot
    // tick can never read a half-consumed counter.
    int32 const blast = track.accDamage;
    g_shrapnel[playerGuid].erase(targetGuid);
    if (blast <= 0)
        return;
    ForEachHostileNear(player, victim, SHRAPNEL_BLAST_RANGE, [player, blast](Unit* target)
    {
        Unit::DealDamage(player, target, uint32(blast), nullptr, SPELL_DIRECT_DAMAGE,
            SPELL_SCHOOL_MASK_FIRE, nullptr, false);
    });
}

// Module tick for the persistent trap zones (2s cadence -- OnPlayerUpdate).
// Each zone re-casts its trap spell at the recorded position every 10s, max
// TRAP_ZONE_MAX_REARMS re-arms per placement, and only while at least one
// enemy stands inside the zone's (doubled) blast radius, so nothing churns
// in an empty room.
float HunterTrapZoneRadius(Player* player, SpellInfo const* info);
void TickHunterSurvivalTrapZones(Player* player, uint32 diff)
{
    if (!player || GetClassPerk(player) != SPELL_HUNTER_SURVIVAL)
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    auto it = g_trapZones.find(guid);
    if (it == g_trapZones.end() || it->second.empty())
        return;
    std::vector<HunterTrapZone>& zones = it->second;
    for (size_t i = 0; i < zones.size();)
    {
        HunterTrapZone& zone = zones[i];
        SpellInfo const* trapInfo = sSpellMgr->GetSpellInfo(zone.spellId);
        if (zone.mapId != player->GetMapId() || !trapInfo || zone.rearms >= TRAP_ZONE_MAX_REARMS)
        {
            zones.erase(zones.begin() + i);
            continue;
        }
        zone.rearmAcc += diff;
        if (zone.rearmAcc < TRAP_ZONE_REARM_MS)
        {
            ++i;
            continue;
        }
        zone.rearmAcc = 0;
        float const radius = HunterTrapZoneRadius(player, trapInfo);
        bool enemyInside = false;
        ForEachHostileNear(player, player,
            player->GetDistance(zone.x, zone.y, zone.z) + radius,
            [&zone, radius, &enemyInside](Unit* target)
        {
            if (!enemyInside && target->IsWithinDist2d(zone.x, zone.y, radius))
                enemyInside = true;
        });
        if (!enemyInside)
        {
            ++i;
            continue;
        }
        // Triggered cast: no GCD, no focus, no cooldown bookkeeping, and the
        // cast hook above never sees it.
        player->CastSpell(zone.x, zone.y, zone.z, zone.spellId, true);
        ++zone.rearms;
        ++i;
    }
    if (zones.empty())
        g_trapZones.erase(guid);
}

bool IsHunterTrapSpell(SpellInfo const* info)
{
    if (!info)
        return false;
    if (info->Id == SPELL_SNAKE_TRAP)
        return true;
    for (uint32 id : SPELL_HUNTER_TRAP_FIRST_RANKS)
        if (RankOf(info, id))
            return true;
    return false;
}

// The (doubled) blast radius of a trap spell, used both to size the persistent
// zone's "is anyone standing in it" check and to bound the tick's search.
float HunterTrapZoneRadius(Player* player, SpellInfo const* info)
{
    float radius = 0.0f;
    if (!player || !info)
        return radius;
    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        if (info->Effects[i].IsEffect() && info->Effects[i].HasRadius())
            radius = std::max(radius, info->Effects[i].CalcRadius(player));
    // Matches the perk's own x2 radius modifier (ApplyHunterSurvivalTrapRadius).
    return std::max(5.0f, radius * HUNTER_TRAP_RADIUS_MULT);
}

void RecordHunterTrapZone(Player* player, SpellInfo const* info)
{
    uint32 const guid = player->GetGUID().GetCounter();
    std::vector<HunterTrapZone>& zones = g_trapZones[guid];
    // Hard cap: the oldest placement is dropped first once the hunter is
    // holding TRAP_ZONE_MAX_LIVE zones.
    if (zones.size() >= TRAP_ZONE_MAX_LIVE)
        zones.erase(zones.begin());
    HunterTrapZone zone;
    zone.spellId = info->Id;
    zone.mapId = player->GetMapId();
    zone.x = player->GetPositionX();
    zone.y = player->GetPositionY();
    zone.z = player->GetPositionZ();
    zone.rearmAcc = 0;
    zone.rearms = 0;
    zones.push_back(zone);
}

// -------------------------------------------------------------------------
// Call of the Wilds bear: tank-flavored temp pet. Attached via
// AIM_Initialize() on the hunter's pet's own entry (or the fallback bear
// entry when there is no pet), so it needs no creature_template/ScriptName
// row of its own -- same shape as npc_lg_temp_petAI above.
// -------------------------------------------------------------------------
struct npc_lg_wilds_bearAI : public ScriptedAI
{
    npc_lg_wilds_bearAI(Creature* c) : ScriptedAI(c) { }

    void Reset() override
    {
        me->SetReactState(REACT_DEFENSIVE);
        _threatTickMs = 0;
    }

    void UpdateAI(uint32 diff) override
    {
        Unit* ownerUnit = me->GetOwner();
        Player* owner = ownerUnit ? ownerUnit->ToPlayer() : nullptr;
        if (!owner || !owner->IsInWorld() || !me->IsWithinDistInMap(owner, 60.0f))
        {
            me->DespawnOrUnsummon();
            return;
        }

        if (!me->GetVictim())
        {
            if (Unit* attacker = me->getAttackerForHelper())
            {
                AttackStart(attacker);
                HoldThreat(attacker);
                DoMeleeAttackIfReady();
                return;
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
                me->GetMotionMaster()->MoveFollow(owner, 3.0f, frand(0.0f, 2.0f * float(M_PI)));
            return;
        }

        if (me->GetVictim() != ownerTarget)
            AttackStart(ownerTarget);
        HoldThreat(ownerTarget);
        // The spawn Taunt is one-off and threat keeps climbing from dots and
        // heals after it, so re-assert top threat on whatever we are tanking.
        if (_threatTickMs > diff)
            _threatTickMs -= diff;
        else
        {
            _threatTickMs = WILDS_BEAR_THREAT_TICK_MS;
            if (Unit* victim = me->GetVictim())
                HoldThreat(victim);
        }
        DoMeleeAttackIfReady();
    }

private:
    uint32 _threatTickMs = 0;

    // Same raw-threat push the Army tank ghoul uses -- not a real Taunt cast,
    // which bosses can resist or ignore.
    void HoldThreat(Unit* target)
    {
        if (target && target->IsAlive())
            target->GetThreatMgr().AddThreat(me, 500.0f, nullptr, true, true);
    }
};

void SummonWildsBears(Player* player)
{
    if (!player || !player->IsInWorld())
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    // Refresh per cast: despawn any previous pair still standing rather than
    // letting pairs pile up -- same one-slot-per-owner rule as g_bmPack.
    auto old = g_wildsBears.find(guid);
    if (old != g_wildsBears.end())
    {
        for (ObjectGuid const& g : old->second)
            if (Creature* c = ObjectAccessor::GetCreature(*player, g))
                c->DespawnOrUnsummon();
        old->second.clear();
    }
    Pet* pet = player->GetPet();
    uint32 const entry = pet ? pet->GetEntry() : NPC_WILDS_FALLBACK_BEAR;
    uint32 const hp = std::max<uint32>(1, (pet ? pet->GetMaxHealth() : player->GetMaxHealth()) / 2);
    std::vector<ObjectGuid>& bears = g_wildsBears[guid];
    for (uint32 i = 0; i < WILDS_BEAR_COUNT; ++i)
    {
        Position p = player->GetPosition();
        p.RelocateOffset({ frand(-3.0f, 3.0f), frand(-3.0f, 3.0f), 0.0f, 0.0f });
        TempSummon* s = player->SummonCreature(entry, p, TEMPSUMMON_TIMED_DESPAWN, WILDS_BEAR_DESPAWN_MS);
        if (!s)
            continue;
        s->SetOwnerGUID(player->GetGUID());
        s->SetFaction(player->GetFaction());
        s->SetLevel(player->GetLevel());
        s->SetMaxHealth(hp);
        s->SetHealth(hp);
        s->AIM_Initialize(new npc_lg_wilds_bearAI(s));
        bears.push_back(s->GetGUID());
        // Taunt on spawn: a real Taunt cast at everything nearby, backed by
        // the raw threat push so a resisted/immune Taunt never leaves the
        // pair standing idle.
        ForEachHostileInRange(player, WILDS_BEAR_TAUNT_RANGE, [s](Unit* target)
        {
            if (!target || !target->IsAlive())
                return;
            s->CastSpell(target, SPELL_TAUNT, true);
            target->GetThreatMgr().AddThreat(s, 1000.0f, nullptr, true, true);
        });
    }
}

void TryHunterSurvivalOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_HUNTER_SURVIVAL)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info)
        return;
    if (info->Id == SPELL_CALL_OF_WILDS)
    {
        // Deferred for the same reentrancy reason as the BM pack summon --
        // see TryHunterBmOnCast above.
        ObjectGuid playerGuid = player->GetGUID();
        player->m_Events.AddEventAtOffset([playerGuid]()
        {
            if (Player* p = ObjectAccessor::FindPlayer(playerGuid))
                if (p->IsInWorld() && GetClassPerk(p) == SPELL_HUNTER_SURVIVAL)
                    SummonWildsBears(p);
        }, std::chrono::milliseconds(1));
        return;
    }
    if (!IsHunterTrapSpell(info))
        return;
    ClearCooldownAfterCast(player, info->Id, info->GetCategory());
    // Persistent zone: remember where this trap went down so the module tick
    // can re-cast the trap at the same spot. Re-arms are triggered casts, so
    // they never land here (OnPlayerSpellCast skips triggered spells).
    RecordHunterTrapZone(player, info);
}

void ApplyHunterSurvivalTrapRadius(Spell* spell, Player* player, SpellInfo const* info)
{
    if (!spell || !player || GetClassPerk(player) != SPELL_HUNTER_SURVIVAL || !IsHunterTrapSpell(info))
        return;
    spell->SetSpellValue(SPELLVALUE_RADIUS_MOD, int32(HUNTER_TRAP_RADIUS_MOD));
}

// Own-trap self-damage immunity: traps attribute their explosion damage to
// the hunter who set them, so a self-hit shows up here as attacker==victim.
// Traps deal their damage as spell damage, never melee, so this only needs
// the int32 spell-damage-taken shape.
void ApplyHunterSurvivalTrapImmunity(Unit* attacker, Unit* victim, int32& damage, SpellInfo const* info)
{
    if (!attacker || attacker != victim || damage <= 0 || !IsHunterTrapSpell(info))
        return;
    Player* player = attacker->ToPlayer();
    if (player && GetClassPerk(player) == SPELL_HUNTER_SURVIVAL)
        damage = 0;
}

// -------------------------------------------------------------------------
// Shaman: Enhancement (910155) -- "The Storm Caller"
// "Feral Spirit is a free toggle: your 2 spirit wolves never expire while
// it's active and deal double damage, and 25% of their strikes call down a
// free Lightning Bolt on the target. Stormstrike has no cooldown and marks
// its target Charged for 8 sec; Lightning Bolt and Chain Lightning on a
// Charged target echo a second bolt at 50% damage and refresh the charge.
// Static Field is a free toggle: a 10-yard storm circle follows you,
// shocking everything inside every 2 sec for nature damage scaled with
// attack power."
// -------------------------------------------------------------------------

// Stormstrike marks: char guid -> target low guid -> expiry (ms timer).
// Expired entries are pruned whenever a new mark lands.
void MarkShamanTargetCharged(Player* player, Unit* target)
{
    if (!player || !target)
        return;
    uint32 const now = getMSTime();
    auto& charges = g_shamanCharged[player->GetGUID().GetCounter()];
    for (auto itr = charges.begin(); itr != charges.end();)
    {
        if (itr->second <= now)
            itr = charges.erase(itr);
        else
            ++itr;
    }
    charges[target->GetGUID().GetCounter()] = now + SHAMAN_CHARGE_DURATION_MS;
}

bool IsTargetCharged(Player* player, Unit* target)
{
    if (!player || !target)
        return false;
    auto const itr = g_shamanCharged.find(player->GetGUID().GetCounter());
    if (itr == g_shamanCharged.end())
        return false;
    uint32 const now = getMSTime();
    auto const exp = itr->second.find(target->GetGUID().GetCounter());
    return exp != itr->second.end() && exp->second > now;
}

// Static Field is ON while the shaman's OWN Thorns aura is up. Own-cast only:
// a Thorns buff from a friendly druid must never switch the field on.
Aura* GetOwnStaticFieldAura(Player* player)
{
    if (!player)
        return nullptr;
    for (uint32 id = SPELL_THORNS_R1; id; id = sSpellMgr->GetNextSpellInChain(id))
        if (Aura* aura = player->GetAura(id))
            if (aura->GetCasterGUID() == player->GetGUID())
                return aura;
    return nullptr;
}

void TryShamanEnhOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_SHAMAN_ENHANCEMENT)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info)
        return;
    if (info->Id == SPELL_FERAL_SPIRIT)
    {
        if (int32 const cost = spell->GetPowerCost())
            player->ModifyPower(POWER_MANA, cost);
        ClearCooldownAfterCast(player, SPELL_FERAL_SPIRIT, info->GetCategory());
        return;
    }
    if (info->Id == SPELL_STORMSTRIKE)
    {
        ClearCooldownAfterCast(player, SPELL_STORMSTRIKE, info->GetCategory());
        if (Unit* target = spell->m_targets.GetUnitTarget())
            MarkShamanTargetCharged(player, target);
        return;
    }
    // Static Field toggle ON: Thorns casts free and off cooldown; the aura
    // applying IS the on-state (GetOwnStaticFieldAura observes it).
    if (RankOf(info, SPELL_THORNS_R1))
    {
        if (int32 const cost = spell->GetPowerCost())
            player->ModifyPower(POWER_MANA, cost);
        ClearCooldownAfterCast(player, info->Id, info->GetCategory());
        return;
    }
    // Charged bolt echo.
    if (!RankOf(info, SPELL_LIGHTNING_BOLT_R1) && !RankOf(info, SPELL_CHAIN_LIGHTNING_R1))
        return;
    Unit* target = spell->m_targets.GetUnitTarget();
    if (!target || !IsTargetCharged(player, target))
        return;
    // Same rank as the hand cast. School Damage is effect 0 on both chains and
    // a DieSides-1 effect's calculated value is BasePoints + 1, so the echo's
    // BASE_POINT0 override is (BasePoints + 1) * 50%. The echo is a TRIGGERED
    // cast and OnPlayerSpellCast early-returns on those, so it can never reach
    // this code again -- that is the recursion protection.
    int32 const bp = int32(float(info->Effects[0].BasePoints + 1) * SHAMAN_ECHO_PCT);
    ObjectGuid playerGuid = player->GetGUID();
    ObjectGuid targetGuid = target->GetGUID();
    uint32 const boltRank = info->Id;
    // Deferred like the BM pack: this hook fires midway through Spell::cast().
    player->m_Events.AddEventAtOffset([playerGuid, targetGuid, boltRank, bp]()
    {
        Player* p = ObjectAccessor::FindPlayer(playerGuid);
        if (!p || !p->IsInWorld() || GetClassPerk(p) != SPELL_SHAMAN_ENHANCEMENT)
            return;
        Unit* victim = ObjectAccessor::GetUnit(*p, targetGuid);
        if (!victim || !victim->IsAlive() || !p->IsValidAttackTarget(victim))
            return;
        p->CastCustomSpell(victim, boltRank, &bp, nullptr, nullptr, true);
        // The echo refreshes the charge.
        MarkShamanTargetCharged(p, victim);
    }, std::chrono::milliseconds(1));
}

void TickShamanEnhWolves(Player* player)
{
    if (!player || GetClassPerk(player) != SPELL_SHAMAN_ENHANCEMENT)
        return;
    if (!player->HasAura(SPELL_FERAL_SPIRIT))
        return;
    if (Aura* aura = player->GetAura(SPELL_FERAL_SPIRIT))
        if (aura->GetDuration() >= 0 && aura->GetDuration() < 10000)
            aura->SetDuration(45000);
}

// Static Field heartbeat: while the field is on, everything within 10 yards
// takes a nature shock every 2 sec scaled with the shaman's attack power
// (same AP access as the core's weapon-damage math). The field is
// player-centered, so unlike Consecration there is no dynobject to relocate
// on a cadence -- the circle follows by construction. Thorns is a 10-minute
// aura, so unlike the wolf refresh nothing expires mid-fight, but the
// duration is topped up anyway so the toggle never silently dies.
void TickShamanEnhStaticField(Player* player, TickState& st, uint32 diff)
{
    if (!player || GetClassPerk(player) != SPELL_SHAMAN_ENHANCEMENT || !player->IsAlive())
        return;
    Aura* field = GetOwnStaticFieldAura(player);
    if (!field)
        return; // Static Field toggled off
    if (field->GetDuration() >= 0 && field->GetDuration() < 60000)
        field->SetDuration(600000);
    st.acc += diff;
    if (st.acc < SHAMAN_STATIC_FIELD_TICK_MS)
        return;
    st.acc = 0;
    uint32 const damage = uint32(float(player->GetTotalAttackPowerValue(BASE_ATTACK)) * SHAMAN_STATIC_FIELD_AP_PCT);
    if (!damage)
        return;
    ForEachHostileInRange(player, SHAMAN_STATIC_FIELD_RANGE, [player, damage](Unit* unit)
    {
        if (!unit || !unit->IsAlive() || !player->IsValidAttackTarget(unit))
            return;
        Unit::DealDamage(player, unit, damage, nullptr, SPELL_DIRECT_DAMAGE,
            SPELL_SCHOOL_MASK_NATURE, nullptr, false);
    });
}

// The off-switch, in CheckCast's strict pass (same ordering argument as the
// Starfall toggle): recasting Thorns while the field is up switches it off
// and the cast never starts.
void TryShamanEnhStaticFieldToggleOff(Spell* spell, bool strict, SpellCastResult& res)
{
    if (!strict || res != SPELL_CAST_OK || !spell)
        return;
    Unit* caster = spell->GetCaster();
    Player* player = caster ? caster->ToPlayer() : nullptr;
    if (!player || GetClassPerk(player) != SPELL_SHAMAN_ENHANCEMENT)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || !RankOf(info, SPELL_THORNS_R1))
        return;
    Aura* field = GetOwnStaticFieldAura(player);
    if (!field)
        return;                     // not running -> let the cast through, toggle ON
    player->RemoveAurasDueToSpell(field->GetId());
    res = SPELL_FAILED_DONT_REPORT; // toggle OFF, and the cast never happens
}

bool IsOwnedSpiritWolf(Unit* unit, uint32& ownerGuidOut)
{
    if (!unit || unit->GetEntry() != NPC_SPIRIT_WOLF)
        return false;
    Unit* owner = unit->GetOwner();
    if (!owner)
        return false;
    ownerGuidOut = owner->GetGUID().GetCounter();
    return true;
}

void ApplyShamanEnhWolfMeleeDamage(Unit* attacker, uint32& damage)
{
    uint32 ownerGuid = 0;
    if (!IsOwnedSpiritWolf(attacker, ownerGuid))
        return;
    Player* owner = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(ownerGuid));
    if (owner && GetClassPerk(owner) == SPELL_SHAMAN_ENHANCEMENT)
    {
        damage *= 2;
        // "each wolf strike has 25% to call a mini lightning strike on its
        // target" -- the shaman fires a free triggered Lightning Bolt (best
        // owned rank) at the wolf's victim. Deferred into the owner's event
        // queue: casting synchronously from inside a melee-damage hook is the
        // reentry the Starfall proc comment warns about.
        if (roll_chance_i(SHAMAN_WOLF_BOLT_CHANCE))
        {
            Unit* victim = attacker->GetVictim();
            if (victim && victim->IsAlive() && owner->IsValidAttackTarget(victim))
            {
                uint32 const bolt = BestOwnedOrFirst(owner, SPELL_LIGHTNING_BOLT_R1);
                ObjectGuid ownerGuidObj = owner->GetGUID();
                ObjectGuid victimGuid = victim->GetGUID();
                owner->m_Events.AddEventAtOffset([ownerGuidObj, victimGuid, bolt]()
                {
                    Player* p = ObjectAccessor::FindPlayer(ownerGuidObj);
                    if (!p || !p->IsInWorld() || GetClassPerk(p) != SPELL_SHAMAN_ENHANCEMENT)
                        return;
                    Unit* target = ObjectAccessor::GetUnit(*p, victimGuid);
                    if (!target || !target->IsAlive() || !p->IsValidAttackTarget(target))
                        return;
                    p->CastSpell(target, bolt, true);
                }, std::chrono::milliseconds(SHAMAN_WOLF_BOLT_DELAY_MS));
            }
        }
    }
}

// -------------------------------------------------------------------------
// Shaman: Restoration (910156)
// "Riptide has no cooldown and its HoT also jumps to 2 more injured allies
// within 15 yards. Chain Heal has no bounce cap."
// -------------------------------------------------------------------------
void TryShamanRestOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_SHAMAN_RESTORATION)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    // Riptide is rank 1 of 4; an exact match skipped every higher rank.
    if (!info || !RankOf(info, SPELL_RIPTIDE))
        return;
    // info->Id, not SPELL_RIPTIDE: the player may be casting a higher rank,
    // and clearing rank 1's cooldown entry would leave theirs untouched.
    ClearCooldownAfterCast(player, info->Id, info->GetCategory());
    Unit* target = spell->m_targets.GetUnitTarget();
    if (!target)
        return;
    uint32 spread = 0;
    ForEachInjuredAllyNear(player, target, SHAMAN_RIPTIDE_SPREAD_RANGE, [&](Unit* ally)
    {
        if (spread >= SHAMAN_RIPTIDE_SPREAD_COUNT || ally == target || ally->HasAura(SPELL_RIPTIDE, player->GetGUID()))
            return;
        player->CastSpell(ally, BestOwnedOrFirst(player, SPELL_RIPTIDE), true);
        ++spread;
    });
}

void ApplyShamanRestChainHealTargets(Spell* spell, Player* player, SpellInfo const* info)
{
    if (!spell || !player || !info || GetClassPerk(player) != SPELL_SHAMAN_RESTORATION)
        return;
    if (RankOf(info, SPELL_CHAIN_HEAL_R1))
        spell->SetSpellValue(SPELLVALUE_MAX_TARGETS, CHAIN_LIGHTNING_MAX_TARGETS);
}

// -------------------------------------------------------------------------
// Warlock: Affliction (910157)
// "Haunt is instant, has no cooldown, and casting it applies Unstable
// Affliction, Corruption, Curse of Agony and Curse of the Elements to the
// target. Your DoTs spread from every infected enemy to others within 15
// yards every 1 sec. DoT tick damage is increased by your haste and deals
// quadruple damage (duration is not shortened)."
//
// CONTAGIOUS, not a burst around your target. The first version read the DoTs
// on your selected target and applied them to that target's neighbours, and
// that was the whole of it: a newly infected enemy never became a carrier, so
// the effect was permanently a fixed 15-yard ring around whatever you happened
// to be looking at. Every carrier is a source now, so the infection creeps
// outward a hop per second for as long as there are enemies to reach -- which
// is the spec fantasy, and is deliberately wide.
//
// Cost is held down by doing ONE grid search per tick rather than one per
// carrier: everything hostile within CONTAGION_RANGE is fetched once, split
// into carriers and clean, and the hop test after that is plain distance
// arithmetic. Casts are capped per tick as a backstop.
// -------------------------------------------------------------------------
void TickWarlockAffliction(Player* player, TickState& st, uint32 diff)
{
    if (!player || GetClassPerk(player) != SPELL_WARLOCK_AFFLICTION || !player->IsAlive() || !player->IsInCombat())
        return;
    st.acc += diff;
    if (st.acc < WARLOCK_AFFLICTION_SPREAD_TICK_MS)
        return;
    st.acc = 0;
    // Spellcasters commonly have a selected hostile target without an
    // auto-attack victim. GetVictim() is the melee/attack target, so using it
    // alone made the perk silently do nothing after casting Corruption unless
    // the warlock also started wanding or meleeing (report #96).
    //
    // It is only a SEED now, not a requirement. Once the infection is running
    // it feeds itself off whatever is already carrying DoTs, so it must keep
    // creeping while you tab off, switch targets, or the original victim dies.
    // Bailing here would have stopped the plague dead the moment you looked away.
    Unit* source = player->GetSelectedUnit();
    if (!source || !player->IsValidAttackTarget(source))
        source = player->GetVictim();

    uint32 const guid = player->GetGUID().GetCounter();
    if (!g_reentryGuard.insert(guid).second)
        return;

    // The player's own warlock DoTs currently on a unit, or empty if it is clean.
    auto dotsOn = [player](Unit* unit)
    {
        std::vector<uint32> ids;
        Unit::AuraApplicationMap const& auras = unit->GetAppliedAuras();
        for (Unit::AuraApplicationMap::const_iterator itr = auras.begin(); itr != auras.end(); ++itr)
        {
            AuraApplication* aa = itr->second;
            Aura* aura = aa ? aa->GetBase() : nullptr;
            if (!aura || aura->GetCasterGUID() != player->GetGUID())
                continue;
            SpellInfo const* dotInfo = aura->GetSpellInfo();
            if (dotInfo && dotInfo->SpellFamilyName == SPELLFAMILY_WARLOCK && dotInfo->HasAura(SPELL_AURA_PERIODIC_DAMAGE))
                ids.push_back(dotInfo->Id);
        }
        return ids;
    };

    struct Carrier
    {
        Unit* unit;
        std::vector<uint32> dots;
    };
    std::vector<Carrier> carriers;
    std::vector<Unit*> clean;

    ForEachHostileInRange(player, WARLOCK_AFFLICTION_CONTAGION_RANGE, [&](Unit* unit)
    {
        std::vector<uint32> ids = dotsOn(unit);
        if (ids.empty())
            clean.push_back(unit);
        else
            carriers.push_back({unit, std::move(ids)});
    });

    // The selected target seeds the very first hop. It is normally found by the
    // search above, but keep it explicit: report #96 was this perk silently
    // doing nothing because the one unit that mattered was not being looked at.
    if (source && std::find_if(carriers.begin(), carriers.end(),
            [source](Carrier const& c) { return c.unit == source; }) == carriers.end())
    {
        std::vector<uint32> ids = dotsOn(source);
        if (!ids.empty())
            carriers.push_back({source, std::move(ids)});
    }

    uint32 spread = 0;
    for (Unit* target : clean)
    {
        if (spread >= WARLOCK_AFFLICTION_MAX_SPREADS_PER_TICK)
            break;
        for (Carrier const& carrier : carriers)
        {
            if (carrier.unit == target || !target->IsWithinDist(carrier.unit, WARLOCK_AFFLICTION_SPREAD_RANGE))
                continue;
            for (uint32 id : carrier.dots)
                if (!target->HasAura(id, player->GetGUID()))
                {
                    ++spread;
                    // Report #156: the new infection is cast BY the carrier so
                    // the plague visibly creeps mob to mob instead of streaming
                    // out of the warlock. originalCaster stays the warlock --
                    // the aura must keep the player's GUID or dotsOn() stops
                    // seeing it as a carrier next tick and the haste/damage
                    // multiplier (which keys off the owning player) falls off.
                    carrier.unit->CastSpell(target, id, true, nullptr, nullptr,
                        player->GetGUID());
                }
        }
    }

    // Report #96: "Warlock Affliction perk not working, dots are not spreading."
    // The tick loop read correct on inspection, so count what actually happens
    // per tick instead of guessing again -- same instrumentation shape as the
    // shadowstep pickpocket and blizzard counters.
    LOG_DEBUG("module.livinggear", "affliction spread: {} carrier(s), {} clean, {} cast(s) this tick",
        carriers.size(), clean.size(), spread);
    g_reentryGuard.erase(guid);
}

void ApplyWarlockAfflictionHaste(Unit* target, Unit* attacker, uint32& damage, SpellInfo const* info)
{
    if (!target || !attacker || !damage || !info || info->SpellFamilyName != SPELLFAMILY_WARLOCK)
        return;
    // The hook fires for every periodic aura tick; report #106 is about DoTs
    // only, so require an actual periodic-damage effect.
    if (!info->HasAura(SPELL_AURA_PERIODIC_DAMAGE))
        return;
    Player* player = attacker->ToPlayer();
    if (!player || GetClassPerk(player) != SPELL_WARLOCK_AFFLICTION)
        return;
    float const haste = player->GetRatingBonusValue(CR_HASTE_SPELL);
    float const mult = WARLOCK_AFFLICTION_DOT_MULT * (1.0f + (haste > 0.0f ? haste / 100.0f : 0.0f));
    damage = uint32(float(damage) * mult);
}

// Reports #105/#116: Haunt is the Affliction perk's button. Three things
// happen on cast: the cooldown is cleared (deferred -- the hook fires before
// the engine writes its own cooldown entry, see ClearCooldownAfterCast), the
// cast is instant (LivingGear_SpellIsInstantCast is consulted from
// Spell::prepare via core-patch 0032), and the target is seeded with the
// full DoT set from the best rank the warlock owns.
void TryWarlockAfflictionOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_WARLOCK_AFFLICTION)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || !RankOf(info, SPELL_HAUNT))
        return;
    ClearCooldownAfterCast(player, info->Id, info->GetCategory());
    Unit* target = spell->m_targets.GetUnitTarget();
    if (!target)
        return;
    // Report #123: UA comes off CLASS_PERK_GRANTS now (it is a talent, not a
    // trainer spell), so BestOwnedOrFirst hands over rank 1 -- the perk grant
    // then records it in lg_char_class_grant and BestOwned finds the best rank
    // from the next cast on. Same shape as the hunter stings and the druid
    // Insect Swarm, which also cast granted/baseline spells by chain head.
    for (uint32 const firstId : { SPELL_UNSTABLE_AFFLICTION_R1, SPELL_CORRUPTION_R1,
                                  SPELL_CURSE_OF_AGONY_R1, SPELL_CURSE_OF_ELEMENTS_R1 })
    {
        uint32 const best = firstId == SPELL_UNSTABLE_AFFLICTION_R1
            ? BestOwnedOrFirst(player, firstId)
            : BestOwned(player, firstId);
        if (best)
            player->CastSpell(target, best, true);
    }
}

// -------------------------------------------------------------------------
// Warlock: Demonology (910158) -- "The Imp Lord"
// "Metamorphosis has no cooldown or shard cost. Your demon pet's damage is
// doubled. Fel Domination is now your Imp Legion toggle: summon 8 imps
// cloned from your own imp at 35% health for 30 sec; recast to refresh,
// recast again to dismiss. Metamorphosis upgrades the pack to felguards
// until it fades."
//
// SIMPLIFICATION: "auto-resummons if it dies" was dropped -- there's no
// cheap way to know which summon spell produced the current pet (Imp vs
// Voidwalker vs Succubus vs Felhunter vs Felguard) without tracking every
// summon cast, and getting that wrong would resummon the wrong demon. Not
// worth it for one perk's flavor clause.
//
// The doubling on the MAIN pet stays unconditional (not gated on the legion
// being up) -- the doubling predates the pack and gating it would have
// nerfed the perk's core clause.
// -------------------------------------------------------------------------
struct npc_lg_demo_legionAI : public ScriptedAI
{
    npc_lg_demo_legionAI(Creature* c, bool castFirebolt) : ScriptedAI(c),
        _castFirebolt(castFirebolt), _boltAcc(urand(500, DEMO_IMP_FIREBOLT_COOLDOWN_MS)) { }

    void Reset() override
    {
        me->SetReactState(REACT_DEFENSIVE);
    }

    void UpdateAI(uint32 diff) override
    {
        Unit* ownerUnit = me->GetOwner();
        Player* owner = ownerUnit ? ownerUnit->ToPlayer() : nullptr;
        if (!owner || !owner->IsInWorld() || !me->IsWithinDistInMap(owner, 60.0f))
        {
            me->DespawnOrUnsummon();
            return;
        }

        // Imp firebolt: data-driven, same store the core's own pet spell
        // loading uses (Pet::InitLevelupSpellsForLevel -> SpellMgr's
        // SkillLineAbility map for CREATURE_FAMILY_IMP). rbegin() is the
        // highest SpellLevel entry, i.e. the level-80 rank. Felguard form
        // melee-only.
        if (_castFirebolt && _boltAcc < DEMO_IMP_FIREBOLT_COOLDOWN_MS)
            _boltAcc += diff;

        if (_castFirebolt && _boltAcc >= DEMO_IMP_FIREBOLT_COOLDOWN_MS
            && me->GetVictim() && !me->HasUnitState(UNIT_STATE_CASTING)
            && me->IsWithinDistInMap(me->GetVictim(), 30.0f))
        {
            if (PetLevelupSpellSet const* bolts = sSpellMgr->GetPetLevelupSpellList(CREATURE_FAMILY_IMP))
            {
                if (!bolts->empty())
                {
                    _boltAcc = 0;
                    me->CastSpell(me->GetVictim(), bolts->rbegin()->second, true);
                }
            }
        }

        if (!me->GetVictim())
        {
            if (Unit* attacker = me->getAttackerForHelper())
            {
                AttackStart(attacker);
                DoMeleeAttackIfReady();
                return;
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
                me->GetMotionMaster()->MoveFollow(owner, 4.0f, frand(0.0f, 2.0f * float(M_PI)));
            return;
        }
        if (me->GetVictim() != ownerTarget)
            AttackStart(ownerTarget);
        DoMeleeAttackIfReady();
    }

private:
    bool _castFirebolt;
    uint32 _boltAcc;
};

void DespawnWarlockDemoLegion(Player* player)
{
    auto itr = g_demoLegion.find(player->GetGUID().GetCounter());
    if (itr == g_demoLegion.end())
        return;
    for (ObjectGuid const& g : itr->second.pack)
        if (Creature* c = ObjectAccessor::GetCreature(*player, g))
            c->DespawnOrUnsummon();
    itr->second.pack.clear();
    itr->second.metaForm = false;
}

void SummonWarlockDemoLegion(Player* player, bool meta)
{
    if (!player || !player->IsInWorld())
        return;
    DespawnWarlockDemoLegion(player);
    WarlockDemoLegion& state = g_demoLegion[player->GetGUID().GetCounter()];
    // Clone the player's own imp entry when they actually have one up;
    // otherwise the default warlock imp summon entry (688 -> 416, verified
    // in the DBC -- see the comment at g_demoLegion).
    Pet* pet = player->GetPet();
    uint32 const entry = meta ? DEMO_FELGUARD_ENTRY
        : (pet && pet->GetEntry() == DEMO_IMP_ENTRY) ? pet->GetEntry() : DEMO_IMP_ENTRY;
    for (uint32 i = 0; i < DEMO_LEGION_SIZE; ++i)
    {
        Position p = player->GetPosition();
        p.RelocateOffset({ frand(-3.0f, 3.0f), frand(-3.0f, 3.0f), 0.0f, 0.0f });
        TempSummon* s = player->SummonCreature(entry, p, TEMPSUMMON_TIMED_DESPAWN, DEMO_LEGION_DESPAWN_MS);
        if (!s)
            continue;
        s->SetOwnerGUID(player->GetGUID());
        s->SetFaction(player->GetFaction());
        s->SetLevel(player->GetLevel());
        uint32 const base = std::max<uint32>(s->GetMaxHealth(), pet ? pet->GetMaxHealth() : 0);
        uint32 const hp = std::max<uint32>(1, base * DEMO_LEGION_HP_PCT / 100);
        s->SetMaxHealth(hp);
        s->SetHealth(hp);
        s->AIM_Initialize(new npc_lg_demo_legionAI(s, !meta));
        state.pack.push_back(s->GetGUID());
    }
    state.metaForm = meta;
}

// Prune dead members and observe Meta's aura. Runs from the per-player
// update tick (see TickWarriorArmsBladestorm for why the tick observes and
// never interferes with an aura's lifetime): when Meta drops, the felguard
// pack is re-summoned as imps. No aura is removed here, so none of the
// CheckCast-after-removal reentrancy traps are reachable.
void TickWarlockDemo(Player* player)
{
    if (!player || GetClassPerk(player) != SPELL_WARLOCK_DEMONOLOGY)
        return;
    WarlockDemoLegion& state = g_demoLegion[player->GetGUID().GetCounter()];
    std::erase_if(state.pack, [player](ObjectGuid const& g)
    {
        Creature const* c = ObjectAccessor::GetCreature(*player, g);
        return !c || !c->IsAlive();
    });
    if (state.metaForm && !player->HasAura(SPELL_METAMORPHOSIS))
    {
        state.metaForm = false;
        if (!state.pack.empty())
            SummonWarlockDemoLegion(player, false);
    }
}

// Toggle: Fel Domination (18708) summons/refreshes the legion when it is
// down; when it is up, the STRICT OnSpellCheckCast pass despawns it and
// kills the cast (same shape as TryWarriorArmsBladestormToggleOff).
void TryWarlockDemoLegionToggleOff(Spell* spell, bool strict, SpellCastResult& res)
{
    if (!strict || res != SPELL_CAST_OK || !spell)
        return;
    Unit* caster = spell->GetCaster();
    Player* player = caster ? caster->ToPlayer() : nullptr;
    if (!player || GetClassPerk(player) != SPELL_WARLOCK_DEMONOLOGY)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || info->Id != SPELL_FEL_DOMINATION)
        return;
    auto itr = g_demoLegion.find(player->GetGUID().GetCounter());
    if (itr == g_demoLegion.end() || itr->second.pack.empty())
        return;                     // no legion up -> let the cast through, toggle ON
    DespawnWarlockDemoLegion(player);
    res = SPELL_FAILED_DONT_REPORT; // toggle OFF, and the cast never happens
}

void TryWarlockDemoOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_WARLOCK_DEMONOLOGY)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info)
        return;
    if (info->Id == SPELL_FEL_DOMINATION)
    {
        if (int32 const cost = spell->GetPowerCost())
            player->ModifyPower(POWER_MANA, cost);
        ClearCooldownAfterCast(player, SPELL_FEL_DOMINATION, info->GetCategory());
        // It is a pure toggle here -- strip Fel Domination's native
        // instant-next-summon buff so the button reads as one thing.
        player->RemoveAurasDueToSpell(SPELL_FEL_DOMINATION);
        // Deferred for the same reason as the BM pack -- see TryHunterBmOnCast.
        ObjectGuid playerGuid = player->GetGUID();
        player->m_Events.AddEventAtOffset([playerGuid]()
        {
            if (Player* p = ObjectAccessor::FindPlayer(playerGuid))
                if (p->IsInWorld() && GetClassPerk(p) == SPELL_WARLOCK_DEMONOLOGY)
                    SummonWarlockDemoLegion(p, false);
        }, std::chrono::milliseconds(1));
        return;
    }
    if (info->Id != SPELL_METAMORPHOSIS)
        return;
    if (int32 const cost = spell->GetPowerCost())
        player->ModifyPower(POWER_MANA, cost);
    ClearCooldownAfterCast(player, SPELL_METAMORPHOSIS, info->GetCategory());
    // Meta upgrades an alive legion to felguards for its duration; the
    // revert runs in TickWarlockDemo when the aura drops.
    auto itr = g_demoLegion.find(player->GetGUID().GetCounter());
    if (itr != g_demoLegion.end() && !itr->second.pack.empty() && !itr->second.metaForm)
    {
        itr->second.metaForm = true;
        ObjectGuid playerGuid = player->GetGUID();
        player->m_Events.AddEventAtOffset([playerGuid]()
        {
            if (Player* p = ObjectAccessor::FindPlayer(playerGuid))
                if (p->IsInWorld() && GetClassPerk(p) == SPELL_WARLOCK_DEMONOLOGY && p->HasAura(SPELL_METAMORPHOSIS))
                    SummonWarlockDemoLegion(p, true);
        }, std::chrono::milliseconds(1));
    }
}

// The doubling covers the main demon pet AND every living legion member --
// their owner-attributed spell damage (the imps' firebolts) and melee both
// route through these two hooks, which previously gated on GetPet() only
// and never saw the temp-summoned pack.
Player* GetWarlockDemoDoublingOwner(Unit const* attacker)
{
    if (!attacker)
        return nullptr;
    Unit* owner = attacker->GetOwner();
    Player* player = owner ? owner->ToPlayer() : nullptr;
    if (!player || GetClassPerk(player) != SPELL_WARLOCK_DEMONOLOGY)
        return nullptr;
    if (player->GetPet() == attacker)
        return player;              // main demon: unconditional, as before
    auto itr = g_demoLegion.find(player->GetGUID().GetCounter());
    if (itr == g_demoLegion.end())
        return nullptr;
    ObjectGuid const attackerGuid = attacker->GetGUID();
    for (ObjectGuid const& packGuid : itr->second.pack)
        if (packGuid == attackerGuid)
            return player;
    return nullptr;
}

void ApplyWarlockDemoPetDamage(Unit* attacker, uint32& damage)
{
    if (!GetWarlockDemoDoublingOwner(attacker))
        return;
    damage *= 2;
}

void ApplyWarlockDemoPetSpellDamage(Unit* attacker, int32& damage)
{
    if (damage <= 0 || !GetWarlockDemoDoublingOwner(attacker))
        return;
    damage *= 2;
}

// -------------------------------------------------------------------------
// Warlock: Destruction (910159)
// "Chaos Bolt has no cooldown. Conflagrate also casts a free, instant
// Chaos Bolt at the same target."
// -------------------------------------------------------------------------
void TryWarlockDestroOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_WARLOCK_DESTRUCTION)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info)
        return;
    if (RankOf(info, SPELL_CHAOS_BOLT_R1))
    {
        ClearCooldownAfterCast(player, info->Id, info->GetCategory());
        return;
    }
    if (!RankOf(info, SPELL_CONFLAGRATE_R1))
        return;
    Unit* target = spell->m_targets.GetUnitTarget();
    if (!target || !player->IsValidAttackTarget(target))
        return;
    uint32 const bolt = BestOwned(player, SPELL_CHAOS_BOLT_R1);
    if (bolt)
        player->CastSpell(target, bolt, true);
}

// -------------------------------------------------------------------------
// Druid: Balance (910160)
// "Starfall has no cooldown/mana cost. You are permanently in both Solar
// and Lunar Eclipse at once."
//
// Starfall is a TOGGLE: cast to switch it on and it never expires, recast to
// switch it off. Spamming a no-cooldown Starfall worked but played badly.
//
// The off-switch is the part with crash history, and it has one safe shape.
// Ordering in the core, read rather than recalled:
//
//   Spell::prepare  -> CheckCast(true)        <-- we act HERE
//   Spell::_cast    -> OnPlayerSpellCast      (Spell.cpp:3869)
//   Spell::_cast    -> CheckCast(false)       (Spell.cpp:3895)
//
// Removing the aura from OnPlayerSpellCast and letting the cast continue just
// re-applies it a moment later -- that is the documented Bladestorm trap. Doing
// it in the STRICT CheckCast pass instead means the cast never starts at all:
// OnSpellCheckCast sits at the very top of Spell::CheckCast (:5726) and the
// function returns immediately on any non-OK result. So: remove the aura, fail
// with SPELL_FAILED_DONT_REPORT, done. Nothing re-applies because nothing runs.
//
// The on-state uses the proven free-permanent-toggle pattern (Bladestorm, Feral
// Berserk, Arcane Power): refund the cost and clear the cooldown in the cast
// hook, and observe the aura's lifetime from the update loop rather than
// touching it during its own application, which is what keeps it out of
// Unit::_AddAura.
//
// Balance also had no damage multiplier at all, while Mage ran x4 and DK Frost
// x2 -- the "perk that spreads noise" shape called out in the wiki. Rather than
// chase Starfall's damage id (it periodically triggers a ranked dummy, 50286,
// whose handler then casts a per-rank effect value -- exactly the moving target
// that left Mind Flay and Explosive Shot unmultiplied for months), the
// multiplier is gated on SCHOOL plus "is Starfall switched on". That makes the
// toggle a damage stance and cannot be broken by a triggered id changing.
// -------------------------------------------------------------------------
uint32 const DRUID_BALANCE_DAMAGE_MULT = 3;

// Keep the toggle alive. Same observe-only shape as TickMageArcanePower.
void TickDruidBalanceStarfall(Player* player)
{
    if (!player || !player->IsAlive() || GetClassPerk(player) != SPELL_DRUID_BALANCE)
        return;
    for (uint32 id = SPELL_STARFALL; id; id = sSpellMgr->GetNextSpellInChain(id))
        if (Aura* aura = player->GetAura(id))
            if (aura->GetDuration() >= 0 && aura->GetDuration() < 5000)
            {
                aura->SetDuration(aura->GetMaxDuration() > 0 ? aura->GetMaxDuration() : 20000);
                // Bug report #82: "Starfall refreshes the duration, but doesn't
                // actually continue doing damage indefinitely." The buff bar
                // reset but the damage stopped because the periodic's tick
                // budget runs out: AuraEffect::Update stops ticking once
                // m_tickNumber exceeds totalTicks (SpellAuraEffects.cpp:935),
                // and totalTicks comes from the ORIGINAL duration, not the
                // refreshed one. Reset the tick counter (and re-arm its timer)
                // whenever the duration is extended so the stars keep falling.
                for (uint8 eff = 0; eff < MAX_SPELL_EFFECTS; ++eff)
                    if (AuraEffect* aurEff = aura->GetEffect(eff))
                        aurEff->ResetPeriodic(true);
            }
}

// Report #130: "Starfall to have a 30% chance of casting a free Moonfire on
// any hit target (which would then automatically apply Hurricane as well)".
//
// Detection is rank-correct by construction instead of a hardcoded spell-id
// list (the exact trap the Balance multiplier comment below warns about):
// whatever rank of Starfall the druid is running, its toggle aura's
// SPELL_AURA_PERIODIC_TRIGGER_SPELL effect names that rank's dummy spell, and
// the dummy's EFFECT_0 value IS the AoE damage spell that actually hits
// (verified against the shipped spell_dru_starfall_dummy script: HandleDummy
// casts GetEffectValue() at the hit unit, and 50286's dummy value is 50288).
// So: read the aura -> read the dummy -> compare against the spell that just
// dealt damage.
bool IsStarfallDamageSpell(Player* player, SpellInfo const* info)
{
    if (!player || !info)
        return false;
    for (uint32 id = SPELL_STARFALL; id; id = sSpellMgr->GetNextSpellInChain(id))
    {
        SpellInfo const* toggle = sSpellMgr->GetSpellInfo(id);
        if (!toggle || !player->HasAura(id))
            continue;
        for (uint8 eff = 0; eff < MAX_SPELL_EFFECTS; ++eff)
        {
            if (toggle->Effects[eff].ApplyAuraName != SPELL_AURA_PERIODIC_TRIGGER_SPELL)
                continue;
            SpellInfo const* dummy = sSpellMgr->GetSpellInfo(toggle->Effects[eff].TriggerSpell);
            if (!dummy)
                continue;
            for (uint8 deff = 0; deff < MAX_SPELL_EFFECTS; ++deff)
                // DBC value semantics: an effect's calculated value is
                // BasePoints + 1 for a DieSides=1 dummy, which is how the
                // dummy carries the AoE spell id.
                if (dummy->Effects[deff].Effect && dummy->Effects[deff].BasePoints + 1 == int32(info->Id))
                    return true;
        }
    }
    return false;
}

// The proc half. Runs from ModifySpellDamageTaken (direct spell damage -- the
// Starfall AoE hits land there) so the target is the unit the star actually
// hit, not "every nearby enemy" like a tick-loop proc would be.
//
// CASCADE, fired explicitly: the report wants Moonfire to drag its Hurricane
// along. The Moonfire->Hurricane hook (TryDruidBalanceMoonfireHurricane) is
// dispatched from OnPlayerSpellCast, which early-returns on triggered casts --
// so a free triggered Moonfire can never cascade on its own. We therefore cast
// the Hurricane directly here, with the same explicit source/target positions
// that hook uses. Recursion is safe both ways: both casts are TRIGGERED_FULL_MASK
// (the IsTriggered() dispatch guard ignores them), and Moonfire/Hurricane
// damage can never match IsStarfallDamageSpell.
void TryDruidBalanceStarfallMoonfire(Unit* attacker, Unit* target, SpellInfo const* info)
{
    Player* player = attacker ? attacker->ToPlayer() : nullptr;
    if (!player || !target || !info || GetClassPerk(player) != SPELL_DRUID_BALANCE)
        return;
    if (!HasAuraRankOf(player, SPELL_STARFALL) || !IsStarfallDamageSpell(player, info))
        return;
    if (!player->IsValidAttackTarget(target) || !roll_chance_i(DRUID_STARFALL_MOONFIRE_CHANCE))
        return;

    uint32 const moonfire = BestOwnedOrFirst(player, SPELL_MOONFIRE_R1);
    uint32 const hurricane = BestOwned(player, SPELL_HURRICANE_R1);
    ObjectGuid const playerGuid = player->GetGUID();
    ObjectGuid const targetGuid = target->GetGUID();
    // Deferred one tick like ClearCooldownAfterCast: this runs mid-way through
    // the Starfall damage event, and casting synchronously from inside another
    // spell's damage pipeline is the reentrancy shape the _AddAura crash
    // history forbids.
    player->m_Events.AddEventAtOffset([playerGuid, targetGuid, moonfire, hurricane]()
    {
        Player* p = ObjectAccessor::FindPlayer(playerGuid);
        if (!p || !p->IsInWorld())
            return;
        Unit* t = ObjectAccessor::GetUnit(*p, targetGuid);
        if (!t || !t->IsInWorld() || !t->IsAlive() || !p->IsValidAttackTarget(t))
            return;
        p->CastSpell(t, moonfire, true);
        if (hurricane)
        {
            SpellCastTargets targets;
            targets.SetUnitTarget(t);
            targets.SetSrc(t->GetPosition());
            targets.SetDst(t->GetPosition());
            p->CastSpell(targets, sSpellMgr->GetSpellInfo(hurricane), nullptr, TRIGGERED_FULL_MASK);
        }
    }, std::chrono::milliseconds(1));
}

// The off-switch. Runs in CheckCast's strict pass -- see the ordering note above.
void TryDruidBalanceStarfallToggleOff(Spell* spell, bool strict, SpellCastResult& res)
{
    if (!strict || res != SPELL_CAST_OK || !spell)
        return;
    Unit* caster = spell->GetCaster();
    Player* player = caster ? caster->ToPlayer() : nullptr;
    if (!player || GetClassPerk(player) != SPELL_DRUID_BALANCE)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || !RankOf(info, SPELL_STARFALL))
        return;
    if (!HasAuraRankOf(player, SPELL_STARFALL))
        return;                     // not running -> let the cast through, toggle ON
    for (uint32 id = SPELL_STARFALL; id; id = sSpellMgr->GetNextSpellInChain(id))
        player->RemoveAurasDueToSpell(id);
    res = SPELL_FAILED_DONT_REPORT; // toggle OFF, and the cast never happens
}

// Arcane and Nature are Balance's two schools (Starfire/Moonfire arcane,
// Wrath/Insect Swarm nature, Starfall arcane), so this covers the whole kit
// while the toggle is up and nothing while it is down.
void ApplyDruidBalanceDamage(Unit* attacker, int32& damage, SpellInfo const* info)
{
    if (!attacker || damage <= 0 || !info)
        return;
    Player* player = attacker->ToPlayer();
    if (!player || GetClassPerk(player) != SPELL_DRUID_BALANCE)
        return;
    if (!(info->GetSchoolMask() & (SPELL_SCHOOL_MASK_ARCANE | SPELL_SCHOOL_MASK_NATURE)))
        return;
    if (!HasAuraRankOf(player, SPELL_STARFALL))
        return;
    damage *= int32(DRUID_BALANCE_DAMAGE_MULT);
}

void TryDruidBalanceOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_DRUID_BALANCE)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    // Starfall is rank 1 of 4; an exact match skipped every higher rank.
    if (!info || !RankOf(info, SPELL_STARFALL))
        return;
    if (int32 const cost = spell->GetPowerCost())
        player->ModifyPower(POWER_MANA, cost);
    // info->Id, not SPELL_STARFALL: the player may be casting a higher rank,
    // and clearing rank 1's cooldown entry would leave theirs untouched.
    ClearCooldownAfterCast(player, info->Id, info->GetCategory());
}

void TickDruidBalanceEclipse(Player* player, TickState& st, uint32 diff)
{
    if (!player || GetClassPerk(player) != SPELL_DRUID_BALANCE || !player->IsAlive())
        return;
    st.acc += diff;
    if (st.acc < DRUID_ECLIPSE_TICK_MS)
        return;
    st.acc = 0;
    for (uint32 id : { SPELL_ECLIPSE_SOLAR, SPELL_ECLIPSE_LUNAR })
    {
        if (Aura* aura = player->GetAura(id))
        {
            if (aura->GetDuration() >= 0 && aura->GetDuration() < DRUID_ECLIPSE_REFRESH_DURATION)
                aura->SetDuration(DRUID_ECLIPSE_REFRESH_DURATION);
        }
        else
            player->CastSpell(player, id, true);
    }
}

// -------------------------------------------------------------------------
// Reports #85/#86/#87: Insect Swarm spreads to everything within 25 yards on
// cast, auto-casts on whatever strikes the player in combat, and Thorns
// covers the party while the Balance perk is active.
// -------------------------------------------------------------------------
uint32 const DRUID_BALANCE_INSECT_RANGE = 25;
uint32 const DRUID_BALANCE_THORNS_TICK_MS = 5000;
// Report #119: Insect Swarm is contagious on its own, like the warlock DoT
// plague -- carriers spread it onward, not just the cast target. Same shape
// and bounds as the warlock spread tick: one grid search per second, hop
// range 25 yards, search bounded at 60 yards from the druid.
uint32 const DRUID_INSECT_CONTAGION_TICK_MS = 1000;
float const DRUID_INSECT_CONTAGION_SEARCH_RANGE = 60.0f;
uint32 const DRUID_INSECT_MAX_SPREADS_PER_TICK = 60;
// A carrier this low on duration gets topped up by a fresh cast (report #119:
// "refreshing duration on existing targets") instead of ticking out.
uint32 const DRUID_INSECT_REFRESH_THRESHOLD_MS = 2000;

// Cast-side spread. "On cast" is the OnPlayerSpellCast hook (fired from
// Spell::_cast), which is also where every other perk's free-cast /
// no-cooldown wiring lives.
void TryDruidBalanceInsectSpread(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_DRUID_BALANCE)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || !RankOf(info, SPELL_INSECT_SWARM_R1))
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    if (!g_reentryGuard.insert(guid).second)
        return;
    Unit* victim = spell->m_targets.GetUnitTarget();
    ForEachHostileNear(player, victim ? victim : player,
        float(DRUID_BALANCE_INSECT_RANGE), [player, victim](Unit* target)
    {
        if (target == victim)
            return;
        // Report #119: a target already carrying my swarm gets a fresh cast
        // (refresh) instead of being skipped. Was HasAura(SPELL_INSECT_SWARM_R1)
        // which also missed every rank above rank 1.
        player->CastSpell(target, BestOwnedOrFirst(player, SPELL_INSECT_SWARM_R1), true);
    });
    g_reentryGuard.erase(guid);
}

// Struck-side auto-cast. Runs from the OnDamage hook (attacker hits the
// player), which is the same hook the Protection thorns-perk uses.
void TryDruidBalanceInsectOnStruck(Unit* attacker, Unit* victim)
{
    Player* player = victim ? victim->ToPlayer() : nullptr;
    if (!player || GetClassPerk(player) != SPELL_DRUID_BALANCE || !player->IsAlive())
        return;
    if (!attacker || !attacker->IsAlive() || !player->IsValidAttackTarget(attacker))
        return;
    if (attacker->HasAura(SPELL_INSECT_SWARM_R1, player->GetGUID()))
        return;
    player->CastSpell(attacker, BestOwnedOrFirst(player, SPELL_INSECT_SWARM_R1), true);
}

// Report #119: the contagious half. Every second, enemies carrying MY Insect
// Swarm (any rank -- the old HasAura(SPELL_INSECT_SWARM_R1, ...) checks only
// ever matched rank 1) pass it on to clean enemies within 25 yards of the
// carrier, and carriers about to tick out get refreshed. Same carrier split
// as TickWarlockAffliction: one grid search per tick, carriers snapshotted
// before anything is cast, casts capped as a backstop.
void TickDruidBalanceInsectContagion(Player* player, TickState& st, uint32 diff)
{
    if (!player || GetClassPerk(player) != SPELL_DRUID_BALANCE || !player->IsAlive() || !player->IsInCombat())
        return;
    st.acc += diff;
    if (st.acc < DRUID_INSECT_CONTAGION_TICK_MS)
        return;
    st.acc = 0;

    uint32 const guid = player->GetGUID().GetCounter();
    if (!g_reentryGuard.insert(guid).second)
        return;

    auto swarmOn = [player](Unit* unit) -> Aura*
    {
        for (uint32 id = SPELL_INSECT_SWARM_R1; id; id = sSpellMgr->GetNextSpellInChain(id))
            if (Aura* aura = unit->GetAura(id, player->GetGUID()))
                return aura;
        return nullptr;
    };

    std::vector<Unit*> carriers;
    std::vector<Unit*> clean;
    ForEachHostileInRange(player, DRUID_INSECT_CONTAGION_SEARCH_RANGE, [&](Unit* unit)
    {
        if (swarmOn(unit))
            carriers.push_back(unit);
        else
            clean.push_back(unit);
    });

    uint32 const swarm = BestOwnedOrFirst(player, SPELL_INSECT_SWARM_R1);
    uint32 spread = 0;
    for (Unit* carrier : carriers)
    {
        if (Aura* aura = swarmOn(carrier))
            if (aura->GetDuration() >= 0 && uint32(aura->GetDuration()) < DRUID_INSECT_REFRESH_THRESHOLD_MS)
            {
                ++spread;
                player->CastSpell(carrier, swarm, true);
            }
    }
    for (Unit* target : clean)
    {
        if (spread >= DRUID_INSECT_MAX_SPREADS_PER_TICK)
            break;
        for (Unit* carrier : carriers)
        {
            if (!target->IsWithinDist(carrier, float(DRUID_BALANCE_INSECT_RANGE)))
                continue;
            ++spread;
            // Report #156 parity: cast BY the carrier so the swarm visibly
            // creeps bug to bug; originalCaster stays the druid so swarmOn
            // (caster-guid keyed) keeps recognizing the fresh carriers.
            carrier->CastSpell(target, swarm, true, nullptr, nullptr,
                player->GetGUID());
            break; // one fresh application per clean target is enough
        }
    }

    LOG_DEBUG("module.livinggear", "insect contagion: {} carrier(s), {} clean, {} cast(s) this tick",
        carriers.size(), clean.size(), spread);
    g_reentryGuard.erase(guid);
}

// -------------------------------------------------------------------------
// Report #120: Moonfire auto-applies Hurricane, and Wrath/Starfire pair up.
//
// Moonfire (any rank the player owns) cast on an enemy also places a free
// Hurricane centered ON that enemy. Hurricane's ranks are DE-CHANNELED
// server-side (PatchHurricaneServerSide, same treatment as Blizzard), so the
// circle is placed at the enemy and ticks damage on its own until it expires --
// the druid is NOT held in place and can keep casting. The cast carries an
// explicit source position at the enemy because Hurricane's area reference is
// the cast's SOURCE location, and Spell::InitExplicitTargets only fills the
// source with the caster's position when the caller left it unset
// (Spell.cpp:853); dst and the unit target are set too so both SRC- and
// DEST-referenced variants land the same way.
//
// The damage is driven by TickDruidBalanceHurricane (below) rather than by the
// spell's own channel machinery -- exactly the Blizzard pattern. Without the
// channel flag the engine never syncs the persistent area aura's lifetime to
// anything, so a spell-driven tick is the only reliable damage source.
//
// RECURSION GUARD: every free cast here goes out with `true`
// (TRIGGERED_FULL_MASK), and OnPlayerSpellCast bails out on any triggered
// spell before the Try* hooks run (the `if (spell->IsTriggered()) return;`
// early return in the dispatch below). So the free Starfire fired from Wrath
// never fires a free Wrath back, and vice versa -- verified, not assumed; that
// early return is what every sibling perk's free cast already relies on.
//
// Rank selection uses BestOwned -- the highest rank the player genuinely
// LEARNED, same helper the hunter sting perks use. A Balance druid trains all
// four lines natively, so nothing is granted here.
// -------------------------------------------------------------------------
bool IsHurricaneRank(uint32 spellId)
{
    for (uint32 id : SPELL_HURRICANE_RANKS)
        if (id == spellId)
            return true;
    return false;
}

void TryDruidBalanceMoonfireHurricane(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_DRUID_BALANCE)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || !RankOf(info, SPELL_MOONFIRE_R1))
        return;
    uint32 const hurricane = BestOwned(player, SPELL_HURRICANE_R1);
    if (!hurricane)
        return;
    Unit* target = spell->m_targets.GetUnitTarget();
    if (!target || !player->IsValidAttackTarget(target))
        return;
    SpellCastTargets targets;
    targets.SetUnitTarget(target);
    targets.SetSrc(target->GetPosition());
    targets.SetDst(target->GetPosition());
    player->CastSpell(targets, sSpellMgr->GetSpellInfo(hurricane), nullptr, TRIGGERED_FULL_MASK);
}

void TryDruidBalanceStarPair(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_DRUID_BALANCE)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info)
        return;
    bool const isWrath = RankOf(info, SPELL_WRATH_R1);
    bool const isStarfire = RankOf(info, SPELL_STARFIRE_R1);
    if (!isWrath && !isStarfire)
        return;
    uint32 const twin = BestOwned(player, isWrath ? SPELL_STARFIRE_R1 : SPELL_WRATH_R1);
    if (!twin)
        return;
    Unit* target = spell->m_targets.GetUnitTarget();
    if (!target || !player->IsValidAttackTarget(target))
        return;
    // Triggered -> TRIGGERED_FULL_MASK: free, instant, no cooldown, and the
    // IsTriggered() dispatch guard keeps it from firing its own twin back.
    player->CastSpell(target, twin, true);
}

// Hurricane's damage, driven by us exactly like Blizzard's. The ranks are
// de-channeled server-side, so the persistent area aura inside the dynamic
// object never ticks on its own -- this walks every Hurricane object the
// druid has on the ground and every second deals the rank's own damage to
// everything inside it. The damage spell is the rank's PERIODIC_TRIGGER_SPELL
// effect (the same path Blizzard's tick spell is read from), so every rank
// hits for its correct amount with no hardcoded table.
void TickDruidBalanceHurricane(Player* player, TickState& st, uint32 diff)
{
    if (!player || GetClassPerk(player) != SPELL_DRUID_BALANCE || !player->IsAlive())
        return;
    st.acc += diff;
    if (st.acc < DRUID_HURRICANE_TICK_MS)
        return;
    st.acc = 0;

    uint32 const guid = player->GetGUID().GetCounter();
    if (!g_reentryGuard.insert(guid).second)
        return;

    for (uint32 id : SPELL_HURRICANE_RANKS)
    {
        DynamicObject* dyn = player->GetDynObject(id);
        if (!dyn)
            continue;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(id);
        if (!info)
            continue;
        uint32 tick = 0;
        for (uint8 i = EFFECT_0; i < MAX_SPELL_EFFECTS && !tick; ++i)
            tick = info->Effects[i].TriggerSpell;
        if (!tick)
            continue;
        float radius = dyn->GetRadius();
        if (radius <= 0.0f)
            radius = CLASS_PERK_RANGE;
        ForEachHostileNear(player, dyn, radius, [player, tick](Unit* target)
        {
            player->CastSpell(target, tick, true);
        });
    }

    g_reentryGuard.erase(guid);
}

// Thorns on the party. Same observe-only shape as the Eclipse tick --
// touching a freshly-applied aura from inside its own hook is the documented
// Bladestorm trap, so this just re-casts anyone who's missing it.
void TickDruidBalanceThorns(Player* player, TickState& st, uint32 diff)
{
    if (!player || GetClassPerk(player) != SPELL_DRUID_BALANCE || !player->IsAlive())
        return;
    st.acc += diff;
    if (st.acc < DRUID_BALANCE_THORNS_TICK_MS)
        return;
    st.acc = 0;
    Group* group = player->GetGroup();
    if (!group)
    {
        if (!player->HasAura(SPELL_THORNS_R1))
            player->CastSpell(player, BestOwnedOrFirst(player, SPELL_THORNS_R1), true);
        return;
    }
    for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
    {
        Player* member = itr->GetSource();
        if (!member || !member->IsAlive() || !member->IsInWorld())
            continue;
        if (!member->HasAura(SPELL_THORNS_R1))
            member->CastSpell(member, BestOwnedOrFirst(member, SPELL_THORNS_R1), true);
    }
}

// -------------------------------------------------------------------------
// Druid: Feral (910161)
// "Berserk is a free toggle. While active, Cat/Bear abilities cost no
// energy/rage and lose their cooldowns."
// -------------------------------------------------------------------------
void TryDruidFeralOnCast(Player* player, Spell* spell)
{
    if (!player || !spell)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info)
        return;
    if (GetClassPerk(player) == SPELL_DRUID_FERAL && info->Id == SPELL_BERSERK_DRUID)
    {
        ClearCooldownAfterCast(player, SPELL_BERSERK_DRUID, info->GetCategory());
        return;
    }
    if (GetClassPerk(player) != SPELL_DRUID_FERAL || info->SpellFamilyName != SPELLFAMILY_DRUID)
        return;
    if (!player->HasAura(SPELL_BERSERK_DRUID))
        return;
    if (int32 const cost = spell->GetPowerCost())
        player->ModifyPower(Powers(info->PowerType), cost);
    ClearCooldownAfterCast(player, info->Id, info->GetCategory());
}

// -------------------------------------------------------------------------
// Druid: Restoration (910162)
// "Wild Growth has no cooldown and heals up to 10 allies within 30 yards.
// Rejuvenation spreads to injured allies within 15 yards every 3 sec."
// -------------------------------------------------------------------------
void TryDruidRestOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_DRUID_RESTORATION)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || !RankOf(info, SPELL_WILD_GROWTH_R1))
        return;
    ClearCooldownAfterCast(player, info->Id, info->GetCategory());
}

void ApplyDruidRestWildGrowthTargets(Spell* spell, Player* player, SpellInfo const* info)
{
    if (!spell || !player || !info || GetClassPerk(player) != SPELL_DRUID_RESTORATION)
        return;
    if (RankOf(info, SPELL_WILD_GROWTH_R1))
        spell->SetSpellValue(SPELLVALUE_MAX_TARGETS, WILD_GROWTH_MAX_TARGETS);
}

void TickDruidRestRejuvSpread(Player* player, TickState& st, uint32 diff)
{
    if (!player || GetClassPerk(player) != SPELL_DRUID_RESTORATION || !player->IsAlive())
        return;
    st.acc += diff;
    if (st.acc < DRUID_REJUV_SPREAD_TICK_MS)
        return;
    st.acc = 0;
    uint32 const guid = player->GetGUID().GetCounter();
    if (!g_reentryGuard.insert(guid).second)
        return;
    ForEachInjuredAllyNear(player, player, 60.0f, [player](Unit* source)
    {
        if (!HasAuraRankOf(source, SPELL_REJUVENATION_R1))
            return;
        uint32 const rejuv = BestOwned(player, SPELL_REJUVENATION_R1);
        if (!rejuv)
            return;
        ForEachInjuredAllyNear(player, source, DRUID_REJUV_SPREAD_RANGE, [player, source, rejuv](Unit* ally)
        {
            if (ally != source && !ally->HasAura(rejuv, player->GetGUID()))
                player->CastSpell(ally, rejuv, true);
        });
    });
    g_reentryGuard.erase(guid);
}

// -------------------------------------------------------------------------
// Priest: Discipline (910163)
// "Penance has no cooldown and also applies Power Word: Shield to the
// target."
// -------------------------------------------------------------------------
// Penance ricochets off up to PRIEST_PENANCE_BOUNCES further enemies.
//
// Penance itself is a channelled dummy (47540) that fires bolts; re-casting the
// channel at each hop would fight the engine, so a hop casts Penance's own
// DAMAGE spell instead. That spell is ranked in lockstep with the channel
// (47758 -> 53001/53002/53003 against 47540 -> 53005/53006/53007), and the core
// pairs them with GetSpellWithRank, so the hop uses the rank the priest
// actually cast rather than a hardcoded id -- the mistake that left Mind Flay
// and Explosive Shot unmultiplied.
//
// Staggered one hop per event, exactly like AvengerBounceStep above, because
// this is scheduled from inside OnPlayerSpellCast (mid-Spell::cast) and a
// synchronous loop of casts there is the documented Unit::_AddAura reentrancy
// crash. g_reentryGuard is held for the whole chain and cleared on every exit.
uint32 const PRIEST_PENANCE_BOUNCES = 5;
float const PRIEST_PENANCE_HOP_RANGE = 15.0f;

static void PenanceBounceStep(ObjectGuid playerGuid, ObjectGuid currentGuid, uint32 damageSpellId, uint32 bouncesLeft)
{
    uint32 const guid = playerGuid.GetCounter();
    if (!bouncesLeft)
    {
        g_reentryGuard.erase(guid);
        return;
    }
    Player* player = ObjectAccessor::FindPlayer(playerGuid);
    if (!player || !player->IsInWorld() || !player->IsAlive())
    {
        g_reentryGuard.erase(guid);
        return;
    }
    Unit* current = ObjectAccessor::GetUnit(*player, currentGuid);
    if (!current || !current->IsInWorld())
    {
        g_reentryGuard.erase(guid);
        return;
    }
    // Hop to someone new near the last target; never bounce back onto it, so a
    // single enemy does not eat all five jumps.
    Unit* next = nullptr;
    ForEachHostileNear(player, current, PRIEST_PENANCE_HOP_RANGE, [&next, current](Unit* target)
    {
        if (!next && target != current)
            next = target;
    });
    if (!next)
    {
        g_reentryGuard.erase(guid);
        return;
    }
    player->CastSpell(next, damageSpellId, true);
    ObjectGuid nextGuid = next->GetGUID();
    player->m_Events.AddEventAtOffset([playerGuid, nextGuid, damageSpellId, bouncesLeft]()
    {
        PenanceBounceStep(playerGuid, nextGuid, damageSpellId, bouncesLeft - 1);
    }, std::chrono::milliseconds(150));
}

void TryPriestDiscOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_PRIEST_DISCIPLINE)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || !RankOf(info, SPELL_PENANCE_R1))
        return;
    ClearCooldownAfterCast(player, info->Id, info->GetCategory());
    Unit* target = spell->m_targets.GetUnitTarget();
    uint32 const shield = BestOwned(player, SPELL_POWER_WORD_SHIELD_R1);
    if (target && shield)
        player->CastSpell(target, shield, true);

    // Only ricochet when this was aimed at an enemy -- Penance on a friend is a
    // heal, and chaining that off "nearby enemies" would be nonsense.
    if (!target || !player->IsValidAttackTarget(target))
        return;
    uint32 const damageSpellId = sSpellMgr->GetSpellWithRank(SPELL_PENANCE_R1_DAMAGE, info->GetRank(), true);
    if (!damageSpellId)
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    if (!g_reentryGuard.insert(guid).second)
        return;
    ObjectGuid playerGuid = player->GetGUID();
    ObjectGuid targetGuid = target->GetGUID();
    player->m_Events.AddEventAtOffset([playerGuid, targetGuid, damageSpellId]()
    {
        PenanceBounceStep(playerGuid, targetGuid, damageSpellId, PRIEST_PENANCE_BOUNCES);
    }, std::chrono::milliseconds(1));
}

// -------------------------------------------------------------------------
// Priest: Holy (910164)
// "Guardian Spirit has no cooldown and also applies to 2 more injured
// allies within 20 yards."
// -------------------------------------------------------------------------
void TryPriestHolyOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_PRIEST_HOLY)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || info->Id != SPELL_GUARDIAN_SPIRIT)
        return;
    ClearCooldownAfterCast(player, SPELL_GUARDIAN_SPIRIT, info->GetCategory());
    Unit* target = spell->m_targets.GetUnitTarget();
    if (!target)
        return;
    uint32 spread = 0;
    ForEachInjuredAllyNear(player, target, PRIEST_GS_SPREAD_RANGE, [&](Unit* ally)
    {
        if (spread >= PRIEST_GS_SPREAD_COUNT || ally == target || ally->HasAura(SPELL_GUARDIAN_SPIRIT, player->GetGUID()))
            return;
        player->CastSpell(ally, BestOwnedOrFirst(player, SPELL_GUARDIAN_SPIRIT), true);
        ++spread;
    });
}

// -------------------------------------------------------------------------
// Priest: Shadow (910165) -- "The Void Choir"
// "Shadowfiend has no cooldown. Mind Flay deals quadruple damage. Every
// Mind Flay channel sings with two void tendrils. Mind Blast detonates your
// Shadow Word: Pain -- its remaining duration dealt at once, then refreshed
// to full. Voidform (toggle): a free Mind Blast at your target every 3 sec."
//
// Tendrils are cloned from the Shadowfiend's own summon entry (19668, read
// out of Spell.dbc 34433's SUMMON effect), given the shared temp-pet AI, and
// capped at 6 alive per priest with oldest-first despawn -- the exact
// discipline SummonBmPack uses. Shadowform (15473) is the Voidform toggle
// vehicle: an existing permanent aura-bearing castable, so the on-state is
// just "is the aura up" and the off-switch is a STRICT OnSpellCheckCast pass
// that removes it and fails the recast with SPELL_FAILED_DONT_REPORT.
// -------------------------------------------------------------------------
void SummonVoidTendrils(Player* player)
{
    if (!player || !player->IsInWorld())
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    std::vector<ObjectGuid>& pack = g_voidTendrils[guid];
    while (pack.size() >= PRIEST_VOID_TENDRIL_MAX_ALIVE)
    {
        if (Creature* oldest = ObjectAccessor::GetCreature(*player, pack.front()))
            oldest->DespawnOrUnsummon();
        pack.erase(pack.begin());
    }
    uint32 const hp = std::max<uint32>(1, player->GetMaxHealth() / 5); // 20%
    for (uint32 i = 0; i < PRIEST_VOID_TENDRIL_COUNT; ++i)
    {
        Position p = player->GetPosition();
        p.RelocateOffset({ frand(-3.0f, 3.0f), frand(-3.0f, 3.0f), 0.0f, 0.0f });
        TempSummon* s = player->SummonCreature(PRIEST_SHADOWFIEND_ENTRY, p,
            TEMPSUMMON_TIMED_DESPAWN, PRIEST_VOID_TENDRIL_LIFETIME_MS);
        if (!s)
            continue;
        s->SetOwnerGUID(player->GetGUID());
        s->SetFaction(player->GetFaction());
        s->SetLevel(player->GetLevel());
        s->SetMaxHealth(hp);
        s->SetHealth(hp);
        s->AIM_Initialize(new npc_lg_temp_petAI(s));
        pack.push_back(s->GetGUID());
    }
}

void TryPriestShadowOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_PRIEST_SHADOW)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info)
        return;
    if (info->Id == SPELL_SHADOWFIEND)
        ClearCooldownAfterCast(player, SPELL_SHADOWFIEND, info->GetCategory());
    if (!RankOf(info, SPELL_MIND_FLAY_R1))
        return;
    // Deferred like the BM pack: this hook fires midway through Spell::cast(),
    // so the summons wait one tick.
    ObjectGuid playerGuid = player->GetGUID();
    player->m_Events.AddEventAtOffset([playerGuid]()
    {
        if (Player* p = ObjectAccessor::FindPlayer(playerGuid))
            if (p->IsInWorld() && GetClassPerk(p) == SPELL_PRIEST_SHADOW)
                SummonVoidTendrils(p);
    }, std::chrono::milliseconds(1));
}

// Mind Blast on a target carrying the priest's own Shadow Word: Pain
// detonates it -- the Envenom detonator math (TryRogueAssassinationDetonate):
// remaining duration's worth of ticks dealt at once, then the DoT is put
// back at full duration. Single target by design: the blast cashes in what
// it hits, it is not an AoE cleanse.
void TryPriestShadowMindBlastDetonate(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_PRIEST_SHADOW)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || !RankOf(info, SPELL_MIND_BLAST_R1))
        return;
    Unit* target = spell->m_targets.GetUnitTarget();
    if (!target || !target->IsAlive() || !player->IsValidAttackTarget(target))
        return;

    ObjectGuid const owner = player->GetGUID();
    // Collect first: dealing damage can kill the target and invalidate the
    // aura list mid-walk (the discipline the Envenom detonator established).
    std::vector<Aura*> pending;
    for (AuraEffect* eff : target->GetAuraEffectsByType(SPELL_AURA_PERIODIC_DAMAGE))
    {
        Aura* aura = eff->GetBase();
        if (!aura || aura->GetCasterGUID() != owner)
            continue;
        if (!RankOf(aura->GetSpellInfo(), SPELL_SW_PAIN_R1))
            continue;
        if (aura->GetDuration() <= 0 || eff->GetAmplitude() <= 0)
            continue;
        pending.push_back(aura);
    }

    for (Aura* aura : pending)
    {
        if (!target->IsAlive())
            break;
        AuraEffect* damEff = aura->GetEffect(0); // SW:P effect 0 is the periodic damage
        if (!damEff)
            continue;
        int32 const period = damEff->GetAmplitude();
        int32 const left = aura->GetDuration();
        int32 const ticks = left / period;
        if (ticks <= 0)
            continue;
        int32 const damage = damEff->GetAmount() * ticks;
        Unit::DealDamage(player, target, uint32(std::max(0, damage)), nullptr,
            SPELL_DIRECT_DAMAGE, aura->GetSpellInfo()->GetSchoolMask(),
            aura->GetSpellInfo(), false);
        // Refreshed to full rather than consumed -- detonation, not dispel.
        if (Aura* again = target->GetAura(aura->GetId(), owner))
            again->RefreshDuration();
    }
}

// The off-switch, in CheckCast's strict pass (same ordering argument as the
// Starfall toggle): recasting Shadowform while it is up turns Voidform off
// and the cast never starts.
void TryPriestShadowVoidformToggleOff(Spell* spell, bool strict, SpellCastResult& res)
{
    if (!strict || res != SPELL_CAST_OK || !spell)
        return;
    Unit* caster = spell->GetCaster();
    Player* player = caster ? caster->ToPlayer() : nullptr;
    if (!player || GetClassPerk(player) != SPELL_PRIEST_SHADOW)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || !RankOf(info, SPELL_SHADOWFORM) || !HasAuraRankOf(player, SPELL_SHADOWFORM))
        return;                     // not running -> let the cast through, toggle ON
    player->RemoveAurasDueToSpell(SPELL_SHADOWFORM);
    res = SPELL_FAILED_DONT_REPORT; // toggle OFF, and the cast never happens
}

// Voidform's on-state heartbeat: while Shadowform is up, fire a free
// triggered Mind Blast at the priest's current target every 3s. Shadowform's
// duration is permanent (-1), so unlike Starfall nothing needs refreshing --
// the aura observation is just the gate. Skip while the priest is mid-cast
// or channeling so the free blast never clips a Mind Flay channel. Triggered
// casts early-return from OnPlayerSpellCast, so the free blast cannot
// recursively detonate (or proc anything else that fires from the cast hook).
void TickPriestShadowVoidform(Player* player, TickState& st, uint32 diff)
{
    if (!player || GetClassPerk(player) != SPELL_PRIEST_SHADOW || !player->IsAlive())
        return;
    if (!HasAuraRankOf(player, SPELL_SHADOWFORM))
        return; // Voidform off
    st.acc += diff;
    if (st.acc < PRIEST_VOID_TICK_MS)
        return;
    st.acc = 0;
    if (player->IsNonMeleeSpellCast(false))
        return;
    Unit* target = player->GetSelectedUnit();
    if (!target || !target->IsAlive() || !player->IsValidAttackTarget(target))
        target = player->GetVictim(); // #96: GetVictim fallback for target-driven casters
    if (!target || !target->IsAlive() || !player->IsValidAttackTarget(target))
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    if (!g_reentryGuard.insert(guid).second)
        return;
    player->CastSpell(target, BestOwnedOrFirst(player, SPELL_MIND_BLAST_R1), true);
    g_reentryGuard.erase(guid);
}

// Mind Flay's damage arrives as its PERIODIC_TRIGGER_SPELL_WITH_VALUE effect
// casting spell 58381 -- the same id for all nine ranks (verified by reading
// EffectTriggerSpell out of Spell.dbc for each). 58381 is not in the rank chain
// (spell_ranks: 15407 -> 17311 ... 48156), so RankOf alone never matched.
void ApplyPriestShadowMindFlayDamage(Unit* attacker, int32& damage, SpellInfo const* info)
{
    if (!attacker || damage <= 0 || !info)
        return;
    if (info->Id != SPELL_MIND_FLAY_DAMAGE && !RankOf(info, SPELL_MIND_FLAY_R1))
        return;
    Player* player = attacker->ToPlayer();
    if (!player || GetClassPerk(player) != SPELL_PRIEST_SHADOW)
        return;
    damage *= 4;
}

// -------------------------------------------------------------------------
// Death Knight: Blood (910166)
// "Dancing Rune Weapon has no cooldown/runic cost. While active, melee
// hits heal you for 5% of the damage dealt."
//
// SIMPLIFICATION: "+50% parry" was dropped -- applying a flat parry bonus
// means touching stat-modifier internals nothing else in this file goes
// near; the self-heal-on-hit clause reuses the exact mechanism already
// proven for Warrior Fury's "heal 1% of max health in combat" below.
// -------------------------------------------------------------------------
void TryDkBloodOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_DK_BLOOD)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || info->Id != SPELL_DANCING_RUNE_WEAPON)
        return;
    if (int32 const cost = spell->GetPowerCost())
        player->ModifyPower(POWER_RUNIC_POWER, cost);
    ClearCooldownAfterCast(player, SPELL_DANCING_RUNE_WEAPON, info->GetCategory());
}

void NoteDkBloodMeleeHit(Player* player, uint32 damage)
{
    if (!player || !damage || GetClassPerk(player) != SPELL_DK_BLOOD || !player->HasAura(SPELL_DANCING_RUNE_WEAPON))
        return;
    if (!player->IsInCombat() || player->GetHealth() >= player->GetMaxHealth())
        return;
    uint32 const heal = damage / 20; // 5%
    if (heal)
        player->ModifyHealth(int32(heal));
}

// -------------------------------------------------------------------------
// Death Knight: Frost (910167)
// "Hungering Cold has no cooldown/runic cost. Frost Strike and Obliterate
// deal double damage."
// -------------------------------------------------------------------------
void TryDkFrostOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_DK_FROST)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || info->Id != SPELL_HUNGERING_COLD)
        return;
    if (int32 const cost = spell->GetPowerCost())
        player->ModifyPower(POWER_RUNIC_POWER, cost);
    ClearCooldownAfterCast(player, SPELL_HUNGERING_COLD, info->GetCategory());
}

void ApplyDkFrostDamage(Unit* attacker, int32& damage, SpellInfo const* info)
{
    if (!attacker || damage <= 0 || !info || (!RankOf(info, SPELL_FROST_STRIKE_R1) && !RankOf(info, SPELL_OBLITERATE_R1)))
        return;
    Player* player = attacker->ToPlayer();
    if (!player || GetClassPerk(player) != SPELL_DK_FROST)
        return;
    damage *= 2;
}

// -------------------------------------------------------------------------
// Paladin: Protection (910070)
// "Avenger's Shield bounces 30 times and can rehit. Range 60 yards.
// Devotion Aura: 10% damage reduction and +20% run/mount speed for
// allies. You deal Holy thorns equal to 50% of your armor."
//
// SIMPLIFICATION: the initial-cast range increase to 60 yards is not
// implemented (would require mutating shared SpellInfo range data, which
// is riskier than it's worth for this pass); the bounce count, rehit,
// damage reduction, and thorns are. The +20% run/mount speed for allies
// is also left out -- this codebase has a documented crash/near-miss
// history specifically around ad hoc movement-speed code (extra jump
// disabled, 500% speed-cap near-miss; see Bonesaw.md), so a new
// speed-granting mechanic was judged not worth the risk for this pass.
// -------------------------------------------------------------------------
// Performs one Avenger's Shield bounce, then schedules the next one as its
// own separate m_Events callback (see AvengerBounceStep) instead of looping
// synchronously. Running up to PALADIN_AS_BOUNCES (30) CastSpell calls
// back-to-back in a single call, from inside OnPlayerSpellCast (which fires
// mid-way through the triggering spell's own Spell::cast()), is exactly the
// reentrant pattern that produced the recurring Unit::_AddAura assert
// "!m_cleanupDone" crashes on Shadowstep/ChainAmbush (2026-08-20) -- staggering
// each bounce onto its own tick avoids both the reentrancy and the "spam N
// heavy operations synchronously" failure mode.
static void AvengerBounceStep(ObjectGuid playerGuid, ObjectGuid currentGuid, uint32 spellId, uint32 bouncesLeft)
{
    // g_reentryGuard stays held for the whole staggered sequence (cleared
    // on every exit path below) so an overlapping non-triggered cast can't
    // kick off a second bounce chain while this one is still running.
    uint32 const guid = playerGuid.GetCounter();
    if (!bouncesLeft)
    {
        g_reentryGuard.erase(guid);
        return;
    }
    Player* player = ObjectAccessor::FindPlayer(playerGuid);
    if (!player || !player->IsInWorld())
    {
        g_reentryGuard.erase(guid);
        return;
    }
    Unit* current = ObjectAccessor::GetUnit(*player, currentGuid);
    if (!current || !current->IsInWorld())
    {
        g_reentryGuard.erase(guid);
        return;
    }
    Unit* next = nullptr;
    ForEachHostileNear(player, current, PALADIN_AS_HOP_RANGE, [&next](Unit* target)
    {
        if (!next)
            next = target;
    });
    if (!next && current->IsAlive() && player->IsValidAttackTarget(current))
        next = current;
    if (!next)
    {
        g_reentryGuard.erase(guid);
        return;
    }
    player->CastSpell(next, spellId, true);
    ObjectGuid nextGuid = next->GetGUID();
    player->m_Events.AddEventAtOffset([playerGuid, nextGuid, spellId, bouncesLeft]()
    {
        AvengerBounceStep(playerGuid, nextGuid, spellId, bouncesLeft - 1);
    }, std::chrono::milliseconds(150));
}

// Shared scheduler: fire the full bounce chain at a target, starting in 1ms
// (the first hop resolves immediately, then each subsequent hop is its own
// m_Events callback -- see AvengerBounceStep). The caller owns the re-entry
// guard; AvengerBounceStep releases it when the chain ends.
void ScheduleAvengerBouncesOn(Player* player, ObjectGuid targetGuid, uint32 spellId)
{
    if (!player || targetGuid.IsEmpty() || !spellId)
        return;
    ObjectGuid playerGuid = player->GetGUID();
    player->m_Events.AddEventAtOffset([playerGuid, targetGuid, spellId]()
    {
        AvengerBounceStep(playerGuid, targetGuid, spellId, PALADIN_AS_BOUNCES);
    }, std::chrono::milliseconds(1));
}

void ScheduleAvengerBounces(Player* player, Spell* spell)
{
    if (!player || !spell)
        return;
    Unit* current = spell->m_targets.GetUnitTarget();
    if (!current)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info)
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    if (!g_reentryGuard.insert(guid).second)
        return;
    ScheduleAvengerBouncesOn(player, current->GetGUID(), info->Id);
}

// Report #81: a Judgement cast has PALADIN_AS_PROC_CHANCE % to fire the full
// Avenger's Shield bounce chain for free. Runs from the cast hook like every
// other perk trigger, so it is a real button being pressed -- not a proc
// hidden in a tick loop. The re-entry guard is held until the chain ends
// (AvengerBounceStep erases it), exactly like a hand-cast.
void TryPaladinProtJudgementProc(Player* player, Spell* spell)
{
    if (!player || !spell || spell->IsTriggered() || GetClassPerk(player) != SPELL_PALADIN_PROTECTION)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info)
        return;
    // Report #132: both Judgement and Hammer of the Righteous roll the same
    // PALADIN_AS_PROC_CHANCE for a free shield; every other spell is ignored.
    if (!RankOf(info, SPELL_JUDGEMENT_R1) && !RankOf(info, SPELL_HAMMER_OF_THE_RIGHTEOUS_R1))
        return;
    if (!roll_chance_i(PALADIN_AS_PROC_CHANCE))
        return;
    Unit* target = spell->m_targets.GetUnitTarget();
    if (!target || !player->IsValidAttackTarget(target))
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    if (!g_reentryGuard.insert(guid).second)
        return;
    ScheduleAvengerBouncesOn(player, target->GetGUID(), BestOwnedOrFirst(player, SPELL_AVENGERS_SHIELD));
}

void TryPaladinProtOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || spell->IsTriggered() || GetClassPerk(player) != SPELL_PALADIN_PROTECTION)
        return;
    if (g_reentryGuard.count(player->GetGUID().GetCounter()))
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || !RankOf(info, SPELL_AVENGERS_SHIELD))
        return;
    // Report #132: the 30s category cooldown is out; the button comes back
    // on a flat 6s. Cleared deferred (after the engine writes its own 30s
    // entry), then a 6s cooldown is applied in the same deferred tick so the
    // pacing the report asks for is real and not just a free button.
    ClearCooldownAfterCast(player, info->Id, info->GetCategory());
    LOG_INFO("module.livinggear",
        "avenger shield: cast {} (category {}) by {} -- scheduling deferred 30s clear + 6s",
        info->Id, info->GetCategory(), player->GetName());
    ObjectGuid const playerGuid = player->GetGUID();
    uint32 const spellId = info->Id;
    player->m_Events.AddEventAtOffset([playerGuid, spellId]()
    {
        if (Player* p = ObjectAccessor::FindPlayer(playerGuid))
            if (p->HasSpell(spellId))
            {
                // Report #207: the 6s server state is correct (verified by the
                // log line below), but the client kept displaying the engine's
                // 30s category entry from the cast packet. Wipe the client's
                // stale entry first, then re-arm -- otherwise the icon shows
                // 30s while the server is already ready.
                p->SendClearCooldown(spellId, p);
                p->AddSpellCooldown(spellId, 0, PALADIN_AS_COOLDOWN_MS, true);
                // Report #187: the 30s cooldown came back anyway. Log the
                // cooldown state 1ms after the override lands so the next
                // re-report tells us whether something re-writes it later.
                auto const& cds = p->GetSpellCooldownMap();
                for (auto const& [cdSpell, cd] : cds)
                    if (cdSpell == spellId || cd.category)
                        LOG_INFO("module.livinggear",
                            "avenger shield: cooldown after override -- spell {} end {} category {} maxduration {}",
                            cdSpell, cd.end, cd.category, cd.maxduration);
            }
    }, std::chrono::milliseconds(1));
    ScheduleAvengerBounces(player, spell);
}

bool NearProtDevotion(Unit* victim)
{
    if (!victim || !victim->IsAlive())
        return false;
    Player* vp = victim->ToPlayer();
    if (!vp)
        return false;
    if (GetClassPerk(vp) == SPELL_PALADIN_PROTECTION && HasAuraRankOf(vp, SPELL_DEVOTION_AURA))
        return true;
    Group* group = vp->GetGroup();
    if (!group)
        return false;
    for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
    {
        Player* member = itr->GetSource();
        if (!member || member == vp || !member->IsInWorld())
            continue;
        if (member->getClass() != CLASS_PALADIN || GetClassPerk(member) != SPELL_PALADIN_PROTECTION)
            continue;
        if (!HasAuraRankOf(member, SPELL_DEVOTION_AURA))
            continue;
        if (vp->IsInMap(member) && vp->IsWithinDistInMap(member, PALADIN_DEVO_ALLY_RANGE))
            return true;
    }
    return false;
}

void ApplyDevotionDR(Unit* victim, uint32& damage)
{
    if (!victim || !damage)
        return;
    if (NearProtDevotion(victim))
    {
        uint32 const reduced = uint32(float(damage) * PALADIN_DEVO_DR);
        damage = reduced < 1 ? 1 : reduced;
    }
}

void TryProtThorns(Unit* attacker, Unit* victim, uint32 damage)
{
    if (!attacker || !victim || !damage || attacker == victim || !attacker->IsAlive())
        return;
    Player* paladin = victim->ToPlayer();
    if (!paladin || GetClassPerk(paladin) != SPELL_PALADIN_PROTECTION)
        return;
    if (!HasAuraRankOf(paladin, SPELL_DEVOTION_AURA))
        return;
    uint32 const guid = paladin->GetGUID().GetCounter();
    if (!g_reentryGuard.insert(guid).second)
        return;
    uint32 const thorns = uint32(float(paladin->GetArmor()) * PALADIN_THORNS_PCT * PALADIN_THORNS_DMG_MULT);
    if (thorns)
        Unit::DealDamage(paladin, attacker, thorns, nullptr, SPELL_DIRECT_DAMAGE, SPELL_SCHOOL_MASK_HOLY, nullptr, false);
    g_reentryGuard.erase(guid);
}

// Thorns x20 on the whole party/raid. Same observe-only shape as the Druid
// Balance party tick: re-cast anyone who is missing the aura (or is carrying a
// weaker rank) with the paladin's own best rank, so a raid keeps the buff
// through deaths and relogs without ever touching a fresh application.
// The x20 is perk-gated via CastCustomSpell basepoints -- the DAMAGE_SHIELD
// aura's amount is computed from the basepoints at apply time, so every aura
// this tick applies hits for 20x the base spell damage. Perk-less paladins
// (and druids' own casts) keep the stock numbers.

// Which effect of a Thorns rank carries the DAMAGE_SHIELD aura, and its
// x20 basepoint. All ranks share the shape (effect 0), but walking the list
// keeps this honest against future data changes.
int32 ThornsBasePointsX20(SpellInfo const* info)
{
    if (!info)
        return 0;
    for (uint8 i = EFFECT_0; i < MAX_SPELL_EFFECTS; ++i)
    {
        if (info->Effects[i].Effect && info->Effects[i].ApplyAuraName == SPELL_AURA_DAMAGE_SHIELD)
        {
            int32 const bp = info->Effects[i].CalcValue();
            return bp * int32(PALADIN_THORNS_DMG_MULT);
        }
    }
    return 0;
}

void TickPaladinProtThorns(Player* player, TickState& st, uint32 diff)
{
    if (!player || GetClassPerk(player) != SPELL_PALADIN_PROTECTION || !player->IsAlive())
        return;
    if (!player->HasAura(SPELL_THORNS_R1))
    {
        if (SpellInfo const* rank = sSpellMgr->GetSpellInfo(BestOwnedOrFirst(player, SPELL_THORNS_R1)))
            if (int32 const bp = ThornsBasePointsX20(rank))
                player->CastCustomSpell(player, rank->Id, &bp, nullptr, nullptr, true);
    }
    Group* group = player->GetGroup();
    if (!group)
        return;
    st.acc += diff;
    if (st.acc < PALADIN_THORNS_PARTY_TICK_MS)
        return;
    st.acc = 0;
    uint32 const paladinRank = BestOwnedOrFirst(player, SPELL_THORNS_R1);
    for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
    {
        Player* member = itr->GetSource();
        if (!member || member == player || !member->IsAlive() || !member->IsInWorld())
            continue;
        if (!player->IsInMap(member))
            continue;
        if (!member->HasAura(SPELL_THORNS_R1))
        {
            if (SpellInfo const* rank = sSpellMgr->GetSpellInfo(paladinRank))
                if (int32 const bp = ThornsBasePointsX20(rank))
                    member->CastCustomSpell(member, paladinRank, &bp, nullptr, nullptr, true);
        }
    }
}

// -------------------------------------------------------------------------
// Script hooks
// -------------------------------------------------------------------------
class ClassPerksPlayer : public PlayerScript
{
public:
    ClassPerksPlayer() : PlayerScript("LivingGearClassPerksPlayer", {
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_LOGOUT,
        PLAYERHOOK_ON_UPDATE,
        PLAYERHOOK_ON_SPELL_CAST,
        PLAYERHOOK_ON_PLAYER_LEAVE_COMBAT,
        PLAYERHOOK_ON_PLAYER_RESURRECT,
        PLAYERHOOK_ON_BEFORE_TEMP_SUMMON_INIT_STATS
    }) { }

    void OnPlayerResurrect(Player* player, float /*restore_percent*/, bool& /*applySickness*/) override
    {
        ApplyRogueCombatBladeFlurry(player);
    }

    // Report #167: the Fire Elemental guardians (vanilla + twin) come through
    // here as player-owned temp summons -- shrink them, drop their duration
    // to zero (permanent until killed) and book them for pack replacement.
    void OnPlayerBeforeTempSummonInitStats(Player* player, TempSummon* tempSummon, uint32& duration) override
    {
        ApplyShamanElementalGuardianSummon(player, tempSummon, duration);
    }

    void OnPlayerLogin(Player* player) override
    {
        if (!player || !player->GetSession())
            return;
        DetectSchema();
        LoadClassPerk(player->GetGUID().GetCounter());
        GrantAndBroadcastClassPerks(player);
        // A character that picked a spec before this existed, or levelled past
        // a rank breakpoint since, gets topped up here. Passing the same perk
        // as both arguments means nothing is revoked.
        if (uint32 const selected = GetClassPerk(player))
            ApplyClassPerkSpells(player, selected, selected);
        ApplyRogueCombatBladeFlurry(player);
        // Not GrantMageFrostBlizzard: ApplyClassPerkSpells above already
        // hands Blizzard over AND records it, which is what makes the revoke
        // in report #54 possible. Granting it twice by two different routes is
        // how it became unrevokable in the first place.
    }

    void OnPlayerLogout(Player* player) override
    {
        if (!player)
            return;
        uint32 const guid = player->GetGUID().GetCounter();
        auto it = g_fury.find(guid);
        if (it != g_fury.end() && it->second.appliedPct)
            SetFuryHastePct(player, it->second, 0);
        g_mage.erase(guid);
        g_fury.erase(guid);
        g_rogue.erase(guid);
        g_afflictionTick.erase(guid);
        g_bladestormTick.erase(guid);
        g_eclipseTick.erase(guid);
        g_insectTick.erase(guid);
        g_dkUnholyTick.erase(guid); // Kit 7 Unholy blight tick state
        g_rejuvTick.erase(guid);
        auto army = g_armyGroup.find(guid);
        if (army != g_armyGroup.end())
        {
            for (ObjectGuid const& g : army->second)
                if (Creature* c = ObjectAccessor::GetCreature(*player, g))
                    c->DespawnOrUnsummon();
            g_armyGroup.erase(army);
        }
        auto pack = g_bmPack.find(guid);
        if (pack != g_bmPack.end())
        {
            for (ObjectGuid const& g : pack->second)
                if (Creature* c = ObjectAccessor::GetCreature(*player, g))
                    c->DespawnOrUnsummon();
            g_bmPack.erase(pack);
        }
        auto tendrils = g_voidTendrils.find(guid);
        if (tendrils != g_voidTendrils.end())
        {
            for (ObjectGuid const& g : tendrils->second)
                if (Creature* c = ObjectAccessor::GetCreature(*player, g))
                    c->DespawnOrUnsummon();
            g_voidTendrils.erase(tendrils);
        }
        g_voidTick.erase(guid);
        g_trapZones.erase(guid);
        g_shrapnel.erase(guid);
        // Avenger's Shield bounce chains hold g_reentryGuard for the whole
        // staggered sequence; the pending hop callbacks live in the player's
        // own m_Events, so logging out mid-chain strands the guard forever and
        // silently kills the 6s cooldown conversion AND the Judgement/HotR
        // procs for that character (reported as "AS cooldown still 30s").
        g_reentryGuard.erase(guid);
        auto bears = g_wildsBears.find(guid);
        if (bears != g_wildsBears.end())
        {
            for (ObjectGuid const& g : bears->second)
                if (Creature* c = ObjectAccessor::GetCreature(*player, g))
                    c->DespawnOrUnsummon();
            g_wildsBears.erase(bears);
        }
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        // Crash guard: several ticks below cast auras on the player, and doing
        // that after Player::CleanupsBeforeDelete asserts on !m_cleanupDone and
        // takes the realm down. See LivingGear_SafeToCastOn.
        if (!LivingGear_SafeToCastOn(player))
            return;
        uint32 const selected = GetClassPerk(player);
        if (selected == SPELL_MAGE_ARCANE)
        {
            TickMageArcane(player);
            TickMageArcanePower(player);
        }
        else if (selected == SPELL_MAGE_FIRE)
        {
            TickMageFire(player, g_mage[player->GetGUID().GetCounter()], diff);
        }
        else if (selected == SPELL_MAGE_FROST)
        {
            TickMageFrostBlizzard(player);
            TickMageFrostBlizzardDamage(player, g_mage[player->GetGUID().GetCounter()], diff);
            TickMageFrostIceLance(player, g_mage[player->GetGUID().GetCounter()], diff);
        }
        else if (selected == SPELL_ROGUE_COMBAT)
        {
            TickRogueCombatAdrenalineRush(player);
            // Was a second `else if (selected == SPELL_ROGUE_COMBAT)` further
            // down this same chain, which is unreachable -- so Combat's entire
            // "+50% energy regeneration" clause never ran. Merged into the one
            // branch that is actually entered.
            TickRogueCombat(player, g_rogue[player->GetGUID().GetCounter()], diff);
        }
        else if (selected == SPELL_WARRIOR_ARMS)
        {
            TickWarriorArmsBladestorm(player, g_bladestormTick[player->GetGUID().GetCounter()], diff);
        }
        else if (selected == SPELL_SHAMAN_ENHANCEMENT)
        {
            TickShamanEnhWolves(player);
            TickShamanEnhStaticField(player, g_shamanStaticTick[player->GetGUID().GetCounter()], diff);
        }
        else if (selected == SPELL_HUNTER_SURVIVAL)
        {
            TickHunterSurvivalTrapZones(player, diff);
        }
        else if (selected == SPELL_PALADIN_PROTECTION)
        {
            // Thorns x20 kept on the paladin and every party/raid member.
            TickPaladinProtThorns(player, g_thornsTick[player->GetGUID().GetCounter()], diff);
        }
        else if (selected == SPELL_WARLOCK_AFFLICTION)
        {
            TickWarlockAffliction(player, g_afflictionTick[player->GetGUID().GetCounter()], diff);
        }
        else if (selected == SPELL_WARLOCK_DEMONOLOGY)
        {
            TickWarlockDemo(player);
        }
        else if (selected == SPELL_DRUID_BALANCE)
        {
            TickDruidBalanceEclipse(player, g_eclipseTick[player->GetGUID().GetCounter()], diff);
            TickDruidBalanceStarfall(player);
            TickDruidBalanceThorns(player, g_thornsTick[player->GetGUID().GetCounter()], diff);
            TickDruidBalanceHurricane(player, g_eclipseTick[player->GetGUID().GetCounter()], diff);
            TickDruidBalanceInsectContagion(player, g_insectTick[player->GetGUID().GetCounter()], diff);
        }
        else if (selected == SPELL_DRUID_RESTORATION)
        {
            TickDruidRestRejuvSpread(player, g_rejuvTick[player->GetGUID().GetCounter()], diff);
        }
        else if (selected == SPELL_PRIEST_SHADOW)
        {
            TickPriestShadowVoidform(player, g_voidTick[player->GetGUID().GetCounter()], diff);
        }
        else if (selected == SPELL_DK_UNHOLY)
        {
            // Kit 7: permanent blight aura (disease apply/refresh), Unholy
            // Blight visual marker, gargoyle timer refresh.
            TickDkUnholyBlight(player, g_dkUnholyTick[player->GetGUID().GetCounter()], diff);
        }
        if (player->getClass() == CLASS_WARRIOR)
        {
            TickWarriorFury(player);
            // Called for every warrior, not just Prot: the function itself
            // revokes the Avalanche block passive when the perk is switched
            // away or the circle expires.
            TickWarriorProtAvalanche(player, g_protAvalancheTick[player->GetGUID().GetCounter()], diff);
        }
    }

    void OnPlayerSpellCast(Player* player, Spell* spell, bool /*skip*/) override
    {
        if (!player || !spell)
            return;
        SpellInfo const* info = spell->GetSpellInfo();
        if (!info)
            return;
        if (!spell->IsTriggered() && CanSelectClassPerk(player, info->Id))
        {
            SelectClassPerk(player, info->Id);
            return;
        }
        if (spell->IsTriggered())
            return;
        TryMageFireOnCast(player, spell);
        TryMageFireDetonate(player, spell);
        TryMageArcaneOnCast(player, spell);
        TryMageFrostBlizzardLinger(player, spell);
        TryPaladinProtOnCast(player, spell);
        TryPaladinProtJudgementProc(player, spell);
        TryWarriorArmsOnCast(player, spell);
        TryWarriorProtOnCast(player, spell);
        TryRogueCombatKillingSpree(player, spell);
        TryRogueCombatAdrenalineRush(player, spell);
        RefundRogueCombatEnergy(player, spell);
        TryRogueAssassinationDetonate(player, spell);
        TryHunterMMOnCast(player, spell);
        TryShamanElementalOnCast(player, spell);
        TryDkUnholyOnCast(player, spell);
        TryHunterBmOnCast(player, spell);
        TryHunterSurvivalOnCast(player, spell);
        TryShamanEnhOnCast(player, spell);
        TryShamanRestOnCast(player, spell);
        TryWarlockAfflictionOnCast(player, spell);
        TryWarlockDemoOnCast(player, spell);
        TryWarlockDestroOnCast(player, spell);
        TryDruidBalanceInsectSpread(player, spell);
        TryDruidBalanceOnCast(player, spell);
        // Report #120: after the Starfall no-cooldown bookkeeping above, so
        // the free twins below are ordinary triggered casts.
        TryDruidBalanceMoonfireHurricane(player, spell);
        TryDruidBalanceStarPair(player, spell);
        TryDruidFeralOnCast(player, spell);
        TryDruidRestOnCast(player, spell);
        TryPriestDiscOnCast(player, spell);
        TryPriestHolyOnCast(player, spell);
        TryPriestShadowOnCast(player, spell);
        TryPriestShadowMindBlastDetonate(player, spell);
        TryDkBloodOnCast(player, spell);
        TryDkFrostOnCast(player, spell);
    }

    void OnPlayerLeaveCombat(Player* player) override
    {
        // CombatStop() runs AFTER m_cleanupDone is set (Unit.cpp:12745 then
        // :12747), so this hook fires on a torn-down player when someone logs
        // out in combat. Same guard as the ticks. See the note on the matching
        // handler in LivingGear_Perks.cpp.
        if (!LivingGear_SafeToCastOn(player))
            return;
        OnLeaveCombatMage(player);
    }
};

class ClassPerksSpell : public AllSpellScript
{
public:
    ClassPerksSpell() : AllSpellScript("LivingGearClassPerksSpell", {
        ALLSPELLHOOK_ON_PREPARE,
        ALLSPELLHOOK_ON_SPELL_CHECK_CAST
    }) { }

    // Toggle-off lives here rather than in the cast hook. See the long note
    // above TryDruidBalanceStarfallToggleOff for why that ordering matters.
    void OnSpellCheckCast(Spell* spell, bool strict, SpellCastResult& res) override
    {
        TryDruidBalanceStarfallToggleOff(spell, strict, res);
        TryWarriorArmsBladestormToggleOff(spell, strict, res);
        TryWarlockDemoLegionToggleOff(spell, strict, res);
        TryPriestShadowVoidformToggleOff(spell, strict, res);
        TryShamanEnhStaticFieldToggleOff(spell, strict, res);
        TryWarriorProtAvalancheToggleOff(spell, strict, res);
    }

    void OnSpellPrepare(Spell* spell, Unit* caster, SpellInfo const* spellInfo) override
    {
        if (!spell || !caster || !spellInfo || !caster->IsPlayer())
            return;
        ApplyProtThunderClapRadius(spell, caster->ToPlayer(), spellInfo);
        ApplyShamanElementalChainLightningTargets(spell, caster->ToPlayer(), spellInfo);
        ApplyHunterSurvivalTrapRadius(spell, caster->ToPlayer(), spellInfo);
        ApplyShamanRestChainHealTargets(spell, caster->ToPlayer(), spellInfo);
        ApplyDruidRestWildGrowthTargets(spell, caster->ToPlayer(), spellInfo);
    }
};

class ClassPerksUnit : public UnitScript
{
public:
    ClassPerksUnit() : UnitScript("LivingGearClassPerksUnit", true, {
        UNITHOOK_ON_DAMAGE,
        UNITHOOK_MODIFY_SPELL_DAMAGE_TAKEN,
        UNITHOOK_MODIFY_MELEE_DAMAGE,
        UNITHOOK_MODIFY_PERIODIC_DAMAGE_AURAS_TICK,
        UNITHOOK_ON_CALCULATE_THREAT
    }) { }

    // Report #91: "Add 1000% aggro gen on warrior protection perk, can't keep
    // anything attacking me as tank because everyone hits too hard." The
    // Protection warrior multiplies every point of threat it generates by 11
    // (+1000%). Fires for melee, spells, and periodic damage alike because the
    // hook sits at the bottom of ThreatManager::AddThreat, after the engine's
    // own modifiers -- so taunts, dps-threat and heal-threat all hold.
    // Report #177: x11 was not enough -- tanks were still losing aggro, and
    // the ask went to +10000%, so the multiplier is now x100 for the tank
    // specs. The other tank specs carry the same x100: Protection paladin
    // always, Feral druid only in Bear/Dire Bear Form, Blood DK always.
    void OnCalculateThreat(Unit* attacker, Unit* victim, float& threat, SpellInfo const* /*spell*/) override
    {
        if (!attacker || threat <= 0.0f)
            return;
        Player* player = attacker->ToPlayer();
        if (!player)
            return;
        if (victim && !player->IsValidAttackTarget(victim))
            return;

        bool isTankSpec = false;
        switch (GetClassPerk(player))
        {
            case SPELL_WARRIOR_PROTECTION:
            case SPELL_PALADIN_PROTECTION:
            case SPELL_DK_BLOOD:
                isTankSpec = true;
                break;
            case SPELL_DRUID_FERAL:
            {
                ShapeshiftForm form = player->GetShapeshiftForm();
                isTankSpec = form == FORM_BEAR || form == FORM_DIREBEAR;
                break;
            }
            default:
                break;
        }

        if (isTankSpec)
        {
            threat *= 100.0f;
            return;
        }

        // Report #211: everyone else generates 99% less threat, so dps and
        // healers stop ripping mobs off the tank the moment burst damage or
        // a big heal lands. Tank specs above keep their x100.
        threat *= 0.01f;
    }

    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        if (!attacker || !victim || !damage)
            return;
        ApplyDevotionDR(victim, damage);
        TryProtThorns(attacker, victim, damage);
        TryDruidBalanceInsectOnStruck(attacker, victim);
        ApplyShamanElementalGuardianDamage(attacker, damage);
    }

    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellInfo const* spellInfo) override
    {
        if (!attacker || damage <= 0 || !spellInfo)
            return;
        ApplyProtShockwaveDamage(attacker, damage, spellInfo);
        ApplyShamanElementalLavaBurstDamage(attacker, damage, spellInfo);
        ApplyHunterSurvivalExplosiveShotDamage(attacker, damage, spellInfo);
        if (target)
            ApplyHunterSurvivalShrapnel(target, attacker, damage, spellInfo);
        ApplyPriestShadowMindFlayDamage(attacker, damage, spellInfo);
        ApplyDkFrostDamage(attacker, damage, spellInfo);
        ApplyMageFireDamage(attacker, damage, spellInfo);
        ApplyMageFrostDamage(attacker, damage, spellInfo);
        ApplyMageArcaneDamage(attacker, damage, spellInfo);
        ApplyDruidBalanceDamage(attacker, damage, spellInfo);
        TryDruidBalanceStarfallMoonfire(target, attacker, spellInfo);
        ApplyWarlockDemoPetSpellDamage(attacker, damage);
        if (target)
            ApplyHunterSurvivalTrapImmunity(attacker, target, damage, spellInfo);
    }

    void ModifyMeleeDamage(Unit* /*target*/, Unit* attacker, uint32& damage) override
    {
        if (Player* player = attacker ? attacker->ToPlayer() : nullptr)
        {
            NoteFuryMeleeHit(player);
            NoteDkBloodMeleeHit(player, damage);
        }
        ApplyShamanEnhWolfMeleeDamage(attacker, damage);
        ApplyWarlockDemoPetDamage(attacker, damage);
        TryRogueCombatFlurrySpread(attacker, damage);
    }

    void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage, SpellInfo const* spellInfo) override
    {
        if (!attacker || !damage || !spellInfo)
            return;
        ApplyWarlockAfflictionHaste(target, attacker, damage, spellInfo);
        ApplyMageFirePeriodic(attacker, damage, spellInfo);
        ApplyFuryBleedPeriodic(attacker, damage, spellInfo);
        ApplyProtTCBleedPeriodic(attacker, damage, spellInfo);
        // Kit 7: Unholy DK diseases (Blood Plague / Frost Fever / Ebon
        // Plague) tick for 5x damage.
        ApplyDkUnholyDiseaseDamage(attacker, damage, spellInfo);
    }
};

class ClassPerksWorld : public WorldScript
{
public:
    ClassPerksWorld() : WorldScript("LivingGearClassPerksWorld", {
        WORLDHOOK_ON_STARTUP
    }) { }

    void OnStartup() override
    {
        DetectSchema();
        PatchBlizzardServerSide();
        PatchHurricaneServerSide();
        LOG_INFO("server.loading",
            "Living Gear class perks module loaded (all 10 classes, 3 specs each except "
            "Paladin Holy/Retribution and Rogue Assassination/Subtlety, which live in other files)");
    }

private:
    // build_patch.py patches the CLIENT's Spell.dbc so Blizzard *looks*
    // instant/uncancelled, but the server loads its own independent copy of
    // spell data and was still enforcing the real channel (move = interrupt,
    // real cast/cooldown timers) underneath that visual. Mirror the same
    // "instant, no cooldown, not channeled" change server-side so it isn't
    // just cosmetic.
    void PatchBlizzardServerSide()
    {
        SpellCastTimesEntry const* instant = sSpellCastTimesStore.LookupEntry(1);
        uint32 n = 0;
        for (uint32 id : SPELL_BLIZZARD_RANKS)
        {
            SpellInfo* info = const_cast<SpellInfo*>(sSpellMgr->GetSpellInfo(id));
            if (!info)
                continue;
            info->AttributesEx &= ~(SPELL_ATTR1_IS_CHANNELED | SPELL_ATTR1_IS_SELF_CHANNELED);
            info->CastTimeEntry = instant;
            info->RecoveryTime = 0;
            info->CategoryRecoveryTime = 0;
            info->StartRecoveryTime = 0;
            info->InterruptFlags = 0;
            info->ChannelInterruptFlags = 0;
            ++n;
        }
        LOG_INFO("server.loading", "Living Gear: patched {} Blizzard ranks server-side (instant, uncancellable, lingers like Death and Decay)", n);
    }

    // Report #120: same treatment for Hurricane so the free Hurricane from
    // Moonfire places-and-damages like Blizzard instead of channeling the
    // druid in place. Only the channel flags are cleared here -- the cast
    // time stays native because the free cast goes out with
    // TRIGGERED_FULL_MASK (instant and free regardless), and a hand-cast
    // Hurricane by a non-perk druid is unaffected because this runs in the
    // module's world-load path for the Balance perk file regardless of who
    // holds the perk; a non-Balance druid's hand Hurricane simply casts
    // instantly too, which is consistent with how Blizzard was already
    // shipped server-wide.
    void PatchHurricaneServerSide()
    {
        uint32 n = 0;
        for (uint32 id : SPELL_HURRICANE_RANKS)
        {
            SpellInfo* info = const_cast<SpellInfo*>(sSpellMgr->GetSpellInfo(id));
            if (!info)
                continue;
            info->AttributesEx &= ~(SPELL_ATTR1_IS_CHANNELED | SPELL_ATTR1_IS_SELF_CHANNELED);
            info->InterruptFlags = 0;
            info->ChannelInterruptFlags = 0;
            ++n;
        }
        LOG_INFO("server.loading", "Living Gear: patched {} Hurricane ranks server-side (de-channeled, places and damages like Death and Decay)", n);
    }
};

} // namespace LivingGearClassPerks

uint32 GetClassPerk(Player* player)
{
    return LivingGearClassPerks::GetClassPerk(player);
}

// Core-patch callback from Spell::prepare. An ability a class perk advertises
// as free has to cost nothing when CheckPower runs, because a refund after the
// cast still requires the caster to have had the power to spend -- which is
// why Bladestorm kept answering "Not enough rage" on an empty bar.
bool LivingGear_SpellIsFreeCast(Unit* caster, uint32 spellId)
{
    if (!caster || spellId != LivingGearClassPerks::SPELL_BLADESTORM)
        return false;
    Player* player = caster->ToPlayer();
    return player
        && LivingGearClassPerks::GetClassPerk(player) == LivingGearClassPerks::SPELL_WARRIOR_ARMS;
}

// Core-patch callback from Spell::prepare (0030). Haunt is instant for
// Affliction perk holders only -- other specs keep the real cast time. Runs
// at the same point as CHEAT_CASTTIME so hasted and cheat paths compose the
// same way, and matches the whole Haunt chain so a future rank cannot regress.
bool LivingGear_SpellIsInstantCast(Unit* caster, uint32 spellId)
{
    if (!caster)
        return false;
    Player* player = caster->ToPlayer();
    if (!player || LivingGearClassPerks::GetClassPerk(player) != LivingGearClassPerks::SPELL_WARLOCK_AFFLICTION)
        return false;
    return spellId
        && sSpellMgr->GetFirstSpellInChain(spellId) == LivingGearClassPerks::SPELL_HAUNT;
}

// Addon-command entry point, called by the dispatcher in LivingGear.cpp.
// The client's Class-tab buttons send "CLASS|<id>" (LivingGear.lua,
// ui.classBtns OnClick).
bool LivingGear_HandleClassPerksCommand(Player* player, std::string const& msg)
{
    uint32 spellId = 0;
    if (sscanf(msg.c_str(), "CLASS|%u", &spellId) != 1)
        return false;
    // Yorgen (priest) reports the three cards render but no click does
    // anything. Every link reads correct: the account owns 910163-910165, the
    // spells are learned, CanSelectClassPerk handles CLASS_PRIEST,
    // GrantAndBroadcastClassPerks runs at login and on REQ, the CPKALL handler
    // parses, and LayoutClass sets btn.ownClass through LayoutRows.
    //
    // So stop reading and measure. This says whether the click reaches the
    // server at all, and if it does, exactly which gate rejects it. Cheap, and
    // it splits the problem in half instead of producing another theory.
    bool const owns = LivingGearClassPerks::HasPerk(player, spellId);
    bool const selectable = LivingGearClassPerks::CanSelectClassPerk(player, spellId);
    LOG_INFO("module.livinggear",
        "CLASS| from {} (class {}) for perk {}: owns={} selectable={} current={}",
        player ? player->GetName() : "?", player ? uint32(player->getClass()) : 0,
        spellId, owns, selectable, LivingGearClassPerks::GetClassPerk(player));
    LivingGearClassPerks::SelectClassPerk(player, spellId);
    return true;
}

// CPKALL was login-only, so the Class tab was blank after any /reload
// until the player logged out and back in. Idempotent: UnlockPerk skips
// anything already known.
void LivingGear_SendClassPerksSync(Player* player)
{
    LivingGearClassPerks::GrantAndBroadcastClassPerks(player);
}

void AddSC_LivingGearClassPerks()
{
    new LivingGearClassPerks::ClassPerksWorld();
    new LivingGearClassPerks::ClassPerksPlayer();
    new LivingGearClassPerks::ClassPerksSpell();
    new LivingGearClassPerks::ClassPerksUnit();
    new LivingGearClassPerks::npc_lg_army_ghoul_tank();
    new LivingGearClassPerks::npc_lg_army_ghoul_healer();
    new LivingGearClassPerks::npc_lg_army_ghoul_dps();
}
