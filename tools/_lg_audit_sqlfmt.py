"""Scratch: audit fmt-string Execute() calls in LivingGear module + Spell.cpp refusal sites."""
import io, glob, re

print("=== Execute fmt audit (module srcs) ===")
for p in sorted(glob.glob('modules/mod-living-gear/src/*.cpp')):
    s = io.open(p, encoding='utf-8', errors='replace').read()
    for m in re.finditer(r'\.\s*(P?Execute|DirectExecute)\s*\(', s):
        seg = s[m.end():m.end() + 900]
        strs = re.findall(r'"((?:[^"\\]|\\.)*)"', seg)
        if not strs:
            continue
        ph = 0
        for i, x in enumerate(strs):
            if i == 0 or re.match(r'\s*(INSERT|UPDATE|DELETE|SELECT|REPLACE|VALUES|\(|`|ON|\{)', x, re.I):
                ph += x.count('{}')
            else:
                break
        sql = strs[0]
        if not re.match(r'\s*(INSERT|UPDATE|DELETE|SELECT|REPLACE)', sql, re.I):
            continue
        line = s[:m.start()].count('\n') + 1
        print('%s:%d ph=%d :: %s' % (p.replace('\\', '/').split('/')[-1], line, ph, sql[:96]))

print("=== Spell.cpp vault/refusal sites ===")
sp = io.open('src/server/game/Spells/Spell.cpp', encoding='utf-8', errors='replace').read().splitlines()
seen = set()
for i, l in enumerate(sp):
    if ('LivingGear_TopUpReagentFromVault' in l or 'reagent bank craft refused' in l
            or ('SPELL_FAILED_REAGENTS' in l and 'return' in l)):
        lo = max(0, i - 7)
        key = lo // 8
        if key in seen:
            continue
        seen.add(key)
        print('--- lines %d-%d ---' % (lo + 1, min(len(sp), i + 8)))
        for j in range(lo, min(len(sp), i + 8)):
            print(j + 1, sp[j].rstrip())
