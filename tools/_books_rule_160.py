p = 'modules/mod-living-gear/src/LivingGear_Vault.cpp'
s = open(p, 'r', encoding='utf-8', newline='').read()

old1 = """    return proto->TotemCategory != 0 && proto->Class == ITEM_CLASS_TRADE_GOODS;
}
"""
new1 = """    return proto->TotemCategory != 0 && proto->Class == ITEM_CLASS_TRADE_GOODS;
}

// Report #160: the classic spell-teaching books -- [Book: Gift of the Wild
// II], [Tome of Frost Nova], [Codex of Renew], [Tablet of Lightning Shield],
// [Libram: Divine Favor], [Grimoire of ...] -- are class RECIPE / subclass
// BOOK, and on a realm where class spells auto-train they are pure vendor
// trash. Matched by first word so profession recipes (also class RECIPE, but
// a real subclass like Cooking or Enchanting) and teaching journals that
// matter ([Weather-Beaten Journal]) are untouched.
bool IsSpellBookName(std::string const& name)
{
    static constexpr std::string_view prefixes[] = { "Book", "Tome", "Tablet", "Codex", "Libram", "Grimoire" };
    for (std::string_view prefix : prefixes)
        if (name.rfind(prefix, 0) == 0)
        {
            char const next = name.size() > prefix.size() ? name[prefix.size()] : '\\0';
            return next == ' ' || next == ':';
        }
    return false;
}
"""
n1 = s.count(old1)
assert n1 == 1, f'helper anchor: {n1}'
s = s.replace(old1, new1)

old2 = """    if (proto->Quality == ITEM_QUALITY_POOR)
        return ACT_VENDOR;
"""
new2 = """    if (proto->Quality == ITEM_QUALITY_POOR)
        return ACT_VENDOR;
    // Report #160: obsolete class-spell books vendor automatically. An
    // explicit autoloot rule still wins -- ResolveLootAction only falls
    // through to this function when no stored rule matched, so a collector
    // keeps theirs with a matching rule. Books with no sell price are left
    // alone here and destroyed only by the player's own rules.
    if (proto->Class == ITEM_CLASS_RECIPE && proto->SubClass == ITEM_SUBCLASS_BOOK && IsSpellBookName(proto->Name1))
        return ACT_VENDOR;
"""
n2 = s.count(old2)
assert n2 == 1, f'action anchor: {n2}'
s = s.replace(old2, new2)

open(p, 'w', encoding='utf-8', newline='').write(s)
print('PATCHED')
