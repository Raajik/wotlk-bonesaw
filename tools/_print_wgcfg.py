s = open('modules/mod-living-gear/src/LivingGear_Support.cpp', encoding='utf-8', errors='replace').read()
lines = s.split('\n')
print('\n'.join('%4d %s' % (i + 1, lines[i]) for i in range(841, 900)))
# find g_wgSiegeScale config decls
import re
for m in re.finditer(r'g_wgSiegeScale|g_wgFullRoster|g_wgMaxSiegeMult', s):
    line = s.count('\n', 0, m.start()) + 1
    if 'uint32 const' in lines[line - 1] or 'GetOption' in lines[line - 1] or 'float' in lines[line - 1]:
        print('CFG line', line, lines[line - 1])
