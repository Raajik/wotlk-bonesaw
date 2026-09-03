def crlf(t):
    return t  # these files are LF

p = 'modules/mod-living-gear/src/LivingGear_Support.cpp'
s = open(p, 'r', encoding='utf-8', newline='').read()

# 1. globals
old = """// Bug report #3: Wintergrasp siege damage scales with how many people are
// actually there. WG_FULL_ROSTER is the population the stock building health
// is balanced around -- at or above it nothing changes at all. Below it,
// damage is divided by the shortfall, capped at WG_MAX_SIEGE_MULT.
//
// 20 and 10 together give exactly what was asked for: "if there's only a couple
// of players, make them do 10x normal damage" -- two players hit 20/2 = 10x.
bool g_wgSiegeScale = true;
uint32 g_wgFullRoster = 20;
float g_wgMaxSiegeMult = 10.0f;
"""
new = """// Bug reports #3 and #162: siege vehicle damage. #3 asked for a Wintergrasp
// attendance scale; #162 replaced that with a flat, permanent multiplier:
// "make the 10x damage bonus on siege vehicles game-wide (wintergrasp, strand
// of the ancients, isle of conquest, ulduar, etc) and apply permanently
// instead of being squad-size-based". g_wgVehicleMult is that multiplier --
// every hit from a siege vehicle (the vehicle itself or its rider) to a
// destructible wall or any other unit carries it, on every map.
bool g_wgSiegeScale = true;
float g_wgVehicleMult = 10.0f;

// A unit is siege-vehicle damage when it rides a vehicle (the player in the
// siege engine seat, casting its spells) or is itself a vehicle (the siege
// engine creature firing its own AI or accessory spells). Ordinary mounts are
// not vehicles -- they never have a kit and the rider's GetVehicle() is null.
bool IsSiegeVehicleDamage(Unit const* unit)
{
    return unit && (unit->GetVehicle() || unit->GetVehicleKit());
}
"""
n = s.count(old)
assert n == 1, f'globals anchor: {n}'
s = s.replace(old, new)

# 2. GO hook: flat game-wide multiplier
old2 = """// Bug report #3, 2026-08-22: "make wintergrasp siege damage scale with the
// number of players -- if there's only a couple of players, make them do 10x
// normal damage."
//
// Wintergrasp's walls and towers are destructible GameObjects, so their damage
// does not go through any of the Unit damage hooks -- it arrives here, at
// GameObject::ModifyHealth. `change` is negative for damage and positive for
// repair; only damage is touched, so repairing is unaffected.
//
// Scoped to the Wintergrasp area, and counts only players actually in that
// area rather than everyone on the Northrend map, which would otherwise let
// half of Dalaran suppress the multiplier without ever setting foot in the
// battle.
class SupportWintergrasp : public AllGameObjectScript
{
public:
    SupportWintergrasp() : AllGameObjectScript("LivingGearSupportWintergrasp") { }

    void OnGameObjectModifyHealth(GameObject* go, Unit* attackerOrHealer, int32& change,
        SpellInfo const* /*spellInfo*/) override
    {
        if (!g_wgSiegeScale || !go || change >= 0 || !attackerOrHealer)
            return;
        if (go->GetAreaId() != AREA_WINTERGRASP && go->GetZoneId() != AREA_WINTERGRASP)
            return;
        Map* map = go->GetMap();
        if (!map)
            return;

        uint32 present = 0;
        for (auto const& pair : map->GetPlayers())
            if (Player* p = pair.GetSource())
                if (p->IsInWorld() && (p->GetZoneId() == AREA_WINTERGRASP || p->GetAreaId() == AREA_WINTERGRASP))
                    ++present;

        if (present >= g_wgFullRoster)
            return;
        float mult = float(g_wgFullRoster) / float(std::max<uint32>(present, 1));
        if (mult > g_wgMaxSiegeMult)
            mult = g_wgMaxSiegeMult;
        if (mult <= 1.0f)
            return;

        double const scaled = double(change) * double(mult);
        change = scaled <= double(std::numeric_limits<int32>::min())
            ? std::numeric_limits<int32>::min() : int32(scaled);
    }
};
"""
new2 = """// Bug report #3, 2026-08-22: "make wintergrasp siege damage scale with the
// number of players -- if there's only a couple of players, make them do 10x
// normal damage." Report #162 then replaced the attendance scale: the 10x is
// now flat and permanent, and it follows the vehicle to every map -- the
// destructible gates of Strand of the Ancients and Isle of Conquest, Ulduar's
// towers, not just the Wintergrasp keep.
//
// Wintergrasp's walls and towers are destructible GameObjects, so their damage
// does not go through any of the Unit damage hooks -- it arrives here, at
// GameObject::ModifyHealth. `change` is negative for damage and positive for
// repair; only damage is touched, so repairing is unaffected.
class SupportWintergrasp : public AllGameObjectScript
{
public:
    SupportWintergrasp() : AllGameObjectScript("LivingGearSupportWintergrasp") { }

    void OnGameObjectModifyHealth(GameObject* go, Unit* attackerOrHealer, int32& change,
        SpellInfo const* /*spellInfo*/) override
    {
        if (!g_wgSiegeScale || !go || change >= 0 || !attackerOrHealer)
            return;
        if (!IsSiegeVehicleDamage(attackerOrHealer))
            return ansy;
        double const scaled = double(change) * double(g_wgVehicleMult);
        change = scaled <= double(std::numeric_limits<int32>::min())
            ? std::numeric_limits<int32>::min() : int32(scaled);
    }
};

// The other half of #162: the same flat multiplier on a siege vehicle's
// damage to units, so the Ulduar vehicle fights and Wintergrasp's defenders
// fall at siege pace too. OnDamage is the one funnel every direct damage
// number passes through (melee, spell hits and periodic ticks alike), so one
// hook covers cannon fire, boulders and ram hits; the GO hook above is the
// separate path for destructible buildings.
class SupportVehicles : public UnitScript
{
public:
    SupportVehicles() : UnitScript("LivingGearSupportVehicles", true, { UNITHOOK_ON_DAMAGE }) { }

    void OnDamage(Unit* attacker, Unit* /*victim*/, uint32& damage) override
    {
        if (!g_wgSiegeScale || !damage || !IsSiegeVehicleDamage(attacker))
            return;
        uint64 const scaled = uint64(damage) * uint64(g_wgVehicleMult);
        damage = scaled > uint64(std::numeric_limits<uint32>::max())
            ? std::numeric_limits<uint32>::max() : uint32(scaled);
    }
};
"""
n2 = s.count(old2)
assert n2 == 1, f'go hook anchor: {n2}'
s = s.replace(old2, new2)
# fix deliberate typo marker
s = s.replace('return ansy;', 'return;')

# 3. registration + config read
old3 = """    new LivingGearSupport::SupportPlayer();
    new LivingGearSupport::SupportWintergrasp();
"""
new3 = """    LivingGearSupport::g_wgVehicleMult =
        sConfigMgr->GetOption<float>("LivingGear.Wintergrasp.VehicleDamageMult", 10.0f);

    new LivingGearSupport::SupportPlayer();
    new LivingGearSupport::SupportWintergrasp();
    new LivingGearSupport::SupportVehicles();
"""
n3 = s.count(old3)
assert n3 == 1, f'registration anchor: {n3}'
s = s.replace(old3, new3)

# drop the retired config reads
old4 = """    LivingGearSupport::g_wgSiegeScale =
        sConfigMgr->GetOption<bool>("LivingGear.Wintergrasp.SiegeScale", true);
    LivingGearSupport::g_wgFullRoster =
        std::max<uint32>(1, sConfigMgr->GetOption<uint32>("LivingGear.Wintergrasp.FullRoster", 20));
    LivingGearSupport::g_wgMaxSiegeMult =
        sConfigMgr->GetOption<float>("LivingGear.Wintergrasp.MaxSiegeMult", 10.0f);
"""
new4 = """    LivingGearSupport::g_wgSiegeScale =
        sConfigMgr->GetOption<bool>("LivingGear.Wintergrasp.SiegeScale", true);
"""
n4 = s.count(old4)
assert n4 == 1, f'config read anchor: {n4}'
s = s.replace(old4, new4)

open(p, 'w', encoding='utf-8', newline='').write(s)

# 4. conf.dist
pc = 'modules/mod-living-gear/conf/living_gear.conf.dist'
sc = open(pc, 'r', encoding='utf-8', newline='').read()
oldc = """# Wintergrasp siege damage scales up when the zone is underpopulated
# (bug report #3). At or above FullRoster nothing changes; below it, damage is
# multiplied by FullRoster/present, capped at MaxSiegeMult.
LivingGear.Wintergrasp.SiegeScale = 1
LivingGear.Wintergrasp.FullRoster = 20
LivingGear.Wintergrasp.MaxSiegeMult = 10.0
"""
newc = """# Siege vehicle damage (bug reports #3 and #162). Every hit a siege vehicle
# (or its rider) lands -- on destructible walls and towers, and on anything
# else -- is multiplied by VehicleDamageMult on every map: Wintergrasp,
# Strand of the Ancients, Isle of Conquest, the Ulduar vehicle fights. Flat
# and permanent; SiegeScale = 0 turns the whole thing off.
LivingGear.Wintergrasp.SiegeScale = 1
LivingGear.Wintergrasp.VehicleDamageMult = 10.0
"""
nc = sc.count(oldc)
assert nc == 1, f'conf anchor: {nc}'
sc = sc.replace(oldc, newc)
open(pc, 'w', encoding='utf-8', newline='').write(sc)
print('ALL #162 EDITS APPLIED')
