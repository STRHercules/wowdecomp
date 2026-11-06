"""Runs the EsteriaHousing addon against the creator skin test's fake widget layer and a fake Housing extension, and
checks the plot window (buy, enter), the House Editor (click-to-place, select, turn, store, errors) and Layout mode
(a room built through a free wall)."""
import re
from pathlib import Path
from lupa.lua51 import LuaRuntime

REPO = Path(__file__).resolve().parents[2]
GLUE = REPO / "Tools" / "RetailAssets" / "glue"
ADDON = REPO / "Addons" / "EsteriaHousing"
# shortcut: same harness slice as test_codex.py; move HARNESS to a shared module if a third test needs it.
HARNESS = re.search(r"HARNESS = r'''(.*?)'''", (GLUE / "test_creator_skin.py").read_text(encoding="utf-8"), re.S).group(1)

GAME = r'''
local W = HarnessWidget
-- OnShow/OnHide fire like the client's, on a real change only
function W:Show() local was = self._shown; self._shown = true; if not was and self._scripts.OnShow then self._scripts.OnShow(self) end end
function W:Hide() local was = self._shown; self._shown = false; if was and self._scripts.OnHide then self._scripts.OnHide(self) end end
function W:Click(button) local f = self._scripts.OnClick if f then f(self, button or "LeftButton") end end
SlashCmdList, UISpecialFrames, toasts, sent, StaticPopupDialogs = {}, {}, {}, {}, {}
tinsert = table.insert
YES, NO = "Yes", "No"
UIParent = W.new("UIParent")
GameTooltip = W.new("GameTooltip")
STANDARD_TEXT_FONT = "Fonts\FRIZQT__.TTF"
UIErrorsFrame = { AddMessage = function(_, m) toasts[#toasts + 1] = m end }
function StaticPopup_Show(which, ...) shownPopup = which; StaticPopupDialogs[which].OnAccept() end
function StaticPopup_Hide() end
function GetCoinTextureString(c) return c .. "c" end
function IsShiftKeyDown() return shift end
money = 2000000
function GetMoney() return money end

available, state, catalog, plot, layout = true, nil, {}, nil, nil
px, py, pz, facing = 10, 20, 5, 90
cursor = nil
function EsteriaHousing_IsAvailable() return available end
function EsteriaHousing_GetState() return state end
function EsteriaHousing_GetCatalog() return catalog end
function EsteriaHousing_GetPlot() return plot end
function EsteriaHousing_GetLayout() return layout end
function EsteriaHousing_GetPlayerPosition() return px, py, pz, facing end
function EsteriaHousing_GetCursorWorld() if cursor then return cursor[1], cursor[2], cursor[3] end end
function EsteriaHousing_SetGhost(fdid, yaw) ghost = fdid and { fdid, yaw } or nil end
function EsteriaHousing_GetGhost() if ghost and cursor then return cursor[1], cursor[2], math.abs(cursor[3] - pz) > 4 and pz or cursor[3] end end
local function log(...) sent[#sent + 1] = { ... } return state == nil or state.isOwner end
function EsteriaHousing_Place(...) return log("place", ...) end
function EsteriaHousing_Move(...) return log("move", ...) end
function EsteriaHousing_Remove(...) return log("remove", ...) end
function EsteriaHousing_Leave() return log("leave") end
function EsteriaHousing_BuyPlot() sent[#sent + 1] = { "buy" } return true end
function EsteriaHousing_EnterPlot() sent[#sent + 1] = { "enter" } return true end
function EsteriaHousing_AddRoom(...) return log("addroom", ...) end
function EsteriaHousing_RemoveRoom() return log("removeroom") end
function EsteriaHousing_StyleRoom(...) return log("styleroom", ...) end
function EsteriaHousing_SetCeilingHidden(...) return log("ceiling", ...) end
function EsteriaHousing_StairsUp(...) return log("stairsup", ...) end
function EasyMenu(items) lastMenu = items end
function CloseDropDownMenus() end
CANCEL = "Cancel"
function event(name, ...)
  for _, o in ipairs(created) do local f = o._scripts.OnEvent if f then f(o, name, ...) end end
end
function buttonNamed(text)
  for _, o in ipairs(created) do if o._text == text and o._kind == "Button" then return o end end
end
'''

L = LuaRuntime()
L.execute(HARNESS + "\nHarnessWidget = W\n")
L.execute(GAME)
for name in ("Editor.lua", "Layout.lua", "Plot.lua"):
    L.execute((ADDON / name).read_text(encoding="utf-8"))
g = L.globals()


def click(text, button="LeftButton"):
    b = g.buttonNamed(text)
    assert b is not None, "no button " + text
    b.Click(b, button)


def last():
    return list(g.sent[len(g.sent)].values())


def toast():
    return g.toasts[len(g.toasts)]


# A cornerstone for sale: the window opens with the price; Buy confirms, then asks the server.
L.execute('plot = { plot = 4, status = "sale", price = 1000000, houseElsewhere = false, neighborhood = "Founder\'s Point", owner = "", message = "" }')
g.event("ESTERIA_HOUSING_PLOT")
assert g.EsteriaHousingPlotFrame.IsShown(g.EsteriaHousingPlotFrame)
click("Buy Plot")
assert g.shownPopup == "ESTERIA_HOUSING_BUY_PLOT" and last() == ["buy"]
# Bought: the same window now enters the house.
L.execute('plot.status = "yours"; plot.message = "Welcome home!"')
g.event("ESTERIA_HOUSING_PLOT")
click("Enter House")
assert last() == ["enter"] and not g.EsteriaHousingPlotFrame.IsShown(g.EsteriaHousingPlotFrame)

# The owner inside: the editor bar shows; Decor opens the catalog.
L.execute('''catalog = { { decorId = 498, name = "Folk Rug", available = 5, placed = 0, category = 2, fileDataId = 111 },
                         { decorId = 496, name = "Folk Table", available = 0, placed = 5, category = 1, fileDataId = 222 } }
state = { inHouse = true, isOwner = true, houseId = 1, revision = 0, placed = 0, maxDecor = 50, pending = 0, decor = {} }''')
g.event("ESTERIA_HOUSING_CATALOG")
g.event("ESTERIA_HOUSING_STATE")
assert g.EsteriaHousingBar.IsShown(g.EsteriaHousingBar)
click("Decor")
assert g.EsteriaHousingDecor.IsShown(g.EsteriaHousingDecor)

# A tile with none left is refused; one in storage is picked up and placed where the cursor meets the floor, facing the
# player. A ray that misses the room floor (z far off) is kept at the player's floor.
L.execute('for _, o in ipairs(created) do if o.entry and o.entry.decorId == 496 then o._scripts.OnClick(o) end end')
assert "none of that" in toast()
L.execute('for _, o in ipairs(created) do if o.entry and o.entry.decorId == 498 then o._scripts.OnClick(o) end end')
catcher = g.EsteriaHousingCatcher
assert catcher.IsShown(catcher)
assert list(g.ghost.values()) == [111, 270], "the ghost shows the held decor, facing the player"
catcher._scripts.OnMouseWheel(catcher, 1)  # turn it 15 degrees first
assert g.ghost[2] == 285
L.execute('cursor = { 12, 21, -40 }')
catcher.Click(catcher)
op, decor, x, y, z, yaw = last()
assert (op, decor, x, y, z, yaw) == ("place", 498, 12, 21, 5, 285), (op, decor, x, y, z, yaw)
assert not catcher.IsShown(catcher) and g.ghost is None, "a plain click places one and drops the ghost"

# The ack selects the new decor; the selection bar turns, raises, moves and stores it.
L.execute('state.decor = { { placedId = 7, decorId = 498, x = 12, y = 21, z = 5, yaw = 285 } }; state.placed = 1')
g.event("ESTERIA_HOUSING_RESULT", 0, 1, 7)
assert g.EsteriaHousingSelected.IsShown(g.EsteriaHousingSelected)
click("Turn L")
assert last()[:2] == ["move", 7] and last()[5] == 300
click("Up")
assert abs(last()[4] - 5.1) < 1e-6
click("Move")
L.execute('cursor = { 14, 22, 5.2 }')
catcher.Click(catcher)
assert last()[:5] == ["move", 7, 14, 22, 5.2]
click("Store")
assert last() == ["remove", 7]

# Select mode picks the decor nearest the click; right-click ends it.
click("Select Placed Decor")
L.execute('cursor = { 30, 30, 5 }')
catcher.Click(catcher)
assert toast() == "No decor there."
L.execute('cursor = { 12.5, 21.5, 5 }')
catcher.Click(catcher)
assert g.EsteriaHousingSelected.IsShown(g.EsteriaHousingSelected)
catcher.Click(catcher, "RightButton")
assert not catcher.IsShown(catcher)

# Server refusals become toasts.
g.event("ESTERIA_HOUSING_RESULT", 9, 2, 0)
assert toast() == "That spot is outside your rooms."

# Layout: a free wall's "+" opens a menu of the kinds that fit with their prices (too dear: greyed); picking one builds it.
L.execute('''layout = { maxRooms = 8, originX = 100, originY = 200,
  kinds = { [1] = { id = 1, name = "Square Room (Small)", sizeX = 23, sizeY = 23, price = 250000 },
            [2] = { id = 2, name = "Hallway", sizeX = 23, sizeY = 11, price = 3000000 } },
  rooms = { { kind = 1, x = 0, y = 0, quarter = 0, wallTheme = 3, floorTheme = 3, ceilingTheme = 3, ceilingHidden = false, level = 0 } },
  sockets = { { room = 0, piece = 4, level = 0, x = 12, y = 0, nx = 1, ny = 0, fits = { 1, 2 } } },
  themes = { { id = 1, name = "Folk" }, { id = 3, name = "Generic" } } }''')
click("Layout")
assert g.EsteriaHousingLayout.IsShown(g.EsteriaHousingLayout) and not g.EsteriaHousingDecor.IsShown(g.EsteriaHousingDecor)
L.execute('for _, o in ipairs(created) do if o.socket then o._scripts.OnClick(o) end end')
items = list(g.lastMenu.values())
assert items[1].disabled is False and items[2].disabled is True, "the 300 g hallway is out of reach with 200 g"
items[1].func()
assert last() == ["addroom", 0, 4, 1], last()
# A room's menu styles its walls, floor or ceiling and hides the ceiling.
L.execute('for _, o in ipairs(created) do if o.index == 1 and o.border then o._scripts.OnClick(o) end end')
items = list(g.lastMenu.values())
walls = list(items[1].menuList.values())
walls[0].func()
assert last() == ["styleroom", 0, 1, 1], last()
items[4].func()
assert last() == ["ceiling", 0, True], last()

# Floors: with a stairwell, Floor tabs appear and each floor shows only its own "+" markers.
L.execute('''layout.kinds[10] = { id = 10, name = "Stairwell (Left)", sizeX = 23, sizeY = 23, price = 250000 }
layout.kinds[48] = { id = 48, name = "Stairwell Room (Empty)", sizeX = 23, sizeY = 23, price = 250000 }
layout.rooms[2] = { kind = 10, x = 24, y = 0, quarter = 0, wallTheme = 3, floorTheme = 3, ceilingTheme = 3, level = 0 }
layout.rooms[3] = { kind = 48, x = 24, y = 0, quarter = 0, wallTheme = 3, floorTheme = 3, ceilingTheme = 3, level = 1 }
layout.sockets[2] = { room = 2, piece = 2, level = 1, x = 36, y = 0, nx = 1, ny = 0, fits = { 1 } }''')
g.event("ESTERIA_HOUSING_LAYOUT")
def shown_sockets():
    return sorted(o.socket.level for o in g.created.values() if o.socket and o._shown)
assert shown_sockets() == [0], shown_sockets()
click("Floor 2")
assert shown_sockets() == [1], shown_sockets()
# The stairwell's top offers Extend Stairs Up; a basement adds a Basement tab.
L.execute('for _, o in ipairs(created) do if o.index == 3 and o.border then o._scripts.OnClick(o) end end')
items = list(g.lastMenu.values())
assert "Extend Stairs Up" in items[-1].text
items[-1].func()
assert last() == ["stairsup", 2], last()
L.execute('layout.rooms[4] = { kind = 10, x = 0, y = 24, quarter = 0, wallTheme = 3, floorTheme = 3, ceilingTheme = 3, level = -1 }')
g.event("ESTERIA_HOUSING_LAYOUT")
assert g.buttonNamed("Basement")._shown

# A visitor sees only Leave; leaving the house hides everything.
L.execute('state.isOwner = false')
g.event("ESTERIA_HOUSING_STATE")
assert not g.EsteriaHousingLayout.IsShown(g.EsteriaHousingLayout)
click("Leave House")
assert last() == ["leave"]
L.execute('state = { inHouse = false, isOwner = false, houseId = 0, revision = 0, placed = 0, maxDecor = 0, pending = 0, decor = {} }')
g.event("ESTERIA_HOUSING_STATE")
assert not g.EsteriaHousingBar.IsShown(g.EsteriaHousingBar)
print("housing addon test passed")
