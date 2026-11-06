"""Runs the EsteriaHousing addon against the creator skin test's fake widget layer and a fake Housing extension, and
checks the panel: catalog drawing, place position/facing, selection, nudge/rotate/remove calls and error toasts."""
import math
import re
from pathlib import Path
from lupa.lua51 import LuaRuntime

REPO = Path(__file__).resolve().parents[2]
GLUE = REPO / "Tools" / "RetailAssets" / "glue"
ADDON = REPO / "Addons" / "EsteriaHousing" / "EsteriaHousing.lua"
# shortcut: same harness slice as test_codex.py; move HARNESS to a shared module if a third test needs it.
HARNESS = re.search(r"HARNESS = r'''(.*?)'''", (GLUE / "test_creator_skin.py").read_text(encoding="utf-8"), re.S).group(1)

GAME = r'''
local W = HarnessWidget
function W:Click() local f = self._scripts.OnClick if f then f(self) end end
for _, name in ipairs({ "SetBackdrop", "SetMovable", "RegisterForDrag", "SetHighlightTexture", "StartMoving",
    "StopMovingOrSizing", "SetJustifyH", "SetWidth" }) do
  if not W[name] then W[name] = function() end end
end
SlashCmdList, UISpecialFrames, toasts, sent = {}, {}, {}, {}
tinsert = table.insert
UIErrorsFrame = { AddMessage = function(_, m) toasts[#toasts + 1] = m end }

available, state, catalog = true, nil, {}
px, py, pz, facing = 10, 20, 5, 90
function EsteriaHousing_IsAvailable() return available end
function EsteriaHousing_GetState() return state end
function EsteriaHousing_GetCatalog() return catalog end
function EsteriaHousing_GetPlayerPosition() return px, py, pz, facing end
function EsteriaHousing_Place(...) sent[#sent + 1] = { "place", ... } return state and state.isOwner end
function EsteriaHousing_Move(...) sent[#sent + 1] = { "move", ... } return state and state.isOwner end
function EsteriaHousing_Remove(...) sent[#sent + 1] = { "remove", ... } return state and state.isOwner end
function EsteriaHousing_Leave() sent[#sent + 1] = { "leave" } return true end
function event(name, ...)
  for _, o in ipairs(created) do local f = o._scripts.OnEvent if f then f(o, name, ...) end end
end
function buttonNamed(text)
  for _, o in ipairs(created) do if o._text == text or (o.GetText and o:GetText() == text) then return o end end
end
'''

L = LuaRuntime()
L.execute(HARNESS + "\nHarnessWidget = W\n")
L.execute(GAME)
L.execute(ADDON.read_text(encoding="utf-8"))
g = L.globals()
frame = g.EsteriaHousingFrame
assert frame is not None, "addon did not build its frame"


def click(text):
    button = g.buttonNamed(text)
    assert button is not None, "no button " + text
    button.Click(button)


def last():
    return list(g.sent[len(g.sent)].values())


# Owner enters: the panel opens and lists the catalog.
L.execute('''catalog = { { decorId = 498, name = "Folk Rug", available = 5, placed = 0 },
                         { decorId = 496, name = "Folk Table", available = 0, placed = 5 } }
state = { inHouse = true, isOwner = true, houseId = 1, revision = 0, placed = 0, maxDecor = 50, pending = 0, decor = {} }''')
g.event("ESTERIA_HOUSING_CATALOG")
g.event("ESTERIA_HOUSING_STATE")
assert frame.IsShown(frame), "panel should open for the owner"

# Place without a selection is refused locally; with one it goes 2.5 yd in front, facing the player.
click("Place")
assert "Pick a decor" in g.toasts[len(g.toasts)]
L.execute('for _, o in ipairs(created) do if o.decorId == 498 then o._scripts.OnClick(o) end end')
click("Place")
op, decor, x, y, z, yaw = last()
assert (op, decor) == ("place", 498)
assert abs(x - 10) < 1e-6 and abs(y - 22.5) < 1e-6 and z == 5 and yaw == 270, (x, y, z, yaw)

# The ack selects the new placement; nudges and rotations move it relative to the player's facing.
L.execute('state.decor = { { placedId = 7, decorId = 498, x = 10, y = 22.5, z = 5, yaw = 270 } }; state.placed = 1')
g.event("ESTERIA_HOUSING_RESULT", 0, 1, 7)
click("Fwd")
op, placed, x, y, z, yaw = last()
assert (op, placed) == ("move", 7) and abs(x - 10) < 1e-6 and abs(y - 22.75) < 1e-6 and yaw == 270
click("Right")
op, placed, x, y, z, yaw = last()
assert abs(x - 10.25) < 1e-6 and abs(y - 22.5) < 1e-6, (x, y)
click("Up")
assert abs(last()[4] - 5.1) < 1e-6
click("+15")
assert last()[5] == 285
click("Remove")
assert last()[:2] == ["remove", 7]

# Server refusals become toasts; a visitor's edits are refused by the extension and toasted too.
g.event("ESTERIA_HOUSING_RESULT", 9, 2, 0)
assert g.toasts[len(g.toasts)] == "Out of the room's bounds."
g.event("ESTERIA_HOUSING_RESULT", 11, 1, 0)
assert g.toasts[len(g.toasts)] == "The house is full."
L.execute('state.isOwner = false')
g.event("ESTERIA_HOUSING_STATE")
click("Remove")
assert "own house" in g.toasts[len(g.toasts)]

# Leaving the house hides the panel.
L.execute('state = { inHouse = false, isOwner = false, houseId = 0, revision = 0, placed = 0, maxDecor = 0, pending = 0, decor = {} }')
g.event("ESTERIA_HOUSING_STATE")
assert not frame.IsShown(frame)
print("housing addon test passed")
