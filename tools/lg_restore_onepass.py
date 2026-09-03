# One-pass restore of LivingGear.cpp from locked backup + compile fixes.
# Writes LivingGear.cpp.next, never truncates the live file until verify.

from pathlib import Path
import re
import hashlib
import sys

SRC = Path(r"A:\wow-bonesaw\modules\mod-living-gear\src")
BACKUP = SRC / "LivingGear.cpp.backup-20260818"
EXPECT = "8A949243827DDEE91B50469309FF22AB48CF418FCE43529622CE5D486EBEB475"
OUT_NEXT = SRC / "LivingGear.cpp.next"
OUT_LIVE = SRC / "LivingGear.cpp"

raw = BACKUP.read_bytes()
got = hashlib.sha256(raw).hexdigest().upper()
if got != EXPECT:
    print("BACKUP HASH MISMATCH", got, file=sys.stderr)
    sys.exit(1)

text = raw.decode("utf-8", errors="replace").replace("\r\n", "\n")

orphan = """
    SPELL_TRADE_75, SPELL_TRADE_150, SPELL_TRADE_225,
    SPELL_TRADE_300, SPELL_TRADE_375, SPELL_TRADE_450
};

    SPELL_POST, SPELL_AUCTION, SPELL_TRAINER, SPELL_BANK,
    SPELL_STABLE, SPELL_BIND, SPELL_AUTOLOOT, SPELL_FLIGHT,
    SPELL_HONOR_LOSS, SPELL_HONOR_WIN, SPELL_HONOR_KILLS,
    SPELL_REP_1, SPELL_REP_5, SPELL_REP_10,
    SPELL_REP_BLOODSAIL, SPELL_REP_DARKMOON, SPELL_REP_RAVENHOLDT, SPELL_REP_SHENDRALAR,
    SPELL_REP_ARATHOR, SPELL_REP_DEFILERS, SPELL_REP_SILVERWING, SPELL_REP_WARSONG,
    SPELL_REP_STORMPIKE, SPELL_REP_FROSTWOLF,
    SPELL_TRADE_75, SPELL_TRADE_150, SPELL_TRADE_225,
    SPELL_TRADE_300, SPELL_TRADE_375, SPELL_TRADE_450,
    SPELL_QUEST_SPEED, SPELL_JUMP_DOUBLE, SPELL_JUMP_TRIPLE
};
"""
text = text.replace(orphan, "\n")
lines = text.split("\n")


def insert_after(lines, needle, block):
    for i, l in enumerate(lines):
        if l.strip() == needle.strip():
            nxt = "\n".join(lines[i + 1 : i + 8])
            if block.split("\n")[0] in nxt:
                return lines
            return lines[: i + 1] + block.split("\n") + lines[i + 1 :]
    raise SystemExit("missing " + needle)


lines = insert_after(
    lines,
    "uint32 const SPELL_LEVELING_10 = 910062;",
    """uint32 const SPELL_LEVELING[] = { 910053, 910054, 910055, 910056, 910057, 910058, 910059, 910060, 910061, 910062 };
uint32 const LEVELING_TIERS = 10;
float const LEVELING_XP_BONUS = 0.10f;""",
)
lines = insert_after(
    lines,
    "uint32 const SPELL_COOK[] = { 910063, 910064, 910065, 910066, 910067, 910068 };",
    "uint32 const COOK_REGEN_MS = 1000;",
)
lines = insert_after(
    lines,
    "uint32 const SPELL_TRAVEL_10 = 910082;",
    """uint32 const SPELL_TRAVEL[] = { 910073, 910074, 910075, 910076, 910077, 910078, 910079, 910080, 910081, 910082 };
uint32 const TRAVEL_NEED[] = { 1, 2, 3, 4, 5, 10, 25, 50, 100, 250 };
uint32 const TRAVEL_TIERS = 10;
float const TRAVEL_REDUCE = 0.20f;""",
)
lines = insert_after(
    lines,
    "uint32 const QUEST_SPEED_NEED = 100;",
    """uint32 const AUTO_QUEST_NEED = 100;
uint32 const FISH_POOL_NEED = 500;
uint32 const FIND_QUESTS_NEED = 50;
uint32 const ACHIEVEMENT_50_QUESTS = 482;
uint32 const ACHIEVEMENT_500_FISH = 1447;
uint32 const FIRST_AID_MAX = 450;
float const FISH_POOL_RANGE = 20.0f;
uint32 const AUTO_ATTUNE_ITEM_LEVEL = 10;
uint32 const COLLECTION_PASSIVE_COUNT = 8;
uint8 const AUTO_ATTUNE_MAX_QUALITY = 4;
uint32 const LOGIN_SETTLE_MS = 2000;""",
)
lines = insert_after(
    lines,
    "std::unordered_map<uint32, uint32> g_lastAidCleanse;",
    """std::unordered_set<uint32> g_loginReady;
std::unordered_map<uint32, uint32> g_loginMs;
std::unordered_set<uint32> g_chatOn;
std::unordered_map<uint64, uint32> g_recentQuestUse;""",
)

login_fn = """
bool LoginSettled(Player* player)
{
    if (!player)
        return false;
    uint32 const guid = player->GetGUID().GetCounter();
    if (!g_loginReady.count(guid))
        return false;
    auto const it = g_loginMs.find(guid);
    if (it == g_loginMs.end())
        return true;
    return GetMSTimeDiffToNow(it->second) >= LOGIN_SETTLE_MS;
}
"""
joined = "\n".join(lines)
idx = joined.find("void PushSharedCurrencies(Player* source)")
if idx < 0:
    raise SystemExit("PushSharedCurrencies missing")
if "bool LoginSettled(Player* player)" not in joined:
    joined = joined[:idx] + login_fn + "\n" + joined[idx:]
lines = joined.split("\n")


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
    t = "".join(out)
    return t.count("{") - t.count("}")


def skip_braced(arr, i, cap=250):
    local = 0
    started = False
    k = i
    n = len(arr)
    while k < n and (k - i) < cap:
        if "{" in arr[k]:
            started = True
        local += brace_delta(arr[k])
        k += 1
        if started and local <= 0:
            return k
    return None


fn_sig = re.compile(
    r"^(?:static\s+)?(?:[\w:<>\*&\s]+?)\s+(\w+)\s*\([^;]*\)\s*(?:const)?\s*$"
)
struct_sig = re.compile(r"^struct\s+(\w+)\s*$")
class_sig = re.compile(r"^class\s+(\w+)\b")
array_sig = re.compile(r"^(?:static\s+)?[\w:<>\s\*&]+?\b(\w+)\s*\[[^\]]*\]\s*=")

first_class = next(i for i, l in enumerate(lines) if l.startswith("class LivingGear"))
ns_close = next(i for i, l in enumerate(lines) if l.startswith("} // namespace LivingGear"))
head, classes, tail = lines[:first_class], lines[first_class:ns_close], lines[ns_close:]


def dedupe_defs(arr, do_fn=True, do_class=False):
    keep = []
    seen = set()
    i = 0
    n = len(arr)
    while i < n:
        s = arr[i]
        raw = s.rstrip()
        name = None
        kind = None
        if raw and raw[0] not in " \t#" and not raw.startswith("//"):
            if do_class:
                m = class_sig.match(raw)
                if m:
                    name, kind = m.group(1), "class"
            if name is None:
                m = struct_sig.match(raw)
                if m:
                    name, kind = m.group(1), "struct"
            if name is None:
                m = array_sig.match(raw)
                if m:
                    name, kind = m.group(1), "array"
            if name is None and do_fn:
                m = fn_sig.match(raw)
                if m:
                    nxt = i + 1
                    while nxt < n and not arr[nxt].strip():
                        nxt += 1
                    if raw.endswith("{") or (nxt < n and arr[nxt].strip() == "{"):
                        nm = m.group(1)
                        if nm not in ("if", "for", "while", "switch", "catch", "else", "do"):
                            name, kind = nm, "fn"
        if name:
            key = kind + ":" + name
            if key in seen:
                k = skip_braced(arr, i)
                if k is None:
                    keep.append(s)
                    i += 1
                    continue
                i = k
                continue
            seen.add(key)
        keep.append(s)
        i += 1
    return keep


head2 = dedupe_defs(head, do_fn=True)
cls2 = dedupe_defs(classes, do_fn=False, do_class=True)
t = "\n".join(head2 + cls2 + tail)
if not t.endswith("\n"):
    t += "\n"

# LgConfig
if "collectionPassiveMinGap" not in t:
    t = t.replace(
        "    uint32 collectionPassiveTickMs = 10000;\n",
        "    uint32 collectionPassiveTickMs = 10000;\n    uint8 collectionPassiveMinGap = 5;\n",
        1,
    )
    t = t.replace(
        """        if (collectionPassiveTickMs < 1000)
            collectionPassiveTickMs = 1000;
    }
""",
        """        if (collectionPassiveTickMs < 1000)
            collectionPassiveTickMs = 1000;
        collectionPassiveMinGap = static_cast<uint8>(sConfigMgr->GetOption<uint32>("LivingGear.CollectionPassive.MinGap", 5));
    }
""",
        1,
    )

if "uint32 BagCountWithoutVault(Player const* player, uint32 itemEntry);" not in t:
    t = t.replace(
        "bool IsQuestItem(ItemTemplate const* proto);\nuint8 ResolveLootAction",
        """bool IsQuestItem(ItemTemplate const* proto);
uint32 BagCountWithoutVault(Player const* player, uint32 itemEntry);
void SyncDeliverQuests(Player* player);
void SendNewlyCompletedQuests(Player* player, std::vector<uint32> const& wasIncomplete);
uint8 ResolveLootAction""",
        1,
    )

t = t.replace(
    """static bool IsChatEnabled(Player* player)
{
    if (!player)
        return false;
    return g_chatOn.find(player->GetGUID().GetCounter()) != g_chatOn.end();
}""",
    """static bool IsChatEnabled(Player* player)
{
    if (!player)
        return false;
    if (g_chatOn.empty())
        return true;
    return g_chatOn.find(player->GetGUID().GetCounter()) != g_chatOn.end();
}""",
)

if "bool TryHandleRecipeItem" not in t:
    t = t.replace(
        "void ApplyLootRule(Player* player, Item* item)\n{\n    if (!player || !item)\n        return;\n    if (TryHandleRecipeItem(player, item))\n",
        """bool TryHandleRecipeItem(Player* /*player*/, Item* /*item*/)
{
    return false;
}

void ApplyLootRule(Player* player, Item* item)
{
    if (!player || !item)
        return;
    if (TryHandleRecipeItem(player, item))
""",
        1,
    )

# Close SendPendingRepUpdates if truncated into BaseReputationFor
t = t.replace(
    """        mgr.SendState(&pair.second);
        return;
    }
int32 BaseReputationFor(FactionEntry const* faction, uint8 race, uint8 cls)""",
    """        mgr.SendState(&pair.second);
        return;
    }
}

int32 BaseReputationFor(FactionEntry const* faction, uint8 race, uint8 cls)""",
)

# Jump / attune helpers before SendAddonSync if missing
helpers = """
uint32 AttunedCount(uint32 accountId)
{
    auto const it = g_attuned.find(accountId);
    return it == g_attuned.end() ? 0 : uint32(it->second.size());
}

uint8 MaxJumpUnlock(Player* player)
{
    if (!player || !player->GetSession())
        return 0;
    uint32 const accountId = player->GetSession()->GetAccountId();
    if (AccountHasPerk(accountId, SPELL_JUMP_TRIPLE))
        return 2;
    if (AccountHasPerk(accountId, SPELL_JUMP_DOUBLE))
        return 1;
    return 0;
}

uint8 SelectedJumpRank(Player* player)
{
    if (!player)
        return 0;
    uint8 const max = MaxJumpUnlock(player);
    auto const it = g_jumpMode.find(player->GetGUID().GetCounter());
    if (it == g_jumpMode.end())
        return max;
    return it->second > max ? max : it->second;
}

"""
if "uint32 AttunedCount(" not in t:
    t = t.replace(
        "\nstatic void SendAddonSync(Player* player, bool includeBags = true)\n",
        "\n" + helpers + "static void SendAddonSync(Player* player, bool includeBags = true)\n",
        1,
    )

if "static void AnnounceAutoAttuneTiers" not in t and "AnnounceAutoAttuneTiers" in t:
    t = t.replace(
        "static void AttuneQuestRewards(Player* player, Quest const* quest)\n",
        """static void AnnounceAutoAttuneTiers(Player* player, uint32 oldCount)
{
    if (!player || !player->GetSession())
        return;
    uint32 const now = AttunedCount(player->GetSession()->GetAccountId());
    if (now <= oldCount)
        return;
    Say(player, Acore::StringFormat("|cff66ccff[Living Gear]|r Attuned collection: {}.", now).c_str());
}

static void AttuneQuestRewards(Player* player, Quest const* quest)
""",
        1,
    )

if "static bool AutoAttuneQualityEnabled" not in t and "AutoAttuneQualityEnabled" in t:
    t = t.replace(
        "static bool TryAutoAttuneLoot(Player* player, Item* item)\n",
        """static bool AutoAttuneQualityEnabled(uint32 /*accountId*/, uint8 quality)
{
    return quality <= AUTO_ATTUNE_MAX_QUALITY;
}

static bool TryAutoAttuneLoot(Player* player, Item* item)
""",
        1,
    )

t = t.replace(
    "static bool SacrificeItem(Player* player, Item* item, ChatHandler* handler);",
    "static bool SacrificeItem(Player* player, Item* item, ChatHandler* handler, bool autoAttune = false, bool quiet = false);",
)
needle = "static bool SacrificeItem(Player* player, Item* item, ChatHandler* handler, bool autoAttune = false, bool quiet = false);\n"
parts = t.split(needle)
if len(parts) > 2:
    t = needle.join(parts[:2]) + "".join(parts[2:])
t = t.replace(
    "return SacrificeItem(player, item, &handler, true);",
    "return SacrificeItem(player, item, &handler, true, true);",
)

# Verify before writing live
if not t.startswith("/*"):
    print("REFUSE: output does not start with comment", file=sys.stderr)
    sys.exit(2)
if "void AddSC_LivingGear()" not in t:
    print("REFUSE: AddSC_LivingGear missing", file=sys.stderr)
    sys.exit(2)
nlines = t.count("\n")
if nlines < 8000:
    print("REFUSE: too few lines", nlines, file=sys.stderr)
    sys.exit(2)
if t.lstrip().startswith("static bool SacrificeItem"):
    print("REFUSE: truncated from SacrificeItem", file=sys.stderr)
    sys.exit(2)

OUT_NEXT.write_text(t, encoding="utf-8", newline="\n")
print("NEXT lines", nlines, "bytes", OUT_NEXT.stat().st_size)
print("SelectClassPerk", len(re.findall(r"^void SelectClassPerk\(", t, re.M)))
print("AddSC", "void AddSC_LivingGear()" in t)
print("LoginSettled", "LoginSettled" in t)
print("BagCount", t.count("BagCountWithoutVault"))
print("backup still", hashlib.sha256(BACKUP.read_bytes()).hexdigest().upper() == EXPECT)
