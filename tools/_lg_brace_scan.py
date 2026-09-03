from pathlib import Path

t = Path(r"A:/wow-bonesaw/modules/mod-living-gear/src/LivingGear.cpp").read_text(encoding="utf-8")
start = t.find("namespace LivingGear")
print("ns line", t[:start].count("\n") + 1)
depth = 0
line = 1
ns_depth_at_open = None
for i, ch in enumerate(t):
    if ch == "\n":
        line += 1
        continue
    if ch == "{":
        depth += 1
        if ns_depth_at_open is None and i > start:
            ns_depth_at_open = depth
    elif ch == "}":
        depth -= 1
        if ns_depth_at_open is not None and depth < ns_depth_at_open:
            print("namespace closed at line", line, "filepos", i, "depth", depth)
            ns_depth_at_open = None
            # show snippet
            print(repr(t[max(0, i - 80) : i + 40]))
            break
print("final depth", depth, "lines", line)
# depth at line 2566
depth = 0
line = 1
target = 2566
at = None
for i, ch in enumerate(t):
    if ch == "\n":
        line += 1
        if line == target:
            at = depth
            break
        continue
    if ch == "{":
        depth += 1
    elif ch == "}":
        depth -= 1
print("depth at line", target, at)
