"""Tail a log file (default: the last build log), tolerating missing files."""
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
path = sys.argv[1] if len(sys.argv) > 1 else r"A:\wow-bonesaw\tools\_build_rep220.log"
try:
    t = open(path, "rb").read().decode("utf-8", "replace")
except FileNotFoundError:
    print("log not created yet:", path)
    raise SystemExit
lines = t.splitlines()
print("%d lines total" % len(lines))
print("\n".join(lines[-25:]))
