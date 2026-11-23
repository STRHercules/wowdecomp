"""Lua behavior check for the chess/checkers, poker and lottery windows with UI mocks; does not prove rendering.

  python test_casino_games.py
"""
from pathlib import Path

from lupa.lua51 import LuaRuntime

import test_casino

HERE = Path(__file__).resolve().parent
FILES = ("esteria_style.lua", "EsteriaCasino.lua", "CasinoBoards.lua", "CasinoPoker.lua", "CasinoLottery.lua")


def main():
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute(test_casino.MOCKS)
    for name in FILES:
        lua.execute((HERE / name).read_text(encoding="utf-8"))
    lua.execute('''
frame = EsteriaCasinoFrame; B = EsteriaCasinoBoardsDebug; P = EsteriaCasinoPokerDebug; L = EsteriaCasinoLotteryDebug
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
function title() return EsteriaCasinoDebug.title.texture end
CHESS = "RNBQKBNRPPPPPPPP................................pppppppprnbqkbnr"
''')
    lua.execute('event("PLAYER_LOGIN")')

    # The lobby: challenges and matches from the server; a challenge is posted from the stake box.
    lua.execute('msg("O:board:100000000:500:10")')
    assert lua.eval('frame.shown') and lua.eval('shows("Chess & Checkers")')
    assert not lua.eval('EsteriaCasinoDeal.shown'), "the blackjack betting bar stays hidden"
    lua.execute('msg("GL:3:c:Arthas:500000:10:0"); msg("GL:4:k:Tester:0:5:1"); msg("GLE")')
    lua.execute('msg("GG:9:c:Jaina:Thrall:1000000"); msg("GGE")')
    assert lua.eval('shows("Arthas")') and lua.eval('shows("Jaina vs Thrall")')
    lua.execute('click(B.rows[1].button)')
    assert lua.eval('lastSent()') == "GA:3"
    lua.execute('click(B.rows[2].button)')
    assert lua.eval('lastSent()') == "GX", "my own challenge offers cancel"
    lua.execute('click(B.rows[3].button)')
    assert lua.eval('lastSent()') == "GW:9"
    lua.execute('B.stake:SetText("50"); click(B.post)')
    assert lua.eval('lastSent()') == "GC:c:500000:10", lua.eval('lastSent()')

    # Practice against the house: the level button cycles, the request carries game, level and minutes.
    lua.execute('click(B.level); click(B.house)')
    assert lua.eval('lastSent()') == "GH:c:3:10", lua.eval('lastSent()')
    lua.execute('msg("E:seated")')
    assert lua.eval('shows("seated at the Hold")')
    lua.execute('msg("E:declined")')
    assert lua.eval('shows("The House declines the draw.")')
    lua.execute('sent[#sent + 1] = { "ECAS", "GC:c:500000:10" }')  # the next check expects nothing new sent after this

    # A chess match as black: the board is flipped, moves come from the server's list, a click pair sends one.
    lua.execute('msg("GS:5:c:Arthas:Tester:500000:1")')
    lua.execute('msg("GB:5:" .. CHESS .. ":0::600:600:")')
    assert lua.eval('B.view.side') == 1 and lua.eval('#B.view.legal') == 0
    lua.execute('click(B.squares[52])')
    assert lua.eval('lastSent()') == "GC:c:500000:10", "nothing to do on the opponent's turn"
    lua.execute('msg("GB:5:" .. CHESS .. ":1:e2e4:598:600:"); msg("GV:e7e5,e7e6,g8f6"); msg("GVE")')
    lua.execute('click(B.squares[52])')
    assert lua.eval('B.highlights[36].shown') and lua.eval('B.highlights[44].shown'), "e7 shows e5 and e6"
    lua.execute('click(B.squares[36])')
    assert lua.eval('lastSent()') == "GM:e7e5"
    before = lua.eval('B.view.clocks[2]')
    lua.execute('run(2)')
    assert lua.eval('B.view.clocks[2]') < before, "the clock of the side to move runs"
    lua.execute('msg("GB:5:" .. CHESS .. ":1::300:300:offer0")')
    assert lua.eval('shows("Draw offered.")') and lua.eval('shows("ACCEPT DRAW")')
    lua.execute('click(B.draw)')
    assert lua.eval('lastSent()') == "GD"
    lua.execute('msg("GE:5:1:checkmate:950000")')
    assert lua.eval('title()') == "Interface\\AddOns\\EsteriaCasino\\Art\\title_checkmate"
    assert lua.eval('shows("+450000c")'), "the net win after the house cut"
    lua.execute('run(5.5)')
    assert lua.eval('lastSent()') == "GL", "back to the lobby"

    # Checkers as black (side 0): paths from the server, a click pair sends the path.
    lua.execute('msg("GS:6:k:Tester:Arthas:0:0")')
    board = "w.w.w.w..w.w.w.ww.w.w.w.................b.b.b.b..b.b.b.bb.b.b.b."
    lua.execute(f'msg("GB:6:{board}:0::300:300:"); msg("GV:41-32,41-34,43-34"); msg("GVE")')
    lua.execute('click(B.squares[41]); click(B.squares[34])')
    assert lua.eval('lastSent()') == "GM:41-34"
    lua.execute('msg("GE:6:2:quiet:0")')
    assert lua.eval('title()') == "Interface\\AddOns\\EsteriaCasino\\Art\\title_draw"

    # Poker: an empty seat offers a buy-in; seated, the actions follow the server's legal list.
    lua.execute('msg("O:poker:5000:10000:200000:1000000:500:20000:30")')
    assert lua.eval('shows("Texas Hold\'em")')
    for i in range(6):
        lua.execute(f'msg("PS:{i}::0:0:")')
    lua.execute('msg("PT:0:0:0::-1:0::0:0:10000")')
    lua.execute('click(P.seats[2].sit)')
    assert lua.eval('P.buyRow.shown')
    lua.execute('P.buyBox:SetText("50"); click(P.buy)')
    assert lua.eval('lastSent()') == "PJ:2:500000", lua.eval('lastSent()')
    lua.execute('msg("PS:2:Tester:500000:0:s"); msg("PS:4:Arthas:800000:0:")')
    assert lua.eval('P.fold.shown') and lua.eval('P.leave.shown')
    lua.execute('msg("PS:2:Tester:495000:5000:isdt"); msg("PS:4:Arthas:790000:10000:i")')
    lua.execute('msg("PT:1:1:15000::2:10000:fcr:30:5000:10000"); msg("PH:AS,KS")')
    assert lua.eval('P.hand[1].shown') and lua.eval('P.hand[1].code') == "AS"
    assert lua.eval('shows("CALL 5000c")') and lua.eval('shows("RAISE 10000c")')
    lua.execute('click(P.raise)')
    assert lua.eval('lastSent()') == "PA:r"
    lua.execute('msg("PT:1:2:40000:QS,JS,TS:4:0::25:0:10000"); run(1)')
    assert lua.eval('P.board[3].shown') and not lua.eval('P.board[4].shown'), "the flop is out"
    assert lua.eval('P.fold.enabled') is False, "not your turn"
    lua.execute('msg("PW:1:2:38000:AS,KS:Straight Flush"); msg("PR:1:40000:2000")')
    assert lua.eval('title()') == "Interface\\AddOns\\EsteriaCasino\\Art\\title_youwin"
    assert lua.eval('shows("+38000c  Straight Flush")')
    lua.execute('msg("PT:2:0:0::-1:0::0:0:10000")')
    assert not lua.eval('P.board[1].shown'), "a new hand clears the board"
    lua.execute('click(P.leave)')
    assert lua.eval('lastSent()') == "PL"

    # The lottery: pick four balls, buy; a new draw drops the balls and celebrates a jackpot ticket.
    lua.execute('msg("O:lottery:100000:1500000:10:4:20")')
    lua.execute('msg("LS:7:5000000:3600:0"); msg("LR:6:2,5,9,14:0:0:1:1500000:3500000")')
    assert lua.eval('shows("Jackpot pool  5000000c")') and lua.eval('shows("Draw #6")')
    assert lua.eval('L.buy.enabled') is False, "four numbers first"
    for n in (19, 3, 12, 7):
        lua.execute(f'click(EsteriaCasinoBall{n})')
    lua.execute('click(EsteriaCasinoBall20)')
    assert lua.eval('#L.data.picks') == 4, "a fifth number is refused"
    assert lua.eval('shows("Your numbers: 3  7  12  19")')
    lua.execute('msg("LS:7:5000000:3600:0")')
    lua.execute('click(L.buy)')
    assert lua.eval('lastSent()') == "LB:3,7,12,19", lua.eval('lastSent()')
    lua.execute('msg("LK:3,7,12,19"); msg("LS:7:5100000:3590:1"); msg("LT:3,7,12,19"); msg("LR:6:2,5,9,14:0:0:1:1500000:3500000")')
    assert lua.eval('#L.data.picks') == 0 and lua.eval('L.tickets[1].balls[1].shown')
    lua.execute('click(L.quick)')
    assert lua.eval('lastSent()') == "LB:Q"
    lua.execute('msg("LS:8:0:3600:0"); msg("LR:7:3,7,12,19:1:5100000:0:0:0"); msg("LP:3,7,12,19:4:5100000")')
    assert lua.eval('shows("3,7,12,19: 4 matched  +5100000c")')
    lua.execute('run(2)')
    assert lua.eval('title()') == "Interface\\AddOns\\EsteriaCasino\\Art\\title_jackpot"

    # Closing each window tells its server half.
    lua.execute('frame:Hide()')
    assert lua.eval('lastSent()') == "LQ"
    print("EsteriaCasino game window checks passed")


if __name__ == "__main__":
    main()
