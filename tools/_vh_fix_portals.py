import sys
p = 'src/server/scripts/Northrend/VioletHold/instance_violet_hold.cpp'
s = open(p, 'r', encoding='utf-8', newline='').read()
old = """                            _events.RescheduleEvent(EVENT_CYANIGOSA_TRANSFORM, 5s);
                        }
                    }
                    break;""".replace("\n", "\r\n")
new = """                            _events.RescheduleEvent(EVENT_CYANIGOSA_TRANSFORM, 5s);
                        }
                        else if (_waveCount % 2 == 0)
                            sinclari->SummonCreature(NPC_TELEPORTATION_PORTAL, MiddleRoomPortalSaboLocation, TEMPSUMMON_CORPSE_DESPAWN);
                        else
                            sinclari->SummonCreature(NPC_TELEPORTATION_PORTAL, PortalLocations[GetData(DATA_PORTAL_LOCATION)], TEMPSUMMON_CORPSE_DESPAWN);
                    }
                    break;""".replace("\n", "\r\n")
assert s.count(old) == 1, f"found {s.count(old)}"
open(p, 'w', encoding='utf-8', newline='').write(s.replace(old, new))
print('patched')
