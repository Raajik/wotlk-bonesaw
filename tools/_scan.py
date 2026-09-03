import io, re, sys
path = sys.argv[1]
pats = [p.lower() for p in sys.argv[2:]]
src = io.open(path, encoding='utf-8', errors='replace').read()
for i, line in enumerate(src.splitlines(), 1):
    low = line.lower()
    if any(p in low for p in pats):
        print(i, line[:170])
