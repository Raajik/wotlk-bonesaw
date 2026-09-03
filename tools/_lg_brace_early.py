from pathlib import Path

t = Path(r"A:/wow-bonesaw/modules/mod-living-gear/src/LivingGear.cpp").read_text(encoding="utf-8")
depth = 0
line = 1
ns = t.find("namespace LivingGear")
started = False
for i, ch in enumerate(t):
    if ch == "\n":
        line += 1
        continue
    if ch == "{":
        depth += 1
        if line > 100 and depth >= 4 and line < 1800:
            print("depth", depth, "line", line)
            if depth >= 5:
                print(t[max(0, i - 120) : i + 40])
                break
    elif ch == "}":
        depth -= 1
print("done depth", depth, "line", line)
