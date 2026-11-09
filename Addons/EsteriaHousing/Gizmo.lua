-- Esteria housing: Expert mode's handles, as retail's (Translate, Rotate, Scale). The extension draws them
-- (EsteriaHousing_SetGizmo); this file picks and drags them against the cursor's ray, previews the result on the ghost,
-- and sends one edit when the mouse is let go. Snaps: 0.25 yd, 15 degrees, x0.05; Shift turns snapping off.
-- Axes are the world's (Blender's Global): X red, Y green, Z blue.

if not EsteriaHousing_IsAvailable then return end
EsteriaHousingUI = EsteriaHousingUI or {}
local H = EsteriaHousingUI
local G = {}
H.Gizmo = G

local MIN_SCALE, MAX_SCALE = 0.5, 2.0
local AXES = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } }

-- ---------------------------------------------------------------------------------------------- vectors, quaternions

local function add(a, b) return { a[1] + b[1], a[2] + b[2], a[3] + b[3] } end
local function sub(a, b) return { a[1] - b[1], a[2] - b[2], a[3] - b[3] } end
local function mul(a, s) return { a[1] * s, a[2] * s, a[3] * s } end
local function dot(a, b) return a[1] * b[1] + a[2] * b[2] + a[3] * b[3] end
local function len(a) return math.sqrt(dot(a, a)) end
local function norm(a) local l = len(a); return l > 1e-9 and mul(a, 1 / l) or a end

-- quaternions are { x, y, z, w }
local function qmul(a, b)
  return { a[4] * b[1] + a[1] * b[4] + a[2] * b[3] - a[3] * b[2], a[4] * b[2] - a[1] * b[3] + a[2] * b[4] + a[3] * b[1],
    a[4] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[4], a[4] * b[4] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3] }
end
local function qaxis(axis, radians)
  local s = math.sin(radians / 2)
  return { axis[1] * s, axis[2] * s, axis[3] * s, math.cos(radians / 2) }
end
local function qnorm(q)
  local l = math.sqrt(q[1] ^ 2 + q[2] ^ 2 + q[3] ^ 2 + q[4] ^ 2)
  return { q[1] / l, q[2] / l, q[3] / l, q[4] / l }
end
local function heading(q) return math.atan2(2 * (q[4] * q[3] + q[1] * q[2]), 1 - 2 * (q[2] ^ 2 + q[3] ^ 2)) end
G.qmul, G.qaxis, G.heading = qmul, qaxis, heading

local function snap(value, step) return IsShiftKeyDown() and value or math.floor(value / step + 0.5) * step end

-- ---------------------------------------------------------------------------------------------- geometry

-- The cursor's ray: origin and unit direction.
local function Ray()
  local sx, sy, sz, ex, ey, ez = EsteriaHousing_GetCursorRay()
  if not sx then return end
  return { sx, sy, sz }, norm({ ex - sx, ey - sy, ez - sz })
end

-- Closest approach of the ray (o, d) to the line (p, a): the line's parameter, the ray's, and the gap between them.
function G.LineRay(p, a, o, d)
  local w = sub(p, o)
  local b, dd, e = dot(a, d), dot(a, w), dot(d, w)
  local denom = 1 - b * b
  if denom < 1e-6 then return end
  local s, t = (b * e - dd) / denom, (e - b * dd) / denom
  return s, t, len(sub(add(p, mul(a, s)), add(o, mul(d, t))))
end

-- Where the ray meets the plane through c with normal a, and that point's angle about a (from the ring's u toward v).
function G.PlaneRay(c, axis, o, d)
  local a = AXES[axis]
  local denom = dot(d, a)
  if math.abs(denom) < 1e-4 then return end
  local t = dot(sub(c, o), a) / denom
  if t < 0 then return end
  local hit = add(o, mul(d, t))
  local r = sub(hit, c)
  local u, v = AXES[axis % 3 + 1], AXES[(axis + 1) % 3 + 1]
  return t, len(r), math.atan2(dot(r, v), dot(r, u))
end

-- ---------------------------------------------------------------------------------------------- state

local mode = 1          -- 1 translate, 2 rotate, 3 scale
local target            -- the selection: { placedId, pos, q, scale, fdid, commit, yawOnly, minScale, maxScale }
local hover, drag       -- axis under the cursor; the drag in progress
local radius = 1.2

function G.SetMode(m) mode = m; drag = nil end
function G.Mode() return mode end
function G.Dragging() return drag ~= nil end

-- What the handles act on, or nil: a GetState().decor entry, or anything shaped like one (x y z, yaw or qx..qw, scale)
-- with commit(pos, q, scale) to send it instead of a decor move, yawOnly to offer only the Z ring, minScale/maxScale.
function G.SetTarget(d, fdid)
  if not d then
    target, drag, hover = nil, nil, nil
    EsteriaHousing_SetGizmo(0)
    return
  end
  if drag and target and target.placedId == d.placedId then return end -- the server echoed our own edit mid-drag
  target = { placedId = d.placedId, decorId = d.decorId, pos = { d.x, d.y, d.z },
    q = { d.qx or 0, d.qy or 0, d.qz or math.sin(math.rad(d.yaw) / 2), d.qw or math.cos(math.rad(d.yaw) / 2) },
    scale = d.scale or 1, fdid = fdid, commit = d.commit, yawOnly = d.yawOnly, minScale = d.minScale or MIN_SCALE,
    maxScale = d.maxScale or MAX_SCALE }
end

-- Sends a transform for the target: its own commit, or a decor move.
local function Commit(pos, q, scale)
  if target.commit then return target.commit(pos, q, scale) end
  return (H.Move or EsteriaHousing_Move)(target.placedId, pos[1], pos[2], pos[3], q[1], q[2], q[3], q[4], scale)
end

local function Pick(o, d)
  if not target then return end
  local c, best, bestT = target.pos, nil, math.huge
  local reach = math.max(0.15, radius * 0.12)
  if mode == 1 then
    for axis = 1, 3 do
      local s, t, gap = G.LineRay(c, AXES[axis], o, d)
      if s and s >= 0 and s <= radius * 1.3 and gap < reach and t < bestT then best, bestT = axis, t end
    end
  elseif mode == 2 then
    for axis = target.yawOnly and 3 or 1, 3 do
      local t, r = G.PlaneRay(c, axis, o, d)
      if t and math.abs(r - radius) < reach and t < bestT then best, bestT = axis, t end
    end
  else
    local s, t, gap = G.LineRay(c, AXES[3], o, d)
    if s and s >= radius * 1.15 and s <= radius * 1.5 and gap < reach * 1.5 then best = 4 end
  end
  return best
end

-- The transform the drag gives now: pos, q, scale.
local function Dragged(o, d)
  local c = drag.pos
  if drag.axis <= 3 and mode == 1 then
    local s = G.LineRay(c, AXES[drag.axis], o, d)
    if not s then return drag.pos, drag.q, drag.scale end
    return add(c, mul(AXES[drag.axis], snap(s - drag.s0, 0.25))), drag.q, drag.scale
  elseif mode == 2 then
    local _, _, angle = G.PlaneRay(c, drag.axis, o, d)
    if not angle then return drag.pos, drag.q, drag.scale end
    local turn = snap(math.deg(angle - drag.a0), 15)
    return drag.pos, qnorm(qmul(qaxis(AXES[drag.axis], math.rad(turn)), drag.q)), drag.scale
  else
    local s = G.LineRay(c, AXES[3], o, d)
    if not s or drag.s0 <= 0 then return drag.pos, drag.q, drag.scale end
    local scale = math.max(target.minScale, math.min(target.maxScale, snap(drag.scale * s / drag.s0, 0.05)))
    return drag.pos, drag.q, scale
  end
end

-- Every frame while Expert mode has decor selected: hover, or the drag's preview on the ghost.
function G.Update()
  if not target then return EsteriaHousing_SetGizmo(0) end
  local o, d = Ray()
  if o then
    radius = math.max(0.8, math.min(3.0, len(sub(target.pos, o)) * 0.12))
    if drag then
      local pos, q, scale = Dragged(o, d)
      drag.now = { pos = pos, q = q, scale = scale }
      if target.fdid then -- a preview where there is a model for one
        EsteriaHousing_PinGhost(pos[1], pos[2], pos[3])
        EsteriaHousing_SetGhost(target.fdid, q[1], q[2], q[3], q[4], scale * ((H.BASE_SCALE or {})[target.decorId] or 1))
      end
    else
      hover = Pick(o, d)
    end
  end
  local p = drag and drag.now and drag.now.pos or target.pos
  EsteriaHousing_SetGizmo(mode, p[1], p[2], p[3], radius, drag and drag.axis or hover or 0, drag ~= nil)
end

-- Left button down: grab the handle under the cursor. True when a handle took the click.
function G.MouseDown()
  if not target or not hover then return false end
  local o, d = Ray()
  if not o then return false end
  drag = { axis = hover, pos = target.pos, q = target.q, scale = target.scale }
  if mode == 1 or mode == 3 then
    drag.s0 = G.LineRay(target.pos, AXES[mode == 1 and hover or 3], o, d) or 0
  else
    local _, _, angle = G.PlaneRay(target.pos, hover, o, d)
    drag.a0 = angle or 0
  end
  return true
end

-- Left button up: send the drag's transform as one edit.
function G.MouseUp()
  if not drag then return false end
  local now = drag.now
  drag = nil
  EsteriaHousing_PinGhost()
  EsteriaHousing_SetGhost()
  if now then
    target.pos, target.q, target.scale = now.pos, now.q, now.scale
    if not Commit(now.pos, now.q, now.scale) then H.Toast("You can only edit your own house.") end
  end
  return true
end

-- The wheel turns the selection about Z; Reset keeps its heading, levels it and sets its size back.
function G.Turn(degrees)
  if not target then return end
  return Commit(target.pos, qnorm(qmul(qaxis(AXES[3], math.rad(degrees)), target.q)), target.scale)
end

function G.Reset()
  if not target then return end
  local h = heading(target.q)
  return Commit(target.pos, { 0, 0, math.sin(h / 2), math.cos(h / 2) }, 1)
end
