#!/usr/bin/env python3
"""Replace corrupted LgConfig struct in LivingGear.cpp with clean version."""
from __future__ import annotations

import re
from pathlib import Path

TARGET = Path(__file__).resolve().parents[1] / "modules/mod-living-gear/src/LivingGear.cpp"

CLEAN_CONFIG = r"""struct LgConfig
{
    bool enabled = true;
    uint32 xpPerKill = 1;
    uint32 xpEliteKill = 3;
    uint32 xpBossKill = 10;
    uint16 maxLevel = 50;
    float growthPerLevel = 0.10f;
    float absorbPct = 0.10f;
    float rollChance = 25.0f;
    uint8 rollStatCount = 1;
    bool learnMailSpell = false;
    bool learnAuctionSpell = false;
    bool learnTrainerSpell = false;
    bool learnBankSpell = false;
    bool learnStableSpell = false;
    bool learnBindSpell = false;
    bool learnAutolootSpell = false;
    bool learnFlightSpell = false;
    bool disableDurability = true;
    bool instantFlight = true;
    bool instantMount = true;
    bool uniformMountSpeed = true;
    bool autolearnClassSpells = true;
    uint32 autolootCorpses = 10;
    float autolootRadius = 10.0f;
    float autolootKillRadius = 45.0f;
    bool groupKillXp = true;
    bool botAutoAttune = true;
    uint32 botAutoAttuneDelay = 1000;
    bool pvpRealm = true;
    bool worldFfa = true;
    bool jumpMode = false;
    bool uiScale = false;
    bool sharedGold = false;
    bool taxiMask = false;

    // GW2-style open-world zone scaling (combat down, reward up).
    bool zoneScaleEnabled = true;
    uint8 zoneScaleCombatBuffer = 3;
    uint8 zoneScaleMinPlayerLevel = 10;
    float zoneScaleRewardFloor = 0.35f;
    float zoneScaleRewardGapDecay = 12.0f;
    float zoneScaleIncomingPerLevel = 0.12f;
    bool zoneScaleNotify = true;

    void Load()
    {
        enabled = sConfigMgr->GetOption<bool>("LivingGear.Enable", true);
        xpPerKill = sConfigMgr->GetOption<uint32>("LivingGear.XpPerKill", 1);
        xpEliteKill = sConfigMgr->GetOption<uint32>("LivingGear.XpEliteKill", 3);
        xpBossKill = sConfigMgr->GetOption<uint32>("LivingGear.XpBossKill", 10);
        maxLevel = static_cast<uint16>(sConfigMgr->GetOption<uint32>("LivingGear.MaxLevel", 50));
        growthPerLevel = sConfigMgr->GetOption<float>("LivingGear.GrowthPerLevel", 0.10f);
        absorbPct = sConfigMgr->GetOption<float>("LivingGear.AbsorbPct", 0.10f);
        rollChance = sConfigMgr->GetOption<float>("LivingGear.RollChance", 25.0f);
        rollStatCount = static_cast<uint8>(sConfigMgr->GetOption<uint32>("LivingGear.RollStatCount", 1));
        if (rollStatCount < 1)
            rollStatCount = 1;
        if (rollStatCount > 5)
            rollStatCount = 5;
        learnMailSpell = sConfigMgr->GetOption<bool>("LivingGear.LearnMailSpell", false);
        learnAuctionSpell = sConfigMgr->GetOption<bool>("LivingGear.LearnAuctionSpell", false);
        learnTrainerSpell = sConfigMgr->GetOption<bool>("LivingGear.LearnTrainerSpell", false);
        learnBankSpell = sConfigMgr->GetOption<bool>("LivingGear.LearnBankSpell", false);
        learnStableSpell = sConfigMgr->GetOption<bool>("LivingGear.LearnStableSpell", false);
        learnBindSpell = sConfigMgr->GetOption<bool>("LivingGear.LearnBindSpell", false);
        learnAutolootSpell = sConfigMgr->GetOption<bool>("LivingGear.LearnAutolootSpell", false);
        learnFlightSpell = sConfigMgr->GetOption<bool>("LivingGear.LearnFlightSpell", false);
        disableDurability = sConfigMgr->GetOption<bool>("LivingGear.DisableDurability", true);
        instantFlight = sConfigMgr->GetOption<bool>("LivingGear.InstantFlight", true);
        instantMount = sConfigMgr->GetOption<bool>("LivingGear.InstantMount", true);
        uniformMountSpeed = sConfigMgr->GetOption<bool>("LivingGear.UniformMountSpeed", true);
        autolearnClassSpells = sConfigMgr->GetOption<bool>("LivingGear.AutolearnClassSpells", true);
        autolootCorpses = sConfigMgr->GetOption<uint32>("LivingGear.AutolootCorpses", 10);
        if (autolootCorpses < 1)
            autolootCorpses = 1;
        autolootRadius = sConfigMgr->GetOption<float>("LivingGear.AutolootRadius", 10.0f);
        autolootKillRadius = sConfigMgr->GetOption<float>("LivingGear.AutolootKillRadius", 45.0f);
        if (autolootRadius < 1.0f)
            autolootRadius = 1.0f;
        if (autolootKillRadius < autolootRadius)
            autolootKillRadius = autolootRadius;
        groupKillXp = sConfigMgr->GetOption<bool>("LivingGear.GroupKillXp", true);
        botAutoAttune = sConfigMgr->GetOption<bool>("LivingGear.BotAutoAttune", true);
        botAutoAttuneDelay = sConfigMgr->GetOption<uint32>("LivingGear.BotAutoAttuneDelay", 1000);
        if (botAutoAttuneDelay < 200)
            botAutoAttuneDelay = 200;
        pvpRealm = sConfigMgr->GetOption<bool>("LivingGear.PvpRealm", true);
        worldFfa = sConfigMgr->GetOption<bool>("LivingGear.WorldFfa", true);
        zoneScaleEnabled = sConfigMgr->GetOption<bool>("LivingGear.ZoneScale.Enable", true);
        zoneScaleCombatBuffer = static_cast<uint8>(sConfigMgr->GetOption<uint32>("LivingGear.ZoneScale.CombatBuffer", 3));
        if (zoneScaleCombatBuffer > 10)
            zoneScaleCombatBuffer = 10;
        zoneScaleMinPlayerLevel = static_cast<uint8>(sConfigMgr->GetOption<uint32>("LivingGear.ZoneScale.MinPlayerLevel", 10));
        zoneScaleRewardFloor = sConfigMgr->GetOption<float>("LivingGear.ZoneScale.RewardFloor", 0.35f);
        if (zoneScaleRewardFloor < 0.05f)
            zoneScaleRewardFloor = 0.05f;
        if (zoneScaleRewardFloor > 1.0f)
            zoneScaleRewardFloor = 1.0f;
        zoneScaleRewardGapDecay = sConfigMgr->GetOption<float>("LivingGear.ZoneScale.RewardGapDecay", 12.0f);
        if (zoneScaleRewardGapDecay < 1.0f)
            zoneScaleRewardGapDecay = 1.0f;
        zoneScaleIncomingPerLevel = sConfigMgr->GetOption<float>("LivingGear.ZoneScale.IncomingPerLevel", 0.12f);
        if (zoneScaleIncomingPerLevel < 0.0f)
            zoneScaleIncomingPerLevel = 0.0f;
        if (zoneScaleIncomingPerLevel > 1.0f)
            zoneScaleIncomingPerLevel = 1.0f;
        zoneScaleNotify = sConfigMgr->GetOption<bool>("LivingGear.ZoneScale.Notify", true);
    }
};
"""


def main() -> int:
    text = TARGET.read_text(encoding="utf-8")
    before = len(text.splitlines())
    pattern = re.compile(r"struct LgConfig\s*\{.*?\n\};\n\nLgConfig g_cfg;", re.DOTALL)
    m = pattern.search(text)
    if not m:
        print("LgConfig block not found")
        return 1
    text = text[: m.start()] + CLEAN_CONFIG + "\n\nLgConfig g_cfg;" + text[m.end() :]
    after = len(text.splitlines())
    TARGET.write_text(text, encoding="utf-8", newline="\n")
    print(f"Fixed LgConfig: {before} -> {after} lines")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
