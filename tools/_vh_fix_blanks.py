p = 'src/server/scripts/Northrend/VioletHold/violet_hold.h'
s = open(p, 'r', encoding='utf-8', newline='').read()
old = '"instance_violet_hold"\r\n\r\n\r\nenum VHWaves'
new = '"instance_violet_hold"\r\n\r\nenum VHWaves'
assert s.count(old) == 1
s = s.replace(old, new)
old2 = 'VH_WAVE_CYANIGOSA  = 13,\r\n};\r\n\r\n\r\nenum VHData'
new2 = 'VH_WAVE_CYANIGOSA  = 13,\r\n};\r\n\r\nenum VHData'
assert s.count(old2) == 1
s = s.replace(old2, new2)
open(p, 'w', encoding='utf-8', newline='').write(s)
print('fixed')
