-- Modern widget methods the retail templates call, added to the 3.3.5a widget types where they are missing.
-- Behaviour the engine has no counterpart for (masks, pixel snapping, mouse propagation) is a no-op.

local function noop() end
local types = {}
do
	local holder = CreateFrame("Frame")
	holder:Hide()
	local function add(kind, object) types[kind] = getmetatable(object).__index end
	for _, kind in ipairs({ "Frame", "Button", "CheckButton", "EditBox", "ScrollFrame", "Slider", "StatusBar", "Cooldown",
			"Model", "PlayerModel", "SimpleHTML", "MessageFrame", "ScrollingMessageFrame", "ColorSelect", "GameTooltip" }) do
		local ok, object = pcall(CreateFrame, kind, nil, holder)
		if ok and object then add(kind, object) end
	end
	add("Texture", holder:CreateTexture())
	add("FontString", holder:CreateFontString())
	add("Font", CreateFont("EsteriaRetailUIFontProbe"))
	local group = holder:CreateAnimationGroup()
	add("AnimationGroup", group)
	for _, kind in ipairs({ "Alpha", "Scale", "Translation", "Rotation", "Animation" }) do
		local ok, anim = pcall(group.CreateAnimation, group, kind)
		if ok and anim then add(kind, anim) end
	end
end

local function provide(kinds, name, func)
	for _, kind in ipairs(kinds) do
		local methods = types[kind]
		if methods and methods[name] == nil then methods[name] = func end
	end
end

local FRAMES = { "Frame", "Button", "CheckButton", "EditBox", "ScrollFrame", "Slider", "StatusBar", "Cooldown", "Model",
	"PlayerModel", "SimpleHTML", "MessageFrame", "ScrollingMessageFrame", "ColorSelect", "GameTooltip" }
local REGIONS = { "Texture", "FontString" }
for _, k in ipairs(FRAMES) do REGIONS[#REGIONS + 1] = k end
local BUTTONS = { "Button", "CheckButton" }
local ANIMS = { "Alpha", "Scale", "Translation", "Rotation", "Animation" }
EsteriaRetailUI.widgetTypes = types

-- Regions ------------------------------------------------------------------------------------------------------------
provide(REGIONS, "SetShown", function(self, shown) if shown then self:Show() else self:Hide() end end)
provide(REGIONS, "GetWindow", function() return nil end) -- nil is the main window
provide(REGIONS, "SetWindow", noop)
provide(REGIONS, "GetDebugName", function(self) return self:GetName() or tostring(self) end)
provide(REGIONS, "IsRectValid", function(self) return self:GetLeft() ~= nil end)
provide(REGIONS, "IsObjectLoaded", function() return true end)
provide(REGIONS, "IsAnchoringRestricted", function() return false end)
provide(REGIONS, "SetIgnoreParentAlpha", noop)
provide(REGIONS, "SetIgnoreParentScale", noop)
provide(REGIONS, "SetSnapToPixelGrid", noop)
provide(REGIONS, "SetTexelSnappingBias", noop)
provide(REGIONS, "SetParentKey", function(self, key, keep)
	local parent = self:GetParent()
	if parent then parent[key] = self end
end)
provide(REGIONS, "GetPointByName", function(self, name)
	for i = 1, self:GetNumPoints() do
		local point, relativeTo, relativePoint, x, y = self:GetPoint(i)
		if point == name then return point, relativeTo, relativePoint, x, y end
	end
end)
provide(REGIONS, "ClearPoint", function(self, name)
	local keep = {}
	for i = 1, self:GetNumPoints() do
		local point, relativeTo, relativePoint, x, y = self:GetPoint(i)
		if point ~= name then keep[#keep + 1] = { point, relativeTo, relativePoint, x, y } end
	end
	self:ClearAllPoints()
	for _, p in ipairs(keep) do self:SetPoint(p[1], p[2], p[3], p[4], p[5]) end
end)
local function offsetPoints(self, dx, dy, absolute)
	local points = {}
	for i = 1, self:GetNumPoints() do points[i] = { self:GetPoint(i) } end
	self:ClearAllPoints()
	for _, p in ipairs(points) do
		self:SetPoint(p[1], p[2], p[3], (absolute and 0 or p[4] or 0) + dx, (absolute and 0 or p[5] or 0) + dy)
	end
end
provide(REGIONS, "AdjustPointsOffset", function(self, dx, dy) offsetPoints(self, dx or 0, dy or 0) end)
provide(REGIONS, "SetPointsOffset", function(self, x, y) offsetPoints(self, x or 0, y or 0, true) end)
provide(REGIONS, "ClearPointsOffset", function(self) offsetPoints(self, 0, 0, true) end)
provide(REGIONS, "GetScaledRect", function(self)
	local left, bottom, width, height = self:GetRect()
	if not left then return end
	local scale = self:GetEffectiveScale()
	return left * scale, bottom * scale, width * scale, height * scale
end)

-- Frames -------------------------------------------------------------------------------------------------------------
provide(FRAMES, "SetMouseMotionEnabled", function(self, enable) if enable then self:EnableMouse(true) end end)
provide(FRAMES, "SetMouseClickEnabled", function(self, enable) self:EnableMouse(enable) end)
provide(FRAMES, "IsMouseMotionEnabled", function(self) return self:IsMouseEnabled() end)
provide(FRAMES, "IsMouseClickEnabled", function(self) return self:IsMouseEnabled() end)
provide(FRAMES, "IsMouseMotionFocus", function(self) return GetMouseFocus() == self end)
provide(FRAMES, "SetPropagateMouseMotion", noop)
provide(FRAMES, "SetPropagateMouseClicks", noop)
provide(FRAMES, "SetPropagateKeyboardInput", noop)
provide(FRAMES, "RegisterForMouse", noop)
provide(FRAMES, "RegisterUnitEvent", function(self, event) self:RegisterEvent(event) end)
provide(FRAMES, "SetFixedFrameStrata", noop)
provide(FRAMES, "SetFixedFrameLevel", noop)
provide(FRAMES, "SetUsingParentLevel", noop)
provide(FRAMES, "SetFlattensRenderLayers", noop)
provide(FRAMES, "SetClipsChildren", noop)
provide(FRAMES, "DoesClipChildren", function() return false end)
provide(FRAMES, "SetHyperlinksEnabled", noop)
provide(FRAMES, "SetResizeBounds", function(self, minW, minH, maxW, maxH)
	if minW then self:SetMinResize(minW, minH) end
	if maxW then self:SetMaxResize(maxW, maxH) end
end)
provide(FRAMES, "GetResizeBounds", function(self)
	local minW, minH = self:GetMinResize()
	local maxW, maxH = self:GetMaxResize()
	return minW, minH, maxW, maxH
end)
provide(FRAMES, "CreateMaskTexture", function(self, name, layer)
	local mask = self:CreateTexture(name, layer)
	mask:Hide()
	return mask
end)
provide(FRAMES, "CreateLine", function(self, name, layer) return self:CreateTexture(name, layer) end)
provide(FRAMES, "SetEnabled", function(self, enabled) if enabled then self:Enable() else self:Disable() end end)
provide(FRAMES, "GetEffectiveFrameLevel", function(self) return self:GetFrameLevel() end)

-- Textures -----------------------------------------------------------------------------------------------------------
provide({ "Texture" }, "SetColorTexture", function(self, r, g, b, a) self:SetTexture(r, g, b, a or 1) end)
provide({ "Texture" }, "SetDesaturation", function(self, amount) self:SetDesaturated((amount or 0) > 0) end)
provide({ "Texture" }, "SetMask", noop)
provide({ "Texture" }, "AddMaskTexture", noop)
provide({ "Texture" }, "RemoveMaskTexture", noop)
provide({ "Texture" }, "GetNumMaskTextures", function() return 0 end)
provide({ "Texture" }, "SetBlockingLoadsRequested", noop)
provide({ "Texture" }, "SetTextureSliceMargins", noop)
provide({ "Texture" }, "SetTextureSliceMode", noop)
provide({ "Texture" }, "SetRotationRadians", function(self, r) self:SetRotation(r) end)

-- Methods the retail files call under Retail names (see Tools/RetailUI/isolate.py METHODS), because addons such as
-- ClassicAPI replace the ordinary ones on the shared widget metatables. RetailSetAtlas/RetailGetAtlas are native.
local function always(kinds, name, func)
	for _, kind in ipairs(kinds) do if types[kind] then types[kind][name] = func end end
end

-- 3.3.5a CopyFontObject keeps only the source's parent link: the copied attributes are masked off but their values
-- stay, so a later SetFont with the same values is a no-op and the copy never gets a font. Retail copies everything;
-- this sets each attribute instead (the stock copy is not called).
always({ "Font" }, "RetailCopyFontObject", function(self, source)
	if type(source) == "string" then source = _G[source] end
	local file, height, flags = source:GetFont()
	if file then self:SetFont(file, height, flags) end
	self:SetTextColor(source:GetTextColor())
	self:SetShadowColor(source:GetShadowColor())
	self:SetShadowOffset(source:GetShadowOffset())
	self:SetSpacing(source:GetSpacing())
	self:SetJustifyH(source:GetJustifyH())
	self:SetJustifyV(source:GetJustifyV())
end)

-- Retail's SetTexCoord(0, 1, 0, 1) after SetAtlas means "the whole atlas piece"; 3.3.5a would show the whole sheet.
do
	local setTexCoord = types.Texture and types.Texture.SetTexCoord
	if setTexCoord then
		always({ "Texture" }, "SetTexCoord", function(self, a, b, c, d, ...)
			if a == 0 and b == 1 and c == 0 and d == 1 and select("#", ...) == 0 and self:RetailGetAtlas() then return end
			return setTexCoord(self, a, b, c, d, ...)
		end)
	end
end

-- 3.3.5a SetRotation replaces the texture coordinates with a rotated 0..1 square, which wipes out an atlas piece.
-- Retail rotates the piece in place: for a texture showing an atlas, rotate that atlas's own corners about their
-- centre; whole-file textures use the stock rotation.
do
	local stockRotation = types.Texture.SetRotation
	always({ "Texture" }, "RetailSetRotation", function(self, radians, ...)
		local atlas = self:RetailGetAtlas()
		local info = atlas and RetailGetAtlasInfo(atlas)
		self.retailRotation = radians
		if not info then return stockRotation(self, radians, ...) end
		local l, r, t, b = info.leftTexCoord, info.rightTexCoord, info.topTexCoord, info.bottomTexCoord
		local cx, cy, s, c = (l + r) / 2, (t + b) / 2, math.sin(-radians), math.cos(-radians)
		local function at(x, y)
			local dx, dy = x - cx, y - cy
			return cx + dx * c - dy * s, cy + dx * s + dy * c
		end
		local ulx, uly = at(l, t)
		local llx, lly = at(l, b)
		local urx, ury = at(r, t)
		local lrx, lry = at(r, b)
		self:SetTexCoord(ulx, uly, llx, lly, urx, ury, lrx, lry)
	end)
	always({ "Texture" }, "RetailGetRotation", function(self) return self.retailRotation or 0 end)
	-- Models' SetRotation is their facing: the stock meaning is the retail one.
	for _, kind in ipairs({ "Model", "PlayerModel", "Cooldown" }) do
		local m = types[kind]
		if m then
			m.RetailSetRotation = m.SetRotation or function() end
			m.RetailGetRotation = m.GetFacing or m.GetRotation or function() return 0 end
		end
	end
end

local function atlasSetter(getter, setter)
	return function(self, atlas, useAtlasSize)
		local texture = self[getter](self)
		if not texture then
			self[setter](self, "Interface\Buttons\WHITE8X8")
			texture = self[getter](self)
		end
		if texture then texture:RetailSetAtlas(atlas, useAtlasSize) end
	end
end
always(BUTTONS, "RetailSetNormalAtlas", atlasSetter("GetNormalTexture", "SetNormalTexture"))
always(BUTTONS, "RetailSetPushedAtlas", atlasSetter("GetPushedTexture", "SetPushedTexture"))
always(BUTTONS, "RetailSetDisabledAtlas", atlasSetter("GetDisabledTexture", "SetDisabledTexture"))
always(BUTTONS, "RetailSetHighlightAtlas", atlasSetter("GetHighlightTexture", "SetHighlightTexture"))
always({ "CheckButton" }, "RetailSetCheckedAtlas", atlasSetter("GetCheckedTexture", "SetCheckedTexture"))
always({ "CheckButton" }, "RetailSetDisabledCheckedAtlas", atlasSetter("GetDisabledCheckedTexture", "SetDisabledCheckedTexture"))
always({ "StatusBar" }, "RetailSetStatusBarAtlas", function(self, atlas)
	if not self:GetStatusBarTexture() then self:SetStatusBarTexture("Interface\Buttons\WHITE8X8") end
	self:GetStatusBarTexture():RetailSetAtlas(atlas)
end)
always({ "StatusBar" }, "RetailGetStatusBarAtlas", function(self)
	return self:GetStatusBarTexture() and self:GetStatusBarTexture():RetailGetAtlas()
end)
provide({ "Texture" }, "GetTextureFileID", function(self)
	local texture = self:GetTexture()
	return texture and tonumber(texture:match("^retail[/\\](%d+)%.blp$")) or texture
end)

-- Buttons ------------------------------------------------------------------------------------------------------------
provide(BUTTONS, "ClearNormalTexture", function(self) self:SetNormalTexture(nil) end)
provide(BUTTONS, "ClearPushedTexture", function(self) self:SetPushedTexture(nil) end)
provide(BUTTONS, "ClearDisabledTexture", function(self) self:SetDisabledTexture(nil) end)
provide(BUTTONS, "ClearHighlightTexture", function(self) self:SetHighlightTexture(nil) end)
provide(BUTTONS, "SetHighlightLocked", function(self, locked) if locked then self:LockHighlight() else self:UnlockHighlight() end end)
provide(BUTTONS, "IsHighlightLocked", function() return false end)
provide(BUTTONS, "GetTextFontString", function(self) return self:GetFontString() end)

-- Font strings -------------------------------------------------------------------------------------------------------
provide({ "FontString" }, "GetUnboundedStringWidth", function(self) return self:GetStringWidth() end)
provide({ "FontString" }, "GetLineHeight", function(self) local _, height = self:GetFont() return height or 12 end)
provide({ "FontString" }, "SetFontHeight", function(self, height)
	local font, _, flags = self:GetFont()
	if font then self:SetFont(font, height, flags) end
end)
provide({ "FontString" }, "IsTruncated", function(self) return self:GetStringWidth() > (self:GetWidth() + 0.5) end)
provide({ "FontString" }, "SetMaxLines", noop)
provide({ "FontString" }, "GetMaxLines", function() return 0 end)
-- always: ClassicAPI ships a SetTextToFit that does not set the text.
always({ "FontString" }, "SetTextToFit", function(self, text) self:SetText(text) self:SetWidth(self:GetStringWidth()) end)
provide({ "FontString" }, "SetTextScale", noop)
provide({ "FontString" }, "GetTextScale", function() return 1 end)
provide({ "FontString" }, "SetFormattedTextScale", noop)
-- ClassicAPI's ClearText calls SetText, which errors on a pooled string that has no font yet.
always({ "FontString" }, "RetailClearText", function(self) if self:GetFont() then self:SetText("") end end)

-- Animations ---------------------------------------------------------------------------------------------------------
provide({ "AnimationGroup" }, "SetToFinalAlpha", noop)
provide({ "AnimationGroup" }, "Restart", function(self) self:Stop() self:Play() end)
provide({ "AnimationGroup" }, "SetPlaying", function(self, playing) if playing then self:Play() else self:Stop() end end)
provide(ANIMS, "SetToFinalAlpha", noop)
provide({ "Alpha" }, "SetFromAlpha", function(self, a) self.fromAlpha = a self:SetChange((self.toAlpha or 1) - a) end)
provide({ "Alpha" }, "SetToAlpha", function(self, a) self.toAlpha = a self:SetChange(a - (self.fromAlpha or 1)) end)
provide({ "Scale" }, "SetScaleFrom", function(self, x, y) self.from = { x, y } end)
provide({ "Scale" }, "SetScaleTo", function(self, x, y)
	local from = self.from or { 1, 1 }
	self:SetScale(x / (from[1] ~= 0 and from[1] or 1), y / (from[2] ~= 0 and from[2] or 1))
end)

-- StatusBar ----------------------------------------------------------------------------------------------------------
provide({ "StatusBar" }, "SetColorFill", function(self, r, g, b, a) self:SetStatusBarColor(r, g, b, a) end)

-- Cooldown --------------------------------------------------------------------------------------------------------------
provide({ "Cooldown" }, "SetHideCountdownNumbers", noop)
provide({ "Cooldown" }, "SetSwipeColor", noop)
provide({ "Cooldown" }, "SetDrawBling", noop)
provide({ "Cooldown" }, "SetDrawEdge", noop)
provide({ "Cooldown" }, "SetDrawSwipe", noop)
provide({ "Cooldown" }, "Clear", function(self) self:SetCooldown(0, 0) end)

-- Pools reset recycled regions with SetToDefaults (12.x): back to hidden, unanchored and plain.
provide(REGIONS, "SetToDefaults", function(self)
	self:Hide()
	self:ClearAllPoints()
	self:SetAlpha(1)
	if self.SetVertexColor and self:GetObjectType() == "Texture" then self:SetVertexColor(1, 1, 1, 1) self:SetTexture(nil) end
	if self:GetObjectType() == "FontString" and self:GetFont() then self:SetText("") end
end)

-- Retail's secure helpers reach the engine's original widget methods through these (e.g. GetFontStringMetatable().__index.SetText).
for kind, name in pairs({ FontString = "GetFontStringMetatable", Frame = "GetFrameMetatable", Button = "GetButtonMetatable",
		Texture = "GetTextureMetatable", EditBox = "GetEditBoxMetatable", StatusBar = "GetStatusBarMetatable" }) do
	if types[kind] and rawget(_G, name) == nil then
		local metatable = { __index = types[kind] }
		_G[name] = function() return metatable end
	end
end

-- Menus size each item from its regions' rects right after re-anchoring them, but 3.3.5a reports a region's rect from the
-- last layout pass, so reused items picked up stale sizes and every later opening grew or overlapped rows. For regions
-- of menu items the rect is worked out from anchors and sizes instead, in the item's own coordinates (only differences
-- between regions matter to the measurement). Stretched regions (two or more anchors) follow the item, never size it.
do
	local FX = { LEFT = 0, RIGHT = 1, CENTER = 0.5, TOP = 0.5, BOTTOM = 0.5, TOPLEFT = 0, BOTTOMLEFT = 0, TOPRIGHT = 1, BOTTOMRIGHT = 1 }
	local FY = { TOP = 1, BOTTOM = 0, CENTER = 0.5, LEFT = 0.5, RIGHT = 0.5, TOPLEFT = 1, TOPRIGHT = 1, BOTTOMLEFT = 0, BOTTOMRIGHT = 0 }
	local function localRect(region, item, depth)
		if region == item then return 0, 0, item:GetWidth(), item:GetHeight() end
		if depth > 6 or region:GetNumPoints() ~= 1 or region:GetParent() ~= item then return end
		local point, relativeTo, relativePoint, x, y = region:GetPoint(1)
		local l, b, w, h = localRect(relativeTo or item, item, depth + 1)
		if not l then return end
		local width, height = region:GetWidth(), region:GetHeight()
		local ax = l + (FX[relativePoint] or 0) * w + (x or 0)
		local ay = b + (FY[relativePoint] or 0) * h + (y or 0)
		return ax - (FX[point] or 0) * width, ay - (FY[point] or 0) * height, width, height
	end
	for _, kind in ipairs({ "Texture", "FontString" }) do
		local original = types[kind] and types[kind].GetRect
		if original then
			always({ kind }, "GetRect", function(self)
				local item = self:GetParent()
				if item and item.GetElementDescription then
					if self:GetNumPoints() >= 2 then return end
					local l, b, w, h = localRect(self, item, 0)
					if l then return l, b, w, h end
				end
				return original(self)
			end)
		end
	end
end

-- 3.3.5a GetDrawLayer returns only the layer; retail reads a sublevel too (menus do "drawLevel + 1" with it).
do
	local original = types.Texture and types.Texture.GetDrawLayer
	if original then
		always({ "Texture" }, "GetDrawLayer", function(self)
			local layer, sublevel = original(self)
			return layer, sublevel or 0
		end)
	end
end

-- Retail's |A:atlas:height:width[:x:y]|a text markup is unknown to 3.3.5a, which printed it raw ("Officer |A:communi...").
-- Rewrite it as the |T texture markup the client understands, cropped to the atlas piece.
do
	local function texture(name, height, width, x, y)
		local info = RetailGetAtlasInfo and RetailGetAtlasInfo(name)
		if not info then return "" end
		local spanW, spanH = info.rightTexCoord - info.leftTexCoord, info.bottomTexCoord - info.topTexCoord
		local fileW = spanW > 0 and math.floor(info.width / spanW + 0.5) or info.width
		local fileH = spanH > 0 and math.floor(info.height / spanH + 0.5) or info.height
		height, width = tonumber(height) or 0, tonumber(width) or 0
		if width == 0 then width = height end
		return string.format("|T%s:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d|t", (info.filename or ""):gsub("%.blp$", ""), height, width,
			tonumber(x) or 0, tonumber(y) or 0, fileW, fileH, math.floor(info.leftTexCoord * fileW + 0.5),
			math.floor(info.rightTexCoord * fileW + 0.5), math.floor(info.topTexCoord * fileH + 0.5), math.floor(info.bottomTexCoord * fileH + 0.5))
	end
	local function convert(text)
		if type(text) ~= "string" or not text:find("|A:", 1, true) then return text end
		return (text:gsub("|A:([^:|]+):?([%-%d]*):?([%-%d]*):?([%-%d]*):?([%-%d]*)[^|]*|a", texture))
	end
	EsteriaRetailUI.ConvertAtlasMarkup = convert
	local setText = types.FontString and types.FontString.SetText
	local setFormatted = types.FontString and types.FontString.SetFormattedText
	if setText then
		always({ "FontString" }, "SetText", function(self, text, ...) return setText(self, convert(text), ...) end)
	end
	if setText and setFormatted then
		always({ "FontString" }, "SetFormattedText", function(self, format, ...)
			if type(format) == "string" and format:find("|A:", 1, true) then return setText(self, convert(format:format(...))) end
			return setFormatted(self, format, ...)
		end)
	end
end
