"""Poll a docker build log until 'Built' markers appear or an error shows."""
import os
import sys
import time

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
path = sys.argv[1] if len(sys.argv) > 1 else r"A:\wow-bonesaw\tools\_build_164.log"
deadline = time.time() + 420
while time.time() < deadline:
    time.sleep(20)
    try:
        t = open(path, "rb").read().decode("utf-8", "replace")
    except FileNotFoundError:
        continue
    if t.count("Built") >= 2 and "provenance" in t:
        print("BUILD COMPLETE after ~%ds of polling" % 20)
        break
    low = t.lower()
    if "error" in low and "0 errors" not in low and "error:" in low:
        print("BUILD FAILURE detected")
        break
else:
    print("still building after polling window")

try:
    t = open(path, "rb").read().decode("utf-8", "replace")
except FileNotFoundError:
    raise SystemExit
lines = t.splitlines()
print("%d lines; tail:" % len(lines))
print("\n".join(lines[-8:]))
