-- Living Gear window. Loaded from patch-enUS-4.MPQ via FrameXML, or Interface/AddOns.
-- Server pushes numbers over addon whispers (prefix LG). ASCII-only strings.

local LG_UI_REV = 25

-- Lua 5.1 (what the WotLK 3.3.5 client actually runs) also hard-caps the
-- main chunk -- the whole file, treated as one implicit function -- at 200
-- local variables, and every top-level `local`/`local function` declaration
-- consumes one slot for the rest of the file (see Bonesaw.md, "Lua 5.1's
-- 200-local main-chunk limit", 2026-08-20). Functions that only ever have
-- one or two call sites are attached here as fields instead of getting
-- their own top-level `local function` -- `function LG2.Foo()` doesn't
-- consume a main-chunk local slot the way `local function Foo()` does,
-- while still being a real top-level function with its own independent
-- 60-upvalue budget (nesting depth, not table-field-vs-local, is what the
-- upvalue ceiling cares about). New low-call-count helpers should default
-- to going here rather than adding another top-level `local function`.
local LG2 = {}

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

-- Index order is a wire format shared with LgAction in LivingGear_Vault.cpp.
-- Entries can be renamed but never reordered. "Destroy" (7) added 2026-08-22.
local ACTION_NAMES = { "Bags", "Vendor", "Hold", "Reagent vault", "Skip", "Disenchant", "Learn", "Destroy" }
local DEFAULT_RULES = {
    { match = 2, action = 2, negate = 0, quality = 0, text = "" },
    { match = 3, action = 3, negate = 0, quality = 0, text = "" },
    { match = 9, action = 6, negate = 0, quality = 0, text = "" },
    -- Food/Potion/Scroll auto-vendor (action 1) instead of skip (action 4),
    -- requested 2026-08-21.
    { match = 10, action = 1, negate = 0, quality = 0, text = "" },
    { match = 11, action = 1, negate = 0, quality = 0, text = "" },
    { match = 13, action = 1, negate = 0, quality = 0, text = "" },
    { match = 12, action = 4, negate = 0, quality = 0, text = "" },
    { match = 7, action = 0, negate = 0, quality = 0, text = "" },
    { match = 8, action = 1, negate = 0, quality = 0, text = "" },
    { match = 1, action = 1, negate = 0, quality = 0, text = "" },
    { match = 0, action = 0, negate = 0, quality = 0, text = "" },
}
local RULE_FIELDS = { "Type", "Quality", "Name", "Item Level" }
local RULE_OPS = { "==", "!=" }
local RULE_QUAL_OPS = { "==", "!=", ">=", "<=" }
local RULE_NAME_OPS = { "Matches", "Does not match" }
local RULE_TYPES = { "All", "Quest", "Reagent", "Living", "Unattuned", "Attuned", "Recipe", "Food", "Potion", "Bags", "Scroll" }
local RULE_TYPE_MATCH = { 0, 2, 3, 4, 7, 8, 9, 10, 11, 12, 13 }
-- Derived, not hand-maintained -- RuleText used to keep its own separate
-- "names" array that fell out of sync with RULE_TYPES/RULE_TYPE_MATCH once
-- (Food/Potion/Bags/Scroll rendered as "Type == ? -> ..." until this
-- existed), so build the lookup from the single source of truth instead.
local MATCH_TO_NAME = {}
for i = 1, #RULE_TYPE_MATCH do
    MATCH_TO_NAME[RULE_TYPE_MATCH[i]] = RULE_TYPES[i]
end
local RULE_QUALS = { "Grey", "White", "Green", "Blue", "Epic", "Legendary" }
local RULE_ROWS = 10
local WORLD_UNLOCKS = {
    { id = 910092, name = "Solo Queue", how = "Queue for dungeons and raids by yourself. No group required.", toggle = true, toggleKey = "solo", icon = "Interface\\Icons\\Achievement_Dungeon_UtgardeKeep" },
    { id = 910105, name = "Auto-Mount", how = "Automatically mount when you leave combat. Unlocked by learning a mount.", toggle = true, toggleKey = "autoMount", icon = "Interface\\Icons\\Ability_Mount_BlackPanther" },
    { id = 910168, name = "Pull Radius", how = "Quadruples how far enemies detect and aggro onto you. For pulling everything in an area on purpose.", toggle = true, toggleKey = "pullRadius", icon = "Interface\\Icons\\Ability_Physical_Taunt" },
    { id = 910170, name = "Track Ore", how = "Shows nearby ore veins on the minimap.", toggle = true, toggleKey = "trackOre", icon = "Interface\\Icons\\Spell_Nature_WispSplode" },
    { id = 910171, name = "Track Herbs", how = "Shows nearby herbs on the minimap.", toggle = true, toggleKey = "trackHerb", icon = "Interface\\Icons\\Spell_Nature_WispSplodeGreen" },
    { id = 910106, name = "Class Buffs", how = "Clear Naxxramas 25 on a class. That class then applies 10% primary stats to you and nearby party.", icon = "Interface\\Icons\\Spell_Holy_GreaterBlessingofKings" },
    { id = 910107, name = "Riding", how = "Train riding on any character. Alts can mount from level 1, and every mount and pet you own is shared across the account.", icon = "Interface\\Icons\\Ability_Mount_RidingHorse" },
    { id = 910172, name = "CC Reduction", how = "Get crowd controlled once. Stuns, roots, fears, snares and other crowd control then last 95% less on you.", icon = "Interface\\Icons\\INV_Jewelry_TrinketPVP_01" },
    { id = 910108, name = "Auto-Accept", how = "Accept a quest. Then auto-accept when you talk to an NPC. Hold Shift to skip.", icon = "Interface\\Icons\\inv_letter_09" },
    { id = 910091, name = "Armory", how = "Copy an attuned item into your bags to wear.", icon = "Interface\\Icons\\INV_Chest_Plate02" },
    { id = 910003, name = "Auction", how = "List or bid at an auction house.", icon = "Interface\\Icons\\INV_Misc_Coin_01" },
    { id = 910090, name = "Quests - Finish", how = "Complete 1 quest. Summons questgivers for completed log quests for 60 sec. Turn in and take follow-ups.", icon = "Interface\\Icons\\INV_Misc_Note_01" },
    { id = 910008, name = "Autoloot", how = "Manually loot 10 corpses.", icon = "Interface\\Icons\\INV_Misc_Bag_10_Black" },
    { id = 910005, name = "Bank", how = "Open a world bank.", icon = "Interface\\Icons\\INV_Box_01" },
    { id = 910007, name = "Bind Hearthstone", how = "Enter a dungeon.", icon = "Interface\\Icons\\INV_Misc_Rune_01" },
    { id = 910009, name = "Flight", how = "Learn a flight path.", icon = "Interface\\Icons\\Ability_Druid_FlightForm" },
    { id = 910088, name = "Quests - Find", how = "Complete 50 quests. Adds up to 5 zone quests per use.", icon = "Interface\\Icons\\INV_Misc_Note_01" },
    { id = 910002, name = "Mailbox", how = "Send or receive mail.", icon = "Interface\\Icons\\INV_Letter_04" },
    { id = 910006, name = "Stable", how = "Hunters: take on a pet.", icon = "Interface\\Icons\\Ability_Hunter_BeastCall" },
    { id = 910004, name = "Trainer", how = "Enter a dungeon.", icon = "Interface\\Icons\\INV_Misc_Book_09" },
}

local WORLD_TRACKS = {
    {
        name = "Attune",
        ticks = {
            { id = 910101, name = "Curator 1", how = "Items in your bags and bank count for 25% of their value toward your account stats.", bonus = 25 },
            { id = 910178, name = "Curator 2", how = "Items in your bags and bank count for 50% of their value.", bonus = 50 },
            { id = 910179, name = "Curator 3", how = "Items in your bags and bank count for 75% of their value.", bonus = 75 },
            { id = 910180, name = "Curator 4", how = "Items in your bags and bank count for 100% of their value. Gear you wear still grows past this by levelling.", bonus = 100 },
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
        },
    },
    {
        name = "Fishing Yield",
        unit = "gather",
        ticks = {
            { id = 910127, name = "150", how = "Reach Fishing 150. Fish yield 2x. Stacks to 4x at 300 and 8x at 450.", bonus = 0, yield = 2 },
            { id = 910128, name = "300", how = "Reach Fishing 300. Fish yield 4x.", bonus = 0, yield = 4 },
            { id = 910129, name = "450", how = "Reach Fishing 450. Fish yield 8x.", bonus = 0, yield = 8 },
        },
    },
    {
        name = "Fishing Reach",
        unit = "gather",
        ticks = {
            { id = 910130, name = "Reach 75", how = "Reach Fishing 75. Auto-loot pools from +3 yards. Stacks to +9 yards at 375.", bonus = 3 },
            { id = 910131, name = "Reach 225", how = "Reach Fishing 225. Auto-loot pools from +6 yards.", bonus = 6 },
            { id = 910132, name = "Reach 375", how = "Reach Fishing 375. Auto-loot pools from +9 yards.", bonus = 9 },
        },
    },
        {
        name = "Engineering Yield",
        unit = "gather",
        ticks = {
            { id = 910133, name = "150", how = "Reach Engineering 150. Crafts and blasting/salvage yield 2x. Stacks to 4x at 300 and 8x at 450.", bonus = 0, yield = 2 },
            { id = 910134, name = "300", how = "Reach Engineering 300. Crafts and blasting/salvage yield 4x.", bonus = 0, yield = 4 },
            { id = 910135, name = "450", how = "Reach Engineering 450. Crafts and blasting/salvage yield 8x.", bonus = 0, yield = 8 },
        },
    },
    {
        name = "Engineering Reach",
        unit = "gather",
        ticks = {
            { id = 910136, name = "Reach 75", how = "Reach Engineering 75. Auto-gather blasting nodes and salvage from +3 yards. Stacks to +9 yards at 375.", bonus = 3 },
            { id = 910137, name = "Reach 225", how = "Reach Engineering 225. Auto-gather blasting nodes and salvage from +6 yards.", bonus = 6 },
            { id = 910138, name = "Reach 375", how = "Reach Engineering 375. Auto-gather blasting nodes and salvage from +9 yards.", bonus = 9 },
        },
    },
        {
        name = "Herbalism Yield",
        unit = "gather",
        ticks = {
            { id = 910115, name = "150", how = "Reach Herbalism 150. Herbs yield 2x. Stacks to 4x at 300 and 8x at 450.", bonus = 0, yield = 2 },
            { id = 910116, name = "300", how = "Reach Herbalism 300. Herbs yield 4x.", bonus = 0, yield = 4 },
            { id = 910117, name = "450", how = "Reach Herbalism 450. Herbs yield 8x.", bonus = 0, yield = 8 },
        },
    },
    {
        name = "Herbalism Reach",
        unit = "gather",
        ticks = {
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
        -- Double Jump (910039) and Triple Jump (910040) were removed from this
        -- track on 2026-08-22. They were advertised and earnable and did
        -- nothing: on-foot extra jump is deliberately disabled because
        -- ApplyLgMoveSpeed + KnockbackFrom crashed on the first tick, and the
        -- wiki records "do not re-enable". Advertising a feature that is off on
        -- purpose is worse than not listing it.
        --
        -- Mounted Opener (910104) followed them on 2026-08-24, for the same
        -- reason and with better evidence. The non-class perk audit proved it
        -- could never fire at all: its only trigger was a cast branch for a
        -- spell that is not castable and is never learned, so a level 40
        -- account earned it, saw it green here, and got nothing. Its
        -- description also advertised the very jump behaviour that is disabled
        -- on purpose. The slam/pull/Thunder Clap implementation is parked in
        -- LivingGear_Perks.cpp, not deleted, so it can come back as a real
        -- mounted button if that is ever judged worth doing.
        ticks = {
            { id = 910038, name = "Wayfarer 1", how = "Explore your home zone, or earn Going Down?. Movement speed +20% on foot, mounted and flying.", bonus = 20 },
            { id = 910176, name = "Wayfarer 2", how = "Explore Eastern Kingdoms or Explore Kalimdor. Movement speed +40%.", bonus = 40 },
            { id = 910177, name = "Wayfarer 3", how = "Explore Outland or Explore Northrend. Movement speed +60%.", bonus = 60 },
            { id = 910039, name = "Wayfarer 4", how = "Movement speed +80% on foot, mounted and flying.", bonus = 80 },
            { id = 910040, name = "Wayfarer 5", how = "Movement speed +100% on foot, mounted and flying.", bonus = 100 },
        },
    },
        {
        name = "Mining Yield",
        unit = "gather",
        ticks = {
            { id = 910109, name = "150", how = "Reach Mining 150. Ore yield 2x. Stacks to 4x at 300 and 8x at 450.", bonus = 0, yield = 2 },
            { id = 910110, name = "300", how = "Reach Mining 300. Ore yield 4x.", bonus = 0, yield = 4 },
            { id = 910111, name = "450", how = "Reach Mining 450. Ore yield 8x.", bonus = 0, yield = 8 },
        },
    },
    {
        name = "Mining Reach",
        unit = "gather",
        ticks = {
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
        name = "Skinning Yield",
        unit = "gather",
        ticks = {
            { id = 910121, name = "150", how = "Reach Skinning 150. Skins yield 2x. Stacks to 4x at 300 and 8x at 450.", bonus = 0, yield = 2 },
            { id = 910122, name = "300", how = "Reach Skinning 300. Skins yield 4x.", bonus = 0, yield = 4 },
            { id = 910123, name = "450", how = "Reach Skinning 450. Skins yield 8x.", bonus = 0, yield = 8 },
        },
    },
    {
        name = "Skinning Reach",
        unit = "gather",
        ticks = {
            { id = 910124, name = "Reach 75", how = "Reach Skinning 75. Auto-skin from +3 yards. Stacks to +9 yards at 375.", bonus = 3 },
            { id = 910125, name = "Reach 225", how = "Reach Skinning 225. Auto-skin from +6 yards.", bonus = 6 },
            { id = 910126, name = "Reach 375", how = "Reach Skinning 375. Auto-skin from +9 yards.", bonus = 9 },
        },
    },
        {
        name = "Swim",
        ticks = {
            { id = 910098, name = "Swim", how = "Reach level 10. Swim speed +500%.", bonus = 500 },
        },
    },
    {
        name = "Travel",
        ticks = {
            { id = 910073, name = "1", how = "Use your Hearthstone 1 time. Cast time and cooldown -20%.", bonus = 20 },
            { id = 910074, name = "2", how = "Use your Hearthstone 2 times. Cast time and cooldown -20%.", bonus = 20 },
            { id = 910075, name = "3", how = "Use your Hearthstone 3 times. Cast time and cooldown -20%.", bonus = 20 },
            { id = 910076, name = "4", how = "Use your Hearthstone 4 times. Cast time and cooldown -20%.", bonus = 20 },
            { id = 910077, name = "5", how = "Use your Hearthstone 5 times. Cast time and cooldown -20%.", bonus = 20 },
        },
    },
}

-- World retired 2026-08-25: it rendered the same perk data as the Unlocks and
-- Perks tabs on the achievement frame, which meant two places to edit in
-- lockstep and one of them silently drifting.
--
-- Gear, Attune and the Armory collapsed into Items. All three were views of the
-- same objects, which is why the Armory had ended up two clicks deep inside a
-- sibling tab -- there was nowhere better for it to be.
local TABS = {
    { id = "class", label = "Class" },
    { id = "items", label = "Armory" },
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
local FRAME_W = 640
local FRAME_H = 520
-- Centralized color tokens. Every semantic on/off/hover/danger state used to
-- be a separately-typed-out RGB literal at each call site (slightly
-- different every time), which is how the UI ended up looking inconsistent
-- across tabs. Use these everywhere instead of new literals.
local COLOR_BG = { 0.10, 0.10, 0.10 }
local COLOR_BTN = { 0.16, 0.16, 0.16 }
local COLOR_BTN_HOVER = { 0.24, 0.24, 0.24 }
local COLOR_ON = { 0.14, 0.36, 0.16 }
local COLOR_OFF = { 0.30, 0.13, 0.13 }
local COLOR_DANGER = { 0.42, 0.12, 0.12 }
local COLOR_ACCENT = { 0.20, 0.45, 0.75 }
local COLOR_ADD = { 0.14, 0.22, 0.14 } -- softer green for add/apply/deposit actions, distinct from COLOR_ON's "currently toggled on" state
local COLOR_BORDER = { 0.35, 0.35, 0.35 }
local COLOR_ROW_HOVER = { 0.16, 0.16, 0.16 }
local COLOR_TEXT = { 0.90, 0.90, 0.90 }
local COLOR_TEXT_DIM = { 0.55, 0.55, 0.55 }
local COLOR_TEXT_LOCKED = { 0.45, 0.45, 0.45 }
local SCALES = { 0.85, 1.0, 1.15, 1.3, 1.5, 1.75 }
local SCALE_LABELS = { "85%", "100%", "115%", "130%", "150%", "175%" }
local LG_MAX_LEVEL = 50
-- Matches the server's LivingGear.Attune.CapLevel default (LivingGear.cpp,
-- 2026-08-20 attunement redesign) -- an item is fully attuned (banking
-- 100% of its current stats to the account) at this level, so the UI
-- treats it as "done" here even though the item keeps growing its own
-- worn stats up to LG_MAX_LEVEL. Not synced from the server -- if that
-- config is ever changed, update this to match, same as LG_MAX_LEVEL
-- itself already is (not server-synced either).
local LG_ATTUNE_CAP_LEVEL = 25
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
        { id = 910032, name = "Arcane", how = "Arcane Power is a free toggle with no cooldown. While it is up your Arcane damage is quadrupled. In combat, Mirror Images appear and chain-cast, and linger 60 sec after combat.",
          lines = {
            { spell = 12042, text = "Learned for free, and a toggle with no cooldown. While it is up all your Arcane damage is quadrupled." },
            { spell = 55342, text = "In combat these appear and chain-cast, and linger 60 sec after combat." },
          } },
        { id = 910033, name = "Fire", how = "Fire spells apply Living Bomb, which spreads every 1 sec and deals +300% damage. Fire Blast detonates every Living Bomb within 15 yards at once, and each blast re-applies Living Bomb around it. Combustion is kept up for free.",
          lines = {
            { spell = 55361, text = "Learned for free. Any harmful Fire spell applies it to the target. It spreads to enemies within 15 yards every 1 sec, and all its damage (ticks and blasts) is +300%." },
            { spell = 42873, text = "Detonates every Living Bomb you own within 15 yards at once, and each detonation re-applies Living Bomb around that enemy." },
            { spell = 11129, text = "Kept up for free. Any harmful Fire spell applies it." },
          } },
        { id = 910034, name = "Frost", how = "Blizzard is instant, no cooldown, and lingers like Death and Decay, damaging everything inside it every 1 sec. Frost damage quadrupled. Ice Lance hits nearby enemies every 2 sec.",
          lines = {
            { spell = 42940, text = "Learned for free, instant, no cooldown, and lingers like Death and Decay (8 sec), damaging everything inside it every 1 sec." },
            { spell = 42914, text = "Learned for free. In combat it automatically hits everything within 15 yards every 2 sec." },
            { text = "All your Frost damage is quadrupled." },
          } },
    },
    -- Rogue is the worked example for the `lines` format (see the comment above
    -- CLASS_PERKS). One entry per ABILITY, not per sentence: the old prose said
    -- "Learn Envenom" and "Envenom detonates..." as two separate bullets, so a
    -- reader had to reassemble what any single button actually does now.
    -- Every spell id below was read out of var/mmap-output/dbc/Spell.dbc at the
    -- rank a level 80 actually has -- do not guess these, a wrong id shows a
    -- confidently wrong tooltip.
    ROGUE = {
        { id = 910035, name = "Assassination", how = "Learn Envenom. Each melee hit has a 20% chance to apply a random poison. Envenom detonates every poison and bleed you own on all enemies within 15 yards, dealing their whole remaining duration at once, then puts them back at full.",
          lines = {
            { spell = 57993, text = "Learned for free. Detonates every poison and bleed you own on all enemies within 15 yards, dealing their whole remaining duration at once, then puts them back to full." },
            { text = "Each melee hit has a 20% chance to apply a random poison (Crippling, Wound or Deadly)." },
          } },
        { id = 910036, name = "Combat", how = "Learn Adrenaline Rush as a free toggle with no cooldown. While it is up your abilities cost no energy, and Blade Flurry strikes everything within 15 yards. Blade Flurry is always on. Combo builders have a 30% chance to trigger a free Killing Spree, and your energy regenerates 50% faster.",
          lines = {
            { spell = 13750, text = "Learned for free, and a toggle with no cooldown. While it is up your abilities cost no energy." },
            { spell = 13877, text = "Learned for free, and always on. While Adrenaline Rush is up it strikes everything within 15 yards." },
            { spell = 51690, text = "Learned for free. Combo point builders have a 30% chance to trigger a free one." },
            { text = "Your energy regenerates 50% faster." },
          } },
        { id = 910037, name = "Subtlety", how = "Learn Hemorrhage and Shadowstep. Hemorrhage spreads itself, a boosted Ambush and the Garrote bleed to everything within 15 yards. Eviscerate applies Slice and Dice to you and Rupture to everything within 15 yards. Garrote and Rupture deal +2000% damage and tick faster with haste. Pickpocket hits every humanoid within 10 yards. Learn Shadow Dance.",
          lines = {
            { spell = 48660, text = "Learned for free. Spreads itself, a boosted Ambush and the Garrote bleed to everything within 15 yards (the Ambush and Garrote land only on enemies in melee range in front of you)." },
            { spell = 36554, text = "Learned for free, on a 2 sec cooldown." },
            { spell = 921, text = "Pickpockets every humanoid within 10 yards at once." },
            { spell = 48668, text = "Also applies Slice and Dice to you, and Rupture to everything within 15 yards." },
            { spell = 48676, text = "Deals +2000% damage and ticks faster with haste." },
            { spell = 48672, text = "Deals +2000% damage and ticks faster with haste." },
          },
          subPerks = { { id = 910102, spell = 51713, name = "Shadow Dance", how = "Permanent. Openers usable without stealth. +10% attack power to your party/raid." } } },
    },
    PALADIN = {
        { id = 910069, name = "Holy", how = "Consecration follows you and toggles off if recast. Consecration damage +1000%. Holy Shock damage +300% and hits enemies within 10 yards of the target.",
          lines = {
            { spell = 48819, text = "Consecration: follows you as you move, and toggles off if recast. All its damage is +1000%." },
            { spell = 48825, text = "Holy Shock: deals +300% damage and hits enemies within 10 yards of the target." },
          } },
        { id = 910070, name = "Protection", how = "Avenger's Shield bounces 30 times, can rehit the same target, and deals +300% damage. A Judgement cast has a 15% chance to trigger a full bounce chain. Devotion Aura reduces ally damage taken by 10%. You deal Holy thorns equal to 50% of your armor.",
          lines = {
            { spell = 48827, text = "Avenger's Shield: bounces 30 times, can rehit the same target, and deals +300% damage." },
            { spell = 20271, text = "Judgement: 15% chance to trigger a full Avenger's Shield bounce chain." },
            { spell = 48942, text = "Devotion Aura: allies within 40 yards take 10% less damage." },
            { text = "You deal Holy thorns back to attackers equal to 50% of your armor." },
            { text = "Generates 10x threat on everything you do." },
          } },
        { id = 910071, name = "Retribution", how = "Learn Sanctified Whirlwind, a toggle: your Consecration follows you, and Divine Storm's cooldown clears whenever you deal Holy damage. Each Divine Storm press hits 4 times, and while the storm lasts your melee swings also cleave everything within 8 yards for 50%. Learn Crusader Strike: 30% chance on hit to fire a free Avenger's Shield bounce chain.",
          lines = {
            { spell = 910182, text = "Sanctified Whirlwind: learned for free, a toggle. While it is up your Consecration follows you as you move, and Divine Storm's cooldown is cleared whenever you deal Holy damage." },
            { spell = 53385, text = "Divine Storm: each press hits 4 times, and while the storm lasts your melee swings also strike everything within 8 yards for 50% of the swing's damage." },
            { spell = 35395, text = "Crusader Strike: learned for free. While Retribution Aura is up it also casts Exorcism on nearby enemies, and each hit has a 30% chance to fire a free Avenger's Shield bounce chain." },
          } },
    },
    WARRIOR = {
        { id = 910083, name = "Arms", how = "Learn Bladestorm. No rage cost, no cooldown, and it does not end. Recast to stop. You can use other abilities while spinning, and Whirlwind plus Thunder Clap autocast every 6 sec while it spins.",
          lines = {
            { spell = 46924, text = "Bladestorm: learned for free. No rage cost, no cooldown, and it does not end. Recast to stop. You can use other abilities while spinning." },
            { spell = 1680, text = "While Bladestorm spins, Whirlwind and Thunder Clap autocast every 6 sec." },
          } },
        { id = 910084, name = "Fury", how = "Titan's Grip. Each melee hit: +5% attack speed (stacks to 20) and heal 1% of max health in combat. Attack speed lingers 60 sec after combat. Rend and Deep Wounds deal +300% damage.",
          lines = {
            { text = "Titan's Grip. Each melee hit grants +5% attack speed (stacks to 20) and heals 1% of your max health in combat; the attack speed lingers 60 sec after combat." },
            { spell = 47465, text = "Rend and Deep Wounds deal +300% damage." },
          } },
        { id = 910085, name = "Protection", how = "Learn Shockwave with no cooldown and +300% damage. Learn Avalanche, a castable toggle that drops a 12-yard circle dealing Shield Slam damage and a 50% slow every 1 sec; recast to move it, recast on the circle to dismiss it, and gain +25% block chance while it is up. Shield Slam hits 4 times, each hit rupturing enemies within 8 yards. Thunder Clap radius is tripled and it applies your Rend and Deep Wounds at 3x to every enemy hit.",
          lines = {
            { spell = 46969, text = "Shockwave: learned for free, with no cooldown and +300% damage." },
            { spell = 43265, text = "Avalanche: learned for free, a toggle that drops a 12-yard circle at your target. Everything inside takes your Shield Slam damage and a 50% slow every 1 sec. Recast to move it; recast on the circle to dismiss it. +25% block chance while it is on the ground." },
            { spell = 47488, text = "Shield Slam: each press hits 4 times, and every hit ruptures enemies within 8 yards." },
            { spell = 47502, text = "Thunder Clap: radius tripled, and it applies your Rend and Deep Wounds at 3x damage to every enemy hit." },
          } },
    },
    HUNTER = {
        { id = 910150, name = "Marksmanship", how = "Chimera Shot has no cooldown and recasts your highest Serpent Sting on the target. Any ranged damage spell has a 20% chance to grant a free, instant Aimed Shot.",
          lines = {
            { spell = 53209, text = "Chimera Shot: no cooldown. Recasts your highest rank of Serpent Sting on the target." },
            { spell = 49001, text = "Serpent Sting: Chimera Shot reapplies it at full strength." },
            { spell = 49050, text = "Aimed Shot: 20% chance on any ranged damage spell to grant a free, instant one." },
          } },
        { id = 910153, name = "Beast Mastery", how = "Bestial Wrath has no cooldown/focus cost, and each cast summons 4 clones of your current pet at 50% health for 20 sec.",
          lines = {
            { spell = 19574, text = "Bestial Wrath: no cooldown, focus cost refunded. Each cast also summons 4 clones of your current pet at 50% health, lasting 20 sec." },
          } },
        { id = 910154, name = "Survival", how = "Explosive Shot deals double damage. Traps lose their cooldown and their blast radius is doubled. You are immune to your own trap damage. Traps left on the ground re-arm every 10 sec (up to 3 times) while an enemy stands in them, and Call of the Wilds summons 2 tank bears that taunt and hold a pack for you.",
          lines = {
            { spell = 60053, text = "Explosive Shot: deals double damage (the direct hit and every tick). Five ticks banked on a target, the next one detonates for all banked damage to everything within 8 yards." },
            { spell = 13809, text = "All traps (Immolation, Frost, Freezing, Explosive, Snake): no cooldown and double blast radius. A placed trap re-arms at its spot every 10 sec, up to 3 times, while an enemy stands inside it (max 6 live zones)." },
            { spell = 910181, text = "Call of the Wilds: summons 2 bear clones of your pet (or wild bears if you have no pet) at 50% health. They taunt everything nearby on arrival and keep holding threat for 60 sec. Recast to refresh." },
            { text = "You take no damage from your own traps." },
          } },
    },
    SHAMAN = {
        { id = 910151, name = "Elemental", how = "Thunderstorm has no cooldown. Lava Burst deals double damage. Chain Lightning has no target cap.",
          lines = {
            { spell = 59159, text = "Thunderstorm: no cooldown." },
            { spell = 60043, text = "Lava Burst: deals double damage." },
            { spell = 49271, text = "Chain Lightning: no target cap (jumps up to 99 targets)." },
          } },
        { id = 910155, name = "Enhancement", how = "Feral Spirit is effectively permanent: no cooldown/mana cost, and your spirit wolves' melee damage is doubled while the buff lasts, with a 25% chance per wolf strike to call down a lightning strike on its target. Stormstrike has no cooldown and marks its target Charged for 8 sec; Lightning Bolt and Chain Lightning on a Charged target fire a second bolt at 50% damage and refresh the charge. Learn Static Field (a Thorns toggle): a 10-yard storm circle follows you, shocking everything inside every 2 sec for Nature damage that scales with your attack power.",
          lines = {
            { spell = 51533, text = "Feral Spirit: no cooldown, mana cost refunded. The buff never drops below 10 sec remaining, so your wolves never expire, and their melee damage is doubled. Each wolf strike has a 25% chance to call a lightning strike on its target." },
            { spell = 17364, text = "Stormstrike: no cooldown, and it marks the target Charged for 8 sec." },
            { spell = 49238, text = "Lightning Bolt: on a Charged target it fires a second bolt at 50% damage and refreshes the charge." },
            { spell = 49271, text = "Chain Lightning: on a Charged target it fires a second bolt at 50% damage and refreshes the charge." },
            { spell = 53307, text = "Static Field: learned for free, a toggle. A 10-yard storm circle follows you and shocks everything inside every 2 sec for Nature damage scaled with your attack power." },
          } },
        { id = 910156, name = "Restoration", how = "Riptide has no cooldown and also applies to 2 more injured (below 95% health) allies within 15 yards of your target. Chain Heal has no bounce cap.",
          lines = {
            { spell = 61301, text = "Riptide: no cooldown. Also applies to 2 more injured (below 95% health) allies within 15 yards of your target." },
            { spell = 55459, text = "Chain Heal: no bounce cap (heals up to 99 targets)." },
          } },
    },
    DEATHKNIGHT = {
        { id = 910152, name = "Unholy", how = "A permanent blight shroud follows you: every 2 sec it applies your Blood Plague and Frost Fever (and Ebon Plague if you know it) to every enemy within 10 yards, refreshing any already infected, and all your diseases deal 5x damage. Summon Gargoyle has no cooldown and your gargoyle stays until it dies. Army of the Dead has no cooldown and also summons a 5-ghoul group: 1 tank (3x health), 1 healer (heals your lowest ally every 2 sec), 3 dps. The group persists while you are in combat and lasts 60 sec after you leave it.",
          lines = {
            { spell = 50536, text = "Your blight shroud is permanent: every 2 sec it applies your diseases to every enemy within 10 yards and refreshes any it already infected." },
            { spell = 55078, text = "Blood Plague, Frost Fever, and Ebon Plague deal 5x damage." },
            { spell = 49206, text = "Summon Gargoyle: no cooldown, and your gargoyle stays until it dies." },
            { spell = 42650, text = "Army of the Dead: no cooldown, and also summons a 5-ghoul group: 1 tank (3x health), 1 healer (heals your lowest ally every 2 sec), 3 dps. The group persists while you are in combat and lasts 60 sec after you leave it." },
          } },
        { id = 910166, name = "Blood", how = "Dancing Rune Weapon has no cooldown/runic cost. While it is up, your melee hits heal you for 5% of the damage dealt.",
          lines = {
            { spell = 49028, text = "Dancing Rune Weapon: no cooldown/runic cost. While it is up, your melee hits heal you for 5% of the damage dealt." },
            { text = "Generates 10x threat on everything you do." },
          } },
        { id = 910167, name = "Frost", how = "Hungering Cold has no cooldown/runic cost. Frost Strike and Obliterate deal double damage.",
          lines = {
            { spell = 49203, text = "Hungering Cold: no cooldown/runic cost." },
            { spell = 49143, text = "Frost Strike and Obliterate deal double damage." },
          } },
    },
    WARLOCK = {
        { id = 910157, name = "Affliction", how = "Haunt is instant, castable while moving, and has no cooldown. Casting it seeds your whole DoT set on the target: Unstable Affliction (learned with the perk), Corruption, and both Curse of Agony and Curse of the Elements. In combat, your DoTs spread every 1 sec from every infected enemy to others within 15 yards, hopping out as far as 60 yards from you. DoT ticks deal 2000% damage, increased further by your haste.",
          lines = {
            { spell = 48181, text = "Haunt: instant, no cooldown, and applies Unstable Affliction, Corruption, Curse of Agony and Curse of the Elements to the target." },
            { text = "In combat, your DoTs spread every 1 sec from every infected enemy to others within 15 yards, hopping out as far as 60 yards from you." },
            { text = "Your DoT ticks deal quadruple damage, and DoT tick damage is also increased by your haste." },
          } },
        { id = 910158, name = "Demonology", how = "Learn Imp Legion (a Fel Domination toggle): it summons 8 imps at 35% health for 30 sec; recast refreshes them, recast again dismisses them. Metamorphosis has no cooldown or mana cost and turns the pack into felguards while it lasts. Your demon pet's damage is always doubled, and the legion's firebolts are doubled too.",
          lines = {
            { spell = 18708, text = "Imp Legion: learned for free, a toggle with no cooldown or mana cost. Summons 8 imps at 35% health lasting 30 sec. Recast refreshes the pack; recast again to dismiss it." },
            { spell = 47241, text = "Metamorphosis: no cooldown or mana cost. While it is up, your imp pack becomes felguards until it fades." },
            { text = "Your demon pet's damage is doubled (always on)." },
            { text = "The legion's firebolts deal double damage." },
          } },
        { id = 910159, name = "Destruction", how = "Chaos Bolt has no cooldown. Conflagrate also casts a free, instant Chaos Bolt at the same target.",
          lines = {
            { spell = 50796, text = "Chaos Bolt: no cooldown." },
            { spell = 17962, text = "Conflagrate: also casts a free, instant Chaos Bolt at the same target." },
          } },
    },
    DRUID = {
        { id = 910160, name = "Balance", how = "Starfall is a free toggle: cast to switch it on, recast to switch it off, and it never expires. While it is up, your Arcane and Nature damage is tripled, and each star hit has a 30% chance to cast a free Moonfire on the target, which also drops its standalone Hurricane. You are permanently in both Solar and Lunar Eclipse at once. Insect Swarm spreads to all other enemies within 25 yards of your target when cast, spreads itself from every infected enemy every 1 sec, and refreshes on existing targets. Thorns is maintained on you and your party. Moonfire automatically applies a standalone Hurricane to the target, and Wrath and Starfire each fire the other free and instant at the same target.",
          lines = {
            { spell = 53201, text = "Starfall: a free toggle. Cast to switch it on, recast to switch it off, and it never expires. While it is up, your Arcane and Nature damage is tripled, and each star hit has a 30% chance to cast a free Moonfire on the target, which also drops its standalone Hurricane." },
            { text = "You are permanently in both Solar and Lunar Eclipse at once." },
            { spell = 48468, text = "Insect Swarm: spreads to all other enemies within 25 yards of your target when cast, spreads itself from every infected enemy every 1 sec, auto-casts on enemies that strike you, and refreshes on existing targets." },
            { spell = 53307, text = "Thorns: maintained on you and your party." },
            { spell = 8921, text = "Moonfire: automatically applies a standalone Hurricane to the target." },
            { spell = 2912, text = "Wrath and Starfire: casting one also fires the other free and instant at the same target." },
          } },
        { id = 910161, name = "Feral", how = "Berserk has no cooldown. While Berserk is active, your druid abilities cost no energy/rage/mana and lose their cooldowns.",
          lines = {
            { spell = 50334, text = "Berserk: no cooldown. While it is active, your druid abilities cost no energy/rage/mana and lose their cooldowns." },
            { text = "Generates 10x threat while in Bear Form." },
          } },
        { id = 910162, name = "Restoration", how = "Wild Growth has no cooldown and heals up to 10 allies within 30 yards. Rejuvenation spreads from every rejuvenated ally to injured allies within 15 yards every 3 sec.",
          lines = {
            { spell = 53251, text = "Wild Growth: no cooldown, and heals up to 10 allies within 30 yards." },
            { spell = 48441, text = "Rejuvenation: spreads from every rejuvenated ally to injured allies within 15 yards every 3 sec." },
          } },
    },
    PRIEST = {
        { id = 910163, name = "Discipline", how = "Penance has no cooldown and also applies Power Word: Shield to the target. When cast on an enemy, it ricochets to up to 5 more enemies within 15 yards of the last target.",
          lines = {
            { spell = 53007, text = "Penance: no cooldown, and also applies Power Word: Shield to the target. When cast on an enemy, it ricochets to up to 5 more enemies within 15 yards of the last target." },
          } },
        { id = 910164, name = "Holy", how = "Guardian Spirit has no cooldown and also applies to 2 more injured allies within 20 yards.",
          lines = {
            { spell = 47788, text = "Guardian Spirit: no cooldown, and also applies to 2 more injured allies within 20 yards." },
          } },
        { id = 910165, name = "Shadow", how = "Shadowfiend has no cooldown. Learn Voidform (a Shadowform toggle): while it is up, a free Mind Blast fires at your target every 3 sec. Every Mind Flay channel also sings with 2 shadow constructs that last 9 sec (up to 6 alive). Mind Blast detonates your Shadow Word: Pain on the target, dealing its remaining damage at once and refreshing it to full. Mind Flay deals quadruple damage.",
          lines = {
            { spell = 34433, text = "Shadowfiend: no cooldown." },
            { spell = 15473, text = "Voidform: learned for free, a toggle. While it is up, a free Mind Blast fires at your target every 3 sec." },
            { spell = 48156, text = "Mind Flay: deals quadruple damage, and each channel spawns 2 shadow constructs that last 9 sec (up to 6 alive)." },
            { spell = 48127, text = "Mind Blast: detonates your Shadow Word: Pain on the target -- its remaining damage dealt at once, then refreshed to full." },
          } },
    },
}

-- Lookup by spell id from server CPK lines. Do not key the Class tab on UnitClass.
local CLASS_PERK_BY_ID = {}
for _, list in pairs(CLASS_PERKS) do
    for i = 1, #list do
        CLASS_PERK_BY_ID[list[i].id] = list[i]
    end
end

-- Fixed display order for the class-browse tabs, independent of table iteration order.
-- Alphabetical (matches the class-tab grid, 5 per row / 2 rows).
local CLASS_ORDER = { "DEATHKNIGHT", "DRUID", "HUNTER", "MAGE", "PALADIN", "PRIEST", "ROGUE", "SHAMAN", "WARLOCK", "WARRIOR" }
local CLASS_LABEL = {
    WARRIOR = "Warrior", PALADIN = "Paladin", HUNTER = "Hunter", ROGUE = "Rogue", PRIEST = "Priest",
    DEATHKNIGHT = "Death Knight", SHAMAN = "Shaman", MAGE = "Mage", WARLOCK = "Warlock", DRUID = "Druid",
}
-- Standard WoW class colors, hardcoded rather than read from the client's
-- own RAID_CLASS_COLORS global -- this addon's FrameXML-baked copy can
-- build its UI before that global is guaranteed populated, and a wrong/nil
-- read here would silently mis-color every tab rather than error.
local CLASS_COLOR = {
    DEATHKNIGHT = { 0.77, 0.12, 0.23 },
    DRUID       = { 1.00, 0.49, 0.04 },
    HUNTER      = { 0.67, 0.83, 0.45 },
    MAGE        = { 0.41, 0.80, 0.94 },
    PALADIN     = { 0.96, 0.55, 0.73 },
    PRIEST      = { 1.00, 1.00, 1.00 },
    ROGUE       = { 1.00, 0.96, 0.41 },
    SHAMAN      = { 0.00, 0.44, 0.87 },
    WARLOCK     = { 0.58, 0.51, 0.79 },
    WARRIOR     = { 0.78, 0.61, 0.43 },
}
-- Only classes with an entry in CLASS_PERKS get a tab; this grows automatically as more get added.
local CLASS_TAB_LIST = {}
for _, token in ipairs(CLASS_ORDER) do
    if CLASS_PERKS[token] then
        table.insert(CLASS_TAB_LIST, token)
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
    classBrowse = nil, -- class token currently shown in the Class tab; defaults to the player's own class
    rules = {},
    vault = {},
    autoloot = { on = 0, corpses = 0, need = 10, de = 0 },
    attune = { on = 1, count = 0, off = 0 },
    -- [itemId] = true for every item entry attuned on this account. Fed by
    -- ATL| (batched, at sync) and ATT| (one entry, live as it attunes);
    -- read only by the tooltip's ATTUNED line.
    attuned = {},
    tab = "items",
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
    reagentCat = "All",
    jump = { mode = 2, max = 0 },
    -- Wayfarer: pct is the share of the dial spent on damage, cap is how far
    -- the unlocked tiers let it reach (0 = not unlocked), cd is seconds left
    -- on the swap cooldown. Replaced wholesale by every WAY| line.
    way = { pct = 0, cap = 0, cd = 0 },
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

function LG2.SplitPipe(s)
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

local function StyleBtnColor(btn, color)
    StyleBtn(btn, color[1], color[2], color[3])
end

function LG2.SendAttune(slot)
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

-- Refund is all-or-nothing and cannot be undone cheaply (there is a cooldown
-- on the server), so it asks first. Perks earned by playing are untouched --
-- the server only revokes rows that have a matching purchase.
StaticPopupDialogs["LG_PERK_RESPEC"] = {
    text = "Refund every perk you have bought and get those points back?\n\nPerks you earned by playing are not affected.",
    button1 = YES,
    button2 = NO,
    OnAccept = function()
        SendLine("PERKRESPEC")
    end,
    timeout = 0,
    whileDead = true,
    hideOnEscape = true,
}

function LG2.IsCriticalChatText(text)
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

function LG2.IsLivingGearChatText(text)
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

function LG2.ShouldFilterChatText(text)
    if LivingGearDB.showChat then
        return false
    end
    if not LG2.IsLivingGearChatText(text) then
        return false
    end
    if LG2.IsCriticalChatText(text) then
        return false
    end
    return true
end

local chatFilterInstalled = false

function LG2.InstallChatFilter()
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
            if LG2.ShouldFilterChatText(text) then
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

function LG2.SetShowChat(on)
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

function LG2.BankHostFrame()
    for i = 1, #BANK_HOSTS do
        local f = _G[BANK_HOSTS[i]]
        if f and f.IsShown and f:IsShown() then
            return f
        end
    end
    return nil
end

function LG2.EnsureBankDeposit()
    if ui.bankDeposit then
        return ui.bankDeposit
    end
    local btn = CreateFrame("Button", "LivingGearBankDeposit", UIParent)
    btn:SetSize(118, 22)
    btn:SetFrameStrata("HIGH")
    btn:SetFrameLevel(200)
    StyleBtn(btn, COLOR_ADD[1], COLOR_ADD[2], COLOR_ADD[3])
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
    local btn = LG2.EnsureBankDeposit()
    btn:ClearAllPoints()
    local host = LG2.BankHostFrame()
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
    if m == 14 then
        local rng = rule.text ~= "" and rule.text or "?"
        return string.format("Item Level %s %s -> %s", neg and "not in" or "in", rng, act)
    end
    if m == 6 then
        -- rule.negate is a 4-value comparison op here (0..3), not a plain
        -- negate bit -- see RuleMatches server-side for why.
        local q = RULE_QUALS[(tonumber(rule.quality) or 0) + 1] or "?"
        local qop = RULE_QUAL_OPS[(tonumber(rule.negate) or 0) + 1] or "=="
        return string.format("Quality %s %s -> %s", qop, q, act)
    end
    if m == 1 then
        return string.format("Quality %s Grey -> %s", neg and "!=" or "==", act)
    end
    local n = MATCH_TO_NAME[m] or "?"
    return string.format("Type %s %s -> %s", neg and "!=" or "==", n, act)
end

function LG2.DefaultRules()
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
    return LG2.DefaultRules()
end

function LG2.ActionIndex(name)
    name = string.lower(name or "")
    for i = 1, #ACTION_NAMES do
        if string.lower(ACTION_NAMES[i]) == name then
            return i - 1
        end
    end
    return nil
end

function LG2.ExportRules()
    local rules = ActiveRules()
    local lines = {}
    for i = 1, #rules do
        table.insert(lines, RuleText(rules[i]))
    end
    return table.concat(lines, "\n")
end

function LG2.ParseImportLine(line)
    line = string.gsub(line or "", "^%s+", "")
    line = string.gsub(line, "%s+$", "")
    if line == "" then
        return nil
    end
    local left, actName = string.match(line, "^(.-)%s*->%s*(.+)$")
    if not left then
        return nil
    end
    local action = LG2.ActionIndex(actName)
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
    local op, qual = string.match(left, "^Quality%s+(==|!=|>=|<=)%s+(%S+)$")
    if op and qual then
        local q = nil
        for i = 1, #RULE_QUALS do
            if string.lower(RULE_QUALS[i]) == string.lower(qual) then
                q = i - 1
                break
            end
        end
        if q then
            local qopIdx = 0
            for i = 1, #RULE_QUAL_OPS do
                if RULE_QUAL_OPS[i] == op then
                    qopIdx = i - 1
                    break
                end
            end
            return { match = 6, action = action, negate = qopIdx, quality = q, text = "" }
        end
    end
    local ilOp, ilRange = string.match(left, "^Item Level%s+(in|not in)%s+(%d+%-%d+)$")
    if ilOp and ilRange then
        return { match = 14, action = action, negate = ilOp == "not in" and 1 or 0, quality = 0, text = ilRange }
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

function LG2.SendRuleAdd(rule)
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
        LG2.SendRuleAdd(rules[i])
    end
end

function LG2.InsertRule(rule)
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

-- Prices come from the server (lg_perk_cost), never from this file: they are
-- meant to be retuned without shipping a new addon. A perk with no price row
-- is not purchasable at all -- that is how the multiplier tracks stay gated on
-- their conditions instead of showing up as something to buy.
function LG2.PerkCost(id)
    local c = db.cost and db.cost[id]
    return c and c.cost or nil
end

function LG2.PerkPoints()
    local p = db.points or {}
    return tonumber(p.avail) or 0, tonumber(p.earned) or 0, tonumber(p.spent) or 0
end

function LG2.PerkPrereqOk(id)
    local c = db.cost and db.cost[id]
    local need = c and tonumber(c.prereq) or 0
    return need == 0 or PerkKnown(need)
end

-- Buyable means all four: priced, not already owned, prerequisite held, and
-- affordable. The server re-checks every one of these -- this only decides
-- whether the pip lights up.
function LG2.PerkBuyable(id)
    local cost = LG2.PerkCost(id)
    if not cost or PerkKnown(id) or not LG2.PerkPrereqOk(id) then
        return false
    end
    local avail = LG2.PerkPoints()
    return avail >= cost
end

-- The Perks tab shows progression tracks only. The one-off unlocks moved to
-- their own tab: they are a different kind of thing -- a capability you either
-- have or do not -- and rendering them as a single pip next to a six-rank
-- ladder made them read as rank 1 of something.
function LG2.PerkRowData()
    local out = {}
    for t = 1, #WORLD_TRACKS do
        table.insert(out, { label = WORLD_TRACKS[t].name, ticks = WORLD_TRACKS[t].ticks })
    end
    return out
end

-- Colour and tooltip for one pip. Four states, and the fourth is the one that
-- matters: a perk with no price is not for sale rather than unaffordable, and
-- drawing those two the same way would be a lie.
function LG2.StylePerkPip(pip, info)
    local cost = info and LG2.PerkCost(info.id) or nil
    local avail = LG2.PerkPoints()
    if not info then
        pip.bg:SetTexture(0.07, 0.07, 0.08)
        pip.tip = nil
    elseif PerkKnown(info.id) then
        pip.bg:SetTexture(0.28, 0.62, 0.32)
        pip.tip = info.name .. " - " .. info.how .. "|n|cff7fdc7fOwned.|r"
    elseif LG2.PerkBuyable(info.id) then
        pip.bg:SetTexture(0.85, 0.70, 0.25)
        pip.tip = info.name .. " - " .. info.how
            .. "|n|cffffd966Costs " .. cost .. " points. Click to unlock.|r"
    elseif cost then
        pip.bg:SetTexture(0.10, 0.10, 0.12)
        if not LG2.PerkPrereqOk(info.id) then
            pip.tip = info.name .. " - " .. info.how
                .. "|n|cff999999Unlock the previous rank first.|r"
        else
            pip.tip = info.name .. " - " .. info.how
                .. "|n|cff999999Costs " .. cost .. " points - you have " .. avail .. ".|r"
        end
    else
        pip.bg:SetTexture(0.16, 0.18, 0.26)
        pip.tip = info.name .. " - " .. info.how .. "|n|cff8a9bc4Earned by playing.|r"
    end
end

-- Shared scaffolding for both perk tabs: a scroll frame with a header above it.
--
-- Guarded because CreateFrame throws on an unknown template and this runs from
-- ADDON_LOADED, where that would take the whole addon down. Without the scroll
-- frame the rows parent to the panel and the tail is clipped, which is worse
-- than scrolling but far better than no tab at all.
function LG2.MakePerkScroll(panel, name, rowCount, rowH)
    local host = panel
    pcall(function()
        local scroll = CreateFrame("ScrollFrame", name, panel, "UIPanelScrollFrameTemplate")
        scroll:SetPoint("TOPLEFT", 0, -44)
        scroll:SetPoint("BOTTOMRIGHT", -26, 4)
        local child = CreateFrame("Frame", nil, scroll)
        child:SetSize(660, rowCount * rowH + 8)
        scroll:SetScrollChild(child)
        panel.scroll, host = scroll, child
    end)
    return host
end

-- Progression tracks. Name, the rank pips, then what the next rank actually
-- does and what it costs -- all inline.
--
-- The cost and the effect are deliberately NOT tooltip-only. Deciding whether
-- to spend points should not require hovering every row in turn; a tooltip is
-- for detail you want occasionally, not for the one fact the screen exists to
-- convey.
--
-- Two lines per row because that is what the descriptions actually need. At one
-- line the longer ones wrapped anyway and collided with the row below, which
-- looked like a bug rather than a layout. Scrolling is free here; overlap is not.
function LG2.BuildAchievementPerkRows(panel)
    local data = LG2.PerkRowData()
    local ROW_H, NAME_W, PIP_X, PIP_STEP, NEXT_X = 28, 116, 122, 11, 240
    local host = LG2.MakePerkScroll(panel, "LivingGearPerkScroll", #data, ROW_H)
    panel.rows = {}
    for i = 1, #data do
        local row = CreateFrame("Frame", nil, host)
        row:SetSize(640, ROW_H - 2)
        row:SetPoint("TOPLEFT", 4, -4 - (i - 1) * ROW_H)
        row.entry = data[i]

        row.name = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        row.name:SetPoint("TOPLEFT", 2, -1)
        row.name:SetWidth(NAME_W)
        row.name:SetJustifyH("LEFT")

        -- Tracks you cannot buy into show "4 / 10" instead of ten dead squares.
        -- Ten unclickable pips were both the widest thing on the panel and the
        -- least informative, and they invited clicks that do nothing.
        row.count = row:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
        row.count:SetPoint("TOPLEFT", PIP_X, -1)
        row.count:Hide()

        row.next = row:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
        row.next:SetPoint("TOPLEFT", NEXT_X, -1)
        row.next:SetWidth(640 - NEXT_X - 4)
        row.next:SetHeight(ROW_H - 4)
        row.next:SetJustifyH("LEFT")
        row.next:SetJustifyV("TOP")

        row.pips = {}
        for k = 1, #data[i].ticks do
            local pip = CreateFrame("Button", nil, row)
            pip:SetSize(9, 10)
            pip:SetPoint("TOPLEFT", PIP_X + (k - 1) * PIP_STEP, -2)
            pip.bg = pip:CreateTexture(nil, "ARTWORK")
            pip.bg:SetAllPoints(pip)
            pip.info = data[i].ticks[k]
            pip:SetScript("OnClick", function(self)
                if self.info and LG2.PerkBuyable(self.info.id) then
                    SendLine("PERKBUY|" .. tostring(self.info.id))
                end
            end)
            pip:SetScript("OnEnter", function(self)
                if not self.tip then
                    return
                end
                GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
                GameTooltip:SetText(self.tip, 1, 1, 1, 1, true)
                GameTooltip:Show()
            end)
            pip:SetScript("OnLeave", function()
                GameTooltip:Hide()
            end)
            row.pips[k] = pip
        end
        panel.rows[i] = row
    end
end

-- One-off unlocks. Icon, name, the whole description, and the price, with
-- nothing hidden behind a hover.
function LG2.BuildAchievementUnlockRows(panel)
    local ROW_H = 26
    local host = LG2.MakePerkScroll(panel, "LivingGearUnlockScroll", #WORLD_UNLOCKS, ROW_H)
    panel.rows = {}
    for i = 1, #WORLD_UNLOCKS do
        local info = WORLD_UNLOCKS[i]
        local row = CreateFrame("Button", nil, host)
        row:SetSize(640, ROW_H - 2)
        row:SetPoint("TOPLEFT", 4, -4 - (i - 1) * ROW_H)
        row.info = info

        row.icon = row:CreateTexture(nil, "ARTWORK")
        row.icon:SetSize(20, 20)
        row.icon:SetPoint("LEFT", 2, 0)
        row.icon:SetTexture(info.icon or "Interface\\Icons\\INV_Misc_QuestionMark")

        row.name = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        row.name:SetPoint("LEFT", 28, 0)
        row.name:SetWidth(116)
        row.name:SetJustifyH("LEFT")
        row.name:SetText(info.name)

        row.how = row:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
        row.how:SetPoint("LEFT", 148, 0)
        row.how:SetWidth(430)
        row.how:SetJustifyH("LEFT")
        row.how:SetText(info.how or "")

        row.cost = row:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
        row.cost:SetPoint("RIGHT", -4, 0)
        row.cost:SetJustifyH("RIGHT")

        row:SetScript("OnClick", function(self)
            if LG2.PerkBuyable(self.info.id) then
                SendLine("PERKBUY|" .. tostring(self.info.id))
            end
        end)
        row:SetScript("OnEnter", function(self)
            self.hl:Show()
        end)
        row:SetScript("OnLeave", function(self)
            self.hl:Hide()
        end)
        row.hl = row:CreateTexture(nil, "BACKGROUND")
        row.hl:SetAllPoints(row)
        row.hl:SetTexture(0.18, 0.18, 0.20)
        row.hl:Hide()

        panel.rows[i] = row
    end
end

local function RefreshPerkHeader(panel)
    if not panel or not panel.points then
        return
    end
    local avail, earned, spent = LG2.PerkPoints()
    panel.points:SetText(string.format(
        "|cffffd966%d|r to spend   |cff999999%d earned, %d spent|r", avail, earned, spent))
    if panel.respec then
        if spent > 0 then
            panel.respec:Show()
        else
            panel.respec:Hide()
        end
    end
end

function LG2.RefreshAchievementPerks()
    local panel = LG2.achPanel
    if panel and panel.rows then
        RefreshPerkHeader(panel)
        local avail = LG2.PerkPoints()
        for i = 1, #panel.rows do
            local row = panel.rows[i]
            local total, nextId, nextHow = #row.entry.ticks, nil, nil
            local owned, priced = 0, false
            for k = 1, total do
                local tick = row.entry.ticks[k]
                LG2.StylePerkPip(row.pips[k], tick)
                if LG2.PerkCost(tick.id) then
                    priced = true
                end
                if PerkKnown(tick.id) then
                    owned = owned + 1
                elseif not nextId then
                    nextId, nextHow = tick.id, tick.how
                end
            end
            row.name:SetText(row.entry.label)

            -- Costs arrive asynchronously, so this is decided every refresh
            -- rather than baked in at build time.
            for k = 1, total do
                if priced then
                    row.pips[k]:Show()
                else
                    row.pips[k]:Hide()
                end
            end
            if priced then
                row.count:Hide()
            else
                row.count:SetText(string.format("|cff8a9bc4%d / %d|r", owned, total))
                row.count:Show()
            end
            local cost = nextId and LG2.PerkCost(nextId) or nil
            if not nextId then
                row.next:SetText("|cff4fd14fComplete.|r")
            elseif cost and LG2.PerkBuyable(nextId) then
                row.next:SetText(string.format("|cffffd966%d pts|r  %s", cost, nextHow or ""))
            elseif cost then
                row.next:SetText(string.format("|cff777777%d pts  %s|r", cost, nextHow or ""))
            else
                -- Earned rather than bought, so there is no price to show -- but
                -- the rank's own text already says what earns it ("Win a
                -- battleground", "Have 2 characters at level 80"). Saying only
                -- "Earned by playing" threw that away and told the player
                -- nothing they could act on.
                row.next:SetText("|cff8a9bc4" .. (nextHow or "Earned by playing") .. "|r")
            end
        end
    end

    local up = LG2.achUnlockPanel
    if up and up.rows then
        RefreshPerkHeader(up)
        local avail = LG2.PerkPoints()
        for i = 1, #up.rows do
            local row = up.rows[i]
            local id = row.info.id
            local cost = LG2.PerkCost(id)
            if PerkKnown(id) then
                row.cost:SetText("|cff4fd14fowned|r")
                row.icon:SetVertexColor(1, 1, 1)
                row.name:SetTextColor(0.88, 0.9, 0.94)
            elseif LG2.PerkBuyable(id) then
                row.cost:SetText(string.format("|cffffd966%d pts|r", cost))
                row.icon:SetVertexColor(1, 1, 1)
                row.name:SetTextColor(0.95, 0.82, 0.35)
            elseif cost then
                row.cost:SetText(string.format("|cff777777%d pts (have %d)|r", cost, avail))
                row.icon:SetVertexColor(0.35, 0.35, 0.35)
                row.name:SetTextColor(0.5, 0.5, 0.55)
            else
                row.cost:SetText("|cff5a6a8aearned by playing|r")
                row.icon:SetVertexColor(0.45, 0.5, 0.6)
                row.name:SetTextColor(0.54, 0.6, 0.72)
            end
        end
    end
end

function LG2.NextLocked(ticks)
    for i = 1, #ticks do
        if not PerkKnown(ticks[i].id) then
            return ticks[i]
        end
    end
    return nil
end

function LG2.TrackBonus(ticks)
    local bonus = 0
    for i = 1, #ticks do
        if PerkKnown(ticks[i].id) then
            bonus = bonus + (ticks[i].bonus or 0)
        end
    end
    return bonus
end

-- Gathering tracks (unit = "gather") mix two different kinds of tick in one
-- list: reach ticks and yield ticks. Each rank's own text is already the
-- CUMULATIVE total at that rank ("+9 yards", "8x"), not an increment to add
-- to the others, so both need max-of-unlocked, not TrackBonus()'s sum --
-- summing showed nonsense like "(18%)" for +3/+6/+9 yards stacked, and
-- yield never showed at all since its ticks all had bonus = 0.
function LG2.TrackReach(ticks)
    local reach = 0
    for i = 1, #ticks do
        if PerkKnown(ticks[i].id) and ticks[i].bonus and ticks[i].bonus > reach then
            reach = ticks[i].bonus
        end
    end
    return reach
end

function LG2.TrackYield(ticks)
    local mult = 0
    for i = 1, #ticks do
        if PerkKnown(ticks[i].id) and ticks[i].yield and ticks[i].yield > mult then
            mult = ticks[i].yield
        end
    end
    return mult
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

function LG2.LoadScale()
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

function LG2.ToggleScaleMenu()
    if not ui.scaleMenu then
        return
    end
    if ui.scaleMenu:IsShown() then
        ui.scaleMenu:Hide()
    else
        ui.scaleMenu:Show()
    end
end

function LG2.DefaultWorldTip()
    for i = 1, #WORLD_UNLOCKS do
        local info = WORLD_UNLOCKS[i]
        if not PerkKnown(info.id) then
            return "Next: " .. info.name .. " - " .. info.how
        end
    end
    for t = 1, #WORLD_TRACKS do
        local track = WORLD_TRACKS[t]
        local nxt = LG2.NextLocked(track.ticks)
        if nxt then
            return track.name .. " next: " .. nxt.name .. " - " .. nxt.how
        end
    end
    return "All world perks unlocked."
end

local function SetWorldTip(text)
    if ui.worldTip then
        ui.worldTip:SetText(text or LG2.DefaultWorldTip())
    end
end

-- PaintTick was removed 2026-08-22 with the per-tier tick buttons. The tier
-- bar in the PROGRESSION cards colours its own segments (see LayoutWorld),
-- so there is nothing left for it to paint.

local function ShowTab(name)
    -- db.tab persists across sessions, so a saved value naming a retired tab
    -- would otherwise select nothing and leave the window blank.
    if name == "quest" or name == "world" then
        name = "class"
    elseif name == "gear" or name == "attune" or name == "armory" then
        name = "items"
    end
    db.tab = name
    -- The retired panels are still constructed and other code paths still call
    -- Show() on them, so they have to be hidden explicitly. Dropping them from
    -- the map below did not hide them -- it stopped anything from ever hiding
    -- them, and they drew straight through whichever tab was selected.
    local retired = { ui.world, ui.gear, ui.attune }
    for i = 1, #retired do
        if retired[i] then
            retired[i]:Hide()
        end
    end

    local panels = {
        class = ui.class,
        items = ui.items,
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

function LG2.LayoutGear()
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
        if it then
            row.slot = it.slot
            row.invSlot = invSlot
            row.name:SetText(it.name or info.name)
            -- Real item rarity color (character-sheet style), not the old
            -- fixed near-white -- GetInventoryItemQuality is the client's
            -- own authority on the equipped item's actual quality, no
            -- server data needed for this (2026-08-20).
            local realQ = GetInventoryItemQuality("player", invSlot)
            local nr, ng, nb = GetItemQualityColor(realQ or 1)
            row.name:SetTextColor(nr, ng, nb, 1)
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
            local r, g, b = GetItemQualityColor(LevelQuality(lv))
            -- 25 (LG_ATTUNE_CAP_LEVEL) is where attunement actually caps
            -- out now (2026-08-20) -- showing 26, 27, 28... up to 50 here
            -- would read as "still capping" when there's really nothing
            -- left to attune, just optional further personal growth.
            if lv >= LG_ATTUNE_CAP_LEVEL then
                row.level.label:SetText("MAX")
            else
                row.level.label:SetText(tostring(lv))
            end
            row.level.label:SetTextColor(r, g, b, 1)
            if lv >= LG_MAX_LEVEL then
                row.level.tip = { info.name, "Level " .. tostring(lv) .. " (max)" }
            elseif lv >= LG_ATTUNE_CAP_LEVEL then
                row.level.tip = { info.name, "Fully attuned (level " .. tostring(lv)
                    .. ") -- still growing toward level " .. tostring(LG_MAX_LEVEL) }
            else
                row.level.tip = { info.name, string.format("Level %s -- %s / %s XP to fully attuned", lv, xp, need) }
            end
            row.level:Show()
        else
            row.slot = nil
            row.invSlot = nil
            row.name:SetText(info.name)
            row.name:SetTextColor(0.45, 0.45, 0.45, 1)
            row.stats:SetText("")
            row.icon:SetTexture(info.tex)
            row.icon:SetTexCoord(0, 1, 0, 1)
            row.icon:SetVertexColor(0.7, 0.7, 0.7, 0.9)
            row.level.tip = nil
            row.level:Hide()
        end
    end
    if ui.empty then
        ui.empty:Hide()
    end
    local ab = db.absorb
    ui.absorb:SetText(string.format("Attuned (%s) Items:", ab.count or 0))
    local vals = {
        { ab.str, "str" }, { ab.agi, "agi" }, { ab.sta, "sta" },
        { ab.intel, "int" }, { ab.spi, "spi" }, { ab.armor, "armor" },
    }
    local n = 0
    for i = 1, #vals do
        local v = tonumber(vals[i][1]) or 0
        if v ~= 0 and ui.absorbChips[n + 1] then
            n = n + 1
            local chip = ui.absorbChips[n]
            chip:SetText(string.format("+%.0f %s", v, vals[i][2]))
            chip:Show()
        end
    end
    for i = n + 1, #ui.absorbChips do
        ui.absorbChips[i]:Hide()
    end
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
            StyleBtn(ui.alToggle, COLOR_ON[1], COLOR_ON[2], COLOR_ON[3])
        else
            StyleBtn(ui.alToggle, COLOR_OFF[1], COLOR_OFF[2], COLOR_OFF[3])
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
        local base = usingDefault and "Default rules. Edit to customize. Reset restores these."
            or "Custom rules. First match wins."
        -- The list scrolls via mouse wheel over it (db.ruleOff), but that
        -- was completely undiscoverable -- with no scrollbar and no visible
        -- affordance, rules past RULE_ROWS looked silently dropped instead
        -- of just off-screen.
        if #rules > RULE_ROWS then
            base = base .. string.format("  (%d rules -- scroll list for more)", #rules)
        end
        ui.ruleHint:SetText(base)
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
        if ui.ruleType then
            ui.ruleType:Hide()
        end
        if ui.ruleQual then
            ui.ruleQual:Hide()
        end
        if ui.ruleNameWrap then
            ui.ruleNameWrap:Hide()
        end
        if db.ruleField == 3 then
            ui.ruleOp.label:SetText(RULE_NAME_OPS[db.ruleOp] or "Matches")
            if ui.ruleNameWrap then
                ui.ruleNameWrap:Show()
            end
        elseif db.ruleField == 4 then
            ui.ruleOp.label:SetText(RULE_OPS[db.ruleOp] or "==")
            if ui.ruleNameWrap then
                ui.ruleNameWrap:Show()
            end
        elseif db.ruleField == 2 then
            ui.ruleOp.label:SetText(RULE_QUAL_OPS[db.ruleOp] or "==")
            if ui.ruleQual then
                ui.ruleQual:Show()
                ui.ruleQual.label:SetText(RULE_QUALS[db.ruleQual] or "Grey")
            end
        else
            ui.ruleOp.label:SetText(RULE_OPS[db.ruleOp] or "==")
            if ui.ruleType then
                ui.ruleType:Show()
                ui.ruleType.label:SetText(RULE_TYPES[db.ruleType] or "Quest")
            end
        end
        ui.ruleThen.label:SetText(ACTION_NAMES[db.ruleAction] or "Bags")
        if ui.rulePreview then
            local matchVal = RULE_TYPE_MATCH[db.ruleType]
            if db.ruleField == 3 then
                matchVal = 5
            elseif db.ruleField == 2 then
                matchVal = 6
            elseif db.ruleField == 4 then
                matchVal = 14
            end
            ui.rulePreview:SetText(RuleText({
                match = matchVal,
                action = db.ruleAction - 1,
                negate = db.ruleField == 2 and (db.ruleOp - 1) or (db.ruleOp == 2 and 1 or 0),
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
        -- GetItemFamily can return zero values (not even nil) for an item
        -- the client hasn't locally cached yet -- tonumber() called with no
        -- argument at all throws "value expected" rather than just giving
        -- back nil. The extra parens force the call's result down to
        -- exactly one value (nil if it returned nothing) before tonumber
        -- ever sees it.
        fam = tonumber((GetItemFamily(id))) or 0
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
    -- No family-bit shortcut for Leather: the 0x80 bag-family bit this used to
    -- read is ENGINEERING_SUPP, not a leather marker (BAG_FAMILY_MASK in
    -- ItemTemplate.h), so raw hides landed in the same bucket as engineering
    -- parts and clicking "Leather" showed a wall of engineering items. The
    -- subclass check below ("Leather") is the correct classifier.
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
        return ""
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

-- id -> total count across the vault, rebuilt only when the vault actually
-- changes (LG2._vaultGen).
--
-- This used to be a linear scan of db.vault per call, which was fine while the
-- only caller was the Reagents panel. It is not fine now that GetTradeSkillInfo
-- consults it: TradeSkillFrame_Update asks about every row of a several-hundred
-- recipe list, each with up to eight reagents, so the scan version would be
-- hundreds of thousands of table walks per redraw and would visibly hang the
-- profession window.
function LG2.VaultMap()
    local gen = LG2._vaultGen or 0
    if LG2._vaultMap and LG2._vaultMapGen == gen then
        return LG2._vaultMap
    end
    local m = {}
    for i = 1, #db.vault do
        local it = db.vault[i]
        local id = tonumber(it.entry)
        if id then
            m[id] = (m[id] or 0) + (tonumber(it.count) or 0)
        end
    end
    LG2._vaultMap, LG2._vaultMapGen = m, gen
    return m
end

local function VaultCountOf(entry)
    local want = tonumber(entry)
    if not want then
        return 0
    end
    return LG2.VaultMap()[want] or 0
end

function LG2.ItemIdFromArg(item)
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
            row.itemName = it.name
            row.link = LG2.ItemLinkText(it.entry, it.name)
            row.text:SetText(string.format("%s  |cff909090x%s|r", row.link, it.count or 0))
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
        -- Reagents: no "1-14 / 109" readout. Every row is one item TYPE with a
        -- stack count, so that number counted types, not items -- useless at a
        -- glance and the user asked it gone. Quest vault keeps the range since
        -- its list is short enough to eyeball but scrolled.
        if kind ~= VAULT_REAGENT and #list > #rows then
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

function LG2.PlayerClassToken()
    if not UnitClass then
        return nil
    end
    local _, englishClass = UnitClass("player")
    return englishClass
end

-- Which class the Class tab is currently showing. Defaults to (and stays
-- pinned to) the player's own class until they click another class's tab.
function LG2.ClassBrowseToken()
    if not db.classBrowse then
        db.classBrowse = LG2.PlayerClassToken() or CLASS_TAB_LIST[1]
    end
    return db.classBrowse
end

local function LayoutClass()
    if not ui.class then
        return
    end
    local browse = LG2.ClassBrowseToken()
    local ownClass = browse == LG2.PlayerClassToken()

    if ui.classTabs then
        for i = 1, #ui.classTabs do
            local tabBtn = ui.classTabs[i]
            local on = tabBtn.token == browse
            -- Background stays the plain button color always; the class
            -- color lives in the text, at full brightness when this is the
            -- class currently being browsed and dimmed otherwise. The
            -- background still gets a faint tint when selected so the
            -- active tab reads clearly even at a glance.
            local c = tabBtn.classColor or COLOR_TEXT
            if on then
                StyleBtnColor(tabBtn, { c[1] * 0.22, c[2] * 0.22, c[3] * 0.22 })
                tabBtn.label:SetTextColor(c[1], c[2], c[3], 1)
            else
                StyleBtnColor(tabBtn, COLOR_BTN)
                tabBtn.label:SetTextColor(c[1] * 0.6, c[2] * 0.6, c[3] * 0.6, 1)
            end
        end
    end

    -- Own class uses the server-synced pick (db.classPerks); other classes
    -- are preview-only, so just show every perk that class has defined.
    local list
    if ownClass then
        list = db.classPerks or {}
    else
        list = {}
        for _, info in ipairs(CLASS_PERKS[browse] or {}) do
            table.insert(list, info.id)
        end
    end

    if #list == 0 then
        ui.classEmpty:Show()
        ui.classEmpty:SetText("No class perk data yet for " .. (CLASS_LABEL[browse] or browse) .. ".")
        for i = 1, #ui.classBtns do
            ui.classBtns[i]:Hide()
        end
        return
    end
    ui.classEmpty:Hide()

    -- "Sentence one. Sentence two." -> {"Sentence one", "Sentence two"},
    -- shown as the card's bulleted list. Every how= string in CLASS_PERKS
    -- is authored as clean "X. Y." sentences, so splitting on ". " is
    -- reliable here without needing a separate structured bullets field.
    local function SplitHow(text)
        local parts = {}
        if not text or text == "" then
            return parts
        end
        local norm = string.gsub(text, "%.%s+", ".\n")
        for line in string.gmatch(norm, "[^\n]+") do
            line = string.gsub(line, "%.$", "")
            line = string.gsub(line, "^%s+", "")
            if line ~= "" then
                table.insert(parts, line)
            end
        end
        return parts
    end
    for i = 1, #ui.classBtns do
        local btn = ui.classBtns[i]
        local id = list[i]
        local info = id and CLASS_PERK_BY_ID[id]
        if id then
            btn:Show()
            btn.id = id
            btn.ownClass = ownClass
            btn.how = info and info.how or "Class perk."
            local name = info and info.name
            local icon
            -- GetSpellTexture doesn't exist in this client (3.3.5 -- it's a
            -- later-expansion API); GetSpellInfo's 3rd return value is the
            -- correct way to get a spell's icon here. Was silently falling
            -- back to a question-mark icon for every card until this was
            -- fixed (2026-08-20, caught from a screenshot).
            if GetSpellInfo then
                local specName, _, specIcon = GetSpellInfo(id)
                icon = specIcon
                if not name then
                    name = specName
                end
            end
            btn.label:SetText(name or ("Perk " .. tostring(id)))
            btn.icon:SetTexture(icon or "Interface\\Icons\\INV_Misc_QuestionMark")
            -- Icon + title sit as one centred group, not icon-far-left and
            -- title-centred-on-its-own: measure the rendered title and place
            -- the icon just left of where the text actually starts.
            local iconSize = 22
            local textW = btn.label:GetStringWidth() or 0
            local groupW = iconSize + 4 + textW
            -- NOTE: CLASS_CARD_W is a BuildUI local, not in scope here; the
            -- literal must stay in sync with it.
            local cardW = btn:GetWidth() or 196
            local ix = math.floor((cardW - groupW) / 2)
            if ix < 4 then
                ix = 4
            end
            btn.icon:ClearAllPoints()
            btn.icon:SetPoint("TOPLEFT", ix, -6)
            btn.label:ClearAllPoints()
            btn.label:SetPoint("TOPLEFT", ix + iconSize + 4, -6)
            btn.label:SetPoint("TOPRIGHT", -6, -6)
            btn.label:SetHeight(22)
            btn.label:SetJustifyH("LEFT")
            btn.label:SetJustifyV("MIDDLE")
            local picked = ownClass and db.classPerk == id
            if picked then
                btn._ir, btn._ig, btn._ib = 0.12, 0.32, 0.14
            elseif ownClass then
                btn._ir, btn._ig, btn._ib = 0.14, 0.14, 0.14
            else
                btn._ir, btn._ig, btn._ib = 0.11, 0.11, 0.11
            end
            Solid(btn.bg, btn._ir, btn._ig, btn._ib, 1)

            -- Bulleted list: the perk's own "how" sentences first, then
            -- (Rogue Subtlety only, right now) its subPerks as extra lines
            -- -- everything about this card lives in the card, nothing
            -- needs a hover anymore. "*" as the bullet marker, not "-" --
            -- client strings are ASCII-only in this repo (no real bullet
            -- glyph), and a hyphen next to a "+300%"-style line read as a
            -- minus sign at a glance.
            -- Two renderers. A spec that has authored `lines` gets icon bullets
            -- with real spell tooltips; anything still on plain `how` prose
            -- falls back to the old asterisk block, so converting the remaining
            -- classes is incremental rather than all-or-nothing.
            local structured = info and info.lines
            if structured then
                btn.body:Hide()
                local rows = {}
                for _, ln in ipairs(structured) do
                    table.insert(rows, { spell = ln.spell, text = ln.text })
                end
                if info.subPerks then
                    for _, sub in ipairs(info.subPerks) do
                        local known = ownClass and PerkKnown(sub.id)
                        table.insert(rows, {
                            spell = sub.spell or sub.id,
                            text = (known and "|cff4fd14f" or "|cff9a9a9a") .. sub.name .. "|r  " .. (sub.how or ""),
                        })
                    end
                end
                -- Stacked by measured height rather than a fixed row pitch: a
                -- one-line bullet and a four-line one differ by 40px, and any
                -- fixed pitch is either wasteful for the short ones or overlaps
                -- the long ones.
                local y = -32
                for bi = 1, #btn.bullets do
                    local bl = btn.bullets[bi]
                    local ln = rows[bi]
                    if not ln then
                        bl:Hide()
                    else
                        bl.spell = ln.spell
                        bl.text:SetWidth(btn._textW)
                        bl.text:SetText(ln.text or "")
                        local h = bl.text:GetStringHeight() or 12
                        if h < 14 then
                            h = 14
                        end
                        if ln.spell and GetSpellInfo then
                            local _, _, tex = GetSpellInfo(ln.spell)
                            bl.icon:SetTexture(tex or "Interface\\Icons\\INV_Misc_QuestionMark")
                            bl.icon:SetVertexColor(1, 1, 1)
                        else
                            -- No single ability owns this line ("all your
                            -- poisons"), so it gets a plain marker instead of a
                            -- borrowed icon that would imply the wrong spell.
                            bl.icon:SetTexture("Interface\\Buttons\\WHITE8X8")
                            bl.icon:SetVertexColor(0.42, 0.42, 0.42)
                        end
                        bl:ClearAllPoints()
                        bl:SetPoint("TOPLEFT", 8, y)
                        bl:SetSize(btn._textW + 18, h + 2)
                        bl:Show()
                        y = y - (h + 7)
                    end
                end
            else
                for bi = 1, #btn.bullets do
                    btn.bullets[bi]:Hide()
                end
                btn.body:Show()
                -- Sub-perks bring their own coloured bullet (green when known,
                -- grey when not), so they must not also get the plain "* "
                -- prefix the how-sentences take -- that is what rendered them
                -- as "* * Shadow Dance".
                local lines = {}
                for _, b in ipairs(SplitHow(info and info.how)) do
                    table.insert(lines, "* " .. b)
                end
                if info and info.subPerks then
                    for _, sub in ipairs(info.subPerks) do
                        local mark = (ownClass and PerkKnown(sub.id)) and "|cff4fd14f*|r " or "|cff666666*|r "
                        table.insert(lines, mark .. sub.name)
                    end
                end
                btn.body:SetText(table.concat(lines, "\n"))
            end
        else
            btn:Hide()
            btn.id = nil
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

function LG2.SendWorldToggle(info)
    if info.id == 910092 then
        SendLine("SOLOSET|" .. (WorldToggleOn(info) and "0" or "1"))
    elseif info.id == 910105 then
        SendLine("AMSET|" .. (WorldToggleOn(info) and "0" or "1"))
    elseif info.id == 910168 then
        SendLine("PULLSET|" .. (WorldToggleOn(info) and "0" or "1"))
    elseif info.id == 910170 then
        SendLine("TRACKORESET|" .. (WorldToggleOn(info) and "0" or "1"))
    elseif info.id == 910171 then
        SendLine("TRACKHERBSET|" .. (WorldToggleOn(info) and "0" or "1"))
    end
end

local function RefreshWayfarer()
    if not ui.waySlider then
        return
    end
    -- The dial is retired. Wayfarer is a straight five-rank speed track now, so
    -- there is nothing left to balance and the slider would only offer a choice
    -- that no longer exists. Hidden rather than deleted so the surrounding
    -- layout anchors stay exactly where they were.
    ui.waySlider:Hide()
    local way = db.way or {}
    local cap = way.cap or 0
    if cap <= 0 then
        ui.wayDesc:SetText("Locked - explore your home zone, or earn Going Down?")
        ui.wayDesc:SetTextColor(0.45, 0.45, 0.45, 1)
        return
    end
    ui.wayDesc:SetText(string.format(
        "Movement speed |cff4fd14f+%d%%|r on foot, mounted and flying.", cap))
    ui.wayDesc:SetTextColor(0.5, 0.5, 0.55, 1)
end

function LG2.PreviewWayfarer(value)
    if not ui.wayDesc then
        return
    end
    local way = db.way or {}
    local cap = way.cap or 0
    if cap <= 0 then
        return
    end
    local dmg = math.floor((tonumber(value) or 0) + 0.5)
    if dmg < 0 then dmg = 0 elseif dmg > 100 then dmg = 100 end
    local text = string.format("+%d%% speed, +%d%% damage",
        math.floor((100 - dmg) * cap / 100), math.floor(dmg * cap / 100))
    if (way.cd or 0) > 0 then
        text = text .. string.format("  (settling, %ds)", way.cd)
    end
    ui.wayDesc:SetText(text)
end

function LG2.SendWayfarer(value)
    local dmg = math.floor((tonumber(value) or 0) + 0.5)
    if dmg < 0 then dmg = 0 elseif dmg > 100 then dmg = 100 end
    SendLine("WAYSET|" .. tostring(dmg))
end

local function LayoutWorld()
    if not ui.worldUnlocks then
        return
    end
    RefreshWayfarer()
    for i = 1, #WORLD_UNLOCKS do
        local info = WORLD_UNLOCKS[i]
        local btn = ui.worldUnlocks[i]
        local known = PerkKnown(info.id)
        local on = known
        if info.toggle then
            on = WorldToggleOn(info)
        end
        btn.tip = info.name .. " - " .. info.how
        btn.label:SetText(info.name)
        if info.toggle then
            -- State lives in the switch, not in the label string, and the row
            -- itself stays neutral. Green and red now mean one thing only:
            -- this toggle is on, or it is off.
            btn.label:SetTextColor(known and 0.88 or 0.5, known and 0.9 or 0.5, known and 0.94 or 0.5, 1)
            btn.desc:SetTextColor(known and 0.5 or 0.38, known and 0.5 or 0.38, known and 0.55 or 0.42, 1)
            btn._ir, btn._ig, btn._ib = 0.12, 0.12, 0.13
            if not known then
                btn.swLabel:SetText("LOCKED")
                btn.swLabel:SetTextColor(0.5, 0.5, 0.5, 1)
                Solid(btn.sw.bg, 0.12, 0.12, 0.13, 1)
            elseif on then
                btn.swLabel:SetText("ON")
                btn.swLabel:SetTextColor(0.72, 0.96, 0.76, 1)
                Solid(btn.sw.bg, 0.14, 0.35, 0.18, 1)
            else
                btn.swLabel:SetText("OFF")
                btn.swLabel:SetTextColor(0.9, 0.62, 0.62, 1)
                Solid(btn.sw.bg, 0.3, 0.14, 0.14, 1)
            end
        else
            -- Actions are not stateful, so they only need to say whether they
            -- can be used. No green.
            if known then
                btn._ir, btn._ig, btn._ib = 0.16, 0.19, 0.24
                btn.label:SetTextColor(0.86, 0.9, 0.95, 1)
            else
                btn._ir, btn._ig, btn._ib = 0.11, 0.11, 0.11
                btn.label:SetTextColor(0.45, 0.45, 0.45, 1)
            end
        end
        Solid(btn.bg, btn._ir, btn._ig, btn._ib, 1)
    end
    if ui.worldPoints then
        local avail, earned, spent = LG2.PerkPoints()
        ui.worldPoints:SetText(string.format(
            "|cffffd966%d|r to spend   |cff777777%d earned / %d spent|r", avail, earned, spent))
    end
    if ui.worldRespec then
        local _, _, spent = LG2.PerkPoints()
        if spent > 0 then
            ui.worldRespec:Show()
        else
            ui.worldRespec:Hide()
        end
    end
    for t = 1, #WORLD_TRACKS do
        local track = WORLD_TRACKS[t]
        local row = ui.worldTracks[t]
        if track.unit == "gather" then
            local reach = LG2.TrackReach(track.ticks)
            local yield = LG2.TrackYield(track.ticks)
            local parts = {}
            if reach > 0 then
                table.insert(parts, "+" .. reach .. " yd")
            end
            if yield > 0 then
                table.insert(parts, yield .. "x yield")
            end
            if #parts > 0 then
                row.head:SetText(string.format("%s (|cff4fd14f%s|r)", track.name, table.concat(parts, ", ")))
            else
                row.head:SetText(track.name)
            end
        else
            local bonus = LG2.TrackBonus(track.ticks)
            if bonus > 0 then
                row.head:SetText(string.format("%s (|cff4fd14f%s%%|r)", track.name, bonus))
            else
                row.head:SetText(track.name)
            end
        end
        -- Bar, count and next-reward line: the three things the pips alone
        -- could never say.
        local got, total, nextHow, nextId = 0, #track.ticks, nil, nil
        for i = 1, total do
            if PerkKnown(track.ticks[i].id) then
                got = got + 1
            elseif not nextHow then
                nextHow = track.ticks[i].how
                nextId = track.ticks[i].id
            end
        end
        row.count:SetText(got .. " / " .. total)
        -- Each segment is coloured by whether THAT tier is held, not by
        -- overall progress, so a track earned out of order still reads
        -- correctly -- which is the one thing the old tick marks did that a
        -- plain percentage bar could not.
        local avail = LG2.PerkPoints()
        for i = 1, #row.segs do
            local seg = row.segs[i]
            local info = track.ticks[i]
            local cost = info and LG2.PerkCost(info.id) or nil
            if info and PerkKnown(info.id) then
                Solid(seg.bg, 0.28, 0.62, 0.32, 1)
                seg.tip = info.name .. " - " .. info.how .. "|n|cff7fdc7fOwned.|r"
            elseif info and LG2.PerkBuyable(info.id) then
                -- Gold, and the only state that is clickable.
                Solid(seg.bg, 0.85, 0.70, 0.25, 1)
                seg.tip = info.name .. " - " .. info.how
                    .. "|n|cffffd966Costs " .. cost .. " points. Click to unlock.|r"
            elseif info and cost then
                Solid(seg.bg, 0.10, 0.10, 0.12, 1)
                if not LG2.PerkPrereqOk(info.id) then
                    seg.tip = info.name .. " - " .. info.how
                        .. "|n|cff999999Unlock the previous rank first.|r"
                else
                    seg.tip = info.name .. " - " .. info.how
                        .. "|n|cff999999Costs " .. cost .. " points - you have " .. avail .. ".|r"
                end
            elseif info then
                -- Priced nowhere, so it is not for sale: these are the tracks
                -- still earned by playing (Honor, Reputation, Factions,
                -- Leveling, Professions). Deliberately a different colour from
                -- "you cannot afford it", because it is a different thing.
                Solid(seg.bg, 0.16, 0.18, 0.26, 1)
                seg.tip = info.name .. " - " .. info.how .. "|n|cff8a9bc4Earned by playing.|r"
            else
                Solid(seg.bg, 0.07, 0.07, 0.08, 1)
                seg.tip = nil
            end
        end
        if nextHow then
            local cost = nextId and LG2.PerkCost(nextId) or nil
            if cost and LG2.PerkBuyable(nextId) then
                row.next:SetText(string.format("Next (|cffffd966%d pts|r): %s", cost, nextHow))
                row.next:SetTextColor(0.62, 0.62, 0.68, 1)
            elseif cost then
                row.next:SetText(string.format("Next (%d pts): %s", cost, nextHow))
                row.next:SetTextColor(0.52, 0.52, 0.58, 1)
            else
                row.next:SetText("Next: " .. nextHow)
                row.next:SetTextColor(0.52, 0.52, 0.58, 1)
            end
        else
            row.next:SetText("Complete.")
            row.next:SetTextColor(0.42, 0.62, 0.45, 1)
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

function LG2.LayoutAttune()
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
            StyleBtn(ui.aaToggle, COLOR_ON[1], COLOR_ON[2], COLOR_ON[3])
        else
            StyleBtn(ui.aaToggle, COLOR_OFF[1], COLOR_OFF[2], COLOR_OFF[3])
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
                StyleBtn(row.toggle, COLOR_ON[1], COLOR_ON[2], COLOR_ON[3])
            else
                StyleBtn(row.toggle, COLOR_OFF[1], COLOR_OFF[2], COLOR_OFF[3])
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

-- A real item link where possible: quality-coloured and bracketed exactly as
-- it appears in chat, which is what makes a long list scannable.
--
-- `fallback` is the server-sent name, used when the client has no local copy
-- of the item. That is the normal case for attuned entries -- an account
-- entitlement need never have passed through this character's bags -- so the
-- fallback still gets built into a proper (grey) link rather than bare text,
-- and stays hoverable and shift-clickable like any other.
function LG2.ItemLinkText(entry, fallback)
    if not entry or entry == 0 then
        return fallback or "Item"
    end
    local name, link = GetItemInfo(entry)
    if link then
        return link
    end
    name = name or fallback
    if name and name ~= "" then
        return "|cff9d9d9d|Hitem:" .. tostring(entry) .. ":0:0:0:0:0:0:0|h[" .. name .. "]|h|r"
    end
    return "Item " .. tostring(entry)
end

local function LayoutAll()
    LG2.LayoutGear()
    LayoutLoot()
    LG2.LayoutAttune()
    LayoutClass()
    LayoutWorld()
    LG2.RefreshItems()
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

-- The Items tab: everything you own, in one list.
--
-- This replaces three separate panels -- Gear, Attune and the Armory that hid
-- behind a button inside Attune. They were three views of the same objects,
-- which is why the Armory ended up two clicks deep: it had nowhere better to
-- live. An attuned item is simply an item you own that is not currently
-- instantiated, so a state column subsumes the distinction and the drill-down
-- stops existing.
--
-- Slot comes from GetItemInfo rather than from the server. Real items carry an
-- equipment-slot index (0-18) and attuned entries carry an InventoryType
-- (1-28), so filtering across both on the server's number would silently mix
-- two different numbering schemes. The client already knows the true equip slot
-- for any entry id it has cached, and that is also where the icon comes from.
local INV_ORDER = {
    "INVTYPE_HEAD", "INVTYPE_NECK", "INVTYPE_SHOULDER", "INVTYPE_CLOAK",
    "INVTYPE_CHEST", "INVTYPE_ROBE", "INVTYPE_WRIST", "INVTYPE_HAND",
    "INVTYPE_WAIST", "INVTYPE_LEGS", "INVTYPE_FEET", "INVTYPE_FINGER",
    "INVTYPE_TRINKET", "INVTYPE_WEAPON", "INVTYPE_2HWEAPON",
    "INVTYPE_WEAPONMAINHAND", "INVTYPE_WEAPONOFFHAND", "INVTYPE_SHIELD",
    "INVTYPE_RANGED", "INVTYPE_RANGEDRIGHT", "INVTYPE_THROWN", "INVTYPE_HOLDABLE",
}

local ITEM_FILTERS = {
    { key = "all", label = "All" },
    { key = "armor", label = "Armor" },
    { key = "weapon", label = "Weapons" },
}
-- Subtype filter, fed by GetItemInfo's 13th return (equip/subtype "InventoryType-
-- adjacent" wording: for weapons it is the weapon skill subtype name, for armor
-- the armor class). Populated lazily from the rows actually present so the list
-- never shows an empty pick.
local ARMORY_SUBTYPES = {
    weapon = { "All", "Dagger", "Fist Weapon", "Axe", "Sword", "Mace", "Polearm",
        "Staff", "Sword (Two-Handed)", "Axe (Two-Handed)", "Mace (Two-Handed)",
        "Bow", "Crossbow", "Gun", "Thrown", "Wand", "Fishing Pole" },
    armor  = { "All", "Cloth", "Leather", "Mail", "Plate", "Shield", "Miscellaneous" },
    all    = { "All" },
}

function LG2.ItemSlotGroup(inv)
    if not inv then
        return "other"
    end
    if inv == "INVTYPE_WEAPON" or inv == "INVTYPE_2HWEAPON" or inv == "INVTYPE_WEAPONMAINHAND"
        or inv == "INVTYPE_WEAPONOFFHAND" or inv == "INVTYPE_RANGED" or inv == "INVTYPE_RANGEDRIGHT"
        or inv == "INVTYPE_THROWN" then
        return "weapon"
    end
    for i = 1, #INV_ORDER do
        if INV_ORDER[i] == inv then
            return "armor"
        end
    end
    return "other"
end

-- One row per thing you own. Real items come from db.byKey (which holds both
-- worn and bagged pieces); attuned entitlements come from db.armory and are
-- included only when no real copy exists, so owning the item hides its ghost.
function LG2.ItemsRowData()
    local rows, have = {}, {}

    for _, it in pairs(db.byKey or {}) do
        local entry = tonumber(it.entry) or 0
        if entry > 0 then
            have[entry] = true
        end
        table.insert(rows, {
            entry = entry,
            name = it.name or "Item",
            lv = tonumber(it.lv) or 1,
            xp = tonumber(it.xp) or 0,
            need = tonumber(it.need) or 0,
            growth = LG2.GrowthText(it),
            state = (it.kind == "inv") and "worn" or "bag",
            real = true,
        })
    end

    for i = 1, #(db.armory or {}) do
        local a = db.armory[i]
        local entry = tonumber(a.entry) or 0
        if entry > 0 and not have[entry] then
            table.insert(rows, {
                entry = entry,
                name = a.name or "Item",
                lv = 0, xp = 0, need = 0,
                growth = "",
                state = "attuned",
                real = false,
                -- Passed straight back on Create. The server feeds this to
                -- CanEquipNewItem, which takes an equipment-slot index while
                -- this is an InventoryType -- they do not line up, so the equip
                -- attempt reliably fails and the item lands in bags. That
                -- mismatch is longstanding and is exactly what makes Create
                -- mean "give me a copy" rather than "wear this now", so it is
                -- preserved deliberately rather than corrected.
                invslot = tonumber(a.slot) or 0,
                -- Carried so the row can draw its own tooltip when the client
                -- has never cached this item. An attuned entry is an account
                -- entitlement, not something that has to have passed through
                -- this character's bags, so "never seen it" is the normal case.
                ilvl = tonumber(a.ilvl) or 0,
                str = tonumber(a.str) or 0,
                agi = tonumber(a.agi) or 0,
                sta = tonumber(a.sta) or 0,
                intel = tonumber(a.intel) or 0,
                spi = tonumber(a.spi) or 0,
                armor = tonumber(a.armor) or 0,
            })
        end
    end

    local rank = { worn = 1, bag = 2, attuned = 3 }
    table.sort(rows, function(x, y)
        if rank[x.state] ~= rank[y.state] then
            return rank[x.state] < rank[y.state]
        end
        if x.lv ~= y.lv then
            return x.lv > y.lv
        end
        return (x.name or "") < (y.name or "")
    end)
    return rows
end

function LG2.ItemRowMatches(row)
    local filter = db.itemFilter or "all"
    -- "Attuned only" filter removed: every armory row is an attuned account
    -- entitlement (or a real copy of one), so the filter told the user nothing
    -- the tab itself did not already imply.
    if filter == "armor" or filter == "weapon" then
        local _, _, _, _, _, _, _, _, inv = GetItemInfo(row.entry)
        if LG2.ItemSlotGroup(inv) ~= filter then
            return false
        end
    end
    -- Equipment-type drill-down (weapon subtype / armor class). GetItemInfo's
    -- 13th return is the localized subtype name ("Dagger", "Leather", ...) --
    -- exactly what "show me daggers" means. A row the client has not cached
    -- cannot be classified, so it only survives the "All" subtype pick.
    local sub = db.itemSubtype or "All"
    if sub ~= "All" then
        local _, _, _, _, _, _, _, _, _, _, _, _, rowSub = GetItemInfo(row.entry)
        if rowSub ~= sub then
            return false
        end
    end
    local q = db.itemSearch
    if q and q ~= "" then
        if not string.find(string.lower(row.name or ""), string.lower(q), 1, true) then
            return false
        end
    end
    return true
end

function LG2.BuildItemsPanel(parent)
    local p = CreateFrame("Frame", nil, parent)
    p:SetPoint("TOPLEFT", 0, -50)
    p:SetPoint("BOTTOMRIGHT", 0, 0)
    p:Hide()

    db.itemFilter = db.itemFilter or "all"
    db.itemSearch = db.itemSearch or ""

    p.filters = {}
    for i = 1, #ITEM_FILTERS do
        local f = ITEM_FILTERS[i]
        local btn = CreateFrame("Button", nil, p)
        btn:SetSize(84, 18)
        btn:SetPoint("TOPLEFT", 10 + (i - 1) * 88, -4)
        StyleBtn(btn, 0.10, 0.10, 0.10)
        btn.label = Font(btn, 10, 0.75, 0.75, 0.75)
        btn.label:SetPoint("CENTER", 0, 0)
        btn.label:SetJustifyH("CENTER")
        btn.label:SetText(f.label)
        btn.key = f.key
        btn:SetScript("OnClick", function(self)
            db.itemFilter = self.key
            db.itemSubtype = "All"
            db.itemOff = 0
            LG2.RefreshItems()
        end)
        p.filters[i] = btn
    end

    -- Equipment-type dropdown (weapon subtype / armor class), sitting right of
    -- the All/Armor/Weapons pills. Cycling button rather than a UIDropDown so
    -- it matches the rest of this panel's controls and needs no template.
    local subBtn = CreateFrame("Button", nil, p)
    subBtn:SetSize(130, 18)
    subBtn:SetPoint("TOPLEFT", 10 + #ITEM_FILTERS * 88, -4)
    StyleBtn(subBtn, 0.10, 0.10, 0.10)
    subBtn.label = Font(subBtn, 10, 0.75, 0.75, 0.75)
    subBtn.label:SetPoint("CENTER", 0, 0)
    subBtn.label:SetJustifyH("CENTER")
    subBtn:SetScript("OnClick", function(self)
        local opts = ARMORY_SUBTYPES[db.itemFilter or "all"] or ARMORY_SUBTYPES.all
        local cur = db.itemSubtype or "All"
        local nextIdx = 1
        for i = 1, #opts do
            if opts[i] == cur then
                nextIdx = i % #opts + 1
                break
            end
        end
        db.itemSubtype = opts[nextIdx]
        db.itemOff = 0
        LG2.RefreshItems()
    end)
    subBtn:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_LEFT")
        GameTooltip:SetText("Equipment type")
        GameTooltip:AddLine("Click to cycle. Pick Armor or Weapons first.", 0.7, 0.7, 0.7, true)
        GameTooltip:Show()
    end)
    subBtn:SetScript("OnLeave", function()
        GameTooltip:Hide()
    end)
    p.subBtn = subBtn

    local wrap = CreateFrame("Frame", nil, p)
    wrap:SetSize(150, 18)
    wrap:SetPoint("TOPRIGHT", -10, -4)
    wrap:SetBackdrop({
        bgFile = WHITE,
        edgeFile = WHITE,
        edgeSize = 1,
        insets = { left = 1, right = 1, top = 1, bottom = 1 },
    })
    wrap:SetBackdropColor(0.08, 0.08, 0.08, 1)
    wrap:SetBackdropBorderColor(0.22, 0.22, 0.22, 1)
    local search = CreateFrame("EditBox", nil, wrap)
    search:SetPoint("TOPLEFT", 4, -1)
    search:SetPoint("BOTTOMRIGHT", -18, 1)
    search:SetAutoFocus(false)
    search:SetFontObject("GameFontHighlightSmall")
    search:SetScript("OnTextChanged", function(self)
        db.itemSearch = self:GetText() or ""
        db.itemOff = 0
        LG2.RefreshItems()
        if p.searchClear then
            if (self:GetText() or "") == "" then
                p.searchClear:Hide()
            else
                p.searchClear:Show()
            end
        end
    end)
    -- Release focus rather than clearing the text: the X clears, Escape backs
    -- out. A second Escape then closes the window through UISpecialFrames.
    search:SetScript("OnEscapePressed", function(self)
        self:ClearFocus()
    end)
    p.search = search

    local searchClear = CreateFrame("Button", nil, wrap)
    searchClear:SetSize(14, 14)
    searchClear:SetPoint("RIGHT", -2, 0)
    searchClear:SetFrameLevel((wrap:GetFrameLevel() or 0) + 2)
    searchClear.label = Font(searchClear, 12, 0.75, 0.75, 0.75)
    searchClear.label:SetPoint("CENTER", 0, 0)
    searchClear.label:SetText("x")
    searchClear:SetScript("OnClick", function()
        search:SetText("")
        search:ClearFocus()
    end)
    searchClear:SetScript("OnEnter", function(self)
        self.label:SetTextColor(1, 0.85, 0.85, 1)
    end)
    searchClear:SetScript("OnLeave", function(self)
        self.label:SetTextColor(0.75, 0.75, 0.75, 1)
    end)
    searchClear:Hide()
    p.searchClear = searchClear

    p.summary = Font(p, 10, 0.7, 0.7, 0.7)
    p.summary:SetPoint("TOPLEFT", 10, -26)
    p.summary:SetWordWrap(false)

    -- Attune All belongs here because Items is already where attuned entries
    -- are listed. It previously lived on the Armory panel, and when that panel
    -- was retired into this tab the button went with it -- leaving no way to
    -- attune deliberately anywhere in the UI (report #104).
    --
    -- Two presses, never one. The first asks, the second does it. This
    -- destroys gear -- a single mis-click on a button that eats your bags
    -- would be indefensible, and no amount of tooltip makes that acceptable.
    local attuneAll = CreateFrame("Button", nil, p)
    attuneAll:SetSize(120, 20)
    attuneAll:SetPoint("BOTTOM", 0, 8)
    StyleBtn(attuneAll, 0.14, 0.24, 0.14)
    attuneAll.label = Font(attuneAll, 10, 0.85, 0.95, 0.85)
    attuneAll.label:SetPoint("CENTER", 0, 0)
    attuneAll.label:SetJustifyH("CENTER")
    attuneAll.label:SetText("Attune All")
    attuneAll._armed = 0
    attuneAll:SetScript("OnClick", function(self)
        if GetTime() - (self._armed or 0) < 6 then
            self._armed = 0
            self.label:SetText("Attune All")
            SendLine("ATTUNEALL")
            return
        end
        self._armed = GetTime()
        self.label:SetText("Sure?")
    end)
    attuneAll:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_LEFT")
        GameTooltip:SetText("Attune All")
        GameTooltip:AddLine("Consumes every attunable item in your bags and banks a permanent share of its stats to your account.", 1, 1, 1, true)
        GameTooltip:AddLine("Each item counts once. Duplicates and anything already attuned are left alone.", 0.7, 0.7, 0.7, true)
        GameTooltip:AddLine("Equipped gear is never touched.", 0.6, 0.9, 0.6, true)
        GameTooltip:Show()
    end)
    attuneAll:SetScript("OnLeave", function(self)
        GameTooltip:Hide()
        if GetTime() - (self._armed or 0) >= 6 then
            self._armed = 0
            self.label:SetText("Attune All")
        end
    end)
    ui.attuneAllBtn = attuneAll

    p.empty = Font(p, 11, 0.6, 0.6, 0.6)
    p.empty:SetPoint("TOPLEFT", 10, -46)
    p.empty:SetText("Nothing matches these filters.")
    p.empty:Hide()

    -- Rows are two lines: name on top, level and growth beneath. The Perks tab
    -- learned this the hard way -- a single line wrapped anyway and collided
    -- with the row below, which reads as a bug rather than a layout.
    --
    -- Two columns, filled top-to-bottom then left-to-right. A single column of
    -- full-width rows left most of the panel empty between the name and the
    -- Create button, while the list itself ran to hundreds of entries; the
    -- space is worth more as a second column than as padding.
    local ROW_H = 30
    local ROWS_PER_COL = 13
    local COL_W = 300
    local COL_GAP = 8
    p.rowsPerCol = ROWS_PER_COL
    p.rows = {}
    for i = 1, ROWS_PER_COL * 2 do
        local col = math.floor((i - 1) / ROWS_PER_COL)
        local slotInCol = (i - 1) % ROWS_PER_COL
        local row = CreateFrame("Button", nil, p)
        row:SetSize(COL_W - 4, ROW_H - 2)
        row:SetPoint("TOPLEFT", 10 + col * (COL_W + COL_GAP), -42 - slotInCol * ROW_H)

        row.hl = row:CreateTexture(nil, "BACKGROUND")
        row.hl:SetAllPoints(row)
        Solid(row.hl, 0.16, 0.16, 0.18, 1)
        row.hl:Hide()

        row.icon = row:CreateTexture(nil, "ARTWORK")
        row.icon:SetSize(24, 24)
        row.icon:SetPoint("LEFT", 2, 0)

        -- Widths are set per-refresh, not here: a row showing Create has ~60px
        -- less to play with than one without, and item names are long enough
        -- that giving away that space unconditionally truncates for no reason.
        row.name = Font(row, 11, 0.9, 0.9, 0.94)
        row.name:SetPoint("TOPLEFT", 30, -1)
        row.name:SetJustifyH("LEFT")
        row.name:SetWordWrap(false)

        row.detail = Font(row, 10, 0.62, 0.66, 0.62)
        row.detail:SetPoint("TOPLEFT", 30, -15)
        row.detail:SetJustifyH("LEFT")
        row.detail:SetWordWrap(false)

        row.create = CreateFrame("Button", nil, row)
        row.create:SetSize(58, 16)
        row.create:SetPoint("RIGHT", -2, 0)
        StyleBtn(row.create, 0.14, 0.20, 0.16)
        row.create.label = Font(row.create, 10, 0.8, 0.95, 0.8)
        row.create.label:SetPoint("CENTER", 0, 0)
        row.create.label:SetJustifyH("CENTER")
        row.create.label:SetText("Create")
        row.create:Hide()
        row.create:SetScript("OnClick", function(self)
            local r = self:GetParent().data
            if r and r.entry and r.entry > 0 then
                SendLine("ARMEQUIP|" .. tostring(r.invslot or 0) .. "|" .. tostring(r.entry))
            end
        end)

        -- Attunement bar, drawn under the icon the same way the character
        -- sheet's gear overlays do (flat textures, not a StatusBar -- same
        -- reasoning as OverlayFor at the bottom of the file).
        row.barBg = row:CreateTexture(nil, "ARTWORK")
        row.barBg:SetHeight(3)
        row.barBg:SetPoint("BOTTOMLEFT", row.icon, "BOTTOMLEFT", 0, -4)
        row.barBg:SetPoint("BOTTOMRIGHT", row.icon, "BOTTOMRIGHT", 0, -4)
        Solid(row.barBg, 0, 0, 0, 0.7)
        row.barBg:Hide()
        row.bar = row:CreateTexture(nil, "ARTWORK")
        row.bar:SetHeight(3)
        row.bar:SetPoint("BOTTOMLEFT", row.icon, "BOTTOMLEFT", 0, -4)
        row.bar:SetPoint("BOTTOMRIGHT", row.icon, "BOTTOMRIGHT", 0, -4)
        row.bar:Hide()

        -- SetHyperlink draws nothing at all for an item this client has never
        -- cached, which is most of the attuned list -- an entitlement does not
        -- have to have passed through this character's bags. That read as "half
        -- the items have no tooltip". When the client cannot resolve the item,
        -- draw the tooltip from what the server already sent us instead.
        row:SetScript("OnEnter", function(self)
            self.hl:Show()
            local r = self.data
            if not r or not r.entry or r.entry <= 0 then
                return
            end
            GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
            if GetItemInfo(r.entry) then
                GameTooltip:SetHyperlink("item:" .. tostring(r.entry))
                -- Bonus stats (the growth line that used to be printed on
                -- every row) live on the tooltip now, appended after the
                -- real item tooltip content.
                local growth = r.growth
                if growth and growth ~= "" then
                    GameTooltip:AddLine(" ")
                    GameTooltip:AddLine("Living Gear growth:", 0.55, 0.75, 0.95, true)
                    GameTooltip:AddLine(growth, 0.75, 0.85, 0.75, true)
                end
                if r.state == "worn" then
                    GameTooltip:AddLine("Worn", 0.5, 0.86, 0.5, true)
                elseif r.state == "bag" then
                    GameTooltip:AddLine("In bags", 0.62, 0.62, 0.62, true)
                end
                GameTooltip:Show()
                return
            end
            -- No local cache: draw the tooltip from the server record.
            GameTooltip:SetText(r.name or "Item")
            if r.ilvl and r.ilvl > 0 then
                GameTooltip:AddLine("Item Level " .. tostring(r.ilvl), 0.7, 0.7, 0.7)
            end
            local stats = {
                { "Strength", r.str }, { "Agility", r.agi }, { "Stamina", r.sta },
                { "Intellect", r.intel }, { "Spirit", r.spi }, { "Armor", r.armor },
            }
            for i = 1, #stats do
                local v = tonumber(stats[i][2]) or 0
                if v > 0 then
                    GameTooltip:AddLine(string.format("+%d %s", v, stats[i][1]), 1, 1, 1)
                end
            end
            local growth = r.growth
            if growth and growth ~= "" then
                GameTooltip:AddLine(" ")
                GameTooltip:AddLine("Living Gear growth:", 0.55, 0.75, 0.95, true)
                GameTooltip:AddLine(growth, 0.75, 0.85, 0.75, true)
            end
            GameTooltip:AddLine("Attuned to this account.", 0.55, 0.75, 0.95, true)
            GameTooltip:AddLine("Your client has no local copy of this item, so this is the account record rather than the full item tooltip.", 0.5, 0.5, 0.5, true)
            GameTooltip:Show()
        end)
        row:SetScript("OnLeave", function(self)
            self.hl:Hide()
            GameTooltip:Hide()
        end)
        -- Shift-click pastes the link into an open chat edit box, the way
        -- shift-clicking an item anywhere else in the UI does. The row is a
        -- Button rather than a FontString, so the link has to be inserted by
        -- hand -- FontString hyperlinks are not clickable on a plain frame.
        row:SetScript("OnClick", function(self)
            local r = self.data
            if not r or not IsShiftKeyDown() or not self.link then
                return
            end
            if ChatEdit_InsertLink then
                ChatEdit_InsertLink(self.link)
            end
        end)
        row:Hide()
        p.rows[i] = row
    end

    -- 636 attuned entries against 13 rows. There was no scroll of any kind on
    -- this list and no sign that it continued, so everything past the first
    -- screenful was simply unreachable.
    -- One notch moves exactly one column, so the right-hand column becomes the
    -- left-hand one. Scrolling by a single entry would reflow both columns and
    -- make the list appear to shuffle; scrolling by a full page would skip past
    -- what you were reading.
    db.itemOff = db.itemOff or 0
    p:EnableMouseWheel(true)
    p:SetScript("OnMouseWheel", function(_, delta)
        local off = (db.itemOff or 0) - delta * ROWS_PER_COL
        if off < 0 then
            off = 0
        end
        db.itemOff = off
        LG2.RefreshItems()
    end)

    ui.items = p
    return p
end

function LG2.RefreshItems()
    local p = ui.items
    if not p or not p.rows then
        return
    end

    for i = 1, #p.filters do
        local btn = p.filters[i]
        local on = btn.key == (db.itemFilter or "all")
        StyleBtn(btn, on and 0.14 or 0.10, on and 0.22 or 0.10, on and 0.28 or 0.10)
        if on then
            btn.label:SetTextColor(0.85, 0.95, 0.95, 1)
        else
            btn.label:SetTextColor(0.72, 0.72, 0.72, 1)
        end
    end
    if p.subBtn then
        local sub = db.itemSubtype or "All"
        p.subBtn.label:SetText("Type: " .. sub)
        local subOn = sub ~= "All"
        StyleBtn(p.subBtn, subOn and 0.14 or 0.10, subOn and 0.22 or 0.10, subOn and 0.28 or 0.10)
        p.subBtn.label:SetTextColor(subOn and 0.85 or 0.72, subOn and 0.95 or 0.72, subOn and 0.95 or 0.72, 1)
    end

    local all = LG2.ItemsRowData()
    local shown = {}
    for i = 1, #all do
        if LG2.ItemRowMatches(all[i]) then
            table.insert(shown, all[i])
        end
    end

    -- Count, not a percentage. There is no denominator to be a percentage of --
    -- attunement unlocks higher rarities at 10, 100, 1000 items -- and the
    -- field a percentage would have come from is never actually set, so the
    -- readout would have sat at 0% forever.
    -- Clamped here rather than in the wheel handler because the ceiling moves
    -- with the filter: narrowing the list while scrolled to the bottom would
    -- otherwise leave the view past the end, showing nothing.
    local maxOff = #shown - #p.rows
    if maxOff < 0 then
        maxOff = 0
    end
    local off = db.itemOff or 0
    if off > maxOff then
        off = maxOff
    end
    db.itemOff = off

    local attuned = tonumber(db.attune and db.attune.count) or 0
    local first = (#shown == 0) and 0 or (off + 1)
    local last = off + #p.rows
    if last > #shown then
        last = #shown
    end
    p.summary:SetText(string.format(
        "|cffb0b0b0%d-%d of %d|r    |cff8a9bc4%d attuned on this account|r",
        first, last, #shown, attuned))

    if #shown == 0 then
        p.empty:Show()
    else
        p.empty:Hide()
    end

    for i = 1, #p.rows do
        local row = p.rows[i]
        local r = shown[i + off]
        if not r then
            row:Hide()
        else
            row.data = r
            row:Show()

            local _, _, _, _, _, _, _, _, _, tex = GetItemInfo(r.entry)
            row.icon:SetTexture(tex or "Interface\\Icons\\INV_Misc_QuestionMark")

            -- The real chat item link, so quality colour does the work the
            -- separate WORN/BAG/ATTUNED column used to do badly. That column
            -- cost 70px of every row to restate what the row already showed.
            row.link = LG2.ItemLinkText(r.entry, r.name)
            row.name:SetText(row.link)

            local textW = row:GetWidth() - 34
            -- Attunement bar under the icon, exactly like the character-sheet
            -- gear rows draw (OverlayFor). "Half-completed at max" fixed by
            -- clamping: anything at or past the attunement cap shows a FULL
            -- bar, not the level-25 fraction of the level-50 track.
            local bar = row.bar
            if r.state == "attuned" then
                row.icon:SetVertexColor(0.55, 0.55, 0.62)
                row.detail:SetText("|cff5a6a8aattuned - not created|r")
                row.create:Show()
                textW = textW - 60
                bar:Hide()
                row.barBg:Hide()
            else
                row.icon:SetVertexColor(1, 1, 1)
                row.create:Hide()
                local lv = tonumber(r.lv) or 1
                local xp = tonumber(r.xp) or 0
                local need = tonumber(r.need) or 0
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
                local r2, g2, b2 = GetItemQualityColor(LevelQuality(lv))
                -- growth can be nil here: the ATT| live-sync path builds rows
                -- through a different constructor than the full sync and does
                -- not always set it. Never hand a nil to %s.
                local growth = r.growth or ""
                if growth == "" then
                    growth = "|cff707070no growth|r"
                end
                -- 24px icon width, clamped to the pixel so a 3px bar can take
                -- any fraction cleanly.
                row.barBg:Show()
                if lv >= LG_ATTUNE_CAP_LEVEL then
                    row.detail:SetText(string.format("|cffffd966Lv %d|r max %s", lv, growth))
                    row.bar:SetWidth(24)
                    Solid(row.bar, r2, g2, b2, 0.95)
                    row.bar:Show()
                elseif need > 0 then
                    row.detail:SetText(string.format("|cffffd966Lv %d|r %d/%d xp %s",
                        lv, xp, need, growth))
                    row.bar:SetWidth(math.max(1, math.floor(24 * frac + 0.5)))
                    Solid(row.bar, r2, g2, b2, 0.95)
                    row.bar:Show()
                else
                    row.detail:SetText(string.format("|cffffd966Lv %d|r max %s", lv, growth))
                    row.bar:SetWidth(24)
                    Solid(row.bar, r2, g2, b2, 0.95)
                    row.bar:Show()
                end
            end
            if textW < 40 then
                textW = 40
            end
            row.name:SetWidth(textW)
            row.detail:SetWidth(textW)
        end
    end
end

function LG2.MakeVaultPanel(parent, kind, withCats)
    local p = MakePanel(parent)
    local listX = withCats and 96 or 10
    -- (Two-column list below; rowW retired when the columns took over.)
    local hint = Font(p, 10, 0.55, 0.55, 0.55)
    hint:SetPoint("TOPLEFT", listX, -4)
    if kind == VAULT_REAGENT then
        local dep = CreateFrame("Button", nil, p)
        dep:SetSize(120, 20)
        dep:SetPoint("BOTTOM", 0, 8)
        dep:SetFrameLevel((p:GetFrameLevel() or 0) + 6)
        StyleBtn(dep, COLOR_ADD[1], COLOR_ADD[2], COLOR_ADD[3])
        dep.label = Font(dep, 10, 0.85, 0.95, 0.85)
        dep.label:SetPoint("CENTER", 0, 0)
        dep.label:SetJustifyH("CENTER")
        dep.label:SetText("Deposit All")
        dep:SetScript("OnClick", function()
            SendLine("DEPOSITALL")
        end)
        ui.reagentDeposit = dep
        dep:Show()
        local searchWrap = CreateFrame("Frame", nil, p)
        searchWrap:SetSize(160, 18)
        searchWrap:SetPoint("TOPRIGHT", -10, -2)
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
        -- Stops short of the right edge so typed text never runs under the
        -- clear button sitting there.
        search:SetPoint("BOTTOMRIGHT", -18, 1)
        search:SetFont("Fonts\\FRIZQT__.TTF", 10, "")
        search:SetTextColor(0.9, 0.9, 0.9, 1)
        search:SetAutoFocus(false)
        search:SetMaxLetters(32)
        search:SetText(db.reagentSearch or "")
        search:SetScript("OnTextChanged", function(self)
            db.reagentSearch = self:GetText() or ""
            db.vaultOff[VAULT_REAGENT] = 0
            RefreshVaultPanel()
            if ui.reagentSearchClear then
                if (self:GetText() or "") == "" then
                    ui.reagentSearchClear:Hide()
                else
                    ui.reagentSearchClear:Show()
                end
            end
        end)
        -- Escape gives the field back rather than being swallowed by it. The
        -- window is in UISpecialFrames, so once focus is released a second
        -- Escape closes the panel -- without this the field ate every Escape
        -- and the only way out was the X in the corner.
        search:SetScript("OnEscapePressed", function(self)
            self:ClearFocus()
        end)
        local clear = CreateFrame("Button", nil, searchWrap)
        clear:SetSize(14, 14)
        clear:SetPoint("RIGHT", -2, 0)
        clear:SetFrameLevel((searchWrap:GetFrameLevel() or 0) + 2)
        clear.label = Font(clear, 12, 0.75, 0.75, 0.75)
        clear.label:SetPoint("CENTER", 0, 0)
        clear.label:SetText("x")
        clear:SetScript("OnClick", function()
            search:SetText("")
            search:ClearFocus()
        end)
        clear:SetScript("OnEnter", function(self)
            self.label:SetTextColor(1, 0.85, 0.85, 1)
        end)
        clear:SetScript("OnLeave", function(self)
            self.label:SetTextColor(0.75, 0.75, 0.75, 1)
        end)
        if (db.reagentSearch or "") == "" then
            clear:Hide()
        end
        ui.reagentSearchClear = clear
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
            StyleBtn(btn, COLOR_BG[1], COLOR_BG[2], COLOR_BG[3])
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
    -- Two columns, filled top-to-bottom then left-to-right (same pattern as
    -- the Armory list). The single 14-row column wasted the entire right half
    -- of the panel; doubling visible rows is free space reclaimed.
    local ROWS_PER_COL = 14
    local COL_W = withCats and 210 or 300
    local COL_GAP = 8
    for i = 1, VAULT_ROWS * 2 do
        local col = math.floor((i - 1) / ROWS_PER_COL)
        local slotInCol = (i - 1) % ROWS_PER_COL
        local row = CreateFrame("Button", nil, p)
        row:SetSize(COL_W, 18)
        row:SetPoint("TOPLEFT", listX + col * (COL_W + COL_GAP), -28 - slotInCol * 20)
        row.bg = row:CreateTexture(nil, "BACKGROUND")
        row.bg:SetAllPoints(row)
        Solid(row.bg, 0.10, 0.10, 0.10, 0)
        row.icon = row:CreateTexture(nil, "ARTWORK")
        row.icon:SetSize(14, 14)
        row.icon:SetPoint("LEFT", 2, 0)
        row.text = Font(row, 11, 0.85, 0.85, 0.85)
        row.text:SetPoint("LEFT", 20, 0)
        row.text:SetPoint("RIGHT", -4, 0)
        -- Same story as the attuned list: a vault row is an account-side
        -- record, so the client often has no local copy of the item and
        -- SetHyperlink would draw nothing at all. Fall back to the name the
        -- server sent rather than showing an empty tooltip.
        row:SetScript("OnEnter", function(self)
            Solid(self.bg, 0.16, 0.16, 0.16, 1)
            local e = tonumber(self.entry) or 0
            if e <= 0 then
                return
            end
            GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
            if GetItemInfo(e) then
                GameTooltip:SetHyperlink("item:" .. tostring(e))
            else
                GameTooltip:SetText(self.itemName or "Item")
                GameTooltip:AddLine("Stored in your vault.", 0.55, 0.75, 0.95, true)
                GameTooltip:AddLine("Your client has no local copy of this item, so there is no full tooltip for it.", 0.5, 0.5, 0.5, true)
            end
            GameTooltip:Show()
        end)
        row:SetScript("OnLeave", function(self)
            Solid(self.bg, 0.10, 0.10, 0.10, 0)
            GameTooltip:Hide()
        end)
        row:SetScript("OnClick", function(self)
            if IsShiftKeyDown() then
                if self.link and ChatEdit_InsertLink then
                    ChatEdit_InsertLink(self.link)
                end
                return
            end
            if self.kind and self.entry then
                SendLine("TAKE|" .. tostring(self.kind) .. "|" .. tostring(self.entry))
            end
        end)
        row:Hide()
        rows[i] = row
    end
    p:EnableMouse(true)
    p:EnableMouseWheel(true)
    -- One notch = one column of rows (14), matching the two-column layout:
    -- scrolling by a single row reflows both columns and makes the list
    -- appear to shuffle.
    p:SetScript("OnMouseWheel", function(_, delta)
        local off = db.vaultOff[kind] or 0
        off = off - delta * 14
        if off < 0 then
            off = 0
        end
        db.vaultOff[kind] = off
        LayoutAll()
    end)
    return p, rows, empty, hint
end

-- Split out of BuildUI() (2026-08-20): BuildUI is a single ~1100-line
-- function that already sat right at Lua 5.1's hard 60-upvalue-per-function
-- ceiling. Adding the class-browse tab row's two extra outer-scope
-- references (CLASS_TAB_LIST, CLASS_LABEL) tipped it over --
-- "Interface\FrameXML\LivingGear.lua:<n>: function at line <n> has more
-- than 60 upvalues" -- which is a load-time compile error, so the ENTIRE
-- addon file failed to execute at all (no event registration, no slash
-- command, nothing) rather than just this one tab row breaking. Any future
-- work that adds a new file-level local referenced from inside BuildUI
-- risks the same failure -- prefer extracting a helper function (its own
-- fresh 60-upvalue budget) over adding another reference directly inside
-- BuildUI itself.
-- Fixed 5-per-row / 2-row grid (2026-08-20) -- now that all 10 classes have
-- perk data, CLASS_TAB_LIST always has exactly 10 entries in CLASS_ORDER's
-- (alphabetical) order, so a fixed grid reads better than the old
-- width-driven wrap, which left 9 tabs on one row and 1 lonely tab on the
-- next. If CLASS_TAB_LIST is ever shorter than 10 (a class's perk data
-- pulled), the grid just leaves the trailing slots empty rather than
-- reflowing -- acceptable since class perk data isn't expected to shrink.
-- Returns the Y offset (a negative number) that the rest of the Class
-- panel should start below.
local CLASS_TAB_COLS = 5
local CLASS_TAB_W = 116
local CLASS_TAB_H = 18
local CLASS_TAB_GAP = 4
local CLASS_TAB_ROW_H = 20

function LG2.BuildClassTabs(class)
    ui.classTabs = {}
    local myClass = LG2.PlayerClassToken()
    local rows = math.ceil(#CLASS_TAB_LIST / CLASS_TAB_COLS)
    -- Centred rather than pinned 10px from the left. The grid is a fixed 5
    -- columns of fixed width, so it never filled the panel and sat visibly
    -- off to one side.
    local gridW = CLASS_TAB_COLS * CLASS_TAB_W + (CLASS_TAB_COLS - 1) * CLASS_TAB_GAP
    local gridX = math.floor((FRAME_W - gridW) / 2)
    if gridX < 4 then
        gridX = 4
    end
    for i, token in ipairs(CLASS_TAB_LIST) do
        local col = (i - 1) % CLASS_TAB_COLS
        local row = math.floor((i - 1) / CLASS_TAB_COLS)
        local label = CLASS_LABEL[token] or token
        local tabBtn = CreateFrame("Button", nil, class)
        tabBtn:SetSize(CLASS_TAB_W, CLASS_TAB_H)
        tabBtn:SetPoint("TOPLEFT", gridX + col * (CLASS_TAB_W + CLASS_TAB_GAP), -6 - row * CLASS_TAB_ROW_H)
        -- Background stays the normal dark button color (consistent with
        -- the rest of the UI, and sidesteps contrast problems -- Priest's
        -- class color is pure white, which a white background would make
        -- unreadable against light text). The class color lives in the
        -- LABEL TEXT instead, same convention WoW's own UI uses for
        -- class-colored names -- see LayoutClass for the dim/bright toggle.
        StyleBtnColor(tabBtn, COLOR_BTN)
        tabBtn.token = token
        tabBtn.classColor = CLASS_COLOR[token] or COLOR_TEXT
        tabBtn.label = Font(tabBtn, 10, tabBtn.classColor[1], tabBtn.classColor[2], tabBtn.classColor[3])
        tabBtn.label:SetPoint("CENTER", 0, 0)
        tabBtn.label:SetJustifyH("CENTER")
        tabBtn.label:SetText(label)
        if token == myClass then
            -- "This is you": a bold light outline around your own class's
            -- tab, distinct from the selected/browsing highlight (which is
            -- just brightness -- see LayoutClass). Native SetBackdrop
            -- works on a plain CreateFrame("Button", ...) in this client
            -- (3.3.5, no BackdropTemplate requirement).
            tabBtn:SetBackdrop({
                edgeFile = WHITE,
                edgeSize = 2,
            })
            tabBtn:SetBackdropBorderColor(1, 0.92, 0.55, 1)
        end
        tabBtn:SetScript("OnClick", function(self)
            db.classBrowse = self.token
            LayoutClass()
        end)
        ui.classTabs[i] = tabBtn
    end
    return -6 - rows * CLASS_TAB_ROW_H
end

-- Split out of BuildUI() for the same reason as BuildClassTabs above --
-- still over the 60-upvalue ceiling even after that first extraction
-- (confirmed via a static upvalue-reference count, since Lua 5.1 -- what
-- the actual WotLK client runs -- isn't available to compile-check against
-- directly in this environment). The Autoloot/Rules tab was the next
-- largest self-contained block, pulling out 7 outer-scope references at
-- once (RULE_ROWS, RULE_FIELDS, RULE_TYPES, RULE_QUALS, RULE_TYPE_MATCH,
-- ACTION_NAMES, DEFAULT_RULES).
function LG2.BuildLootPanel(f)
    local loot = MakePanel(f)
    ui.loot = loot

    ui.alToggle = CreateFrame("Button", nil, loot)
    ui.alToggle:SetSize(120, 18)
    ui.alToggle:SetPoint("TOPLEFT", 10, -2)
    StyleBtn(ui.alToggle, COLOR_ON[1], COLOR_ON[2], COLOR_ON[3])
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

    -- listPane/editPane used to be 268/244 wide with only an 8px gap
    -- between them and just 10px of margin against the (old, narrower)
    -- frame edge -- any label overrun bled straight past the frame.
    -- Widened and given real margins now that the frame itself is bigger.
    local listPane = CreateFrame("Frame", nil, loot)
    listPane:SetPoint("TOPLEFT", 10, -44)
    listPane:SetSize(300, 250)
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
        row:SetSize(300, 18)
        row:SetPoint("TOPLEFT", 0, -(i - 1) * 20)
        row.text = Font(row, 10, 0.85, 0.85, 0.85)
        row.text:SetPoint("LEFT", 0, 0)
        row.text:SetPoint("RIGHT", -48, 0)
        row.del = CreateFrame("Button", nil, row)
        row.del:SetSize(44, 16)
        row.del:SetPoint("RIGHT", 0, 0)
        StyleBtn(row.del, COLOR_OFF[1], COLOR_OFF[2], COLOR_OFF[3])
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
    editPane:SetPoint("TOPLEFT", 330, -44)
    editPane:SetSize(290, 250)
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
    StyleBtn(ui.ruleField, COLOR_BTN[1], COLOR_BTN[2], COLOR_BTN[3])
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
    StyleBtn(ui.ruleOp, COLOR_BTN[1], COLOR_BTN[2], COLOR_BTN[3])
    ui.ruleOp.label = Font(ui.ruleOp, 10, 0.9, 0.9, 0.9)
    ui.ruleOp.label:SetPoint("CENTER", 0, 0)
    ui.ruleOp.label:SetJustifyH("CENTER")
    ui.ruleOp.label:SetText("==")
    ui.ruleOp:SetScript("OnClick", function()
        Cycle(ui.ruleOp, db.ruleField == 2 and 4 or 2, "ruleOp")
    end)

    ui.ruleType = CreateFrame("Button", nil, editPane)
    ui.ruleType:SetSize(220, 18)
    ui.ruleType:SetPoint("TOPLEFT", 0, -42)
    StyleBtn(ui.ruleType, COLOR_BTN[1], COLOR_BTN[2], COLOR_BTN[3])
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
    StyleBtn(ui.ruleQual, COLOR_BTN[1], COLOR_BTN[2], COLOR_BTN[3])
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
    StyleBtn(ui.ruleThen, COLOR_BTN[1], COLOR_BTN[2], COLOR_BTN[3])
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
    StyleBtn(addBtn, COLOR_ADD[1], COLOR_ADD[2], COLOR_ADD[3])
    addBtn.label = Font(addBtn, 10, 0.85, 0.95, 0.85)
    addBtn.label:SetPoint("CENTER", 0, 0)
    addBtn.label:SetJustifyH("CENTER")
    addBtn.label:SetText("Add rule")
    addBtn:SetScript("OnClick", function()
        local field = db.ruleField
        local op = db.ruleOp
        local action = db.ruleAction - 1
        local rule = { match = 0, action = action,
            negate = field == 2 and (op - 1) or (op == 2 and 1 or 0), quality = 0, text = "" }
        if field == 1 then
            rule.match = RULE_TYPE_MATCH[db.ruleType]
        elseif field == 2 then
            rule.match = 6
            rule.quality = db.ruleQual - 1
        elseif field == 4 then
            rule.match = 14
            local text = ui.ruleName:GetText() or ""
            local lo, hi = string.match(text, "^%s*(%d+)%s*-%s*(%d+)%s*$")
            if not lo then
                return
            end
            rule.text = lo .. "-" .. hi
        else
            rule.match = 5
            rule.text = string.gsub(ui.ruleName:GetText() or "", "|", "")
            if rule.text == "" then
                return
            end
        end
        LG2.InsertRule(rule)
    end)

    ui.rulePreview = Font(editPane, 10, 0.7, 0.7, 0.7)
    ui.rulePreview:SetPoint("TOPLEFT", 0, -116)
    ui.rulePreview:SetWidth(240)
    ui.rulePreview:SetText("Type == Quest -> Bags")

    local expBtn = CreateFrame("Button", nil, loot)
    expBtn:SetSize(64, 18)
    expBtn:SetPoint("BOTTOMRIGHT", -148, 10)
    StyleBtn(expBtn, COLOR_BTN[1], COLOR_BTN[2], COLOR_BTN[3])
    expBtn.label = Font(expBtn, 10, 0.9, 0.9, 0.9)
    expBtn.label:SetPoint("CENTER", 0, 0)
    expBtn.label:SetJustifyH("CENTER")
    expBtn.label:SetText("Export")
    expBtn:SetScript("OnClick", function()
        if ui.shareBox then
            ui.shareBox:SetText(LG2.ExportRules())
            ui.shareBox:HighlightText()
            ui.shareBox:SetFocus()
            ui.shareWrap:Show()
            ui.shareHint:SetText("Copy this list. Close when done.")
        end
    end)

    local impBtn = CreateFrame("Button", nil, loot)
    impBtn:SetSize(64, 18)
    impBtn:SetPoint("BOTTOMRIGHT", -80, 10)
    StyleBtn(impBtn, COLOR_BTN[1], COLOR_BTN[2], COLOR_BTN[3])
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
    StyleBtn(resetBtn, COLOR_DANGER[1], COLOR_DANGER[2], COLOR_DANGER[3])
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
    StyleBtn(shareClose, COLOR_BTN[1], COLOR_BTN[2], COLOR_BTN[3])
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
    StyleBtn(shareApply, COLOR_ADD[1], COLOR_ADD[2], COLOR_ADD[3])
    shareApply.label = Font(shareApply, 10, 0.85, 0.95, 0.85)
    shareApply.label:SetPoint("CENTER", 0, 0)
    shareApply.label:SetJustifyH("CENTER")
    shareApply.label:SetText("Apply")
    shareApply:SetScript("OnClick", function()
        local text = ui.shareBox:GetText() or ""
        local parsed = {}
        for line in string.gmatch(text .. "\n", "(.-)\n") do
            local rule = LG2.ParseImportLine(line)
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
end

-- Same split-out-of-BuildUI treatment as BuildClassTabs/BuildLootPanel
-- above -- done preemptively this time (2026-08-20) while there was
-- headroom to spare, since more class-perk UI work is coming and BuildUI
-- was still only a few tabs away from the 60-upvalue ceiling again.
function LG2.BuildGearPanel(f)
    local gear = CreateFrame("Frame", nil, f)
    gear:SetPoint("TOPLEFT", 0, -50)
    gear:SetPoint("BOTTOMRIGHT", 0, 0)
    ui.gear = gear

    ui.absorb = Font(gear, 11, 0.85, 0.75, 0.45)
    ui.absorb:SetPoint("TOPLEFT", 10, -2)
    ui.absorb:SetWordWrap(false)

    -- Attuned stat totals used to be one long concatenated FontString that
    -- word-wrapped mid-stat once there were enough non-zero stats to
    -- overflow the panel width. Chips instead -- fixed grid, no wrap ever,
    -- and headroom (12 slots) for stat types the server doesn't send yet
    -- (haste/crit/hit rating) to just slot in later with zero layout work.
    local ABSORB_CHIP_COLS = 4
    local ABSORB_CHIP_ROWS = 3
    ui.absorbChips = {}
    for i = 1, ABSORB_CHIP_COLS * ABSORB_CHIP_ROWS do
        local col = (i - 1) % ABSORB_CHIP_COLS
        local row = math.floor((i - 1) / ABSORB_CHIP_COLS)
        local chip = Font(gear, 10, 0.7, 0.75, 0.7)
        chip:SetPoint("TOPLEFT", 10 + col * 130, -16 - row * 12)
        chip:SetWidth(126)
        chip:SetWordWrap(false)
        chip:Hide()
        ui.absorbChips[i] = chip
    end

    ui.empty = Font(gear, 11, 0.6, 0.6, 0.6)
    ui.empty:SetPoint("TOPLEFT", 10, -58)
    ui.empty:Hide()

    ui.rows = {}
    for i = 1, #GEAR_SLOTS do
        -- "Button" (not "Frame") so the row can act as a real item link
        -- (2026-08-20): hover shows the actual item tooltip, shift-click
        -- inserts a real chat link -- same as the paper doll/bags. Only
        -- active when row.invSlot is set (an item actually occupies the
        -- slot -- see LayoutGear).
        local row = CreateFrame("Button", nil, gear)
        row:SetSize(520, 18)
        row:SetPoint("TOPLEFT", 10, -58 - (i - 1) * 19)
        row:EnableMouse(true)
        row:SetScript("OnEnter", function(self)
            if self.invSlot then
                GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
                GameTooltip:SetInventoryItem("player", self.invSlot)
                GameTooltip:Show()
            end
        end)
        row:SetScript("OnLeave", function()
            GameTooltip:Hide()
        end)
        row:SetScript("OnClick", function(self)
            if self.invSlot and IsModifiedClick("CHATLINK") then
                local link = GetInventoryItemLink("player", self.invSlot)
                if link then
                    HandleModifiedItemClick(link)
                end
            end
        end)
        row.icon = row:CreateTexture(nil, "ARTWORK")
        row.icon:SetSize(16, 16)
        row.icon:SetPoint("LEFT", 0, 0)
        row.name = Font(row, 11, 0.92, 0.92, 0.92)
        row.name:SetPoint("LEFT", 20, 0)
        row.name:SetWidth(168)
        row.stats = Font(row, 10, 0.7, 0.7, 0.7)
        row.stats:SetPoint("LEFT", 190, 0)
        row.stats:SetWidth(150)
        -- Plain level number, not a filled bar (2026-08-20) -- XP now
        -- shows as a real mouseover tooltip (self.tip, a 2-line table) via
        -- GameTooltip instead of a permanent bottom-of-panel text row.
        -- No more per-row "Attune" button either (2026-08-20 attunement
        -- redesign) -- gear now attunes automatically as it levels while
        -- equipped, nothing to click here anymore. See Bonesaw.md.
        row.level = CreateFrame("Frame", nil, row)
        row.level:SetSize(28, 16)
        row.level:SetPoint("RIGHT", 0, 0)
        row.level.label = Font(row.level, 10, 0.95, 0.95, 0.95)
        row.level.label:SetPoint("CENTER", 0, 0)
        row.level.label:SetJustifyH("CENTER")
        row.level:EnableMouse(true)
        row.level:SetScript("OnEnter", function(self)
            if self.tip and GameTooltip then
                GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
                GameTooltip:AddLine(self.tip[1], 1, 1, 1)
                GameTooltip:AddLine(self.tip[2], 0.8, 0.8, 0.8)
                GameTooltip:Show()
            end
        end)
        row.level:SetScript("OnLeave", function()
            if GameTooltip then
                GameTooltip:Hide()
            end
        end)
        ui.rows[i] = row
    end
end

function LG2.BuildAttunePanel(f)
    local attune = MakePanel(f)
    ui.attune = attune

    ui.aaToggle = CreateFrame("Button", nil, attune)
    ui.aaToggle:SetSize(124, 18)
    ui.aaToggle:SetPoint("TOP", attune, "TOP", -132, -2)
    StyleBtn(ui.aaToggle, COLOR_ON[1], COLOR_ON[2], COLOR_ON[3])
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
        StyleBtn(row.toggle, COLOR_ON[1], COLOR_ON[2], COLOR_ON[3])
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
end

function LG2.BuildWorldPanel(f)
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

    -- 2026-08-22 redesign. The old World tab was a single 4-wide grid of
    -- identical buttons followed by rows of unlabelled pips, all sharing one
    -- scroll. Three things were wrong with that and each is addressed here:
    --
    --   Actions and progression were interleaved. They are different kinds of
    --   thing -- one you press, one you earn -- so they now get their own
    --   sections with headings and a rule between them.
    --
    --   Colour was overloaded. Green meant BOTH "toggle is on" and "passive is
    --   unlocked", so Riding and Track Ore: ON looked identical while meaning
    --   different things. Toggles now carry an ON/OFF switch and own the
    --   green/red; everything else stops using those colours entirely.
    --
    --   The pips said nothing. Each track is now a card with a progress bar, a
    --   tier count, and the next reward spelled out from the `how` text that
    --   was already in the data and only ever reachable by hovering.
    local TOGGLE_ROW_H = 32
    local ACTION_W, ACTION_H = 124, 18
    local worldY = -6

    -- Wayfarer. One dial: everything spent on damage comes out of movement
    -- speed. Lives at the top of the World tab rather than inside the
    -- Movement track because it is the only perk in the addon the player
    -- SETS rather than simply owns.
    --
    -- The value is sent on mouse release, never on OnValueChanged: dragging
    -- fires that continuously, and every send is an addon whisper the server
    -- answers with a state line. The label still tracks the drag live so the
    -- slider does not feel dead while it is moving.
    ui.waySection = Font(content, 10, 0.5, 0.68, 0.92)
    ui.waySection:SetPoint("TOPLEFT", 6, worldY)
    ui.waySection:SetText("WAYFARER")
    worldY = worldY - 16

    local wayRow = CreateFrame("Frame", nil, content)
    wayRow:SetSize(506, 40)
    wayRow:SetPoint("TOPLEFT", 6, worldY)
    wayRow.bg = wayRow:CreateTexture(nil, "BACKGROUND")
    wayRow.bg:SetAllPoints(wayRow)
    Solid(wayRow.bg, 0.12, 0.12, 0.13, 1)
    ui.wayRow = wayRow

    ui.wayLabel = Font(wayRow, 11, 0.88, 0.9, 0.94)
    ui.wayLabel:SetPoint("TOPLEFT", 8, -4)
    ui.wayLabel:SetText("Wayfarer")

    ui.wayDesc = Font(wayRow, 9, 0.5, 0.5, 0.55)
    ui.wayDesc:SetPoint("TOPLEFT", 8, -18)
    ui.wayDesc:SetWidth(200)
    ui.wayDesc:SetText("Locked")

    local waySlider = CreateFrame("Slider", nil, wayRow)
    waySlider:SetOrientation("HORIZONTAL")
    waySlider:SetSize(230, 14)
    waySlider:SetPoint("RIGHT", wayRow, "RIGHT", -12, 0)
    waySlider:SetMinMaxValues(0, 100)
    waySlider:SetValueStep(5)
    waySlider:SetThumbTexture("Interface\\Buttons\\UI-SliderBar-Button-Horizontal")
    waySlider.track = waySlider:CreateTexture(nil, "BACKGROUND")
    waySlider.track:SetPoint("LEFT", 0, 0)
    waySlider.track:SetPoint("RIGHT", 0, 0)
    waySlider.track:SetHeight(4)
    Solid(waySlider.track, 0.24, 0.24, 0.28, 1)
    ui.waySlider = waySlider

    ui.wayLeft = Font(wayRow, 9, 0.6, 0.8, 0.98)
    ui.wayLeft:SetPoint("BOTTOMLEFT", waySlider, "TOPLEFT", 0, 2)
    ui.wayLeft:SetText("SPEED")
    ui.wayRight = Font(wayRow, 9, 0.98, 0.72, 0.55)
    ui.wayRight:SetPoint("BOTTOMRIGHT", waySlider, "TOPRIGHT", 0, 2)
    ui.wayRight:SetText("DAMAGE")

    waySlider:SetScript("OnValueChanged", function(self, value)
        LG2.PreviewWayfarer(value)
    end)
    waySlider:SetScript("OnMouseUp", function(self)
        LG2.SendWayfarer(self:GetValue())
    end)
    waySlider:SetScript("OnEnter", function()
        SetWorldTip("Wayfarer - slide toward SPEED or DAMAGE. Whatever one gains the other loses. Mounted and flying speed get half the movement share. Changing takes 30 seconds and cannot be done in combat.")
    end)
    waySlider:SetScript("OnLeave", function()
        SetWorldTip()
    end)

    worldY = worldY - 46

    ui.worldSectionToggles = Font(content, 10, 0.5, 0.68, 0.92)
    ui.worldSectionToggles:SetPoint("TOPLEFT", 6, worldY)
    ui.worldSectionToggles:SetText("TOGGLES")
    worldY = worldY - 16

    -- Laid out in two passes so toggles occupy a rail and the rest a grid,
    -- while ui.worldUnlocks stays indexed 1:1 with WORLD_UNLOCKS. LayoutWorld
    -- keeps its single loop that way, and nothing downstream has to care.
    local toggleIdx, actionIdx = 0, 0
    for i = 1, #WORLD_UNLOCKS do
        if WORLD_UNLOCKS[i].toggle then
            toggleIdx = toggleIdx + 1
        else
            actionIdx = actionIdx + 1
        end
    end
    local toggleCount = toggleIdx
    local toggleTop = worldY
    local actionTop = worldY - toggleCount * TOGGLE_ROW_H - 24
    ui.worldActionTop = actionTop

    ui.worldSectionActions = Font(content, 10, 0.5, 0.68, 0.92)
    ui.worldSectionActions:SetPoint("TOPLEFT", 6, actionTop + 16)
    ui.worldSectionActions:SetText("ACTIONS")

    local rule = content:CreateTexture(nil, "ARTWORK")
    rule:SetPoint("TOPLEFT", 6, actionTop + 34)
    rule:SetSize(500, 1)
    Solid(rule, 0.26, 0.26, 0.3, 1)

    ui.worldUnlocks = {}
    toggleIdx, actionIdx = 0, 0
    for i = 1, #WORLD_UNLOCKS do
        local info = WORLD_UNLOCKS[i]
        local btn = CreateFrame("Button", nil, content)
        if info.toggle then
            btn:SetSize(506, TOGGLE_ROW_H - 4)
            btn:SetPoint("TOPLEFT", 6, toggleTop - toggleIdx * TOGGLE_ROW_H)
            toggleIdx = toggleIdx + 1
            StyleBtn(btn, 0.12, 0.12, 0.13)
            btn.label = Font(btn, 11, 0.88, 0.9, 0.94)
            btn.label:SetPoint("TOPLEFT", 8, -3)
            -- The requirement/description text has always existed on every
            -- perk. It was only ever visible on hover; here it is just there.
            btn.desc = Font(btn, 9, 0.5, 0.5, 0.55)
            btn.desc:SetPoint("TOPLEFT", 8, -16)
            btn.desc:SetWidth(400)
            btn.desc:SetText(info.how)
            btn.sw = CreateFrame("Frame", nil, btn)
            btn.sw:SetSize(52, 16)
            btn.sw:SetPoint("RIGHT", btn, "RIGHT", -8, 0)
            btn.sw.bg = btn.sw:CreateTexture(nil, "ARTWORK")
            btn.sw.bg:SetAllPoints(btn.sw)
            btn.swLabel = Font(btn.sw, 10, 0.9, 0.9, 0.9)
            btn.swLabel:SetPoint("CENTER", 0, 0)
            btn.swLabel:SetJustifyH("CENTER")
        else
            local col = actionIdx % 4
            local row = math.floor(actionIdx / 4)
            actionIdx = actionIdx + 1
            btn:SetSize(ACTION_W, ACTION_H)
            btn:SetPoint("TOPLEFT", 6 + col * 128, actionTop - row * 22)
            StyleBtn(btn, 0.12, 0.12, 0.12)
            btn.label = Font(btn, 10, 0.85, 0.85, 0.85)
            btn.label:SetPoint("CENTER", 0, 0)
            btn.label:SetJustifyH("CENTER")
        end
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
                LG2.SendWorldToggle(info)
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
    local actionRows = 0
    for i = 1, #WORLD_UNLOCKS do
        if not WORLD_UNLOCKS[i].toggle then
            actionRows = actionRows + 1
        end
    end
    actionRows = math.ceil(actionRows / 4)
    -- 56 rather than 44: the "Next" line gets two lines to itself now instead
    -- of being truncated mid-sentence.
    local CARD_H = 56
    -- 13 rather than 10: these segments are click targets now, not decoration.
    local BAR_X, BAR_W, BAR_H = 204, 150, 13
    local trackY = ui.worldActionTop - actionRows * 22 - 34

    -- Rule sits ABOVE the heading, not below it. They were the wrong way round.
    local rule2 = content:CreateTexture(nil, "ARTWORK")
    rule2:SetPoint("TOPLEFT", 6, trackY + 38)
    rule2:SetSize(500, 1)
    Solid(rule2, 0.26, 0.26, 0.3, 1)
    ui.worldSectionProgress = Font(content, 10, 0.5, 0.68, 0.92)
    ui.worldSectionProgress:SetPoint("TOPLEFT", 6, trackY + 22)
    ui.worldSectionProgress:SetText("PROGRESSION")

    ui.worldPoints = Font(content, 11, 0.95, 0.82, 0.35)
    ui.worldPoints:SetPoint("TOPLEFT", 122, trackY + 21)
    ui.worldPoints:SetJustifyH("LEFT")

    ui.worldRespec = CreateFrame("Button", nil, content)
    ui.worldRespec:SetSize(84, 18)
    ui.worldRespec:SetPoint("TOPLEFT", 422, trackY + 26)
    StyleBtn(ui.worldRespec, COLOR_BTN[1], COLOR_BTN[2], COLOR_BTN[3])
    ui.worldRespec.label = Font(ui.worldRespec, 10, 0.9, 0.9, 0.9)
    ui.worldRespec.label:SetPoint("CENTER", 0, 0)
    ui.worldRespec.label:SetJustifyH("CENTER")
    ui.worldRespec.label:SetText("Refund all")
    ui.worldRespec:SetScript("OnClick", function()
        StaticPopup_Show("LG_PERK_RESPEC")
    end)

    for t = 1, #WORLD_TRACKS do
        local track = WORLD_TRACKS[t]
        local row = CreateFrame("Frame", nil, content)
        row:SetSize(506, CARD_H - 4)
        row:SetPoint("TOPLEFT", 6, trackY)
        row.bg = row:CreateTexture(nil, "BACKGROUND")
        row.bg:SetAllPoints(row)
        Solid(row.bg, 0.105, 0.105, 0.115, 1)
        row.head = Font(row, 11, 0.55, 0.85, 1)
        row.head:SetPoint("TOPLEFT", 8, -5)
        row.head:SetWidth(190)

        -- The bar IS the tier display now. One segment per tier, filled if that
        -- tier is held. The separate row of tick marks is gone: it sat on top
        -- of the Next line and collided with it, and it was saying the same
        -- thing this already says. Segments also mean an empty track draws as
        -- empty boxes rather than the 1px sliver a zero-width fill left behind.
        --
        -- Kept as individual textures rather than a StatusBar so a reskin
        -- cannot swap the texture out from under it, and so out-of-order
        -- unlocks stay visible -- which is the one thing the ticks did that a
        -- plain percentage bar cannot.
        local n = math.max(1, #track.ticks)
        local gap = n > 1 and 2 or 0
        local segW = math.max(2, math.floor((BAR_W - gap * (n - 1)) / n))
        row.segs = {}
        for i = 1, n do
            -- A Button rather than a Texture: each segment is the buy control
            -- for its own rank. The server re-validates every purchase, so a
            -- lit pip is a hint, not an authority.
            local seg = CreateFrame("Button", nil, row)
            seg:SetPoint("TOPLEFT", BAR_X + (i - 1) * (segW + gap), -6)
            seg:SetSize(segW, BAR_H)
            seg.bg = seg:CreateTexture(nil, "ARTWORK")
            seg.bg:SetAllPoints(seg)
            Solid(seg.bg, 0.07, 0.07, 0.08, 1)
            seg.trackIndex = t
            seg.tickIndex = i
            seg:SetScript("OnClick", function(self)
                local tk = WORLD_TRACKS[self.trackIndex]
                local info = tk and tk.ticks[self.tickIndex]
                if info and LG2.PerkBuyable(info.id) then
                    SendLine("PERKBUY|" .. tostring(info.id))
                end
            end)
            seg:SetScript("OnEnter", function(self)
                if not self.tip then
                    return
                end
                GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
                GameTooltip:SetText(self.tip, 1, 1, 1, 1, true)
                GameTooltip:Show()
            end)
            seg:SetScript("OnLeave", function()
                GameTooltip:Hide()
            end)
            row.segs[i] = seg
        end

        row.count = Font(row, 10, 0.6, 0.6, 0.66)
        row.count:SetPoint("TOPLEFT", BAR_X + BAR_W + 8, -5)
        -- Full card width and two lines. Nothing is drawn over this any more.
        row.next = Font(row, 9, 0.52, 0.52, 0.58)
        row.next:SetPoint("TOPLEFT", 8, -24)
        row.next:SetWidth(486)
        row.next:SetHeight(26)
        row.next:SetJustifyV("TOP")
        ui.worldTracks[t] = row
        trackY = trackY - CARD_H
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
        StyleBtn(btn, COLOR_BTN[1], COLOR_BTN[2], COLOR_BTN[3])
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
    StyleBtn(ui.chatToggle, COLOR_OFF[1], COLOR_OFF[2], COLOR_OFF[3])
    ui.chatToggle.label = Font(ui.chatToggle, 10, 0.9, 0.95, 0.9)
    ui.chatToggle.label:SetPoint("CENTER", 0, 0)
    ui.chatToggle.label:SetJustifyH("CENTER")
    ui.chatToggle.label:SetText("Show Living Gear chat: OFF")
    ui.chatToggle.tip = "Show Living Gear progress and unlock messages in chat. Errors always show."
    ui.chatToggle:SetScript("OnClick", function()
        LG2.SetShowChat(not LivingGearDB.showChat)
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
    StyleBtn(close, COLOR_BTN[1], COLOR_BTN[2], COLOR_BTN[3])
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
    StyleBtn(scaleBtn, COLOR_BTN[1], COLOR_BTN[2], COLOR_BTN[3])
    scaleBtn.icon = scaleBtn:CreateTexture(nil, "ARTWORK")
    scaleBtn.icon:SetTexture("Interface\\Icons\\INV_Misc_Spyglass_02")
    scaleBtn.icon:SetPoint("CENTER", 0, 0)
    scaleBtn.icon:SetSize(14, 14)
    scaleBtn:SetScript("OnClick", function()
        LG2.ToggleScaleMenu()
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
        StyleBtn(opt, COLOR_BTN[1], COLOR_BTN[2], COLOR_BTN[3])
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
    -- Centred as a block. The widths are label-derived so the total is not a
    -- constant, which is why it is measured first rather than hard-coded.
    local tabTotal = 0
    for i = 1, #TABS do
        local w = 10 + string.len(TABS[i].label) * 7
        if w < 48 then
            w = 48
        end
        tabTotal = tabTotal + w + 4
    end
    tabTotal = tabTotal - 4
    local tabX = math.floor((FRAME_W - tabTotal) / 2)
    for i = 1, #TABS do
        local info = TABS[i]
        local w = 10 + string.len(info.label) * 7
        if w < 48 then
            w = 48
        end
        local btn = CreateFrame("Button", nil, f)
        btn:SetSize(w, 18)
        btn:SetPoint("TOPLEFT", tabX, -28)
        StyleBtn(btn, COLOR_BG[1], COLOR_BG[2], COLOR_BG[3])
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

    LG2.BuildGearPanel(f)

    LG2.BuildAttunePanel(f)


    LG2.BuildLootPanel(f)

    LG2.BuildItemsPanel(f)

    ui.reagents, ui.reagentRows, ui.reagentEmpty, ui.reagentHint = LG2.MakeVaultPanel(f, VAULT_REAGENT, true)

    local class = MakePanel(f)
    ui.class = class
    -- Class-browse tabs: one pill per class that has perk data, in fixed
    -- CLASS_ORDER. Defaults to the player's own class (LG2.ClassBrowseToken());
    -- clicking another class previews its perks read-only. Built by a
    -- separate function -- see BuildClassTabs's comment for why.
    local classBelowTabsY = LG2.BuildClassTabs(class)
    -- No standalone instruction line (2026-08-20) -- the cards are
    -- self-explanatory (icon, title, and the actual changes right there),
    -- so "Pick one..."/"Preview only..." was redundant with what's already
    -- on screen. ui.classEmpty still covers the (currently never hit,
    -- since every class has data) case where a class has no perks at all.
    ui.classEmpty = Font(class, 11, 0.6, 0.6, 0.6)
    ui.classEmpty:SetPoint("TOPLEFT", 10, classBelowTabsY - 24)
    ui.classEmpty:SetText("No class perk for your class yet.")
    ui.classEmpty:Hide()
    -- Cards (2026-08-20): icon + title header, then the perk's changes as
    -- a small-font bulleted list below, all inside the card, so the 3
    -- specs can be scanned/compared side by side without hovering each one
    -- in turn.
    --
    -- Full height, not a fixed 128. The cards used to be sized for the common
    -- case of 2-3 short bullets and a longer list was allowed to spill past the
    -- card's background on the grounds that overflowing text still reads. It
    -- does not: once Subtlety grew to ten bullets, over half of them hung
    -- outside the green box on bare background and looked like a broken frame.
    -- Anchoring top and bottom means the card is always as tall as the panel
    -- allows, which is roughly three times the old height and comfortably fits
    -- the longest spec.
    local CLASS_CARD_W = 196
    local CLASS_CARD_GAP = 10
    ui.classBtns = {}
    for i = 1, 3 do
        local btn = CreateFrame("Button", nil, class)
        btn:SetWidth(CLASS_CARD_W)
        btn:SetPoint("TOPLEFT", 10 + (i - 1) * (CLASS_CARD_W + CLASS_CARD_GAP), classBelowTabsY - 24)
        btn:SetPoint("BOTTOMLEFT", class, "BOTTOMLEFT", 10 + (i - 1) * (CLASS_CARD_W + CLASS_CARD_GAP), 10)
        StyleBtn(btn, COLOR_BTN[1], COLOR_BTN[2], COLOR_BTN[3])
        btn.icon = btn:CreateTexture(nil, "ARTWORK")
        btn.icon:SetSize(22, 22)
        btn.icon:SetPoint("TOPLEFT", 6, -6)
        btn.icon:SetTexCoord(0.07, 0.93, 0.07, 0.93)
        -- Centred across the whole card rather than butted against the icon.
        -- The icon keeps its top-left corner and the title is short enough that
        -- the two never meet at this card width.
        btn.label = Font(btn, 12, 0.9, 0.9, 0.9)
        btn.label:SetPoint("TOPLEFT", 6, -6)
        btn.label:SetPoint("TOPRIGHT", -6, -6)
        btn.label:SetHeight(22)
        btn.label:SetJustifyH("CENTER")
        btn.label:SetJustifyV("MIDDLE")
        -- One wrapping text block instead of fixed-height single-line rows
        -- (2026-08-20) -- fixed rows clipped/ellipsized any sentence too
        -- long for one line rather than wrapping it. A single FontString
        -- word-wraps naturally and just grows downward; if a card's total
        -- text is unusually long it can extend past the card's background,
        -- which reads fine (text still fully visible, just not boxed) --
        -- better than losing words to an ellipsis.
        btn.body = Font(btn, 9, 0.65, 0.65, 0.65)
        btn.body:SetPoint("TOPLEFT", 8, -34)
        btn.body:SetJustifyH("LEFT")
        btn.body:SetJustifyV("TOP")
        btn.body:SetSpacing(3)
        btn.body:SetWordWrap(true)
        -- A FontString with word wrap on but no explicit height truncates with
        -- "..." past a few lines in this client instead of auto-growing, so it
        -- needs a definite height. Anchoring its bottom to the card supplies
        -- one, and now that the card fills the panel that height is generous
        -- enough for the longest spec without spilling outside the background.
        btn.body:SetPoint("BOTTOMRIGHT", -6, 8)

        -- Icon bullets. Each line is its own small frame so it can own a
        -- mouseover: the icon IS the bullet and the hover target, which is what
        -- lets someone read a spec they have never played and pull up the real
        -- spell tooltip for each ability it changes.
        --
        -- Deliberately not inline |Hspell:| hyperlinks in the body FontString:
        -- hyperlink hover on a plain frame is not something this addon does
        -- anywhere, and a per-line frame uses the same OnEnter pattern as every
        -- other hoverable thing here.
        btn._textW = CLASS_CARD_W - 32
        btn.bullets = {}
        for b = 1, 12 do
            local bl = CreateFrame("Frame", nil, btn)
            bl:EnableMouse(true)
            bl.icon = bl:CreateTexture(nil, "ARTWORK")
            bl.icon:SetSize(14, 14)
            bl.icon:SetPoint("TOPLEFT", 0, -1)
            bl.icon:SetTexCoord(0.07, 0.93, 0.07, 0.93)
            bl.text = Font(bl, 9, 0.68, 0.68, 0.68)
            bl.text:SetPoint("TOPLEFT", 18, 0)
            bl.text:SetJustifyH("LEFT")
            bl.text:SetJustifyV("TOP")
            bl.text:SetSpacing(2)
            bl.text:SetWordWrap(true)
            bl:SetScript("OnEnter", function(self)
                if not self.spell then
                    return
                end
                GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
                GameTooltip:SetHyperlink("spell:" .. tostring(self.spell))
                GameTooltip:Show()
            end)
            bl:SetScript("OnLeave", function()
                GameTooltip:Hide()
            end)
            bl:Hide()
            btn.bullets[b] = bl
        end
        btn:SetScript("OnClick", function()
            if btn.id and btn.ownClass then
                SendLine("CLASS|" .. tostring(btn.id))
            end
        end)
        btn:SetScript("OnEnter", function(self)
            Solid(self.bg, math.min(1, self._ir + 0.08), math.min(1, self._ig + 0.08), math.min(1, self._ib + 0.08), 1)
        end)
        btn:SetScript("OnLeave", function(self)
            Solid(self.bg, self._ir, self._ig, self._ib, 1)
        end)
        ui.classBtns[i] = btn
    end

    LG2.BuildWorldPanel(f)

    SaveScale(LG2.LoadScale())
    ShowTab("items")
    LayoutAll()
end

function LG2.OpenWindow()
    BuildUI()
    ui.frame:Show()
    RequestSync()
end

local function Toggle()
    BuildUI()
    if ui.frame:IsShown() then
        ui.frame:Hide()
    else
        LG2.OpenWindow()
    end
end

function LG2.IsAccountPerksName(name)
    if not name or name == "" then
        return false
    end
    local want = GetSpellInfo(ACCOUNT_PERKS_ID)
    -- Report #95: this used to also accept any name CONTAINING "Account
    -- Perks" or "Windblown", so anything casting a similarly-named spell --
    -- including whatever path fires on every kill -- popped the window. Only
    -- an exact match on the real perk spell's current name opens it now; a
    -- legacy "Windblown" name from before the rename no longer qualifies.
    return want ~= nil and name == want
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

-- Where the *Attuned Armory perk lands. Casting it used to call SendArmory and
-- nothing else, which only pushes data; the reveal lived on the ARMEND handler
-- and was removed when the armory feed started riding the ordinary login sync,
-- so the button silently did nothing from then on (report #104).
--
-- Shows rather than toggles: the player asked for the armory by name, so a cast
-- that happened to close the window would read as the button being broken
-- again.
function LG2.OpenArmoryView()
    LG2.OpenWindow()
    db.itemFilter = "attuned"
    ShowTab("items")
    LG2.RefreshItems()
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

function LG2.ParseLeaderBoard(text)
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

function LG2.BagCountForName(name)
    if type(origGetItemCount) ~= "function" or not name or name == "" then
        return 0
    end
    return tonumber(origGetItemCount(name)) or 0
end

function LG2.DesiredHave(name, need)
    local total = LG2.BagCountForName(name) + VaultCountForName(name)
    if need and need > 0 and total > need then
        total = need
    end
    return total
end

local function ApplyVaultCountToText(text)
    local name, cur, need = LG2.ParseLeaderBoard(text)
    if not name then
        return text
    end
    cur = cur or 0
    need = need or 0
    local total = LG2.DesiredHave(name, need)
    if total == cur then
        return text
    end
    if string.match(StripColors(text), "^%d+%s*/%s*%d+") then
        return tostring(total) .. "/" .. tostring(need) .. " " .. name
    end
    return name .. ": " .. tostring(total) .. "/" .. tostring(need)
end

function LG2.PatchWatchFrameLines()
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

function LG2.UpsertVault(kind, entry, count, name)
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
            LG2._vaultGen = (LG2._vaultGen or 0) + 1
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
    LG2._vaultGen = (LG2._vaultGen or 0) + 1
end

function LG2.HookQuestTracker()
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
        hooksecurefunc("WatchFrame_Update", LG2.PatchWatchFrameLines)
    end
end

LG2.HookQuestTracker()

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

function LG2.BindingHeld(cmd)
    if not GetBindingKey then
        return false
    end
    local k1, k2 = GetBindingKey(cmd)
    return KeyHeld(k1) or KeyHeld(k2)
end

local function DirHeld(cmd, keys)
    if LG2.BindingHeld(cmd) then
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

function LG2.SnapshotMoveKeys()
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

function LG2.HookJump()
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
        LG2.SnapshotMoveKeys()
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
LG2.HookJump()

-- Kill Combo's HUD frame lived here until 2026-08-22. The buff is a real
-- non-passive aura now, so the stock buff bar draws the icon, the stack
-- count and a working countdown -- a second copy floating over the screen
-- was just noise. The server no longer sends COMBO lines at all.

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

-- The "Effective 42 | Rewards 42 (zone ~20)" readout was removed 2026-08-22.
-- It reported internal scaling bookkeeping that a player could not act on.
-- The scaling itself is unchanged; only the display is gone.

-- The tail of an ITM line: the item entry id, then the ten secondary stat
-- deltas in the order the server packs them.
--
-- They are appended rather than interleaved because the fields ahead of them
-- sit at three different offsets depending on whether the item is equipped, in
-- a bag, or came from the legacy plain-slot form -- so each caller passes the
-- index it already worked out for its own shape.
--
-- Attunement is deliberately NOT resolved here. It is looked up at render time
-- against db.attuned, because ATL| and ITM| arrive in no guaranteed order and a
-- flag captured now would be wrong for every item that landed first.
function LG2.ReadItemTail(it, p, at)
    it.entry = tonumber(p[at]) or 0
    it.sec = {
        crit = tonumber(p[at + 1]) or 0,
        hit = tonumber(p[at + 2]) or 0,
        haste = tonumber(p[at + 3]) or 0,
        exp = tonumber(p[at + 4]) or 0,
        arpen = tonumber(p[at + 5]) or 0,
        resil = tonumber(p[at + 6]) or 0,
        ap = tonumber(p[at + 7]) or 0,
        sp = tonumber(p[at + 8]) or 0,
        dmin = tonumber(p[at + 9]) or 0,
        dmax = tonumber(p[at + 10]) or 0,
    }
end

-- Formats the growth a row shows: what levelling ADDED, never the item's own
-- printed stats. The tooltip already carries those, and repeating them would
-- hide the one number the panel exists to justify.
function LG2.GrowthText(it)
    local parts = {}
    local function add(v, label)
        local n = tonumber(v) or 0
        if n >= 1 then
            table.insert(parts, string.format("+%d%s", n, label))
        end
    end
    add(it.ds, "str")
    add(it.da, "agi")
    add(it.dt, "sta")
    add(it.di, "int")
    add(it.dp, "spi")
    local s = it.sec
    if s then
        add(s.crit, "crit")
        add(s.hit, "hit")
        add(s.haste, "haste")
        add(s.ap, "ap")
        add(s.sp, "sp")
        add(s.exp, "exp")
        add(s.arpen, "arp")
        add(s.resil, "res")
    end
    add(it.dar, "armor")
    if s and (tonumber(s.dmax) or 0) >= 1 then
        table.insert(parts, string.format("+%d-%d dmg", tonumber(s.dmin) or 0, tonumber(s.dmax) or 0))
    end
    if #parts == 0 then
        return ""
    end
    return table.concat(parts, " ")
end

function LG2.HandleAddon(prefix, message)
    if prefix ~= PREFIX or not message then
        return
    end
    if message == "OPEN" then
        OpenFromCast()
        return
    end
    if message == "OPENARM" then
        LG2.OpenArmoryView()
        return
    end
    -- ".bug" with no text asks for the report form. Built lazily so a player
    -- who never reports never pays for the frame.
    if message == "REPORTUI" then
        LG2.ReportUI.Toggle()
        return
    end
    -- A Create just materialised an item, so the entry is no longer uncreated.
    -- Ask for a fresh sync rather than patching the row locally: the item now
    -- exists in bags and belongs in the list as a real item, which is the
    -- ordinary sync's job to describe.
    if message == "ARMMADE" then
        RequestSync()
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
        -- db.perks/db.classPerks/db.classPerk are NOT cleared here: they're
        -- owned entirely by the independent PK/PKALL/CPK/CPKALL messages
        -- (sent from OnPlayerLogin, not from the REQ this CLR answers), so
        -- whichever channel happens to land second would otherwise silently
        -- wipe out data the other channel already delivered -- a race, not
        -- a redraw-ordering issue. See Bonesaw.md if this needs revisiting.
        db.rules = {}
        db.vault = {}
        LG2._vaultGen = (LG2._vaultGen or 0) + 1
        db.armory = {}
        db.attune = { on = 1, count = 0, off = 0 }
        -- Rebuilt in full by the ATL| burst that follows in this same sync,
        -- so clearing here is what keeps a de-attuned entry from lingering.
        db.attuned = {}
        db.jump = { mode = 2, max = 0 }
        -- db.solo/db.autoMount are NOT reset here, same reasoning as
        -- db.perks above: they're owned by the independent SQ|/AM|
        -- messages sent from OnPlayerLogin, and whichever channel landed
        -- second used to silently win, leaving the World Perks toggle
        -- showing "off" even when the server had it on.
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
        -- db.vault is fully populated by now (all VLT| lines land before
        -- END in the same sync burst) -- without this, the vaultLayoutPending
        -- flag this same statement just cleared above never gets a chance
        -- to be consumed by the OnUpdate poller, so the Reagents panel got
        -- stuck showing its empty initial state even with real data banked.
        if ui.reagents and ui.reagents:IsShown() then
            RefreshVaultPanel()
        end
        return
    end
    local p = LG2.SplitPipe(message)
    if p[1] == "PK" then
        db.perks[tonumber(p[2]) or 0] = tonumber(p[3]) or 0
        LG2.RefreshAchievementPerks()
        return
    end
    if p[1] == "PKPTS" then
        db.points = {
            avail = tonumber(p[2]) or 0,
            earned = tonumber(p[3]) or 0,
            spent = tonumber(p[4]) or 0,
        }
        LayoutRows()
        LG2.RefreshAchievementPerks()
        return
    end
    if p[1] == "PKCOST" then
        -- Additive across the burst, like PKALL: the server splits this over
        -- several lines because the addon-whisper channel truncates near 255
        -- bytes, so clearing here would drop every batch but the last.
        db.cost = db.cost or {}
        for entry in string.gmatch(p[2] or "", "[^,]+") do
            local id, cost, prereq = string.match(entry, "^(%d+):(%d+):(%d+)$")
            id = tonumber(id)
            if id then
                db.cost[id] = { cost = tonumber(cost) or 0, prereq = tonumber(prereq) or 0 }
            end
        end
        LayoutRows()
        LG2.RefreshAchievementPerks()
        return
    end
    if p[1] == "PKALL" then
        for id in string.gmatch(p[2] or "", "[^,]+") do
            local n = tonumber(id)
            if n then
                db.perks[n] = 1
            end
        end
        -- PKALL is sent independently of the CLR/END sync cycle (from
        -- OnPlayerLogin, not RequestSync), so it must trigger its own
        -- redraw instead of relying on END, which may have already run.
        BuildUI()
        LayoutRows()
        RefreshOverlays()
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
        BuildUI()
        LayoutRows()
        RefreshOverlays()
        return
    end
    if p[1] == "CPKALL" then
        db.classPerks = {}
        for pair in string.gmatch(p[2] or "", "[^,]+") do
            local id, sel = pair:match("^(%d+):(%d+)$")
            id = tonumber(id)
            if id then
                table.insert(db.classPerks, id)
                if tonumber(sel) == 1 then
                    db.classPerk = id
                end
            end
        end
        BuildUI()
        LayoutRows()
        RefreshOverlays()
        return
    end
    if p[1] == "AL" then
        db.autoloot.on = tonumber(p[2]) or 0
        db.autoloot.corpses = tonumber(p[3]) or 0
        db.autoloot.need = tonumber(p[4]) or 10
        LayoutLoot()
        return
    end
    if p[1] == "ALDE" then
        db.autoloot.de = tonumber(p[2]) or 0
        LayoutLoot()
        return
    end
    if p[1] == "AA" then
        db.attune.on = tonumber(p[2]) or 0
        db.attune.count = tonumber(p[3]) or 0
        db.attune.off = tonumber(p[4]) or 0
        return
    end
    if p[1] == "ATL" then
        for id in string.gmatch(p[2] or "", "[^,]+") do
            local n = tonumber(id)
            if n then
                db.attuned[n] = true
            end
        end
        return
    end
    if p[1] == "ATT" then
        local n = tonumber(p[2])
        if n then
            db.attuned[n] = true
        end
        return
    end
    if p[1] == "JMP" then
        db.jump.mode = tonumber(p[2]) or 0
        db.jump.max = tonumber(p[3]) or 0
        return
    end
    if p[1] == "SQ" then
        db.solo = tonumber(p[2]) or 0
        LayoutWorld()
        return
    end
    if p[1] == "AM" then
        db.autoMount = tonumber(p[2]) or 0
        LayoutWorld()
        return
    end
    if p[1] == "PULL" then
        db.pullRadius = tonumber(p[2]) or 0
        LayoutWorld()
        return
    end
    if p[1] == "WAY" then
        db.way = {
            pct = tonumber(p[2]) or 0,
            cap = tonumber(p[3]) or 0,
            cd = tonumber(p[4]) or 0,
        }
        LayoutWorld()
        return
    end
    if p[1] == "TRACKORE" then
        db.trackOre = tonumber(p[2]) or 0
        LayoutWorld()
        return
    end
    if p[1] == "TRACKHERB" then
        db.trackHerb = tonumber(p[2]) or 0
        LayoutWorld()
        return
    end
    if p[1] == "SCAP" then
        db.speedCap = tonumber(p[2]) or 500
        if ui.speedCap then
            LayoutWorld()
        end
        return
    end
    if p[1] == "QDONECD" then
        LG2.SetQuestCompleteCooldown(tonumber(p[2]) or 0)
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
            LG2._vaultGen = (LG2._vaultGen or 0) + 1
            if ui.reagents and ui.reagents:IsShown() then
                vaultLayoutPending = true
            end
            return
        end
        LG2.UpsertVault(kind, entry, count, name)
        if ui.frame and ui.frame:IsShown() then
            LayoutRows()
        end
        -- A live VLT| (a withdraw, or an auto-deposit while looting) only
        -- redrew the main rows, never the Reagents panel -- so taking an
        -- item out left the list showing the old count and read as another
        -- dead button. Hand it to the same OnUpdate poller the CLR/END
        -- sync path uses rather than refreshing inline on every line.
        if ui.reagents and ui.reagents:IsShown() then
            vaultLayoutPending = true
        end
        RefreshQuestWatch()
        return
    end
    if p[1] == "ARMCLR" then
        db.armory = {}
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
        -- Data only, deliberately. This feed rides the ordinary login sync, so
        -- revealing anything here would pop the window open on every login and
        -- every reload. The *Attuned Armory perk gets its own OPENARM message
        -- for the case where the player actually asked to see it.
        LG2.RefreshItems()
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
            LG2.ReadItemTail(it, p, 19)
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
            LG2.ReadItemTail(it, p, 20)
            key = "bag:" .. tostring(p[3]) .. ":" .. tostring(p[4])
        else
            it = {
                kind = "inv",
                slot = p[2],
                name = p[3],
                lv = p[4], xp = p[5], need = p[6],
                ds = p[7], da = p[8], dt = p[9], di = p[10], dp = p[11], dar = p[12],
            }
            LG2.ReadItemTail(it, p, 18)
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

function LG2.LevelProgress(it)
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
    bar:SetValue(LG2.LevelProgress(it))
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

local hookIconOverlaysDone = false
function LG2.HookIconOverlays()
    if hookIconOverlaysDone then
        return
    end
    hookIconOverlaysDone = true
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

function LG2.RequestTip(key)
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

function LG2.IsFlavorLine(text)
    return string.find(text, "Set:", 1, true)
        or string.find(text, "Equip:", 1, true)
        or string.find(text, "Use:", 1, true)
        or string.find(text, "Chance", 1, true)
        or string.find(text, "Requires", 1, true)
end

local STAT_NAMES = { "Strength", "Agility", "Stamina", "Intellect", "Spirit", "Armor" }

local function ParseStatLine(text)
    if text == "" or LG2.IsFlavorLine(text) or string.find(text, "%(%d+/%d+%)") then
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

function LG2.ApplyStatText(left, right, it, prefix, num, name)
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

function LG2.RewriteStatLines(tip, it)
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
            local field = LG2.ApplyStatText(left, right, it, prefix or "", num, name)
            if field then
                used[field] = true
            end
        end
    end)
    return used
end

function LG2.AddLivingFooter(tip, it)
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

-- "Do I already have this attuned?" -- the one question a player asks while
-- looking at a drop, which until now could only be answered by opening the
-- Armory and scrolling. Runs off db.attuned (ATL|/ATT|), so it is a table
-- lookup with no server round trip and no delay.
--
-- Guarded by its own _lgAttuned flag rather than sharing _lgFooter: the
-- footer is added from OnTooltipSetItem only, but this also has to survive
-- the paper doll re-showing the same tooltip every frame, and one shared
-- flag meant whichever ran first suppressed the other.
function LG2.AddAttunedLine(tip)
    if tip._lgAttuned then
        return
    end
    local _, link = tip:GetItem()
    if not link then
        return
    end
    local id = LG2.ItemIdFromArg(link)
    if not id or not db.attuned[id] then
        return
    end
    tip._lgAttuned = true
    tip:AddLine("|cff66ccffATTUNED|r", 0.4, 0.8, 1)
end

local function LivingForTip(tip)
    local key = tip and tip._lgKey
    if not key then
        return nil
    end
    local it = db.byKey[key]
    if not it then
        LG2.RequestTip(key)
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
        self._lgUsed = LG2.RewriteStatLines(self, it)
    end

    local origInv = tip.SetInventoryItem
    tip.SetInventoryItem = function(self, unit, slot, ...)
        self._lgFooter = nil
        self._lgAttuned = nil
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
        self._lgAttuned = nil
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
        -- Before the Living Gear footer and outside its "is this a tracked
        -- living item" gate: attunement is an account fact about the item
        -- entry, so it applies to a vendor listing or a link in chat just
        -- as much as to a piece the server is tracking for this character.
        LG2.AddAttunedLine(self)
        local it = LivingForTip(self)
        if not it then
            return
        end
        LG2.AddLivingFooter(self, it)
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
        self._lgAttuned = nil
        self._lgUsed = nil
    end)
end

local origGetItemCount = GetItemCount
if origGetItemCount then
    -- Pristine, pre-hook count. The craft staging path needs what the
    -- client's own native cast check will see (real bag contents), which the
    -- hooked GetItemCount below reports plus the vault.
    LG2.RawItemCount = origGetItemCount
    GetItemCount = function(item, includeBank, includeCharges)
        local n = origGetItemCount(item, includeBank, includeCharges) or 0
        local id = LG2.ItemIdFromArg(item)
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

-- How many of a recipe the player could make if the reagent bank counted as
-- bag space.
--
-- This is the piece that was missing, and it is why the old system had to
-- physically move materials into the backpack. GetTradeSkillReagentInfo was
-- already hooked below to report vault-inclusive counts, so a recipe LOOKED
-- craftable -- but numAvailable comes back from GetTradeSkillInfo, computed in
-- C from real bag contents, and TradeSkillFrame_Update greys out Create
-- whenever that reads 0. Fixing the reagent rows without fixing numAvailable
-- produced exactly bug #16: "shows up in the profession interface already" and
-- still refuses to craft.
--
-- Recomputing it from the (vault-inclusive) reagent rows is the actual answer
-- to "point the profession window at the reagent bank": the window is told
-- about the bank, instead of the bank being shovelled into the window ten
-- crafts at a time and left there.
function LG2.CraftableWithVault(index, numReagents, reagentInfo)
    if not index or not numReagents or numReagents < 1 then
        return nil
    end
    -- Bag contents move without the vault changing, so the cache is keyed on
    -- both. LG2._bagGen is bumped from BAG_UPDATE.
    local gen = (LG2._vaultGen or 0) * 1048576 + (LG2._bagGen or 0)
    if gen ~= LG2._craftAvailGen or not LG2._craftAvail then
        LG2._craftAvail, LG2._craftAvailGen = {}, gen
    end
    local hit = LG2._craftAvail[index]
    if hit ~= nil then
        return hit
    end

    local best
    for r = 1, numReagents do
        local _, _, required, have = reagentInfo(index, r)
        required = tonumber(required) or 0
        have = tonumber(have) or 0
        if required > 0 then
            local canDo = math.floor(have / required)
            if not best or canDo < best then
                best = canDo
            end
            if best == 0 then
                break
            end
        end
    end
    LG2._craftAvail[index] = best or false
    return best
end

LG2._origTradeSkillInfo = GetTradeSkillInfo
if LG2._origTradeSkillInfo then
    GetTradeSkillInfo = function(index)
        local name, skillType, numAvailable, isExpanded, altVerb, numSkillUps =
            LG2._origTradeSkillInfo(index)
        -- Headers have no reagents; asking about them returns nothing useful
        -- and would poison the cache.
        if name and skillType ~= "header" and GetTradeSkillNumReagents then
            local ok, n = pcall(LG2.CraftableWithVault, index,
                GetTradeSkillNumReagents(index), GetTradeSkillReagentInfo)
            if ok and n and n > (numAvailable or 0) then
                numAvailable = n
            end
        end
        return name, skillType, numAvailable, isExpanded, altVerb, numSkillUps
    end
end

-- Report #197: the diag used to capture whatever recipe the UI last QUERIED
-- (GetTradeSkillInfo fires for every list redraw), so a craft-all of Heavy
-- Runecloth Bandage reported "Anti-Venom (index 13)". Capture at the actual
-- craft call: the index passed to DoTradeSkill/DoCraft IS what the server is
-- asked to make.
LG2._origDoTradeSkill = DoTradeSkill
if LG2._origDoTradeSkill then
    DoTradeSkill = function(index, repeatCount)
        local numReagents = index and GetTradeSkillNumReagents and GetTradeSkillNumReagents(index)
        if index and numReagents then
            local name = LG2._origTradeSkillInfo and LG2._origTradeSkillInfo(index) or nil
            LG2._lastCraftDiag = { name = name, index = index,
                numReagents = numReagents,
                reagentInfo = GetTradeSkillReagentInfo,
                reagentLink = GetTradeSkillReagentItemLink }
        end
        if index and numReagents and LG2.StageVaultCraft
            and LG2.StageVaultCraft(index, numReagents, GetTradeSkillReagentInfo,
                GetTradeSkillReagentItemLink, repeatCount, GetTradeSkillItemLink) then
            return -- the server is crafting it straight from the vault
        end
        return LG2._origDoTradeSkill(index, repeatCount)
    end
end

LG2._origDoCraft = DoCraft
if LG2._origDoCraft then
    DoCraft = function(index, repeatCount)
        local numReagents = index and GetCraftNumReagents and GetCraftNumReagents(index)
        if index and numReagents then
            local name = LG2._origCraftInfo and LG2._origCraftInfo(index) or nil
            LG2._lastCraftDiag = { name = name, index = index,
                numReagents = numReagents,
                reagentInfo = GetCraftReagentInfo,
                reagentLink = GetCraftReagentItemLink }
        end
        if index and numReagents and LG2.StageVaultCraft
            and LG2.StageVaultCraft(index, numReagents, GetCraftReagentInfo,
                GetCraftReagentItemLink, repeatCount, GetCraftItemLink) then
            return -- the server is crafting it straight from the vault
        end
        return LG2._origDoCraft(index, repeatCount)
    end
end

-- Reports #195/#198 (recurring, critical): a craft whose reagents live only
-- in the reagent vault was refused with "Missing Reagents" and NOTHING in
-- the worldserver log. The window promises the craft because every count it
-- shows is vault-inclusive, but the C code under DoCraft/DoTradeSkill counts
-- REAL bag contents and refuses the cast before a packet is ever sent. The
-- player's own diag line is the shape of it: "needs 14047 x2 (bags 0-ish +
-- vault 249)" -- bags 249 was the vault-inclusive hooked count, the real
-- bags were empty.
--
-- The staging path pulls exactly the shortfall out of the vault (the same
-- TAKE message the vault panel's row click sends, with an exact count now)
-- and re-fires the craft the moment the withdrawal lands in the bags. A
-- craft the bags can already pay for stages nothing; a craft that is short
-- even with the vault stages nothing and lets the native refusal speak.
function LG2.StageVaultCraft(index, numReagents, reagentInfo, reagentLink, repeatCount, itemLinkFn)
    if not (index and numReagents and numReagents > 0 and reagentInfo and reagentLink
        and LG2.RawItemCount and SendLine) then
        return false
    end
    local want = tonumber(repeatCount) or 1
    if want < 1 then
        want = 1
    end
    local need, covered = {}, true
    for r = 1, numReagents do
        local ok, link = pcall(reagentLink, index, r)
        local id = ok and link and tonumber(string.match(link, "item:(%d+)"))
        local okReq, req = pcall(select, 3, reagentInfo(index, r))
        req = okReq and (tonumber(req) or 0) or 0
        if ok and id and req > 0 then
            local bags = tonumber(LG2.RawItemCount(id)) or 0
            local total = bags + (VaultCountOf(id) or 0)
            if total < req then
                -- Short even counting the vault: the refusal the player is
                -- about to see is the truth. Stage nothing.
                return false
            end
            if bags < req then
                covered = false
            end
            table.insert(need, { id = id, req = req, total = req * want, bags = bags })
        end
    end
    if covered then
        return false -- bags pay in full; nothing to stage, craft normally
    end
    -- Report #219 (reopened): staging the shortfall into the backpack was
    -- the old fix, and it defeats the entire point of the reagent bank --
    -- nothing should ever have to ride in the bags. The server's own cast
    -- gate already counts the vault (Spell::CheckItems) and pays
    -- bags-then-vault in place, so ask the server to run the craft itself.
    -- CRAFTCAST casts the recipe through the normal server path: nothing is
    -- withdrawn, and the crafted item still lands in the bags like any
    -- other craft. The recipe's spell id rides in its item link
    -- (enchant:...).
    local itemLink = itemLinkFn and itemLinkFn(index) or nil
    local spellId = itemLink and tonumber(string.match(itemLink, "enchant:(%d+)")) or nil
    if not spellId then
        -- No spell id could be parsed: fall back to the native refusal
        -- rather than staging reagents into the bags.
        return false
    end
    SendLine("CRAFTCAST|" .. tostring(spellId) .. "|" .. tostring(want))
    return true
end

LG2._origCraftInfo = GetCraftInfo
if LG2._origCraftInfo then
    GetCraftInfo = function(index)
        local name, subName, craftType, numAvailable, isExpanded, cost, level =
            LG2._origCraftInfo(index)
        if name and craftType ~= "header" and GetCraftNumReagents then
            local ok, n = pcall(LG2.CraftableWithVault, index,
                GetCraftNumReagents(index), GetCraftReagentInfo)
            if ok and n and n > (numAvailable or 0) then
                numAvailable = n
            end
        end
        return name, subName, craftType, numAvailable, isExpanded, cost, level
    end
end

-- Report #137/#138 (recurring): batch-crafting from the reagent bank reads as
-- "missing reagent" partway through. The server is right when it says no --
-- a spam-clicked batch can outrun the vault -- but the client made the
-- promise. Two holes, both closed now:
--   1. UpsertVault updated an existing row's count WITHOUT bumping
--      _vaultGen, so a live VLT| line during a craft left VaultMap and
--      CraftableWithVault showing the pre-craft count. The update path bumps
--      now, same as the insert path always did.
--   2. A refusal that was true a moment ago says nothing about now: between
--      the click and the error the vault may have changed under us. The
--      UI_ERROR_MESSAGE handler below drops the craftable cache on any
--      reagent error so the next frame's counts are recomputed from the
--      vault as it actually stands -- the window heals itself instead of
--      staying stale until the next BAG_UPDATE.
-- Own frame for this handler: the main event frame is declared further down
-- the file, and at this load point it does not exist yet. Registering
-- UI_ERROR_MESSAGE here keeps the cache-heal independent of load order.
local evCraftErr = CreateFrame("Frame")
evCraftErr:RegisterEvent("UI_ERROR_MESSAGE")
evCraftErr:SetScript("OnEvent", function(_, _, a1, a2)
    -- a1 is the message id, a2 the text (3.3.5 fires both shapes)
    local text = a2 or (GetGameMessageText and GetGameMessageText(a1)) or a1
    if type(text) == "string" and (string.find(text, SPELL_FAILED_REAGENTS or "Missing reagent", 1, true)
        or string.find(text, ERR_SPELL_FAILED_REAGENTS_GENERIC or "Missing reagent", 1, true)) then
        -- Report #192 diagnostics: a refusal with zero server-side refusals in
        -- the log means the CLIENT blocked the cast before it was ever sent.
        -- Name the recipe, its reagents, and where the counts came from, so the
        -- next occurrence is diagnosable from the player's own chat line.
        if LG2._lastCraftDiag then
            -- Report #192 follow-up: reagentLink/reagentInfo were called with the
            -- reagent index only; both want (craftIndex, reagentIndex), so
            -- the first reagent error during a craft-all threw inside this
            -- handler -- and because it throws, it also skipped the cache
            -- heal right below, un-doing the #137/#138 fix it sits next to.
            -- Diagnostics must never be able to re-break the bug they
            -- diagnose: pcall the whole block.
            pcall(function()
                local d = LG2._lastCraftDiag
                -- RawItemCount is the pristine, pre-hook count: the hooked
                -- GetItemCount adds the vault, which is why the old diag
                -- printed "bags 249 + vault 249" for a player whose bags
                -- were EMPTY (reports #195/#198) -- the refusal was the
                -- client's own native bag check, and the staging path now
                -- covers it.
                local raw = LG2.RawItemCount or origGetItemCount or GetItemCount
                local parts = {}
                for r = 1, (d.numReagents or 0) do
                    local link = d.reagentLink and d.reagentLink(d.index, r)
                    local id = link and tonumber(string.match(link, "item:(%d+)"))
                    local req = d.reagentInfo and select(3, d.reagentInfo(d.index, r)) or 0
                    local have = id and (raw(id) or 0) or 0
                    local vault = id and VaultCountOf(id) or 0
                    table.insert(parts, string.format("%s x%s (bags %d + vault %d)",
                        tostring(id), tostring(req), have, vault))
                end
                DEFAULT_CHAT_FRAME:AddMessage(string.format(
                    "|cffff3333[LG diag]|r craft blocked: %s (index %s) needs %s",
                    tostring(d.name), tostring(d.index), table.concat(parts, ", ")))
            end)
        end
        LG2._craftAvail, LG2._craftAvailGen = {}, nil
        LG2._bagGen = (LG2._bagGen or 0) + 1
    end
end)

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

-- The soulbind auto-confirm used to live here.
--
-- It is gone because the prompt is: rev_living_gear_no_soulbind.sql clears
-- bonding 1 and 2 from item_template, and bonding is sent to the client in the
-- item query response, so the client stops asking. Equipment does not bind at
-- all any more.
--
-- Retiring it also retires the bug class it kept producing. Clicking a
-- StaticPopup's button from script raced the dialog's own setup: OnShow runs
-- partway through StaticPopup_Show, before .data is filled in, so OnAccept
-- reached EquipPendingItem(nil) and threw "Usage: EquipPendingItem(index)".
-- That was worked around twice. Not showing the dialog at all is the fix.

function LG2.TryAutoAccept()
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

-- The CRAFTPREP staging request that used to live here is retired.
--
-- It existed because the 3.3.5 tradeskill/craft window works out "how many
-- can I make" purely from what is in your bags, so a recipe stocked only by
-- the reagent vault read as 0 and Create stayed greyed out; the addon asked
-- the server to move one craft's worth of materials into the backpack as a
-- fallback whenever a craft came back refused. That is the behaviour the
-- reagent bank exists to prevent -- materials being shovelled out of the
-- bank into bags for no reason the player can see.
--
-- Nothing needs it any more: the GetTradeSkillInfo / GetCraftInfo hooks
-- above make Create count the vault, the reagent rows are vault-inclusive,
-- the server answers the craft gate from the vault in place, and
-- Spell::TakeReagents pays the shortfall straight out of the vault. The
-- window shows the true potential total, nothing is moved, and a refused
-- craft is refused for a real reason worth reading instead of covered up
-- with a quiet withdrawal.

-- Bug report #16, 2026-08-22: "STILL can't do professions directly from reagent
-- bank, 'missing reagent: x' ... even though it shows up in the profession
-- interface already."
--
-- The original reading of that was "pre-stage materials into the backpack
-- whenever a craft is refused" (CRAFTPREP, with a watchdog that fired on a
-- cast that never reached the server and a staging attempt on any error
-- popup). That worked, but it made the reagent bank a place things were
-- constantly being shovelled out of and swept back into, for no reason the
-- player could see.
--
-- The real blocker was narrower and is now handled properly: numAvailable
-- came from GetTradeSkillInfo, computed in C from bag contents, and
-- TradeSkillFrame_Update greys out Create whenever it reads 0. That
-- function is hooked further up, so the window counts the reagent bank
-- itself. With the server answering the craft gate in place and consuming
-- from the vault directly, the fallback machinery (CraftFallback,
-- HookCraftPrep, NoteCraftCastSent, NoteCraftError, TickCraftWatch) has
-- nothing left to do and is gone.

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
ev:RegisterEvent("BAG_UPDATE")
ev:SetScript("OnEvent", function(_, event, a1, a2)
    if event == "PLAYER_LOGIN" then
        LG2.InstallChatFilter()
        BuildUI()
        HookTooltip(GameTooltip)
        HookTooltip(ItemRefTooltip)
        HookTooltip(ShoppingTooltip1)
        HookTooltip(ShoppingTooltip2)
        LG2.HookIconOverlays()
        pcall(HookQuestTracker)
        pcall(HookJump)
    elseif event == "PLAYER_ENTERING_WORLD" then
        RequestSync()
        ev._reqRetry = GetTime() + 2
        HideBankDeposit()
    elseif event == "CHAT_MSG_ADDON" then
        LG2.HandleAddon(a1, a2)
    elseif event == "BANKFRAME_OPENED" then
        ShowBankDeposit()
        RequestSync()
    elseif event == "BANKFRAME_CLOSED" then
        HideBankDeposit()
    elseif event == "QUEST_DETAIL" then
        LG2.TryAutoAccept()
    elseif event == "QUEST_GREETING" then
        -- Bug report #48: "quest auto-accept only works when there's a single
        -- quest available, not for questgivers with multiple quests." Both of
        -- these tested `== 1`, so a giver holding two or more quests had
        -- nothing selected at all and auto-accept looked broken rather than
        -- partial. Take the first available one instead.
        --
        -- That also chains on its own where the client re-shows the list after
        -- an accept: this same event fires again and picks the next quest. No
        -- protected call is involved, which is why it is done this way rather
        -- than by re-interacting with the NPC from Lua.
        if PerkKnown(910108) and not (IsShiftKeyDown and IsShiftKeyDown()) then
            if GetNumAvailableQuests and SelectAvailableQuest and GetNumAvailableQuests() >= 1 then
                SelectAvailableQuest(1)
            end
        end
    elseif event == "GOSSIP_SHOW" then
        if PerkKnown(910108) and not (IsShiftKeyDown and IsShiftKeyDown()) then
            if GetNumGossipAvailableQuests and SelectGossipAvailableQuest and GetNumGossipAvailableQuests() >= 1 then
                SelectGossipAvailableQuest(1)
            end
        end
    elseif event == "BAG_UPDATE" then
        -- Invalidates the craftable-count cache: what is in the bags is half
        -- of that sum.
        LG2._bagGen = (LG2._bagGen or 0) + 1
    elseif event == "UNIT_SPELLCAST_SENT" or event == "UNIT_SPELLCAST_SUCCEEDED" then
        if a1 == "player" and LG2.IsAccountPerksName(a2) then
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

function LG2.DumpTip(tip)
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

-- ---------------------------------------------------------------------
-- Complete Quest button on the quest log.
--
-- A repair tool for quests that have bugged out. Deliberately a button here
-- rather than a spell: a spellbook entry for "fix my broken quest" reads as
-- a game mechanic, and this is not one. The server owns the 10 minute
-- cooldown -- everything below is display only, so a client that lies about
-- the remaining time still gets refused.
--
-- 3.3.5 has no API that hands back the quest id directly, so it is pulled out
-- of the quest link, which is of the form
--   |cffffff00|Hquest:1234:15|h[Quest Name]|h|r
-- ---------------------------------------------------------------------
local questCompleteBtn
local questCompleteReadyAt = 0
-- Copper the server will charge to skip the remaining wait, pushed alongside
-- the cooldown so the two can never disagree.
local questCompleteCost = 0

local function SelectedQuestId()
    local index = GetQuestLogSelection()
    if not index or index < 1 then
        return nil
    end
    local link = GetQuestLink(index)
    if not link then
        return nil
    end
    local id = string.match(link, "|Hquest:(%d+):")
    return tonumber(id)
end

local function UpdateQuestCompleteBtn()
    if not questCompleteBtn then
        return
    end
    local left = questCompleteReadyAt - GetTime()
    if left > 0 then
        -- On cooldown the button becomes the buyout instead of going dead.
        -- The server sends the current price with the cooldown, so the label
        -- never has to model the escalation curve itself -- and it cannot
        -- disagree with what will actually be charged.
        if questCompleteCost and questCompleteCost > 0 and SelectedQuestId() then
            questCompleteBtn:Enable()
            questCompleteBtn:SetText(string.format("Skip Wait (%dg)",
                math.floor(questCompleteCost / 10000)))
            return
        end
        questCompleteBtn:Disable()
        questCompleteBtn:SetText(string.format("Complete (%d:%02d)",
            math.floor(left / 60), math.floor(left % 60)))
        return
    end
    questCompleteBtn:SetText("Complete Quest")
    -- A header row has no quest link; only enable on a real, selected quest.
    if SelectedQuestId() then
        questCompleteBtn:Enable()
    else
        questCompleteBtn:Disable()
    end
end

function LG2.SetQuestCompleteCooldown(seconds, cost)
    seconds = tonumber(seconds) or 0
    questCompleteCost = tonumber(cost) or 0
    questCompleteReadyAt = seconds > 0 and (GetTime() + seconds) or 0
    UpdateQuestCompleteBtn()
end

local function BuildQuestCompleteBtn()
    if questCompleteBtn or not QuestLogFrame then
        return
    end
    questCompleteBtn = CreateFrame("Button", "LivingGearQuestComplete", QuestLogFrame,
        "UIPanelButtonTemplate")
    questCompleteBtn:SetSize(130, 22)
    -- Anchored OUTSIDE the quest log, hanging below its bottom edge, rather
    -- than inside it (bug report #5, 2026-08-22: unclickable under ElvUI).
    --
    -- ElvUI reskins QuestLogFrame and lays its own textures and frames over
    -- the stock one, so anything parented inside the window competes with
    -- them for clicks and loses. Sitting outside the frame's bounds sidesteps
    -- the whole argument -- there is nothing there to overlap with -- and it
    -- looks the same on the default UI, just below the window instead of in
    -- the empty strip above Abandon.
    --
    -- Strata and level are raised for the same reason: a reskin that draws a
    -- backdrop over the region should still not end up on top of the button.
    --
    -- 2026-08-23: the offset was +78, which is UP from the bottom edge -- so
    -- despite the paragraph above, the button was sitting back inside the
    -- window, over the quest list. On ElvUI that lands squarely in the middle
    -- of the entries (screenshot: overlapping "Shadowsward Fragments"). A
    -- positive Y here means "above the anchor", so anything that is supposed
    -- to hang BELOW the frame has to be zero or negative.
    questCompleteBtn:SetPoint("TOP", QuestLogFrame, "BOTTOM", 0, -2)
    questCompleteBtn:SetFrameStrata("HIGH")
    questCompleteBtn:SetFrameLevel((QuestLogFrame:GetFrameLevel() or 0) + 10)
    questCompleteBtn:SetToplevel(true)
    questCompleteBtn:SetText("Complete Quest")
    questCompleteBtn:SetScript("OnClick", function()
        local id = SelectedQuestId()
        if not id then
            DEFAULT_CHAT_FRAME:AddMessage("|cff66ccff[Quest]|r Select a quest in the log first.")
            return
        end
        if questCompleteReadyAt - GetTime() > 0 then
            -- Paying is always a second, explicit press: the button has
            -- already relabelled itself with the price, so this click is the
            -- player accepting it rather than being surprised by a charge.
            SendLine("QDONEBUY|" .. id)
        else
            SendLine("QDONE|" .. id)
        end
    end)
    questCompleteBtn:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:SetText("Complete Quest")
        GameTooltip:AddLine("Force-completes the selected quest.", 1, 1, 1, true)
        GameTooltip:AddLine("For quests that have bugged out and cannot be finished normally. 10 minute cooldown.", 0.8, 0.8, 0.8, true)
        GameTooltip:Show()
    end)
    questCompleteBtn:SetScript("OnLeave", function() GameTooltip:Hide() end)
    questCompleteBtn:SetScript("OnUpdate", function(self, elapsed)
        self.since = (self.since or 0) + elapsed
        if self.since < 0.25 then
            return
        end
        self.since = 0
        UpdateQuestCompleteBtn()
    end)
    SendLine("QDONEREQ")
    UpdateQuestCompleteBtn()
end

local questCompleteWatcher = CreateFrame("Frame")
questCompleteWatcher:RegisterEvent("PLAYER_ENTERING_WORLD")
questCompleteWatcher:RegisterEvent("QUEST_LOG_UPDATE")
questCompleteWatcher:SetScript("OnEvent", function()
    BuildQuestCompleteBtn()
    UpdateQuestCompleteBtn()
end)

-- =====================================================================
-- Account Perks redesign preview  (/lgpreview)
--
-- Ten candidate layouts for the Account Perks panel, rendered in game from
-- the REAL perk data so they can be judged against a real account rather
-- than a mockup full of placeholder text.
--
-- This is a viewer, nothing more. It is deliberately inert:
--   - it never calls SendLine, so no addon command ever reaches the server
--   - it never writes db, so no setting is changed
--   - it never touches ui.*, so the live panel is untouched whether this
--     window is open or not
-- Every control drawn below is a picture of a control. Clicking one says so.
--
-- Each design is built once, lazily, into its own container frame; switching
-- designs just shows one and hides the rest. That avoids widget pooling
-- entirely, which 3.3.5 makes awkward since frames cannot be destroyed.
-- =====================================================================
local Preview = {}
LG2.Preview = Preview
Preview.panels = {}

local PV_W, PV_H = 720, 560
local PV_BODY_W = PV_W - 172

local PV_DESIGNS = {
    { key = "split",     label = "1 Split",       title = "Split: Actions vs Progression" },
    { key = "toggles",   label = "2 Toggle rail", title = "Toggle rail" },
    { key = "spellbook", label = "3 Spellbook",   title = "Spellbook model" },
    { key = "cards",     label = "4 Track cards", title = "Track cards" },
    { key = "grouped",   label = "5 Grouped",     title = "One list, grouped, with search" },
    { key = "dash",      label = "6 Dashboard",   title = "Dashboard first" },
    { key = "icons",     label = "7 Icon grid",   title = "Dense icon grid" },
    { key = "next",      label = "8 Next queue",  title = "What's next queue" },
    { key = "detail",    label = "9 Master/detail", title = "Master and detail" },
    { key = "minimal",   label = "10 Minimal",    title = "Shrink to almost nothing" },
}

-- ---- small drawing helpers, local to the preview ---------------------
local function PBox(parent, w, h, r, g, b, a)
    local f = CreateFrame("Frame", nil, parent)
    f:SetSize(w, h)
    f.bg = f:CreateTexture(nil, "BACKGROUND")
    f.bg:SetAllPoints(f)
    Solid(f.bg, r or 0.12, g or 0.12, b or 0.12, a or 1)
    return f
end

local function PLabel(parent, text, size, r, g, b)
    local fs = Font(parent, size or 12, r, g, b)
    fs:SetText(text or "")
    return fs
end

-- Two flat textures rather than a StatusBar: no texture atlas to fight, and
-- it renders identically under a reskin.
local function PBar(parent, w, h, pct, r, g, b)
    local f = PBox(parent, w, h, 0.08, 0.08, 0.08, 1)
    local fill = f:CreateTexture(nil, "ARTWORK")
    fill:SetPoint("LEFT", f, "LEFT", 0, 0)
    fill:SetSize(math.max(1, w * math.max(0, math.min(1, pct or 0))), h)
    Solid(fill, r or 0.25, g or 0.6, b or 0.3, 1)
    f.fill = fill
    return f
end

-- ---- read-only views of the live data --------------------------------
local function PV_TrackProgress(track)
    local total, got = 0, 0
    for i = 1, #track.ticks do
        total = total + 1
        if PerkKnown(track.ticks[i].id) then
            got = got + 1
        end
    end
    return got, total
end

local function PV_NextTick(track)
    for i = 1, #track.ticks do
        if not PerkKnown(track.ticks[i].id) then
            return track.ticks[i]
        end
    end
    return nil
end

local function PV_FirstSentence(s)
    if not s or s == "" then
        return ""
    end
    local cut = string.find(s, "%. ")
    if cut then
        return string.sub(s, 1, cut)
    end
    return s
end

local function PV_Toggles()
    local out = {}
    for i = 1, #WORLD_UNLOCKS do
        if WORLD_UNLOCKS[i].toggle then
            out[#out + 1] = WORLD_UNLOCKS[i]
        end
    end
    return out
end

local function PV_NonToggles()
    local out = {}
    for i = 1, #WORLD_UNLOCKS do
        if not WORLD_UNLOCKS[i].toggle then
            out[#out + 1] = WORLD_UNLOCKS[i]
        end
    end
    return out
end

-- =====================================================================
-- The ten designs. Each takes an empty container and fills it.
-- =====================================================================
local PV_BUILD = {}

-- 1. Split: actions above, progression below, with a real divider and each
--    half given a heading. The single change that addresses the actual
--    complaint -- two different kinds of thing sharing one scroll.
PV_BUILD.split = function(p)
    local y = -6
    PLabel(p, "ACTIONS", 11, 0.6, 0.75, 0.95):SetPoint("TOPLEFT", p, "TOPLEFT", 4, y)
    y = y - 18
    local acts = PV_NonToggles()
    local col, per = 0, 3
    local cw = math.floor((PV_BODY_W - 16) / per)
    for i = 1, math.min(#acts, 12) do
        local info = acts[i]
        local known = PerkKnown(info.id)
        local b = PBox(p, cw - 6, 20, known and 0.12 or 0.10, known and 0.26 or 0.10, known and 0.14 or 0.10, 1)
        b:SetPoint("TOPLEFT", p, "TOPLEFT", 4 + col * cw, y)
        local t = PLabel(b, info.name, 11, known and 0.85 or 0.45, known and 0.95 or 0.45, known and 0.85 or 0.45)
        t:SetPoint("LEFT", b, "LEFT", 6, 0)
        col = col + 1
        if col >= per then
            col = 0
            y = y - 23
        end
    end
    if col > 0 then y = y - 23 end
    y = y - 10

    local rule = p:CreateTexture(nil, "ARTWORK")
    rule:SetPoint("TOPLEFT", p, "TOPLEFT", 4, y)
    rule:SetSize(PV_BODY_W - 12, 1)
    Solid(rule, 0.3, 0.3, 0.34, 1)
    y = y - 14

    PLabel(p, "PROGRESSION", 11, 0.6, 0.75, 0.95):SetPoint("TOPLEFT", p, "TOPLEFT", 4, y)
    y = y - 18
    for t = 1, math.min(#WORLD_TRACKS, 9) do
        local track = WORLD_TRACKS[t]
        local got, total = PV_TrackProgress(track)
        PLabel(p, track.name, 11, 0.8, 0.85, 0.9):SetPoint("TOPLEFT", p, "TOPLEFT", 8, y)
        local bar = PBar(p, 200, 10, total > 0 and got / total or 0)
        bar:SetPoint("TOPLEFT", p, "TOPLEFT", 130, y - 1)
        PLabel(p, got .. "/" .. total, 10, 0.6, 0.6, 0.65):SetPoint("TOPLEFT", p, "TOPLEFT", 340, y)
        y = y - 17
    end
end

-- 2. Toggle rail: state lives in a switch, not in the label string, so the
--    red/green overload disappears. Description sits under the name, which
--    is where the `how` text has always belonged.
PV_BUILD.toggles = function(p)
    local y = -6
    PLabel(p, "Switches are drawn, not wired. Clicking does nothing here.", 10, 0.55, 0.55, 0.6)
        :SetPoint("TOPLEFT", p, "TOPLEFT", 4, y)
    y = y - 20
    local toggles = PV_Toggles()
    for i = 1, #toggles do
        local info = toggles[i]
        local on = WorldToggleOn(info)
        local row = PBox(p, PV_BODY_W - 12, 34, 0.11, 0.11, 0.12, 1)
        row:SetPoint("TOPLEFT", p, "TOPLEFT", 4, y)
        PLabel(row, info.name, 12, 0.9, 0.92, 0.95):SetPoint("TOPLEFT", row, "TOPLEFT", 8, -4)
        local sub = PLabel(row, PV_FirstSentence(info.how), 10, 0.5, 0.5, 0.55)
        sub:SetPoint("TOPLEFT", row, "TOPLEFT", 8, -18)
        sub:SetWidth(PV_BODY_W - 120)
        local sw = PBox(row, 54, 18, on and 0.15 or 0.2, on and 0.35 or 0.14, on and 0.18 or 0.14, 1)
        sw:SetPoint("RIGHT", row, "RIGHT", -8, 0)
        local st = PLabel(sw, on and "ON" or "OFF", 11, on and 0.7 or 0.75, on and 0.95 or 0.6, on and 0.75 or 0.6)
        st:SetPoint("CENTER", sw, "CENTER", 0, 0)
        st:SetJustifyH("CENTER")
        y = y - 38
    end
end

-- 3. Spellbook model: these utilities are already real spells. Draw them as
--    icons you would drag to a bar and the panel stops needing to hold them
--    at all. Icon here is a placeholder square; the real thing would pull
--    the spell texture.
PV_BUILD.spellbook = function(p)
    local y = -6
    PLabel(p, "These are real spells. Drag to an action bar and close this panel for good.", 10, 0.55, 0.55, 0.6)
        :SetPoint("TOPLEFT", p, "TOPLEFT", 4, y)
    y = y - 22
    local acts = PV_NonToggles()
    local col, per = 0, 8
    for i = 1, #acts do
        local info = acts[i]
        local known = PerkKnown(info.id)
        local cell = PBox(p, 54, 54, 0.1, 0.1, 0.11, 1)
        cell:SetPoint("TOPLEFT", p, "TOPLEFT", 6 + col * 60, y)
        local ic = PBox(cell, 38, 38, known and 0.18 or 0.12, known and 0.3 or 0.12, known and 0.2 or 0.12, 1)
        ic:SetPoint("TOP", cell, "TOP", 0, -3)
        local nm = PLabel(cell, info.name, 8, known and 0.8 or 0.4, known and 0.85 or 0.4, known and 0.8 or 0.4)
        nm:SetPoint("BOTTOM", cell, "BOTTOM", 0, 2)
        nm:SetWidth(52)
        nm:SetJustifyH("CENTER")
        col = col + 1
        if col >= per then
            col = 0
            y = y - 60
        end
    end
end

-- 4. Track cards: the pips become a bar with a number, and the next reward
--    is spelled out inline instead of hiding in a tooltip.
PV_BUILD.cards = function(p)
    local y = -6
    for t = 1, math.min(#WORLD_TRACKS, 8) do
        local track = WORLD_TRACKS[t]
        local got, total = PV_TrackProgress(track)
        local nxt = PV_NextTick(track)
        local card = PBox(p, PV_BODY_W - 12, 52, 0.11, 0.11, 0.12, 1)
        card:SetPoint("TOPLEFT", p, "TOPLEFT", 4, y)
        PLabel(card, track.name, 12, 0.9, 0.92, 0.95):SetPoint("TOPLEFT", card, "TOPLEFT", 8, -5)
        local tier = PLabel(card, "Tier " .. got .. " / " .. total, 10, 0.6, 0.8, 0.6)
        tier:SetPoint("TOPLEFT", card, "TOPLEFT", 110, -5)
        local bar = PBar(card, 180, 8, total > 0 and got / total or 0)
        bar:SetPoint("TOPLEFT", card, "TOPLEFT", 200, -6)
        local nextText = nxt and ("Next: " .. PV_FirstSentence(nxt.how)) or "Complete."
        local sub = PLabel(card, nextText, 10, 0.55, 0.55, 0.6)
        sub:SetPoint("TOPLEFT", card, "TOPLEFT", 8, -24)
        sub:SetWidth(PV_BODY_W - 30)
        sub:SetHeight(22)
        y = y - 56
    end
end

-- 5. Grouped list with a search box. Scales as perks keep being added, which
--    is the direction of travel.
PV_BUILD.grouped = function(p)
    local y = -6
    local sb = PBox(p, PV_BODY_W - 12, 22, 0.08, 0.08, 0.09, 1)
    sb:SetPoint("TOPLEFT", p, "TOPLEFT", 4, y)
    PLabel(sb, "Search perks...", 11, 0.45, 0.45, 0.5):SetPoint("LEFT", sb, "LEFT", 8, 0)
    local hide = PLabel(p, "[x] Hide locked", 10, 0.6, 0.6, 0.65)
    hide:SetPoint("TOPRIGHT", p, "TOPRIGHT", -8, y - 28)
    y = y - 30

    local sections = {
        { "TOGGLES", PV_Toggles() },
        { "UTILITIES", PV_NonToggles() },
    }
    for s = 1, #sections do
        y = y - 6
        PLabel(p, sections[s][1], 10, 0.55, 0.7, 0.9):SetPoint("TOPLEFT", p, "TOPLEFT", 6, y)
        y = y - 16
        local list = sections[s][2]
        for i = 1, math.min(#list, 7) do
            local info = list[i]
            local known = PerkKnown(info.id)
            local row = PBox(p, PV_BODY_W - 16, 18, 0.1, 0.1, 0.11, 1)
            row:SetPoint("TOPLEFT", p, "TOPLEFT", 6, y)
            PLabel(row, info.name, 11, known and 0.85 or 0.45, known and 0.9 or 0.45, known and 0.85 or 0.45)
                :SetPoint("LEFT", row, "LEFT", 6, 0)
            local st = PLabel(row, known and "unlocked" or "locked", 9, 0.5, 0.5, 0.55)
            st:SetPoint("RIGHT", row, "RIGHT", -6, 0)
            y = y - 20
        end
    end
end

-- 6. Dashboard: lead with what you already have, because checking status is
--    the common visit and browsing is the rare one.
PV_BUILD.dash = function(p)
    local y = -6
    PLabel(p, "WHAT YOU HAVE", 11, 0.55, 0.7, 0.9):SetPoint("TOPLEFT", p, "TOPLEFT", 6, y)
    y = y - 20
    local shown, col = 0, 0
    for t = 1, #WORLD_TRACKS do
        local track = WORLD_TRACKS[t]
        local got, total = PV_TrackProgress(track)
        if got > 0 then
            local tile = PBox(p, 168, 40, 0.11, 0.13, 0.11, 1)
            tile:SetPoint("TOPLEFT", p, "TOPLEFT", 6 + col * 176, y)
            PLabel(tile, track.name, 11, 0.8, 0.9, 0.8):SetPoint("TOPLEFT", tile, "TOPLEFT", 8, -5)
            PLabel(tile, got .. " of " .. total .. " tiers", 14, 0.6, 0.95, 0.65)
                :SetPoint("BOTTOMLEFT", tile, "BOTTOMLEFT", 8, 6)
            col = col + 1
            shown = shown + 1
            if col >= 3 then
                col = 0
                y = y - 46
            end
            if shown >= 9 then break end
        end
    end
    if col > 0 then y = y - 46 end
    y = y - 12

    PLabel(p, "NEXT THREE GOALS", 11, 0.55, 0.7, 0.9):SetPoint("TOPLEFT", p, "TOPLEFT", 6, y)
    y = y - 20
    local goals = 0
    for t = 1, #WORLD_TRACKS do
        local nxt = PV_NextTick(WORLD_TRACKS[t])
        if nxt and goals < 3 then
            local row = PBox(p, PV_BODY_W - 16, 34, 0.11, 0.11, 0.12, 1)
            row:SetPoint("TOPLEFT", p, "TOPLEFT", 6, y)
            PLabel(row, WORLD_TRACKS[t].name, 11, 0.85, 0.9, 0.95)
                :SetPoint("TOPLEFT", row, "TOPLEFT", 8, -4)
            local sub = PLabel(row, PV_FirstSentence(nxt.how), 10, 0.55, 0.55, 0.6)
            sub:SetPoint("TOPLEFT", row, "TOPLEFT", 8, -18)
            sub:SetWidth(PV_BODY_W - 40)
            goals = goals + 1
            y = y - 38
        end
    end
end

-- 7. Dense icon grid: everything at once in about a third the height, detail
--    on hover. The opposite bet to design 9.
PV_BUILD.icons = function(p)
    local y = -6
    PLabel(p, "Everything at a glance. Detail on hover.", 10, 0.55, 0.55, 0.6)
        :SetPoint("TOPLEFT", p, "TOPLEFT", 4, y)
    y = y - 20
    local col, per = 0, 10
    for i = 1, #WORLD_UNLOCKS do
        local info = WORLD_UNLOCKS[i]
        local known = PerkKnown(info.id)
        local on = info.toggle and WorldToggleOn(info) or false
        local r, g, b = 0.12, 0.12, 0.13
        if info.toggle and on then r, g, b = 0.14, 0.34, 0.16
        elseif info.toggle and known then r, g, b = 0.3, 0.14, 0.14
        elseif known then r, g, b = 0.16, 0.24, 0.3 end
        local cell = PBox(p, 34, 34, r, g, b, 1)
        cell:SetPoint("TOPLEFT", p, "TOPLEFT", 6 + col * 38, y)
        local ab = PLabel(cell, string.sub(info.name, 1, 3), 9, known and 0.85 or 0.4, known and 0.9 or 0.4, known and 0.85 or 0.4)
        ab:SetPoint("CENTER", cell, "CENTER", 0, 0)
        ab:SetJustifyH("CENTER")
        col = col + 1
        if col >= per then col = 0; y = y - 38 end
    end
    if col > 0 then y = y - 38 end
    y = y - 10
    PLabel(p, "green = toggle on   red = toggle off   blue = unlocked   grey = locked", 10, 0.5, 0.5, 0.55)
        :SetPoint("TOPLEFT", p, "TOPLEFT", 6, y)
end

-- 8. Next queue: turn the single "Next:" footer into the whole organising
--    idea. Everything still to earn, nearest first.
PV_BUILD.next = function(p)
    local y = -6
    PLabel(p, "Everything still to earn, closest first.", 10, 0.55, 0.55, 0.6)
        :SetPoint("TOPLEFT", p, "TOPLEFT", 4, y)
    y = y - 20
    local queue = {}
    for t = 1, #WORLD_TRACKS do
        local track = WORLD_TRACKS[t]
        local got, total = PV_TrackProgress(track)
        local nxt = PV_NextTick(track)
        if nxt then
            queue[#queue + 1] = { name = track.name, how = nxt.how, pct = total > 0 and got / total or 0 }
        end
    end
    table.sort(queue, function(a, b) return a.pct > b.pct end)
    for i = 1, math.min(#queue, 10) do
        local q = queue[i]
        local row = PBox(p, PV_BODY_W - 16, 32, 0.11, 0.11, 0.12, 1)
        row:SetPoint("TOPLEFT", p, "TOPLEFT", 6, y)
        local bar = PBar(row, 4, 32, 1, 0.3, 0.6 * q.pct + 0.2, 0.3)
        bar:SetPoint("LEFT", row, "LEFT", 0, 0)
        PLabel(row, q.name, 11, 0.9, 0.92, 0.95):SetPoint("TOPLEFT", row, "TOPLEFT", 12, -3)
        local sub = PLabel(row, PV_FirstSentence(q.how), 10, 0.55, 0.55, 0.6)
        sub:SetPoint("TOPLEFT", row, "TOPLEFT", 12, -16)
        sub:SetWidth(PV_BODY_W - 46)
        y = y - 36
    end
end

-- 9. Master and detail: categories left, full text right. No hovering to
--    read anything. The opposite bet to design 7.
PV_BUILD.detail = function(p)
    local listW = 150
    local list = PBox(p, listW, 300, 0.09, 0.09, 0.1, 1)
    list:SetPoint("TOPLEFT", p, "TOPLEFT", 4, -6)
    local y = -4
    for t = 1, math.min(#WORLD_TRACKS, 12) do
        local sel = (t == 2)
        local row = PBox(list, listW - 8, 20, sel and 0.16 or 0.09, sel and 0.2 or 0.09, sel and 0.26 or 0.1, 1)
        row:SetPoint("TOPLEFT", list, "TOPLEFT", 4, y)
        PLabel(row, WORLD_TRACKS[t].name, 11, sel and 0.9 or 0.6, sel and 0.95 or 0.6, sel and 1 or 0.65)
            :SetPoint("LEFT", row, "LEFT", 6, 0)
        y = y - 22
    end

    local track = WORLD_TRACKS[2]
    local dx = listW + 14
    PLabel(p, track.name, 15, 0.95, 0.95, 1):SetPoint("TOPLEFT", p, "TOPLEFT", dx, -8)
    local dy = -32
    for i = 1, #track.ticks do
        local tick = track.ticks[i]
        local known = PerkKnown(tick.id)
        local dot = PBox(p, 10, 10, known and 0.3 or 0.16, known and 0.7 or 0.16, known and 0.35 or 0.17, 1)
        dot:SetPoint("TOPLEFT", p, "TOPLEFT", dx, dy - 2)
        local txt = PLabel(p, tick.how, 10, known and 0.8 or 0.5, known and 0.85 or 0.5, known and 0.8 or 0.5)
        txt:SetPoint("TOPLEFT", p, "TOPLEFT", dx + 18, dy)
        txt:SetWidth(PV_BODY_W - dx - 26)
        txt:SetHeight(26)
        dy = dy - 30
    end
end

-- 10. Minimal: toggles only. Utilities live on action bars (design 3),
--     progression collapses to one line. The least panel that still works.
PV_BUILD.minimal = function(p)
    local y = -6
    local toggles = PV_Toggles()
    for i = 1, #toggles do
        local info = toggles[i]
        local on = WorldToggleOn(info)
        local row = PBox(p, 260, 20, 0.11, 0.11, 0.12, 1)
        row:SetPoint("TOPLEFT", p, "TOPLEFT", 4, y)
        PLabel(row, info.name, 11, 0.85, 0.9, 0.95):SetPoint("LEFT", row, "LEFT", 6, 0)
        local st = PLabel(row, on and "ON" or "OFF", 10, on and 0.6 or 0.7, on and 0.95 or 0.5, on and 0.65 or 0.5)
        st:SetPoint("RIGHT", row, "RIGHT", -8, 0)
        y = y - 23
    end
    y = y - 14
    local done, total = 0, 0
    for t = 1, #WORLD_TRACKS do
        local g, n = PV_TrackProgress(WORLD_TRACKS[t])
        done = done + g
        total = total + n
    end
    PLabel(p, "Perks earned: " .. done .. " of " .. total, 12, 0.7, 0.85, 0.7)
        :SetPoint("TOPLEFT", p, "TOPLEFT", 4, y)
    y = y - 20
    PLabel(p, "Everything castable lives on your action bars. Type /lg perks for the full list.", 10, 0.5, 0.5, 0.55)
        :SetPoint("TOPLEFT", p, "TOPLEFT", 4, y)
end

-- =====================================================================
-- The viewer frame itself.
-- =====================================================================
local function PV_Show(index)
    local pv = Preview.frame
    if not pv then
        return
    end
    for i = 1, #PV_DESIGNS do
        if Preview.panels[i] then
            Preview.panels[i]:Hide()
        end
        if pv.tabs[i] then
            Solid(pv.tabs[i].bg, i == index and 0.2 or 0.12, i == index and 0.26 or 0.12,
                i == index and 0.34 or 0.13, 1)
        end
    end
    if not Preview.panels[index] then
        local panel = CreateFrame("Frame", nil, pv.body)
        panel:SetAllPoints(pv.body)
        Preview.panels[index] = panel
        local ok, err = pcall(PV_BUILD[PV_DESIGNS[index].key], panel)
        if not ok then
            PLabel(panel, "This mockup failed to draw: " .. tostring(err), 11, 1, 0.5, 0.5)
                :SetPoint("TOPLEFT", panel, "TOPLEFT", 4, -6)
        end
    end
    Preview.panels[index]:Show()
    pv.title:SetText(PV_DESIGNS[index].title)
    Preview.current = index
end

function Preview.Toggle()
    if Preview.frame and Preview.frame:IsShown() then
        Preview.frame:Hide()
        return
    end
    if not Preview.frame then
        local pv = CreateFrame("Frame", "LivingGearPerkPreview", UIParent)
        pv:SetSize(PV_W, PV_H)
        pv:SetPoint("CENTER", UIParent, "CENTER", 0, 0)
        pv:SetFrameStrata("DIALOG")
        pv:SetToplevel(true)
        pv:EnableMouse(true)
        pv:SetMovable(true)
        pv:RegisterForDrag("LeftButton")
        pv:SetScript("OnDragStart", function(self) self:StartMoving() end)
        pv:SetScript("OnDragStop", function(self) self:StopMovingOrSizing() end)

        local bg = pv:CreateTexture(nil, "BACKGROUND")
        bg:SetAllPoints(pv)
        Solid(bg, 0.06, 0.06, 0.07, 0.96)

        local head = PLabel(pv, "Account Perks - design preview", 15, 0.95, 0.95, 1)
        head:SetPoint("TOPLEFT", pv, "TOPLEFT", 12, -10)

        -- Said plainly and kept on screen, because a mockup that looks live
        -- is worse than no mockup.
        local warn = PLabel(pv, "PREVIEW ONLY - nothing here is wired up. No setting is changed and nothing is sent to the server.",
            10, 1, 0.75, 0.4)
        warn:SetPoint("TOPLEFT", pv, "TOPLEFT", 12, -30)

        local close = CreateFrame("Button", nil, pv)
        close:SetSize(22, 20)
        close:SetPoint("TOPRIGHT", pv, "TOPRIGHT", -8, -8)
        StyleBtn(close, 0.3, 0.12, 0.12)
        local cx = PLabel(close, "X", 12, 0.95, 0.85, 0.85)
        cx:SetPoint("CENTER", close, "CENTER", 0, 0)
        close:SetScript("OnClick", function() pv:Hide() end)

        local title = PLabel(pv, "", 13, 0.75, 0.88, 1)
        title:SetPoint("TOPLEFT", pv, "TOPLEFT", 168, -50)

        pv.tabs = {}
        for i = 1, #PV_DESIGNS do
            local b = CreateFrame("Button", nil, pv)
            b:SetSize(150, 24)
            b:SetPoint("TOPLEFT", pv, "TOPLEFT", 10, -48 - (i - 1) * 27)
            StyleBtn(b, 0.12, 0.12, 0.13)
            local t = PLabel(b, PV_DESIGNS[i].label, 11, 0.85, 0.88, 0.92)
            t:SetPoint("LEFT", b, "LEFT", 8, 0)
            b:SetScript("OnClick", function() PV_Show(i) end)
            pv.tabs[i] = b
        end

        local body = CreateFrame("Frame", nil, pv)
        body:SetPoint("TOPLEFT", pv, "TOPLEFT", 168, -68)
        body:SetSize(PV_BODY_W, PV_H - 84)
        local bbg = body:CreateTexture(nil, "BACKGROUND")
        bbg:SetAllPoints(body)
        Solid(bbg, 0.085, 0.085, 0.095, 1)

        pv.title = title
        pv.body = body
        Preview.frame = pv
    end
    Preview.frame:Show()
    PV_Show(Preview.current or 1)
end

-- Perks are bought with achievement points, so the button to spend them
-- belongs where they are earned. AchievementFrame is load-on-demand, which is
-- why this waits for ADDON_LOADED instead of being built at login.
local achHook = CreateFrame("Frame")
achHook:RegisterEvent("ADDON_LOADED")
achHook:SetScript("OnEvent", function(self, _, name)
    if name ~= "Blizzard_AchievementUI" or not AchievementFrame then
        return
    end
    self:UnregisterEvent("ADDON_LOADED")

    -- Everything below is function-scoped on purpose. The main chunk is at
    -- 177 of Lua 5.1's hard limit of 200 locals and build_patch.py checks it.
    local PANEL_INSET = 20

    -- Blizzard's own content, hidden while the Perks tab is up and restored
    -- when either of its tabs is clicked. Named rather than walked because the
    -- frame is reskinned on some clients and a blind child sweep would also
    -- hide whatever the reskin added.
    local BLIZZ = {
        "AchievementFrameCategories", "AchievementFrameCategoriesContainer",
        "AchievementFrameAchievements", "AchievementFrameAchievementsContainer",
        "AchievementFrameSummary", "AchievementFrameStats",
        "AchievementFrameStatsContainer", "AchievementFrameStatsBG",
        "AchievementFrameWaterMark", "AchievementFrameSummaryAchievementsHeaderHeader",
    }
    local hidden = {}

    local function HideBlizzContent()
        for i = 1, #BLIZZ do
            local f = _G[BLIZZ[i]]
            if f and f:IsShown() then
                hidden[BLIZZ[i]] = true
                f:Hide()
            end
        end
    end

    local function RestoreBlizzContent()
        for name in pairs(hidden) do
            local f = _G[name]
            if f then
                f:Show()
            end
        end
        hidden = {}
    end

    -- Two panels, two tabs. Unlocks sits before Perks because it holds the
    -- cheap early conveniences -- mailbox, bank, autoloot -- and is where a new
    -- account spends its first points.
    local function MakePanel(hint)
        local f = CreateFrame("Frame", nil, AchievementFrame)
        f:SetPoint("TOPLEFT", AchievementFrame, "TOPLEFT", PANEL_INSET, -PANEL_INSET - 22)
        f:SetPoint("BOTTOMRIGHT", AchievementFrame, "BOTTOMRIGHT", -PANEL_INSET - 2, PANEL_INSET)
        f:Hide()
        f.points = f:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
        f.points:SetPoint("TOPLEFT", 4, -2)
        f.hint = f:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
        f.hint:SetPoint("TOPLEFT", 4, -24)
        f.hint:SetText(hint)
        f.respec = CreateFrame("Button", nil, f, "UIPanelButtonTemplate")
        f.respec:SetSize(96, 20)
        f.respec:SetPoint("TOPRIGHT", -2, -2)
        f.respec:SetText("Refund all")
        f.respec:SetScript("OnClick", function()
            StaticPopup_Show("LG_PERK_RESPEC")
        end)
        return f
    end

    local unlockPanel = MakePanel("Buy once, keep forever. Click a row you can afford.")
    local perkPanel = MakePanel("Gold is affordable - click to buy. Blue is earned by playing.")
    LG2.achUnlockPanel = unlockPanel
    LG2.achPanel = perkPanel

    LG2.BuildAchievementUnlockRows(unlockPanel)
    LG2.BuildAchievementPerkRows(perkPanel)

    local panels = { [3] = unlockPanel, [4] = perkPanel }

    local function HideOurPanels()
        unlockPanel:Hide()
        perkPanel:Hide()
    end

    -- Guarded: CreateFrame throws outright on an unknown template, and this
    -- runs inside ADDON_LOADED, so an unexpected build would take the entire
    -- addon down rather than just losing a tab. Falls back to the character
    -- sheet's tab template, which has existed since vanilla.
    local function MakeTab(id, label, anchor)
        local tab
        local ok = pcall(function()
            tab = CreateFrame("Button", "AchievementFrameTab" .. id, AchievementFrame,
                "AchievementFrameTabButtonTemplate")
        end)
        if not ok or not tab then
            ok = pcall(function()
                tab = CreateFrame("Button", "AchievementFrameTab" .. id, AchievementFrame,
                    "CharacterFrameTabButtonTemplate")
            end)
        end
        if not ok or not tab then
            return nil
        end
        tab:SetID(id)
        tab:SetText(label)
        tab:SetPoint("LEFT", anchor, "RIGHT", -8, 0)
        if PanelTemplates_TabResize then
            pcall(PanelTemplates_TabResize, tab, 0)
        end
        tab:SetScript("OnClick", function()
            PlaySound("igCharacterInfoTab")
            if PanelTemplates_SetTab then
                pcall(PanelTemplates_SetTab, AchievementFrame, id)
            end
            HideBlizzContent()
            HideOurPanels()
            panels[id]:Show()
            LG2.RefreshAchievementPerks()
            SendLine("PERKPTS")
        end)
        return tab
    end

    local unlockTab = MakeTab(3, "Unlocks", AchievementFrameTab2)
    if not unlockTab then
        return
    end
    local perkTab = MakeTab(4, "Perks", unlockTab)
    if not perkTab then
        return
    end
    if PanelTemplates_SetNumTabs then
        pcall(PanelTemplates_SetNumTabs, AchievementFrame, 4)
    end

    -- HookScript rather than hooksecurefunc on AchievementFrameTab_OnClick:
    -- that global takes different arguments across builds, and this does not
    -- care. Blizzard's own handler re-shows its content; this just gets our
    -- panels out of the way and undoes the specific frames we hid.
    for i = 1, 2 do
        local other = _G["AchievementFrameTab" .. i]
        if other then
            other:HookScript("OnClick", function()
                HideOurPanels()
                RestoreBlizzContent()
            end)
        end
    end

    AchievementFrame:HookScript("OnHide", function()
        HideOurPanels()
        RestoreBlizzContent()
    end)
end)

SLASH_LGPREVIEW1 = "/lgpreview"
SLASH_LGPREVIEW2 = "/lgpv"
SlashCmdList["LGPREVIEW"] = function()
    Preview.Toggle()
end

SLASH_LIVINGGEAR1 = "/lg"
SLASH_LIVINGGEAR2 = "/livinggear"
SlashCmdList["LIVINGGEAR"] = function(msg)
    msg = string.lower(string.gsub(msg or "", "^%s+", ""))
    if msg == "tip" then
        LG2.DumpTip(GameTooltip)
        return
    end
    Toggle()
end

-- Bug reports. /bugreport rather than /bug, because /bug is a stock WoW slash
-- command that opens Blizzard's own report frame -- that frame files into a
-- table nobody here reads, so overriding it would silently swallow reports.
-- The server also accepts ".bug <text>" for anyone without the addon loaded.
-- ---------------------------------------------------------------------
-- Report form (2026-08-29 redesign)
--
-- One intake for everything: /report (or bare /bugreport, /crit, ...) opens a
-- small window where the player picks Bug / Feature / Other, ticks Critical
-- and/or Recurring, types the report, and sends. Replaces the three separate
-- one-line slash flows; the server still accepts BUG|/FEATURE|/CRIT| so an
-- old client copy cannot lose a report.
--
-- Item links: pasting an item link from chat/bags into the edit box keeps its
-- |Hitem hyperlink, which travels over the addon channel and files as a
-- clickable link in the report. Typed "[item:12345]" ids are converted to a
-- link server-side.
-- ---------------------------------------------------------------------
local ReportUI = {}
LG2.ReportUI = ReportUI

-- Everything below hangs off ReportUI rather than the file's local list:
-- LivingGear.lua is within a handful of declarations of the Lua 5.1
-- 200-local main-chunk ceiling (see LG2 at the top and the matching
-- Bonesaw.md entry), so new low-call-count helpers go on tables.
ReportUI.KINDS = { { label = "Bug" }, { label = "Feature" }, { label = "Other" } }
ReportUI.W, ReportUI.H = 560, 430

-- A pasted item link keeps its full |Hitem...|h payload in the edit box text
-- and travels over the addon channel intact, so what files into the report is
-- a real clickable link. Color wrappers are stripped server-side; the
-- |Hitem:...|h[Name]|h core is left alone.
function ReportUI.Clean(raw)
    return raw or ""
end

function ReportUI.Toggle(kindOverride, critOverride)
    if ReportUI.frame and ReportUI.frame:IsShown() then
        ReportUI.frame:Hide()
        return
    end
    if not ReportUI.frame then
        ReportUI.Build()
    end
    ReportUI.frame:Show()
    if kindOverride then
        ReportUI.SetKind(kindOverride)
    end
    if critOverride then
        ReportUI.critBox:SetChecked(true)
    end
    ReportUI.body:SetFocus()
end

function ReportUI.SetKind(idx)
    ReportUI.kind = idx
    -- Kind list lives on ReportUI.KINDS since the main-chunk-local trim;
    -- the old REPORT_KINDS global is gone and #nil crashed every open and
    -- every Type click. StyleBtnColor takes a color TABLE, not r,g,b (the
    -- old r,g,b call here indexed a number -- the crash moved one line down
    -- once the loop bound was fixed, so both are fixed together).
    for i = 1, #ReportUI.KINDS do
        local btn = ReportUI.kindBtns[i]
        if btn then
            if i == idx then
                StyleBtnColor(btn, COLOR_ON)
            else
                StyleBtnColor(btn, COLOR_BTN)
            end
        end
    end
end

function ReportUI.Send()
    local text = ReportUI.Clean(ReportUI.body:GetText() or "")
    -- Links insert with full escape markup (|Hitem:ID:...|h[Name]|h): those
    -- pipes break the chunked, pipe-framed addon protocol, so reports with a
    -- pasted or shift+clicked link never arrived whole (report #208), and the
    -- markup ate most of the 230-letter budget. Collapse to the plain
    -- [item:ID] form -- the server expands it back into a real link -- and
    -- strip any leftover color codes.
    text = text:gsub("|Hitem:(%d+):[^|]*|h%[([^%]]-)%]|h", "[item:%1]")
    text = text:gsub("|H([a-z]+):(%d+):[^|]*|h%[([^%]]-)%]|h", "[%3]")
    text = text:gsub("|c%x%x%x%x%x%x%x%x", "")
    text = text:gsub("|r", "")
    if string.len(string.gsub(text, "%s", "")) < 5 then
        DEFAULT_CHAT_FRAME:AddMessage("|cff66ccff[Report]|r Say a little more about what went wrong or what you would like.")
        return
    end
    local crit = ReportUI.critBox:GetChecked() and 1 or 0
    local rec = ReportUI.recBox:GetChecked() and 1 or 0
    local kind = ReportUI.kind or 1
    SendLine(string.format("REPORT|%d|%d|%d|%s", kind - 1, crit, rec, text))
    ReportUI.body:SetText("")
    ReportUI.critBox:SetChecked(false)
    ReportUI.recBox:SetChecked(false)
    ReportUI.frame:Hide()
end

function ReportUI.Build()
    local f = CreateFrame("Frame", "LivingGearReportFrame", UIParent)
    f:SetSize(ReportUI.W, ReportUI.H)
    f:SetPoint("CENTER", UIParent, "CENTER", 0, 80)
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
    tinsert(UISpecialFrames, "LivingGearReportFrame")
    ReportUI.frame = f

    local title = Font(f, 13, 0.4, 0.8, 1)
    title:SetPoint("TOPLEFT", 10, -8)
    title:SetText("Report")

    local close = CreateFrame("Button", nil, f)
    close:SetSize(22, 18)
    close:SetPoint("TOPRIGHT", -8, -8)
    StyleBtn(close, COLOR_BTN[1], COLOR_BTN[2], COLOR_BTN[3])
    close.label = Font(close, 12, 0.9, 0.9, 0.9)
    close.label:SetPoint("CENTER", 0, 0)
    close.label:SetJustifyH("CENTER")
    close.label:SetText("X")
    close:SetScript("OnClick", function() f:Hide() end)

    -- Type row: three equal buttons, selected one tinted green.
    local kindLabel = Font(f, 10, 0.55, 0.55, 0.55)
    kindLabel:SetPoint("TOPLEFT", 10, -32)
    kindLabel:SetText("Type")

    ReportUI.kindBtns = {}
    for i, kd in ipairs(ReportUI.KINDS) do
        local btn = CreateFrame("Button", nil, f)
        btn:SetSize(100, 20)
        btn:SetPoint("TOPLEFT", 10 + (i - 1) * 107, -44)
        StyleBtn(btn, COLOR_BTN[1], COLOR_BTN[2], COLOR_BTN[3])
        btn.label = Font(btn, 11, 0.9, 0.9, 0.9)
        btn.label:SetPoint("CENTER", 0, 0)
        btn.label:SetJustifyH("CENTER")
        btn.label:SetText(kd.label)
        btn:SetScript("OnClick", function()
            ReportUI.SetKind(i)
        end)
        ReportUI.kindBtns[i] = btn
    end

    -- Flags row: labelled checkboxes (self-describing, no bare color cues).
    local function FlagBox(name, label, x, tip)
        local box = CreateFrame("CheckButton", nil, f)
        box:SetSize(18, 18)
        box:SetPoint("TOPLEFT", x, -74)
        box:SetNormalTexture("Interface\\Buttons\\UI-CheckBox-Up")
        box:SetPushedTexture("Interface\\Buttons\\UI-CheckBox-Down")
        box:SetHighlightTexture("Interface\\Buttons\\UI-CheckBox-Highlight")
        box:SetCheckedTexture("Interface\\Buttons\\UI-CheckBox-Check")
        local lbl = Font(f, 11, 0.85, 0.85, 0.85)
        lbl:SetPoint("LEFT", box, "RIGHT", 4, 0)
        lbl:SetText(label)
        box:SetScript("OnEnter", function(self)
            GameTooltip:SetOwner(self, "ANCHOR_LEFT")
            GameTooltip:SetText(tip, 0.9, 0.9, 0.9, true)
            GameTooltip:Show()
        end)
        box:SetScript("OnLeave", function() GameTooltip:Hide() end)
        return box
    end

    ReportUI.critBox = FlagBox("crit", "Critical", 10,
        "Everything stops until this is fixed. Use for game-breaking problems.")
    ReportUI.recBox = FlagBox("rec", "Recurring", 130,
        "Still happening or keeps coming back -- feedback on a previous fix.")

    -- Body: multi-line edit box, the whole point of the form.
    local bodyLabel = Font(f, 10, 0.55, 0.55, 0.55)
    bodyLabel:SetPoint("TOPLEFT", 10, -100)
    bodyLabel:SetText("What happened? Paste item links straight in.")

    local bodyWrap = CreateFrame("Frame", nil, f)
    bodyWrap:SetSize(ReportUI.W - 20, 260)
    bodyWrap:SetPoint("TOPLEFT", 10, -112)
    bodyWrap:SetBackdrop({
        bgFile = WHITE,
        edgeFile = WHITE,
        edgeSize = 1,
        insets = { left = 1, right = 1, top = 1, bottom = 1 },
    })
    bodyWrap:SetBackdropColor(0.08, 0.08, 0.08, 1)
    bodyWrap:SetBackdropBorderColor(0.22, 0.22, 0.22, 1)

    local body = CreateFrame("EditBox", nil, bodyWrap)
    body:SetPoint("TOPLEFT", 6, -4)
    body:SetPoint("BOTTOMRIGHT", -6, 4)
    body:SetFont("Fonts\\FRIZQT__.TTF", 11, "")
    body:SetTextColor(0.9, 0.9, 0.9, 1)
    body:SetAutoFocus(false)
    body:SetMultiLine(true)
    -- SendAddonMessage in the 3.3.5 client caps a whisper payload at 255
    -- bytes; a longer report would be silently cut off mid-sentence. Stay
    -- under it (REPORT|0|0|0| prefix eats 13 more).
    body:SetMaxLetters(230)
    body:SetScript("OnEscapePressed", function(self) self:ClearFocus() end)
    -- Enter must make a newline (multi-line box); Ctrl+Enter sends, so a
    -- half-written report is never blasted by reflex.
    body:SetScript("OnEnterPressed", function(self)
        if IsControlKeyDown() then
            ReportUI.Send()
        else
            self:Insert("\n")
        end
    end)
    ReportUI.body = body

    local send = CreateFrame("Button", nil, f)
    send:SetSize(90, 20)
    send:SetPoint("BOTTOMRIGHT", -10, 8)
    StyleBtn(send, COLOR_ADD[1], COLOR_ADD[2], COLOR_ADD[3])
    send.label = Font(send, 11, 0.85, 0.95, 0.85)
    send.label:SetPoint("CENTER", 0, 0)
    send.label:SetJustifyH("CENTER")
    send.label:SetText("Send")
    send:SetScript("OnClick", function() ReportUI.Send() end)

    local hint = Font(f, 9, 0.5, 0.5, 0.5)
    hint:SetPoint("BOTTOMLEFT", 10, 12)
    hint:SetText("Ctrl+Enter sends. Shift+click items in your bags to link them.")

    -- Live character counter next to the hint (max is on the next line).
    local count = Font(f, 9, 0.5, 0.5, 0.5)
    count:SetPoint("LEFT", hint, "RIGHT", 8, 0)
    body:HookScript("OnTextChanged", function()
        local n = body:GetNumLetters()
        count:SetText(n .. "/230")
        if n >= 200 then
            count:SetTextColor(1, 0.55, 0.3)
        end
    end)

    -- Shift+click item links land in the chat editbox by default, which is
    -- useless when the report body has focus. Route them here instead.
    if not ReportUI._insertHooked then
        ReportUI._insertHooked = true
        local origInsert = ChatEdit_InsertLink
        ChatEdit_InsertLink = function(text, ...)
            if ReportUI.frame and ReportUI.frame:IsShown()
                and ReportUI.body and ReportUI.body:HasFocus() then
                ReportUI.body:Insert(text)
                return true
            end
            return origInsert(text, ...)
        end
    end

    ReportUI.SetKind(1)
end

SLASH_LGREPORT1 = "/lgreport"
SLASH_LGREPORT2 = "/report"
SlashCmdList["LGREPORT"] = function(msg)
    ReportUI.Toggle()
end

-- Bug reports. /bugreport rather than /bug, because /bug is a stock WoW slash
-- command that opens Blizzard's own report frame -- that frame files into a
-- table nobody here reads, so overriding it would silently swallow reports.
-- Bare (no text) now opens the report form; with text it files a plain bug
-- directly, same as before.
SLASH_LGBUG1 = "/bugreport"
SLASH_LGBUG2 = "/lgbug"
SlashCmdList["LGBUG"] = function(msg)
    msg = string.gsub(msg or "", "^%s+", "")
    if msg == "" then
        ReportUI.Toggle()
        return
    end
    SendLine("BUG|" .. msg)
end

-- Feature requests and critical reports route through the same form now.
-- With text they keep their old server meaning (a feature-kind report, a
-- critical-priority one); bare they just open the form.
SLASH_LGFEATURE1 = "/featurerequest"
SLASH_LGFEATURE2 = "/lgfeature"
SlashCmdList["LGFEATURE"] = function(msg)
    msg = string.gsub(msg or "", "^%s+", "")
    if msg == "" then
        ReportUI.Toggle(2)   -- Feature preselected
        return
    end
    SendLine("FEATURE|" .. msg)
end

SLASH_LGCRIT1 = "/crit"
SLASH_LGCRIT2 = "/lgcrit"
SlashCmdList["LGCRIT"] = function(msg)
    msg = string.gsub(msg or "", "^%s+", "")
    if msg == "" then
        ReportUI.Toggle(1, true)   -- Bug preselected, Critical ticked
        return
    end
    SendLine("CRIT|" .. msg)
end

