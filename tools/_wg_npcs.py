import re
src = open('src/server/game/Battlefield/Zones/BattlefieldWG.h').read()
for name in ("WGKeepNPC", "WGOutsideNPC"):
    m = re.search(name + r"\[WG_[A-Z_0-9]+\]\s*=\s*\{(.*?)\n\};", src, re.S)
    if not m:
        print(name, "NOT FOUND")
        continue
    body = m.group(1)
    rows = re.findall(r"\{\s*(\d+)\s*,\s*(\d+)\s*,", body)
    print(name, "rows:", len(rows))
    horde = {}
    ally = {}
    for h, a in rows:
        horde[h] = horde.get(h, 0) + 1
        ally[a] = ally.get(a, 0) + 1
    print("  horde:", horde)
    print("  ally:", ally)
