p = 'modules/mod-playerbots/src/Bot/RandomPlayerbotMgr.cpp'
s = open(p, 'r', encoding='utf-8', newline='').read()
old = '#include "ChannelMgr.h"\n'
new = '#include "ChannelMgr.h"\n#include "CharacterCache.h"\n'
n = s.count(old)
assert n == 1, n
s = s.replace(old, new)
open(p, 'w', encoding='utf-8', newline='').write(s)
print('include added')
