from pathlib import Path
import re

p = Path(r"A:/wow-bonesaw/modules/mod-living-gear/src/LivingGear.cpp")
t = p.read_text(encoding="utf-8")
# namespace-level decls ending in semicolon
decl_re = re.compile(
    r"^(?P<full>(?P<static>static\s+)?(?P<ret>void|bool|uint32|uint8|int32|float)\s+(?P<name>[A-Za-z_]\w*)\s*\((?P<args>[^)]*)\)\s*(?:=\s*default)?;)\s*$",
    re.M,
)
needed = []
for m in decl_re.finditer(t):
    name = m.group("name")
    if name in ("LivingGearPlayer", "LivingGearWorld"):
        continue
    body = re.search(
        rf"^{m.group('static') or ''}{m.group('ret')}\s+{name}\s*\([^)]*\)\s*\{{",
        t,
        re.M,
    )
    if not body:
        needed.append(m)
print("need bodies", len(needed))
for m in needed:
    print(m.group("full")[:120])
