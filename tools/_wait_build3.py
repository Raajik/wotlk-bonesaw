import time
path = 'tools/_build_siege_162.log'
s = ''
for _ in range(55):
    time.sleep(20)
    s = open(path, encoding='utf-8', errors='replace').read()
    if 'Image acore/ac-wotlk-worldserver:master Built' in s and 'Image acore/ac-wotlk-db-import:master Built' in s:
        print('BUILD #3 DONE')
        break
    if 'error:' in s:
        print('FAILURE:')
        print(s[-3000:])
        break
else:
    print('still running after wait window')
print(s[-300:] if s else 'no log yet')
