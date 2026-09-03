from pathlib import Path

path = Path(r"A:\wow-bonesaw\modules\mod-living-gear\src\LivingGear.cpp")
lines = path.read_text(encoding="utf-8").splitlines(keepends=True)

def remove_range(start, end):
    global lines
    del lines[start-1:end]

# 1-based line numbers from stable backup
remove_range(1315, 1332)
# After first removal, line numbers shift by 18
remove_range(1535 - 18, 1574 - 18)
remove_range(1577 - 18 - 40, 1616 - 18 - 40)

path.write_text("".join(lines), encoding="utf-8")
print("lines", len(lines))
