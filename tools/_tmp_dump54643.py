import struct
from pathlib import Path

data = Path(r"tools\client-patch\cache\Spell.dbc.base").read_bytes()
magic, records, fields, recsize, strsize = struct.unpack_from("<4sIIII", data, 0)
found = None
for i in range(records):
    rec = struct.unpack_from("<" + "I" * fields, data, 20 + i * recsize)
    if rec[0] == 54643:
        found = rec
        break
# spell.dbc field layout (3.3.5): EffectBasePoints 80-82, EffectMechanic 83-85,
# ImplicitTargetA 86-88, ImplicitTargetB 89-91, RadiusIndex 92-94, Aura 95-97,
# AuraPeriod 98-100, MiscValue 105-107, MiscValueB 108-110, TriggerSpell 111-113
for label, idxs in (
    ("EffectBasePoints", (80, 81, 82)),
    ("TargetA", (86, 87, 88)),
    ("TargetB", (89, 90, 91)),
    ("Aura", (95, 96, 97)),
    ("AuraPeriod", (98, 99, 100)),
    ("MiscValue", (105, 106, 107)),
    ("TriggerSpell", (111, 112, 113)),
    ("Duration", (40,)),
    ("CastingTime", (28,)),
    ("Range", (46,)),
):
    print(label, [found[i] for i in idxs])
