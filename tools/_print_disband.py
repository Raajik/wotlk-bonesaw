import re
s = open('modules/mod-playerbots/src/Bot/RandomPlayerbotMgr.cpp', encoding='utf-8', errors='replace').read()
for i, m in enumerate(re.finditer(r'LeaveOrDisbandGroup\(\)', s)):
    line = s.count('\n', 0, m.start()) + 1
    print(f'=== occurrence {i + 1} at line {line}:')
    print(s[max(0, m.start() - 700):m.start() + 300])
    print()
