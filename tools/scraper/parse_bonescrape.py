#!/usr/bin/env python3
"""Turn a BoneScrape SavedVariables file into JSON plus a readable feature report.

    python parse_bonescrape.py "B:/Games/WoW 3.3.5/Peloria/WTF/Account/UNABLE3022/SavedVariables/BoneScrape.lua" -o out/peloria

Writes <out>.json (everything) and <out>.md (a human-readable digest aimed at
working out which server features are worth duplicating).

The SavedVariables format is a small, well-behaved subset of Lua -- assignments
of nested table constructors containing only strings, numbers and booleans --
so a hand-rolled tokenizer beats pulling in a Lua runtime.
"""

import argparse
import json
import os
import re
import sys

TOKEN = re.compile(
    r"""
    (?P<ws>\s+|--\[\[.*?\]\]|--[^\n]*)
  | (?P<str>"(?:\\.|[^"\\])*")
  | (?P<num>-?(?:0[xX][0-9a-fA-F]+|\d+\.?\d*(?:[eE][-+]?\d+)?))
  | (?P<name>[A-Za-z_][A-Za-z0-9_]*)
  | (?P<sym>[{}\[\]=,;])
    """,
    re.VERBOSE | re.DOTALL,
)

ESCAPES = {"n": "\n", "t": "\t", "r": "\r", '"': '"', "\\": "\\", "'": "'"}


def unquote(raw):
    body, out, i = raw[1:-1], [], 0
    while i < len(body):
        c = body[i]
        if c == "\\" and i + 1 < len(body):
            nxt = body[i + 1]
            if nxt.isdigit():
                j = i + 1
                digits = ""
                while j < len(body) and body[j].isdigit() and len(digits) < 3:
                    digits += body[j]
                    j += 1
                out.append(chr(int(digits)))
                i = j
                continue
            out.append(ESCAPES.get(nxt, nxt))
            i += 2
            continue
        out.append(c)
        i += 1
    return "".join(out)


def tokenize(text):
    pos, end, toks = 0, len(text), []
    while pos < end:
        m = TOKEN.match(text, pos)
        if not m:
            raise SyntaxError("bad token at offset %d: %r" % (pos, text[pos : pos + 40]))
        pos = m.end()
        kind = m.lastgroup
        if kind == "ws":
            continue
        toks.append((kind, m.group()))
    return toks


class Parser:
    def __init__(self, toks):
        self.t, self.i = toks, 0

    def peek(self):
        return self.t[self.i] if self.i < len(self.t) else (None, None)

    def take(self, want=None):
        kind, val = self.peek()
        if want and val != want:
            raise SyntaxError("expected %r, got %r" % (want, val))
        self.i += 1
        return val

    def value(self):
        kind, val = self.peek()
        if val == "{":
            return self.table()
        self.i += 1
        if kind == "str":
            return unquote(val)
        if kind == "num":
            if val.lower().startswith("0x") or val.lower().startswith("-0x"):
                return int(val, 16)
            return float(val) if ("." in val or "e" in val.lower()) else int(val)
        if val == "true":
            return True
        if val == "false":
            return False
        if val == "nil":
            return None
        raise SyntaxError("unexpected value %r" % (val,))

    def table(self):
        self.take("{")
        out, arr = {}, []
        while True:
            kind, val = self.peek()
            if val == "}":
                self.i += 1
                break
            if val == "[":
                self.i += 1
                key = self.value()
                self.take("]")
                self.take("=")
                out[key] = self.value()
            elif kind == "name" and self.t[self.i + 1][1] == "=":
                key = self.take()
                self.take("=")
                out[key] = self.value()
            else:
                arr.append(self.value())
            kind, val = self.peek()
            if val in (",", ";"):
                self.i += 1
        if arr and not out:
            return arr
        for n, v in enumerate(arr, 1):
            out.setdefault(n, v)
        return out


def load_saved_variables(path):
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    toks = tokenize(text)
    p, result = Parser(toks), {}
    while p.i < len(p.t):
        kind, val = p.peek()
        if kind != "name":
            p.i += 1
            continue
        name = p.take()
        if p.peek()[1] != "=":
            continue
        p.take("=")
        result[name] = p.value()
        if p.peek()[1] == ";":
            p.i += 1
    return result


# ------------------------------------------------------------------ report --

QUALITY = ["Poor", "Common", "Uncommon", "Rare", "Epic", "Legendary", "Artifact", "Heirloom"]

# Tooltip text that stock 3.3.5 never produces. A line matching none of the
# vanilla shapes is a candidate custom mechanic worth reading.
VANILLA_TIP = re.compile(
    r"^(\+?\d|Binds when|Soulbound|Unique|Item Level|Requires Level|Requires |"
    r"Durability|Sell Price|Classes:|Races:|Use:|Equip:|Chance on hit:|"
    r"Cooldown remaining|\d+ Slot|Speed |\(\d|Head|Neck|Shoulder|Back|Chest|"
    r"Shirt|Tabard|Wrist|Hands|Waist|Legs|Feet|Finger|Trinket|Main Hand|"
    r"Off Hand|One-Hand|Two-Hand|Held In Off-hand|Ranged|Thrown|Relic|"
    r"Cloth|Leather|Mail|Plate|Shield|Dagger|Sword|Axe|Mace|Staff|Bow|Gun|"
    r"Crossbow|Wand|Fist Weapon|Polearm|Miscellaneous|Quest Item|Conjured|"
    r"Sockets?|Socket Bonus|Meta|Red|Yellow|Blue|Prismatic|Set:|\s*$)",
    re.IGNORECASE,
)


def tip_lines(entry):
    out = list(entry.get("tip") or [])
    for lines in (entry.get("vtip") or {}).values():
        out.extend(lines)
    return out


def custom_tip_lines(entry):
    return [l for l in tip_lines(entry) if l and not VANILLA_TIP.match(l.strip())]


def as_rows(bucket):
    """SavedVariables integer keys come back as ints; normalise to (key, dict)."""
    if not isinstance(bucket, dict):
        return []
    return [(k, v) for k, v in bucket.items() if isinstance(v, dict)]


def report(realm, db, fh):
    w = fh.write
    w("# %s\n\n" % realm)
    w("Client build: `%s`\n\n" % db.get("build", "?"))
    counts = {
        k: len(db.get(k) or {})
        for k in ("items", "spells", "npcs", "vendors", "gossip", "quests",
                  "questlog", "trainers", "loot", "craft", "addon", "chat", "book")
    }
    w("| bucket | rows |\n|---|---|\n")
    for k, v in counts.items():
        if v:
            w("| %s | %d |\n" % (k, v))
    w("\n")

    # --- addon protocol: the highest-signal section on a custom server ------
    addon = db.get("addon") or {}
    if addon:
        w("## Addon message protocol\n\n")
        w("Every custom UI panel on the server is driven through one of these.\n\n")
        for prefix, rec in sorted(addon.items(), key=lambda kv: -(kv[1].get("n") or 0)):
            w("### `%s` (%d messages)\n\n" % (prefix, rec.get("n") or 0))
            samples = rec.get("samples") or []
            if isinstance(samples, dict):
                samples = [samples[k] for k in sorted(samples)]
            w("```\n")
            for s in samples[:40]:
                w("%s\n" % s)
            w("```\n\n")

    # --- gossip menus: feature entry points --------------------------------
    gossip = db.get("gossip") or {}
    if gossip:
        w("## Gossip menus (%d NPCs)\n\n" % len(gossip))
        for npc_id, rec in sorted(as_rows(gossip)):
            menus = rec.get("menus") or {}
            if isinstance(menus, dict):
                menus = [menus[k] for k in sorted(menus)]
            w("### %s (entry %s) -- %s\n\n" % (rec.get("name") or "?", npc_id,
                                               rec.get("zone") or "?"))
            for menu in menus:
                body = (menu.get("text") or "").strip()
                if body:
                    w("> %s\n\n" % body.replace("\n", "\n> "))
                opts = menu.get("opt") or {}
                if isinstance(opts, dict):
                    opts = [opts[k] for k in sorted(opts)]
                for o in opts:
                    w("- %s\n" % o.replace("\t", "  --  "))
                w("\n")

    # --- items whose tooltips contain non-vanilla mechanics ----------------
    items = db.get("items") or {}
    interesting = []
    for item_id, rec in as_rows(items):
        extra = custom_tip_lines(rec)
        if extra:
            interesting.append((item_id, rec, extra))
    if interesting:
        w("## Items with non-standard tooltip text (%d of %d)\n\n"
          % (len(interesting), len(items)))
        w("Lines below did not match any stock 3.3.5 tooltip shape, so they are\n"
          "candidate custom mechanics.\n\n")
        interesting.sort(key=lambda t: -(t[1].get("ilvl") or 0))
        for item_id, rec, extra in interesting:
            q = rec.get("q")
            w("- **%s** (`%s`, ilvl %s, %s)\n" % (
                rec.get("name") or "?", item_id, rec.get("ilvl") or "?",
                QUALITY[q] if isinstance(q, int) and 0 <= q < len(QUALITY) else "?"))
            for l in extra:
                w("    - %s\n" % l.replace("\t", "  --  "))
        w("\n")

    # --- vendors, including token/currency costs ---------------------------
    vendors = db.get("vendors") or {}
    if vendors:
        w("## Vendors\n\n")
        for npc_id, rec in sorted(as_rows(vendors)):
            rows = rec.get("items") or {}
            if isinstance(rows, dict):
                rows = [rows[k] for k in sorted(rows)]
            w("### %s (entry %s) -- %s, %d items\n\n"
              % (rec.get("name") or "?", npc_id, rec.get("zone") or "?", len(rows)))
            for r in rows:
                cost = r.get("cost") or {}
                if isinstance(cost, dict):
                    cost = [cost[k] for k in sorted(cost)]
                extra = ""
                if cost:
                    extra = " [tokens: %s]" % ", ".join(
                        "%sx %s" % (c.get("n"), c.get("link") or c.get("tex"))
                        for c in cost)
                w("- %s -- %s copper%s\n" % (r.get("name") or "?", r.get("price") or 0, extra))
            w("\n")

    # --- spells seen that are outside the retail 3.3.5 id space ------------
    spells = db.get("spells") or {}
    custom_spells = [(sid, rec) for sid, rec in as_rows(spells)
                     if isinstance(sid, int) and sid > 80000]
    if custom_spells:
        w("## Spells above id 80000 (likely server-authored) -- %d\n\n" % len(custom_spells))
        for sid, rec in sorted(custom_spells):
            w("- **%s** (`%s`)\n" % (rec.get("name") or "?", sid))
            for l in tip_lines(rec)[1:]:
                w("    - %s\n" % l.replace("\t", "  --  "))
        w("\n")

    # --- loot tables -------------------------------------------------------
    loot = db.get("loot") or {}
    if loot:
        w("## Observed loot\n\n")
        for src, rec in as_rows(loot):
            drops = rec.get("drops") or {}
            w("### %s (entry %s) -- %s opens\n\n"
              % (rec.get("name") or "?", src, rec.get("opens") or 0))
            for item_id, d in sorted(as_rows(drops)):
                name = (items.get(item_id) or {}).get("name") or "?"
                w("- %s (`%s`) x%s-%s, seen %s\n"
                  % (name, item_id, d.get("min"), d.get("max"), d.get("n")))
            w("\n")

    # --- quests ------------------------------------------------------------
    quests = db.get("quests") or {}
    if quests:
        w("## Quest text captured (%d)\n\n" % len(quests))
        for title, rec in sorted(as_rows(quests)):
            w("### %s\n\n" % title)
            if rec.get("giverName"):
                w("Giver: %s (entry %s), %s\n\n"
                  % (rec["giverName"], rec.get("giver"), rec.get("zone") or "?"))
            for k in ("detail", "obj", "progress", "complete"):
                if rec.get(k):
                    w("**%s:** %s\n\n" % (k, rec[k]))

    # --- server broadcasts -------------------------------------------------
    chat = db.get("chat") or {}
    if chat:
        w("## Server messages\n\n")
        for e, lines in sorted(chat.items()):
            if isinstance(lines, dict):
                lines = [lines[k] for k in sorted(lines)]
            if not lines:
                continue
            w("### %s\n\n```\n" % e)
            for l in lines[:120]:
                w("%s\n" % l)
            w("```\n\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("saved_variables", help="path to BoneScrape.lua in WTF/.../SavedVariables")
    ap.add_argument("-o", "--out", default="bonescrape",
                    help="output path prefix (default: bonescrape)")
    ap.add_argument("--realm", help="only report this realm")
    args = ap.parse_args()

    data = load_saved_variables(args.saved_variables)
    db = data.get("BoneScrapeDB")
    if not db:
        sys.exit("no BoneScrapeDB table found in %s" % args.saved_variables)

    out_dir = os.path.dirname(os.path.abspath(args.out))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)

    with open(args.out + ".json", "w", encoding="utf-8") as fh:
        json.dump(db, fh, indent=1, ensure_ascii=False, default=str)

    with open(args.out + ".md", "w", encoding="utf-8") as fh:
        for realm, realm_db in sorted(db.items()):
            if args.realm and realm != args.realm:
                continue
            if isinstance(realm_db, dict):
                report(realm, realm_db, fh)

    print("wrote %s.json and %s.md" % (args.out, args.out))
    for realm, realm_db in sorted(db.items()):
        if isinstance(realm_db, dict):
            print("  %-20s items=%d spells=%d npcs=%d gossip=%d addon=%d" % (
                realm,
                len(realm_db.get("items") or {}),
                len(realm_db.get("spells") or {}),
                len(realm_db.get("npcs") or {}),
                len(realm_db.get("gossip") or {}),
                len(realm_db.get("addon") or {}),
            ))


if __name__ == "__main__":
    main()
