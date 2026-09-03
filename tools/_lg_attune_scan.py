from pathlib import Path

t = Path(r"A:/wow-bonesaw/modules/mod-living-gear/src/LivingGear.cpp").read_text(encoding="utf-8")
for n in ["AttunedCount", "SelectedJumpRank", "MaxJumpUnlock", "IsAutolootOn", "GetClassPerk"]:
    print(n, t.count(n))
    idx = 0
    c = 0
    while c < 4:
        i = t.find(n, idx)
        if i < 0:
            break
        print(" ", t[:i].count("\n") + 1, t[i : i + 70].split("\n")[0])
        idx = i + 1
        c += 1
