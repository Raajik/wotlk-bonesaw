import re
from pathlib import Path

path = Path(r"A:\wow-bonesaw\modules\mod-living-gear\src\LivingGear.cpp")
lines = path.read_text(encoding="utf-8").splitlines(keepends=True)

start_re = re.compile(
    r"^((?:static\s+)?(?:[\w:<>,*&\s]+?)\s+(\w+)\s*\([^;]*\))\s*\{?\s*$"
)

def is_start(line):
    s = line.strip()
    if not s or s.startswith("//") or s.startswith("#"):
        return None
    if s.endswith(";"):
        return None
    m = start_re.match(s)
    if m:
        return m.group(2)
    return None

method_re = re.compile(r"^([\w:<>,*&\s]+\s+(\w+)::\w+\s*\([^;]*\))\s*\{?\s*$")

def is_method_start(line):
    s = line.strip()
    if s.endswith(";"):
        return None
    m = method_re.match(s)
    if m:
        return m.group(1)
    return None

i = 0
out = []
seen = set()
removed = []

while i < len(lines):
    line = lines[i]
    stripped = line.strip()
    name = is_start(line)
    key = None
    if name:
        key = ("fn", stripped.split("{")[0].strip())
    else:
        mk = is_method_start(line)
        if mk:
            key = ("meth", mk.split("{")[0].strip())

    if key and key in seen:
        depth = stripped.count("{") - stripped.count("}")
        if "{" not in stripped:
            i += 1
            if i >= len(lines):
                break
            depth += lines[i].count("{") - lines[i].count("}")
            i += 1
        while i < len(lines) and depth > 0:
            depth += lines[i].count("{") - lines[i].count("}")
            i += 1
        removed.append(key[1][:80])
        continue

    if key:
        seen.add(key)

    out.append(line)
    i += 1

path.write_text("".join(out), encoding="utf-8")
print("removed", len(removed), "duplicates")
print("lines before", len(lines), "after", len(out))
for r in removed[:40]:
    print(" -", r)
if len(removed) > 40:
    print(" ...", len(removed) - 40, "more")
