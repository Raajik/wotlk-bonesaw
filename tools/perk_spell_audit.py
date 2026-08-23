"""
Find perks the addon advertises that have no spell to grant.

This is the third and bluntest of the perk audits, and the one that would have
caught bug #22 the day the Leveling track was written:

    tools/perk_audit.py          does any code READ this perk id?
    tools/perk_promise_audit.py  does the module reference the ability it names?
    tools/perk_spell_audit.py    does a spell with this id EXIST?   <- this one

Swayss reported having no Leveling perk while standing on a level 80 character.
The logic was correct, the level threshold was correct, and the account had the
right characters. Spells 910053-910062 simply had no `spell_dbc` row, so
UnlockPerk() bailed at its `!sSpellMgr->GetSpellInfo(spellId)` guard and
returned silently. Not one account on the realm had ever earned one.

Worse, that guard is not universal. Other UnlockPerk implementations skip the
check and write the row into `lg_account_perk` anyway -- so the perk reads as
owned by the account-set flavour of HasPerk() and as missing by the
`player->HasSpell()` flavour, which is the same split that produced bug #25.
A perk with no spell is therefore not merely inert, it is inconsistent.

  python tools/perk_spell_audit.py
  python tools/perk_spell_audit.py --sql       # emit a migration for the gaps

Needs the ac-database container up; it reads the live `spell_dbc`.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LUA = ROOT / "modules" / "mod-living-gear" / "client_addon" / "LivingGear" / "LivingGear.lua"

DB_CONTAINER = "ac-database"


def perk_entries(text):
    """Every `{ id = 91xxxx, name = ..., how = ... }` with the group it sits in.

    Two shapes hold perks and they need different handling:

      account tracks   name = "Cooking", [unit = "gather",] ticks = { ... }
      class perks      HUNTER = { { id = 910150, name = "Marksmanship", ... } }

    A track header may carry extra keys between `name` and `ticks` -- Fishing
    has `unit = "gather"` -- so the header pattern has to tolerate them. Without
    that the Fishing perks get attributed to the First Aid track above them,
    which is how the first run of this labelled 910043 "First Aid: Cast".
    """
    header = r'name = "([^"]+)",\s*\n(?:[ \t]*\w+ = [^\n]*\n)*?[ \t]*ticks = \{'
    klass = r'\n    ([A-Z_]{3,}) = \{'
    entry = r'\{\s*id = (91\d{4}),\s*name = "([^"]*)",\s*how = "([^"]*)"'

    entries = []
    group = None
    for match in re.finditer("|".join((header, klass, entry)), text):
        if match.group(1):
            group = match.group(1)
        elif match.group(2):
            group = match.group(2).capitalize()
        else:
            entries.append((int(match.group(3)), group, match.group(4), match.group(5)))
    return entries


def existing_spell_ids():
    proc = subprocess.run(
        ["docker", "exec", "-i", DB_CONTAINER, "mysql", "--user=root", "--password=password",
         "--batch", "--skip-column-names", "acore_world", "-e",
         "SELECT ID FROM spell_dbc WHERE ID BETWEEN 910000 AND 919999;"],
        capture_output=True, text=True)
    if proc.returncode != 0:
        err = "\n".join(l for l in proc.stderr.splitlines()
                        if "Using a password" not in l).strip()
        raise RuntimeError("mysql failed: %s" % err)
    return {int(x) for x in proc.stdout.split() if x.isdigit()}


def escape(text):
    return text.replace("\\", "\\\\").replace("'", "''")


def ascii_only(text):
    """Client-facing strings are ASCII here; a stray dash breaks the import."""
    return text.encode("ascii", "replace").decode("ascii")


def emit_sql(missing):
    print("-- Badge spells for perks the interface advertises but that had no")
    print("-- spell_dbc row, so nothing could ever grant them. See bug #22 and")
    print("-- tools/perk_spell_audit.py.")
    print("--")
    print("-- Shape copied from the badges that already work (910011, 910070):")
    print("-- Attributes 16 (passive), Effect_1 3 (dummy), ImplicitTargetA_1 1")
    print("-- (caster), RangeIndex 1, no duration. They carry no mechanics of")
    print("-- their own -- the module reads the id and does the work.")
    print()
    for spell_id, track, name, how in missing:
        label = ascii_only("*%s: %s" % (track, name) if track else "*%s" % name)
        desc = ascii_only(how)
        print("INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,")
        print("    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,")
        print("    `Description_Lang_enUS`, `Description_Lang_Mask`)")
        print("  VALUES (%d, 16, 1, 3, 1, 1, '%s', 16712190, '%s', 16712190)"
              % (spell_id, escape(label), escape(desc)))
        print("  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);")
    print()
    print("-- %d spell(s)." % len(missing))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sql", action="store_true", help="emit a migration for the gaps")
    args = ap.parse_args()

    if not LUA.is_file():
        print("cannot find %s" % LUA, file=sys.stderr)
        return 2

    entries = perk_entries(LUA.read_text(encoding="utf-8", errors="replace"))
    try:
        have = existing_spell_ids()
    except RuntimeError as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 1

    seen = set()
    missing = []
    for spell_id, track, name, how in entries:
        if spell_id in seen:
            continue
        seen.add(spell_id)
        if spell_id not in have:
            missing.append((spell_id, track, name, how))
    missing.sort()

    if args.sql:
        emit_sql(missing)
        return 0

    print("%d perk id(s) advertised, %d with no spell_dbc row"
          % (len(seen), len(missing)))
    if not missing:
        print("Every advertised perk has a spell. Nothing to do.")
        return 0
    print()
    for spell_id, track, name, how in missing:
        print("  %d  %-12s %-14s %s" % (spell_id, (track or "?")[:12], name[:14], how[:70]))
    print()
    print("These cannot be granted. UnlockPerk() in LivingGear_Progression.cpp")
    print("bails on a missing spell; the implementations that do not check write")
    print("an lg_account_perk row anyway, so the perk reads owned by one flavour")
    print("of HasPerk and missing by the player->HasSpell flavour.")
    print()
    print("Re-run with --sql to emit a migration.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
