import re
s = open('modules/mod-living-gear/client_addon/LivingGear/LivingGear.lua', encoding='utf-8', errors='replace').read()
# find where world toggles are declared (info tables with toggle=, toggleKey=, id=)
for m in re.finditer(r'910092', s):
    line = s.count('\n', 0, m.start()) + 1
    lines = s.split('\n')
    print('=== line', line)
    print('\n'.join(lines[max(0, line - 14):line + 8]))
    print()
