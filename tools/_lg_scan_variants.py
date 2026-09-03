from pathlib import Path
import hashlib

base = Path(r"A:/wow-bonesaw/modules/mod-living-gear/src")
for p in sorted(base.glob("LivingGear.cpp*"), key=lambda x: -x.stat().st_size):
    b = p.read_bytes()
    t = b.decode("utf-8", errors="replace")
    h = hashlib.sha256(b).hexdigest()[:16]
    print(
        f"{p.name:40} bytes={len(b):7} nl={t.count(chr(10)):5} "
        f"craftdef={('bool IsCraftingSpell(SpellInfo' in t)} "
        f"g_syncing={'g_syncing' in t} sha={h}"
    )
