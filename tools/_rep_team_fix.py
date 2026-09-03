"""Team-scope gate for the account-wide reputation pool (bug report #220).

LivingGear_Next.cpp is LF. Four gates + a helper block:
  1. helpers after RepSyncEligible
  2. PushAccountReputation rejects factions illegal for the earner's team
  3. PropagateAccountReputation skips alts of an illegal team
  4. EnsureAccountReputation backfill + replay skip illegal factions
"""
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
PATH = r"A:\wow-bonesaw\modules\mod-living-gear\src\LivingGear_Next.cpp"

data = open(PATH, "rb").read()
if data.count(b"\r\n"):
    raise SystemExit("expected LF file, found CRLF")
text = data.decode("utf-8")

HELPERS = """// Team-scoped reputation (bug report #220).
//
// Faction.dbc gives every faction race-dependent base reputation, and every
// faction that belongs to one team (the nine cities, the BG teams, the
// expedition factions, Kurenai vs The Mag'har, ...) puts each character of
// the OPPOSITE team at Hated (-42000) through a base-reputation slot
// covering that team's races. A standing captured from one team carries
// that team's base inside the displayed total, so replaying it onto a
// character of the other team writes raw = total - base(other team). That
// is how a Horde alt ended up Hated with its own faction cities and exalted
// with enemy ones, and every round trip ratcheted the pool further.
//
// A faction therefore syncs only between characters of a team whose
// races/classes do not START it at Hated. Teams are the DBC race masks
// (1101 alliance, 690 horde); legality is the worst base reputation any
// race/class of that team receives, matched exactly like
// ReputationMgr::GetBaseReputation (first matching slot, wildcards
// included). Factions that start EVERYONE at Hated (Netherwing, Brood of
// Nozdormu, Sons of Hodir) have a team-independent base and stay shareable.
constexpr uint32 REP_RACEMASK_ALLIANCE = 1101;  // human, dwarf, night elf, gnome, draenei
constexpr uint32 REP_RACEMASK_HORDE = 690;      // orc, undead, tauren, troll, blood elf
constexpr int32 REP_HATED_BASE = -42000;

int32 RepWorstBaseFor(FactionEntry const* factionEntry, uint32 raceMask)
{
    int32 worst = 0;
    for (uint32 raceBit = 1; raceBit <= (1u << 10); raceBit <<= 1)
    {
        if (!(raceMask & raceBit))
            continue;
        for (uint32 classBit = 1; classBit <= (1u << 10); classBit <<= 1)
        {
            for (int i = 0; i < 4; ++i)
            {
                bool const raceMatch = (factionEntry->BaseRepRaceMask[i] & raceBit)
                    || (factionEntry->BaseRepRaceMask[i] == 0 && factionEntry->BaseRepClassMask[i] != 0);
                bool const classMatch = (factionEntry->BaseRepClassMask[i] & classBit)
                    || factionEntry->BaseRepClassMask[i] == 0;
                if (raceMatch && classMatch)
                {
                    worst = std::min(worst, factionEntry->BaseRepValue[i]);
                    break;
                }
            }
        }
    }
    return worst;
}

// Bit 1 = alliance characters may sync this faction, bit 2 = horde.
uint8 RepTeamLegalTeams(FactionEntry const* factionEntry)
{
    if (!factionEntry || factionEntry->reputationListID < 0)
        return 0;
    return (RepWorstBaseFor(factionEntry, REP_RACEMASK_ALLIANCE) > REP_HATED_BASE ? 1u : 0u)
        | (RepWorstBaseFor(factionEntry, REP_RACEMASK_HORDE) > REP_HATED_BASE ? 2u : 0u);
}

uint8 RepTeamBitFor(Player const* player)
{
    return (player->getRaceMask() & REP_RACEMASK_ALLIANCE) ? 1 : 2;
}

"""

REPLACEMENTS = [
    # 1. helpers
    ("""bool RepSyncEligible(Player* player)
{
    return player && player->GetSession() && !player->GetSession()->IsBot();
}

void LoadAccountReputation(uint32 accountId)
""",
     """bool RepSyncEligible(Player* player)
{
    return player && player->GetSession() && !player->GetSession()->IsBot();
}

""" + HELPERS + """void LoadAccountReputation(uint32 accountId)
"""),
    # 2. capture gate
    ("""    uint32 const accountId = source->GetSession()->GetAccountId();
    if (g_syncingReputation.count(accountId))
        return;
    LoadAccountReputation(accountId);
""",
     """    uint32 const accountId = source->GetSession()->GetAccountId();
    if (g_syncingReputation.count(accountId))
        return;
    FactionEntry const* factionEntry = sFactionStore.LookupEntry(factionId);
    if (!factionEntry || !(RepTeamLegalTeams(factionEntry) & RepTeamBitFor(source)))
        return;
    LoadAccountReputation(accountId);
"""),
    # 3a. propagate legalTeams
    ("""    FactionEntry const* factionEntry = sFactionStore.LookupEntry(factionId);
    if (!factionEntry)
        return;
    uint32 const accountId = source->GetSession()->GetAccountId();
    g_syncingReputation.insert(accountId);
""",
     """    FactionEntry const* factionEntry = sFactionStore.LookupEntry(factionId);
    if (!factionEntry)
        return;
    uint8 const legalTeams = RepTeamLegalTeams(factionEntry);
    uint32 const accountId = source->GetSession()->GetAccountId();
    g_syncingReputation.insert(accountId);
"""),
    # 3b. propagate per-alt gate
    ("""        if (alt->GetSession()->GetAccountId() != accountId)
            continue;
        if (FactionState const* state = alt->GetReputationMgr().GetState(factionEntry))
""",
     """        if (alt->GetSession()->GetAccountId() != accountId)
            continue;
        if (!(legalTeams & RepTeamBitFor(alt)))
            continue;
        if (FactionState const* state = alt->GetReputationMgr().GetState(factionEntry))
"""),
    # 4a. hoist teamBit
    ("""    LoadAccountReputation(accountId);
    auto& factions = g_accountReputation[accountId];
""",
     """    LoadAccountReputation(accountId);
    auto& factions = g_accountReputation[accountId];
    uint8 const teamBit = RepTeamBitFor(player);
"""),
    # 4b. backfill gate
    ("""        auto const found = accountMaxSaved.find(factionId);
        if (found == accountMaxSaved.end())
            continue;
        FactionEntry const* factionEntry = sFactionStore.LookupEntry(factionId);
        if (!factionEntry)
            continue;
""",
     """        auto const found = accountMaxSaved.find(factionId);
        if (found == accountMaxSaved.end())
            continue;
        FactionEntry const* factionEntry = sFactionStore.LookupEntry(factionId);
        if (!factionEntry || !(RepTeamLegalTeams(factionEntry) & teamBit))
            continue;
"""),
    # 4c. replay gate
    ("""    for (auto const& [factionId, storedStanding] : factions)
    {
        FactionEntry const* factionEntry = sFactionStore.LookupEntry(factionId);
        if (!factionEntry)
            continue;
""",
     """    for (auto const& [factionId, storedStanding] : factions)
    {
        FactionEntry const* factionEntry = sFactionStore.LookupEntry(factionId);
        if (!factionEntry || !(RepTeamLegalTeams(factionEntry) & teamBit))
            continue;
"""),
]

for before, after in REPLACEMENTS:
    n = text.count(before)
    if n != 1:
        raise SystemExit("anchor count %d (expected 1): %r" % (n, before[:80]))
    text = text.replace(before, after)

open(PATH, "wb").write(text.encode("utf-8"))
print("ok: 7 replacements applied, %d chars" % len(text))
