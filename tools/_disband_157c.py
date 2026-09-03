def crlf(t):
    return t.replace('\n', '\r\n')

# ---- header: add declaration after UpdateLfgFillTeleports
p = 'modules/mod-playerbots/src/Bot/RandomPlayerbotMgr.h'
s = open(p, 'r', encoding='utf-8', newline='').read()
old = """    void ProcessLfgFillTeleports();
    void UpdateLfgFillTeleports();
"""
new = """    void ProcessLfgFillTeleports();
    void UpdateLfgFillTeleports();
    // Report #157: dissolve groups whose members are all bots (the raid a
    // real player walked away from), so the bots go back to the pool.
    void DisbandBotOnlyGroups();
"""
n = s.count(crlf(old))
assert n == 1, f'header anchor: {n}'
s = s.replace(crlf(old), crlf(new))
open(p, 'w', encoding='utf-8', newline='').write(s)
print('h OK')

# ---- cpp: wire the call into UpdateAIInternal
p2 = 'modules/mod-playerbots/src/Bot/RandomPlayerbotMgr.cpp'
s2 = open(p2, 'r', encoding='utf-8', newline='').read()
old2 = """    // hook drives UpdateLfgFillTeleports every update instead (report #165).
"""
new2 = """    // hook drives UpdateLfgFillTeleports every update instead (report #165).
    // The same pass cadence is right for orphaned bot groups: a raid whose
    // real players left lives minutes, not forever (report #157).
    DisbandBotOnlyGroups();
"""
n2 = s2.count(crlf(old2))
assert n2 == 1, f'cpp call anchor: {n2}'
s2 = s2.replace(crlf(old2), crlf(new2))
open(p2, 'w', encoding='utf-8', newline='').write(s2)
print('cpp call OK')
