import time
for _ in range(55):
    time.sleep(20)
    s = open('tools/_build_launcher_docker.log', encoding='utf-8', errors='replace').read()
    if 'Finished' in s and 'release [optimized]' in s:
        print('LAUNCHER CROSS-BUILD DONE')
        break
    if 'error' in s and 'warning' not in s.split('error')[-1][:60]:
        print('FAILURE:')
        print(s[-1500:])
        break
else:
    print('still running')
print(s[-200:] if (s := open('tools/_build_launcher_docker.log', encoding='utf-8', errors='replace').read()) else '')
