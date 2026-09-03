import re, glob
for p in glob.glob('modules/mod-playerbots/src/**/*.cpp', recursive=True):
    s = open(p, encoding='utf-8', errors='replace').read()
    for m in re.finditer(r'GROUP_DISBAND|GROUP_LEAVE|CMSG_GROUP|OnPlayerLeav|HandleGroupDisband|OnMasterLog', s):
        line = s.count('\n', 0, m.start()) + 1
        print(p.replace('\\', '/'), line, m.group())
