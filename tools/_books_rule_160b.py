p = 'modules/mod-living-gear/src/LivingGear_Vault.cpp'
s = open(p, 'r', encoding='utf-8', newline='').read()
old = """    // Report #160: obsolete class-spell books vendor automatically. An
    // explicit autoloot rule still wins -- ResolveLootAction only falls
    // through to this function when no stored rule matched, so a collector
    // keeps theirs with a matching rule. Books with no sell price are left
    // alone here and destroyed only by the player's own rules.
    if (proto->Class == ITEM_CLASS_RECIPE && proto->SubClass == ITEM_SUBCLASS_BOOK && IsSpellBookName(proto->Name1))
        return ACT_VENDOR;
"""
new = """    // Report #160: obsolete class-spell books vendor automatically. An
    // explicit autoloot rule still wins -- ResolveLootAction only falls
    // through to this function when no stored rule matched, so a collector
    // keeps theirs with a matching rule. A book with no sell price would be
    // destroyed with no coin by the vendor path (the #31 conjured-food trap),
    // so those stay in the bag instead.
    if (proto->Class == ITEM_CLASS_RECIPE && proto->SubClass == ITEM_SUBCLASS_BOOK && IsSpellBookName(proto->Name1))
        return proto->SellPrice ? ACT_VENDOR : ACT_BAG;
"""
assert s.count(old) == 1
s = s.replace(old, new)
open(p, 'w', encoding='utf-8', newline='').write(s)
print('PATCHED')
