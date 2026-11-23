-- Engine-backed retail namespaces the shared framework uses, in terms of the 3.3.5a API.

RetailC_UIColor.GetColors = function()
	local list = {}
	for i, c in ipairs(EsteriaRetailUI.colors) do
		list[i] = { baseTag = c[1], color = { r = c[2], g = c[3], b = c[4], a = c[5] } }
	end
	return list
end

-- Colors from the stock tables, as retail ColorMixin objects (RetailCreateColor is defined by the retail files,
-- which load after this one; these run only once the UI calls them).
local function color(r, g, b, a) return RetailCreateColor(r, g, b, a or 1) end
RetailC_ClassColor.GetClassColor = function(classFile)
	local c = RAID_CLASS_COLORS[classFile]
	if c then return color(c.r, c.g, c.b) end
end
RetailC_ColorOverrides.GetColorForQuality = function(quality)
	local c = ITEM_QUALITY_COLORS[quality]
	if c then return color(c.r, c.g, c.b) end
end
RetailC_ColorOverrides.GetColorOverrideInfo = function() return nil end
RetailC_ColorUtil.GenerateTextColorCode = function(c)
	return string.format("ff%02x%02x%02x", (c.r or 0) * 255, (c.g or 0) * 255, (c.b or 0) * 255)
end
RetailC_ColorUtil.WrapTextInColor = function(text, c) return "|c" .. RetailC_ColorUtil.GenerateTextColorCode(c) .. text .. "|r" end
RetailC_ScriptedAnimations.GetAllScriptedAnimationEffects = function() return {} end

-- Piecewise curves (retail LuaCurve): points sorted by x; Linear interpolates, Step holds the previous point.
local CurveMixin = {}
function CurveMixin:SetType(curveType) self.curveType = curveType end
function CurveMixin:GetType() return self.curveType end
function CurveMixin:ClearPoints() self.points = {} end
function CurveMixin:GetPointCount() return #self.points end
function CurveMixin:GetPoint(index) local p = self.points[index] if p then return p.x, p.y end end
function CurveMixin:GetPoints() return self.points end
function CurveMixin:AddPoint(x, y)
	local points = self.points
	local at = #points + 1
	for i, p in ipairs(points) do if x < p.x then at = i break end end
	table.insert(points, at, { x = x, y = y })
end
function CurveMixin:Evaluate(x)
	local points = self.points
	local n = #points
	if n == 0 then return 0 end
	if x <= points[1].x then return points[1].y end
	if x >= points[n].x then return points[n].y end
	for i = 2, n do
		local a, b = points[i - 1], points[i]
		if x <= b.x then
			if self.curveType == Enum.LuaCurveType.Step then return a.y end
			return a.y + (b.y - a.y) * (x - a.x) / (b.x - a.x)
		end
	end
end
RetailC_CurveUtil.CreateCurve = function()
	local curve = { points = {}, curveType = Enum.LuaCurveType.Linear }
	for k, v in pairs(CurveMixin) do curve[k] = v end
	return curve
end
RetailC_CurveUtil.CreateColorCurve = RetailC_CurveUtil.CreateCurve

-- Timers on one OnUpdate frame.
do
	local timers, frame = {}, CreateFrame("Frame")
	frame:SetScript("OnUpdate", function()
		local now = GetTime()
		for i = #timers, 1, -1 do
			local t = timers[i]
			if t.cancelled then
				table.remove(timers, i)
			elseif now >= t.due then
				t.runs = t.runs + 1
				if t.iterations and t.runs >= t.iterations then table.remove(timers, i) else t.due = now + t.duration end
				local ok, err = pcall(t.callback, t.handle)
				if not ok then geterrorhandler()(err) end
			end
		end
	end)
	local Handle = {}
	Handle.__index = Handle
	function Handle:Cancel() self.timer.cancelled = true end
	function Handle:IsCancelled() return self.timer.cancelled == true end
	local function start(duration, callback, iterations)
		local timer = { duration = duration, due = GetTime() + duration, callback = callback, iterations = iterations, runs = 0 }
		timer.handle = setmetatable({ timer = timer }, Handle)
		timers[#timers + 1] = timer
		return timer.handle
	end
	RetailC_Timer.After = function(duration, callback) start(duration, callback, 1) end
	RetailC_Timer.NewTimer = function(duration, callback) return start(duration, callback, 1) end
	RetailC_Timer.NewTicker = function(duration, callback, iterations) return start(duration, callback, iterations) end
end

RetailC_CVar.GetCVar = GetCVar
RetailC_CVar.SetCVar = function(name, value) SetCVar(name, value) return true end
RetailC_CVar.GetCVarBool = function(name) return GetCVarBool(name) and true or false end
RetailC_CVar.GetCVarDefault = GetCVarDefault
RetailC_CVar.RegisterCVar = function(name, value) RegisterCVar(name, value) end
-- Tutorial flags (closedInfoFrames) are named by engine constants that may be missing here: an unknown flag reads
-- as unset and is not stored.
RetailC_CVar.GetCVarBitfield = function(name, index)
	if not index then return false end
	return bit.band(tonumber(GetCVar(name)) or 0, bit.lshift(1, index - 1)) ~= 0
end
RetailC_CVar.SetCVarBitfield = function(name, index, value)
	if not index then return false end
	local mask, current = bit.lshift(1, index - 1), tonumber(GetCVar(name)) or 0
	SetCVar(name, value and bit.bor(current, mask) or bit.band(current, bit.bnot(mask)))
	return true
end

RetailC_Texture.GetAtlasInfo = RetailGetAtlasInfo
RetailC_Texture.GetAtlasExists = function(name) return RetailGetAtlasInfo(name) ~= nil end
RetailC_Texture.GetFilenameFromFileDataID = function(id) return "retail/" .. id .. ".blp" end

RetailC_XMLUtil.GetTemplateInfo = GetTemplateInfo
RetailC_XMLUtil.GetTemplates = function() return {} end

RetailC_Glue.IsOnGlueScreen = function() return false end

-- Battle.net client icons ("title icons") from retail's own files: the game a friend is in, or the launcher.
do
	local icons = {
		WoW = { medium = "retail/374212.blp", small = "retail/511913.blp" },  -- battlenet-wowicon, ui-chaticon-wow
		App = { medium = "retail/796351.blp", small = "retail/841277.blp" },  -- battlenet-battleneticon, ui-chaticon-battlenet
	}
	local function path(program, version)
		local set = icons[program] or icons.App
		return version == Enum.TitleIconVersion.Small and set.small or set.medium
	end
	RetailC_Texture.SetTitleIconTexture = function(texture, program, version) texture:SetTexture(path(program, version)) end
	RetailC_Texture.GetTitleIconTexture = function(program, version, callback) callback(true, path(program, version)) end
	RetailC_Texture.IsTitleIconTextureReady = function() return true end
end
