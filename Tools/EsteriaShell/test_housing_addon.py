"""Runs the EsteriaHousing addon against the creator skin test's fake widget layer and a fake Housing extension, and
checks the plot window (buy, enter), the House Editor (click-to-place, select, turn, store, errors), Layout mode
(a room built through a free wall) and the Neighborhood window with its stock and DragonUI buttons."""
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
function W:Enable() self._disabled = false end
function W:Disable() self._disabled = true end
function W:IsEnabled() return not self._disabled end
function W:Click(button) local f = self._scripts.OnClick if f then f(self, button or "LeftButton") end end
SlashCmdList, UISpecialFrames, toasts, sent, StaticPopupDialogs = {}, {}, {}, {}, {}
tinsert = table.insert
YES, NO = "Yes", "No"
UIParent = W.new("UIParent")
GameTooltip = W.new("GameTooltip")
STANDARD_TEXT_FONT = "Fonts\FRIZQT__.TTF"
UIErrorsFrame = { AddMessage = function(_, m) toasts[#toasts + 1] = m end }
-- like the client's: the dialog text is formatted with two values at most, so a third %s errors
function StaticPopup_Show(which, a1, a2, data)
  shownPopup = which
  popupText = string.format(StaticPopupDialogs[which].text, a1, a2)
  StaticPopupDialogs[which].OnAccept({ data = data }, data)
end
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
function EsteriaHousing_GetCursorRay() if ray then return unpack(ray) end end
function GetCursorPosition() return 500, 400 end
function UIParent:GetEffectiveScale() return 1 end
function EsteriaHousing_SetGizmo(...) gizmo = { ... } end
function EsteriaHousing_PinGhost(...) pinned = { ... } end
function EsteriaHousing_ShowPlotBounds(on) plotBounds = on end
function EsteriaHousing_GetGhost() if ghost and cursor then return cursor[1], cursor[2], math.abs(cursor[3] - pz) > 4 and pz or cursor[3] end end
local function log(...) sent[#sent + 1] = { ... } return state == nil or state.isOwner end
function EsteriaHousing_Place(...) return log("place", ...) end
function EsteriaHousing_Move(...) return log("move", ...) end
function EsteriaHousing_Remove(...) return log("remove", ...) end
function EsteriaHousing_Leave() return log("leave") end
function EsteriaHousing_GetNeighborhood() return neighborhood end
function EsteriaHousing_RequestNeighborhood() return log("nrequest") end
function EsteriaHousing_TravelNeighborhood() return log("ntravel") end
function EsteriaHousing_SetHouseAccess(...) return log("access", ...) end
function EsteriaHousing_TravelBack() return log("nback") end
function EsteriaHousing_TravelPlot(...) return log("nplot", ...) end
inCombat, dead, onTaxi, inInstance, instanceKind = false, false, false, nil, "none"
function UnitAffectingCombat() return inCombat end
function UnitIsDeadOrGhost() return dead end
function UnitOnTaxi() return onTaxi end
function IsInInstance() return inInstance, instanceKind end
function UnitName() return "Tester" end
function PlaySound() end
vehicleHooks = {}
function hooksecurefunc(name, fn) if name == "VehicleMenuBar_MoveMicroButtons" then vehicleHooks[#vehicleHooks + 1] = fn end end
MainMenuBarArtFrame = W.new("MainMenuBarArtFrame")
HelpMicroButton = W.new("HelpMicroButton")
function EsteriaHousing_BuyPlot() sent[#sent + 1] = { "buy" } return true end
function EsteriaHousing_EnterPlot() sent[#sent + 1] = { "enter" } return true end
function EsteriaHousing_AddRoom(...) return log("addroom", ...) end
function EsteriaHousing_RemoveRoom() return log("removeroom") end
function EsteriaHousing_StyleRoom(...) return log("styleroom", ...) end
function EsteriaHousing_SetCeilingHidden(...) return log("ceiling", ...) end
function EsteriaHousing_StairsUp(...) return log("stairsup", ...) end
function EasyMenu(items) lastMenu = items end
function EsteriaHousing_BuyDecor(...) return log("buydecor", ...) end
function EsteriaHousing_Upgrade(...) return log("upgrade", ...) end
function EsteriaHousing_SetPlotStyle(...) return log("plotstyle", ...) end
function EsteriaHousing_SetPlotPart(...) return log("plotpart", ...) end
function EsteriaHousing_PlacePlotHouse(...) return log("plotplace", ...) end
function ClearOverrideBindings() end
function SetOverrideBindingClick() end
function ReloadUI() end
function W:SetFontString(fs) self._fs = fs end
function W:GetFontString() return self._fs end
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
for name in ("Atlas.lua", "Gizmo.lua", "Editor.lua", "Layout.lua", "Plot.lua", "Neighborhood.lua"):
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
assert "Buy plot 4 in Founder's Point" in g.popupText, g.popupText
# Bought: the same window now enters the house.
L.execute('plot.status = "yours"; plot.message = "Welcome home!"')
g.event("ESTERIA_HOUSING_PLOT")
# Your plot: the style menu restyles the house, the pad moves and turns it, clamped to its reach.
L.execute('''plot.style, plot.dx, plot.dy, plot.turn, plot.reach = 92, 7.5, 0, 0, 8
plot.styles = { { id = 92, name = "Human House (Small)" }, { id = 552, name = "Night Elf House (Small)" } }''')
g.event("ESTERIA_HOUSING_PLOT")
g.EsteriaHousingPlotStyle.Click(g.EsteriaHousingPlotStyle)
list(g.lastMenu.values())[2].func()
assert last() == ["plotstyle", 552], last()
# House parts: a shape picks itself; one with colours opens them, each its own component.
L.execute('''plot.parts = { { type = 11, chosen = 1380, options = { { id = 1380, parent = 0, name = "Simple Entryway - Ocean" },
  { id = 1381, parent = 1380, name = "Simple Entryway - Mustard" }, { id = 1419, parent = 0, name = "Square Entryway" } } } }''')
g.event("ESTERIA_HOUSING_PLOT")
assert g.EsteriaHousingPlotPart1._text == "Simple Entryway - Ocean", g.EsteriaHousingPlotPart1._text
g.EsteriaHousingPlotPart1.Click(g.EsteriaHousingPlotPart1)
L.execute('assert(lastMenu[2].text == "Simple Entryway" and lastMenu[2].checked and lastMenu[2].hasArrow); lastMenu[2].menuList[2].func()')
assert last() == ["plotpart", 11, 1381], last()
L.execute('lastMenu[3].func()')
assert last() == ["plotpart", 11, 1419], last()
g.EsteriaHousingPlotNorth.Click(g.EsteriaHousingPlotNorth)
assert last() == ["plotplace", 8, 0, 0, 0, 1], last()  # 8.5 would be past the reach: held at 8
g.EsteriaHousingPlotTurnLeft.Click(g.EsteriaHousingPlotTurnLeft)
assert last() == ["plotplace", 7.5, 0, 15, 0, 1], last()  # from where the server last put it
# Up raises it a yard; + makes it 5% bigger; both keep where it stands.
L.execute('plot.dz, plot.scale = 1, 1.2')
g.EsteriaHousingPlotUp.Click(g.EsteriaHousingPlotUp)
assert last() == ["plotplace", 7.5, 0, 0, 2, 1.2], last()
g.EsteriaHousingPlotBigger.Click(g.EsteriaHousingPlotBigger)
assert last() == ["plotplace", 7.5, 0, 0, 1, 1.25], last()
click("Enter House")
assert last() == ["enter"] and not g.EsteriaHousingPlotFrame.IsShown(g.EsteriaHousingPlotFrame)

# The owner inside: the controls bar shows; Decorate opens the decor chest (Storage) and the ability bar.
L.execute('''catalog = { { decorId = 498, name = "Folk Rug", available = 5, placed = 0, category = 2, fileDataId = 111, price = 20000 },
                         { decorId = 496, name = "Folk Table", available = 0, placed = 5, category = 1, fileDataId = 222, price = 50000 },
                         { decorId = 497, name = "Folk Long Table", available = 0, placed = 0, category = 1, fileDataId = 333, price = 50000 } }
state = { inHouse = true, isOwner = true, houseId = 1, revision = 0, placed = 0, maxDecor = 50, pending = 0, decor = {} }''')
g.event("ESTERIA_HOUSING_CATALOG")
g.event("ESTERIA_HOUSING_STATE")
assert g.EsteriaHousingControls.IsShown(g.EsteriaHousingControls) and g.EsteriaHousingBudget.IsShown(g.EsteriaHousingBudget)
g.EsteriaHousingDecorate.Click(g.EsteriaHousingDecorate)
assert g.EsteriaHousingDecor.IsShown(g.EsteriaHousingDecor) and g.EsteriaHousingAbilities.IsShown(g.EsteriaHousingAbilities)
def tile_ids():
    return sorted(o.entry.decorId for o in g.created.values() if o.entry and o._shown)
assert tile_ids() == [496, 498], "storage lists what you own (placed or not), not the whole catalog"

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

# Expert (2): the ack's decor is selected and its handles drag it. The cursor's ray is faked; the handles' size comes
# from the camera distance (about 10 yd here, so radius 1.2).
L.execute('state.decor = { { placedId = 7, decorId = 498, x = 12, y = 21, z = 5, yaw = 285, qx = 0, qy = 0, qz = math.sin(math.rad(285) / 2), qw = math.cos(math.rad(285) / 2), scale = 1 } }; state.placed = 1')
g.event("ESTERIA_HOUSING_RESULT", 0, 1, 7)
g.EsteriaHousingAbility2.Click(g.EsteriaHousingAbility2)
assert g.EsteriaHousingExpert.IsShown(g.EsteriaHousingExpert) and catcher.IsShown(catcher)

def drag(start, end):
    """Hover with the ray `start`, press, move to `end`, release."""
    L.execute(f"ray = {{ {', '.join(map(str, start))} }}")
    catcher._scripts.OnUpdate(catcher)
    catcher._scripts.OnMouseDown(catcher, "LeftButton")
    L.execute(f"ray = {{ {', '.join(map(str, end))} }}")
    catcher._scripts.OnUpdate(catcher)
    catcher._scripts.OnMouseUp(catcher, "LeftButton")
    catcher._scripts.OnClick(catcher, "LeftButton")  # the press that dragged a handle selects nothing

def heading(q):
    import math
    return math.degrees(math.atan2(2 * (q[3] * q[2] + q[0] * q[1]), 1 - 2 * (q[1] ** 2 + q[2] ** 2))) % 360

# Translate: grab the X arrow 0.6 yd out from straight above, drag 1 yd along X.
drag((12.6, 21, 15, 12.6, 21, -5), (13.6, 21, 15, 13.6, 21, -5))
op, placed, x, y, z, qx, qy, qz, qw, scale = last()
assert (op, placed) == ("move", 7) and abs(x - 13) < 1e-6 and (y, z, scale) == (21, 5, 1), last()
assert abs(heading((qx, qy, qz, qw)) - 285) < 1e-3
# Rotate: grab the Z ring on its +X side, drag a quarter turn round to +Y: the heading goes from 285 to 15.
g.EsteriaHousingExpertRotate.Click(g.EsteriaHousingExpertRotate)
drag((13.2, 21, 15, 13.2, 21, -5), (12, 22.2, 15, 12, 22.2, -5))
op, placed, x, y, z, qx, qy, qz, qw, scale = last()
assert op == "move" and abs(heading((qx, qy, qz, qw)) - 15) < 1e-3, heading((qx, qy, qz, qw))
# Scale: grab the handle above (1.6 yd up) with a level ray and drag it to 3.2 yd: twice the size, the most allowed.
g.EsteriaHousingExpertScale.Click(g.EsteriaHousingExpertScale)
drag((2, 21, 6.6, 22, 21, 6.6), (2, 21, 8.2, 22, 21, 8.2))
assert last()[9] == 2, last()
# Reset levels it and sets the size back; Store puts it away.
L.execute('state.decor[1].qx, state.decor[1].qw, state.decor[1].scale = 0.3, 0.95, 1.5')
g.event("ESTERIA_HOUSING_STATE")
g.EsteriaHousingExpertReset.Click(g.EsteriaHousingExpertReset)
op, placed, x, y, z, qx, qy, qz, qw, scale = last()
assert (qx, qy, scale) == (0, 0, 1), last()
g.EsteriaHousingExpertStore.Click(g.EsteriaHousingExpertStore)
assert last() == ["remove", 7]
L.execute('ray = nil')
# Selecting picks the decor nearest the click.
L.execute('cursor = { 30, 30, 5 }')
catcher.Click(catcher)
assert toast() == "No decor there."
L.execute('cursor = { 12.5, 21.5, 5 }')
catcher.Click(catcher)
assert g.EsteriaHousingExpert.IsShown(g.EsteriaHousingExpert)

# Cleanup (4): a click stores the decor under it; right-click goes back to Basic.
g.EsteriaHousingAbility4.Click(g.EsteriaHousingAbility4)
catcher.Click(catcher)
assert last() == ["remove", 7]
catcher.Click(catcher, "RightButton")
assert not catcher.IsShown(catcher)

# Catalog: every decor with its price; a click adds it to the cart, and Buy pays for it all.
L.execute('for _, o in ipairs(created) do if o.key == "catalog" then o._scripts.OnClick(o) end end')
assert tile_ids() == [496, 497, 498]
L.execute('for _, o in ipairs(created) do if o.entry and o.entry.decorId == 497 then o._scripts.OnClick(o); o._scripts.OnClick(o) end end')
assert g.EsteriaHousingCart.IsShown(g.EsteriaHousingCart)
click("Buy  100000c")
assert last() == ["buydecor", 497, 2], last()
assert not g.EsteriaHousingCart.IsShown(g.EsteriaHousingCart)

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
g.EsteriaHousingAbility5.Click(g.EsteriaHousingAbility5)
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

# Customize (3) opens the style menu of the room the player stands in (5, 5 is inside room 1 at the origin).
L.execute('layout.originX, layout.originY = 0, 0; lastMenu = nil; px, py = 5, 5')
g.EsteriaHousingAbility3.Click(g.EsteriaHousingAbility3)
assert g.lastMenu is not None and list(g.lastMenu.values())[0].text == "Square Room (Small)"

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

# House upgrades: the House button opens them; each buys the next step after a confirmation.
L.execute('layout.maxFloors, layout.floorPrice, layout.decorPrice, layout.decorStep, layout.decorCap = 4, 1000000, 100000, 100, 5000')
g.EsteriaHousingControlhouseinfo.Click(g.EsteriaHousingControlhouseinfo)
assert g.EsteriaHousingUpgrades.IsShown(g.EsteriaHousingUpgrades)
L.execute('for _, o in ipairs(created) do if o.question and o.question:find("floor") then o._scripts.OnClick(o) end end')
assert last() == ["upgrade", "floors"], last()
L.execute('for _, o in ipairs(created) do if o.question and o.question:find("decor budget") then o._scripts.OnClick(o) end end')
assert last() == ["upgrade", "decor"], last()

L.execute('layout.roomPrice, layout.roomStep, layout.roomCap = 500000, 4, 64')
g.EsteriaHousingUpgrades.Hide(g.EsteriaHousingUpgrades); g.EsteriaHousingUpgrades.Show(g.EsteriaHousingUpgrades)
L.execute('for _, o in ipairs(created) do if o.question and o.question:find("room limit") then o._scripts.OnClick(o) end end')
assert last() == ["upgrade", "rooms"], last()

# Outdoors on your plot: the editor stays, without Exit, Customize, Layout or Scale.
L.execute('state.outdoors = true')
g.event("ESTERIA_HOUSING_STATE")
assert g.EsteriaHousingControls.IsShown(g.EsteriaHousingControls) and not g.EsteriaHousingControlexit._shown
g.EsteriaHousingAbility5.Click(g.EsteriaHousingAbility5)
assert not g.EsteriaHousingLayout.IsShown(g.EsteriaHousingLayout) and "inside your house" in toast()
g.EsteriaHousingDecorate.Click(g.EsteriaHousingDecorate)
was = L.eval('plotBounds')
g.EsteriaHousingDecorate.Click(g.EsteriaHousingDecorate)
assert was in (True, False) and L.eval('plotBounds') == (not was), "Decorate shows and hides the plot boundary"
# Outdoors, clicking your house (no decor there) selects it; its Z arrow raises it from its spot.
L.execute('''state.house = { spotX = 100, spotY = 200, spotZ = 50, spotYaw = 0, dx = 0, dy = 0, dz = 0, turn = 0, scale = 1, reach = 15 }
state.decor = {}; EsteriaHousingUI.SetEditing(true); EsteriaHousingUI.SetAbility(2); cursor = { 101, 200, 50 }''')
g.event("ESTERIA_HOUSING_STATE")
catcher.Click(catcher)
assert g.EsteriaHousingExpert.IsShown(g.EsteriaHousingExpert), "the house is not selected"
assert any("Your House" in str(o._text) for o in g.created.values() if o._kind == "FontString")
g.EsteriaHousingExpertTranslate.Click(g.EsteriaHousingExpertTranslate)
drag((90, 200, 50.6, 100, 200, 50.6), (90, 200, 52.6, 100, 200, 52.6))
assert last()[:4] == ["plotplace", 0, 0, 0] and abs(last()[4] - 2) < 1e-6 and last()[5] == 1, last()
L.execute('state.outdoors = false; state.house = nil')
g.event("ESTERIA_HOUSING_STATE")

# A visitor sees only Exit; leaving the house hides everything.
L.execute('state.isOwner = false')
g.event("ESTERIA_HOUSING_STATE")
assert not g.EsteriaHousingLayout.IsShown(g.EsteriaHousingLayout) and not g.EsteriaHousingDecorate._shown
g.EsteriaHousingControlexit.Click(g.EsteriaHousingControlexit)
assert last() == ["leave"]
L.execute('state = { inHouse = false, isOwner = false, houseId = 0, revision = 0, placed = 0, maxDecor = 0, pending = 0, decor = {} }')
g.event("ESTERIA_HOUSING_STATE")
assert not g.EsteriaHousingControls.IsShown(g.EsteriaHousingControls)

# The Neighborhood button: on the stock bar after Help, greyed until the realm says housing is available.
frame, micro = g.EsteriaHousingNeighborhoodFrame, g.NeighborhoodMicroButton
assert micro is not None and not g.buttonNamed("Teleport to Neighborhood")
L.execute('available = false')
g.event("ESTERIA_HOUSING_AVAILABLE")
g.EsteriaHousing_ToggleNeighborhood()
assert not frame.IsShown(frame), "opened without housing"
L.execute('available = true')
g.event("ESTERIA_HOUSING_AVAILABLE")

# Opening asks the server; the buttons wait for its answer.
g.EsteriaHousing_ToggleNeighborhood()
assert frame.IsShown(frame) and last() == ["nrequest"]
travel = g.EsteriaHousingNeighborhoodTravel
L.execute('''neighborhood = { name = "Founder's Point", price = 1000000, where = "away", message = "", homePlot = 0, rooms = 0, placed = 0, maxDecor = 0,
  plots = { { plot = 1, status = "private", name = "" }, { plot = 2, status = "sale", name = "" }, { plot = 3, status = "visit", name = "Friend" } } }
for i = 4, 12 do neighborhood.plots[i] = { plot = i, status = "sale", name = "" } end''')
g.event("ESTERIA_HOUSING_NEIGHBORHOOD")

# Teleport is the primary action; a plot's Go passes its number. Occupied plots aren't offered; your group's come first.
travel.Click(travel)
assert last() == ["ntravel"], last()
g.EsteriaHousingNeighborhoodGo1.Click(g.EsteriaHousingNeighborhoodGo1)
assert last() == ["nplot", 3], last()
g.EsteriaHousingNeighborhoodGo2.Click(g.EsteriaHousingNeighborhoodGo2)
assert last() == ["nplot", 2], last()
# 11 listed plots: the second page holds the rest.
g.EsteriaHousingNeighborhoodNext.Click(g.EsteriaHousingNeighborhoodNext)
g.EsteriaHousingNeighborhoodGo1.Click(g.EsteriaHousingNeighborhoodGo1)
assert last() == ["nplot", 8], last()
g.EsteriaHousingNeighborhoodPrev.Click(g.EsteriaHousingNeighborhoodPrev)

# Your home comes first, and inside the neighborhood the primary action leaves it.
L.execute('''neighborhood.homePlot, neighborhood.rooms, neighborhood.placed, neighborhood.maxDecor, neighborhood.where = 2, 3, 12, 200, "neighborhood"
neighborhood.plots[2].status = "yours"
neighborhood.access = { party = true, friends = false, guild = false, everyone = false }''')
g.event("ESTERIA_HOUSING_NEIGHBORHOOD")
assert any("Visitors: party and raid members" in str(o._text) for o in g.created.values() if o._kind == "FontString")
# Visitors: the owner's checkboxes, saved as they are ticked.
g.EsteriaHousingNeighborhoodVisitors.Click(g.EsteriaHousingNeighborhoodVisitors)
L.execute('for _, i in ipairs(lastMenu) do if i.text == "Friends" then i.func() end end')
assert last() == ["access", True, True, False, False], last()
g.EsteriaHousingNeighborhoodGo1.Click(g.EsteriaHousingNeighborhoodGo1)
assert last() == ["nplot", 2], last()
travel.Click(travel)
assert last() == ["nback"], last()
assert travel._text == "Leave Founder's Point", travel._text

# Restricted states grey travel and say why; a housing instance is not "a dungeon".
def blocked(setup, clear, text):
    L.execute(setup)
    g.event("PLAYER_REGEN_DISABLED")
    assert any(text in str(o._text) for o in g.created.values() if o._kind == "FontString"), text
    assert travel.IsEnabled(travel) in (False, 0, None), "travel enabled: " + text
    L.execute(clear)
    g.event("PLAYER_REGEN_ENABLED")
    assert travel.IsEnabled(travel) not in (False, 0, None), "travel still off: " + text

blocked("inCombat = true", "inCombat = false", "while in combat")
blocked("dead = true", "dead = false", "while dead")
blocked("onTaxi = true", "onTaxi = false", "flight path")
blocked('inInstance, instanceKind = 1, "pvp"', 'inInstance, instanceKind = nil, "none"', "battleground")
L.execute('neighborhood.where = "away"')
g.event("ESTERIA_HOUSING_NEIGHBORHOOD")
blocked('inInstance, instanceKind = 1, "party"', 'inInstance, instanceKind = nil, "none"', "dungeon or raid")
L.execute('neighborhood.where = "house"; inInstance, instanceKind = 1, "party"')
g.event("ESTERIA_HOUSING_NEIGHBORHOOD")
assert travel.IsEnabled(travel) not in (False, 0, None), "a house counted as a dungeon"
L.execute('inInstance, instanceKind = nil, "none"')
# The server's refusal shows too.
L.execute('neighborhood.message = "You are already traveling."')
g.event("ESTERIA_HOUSING_NEIGHBORHOOD")
assert any("already traveling" in str(o._text) for o in g.created.values() if o._kind == "FontString")

# A vehicle bar takes the button along with Help.
assert len(g.vehicleHooks) == 1

# Closing closes; a second load (DragonUI's strip made the button first) keeps that button instead of a stock one.
g.EsteriaHousing_ToggleNeighborhood()
assert not frame.IsShown(frame)
L.execute("firstMicro = NeighborhoodMicroButton")
L.execute((ADDON / "Neighborhood.lua").read_text(encoding="utf-8"))
assert L.eval("firstMicro == NeighborhoodMicroButton") and len(g.vehicleHooks) == 1
print("housing addon test passed")
