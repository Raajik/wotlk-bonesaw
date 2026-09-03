from pathlib import Path

path = Path(r"A:\wow-bonesaw\modules\mod-living-gear\src\LivingGear.cpp")
lines = path.read_text(encoding="utf-8").splitlines(keepends=True)

def end_of_func(start_idx: int) -> int:
    i = start_idx
    depth = 0
    while i < len(lines):
        depth += lines[i].count("{") - lines[i].count("}")
        i += 1
        if depth == 0 and i > start_idx + 1:
            return i
    return i

pairs = []
i = 0
while i < len(lines):
    if lines[i].startswith("bool IsAccountRepFaction"):
        t = i
        j = i + 1
        while j < len(lines) and not lines[j].startswith("void SendPendingRepUpdates"):
            j += 1
        if j >= len(lines):
            break
        end = end_of_func(j)
        pairs.append((t, end))
        i = end
        continue
    i += 1

for t, end in reversed(pairs[1:]):
    del lines[t:end]

path.write_text("".join(lines), encoding="utf-8")
print("rep pairs", len(pairs), "removed", max(0, len(pairs)-1), "lines", len(lines))
