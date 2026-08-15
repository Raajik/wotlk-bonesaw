-- Living Gear window. Loaded from patch-enUS-4.MPQ via FrameXML.
-- Server pushes numbers over addon whispers (prefix LG). ASCII-only strings.

local PREFIX = "LG"
local WHITE = "Interface\\Buttons\\WHITE8X8"
local WINDBLOWN_ID = 910001

local db = {
    items = {},
    byKey = {},
    asked = {},
    absorb = { str = 0, agi = 0, sta = 0, intel = 0, spi = 0, armor = 0, count = 0 },
}

local syncing = false

local ui = {}

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
    return string.format("+%.0f str  +%.0f agi  +%.0f sta  +%.0f int  +%.0f spi  +%.0f armor",
        tonumber(s) or 0, tonumber(a) or 0, tonumber(t) or 0,
        tonumber(i) or 0, tonumber(p) or 0, tonumber(ar) or 0)
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

local function RequestSync()
    local name = UnitName("player")
    if name then
        SendAddonMessage(PREFIX, "REQ", "WHISPER", name)
    end
end

local function SendAttune(slot)
    local name = UnitName("player")
    if name and slot then
        SendAddonMessage(PREFIX, "ATTUNE|" .. tostring(slot), "WHISPER", name)
    end
end

local function LayoutRows()
    for i = 1, 16 do
        local row = ui.rows[i]
        local it = db.items[i]
        if it then
            row:Show()
            row.name:SetText(it.name or "?")
            row.meta:SetText(string.format("Lv %s   XP %s/%s", it.lv, it.xp, it.need))
            row.stats:SetText(StatLine(it.ds, it.da, it.dt, it.di, it.dp, it.dar))
            row.slot = it.slot
            row.armed = false
            row.attune.label:SetText("Attune")
            StyleBtn(row.attune, 0.28, 0.10, 0.10)
        else
            row:Hide()
            row.slot = nil
            row.armed = false
        end
    end

    local n = #db.items
    if n == 0 then
        ui.empty:Show()
        ui.empty:SetText("No living gear equipped.")
    else
        ui.empty:Hide()
    end

    local ab = db.absorb
    ui.absorb:SetText(string.format("Absorb (%s attuned):  %s",
        ab.count or 0, StatLine(ab.str, ab.agi, ab.sta, ab.intel, ab.spi, ab.armor)))

    local h = 92 + (n * 44)
    if n == 0 then
        h = 120
    end
    ui.frame:SetHeight(h)
end

local function BuildUI()
    if ui.frame then
        return
    end

    local f = CreateFrame("Frame", "LivingGearFrame", UIParent)
    f:SetSize(440, 160)
    f:SetPoint("CENTER", UIParent, "CENTER", 220, 80)
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

    local title = Font(f, 13, 0.4, 0.8, 1)
    title:SetPoint("TOPLEFT", 10, -8)
    title:SetText("Living Gear")

    local hint = Font(f, 10, 0.55, 0.55, 0.55)
    hint:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 0, -2)
    hint:SetText("Sheet is power. Attune destroys the item and keeps 10% on the account.")

    local close = CreateFrame("Button", nil, f)
    close:SetSize(22, 18)
    close:SetPoint("TOPRIGHT", -8, -8)
    StyleBtn(close, 0.14, 0.14, 0.14)
    close.label = Font(close, 12, 0.9, 0.9, 0.9)
    close.label:SetPoint("CENTER", 0, 0)
    close.label:SetJustifyH("CENTER")
    close.label:SetText("X")
    close:SetScript("OnClick", function()
        f:Hide()
    end)

    ui.absorb = Font(f, 11, 0.85, 0.75, 0.45)
    ui.absorb:SetPoint("TOPLEFT", 10, -40)
    ui.absorb:SetPoint("RIGHT", -10, 0)

    ui.empty = Font(f, 11, 0.6, 0.6, 0.6)
    ui.empty:SetPoint("TOPLEFT", 10, -64)

    ui.rows = {}
    for i = 1, 16 do
        local row = CreateFrame("Frame", nil, f)
        row:SetSize(420, 42)
        row:SetPoint("TOPLEFT", 10, -62 - (i - 1) * 44)
        row.name = Font(row, 12, 0.92, 0.92, 0.92)
        row.name:SetPoint("TOPLEFT", 0, 0)
        row.meta = Font(row, 11, 0.55, 0.8, 1)
        row.meta:SetPoint("TOPRIGHT", -72, 0)
        row.stats = Font(row, 10, 0.7, 0.7, 0.7)
        row.stats:SetPoint("TOPLEFT", row.name, "BOTTOMLEFT", 0, -2)
        row.attune = CreateFrame("Button", nil, row)
        row.attune:SetSize(64, 18)
        row.attune:SetPoint("RIGHT", 0, -6)
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
        row:Hide()
        ui.rows[i] = row
    end
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

local function HandleAddon(prefix, message)
    if prefix ~= PREFIX or not message then
        return
    end
    if message == "OPEN" then
        if ui.frame and ui.frame:IsShown() then
            RequestSync()
        end
        return
    end
    if message == "CLR" then
        syncing = true
        db.items = {}
        db.byKey = {}
        db.asked = {}
        return
    end
    if message == "END" then
        syncing = false
        BuildUI()
        LayoutRows()
        return
    end
    local p = SplitPipe(message)
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
    end
end

local function TipStatLine(it)
    local line = StatLine(it.ds, it.da, it.dt, it.di, it.dp, it.dar)
    if line == "+0 str  +0 agi  +0 sta  +0 int  +0 spi  +0 armor" then
        return nil
    end
    return line
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

local function AppendLiving(tip)
    local key = tip and tip._lgKey
    if not key then
        return
    end
    local it = db.byKey[key]
    if not it then
        RequestTip(key)
        return
    end
    local xp = "MAX"
    if tonumber(it.need) and tonumber(it.need) > 0 then
        xp = string.format("XP %s/%s", it.xp or "0", it.need)
    end
    tip:AddLine(" ")
    tip:AddLine(string.format("|cff66ccffLiving Gear|r  Lv %s  %s", it.lv or "1", xp), 1, 1, 1)
    local extras = TipStatLine(it)
    if extras then
        tip:AddLine(extras, 0.7, 0.7, 0.7)
    end
    tip:Show()
end

local function HookTooltip(tip)
    if not tip or tip._lgHooked then
        return
    end
    tip._lgHooked = true

    local origInv = tip.SetInventoryItem
    tip.SetInventoryItem = function(self, unit, slot, ...)
        if unit == "player" and slot then
            self._lgKey = "inv:" .. tostring(tonumber(slot) - 1)
        else
            self._lgKey = nil
        end
        return origInv(self, unit, slot, ...)
    end

    local origBag = tip.SetBagItem
    tip.SetBagItem = function(self, bag, slot, ...)
        if bag ~= nil and slot ~= nil then
            self._lgKey = "bag:" .. tostring(bag) .. ":" .. tostring(slot)
        else
            self._lgKey = nil
        end
        return origBag(self, bag, slot, ...)
    end

    tip:HookScript("OnTooltipSetItem", function(self)
        AppendLiving(self)
    end)
    tip:HookScript("OnHide", function(self)
        self._lgKey = nil
    end)
end

local ev = CreateFrame("Frame")
ev:RegisterEvent("PLAYER_LOGIN")
ev:RegisterEvent("PLAYER_ENTERING_WORLD")
ev:RegisterEvent("CHAT_MSG_ADDON")
ev:RegisterEvent("UNIT_SPELLCAST_SUCCEEDED")
ev:SetScript("OnEvent", function(_, event, a1, a2)
    if event == "PLAYER_LOGIN" then
        BuildUI()
        HookTooltip(GameTooltip)
        HookTooltip(ShoppingTooltip1)
        HookTooltip(ShoppingTooltip2)
    elseif event == "PLAYER_ENTERING_WORLD" then
        RequestSync()
    elseif event == "CHAT_MSG_ADDON" then
        HandleAddon(a1, a2)
    elseif event == "UNIT_SPELLCAST_SUCCEEDED" then
        if a1 == "player" and a2 == (GetSpellInfo(WINDBLOWN_ID) or "*Windblown") then
            Toggle()
        end
    end
end)

SLASH_LIVINGGEAR1 = "/lg"
SLASH_LIVINGGEAR2 = "/livinggear"
SlashCmdList["LIVINGGEAR"] = function()
    Toggle()
end
