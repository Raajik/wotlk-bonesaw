from pathlib import Path
import re
from collections import defaultdict

p = Path(r"A:/wow-bonesaw/modules/mod-living-gear/src/LivingGear.cpp")
text = p.read_text(encoding="utf-8")

text = text.replace(
    "    return have > vault ? have - vault : 0;\nvoid SaveRules",
    "    return have > vault ? have - vault : 0;\n}\n\nvoid SaveRules",
)
text = text.replace(
    '    LOG_ERROR("module", "Living Gear login done {}", name);\nclass LivingGearPlayer',
    '    LOG_ERROR("module", "Living Gear login done {}", name);\n}\n\nclass LivingGearPlayer',
)

servers = list(re.finditer(r"class LivingGearServer : public ServerScript\s*\{.*?\n\};\n", text, re.S))
keep_s = None
for m in servers:
    if "CMSG_QUESTGIVER_CANCEL" in m.group(0):
        keep_s = m
        break
if keep_s is None and servers:
    keep_s = servers[-1]
if servers and keep_s:
    text = text[: servers[0].start()] + keep_s.group(0) + text[servers[-1].end() :]

moves = list(re.finditer(r"class LivingGearMove : public MovementHandlerScript\s*\{.*?\n\};\n", text, re.S))
keep_m = None
for m in moves:
    if "g_lastExtraJumpMs.erase" in m.group(0):
        keep_m = m
        break
if keep_m is None and moves:
    keep_m = moves[-1]
if moves and keep_m:
    text = text[: moves[0].start()] + keep_m.group(0) + text[moves[-1].end() :]

cls_start = text.find("class LivingGearPlayer : public PlayerScript")
cls_end = text.find("\nclass LivingGearMail", cls_start)
body = text[cls_start:cls_end]
meth = re.compile(r"^(?:bool|void|static bool) (\w+)\s*\(", re.M)
blocks = []
for m in meth.finditer(body):
    name = m.group(1)
    if name == "LivingGearPlayer":
        continue
    i = body.find("{", m.end())
    if i == -1:
        continue
    depth = 0
    pos = i
    while pos < len(body):
        if body[pos] == "{":
            depth += 1
        elif body[pos] == "}":
            depth -= 1
            if depth == 0:
                end = pos + 1
                if end < len(body) and body[end] == "\n":
                    end += 1
                blocks.append((name, m.start(), end))
                break
        pos += 1

by = defaultdict(list)
for b in blocks:
    by[b[0]].append(b)
print("dup methods", {k: len(v) for k, v in by.items() if len(v) > 1})
remove = []
for name, copies in by.items():
    if len(copies) <= 1:
        continue
    for c in copies[1:]:
        remove.append((c[1], c[2]))
remove.sort(reverse=True)
for a, b in remove:
    body = body[:a] + body[b:]
text = text[:cls_start] + body + text[cls_end:]

p.write_text(text, encoding="utf-8", newline="\n")
print("lines", text.count("\n"))
print("LivingGearServer", text.count("class LivingGearServer"))
print("LivingGearMove", text.count("class LivingGearMove"))
print("OnPlayerReputationChange", text.count("OnPlayerReputationChange"))
print("OnPlayerSpellCast", text.count("void OnPlayerSpellCast"))
print("BagCount closed", "return have > vault ? have - vault : 0;\n}" in text)
print("Finish closed", 'login done {}", name);\n}' in text)
