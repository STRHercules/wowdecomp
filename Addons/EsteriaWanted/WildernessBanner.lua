-- The server whispers EsteriaWild 1 every half second inside an active Wilderness zone and 0 on leaving.
local banner = EsteriaBanner.Hold(560, 78, 26, -14)
local expiresAt = 0

local function enter()
    banner:Enter({
        title = "Wilderness",
        subtitle = (GetZoneText() or "") .. "  -  Free-for-all PvP. Kills here are lawful.",
        color = { 0.95, 0.36, 0.14 },
        icon = "Interface\\Icons\\Ability_DualWield",
    })
end

local frame = CreateFrame("Frame")
frame:RegisterEvent("CHAT_MSG_ADDON")
frame:RegisterEvent("PLAYER_LEAVING_WORLD")
frame:SetScript("OnEvent", function(_, event, prefix, message, channel, sender)
    if event == "PLAYER_LEAVING_WORLD" then
        expiresAt = 0
        banner:Clear()
    elseif prefix == "EsteriaWild" and channel == "WHISPER" and sender == UnitName("player") then
        if message == "1" then
            if not banner:Active() then
                PlaySound("RaidWarning")
                enter()
            end
            expiresAt = GetTime() + 1.5
        elseif message == "0" then
            expiresAt = 0
            banner:Leave()
        end
    end
end)
-- A missed refresh (lag, a lost whisper) also ends the banner instead of leaving it stuck.
frame:SetScript("OnUpdate", function()
    if expiresAt > 0 and GetTime() >= expiresAt then
        expiresAt = 0
        banner:Leave()
    end
end)
