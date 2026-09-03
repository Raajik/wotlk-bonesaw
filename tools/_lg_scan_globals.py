import re
from pathlib import Path

t = Path(r"A:/wow-bonesaw/modules/mod-living-gear/src/LivingGear.cpp").read_text(encoding="utf-8")
used = sorted(set(re.findall(r"\bg_[A-Za-z][A-Za-z0-9_]*\b", t)))
# declared if appears with a type in front at column 0-ish
decl_re = re.compile(
    r"^(?:static\s+)?(?:std::[\w:<>,\s*&]+|bool|uint\d+|int\d+|float|double|Lg\w+|Fury\w+|Zone\w+)\s+(g_\w+)\b",
    re.MULTILINE,
)
declared = set(decl_re.findall(t))
# also struct instances
declared |= set(re.findall(r"\n(?:static\s+)?(?:std::unordered_(?:map|set)<[^;]+>|std::mutex|std::vector<[^;]+>|bool|uint32|uint8)\s+(g_\w+)", t))
missing = [g for g in used if g not in declared]
print("used", len(used), "declared", len(declared), "missing", len(missing))
for g in missing:
    # sample use
    i = t.find(g)
    line = t[:i].count("\n") + 1
    print(f"{g:30} first_line={line} count={t.count(g)}")
