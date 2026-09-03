import time
for _ in range(55):
    time.sleep(20)
    s = open('tools/_build_ship_0119.log', encoding='utf-8', errors='replace').read()
    if 'Image acore/ac-wotlk-worldserver:master Built' in s and 'Image acore/ac-wotlk-db-import:master Built' in s:
        print('SHIP BUILD DONE')
        break
    if 'error:' in s:
        print('BUILD FAILURE:')
        print(s[-2500:])
        break
else:
    print('still running')
