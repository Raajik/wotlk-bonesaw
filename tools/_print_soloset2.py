import re, glob
for p in glob.glob('modules/mod-living-gear/src/*.cpp') + glob.glob('modules/mod-living-gear/src/*.h'):
    s = open(p, encoding='utf-8', errors='replace').read()
    if 'SOLOSET' in s:
        for m in re.finditer('SOLOSET', s):
            line = s.count('\n', 0, m.start()) + 1
            lines = s.split('\n')
            print(f'=== {p} line {line}:')
            print('\n'.join(lines[max(0, line - 10):line + 12]))
            print()
# also check perk 910092 refs
for p in glob.glob('modules/mod-living-gear/src/*.cpp'):
    s = open(p, encoding='utf-8', errors='replace').read()
    for m in re.finditer(r'910092', s):
        line = s.count('\n', 0, m.start()) + 1
        lines = s.split('\n')
        print(f'--- 910092 {p} line {line}:')
        print('\n'.join(lines[max(0, line - 5):line + 5]))
        print()
