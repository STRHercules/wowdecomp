if type(EsteriaGetVisibleWantedMapPins) ~= "function" then
  return
end

local pins = {}
local elapsed = 0

local function ShowWantedTooltip(pin)
  GameTooltip:SetOwner(pin, "ANCHOR_RIGHT")
  GameTooltip:SetText(pin.wantedName)
  if pin.wantedAge then
    GameTooltip:AddLine(("Last seen %ds ago"):format(pin.wantedAge), 1, 0.72, 0.30)
  else
    GameTooltip:AddLine("Wanted", 1, 0.25, 0.25)
  end
  GameTooltip:Show()
end

local function HideWantedTooltip(pin)
  if GameTooltip:IsOwned(pin) then
    GameTooltip:Hide()
  end
end

local function CreatePin()
  local pin = CreateFrame("Frame", nil, WorldMapButton)
  pin:SetWidth(16)
  pin:SetHeight(16)
  pin:SetFrameLevel(WorldMapButton:GetFrameLevel() + 3)
  pin:EnableMouse(true)
  pin:SetScript("OnEnter", ShowWantedTooltip)
  pin:SetScript("OnLeave", HideWantedTooltip)
  pin:SetScript("OnHide", HideWantedTooltip)

  local icon = pin:CreateTexture(nil, "OVERLAY")
  icon:SetAllPoints(pin)
  icon:SetTexture("Interface\\TargetingFrame\\UI-RaidTargetingIcons")
  icon:SetTexCoord(0.75, 1, 0.25, 0.5)
  pin.texture = icon
  return pin
end

local function RefreshPins()
  local wanted = EsteriaGetVisibleWantedMapPins()
  local width, height = WorldMapButton:GetWidth(), WorldMapButton:GetHeight()
  for index, target in ipairs(wanted) do
    local pin = pins[index]
    if not pin then
      pin = CreatePin()
      pins[index] = pin
    end

    if pin.wantedName ~= target.name or pin.wantedAge ~= target.age then
      pin.wantedName = target.name
      pin.wantedAge = target.age
      pin.texture:SetVertexColor(1, target.age and 0.65 or 1, target.age and 0.35 or 1)
      if GameTooltip:IsOwned(pin) then
        ShowWantedTooltip(pin)
      end
    end

    pin:ClearAllPoints()
    pin:SetPoint("CENTER", WorldMapButton, "TOPLEFT", target.x * width, -target.y * height)
    pin:Show()
  end

  for index = #wanted + 1, #pins do
    pins[index]:Hide()
  end
end

local loader = CreateFrame("Frame")
local function Attach()
  if not WorldMapFrame or not WorldMapButton then
    return
  end

  loader:UnregisterAllEvents()
  local driver = CreateFrame("Frame", nil, WorldMapFrame)
  driver:SetScript("OnShow", RefreshPins)
  driver:SetScript("OnHide", function()
    for _, pin in ipairs(pins) do
      pin:Hide()
    end
  end)
  driver:SetScript("OnUpdate", function(_, delta)
    elapsed = elapsed + delta
    if elapsed >= 0.25 then
      elapsed = 0
      RefreshPins()
    end
  end)

  if WorldMapFrame:IsShown() then
    RefreshPins()
  end
end

loader:RegisterEvent("ADDON_LOADED")
loader:RegisterEvent("PLAYER_ENTERING_WORLD")
loader:SetScript("OnEvent", Attach)
Attach()
