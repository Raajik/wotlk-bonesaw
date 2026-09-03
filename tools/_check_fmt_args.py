"""One-off ship check: verify {} placeholder count == argument count for every
DirectExecute/Query call whose SQL contains a given marker (default lg_absorb)."""
import sys

path = 'modules/mod-living-gear/src/LivingGear.cpp'
marker = sys.argv[1] if len(sys.argv) > 1 else 'REPLACE INTO `lg_absorb'
lines = open(path, encoding='utf-8').read().splitlines()

starts = [i for i, l in enumerate(lines) if marker in l and 'DirectExecute' in lines[i - 1]]
if not starts:
    print('no call sites found for marker:', marker)
    sys.exit(0)

bad = 0
for s in starts:
    call_start = s - 1 if 'DirectExecute' in lines[s - 1] else s
    e = next(j for j in range(call_start + 1, min(call_start + 60, len(lines))) if ');' in lines[j])
    ph = 0
    seg = False
    arg_start = None
    for j in range(call_start + 1, e + 1):
        l = lines[j]
        if l.strip().startswith('"'):
            seg = True
            ph += l.count('{}')
        elif seg and arg_start is None:
            arg_start = j
    args = 0
    if arg_start is not None:
        argtext = ' '.join(lines[k] for k in range(arg_start, e + 1))
        argtext = argtext.rstrip().rstrip(';').rstrip(')')
        depth = 0
        tl = 0
        for ch in argtext:
            if ch in '([':
                depth += 1
            elif ch in ')]':
                depth -= 1
            elif ch == ',' and depth == 0:
                tl += 1
        args = tl + 1
    status = 'OK' if ph == args else 'MISMATCH'
    if ph != args:
        bad += 1
    print('lines {}-{}: placeholders={} args={} -> {}'.format(call_start + 1, e + 1, ph, args, status))

sys.exit(1 if bad else 0)
