import re
s = open('modules/mod-living-gear/src/LivingGear_Support.cpp', encoding='utf-8', errors='replace').read()
lines = s.split('\n')
for m in re.finditer('ModifyHealth', s):
    line = s.count('\n', 0, m.start()) + 1
    print('=== line', line)
    print('\n'.join('%4d %s' % (i + 1, lines[i]) for i in range(max(0, line - 14), min(len(lines), line + 24))))
    print()
