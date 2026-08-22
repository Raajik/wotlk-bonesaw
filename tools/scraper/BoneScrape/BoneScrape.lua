-- BoneScrape -- passive server-data recorder for WotLK 3.3.5a clients.
--
-- Records everything the server hands this client into SavedVariables so it can
-- be parsed offline (tools/scraper/parse_bonescrape.py). It is strictly passive:
-- it hooks events and reads API state, it never targets, moves, clicks, casts,
-- or sends anything to the server. Nothing here is protected or taintable.
--
-- Data lands in WTF/Account/<ACCT>/SavedVariables/BoneScrape.lua on /reload or
-- clean logout, keyed by realm name so two servers never mix.
--
-- Lua 5.1 caps a main chunk at 200 locals, and every top-level `local` burns a
-- slot for the rest of the file, so helpers hang off the BS table instead of
-- getting their own `local function` (same pattern as LivingGear.lua).

local REV = 1
local BS = {}
local DB
local PEND = {}
local NPEND = 0

BoneScrapeDB = BoneScrapeDB or {}

local tip = CreateFrame("GameTooltip", "BoneScrapeTip", UIParent, "GameTooltipTemplate")
tip:SetOwner(UIParent, "ANCHOR_NONE")

local f = CreateFrame("Frame", "BoneScrapeFrame")

-- ---------------------------------------------------------------- helpers --

function BS.Strip(s)
    if type(s) ~= "string" then return nil end
    s = gsub(s, "|c%x%x%x%x%x%x%x%x", "")
    s = gsub(s, "|r", "")
    s = gsub(s, "|H.-|h(.-)|h", "%1")
    s = gsub(s, "|T.-|t", "")
    s = gsub(s, "|n", " ")
    if s == "" then return nil end
    return s
end

-- Tooltip text, both columns, tab-separated. Custom servers put their whole
-- feature surface in here (proc text, soulbind lines, upgrade counters), so
-- this is the single most valuable thing the addon collects.
function BS.TipLines()
    local out = {}
    local n = tip:NumLines()
    if not n or n < 1 then return nil end
    if n > 40 then n = 40 end
    for i = 1, n do
        local l = getglobal("BoneScrapeTipTextLeft" .. i)
        local r = getglobal("BoneScrapeTipTextRight" .. i)
        local lt = BS.Strip(l and l:GetText())
        local rt = BS.Strip(r and r:GetText())
        if lt and rt then
            tinsert(out, lt .. "\t" .. rt)
        elseif lt then
            tinsert(out, lt)
        elseif rt then
            tinsert(out, "\t" .. rt)
        end
    end
    if table.getn(out) == 0 then return nil end
    return out
end

function BS.Bump(t, key)
    if not key then return end
    t[key] = (t[key] or 0) + 1
end

function BS.Where()
    local zone = GetRealZoneText() or "?"
    local sub = GetSubZoneText()
    if sub == "" then sub = nil end
    -- SetMapToCurrentZone would yank the map out from under the player while
    -- they have it open, so only re-home it when the map is closed.
    if not WorldMapFrame:IsShown() then SetMapToCurrentZone() end
    local x, y = GetPlayerMapPosition("player")
    local m = 0
    if GetCurrentMapAreaID then m = GetCurrentMapAreaID() or 0 end
    x = floor((x or 0) * 1000) / 10
    y = floor((y or 0) * 1000) / 10
    return zone, sub, m, x, y
end

-- 3.3.5 GUIDs look like "0xF130000C9E000AB1": nibble 5 is the object type and
-- characters 9-12 are the template entry.
function BS.GuidId(guid)
    if type(guid) ~= "string" or strlen(guid) < 16 then return nil end
    local kind = strsub(guid, 5, 5)
    if kind ~= "1" and kind ~= "3" and kind ~= "4" and kind ~= "5" then
        return nil -- player, item, or something we do not care about
    end
    return kind, tonumber(strsub(guid, 9, 12), 16)
end

function BS.Bucket(name)
    if not DB[name] then DB[name] = {} end
    return DB[name]
end

-- Append to a capped list, skipping exact duplicates.
function BS.Push(list, value, cap)
    for i = 1, table.getn(list) do
        if list[i] == value then return false end
    end
    if table.getn(list) >= (cap or 50) then return false end
    tinsert(list, value)
    return true
end

-- ------------------------------------------------------------------ items --

function BS.Item(link, src)
    if type(link) ~= "string" then return end
    local istr = strmatch(link, "item:[%-%d:]+")
    if not istr then return end
    local id = tonumber(strmatch(istr, "item:(%d+)"))
    if not id or id == 0 then return end

    local t = BS.Bucket("items")[id]
    if not t then
        t = { n = 0, src = {} }
        BS.Bucket("items")[id] = t
    end
    t.n = t.n + 1
    BS.Bump(t.src, src)

    -- Keep every distinct suffix/enchant/random-property variant: on servers
    -- with custom item upgrade systems the variant tail is the feature.
    local tail = strmatch(istr, "^item:%d+:(.+)$")
    if tail and strfind(tail, "[1-9]") then
        t.v = t.v or {}
        if not t.v[tail] then
            t.v[tail] = true
            if not PEND[istr] then PEND[istr] = src or true; NPEND = NPEND + 1 end
        end
    end

    if t.name and t.tip then return end
    if not PEND[istr] then
        PEND[istr] = src or true
        NPEND = NPEND + 1
    end
end

-- GetItemInfo returns nil until the client has the server's item query
-- response, so resolution is retried off a ticker rather than inline.
function BS.Resolve(istr)
    local name, _, q, ilvl, req, cls, sub, stack, slot, tex, price = GetItemInfo(istr)
    if not name then return false end
    local id = tonumber(strmatch(istr, "item:(%d+)"))
    local t = BS.Bucket("items")[id]
    if not t then return true end
    t.name, t.q, t.ilvl, t.req = name, q, ilvl, req
    t.cls, t.sub, t.stack, t.slot, t.price = cls, sub, stack, slot, price
    t.tex = gsub(tex or "", "^.*\\", "")

    tip:SetOwner(UIParent, "ANCHOR_NONE")
    tip:ClearLines()
    local ok = pcall(tip.SetHyperlink, tip, istr)
    if ok then
        local lines = BS.TipLines()
        if lines then
            local tail = strmatch(istr, "^item:%d+:(.+)$")
            if tail and strfind(tail, "[1-9]") then
                t.vtip = t.vtip or {}
                t.vtip[tail] = lines
            else
                t.tip = lines
            end
        end
    end
    tip:Hide()
    return true
end

-- ----------------------------------------------------------------- spells --

function BS.Spell(id, name, src)
    id = tonumber(id)
    if not id or id == 0 then return end
    local t = BS.Bucket("spells")[id]
    if not t then
        t = { n = 0, src = {} }
        BS.Bucket("spells")[id] = t
    end
    t.n = t.n + 1
    BS.Bump(t.src, src)
    if name and not t.name then t.name = name end
    if t.tip then return end

    tip:SetOwner(UIParent, "ANCHOR_NONE")
    tip:ClearLines()
    local ok = pcall(tip.SetHyperlink, tip, "spell:" .. id)
    if ok then
        local lines = BS.TipLines()
        if lines then
            t.tip = lines
            if not t.name then t.name = lines[1] end
        end
    end
    tip:Hide()
end

-- ------------------------------------------------------------------ units --

function BS.Unit(unit, src)
    if not UnitExists(unit) then return end
    if UnitIsPlayer(unit) or UnitPlayerControlled(unit) then return end
    local kind, id = BS.GuidId(UnitGUID(unit))
    if not id or id == 0 then return end

    local bucket = BS.Bucket("npcs")
    local t = bucket[id]
    if not t then
        t = { n = 0, loc = {} }
        bucket[id] = t
    end
    t.n = t.n + 1
    t.name = UnitName(unit) or t.name
    t.lvl = UnitLevel(unit)
    t.hp = UnitHealthMax(unit)
    t.mana = UnitManaMax(unit)
    t.cls = UnitClassification(unit)
    t.ct = UnitCreatureType(unit)
    t.fam = UnitCreatureFamily(unit)
    t.react = UnitReaction("player", unit)
    if kind == "5" then t.vehicle = true end
    if kind == "4" then t.pet = true end

    local zone, sub, map, x, y = BS.Where()
    if x > 0 or y > 0 then
        BS.Push(t.loc, zone .. "|" .. (sub or "") .. "|" .. map .. "|" .. x .. "|" .. y, 25)
    end

    -- Anything sitting on a mob is fair game for a custom aura system.
    for i = 1, 40 do
        local aname, _, _, _, _, _, _, _, _, _, sid = UnitAura(unit, i)
        if not aname then break end
        if sid then BS.Spell(sid, aname, "aura") end
    end
end

-- --------------------------------------------------------------- handlers --

BS.ev = {}

function BS.NpcId()
    local kind, id = BS.GuidId(UnitGUID("npc") or UnitGUID("target") or "")
    return id or 0
end

function BS.ev.MERCHANT_SHOW()
    local zone, sub, map, x, y = BS.Where()
    local rec = {
        name = UnitName("npc") or UnitName("target"),
        zone = zone, sub = sub, map = map, x = x, y = y,
        items = {},
    }
    for i = 1, GetMerchantNumItems() do
        local iname, _, price, qty, avail, usable, ec = GetMerchantItemInfo(i)
        local link = GetMerchantItemLink(i)
        BS.Item(link, "vendor")
        local row = {
            name = iname, link = strmatch(link or "", "item:[%-%d:]+"),
            price = price, qty = qty, avail = avail, usable = usable and 1 or 0,
        }
        if ec and GetMerchantItemCostInfo then
            row.cost = {}
            for j = 1, GetMerchantItemCostInfo(i) do
                local ctex, amt, clink = GetMerchantItemCostItem(i, j)
                BS.Item(clink, "vendorcost")
                tinsert(row.cost, {
                    n = amt,
                    link = strmatch(clink or "", "item:[%-%d:]+"),
                    tex = gsub(ctex or "", "^.*\\", ""),
                })
            end
        end
        tinsert(rec.items, row)
    end
    BS.Bucket("vendors")[BS.NpcId()] = rec
end

function BS.ev.GOSSIP_SHOW()
    local id = BS.NpcId()
    local bucket = BS.Bucket("gossip")
    local t = bucket[id]
    if not t then
        t = { menus = {} }
        bucket[id] = t
        local zone, sub, map, x, y = BS.Where()
        t.name = UnitName("npc") or UnitName("target")
        t.zone, t.sub, t.map, t.x, t.y = zone, sub, map, x, y
    end

    local body = BS.Strip(GetGossipText()) or ""
    local menu = { text = body, opt = {} }
    local o = { GetGossipOptions() }
    for i = 1, table.getn(o), 2 do
        tinsert(menu.opt, (BS.Strip(o[i]) or "") .. "\t" .. tostring(o[i + 1]))
    end
    local a = { GetGossipAvailableQuests() }
    for i = 1, table.getn(a), 5 do
        tinsert(menu.opt, "AVAILQUEST\t" .. (BS.Strip(a[i]) or ""))
    end
    local c = { GetGossipActiveQuests() }
    for i = 1, table.getn(c), 5 do
        tinsert(menu.opt, "ACTIVEQUEST\t" .. (BS.Strip(c[i]) or ""))
    end

    -- One NPC can have many menu states (mythic panels, soulbind windows,
    -- bounty boards), so keep each distinct one instead of overwriting.
    local sig = body .. "##" .. table.concat(menu.opt, "|")
    for i = 1, table.getn(t.menus) do
        if t.menus[i].sig == sig then return end
    end
    menu.sig = sig
    if table.getn(t.menus) < 40 then tinsert(t.menus, menu) end
end

function BS.QuestFrame(kind)
    local title = BS.Strip(GetTitleText()) or "?"
    local bucket = BS.Bucket("quests")
    local t = bucket[title]
    if not t then
        t = {}
        bucket[title] = t
    end
    t[kind] = BS.Strip(GetQuestText())
    if kind == "detail" then
        t.obj = BS.Strip(GetObjectiveText())
        t.giver = BS.NpcId()
        t.giverName = UnitName("npc") or UnitName("target")
        local zone, sub, map, x, y = BS.Where()
        t.zone, t.map, t.x, t.y = zone, map, x, y
    end
    if kind == "complete" then
        if GetRewardMoney then t.money = GetRewardMoney() end
        if GetRewardXP then t.xp = GetRewardXP() end
        t.reward, t.choice = {}, {}
        for i = 1, (GetNumQuestRewards() or 0) do
            local l = GetQuestItemLink("reward", i)
            BS.Item(l, "questreward")
            tinsert(t.reward, strmatch(l or "", "item:[%-%d:]+") or "?")
        end
        for i = 1, (GetNumQuestChoices() or 0) do
            local l = GetQuestItemLink("choice", i)
            BS.Item(l, "questreward")
            tinsert(t.choice, strmatch(l or "", "item:[%-%d:]+") or "?")
        end
    end
end

function BS.ev.QUEST_DETAIL() BS.QuestFrame("detail") end
function BS.ev.QUEST_PROGRESS() BS.QuestFrame("progress") end
function BS.ev.QUEST_COMPLETE() BS.QuestFrame("complete") end

function BS.ev.TRAINER_SHOW()
    local rec = { name = UnitName("npc") or UnitName("target"), svc = {} }
    for i = 1, (GetNumTrainerServices() or 0) do
        local sname, rank, stype = GetTrainerServiceInfo(i)
        if sname and stype ~= "header" then
            local cost = GetTrainerServiceCost and GetTrainerServiceCost(i) or 0
            local lvl = GetTrainerServiceLevelReq and GetTrainerServiceLevelReq(i) or 0
            local skill, sreq
            if GetTrainerServiceSkillReq then skill, sreq = GetTrainerServiceSkillReq(i) end
            tinsert(rec.svc, table.concat({
                BS.Strip(sname) or "?", rank or "", cost, lvl,
                skill or "", sreq or "",
            }, "\t"))
        end
    end
    BS.Bucket("trainers")[BS.NpcId()] = rec
end

function BS.ev.LOOT_OPENED()
    local src = "unknown"
    if UnitExists("target") and UnitIsDead("target") and not UnitIsPlayer("target") then
        local kind, id = BS.GuidId(UnitGUID("target"))
        if id then src = tostring(id) end
    end
    local bucket = BS.Bucket("loot")
    local t = bucket[src]
    if not t then
        t = { opens = 0, drops = {} }
        bucket[src] = t
    end
    t.opens = t.opens + 1
    t.name = UnitName("target") or t.name
    for i = 1, GetNumLootItems() do
        local link = GetLootSlotLink(i)
        if link then
            BS.Item(link, "loot")
            local id = tonumber(strmatch(link, "item:(%d+)"))
            local _, _, qty = GetLootSlotInfo(i)
            if id then
                local d = t.drops[id]
                if not d then d = { n = 0, min = qty, max = qty }; t.drops[id] = d end
                d.n = d.n + 1
                if qty and qty < (d.min or qty) then d.min = qty end
                if qty and qty > (d.max or qty) then d.max = qty end
            end
        end
    end
end

function BS.ev.TRADE_SKILL_SHOW()
    local prof = GetTradeSkillLine()
    if not prof then return end
    local rec = { recipes = {} }
    for i = 1, (GetNumTradeSkills() or 0) do
        local sname, stype = GetTradeSkillInfo(i)
        if sname and stype ~= "header" then
            local link = GetTradeSkillItemLink(i)
            BS.Item(link, "craft")
            local r = {
                name = sname,
                item = strmatch(link or "", "item:[%-%d:]+"),
                reagents = {},
            }
            for j = 1, (GetTradeSkillNumReagents(i) or 0) do
                local rl = GetTradeSkillReagentItemLink(i, j)
                local _, _, need = GetTradeSkillReagentInfo(i, j)
                BS.Item(rl, "reagent")
                tinsert(r.reagents, (strmatch(rl or "", "item:[%-%d:]+") or "?") .. "x" .. (need or 1))
            end
            tinsert(rec.recipes, r)
        end
    end
    BS.Bucket("craft")[prof] = rec
end

-- The server's own addon channel. On a custom server this is the protocol
-- behind every bespoke UI panel, so log prefixes and payload samples verbatim.
function BS.ev.CHAT_MSG_ADDON(prefix, msg, chan)
    if not prefix then return end
    local bucket = BS.Bucket("addon")
    local t = bucket[prefix]
    if not t then
        t = { n = 0, chan = {}, samples = {} }
        bucket[prefix] = t
    end
    t.n = t.n + 1
    BS.Bump(t.chan, chan)
    BS.Push(t.samples, tostring(msg), 200)
end

function BS.Chat(e, msg)
    if type(msg) ~= "string" then return end
    local bucket = BS.Bucket("chat")
    local t = bucket[e]
    if not t then t = {}; bucket[e] = t end
    BS.Push(t, BS.Strip(msg) or msg, 300)
    -- Server feature announcements are usually item-linked; harvest the links.
    for link in string.gmatch(msg, "|H(item:[%-%d:]+)|h") do
        BS.Item(link, "chat")
    end
    for sid in string.gmatch(msg, "|Hspell:(%d+)") do
        BS.Spell(sid, nil, "chat")
    end
end

function BS.ev.COMBAT_LOG_EVENT_UNFILTERED(_, e, _, _, _, _, _, _, a1, a2)
    if type(e) ~= "string" then return end
    if strsub(e, 1, 6) == "SPELL_" or strsub(e, 1, 6) == "RANGE_" then
        if tonumber(a1) then BS.Spell(a1, a2, "combatlog") end
    end
end

function BS.ev.AUCTION_ITEM_LIST_UPDATE()
    for i = 1, (GetNumAuctionItems("list") or 0) do
        BS.Item(GetAuctionItemLink("list", i), "auction")
    end
end

function BS.ev.MAIL_INBOX_UPDATE()
    for i = 1, (GetInboxNumItems() or 0) do
        for j = 1, ATTACHMENTS_MAX_RECEIVE do
            BS.Item(GetInboxItemLink(i, j), "mail")
        end
    end
end

-- ------------------------------------------------------------------ scans --

function BS.ScanBags()
    for bag = 0, NUM_BAG_SLOTS do
        for slot = 1, (GetContainerNumSlots(bag) or 0) do
            BS.Item(GetContainerItemLink(bag, slot), "bag")
        end
    end
end

function BS.ScanEquipped()
    for i = 1, 19 do
        BS.Item(GetInventoryItemLink("player", i), "equip")
    end
end

function BS.ScanBank()
    for slot = 1, 28 do
        BS.Item(GetContainerItemLink(BANK_CONTAINER, slot), "bank")
    end
    for bag = NUM_BAG_SLOTS + 1, NUM_BAG_SLOTS + NUM_BANKBAGSLOTS do
        for slot = 1, (GetContainerNumSlots(bag) or 0) do
            BS.Item(GetContainerItemLink(bag, slot), "bank")
        end
    end
end

function BS.ScanSpellbook()
    local total = 0
    for tab = 1, GetNumSpellTabs() do
        local _, _, offset, count = GetSpellTabInfo(tab)
        if offset and count then total = offset + count end
    end
    local bucket = BS.Bucket("book")
    for i = 1, total do
        local sname, rank = GetSpellName(i, BOOKTYPE_SPELL)
        if sname then
            tip:SetOwner(UIParent, "ANCHOR_NONE")
            tip:ClearLines()
            pcall(tip.SetSpell, tip, i, BOOKTYPE_SPELL)
            local lines = BS.TipLines()
            tip:Hide()
            bucket[sname .. "\t" .. (rank or "")] = lines or { "?" }
        end
    end
end

function BS.ScanQuestLog()
    local bucket = BS.Bucket("questlog")
    for i = 1, GetNumQuestLogEntries() do
        local title, lvl, tag, header, _, _, daily = GetQuestLogTitle(i)
        if title and not header then
            local link = GetQuestLink and GetQuestLink(i)
            local qid = link and tonumber(strmatch(link, "quest:(%d+)")) or nil
            SelectQuestLogEntry(i)
            local desc, obj = GetQuestLogQuestText()
            bucket[qid or title] = {
                title = title, lvl = lvl, tag = tag, daily = daily,
                desc = BS.Strip(desc), obj = BS.Strip(obj),
            }
        end
    end
end

function BS.ScanCurrency()
    if not GetCurrencyListSize then return end
    local bucket = BS.Bucket("currency")
    for i = 1, GetCurrencyListSize() do
        local cname, isHeader, _, _, _, count = GetCurrencyListInfo(i)
        if cname and not isHeader then bucket[cname] = count end
    end
end

function BS.ScanTalents()
    local bucket = BS.Bucket("talents")
    for tab = 1, GetNumTalentTabs() do
        local tabName = GetTalentTabInfo(tab)
        local rows = {}
        for i = 1, GetNumTalents(tab) do
            local tname, _, row, col, cur, max = GetTalentInfo(tab, i)
            if tname then
                tinsert(rows, table.concat({ tname, row, col, cur, max }, "\t"))
            end
        end
        bucket[tostring(tabName or tab)] = rows
    end
end

-- --------------------------------------------------------------- plumbing --

function BS.Init()
    local realm = GetRealmName() or "Unknown"
    if not BoneScrapeDB[realm] then BoneScrapeDB[realm] = {} end
    DB = BoneScrapeDB[realm]
    DB.rev = REV
    DB.realm = realm
    DB.build = table.concat({ GetBuildInfo() }, " ")
    DB.chars = DB.chars or {}
    local me = UnitName("player")
    if me then
        DB.chars[me] = (UnitLevel("player") or 0) .. " " ..
            (UnitClass("player") or "?") .. " " .. (UnitRace("player") or "?")
    end
end

f:SetScript("OnEvent", function()
    local e = event
    if e == "ADDON_LOADED" then
        if arg1 == "BoneScrape" then BS.Init() end
        return
    end
    if not DB then return end

    if e == "PLAYER_LOGIN" then
        BS.Init()
        BS.ScanEquipped(); BS.ScanBags(); BS.ScanCurrency()
        DEFAULT_CHAT_FRAME:AddMessage("|cff33ff99BoneScrape|r recording. /bs for status.")
        return
    end
    if e == "PLAYER_TARGET_CHANGED" then BS.Unit("target", "target"); return end
    if e == "UPDATE_MOUSEOVER_UNIT" then BS.Unit("mouseover", "mouseover"); return end
    if e == "PLAYER_ENTERING_WORLD" or e == "BAG_UPDATE" then BS.ScanBags(); return end
    if e == "PLAYER_EQUIPMENT_CHANGED" then BS.ScanEquipped(); return end
    if e == "BANKFRAME_OPENED" then BS.ScanBank(); return end
    if e == "SPELLS_CHANGED" or e == "LEARNED_SPELL_IN_TAB" then BS.ScanSpellbook(); return end
    if e == "QUEST_LOG_UPDATE" then BS.ScanQuestLog(); return end
    if e == "CURRENCY_DISPLAY_UPDATE" then BS.ScanCurrency(); return end
    if e == "PLAYER_TALENT_UPDATE" or e == "CHARACTER_POINTS_CHANGED" then BS.ScanTalents(); return end

    if strsub(e, 1, 9) == "CHAT_MSG_" and e ~= "CHAT_MSG_ADDON" then
        BS.Chat(e, arg1)
        return
    end

    local h = BS.ev[e]
    if h then h(arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10, arg11) end
end)

for _, e in ipairs({
    "ADDON_LOADED", "PLAYER_LOGIN", "PLAYER_ENTERING_WORLD",
    "PLAYER_TARGET_CHANGED", "UPDATE_MOUSEOVER_UNIT",
    "BAG_UPDATE", "PLAYER_EQUIPMENT_CHANGED", "BANKFRAME_OPENED",
    "MERCHANT_SHOW", "GOSSIP_SHOW", "TRAINER_SHOW", "LOOT_OPENED",
    "QUEST_DETAIL", "QUEST_PROGRESS", "QUEST_COMPLETE", "QUEST_LOG_UPDATE",
    "TRADE_SKILL_SHOW", "AUCTION_ITEM_LIST_UPDATE", "MAIL_INBOX_UPDATE",
    "SPELLS_CHANGED", "LEARNED_SPELL_IN_TAB", "CURRENCY_DISPLAY_UPDATE",
    "PLAYER_TALENT_UPDATE", "CHARACTER_POINTS_CHANGED",
    "COMBAT_LOG_EVENT_UNFILTERED", "CHAT_MSG_ADDON",
    "CHAT_MSG_SYSTEM", "CHAT_MSG_MONSTER_SAY", "CHAT_MSG_MONSTER_YELL",
    "CHAT_MSG_MONSTER_WHISPER", "CHAT_MSG_MONSTER_EMOTE",
    "CHAT_MSG_RAID_BOSS_EMOTE", "CHAT_MSG_RAID_BOSS_WHISPER",
    "CHAT_MSG_LOOT", "CHAT_MSG_CURRENCY", "CHAT_MSG_SKILL",
}) do
    pcall(f.RegisterEvent, f, e)
end

-- Item info arrives asynchronously, so drain the pending queue on a ticker.
-- A bank full of uncached custom items would otherwise spike frame time.
local elapsed = 0
f:SetScript("OnUpdate", function()
    elapsed = elapsed + arg1
    if elapsed < 0.5 then return end
    elapsed = 0
    if NPEND == 0 or not DB then return end
    local done, budget = {}, 15
    for istr in pairs(PEND) do
        if budget <= 0 then break end
        budget = budget - 1
        if BS.Resolve(istr) then tinsert(done, istr) end
    end
    for i = 1, table.getn(done) do
        PEND[done[i]] = nil
        NPEND = NPEND - 1
    end
    if NPEND < 0 then NPEND = 0 end
end)

function BS.Count(t)
    local n = 0
    if type(t) == "table" then for _ in pairs(t) do n = n + 1 end end
    return n
end

SLASH_BONESCRAPE1 = "/bs"
SLASH_BONESCRAPE2 = "/bonescrape"
SlashCmdList["BONESCRAPE"] = function(msg)
    msg = strlower(msg or "")
    local p = DEFAULT_CHAT_FRAME
    if msg == "wipe" then
        BoneScrapeDB[GetRealmName() or "Unknown"] = nil
        BS.Init()
        p:AddMessage("|cff33ff99BoneScrape|r wiped this realm.")
        return
    end
    if msg == "scan" then
        BS.ScanEquipped(); BS.ScanBags(); BS.ScanSpellbook()
        BS.ScanQuestLog(); BS.ScanCurrency(); BS.ScanTalents()
        p:AddMessage("|cff33ff99BoneScrape|r full rescan queued.")
        return
    end
    p:AddMessage("|cff33ff99BoneScrape|r " .. (DB and DB.realm or "?"))
    for _, k in ipairs({ "items", "spells", "npcs", "vendors", "gossip", "quests",
                         "questlog", "trainers", "loot", "craft", "addon", "chat", "book" }) do
        local n = BS.Count(DB and DB[k])
        if n > 0 then p:AddMessage("  " .. k .. ": " .. n) end
    end
    p:AddMessage("  pending item lookups: " .. NPEND)
    p:AddMessage("  /bs scan = force full rescan, /bs wipe = clear this realm")
end
