from pathlib import Path

path = Path(r"A:\wow-bonesaw\modules\mod-living-gear\src\LivingGear.cpp")
lines = path.read_text(encoding="utf-8").splitlines(keepends=True)

# Remove exact duplicate consecutive blocks (>= 15 lines)
i = 0
removed_blocks = 0
while i < len(lines) - 15:
    removed = False
    for block in range(min(200, len(lines) - i), 14, -1):
        chunk = lines[i:i+block]
        # find next exact match within next 500 lines
        for j in range(i+block, min(len(lines)-block, i+500)):
            if lines[j:j+block] == chunk:
                del lines[j:j+block]
                removed_blocks += 1
                removed = True
                break
        if removed:
            break
    if not removed:
        i += 1

path.write_text("".join(lines), encoding="utf-8")
print("removed_blocks", removed_blocks, "final_lines", len(lines))
