from pathlib import Path

p = Path(r"A:/wow-bonesaw/modules/mod-living-gear/src/LivingGear.cpp")
t = p.read_text(encoding="utf-8")

globals_block = r'''
std::unordered_set<uint32> g_loginReady;
std::unordered_map<uint32, uint32> g_loginMs;
std::unordered_set<uint32> g_chatOn;
std::unordered_map<uint64, uint32> g_recentQuestUse;
bool g_syncing = false;
std::unordered_set<uint32> g_addonClient;
std::unordered_set<uint32> g_firstTick;
std::unordered_map<uint32, uint32> g_cookRegenMs;
std::unordered_map<uint32, uint32> g_collectionPassiveMs;
std::unordered_map<uint32, std::unordered_map<uint32, uint32>> g_absorbPassiveXp;
std::unordered_map<uint32, uint8> g_killCombo;
std::unordered_map<uint32, uint8> g_groupKillCombo;
std::unordered_map<uint32, uint32> g_pendingExtraJumpMs;
std::unordered_map<uint32, uint32> g_lastJumpXY;
std::unordered_map<uint32, uint32> g_lastMount;
std::unordered_map<uint32, uint8> g_mountOpenerState;
std::unordered_map<uint32, std::vector<uint32>> g_turnInQueue;
std::unordered_map<uint32, uint8> g_turnInShowing;
std::unordered_map<uint32, ObjectGuid> g_turnInHelper;
std::unordered_map<uint32, uint32> g_turnInReadyMs;
std::unordered_map<uint32, uint32> g_recentLearnMs;
std::unordered_map<uint32, uint32> g_lastAutoQuestMs;
std::unordered_map<uint32, uint8> g_zoneLevelOverride;
std::unordered_set<uint32> g_dungeonRareEnsured;
struct FuryHasteState
{
    uint8 stacks = 0;
    uint32 lastHitMs = 0;
};
std::unordered_map<uint32, FuryHasteState> g_furyHaste;
struct ZoneScaleState
{
    uint8 zoneLevel = 0;
    uint8 effectiveLevel = 0;
    bool active = false;
};
std::unordered_map<uint32, ZoneScaleState> g_zoneScaleState;
struct DungeonRunState
{
    uint32 startMs = 0;
};
std::unordered_map<uint32, DungeonRunState> g_dungeonRuns;
'''

if "bool g_syncing" not in t:
    needle = "std::unordered_map<uint32, uint32> g_lastAidCleanse;"
    if needle not in t:
        raise SystemExit("g_lastAidCleanse missing")
    t = t.replace(needle, needle + globals_block, 1)
    print("inserted globals")

consts = """
uint8 const AUTO_ATTUNE_MAX_QUALITY = 4;
uint32 const JUMP_BOOST_MS = 800;
uint8 const FURY_HASTE_CAP = 5;
uint32 const FURY_HASTE_LINGER_MS = 3000;
uint32 const KILL_COMBO_MS = 180000;
uint8 const KILL_COMBO_CAP = 100;
uint32 const KILL_COMBO_SPEED_PCT = 1;
float const KILL_COMBO_XP = 0.03f;
uint32 const MENU_WINDBLOWN = 910001;
uint32 const ACT_MAIN = 0;
uint32 const ACT_ATTUNE = 100;
uint32 const ACT_DETAIL = 200;
"""
if "uint32 const KILL_COMBO_MS" not in t:
    # after EXTRA_JUMP_Z if present else after FIRST_AID_MAX
    if "float const EXTRA_JUMP_Z" in t:
        t = t.replace("float const EXTRA_JUMP_Z = 8.5f;", "float const EXTRA_JUMP_Z = 8.5f;" + consts, 1)
    else:
        t = t.replace("uint32 const FIRST_AID_MAX = 450;", "uint32 const FIRST_AID_MAX = 450;" + consts, 1)
    print("inserted consts")

if "dungeonRareAlwaysSpawn" not in t:
    t = t.replace(
        "    bool zoneScaleNotify = true;\n",
        "    bool zoneScaleNotify = true;\n"
        "    bool dungeonRareAlwaysSpawn = false;\n"
        "    bool collectionPassiveEnabled = true;\n"
        "    uint32 collectionPassiveXp = 1;\n"
        "    uint32 collectionPassiveTickMs = 10000;\n",
        1,
    )
    t = t.replace(
        '        zoneScaleNotify = sConfigMgr->GetOption<bool>("LivingGear.ZoneScale.Notify", true);\n    }',
        '        zoneScaleNotify = sConfigMgr->GetOption<bool>("LivingGear.ZoneScale.Notify", true);\n'
        '        dungeonRareAlwaysSpawn = sConfigMgr->GetOption<bool>("LivingGear.DungeonRareAlwaysSpawn", false);\n'
        '        collectionPassiveEnabled = sConfigMgr->GetOption<bool>("LivingGear.CollectionPassive.Enable", true);\n'
        '        collectionPassiveXp = sConfigMgr->GetOption<uint32>("LivingGear.CollectionPassive.Xp", 1);\n'
        '        collectionPassiveTickMs = sConfigMgr->GetOption<uint32>("LivingGear.CollectionPassive.TickMs", 10000);\n'
        "        if (collectionPassiveTickMs < 1000)\n"
        "            collectionPassiveTickMs = 1000;\n"
        "    }",
        1,
    )
    print("inserted config fields")

stubs = r'''
void CastFindQuests(Player* player) { (void)player; }
void AutoAcceptZoneQuests(Player* player) { (void)player; }
void PatchBladestormSpell() {}
void PatchKillComboSpell() {}
void PatchBlizzardSpells() {}
void PatchDungeonClearSpells() {}
void LoadDungeonPar() {}
static float DungeonEventPaceMultiplier(Player* player) { (void)player; return 1.0f; }
static void StartDungeonRun(Player* player)
{
    if (!player || !player->GetMap())
        return;
    Map* map = player->GetMap();
    if (!map->IsDungeon())
        return;
    DungeonRunState& run = g_dungeonRuns[map->GetInstanceId()];
    if (!run.startMs)
        run.startMs = getMSTime();
}
static void StopDungeonRunForPlayer(Player* player) { (void)player; }
static void NoteDungeonBossKill(Player* player, Creature* killed) { (void)player; (void)killed; }
static void EnsureDungeonRareSpawns(Map* map) { (void)map; }
void ShowWindblownMain(Player* player) { (void)player; }
void ShowWindblownDetail(Player* player, uint8 slot) { (void)player; (void)slot; }
void CheckTravelSwimPerk(Player* player, bool scanAccount) { (void)player; (void)scanAccount; }
void ApplyTravelSwimAura(Player* player) { (void)player; }
void ApplyLgMoveSpeed(Player* player) { (void)player; }
void AssignClassPerkFromSpec(Player* player, bool announce) { (void)player; (void)announce; }
void CheckAutoAttunePerk(Player* player, bool scanAccount) { (void)player; (void)scanAccount; }
void CheckCollectionPassivePerk(Player* player, bool scanAccount) { (void)player; (void)scanAccount; }
void BoostJump(Player* player, MovementInfo const& move) { (void)player; (void)move; }
void AutoAcceptFollowups(Player* player, Quest const* completed) { (void)player; (void)completed; }
void SpeedUpFishingBobber(Player* player) { (void)player; }
void ApplyRogueSubtletyPerkSpells(Player* player, uint32 selected) { (void)player; (void)selected; }
void DismissShadowClone(Player* player) { (void)player; }
void DismissShadowTraps(Player* player) { (void)player; }
void CastJackInTheBox(Player* player, Spell* spell) { (void)player; (void)spell; }
void SummonShadowClonePet(Player* player) { (void)player; }
void MirrorShadowCloneCast(Player* player, SpellInfo const* info, Unit* target) { (void)player; (void)info; (void)target; }
void ClearSpellAndCategoryCooldown(Player* player, uint32 spellId)
{
    if (player && spellId)
        player->RemoveSpellCooldown(spellId, true);
}
bool PlayerWorldReady(Player* player) { return LoginSettled(player); }
void SaveJumpMode(uint32 accountId) { (void)accountId; }
void SaveUiScale(uint32 accountId) { (void)accountId; }
void SaveAutoAttuneSettings(uint32 accountId) { (void)accountId; }
void SaveAttunedDe(uint32 accountId) { (void)accountId; }
void ApplyDevotionSpeed(Player* player) { (void)player; }
void ApplyZoneScaleLootSeed(Player* player, Loot* loot) { (void)player; (void)loot; }
void CastAutoQuest(Player* player) { (void)player; }
void RemoveAllConsecration(Player* player) { (void)player; }
void TryAutoMount(Player* player) { (void)player; }

'''

if "void CastFindQuests(Player* player) { (void)player; }" not in t:
    marker = "class LivingGearPlayer : public PlayerScript"
    if marker not in t:
        raise SystemExit("LivingGearPlayer missing")
    t = t.replace(marker, stubs + marker, 1)
    print("inserted stubs")

if "uint32 BagCountWithoutVault(Player const* player, uint32 itemEntry);" not in t:
    t = t.replace(
        "void SaveRules(uint32 accountId);",
        "void SaveRules(uint32 accountId);\n"
        "uint32 ExtraVaultCount(Player const* player, uint32 itemEntry);\n"
        "uint32 BagCountWithoutVault(Player const* player, uint32 itemEntry);\n"
        "void SyncDeliverQuests(Player* player);",
        1,
    )
    print("inserted bag/sync decls")

p.write_text(t, encoding="utf-8", newline="\n")
print("final nl", t.count("\n"), "starts", t[:3])
