# Verify the suffix-stat math end to end for Zil's Tumultuous Necklace
# (entry 51996, randomPropertyId -71, suffixFactor stored on the item).
#
# - ItemRandomSuffix 71 = "of the Bandit"? Look up enchantments + allocation.
# - suffixFactor comes from RandomPropertiesPoints at the item's ilvl.
import struct

# 1) suffix 71's enchants + allocation from ItemRandomSuffix DBC
p = r"C:\Users\jeremy\AppData\Local\Temp\ItemRandomSuffix.dbc"
import subprocess, os
if not os.path.exists(p):
    subprocess.run(["docker", "cp", "ac-worldserver:/azerothcore/env/dist/data/dbc/ItemRandomSuffix.dbc", p], check=True)
data = open(p, 'rb').read()
magic, recCount, fieldCount, recSize, strSize = struct.unpack_from("<4s4I", data, 0)
recs = data[20:20 + recCount*recSize]
def f(rec, w): return struct.unpack_from("<i", rec, w*4)[0]
def u(rec, w): return struct.unpack_from("<I", rec, w*4)[0]
# DBCStructure.h RandomPropertiesPointsEntry: ID, enchantment[3]... for suffix:
# ItemRandomSuffixEntry: ID, name, enchantment[3](cols 4-6?), AllocationPct[3](cols 7-9?) - verify by scan
for i in range(recCount):
    rec = recs[i*recSize:(i+1)*recSize]
    if u(rec, 0) == 71:
        print("suffix 71 raw fields 0-12:", [f(rec, w) for w in range(0, 13)])
        break
