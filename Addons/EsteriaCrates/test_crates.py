"""Lua behavior check for EsteriaCrates with UI mocks; does not prove native rendering."""
from pathlib import Path
from lupa.lua51 import LuaRuntime

HERE = Path(__file__).resolve().parent

MOCKS = r'''
unpack = unpack or table.unpack; tinsert = table.insert
UISpecialFrames = {}; StaticPopupDialogs = {}; SlashCmdList = {}; sent = {}; popups = {}; sounds = {}
ITEM_QUALITY_COLORS = { [3] = { r = 0, g = 0.44, b = 0.87 }, [4] = { r = 0.64, g = 0.21, b = 0.93 }, [5] = { r = 1, g = 0.5, b = 0 } }
ITEM_QUALITY3_DESC, ITEM_QUALITY4_DESC, ITEM_QUALITY5_DESC = "Rare", "Epic", "Legendary"
YES, NO = "Yes", "No"
money = 20000000; now = 100; cached = {}
function UnitName() return "Tester" end
function SendAddonMessage(prefix, body) table.insert(sent, { prefix, body }) end
function GetItemInfo(id) if cached[id] then return "Item" .. id, nil, 4, nil, nil, nil, nil, nil, nil, "icon" .. id end end
function GetCoinTextureString(c) return tostring(c) .. "c" end
function GetMoney() return money end
function GetTime() return now end
function time() return 1700000000 end
function PlaySound(name) table.insert(sounds, name) end
function StaticPopup_Show(name, a, b) local p = { name = name, a = a, b = b }; table.insert(popups, p); return p end
function IsModifiedClick() return false end
function DressUpItemLink() end
GameTooltip = setmetatable({}, { __index = function() return function() end end })
frames = {}; texts = {}
local methods = {}
function methods:SetScript(name, fn) self.scripts[name] = fn end
function methods:GetScript(name) return self.scripts[name] end
function methods:Show() local was = self.shown; self.shown = true; if not was and self.scripts.OnShow then self.scripts.OnShow(self) end end
function methods:Hide() local was = self.shown; self.shown = false; if was and self.scripts.OnHide then self.scripts.OnHide(self) end end
function methods:IsShown() return self.shown end
function methods:IsVisible() return self.shown end
function methods:IsEnabled() return self.enabled and 1 or nil end
function methods:Enable() self.enabled = true end
function methods:Disable() self.enabled = false end
function methods:GetChecked() return self.checked end
function methods:SetChecked(v) self.checked = v end
function methods:GetFrameLevel() return 1 end
function methods:SetText(t) self.text = t end
function methods:GetText() return self.text end
function methods:SetSize(w, h) self.w, self.h = w, h end
function methods:SetWidth(w) self.w = w end
function methods:SetHeight(h) self.h = h end
function methods:GetWidth() return self.w or 0 end
function methods:GetHeight() return self.h or 0 end
function methods:SetHorizontalScroll(x) self.scroll = x end
function methods:CreateTexture() return newObject() end
function methods:CreateFontString() local o = newObject(); table.insert(texts, o); return o end
function methods:StartMoving() end
function methods:StopMovingOrSizing() end
local meta = { __index = function(t, k) return methods[k] or (k:match("^%u") and function() end) or nil end }
function newObject() return setmetatable({ scripts = {}, shown = true, enabled = true }, meta) end
function CreateFrame(kind, name)
    local f = newObject()
    if name then _G[name] = f end
    table.insert(frames, f)
    return f
end
'''


def main():
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute(MOCKS)
    for name in ("esteria_style.lua", "EsteriaCrates.lua"):
        lua.execute((HERE / name).read_text(encoding="utf-8"))
    g = lua.globals()
    lua.execute('''
frame = EsteriaCratesFrame; frame.shown = false; EsteriaCratesStore.shown = false
for _, f in ipairs(frames) do if f.scripts.OnEvent then driver = f end end
function event(...) driver.scripts.OnEvent(driver, ...) end
function tick(dt) now = now + dt; for _, f in ipairs(frames) do if f.scripts.OnUpdate then f.scripts.OnUpdate(f, dt) end end end
function msg(body) event("CHAT_MSG_ADDON", "ECRS", body, "WHISPER", "Tester") end
function lastSent() return sent[#sent][2] end
function details(crates, keys)
    msg("H:3450101:3450001:1000000:" .. crates .. ":" .. keys .. ":1:10000:11:10:250000")
    local w = { 1200, 1200, 1200, 1200, 1200, 1200, 1200, 1000, 200, 200, 200 }
    for i = 1, 11 do msg("R:3450101:" .. (i - 1) .. ":" .. (900 + i) .. ":1:" .. w[i] .. ":" .. (i > 8 and 5 or i > 4 and 4 or 3) .. ":0") end
    msg("RE:3450101")
end
function click(b) b.scripts.OnClick(b) end
action = EsteriaCratesAction
''')
    lua.execute('event("PLAYER_LOGIN")')
    assert g.sent[1][2] == "HI", "addon announces itself"

    # Uncached items show a placeholder, then redraw on their own once the client has them.
    lua.execute('msg("OPEN:3450101"); details(2, 1)')
    assert g.frame.shown, "OPEN shows the window"
    assert lua.eval('lastSent()') == "D:3450101"
    assert lua.eval('action.text.text') == "U N L O C K", lua.eval('action.text.text')
    lua.execute('for i = 901, 911 do cached[i] = true end; cached[3450101] = true; cached[3450001] = true; tick(0.5)')
    assert lua.eval('(function() for _, f in ipairs(texts) do if rawget(f, "text") == "Item3450101" then return true end end end)()'), \
        "title redraws once item info arrives"

    lua.execute('click(action)')
    req = lua.eval('lastSent()')
    assert req.startswith("O:3450101:"), req
    assert lua.eval('action.enabled') is False, "button locks while the server answers"
    lua.execute('click(action)')
    assert lua.eval('lastSent()') == req, "a second click sends nothing"

    rid = req.split(":")[2]
    lua.execute(f'msg("W:{rid}:3450101:9:910:1:0"); details(1, 0)')
    for _ in range(200):
        lua.execute('tick(0.05)')
    assert "(0 left)" in lua.eval('EsteriaCratesAgain.text.text'), lua.eval('EsteriaCratesAgain.text.text')
    assert lua.eval('EsteriaCratesAgain.enabled') is False, "no keys left: Open Another is off"
    assert any(s == "LEVELUPSOUND" for s in g.sounds.values()), "legendary reveal plays its sound"

    # Out of keys: the window button becomes a key purchase at the server's price.
    lua.execute('frame:Hide(); EsteriaCrates_Open(3450101); details(1, 0)')
    assert lua.eval('action.text.text').startswith("Buy Item3450001"), lua.eval('action.text.text')
    lua.execute('click(action)')
    assert lua.eval('popups[#popups].name') == "ESTERIA_CRATES_BUY"
    lua.execute('StaticPopupDialogs.ESTERIA_CRATES_BUY.OnAccept(nil, popups[#popups].data)')
    buy = lua.eval('lastSent()')
    assert buy.startswith("B:3450101:1:1000000:"), buy
    lua.execute(f'msg("E:{buy.split(":")[4]}:money")')
    assert lua.eval('action.enabled') is True, "a refusal unlocks the button"

    # Out of crates: the button sells the crate itself.
    lua.execute('details(0, 2)')
    assert "Item3450101" in lua.eval('action.text.text') and "250000c" in lua.eval('action.text.text')

    # Not enough gold disables the purchase.
    lua.execute('money = 5; details(0, 0)')
    assert lua.eval('action.enabled') is False
    lua.execute('money = 20000000')

    # The Collections tab: embedding lists crates, buys crates and keys in quantity, opens the window.
    lua.execute('frame:Hide(); EsteriaCrates_Embed(newObject())')
    assert lua.eval('lastSent()') == "L"
    lua.execute('msg("LC:3450101:3450001:1000000:250000:0:0:1"); msg("LE")')
    assert lua.eval('lastSent()') == "D:3450101", "listing asks for each crate's pool"
    lua.execute('details(0, 0)')
    assert lua.eval('EsteriaCratesStoreOpen.enabled') is False, "no crate and no key: open is off"
    shown = lua.eval('(function() local n = 0; for _, f in ipairs(frames) do if f.reward and f.total and f.shown then n = n + 1 end end; return n end)()')
    assert shown >= 5, f"featured rewards are shown ({shown})"
    lua.execute('click(EsteriaCratesStoreBuyCrate)')
    lua.execute('StaticPopupDialogs.ESTERIA_CRATES_BUY.OnAccept(nil, popups[#popups].data)')
    crate_buy = lua.eval('lastSent()')
    assert crate_buy.startswith("C:3450101:1:250000:"), crate_buy
    lua.execute(f'msg("P:{crate_buy.split(":")[4]}:3450101:1:250000"); details(1, 0)')
    lua.execute('click(EsteriaCratesStoreBuyKey)')
    lua.execute('StaticPopupDialogs.ESTERIA_CRATES_BUY.OnAccept(nil, popups[#popups].data)')
    assert lua.eval('lastSent()').startswith("B:3450101:1:1000000:")
    lua.execute(f'msg("K:{lua.eval("lastSent()").split(":")[4]}:3450101:1:1000000"); details(1, 1)')
    assert lua.eval('EsteriaCratesStoreOpen.enabled') is True

    # Overlapping purchases: a second confirmation while one is in flight sends nothing, and the first still lands.
    lua.execute('click(EsteriaCratesStoreBuyKey); first = popups[#popups].data; click(EsteriaCratesStoreBuyCrate); second = popups[#popups].data')
    lua.execute('StaticPopupDialogs.ESTERIA_CRATES_BUY.OnAccept(nil, first)')
    first = lua.eval('lastSent()')
    lua.execute('StaticPopupDialogs.ESTERIA_CRATES_BUY.OnAccept(nil, second)')
    assert lua.eval('lastSent()') == first, "second purchase waits for the first"
    assert lua.eval('EsteriaCratesStoreBuyCrate.enabled') is False, "buy buttons lock while a purchase is pending"
    lua.execute(f'msg("K:{first.split(":")[4]}:3450101:1:1000000"); details(1, 2); tick(11)')
    timed_out = lua.eval('(function() for _, f in ipairs(texts) do local t = rawget(f, "text"); '
                         'if type(t) == "string" and t:find("No answer") then return t end end end)()')
    assert not timed_out, "an answered purchase never times out"
    assert lua.eval('EsteriaCratesStoreBuyCrate.enabled') is True

    lua.execute('click(EsteriaCratesStoreOpen)')
    assert g.frame.shown and lua.eval('lastSent()') == "D:3450101", "Open launches the unlock window"

    # No answer: the request times out and the window asks for fresh details.
    lua.execute('details(1, 1); click(action); tick(11)')
    assert lua.eval('lastSent()') == "D:3450101", lua.eval('lastSent()')
    assert lua.eval('action.enabled') is True
    print("EsteriaCrates: all checks passed")


if __name__ == "__main__":
    main()
