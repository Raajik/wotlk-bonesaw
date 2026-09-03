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
    return "(" in s

def header_key(line: str) -> str:
    return line.strip().split("{")[0].strip()

def end_of_func(start_idx: int) -> int:
    i = start_idx
    depth = 0
    while i < len(lines):
        depth += lines[i].count("{") - lines[i].count("}")
        i += 1
        if depth == 0 and i > start_idx + 1:
            return i
    return i

seen = set()
out = []
removed = 0
i = 0
while i < len(lines):
    line = lines[i]
    if is_top_level_def(line):
        key = header_key(line)
        if key in seen:
            i = end_of_func(i)
            removed += 1
            continue
        seen.add(key)
    out.append(line)
    i += 1

path.write_text("".join(out), encoding="utf-8")
print("removed", removed, "lines", len(out))
