"""Pre-ship smoke test for the LivingGear client Lua.

Loads the full addon in a stub WoW 3.3.5 environment (via lupa), then drives
the report form (open/toggle/send) and the craft-refusal diag handler, and
logs every unknown global the file touches.

Catches the two shipped-crash classes:
  - renamed locals still referenced by their old global name (REPORT_KINDS)
  - API calls with wrong arity (strict stubs error like the real C APIs)

Run: python tools/_lg_lua_smoke.py
Exit output is a report string; any STEP FAIL line is a real finding.

Needs the Lua 5.1 flavor (same as the WoW client): pip install "lupa[lua51]"
"""
import lupa

SRC = r"A:/wow-bonesaw/modules/mod-living-gear/client_addon/LivingGear/LivingGear.lua"

lua = lupa.lua51.LuaRuntime()
lua.globals()["LG_SRC"] = open(SRC, encoding="utf-8", errors="replace").read()

DRIVER = r'''
local ok, report = xpcall(function()
  local L = {}
  local function log(s) L[#L+1] = tostring(s) end

  local allframes = {}
  local touched, stubcache = {}, {}

  -- Driver-controllable sim time and bag counts: the staging test advances
  -- both to walk the craft path through its states.
  local rawbags, simNow = 0, 0
  local origCasts = { ts = 0, dc = 0 }

  local function widget(name)
    local w = { _name = name or "anon", _scripts = {}, _events = {},
                _shown = false, _text = "Smoke test report text." }
    local special = {
      CreateFrame = function(self, ctype, cname) return widget(cname) end,
      CreateFontString = function(self) return widget() end,
      CreateTexture = function(self) return widget() end,
      CreateAnimationGroup = function(self) return widget() end,
      SetScript = function(self, k, fn) w._scripts[k] = fn end,
      HookScript = function(self, k, fn) w._scripts[k] = fn end,
      GetScript = function(self, k) return w._scripts[k] end,
      RegisterEvent = function(self, e) w._events[e] = true end,
      UnregisterEvent = function(self, e) w._events[e] = nil end,
      RegisterForDrag = function() end, RegisterForClicks = function() end,
      StartMoving = function() end, StopMovingOrSizing = function() end,
      SetBackdrop = function() end, SetBackdropColor = function() end,
      SetBackdropBorderColor = function() end, SetPoint = function() end,
      SetAllPoints = function() end, SetSize = function() end,
      SetWidth = function() end, SetHeight = function() end,
      SetFrameStrata = function() end, SetFrameLevel = function() end,
      SetToplevel = function() end, SetMovable = function() end,
      EnableMouse = function() end, EnableMouseWheel = function() end,
      SetText = function(self, v) w._text = v end,
      GetText = function(self) return w._text or "" end,
      SetChecked = function(self, v) w._checked = (v == true) end,
      GetChecked = function(self) return w._checked or false end,
      Show = function() w._shown = true end, Hide = function() w._shown = false end,
      IsShown = function() return w._shown end,
      IsVisible = function() return w._shown and 1 or 0 end,
      SetNormalTexture = function() end, SetPushedTexture = function() end,
      SetHighlightTexture = function() end, SetCheckedTexture = function() end,
      SetDisabledTexture = function() end, SetFont = function() end,
      SetFontObject = function() end, SetTextColor = function() end,
      SetJustifyH = function() end, SetJustifyV = function() end,
      SetMultiLine = function() end, SetAutoFocus = function() end,
      SetMaxBytes = function() end, SetMaxLetters = function() end,
      SetNumber = function() end, GetNumber = function() return 0 end,
      Insert = function(self, s) w._text = (w._text or "") .. tostring(s) end,
      IsFocused = function() return w._focused or false end,
      ClearFocus = function() end, SetFocus = function() end,
      SetParent = function() end, SetOwner = function() end,
      AddMessage = function(self, s) log("CHAT: " .. tostring(s)) end,
      GetName = function() return w._name end,
      GetNumPoints = function() return 0 end,
      SetID = function() end, SetVertexColor = function() end,
      SetTexture = function() end, SetAlpha = function() end,
      SetDrawLayer = function() end, SetBlendMode = function() end,
      ClearAllPoints = function() end, SetClampedToScreen = function() end,
      SetUserPlaced = function() end, SetHyperlink = function() end,
      GetItem = function() return nil end,
      GetLeft = function() return 0 end, GetRight = function() return 0 end,
      GetTop = function() return 0 end, GetBottom = function() return 0 end,
      GetWidth = function() return 100 end, GetHeight = function() return 20 end,
      GetCenter = function() return 512, 384 end,
      GetScale = function() return 1 end, SetScale = function() end,
      GetEffectiveScale = function() return 1 end,
      IsObjectType = function() return true end,
      GetObjectType = function() return "Frame" end,
      Enable = function() end, Disable = function() end,
      LockHighlight = function() end, UnlockHighlight = function() end,
      SetHighlightTextColor = function() end,
      GetFont = function() return "Fonts\\FRIZQT__.TTF", 12, "" end,
    }
    -- Unknown FIELD reads must be nil (real WoW), or every lazy-init
    -- `if not frame.field then frame.field = ... end` silently skips and
    -- later code indexes a truthy function. Unknown METHODS will error
    -- "attempt to call nil" -- add them to `special` as they surface.
    setmetatable(w, { __index = function(_, k) return special[k] end })
    table.insert(allframes, w)
    return w
  end

  local function autostub(name)
    local v = stubcache[name]
    if v then return v end
    touched[#touched+1] = name
    v = setmetatable({}, {
      __call = function() return nil end,
      __index = function() return nil end,
      __newindex = function() end,
      __len = function() return 0 end,
      __tostring = function() return "STUB:" .. name end,
    })
    stubcache[name] = v
    return v
  end

  local E  -- forward: CreateFrame writes named frames into E

  -- Strict reagent APIs: the real client C APIs error on wrong arity, so the
  -- stubs must too, or the "renamed local / wrong arg" bug class sails through.
  local function twoargs(usage) return function(i, r)
    if type(i) ~= "number" or type(r) ~= "number" then error(usage) end
  end end

  E = setmetatable({
    assert = assert, error = error, ipairs = ipairs, next = next,
    pairs = pairs, pcall = pcall, xpcall = xpcall, select = select,
    tonumber = tonumber, tostring = tostring, type = type,
    unpack = unpack, rawget = rawget, rawset = rawset, rawequal = rawequal,
    setmetatable = setmetatable, getmetatable = getmetatable, debug = debug,
    string = string, table = table, math = math, coroutine = coroutine,
    os = { time = function() return 1779000000 end,
           date = function() return "Thu Aug 27 00:00:00 2026" end,
           clock = os.clock, getenv = function() return nil end },
    print = function(...)
      local t = {}
      for i = 1, select("#", ...) do t[i] = tostring(select(i, ...)) end
      log("PRINT: " .. table.concat(t, "\t"))
    end,

    CreateFrame = function(ctype, cname)
      local w = widget(cname)
      if cname and cname ~= "" then E[cname] = w end
      return w
    end,
    UIParent = widget("UIParent"), WorldFrame = widget("WorldFrame"),
    GameTooltip = widget("GameTooltip"), ItemRefTooltip = widget("ItemRefTooltip"),
    ShoppingTooltip1 = widget("st1"), ShoppingTooltip2 = widget("st2"),
    ChatFrame1 = widget("ChatFrame1"), Minimap = widget("Minimap"),
    WatchFrame = widget("WatchFrame"), QuestLogFrame = widget("QuestLogFrame"),
    AchievementFrame = widget("AchievementFrame"),
    CharacterFrame = widget("CharacterFrame"),
    DEFAULT_CHAT_FRAME = widget("DEFAULT_CHAT_FRAME"),
    UISpecialFrames = {}, SlashCmdList = {},
    -- double-load guards: first load in the real client sees nil/absent; the
    -- values below keep the "proceed" branch taken in the stub env
    LivingGear_Rev = 0, LivingGear_Loaded = false,
    hooksecurefunc = function() end, issecurevariable = function() return true end,
    SendAddonMessage = function(_, msg) log("SEND: " .. tostring(msg)) end,
    SendChatMessage = function() end,
    GetItemCount = function() return rawbags end,
    GetItemInfo = function() return nil end, GetItemIcon = function() return nil end,
    -- Spies, not stubs: the staging test must see whether the real DoCraft /
    -- DoTradeSkill underneath the hook actually ran.
    DoTradeSkill = function() origCasts.ts = origCasts.ts + 1 end,
    DoCraft = function() origCasts.dc = origCasts.dc + 1 end,
    UnitName = function() return "Smokeplayer" end,
    UnitExists = function() return false end, UnitLevel = function() return 80 end,
    UnitHealth = function() return 100 end, UnitHealthMax = function() return 100 end,
    UnitPower = function() return 100 end, UnitPowerMax = function() return 100 end,
    UnitClass = function() return "Warrior", "WARRIOR" end,
    UnitIsPlayer = function() return true end,
    UnitGUID = function() return "0x0000000000000000" end,
    UnitIsDead = function() return false end,
    UnitAffectingCombat = function() return false end,
    GetTime = function() return simNow end, GetMoney = function() return 0 end,
    GetZoneText = function() return "Stormwind" end,
    GetSubZoneText = function() return "Old Town" end,
    IsInInstance = function() return false, nil end,
    IsMounted = function() return false end,
    IsShiftKeyDown = function() return false end,
    IsAltKeyDown = function() return false end,
    IsControlKeyDown = function() return false end,
    InCombatLockdown = function() return false end,
    GetScreenWidth = function() return 1024 end,
    GetScreenHeight = function() return 768 end,
    GetCursorPosition = function() return 0, 0 end,
    GetSpellInfo = function() return nil end,
    GetSpellCooldown = function() return 0, 0, 0 end,
    IsSpellKnown = function() return false end,
    GetGameMessageText = function() return nil end,
    GetTradeSkillInfo = function(i)
      if type(i) ~= "number" then error("Usage: GetTradeSkillInfo(index)") end
      return "Bolt of Linen Cloth", "exclusive", 0, false, nil, 0
    end,
    GetTradeSkillNumReagents = function(i)
      if type(i) ~= "number" then error("Usage: GetTradeSkillNumReagents(index)") end
      return 2
    end,
    GetTradeSkillReagentInfo = function(i, r)
      if type(i) ~= "number" or type(r) ~= "number" then
        error("Usage: GetTradeSkillReagentInfo(index, reagentIndex)")
      end
      return "Linen Cloth", "", 2, 5
    end,
    GetTradeSkillReagentItemLink = function(i, r)
      if type(i) ~= "number" or type(r) ~= "number" then
        error("Usage: GetTradeSkillReagentItemLink(index, reagentIndex)")
      end
      return "|Hitem:2589:0:0:0:0:0:0|h[Linen Cloth]|h"
    end,
    GetCraftInfo = function() return "Rough Copper", "", "crafted", 0, false, 0 end,
    GetCraftNumReagents = function() return 1 end,
    GetCraftReagentInfo = function(i, r)
      if type(i) ~= "number" or type(r) ~= "number" then
        error("Usage: GetCraftReagentInfo(index, reagentIndex)")
      end
      return "Copper Bar", "", 1, 4
    end,
    GetCraftReagentItemLink = function(i, r)
      if type(i) ~= "number" or type(r) ~= "number" then
        error("Usage: GetCraftReagentItemLink(index, reagentIndex)")
      end
      return "|Hitem:2840:0:0:0:0:0:0|h[Copper Bar]|h"
    end,
    SPELL_FAILED_REAGENTS = "You lack the required reagents",
    ERR_SPELL_FAILED_REAGENTS_GENERIC = "Missing Reagent",
    StaticPopup_Show = function() return nil end,
    StaticPopup_Hide = function() end, PlaySound = function() end,
    GetLocale = function() return "enUS" end,
    GetBuildInfo = function() return "3.3.5", "3.3.5a", 12345, "WotLK" end,
    GetCVar = function() return "0" end, SetCVar = function() end,
    GetAddOnMetadata = function() return nil end,
    GetNumAddOns = function() return 0 end,
    GetAddOnInfo = function() return nil end,
    IsAddOnLoaded = function() return false end,
    GetFramerate = function() return 60 end,
    GetNetStats = function() return 0, 0, 0 end,
    GetNumBankSlots = function() return 0, 1 end,
    GetContainerNumSlots = function() return 0 end,
    GetContainerItemLink = function() return nil end,
    GetContainerItemInfo = function() return nil end,
    GetContainerItemID = function() return nil end,
    GetInventoryItemLink = function() return nil end,
    GetInventoryItemID = function() return nil end,
    GetInventoryItemCount = function() return 0 end,
    GetInventorySlotInfo = function() return nil end,
    GetBindingKey = function() return nil end,
    GetItemFamily = function() return 0 end,
    GetNumFactions = function() return 0 end,
    GetNumQuestLogEntries = function() return 0 end,
    GetQuestLogTitle = function() return nil end,
    GetNumCompanions = function() return 0 end,
    GetComboPoints = function() return 0 end,
    GetShapeshiftForm = function() return 0 end,
    GetNumTalentTabs = function() return 0 end,
    GetActiveTalentGroup = function() return 1 end,
    IsFalling = function() return false end, IsFlying = function() return false end,
    IsSwimming = function() return false end,
    IsModifiedClick = function() return false end,
    HandleModifiedItemClick = function() return false end,
    ChatEdit_InsertLink = function() return false end,
    ContainerFrame_Update = function() end,
    PanelTemplates_SetTab = function() end,
    PanelTemplates_SetNumTabs = function() end,
    PanelTemplates_TabResize = function() end,
    QuestLog_Update = function() end, QuestWatch_Update = function() end,
    PaperDollItemSlotButton_Update = function() end,
    strsplit = function(d, s)
      local out, pat = {}, "[^" .. tostring(d) .. "]+"
      for m in string.gmatch(s or "", pat) do out[#out+1] = m end
      if #out == 0 then return "" end
      return unpack(out)
    end,
    strjoin = function(d, ...) return table.concat({ ... }, d) end,
    C_Timer = { NewTimer = function() end, NewTicker = function() end, After = function() end },
    WATCHFRAME_QUESTLINES = {},
    ITEM_QUALITY_COLORS = setmetatable({}, { __index = function() return { r = 1, g = 1, b = 1, hex = "ffffffff" } end }),
    BAG_ITEM_QUALITY_COLORS = setmetatable({}, { __index = function() return { r = 1, g = 1, b = 1, hex = "ffffffff" } end }),
  }, {
    __index = function(_, k) return autostub(k) end,
    __newindex = function(t, k, v) rawset(t, k, v) end,
  })

  -- 1. load the whole addon (Lua 5.1: setfenv, load has no env arg)
  local chunk, cerr = loadstring(LG_SRC, "LivingGear")
  if not chunk then return "LOAD ERROR:\n" .. tostring(cerr) end
  setfenv(chunk, E)
  local cok, cerr2 = xpcall(chunk, debug.traceback)
  if not cok then return "RUNTIME ERROR AT LOAD:\n" .. tostring(cerr2) end
  local nload = #allframes
  log("== load OK (" .. nload .. " frames created at load) ==")

  local function step(name, fn)
    local sok, err = xpcall(fn, debug.traceback)
    if sok then log("STEP OK:   " .. name)
    else log("STEP FAIL: " .. name .. " :: " .. tostring(err)) end
  end

  -- 2. report form lifecycle (the 0.1.112 crash paths)
  step("slash /report opens (SetKind 1)", function() E.SlashCmdList.LGREPORT("") end)
  step("slash /bugreport bare (toggle hide)", function() E.SlashCmdList.LGBUG("") end)
  step("slash /featurerequest bare (SetKind 2)", function() E.SlashCmdList.LGFEATURE("") end)
  step("slash /crit bare (toggle hide)", function() E.SlashCmdList.LGCRIT("") end)
  step("slash /report reopen", function() E.SlashCmdList.LGREPORT("") end)
  step("report Send via editbox OnEnterPressed", function()
    E.IsControlKeyDown = function() return true end
    for i = nload + 1, #allframes do
      local f = allframes[i]
      if f._scripts.OnEnterPressed then return f._scripts.OnEnterPressed(f) end
    end
    error("no post-load editbox with OnEnterPressed found")
  end)

  -- 3. craft-refusal diag handler (the report #192 follow-up path)
  step("DoTradeSkill captures the crafted recipe's diag", function()
    E.DoTradeSkill(1)
  end)
  step("hooked GetTradeSkillInfo stores diag", function()
    local name, typ, numAvail = E.GetTradeSkillInfo(1)
    log("  hooked info: name=" .. tostring(name) .. " avail=" .. tostring(numAvail))
  end)
  step("UI_ERROR_MESSAGE fires craft diag", function()
    local fired = false
    for i = 1, nload do
      local f = allframes[i]
      if f._events.UI_ERROR_MESSAGE and f._scripts.OnEvent then
        f._scripts.OnEvent(f, "UI_ERROR_MESSAGE", 1, "You lack the required reagents")
        fired = true
        break
      end
    end
    if not fired then error("no UI_ERROR_MESSAGE frame found") end
    for _, m in ipairs(L) do
      if string.find(m, "LG diag", 1, true) then return end
    end
    error("handler ran but no [LG diag] line was printed")
  end)

  -- 4. craft staging from an empty-bag vault (reports #195/#198): the click
  -- must hold the craft, send an exact TAKE, and fire the cast only once the
  -- withdrawal lands in the bags. The vault is fed through the real addon
  -- sync handler (VLT| lines), and the casts are watched via the DoTradeSkill
  -- / DoCraft spies in the env, because LG2 is a local inside the addon.
  local function feedAddon(msg)
    for i = 1, #allframes do
      local f = allframes[i]
      if f._events.CHAT_MSG_ADDON and f._scripts.OnEvent then
        f._scripts.OnEvent(f, "CHAT_MSG_ADDON", "LG", msg, "WHISPER", "Smokeplayer")
        return
      end
    end
    error("no CHAT_MSG_ADDON frame found")
  end
  local function countTake()
    local n = 0
    for _, m in ipairs(L) do
      if string.find(m, "SEND: TAKE|2|2589|", 1, true) then n = n + 1 end
    end
    return n
  end
  step("empty-bag craft stages shortfall out of the vault", function()
    feedAddon("VLT|2|2589|5|Linen Cloth") -- vault holds 5x reagent 2589
    local framesBefore, origBefore, takesBefore = #allframes, origCasts.ts, countTake()
    E.DoTradeSkill(1)
    if origCasts.ts ~= origBefore then
      error("the held craft was cast immediately anyway")
    end
    if #allframes == framesBefore then
      error("no retry frame was created")
    end
    if countTake() == takesBefore then
      error("no TAKE sent for a covered shortfall")
    end
  end)
  step("staged craft casts once the withdrawal lands", function()
    rawbags = 5 -- the withdrawal landed
    simNow = 5 -- past the 0.30s re-TAKE gate
    local frames = {}
    for i = 1, #allframes do frames[i] = allframes[i] end
    for i = 1, #frames do
      if frames[i]._scripts.OnUpdate then
        frames[i]._scripts.OnUpdate(frames[i], 0.4)
      end
    end
    -- One cast from the pre-vault diag step, one from the fired retry.
    if origCasts.ts ~= 2 then
      error("retry never cast the held craft (casts=" .. origCasts.ts .. ")")
    end
  end)
  step("craft the bags can pay stages nothing", function()
    local framesBefore, origBefore, takesBefore = #allframes, origCasts.ts, countTake()
    E.DoTradeSkill(1)
    if #allframes ~= framesBefore or origCasts.ts ~= origBefore + 1 or countTake() ~= takesBefore then
      error("a fully-paid craft went through staging")
    end
  end)
  step("craft short even with the vault is not staged", function()
    feedAddon("VLT|2|2589|0|Linen Cloth")
    local framesBefore, origBefore, takesBefore = #allframes, origCasts.ts, countTake()
    E.DoTradeSkill(1)
    if #allframes ~= framesBefore or origCasts.ts ~= origBefore + 1 or countTake() ~= takesBefore then
      error("staged a craft that is truly short")
    end
  end)

  -- 4. unknown globals the file touched (review: WoW API vs typo)
  local uniq = {}
  for k in pairs(stubcache) do uniq[#uniq+1] = k end
  table.sort(uniq)
  local fails = 0
  for _, m in ipairs(L) do
    if string.find(m, "STEP FAIL", 1, true) then fails = fails + 1 end
  end
  local out = table.concat(L, "\n")
  out = out .. "\n\n== AUTO-STUBBED GLOBALS (" .. #uniq .. ") ==\n" .. table.concat(uniq, "\n")
  out = out .. "\n\nFAILURES: " .. fails
  return out
end, debug.traceback)
if ok then return report end
return "DRIVER ERROR:\n" .. tostring(report)
'''

print(lua.execute(DRIVER))
