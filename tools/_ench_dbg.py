import struct
p = r"C:\Users\jeremy\AppData\Local\Temp\SpellItemEnchantment.dbc"
import os
# find the DBC on disk
candidates = [
    r"C:\Users\jeremy\AppData\Local\Temp\SpellItemEnchantment.dbc",
]
# pull from container? no python inside. Read via docker cp already done for Spell.dbc.
# Extract Effect/Arg columns for suffix enchantment ids from the server DBC copy.
import subprocess
if not os.path.exists(candidates[0]):
    subprocess.run(["docker", "cp", "ac-worldserver:/azerothcore/env/dist/data/dbc/SpellItemEnchantment.dbc",
                    r"C:\Users\jeremy\AppData\Local\Temp\SpellItemEnchantment.dbc"], check=True)
data = open(candidates[0], 'rb').read()
magic, recCount, fieldCount, recSize, strSize = struct.unpack_from("<4s4I", data, 0)
recs = data[20:20 + recCount*recSize]
def field(rec, w):
    return struct.unpack_from("<I", rec, w*4)[0]
# DBCStructure.h: SpellItemEnchantmentEntry has Charges? layout: Id, Charges,
# Effect[3], EffectPointsMin[3]... find by known id 2802 (crit suffix)
want = {2802, 2803, 2825, 2806, 2824, 983}
for i in range(recCount):
    rec = recs[i*recSize:(i+1)*recSize]
    sid = field(rec, 0)
    if sid in want:
        # print first 24 fields to identify layout
        print(sid, [field(rec, w) for w in range(0, 18)])
