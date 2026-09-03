# Patch restored LivingGear.cpp with remaining compile identifiers.
# Writes .next then copies to live only if header/AddSC/size checks pass.

from pathlib import Path
import hashlib
import re
import sys

SRC = Path(r"A:\wow-bonesaw\modules\mod-living-gear\src")
LIVE = SRC / "LivingGear.cpp"
WIP = SRC / "LivingGear.cpp.wip-restore"
BACKUP = SRC / "LivingGear.cpp.backup-20260818"
EXPECT = "8A949243827DDEE91B50469309FF22AB48CF418FCE43529622CE5D486EBEB475"
OUT_NEXT = SRC / "LivingGear.cpp.next"

got = hashlib.sha256(BACKUP.read_bytes()).hexdigest().upper()
if got != EXPECT:
    print("BACKUP HASH MISMATCH", got, file=sys.stderr)
    sys.exit(1)

t = LIVE.read_text(encoding="utf-8").replace("\r\n", "\n")
wip = WIP.read_text(encoding="utf-8").replace("\r\n", "\n")

if not t.startswith("/*"):
    print("REFUSE: live truncated", file=sys.stderr)
    sys.exit(2)


def brace_delta(s):
    out = []
    i = 0
    in_str = False
    while i < len(s):
        if not in_str and s[i : i + 2] == "//":
            break
        c = s[i]
        if c == '"':
            esc = 0
            j = i - 1
            while j >= 0 and s[j] == "\\":
                esc += 1
                j -= 1
            if esc % 2 == 0:
                in_str = not in_str
            out.append(" ")
        elif in_str:
            out.append(" ")
        else:
            out.append(c)
        i += 1
    x = "".join(out)
    return x.count("{") - x.count("}")


def extract_fn(src, header, cap=400):
    lines = src.split("\n")
    i = next(idx for idx, l in enumerate(lines) if l.rstrip() == header.rstrip())
    local = 0
    started = False
    k = i
    n = len(lines)
    while k < n and (k - i) < cap:
        if "{" in lines[k]:
            started = True
        local += brace_delta(lines[k])
        k += 1
        if started and local <= 0:
            return "\n".join(lines[i:k])
    raise SystemExit("extract failed " + header)


if "uint32 const SPELL_CRAFT_2 =" not in t:
    t = t.replace(
        "uint32 const SPELL_CRAFT_1 = 910093;\nuint32 const SPELL_CRAFT_5 = 910097;",
        """uint32 const SPELL_CRAFT_1 = 910093;
uint32 const SPELL_CRAFT_2 = 910094;
uint32 const SPELL_CRAFT_3 = 910095;
uint32 const SPELL_CRAFT_4 = 910096;
uint32 const SPELL_CRAFT_5 = 910097;""",
        1,
    )

if "uint32 const KILL_COMBO_MS =" not in t:
    t = t.replace(
        "float const EXTRA_JUMP_Z = 8.5f;\nuint32 const SPELL_THROW = 2764;",
        """float const EXTRA_JUMP_Z = 8.5f;
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
uint32 const SPELL_THROW = 2764;""",
        1,
    )

if "std::unordered_map<uint32, uint32> g_cookRegenMs;" not in t:
    t = t.replace(
        """std::unordered_map<uint64, uint32> g_recentQuestUse;
struct LgMetaColumns
""",
        """std::unordered_map<uint64, uint32> g_recentQuestUse;
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
struct LgMetaColumns
""",
        1,
    )

if "bool IsCraftingSpell(SpellInfo const* info);" not in t:
    t = t.replace(
        "bool IsFishingSpell(uint32 spellId);\nbool TryUnlockLock",
        """bool IsFishingSpell(uint32 spellId);
bool IsCraftingProfessionSkill(uint32 skillId);
bool IsCraftingSpell(SpellInfo const* info);
float CraftTimeMult(Player* player);
bool TryUnlockLock""",
        1,
    )

dup = """void SeedAccountRep(uint32 accountId);
bool AccountHasPerk(uint32 accountId, uint32 spellId);
static bool IsRandomAiBot(Player* player);
bool AccountHasPerk(uint32 accountId, uint32 spellId);
static bool IsRandomAiBot(Player* player);
bool AccountHasPerk(uint32 accountId, uint32 spellId);
static bool IsRandomAiBot(Player* player);
bool AccountHasPerk(uint32 accountId, uint32 spellId);
static bool IsRandomAiBot(Player* player);
bool AccountHasPerk(uint32 accountId, uint32 spellId);
static bool IsRandomAiBot(Player* player);
"""
t = t.replace(
    dup,
    """void SeedAccountRep(uint32 accountId);
bool AccountHasPerk(uint32 accountId, uint32 spellId);
static bool IsRandomAiBot(Player* player);
""",
    1,
)

if "uint32 const SPELL_CRAFT[]" not in t:
    craft_fns = "\n\n".join(
        [
            extract_fn(wip, "bool IsCraftingProfessionSkill(uint32 skillId)"),
            extract_fn(wip, "bool IsCraftingSpell(SpellInfo const* info)"),
            "uint32 const SPELL_CRAFT[] = { SPELL_CRAFT_1, SPELL_CRAFT_2, SPELL_CRAFT_3, SPELL_CRAFT_4, SPELL_CRAFT_5 };\n"
            "uint32 const CRAFT_BREAKS[] = { 75, 150, 225, 300, 375 };",
            extract_fn(wip, "float CraftTimeMult(Player* player)"),
            extract_fn(wip, "void CheckCraftPerks(Player* player)"),
            extract_fn(wip, "void ClearSpellAndCategoryCooldown(Player* player, uint32 spellId)", cap=80),
            extract_fn(wip, "static void StartDungeonRun(Player* player)"),
        ]
    )
    t = t.replace(
        "void TickCookingRegen(Player* player, uint32 diff)\n{",
        craft_fns + "\n\nvoid TickCookingRegen(Player* player, uint32 diff)\n{",
        1,
    )

t = t.replace(
    """            uint32 const nowMs = KillComboNowMs();
            uint8 stacks = 0;
            if (KillComboState* state = KillComboStateForPlayer(player))
                stacks = KillComboStackCount(*state, nowMs);
""",
    """            uint8 stacks = 0;
            auto const kit = g_killCombo.find(player->GetGUID().GetCounter());
            if (kit != g_killCombo.end())
                stacks = kit->second;
""",
    1,
)

sig = "static void SendAddonSync(Player* player, bool includeBags = true)"
nv = "static void NotifyVaultChange"
wi = wip.find(sig)
wj = wip.find(nv, wi)
li = t.find(sig)
lj = t.find(nv, li)
if wi > 0 and wj > wi and li > 0 and lj > li:
    t = t[:li] + wip[wi:wj] + t[lj:]

stub = """
static void SendAddonSync(Player* player, bool includeBags = true)
{
    if (!player || !g_cfg.enabled || !player->GetSession())
        return;
    if (player->isBeingLoaded() || !player->IsInWorld())
        return;
    if (g_syncing)
        return;
    AddonSyncGuard guard;


"""
while stub in t:
    t = t.replace(stub, "\n", 1)

if "static void SendLivingItem(Player* player, Item* item, std::string const& loc);" not in t:
    t = t.replace(
        "static void SendAddonSync(Player* player, bool includeBags = true)\n{",
        """static void SendLivingItem(Player* player, Item* item, std::string const& loc);
static void SendBagLivingItems(Player* player);

static void SendAddonSync(Player* player, bool includeBags = true)
{""",
        1,
    )

if "uint8 collectionPassiveMinGap = 5;" not in t:
    t = t.replace(
        "    bool zoneScaleNotify = true;\n\n    void Load()\n",
        """    bool zoneScaleNotify = true;
    uint32 collectionPassiveTickMs = 10000;
    uint8 collectionPassiveMinGap = 5;

    void Load()
""",
        1,
    )
    t = t.replace(
        """        zoneScaleNotify = sConfigMgr->GetOption<bool>("LivingGear.ZoneScale.Notify", true);
    }
};
""",
        """        zoneScaleNotify = sConfigMgr->GetOption<bool>("LivingGear.ZoneScale.Notify", true);
        collectionPassiveTickMs = sConfigMgr->GetOption<uint32>("LivingGear.CollectionPassive.TickMs", 10000);
        if (collectionPassiveTickMs < 1000)
            collectionPassiveTickMs = 1000;
        collectionPassiveMinGap = static_cast<uint8>(sConfigMgr->GetOption<uint32>("LivingGear.CollectionPassive.MinGap", 5));
    }
};
""",
        1,
    )

if not t.startswith("/*") or "void AddSC_LivingGear()" not in t or t.count("\n") < 8000:
    print("REFUSE: verify failed", file=sys.stderr)
    sys.exit(2)
if t.lstrip().startswith("static bool SacrificeItem"):
    print("REFUSE: truncated", file=sys.stderr)
    sys.exit(2)

OUT_NEXT.write_text(t, encoding="utf-8", newline="\n")
LIVE.write_text(t, encoding="utf-8", newline="\n")
print("lines", t.count("\n"), "bytes", LIVE.stat().st_size)
print("cookRegen decl", "std::unordered_map<uint32, uint32> g_cookRegenMs;" in t)
print("CheckCraftPerks def", len(re.findall(r"^void CheckCraftPerks\(Player\* player\)$", t, re.M)))
print("AddSC", "void AddSC_LivingGear()" in t)
print("backup still", hashlib.sha256(BACKUP.read_bytes()).hexdigest().upper() == EXPECT)
