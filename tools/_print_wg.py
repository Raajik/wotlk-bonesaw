import re
s = open('src/server/scripts/Northrend/zone_wintergrasp.cpp', encoding='utf-8', errors='replace').read()
# find the attendance/squad scaling (#3 shipped 0.1.54)
for m in re.finditer(r'(?i)(squad|attendance|damagemult|damage multiplier|10x|times damage)', s):
    line = s.count('\n', 0, m.start()) + 1
    lines = s.split('\n')
    print('=== line', line)
    print('\n'.join(lines[max(0, line - 8):line + 8]))
    print()
