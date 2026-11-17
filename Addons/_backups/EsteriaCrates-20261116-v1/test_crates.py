"""Lua behavior check for EsteriaCrates with UI mocks; does not prove native rendering."""
from pathlib import Path
from lupa.lua51 import LuaRuntime

HERE = Path(__file__).resolve().parent

MOCKS = r'''
unpack = unpack or table.unpack; tinsert = table.insert
UISpecialFrames = {}; StaticPopupDialogs = {}; sent = {}; popups = {}; sounds = {}
ITEM_QUALITY_COLORS = { [3] = { r = 0, g = 0.44, b = 0.87 }, [4] = { r = 0.64, g = 0.21, b = 0.93 }, [5] = { r = 1, g = 0.5, b = 0 } }
ITEM_QUALITY3_DESC, ITEM_QUALITY4_DESC, ITEM_QUALITY5_DESC = "Rare", "Epic", "Legendary"
YES, NO = "Yes", "No"
money = 2000000; now = 100
function UnitName() return "Tester" end
function SendAddonMessage(prefix, body) table.insert(sent, { prefix, body }) end
function GetItemInfo(id) return "Item" .. id, nil, 4, nil, nil, nil, nil, nil, nil, "icon" .. id end
function GetCoinTextureString(c) return tostring(c) .. "c" end
function GetMoney() return money end
function GetTime() return now end
function time() return 1700000000 end
function PlaySound(name) table.insert(sounds, name) end
function StaticPopup_Show(name, a, b) table.insert(popups, { name, a, b }) end
function IsModifiedClick() return false end
function DressUpItemLink() end
GameTooltip = setmetatable({}, { __index = function() return function() end end })
frames = {}
local methods = {}
function methods:SetScript(name, fn) self.scripts[name] = fn end
function methods:GetScript(name) return self.scripts[name] end
function methods:Show() local was = self.shown; self.shown = true; if not was and self.scripts.OnShow then self.scripts.OnShow(self) end end
function methods:Hide() local was = self.shown; self.shown = false; if was and self.scripts.OnHide then self.scripts.OnHide(self) end end
function methods:IsShown() return self.shown end
function methods:IsEnabled() return self.enabled and 1 or nil end
function methods:Enable() self.enabled = true end
function methods:Disable() self.enabled = false end
function methods:GetChecked() return self.checked end
function methods:SetChecked(v) self.checked = v end
function methods:GetFrameLevel() return 1 end
function methods:SetText(t) self.text = t end
function methods:GetText() return self.text end
function methods:SetHorizontalScroll(x) self.scroll = x end
function methods:CreateTexture() return newObject() end
function methods:CreateFontString() return newObject() end
function methods:StartMoving() end
function methods:StopMovingOrSizing() end
local meta = { __index = function(t, k) return methods[k] or function() end end }
function newObject() local o = setmetatable({ scripts = {}, shown = true, enabled = true }, meta); return o end
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
frame = EsteriaCratesFrame; frame.shown = false
for _, f in ipairs(frames) do if f.scripts.OnEvent then driver = f end end
function event(...) driver.scripts.OnEvent(driver, ...) end
function tick(dt) now = now + dt; for _, f in ipairs(frames) do if f.scripts.OnUpdate then f.scripts.OnUpdate(f, dt) end end end
function msg(body) event("CHAT_MSG_ADDON", "ECRS", body, "WHISPER", "Tester") end
function lastSent() return sent[#sent][2] end
function details(crates, keys)
    msg("H:3450101:3450001:1000000:" .. crates .. ":" .. keys .. ":1:10000:11:10")
    local w = { 1200, 1200, 1200, 1200, 1200, 1200, 1200, 1000, 200, 200, 200 }
    for i = 1, 11 do msg("R:3450101:" .. (i - 1) .. ":" .. (900 + i) .. ":1:" .. w[i] .. ":" .. (i > 8 and 5 or 4) .. ":0") end
    msg("RE:3450101")
end
-- the action button is the frame whose OnClick opens or buys
function action() for _, f in ipairs(frames) do if rawget(f, "bg") and rawget(f, "text") and f.scripts.OnClick then return f end end end
''')
    lua.execute('event("PLAYER_LOGIN")')
    assert g.sent[1][1] == "ECRC" and g.sent[1][2] == "HI", "addon announces itself"

    lua.execute('msg("OPEN:3450101"); details(2, 1)')
    assert g.frame.shown, "OPEN shows the window"
    assert "U N L O C K" == lua.eval('action().text.text'), lua.eval('action().text.text')

    lua.execute('action().scripts.OnClick(action())')
    req = lua.eval('lastSent()')
    assert req.startswith("O:3450101:"), req
    assert lua.eval('action().enabled') is False, "button locks while the server answers"
    lua.execute('action().scripts.OnClick(action())')
    assert lua.eval('lastSent()') == req, "a second click sends nothing"

    rid = req.split(":")[2]
    lua.execute(f'msg("W:{rid}:3450101:9:910:1:0"); details(1, 0)')
    for _ in range(200):
        lua.execute('tick(0.05)')
    again = lua.eval('(function() for _, f in ipairs(frames) do if rawget(f, "text") and rawget(f.text, "text") and '
                     'f.text.text:find("^O P E N") then return f.text.text end end end)()')
    assert again and "(1 left)" in again, again
    assert lua.eval('action().enabled') is False, "no new opening while the reveal is up"
    assert any(s == "LEVELUPSOUND" for s in g.sounds.values()), "legendary reveal plays its sound"

    # Out of keys: the button turns into a key purchase at the server's price.
    lua.execute('frame:Hide(); msg("OPEN:3450101"); details(1, 0)')
    assert lua.eval('action().text.text').startswith("Buy Item3450001"), lua.eval('action().text.text')
    lua.execute('action().scripts.OnClick(action())')
    assert g.popups[1][1] == "ESTERIA_CRATES_BUY_KEY"
    lua.execute('StaticPopupDialogs.ESTERIA_CRATES_BUY_KEY.OnAccept()')
    buy = lua.eval('lastSent()')
    assert buy.startswith("B:3450101:1:1000000:"), buy

    # A refusal unlocks the window and says why.
    rid = buy.split(":")[4]
    lua.execute(f'msg("E:{rid}:money")')
    assert lua.eval('action().enabled') is True

    # Not enough gold disables the purchase.
    lua.execute('money = 5; details(1, 0)')
    assert lua.eval('action().enabled') is False

    # No answer: the request times out and the window asks for fresh details.
    lua.execute('money = 2000000; details(1, 1); action().scripts.OnClick(action()); tick(11)')
    assert lua.eval('lastSent()') == "D:3450101", lua.eval('lastSent()')
    assert lua.eval('action().enabled') is True
    print("EsteriaCrates: all checks passed")


if __name__ == "__main__":
    main()
