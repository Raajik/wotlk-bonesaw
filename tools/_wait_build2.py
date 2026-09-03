import time
path = 'tools/_build_173_160_157.log'
for _ in range(55):
    time.sleep(20)
    s = open(path, encoding='utf-8', errors='replace').read()
    if 'Image acore/ac-wotlk-worldserver:master Built' in s:
        print('BUILD #2 DONE')
        break
    if 'error' in s.lower() and ('FAILED' in s or 'Error ' in s):
        tail = s[-3000:]
        if 'error' in tail.lower():
            print('POSSIBLE FAILURE:')
            print(tail)
            break
else:
    print('still running after wait window')
print(s[-200:] if (s := open(path, encoding='utf-8', errors='replace').read()) else 'no log')
