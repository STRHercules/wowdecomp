#!/usr/bin/env python3
"""The mount window's Display tab: fields read in the server's order, and the keys save what they show."""
from pathlib import Path

from lupa import LuaRuntime

ADDON = Path(__file__).resolve().parents[2] / "Addons" / "EsteriaHousing"

HARNESS = r'''
local mt = {}
function Obj() return setmetatable({}, mt) end
listeners, sent, keys, lineValues = {}, {}, {}, {}
mt.__call = function() return Obj() end
mt.__index = function(t, k)
  if k == "SetScript" then return function(self, n, f) rawset(self, n, f); if n == "OnEvent" then listeners[#listeners + 1] = f end end end
  if k == "SetText" then return function(self, s) rawset(self, "text", s) end end
  if k == "GetText" then return function() return "4" end end
  local o = Obj(); rawset(t, k, o); return o
end
setmetatable(_G, { __index = function() return Obj() end })
strjoin = function(s, ...) local p = {}; for i = 1, select("#", ...) do p[i] = tostring(select(i, ...)) end; return table.concat(p, s) end
IsShiftKeyDown = function() return false end
GetSpellInfo = function() return nil end
EsteriaHousing_Commerce = function(s) sent[#sent + 1] = s end
EsteriaHousing_GetCommerce = function() return answer end
local function Fields(line) local f = {}; for v in (line .. "\t"):gmatch("([^\t]*)\t") do f[#f + 1] = v end; return f end
local Kit = setmetatable({ Fields = Fields, Num = function(v) return tonumber(v) or 0 end, Show = function() end, LINE_H = 24, BOX_TOP = -80,
  Line = function(_, i, top, label) local l = Obj(); lineValues[label] = l; return l end,
  Window = function() return Obj() end }, { __index = function() return function() return Obj() end end })
EsteriaHousingUI = setmetatable({ Kit = Kit, PlotKey = function(name, _, _, click) keys[name] = click; return Obj() end },
  { __index = function() return function() return Obj() end end })
'''

L = LuaRuntime()
L.execute(HARNESS)
L.execute((ADDON / "Collections.lua").read_text(encoding="utf-8"))
L.execute(r'''
answer = "mountdisp\nmount\t7\t1\t6\t0\t\t0\t1.50\t2.00\t90\t0.75\t\t\t0.25\t2\t4"
for _, f in ipairs(listeners) do f() end
''')
values = L.eval('''{ lineValues.Position.value.text, lineValues.Height.value.text, lineValues.Rotation.value.text, lineValues.Size.value.text }''')
assert values[1] == "2.00 ahead  0.25 right", values[1]
assert values[2] == "+1.50 yd", values[2]
assert values[3].startswith("90"), values[3]
assert values[4] == "x0.75", values[4]

L.execute("keys.MountAhead()")
sent = L.eval("sent[#sent]").split("\t")
assert sent[:3] == ["col", "mountstyle", "7"], sent
assert [float(v) for v in sent[3:]] == [0, 0.75, 1.5, 3, 90, 0.25, 2, 4], sent  # finish, size, height, forward, turn, side, motion, radius

# Trophies: same keys, a 0.25 yd step, and the server's order (height, forward, turn, size).
L.execute(r'''
answer = "trophy\ntrophy\t9\t1\t2\t0\t0\t\t\t0\t0\t1.10\t0.50\t30\t0.60\t\t\t\t0\t0.25"
for _, f in ipairs(listeners) do f() end
keys.TrophyAhead()
''')
sent = L.eval("sent[#sent]").split("\t")
assert sent[:3] == ["col", "trophystyle", "9"], sent
assert [float(v) for v in sent[3:]] == [0, 0, 0.6, 1.1, 0.75, 30, 0.25], sent  # style, effect, size, height, forward, turn, side
print("mount and trophy tab test passed")
