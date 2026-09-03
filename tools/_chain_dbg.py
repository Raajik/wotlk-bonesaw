import struct
p = r"C:\Users\jeremy\AppData\Local\Temp\Spell.dbc"
data = open(p, 'rb').read()
magic, recCount, fieldCount, recSize, strSize = struct.unpack_from("<4s4I", data, 0)
recs = data[20:20 + recCount*recSize]
def field(rec, w):
    return struct.unpack_from("<I", rec, w*4)[0]
# EffectImplicitTargetA = 1-based 86-88 -> cols 85-87; ChainTarget 1-based 104-106 -> cols 103-105
for want in (49238, 49239, 49240, 421, 1064, 49271):
    for i in range(recCount):
        rec = recs[i*recSize:(i+1)*recSize]
        if field(rec, 0) == want:
            print(want, "ImplicitA:", [field(rec, w) for w in (85, 86, 87)],
                  "Chain:", [field(rec, w) for w in (103, 104, 105)])
            break
    else:
        print(want, "not in DBC")
