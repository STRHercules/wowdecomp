"""Behavior check for the Adventure Finder with UI and LFD API mocks; does not prove native rendering."""
import sys
from pathlib import Path
from lupa.lua51 import LuaRuntime

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "EsteriaCrates"))
from test_crates import MOCKS  # noqa: E402  shared frame/texture mocks

LFD = r'''
UIErrorsFrame = { AddMessage = function(_, m) lastError = m end }
EXPANSION_LEVEL = 2
calls = {}
local function log(...) table.insert(calls, table.concat({ ... }, ",")) end
mode = nil; roles = { false, false, false, true }
enabledState = { [10] = true, [11] = false, [12] = true }
function UnitLevel() return 80 end
function GetNumPartyMembers() return 0 end
function GetNumRaidMembers() return 0 end
function IsPartyLeader() return true end
function IsRaidLeader() return true end
function GetTexCoordsForRole() return 0, 1, 0, 1 end
function SecondsToTime(s) return tostring(math.floor(s)) .. " sec" end
function GetNumRandomDungeons() return 2 end
function GetLFGRandomDungeonInfo(i) return ({ 261, 262 })[i], ({ "Random Lich King Heroic", "Random Lich King Dungeon" })[i] end
local info = { [261] = { "Random Lich King Heroic", 6, 80, 80, 80, 80, 80, 2, 0, "", 1, 5, "heroic desc" },
               [262] = { "Random Lich King Dungeon", 6, 68, 80, 80, 68, 80, 2, 0, "", 0, 5, "" },
               [-1] = { "Wrath Dungeons" }, [10] = { "Utgarde Keep", 1, 68, 80 }, [11] = { "The Nexus", 1, 69, 80 },
               [12] = { "Halls of Stone", 1, 75, 80 }, [13] = { "Deadmines", 1, 15, 21 } }
function GetLFGDungeonInfo(id) return unpack(info[id]) end
function IsLFGDungeonJoinable(id) return id ~= 261 end
function GetRandomDungeonBestChoice() return 262 end
function GetLFGDungeonRewards() return false, 1000, 0, 5000, 0, 1 end
function GetLFGDungeonRewardInfo() return "Emblem of Frost", "emblem", 2 end
function GetLFDChoiceOrder() return { -1, 10, 11, 12, 13 } end
function GetLFDChoiceEnabledState() local t = {}; for k, v in pairs(enabledState) do t[k] = v end; return t end
function GetLFDChoiceLockedState() return { [13] = true } end
function GetLFDLockPlayerCount() return 1 end
function GetLFDLockInfo(id) return "Tester", id == 13 and 3 or 0 end
function LFDConstructDeclinedMessage() return "You are too high level." end
function SetLFGDungeonEnabled(id, on) enabledState[id] = on; log("enable", id, tostring(on)) end
function GetLFGMode() return mode end
function GetLFGRoles() return roles[4], roles[1], roles[2], roles[3] end
function SetLFGRoles(leader, tank, healer, dps) roles = { tank, healer, dps, leader } end
function GetAvailableRoles() return true, true, true end
function GetLFGQueueStats() return true, 0, 1, 0, 2, 1, "Utgarde Keep", 300, 0, 0, 0, 240, now - 90 end
function ClearAllLFGDungeons() log("clear") end
function SetLFGDungeon(id) log("set", id) end
function JoinLFG() log("join") end
function LeaveLFG() log("leave") end
function RequestLFDPlayerLockInfo() end
function RequestLFDPartyLockInfo() end
EsteriaManastorm_Embed = function(parent, onClose) embedded = parent; closeEmbedded = onClose; return true end
'''


def main():
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute(MOCKS)
    lua.execute(LFD)
    for name in ("esteria_style.lua", "EsteriaFinder.lua"):
        lua.execute((HERE / name).read_text(encoding="utf-8"))
    lua.execute('''
for _, f in ipairs(frames) do if f.scripts.OnEvent then driver = f end end
function event(...) driver.scripts.OnEvent(driver, ...) end
function button(text)
    for _, f in ipairs(frames) do
        local label = rawget(f, "label")
        if label and label.text == text and f.scripts.OnClick then return f end
    end
end
function find(pred) for _, f in ipairs(frames) do if pred(f) then return f end end end
function joined() return table.concat(calls, " | ") end
''')
    lua.execute('event("PLAYER_LOGIN"); ToggleLFDParentFrame()')
    assert lua.eval('EsteriaFinderFrame.shown'), "the Dungeon Finder hotkey opens the Adventure Finder"

    # Random: the best choice is preselected; pick tank and queue.
    lua.execute('local t = find(function(f) return rawget(f, "label") and f.label.text == "Tank" end); t.scripts.OnClick(t)')
    assert lua.eval('roles[1]') is True, "clicking Tank sets the tank role"
    lua.execute('local q = button("Find Group"); q.scripts.OnClick(q)')
    assert lua.eval('joined()') == "clear | set,262 | join", lua.eval('joined()')
    cards = lua.eval('(function() local t = {} for _, f in ipairs(texts) do if rawget(f, "text") then t[#t+1] = tostring(f.text) end end return table.concat(t, "|") end)()')
    assert "Emblem of Frost" in cards and "|Gold|" in cards and "DAILY BONUS" in cards, "reward cards render"

    # A locked random can't be selected.
    lua.execute('''
calls = {}
for _, f in ipairs(frames) do
    if rawget(f, "label") and f.label.text == "Random Lich King Heroic" and f.scripts.OnClick then f.scripts.OnClick(f) end
end
local q = button("Find Group"); q.scripts.OnClick(q)''')
    assert lua.eval('joined()') == "clear | set,262 | join", "a locked random is not queued: " + lua.eval('joined()')

    # Specific: the out-levelled dungeon is hidden, ticking toggles, only enabled unlocked ones queue.
    lua.execute('calls = {}; local s = button("Specific Dungeons"); s.scripts.OnClick(s)')
    names = lua.eval('(function() local t = {} for _, f in ipairs(texts) do if f.shown ~= false and rawget(f, "text") then t[#t+1] = f.text end end return table.concat(t, "|") end)()')
    assert "Deadmines" not in names, "out-levelled dungeons are hidden"
    lua.execute('''
for _, f in ipairs(frames) do
    local d = rawget(f, "data")
    if d and d.id == 11 then f.scripts.OnClick(f) end
end''')
    assert lua.eval('enabledState[11]') is True, "ticking The Nexus enables it"
    lua.execute('calls = {}; local q = button("Find Group"); q.scripts.OnClick(q)')
    queued = lua.eval('joined()')
    assert queued.startswith("clear") and queued.endswith("join") and "set,10" in queued and "set,11" in queued \
        and "set,12" in queued and "set,13" not in queued, queued

    # While queued the button leaves the queue.
    lua.execute('mode = "queued"; event("LFG_UPDATE"); calls = {}; local q = button("Leave Queue"); q.scripts.OnClick(q)')
    assert lua.eval('joined()') == "leave", lua.eval('joined()')

    # The Manastorm tab embeds the Manastorm window; its close closes the finder.
    lua.execute('mode = nil; local t = find(function(f) return rawget(f, "label") and f.label.text == "Manastorm" and f.SetSelected end); t.scripts.OnClick(t)')
    assert lua.eval('embedded') is not None, "the Manastorm tab embeds the Manastorm window"
    lua.execute('closeEmbedded()')
    assert not lua.eval('EsteriaFinderFrame.shown'), "the Manastorm X closes the Adventure Finder"

    # /manastorm and its binding land on the Manastorm tab.
    lua.execute('embedded = nil; EsteriaFinder_Toggle("Manastorm")')
    assert lua.eval('EsteriaFinderFrame.shown') and lua.eval('embedded') is not None
    print("EsteriaFinder: all checks passed")


if __name__ == "__main__":
    main()
