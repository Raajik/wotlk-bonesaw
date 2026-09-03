from pathlib import Path

p = Path(r"A:/wow-bonesaw/modules/mod-living-gear/src/LivingGear.cpp")
t = p.read_text(encoding="utf-8")
changed = False

if "uint32 const SPELL_CRAFT_2" not in t:
    old = "uint32 const SPELL_CRAFT_1 = 910093;\nuint32 const SPELL_CRAFT_5 = 910097;"
    new = (
        "uint32 const SPELL_CRAFT_1 = 910093;\n"
        "uint32 const SPELL_CRAFT_2 = 910094;\n"
        "uint32 const SPELL_CRAFT_3 = 910095;\n"
        "uint32 const SPELL_CRAFT_4 = 910096;\n"
        "uint32 const SPELL_CRAFT_5 = 910097;"
    )
    if old not in t:
        raise SystemExit("SPELL_CRAFT_1/5 block not found")
    t = t.replace(old, new, 1)
    changed = True
    print("added SPELL_CRAFT_2-4")

if "bool IsCraftingSpell(SpellInfo const* info);" not in t:
    old = "bool IsFishingSpell(uint32 spellId);\n"
    new = (
        "bool IsFishingSpell(uint32 spellId);\n"
        "bool IsCraftingSpell(SpellInfo const* info);\n"
        "float CraftTimeMult(Player* player);\n"
    )
    if old not in t:
        raise SystemExit("IsFishingSpell decl not found")
    t = t.replace(old, new, 1)
    changed = True
    print("added decls")

needle = (
    "bool IsFishingSpell(uint32 spellId)\n"
    "{\n"
    "    return spellId == SPELL_FISHING || spellId == 7731 || spellId == 7732 || spellId == 18248;\n"
    "}\n"
)
if "bool IsCraftingSpell(SpellInfo const* info)\n{" not in t:
    insert = needle + """
bool IsCraftingProfessionSkill(uint32 skillId)
{
    switch (skillId)
    {
        case SKILL_ALCHEMY:
        case SKILL_BLACKSMITHING:
        case SKILL_ENCHANTING:
        case SKILL_ENGINEERING:
        case SKILL_INSCRIPTION:
        case SKILL_JEWELCRAFTING:
        case SKILL_LEATHERWORKING:
        case SKILL_TAILORING:
        case SKILL_COOKING:
            return true;
        default:
            return false;
    }
}

bool IsCraftingSpell(SpellInfo const* info)
{
    if (!info)
        return false;
    SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(info->Id);
    for (SkillLineAbilityMap::const_iterator itr = bounds.first; itr != bounds.second; ++itr)
        if (itr->second && IsCraftingProfessionSkill(itr->second->SkillLine))
            return true;
    return false;
}

uint32 const SPELL_CRAFT[] = { SPELL_CRAFT_1, SPELL_CRAFT_2, SPELL_CRAFT_3, SPELL_CRAFT_4, SPELL_CRAFT_5 };
uint32 const CRAFT_BREAKS[] = { 75, 150, 225, 300, 375 };

float CraftTimeMult(Player* player)
{
    if (!player || !player->GetSession())
        return 1.0f;
    uint32 const accountId = player->GetSession()->GetAccountId();
    float mult = 1.0f;
    for (uint32 spellId : SPELL_CRAFT)
        if (AccountHasPerk(accountId, spellId))
            mult *= 0.80f;
    return mult;
}

"""
    if needle not in t:
        raise SystemExit("IsFishingSpell body not found")
    t = t.replace(needle, insert, 1)
    changed = True
    print("added IsCraftingSpell/CraftTimeMult")

if "void CheckCraftPerks(Player* player)\n{" not in t:
    cook_end = (
        "        if (skill >= TRADE_BREAKS[i])\n"
        "            UnlockPerk(player, SPELL_COOK[i]);\n"
        "}\n\n"
        "uint32 CookingRegenPct(Player* player)\n"
    )
    craft = (
        "        if (skill >= TRADE_BREAKS[i])\n"
        "            UnlockPerk(player, SPELL_COOK[i]);\n"
        "}\n\n"
        "void CheckCraftPerks(Player* player)\n"
        "{\n"
        "    if (!player || !player->GetSession() || IsRandomAiBot(player))\n"
        "        return;\n"
        "    uint32 const accountId = player->GetSession()->GetAccountId();\n"
        "    LoadAccountPerks(accountId);\n"
        "    uint32 skill = 0;\n"
        "    uint32 const craftSkills[] = {\n"
        "        SKILL_ALCHEMY, SKILL_BLACKSMITHING, SKILL_ENCHANTING, SKILL_ENGINEERING,\n"
        "        SKILL_INSCRIPTION, SKILL_JEWELCRAFTING, SKILL_LEATHERWORKING, SKILL_TAILORING,\n"
        "        SKILL_COOKING\n"
        "    };\n"
        "    for (uint32 skillId : craftSkills)\n"
        "    {\n"
        "        if (player->HasSkill(skillId))\n"
        "            skill = std::max(skill, uint32(player->GetSkillValue(skillId)));\n"
        "        auto const it = g_accountSkills[accountId].find(skillId);\n"
        "        if (it != g_accountSkills[accountId].end() && it->second.value > skill)\n"
        "            skill = it->second.value;\n"
        "    }\n"
        "    if (!skill)\n"
        "        return;\n"
        "    for (uint32 i = 0; i < 5; ++i)\n"
        "        if (skill >= CRAFT_BREAKS[i])\n"
        "            UnlockPerk(player, SPELL_CRAFT[i]);\n"
        "}\n\n"
        "uint32 CookingRegenPct(Player* player)\n"
    )
    if cook_end not in t:
        raise SystemExit("CheckCookingPerks end not found")
    t = t.replace(cook_end, craft, 1)
    changed = True
    print("added CheckCraftPerks body")

if changed:
    p.write_text(t, encoding="utf-8", newline="\n")
    print("wrote", t.count("\n"), "newlines", "bytes", len(t.encode("utf-8")))
else:
    print("no change needed", "nl", t.count("\n"))
