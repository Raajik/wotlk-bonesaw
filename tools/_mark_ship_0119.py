import subprocess
marks = [
    (200, "10x emblem payout from random dungeons shipped 0.1.119"),
    (212, "Giant Insect Swarm damage gutted, shipped 0.1.119"),
    (133, "SFK courtyard door removed, shipped 0.1.119"),
    (158, "Auto-attune toggle + quality filter shipped 0.1.119"),
    (163, "WG defender portal fix shipped 0.1.119"),
    (172, "Permanent self-buffs + resurrect reapply shipped 0.1.119"),
    (174, "Affliction DoTs indefinite, shipped 0.1.119"),
    (203, "Account-wide reputation shipped 0.1.119"),
    (216, "Account-wide reputation shipped 0.1.119"),
    (218, "Dalaran flying server+client shipped 0.1.119"),
    (165, "5s-cadence fill teleport retry shipped 0.1.119"),
    (173, "Violet Hold one-trash-per-boss rework shipped 0.1.119"),
    (160, "Skill-book autovendor shipped 0.1.119"),
    (157, "Bot-only group disband shipped 0.1.119"),
    (162, "Flat 10x siege damage game-wide shipped 0.1.119"),
    (217, "Living Bomb same-tick chain spread shipped 0.1.119"),
    (219, "CRAFTCAST pays bags-then-vault in place, shipped 0.1.119"),
    (178, "Same CRAFTCAST rework, shipped 0.1.119"),
    (197, "Same CRAFTCAST rework, shipped 0.1.119"),
    (198, "Same CRAFTCAST rework, shipped 0.1.119"),
    (113, "Same CRAFTCAST rework, shipped 0.1.119"),
    (137, "Same CRAFTCAST rework, shipped 0.1.119"),
    (138, "Same CRAFTCAST rework, shipped 0.1.119"),
]
for rid, note in marks:
    r = subprocess.run(["python", "tools/bug-reports/bug_resolve.py", str(rid), "attempted", note],
                       capture_output=True, text=True)
    out = (r.stdout + r.stderr).strip().splitlines()
    print(rid, out[0] if out else ("OK" if r.returncode == 0 else f"rc={r.returncode}"))
