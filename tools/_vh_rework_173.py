# One-shot Violet Hold rework for report #173.
# Files are CRLF; every replacement is asserted to hit exactly once.
import sys

def E(old, new):
    return (old, new)

FILE_H = "src/server/scripts/Northrend/VioletHold/violet_hold.h"
FILE_I = "src/server/scripts/Northrend/VioletHold/instance_violet_hold.cpp"
FILE_V = "src/server/scripts/Northrend/VioletHold/violet_hold.cpp"

EDITS = {
    FILE_H: [
        # 1. new data id
        E(
            """    // Manual GUID tracking (multi-instance entries)
    DATA_EREKEM_GUARD_1_GUID,
    DATA_EREKEM_GUARD_2_GUID,
};
""",
            """    // Manual GUID tracking (multi-instance entries)
    DATA_EREKEM_GUARD_1_GUID,
    DATA_EREKEM_GUARD_2_GUID,

    // Computed: which of the six prison bosses releases on the current wave
    // (boss waves are the even ones, 2..12; 0 when the wave has no boss)
    DATA_BOSS_FOR_CURRENT_WAVE      = 38,
};
""",
        ),
        # 2. persistent data slots for the full boss order
        E(
            """enum VHPersistentData
{
    PERSISTENT_DATA_FIRST_BOSS,
    PERSISTENT_DATA_SECOND_BOSS,
    PERSISTENT_DATA_COUNT
};
""",
            """enum VHPersistentData
{
    PERSISTENT_DATA_FIRST_BOSS,     // legacy (pre-rework runs stored 2 bosses)
    PERSISTENT_DATA_SECOND_BOSS,    // legacy
    PERSISTENT_DATA_BOSS_ORDER_0,   // release order: one slot per boss wave,
    PERSISTENT_DATA_BOSS_ORDER_1,   // wave 2 -> slot 0, wave 4 -> slot 1,
    PERSISTENT_DATA_BOSS_ORDER_2,   // ... wave 12 -> slot 5 (report #173)
    PERSISTENT_DATA_BOSS_ORDER_3,
    PERSISTENT_DATA_BOSS_ORDER_4,
    PERSISTENT_DATA_BOSS_ORDER_5,
    PERSISTENT_DATA_COUNT
};
""",
        ),
    ],
    FILE_I: [
        # 3. boss DONE: mark legacy slots + short wait
        E(
            """                case BOSS_ZURAMAT:
                    if (state == DONE)
                    {
                        if (_waveCount == 6)
                            SetBossState(DATA_1ST_BOSS, DONE);
                        else if (_waveCount == 12)
                            SetBossState(DATA_2ND_BOSS, DONE);
                        _events.RescheduleEvent(EVENT_SUMMON_PORTAL, 35s);
                    }
""",
            """                case BOSS_ZURAMAT:
                    if (state == DONE)
                    {
                        // Report #173: every prison boss is its own encounter
                        // now. The first two killed in a run still fill the
                        // legacy slots so loot gating and old saves work.
                        if (GetBossState(DATA_1ST_BOSS) != DONE)
                            SetBossState(DATA_1ST_BOSS, DONE);
                        else if (GetBossState(DATA_2ND_BOSS) != DONE)
                            SetBossState(DATA_2ND_BOSS, DONE);
                        // 35s of silence after every boss was the worst part
                        // of the old pace; the next portal opens right away.
                        _events.RescheduleEvent(EVENT_SUMMON_PORTAL, 6s);
                    }
""",
        ),
        # 4. ACTION_RELEASE_BOSS uses the computed boss for this wave
        E(
            """                case ACTION_RELEASE_BOSS:
                    if (_waveCount == 6)
                        StartBossEncounter(GetPersistentData(PERSISTENT_DATA_FIRST_BOSS));
                    else
                        StartBossEncounter(GetPersistentData(PERSISTENT_DATA_SECOND_BOSS));
                    break;
""",
            """                case ACTION_RELEASE_BOSS:
                    if (uint32 bossId = BossForCurrentWave())
                        StartBossEncounter(bossId);
                    break;
""",
        ),
        # 5. GetData: expose the current wave's boss
        E(
            """                case DATA_PORTAL_LOCATION:
                    return _portalLocation;
            }

            return 0;
        }
""",
            """                case DATA_PORTAL_LOCATION:
                    return _portalLocation;
                case DATA_BOSS_FOR_CURRENT_WAVE:
                    return BossForCurrentWave();
            }

            return 0;
        }
""",
        ),
        # 6. intro RP chain timings
        E(
            """                        _events.RescheduleEvent(EVENT_GUARDS_FALL_BACK, 4s);
                        _events.RescheduleEvent(EVENT_CHECK_PLAYERS, 5s);
""",
            """                        // Report #173: the guards-march-out chain stacks
                        // four scheduled waits; each is shortened so the first
                        // portal opens ~9s after the player starts the event.
                        _events.RescheduleEvent(EVENT_GUARDS_FALL_BACK, 2s);
                        _events.RescheduleEvent(EVENT_CHECK_PLAYERS, 5s);
""",
        ),
        E(
            """                    _events.RescheduleEvent(EVENT_GUARDS_DISAPPEAR, 5s);
                    break;
""",
            """                    _events.RescheduleEvent(EVENT_GUARDS_DISAPPEAR, 2s);
                    break;
""",
        ),
        E(
            """                    _events.RescheduleEvent(EVENT_SINCLARI_FALL_BACK, 2s);
                    break;
""",
            """                    _events.RescheduleEvent(EVENT_SINCLARI_FALL_BACK, 1s);
                    break;
""",
        ),
        E(
            """                    SummonDefenseSystem();
                    _events.RescheduleEvent(EVENT_START_ENCOUNTER, 4s);
""",
            """                    SummonDefenseSystem();
                    _events.RescheduleEvent(EVENT_START_ENCOUNTER, 2s);
""",
        ),
        E(
            """                    _events.RescheduleEvent(EVENT_SUMMON_PORTAL, 4s);
                    break;
""",
            """                    _events.RescheduleEvent(EVENT_SUMMON_PORTAL, 2s);
                    break;
""",
        ),
        # 7. EVENT_SUMMON_PORTAL: trash odd / boss even / Cyanigosa 13
        E(
            """                    SetData(DATA_PORTAL_LOCATION, (GetData(DATA_PORTAL_LOCATION) + urand(1, 5)) % 6);
                    if (Creature* sinclari = GetCreature(DATA_SINCLARI))
                    {
                        if (_waveCount % 6 != 0)
                            sinclari->SummonCreature(NPC_TELEPORTATION_PORTAL, PortalLocations[GetData(DATA_PORTAL_LOCATION)], TEMPSUMMON_CORPSE_DESPAWN);
                        else if (_waveCount == 6 || _waveCount == 12)
                        {
                            if (!GetPersistentData(PERSISTENT_DATA_FIRST_BOSS) || !GetPersistentData(PERSISTENT_DATA_SECOND_BOSS))
                            {
                                uint32 firstBoss = urand(BOSS_MORAGG, BOSS_ZURAMAT);
                                uint32 secondBoss;
                                do { secondBoss = urand(BOSS_MORAGG, BOSS_ZURAMAT); }
                                while (firstBoss == secondBoss);
                                StorePersistentData(PERSISTENT_DATA_FIRST_BOSS, firstBoss);
                                StorePersistentData(PERSISTENT_DATA_SECOND_BOSS, secondBoss);
                            }
                            sinclari->SummonCreature(NPC_TELEPORTATION_PORTAL, MiddleRoomPortalSaboLocation, TEMPSUMMON_CORPSE_DESPAWN);
                        }
                        else
""",
            """                    SetData(DATA_PORTAL_LOCATION, (GetData(DATA_PORTAL_LOCATION) + urand(1, 5)) % 6);
                    if (Creature* sinclari = GetCreature(DATA_SINCLARI))
                    {
                        // Report #173: one monster-group portal per boss
                        // release. Even waves 2..12 each open a boss portal (a
                        // shuffled order of all six prison bosses, so every
                        // boss of the visit gets released); wave 13 is
                        // Cyanigosa.
                        EnsureBossOrder();
                        if (_waveCount == VH_WAVE_CYANIGOSA)
""",
        ),
        # 8. Cyanigosa transforms sooner
        E(
            """                            _events.RescheduleEvent(EVENT_CYANIGOSA_TRANSFORM, 10s);
""",
            """                            _events.RescheduleEvent(EVENT_CYANIGOSA_TRANSFORM, 5s);
""",
        ),
        # 9. per-boss loot gate in StartBossEncounter
        E(
            """                if ((_waveCount == 6 && GetBossState(DATA_1ST_BOSS) == DONE) || (_waveCount == 12 && GetBossState(DATA_2ND_BOSS) == DONE))
                    boss->SetLootMode(0);
""",
            """                if (GetBossState(bossId) == DONE)
                    boss->SetLootMode(0);
""",
        ),
        # 10. cleanup wave restore from per-boss states
        E(
            """            if (GetBossState(DATA_2ND_BOSS) == DONE)
                _waveCount = 12;
            else if (GetBossState(DATA_1ST_BOSS) == DONE)
                _waveCount = 6;
            else
                _waveCount = 0;
""",
            """            // Report #173: resume after the last released boss. Killed
            // bosses * 2 lands the group on a trash wave, so a re-entering
            // group still gets its one monster-group portal before the next
            // boss portal opens (and a fresh run starts at wave 1).
            uint8 releasedBosses = 0;
            for (uint32 id = BOSS_MORAGG; id <= BOSS_ZURAMAT; ++id)
                if (GetBossState(id) == DONE)
                    ++releasedBosses;
            _waveCount = releasedBosses * 2;
""",
        ),
        # 11. helpers before the private members
        E(
            """    private:
        bool _cleaned{ false };
""",
            """        // Report #173: the release order of all six prison bosses, stored
        // in persistent data (survives save/load) the first time it is
        // needed. Old saves that recorded only two bosses keep them as the
        // first two releases so a dead boss is never fought again.
        void EnsureBossOrder()
        {
            if (GetPersistentData(PERSISTENT_DATA_BOSS_ORDER_0))
                return;

            uint32 pool[6] = { BOSS_MORAGG, BOSS_EREKEM, BOSS_ICHORON, BOSS_LAVANTHOR, BOSS_XEVOZZ, BOSS_ZURAMAT };
            for (int8 i = 5; i > 0; --i)
            {
                uint8 const j = uint8(urand(0, uint32(i)));
                uint32 const tmp = pool[i];
                pool[i] = pool[j];
                pool[j] = tmp;
            }

            uint32 const legacy[2] = { GetPersistentData(PERSISTENT_DATA_FIRST_BOSS), GetPersistentData(PERSISTENT_DATA_SECOND_BOSS) };
            for (uint8 slot = 0; slot < 2; ++slot)
                if (legacy[slot])
                    for (uint8 i = slot; i < 6; ++i)
                        if (pool[i] == legacy[slot])
                        {
                            uint32 const tmp = pool[slot];
                            pool[slot] = pool[i];
                            pool[i] = tmp;
                            break;
                        }

            for (uint8 i = 0; i < 6; ++i)
                StorePersistentData(PERSISTENT_DATA_BOSS_ORDER_0 + i, pool[i]);
        }

        uint32 BossForCurrentWave() const
        {
            if (_waveCount < VH_WAVE_FIRST_BOSS || _waveCount > VH_WAVE_LAST_BOSS || (_waveCount % 2) != 0)
                return 0;
            return GetPersistentData(PERSISTENT_DATA_BOSS_ORDER_0 + (_waveCount / 2 - 1));
        }

    private:
        bool _cleaned{ false };
""",
        ),
    ],
    FILE_V: [
        # 12. portal ctor: boss waves are even, first spawn sooner, bigger adds later
        E(
            """        if (_wave < 12)
            _addValue = 0;
        else
            _addValue = 1;

        if (_wave % 6 != 0)
            _events.RescheduleEvent(RAND(EVENT_SUMMON_KEEPER_OR_GUARDIAN, EVENT_SUMMON_ELITES), 10s);
        else
            _events.RescheduleEvent(EVENT_SUMMON_SABOTEOUR, 3s);
""",
            """        // Report #173: boss waves are the even ones (2..12) -- one trash
        // portal between releases. The back half still sends bigger groups.
        if (_wave >= 7)
            _addValue = 1;
        else
            _addValue = 0;

        if (_wave % 2 != 0)
            _events.RescheduleEvent(RAND(EVENT_SUMMON_KEEPER_OR_GUARDIAN, EVENT_SUMMON_ELITES), 5s);
        else
            _events.RescheduleEvent(EVENT_SUMMON_SABOTEOUR, 3s);
""",
        ),
        # 13. boss portals never call ACTION_PORTAL_DEFEATED
        E(
            """        if (_wave % 6 == 0)
            return;
        _instance->DoAction(ACTION_PORTAL_DEFEATED);
""",
            """        if (_wave % 2 == 0)
            return;
        _instance->DoAction(ACTION_PORTAL_DEFEATED);
""",
        ),
        # 14. saboteur reads the computed boss for this wave
        E(
            """        _boss = _instance->GetData(DATA_WAVE_COUNT) == 6
            ? _instance->GetPersistentData(PERSISTENT_DATA_FIRST_BOSS)
            : _instance->GetPersistentData(PERSISTENT_DATA_SECOND_BOSS);
""",
            """        _boss = _instance->GetData(DATA_BOSS_FOR_CURRENT_WAVE);
""",
        ),
        # 15. saboteur cracks the shield faster
        E(
            """                    if (_count < 3)
                        _events.RescheduleEvent(EVENT_SABOTEUR_SHIELD_DISRUPTION, 1s);
""",
            """                    if (_count < 3)
                        _events.RescheduleEvent(EVENT_SABOTEUR_SHIELD_DISRUPTION, 500ms);
""",
        ),
    ],
}

WAVE_CONSTS = """
enum VHWaves
{
    // Report #173 wave plan: odd waves are trash portals ("monster groups"),
    // even waves 2..12 each release one prison boss (six total), wave 13 is
    // Cyanigosa.
    VH_WAVE_FIRST_BOSS = 2,
    VH_WAVE_LAST_BOSS  = 12,
    VH_WAVE_CYANIGOSA  = 13,
};

"""

failed = False
for path, pairs in EDITS.items():
    with open(path, "r", encoding="utf-8", newline="") as f:
        text = f.read()
    for idx, (old, new) in enumerate(pairs):
        oldc = old.replace("\n", "\r\n")
        newc = new.replace("\n", "\r\n")
        n = text.count(oldc)
        if n != 1:
            print(f"FAIL {path} edit#{idx + 1}: found {n} occurrences")
            failed = True
            continue
        text = text.replace(oldc, newc)
    # header also gains the wave constants, right after the includes
    if path == FILE_H and not failed:
        text = text.replace(
            '#define VioletHoldScriptName "instance_violet_hold"\r\n',
            '#define VioletHoldScriptName "instance_violet_hold"\r\n\r\n' + WAVE_CONSTS.replace("\n", "\r\n"),
            1,
        )
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(text)
    print(f"OK {path}")

if failed:
    sys.exit(1)
print("ALL EDITS APPLIED")
