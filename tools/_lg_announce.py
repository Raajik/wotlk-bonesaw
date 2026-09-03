from pathlib import Path
import re

t = Path(r"A:/wow-bonesaw/modules/mod-living-gear/src/LivingGear.cpp").read_text(encoding="utf-8")
i = t.find("AnnounceAutoAttuneTiers")
print("line", t[:i].count("\n")+1)
print(t[i-120:i+80])
print("count", t.count("AnnounceAutoAttuneTiers"))
# Save helpers
for n in ["SaveJumpMode", "SaveUiScale", "SaveAutoAttuneSettings", "SaveAttunedDe", "AnnounceAutoAttuneTiers"]:
    bodies = len(re.findall(r"void "+n+r"\s*\([^;{]*\)\s*\{", t))
    print(n, "calls", t.count(n+"("), "bodies", bodies)
