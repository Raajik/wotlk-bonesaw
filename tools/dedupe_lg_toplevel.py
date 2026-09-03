from pathlib import Path

path = Path(r"A:\wow-bonesaw\modules\mod-living-gear\src\LivingGear.cpp")
lines = path.read_text(encoding="utf-8").splitlines(keepends=True)

def is_top_level_def(line: str) -> bool:
    if not line or line[0] in " \t":
        return False
    s = line.strip()
    if s.endswith(";"):
        return False
    if s.startswith("//") or s.startswith("#"):
        return False
    if "(" not in s:
        return False
    return True

def header_key(line: str) -> str:
    return line.strip().split("{")[0].strip()

seen = set()
out = []
removed = []
i = 0
while i < len(lines):
    line = lines[i]
    if is_top_level_def(line):
        key = header_key(line)
        if key in seen:
            depth = line.count("{") - line.count("}")
            i += 1
            while i < len(lines) and depth > 0:
                depth += lines[i].count("{") - lines[i].count("}")
                i += 1
            removed.append(key)
            continue
        seen.add(key)
    out.append(line)
    i += 1

path.write_text("".join(out), encoding="utf-8")
print("removed", len(removed), "headers")
print("lines", len(lines), "->", len(out))
for k in removed[:50]:
    print(" -", k[:100])
