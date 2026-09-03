import re
s = open('modules/mod-living-gear/client_addon/LivingGear/LivingGear.lua', encoding='utf-8', errors='replace').read()
for line_no, g in [(s.count('\n', 0, m.start()) + 1, m.group()) for m in re.finditer('SOLOSET', s)]:
    print('=== line', line_no)
    lines = s.split('\n')
    print('\n'.join(lines[max(0, line_no - 13):line_no + 7]))
    print()
