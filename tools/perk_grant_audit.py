"""
Find perks an account owns that its characters never actually learned.

The fourth perk audit, and the one that catches the failure the other three
cannot see. They all reason about code and spell data:

    perk_audit.py          does any code READ this perk id?
    perk_promise_audit.py  does the module reference the ability it names?
    perk_spell_audit.py    does a spell with this id EXIST?
    perk_grant_audit.py    did the character actually GET it?    <- this one

Perks are owned by the ACCOUNT (`lg_account_perk`) but a spell is learned per
CHARACTER (`character_spell`). Every UnlockPerk implementation used to return
as soon as the account already owned the perk -- before learning anything --
so the first character on an account got the spell and no later one ever did.
A perk whose spell_dbc row was added afterwards could never be learned at all,
which is what happened to the 40 badge spells added in 0.1.61: 61 accounts
owned Fishing 910043 and not one character had learned it.

That split is invisible from the code alone, because whether it MATTERS
depends on which flavour of HasPerk the gate happens to use. This asks the
database instead, which is the only place the answer actually lives.

  python tools/perk_grant_audit.py             # summary, worst perks first
  python tools/perk_grant_audit.py --chars     # per-character detail
  python tools/perk_grant_audit.py --quiet     # exit code only, for a hook

Exit code 1 if anything is unlearned, so this can gate a ship.
Needs the ac-database container up.

Only CASTABLE perks are counted. The first version of this reported all 100
unlearned perks and that number was mostly noise: the badges have no
SkillLineAbility row, so learning one puts nothing in the spellbook, and
HasPerk reads the account set regardless. Under that heap sat the finding
that actually mattered -- *Quests - Finish missing from 894 characters, and
*Attuned Armory from 602. Those are buttons a player earned and cannot find.
Counting only those keeps the number meaningful.

Autoloot, Solo Queue and Auto-Mount used to head that list and are now
deliberately not castable at all: they are state, toggled from the Account
Perks window, and a spellbook button duplicating a checkbox was the reason
nobody noticed they had gone missing.
"""
from __future__ import annotations

import argparse
import subprocess
import sys

DB_CONTAINER = "ac-database"
SEP = "\x1f"

# The perks that are real, pressable abilities: the only ones with a
# SkillLineAbility.dbc row, so the only ones that appear in the spellbook.
# Mirrors PerkIsCastable() in LivingGear_Perks.cpp and CASTABLE_SPELLS in
# tools/client-patch/build_patch.py -- keep all three in step.
#
# Everything else in the range is a badge: a flag the module reads, with
# nothing to press and nothing to see. An unlearned badge is not a defect,
# because HasPerk consults the account set. An unlearned CASTABLE perk is a
# button the player earned and cannot find.
CASTABLE = {910001, 910002, 910003, 910004, 910005, 910006, 910007,
            910009, 910042, 910088, 910090, 910091}


def run_sql(sql):
    proc = subprocess.run(
        ["docker", "exec", "-i", DB_CONTAINER, "mysql", "--user=root", "--password=password",
         "--batch", "--raw", "--skip-column-names", "acore_characters"],
        input=sql, capture_output=True, text=True)
    if proc.returncode != 0:
        err = "\n".join(l for l in proc.stderr.splitlines()
                        if "Using a password" not in l).strip()
        raise RuntimeError("mysql failed (%d): %s" % (proc.returncode, err))
    return [l for l in proc.stdout.splitlines() if l.strip()]


# Playerbot characters are excluded from every count below.
#
# 2026-08-23: this tool reported "910090 missing on 645 / 953 characters" and
# that number is real but almost entirely meaningless -- 642 of the 645 were
# RNDBOT accounts. Bots do not keep these spells: ReconcilePerkSpells learns
# them at login (the log shows it doing exactly that, five at a time), the
# playerbot system owns the bot's spellbook, and a `saveall` does not persist
# them, so the same bots are "repaired" every login forever and the DB never
# changes. The three real characters behind that 645 were fixed by logging in.
#
# Counting bots therefore buried a 3-character problem under a 645-character
# headline, which is the opposite of what an audit is for. Real accounts only.
BOT_FILTER = ("JOIN acore_auth.account a ON a.id = c.account "
              "AND a.username NOT LIKE 'RNDBOT%' ")


def by_perk():
    head = ("SELECT CONCAT_WS('%s', p.spell_id, "
            "  SUM(CASE WHEN cs.spell IS NULL THEN 1 ELSE 0 END), COUNT(*)) "
            "FROM characters c " % SEP)
    return run_sql(
        head + BOT_FILTER +
        "JOIN lg_account_perk p ON p.account_id = c.account "
        "LEFT JOIN character_spell cs ON cs.guid = c.guid AND cs.spell = p.spell_id "
        "GROUP BY p.spell_id ORDER BY p.spell_id;")


def by_character():
    head = ("SELECT CONCAT_WS('%s', c.name, c.account, "
            "  SUM(CASE WHEN cs.spell IS NULL THEN 1 ELSE 0 END), COUNT(*)) "
            "FROM characters c " % SEP)
    return run_sql(
        head + BOT_FILTER +
        "JOIN lg_account_perk p ON p.account_id = c.account "
        "LEFT JOIN character_spell cs ON cs.guid = c.guid AND cs.spell = p.spell_id "
        "GROUP BY c.guid, c.name, c.account "
        "HAVING SUM(CASE WHEN cs.spell IS NULL THEN 1 ELSE 0 END) > 0 "
        "ORDER BY 3 DESC LIMIT 40;")


def parse(rows):
    out = []
    for line in rows:
        parts = line.split(SEP)
        if len(parts) < 3:
            continue
        try:
            out.append((parts[0], int(parts[-2]), int(parts[-1]), parts[1]))
        except ValueError:
            continue
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--chars", action="store_true", help="per-character detail")
    ap.add_argument("--quiet", action="store_true", help="exit code only")
    args = ap.parse_args()

    try:
        perks = parse(by_perk())
    except RuntimeError as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 2

    broken = [(pid, missing, total) for pid, missing, total, _ in perks
              if missing and int(pid) in CASTABLE]
    broken.sort(key=lambda r: -r[1])

    if args.quiet:
        return 1 if broken else 0

    if not broken:
        print("Every castable account perk is learned on every character.")
        print("(Badges are not checked: no spellbook entry, and HasPerk reads")
        print("the account set, so an unlearned one is not a defect.)")
        return 0

    print("%d CASTABLE perk(s) owned by an account but missing from some of its"
          % len(broken))
    print("characters -- each is a button the player earned and cannot find.")
    print()
    print("  perk     unlearned / characters whose account owns it")
    for pid, missing, total in broken[:30]:
        print("  %-8s %d / %d" % (pid, missing, total))
    if len(broken) > 30:
        print("  ... and %d more" % (len(broken) - 30))

    if args.chars:
        print()
        print("Worst characters:")
        for name, missing, total, _acct in parse(by_character()):
            print("  %-14s missing %d of %d" % (name[:14], missing, total))

    print()
    print("Perks are owned by the account; spells are learned per character.")
    print("ReconcilePerkSpells() in LivingGear_Perks.cpp closes the gap at login,")
    print("so a non-empty list here means either that repair has not shipped yet")
    print("or the affected characters have not logged in since it did. Expect")
    print("this to drain gradually rather than all at once.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
