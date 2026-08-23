"""
Find class perks that promise an ability the module never touches.

tools/perk_audit.py answers "does anything read this perk id". That is not the
same question as "does this perk do what it says". Paladin Holy passes that
audit -- its id IS read, for the Consecration relocation -- while two of its
three promises do not exist:

    "Consecration follows you and toggles off if recast."   implemented
    "Consecration damage +1000%."                           no code
    "Holy Shock damage +300% and hits enemies within 10 yards."  no code

Nothing in the module so much as contained the string "Holy Shock". A player
selected that perk, saw nothing happen, and reported it -- which is the slow
way to find this.

This reads each perk's description, pulls out the ability names it mentions,
and checks whether the module references each one at all. It cannot tell you
that an implementation is CORRECT; it tells you when there is provably nothing
there, which is the case worth catching automatically.

  python tools/perk_promise_audit.py
  python tools/perk_promise_audit.py --all     # include perks that look fine

Expect some noise. An ability referenced only by numeric spell id under a
differently-worded constant will show as missing. Read the hits, do not just
count them -- the value here is a short list to eyeball, not a gate.
"""
from __future__ import annotations

import argparse
import glob
import os
import re
import sys

MODULE = os.path.join("modules", "mod-living-gear")
LUA = os.path.join(MODULE, "client_addon", "LivingGear", "LivingGear.lua")
SRC_GLOB = os.path.join(MODULE, "src", "*.cpp")

# Words that are capitalised in a description but are not ability names. Without
# this every sentence-initial "The", "While", "Your" becomes a false promise.
STOP = {
    "The", "This", "That", "You", "Your", "Yours", "While", "When", "If", "Each",
    "Every", "All", "Any", "Both", "Also", "And", "But", "For", "Not", "No",
    "Learn", "Learns", "Reach", "Reaches", "Gain", "Gains", "Deal", "Deals",
    "Dealt", "Take", "Takes", "Taken", "Cast", "Casts", "Casting", "Use", "Uses",
    "Using", "Can", "Cannot", "Has", "Have", "Its", "It", "They", "Them", "Their",
    "There", "These", "Those", "Now", "Then", "Than", "With", "Without", "Within",
    "From", "Into", "Onto", "Over", "Under", "After", "Before", "During",
    "Increased", "Reduced", "Removed", "Applies", "Apply", "Grants", "Grant",
    "Stacks", "Stack", "Instead", "Additionally", "Permanent", "Passive",
    "Toggle", "Unlocked", "Requires", "Nearby", "Enemies", "Enemy", "Allies",
    "Ally", "Party", "Raid", "Yards", "Yard", "Sec", "Seconds", "Second",
    "Minutes", "Minute", "Health", "Mana", "Damage", "Attack", "Power", "Speed",
    "Cooldown", "Duration", "Range", "Radius", "Chance", "Rank", "Level",
    "Account", "Character", "Item", "Items", "Spell", "Spells", "Ability",
    "Abilities", "Class", "Skill", "Profession", "Professions", "Quest",
    "Quests", "Combat", "Out", "In", "On", "Off", "Up", "Down", "Per", "At",
    "To", "Of", "As", "Is", "Are", "Be", "By", "Or", "So", "Do", "Does",
    "Never", "Always", "Still", "Only", "Just", "More", "Less", "Most",
    "Clear", "Complete", "Completing", "Kill", "Kills", "Killing", "Loot",
    "Looting", "Open", "Opens", "Free", "Full", "Half", "Double", "Triple",
    "Bonus", "Extra", "Normal", "Total", "Max", "Maximum", "Minimum",
    "Train", "Trains", "Trained", "Earn", "Earns", "Catch", "Catches",
    "Attune", "Attuned", "Exalted", "Manually", "Hunters", "Hold", "Shift",
    "Summon", "Summons", "Make", "Makes", "Select", "Turn", "Bandages",
    "Jumps", "Boosted", "Mounted", "While",
    "Passively", "Passive",
}

# Reputation perks name a faction in prose but are implemented against numeric
# faction ids, so their names never appear in the source. Not a promise gap.
SKIP_IDS = set(range(910013, 910026))


def ability_names(text):
    """Capitalised runs that look like ability names, e.g. 'Holy Shock'."""
    out = []
    for m in re.finditer(r"\b([A-Z][a-z]+(?:[ '][A-Z][a-z]+)*)\b", text or ""):
        phrase = m.group(1)
        head = phrase.split()[0]
        if head in STOP:
            continue
        # Single common words are almost always prose, not an ability.
        if " " not in phrase and phrase in STOP:
            continue
        out.append(phrase)
    return out


def module_text():
    parts = []
    for path in glob.glob(SRC_GLOB):
        if ".backup" in path or ".bak" in path:
            continue
        parts.append(open(path, encoding="utf-8", errors="replace").read())
    return "\n".join(parts)


def referenced(code, phrase):
    """Is this ability mentioned in the module at all, in any usual spelling?"""
    # "chains up to 8 Ambushes" should match SPELL_AMBUSH.
    if phrase.endswith("es") and len(phrase) > 4:
        if referenced(code, phrase[:-2]):
            return True
    if phrase.endswith("s") and len(phrase) > 3:
        if referenced(code, phrase[:-1]):
            return True
    squashed = phrase.replace("'", "").replace(" ", "")
    variants = {
        phrase,
        squashed,
        squashed.upper(),
        phrase.replace(" ", "_").replace("'", "").upper(),
        phrase.replace(" ", "").lower(),
    }
    return any(v and v in code for v in variants)


def class_perks(lua_text):
    """Only the class-perk block: those are the ones making ability promises."""
    out = []
    # Tolerant of line breaks and spacing: the strict single-line form matched
    # only 45 of 139 entries, silently skipping most of the class perks --
    # including every spec whose entry happens to wrap.
    pattern = r'id = (91\d{4}),\s*name = "([^"]*)",\s*how = "([^"]*)"'
    for m in re.finditer(pattern, lua_text, re.S):
        out.append((int(m.group(1)), m.group(2), m.group(3)))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--all", action="store_true", help="also list perks with no missing names")
    args = ap.parse_args()

    if not os.path.isfile(LUA):
        print("run from the repo root (%s not found)" % LUA, file=sys.stderr)
        return 2

    lua_text = open(LUA, encoding="utf-8", errors="replace").read()
    code = module_text()
    perks = class_perks(lua_text)

    flagged = 0
    for pid, name, how in perks:
        if pid in SKIP_IDS:
            continue
        names = ability_names(how)
        missing = sorted({n for n in names if not referenced(code, n)})
        if not missing and not args.all:
            continue
        if missing:
            flagged += 1
            print("%-7d %-24s" % (pid, name[:24]))
            print("        %s" % how[:150])
            print("        NOT REFERENCED ANYWHERE: %s" % ", ".join(missing))
            print()

    print("checked %d perk descriptions, %d mention something the module never names"
          % (len(perks), flagged))
    print()
    print("A hit means the module contains no reference to that ability at all, so")
    print("the promise cannot be implemented. It does NOT prove the ones without")
    print("hits work -- only that something by that name is referenced somewhere.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
