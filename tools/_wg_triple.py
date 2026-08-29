from pathlib import Path

# Header is already patched (consts present). Only the .cpp body needs the
# spawn loops replaced. Header assert removed.

h = Path('src/server/game/Battlefield/Zones/BattlefieldWG.h')
assert 'WG_ARMY_COPIES_PER_ROW' in h.read_text(), "header consts missing - patch header first"

p = Path('src/server/game/Battlefield/Zones/BattlefieldWG.cpp')
src = p.read_text()

old_keep = """    // Spawn NPCs in the defender's keep, both Horde and Alliance
    for (uint8 i = 0; i < WG_MAX_KEEP_NPC; i++)
    {
        // Horde npc
        if (Creature* creature = SpawnCreature(WGKeepNPC[i].entryHorde, WGKeepNPC[i].x, WGKeepNPC[i].y, WGKeepNPC[i].z, WGKeepNPC[i].o, TEAM_HORDE))
            KeepCreature[TEAM_HORDE].insert(creature->GetGUID());
        // Alliance npc
        if (Creature* creature = SpawnCreature(WGKeepNPC[i].entryAlliance, WGKeepNPC[i].x, WGKeepNPC[i].y, WGKeepNPC[i].z, WGKeepNPC[i].o, TEAM_ALLIANCE))
            KeepCreature[TEAM_ALLIANCE].insert(creature->GetGUID());
    }"""

new_keep = """    // Spawn NPCs in the defender's keep, both Horde and Alliance.
    // Report #169 (user clarified): triple the FACTION-ALIGNED BATTLE NPCs,
    // not the PvE wildlife. The keep/outside armies are spawned in code from
    // fixed tables, so the army size is a code constant, not a DB row count.
    // Each table row now spawns 3 copies in a small ring (4yd radius) instead
    // of a single npc, tripling both armies without touching the DB.
    for (uint8 i = 0; i < WG_MAX_KEEP_NPC; i++)
    {
        for (uint8 copy = 0; copy < WG_ARMY_COPIES_PER_ROW; ++copy)
        {
            float const dx = WG_ARMY_RING[copy][0];
            float const dy = WG_ARMY_RING[copy][1];
            // Horde npc
            if (Creature* creature = SpawnCreature(WGKeepNPC[i].entryHorde, WGKeepNPC[i].x + dx, WGKeepNPC[i].y + dy, WGKeepNPC[i].z, WGKeepNPC[i].o, TEAM_HORDE))
                KeepCreature[TEAM_HORDE].insert(creature->GetGUID());
            // Alliance npc
            if (Creature* creature = SpawnCreature(WGKeepNPC[i].entryAlliance, WGKeepNPC[i].x + dx, WGKeepNPC[i].y + dy, WGKeepNPC[i].z, WGKeepNPC[i].o, TEAM_ALLIANCE))
                KeepCreature[TEAM_ALLIANCE].insert(creature->GetGUID());
        }
    }"""

assert old_keep in src, "keep block not found"
src = src.replace(old_keep, new_keep)

old_out = """    // Spawn Horde NPCs outside the keep
    for (uint8 i = 0; i < WG_OUTSIDE_ALLIANCE_NPC; i++)
        if (Creature* creature = SpawnCreature(WGOutsideNPC[i].entryHorde, WGOutsideNPC[i].x, WGOutsideNPC[i].y, WGOutsideNPC[i].z, WGOutsideNPC[i].o, TEAM_HORDE))
            OutsideCreature[TEAM_HORDE].insert(creature->GetGUID());

    // Spawn Alliance NPCs outside the keep
    for (uint8 i = WG_OUTSIDE_ALLIANCE_NPC; i < WG_MAX_OUTSIDE_NPC; i++)
        if (Creature* creature = SpawnCreature(WGOutsideNPC[i].entryAlliance, WGOutsideNPC[i].x, WGOutsideNPC[i].y, WGOutsideNPC[i].z, WGOutsideNPC[i].o, TEAM_ALLIANCE))
            OutsideCreature[TEAM_ALLIANCE].insert(creature->GetGUID());"""

new_out = """    // Spawn Horde/Alliance NPCs outside the keep -- tripled like the keep
    // army above (report #169).
    for (uint8 i = 0; i < WG_MAX_OUTSIDE_NPC; i++)
    {
        TeamId const team = i < WG_OUTSIDE_ALLIANCE_NPC ? TEAM_HORDE : TEAM_ALLIANCE;
        uint32 const entry = team == TEAM_HORDE ? WGOutsideNPC[i].entryHorde
                                                : WGOutsideNPC[i].entryAlliance;
        for (uint8 copy = 0; copy < WG_ARMY_COPIES_PER_ROW; ++copy)
        {
            if (Creature* creature = SpawnCreature(entry,
                    WGOutsideNPC[i].x + WG_ARMY_RING[copy][0],
                    WGOutsideNPC[i].y + WG_ARMY_RING[copy][1],
                    WGOutsideNPC[i].z, WGOutsideNPC[i].o, team))
            {
                OutsideCreature[team].insert(creature->GetGUID());
            }
        }
    }"""

assert old_out in src, "outside block not found"
src = src.replace(old_out, new_out)
p.write_text(src)
print("cpp patched OK")
