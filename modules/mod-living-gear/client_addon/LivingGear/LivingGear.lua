-- Living Gear window. Loaded from patch-enUS-4.MPQ via FrameXML, or Interface/AddOns.
-- Server pushes numbers over addon whispers (prefix LG). ASCII-only strings.

local LG_UI_REV = 23

LivingGearDB = LivingGearDB or {}
if LivingGearDB.showChat == nil then
    LivingGearDB.showChat = false
end

-- FrameXML copy yields to Interface/AddOns/LivingGear so a stale MPQ cannot hide newer buttons.
local function LgLoadedFromAddon()
    local stack = debugstack and debugstack(1, 2, 0) or ""
    return string.find(stack, "AddOns", 1, true) ~= nil
end
if not LgLoadedFromAddon() and GetAddOnInfo then
    local ok, _, _, _, enabled = pcall(GetAddOnInfo, "LivingGear")
    if ok and enabled then
        return
    end
end
if LivingGear_Rev and LivingGear_Rev >= LG_UI_REV and not LgLoadedFromAddon() then
    return
end
LivingGear_Rev = LG_UI_REV
LivingGear_Loaded = true

local PREFIX = "LG"
local WHITE = "Interface\\Buttons\\WHITE8X8"
local ACCOUNT_PERKS_ID = 910001

local ACTION_NAMES = { "Bags", "Vendor", "Hold", "Reagent vault", "Skip", "Disenchant", "Learn" }
local DEFAULT_RULES = {
    { match = 2, action = 2, negate = 0, quality = 0, text = "" },
    { match = 3, action = 3, negate = 0, quality = 0, text = "" },
    { match = 9, action = 6, negate = 0, quality = 0, text = "" },
    { match = 10, action = 4, negate = 0, quality = 0, text = "" },
    { match = 11, action = 4, negate = 0, quality = 0, text = "" },
    { match = 12, action = 4, negate = 0, quality = 0, text = "" },
    { match = 7, action = 0, negate = 0, quality = 0, text = "" },
    { match = 8, action = 1, negate = 0, quality = 0, text = "" },
    { match = 1, action = 1, negate = 0, quality = 0, text = "" },
    { match = 0, action = 0, negate = 0, quality = 0, text = "" },
}
local RULE_FIELDS = { "Type", "Quality", "Name" }
local RULE_OPS = { "==", "!=" }
local RULE_NAME_OPS = { "Matches", "Does not match" }
local RULE_TYPES = { "All", "Quest", "Reagent", "Living", "Unattuned", "Attuned", "Recipe", "Food", "Potion", "Bags" }
local RULE_TYPE_MATCH = { 0, 2, 3, 4, 7, 8, 9, 10, 11, 12 }
local RULE_QUALS = { "Grey", "White", "Green", "Blue", "Epic", "Legendary" }
local RULE_ROWS = 10
local WORLD_UNLOCKS = {
    { id = 910092, name = "Solo Queue", how = "Queue for dungeons and raids by yourself. No group required.", toggle = true, toggleKey = "solo" },
    { id = 910105, name = "Auto-Mount", how = "Automatically mount when you leave combat. Unlocked by learning a mount.", toggle = true, toggleKey = "autoMount" },
    { id = 910106, name = "Class Buffs", how = "Clear Naxxramas 25 on a class. That class then applies 10% primary stats to you and nearby party." },
    { id = 910107, name = "Riding", how = "Train riding on any character. Alts can mount from level 1." },
    { id = 910108, name = "Auto-Accept", how = "Accept a quest. Then auto-accept when you talk to an NPC. Hold Shift to skip." },
    { id = 910091, name = "Armory", how = "Copy an attuned item into your bags to wear." },
    { id = 910003, name = "Auction", how = "List or bid at an auction house." },
    { id = 910090, name = "Quests - Finish", how = "Complete 1 quest. Summons questgivers for completed log quests for 60 sec. Turn in and take follow-ups." },
    { id = 910008, name = "Autoloot", how = "Manually loot 10 corpses." },
    { id = 910005, name = "Bank", how = "Open a world bank." },
    { id = 910007, name = "Bind Hearthstone", how = "Enter a dungeon." },
    { id = 910009, name = "Flight", how = "Learn a flight path." },
    { id = 910088, name = "Quests - Find", how = "Complete 50 quests. Adds up to 5 zone quests per use." },
    { id = 910002, name = "Mailbox", how = "Send or receive mail." },
    { id = 910006, name = "Stable", how = "Hunters: take on a pet." },
    { id = 910004, name = "Trainer", how = "Enter a dungeon." },
}

local WORLD_TRACKS = {
    {
        name = "Attune",
        ticks = {
            { id = 910101, name = "Curator", how = "Attune 1000 items on the account. Passively grants Living Gear XP to your 5 lowest collection pieces (bags, bank, armory) not currently worn.", bonus = 0 },
        },
    },
    {
        name = "Cooking",
        ticks = {
            { id = 910063, name = "75", how = "Reach Cooking 75. Out of combat, heal 1% of max health and mana every second.", bonus = 1 },
            { id = 910064, name = "150", how = "Reach Cooking 150. Out of combat, heal 2% of max health and mana every second.", bonus = 2 },
            { id = 910065, name = "225", how = "Reach Cooking 225. Out of combat, heal 3% of max health and mana every second.", bonus = 3 },
            { id = 910066, name = "300", how = "Reach Cooking 300. Out of combat, heal 4% of max health and mana every second.", bonus = 4 },
            { id = 910067, name = "375", how = "Reach Cooking 375. Out of combat, heal 5% of max health and mana every second.", bonus = 5 },
            { id = 910068, name = "450", how = "Reach Cooking 450. Out of combat, heal 6% of max health and mana every second.", bonus = 6 },
        },
    },
    {
        name = "Craft",
        ticks = {
            { id = 910093, name = "1", how = "Reach skill 75 in a crafting profession. Craft time 20% faster. Stacks.", bonus = 0 },
            { id = 910094, name = "2", how = "Reach skill 150 in a crafting profession. Craft time 20% faster. Stacks.", bonus = 0 },
            { id = 910095, name = "3", how = "Reach skill 225 in a crafting profession. Craft time 20% faster. Stacks.", bonus = 0 },
            { id = 910096, name = "4", how = "Reach skill 300 in a crafting profession. Craft time 20% faster. Stacks.", bonus = 0 },
            { id = 910097, name = "5", how = "Reach skill 375 in a crafting profession. Craft time 20% faster. Stacks.", bonus = 0 },
        },
    },
    {
        name = "Factions",
        ticks = {
            { id = 910020, name = "Arathor", how = "League of Arathor exalted. +100% reputation.", bonus = 100 },
            { id = 910016, name = "Bloodsail", how = "Bloodsail Buccaneers exalted. +100% reputation.", bonus = 100 },
            { id = 910017, name = "Darkmoon", how = "Darkmoon Faire exalted. +100% reputation.", bonus = 100 },
            { id = 910021, name = "Defilers", how = "The Defilers exalted. +100% reputation.", bonus = 100 },
            { id = 910025, name = "Frostwolf", how = "Frostwolf Clan exalted. +100% reputation.", bonus = 100 },
            { id = 910018, name = "Ravenholdt", how = "Ravenholdt exalted. +100% reputation.", bonus = 100 },
            { id = 910019, name = "Shendralar", how = "Shendralar exalted. +100% reputation.", bonus = 100 },
            { id = 910022, name = "Silverwing", how = "Silverwing Sentinels exalted. +100% reputation.", bonus = 100 },
            { id = 910024, name = "Stormpike", how = "Stormpike Guard exalted. +100% reputation.", bonus = 100 },
            { id = 910023, name = "Warsong", how = "Warsong Outriders exalted. +100% reputation.", bonus = 100 },
        },
    },
    {
        name = "First Aid",
        ticks = {
            { id = 910046, name = "Instant", how = "Train First Aid. Bandages become instant HoTs.", bonus = 0 },
            { id = 910047, name = "Restore", how = "Train First Aid. Bandages restore 1% HP per second at 1-75, 2% at 76-150, and so on.", bonus = 0 },
            { id = 910048, name = "Cleanse", how = "Max First Aid. While bandaged, remove debuffs every second.", bonus = 0 },
        },
    },
    {
        name = "Fishing",
        ticks = {
            { id = 910043, name = "Cast", how = "Train Fishing. After you cast Fishing, it recasts and catches for you.", bonus = 0 },
            { id = 910044, name = "Pools", how = "Catch 250 fish. While autofishing, loot pools within 25 yards.", bonus = 0 },
            { id = 910045, name = "Speed", how = "Earn the 500 Fish achievement. Bites come twice as fast.", bonus = 0 },
            { id = 910127, name = "150", how = "Reach Fishing 150. Fish yield 2x. Stacks to 4x at 300 and 8x at 450.", bonus = 0 },
            { id = 910128, name = "300", how = "Reach Fishing 300. Fish yield 4x.", bonus = 0 },
            { id = 910129, name = "450", how = "Reach Fishing 450. Fish yield 8x.", bonus = 0 },
            { id = 910130, name = "Reach 75", how = "Reach Fishing 75. Auto-loot pools from +3 yards. Stacks to +9 yards at 375.", bonus = 3 },
            { id = 910131, name = "Reach 225", how = "Reach Fishing 225. Auto-loot pools from +6 yards.", bonus = 6 },
            { id = 910132, name = "Reach 375", how = "Reach Fishing 375. Auto-loot pools from +9 yards.", bonus = 9 },
        },
    },
    {
        name = "Engineering",
        ticks = {
            { id = 910133, name = "150", how = "Reach Engineering 150. Crafts and blasting/salvage yield 2x. Stacks to 4x at 300 and 8x at 450.", bonus = 0 },
            { id = 910134, name = "300", how = "Reach Engineering 300. Crafts and blasting/salvage yield 4x.", bonus = 0 },
            { id = 910135, name = "450", how = "Reach Engineering 450. Crafts and blasting/salvage yield 8x.", bonus = 0 },
            { id = 910136, name = "Reach 75", how = "Reach Engineering 75. Auto-gather blasting nodes and salvage from +3 yards. Stacks to +9 yards at 375.", bonus = 3 },
            { id = 910137, name = "Reach 225", how = "Reach Engineering 225. Auto-gather blasting nodes and salvage from +6 yards.", bonus = 6 },
            { id = 910138, name = "Reach 375", how = "Reach Engineering 375. Auto-gather blasting nodes and salvage from +9 yards.", bonus = 9 },
        },
    },
    {
        name = "Herbalism",
        ticks = {
            { id = 910115, name = "150", how = "Reach Herbalism 150. Herbs yield 2x. Stacks to 4x at 300 and 8x at 450.", bonus = 0 },
            { id = 910116, name = "300", how = "Reach Herbalism 300. Herbs yield 4x.", bonus = 0 },
            { id = 910117, name = "450", how = "Reach Herbalism 450. Herbs yield 8x.", bonus = 0 },
            { id = 910118, name = "Reach 75", how = "Reach Herbalism 75. Auto-gather herbs from +3 yards. Stacks to +9 yards at 375.", bonus = 3 },
            { id = 910119, name = "Reach 225", how = "Reach Herbalism 225. Auto-gather herbs from +6 yards.", bonus = 6 },
            { id = 910120, name = "Reach 375", how = "Reach Herbalism 375. Auto-gather herbs from +9 yards.", bonus = 9 },
        },
    },
    {
        name = "Honor",
        ticks = {
            { id = 910010, name = "Defeat", how = "Lose a battleground. +100% honor.", bonus = 100 },
            { id = 910011, name = "Victory", how = "Win a battleground. +200% honor.", bonus = 200 },
            { id = 910012, name = "Bloodied", how = "Get 100 honorable kills. +200% honor.", bonus = 200 },
        },
    },
    {
        name = "Leveling",
        ticks = {
            { id = 910053, name = "1", how = "Have 1 character at level 80. +50% XP.", bonus = 50 },
            { id = 910054, name = "2", how = "Have 2 characters at level 80. +50% XP.", bonus = 50 },
            { id = 910055, name = "3", how = "Have 3 characters at level 80. +50% XP.", bonus = 50 },
            { id = 910056, name = "4", how = "Have 4 characters at level 80. +50% XP.", bonus = 50 },
            { id = 910057, name = "5", how = "Have 5 characters at level 80. +50% XP.", bonus = 50 },
            { id = 910058, name = "6", how = "Have 6 characters at level 80. +50% XP.", bonus = 50 },
            { id = 910059, name = "7", how = "Have 7 characters at level 80. +50% XP.", bonus = 50 },
            { id = 910060, name = "8", how = "Have 8 characters at level 80. +50% XP.", bonus = 50 },
            { id = 910061, name = "9", how = "Have 9 characters at level 80. +50% XP.", bonus = 50 },
            { id = 910062, name = "10", how = "Have 10 characters at level 80. +50% XP.", bonus = 50 },
        },
    },
    {
        name = "Movement",
        ticks = {
            { id = 910038, name = "Wayfarer", how = "Complete 100 quests. +40% movement speed (stacks).", bonus = 40 },
            { id = 910039, name = "Double Jump", how = "Reach level 10. Jumps go twice as high and far. 10 sec cooldown. Boosted jumps grant +40% speed for 30 sec.", bonus = 0 },
            { id = 910040, name = "Triple Jump", how = "Reach level 30. Jumps go three times as high and far. 10 sec cooldown. Boosted jumps grant +40% speed for 30 sec.", bonus = 0 },
            { id = 910104, name = "Mounted Opener", how = "Reach level 40. While mounted: jump while moving forward for a boosted leap (+50% forward momentum). Jump again midair to slam down, pull enemies within 20 yards, and Thunder Clap.", bonus = 0 },
        },
    },
    {
        name = "Mining",
        ticks = {
            { id = 910109, name = "150", how = "Reach Mining 150. Ore yield 2x. Stacks to 4x at 300 and 8x at 450.", bonus = 0 },
            { id = 910110, name = "300", how = "Reach Mining 300. Ore yield 4x.", bonus = 0 },
            { id = 910111, name = "450", how = "Reach Mining 450. Ore yield 8x.", bonus = 0 },
            { id = 910112, name = "Reach 75", how = "Reach Mining 75. Auto-gather ore from +3 yards. Stacks to +9 yards at 375.", bonus = 3 },
            { id = 910113, name = "Reach 225", how = "Reach Mining 225. Auto-gather ore from +6 yards.", bonus = 6 },
            { id = 910114, name = "Reach 375", how = "Reach Mining 375. Auto-gather ore from +9 yards.", bonus = 9 },
        },
    },
    {
        name = "Professions",
        ticks = {
            { id = 910026, name = "75", how = "Reach profession skill 75. +100% skill-ups.", bonus = 100 },
            { id = 910027, name = "150", how = "Reach profession skill 150. +100% skill-ups.", bonus = 100 },
            { id = 910028, name = "225", how = "Reach profession skill 225. +100% skill-ups.", bonus = 100 },
            { id = 910029, name = "300", how = "Reach profession skill 300. +100% skill-ups.", bonus = 100 },
            { id = 910030, name = "375", how = "Reach profession skill 375. +100% skill-ups.", bonus = 100 },
            { id = 910031, name = "450", how = "Reach profession skill 450. +100% skill-ups.", bonus = 100 },
        },
    },
    {
        name = "Reputation",
        ticks = {
            { id = 910013, name = "1 Exalted", how = "Reach exalted with 1 faction.", bonus = 100 },
            { id = 910014, name = "5 Exalted", how = "Reach exalted with 5 factions.", bonus = 100 },
            { id = 910015, name = "10 Exalted", how = "Reach exalted with 10 factions.", bonus = 100 },
        },
    },
    {
        name = "Skinning",
        ticks = {
            { id = 910121, name = "150", how = "Reach Skinning 150. Skins yield 2x. Stacks to 4x at 300 and 8x at 450.", bonus = 0 },
            { id = 910122, name = "300", how = "Reach Skinning 300. Skins yield 4x.", bonus = 0 },
            { id = 910123, name = "450", how = "Reach Skinning 450. Skins yield 8x.", bonus = 0 },
            { id = 910124, name = "Reach 75", how = "Reach Skinning 75. Auto-skin from +3 yards. Stacks to +9 yards at 375.", bonus = 3 },
            { id = 910125, name = "Reach 225", how = "Reach Skinning 225. Auto-skin from +6 yards.", bonus = 6 },
            { id = 910126, name = "Reach 375", how = "Reach Skinning 375. Auto-skin from +9 yards.", bonus = 9 },
        },
    },
    {
        name = "Travel",
        ticks = {
            { id = 910098, name = "Swim", how = "Reach level 10. Swim speed +500%.", bonus = 500 },
            { id = 910073, name = "1", how = "Use your Hearthstone 1 time. Cast time and cooldown -20%.", bonus = 20 },
            { id = 910074, name = "2", how = "Use your Hearthstone 2 times. Cast time and cooldown -20%.", bonus = 20 },
            { id = 910075, name = "3", how = "Use your Hearthstone 3 times. Cast time and cooldown -20%.", bonus = 20 },
            { id = 910076, name = "4", how = "Use your Hearthstone 4 times. Cast time and cooldown -20%.", bonus = 20 },
            { id = 910077, name = "5", how = "Use your Hearthstone 5 times. Cast time and cooldown -20%.", bonus = 20 },
        },
    },
}

local TABS = {
    { id = "world", label = "World" },
    { id = "class", label = "Class" },
    { id = "gear", label = "Gear" },
    { id = "attune", label = "Attune" },
    { id = "reagents", label = "Reagents" },
    { id = "loot", label = "Autoloot" },
}

local ATTUNE_QUALS = {
    { q = 0, name = "Poor", need = 0 },
    { q = 1, name = "Common", need = 10 },
    { q = 2, name = "Uncommon", need = 100 },
    { q = 3, name = "Rare", need = 1000 },
    { q = 4, name = "Epic", need = 10000 },
    { q = 5, name = "Legendary", need = 100000 },
}

local VAULT_QUEST = 1
local VAULT_REAGENT = 2
local VAULT_ROWS = 14
local FRAME_W = 540
local FRAME_H = 420
local SCALES = { 0.85, 1.0, 1.15, 1.3, 1.5, 1.75 }
local SCALE_LABELS = { "85%", "100%", "115%", "130%", "150%", "175%" }
local LG_MAX_LEVEL = 50
local GEAR_SLOTS = {
    { slot = 0, name = "Head", tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Head" },
    { slot = 1, name = "Neck", tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Neck" },
    { slot = 2, name = "Shoulders", tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Shoulder" },
    { slot = 14, name = "Back", tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Chest" },
    { slot = 4, name = "Chest", tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Chest" },
    { slot = 8, name = "Wrists", tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Wrists" },
    { slot = 9, name = "Hands", tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Hands" },
    { slot = 5, name = "Waist", tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Waist" },
    { slot = 6, name = "Legs", tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Legs" },
    { slot = 7, name = "Feet", tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Feet" },
    { slot = 10, name = "Finger 1", tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Finger" },
    { slot = 11, name = "Finger 2", tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Finger" },
    { slot = 12, name = "Trinket 1", tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Trinket" },
    { slot = 13, name = "Trinket 2", tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Trinket" },
    { slot = 15, name = "Main Hand", tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-MainHand" },
    { slot = 16, name = "Off Hand", tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-SecondaryHand" },
    { slot = 17, name = "Ranged", tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Ranged" },
}

local ARM_TYPES = { "All", "Armor", "Weapon", "Accessory" }
local ARM_ATTRS = { "All", "Str", "Agi", "Sta", "Int", "Spi", "Armor" }
local ARM_ATTR_KEYS = {
    Str = "str",
    Agi = "agi",
    Sta = "sta",
    Int = "intel",
    Spi = "spi",
    Armor = "armor",
}
local ARM_FILTER_Y = -26
local ARM_SLOT_Y = -66
local ARM_LIST_X = 260

local CLASS_PERKS = {
    MAGE = {
        { id = 910032, name = "Arcane", how = "In combat, Mirror Images appear and chain-cast. They linger 60 sec after combat." },
        { id = 910033, name = "Fire", how = "Fire spells apply Living Bomb. Spreads every 1 sec." },
        { id = 910034, name = "Frost", how = "Blizzard is instant, no cooldown, and lingers like Death and Decay. Ice Lance hits nearby enemies every 2 sec." },
    },
    ROGUE = {
        { id = 910035, name = "Assassination", how = "Poisons deal +300% damage. DoTs spread within 10 yards." },
        { id = 910036, name = "Combat", how = "Blade Flurry always on. +50% energy regen. 30% free Killing Spree on builders." },
        { id = 910037, name = "Subtlety", how = "Shadowstep has no cooldown and grants Stealth. Chains up to 5 Ambushes (+500% damage). Jack in the Box trap and Shadow Clone pet." },
    },
    PALADIN = {
        { id = 910069, name = "Holy", how = "Consecration follows you and toggles off if recast. Consecration damage +1000%. Holy Shock damage +300% and hits enemies within 10 yards of the target." },
        { id = 910070, name = "Protection", how = "Avenger's Shield bounces 30 times and can rehit. Range 60 yards. Devotion Aura: 10% damage reduction and +20% run/mount speed for allies. You deal Holy thorns equal to 50% of your armor." },
        { id = 910071, name = "Retribution", how = "Divine Storm radius doubled and each press hits 4 times. Learn Crusader Strike. While Retribution Aura is up, Crusader Strike also casts Exorcism on nearby enemies." },
    },
    WARRIOR = {
        { id = 910083, name = "Arms", how = "Learn Bladestorm. No rage cost, no cooldown, and it does not end. Recast to stop. You can use other abilities while spinning." },
        { id = 910084, name = "Fury", how = "Titan's Grip. Each melee hit: +5% attack speed (20 stacks) and heal 1% of max health in combat. Attack speed lingers 60 sec after combat. Rend and Deep Wounds deal +300% damage." },
        { id = 910085, name = "Protection", how = "Learn Shockwave with no cooldown and +300% damage. Thunder Clap radius doubled. Thunder Clap applies your Rend and Deep Wounds if trained." },
    },
}

-- Lookup by spell id from server CPK lines. Do not key the Class tab on UnitClass.
local CLASS_PERK_BY_ID = {}
for _, list in pairs(CLASS_PERKS) do
    for i = 1, #list do
        CLASS_PERK_BY_ID[list[i].id] = list[i]
    end
end

local db = {
    items = {},
    byKey = {},
    asked = {},
    absorb = { str = 0, agi = 0, sta = 0, intel = 0, spi = 0, armor = 0, count = 0 },
    perks = {},
    classPerks = {},
    classPerk = 0,
    rules = {},
    vault = {},
    autoloot = { on = 0, corpses = 0, need = 10, de = 0 },
    attune = { on = 1, count = 0, off = 0 },
    tab = "world",
    addMatch = 1,
    addAction = 1,
    ruleField = 1,
    ruleOp = 1,
    ruleType = 2,
    ruleQual = 1,
    ruleAction = 1,
    ruleOff = 0,
    vaultOff = { [1] = 0, [2] = 0 },
    armory = {},
    armoryOff = 0,
    armorySlot = -1,
    armoryType = "All",
    armoryAttr = "All",
    reagentCat = "All",
    jump = { mode = 2, max = 0 },
    solo = 0,
    autoMount = 0,
    speedCap = 500,
    reagentSearch = "",
    scale = 1,
}

local syncing = false
local vaultLayoutPending = false

local ui = {}
local RefreshOverlays

local function Solid(tex, r, g, b, a)
    tex:SetTexture(WHITE)
    tex:SetVertexColor(r, g, b, a or 1)
end

local function Font(parent, size, r, g, b)
    local fs = parent:CreateFontString(nil, "OVERLAY")
    fs:SetFont("Fonts\\FRIZQT__.TTF", size or 12, "")
    fs:SetTextColor(r or 0.9, g or 0.9, b or 0.9, 1)
    fs:SetJustifyH("LEFT")
    return fs
end

local function SplitPipe(s)
    local out = {}
    if not s or s == "" then
        return out
    end
    local start = 1
    while true do
        local i = string.find(s, "|", start, true)
        if not i then
            table.insert(out, string.sub(s, start))
            break
        end
        table.insert(out, string.sub(s, start, i - 1))
        start = i + 1
    end
    return out
end

local function StatLine(s, a, t, i, p, ar)
    local parts = {}
    local vals = {
        { tonumber(s) or 0, "str" },
        { tonumber(a) or 0, "agi" },
        { tonumber(t) or 0, "sta" },
        { tonumber(i) or 0, "int" },
        { tonumber(p) or 0, "spi" },
        { tonumber(ar) or 0, "armor" },
    }
    for n = 1, #vals do
        if vals[n][1] ~= 0 then
            table.insert(parts, string.format("+%.0f %s", vals[n][1], vals[n][2]))
        end
    end
    return table.concat(parts, "  ")
end

local function LevelQuality(lv)
    lv = tonumber(lv) or 1
    if lv >= LG_MAX_LEVEL then
        return 5
    end
    if lv >= 40 then
        return 4
    end
    if lv >= 30 then
        return 3
    end
    if lv >= 20 then
        return 2
    end
    if lv >= 10 then
        return 1
    end
    return 0
end

local function SetGearTip(text)
    if ui.gearTip then
        ui.gearTip:SetText(text or "Hover a bar for XP.")
    end
end

local function StyleBtn(btn, r, g, b)
    r = r or 0.14
    g = g or 0.14
    b = b or 0.14
    if not btn.bg then
        btn.bg = btn:CreateTexture(nil, "BACKGROUND")
        btn.bg:SetAllPoints(btn)
    end
    btn._ir, btn._ig, btn._ib = r, g, b
    Solid(btn.bg, r, g, b, 1)
    btn:SetScript("OnEnter", function(self)
        Solid(self.bg, math.min(1, self._ir + 0.08), math.min(1, self._ig + 0.08), math.min(1, self._ib + 0.08), 1)
    end)
    btn:SetScript("OnLeave", function(self)
        Solid(self.bg, self._ir, self._ig, self._ib, 1)
    end)
end

local function SendAttune(slot)
    local name = UnitName("player")
    if name and slot then
        SendAddonMessage(PREFIX, "ATTUNE|" .. tostring(slot), "WHISPER", name)
    end
end

local function SendLine(line)
    local name = UnitName("player")
    if name and line then
        SendAddonMessage(PREFIX, line, "WHISPER", name)
    end
end

local function IsCriticalChatText(text)
    if not text then
        return false
    end
    if string.find(text, "|cffff6666", 1, true) then
        return true
    end
    if string.find(text, "|cffffcc00", 1, true) then
        return true
    end
    return false
end

local function IsLivingGearChatText(text)
    if not text then
        return false
    end
    if string.find(text, "[Living Gear]", 1, true) then
        return true
    end
    if string.find(text, "[Account Perks]", 1, true) then
        return true
    end
    if string.find(text, "[Zone Scale]", 1, true) then
        return true
    end
    if string.find(text, "[LG]", 1, true) then
        return true
    end
    return false
end

local function ShouldFilterChatText(text)
    if LivingGearDB.showChat then
        return false
    end
    if not IsLivingGearChatText(text) then
        return false
    end
    if IsCriticalChatText(text) then
        return false
    end
    return true
end

local chatFilterInstalled = false

local function InstallChatFilter()
    if chatFilterInstalled then
        return
    end
    chatFilterInstalled = true
    local function hookFrame(frame)
        if not frame or frame._lgChatHook then
            return
        end
        local orig = frame.AddMessage
        if not orig then
            return
        end
        frame._lgChatHook = true
        frame.AddMessage = function(self, text, ...)
            if ShouldFilterChatText(text) then
                return
            end
            orig(self, text, ...)
        end
    end
    hookFrame(DEFAULT_CHAT_FRAME)
    for i = 1, (NUM_CHAT_WINDOWS or 10) do
        hookFrame(_G["ChatFrame" .. i])
    end
end

local function SyncChatSetting()
    SendLine("CHATSET|" .. (LivingGearDB.showChat and "1" or "0"))
end

local function SetShowChat(on)
    LivingGearDB.showChat = on and true or false
    SyncChatSetting()
    if ui.chatToggle then
        LayoutWorld()
    end
end

local function RequestSync()
    if syncing then
        return
    end
    local name = UnitName("player")
    if name then
        SendAddonMessage(PREFIX, "REQ", "WHISPER", name)
        SyncChatSetting()
    end
end

local BANK_HOSTS = {
    "BankFrame",
    "ElvUI_BankContainerFrame",
    "ElvUI_BankContainerFrameHolder",
}

local function BankHostFrame()
    for i = 1, #BANK_HOSTS do
        local f = _G[BANK_HOSTS[i]]
        if f and f.IsShown and f:IsShown() then
            return f
        end
    end
    return nil
end

local function EnsureBankDeposit()
    if ui.bankDeposit then
        return ui.bankDeposit
    end
    local btn = CreateFrame("Button", "LivingGearBankDeposit", UIParent)
    btn:SetSize(118, 22)
    btn:SetFrameStrata("HIGH")
    btn:SetFrameLevel(200)
    StyleBtn(btn, 0.14, 0.22, 0.14)
    btn.label = Font(btn, 11, 0.85, 0.95, 0.85)
    btn.label:SetPoint("CENTER", 0, 0)
    btn.label:SetJustifyH("CENTER")
    btn.label:SetText("Deposit All")
    btn:SetScript("OnClick", function()
        SendLine("DEPOSITALL")
    end)
    btn:Hide()
    ui.bankDeposit = btn
    return btn
end

local function HideBankDeposit()
    if ui.bankDeposit then
        ui.bankDeposit:Hide()
    end
end

local function ShowBankDeposit()
    local btn = EnsureBankDeposit()
    btn:ClearAllPoints()
    local host = BankHostFrame()
    if host then
        btn:SetPoint("TOPLEFT", host, "BOTTOMLEFT", 8, -6)
    else
        btn:SetPoint("TOPLEFT", UIParent, "TOPLEFT", 16, -140)
    end
    btn:Show()
end

local function RuleText(rule)
    local act = ACTION_NAMES[(rule.action or 0) + 1] or "?"
    local neg = (tonumber(rule.negate) or 0) == 1
    local m = tonumber(rule.match) or 0
    if m == 5 then
        local op = neg and "Does not match" or "Matches"
        return string.format("Name %s == %s -> %s", op, rule.text ~= "" and rule.text or "?", act)
    end
    if m == 6 then
        local q = RULE_QUALS[(tonumber(rule.quality) or 0) + 1] or "?"
        return string.format("Quality %s %s -> %s", neg and "!=" or "==", q, act)
    end
    local names = { "All", "Grey", "Quest", "Reagent", "Living", "Name", "Quality", "Unattuned", "Attuned", "Recipe" }
    local n = names[m + 1] or "?"
    if m == 1 then
        return string.format("Quality %s Grey -> %s", neg and "!=" or "==", act)
    end
    return string.format("Type %s %s -> %s", neg and "!=" or "==", n, act)
end

local function DefaultRules()
    local rules = {}
    for i = 1, #DEFAULT_RULES do
        local r = DEFAULT_RULES[i]
        local action = r.action
        if tonumber(r.match) == 8 then
            action = (tonumber(db.autoloot.de) == 1) and 5 or 1
        end
        rules[i] = {
            match = r.match,
            action = action,
            negate = r.negate or 0,
            quality = r.quality or 0,
            text = r.text or "",
        }
    end
    return rules
end

local function ActiveRules()
    if #db.rules > 0 then
        return db.rules
    end
    return DefaultRules()
end

local function ActionIndex(name)
    name = string.lower(name or "")
    for i = 1, #ACTION_NAMES do
        if string.lower(ACTION_NAMES[i]) == name then
            return i - 1
        end
    end
    return nil
end

local function ExportRules()
    local rules = ActiveRules()
    local lines = {}
    for i = 1, #rules do
        table.insert(lines, RuleText(rules[i]))
    end
    return table.concat(lines, "\n")
end

local function ParseImportLine(line)
    line = string.gsub(line or "", "^%s+", "")
    line = string.gsub(line, "%s+$", "")
    if line == "" then
        return nil
    end
    local left, actName = string.match(line, "^(.-)%s*->%s*(.+)$")
    if not left then
        return nil
    end
    local action = ActionIndex(actName)
    if not action then
        return nil
    end
    local nameText = string.match(left, "^Name%s+[Dd]oes not match%s+==%s+(.+)$")
        or string.match(left, "^Name%s+does not match%s+(.+)$")
        or string.match(left, "^Name%s+!=%s+(.+)$")
    if nameText then
        return { match = 5, action = action, negate = 1, quality = 0, text = nameText }
    end
    nameText = string.match(left, "^Name%s+[Mm]atches%s+==%s+(.+)$")
        or string.match(left, "^Name%s+[Mm]atches%s+(.+)$")
        or string.match(left, "^Name%s+==%s+(.+)$")
    if nameText then
        return { match = 5, action = action, negate = 0, quality = 0, text = nameText }
    end
    local op, qual = string.match(left, "^Quality%s+(==|!=)%s+(%S+)$")
    if op and qual then
        local q = nil
        for i = 1, #RULE_QUALS do
            if string.lower(RULE_QUALS[i]) == string.lower(qual) then
                q = i - 1
                break
            end
        end
        if q then
            return { match = 6, action = action, negate = op == "!=" and 1 or 0, quality = q, text = "" }
        end
    end
    local top, typ = string.match(left, "^Type%s+(==|!=)%s+(%S+)$")
    if top and typ then
        for i = 1, #RULE_TYPES do
            if string.lower(RULE_TYPES[i]) == string.lower(typ) then
                return {
                    match = RULE_TYPE_MATCH[i],
                    action = action,
                    negate = top == "!=" and 1 or 0,
                    quality = 0,
                    text = "",
                }
            end
        end
    end
    return nil
end

local function SendRuleAdd(rule)
    SendLine(string.format("RULEADD|%s|%s|%s|%s|%s",
        tostring(rule.match or 0),
        tostring(rule.action or 0),
        tostring(rule.negate or 0),
        tostring(rule.quality or 0),
        string.gsub(rule.text or "", "|", "")))
end

local function CopyRules(src)
    local out = {}
    for i = 1, #src do
        local r = src[i]
        out[i] = {
            match = r.match,
            action = r.action,
            negate = r.negate or 0,
            quality = r.quality or 0,
            text = r.text or "",
        }
    end
    return out
end

local function ReplaceRules(rules)
    SendLine("RULECLR")
    for i = 1, #rules do
        SendRuleAdd(rules[i])
    end
end

local function InsertRule(rule)
    local rules = CopyRules(ActiveRules())
    local at = #rules + 1
    for i = 1, #rules do
        if (tonumber(rules[i].match) or 0) == 0 and (tonumber(rules[i].negate) or 0) == 0 then
            at = i
            break
        end
    end
    table.insert(rules, at, rule)
    ReplaceRules(rules)
end

local function PerkKnown(id)
    return (tonumber(db.perks[id]) or 0) == 1
end

local function NextLocked(ticks)
    for i = 1, #ticks do
        if not PerkKnown(ticks[i].id) then
            return ticks[i]
        end
    end
    return nil
end

local function TrackBonus(ticks)
    local bonus = 0
    for i = 1, #ticks do
        if PerkKnown(ticks[i].id) then
            bonus = bonus + (ticks[i].bonus or 0)
        end
    end
    return bonus
end

local function ClampScale(s)
    s = tonumber(s) or 1
    if s < 0.85 then
        s = 0.85
    end
    if s > 1.75 then
        s = 1.75
    end
    return s
end

local function LoadScale()
    return ClampScale(db.scale)
end

local function SaveScale(s)
    s = ClampScale(s)
    db.scale = s
    if ui.frame then
        ui.frame:SetScale(s)
    end
    if ui.scaleBtn and ui.scaleBtn.label then
        ui.scaleBtn.label:SetText(string.format("%d%%", math.floor(s * 100 + 0.5)))
    end
end

local function ToggleScaleMenu()
    if not ui.scaleMenu then
        return
    end
    if ui.scaleMenu:IsShown() then
        ui.scaleMenu:Hide()
    else
        ui.scaleMenu:Show()
    end
end

local function DefaultWorldTip()
    for i = 1, #WORLD_UNLOCKS do
        local info = WORLD_UNLOCKS[i]
        if not PerkKnown(info.id) then
            return "Next: " .. info.name .. " - " .. info.how
        end
    end
    for t = 1, #WORLD_TRACKS do
        local track = WORLD_TRACKS[t]
        local nxt = NextLocked(track.ticks)
        if nxt then
            return track.name .. " next: " .. nxt.name .. " - " .. nxt.how
        end
    end
    return "All world perks unlocked."
end

local function SetWorldTip(text)
    if ui.worldTip then
        ui.worldTip:SetText(text or DefaultWorldTip())
    end
end

local function PaintTick(btn, on)
    if on then
        btn._ir, btn._ig, btn._ib = 0.12, 0.34, 0.16
        if btn.mark then
            btn.mark:SetText("x")
            btn.mark:SetTextColor(0.55, 0.95, 0.6, 1)
        end
    else
        btn._ir, btn._ig, btn._ib = 0.11, 0.11, 0.11
        if btn.mark then
            btn.mark:SetText("")
        end
    end
    if btn.bg then
        Solid(btn.bg, btn._ir, btn._ig, btn._ib, 1)
    end
end

local function ShowTab(name)
    if name == "quest" then
        name = "world"
    end
    db.tab = name
    local panels = {
        world = ui.world,
        class = ui.class,
        gear = ui.gear,
        attune = ui.attune,
        reagents = ui.reagents,
        loot = ui.loot,
    }
    for id, panel in pairs(panels) do
        if panel then
            if id == name then
                panel:Show()
            else
                panel:Hide()
            end
        end
    end
    if ui.tabs then
        for i = 1, #ui.tabs do
            local btn = ui.tabs[i]
            local on = btn.tab == name
            StyleBtn(btn, on and 0.18 or 0.10, on and 0.22 or 0.10, on and 0.28 or 0.10)
        end
    end
end

local function LayoutGear()
    if not ui.rows then
        return
    end
    for i = 1, #GEAR_SLOTS do
        local info = GEAR_SLOTS[i]
        local row = ui.rows[i]
        local it = db.byKey["inv:" .. tostring(info.slot)]
        local invSlot = info.slot + 1
        local icon = GetInventoryItemTexture("player", invSlot)
        row:Show()
        row.armed = false
        row.attune.label:SetText("Attune")
        StyleBtn(row.attune, 0.28, 0.10, 0.10)
        if it then
            row.slot = it.slot
            row.name:SetText(it.name or info.name)
            row.name:SetTextColor(0.92, 0.92, 0.92, 1)
            local stats = StatLine(it.ds, it.da, it.dt, it.di, it.dp, it.dar)
            row.stats:SetText(stats)
            if icon then
                row.icon:SetTexture(icon)
                row.icon:SetTexCoord(0.07, 0.93, 0.07, 0.93)
                row.icon:SetVertexColor(1, 1, 1, 1)
            else
                row.icon:SetTexture(info.tex)
                row.icon:SetTexCoord(0, 1, 0, 1)
                row.icon:SetVertexColor(1, 1, 1, 0.85)
            end
            local lv = tonumber(it.lv) or 1
            local xp = tonumber(it.xp) or 0
            local need = tonumber(it.need) or 0
            local frac = 1
            if lv < LG_MAX_LEVEL and need > 0 then
                frac = xp / need
                if frac < 0 then
                    frac = 0
                end
                if frac > 1 then
                    frac = 1
                end
            end
            local r, g, b = GetItemQualityColor(LevelQuality(lv))
            row.bar:SetMinMaxValues(0, 1)
            row.bar:SetValue(frac)
            row.bar:SetStatusBarColor(r, g, b, 0.95)
            row.bar.label:SetText(tostring(lv))
            if lv >= LG_MAX_LEVEL then
                row.bar.tip = info.name .. " - Level " .. tostring(lv) .. " (max)"
            else
                row.bar.tip = string.format("%s - Level %s  %s / %s XP", info.name, lv, xp, need)
            end
            row.bar:Show()
            row.attune:Show()
        else
            row.slot = nil
            row.name:SetText(info.name)
            row.name:SetTextColor(0.45, 0.45, 0.45, 1)
            row.stats:SetText("")
            row.icon:SetTexture(info.tex)
            row.icon:SetTexCoord(0, 1, 0, 1)
            row.icon:SetVertexColor(0.7, 0.7, 0.7, 0.9)
            row.bar.tip = nil
            row.bar:Hide()
            row.attune:Hide()
        end
    end
    if ui.empty then
        ui.empty:Hide()
    end
    local ab = db.absorb
    local stats = StatLine(ab.str, ab.agi, ab.sta, ab.intel, ab.spi, ab.armor)
    if stats == "" then
        ui.absorb:SetText(string.format("Absorb (%s attuned)", ab.count or 0))
    else
        ui.absorb:SetText(string.format("Absorb (%s attuned):  %s", ab.count or 0, stats))
    end
    SetGearTip()
end

local function LayoutLoot()
    if not ui.loot then
        return
    end
    local al = db.autoloot
    local unlocked = PerkKnown(910008)
    if unlocked then
        ui.alToggle.label:SetText(tonumber(al.on) == 1 and "Autoloot: ON" or "Autoloot: OFF")
        if tonumber(al.on) == 1 then
            StyleBtn(ui.alToggle, 0.12, 0.32, 0.14)
        else
            StyleBtn(ui.alToggle, 0.32, 0.12, 0.12)
        end
        ui.alToggle:Show()
        if ui.alAttuned then
            ui.alAttuned:Show()
            if tonumber(al.de) == 1 then
                ui.alAttuned.label:SetText("Attuned: Disenchant")
                StyleBtn(ui.alAttuned, 0.24, 0.12, 0.28)
            else
                ui.alAttuned.label:SetText("Attuned: Vendor")
                StyleBtn(ui.alAttuned, 0.28, 0.20, 0.10)
            end
        end
        ui.alProg:SetText("Unattuned gear stays. Greys vendor. Lockboxes stay in bags.")
    else
        ui.alToggle:Hide()
        if ui.alAttuned then
            ui.alAttuned:Hide()
        end
        local need = tonumber(al.need) or 10
        local have = tonumber(al.corpses) or 0
        local left = need - have
        if left < 0 then
            left = 0
        end
        ui.alProg:SetText(string.format("Loot %s more corpses to unlock Autoloot. (%s/%s)",
            left, have, need))
    end

    local rules = ActiveRules()
    local usingDefault = #db.rules == 0
    if ui.ruleHint then
        if usingDefault then
            ui.ruleHint:SetText("Default rules. Edit to customize. Reset restores these.")
        else
            ui.ruleHint:SetText("Custom rules. First match wins.")
        end
    end
    local off = db.ruleOff or 0
    local maxOff = #rules - RULE_ROWS
    if maxOff < 0 then
        maxOff = 0
    end
    if off > maxOff then
        off = maxOff
        db.ruleOff = off
    end
    for i = 1, RULE_ROWS do
        local row = ui.ruleRows[i]
        local rule = rules[off + i]
        if rule then
            row:Show()
            row.text:SetText(RuleText(rule))
            row.idx = off + i
            row.del:Show()
        else
            row:Hide()
            row.idx = nil
        end
    end

    if ui.ruleField then
        ui.ruleField.label:SetText(RULE_FIELDS[db.ruleField] or "Type")
        if db.ruleField == 3 then
            ui.ruleOp.label:SetText(RULE_NAME_OPS[db.ruleOp] or "Matches")
            if ui.ruleType then
                ui.ruleType:Hide()
            end
            if ui.ruleQual then
                ui.ruleQual:Hide()
            end
            if ui.ruleNameWrap then
                ui.ruleNameWrap:Show()
            end
        else
            ui.ruleOp.label:SetText(RULE_OPS[db.ruleOp] or "==")
            if ui.ruleNameWrap then
                ui.ruleNameWrap:Hide()
            end
            if db.ruleField == 2 then
                if ui.ruleType then
                    ui.ruleType:Hide()
                end
                if ui.ruleQual then
                    ui.ruleQual:Show()
                    ui.ruleQual.label:SetText(RULE_QUALS[db.ruleQual] or "Grey")
                end
            else
                if ui.ruleQual then
                    ui.ruleQual:Hide()
                end
                if ui.ruleType then
                    ui.ruleType:Show()
                    ui.ruleType.label:SetText(RULE_TYPES[db.ruleType] or "Quest")
                end
            end
        end
        ui.ruleThen.label:SetText(ACTION_NAMES[db.ruleAction] or "Bags")
        if ui.rulePreview then
            ui.rulePreview:SetText(RuleText({
                match = db.ruleField == 3 and 5 or (db.ruleField == 2 and 6 or RULE_TYPE_MATCH[db.ruleType]),
                action = db.ruleAction - 1,
                negate = db.ruleOp == 2 and 1 or 0,
                quality = db.ruleQual - 1,
                text = ui.ruleName and ui.ruleName:GetText() or "",
            }))
        end
    end
end

local REAGENT_CATS = {
    "All", "Herbs", "Ore", "Leather", "Cloth", "Enchanting",
    "Elemental", "Cooking", "Gems", "Parts", "Keys", "Other",
}

local function ReagentCat(entry)
    local id = tonumber(entry) or 0
    local fam = 0
    if GetItemFamily and id > 0 then
        fam = tonumber(GetItemFamily(id)) or 0
    end
    local function hasBit(mask)
        if mask <= 0 then
            return false
        end
        return math.floor(fam / mask) % 2 == 1
    end
    if hasBit(2) then
        return "Herbs"
    end
    if hasBit(32) then
        return "Ore"
    end
    if hasBit(128) then
        return "Leather"
    end
    if hasBit(4) then
        return "Enchanting"
    end
    if hasBit(64) then
        return "Gems"
    end
    if hasBit(8) then
        return "Parts"
    end
    local _, _, _, _, _, class, sub = GetItemInfo(id)
    if class == "Key" or sub == "Key" then
        return "Keys"
    end
    if class == "Gem" or sub == "Jewelcrafting" then
        return "Gems"
    end
    if sub == "Herb" then
        return "Herbs"
    end
    if sub == "Metal & Stone" then
        return "Ore"
    end
    if sub == "Leather" then
        return "Leather"
    end
    if sub == "Cloth" then
        return "Cloth"
    end
    if sub == "Enchanting" or sub == "Armor Enchantment" or sub == "Weapon Enchantment" then
        return "Enchanting"
    end
    if sub == "Elemental" then
        return "Elemental"
    end
    if sub == "Meat" then
        return "Cooking"
    end
    if sub == "Parts" or sub == "Devices" or sub == "Explosives" then
        return "Parts"
    end
    return "Other"
end

local function VaultHint(kind)
    if kind == VAULT_REAGENT then
        return "Click a stack to take it out."
    end
    return "Click a quest item to move a stack to your bags."
end

local function VaultOf(kind, cat)
    local out = {}
    local q = ""
    if kind == VAULT_REAGENT then
        q = string.lower(db.reagentSearch or "")
    end
    for i = 1, #db.vault do
        local it = db.vault[i]
        if tonumber(it.kind) == kind then
            if not cat or cat == "All" or ReagentCat(it.entry) == cat then
                if q == "" or string.find(string.lower(it.name or ""), q, 1, true) then
                    table.insert(out, it)
                end
            end
        end
    end
    return out
end

local function VaultCountOf(entry)
    local n = 0
    local want = tonumber(entry)
    if not want then
        return 0
    end
    for i = 1, #db.vault do
        local it = db.vault[i]
        if tonumber(it.entry) == want then
            n = n + (tonumber(it.count) or 0)
        end
    end
    return n
end

local function VaultCountForEntry(entry)
    return VaultCountOf(entry)
end

local function VaultCountForName(name)
    if not name or name == "" then
        return 0
    end
    local n = 0
    for i = 1, #db.vault do
        if db.vault[i].name == name then
            n = n + (tonumber(db.vault[i].count) or 0)
        end
    end
    return n
end

local function ItemIdFromArg(item)
    if type(item) == "number" then
        return item
    end
    if type(item) ~= "string" then
        return nil
    end
    local fromLink = string.match(item, "item:(%d+)")
    if fromLink then
        return tonumber(fromLink)
    end
    for i = 1, #db.vault do
        if db.vault[i].name == item then
            return tonumber(db.vault[i].entry)
        end
    end
    if GetItemInfo then
        local _, link = GetItemInfo(item)
        if link then
            return tonumber(string.match(link, "item:(%d+)"))
        end
    end
    return nil
end

local function LayoutVault(kind, rows, empty, hint, cat)
    local list = VaultOf(kind, cat)
    local off = db.vaultOff[kind] or 0
    local maxOff = #list - #rows
    if maxOff < 0 then
        maxOff = 0
    end
    if off > maxOff then
        off = maxOff
        db.vaultOff[kind] = off
    end
    if off < 0 then
        off = 0
        db.vaultOff[kind] = 0
    end
    for i = 1, #rows do
        local row = rows[i]
        local it = list[off + i]
        if it then
            row:Show()
            row.text:SetText(string.format("%s  x%s", it.name or "Item", it.count or 0))
            row.kind = it.kind
            row.entry = it.entry
            local icon = GetItemIcon(tonumber(it.entry) or 0)
            if icon and row.icon then
                row.icon:SetTexture(icon)
                row.icon:SetTexCoord(0.07, 0.93, 0.07, 0.93)
                row.icon:Show()
            elseif row.icon then
                row.icon:Hide()
            end
        else
            row:Hide()
            row.kind = nil
            row.entry = nil
        end
    end
    if #list == 0 then
        empty:Show()
    else
        empty:Hide()
    end
    if hint then
        local base = VaultHint(kind)
        if #list > #rows then
            hint:SetText(string.format("%s  %s-%s / %s",
                base, off + 1, math.min(off + #rows, #list), #list))
        else
            hint:SetText(base)
        end
    end
end

local function LayoutReagentCats()
    if not ui.reagentCatBtns then
        return
    end
    local used = {}
    local all = VaultOf(VAULT_REAGENT)
    for i = 1, #all do
        used[ReagentCat(all[i].entry)] = true
    end
    if db.reagentCat ~= "All" and not used[db.reagentCat] then
        db.reagentCat = "All"
        db.vaultOff[VAULT_REAGENT] = 0
    end
    local y = 0
    for i = 1, #REAGENT_CATS do
        local name = REAGENT_CATS[i]
        local btn = ui.reagentCatBtns[i]
        if name == "All" or used[name] then
            btn:Show()
            btn:ClearAllPoints()
            btn:SetPoint("TOPLEFT", 8, -28 - y)
            y = y + 20
            local on = db.reagentCat == name
            StyleBtn(btn, on and 0.18 or 0.10, on and 0.22 or 0.10, on and 0.28 or 0.10)
        else
            btn:Hide()
        end
    end
end

local function LayoutClass()
    if not ui.class then
        return
    end
    local list = db.classPerks or {}
    if #list == 0 then
        ui.classEmpty:Show()
        ui.classHint:SetText("No class perk for your class yet.")
        for i = 1, #ui.classBtns do
            ui.classBtns[i]:Hide()
        end
        return
    end
    ui.classEmpty:Hide()
    for i = 1, #ui.classBtns do
        local btn = ui.classBtns[i]
        local id = list[i]
        local info = id and CLASS_PERK_BY_ID[id]
        if id then
            btn:Show()
            btn.id = id
            btn.how = info and info.how or "Class perk."
            local name = info and info.name
            if not name and GetSpellInfo then
                name = GetSpellInfo(id)
            end
            btn.label:SetText(name or ("Perk " .. tostring(id)))
            if db.classPerk == id then
                btn._ir, btn._ig, btn._ib = 0.12, 0.32, 0.14
            else
                btn._ir, btn._ig, btn._ib = 0.14, 0.14, 0.14
            end
            Solid(btn.bg, btn._ir, btn._ig, btn._ib, 1)
        else
            btn:Hide()
            btn.id = nil
        end
    end
    if db.classPerk == 0 then
        ui.classHint:SetText("Pick one. Each talent spec can have its own pick.")
    else
        local picked = CLASS_PERK_BY_ID[db.classPerk]
        if picked then
            ui.classHint:SetText(picked.how)
        else
            ui.classHint:SetText("Pick one. Each talent spec can have its own pick.")
        end
    end
end

local function UpdateWorldScroll()
    if not ui.worldClip or not ui.worldContent then
        return
    end
    local clipH = ui.worldClip:GetHeight() or 1
    if clipH < 1 then
        clipH = 1
    end
    local contentH = ui.worldContent._h or clipH
    if contentH < clipH then
        contentH = clipH
    end
    ui.worldContent:SetHeight(contentH)
    local max = contentH - clipH
    if max < 0 then
        max = 0
    end
    local cur = ui.worldClip:GetVerticalScroll() or 0
    if cur > max then
        cur = max
    end
    ui.worldClip:SetVerticalScroll(cur)
    if ui.worldBar and ui.worldThumb then
        if max > 0 then
            ui.worldBar:Show()
            local barH = ui.worldBar:GetHeight() or clipH
            local thumbH = barH * (clipH / contentH)
            if thumbH < 16 then
                thumbH = 16
            end
            if thumbH > barH then
                thumbH = barH
            end
            ui.worldThumb:SetHeight(thumbH)
            local range = barH - thumbH
            local y = 0
            if max > 0 and range > 0 then
                y = -(cur / max) * range
            end
            ui.worldThumb:ClearAllPoints()
            ui.worldThumb:SetPoint("TOP", ui.worldBar, "TOP", 0, y)
        else
            ui.worldBar:Hide()
        end
    end
end

local function WorldToggleOn(info)
    if not info or not info.toggle or not info.toggleKey then
        return false
    end
    if not PerkKnown(info.id) then
        return false
    end
    return (tonumber(db[info.toggleKey]) or 0) == 1
end

local function SendWorldToggle(info)
    if info.id == 910092 then
        SendLine("SOLOSET|" .. (WorldToggleOn(info) and "0" or "1"))
    elseif info.id == 910105 then
        SendLine("AMSET|" .. (WorldToggleOn(info) and "0" or "1"))
    end
end

local function LayoutWorld()
    if not ui.worldUnlocks then
        return
    end
    for i = 1, #WORLD_UNLOCKS do
        local info = WORLD_UNLOCKS[i]
        local btn = ui.worldUnlocks[i]
        local known = PerkKnown(info.id)
        local on = known
        if info.toggle then
            on = WorldToggleOn(info)
        end
        btn.tip = info.name .. " - " .. info.how
        if info.toggle then
            btn.label:SetText(on and (info.name .. ": ON") or (info.name .. ": OFF"))
        else
            btn.label:SetText(info.name)
        end
        if on then
            btn._ir, btn._ig, btn._ib = 0.12, 0.32, 0.14
            btn.label:SetTextColor(0.85, 0.95, 0.85, 1)
        elseif info.toggle and known then
            btn._ir, btn._ig, btn._ib = 0.32, 0.12, 0.12
            btn.label:SetTextColor(0.95, 0.8, 0.8, 1)
        else
            btn._ir, btn._ig, btn._ib = 0.12, 0.12, 0.12
            btn.label:SetTextColor(0.55, 0.55, 0.55, 1)
        end
        Solid(btn.bg, btn._ir, btn._ig, btn._ib, 1)
    end
    for t = 1, #WORLD_TRACKS do
        local track = WORLD_TRACKS[t]
        local row = ui.worldTracks[t]
        local bonus = TrackBonus(track.ticks)
        if bonus > 0 then
            row.head:SetText(string.format("%s (|cff4fd14f%s%%|r)", track.name, bonus))
        else
            row.head:SetText(track.name)
        end
        for i = 1, #row.ticks do
            local tick = row.ticks[i]
            local info = track.ticks[i]
            if info then
                tick:Show()
                tick.tip = info.name .. " - " .. info.how
                PaintTick(tick, PerkKnown(info.id))
            else
                tick:Hide()
            end
        end
    end
    if ui.jumpBtns then
        local max = tonumber(db.jump.max) or 0
        local mode = tonumber(db.jump.mode) or 0
        if max < 1 then
            ui.jumpLabel:Hide()
            for i = 1, #ui.jumpBtns do
                ui.jumpBtns[i]:Hide()
            end
        else
            ui.jumpLabel:Show()
            for i = 1, #ui.jumpBtns do
                local btn = ui.jumpBtns[i]
                local need = btn.mode
                local unlocked = need == 0 or max >= need
                btn:Show()
                if not unlocked then
                    btn.tip = btn.how .. " Unlock this rank first."
                    btn._ir, btn._ig, btn._ib = 0.12, 0.12, 0.12
                    btn.label:SetTextColor(0.45, 0.45, 0.45, 1)
                elseif mode == need then
                    btn.tip = btn.how
                    btn._ir, btn._ig, btn._ib = 0.12, 0.32, 0.14
                    btn.label:SetTextColor(0.85, 0.95, 0.85, 1)
                else
                    btn.tip = btn.how
                    btn._ir, btn._ig, btn._ib = 0.14, 0.14, 0.14
                    btn.label:SetTextColor(0.85, 0.85, 0.85, 1)
                end
                Solid(btn.bg, btn._ir, btn._ig, btn._ib, 1)
            end
        end
    end
    if ui.chatToggle then
        local on = LivingGearDB.showChat and true or false
        ui.chatToggle.tip = "Show Living Gear progress and unlock messages in chat. Errors always show."
        ui.chatToggle.label:SetText(on and "Show Living Gear chat: ON" or "Show Living Gear chat: OFF")
        if on then
            ui.chatToggle._ir, ui.chatToggle._ig, ui.chatToggle._ib = 0.12, 0.32, 0.14
            ui.chatToggle.label:SetTextColor(0.85, 0.95, 0.85, 1)
        else
            ui.chatToggle._ir, ui.chatToggle._ig, ui.chatToggle._ib = 0.32, 0.12, 0.12
            ui.chatToggle.label:SetTextColor(0.95, 0.8, 0.8, 1)
        end
        Solid(ui.chatToggle.bg, ui.chatToggle._ir, ui.chatToggle._ig, ui.chatToggle._ib, 1)
    end
    if ui.speedCap then
        local cap = tonumber(db.speedCap) or 500
        if cap < 100 then
            cap = 100
        end
        if cap > 500 then
            cap = 500
        end
        db.speedCap = cap
        ui.speedCap.label:SetText("Speed cap: " .. tostring(cap) .. "%")
        ui.speedCap.tip = "Movement speed bonuses add together. Hard cap 500%. Click to lower."
        ui.speedCap._ir, ui.speedCap._ig, ui.speedCap._ib = 0.14, 0.14, 0.22
        Solid(ui.speedCap.bg, ui.speedCap._ir, ui.speedCap._ig, ui.speedCap._ib, 1)
    end
    SetWorldTip()
    UpdateWorldScroll()
end

local function HasBit(mask, q)
    return math.floor((tonumber(mask) or 0) / (2 ^ q)) % 2 == 1
end

local function LayoutAttune()
    if not ui.attune then
        return
    end
    local unlocked = PerkKnown(910041)
    local aa = db.attune
    local count = tonumber(aa.count) or 0
    local masterOn = tonumber(aa.on) == 1
    local btnW, gap = 124, 8
    if ui.aaBag then
        ui.aaBag:ClearAllPoints()
        ui.aaBag:Show()
    end
    if ui.aaArmory then
        ui.aaArmory:ClearAllPoints()
        ui.aaArmory:Show()
    end
    if unlocked then
        ui.aaToggle:Show()
        ui.aaToggle.label:SetText(masterOn and "Auto-Attune: ON" or "Auto-Attune: OFF")
        if masterOn then
            StyleBtn(ui.aaToggle, 0.12, 0.32, 0.14)
        else
            StyleBtn(ui.aaToggle, 0.32, 0.12, 0.12)
        end
        ui.aaToggle:ClearAllPoints()
        ui.aaToggle:SetPoint("TOP", ui.attune, "TOP", -(btnW + gap), -2)
        if ui.aaBag then
            ui.aaBag:SetPoint("TOP", ui.attune, "TOP", 0, -2)
        end
        if ui.aaArmory then
            ui.aaArmory:SetPoint("TOP", ui.attune, "TOP", (btnW + gap), -2)
        end
        ui.aaProg:SetText(string.format("%s items attuned. Higher rarities unlock at 10, 100, 1000, ...", count))
    else
        ui.aaToggle:Hide()
        if ui.aaBag then
            ui.aaBag:SetPoint("TOP", ui.attune, "TOP", -(btnW + gap) / 2, -2)
        end
        if ui.aaArmory then
            ui.aaArmory:SetPoint("TOP", ui.attune, "TOP", (btnW + gap) / 2, -2)
        end
        ui.aaProg:SetText("Level a Living Gear item to 10 to unlock Auto-Attune. Poor starts on.")
    end
    for i = 1, #ATTUNE_QUALS do
        local info = ATTUNE_QUALS[i]
        local row = ui.aaRows[i]
        local r, g, b = GetItemQualityColor(info.q)
        row.name:SetText(info.name)
        row.name:SetTextColor(r, g, b, 1)
        local ready = unlocked and count >= info.need
        local enabled = ready and masterOn and not HasBit(aa.off, info.q)
        row.q = info.q
        row.ready = ready
        if ready then
            row.toggle:Show()
            row.toggle.label:SetText(enabled and "ON" or "OFF")
            if enabled then
                StyleBtn(row.toggle, 0.12, 0.32, 0.14)
            else
                StyleBtn(row.toggle, 0.32, 0.12, 0.12)
            end
            row.prog:SetText("Unlocked")
            row.prog:SetTextColor(0.55, 0.75, 0.55, 1)
        else
            row.toggle:Hide()
            if unlocked then
                row.prog:SetText(string.format("%s / %s attuned", count, info.need))
            elseif info.need == 0 then
                row.prog:SetText("Unlocked")
            else
                row.prog:SetText(string.format("Attune %s items", info.need))
            end
            row.prog:SetTextColor(0.55, 0.55, 0.55, 1)
        end
    end
end

local function ItemLinkText(entry, fallback)
    if not entry or entry == 0 then
        return fallback or "Item"
    end
    local name, link = GetItemInfo(entry)
    if link then
        return link
    end
    if name then
        return "|cff9d9d9d|Hitem:" .. tostring(entry) .. ":0:0:0:0:0:0:0|h[" .. name .. "]|h|r"
    end
    return fallback or ("Item " .. tostring(entry))
end

local function ArmoryEquipType(slot)
    slot = tonumber(slot) or -1
    if slot == 15 or slot == 16 or slot == 17 then
        return "Weapon"
    end
    if slot == 1 or slot == 10 or slot == 11 or slot == 12 or slot == 13 or slot == 14 then
        return "Accessory"
    end
    if slot >= 0 and slot <= 9 then
        return "Armor"
    end
    return "Other"
end

local function ArmoryItemMatches(it)
    if not it then
        return false
    end
    local slot = tonumber(db.armorySlot)
    if slot and slot >= 0 and tonumber(it.slot) ~= slot then
        return false
    end
    local typ = db.armoryType or "All"
    if typ ~= "All" and ArmoryEquipType(it.slot) ~= typ then
        return false
    end
    local attr = db.armoryAttr or "All"
    if attr ~= "All" then
        local key = ARM_ATTR_KEYS[attr]
        if not key or (tonumber(it[key]) or 0) <= 0 then
            return false
        end
    end
    return true
end

local function LayoutArmory()
    if not ui.armory or not ui.armRows then
        return
    end
    local list = db.armory or {}
    local slot = tonumber(db.armorySlot) or -1
    local filtered = {}
    for i = 1, #list do
        local it = list[i]
        if ArmoryItemMatches(it) then
            table.insert(filtered, it)
        end
    end
    local off = db.armoryOff or 0
    if off > 0 and off >= #filtered then
        off = math.max(0, #filtered - #ui.armRows)
        db.armoryOff = off
    end
    if #filtered == 0 then
        ui.armEmpty:Show()
        ui.armEmpty:SetText("No attuned items match these filters.")
    else
        ui.armEmpty:Hide()
    end
    if ui.armAllSlot then
        local allOn = slot < 0
        StyleBtn(ui.armAllSlot, allOn and 0.14 or 0.10, allOn and 0.22 or 0.10, allOn and 0.28 or 0.10)
        if allOn then
            ui.armAllSlot.label:SetTextColor(0.85, 0.95, 0.95, 1)
        else
            ui.armAllSlot.label:SetTextColor(0.75, 0.75, 0.75, 1)
        end
    end
    if ui.armSlotBtns then
        for i = 1, #ui.armSlotBtns do
            local btn = ui.armSlotBtns[i]
            local on = btn.slot == slot
            btn._ir, btn._ig, btn._ib = on and 0.14 or 0.10, on and 0.22 or 0.10, on and 0.28 or 0.10
            Solid(btn.bg, btn._ir, btn._ig, btn._ib, 1)
            if on then
                btn.label:SetTextColor(0.85, 0.95, 0.95, 1)
            else
                btn.label:SetTextColor(0.75, 0.75, 0.75, 1)
            end
        end
    end
    if ui.armTypeBtns then
        for i = 1, #ui.armTypeBtns do
            local btn = ui.armTypeBtns[i]
            local on = btn.typ == (db.armoryType or "All")
            StyleBtn(btn, on and 0.14 or 0.10, on and 0.22 or 0.10, on and 0.28 or 0.10)
            if on then
                btn.label:SetTextColor(0.85, 0.95, 0.95, 1)
            else
                btn.label:SetTextColor(0.75, 0.75, 0.75, 1)
            end
        end
    end
    if ui.armAttrBtns then
        for i = 1, #ui.armAttrBtns do
            local btn = ui.armAttrBtns[i]
            local on = btn.attr == (db.armoryAttr or "All")
            StyleBtn(btn, on and 0.14 or 0.10, on and 0.22 or 0.10, on and 0.28 or 0.10)
            if on then
                btn.label:SetTextColor(0.85, 0.95, 0.95, 1)
            else
                btn.label:SetTextColor(0.75, 0.75, 0.75, 1)
            end
        end
    end
    for i = 1, #ui.armRows do
        local row = ui.armRows[i]
        local it = filtered[off + i]
        if it then
            row:Show()
            row.entry = it.entry
            row.slot = tonumber(it.slot) or slot
            row.link = ItemLinkText(it.entry, it.name)
            row.text:SetText(row.link)
        else
            row:Hide()
            row.entry = nil
            row.link = nil
        end
    end
end

local function LayoutAll()
    LayoutGear()
    LayoutLoot()
    LayoutAttune()
    LayoutClass()
    LayoutWorld()
    LayoutArmory()
    if ui.reagents then
        LayoutReagentCats()
        LayoutVault(VAULT_REAGENT, ui.reagentRows, ui.reagentEmpty, ui.reagentHint, db.reagentCat)
    end
    if ui.reagentDeposit then
        ui.reagentDeposit:Show()
    end
    if ui.frame then
        ui.frame:SetSize(FRAME_W, FRAME_H)
    end
end

local function LayoutRows()
    LayoutAll()
end

local function RefreshVaultPanel()
    if not ui.reagents then
        return
    end
    LayoutReagentCats()
    LayoutVault(VAULT_REAGENT, ui.reagentRows, ui.reagentEmpty, ui.reagentHint, db.reagentCat)
end

local function MakePanel(parent)
    local p = CreateFrame("Frame", nil, parent)
    p:SetPoint("TOPLEFT", 0, -50)
    p:SetPoint("BOTTOMRIGHT", 0, 0)
    p:Hide()
    return p
end

local function MakeVaultPanel(parent, kind, withCats)
    local p = MakePanel(parent)
    local listX = withCats and 96 or 10
    local rowW = withCats and 424 or 520
    local hint = Font(p, 10, 0.55, 0.55, 0.55)
    hint:SetPoint("TOPLEFT", listX, -4)
    if kind == VAULT_REAGENT then
        local dep = CreateFrame("Button", nil, p)
        dep:SetSize(100, 18)
        dep:SetPoint("TOPRIGHT", -10, -2)
        dep:SetFrameLevel((p:GetFrameLevel() or 0) + 6)
        StyleBtn(dep, 0.14, 0.22, 0.14)
        dep.label = Font(dep, 10, 0.85, 0.95, 0.85)
        dep.label:SetPoint("CENTER", 0, 0)
        dep.label:SetJustifyH("CENTER")
        dep.label:SetText("Deposit All")
        dep:SetScript("OnClick", function()
            SendLine("DEPOSITALL")
        end)
        hint:SetPoint("RIGHT", dep, "LEFT", -8, 0)
        ui.reagentDeposit = dep
        dep:Show()
        local searchWrap = CreateFrame("Frame", nil, p)
        searchWrap:SetSize(160, 18)
        searchWrap:SetPoint("TOPRIGHT", dep, "TOPLEFT", -8, 0)
        searchWrap:SetBackdrop({
            bgFile = WHITE,
            edgeFile = WHITE,
            edgeSize = 1,
            insets = { left = 1, right = 1, top = 1, bottom = 1 },
        })
        searchWrap:SetBackdropColor(0.08, 0.08, 0.08, 1)
        searchWrap:SetBackdropBorderColor(0.22, 0.22, 0.22, 1)
        local search = CreateFrame("EditBox", nil, searchWrap)
        search:SetPoint("TOPLEFT", 4, -1)
        search:SetPoint("BOTTOMRIGHT", -4, 1)
        search:SetFont("Fonts\\FRIZQT__.TTF", 10, "")
        search:SetTextColor(0.9, 0.9, 0.9, 1)
        search:SetAutoFocus(false)
        search:SetMaxLetters(32)
        search:SetText(db.reagentSearch or "")
        search:SetScript("OnTextChanged", function(self)
            db.reagentSearch = self:GetText() or ""
            db.vaultOff[VAULT_REAGENT] = 0
            RefreshVaultPanel()
        end)
        ui.reagentSearch = search
        hint:ClearAllPoints()
        hint:SetPoint("TOPLEFT", listX, -4)
        hint:SetPoint("RIGHT", searchWrap, "LEFT", -8, 0)
    else
        hint:SetPoint("RIGHT", -10, 0)
    end
    hint:SetText(VaultHint(kind))
    local empty = Font(p, 11, 0.6, 0.6, 0.6)
    empty:SetPoint("TOPLEFT", listX, -28)
    empty:SetText(kind == VAULT_QUEST and "No quest items in the vault." or "No reagents in the vault.")
    if withCats then
        ui.reagentCatBtns = {}
        for i = 1, #REAGENT_CATS do
            local name = REAGENT_CATS[i]
            local btn = CreateFrame("Button", nil, p)
            btn:SetSize(84, 18)
            StyleBtn(btn, 0.10, 0.10, 0.10)
            btn.label = Font(btn, 10, 0.9, 0.9, 0.9)
            btn.label:SetPoint("CENTER", 0, 0)
            btn.label:SetJustifyH("CENTER")
            btn.label:SetText(name)
            btn:SetScript("OnClick", function()
                db.reagentCat = name
                db.vaultOff[kind] = 0
                LayoutAll()
            end)
            btn:Hide()
            ui.reagentCatBtns[i] = btn
        end
    end
    local rows = {}
    for i = 1, VAULT_ROWS do
        local row = CreateFrame("Button", nil, p)
        row:SetSize(rowW, 18)
        row:SetPoint("TOPLEFT", listX, -28 - (i - 1) * 20)
        row.bg = row:CreateTexture(nil, "BACKGROUND")
        row.bg:SetAllPoints(row)
        Solid(row.bg, 0.10, 0.10, 0.10, 0)
        row.icon = row:CreateTexture(nil, "ARTWORK")
        row.icon:SetSize(14, 14)
        row.icon:SetPoint("LEFT", 2, 0)
        row.text = Font(row, 11, 0.85, 0.85, 0.85)
        row.text:SetPoint("LEFT", 20, 0)
        row.text:SetPoint("RIGHT", -4, 0)
        row:SetScript("OnEnter", function(self)
            Solid(self.bg, 0.16, 0.16, 0.16, 1)
        end)
        row:SetScript("OnLeave", function(self)
            Solid(self.bg, 0.10, 0.10, 0.10, 0)
        end)
        row:SetScript("OnClick", function()
            if row.kind and row.entry then
                SendLine("TAKE|" .. tostring(row.kind) .. "|" .. tostring(row.entry))
            end
        end)
        row:Hide()
        rows[i] = row
    end
    p:EnableMouse(true)
    p:EnableMouseWheel(true)
    p:SetScript("OnMouseWheel", function(_, delta)
        local off = db.vaultOff[kind] or 0
        off = off - delta
        if off < 0 then
            off = 0
        end
        db.vaultOff[kind] = off
        LayoutAll()
    end)
    return p, rows, empty, hint
end

local function BuildUI()
    if ui.frame then
        return
    end

    local f = CreateFrame("Frame", "LivingGearFrame", UIParent)
    f:SetSize(FRAME_W, FRAME_H)
    f:SetPoint("CENTER", UIParent, "CENTER", 200, 60)
    f:SetFrameStrata("HIGH")
    f:SetMovable(true)
    f:EnableMouse(true)
    f:RegisterForDrag("LeftButton")
    f:SetScript("OnDragStart", f.StartMoving)
    f:SetScript("OnDragStop", f.StopMovingOrSizing)
    f:SetBackdrop({
        bgFile = WHITE,
        edgeFile = WHITE,
        edgeSize = 1,
        insets = { left = 1, right = 1, top = 1, bottom = 1 },
    })
    f:SetBackdropColor(0.07, 0.07, 0.07, 0.96)
    f:SetBackdropBorderColor(0.22, 0.22, 0.22, 1)
    f:Hide()
    ui.frame = f
    tinsert(UISpecialFrames, "LivingGearFrame")
    f:SetScript("OnHide", function()
        if ui.scaleMenu then
            ui.scaleMenu:Hide()
        end
    end)

    local title = Font(f, 13, 0.4, 0.8, 1)
    title:SetPoint("TOPLEFT", 10, -8)
    title:SetText("Account Perks")

    local close = CreateFrame("Button", nil, f)
    close:SetSize(22, 18)
    close:SetPoint("TOPRIGHT", -8, -8)
    StyleBtn(close, 0.14, 0.14, 0.14)
    close.label = Font(close, 12, 0.9, 0.9, 0.9)
    close.label:SetPoint("CENTER", 0, 0)
    close.label:SetJustifyH("CENTER")
    close.label:SetText("X")
    close:SetScript("OnClick", function()
        if ui.scaleMenu then
            ui.scaleMenu:Hide()
        end
        f:Hide()
    end)

    local scaleBtn = CreateFrame("Button", nil, f)
    scaleBtn:SetSize(22, 18)
    scaleBtn:SetPoint("TOPRIGHT", -34, -8)
    StyleBtn(scaleBtn, 0.14, 0.14, 0.14)
    scaleBtn.icon = scaleBtn:CreateTexture(nil, "ARTWORK")
    scaleBtn.icon:SetTexture("Interface\\Icons\\INV_Misc_Spyglass_02")
    scaleBtn.icon:SetPoint("CENTER", 0, 0)
    scaleBtn.icon:SetSize(14, 14)
    scaleBtn:SetScript("OnClick", function()
        ToggleScaleMenu()
    end)
    ui.scaleBtn = scaleBtn

    local scaleMenu = CreateFrame("Frame", nil, f)
    scaleMenu:SetSize(56, 6 + #SCALES * 18)
    scaleMenu:SetPoint("TOPRIGHT", scaleBtn, "BOTTOMRIGHT", 0, -2)
    scaleMenu:SetFrameStrata("DIALOG")
    scaleMenu:SetBackdrop({
        bgFile = WHITE,
        edgeFile = WHITE,
        edgeSize = 1,
        insets = { left = 1, right = 1, top = 1, bottom = 1 },
    })
    scaleMenu:SetBackdropColor(0.08, 0.08, 0.08, 0.98)
    scaleMenu:SetBackdropBorderColor(0.22, 0.22, 0.22, 1)
    scaleMenu:Hide()
    scaleMenu:EnableMouse(true)
    ui.scaleMenu = scaleMenu
    for i = 1, #SCALES do
        local opt = CreateFrame("Button", nil, scaleMenu)
        opt:SetSize(52, 16)
        opt:SetPoint("TOPLEFT", 2, -3 - (i - 1) * 18)
        StyleBtn(opt, 0.14, 0.14, 0.14)
        opt.label = Font(opt, 10, 0.9, 0.9, 0.9)
        opt.label:SetPoint("CENTER", 0, 0)
        opt.label:SetJustifyH("CENTER")
        opt.label:SetText(SCALE_LABELS[i])
        opt:SetScript("OnClick", function()
            SaveScale(SCALES[i])
            SendLine("SCALESET|" .. tostring(math.floor(SCALES[i] * 100 + 0.5)))
            scaleMenu:Hide()
        end)
    end

    ui.tabs = {}
    local tabX = 10
    for i = 1, #TABS do
        local info = TABS[i]
        local w = 10 + string.len(info.label) * 7
        if w < 48 then
            w = 48
        end
        local btn = CreateFrame("Button", nil, f)
        btn:SetSize(w, 18)
        btn:SetPoint("TOPLEFT", tabX, -28)
        StyleBtn(btn, 0.10, 0.10, 0.10)
        btn.label = Font(btn, 10, 0.9, 0.9, 0.9)
        btn.label:SetPoint("CENTER", 0, 0)
        btn.label:SetJustifyH("CENTER")
        btn.label:SetText(info.label)
        btn.tab = info.id
        btn:SetScript("OnClick", function()
            if ui.scaleMenu then
                ui.scaleMenu:Hide()
            end
            ShowTab(info.id)
            if info.id == "reagents" and #VaultOf(VAULT_REAGENT) == 0 then
                RequestSync()
            end
            LayoutAll()
        end)
        ui.tabs[i] = btn
        tabX = tabX + w + 4
    end

    local gear = CreateFrame("Frame", nil, f)
    gear:SetPoint("TOPLEFT", 0, -50)
    gear:SetPoint("BOTTOMRIGHT", 0, 0)
    ui.gear = gear

    ui.absorb = Font(gear, 11, 0.85, 0.75, 0.45)
    ui.absorb:SetPoint("TOPLEFT", 10, -2)
    ui.absorb:SetPoint("RIGHT", -10, 0)

    ui.gearTip = Font(gear, 10, 0.7, 0.7, 0.7)
    ui.gearTip:SetPoint("BOTTOMLEFT", 10, 8)
    ui.gearTip:SetPoint("BOTTOMRIGHT", -10, 8)
    ui.gearTip:SetText("Hover a bar for XP.")

    ui.empty = Font(gear, 11, 0.6, 0.6, 0.6)
    ui.empty:SetPoint("TOPLEFT", 10, -24)
    ui.empty:Hide()

    ui.rows = {}
    for i = 1, #GEAR_SLOTS do
        local row = CreateFrame("Frame", nil, gear)
        row:SetSize(520, 18)
        row:SetPoint("TOPLEFT", 10, -20 - (i - 1) * 19)
        row.icon = row:CreateTexture(nil, "ARTWORK")
        row.icon:SetSize(16, 16)
        row.icon:SetPoint("LEFT", 0, 0)
        row.name = Font(row, 11, 0.92, 0.92, 0.92)
        row.name:SetPoint("LEFT", 20, 0)
        row.name:SetWidth(168)
        row.stats = Font(row, 10, 0.7, 0.7, 0.7)
        row.stats:SetPoint("LEFT", 190, 0)
        row.stats:SetWidth(150)
        row.attune = CreateFrame("Button", nil, row)
        row.attune:SetSize(48, 16)
        row.attune:SetPoint("RIGHT", 0, 0)
        StyleBtn(row.attune, 0.28, 0.10, 0.10)
        row.attune.label = Font(row.attune, 10, 0.95, 0.8, 0.8)
        row.attune.label:SetPoint("CENTER", 0, 0)
        row.attune.label:SetJustifyH("CENTER")
        row.attune.label:SetText("Attune")
        row.attune:SetScript("OnClick", function()
            if not row.slot then
                return
            end
            if not row.armed then
                row.armed = true
                row.attune.label:SetText("Sure?")
                StyleBtn(row.attune, 0.42, 0.12, 0.12)
                return
            end
            row.armed = false
            row.attune.label:SetText("Attune")
            StyleBtn(row.attune, 0.28, 0.10, 0.10)
            SendAttune(row.slot)
        end)
        row.bar = CreateFrame("StatusBar", nil, row)
        row.bar:SetSize(72, 12)
        row.bar:SetPoint("RIGHT", row.attune, "LEFT", -6, 0)
        row.bar:SetStatusBarTexture(WHITE)
        row.bar:SetMinMaxValues(0, 1)
        row.bar:SetValue(0)
        row.bar.bg = row.bar:CreateTexture(nil, "BACKGROUND")
        row.bar.bg:SetAllPoints(row.bar)
        row.bar.bg:SetTexture(WHITE)
        row.bar.bg:SetVertexColor(0.08, 0.08, 0.08, 0.9)
        row.bar.label = Font(row.bar, 10, 0.95, 0.95, 0.95)
        row.bar.label:SetPoint("CENTER", 0, 0)
        row.bar.label:SetJustifyH("CENTER")
        row.bar:EnableMouse(true)
        row.bar:SetScript("OnEnter", function(self)
            if self.tip then
                SetGearTip(self.tip)
            end
        end)
        row.bar:SetScript("OnLeave", function()
            SetGearTip()
        end)
        ui.rows[i] = row
    end

    local attune = MakePanel(f)
    ui.attune = attune

    ui.aaToggle = CreateFrame("Button", nil, attune)
    ui.aaToggle:SetSize(124, 18)
    ui.aaToggle:SetPoint("TOP", attune, "TOP", -132, -2)
    StyleBtn(ui.aaToggle, 0.12, 0.32, 0.14)
    ui.aaToggle.label = Font(ui.aaToggle, 10, 0.9, 0.95, 0.9)
    ui.aaToggle.label:SetPoint("CENTER", 0, 0)
    ui.aaToggle.label:SetJustifyH("CENTER")
    ui.aaToggle.label:SetText("Auto-Attune: ON")
    ui.aaToggle:SetScript("OnClick", function()
        if tonumber(db.attune.on) == 1 then
            SendLine("AASET|on|0")
        else
            SendLine("AASET|on|1")
        end
    end)

    ui.aaBag = CreateFrame("Button", nil, attune)
    ui.aaBag:SetSize(124, 18)
    ui.aaBag:SetPoint("TOP", attune, "TOP", 0, -2)
    StyleBtn(ui.aaBag, 0.22, 0.18, 0.10)
    ui.aaBag.label = Font(ui.aaBag, 10, 0.95, 0.9, 0.8)
    ui.aaBag.label:SetPoint("CENTER", 0, 0)
    ui.aaBag.label:SetJustifyH("CENTER")
    ui.aaBag.label:SetText("Attune Backpack")
    ui.aaBag:SetScript("OnClick", function()
        local name = GetSpellInfo(910042)
        if name then
            CastSpellByName(name)
        end
    end)

    ui.aaArmory = CreateFrame("Button", nil, attune)
    ui.aaArmory:SetSize(124, 18)
    ui.aaArmory:SetPoint("TOP", attune, "TOP", 132, -2)
    StyleBtn(ui.aaArmory, 0.14, 0.18, 0.22)
    ui.aaArmory.label = Font(ui.aaArmory, 10, 0.85, 0.9, 0.95)
    ui.aaArmory.label:SetPoint("CENTER", 0, 0)
    ui.aaArmory.label:SetJustifyH("CENTER")
    ui.aaArmory.label:SetText("Armory")
    ui.aaArmory:SetScript("OnClick", function()
        local name = GetSpellInfo(910091)
        if name then
            CastSpellByName(name)
        else
            SendLine("ARMOPEN")
        end
    end)

    ui.aaProg = Font(attune, 10, 0.7, 0.7, 0.7)
    ui.aaProg:SetPoint("TOPLEFT", 10, -24)
    ui.aaProg:SetPoint("TOPRIGHT", -10, -24)
    ui.aaProg:SetJustifyH("CENTER")

    ui.aaRows = {}
    for i = 1, #ATTUNE_QUALS do
        local row = CreateFrame("Frame", nil, attune)
        row:SetSize(520, 22)
        row:SetPoint("TOPLEFT", 10, -48 - (i - 1) * 26)
        row.name = Font(row, 12, 0.9, 0.9, 0.9)
        row.name:SetPoint("LEFT", 0, 0)
        row.name:SetWidth(100)
        row.prog = Font(row, 10, 0.55, 0.55, 0.55)
        row.prog:SetPoint("LEFT", 110, 0)
        row.prog:SetWidth(280)
        row.toggle = CreateFrame("Button", nil, row)
        row.toggle:SetSize(48, 18)
        row.toggle:SetPoint("RIGHT", 0, 0)
        StyleBtn(row.toggle, 0.12, 0.32, 0.14)
        row.toggle.label = Font(row.toggle, 10, 0.9, 0.95, 0.9)
        row.toggle.label:SetPoint("CENTER", 0, 0)
        row.toggle.label:SetJustifyH("CENTER")
        row.toggle.label:SetText("ON")
        row.toggle:SetScript("OnClick", function()
            if not row.ready then
                return
            end
            local off = HasBit(db.attune.off, row.q)
            SendLine("AASET|q|" .. tostring(row.q) .. "|" .. (off and "1" or "0"))
        end)
        ui.aaRows[i] = row
    end

    local ARM_ROWS = 10
    local arm = CreateFrame("Frame", nil, f)
    arm:SetFrameStrata("DIALOG")
    arm:SetPoint("TOPLEFT", 8, -48)
    arm:SetPoint("BOTTOMRIGHT", -8, 8)
    arm:SetBackdrop({
        bgFile = WHITE,
        edgeFile = WHITE,
        edgeSize = 1,
        insets = { left = 1, right = 1, top = 1, bottom = 1 },
    })
    arm:SetBackdropColor(0.07, 0.07, 0.08, 1)
    arm:SetBackdropBorderColor(0.18, 0.18, 0.18, 1)
    arm:Hide()
    arm:EnableMouse(true)
    ui.armory = arm
    ui.armTitle = Font(arm, 12, 0.9, 0.9, 0.9)
    ui.armTitle:SetPoint("TOPLEFT", 10, -8)
    ui.armTitle:SetText("Attuned Armory - click to equip")
    local armClose = CreateFrame("Button", nil, arm)
    armClose:SetSize(56, 18)
    armClose:SetPoint("TOPRIGHT", -8, -6)
    StyleBtn(armClose, 0.28, 0.12, 0.12)
    armClose.label = Font(armClose, 10, 0.95, 0.8, 0.8)
    armClose.label:SetPoint("CENTER", 0, 0)
    armClose.label:SetJustifyH("CENTER")
    armClose.label:SetText("Close")
    armClose:SetScript("OnClick", function()
        arm:Hide()
    end)
    ui.armTypeBtns = {}
    local typeX = 10
    for i = 1, #ARM_TYPES do
        local name = ARM_TYPES[i]
        local w = 10 + string.len(name) * 7
        if w < 52 then
            w = 52
        end
        local btn = CreateFrame("Button", nil, arm)
        btn:SetSize(w, 18)
        btn:SetPoint("TOPLEFT", typeX, ARM_FILTER_Y)
        btn.typ = name
        StyleBtn(btn, 0.10, 0.10, 0.10)
        btn.label = Font(btn, 10, 0.85, 0.85, 0.85)
        btn.label:SetPoint("CENTER", 0, 0)
        btn.label:SetJustifyH("CENTER")
        btn.label:SetText(name)
        btn:SetScript("OnClick", function()
            db.armoryType = name
            db.armoryOff = 0
            LayoutArmory()
        end)
        ui.armTypeBtns[i] = btn
        typeX = typeX + w + 4
    end
    ui.armAttrBtns = {}
    local attrX = ARM_LIST_X
    for i = 1, #ARM_ATTRS do
        local name = ARM_ATTRS[i]
        local w = 10 + string.len(name) * 7
        if w < 44 then
            w = 44
        end
        local btn = CreateFrame("Button", nil, arm)
        btn:SetSize(w, 18)
        btn:SetPoint("TOPLEFT", attrX, ARM_FILTER_Y)
        btn.attr = name
        StyleBtn(btn, 0.10, 0.10, 0.10)
        btn.label = Font(btn, 10, 0.85, 0.85, 0.85)
        btn.label:SetPoint("CENTER", 0, 0)
        btn.label:SetJustifyH("CENTER")
        btn.label:SetText(name)
        btn:SetScript("OnClick", function()
            db.armoryAttr = name
            db.armoryOff = 0
            LayoutArmory()
        end)
        ui.armAttrBtns[i] = btn
        attrX = attrX + w + 4
    end
    local armAllSlot = CreateFrame("Button", nil, arm)
    armAllSlot:SetSize(246, 18)
    armAllSlot:SetPoint("TOPLEFT", 10, ARM_SLOT_Y)
    StyleBtn(armAllSlot, 0.10, 0.10, 0.10)
    armAllSlot.label = Font(armAllSlot, 10, 0.85, 0.85, 0.85)
    armAllSlot.label:SetPoint("CENTER", 0, 0)
    armAllSlot.label:SetJustifyH("CENTER")
    armAllSlot.label:SetText("All slots")
    armAllSlot:SetScript("OnClick", function()
        db.armorySlot = -1
        db.armoryOff = 0
        LayoutArmory()
    end)
    ui.armAllSlot = armAllSlot
    ui.armSlotBtns = {}
    for i = 1, #GEAR_SLOTS do
        local info = GEAR_SLOTS[i]
        local btn = CreateFrame("Button", nil, arm)
        btn:SetSize(118, 18)
        btn.slot = info.slot
        local col = (i - 1) % 2
        local row = math.floor((i - 1) / 2)
        btn:SetPoint("TOPLEFT", 10 + col * 124, ARM_SLOT_Y - 20 - row * 20)
        StyleBtn(btn, 0.10, 0.10, 0.10)
        btn.label = Font(btn, 10, 0.85, 0.85, 0.85)
        btn.label:SetPoint("CENTER", 0, 0)
        btn.label:SetJustifyH("CENTER")
        btn.label:SetText(info.name)
        btn:SetScript("OnClick", function()
            db.armorySlot = info.slot
            db.armoryOff = 0
            LayoutArmory()
        end)
        ui.armSlotBtns[i] = btn
    end
    ui.armEmpty = Font(arm, 11, 0.6, 0.6, 0.6)
    ui.armEmpty:SetPoint("TOPLEFT", ARM_LIST_X, ARM_SLOT_Y)
    ui.armEmpty:SetText("No attuned items match these filters.")
    ui.armRows = {}
    for i = 1, ARM_ROWS do
        local row = CreateFrame("Button", nil, arm)
        row:SetSize(260, 18)
        row:SetPoint("TOPLEFT", ARM_LIST_X, ARM_SLOT_Y - (i - 1) * 20)
        row.bg = row:CreateTexture(nil, "BACKGROUND")
        row.bg:SetAllPoints(row)
        Solid(row.bg, 0.10, 0.10, 0.10, 0)
        row.text = Font(row, 11, 0.85, 0.85, 0.85)
        row.text:SetPoint("LEFT", 4, 0)
        row.text:SetPoint("RIGHT", -4, 0)
        row.text:SetJustifyH("LEFT")
        row:SetScript("OnEnter", function(self)
            Solid(self.bg, 0.16, 0.16, 0.16, 1)
            if self.link and GameTooltip then
                GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
                GameTooltip:SetHyperlink(self.link)
                GameTooltip:Show()
            end
        end)
        row:SetScript("OnLeave", function(self)
            Solid(self.bg, 0.10, 0.10, 0.10, 0)
            if GameTooltip then
                GameTooltip:Hide()
            end
        end)
        row:SetScript("OnClick", function(self)
            if self.entry then
                SendLine("ARMEQUIP|" .. tostring(self.slot or db.armorySlot or 0) .. "|" .. tostring(self.entry))
            end
        end)
        row:Hide()
        ui.armRows[i] = row
    end
    arm:EnableMouseWheel(true)
    arm:SetScript("OnMouseWheel", function(_, delta)
        local off = db.armoryOff or 0
        off = off - delta
        if off < 0 then
            off = 0
        end
        db.armoryOff = off
        LayoutArmory()
    end)

    local loot = MakePanel(f)
    ui.loot = loot

    ui.alToggle = CreateFrame("Button", nil, loot)
    ui.alToggle:SetSize(120, 18)
    ui.alToggle:SetPoint("TOPLEFT", 10, -2)
    StyleBtn(ui.alToggle, 0.12, 0.32, 0.14)
    ui.alToggle.label = Font(ui.alToggle, 10, 0.9, 0.95, 0.9)
    ui.alToggle.label:SetPoint("CENTER", 0, 0)
    ui.alToggle.label:SetJustifyH("CENTER")
    ui.alToggle.label:SetText("Autoloot: ON")
    ui.alToggle:SetScript("OnClick", function()
        if tonumber(db.autoloot.on) == 1 then
            SendLine("ALSET|0")
        else
            SendLine("ALSET|1")
        end
    end)

    ui.alAttuned = CreateFrame("Button", nil, loot)
    ui.alAttuned:SetSize(148, 18)
    ui.alAttuned:SetPoint("TOPLEFT", 138, -2)
    StyleBtn(ui.alAttuned, 0.28, 0.20, 0.10)
    ui.alAttuned.label = Font(ui.alAttuned, 10, 0.95, 0.9, 0.8)
    ui.alAttuned.label:SetPoint("CENTER", 0, 0)
    ui.alAttuned.label:SetJustifyH("CENTER")
    ui.alAttuned.label:SetText("Attuned: Vendor")
    ui.alAttuned:SetScript("OnClick", function()
        if tonumber(db.autoloot.de) == 1 then
            SendLine("ALDE|0")
        else
            SendLine("ALDE|1")
        end
    end)

    ui.alProg = Font(loot, 10, 0.7, 0.7, 0.7)
    ui.alProg:SetPoint("TOPLEFT", 294, -4)
    ui.alProg:SetPoint("RIGHT", -10, 0)

    ui.ruleHint = Font(loot, 10, 0.55, 0.55, 0.55)
    ui.ruleHint:SetPoint("TOPLEFT", 10, -24)
    ui.ruleHint:SetText("Default rules. Edit to customize. Reset restores these.")

    local listPane = CreateFrame("Frame", nil, loot)
    listPane:SetPoint("TOPLEFT", 10, -44)
    listPane:SetSize(268, 250)
    listPane:EnableMouse(true)
    listPane:EnableMouseWheel(true)
    listPane:SetScript("OnMouseWheel", function(_, delta)
        db.ruleOff = (db.ruleOff or 0) - delta
        if db.ruleOff < 0 then
            db.ruleOff = 0
        end
        LayoutLoot()
    end)
    ui.ruleRows = {}
    for i = 1, RULE_ROWS do
        local row = CreateFrame("Frame", nil, listPane)
        row:SetSize(268, 18)
        row:SetPoint("TOPLEFT", 0, -(i - 1) * 20)
        row.text = Font(row, 10, 0.85, 0.85, 0.85)
        row.text:SetPoint("LEFT", 0, 0)
        row.text:SetPoint("RIGHT", -48, 0)
        row.del = CreateFrame("Button", nil, row)
        row.del:SetSize(44, 16)
        row.del:SetPoint("RIGHT", 0, 0)
        StyleBtn(row.del, 0.28, 0.10, 0.10)
        row.del.label = Font(row.del, 10, 0.95, 0.8, 0.8)
        row.del.label:SetPoint("CENTER", 0, 0)
        row.del.label:SetJustifyH("CENTER")
        row.del.label:SetText("Del")
        row.del:SetScript("OnClick", function()
            if not row.idx then
                return
            end
            if #db.rules == 0 then
                local rules = CopyRules(DEFAULT_RULES)
                table.remove(rules, row.idx)
                ReplaceRules(rules)
                return
            end
            SendLine("RULEDEL|" .. tostring(row.idx - 1))
        end)
        row:Hide()
        ui.ruleRows[i] = row
    end

    local editPane = CreateFrame("Frame", nil, loot)
    editPane:SetPoint("TOPLEFT", 286, -44)
    editPane:SetSize(244, 250)
    local editHead = Font(editPane, 10, 0.4, 0.8, 1)
    editHead:SetPoint("TOPLEFT", 0, 0)
    editHead:SetText("New rule")
    local ifLbl = Font(editPane, 10, 0.55, 0.55, 0.55)
    ifLbl:SetPoint("TOPLEFT", 0, -22)
    ifLbl:SetText("If")

    local function Cycle(btn, maxv, key)
        db[key] = db[key] + 1
        if db[key] > maxv then
            db[key] = 1
        end
        LayoutLoot()
    end

    ui.ruleField = CreateFrame("Button", nil, editPane)
    ui.ruleField:SetSize(72, 18)
    ui.ruleField:SetPoint("TOPLEFT", 18, -20)
    StyleBtn(ui.ruleField, 0.14, 0.14, 0.14)
    ui.ruleField.label = Font(ui.ruleField, 10, 0.9, 0.9, 0.9)
    ui.ruleField.label:SetPoint("CENTER", 0, 0)
    ui.ruleField.label:SetJustifyH("CENTER")
    ui.ruleField.label:SetText("Type")
    ui.ruleField:SetScript("OnClick", function()
        Cycle(ui.ruleField, #RULE_FIELDS, "ruleField")
    end)

    ui.ruleOp = CreateFrame("Button", nil, editPane)
    ui.ruleOp:SetSize(100, 18)
    ui.ruleOp:SetPoint("LEFT", ui.ruleField, "RIGHT", 6, 0)
    StyleBtn(ui.ruleOp, 0.14, 0.14, 0.14)
    ui.ruleOp.label = Font(ui.ruleOp, 10, 0.9, 0.9, 0.9)
    ui.ruleOp.label:SetPoint("CENTER", 0, 0)
    ui.ruleOp.label:SetJustifyH("CENTER")
    ui.ruleOp.label:SetText("==")
    ui.ruleOp:SetScript("OnClick", function()
        Cycle(ui.ruleOp, 2, "ruleOp")
    end)

    ui.ruleType = CreateFrame("Button", nil, editPane)
    ui.ruleType:SetSize(220, 18)
    ui.ruleType:SetPoint("TOPLEFT", 0, -42)
    StyleBtn(ui.ruleType, 0.14, 0.14, 0.14)
    ui.ruleType.label = Font(ui.ruleType, 10, 0.9, 0.9, 0.9)
    ui.ruleType.label:SetPoint("CENTER", 0, 0)
    ui.ruleType.label:SetJustifyH("CENTER")
    ui.ruleType.label:SetText("Quest")
    ui.ruleType:SetScript("OnClick", function()
        Cycle(ui.ruleType, #RULE_TYPES, "ruleType")
    end)

    ui.ruleQual = CreateFrame("Button", nil, editPane)
    ui.ruleQual:SetSize(220, 18)
    ui.ruleQual:SetPoint("TOPLEFT", 0, -42)
    StyleBtn(ui.ruleQual, 0.14, 0.14, 0.14)
    ui.ruleQual.label = Font(ui.ruleQual, 10, 0.9, 0.9, 0.9)
    ui.ruleQual.label:SetPoint("CENTER", 0, 0)
    ui.ruleQual.label:SetJustifyH("CENTER")
    ui.ruleQual.label:SetText("Grey")
    ui.ruleQual:SetScript("OnClick", function()
        Cycle(ui.ruleQual, #RULE_QUALS, "ruleQual")
    end)
    ui.ruleQual:Hide()

    ui.ruleNameWrap = CreateFrame("Frame", nil, editPane)
    ui.ruleNameWrap:SetSize(220, 20)
    ui.ruleNameWrap:SetPoint("TOPLEFT", 0, -42)
    ui.ruleNameWrap:SetBackdrop({
        bgFile = WHITE,
        edgeFile = WHITE,
        edgeSize = 1,
        insets = { left = 1, right = 1, top = 1, bottom = 1 },
    })
    ui.ruleNameWrap:SetBackdropColor(0.08, 0.08, 0.08, 1)
    ui.ruleNameWrap:SetBackdropBorderColor(0.22, 0.22, 0.22, 1)
    ui.ruleName = CreateFrame("EditBox", nil, ui.ruleNameWrap)
    ui.ruleName:SetPoint("TOPLEFT", 4, -2)
    ui.ruleName:SetPoint("BOTTOMRIGHT", -4, 2)
    ui.ruleName:SetFont("Fonts\\FRIZQT__.TTF", 11, "")
    ui.ruleName:SetTextColor(0.9, 0.9, 0.9, 1)
    ui.ruleName:SetAutoFocus(false)
    ui.ruleName:SetMaxLetters(36)
    ui.ruleName:SetText("Book of Glyph Mastery")
    ui.ruleName:SetScript("OnTextChanged", function()
        LayoutLoot()
    end)
    ui.ruleNameWrap:Hide()

    local thenLbl = Font(editPane, 10, 0.55, 0.55, 0.55)
    thenLbl:SetPoint("TOPLEFT", 0, -68)
    thenLbl:SetText("Then")

    ui.ruleThen = CreateFrame("Button", nil, editPane)
    ui.ruleThen:SetSize(120, 18)
    ui.ruleThen:SetPoint("TOPLEFT", 36, -66)
    StyleBtn(ui.ruleThen, 0.14, 0.14, 0.14)
    ui.ruleThen.label = Font(ui.ruleThen, 10, 0.9, 0.9, 0.9)
    ui.ruleThen.label:SetPoint("CENTER", 0, 0)
    ui.ruleThen.label:SetJustifyH("CENTER")
    ui.ruleThen.label:SetText("Bags")
    ui.ruleThen:SetScript("OnClick", function()
        Cycle(ui.ruleThen, #ACTION_NAMES, "ruleAction")
    end)

    local addBtn = CreateFrame("Button", nil, editPane)
    addBtn:SetSize(80, 18)
    addBtn:SetPoint("TOPLEFT", 0, -92)
    StyleBtn(addBtn, 0.14, 0.22, 0.14)
    addBtn.label = Font(addBtn, 10, 0.85, 0.95, 0.85)
    addBtn.label:SetPoint("CENTER", 0, 0)
    addBtn.label:SetJustifyH("CENTER")
    addBtn.label:SetText("Add rule")
    addBtn:SetScript("OnClick", function()
        local field = db.ruleField
        local op = db.ruleOp
        local action = db.ruleAction - 1
        local rule = { match = 0, action = action, negate = op == 2 and 1 or 0, quality = 0, text = "" }
        if field == 1 then
            rule.match = RULE_TYPE_MATCH[db.ruleType]
        elseif field == 2 then
            rule.match = 6
            rule.quality = db.ruleQual - 1
        else
            rule.match = 5
            rule.text = string.gsub(ui.ruleName:GetText() or "", "|", "")
            if rule.text == "" then
                return
            end
        end
        InsertRule(rule)
    end)

    ui.rulePreview = Font(editPane, 10, 0.7, 0.7, 0.7)
    ui.rulePreview:SetPoint("TOPLEFT", 0, -116)
    ui.rulePreview:SetWidth(240)
    ui.rulePreview:SetText("Type == Quest -> Bags")

    local expBtn = CreateFrame("Button", nil, loot)
    expBtn:SetSize(64, 18)
    expBtn:SetPoint("BOTTOMRIGHT", -148, 10)
    StyleBtn(expBtn, 0.14, 0.14, 0.14)
    expBtn.label = Font(expBtn, 10, 0.9, 0.9, 0.9)
    expBtn.label:SetPoint("CENTER", 0, 0)
    expBtn.label:SetJustifyH("CENTER")
    expBtn.label:SetText("Export")
    expBtn:SetScript("OnClick", function()
        if ui.shareBox then
            ui.shareBox:SetText(ExportRules())
            ui.shareBox:HighlightText()
            ui.shareBox:SetFocus()
            ui.shareWrap:Show()
            ui.shareHint:SetText("Copy this list. Close when done.")
        end
    end)

    local impBtn = CreateFrame("Button", nil, loot)
    impBtn:SetSize(64, 18)
    impBtn:SetPoint("BOTTOMRIGHT", -80, 10)
    StyleBtn(impBtn, 0.14, 0.14, 0.14)
    impBtn.label = Font(impBtn, 10, 0.9, 0.9, 0.9)
    impBtn.label:SetPoint("CENTER", 0, 0)
    impBtn.label:SetJustifyH("CENTER")
    impBtn.label:SetText("Import")
    impBtn:SetScript("OnClick", function()
        if ui.shareBox then
            ui.shareBox:SetText("")
            ui.shareBox:SetFocus()
            ui.shareWrap:Show()
            ui.shareHint:SetText("Paste rules, then click Apply.")
        end
    end)

    local resetBtn = CreateFrame("Button", nil, loot)
    resetBtn:SetSize(64, 18)
    resetBtn:SetPoint("BOTTOMRIGHT", -12, 10)
    StyleBtn(resetBtn, 0.42, 0.12, 0.12)
    resetBtn.label = Font(resetBtn, 10, 0.95, 0.8, 0.8)
    resetBtn.label:SetPoint("CENTER", 0, 0)
    resetBtn.label:SetJustifyH("CENTER")
    resetBtn.label:SetText("Reset")
    resetBtn:SetScript("OnClick", function()
        SendLine("RULERESET")
    end)

    ui.shareWrap = CreateFrame("Frame", nil, loot)
    ui.shareWrap:SetPoint("BOTTOMLEFT", 10, 34)
    ui.shareWrap:SetPoint("BOTTOMRIGHT", -10, 34)
    ui.shareWrap:SetHeight(72)
    ui.shareWrap:SetBackdrop({
        bgFile = WHITE,
        edgeFile = WHITE,
        edgeSize = 1,
        insets = { left = 1, right = 1, top = 1, bottom = 1 },
    })
    ui.shareWrap:SetBackdropColor(0.08, 0.08, 0.08, 1)
    ui.shareWrap:SetBackdropBorderColor(0.22, 0.22, 0.22, 1)
    ui.shareWrap:Hide()
    ui.shareHint = Font(ui.shareWrap, 10, 0.55, 0.55, 0.55)
    ui.shareHint:SetPoint("TOPLEFT", 6, -4)
    ui.shareHint:SetText("Copy this list. Close when done.")
    ui.shareBox = CreateFrame("EditBox", nil, ui.shareWrap)
    ui.shareBox:SetPoint("TOPLEFT", 6, -18)
    ui.shareBox:SetPoint("BOTTOMRIGHT", -52, 6)
    ui.shareBox:SetFont("Fonts\\FRIZQT__.TTF", 11, "")
    ui.shareBox:SetTextColor(0.9, 0.9, 0.9, 1)
    ui.shareBox:SetAutoFocus(false)
    ui.shareBox:SetMultiLine(true)
    ui.shareBox:SetMaxLetters(800)
    local shareClose = CreateFrame("Button", nil, ui.shareWrap)
    shareClose:SetSize(40, 16)
    shareClose:SetPoint("TOPRIGHT", -6, -4)
    StyleBtn(shareClose, 0.14, 0.14, 0.14)
    shareClose.label = Font(shareClose, 10, 0.9, 0.9, 0.9)
    shareClose.label:SetPoint("CENTER", 0, 0)
    shareClose.label:SetJustifyH("CENTER")
    shareClose.label:SetText("Close")
    shareClose:SetScript("OnClick", function()
        ui.shareWrap:Hide()
    end)
    local shareApply = CreateFrame("Button", nil, ui.shareWrap)
    shareApply:SetSize(44, 16)
    shareApply:SetPoint("TOPRIGHT", -50, -4)
    StyleBtn(shareApply, 0.14, 0.22, 0.14)
    shareApply.label = Font(shareApply, 10, 0.85, 0.95, 0.85)
    shareApply.label:SetPoint("CENTER", 0, 0)
    shareApply.label:SetJustifyH("CENTER")
    shareApply.label:SetText("Apply")
    shareApply:SetScript("OnClick", function()
        local text = ui.shareBox:GetText() or ""
        local parsed = {}
        for line in string.gmatch(text .. "\n", "(.-)\n") do
            local rule = ParseImportLine(line)
            if rule then
                table.insert(parsed, rule)
            end
        end
        if #parsed == 0 then
            return
        end
        ReplaceRules(parsed)
        ui.shareWrap:Hide()
    end)

    ui.reagents, ui.reagentRows, ui.reagentEmpty, ui.reagentHint = MakeVaultPanel(f, VAULT_REAGENT, true)

    local class = MakePanel(f)
    ui.class = class
    ui.classHint = Font(class, 11, 0.7, 0.7, 0.7)
    ui.classHint:SetPoint("TOPLEFT", 10, -6)
    ui.classHint:SetWidth(520)
    ui.classHint:SetText("Pick one. Each talent spec can have its own pick.")
    ui.classEmpty = Font(class, 11, 0.6, 0.6, 0.6)
    ui.classEmpty:SetPoint("TOPLEFT", 10, -48)
    ui.classEmpty:SetText("No class perk for your class yet.")
    ui.classEmpty:Hide()
    ui.classBtns = {}
    for i = 1, 3 do
        local btn = CreateFrame("Button", nil, class)
        btn:SetSize(164, 28)
        btn:SetPoint("TOPLEFT", 10 + (i - 1) * 172, -40)
        StyleBtn(btn, 0.14, 0.14, 0.14)
        btn.label = Font(btn, 12, 0.9, 0.9, 0.9)
        btn.label:SetPoint("CENTER", 0, 0)
        btn.label:SetJustifyH("CENTER")
        btn:SetScript("OnClick", function()
            if btn.id then
                SendLine("CLASS|" .. tostring(btn.id))
            end
        end)
        btn:SetScript("OnEnter", function(self)
            Solid(self.bg, math.min(1, self._ir + 0.08), math.min(1, self._ig + 0.08), math.min(1, self._ib + 0.08), 1)
            if self.how then
                ui.classHint:SetText(self.how)
            end
        end)
        btn:SetScript("OnLeave", function(self)
            Solid(self.bg, self._ir, self._ig, self._ib, 1)
            LayoutClass()
        end)
        ui.classBtns[i] = btn
    end

    local world = MakePanel(f)
    ui.world = world
    ui.worldTip = Font(world, 10, 0.7, 0.7, 0.7)
    ui.worldTip:SetPoint("BOTTOMLEFT", 10, 8)
    ui.worldTip:SetPoint("BOTTOMRIGHT", -10, 8)
    ui.worldTip:SetHeight(28)
    ui.worldTip:SetText("Hover a mark for how to unlock.")

    local clip = CreateFrame("ScrollFrame", nil, world)
    clip:SetPoint("TOPLEFT", 4, -4)
    clip:SetPoint("BOTTOMRIGHT", -18, 38)
    clip:EnableMouse(true)
    clip:EnableMouseWheel(true)
    ui.worldClip = clip

    local content = CreateFrame("Frame", nil, clip)
    content:SetWidth(508)
    content:SetHeight(400)
    clip:SetScrollChild(content)
    ui.worldContent = content

    local bar = CreateFrame("Frame", nil, world)
    bar:SetWidth(10)
    bar:SetPoint("TOPRIGHT", -6, -4)
    bar:SetPoint("BOTTOMRIGHT", -6, 38)
    bar.bg = bar:CreateTexture(nil, "BACKGROUND")
    bar.bg:SetAllPoints(bar)
    Solid(bar.bg, 0.10, 0.10, 0.10, 1)
    ui.worldBar = bar
    local thumb = CreateFrame("Frame", nil, bar)
    thumb:SetWidth(10)
    thumb:SetHeight(32)
    thumb:SetPoint("TOP", bar, "TOP", 0, 0)
    thumb.bg = thumb:CreateTexture(nil, "ARTWORK")
    thumb.bg:SetAllPoints(thumb)
    Solid(thumb.bg, 0.22, 0.22, 0.22, 1)
    ui.worldThumb = thumb
    clip:SetScript("OnMouseWheel", function(self, delta)
        local cur = self:GetVerticalScroll() or 0
        local next = cur - delta * 28
        if next < 0 then
            next = 0
        end
        local max = self:GetVerticalScrollRange() or 0
        if next > max then
            next = max
        end
        self:SetVerticalScroll(next)
        UpdateWorldScroll()
    end)

    ui.worldUnlocks = {}
    for i = 1, #WORLD_UNLOCKS do
        local info = WORLD_UNLOCKS[i]
        local col = (i - 1) % 4
        local row = math.floor((i - 1) / 4)
        local btn = CreateFrame("Button", nil, content)
        btn:SetSize(124, 18)
        btn:SetPoint("TOPLEFT", 6 + col * 128, -6 - row * 22)
        StyleBtn(btn, 0.12, 0.12, 0.12)
        btn.label = Font(btn, 10, 0.85, 0.85, 0.85)
        btn.label:SetPoint("CENTER", 0, 0)
        btn.label:SetJustifyH("CENTER")
        btn.label:SetText(info.name)
        btn:SetScript("OnEnter", function(self)
            Solid(self.bg, math.min(1, self._ir + 0.08), math.min(1, self._ig + 0.08), math.min(1, self._ib + 0.08), 1)
            SetWorldTip(self.tip)
        end)
        btn:SetScript("OnLeave", function(self)
            Solid(self.bg, self._ir, self._ig, self._ib, 1)
            SetWorldTip()
        end)
        if info.toggle then
            btn:SetScript("OnClick", function()
                if not PerkKnown(info.id) then
                    return
                end
                SendWorldToggle(info)
            end)
        else
            btn:SetScript("OnClick", function()
                if not PerkKnown(info.id) then
                    return
                end
                if info.id == 910091 then
                    local name = GetSpellInfo(910091)
                    if name then
                        CastSpellByName(name)
                    else
                        SendLine("ARMOPEN")
                    end
                    return
                end
                local name = GetSpellInfo(info.id)
                if name then
                    CastSpellByName(name)
                end
            end)
        end
        ui.worldUnlocks[i] = btn
    end

    ui.worldTracks = {}
    local unlockRows = math.ceil(#WORLD_UNLOCKS / 4)
    local trackY = -6 - unlockRows * 22 - 8
    for t = 1, #WORLD_TRACKS do
        local track = WORLD_TRACKS[t]
        local row = CreateFrame("Frame", nil, content)
        row:SetSize(500, 20)
        row:SetPoint("TOPLEFT", 6, trackY)
        row.head = Font(row, 10, 0.4, 0.8, 1)
        row.head:SetPoint("LEFT", 0, 0)
        row.head:SetWidth(168)
        row.ticks = {}
        local n = #track.ticks
        for i = 1, n do
            local tick = CreateFrame("Button", nil, row)
            tick:SetSize(18, 16)
            tick:SetPoint("LEFT", 172 + (i - 1) * 22, 0)
            tick.bg = tick:CreateTexture(nil, "BACKGROUND")
            tick.bg:SetAllPoints(tick)
            Solid(tick.bg, 0.11, 0.11, 0.11, 1)
            tick.mark = Font(tick, 10, 0.55, 0.95, 0.6)
            tick.mark:SetPoint("CENTER", 0, 0)
            tick.mark:SetJustifyH("CENTER")
            tick:SetScript("OnEnter", function(self)
                PaintTick(self, PerkKnown(track.ticks[i].id))
                Solid(self.bg, math.min(1, self._ir + 0.08), math.min(1, self._ig + 0.08), math.min(1, self._ib + 0.08), 1)
                SetWorldTip(self.tip)
            end)
            tick:SetScript("OnLeave", function(self)
                PaintTick(self, PerkKnown(track.ticks[i].id))
                SetWorldTip()
            end)
            row.ticks[i] = tick
        end
        ui.worldTracks[t] = row
        trackY = trackY - 24
    end

    ui.jumpLabel = Font(content, 10, 0.7, 0.7, 0.7)
    ui.jumpLabel:SetPoint("TOPLEFT", 6, trackY - 4)
    ui.jumpLabel:SetText("Jump")
    ui.jumpBtns = {}
    local jumpModes = {
        { mode = 0, name = "Off", how = "Normal jump height." },
        { mode = 1, name = "Double", how = "Jumps go twice as high and far." },
        { mode = 2, name = "Triple", how = "Jumps go three times as high and far." },
    }
    for i = 1, #jumpModes do
        local info = jumpModes[i]
        local btn = CreateFrame("Button", nil, content)
        btn:SetSize(72, 18)
        btn:SetPoint("TOPLEFT", 46 + (i - 1) * 80, trackY - 6)
        StyleBtn(btn, 0.14, 0.14, 0.14)
        btn.mode = info.mode
        btn.how = info.how
        btn.label = Font(btn, 10, 0.9, 0.9, 0.9)
        btn.label:SetPoint("CENTER", 0, 0)
        btn.label:SetJustifyH("CENTER")
        btn.label:SetText(info.name)
        btn:SetScript("OnClick", function()
            local max = tonumber(db.jump.max) or 0
            if info.mode > 0 and max < info.mode then
                return
            end
            SendLine("JMPSET|" .. tostring(info.mode))
        end)
        btn:SetScript("OnEnter", function(self)
            Solid(self.bg, math.min(1, self._ir + 0.08), math.min(1, self._ig + 0.08), math.min(1, self._ib + 0.08), 1)
            SetWorldTip(self.tip or self.how)
        end)
        btn:SetScript("OnLeave", function(self)
            Solid(self.bg, self._ir, self._ig, self._ib, 1)
            SetWorldTip()
        end)
        ui.jumpBtns[i] = btn
    end

    ui.chatLabel = Font(content, 10, 0.7, 0.7, 0.7)
    ui.chatLabel:SetPoint("TOPLEFT", 6, trackY - 30)
    ui.chatLabel:SetText("Chat")
    ui.chatToggle = CreateFrame("Button", nil, content)
    ui.chatToggle:SetSize(220, 18)
    ui.chatToggle:SetPoint("TOPLEFT", 46, trackY - 32)
    StyleBtn(ui.chatToggle, 0.32, 0.12, 0.12)
    ui.chatToggle.label = Font(ui.chatToggle, 10, 0.9, 0.95, 0.9)
    ui.chatToggle.label:SetPoint("CENTER", 0, 0)
    ui.chatToggle.label:SetJustifyH("CENTER")
    ui.chatToggle.label:SetText("Show Living Gear chat: OFF")
    ui.chatToggle.tip = "Show Living Gear progress and unlock messages in chat. Errors always show."
    ui.chatToggle:SetScript("OnClick", function()
        SetShowChat(not LivingGearDB.showChat)
    end)
    ui.chatToggle:SetScript("OnEnter", function(self)
        Solid(self.bg, math.min(1, self._ir + 0.08), math.min(1, self._ig + 0.08), math.min(1, self._ib + 0.08), 1)
        SetWorldTip(self.tip)
    end)
    ui.chatToggle:SetScript("OnLeave", function(self)
        Solid(self.bg, self._ir, self._ig, self._ib, 1)
        SetWorldTip()
    end)

    ui.speedLabel = Font(content, 10, 0.7, 0.7, 0.7)
    ui.speedLabel:SetPoint("TOPLEFT", 6, trackY - 54)
    ui.speedLabel:SetText("Speed")
    ui.speedCap = CreateFrame("Button", nil, content)
    ui.speedCap:SetSize(220, 18)
    ui.speedCap:SetPoint("TOPLEFT", 46, trackY - 56)
    StyleBtn(ui.speedCap, 0.14, 0.14, 0.22)
    ui.speedCap.label = Font(ui.speedCap, 10, 0.9, 0.9, 0.95)
    ui.speedCap.label:SetPoint("CENTER", 0, 0)
    ui.speedCap.label:SetJustifyH("CENTER")
    ui.speedCap.label:SetText("Speed cap: 500%")
    ui.speedCap.tip = "Movement speed bonuses add together. Hard cap 500%. Click to lower."
    ui.speedCap:SetScript("OnClick", function()
        local cap = tonumber(db.speedCap) or 500
        cap = cap + 50
        if cap > 500 then
            cap = 100
        end
        db.speedCap = cap
        SendLine("SCAP|" .. tostring(cap))
        LayoutWorld()
    end)
    ui.speedCap:SetScript("OnEnter", function(self)
        Solid(self.bg, math.min(1, self._ir + 0.08), math.min(1, self._ig + 0.08), math.min(1, self._ib + 0.08), 1)
        SetWorldTip(self.tip)
    end)
    ui.speedCap:SetScript("OnLeave", function(self)
        Solid(self.bg, self._ir, self._ig, self._ib, 1)
        SetWorldTip()
    end)

    content._h = math.abs(trackY) + 100
    content:SetHeight(content._h)

    SaveScale(LoadScale())
    ShowTab("world")
    LayoutAll()
end

local function OpenWindow()
    BuildUI()
    ui.frame:Show()
    RequestSync()
end

local function Toggle()
    BuildUI()
    if ui.frame:IsShown() then
        ui.frame:Hide()
    else
        OpenWindow()
    end
end

local function IsAccountPerksName(name)
    if not name or name == "" then
        return false
    end
    local want = GetSpellInfo(ACCOUNT_PERKS_ID)
    if want and name == want then
        return true
    end
    return string.find(name, "Account Perks", 1, true)
        or string.find(name, "Windblown", 1, true)
end

local lastCastOpen = 0
local function OpenFromCast()
    local now = GetTime()
    if (now - lastCastOpen) < 0.5 then
        return
    end
    lastCastOpen = now
    Toggle()
end

local function RefreshQuestWatch()
    if type(WatchFrame_Update) == "function" then
        pcall(WatchFrame_Update, WatchFrame)
    end
    if type(QuestLog_Update) == "function" then
        pcall(QuestLog_Update)
    end
    if type(QuestWatch_Update) == "function" then
        pcall(QuestWatch_Update)
    end
end

local function StripColors(text)
    text = string.gsub(text or "", "|c%x%x%x%x%x%x%x%x", "")
    text = string.gsub(text, "|r", "")
    text = string.gsub(text, "^%s+", "")
    text = string.gsub(text, "%s+$", "")
    return text
end

local function NormName(text)
    text = string.lower(StripColors(text))
    text = string.gsub(text, "[^%w]+", " ")
    text = string.gsub(text, "%s+", " ")
    text = string.gsub(text, "^%s+", "")
    text = string.gsub(text, "%s+$", "")
    return text
end

local function NamesMatch(a, b)
    a = NormName(a)
    b = NormName(b)
    if a == "" or b == "" then
        return false
    end
    if a == b then
        return true
    end
    if string.find(a, b, 1, true) or string.find(b, a, 1, true) then
        return true
    end
    return false
end

local function VaultCountForEntry(entry)
    entry = tonumber(entry) or 0
    if entry == 0 then
        return 0
    end
    local n = 0
    local vault = db.vault or {}
    for i = 1, #vault do
        local row = vault[i]
        if tonumber(row.kind) == VAULT_QUEST and tonumber(row.entry) == entry then
            n = n + (tonumber(row.count) or 0)
        end
    end
    return n
end

local function VaultCountForName(name)
    local n = 0
    local vault = db.vault or {}
    for i = 1, #vault do
        local row = vault[i]
        if tonumber(row.kind) == VAULT_QUEST then
            local entry = tonumber(row.entry) or 0
            local iname = entry > 0 and GetItemInfo(entry)
            if NamesMatch(name, row.name) or (iname and NamesMatch(name, iname)) then
                n = n + (tonumber(row.count) or 0)
            end
        end
    end
    return n
end

local function ParseLeaderBoard(text)
    text = StripColors(text)
    local name, cur, need = string.match(text, "^(.-):%s*(%d+)%s*/%s*(%d+)%s*$")
    if name then
        return StripColors(name), tonumber(cur), tonumber(need)
    end
    cur, need, name = string.match(text, "^(%d+)%s*/%s*(%d+)%s+(.+)$")
    if name then
        return StripColors(name), tonumber(cur), tonumber(need)
    end
    return nil
end

local origGetItemCount
local questTrackerHooked = false

local function BagCountForName(name)
    if type(origGetItemCount) ~= "function" or not name or name == "" then
        return 0
    end
    return tonumber(origGetItemCount(name)) or 0
end

local function DesiredHave(name, need)
    local total = BagCountForName(name) + VaultCountForName(name)
    if need and need > 0 and total > need then
        total = need
    end
    return total
end

local function ApplyVaultCountToText(text)
    local name, cur, need = ParseLeaderBoard(text)
    if not name then
        return text
    end
    cur = cur or 0
    need = need or 0
    local total = DesiredHave(name, need)
    if total == cur then
        return text
    end
    if string.match(StripColors(text), "^%d+%s*/%s*%d+") then
        return tostring(total) .. "/" .. tostring(need) .. " " .. name
    end
    return name .. ": " .. tostring(total) .. "/" .. tostring(need)
end

local function PatchWatchFrameLines()
    if type(WATCHFRAME_QUESTLINES) ~= "table" then
        return
    end
    for _, line in pairs(WATCHFRAME_QUESTLINES) do
        if line and line.text and line.text.GetText then
            local text = line.text:GetText()
            if text and text ~= "" then
                local patched = ApplyVaultCountToText(text)
                if patched ~= text then
                    line.text:SetText(patched)
                end
            end
        end
    end
end

local function UpsertVault(kind, entry, count, name)
    kind = tonumber(kind) or 0
    entry = tonumber(entry) or 0
    count = tonumber(count) or 0
    name = name or "Item"
    for i = 1, #db.vault do
        if db.vault[i].kind == kind and db.vault[i].entry == entry then
            if count <= 0 then
                table.remove(db.vault, i)
            else
                db.vault[i].count = count
                db.vault[i].name = name
            end
            return
        end
    end
    if count > 0 then
        table.insert(db.vault, {
            kind = kind,
            entry = entry,
            count = count,
            name = name,
        })
    end
end

local function HookQuestTracker()
    if questTrackerHooked then
        return
    end
    questTrackerHooked = true
    origGetItemCount = origGetItemCount or GetItemCount
    local origBoard = GetQuestLogLeaderBoard
    if type(origBoard) == "function" then
        GetQuestLogLeaderBoard = function(i, questIndex)
            local text, typ, finished = origBoard(i, questIndex)
            if text and (typ == "item" or typ == "object" or not typ) then
                text = ApplyVaultCountToText(text)
            end
            return text, typ, finished
        end
    end
    local origItemInfo = GetQuestItemInfo
    if type(origItemInfo) == "function" then
        GetQuestItemInfo = function(typ, idx)
            local name, tex, num, qual, usable = origItemInfo(typ, idx)
            if typ == "required" and name then
                local extra = 0
                local link = GetQuestItemLink and GetQuestItemLink(typ, idx)
                local entry = link and tonumber(string.match(link, "item:(%d+)"))
                if entry then
                    extra = VaultCountForEntry(entry)
                else
                    extra = VaultCountForName(name)
                end
                num = (tonumber(num) or 0) + extra
            end
            return name, tex, num, qual, usable
        end
    end
    local origCompletable = IsQuestCompletable
    if type(origCompletable) == "function" then
        IsQuestCompletable = function()
            if origCompletable() then
                return 1
            end
            local n = GetNumQuestItems and GetNumQuestItems() or 0
            if n == 0 then
                return nil
            end
            for i = 1, n do
                local link = GetQuestItemLink and GetQuestItemLink("required", i)
                local name, _, need = origItemInfo and origItemInfo("required", i)
                need = tonumber(need) or 1
                local entry = link and tonumber(string.match(link, "item:(%d+)"))
                local have = 0
                if entry then
                    have = (origCount and origCount(entry) or 0) + VaultCountForEntry(entry)
                else
                    have = VaultCountForName(name)
                end
                if have < need then
                    return nil
                end
            end
            return 1
        end
    end
    if type(hooksecurefunc) == "function" and type(WatchFrame_Update) == "function" then
        hooksecurefunc("WatchFrame_Update", PatchWatchFrameLines)
    end
end

HookQuestTracker()

local jumpHooked = false
local jumpResumeUntil = 0
local jumpWasFalling = false
local jumpResumeBurst = 0
local jumpSawFalling = false
local jumpNeedReset = false
local jumpHeld = {
    fwd = false,
    back = false,
    sleft = false,
    sright = false,
    tleft = false,
    tright = false,
}

local function KeyHeld(key)
    if not key or not IsKeyDown then
        return false
    end
    if IsKeyDown(key) then
        return true
    end
    local tail = key:match("%-(.+)$")
    return tail and IsKeyDown(tail)
end

local function BindingHeld(cmd)
    if not GetBindingKey then
        return false
    end
    local k1, k2 = GetBindingKey(cmd)
    return KeyHeld(k1) or KeyHeld(k2)
end

local function DirHeld(cmd, keys)
    if BindingHeld(cmd) then
        return true
    end
    if not IsKeyDown or not keys then
        return false
    end
    for i = 1, #keys do
        if IsKeyDown(keys[i]) then
            return true
        end
    end
    return false
end

local function PulseDir(held, startFn, stopFn, reset)
    if not held or not startFn then
        return
    end
    -- Knockback land keeps a stale "key is down" flag while speed is zero.
    -- Stop then Start forces a fresh CMSG_MOVE_START_*.
    if reset and stopFn then
        stopFn()
    end
    startFn()
end

local function SnapshotMoveKeys()
    jumpHeld.fwd = DirHeld("MOVEFORWARD", { "W", "UP" })
    jumpHeld.back = DirHeld("MOVEBACKWARD", { "S", "DOWN" })
    jumpHeld.sleft = DirHeld("STRAFELEFT", { "Q" })
    jumpHeld.sright = DirHeld("STRAFERIGHT", { "E" })
    jumpHeld.tleft = DirHeld("TURNLEFT", { "A", "LEFT" })
    jumpHeld.tright = DirHeld("TURNRIGHT", { "D", "RIGHT" })
end

local function ResumeMoveKeys(reset)
    PulseDir(jumpHeld.fwd, MoveForwardStart, MoveForwardStop, reset)
    PulseDir(jumpHeld.back, MoveBackwardStart, MoveBackwardStop, reset)
    PulseDir(jumpHeld.sleft, StrafeLeftStart, StrafeLeftStop, reset)
    PulseDir(jumpHeld.sright, StrafeRightStart, StrafeRightStop, reset)
    PulseDir(jumpHeld.tleft, TurnLeftStart, TurnLeftStop, reset)
    PulseDir(jumpHeld.tright, TurnRightStart, TurnRightStop, reset)
end

local function BeginJumpResume()
    jumpNeedReset = true
    jumpResumeBurst = 45
    jumpResumeUntil = GetTime() + 1.5
    ResumeMoveKeys(true)
end

local function HookJump()
    if jumpHooked or not JumpOrAscendStart then
        return
    end
    jumpHooked = true
    -- Knockback land zeros speed and leaves a stale key-down flag. Snapshot
    -- keys at jump time and replay Stop+Start from that snapshot on land.
    hooksecurefunc("JumpOrAscendStart", function()
        if IsFlying() or UnitOnTaxi("player") or IsSwimming() then
            return
        end
        SnapshotMoveKeys()
        jumpResumeUntil = GetTime() + 8
        jumpSawFalling = IsFalling() and true or false
    end)
    local f = CreateFrame("Frame")
    f:SetScript("OnUpdate", function()
        local now = GetTime()
        local falling = IsFalling() and true or false
        if now < jumpResumeUntil and falling then
            jumpSawFalling = true
        end
        if jumpSawFalling and jumpWasFalling and not falling and now < jumpResumeUntil then
            BeginJumpResume()
            jumpSawFalling = false
        end
        if jumpResumeBurst > 0 then
            ResumeMoveKeys(jumpNeedReset)
            jumpNeedReset = false
            jumpResumeBurst = jumpResumeBurst - 1
        end
        jumpWasFalling = falling
    end)
end
HookJump()

local comboHud
local function ShowCombo(stacks, seconds)
    stacks = tonumber(stacks) or 0
    seconds = tonumber(seconds) or 180
    if not comboHud then
        comboHud = CreateFrame("Frame", "LivingGearCombo", UIParent)
        comboHud:SetFrameStrata("HIGH")
        comboHud:SetSize(140, 24)
        comboHud:SetPoint("TOP", UIParent, "TOP", 0, -36)
        local bg = comboHud:CreateTexture(nil, "BACKGROUND")
        bg:SetAllPoints()
        Solid(bg, 0.07, 0.07, 0.07, 0.85)
        comboHud.text = Font(comboHud, 14, 1, 0.82, 0.2)
        comboHud.text:SetPoint("CENTER")
        comboHud.text:SetJustifyH("CENTER")
        comboHud:SetScript("OnUpdate", function(self)
            if self.expire and GetTime() >= self.expire then
                self:Hide()
                self.expire = nil
            end
        end)
    end
    if stacks < 1 then
        comboHud:Hide()
        comboHud.expire = nil
        return
    end
    comboHud.text:SetText("Combo x" .. stacks)
    comboHud.expire = GetTime() + seconds
    comboHud:Show()
end

local dungeonHud
local function FormatTimer(sec)
    sec = math.max(0, math.floor(tonumber(sec) or 0))
    return string.format("%d:%02d", math.floor(sec / 60), sec % 60)
end

local function ShowDungeonTimer(mode, parSec, clearSec, tier, speedPct, pacePct)
    if not dungeonHud then
        dungeonHud = CreateFrame("Frame", "LivingGearDungeonTimer", UIParent)
        dungeonHud:SetFrameStrata("HIGH")
        dungeonHud:SetSize(180, 22)
        dungeonHud:SetPoint("TOP", UIParent, "TOP", 0, -60)
        local bg = dungeonHud:CreateTexture(nil, "BACKGROUND")
        bg:SetAllPoints()
        Solid(bg, 0.07, 0.07, 0.07, 0.85)
        dungeonHud.text = Font(dungeonHud, 12, 0.75, 0.88, 1)
        dungeonHud.text:SetPoint("CENTER")
        dungeonHud.text:SetJustifyH("CENTER")
        dungeonHud:SetScript("OnUpdate", function(self)
            if self.mode == "run" and self.startAt and self.parSec then
                local elapsed = math.max(0, math.floor(GetTime() - self.startAt))
                self.text:SetText(string.format("Dungeon %s / %s", FormatTimer(elapsed), FormatTimer(self.parSec)))
            end
        end)
    end
    if mode == "stop" then
        dungeonHud.mode = nil
        dungeonHud.startAt = nil
        dungeonHud.parSec = nil
        dungeonHud:Hide()
        return
    end
    if mode == "start" then
        parSec = tonumber(parSec) or 1800
        dungeonHud.mode = "run"
        dungeonHud.startAt = GetTime()
        dungeonHud.parSec = parSec
        dungeonHud.text:SetText(string.format("Dungeon 0:00 / %s", FormatTimer(parSec)))
        dungeonHud:Show()
        return
    end
    if mode == "clear" then
        clearSec = tonumber(clearSec) or 0
        tier = tonumber(tier) or 0
        speedPct = tonumber(speedPct) or 0
        pacePct = tonumber(pacePct) or 0
        local tierName = "Clear"
        if tier == 3 then tierName = "Gold"
        elseif tier == 2 then tierName = "Silver"
        elseif tier == 1 then tierName = "Bronze"
        end
        dungeonHud.mode = "clear"
        dungeonHud.startAt = nil
        if tier > 0 then
            dungeonHud.text:SetText(string.format("%s %s +%d%% spd +%d%% pace", tierName, FormatTimer(clearSec), speedPct, pacePct))
        else
            dungeonHud.text:SetText(string.format("Clear %s (over par)", FormatTimer(clearSec)))
        end
        dungeonHud:Show()
        return
    end
end

local zoneScaleHud
local function ShowZoneScale(eff, real, zone)
    eff = tonumber(eff) or 0
    real = tonumber(real) or 0
    zone = tonumber(zone) or 0
    if eff < 1 or real < 1 or eff >= real then
        if zoneScaleHud then
            zoneScaleHud:Hide()
        end
        return
    end
    if not zoneScaleHud then
        zoneScaleHud = CreateFrame("Frame", "LivingGearZoneScale", UIParent)
        zoneScaleHud:SetFrameStrata("HIGH")
        zoneScaleHud:SetSize(220, 22)
        zoneScaleHud:SetPoint("TOP", UIParent, "TOP", 0, -58)
        local bg = zoneScaleHud:CreateTexture(nil, "BACKGROUND")
        bg:SetAllPoints()
        Solid(bg, 0.07, 0.07, 0.07, 0.85)
        zoneScaleHud.text = Font(zoneScaleHud, 12, 0.75, 0.88, 1)
        zoneScaleHud.text:SetPoint("CENTER")
        zoneScaleHud.text:SetJustifyH("CENTER")
    end
    zoneScaleHud.text:SetText(string.format("Effective %d | Rewards %d (zone ~%d)", eff, real, zone))
    zoneScaleHud:Show()
end

local function HandleAddon(prefix, message)
    if prefix ~= PREFIX or not message then
        return
    end
    if message == "OPEN" then
        OpenFromCast()
        return
    end
    if message == "LAND" then
        BeginJumpResume()
        return
    end
    if message == "BANKOPEN" then
        ShowBankDeposit()
        RequestSync()
        return
    end
    if message == "CLR" then
        syncing = true
        vaultLayoutPending = false
        db.items = {}
        db.byKey = {}
        db.asked = {}
        db.perks = {}
        db.classPerks = {}
        db.classPerk = 0
        db.rules = {}
        db.vault = {}
        db.armory = {}
        db.attune = { on = 1, count = 0, off = 0 }
        db.jump = { mode = 2, max = 0 }
        db.solo = 0
        db.autoMount = 0
        db.speedCap = db.speedCap or 500
        if ui.reagents and ui.reagents:IsShown() then
            RefreshVaultPanel()
        end
        return
    end
    if message == "END" then
        syncing = false
        vaultLayoutPending = false
        BuildUI()
        LayoutRows()
        RefreshOverlays()
        RefreshQuestWatch()
        return
    end
    local p = SplitPipe(message)
    if p[1] == "PK" then
        db.perks[tonumber(p[2]) or 0] = tonumber(p[3]) or 0
        return
    end
    if p[1] == "CPK" then
        local id = tonumber(p[2]) or 0
        if id > 0 then
            table.insert(db.classPerks, id)
            if (tonumber(p[3]) or 0) == 1 then
                db.classPerk = id
            end
        end
        return
    end
    if p[1] == "AL" then
        db.autoloot.on = tonumber(p[2]) or 0
        db.autoloot.corpses = tonumber(p[3]) or 0
        db.autoloot.need = tonumber(p[4]) or 10
        return
    end
    if p[1] == "ALDE" then
        db.autoloot.de = tonumber(p[2]) or 0
        return
    end
    if p[1] == "AA" then
        db.attune.on = tonumber(p[2]) or 0
        db.attune.count = tonumber(p[3]) or 0
        db.attune.off = tonumber(p[4]) or 0
        return
    end
    if p[1] == "JMP" then
        db.jump.mode = tonumber(p[2]) or 0
        db.jump.max = tonumber(p[3]) or 0
        return
    end
    if p[1] == "SQ" then
        db.solo = tonumber(p[2]) or 0
        return
    end
    if p[1] == "AM" then
        db.autoMount = tonumber(p[2]) or 0
        return
    end
    if p[1] == "SCAP" then
        db.speedCap = tonumber(p[2]) or 500
        if ui.speedCap then
            LayoutWorld()
        end
        return
    end
    if p[1] == "COMBO" then
        ShowCombo(tonumber(p[2]) or 0, tonumber(p[4]) or 180)
        return
    end
    if p[1] == "DTIMER" then
        if p[2] == "start" then
            ShowDungeonTimer("start", p[3])
        elseif p[2] == "stop" then
            ShowDungeonTimer("stop")
        elseif p[2] == "clear" then
            ShowDungeonTimer("clear", nil, p[3], p[4], p[5], p[6])
        end
        return
    end
    if p[1] == "ZSCALE" then
        ShowZoneScale(p[2], p[3], p[4])
        return
    end
    if p[1] == "SCALE" then
        db.scale = ClampScale((tonumber(p[2]) or 100) / 100)
        if ui.frame then
            SaveScale(db.scale)
        end
        return
    end
    if p[1] == "RULE" then
        table.insert(db.rules, {
            idx = tonumber(p[2]) or 0,
            match = tonumber(p[3]) or 0,
            action = tonumber(p[4]) or 0,
            negate = tonumber(p[5]) or 0,
            quality = tonumber(p[6]) or 0,
            text = p[7] or "",
        })
        return
    end
    if p[1] == "VLT" then
        local kind = tonumber(p[2]) or 0
        local entry = tonumber(p[3]) or 0
        local count = tonumber(p[4]) or 0
        local name = p[5] or "Item"
        if syncing then
            table.insert(db.vault, {
                kind = kind,
                entry = entry,
                count = count,
                name = name,
            })
            if ui.reagents and ui.reagents:IsShown() then
                vaultLayoutPending = true
            end
            return
        end
        UpsertVault(kind, entry, count, name)
        if ui.frame and ui.frame:IsShown() then
            LayoutRows()
        end
        RefreshQuestWatch()
        return
    end
    if p[1] == "ARMCLR" then
        db.armory = {}
        db.armoryOff = 0
        return
    end
    if p[1] == "ARM" then
        table.insert(db.armory, {
            slot = tonumber(p[2]) or 0,
            entry = tonumber(p[3]) or 0,
            ilvl = tonumber(p[4]) or 0,
            str = tonumber(p[5]) or 0,
            agi = tonumber(p[6]) or 0,
            sta = tonumber(p[7]) or 0,
            intel = tonumber(p[8]) or 0,
            spi = tonumber(p[9]) or 0,
            armor = tonumber(p[10]) or 0,
            name = p[11] or "Item",
        })
        return
    end
    if p[1] == "ARMEND" then
        if ui.armory then
            if db.armorySlot == nil then
                db.armorySlot = -1
            end
            ui.armory:Show()
            LayoutArmory()
            if ui.frame then
                ui.frame:Show()
            end
        end
        return
    end
    if p[1] == "ABS" then
        db.absorb.str = p[2]
        db.absorb.agi = p[3]
        db.absorb.sta = p[4]
        db.absorb.intel = p[5]
        db.absorb.spi = p[6]
        db.absorb.armor = p[7]
        db.absorb.count = p[8]
        return
    end
    if p[1] == "ITM" then
        local it, key
        if p[2] == "inv" then
            it = {
                kind = "inv",
                slot = p[3],
                name = p[4],
                lv = p[5], xp = p[6], need = p[7],
                ds = p[8], da = p[9], dt = p[10], di = p[11], dp = p[12], dar = p[13],
            }
            key = "inv:" .. tostring(p[3])
            if syncing then
                table.insert(db.items, it)
            end
        elseif p[2] == "bag" then
            it = {
                kind = "bag",
                bag = p[3],
                slot = p[4],
                name = p[5],
                lv = p[6], xp = p[7], need = p[8],
                ds = p[9], da = p[10], dt = p[11], di = p[12], dp = p[13], dar = p[14],
            }
            key = "bag:" .. tostring(p[3]) .. ":" .. tostring(p[4])
        else
            it = {
                kind = "inv",
                slot = p[2],
                name = p[3],
                lv = p[4], xp = p[5], need = p[6],
                ds = p[7], da = p[8], dt = p[9], di = p[10], dp = p[11], dar = p[12],
            }
            key = "inv:" .. tostring(p[2])
            if syncing then
                table.insert(db.items, it)
            end
        end
        if key then
            db.byKey[key] = it
        end
        if not syncing then
            RefreshOverlays()
        end
    end
end

local STAT_FIELD = {
    Strength = "ds",
    Agility = "da",
    Stamina = "dt",
    Intellect = "di",
    Spirit = "dp",
    Armor = "dar",
}

local STAT_LABEL = {
    ds = "Strength",
    da = "Agility",
    dt = "Stamina",
    di = "Intellect",
    dp = "Spirit",
    dar = "Armor",
}

local CHAR_SLOTS = {
    "CharacterHeadSlot", "CharacterNeckSlot", "CharacterShoulderSlot",
    "CharacterBackSlot", "CharacterChestSlot", "CharacterShirtSlot",
    "CharacterTabardSlot", "CharacterWristSlot", "CharacterHandsSlot",
    "CharacterWaistSlot", "CharacterLegsSlot", "CharacterFeetSlot",
    "CharacterFinger0Slot", "CharacterFinger1Slot",
    "CharacterTrinket0Slot", "CharacterTrinket1Slot",
    "CharacterMainHandSlot", "CharacterSecondaryHandSlot",
    "CharacterRangedSlot",
}

local function ExtraOf(it, field)
    return math.floor(tonumber(it and it[field]) or 0)
end

local function LevelProgress(it)
    local lv = tonumber(it.lv) or 1
    if lv >= LG_MAX_LEVEL then
        return 1
    end
    local need = tonumber(it.need) or 0
    local xp = tonumber(it.xp) or 0
    local frac = 0
    if need > 0 then
        frac = xp / need
        if frac < 0 then
            frac = 0
        end
        if frac > 1 then
            frac = 1
        end
    end
    local p = ((lv - 1) + frac) / (LG_MAX_LEVEL - 1)
    if p < 0.06 then
        p = 0.06
    end
    return p
end

local function OverlayFor(btn, it)
    if not btn then
        return
    end
    if not btn._lgBar then
        local bar = CreateFrame("StatusBar", nil, btn)
        bar:SetHeight(4)
        local icon = btn:GetName() and _G[btn:GetName() .. "IconTexture"]
        if icon then
            bar:SetPoint("BOTTOMLEFT", icon, "BOTTOMLEFT", 1, 1)
            bar:SetPoint("BOTTOMRIGHT", icon, "BOTTOMRIGHT", -1, 1)
        else
            bar:SetPoint("BOTTOMLEFT", 2, 2)
            bar:SetPoint("BOTTOMRIGHT", -2, 2)
        end
        bar:SetStatusBarTexture(WHITE)
        bar:SetFrameLevel((btn:GetFrameLevel() or 0) + 3)
        local bg = bar:CreateTexture(nil, "BACKGROUND")
        bg:SetAllPoints()
        bg:SetTexture(WHITE)
        bg:SetVertexColor(0, 0, 0, 0.7)
        btn._lgBar = bar
    end
    local bar = btn._lgBar
    if not it then
        bar:Hide()
        return
    end
    local lv = tonumber(it.lv) or 1
    local r, g, b = GetItemQualityColor(LevelQuality(lv))
    bar:SetMinMaxValues(0, 1)
    bar:SetValue(LevelProgress(it))
    bar:SetStatusBarColor(r, g, b, 0.95)
    bar:Show()
end

RefreshOverlays = function()
    for i = 1, #CHAR_SLOTS do
        local btn = _G[CHAR_SLOTS[i]]
        if btn then
            local slot = btn:GetID()
            local it = nil
            if slot and slot >= 1 then
                it = db.byKey["inv:" .. tostring(slot - 1)]
            end
            OverlayFor(btn, it)
        end
    end
    local n = NUM_CONTAINER_FRAMES or 13
    for i = 1, n do
        local frame = _G["ContainerFrame" .. i]
        if frame and frame.size then
            local bag = frame:GetID()
            for s = 1, frame.size do
                local btn = _G[frame:GetName() .. "Item" .. s]
                if btn then
                    OverlayFor(btn, db.byKey["bag:" .. tostring(bag) .. ":" .. tostring(btn:GetID())])
                end
            end
        end
    end
end

local function HookIconOverlays()
    if HookIconOverlays._done then
        return
    end
    HookIconOverlays._done = true
    if PaperDollItemSlotButton_Update then
        hooksecurefunc("PaperDollItemSlotButton_Update", function(btn)
            if not btn then
                return
            end
            local slot = btn:GetID()
            local it = nil
            if slot and slot >= 1 then
                it = db.byKey["inv:" .. tostring(slot - 1)]
            end
            OverlayFor(btn, it)
        end)
    end
    if ContainerFrame_Update then
        hooksecurefunc("ContainerFrame_Update", function(frame)
            if not frame or not frame.size then
                return
            end
            local bag = frame:GetID()
            for s = 1, frame.size do
                local btn = _G[frame:GetName() .. "Item" .. s]
                if btn then
                    OverlayFor(btn, db.byKey["bag:" .. tostring(bag) .. ":" .. tostring(btn:GetID())])
                end
            end
        end)
    end
end

local function RequestTip(key)
    local now = GetTime()
    local last = db.asked[key]
    if last and (now - last) < 30 then
        return
    end
    db.asked[key] = now
    local name = UnitName("player")
    if not name then
        return
    end
    if string.sub(key, 1, 4) == "inv:" then
        SendAddonMessage(PREFIX, "TIPREQ|inv|" .. string.sub(key, 5), "WHISPER", name)
    elseif string.sub(key, 1, 4) == "bag:" then
        local b, s = string.match(key, "^bag:(%d+):(%d+)$")
        if b then
            SendAddonMessage(PREFIX, "TIPREQ|bag|" .. b .. "|" .. s, "WHISPER", name)
        end
    end
end

local function StripTipText(text)
    if not text then
        return ""
    end
    text = string.gsub(text, "|[cC]%x+", "")
    text = string.gsub(text, "|[rR]", "")
    text = string.gsub(text, "|[tT].-|t", "")
    text = string.gsub(text, "\r", "")
    text = string.gsub(text, "\n", "")
    text = string.gsub(text, "^%s+", "")
    text = string.gsub(text, "%s+$", "")
    return text
end

local function IsFlavorLine(text)
    return string.find(text, "Set:", 1, true)
        or string.find(text, "Equip:", 1, true)
        or string.find(text, "Use:", 1, true)
        or string.find(text, "Chance", 1, true)
        or string.find(text, "Requires", 1, true)
end

local STAT_NAMES = { "Strength", "Agility", "Stamina", "Intellect", "Spirit", "Armor" }

local function ParseStatLine(text)
    if text == "" or IsFlavorLine(text) or string.find(text, "%(%d+/%d+%)") then
        return nil
    end
    for i = 1, #STAT_NAMES do
        local name = STAT_NAMES[i]
        local prefix, num = string.match(text, "(%+?)(%d+)%s+" .. name)
        if num then
            if string.find(text, "(+", 1, true) then
                return prefix, num, name, true
            end
            return prefix, num, name
        end
    end
    return nil
end

local function ApplyStatText(left, right, it, prefix, num, name)
    local field = STAT_FIELD[name]
    local extra = ExtraOf(it, field)
    local base = tonumber(num)
    if not field or extra == 0 or not base then
        return false
    end
    left:SetText(string.format("%s%d %s |cff66ccff(+%d)|r", prefix or "", base + extra, name, extra))
    left:SetTextColor(1, 1, 1, 1)
    if right then
        right:SetText("")
    end
    return field
end

local function EachTipLine(tip, fn)
    local tipName = tip:GetName()
    if not tipName then
        return
    end
    for i = 1, 40 do
        local left = _G[tipName .. "TextLeft" .. i]
        if left then
            fn(left, _G[tipName .. "TextRight" .. i])
        end
    end
end

local function HideDurability(tip)
    EachTipLine(tip, function(left, right)
        local raw = left:GetText()
        if not raw then
            return
        end
        local text = StripTipText(raw)
        if string.find(text, "Durability", 1, true)
            or text == "Soulbound"
            or text == "Binds when picked up"
            or text == "Binds when equipped"
            or text == "Binds when used" then
            left:SetText("")
            if right then
                right:SetText("")
            end
        end
    end)
end

local function RewriteStatLines(tip, it)
    local used = {}
    EachTipLine(tip, function(left, right)
        local raw = left:GetText()
        if not raw or raw == "" then
            return
        end
        local ltext = StripTipText(raw)
        local rtext = StripTipText(right and right:GetText())
        local prefix, num, name, done = ParseStatLine(ltext)
        if not name and rtext ~= "" then
            prefix, num, name, done = ParseStatLine(ltext .. " " .. rtext)
        end
        if not name and rtext ~= "" and STAT_FIELD[rtext] then
            prefix, num = string.match(ltext, "^(%+?)(%d+)$")
            name = rtext
        end
        if done and name and STAT_FIELD[name] then
            used[STAT_FIELD[name]] = true
            return
        end
        if name then
            local field = ApplyStatText(left, right, it, prefix or "", num, name)
            if field then
                used[field] = true
            end
        end
    end)
    return used
end

local function AddLivingFooter(tip, it)
    if tip._lgFooter then
        return
    end
    tip._lgFooter = true
    local used = tip._lgUsed or {}
    local found = used.ds or used.da or used.dt or used.di or used.dp or used.dar
    if found then
        local extras = { "ds", "da", "dt", "di", "dp", "dar" }
        for i = 1, #extras do
            local field = extras[i]
            local extra = ExtraOf(it, field)
            if extra ~= 0 and not used[field] then
                tip:AddLine(string.format("|cff66ccff+%d %s (+%d)|r", extra, STAT_LABEL[field], extra), 1, 1, 1)
            end
        end
    end
    local xp = "MAX"
    if tonumber(it.need) and tonumber(it.need) > 0 then
        xp = string.format("XP %s/%s", it.xp or "0", it.need)
    end
    tip:AddLine(" ")
    tip:AddLine(string.format("|cff66ccffLiving Gear|r  Lv %s  %s", it.lv or "1", xp), 1, 1, 1)
end

local function LivingForTip(tip)
    local key = tip and tip._lgKey
    if not key then
        return nil
    end
    local it = db.byKey[key]
    if not it then
        RequestTip(key)
        return nil
    end
    return it
end

local function HookTooltip(tip)
    if not tip or tip._lgHooked then
        return
    end
    tip._lgHooked = true

    local function PaintTip(self)
        HideDurability(self)
        local it = LivingForTip(self)
        if not it then
            return
        end
        self._lgUsed = RewriteStatLines(self, it)
    end

    local origInv = tip.SetInventoryItem
    tip.SetInventoryItem = function(self, unit, slot, ...)
        self._lgFooter = nil
        self._lgUsed = nil
        if unit == "player" and slot then
            self._lgKey = "inv:" .. tostring(tonumber(slot) - 1)
        else
            self._lgKey = nil
        end
        local r1, r2, r3 = origInv(self, unit, slot, ...)
        PaintTip(self)
        return r1, r2, r3
    end

    local origBag = tip.SetBagItem
    tip.SetBagItem = function(self, bag, slot, ...)
        self._lgFooter = nil
        self._lgUsed = nil
        if bag ~= nil and slot ~= nil then
            self._lgKey = "bag:" .. tostring(bag) .. ":" .. tostring(slot)
        else
            self._lgKey = nil
        end
        local r1, r2, r3 = origBag(self, bag, slot, ...)
        PaintTip(self)
        return r1, r2, r3
    end

    -- Footer before the client's Show() so height stays correct. Do not Show() here:
    -- paper doll refreshes the tooltip every frame while hovered.
    tip:HookScript("OnTooltipSetItem", function(self)
        HideDurability(self)
        local it = LivingForTip(self)
        if not it then
            return
        end
        AddLivingFooter(self, it)
    end)

    tip:HookScript("OnUpdate", function(self)
        HideDurability(self)
        if self._lgKey then
            PaintTip(self)
        end
    end)

    tip:HookScript("OnHide", function(self)
        self._lgKey = nil
        self._lgFooter = nil
        self._lgUsed = nil
    end)
end

local origGetItemCount = GetItemCount
if origGetItemCount then
    GetItemCount = function(item, includeBank, includeCharges)
        local n = origGetItemCount(item, includeBank, includeCharges) or 0
        local id = ItemIdFromArg(item)
        if id then
            n = n + VaultCountOf(id)
        end
        return n
    end
end

local origReagentInfo = GetTradeSkillReagentInfo
if origReagentInfo and GetTradeSkillReagentItemLink then
    GetTradeSkillReagentInfo = function(skillIndex, reagentIndex)
        local name, tex, req, have, a, b, c, d = origReagentInfo(skillIndex, reagentIndex)
        local link = GetTradeSkillReagentItemLink(skillIndex, reagentIndex)
        local id = link and tonumber(string.match(link, "item:(%d+)"))
        if id then
            have = (have or 0) + VaultCountOf(id)
        end
        return name, tex, req, have, a, b, c, d
    end
end

local origCraftReagent = GetCraftReagentInfo
if origCraftReagent and GetCraftReagentItemLink then
    GetCraftReagentInfo = function(index, reagentIndex)
        local name, tex, req, have, a, b, c, d = origCraftReagent(index, reagentIndex)
        local link = GetCraftReagentItemLink(index, reagentIndex)
        local id = link and tonumber(string.match(link, "item:(%d+)"))
        if id then
            have = (have or 0) + VaultCountOf(id)
        end
        return name, tex, req, have, a, b, c, d
    end
end

local function AutoAcceptBindPopup(which)
    local info = StaticPopupDialogs and StaticPopupDialogs[which]
    if not info then
        return
    end
    local origOnShow = info.OnShow
    info.OnShow = function(self, ...)
        if origOnShow then
            origOnShow(self, ...)
        end
        local btn = _G[self:GetName() .. "Button1"]
        if btn then
            btn:Click()
        end
    end
end
AutoAcceptBindPopup("EQUIP_BIND")
AutoAcceptBindPopup("EQUIP_BIND_TRADEABLE")
AutoAcceptBindPopup("AUTOEQUIP_BIND")

local function TryAutoAccept()
    if IsShiftKeyDown and IsShiftKeyDown() then
        return
    end
    if not PerkKnown(910108) then
        return
    end
    if AcceptQuest then
        AcceptQuest()
    end
end

local origPopupShow = StaticPopup_Show
if origPopupShow then
    StaticPopup_Show = function(which, text1, text2, data, inserted)
        if which == "EQUIP_BIND" or which == "EQUIP_BIND_TRADEABLE" or which == "AUTOEQUIP_BIND" then
            local dialog = origPopupShow(which, text1, text2, data, inserted)
            if dialog then
                local btn = _G[dialog:GetName() .. "Button1"]
                if btn then
                    btn:Click()
                end
            elseif EquipPendingItem then
                EquipPendingItem(data or 0)
            end
            return dialog
        end
        return origPopupShow(which, text1, text2, data, inserted)
    end
end

local ev = CreateFrame("Frame")
ev:RegisterEvent("PLAYER_LOGIN")
ev:RegisterEvent("PLAYER_ENTERING_WORLD")
ev:RegisterEvent("CHAT_MSG_ADDON")
ev:RegisterEvent("UNIT_SPELLCAST_SENT")
ev:RegisterEvent("UNIT_SPELLCAST_SUCCEEDED")
ev:RegisterEvent("BANKFRAME_OPENED")
ev:RegisterEvent("BANKFRAME_CLOSED")
ev:RegisterEvent("GOSSIP_SHOW")
ev:RegisterEvent("QUEST_GREETING")
ev:RegisterEvent("QUEST_DETAIL")
ev:SetScript("OnEvent", function(_, event, a1, a2)
    if event == "PLAYER_LOGIN" then
        InstallChatFilter()
        BuildUI()
        HookTooltip(GameTooltip)
        HookTooltip(ItemRefTooltip)
        HookTooltip(ShoppingTooltip1)
        HookTooltip(ShoppingTooltip2)
        HookIconOverlays()
        pcall(HookQuestTracker)
        pcall(HookJump)
        AutoAcceptBindPopup("EQUIP_BIND")
        AutoAcceptBindPopup("EQUIP_BIND_TRADEABLE")
        AutoAcceptBindPopup("AUTOEQUIP_BIND")
    elseif event == "PLAYER_ENTERING_WORLD" then
        RequestSync()
        ev._reqRetry = GetTime() + 2
        HideBankDeposit()
    elseif event == "CHAT_MSG_ADDON" then
        HandleAddon(a1, a2)
    elseif event == "BANKFRAME_OPENED" then
        ShowBankDeposit()
        RequestSync()
    elseif event == "BANKFRAME_CLOSED" then
        HideBankDeposit()
    elseif event == "QUEST_DETAIL" then
        TryAutoAccept()
    elseif event == "QUEST_GREETING" then
        if PerkKnown(910108) and not (IsShiftKeyDown and IsShiftKeyDown()) then
            if GetNumAvailableQuests and SelectAvailableQuest and GetNumAvailableQuests() == 1 then
                SelectAvailableQuest(1)
            end
        end
    elseif event == "GOSSIP_SHOW" then
        if PerkKnown(910108) and not (IsShiftKeyDown and IsShiftKeyDown()) then
            if GetNumGossipAvailableQuests and SelectGossipAvailableQuest and GetNumGossipAvailableQuests() == 1 then
                SelectGossipAvailableQuest(1)
            end
        end
    elseif event == "UNIT_SPELLCAST_SENT" or event == "UNIT_SPELLCAST_SUCCEEDED" then
        if a1 == "player" and IsAccountPerksName(a2) then
            OpenFromCast()
        end
    end
end)
ev:SetScript("OnUpdate", function(self, elapsed)
    if vaultLayoutPending and ui.reagents and ui.reagents:IsShown() then
        vaultLayoutPending = false
        RefreshVaultPanel()
    end
    if not self._reqRetry then
        return
    end
    if syncing then
        if GetTime() >= self._reqRetry then
            self._reqRetry = GetTime() + 1
        end
        return
    end
    if GetTime() < self._reqRetry then
        return
    end
    self._reqRetry = nil
    RequestSync()
end)

local function DumpTip(tip)
    if not tip then
        return
    end
    local tipName = tip:GetName() or "?"
    DEFAULT_CHAT_FRAME:AddMessage("|cff66ccff[LG]|r " .. tipName .. " lines=" .. tostring(tip:NumLines())
        .. " key=" .. tostring(tip._lgKey))
    EachTipLine(tip, function(left, right)
        local raw = left:GetText()
        local hex = ""
        if raw then
            for i = 1, math.min(24, string.len(raw)) do
                hex = hex .. string.format("%02X ", string.byte(raw, i))
            end
        end
        DEFAULT_CHAT_FRAME:AddMessage(string.format("|cff66ccff[LG]|r shown=%s L=[%s] R=[%s] %s",
            tostring(left:IsShown()), tostring(raw), tostring(right and right:GetText()), hex))
    end)
end

SLASH_LIVINGGEAR1 = "/lg"
SLASH_LIVINGGEAR2 = "/livinggear"
SlashCmdList["LIVINGGEAR"] = function(msg)
    msg = string.lower(string.gsub(msg or "", "^%s+", ""))
    if msg == "tip" then
        DumpTip(GameTooltip)
        return
    end
    Toggle()
end
