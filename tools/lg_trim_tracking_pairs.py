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
    if lines[i].startswith("static uint32 TrackingResourceBit"):
        t = i
        j = i + 1
        while j < len(lines) and not lines[j].startswith("void EnableGatherTracking(Player* player)"):
            j += 1
        if j >= len(lines):
            break
        e = j
        end = end_of_func(e)
        pairs.append((t, end))
        i = end
        continue
    i += 1

if len(pairs) <= 1:
    raise SystemExit(f"expected multiple pairs, got {len(pairs)}")

for t, end in reversed(pairs[1:]):
    del lines[t:end]

path.write_text("".join(lines), encoding="utf-8")
text = "".join(lines)
print("removed", len(pairs)-1, "pairs; lines", len(lines), "brace_delta", text.count("{") - text.count("}"))
