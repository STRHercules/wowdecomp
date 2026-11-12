-- MSP v1 framing: https://moonshyne.org/msp/. BS is an experimental, unregistered
-- field, not an official MSP field; its namespace keeps unrelated BS values inert.
BodySlider = { names = {
    "Height", "HeadSize", "NeckLength", "ShoulderWidth", "ArmLength", "ArmGirth",
    "ChestSize", "WaistWidth", "TorsoLength", "HipWidth", "LegLength", "LegGirth",
    "BreastSize", "ButtSize", "JawSize", "JawLength", "FaceWidth", "FaceLength",
    "BrowHeight", "BrowDepth", "EyeSize", "EyeSpacing", "EyeHeight", "NoseSize",
    "NoseWidth", "NoseLength", "NoseHeight", "MouthWidth", "LipFullness", "MouthHeight",
    "ChinLength", "ChinDepth", "CheekWidth", "CheekHeight", "JawWidth", "EarSize", "EarHeight",
} }
local addon = BodySlider
local marker = "BodySlider/2:"
local frame = CreateFrame("Frame")
local peers, fragments, queue = {}, {}, {}
local own, active, elapsed, sendAt = nil, false, 0, 0
local lastSweep = 0
local channelName, envelope = "BodySliderMSP", "[BodySliderMSP1]"
local mode, blocked, announced = "channel", nil, false
local joinAttempts, nextJoin, lastBroadcast, broadcastPayload = 0, 0, nil, nil
local lastOutbound, lastRecipient, failedPeers = nil, nil, {}
local publishedLibrary, publishedValue
local discover

local function unpublish()
    if publishedLibrary and publishedLibrary.my and publishedLibrary.my.BS == publishedValue then
        publishedLibrary.my.BS = nil
        -- LibMSP versions removed fields in Update; it does not send the field itself.
        publishedLibrary:Update()
    end
    publishedLibrary, publishedValue = nil, nil
end

local function validName(name)
    return type(name) == "string" and name ~= "" and #name <= 80 and not name:find("[%c%s]")
        and name:upper() ~= "UNKNOWN" and name ~= _G.UNKNOWNOBJECT and name ~= _G.UNKNOWN
end

local function channelId()
    if type(GetChannelName) ~= "function" then return 0 end
    local id = GetChannelName(channelName)
    return tonumber(id) or 0
end

local function groupDistribution()
    if GetNumRaidMembers() > 0 then return "RAID" end
    if type(GetNumPartyMembers) == "function" and GetNumPartyMembers() > 0 then return "PARTY" end
end

local function stopTransport(reason)
    if blocked then return end
    blocked, queue = reason, {}
    unpublish()
    if not announced and DEFAULT_CHAT_FRAME then
        DEFAULT_CHAT_FRAME:AddMessage("Body Slider sharing stopped: " .. reason .. ". /bodyslider status")
        announced = true
    end
end

function addon.GetTransport() return mode end

function addon.Status()
    if blocked then return mode .. ": blocked (" .. blocked .. ")" end
    if mode == "channel" then
        return "channel: " .. channelName .. (channelId() > 0 and " joined" or
            joinAttempts >= 3 and " unavailable after 3 join attempts" or " joining")
    end
    if mode == "group" then return "group: " .. (groupDistribution() or "waiting for a party or raid") end
    return "whisper: opt-in MSP sharing (server whisper restrictions apply)"
end

function addon.SetTransport(value)
    if value ~= "channel" and value ~= "group" and value ~= "whisper" then return false end
    unpublish()
    mode, queue, fragments, failedPeers = value, {}, {}, {}
    blocked, announced, joinAttempts, nextJoin = nil, false, 0, 0
    lastBroadcast, broadcastPayload, lastOutbound, lastRecipient = nil, nil, nil, nil
    sendAt, lastSweep = 0, 0
    for _, peer in pairs(peers) do peer.request, peer.reply = nil, nil end
    if own then own.transport = mode end
    return true
end

function addon.Encode(values)
    local out = {}
    for i, name in ipairs(addon.names) do
        local value = values[name] or 0
        if type(value) ~= "number" or value ~= value then value = 0 end
        local minimum = (name == "BreastSize" or name == "ButtSize") and 0 or -1
        out[i] = tostring(math.floor(math.max(minimum, math.min(1, value)) * 100 + 0.5))
    end
    return marker .. table.concat(out, ",")
end

function addon.Decode(text)
    if type(text) ~= "string" or #text > 220 then return end
    local prefix = text:sub(1, #marker)
    local expected = prefix == marker and #addon.names or prefix == "BodySlider/1:" and 12
    if not expected then return end
    local body, result, count = text:sub(#marker + 1), {}, 0
    for _, name in ipairs(addon.names) do result[name] = 0 end
    if body:sub(-1) == "," then return end
    for value in body:gmatch("[^,]+") do
        if not value:match("^%-?%d+$") or #value > 4 then return end
        local number = tonumber(value)
        count = count + 1
        local name = addon.names[count]
        local minimum = (name == "BreastSize" or name == "ButtSize") and 0 or -100
        if count > expected or number < minimum or number > 100 then return end
        result[addon.names[count]] = number / 100
    end
    if count ~= expected or body:find(",,", 1, true) or body:sub(1, 1) == "," then return end
    return result
end

local function library()
    local msp = _G.msp
    if type(msp) == "table" and type(msp.my) == "table" and type(msp.Update) == "function" then
        return msp
    end
end

local function enqueue(name, body)
    if #queue < 64 and #body <= 250 then queue[#queue + 1] = { name, body } end
end

function addon.Publish()
    if not own then return end
    local value = addon.Encode(own.values)
    if own.payload ~= value then
        own.payload = value
        own.version = (tonumber(own.version) or 0) % 1000000000 + 1
    end
    local msp = mode == "whisper" and not blocked and library()
    if msp and msp.my.BS ~= value then
        msp.my.BS = value
        msp:Update()
        publishedLibrary, publishedValue = msp, value
    end
end

local function remember(name, guid, now)
    if not validName(name) or
        type(guid) ~= "string" or not guid:match("^0x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x$") or
        guid == UnitGUID("player") then return end
    local peer = peers[name]
    if not peer then
        local count = 0
        for _ in pairs(peers) do count = count + 1 end
        if count >= 128 then return end
        peer = {}
        peers[name] = peer
    elseif peer.guid ~= guid then
        BodySlider_Clear(peer.guid)
        peer.version, peer.request = nil, nil
    end
    peer.guid, peer.seen = guid, now
    if mode == "whisper" and not blocked and not failedPeers[name] and (not peer.request or now - peer.request >= 15) then
        peer.request = now
        enqueue(name, "?BS" .. (peer.version or ""))
    end
end

discover = function(now)
    for _, unit in ipairs({ "target", "mouseover", "focus" }) do
        if UnitIsPlayer(unit) then remember(UnitName(unit), UnitGUID(unit), now) end
    end
    for i = 1, 40 do
        local unit = (GetNumRaidMembers() > 0 and "raid" or "party") .. i
        if UnitIsPlayer(unit) then remember(UnitName(unit), UnitGUID(unit), now) end
    end
    if type(BodySlider_Units) == "function" then
        for _, unit in ipairs(BodySlider_Units() or {}) do remember(unit.name, unit.guid, now) end
    end
end

local function receive(sender, body, now)
    local peer = peers[sender]
    if not peer or now - peer.seen > 60 then return end
    local reply = false
    for chunk in body:gmatch("[^\001]+") do
        local requested = chunk:match("^%?BS(%d*)$")
        if requested and #requested <= 10 then
            -- Public LibMSP owns its response. TRP3's private copy may additionally
            -- answer unknown BS with an empty value; those replies are ignored.
            if mode == "whisper" and not library() and not failedPeers[sender] and not reply and (not peer.reply or now - peer.reply >= 2) then
                local version = tonumber(requested) or 0
                enqueue(sender, version ~= 0 and version == own.version and ("!BS" .. own.version)
                    or ("BS" .. own.version .. "=" .. own.payload))
                peer.reply, reply = now, true
            end
        else
            local version, value = chunk:match("^BS(%d*)=(.*)$")
            if version and #version <= 10 then
                local values = addon.Decode(value)
                if values then
                    -- Only game-observed sender identity chooses the target GUID.
                    for _, name in ipairs(addon.names) do BodySlider_Set(peer.guid, name, values[name]) end
                    peer.version, peer.received = tonumber(version) or 0, now
                end
            else
                local unchanged = chunk:match("^!BS(%d*)$")
                if unchanged and #unchanged <= 10 and peer.received then
                    peer.version, peer.received = tonumber(unchanged) or 0, now
                end
            end
        end
    end
end

local function onMessage(prefix, body, distribution, sender)
    local accepted = mode == "whisper" and distribution == "WHISPER" or
        mode == "group" and type(distribution) == "string" and distribution == groupDistribution()
    if not active or blocked or not accepted or not validName(sender) or type(body) ~= "string" or
        #body > 255 or (IsIgnored and IsIgnored(sender)) then return end
    local now = GetTime()
    if not peers[sender] then discover(now) end
    if not peers[sender] then return end
    if prefix == "MSP" then receive(sender, body, now); return end
    if prefix == "MSP\001" then
        fragments[sender] = { body = body, time = now }
    elseif prefix == "MSP\002" or prefix == "MSP\003" then
        local part = fragments[sender]
        if not part then return end
        if now - part.time > 10 or #part.body + #body > 4096 then fragments[sender] = nil; return end
        part.body = part.body .. body
        if prefix == "MSP\003" then
            fragments[sender] = nil
            receive(sender, part.body, now)
        end
    end
end

local function channelMessage(message, sender, _, _, _, _, _, index, baseName)
    if not active or blocked or mode ~= "channel" or type(message) ~= "string" or #message > 254
        or not validName(sender) or (IsIgnored and IsIgnored(sender)) then return end
    local id = channelId()
    if id == 0 or tonumber(index) ~= id or type(baseName) ~= "string" or baseName:lower() ~= channelName:lower()
        or message:sub(1, #envelope) ~= envelope then return end
    local body = message:sub(#envelope + 1)
    local version, value = body:match("^BS(%d+)=(.*)$")
    if not version or #version > 10 or not addon.Decode(value) then return end
    local now = GetTime()
    if not peers[sender] then discover(now) end
    receive(sender, body, now)
end

local function rejection(...)
    if not lastOutbound or GetTime() - lastOutbound > 3 then return end
    local text
    for i = 1, select("#", ...) do if type(select(i, ...)) == "string" then text = select(i, ...) end end
    if not text then return end
    if mode == "whisper" and lastRecipient and type(ERR_CHAT_PLAYER_NOT_FOUND_S) == "string" then
        if text == string.format(ERR_CHAT_PLAYER_NOT_FOUND_S, lastRecipient) then
            failedPeers[lastRecipient] = true
            local remaining = {}
            for _, item in ipairs(queue) do if item[1] ~= lastRecipient then remaining[#remaining + 1] = item end end
            queue = remaining
            return
        end
    end
    local lower = text:lower()
    local relevant = mode == "whisper" and lower:find("whisper", 1, true) or
        mode == "channel" and lower:find("channel", 1, true) or
        mode == "group" and (lower:find("party", 1, true) or lower:find("raid", 1, true) or lower:find("group", 1, true))
    local level = lower:find("level", 1, true) and relevant
    local restricted = lower:find("throttl", 1, true) or lower:find("flood", 1, true)
        or lower:find("muted", 1, true) or lower:find("chat is disabled", 1, true)
    for _, key in ipairs({ "ERR_CHAT_THROTTLED", "ERR_CHAT_PLAYER_AMBIGUOUS", "ERR_CHAT_RESTRICTED" }) do
        if type(_G[key]) == "string" and text == _G[key] then restricted = true end
    end
    if mode == "channel" then
        restricted = restricted or lower:find("channel", 1, true) and
            (lower:find("banned", 1, true) or lower:find("password", 1, true) or lower:find("not permitted", 1, true))
    end
    if level or restricted then stopTransport(text) end
end

function addon.Start(store)
    own, active = store, true
    addon.SetTransport(store.transport or "channel")
    addon.Publish()
    if RegisterAddonMessagePrefix then
        for _, prefix in ipairs({ "MSP", "MSP\001", "MSP\002", "MSP\003" }) do RegisterAddonMessagePrefix(prefix) end
    end
    frame:RegisterEvent("CHAT_MSG_ADDON")
    frame:RegisterEvent("PLAYER_TARGET_CHANGED")
    frame:RegisterEvent("UPDATE_MOUSEOVER_UNIT")
    frame:RegisterEvent("CHAT_MSG_CHANNEL")
    frame:RegisterEvent("CHAT_MSG_SYSTEM")
    frame:RegisterEvent("UI_ERROR_MESSAGE")
    frame:RegisterEvent("CHAT_MSG_CHANNEL_NOTICE")
    if ChatFrame_AddMessageEventFilter then
        ChatFrame_AddMessageEventFilter("CHAT_MSG_CHANNEL", function(_, _, message, _, _, _, _, _, _, index, baseName)
            local id = channelId()
            if id == 0 or tonumber(index) ~= id or type(baseName) ~= "string"
                or baseName:lower() ~= channelName:lower() or type(message) ~= "string" or #message > 254
                or message:sub(1, #envelope) ~= envelope then return false end
            local version, value = message:sub(#envelope + 1):match("^BS(%d+)=(.*)$")
            return version ~= nil and #version <= 10 and addon.Decode(value) ~= nil
        end)
    end
    discover(GetTime())
end

frame:SetScript("OnEvent", function(_, event, ...)
    if event == "CHAT_MSG_ADDON" then onMessage(...)
    elseif event == "CHAT_MSG_CHANNEL" then channelMessage(...)
    elseif event == "CHAT_MSG_SYSTEM" or event == "UI_ERROR_MESSAGE" then rejection(...)
    elseif event == "CHAT_MSG_CHANNEL_NOTICE" and active and mode == "channel" then
        local notice, _, _, _, _, _, _, _, name = ...
        if type(name) == "string" and name:lower() == channelName:lower() and
            (notice == "BANNED" or notice == "WRONG_PASSWORD" or notice == "MUTED" or notice == "THROTTLED") then
            stopTransport("channel " .. notice:lower())
        end
    elseif active then discover(GetTime()) end
end)
frame:SetScript("OnUpdate", function(_, delta)
    if not active then return end
    elapsed = elapsed + delta
    if elapsed < 0.2 then return end
    elapsed = 0
    local now = GetTime()
    addon.Publish()
    if now - lastSweep >= 2 then
        lastSweep = now
        discover(now)
        for name, peer in pairs(peers) do
            if now - peer.seen > 120 then
                BodySlider_Clear(peer.guid)
                peers[name], fragments[name] = nil, nil
            elseif peer.received and now - peer.received > 120 then
                BodySlider_Clear(peer.guid)
                peer.received, peer.version = nil, nil
            end
        end
        for name, part in pairs(fragments) do if now - part.time > 10 then fragments[name] = nil end end
    end
    if blocked then return end
    if mode == "channel" and channelId() == 0 and joinAttempts < 3 and now >= nextJoin then
        joinAttempts, nextJoin = joinAttempts + 1, now + 10
        lastOutbound, lastRecipient = now, nil
        if JoinChannelByName then JoinChannelByName(channelName) end
    end
    if mode ~= "whisper" then
        local distribution = mode == "group" and groupDistribution()
        local id = mode == "channel" and channelId() or 0
        if (distribution or id > 0) and (not lastBroadcast or
            now - lastBroadcast >= 3 and (broadcastPayload ~= own.payload or now - lastBroadcast >= 30)) then
            local body = "BS" .. own.version .. "=" .. own.payload
            lastOutbound, lastRecipient = now, nil
            if id > 0 then
                local message = envelope .. body
                if #message <= 254 then SendChatMessage(message, "CHANNEL", nil, id) end
            else
                SendAddonMessage("MSP", body, distribution)
            end
            lastBroadcast, broadcastPayload = now, own.payload
        end
        return
    end
    if now >= sendAt and #queue > 0 then
        local message = table.remove(queue, 1)
        if validName(message[1]) and not failedPeers[message[1]] and peers[message[1]] and now - peers[message[1]].seen <= 60 then
            lastOutbound, lastRecipient = now, message[1]
            if ChatThrottleLib then
                ChatThrottleLib:SendAddonMessage("BULK", "MSP", message[2], "WHISPER", message[1])
            else
                SendAddonMessage("MSP", message[2], "WHISPER", message[1])
            end
        end
        sendAt = now + 0.2
    end
end)
