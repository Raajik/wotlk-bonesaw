"""
Find Living Gear perks that are advertised to players but never actually read.

The recurring failure in this module is not "the code is wrong". It is that a
perk gets a spell id, a client entry in WORLD_UNLOCKS/WORLD_TRACKS, and an
UnlockPerk call -- and then nothing ever CONSUMES it. The player earns it, the
panel shows it green, and it does nothing. Solo Queue was exactly this; so were
the five found on 2026-08-22. They surface one player report at a time, which
is the slowest possible way to find them.

This finds them mechanically:

  python tools/perk_audit.py

A perk counts as WIRED if any of these is true:
  - some C++ file references its constant somewhere other than the declaration
    and the UnlockPerk/learnSpell call (i.e. it gates real behaviour)
  - the client Lua references the raw id somewhere other than its declaration
    (client-side perks like Auto-Accept are legitimately implemented there)
  - its spell_dbc row carries a real aura, so the engine applies it with no
    module code at all

Anything matching none of those is reported as UNWIRED.

Checking spell_dbc needs the database container up. Without it the check still
runs, just without that third test -- which can produce false positives for
pure-aura perks, so it says so rather than staying quiet.
"""
from __future__ import annotations

import collections
import glob
import os
import re
import subprocess
import sys

MODULE = os.path.join("modules", "mod-living-gear")
LUA = os.path.join(MODULE, "client_addon", "LivingGear", "LivingGear.lua")
SRC_GLOB = os.path.join(MODULE, "src", "*.cpp")

# Lines that only declare or hand out a perk, rather than acting on one.
GRANT = re.compile(r"(UnlockPerk|learnSpell)")
DECL = re.compile(r"uint32 const \w+\s*(\[\])?\s*=")


def source_files():
    return [f for f in glob.glob(SRC_GLOB) if ".backup" not in f and ".bak" not in f]


def client_perks(lua_text):
    """id -> display name, for everything the panel advertises."""
    out = {}
    for m in re.finditer(r'\{\s*id = (91\d{4}),\s*name = "([^"]+)"', lua_text):
        out[int(m.group(1))] = m.group(2)
    return out


def server_constants(files):
    """id -> set of constant names. One id can have several aliases across
    files, and missing that is how 910035 first looked dead when it was not."""
    names = collections.defaultdict(set)
    for path in files:
        text = open(path, encoding="utf-8", errors="replace").read()
        for m in re.finditer(r"uint32 const (\w+)\s*=\s*(91\d{4});", text):
            names[int(m.group(2))].add(m.group(1))
        for m in re.finditer(r"uint32 const (\w+)\[\]\s*=\s*\{([^}]*)\}", text):
            for idm in re.finditer(r"(91\d{4})", m.group(2)):
                names[int(idm.group(1))].add(m.group(1))
    return names


def server_consumers(files, names):
    """id -> [file:line] where the perk gates behaviour."""
    hits = collections.defaultdict(list)
    patterns = {pid: re.compile(r"\b(%s)\b" % "|".join(re.escape(n) for n in ns))
                for pid, ns in names.items() if ns}
    for path in files:
        base = os.path.basename(path)
        for n, line in enumerate(open(path, encoding="utf-8", errors="replace"), 1):
            stripped = line.strip()
            if stripped.startswith("//") or DECL.search(line) or GRANT.search(line):
                continue
            for pid, pat in patterns.items():
                if pat.search(line):
                    hits[pid].append("%s:%d" % (base, n))
    return hits


def client_consumers(lua_text, ids):
    """id -> count of Lua references beyond the single declaration."""
    out = {}
    for pid in ids:
        total = len(re.findall(r"\b%d\b" % pid, lua_text))
        decl = len(re.findall(r"id = %d," % pid, lua_text))
        out[pid] = total - decl
    return out


def aura_perks(ids):
    """ids whose spell_dbc row has a real aura, so the engine handles them."""
    if not ids:
        return set(), True
    sql = ("SELECT ID FROM spell_dbc WHERE ID IN (%s) AND "
           "(EffectAura_1 > 0 OR EffectAura_2 > 0 OR EffectAura_3 > 0);"
           % ",".join(str(i) for i in sorted(ids)))
    try:
        proc = subprocess.run(
            ["docker", "exec", "-i", "ac-database", "mysql", "--user=root",
             "--password=password", "--batch", "--skip-column-names", "acore_world"],
            input=sql, capture_output=True, text=True, timeout=30)
    except Exception:
        return set(), False
    if proc.returncode != 0:
        return set(), False
    found = set()
    for line in proc.stdout.splitlines():
        line = line.strip()
        if line.isdigit():
            found.add(int(line))
    return found, True


def main():
    if not os.path.isfile(LUA):
        print("run this from the repo root (%s not found)" % LUA, file=sys.stderr)
        return 2

    lua_text = open(LUA, encoding="utf-8", errors="replace").read()
    files = source_files()
    advertised = client_perks(lua_text)
    names = server_constants(files)
    cpp = server_consumers(files, names)
    lua_hits = client_consumers(lua_text, advertised)

    suspects = [pid for pid in advertised if not cpp.get(pid) and not lua_hits.get(pid)]
    auras, db_ok = aura_perks(suspects)

    unwired = []
    for pid in sorted(suspects):
        if pid in auras:
            continue
        unwired.append(pid)

    print("Living Gear perk audit")
    print("  advertised to players : %d" % len(advertised))
    print("  wired in C++          : %d" % sum(1 for p in advertised if cpp.get(p)))
    print("  wired in the addon    : %d" % sum(1 for p in advertised if lua_hits.get(p)))
    print("  real aura, no code    : %d" % len(auras))
    if not db_ok:
        print("  NOTE: could not reach the database, so pure-aura perks may be")
        print("        reported below as unwired. Start ac-database for a clean run.")
    print()

    if not unwired:
        print("No unwired perks. Every advertised perk is read by something.")
        return 0

    print("UNWIRED -- advertised, earnable, and read by nothing:")
    for pid in unwired:
        alias = ", ".join(sorted(names.get(pid, ()))) or "no server constant"
        print("  %-7d %-22s (%s)" % (pid, advertised[pid][:22], alias))
    print()
    print("%d perk(s) need either an implementation or removal from the panel." % len(unwired))
    return 1


if __name__ == "__main__":
    sys.exit(main())
