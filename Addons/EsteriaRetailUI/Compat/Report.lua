-- Puts back the stock globals that 3.3.5a code depends on with its own semantics, then lists every other stock
-- global the retail files replaced (DevHarness logs it), so a new collision is seen rather than guessed at.
local RESTORE = {
	-- retail CVarUtil rewires these through C_CVar; stock callers expect the engine functions.
	"GetCVar", "SetCVar", "GetCVarBool", "GetCVarDefault", "RegisterCVar", "GetCVarBitfield", "SetCVarBitfield",
	-- UIParent's fade/flash state machine and its tables.
	"UIFrameFade", "UIFrameFadeIn", "UIFrameFadeOut", "UIFrameFadeRemoveFrame", "UIFrameFade_OnUpdate", "UIFrameIsFading",
	"UIFrameFlash", "UIFrameFlashStop", "UIFrameFlash_OnUpdate", "UIFrameIsFlashing", "FADEFRAMES", "FLASHFRAMES",
	"RaiseFrameLevel", "RaiseFrameLevelByTwo", "LowerFrameLevel",
}

local stock = EsteriaRetailUI.stock
EsteriaRetailUI.retail = {}
for _, name in ipairs(RESTORE) do
	if stock[name] ~= nil and _G[name] ~= stock[name] then
		EsteriaRetailUI.retail[name] = _G[name] -- kept for retail code that wants its own version
		_G[name] = stock[name]
	end
end

-- C_UIColor.GetColors makes retail write ColorMixin objects over stock colour tables (NORMAL_FONT_COLOR ...). Keep
-- the stock tables, so both UIs share them, and give them the colour methods.
for name, value in pairs(stock) do
	local new = _G[name]
	if new ~= value and type(value) == "table" and type(new) == "table" and value.r and new.GetRGB then
		for k, v in pairs(new) do
			if type(v) == "function" and value[k] == nil then value[k] = v end
		end
		value.a = value.a or new.a
		_G[name] = value
	end
end

local replaced = {}
for name, value in pairs(stock) do
	if _G[name] ~= value then replaced[#replaced + 1] = name end
end
table.sort(replaced)
EsteriaRetailUI.replaced = replaced
EsteriaRetailUI.stock = nil
if DevHarness_Log then DevHarness_Log("EsteriaRetailUI replaced " .. #replaced .. " stock globals: " .. table.concat(replaced, " ")) end
