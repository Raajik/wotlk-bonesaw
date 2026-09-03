s = open('modules/mod-playerbots/src/Bot/RandomPlayerbotMgr.cpp', encoding='utf-8', errors='replace').read()
i = s.find('bool RandomPlayerbotMgr::ProcessBot(uint32 bot)')
seg = s[i:i + 5200]
print(seg)
