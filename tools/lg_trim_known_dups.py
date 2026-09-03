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

def find_nth(prefix: str, n: int) -> int:
    count = 0
    for i, line in enumerate(lines):
        if line.startswith(prefix):
            count += 1
            if count == n:
                return i
    raise SystemExit(f"missing {prefix} #{n}")

def remove_pair(track_n: int):
    t = find_nth("static uint32 TrackingResourceBit", track_n)
    e = find_nth("void EnableGatherTracking", track_n)
    end = end_of_func(e)
    del lines[t:end]

# second SelectClassPerk
s = find_nth("void SelectClassPerk", 2)
del lines[s:end_of_func(s)]

# remove 2nd and 3rd tracking pairs (keep 1st)
remove_pair(3)
remove_pair(2)

path.write_text("".join(lines), encoding="utf-8")
text = "".join(lines)
print("lines", len(lines), "brace_delta", text.count("{") - text.count("}"))
