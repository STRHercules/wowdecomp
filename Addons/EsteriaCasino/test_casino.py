"""Lua behavior check for EsteriaCasino with UI mocks; does not prove native rendering.

  python test_casino.py
"""
from pathlib import Path

from lupa.lua51 import LuaRuntime

HERE = Path(__file__).resolve().parent

MOCKS = r'''
unpack = unpack or table.unpack; tinsert = table.insert
UISpecialFrames = {}; sent = {}; sounds = {}; money = 1000000000
UIParent = {}
function UnitName() return "Tester" end
function SendAddonMessage(prefix, body) table.insert(sent, { prefix, body }) end
function GetCoinTextureString(c) return tostring(c) .. "c" end
function GetMoney() return money end
function PlaySound(name) table.insert(sounds, name) end
GameTooltip = setmetatable({}, { __index = function() return function() end end })
frames = {}
local methods = {}
function methods:SetScript(name, fn) self.scripts[name] = fn end
function methods:Show() local was = self.shown; self.shown = true; if not was and self.scripts.OnShow then self.scripts.OnShow(self) end end
function methods:Hide() local was = self.shown; self.shown = false; if was and self.scripts.OnHide then self.scripts.OnHide(self) end end
function methods:IsShown() return self.shown end
function methods:GetFrameLevel() return 1 end
function methods:Enable() self.enabled = true end
function methods:Disable() self.enabled = false end
function methods:SetText(t) self.text = t end
function methods:GetText() return self.text end
function methods:SetTexture(t) self.texture = t end
function methods:SetFrameLevel(level) self.level = level end
function methods:CreateTexture() return newObject() end
texts = {}
function methods:CreateFontString() local o = newObject(); table.insert(texts, o); return o end
function methods:GetNormalTexture() return newObject() end
function methods:GetPushedTexture() return newObject() end
function methods:GetDisabledTexture() return newObject() end
function methods:SetTexCoord(...) self.coords = { ... } end
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
    for name in ("esteria_style.lua", "EsteriaCasino.lua"):
        lua.execute((HERE / name).read_text(encoding="utf-8"))
    g = lua.globals()
    lua.execute('''
frame = EsteriaCasinoFrame; D = EsteriaCasinoDebug
for _, f in ipairs(frames) do if f.scripts.OnEvent then driver = f end end
function event(...) driver.scripts.OnEvent(driver, ...) end
function msg(body) event("CHAT_MSG_ADDON", "ECAR", body, "WHISPER", "Tester") end
function lastSent() return sent[#sent] and sent[#sent][2] end
function shows(needle)
    for _, t in ipairs(texts) do if t.text and string.find(t.text, needle, 1, true) then return true end end
    return false
end
function click(b, button) if b.enabled then b.scripts.OnClick(b, button or "LeftButton") end end
function tick(dt) for _, f in ipairs(frames) do if f.scripts.OnUpdate then f.scripts.OnUpdate(f, dt) end end end
function run(seconds) for i = 1, math.floor(seconds / 0.05) do tick(0.05) end end
function alive()
    local n, codes = 0, {}
    for _, e in ipairs(D.cards) do if e.alive then n = n + 1; codes[e.key] = e.code end end
    return n, codes
end
function stack(game, i) return D.stacks[game][i or 1].copper end
''')
    lua.execute('event("PLAYER_LOGIN")')
    assert lua.eval('lastSent()') == "HI", "addon announces itself"
    assert not lua.eval('frame.shown'), "window starts hidden"

    # The dealer seats us: limits and rules show, the default bet already sits on the betting circle.
    lua.execute('msg("O:bj:1000:2500000:1"); msg("T:0")')
    assert lua.eval('frame.shown')
    assert lua.eval('EsteriaCasinoDeal.shown') and not lua.eval('EsteriaCasinoHIT.shown')
    assert lua.eval('shows("Dealer hits soft 17")'), "rules are shown before betting"
    assert lua.eval('stack("bj")') == 100000, "the pending bet is on the felt before the deal"

    # Chips are thrown onto the circle as the bet builds; clear takes them back.
    lua.execute('click(EsteriaCasinoClear); run(0.5)')
    assert lua.eval('stack("bj")') == 0
    lua.execute('click(EsteriaCasinoChip4); click(EsteriaCasinoChip4); run(0.5)')
    assert lua.eval('stack("bj")') == 500000, lua.eval('stack("bj")')
    lua.execute('click(EsteriaCasinoDeal)')
    assert lua.eval('lastSent()') == "B:500000", lua.eval('lastSent()')
    lua.execute('click(EsteriaCasinoChip8); run(0.5)')  # 5000g is above the 250g limit
    assert lua.eval('stack("bj")') == 2500000, "the stack follows the clamped bet"
    lua.execute('click(EsteriaCasinoChip1, "RightButton"); run(0.5)')
    assert lua.eval('stack("bj")') == 2490000
    lua.execute('click(EsteriaCasinoDeal)')
    assert lua.eval('lastSent()') == "B:2490000"

    # Not enough gold: deal is disabled.
    lua.execute('money = 500; click(EsteriaCasinoChip1)')
    assert lua.eval('EsteriaCasinoDeal.enabled') is False
    lua.execute('money = 1000000000; click(EsteriaCasinoRebet); run(0.5)')

    # The deal: four cards come out of the shoe; the dealer's second card stays face down.
    lua.execute('msg("T:7:1:0:KS,??:10:9H,2C|11|0|500000|P:HSD"); run(2)')
    n, codes = lua.eval('alive()')
    assert n == 4, n
    assert codes["d2"] == "??" and codes["h1_1"] == "9H" and codes["d1"] == "KS", dict(codes)
    assert lua.eval('EsteriaCasinoHIT.shown') and not lua.eval('EsteriaCasinoDeal.shown')
    assert lua.eval('EsteriaCasinoDOUBLE.enabled') is True
    assert lua.eval('EsteriaCasinoSPLIT.enabled') is False
    lua.execute('click(EsteriaCasinoDOUBLE)')
    assert lua.eval('lastSent()') == "D"

    # Hiding mid-hand does not leave the table.
    before = lua.eval('#sent')
    lua.execute('frame:Hide()')
    assert lua.eval('#sent') == before, "closing mid-hand sends nothing"
    lua.execute('frame:Show()')

    # The final table keeps the dealt cards, turns the hole card and adds the double's card; the stake doubles.
    lua.execute('msg("T:7:2:0:KS,7D:17:9H,2C,TD|21|0|1000000|W:"); run(2)')
    n, codes = lua.eval('alive()')
    assert n == 5 and codes["d2"] == "7D" and codes["h1_3"] == "TD", (n, dict(codes))
    assert lua.eval('stack("bj")') == 1000000, "the doubled stake is on the felt"

    # The result: chips pay out from the dealer, a gold title and the net win show, then the felt clears.
    lua.execute('msg("R:7:1000000:2000000:0")')
    assert lua.eval('EsteriaCasinoDeal.enabled') is False, "betting waits for the payout animation"
    lua.execute('run(1.0)')
    assert lua.eval('shows("+1000000c")')
    assert lua.eval('D.title.texture') == "Interface\\AddOns\\EsteriaCasino\\Art\\title_youwin"
    lua.execute('run(3)')
    assert "LOOTWINDOWCOINSOUND" in list(g.sounds.values())
    assert lua.eval('EsteriaCasinoDeal.shown') and lua.eval('EsteriaCasinoDeal.enabled') is True
    assert lua.eval('stack("bj")') == lua.eval('D.state.bet'), "the next pending bet is back on the circle"
    lua.execute('click(EsteriaCasinoRebet); click(EsteriaCasinoDeal)')
    assert lua.eval('lastSent()') == "B:2490000", "rebet repeats the last stake"

    # A hit fans the hand out, and each later card is drawn above the one before it.
    lua.execute('msg("T:7b:1:0:KS,??:10:AD,6S|17|1|100000|P:HSD"); run(2)')
    lua.execute('msg("T:7b:1:0:KS,??:10:AD,6S,8C|15|0|100000|P:HS"); run(2)')
    levels = lua.eval('(function() local t = {} for _, e in ipairs(D.cards) do if e.alive then t[e.key] = e.frame.level end end return t end)()')
    assert levels["h1_1"] < levels["h1_2"] < levels["h1_3"], dict(levels)
    lua.execute('msg("T:7b:2:0:KS,9D:19:AD,6S,8C|15|0|100000|L:"); msg("R:7b:100000:0:0"); run(5)')

    # A new round sweeps the old cards away before dealing.
    lua.execute('msg("T:8:1:0:AH,??:11:5S,6D|11|0|2490000|P:HSD"); run(2)')
    n, codes = lua.eval('alive()')
    assert n == 4 and codes["h1_1"] == "5S", (n, dict(codes))

    # A split moves the second card to a new hand and throws a matching stack beside it.
    lua.execute('msg("T:8:1:0:AH,??:11:5S,9C|14|0|2490000|P;5H,KD|15|0|2490000|P:HS"); run(2)')
    n, codes = lua.eval('alive()')
    assert n == 6 and codes["h2_1"] == "5H", (n, dict(codes))
    assert lua.eval('stack("bj", 2)') == 2490000
    lua.execute('msg("T:8:2:1:AH,QS:21:5S,9C|14|0|2490000|L;5H,KD|15|0|2490000|L:"); msg("R:8:4980000:0:0")')
    lua.execute('run(4)')
    assert lua.eval('shows("You lose 4980000c")')
    assert lua.eval('stack("bj", 2)') == 0 and lua.eval('D.stacks.bj[1].homeX') == 0, "the circle is reset"

    # Errors are shown in words; the table limit can shrink after the bankroll moves.
    lua.execute('msg("E:range"); msg("M:100000")')
    assert lua.eval('shows("Move closer to the dealer")')
    lua.execute('click(EsteriaCasinoDeal)')
    assert lua.eval('lastSent()') == "B:100000", "bet re-clamps to the new limit"

    # Closing between rounds leaves the seat exactly once; a server close sends nothing back.
    lua.execute('frame:Hide()')
    assert lua.eval('lastSent()') == "L"
    lua.execute('msg("O:bj:1000:2500000:0"); sent = {}; msg("C")')
    assert not lua.eval('frame.shown') and lua.eval('#sent') == 0, "C closes without an L"

    # A blackjack hand left open waits at the dealer: the slot machine still opens and says so; the dealer resumes it.
    lua.execute('msg("O:bj:1000:2500000:0"); msg("T:9:1:0:6S,??:6:5H,8D|13|0|100000|P:HSD"); run(2); frame:Hide()')
    lua.execute('msg("O:slots:1000:400000:10,8,6,4,3,1:4,10,20,50,100,500:1:9746"); msg("W")')
    assert lua.eval('EsteriaCasinoSpin.shown') and not lua.eval('EsteriaCasinoHIT.shown')
    assert lua.eval('shows("Your blackjack hand is still waiting")')
    lua.execute('msg("O:bj:1000:2500000:0"); msg("T:9:1:0:6S,??:6:5H,8D|13|0|100000|P:HSD"); run(1)')
    n, codes = lua.eval('alive()')
    assert n == 4 and codes["h1_2"] == "8D" and lua.eval('EsteriaCasinoHIT.shown'), "the dealer resumes the same hand"
    lua.execute('msg("T:9:2:0:6S,TC,9H:25:5H,8D|13|0|100000|W:"); msg("R:9:100000:200000:0"); run(5); msg("C")')

    # Slots: the pay table and return come from the server; a spin locks the controls until the reels stop.
    lua.execute('msg("O:slots:1000:400000:10,8,6,4,3,1:4,10,20,50,100,500:1:9746")')
    assert lua.eval('frame.shown') and lua.eval('EsteriaCasinoSpin.shown') and not lua.eval('EsteriaCasinoDeal.shown')
    assert lua.eval('shows("Dragon  x500")') and lua.eval('shows("Any 3 coins  x1")')
    assert lua.eval('shows("Returns 97.46%")')
    lua.execute('sent = {}; click(EsteriaCasinoClear); click(EsteriaCasinoChip3); run(0.5); click(EsteriaCasinoSpin)')
    assert lua.eval('lastSent()') == "SP:100000", lua.eval('lastSent()')
    assert lua.eval('stack("slots")') == 100000, "the spin's stake sits beside the reels"
    assert lua.eval('EsteriaCasinoSpin.enabled') is False, "spin locks while the reels turn"
    lua.execute('click(EsteriaCasinoSpin)')
    assert lua.eval('#sent') == 1, "a locked spin sends nothing"
    lua.execute('msg("SR:11:5:5:5:100000:50000000:0"); run(1.0)')
    assert not lua.eval('shows("+49900000c")'), "the win shows only once the last reel stops"
    lua.execute('run(1.5)')
    assert lua.eval('shows("+49900000c")')
    assert lua.eval('D.title.texture') == "Interface\\AddOns\\EsteriaCasino\\Art\\title_jackpot", "a dragon line is a jackpot"
    for r in (1, 2, 3):
        assert lua.eval(f'D.reels[{r}].syms[2]') == 6, "every reel lands its symbol on the payline"
    lua.execute('run(3)')
    assert lua.eval('EsteriaCasinoSpin.enabled') is True, "controls unlock after the payout"

    # Goblin Dice: four calls show their payouts; a roll shakes the cup, then shows the server's dice.
    lua.execute('msg("O:dice:1000:5000000:23300,58000,23300,58000:9708,9666,9708,9666")')
    assert lua.eval('EsteriaCasinoCall1.shown') and not lua.eval('EsteriaCasinoSpin.shown')
    assert lua.eval('shows("pays 2.33x  (97.08%)")') and lua.eval('shows("pays 5.8x  (96.66%)")')
    lua.execute('click(EsteriaCasinoCall2)')
    assert lua.eval('lastSent()') == "DR:100000:1", lua.eval('lastSent()')
    assert lua.eval('EsteriaCasinoCall3.enabled') is False
    lua.execute('msg("DR:12:3:4:1:100000:580000:0"); run(2)')
    assert lua.eval('shows("+480000c")')
    assert lua.eval('D.title.texture') == "Interface\\AddOns\\EsteriaCasino\\Art\\title_luckyroll"
    lua.execute('run(3)')
    assert lua.eval('EsteriaCasinoCall3.enabled') is True
    lua.execute('click(EsteriaCasinoCall1); msg("DR:13:1:1:0:100000:0:0"); run(5)')
    assert lua.eval('shows("You lose 100000c")')
    assert lua.eval('stack("dice")') == 100000, "the pending bet returns to the felt after a loss"
    print("EsteriaCasino Lua checks passed")


if __name__ == "__main__":
    main()
