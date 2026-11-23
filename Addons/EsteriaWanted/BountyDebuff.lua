local BOUNTY_SPELL = 955081
local amount, amountLine, previousText

local function updateAmount(tooltip, unit, index, filter)
  if unit ~= "player" or type(index) ~= "number" or not amount or
      select(11, UnitAura(unit, index, filter)) ~= BOUNTY_SPELL then return end
  previousText = "Current bounty: " .. amount
  tooltip:AddLine(previousText, 1, 0.82, 0)
  amountLine = _G[tooltip:GetName() .. "TextLeft" .. tooltip:NumLines()]
  tooltip:Show()
end

hooksecurefunc(GameTooltip, "SetUnitAura", updateAmount)
hooksecurefunc(GameTooltip, "SetUnitDebuff", function(tooltip, unit, index)
  updateAmount(tooltip, unit, index, "HARMFUL")
end)

local frame = CreateFrame("Frame")
frame:RegisterEvent("CHAT_MSG_ADDON")
frame:RegisterEvent("PLAYER_LEAVING_WORLD")
frame:SetScript("OnEvent", function(_, event, prefix, value, channel, sender)
  if event == "PLAYER_LEAVING_WORLD" then
    amount, amountLine, previousText = nil, nil, nil
  elseif prefix == "EsteriaBounty" and channel == "WHISPER" and sender == UnitName("player") and
      value:match("^%d+g %d+s %d+c$") and value ~= amount then
    amount = value
    if amountLine and GameTooltip:IsShown() and amountLine:GetText() == previousText then
      previousText = "Current bounty: " .. amount
      amountLine:SetText(previousText)
      GameTooltip:Show()
    end
  end
end)
