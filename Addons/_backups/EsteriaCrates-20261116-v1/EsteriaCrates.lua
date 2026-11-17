-- Esteria Crates: the crate window (odds grid, key purchase, unlock reel and reveal).
-- Server half: EsteriaWoW modules/mod-esteria-crates. The server rolls, charges and delivers before it answers; this
-- file only shows the committed result. Nothing here chooses a reward, a price or odds.
local ES = EsteriaStyle;
local PFX_OUT, PFX_IN = "ECRC", "ECRS";

local W, H = 800, 668;
local REEL_W, REEL_H = 760, 176;
local CARD_W, CARD_H, STEP = 140, 166, 148;
local GRID_W, GRID_H, GRID_STEP, PER_ROW = 116, 136, 124, 6;
local REEL_LEN, WIN_AT = 48, 40;
local SPIN_TIME, ANSWER_TIMEOUT = 7, 10;
local ORANGE = { 1, 0.6, 0.2 };
local GREEN = { 0.22, 0.55, 0.18 };
local GREY = { 0.22, 0.22, 0.24 };

local db;
local state = { rewards = {} };
local phase = "idle"; -- idle, pending, spin, reveal
local pendingReq, pendingAt, pendingKind;
local counter = 0;

local function send(body) SendAddonMessage(PFX_OUT, body, "WHISPER", UnitName("player")); end
local function nextRequest() counter = counter + 1; return string.format("%x%x", time(), counter); end
local function spaced(text) return (text:gsub("(.)", "%1 "):gsub(" $", "")); end
local function rarityColor(r)
    local c = ITEM_QUALITY_COLORS[r];
    if c then return { c.r, c.g, c.b }; end
    return { 0.9, 0.8, 0.5 };
end
local function rarityName(r) return _G["ITEM_QUALITY" .. r .. "_DESC"] or "Special"; end
local function percent(weight)
    local p = weight * 100 / state.total;
    if p == math.floor(p) then return string.format("%d%%", p); end
    return (string.format("%.2f", p):gsub("0+$", "")) .. "%";
end
local function sound(name) if not db.mute then PlaySound(name); end end

-- Item info the client has not cached yet: ask the server through a hidden tooltip and redraw until it arrives.
local scan = CreateFrame("GameTooltip", "EsteriaCratesScan", nil, "GameTooltipTemplate");
local waiting, waitTimer = false, 0;
local function itemInfo(id)
    local name, _, quality, _, _, _, _, _, _, icon = GetItemInfo(id);
    if not name then
        scan:SetOwner(UIParent, "ANCHOR_NONE");
        scan:SetHyperlink("item:" .. id .. ":0:0:0:0:0:0:0");
        waiting = true;
        return "Loading...", "Interface\\Icons\\INV_Misc_QuestionMark", quality;
    end
    return name, icon, quality;
end

-------------------------------------------------------------------------------- frame

local frame = CreateFrame("Frame", "EsteriaCratesFrame", UIParent);
frame:SetSize(W, H);
frame:SetPoint("CENTER", 0, 20);
frame:SetFrameStrata("HIGH");
frame:SetToplevel(true);
frame:EnableMouse(true);
frame:SetMovable(true);
frame:RegisterForDrag("LeftButton");
frame:SetScript("OnDragStart", frame.StartMoving);
frame:SetScript("OnDragStop", frame.StopMovingOrSizing);
frame:Hide();
tinsert(UISpecialFrames, "EsteriaCratesFrame");
ES.Solid(frame, "BACKGROUND", ES.INK, 0.97):SetAllPoints();
local glow = ES.Solid(frame, "BACKGROUND", { 0.10, 0.12, 0.18 }, 1);
glow:SetPoint("TOPLEFT"); glow:SetPoint("TOPRIGHT"); glow:SetHeight(H * 0.6);
glow:SetGradientAlpha("VERTICAL", 0.10, 0.12, 0.18, 0, 0.10, 0.12, 0.18, 0.9);
ES.Border(frame, ES.GOLD_DIM, 1);

local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton");
close:SetPoint("TOPRIGHT", 2, 2);

local iconFrame = CreateFrame("Frame", nil, frame);
iconFrame:SetSize(48, 48);
iconFrame:SetPoint("TOPLEFT", 20, -16);
local crateIcon = iconFrame:CreateTexture(nil, "ARTWORK");
crateIcon:SetAllPoints();
ES.Border(iconFrame, ORANGE, 1);

local title = ES.Text(frame, 22, ORANGE);
title:SetPoint("TOPLEFT", iconFrame, "TOPRIGHT", 14, -2);
local subtitle = ES.Text(frame, 11, ES.MUTED);
subtitle:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 0, -6);
subtitle:SetText("Unlock it to receive one of the items below.");
local bagCount = ES.Text(frame, 13, ES.TEXT);
bagCount:SetPoint("TOPRIGHT", -40, -22);
local keyCount = ES.Text(frame, 11, ES.MUTED);
keyCount:SetPoint("TOPRIGHT", bagCount, "BOTTOMRIGHT", 0, -6);

-------------------------------------------------------------------------------- cards

local function makeCard(parent, w, h, iconSize)
    local card = CreateFrame("Button", nil, parent);
    card:SetSize(w, h);
    card.bg = ES.Solid(card, "BACKGROUND", ES.INK, 1);
    card.bg:SetAllPoints();
    card.bar = ES.Solid(card, "BORDER", ES.GOLD, 1);
    card.bar:SetPoint("BOTTOMLEFT"); card.bar:SetPoint("BOTTOMRIGHT"); card.bar:SetHeight(3);
    card.edge = ES.Border(card, ES.GOLD_DIM, 0.6);
    card.icon = card:CreateTexture(nil, "ARTWORK");
    card.icon:SetSize(iconSize, iconSize);
    card.icon:SetPoint("TOP", 0, -16);
    card.name = ES.Text(card, 11, ES.TEXT);
    card.name:SetPoint("TOP", card.icon, "BOTTOM", 0, -8);
    card.name:SetWidth(w - 12);
    card.name:SetHeight(28);
    card.name:SetJustifyV("TOP");
    card.foot = ES.Text(card, 11, ES.GOOD);
    card.foot:SetPoint("BOTTOM", 0, 10);
    card.grade = ES.Text(card, 9, ES.MUTED);
    card.grade:SetPoint("BOTTOMLEFT", 6, 10);
    card.owned = ES.Text(card, 9, ES.GOOD);
    card.owned:SetPoint("TOPRIGHT", -5, -5);
    card:SetScript("OnEnter", function(self)
        if not self.reward then return; end
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT");
        GameTooltip:SetHyperlink("item:" .. self.reward.item .. ":0:0:0:0:0:0:0");
        GameTooltip:AddLine(" ");
        GameTooltip:AddDoubleLine("Chance", percent(self.reward.weight), 1, 1, 1, 1, 1, 1);
        GameTooltip:AddDoubleLine("Exact odds", self.reward.weight .. " / " .. state.total, 0.6, 0.6, 0.6, 0.6, 0.6, 0.6);
        GameTooltip:AddDoubleLine("Grade", rarityName(self.reward.rarity), 0.6, 0.6, 0.6, unpack(rarityColor(self.reward.rarity)));
        if self.reward.count > 1 then GameTooltip:AddDoubleLine("Quantity", self.reward.count, 0.6, 0.6, 0.6, 1, 1, 1); end
        GameTooltip:Show();
    end);
    card:SetScript("OnLeave", function() GameTooltip:Hide(); end);
    card:SetScript("OnClick", function(self)
        if self.reward and IsModifiedClick("DRESSUP") then
            DressUpItemLink("item:" .. self.reward.item);
        end
    end);
    return card;
end

local function fillCard(card, reward, showOdds)
    card.reward = reward;
    local name, icon = itemInfo(reward.item);
    local c = rarityColor(reward.rarity);
    card.icon:SetTexture(icon);
    card.name:SetText(reward.count > 1 and (name .. " x" .. reward.count) or name);
    card.name:SetTextColor(c[1], c[2], c[3]);
    card.bar:SetVertexColor(c[1], c[2], c[3], 1);
    card.bg:SetGradientAlpha("VERTICAL", c[1] * 0.35, c[2] * 0.35, c[3] * 0.35, 0.95, 0.05, 0.06, 0.08, 0.95);
    ES.Tint(card.edge, c, reward.rarity >= 5 and 0.9 or 0.35);
    card.foot:SetText(showOdds and percent(reward.weight) or "");
    card.foot:SetTextColor(reward.rarity >= 5 and 1 or 0.55, reward.rarity >= 5 and 0.45 or 0.85, reward.rarity >= 5 and 0.35 or 0.5);
    card.grade:SetText(showOdds and rarityName(reward.rarity) or "");
    card.owned:SetText(showOdds and reward.known and "Owned" or "");
    card:SetAlpha(1);
    card:Show();
end

-------------------------------------------------------------------------------- reel

local reel = CreateFrame("ScrollFrame", nil, frame);
reel:SetSize(REEL_W, REEL_H);
reel:SetPoint("TOP", 0, -84);
local reelBg = ES.Solid(frame, "BORDER", { 0, 0, 0 }, 0.55);
reelBg:SetPoint("TOPLEFT", reel, -1, 1); reelBg:SetPoint("BOTTOMRIGHT", reel, 1, -1);
local strip = CreateFrame("Frame", nil, reel);
strip:SetSize(REEL_LEN * STEP, REEL_H);
reel:SetScrollChild(strip);
local reelCards = {};
for i = 1, REEL_LEN do
    local card = makeCard(strip, CARD_W, CARD_H, 56);
    card:SetPoint("LEFT", (i - 1) * STEP + 4, 0);
    reelCards[i] = card;
end

local over = CreateFrame("Frame", nil, frame);
over:SetAllPoints(reel);
over:SetFrameLevel(reel:GetFrameLevel() + 5);
for _, side in ipairs({ "LEFT", "RIGHT" }) do
    local fade = ES.Solid(over, "OVERLAY", ES.INK, 1);
    fade:SetPoint("TOP" .. side); fade:SetPoint("BOTTOM" .. side); fade:SetWidth(110);
    local a, b = 1, 0;
    if side == "RIGHT" then a, b = 0, 1; end
    fade:SetGradientAlpha("HORIZONTAL", 0.03, 0.04, 0.055, a, 0.03, 0.04, 0.055, b);
end
local pointer = ES.Solid(over, "OVERLAY", ES.GOLD, 1);
pointer:SetPoint("TOP", 0, 6); pointer:SetPoint("BOTTOM", 0, -6); pointer:SetWidth(3);
for _, side in ipairs({ "TOP", "BOTTOM" }) do
    local cap = ES.Solid(over, "OVERLAY", ES.GOLD, 1);
    cap:SetSize(16, 4);
    cap:SetPoint(side, pointer, side, 0, 0);
end

local function weightedReward()
    local roll = math.random(1, state.total);
    for _, reward in ipairs(state.rewards) do
        if roll <= reward.weight then return reward; end
        roll = roll - reward.weight;
    end
    return state.rewards[#state.rewards];
end

-- Offset that puts the middle of card i (plus a visual nudge) under the pointer.
local function offsetFor(i, nudge) return (i - 1) * STEP + 4 + CARD_W / 2 + (nudge or 0) - REEL_W / 2; end

local function fillReel(winner)
    -- Filler cards are decoration only: the winning card was chosen by the server.
    for i = 1, REEL_LEN do
        fillCard(reelCards[i], (winner and i == WIN_AT) and winner or weightedReward(), false);
    end
end

-------------------------------------------------------------------------------- action button, status, grid, footer

local action = CreateFrame("Button", nil, frame);
action:SetSize(310, 46);
action:SetPoint("TOP", reel, "BOTTOM", 0, -22);
action.bg = ES.Solid(action, "BACKGROUND", GREEN, 1);
action.bg:SetAllPoints();
action.edge = ES.Border(action, { 1, 1, 1 }, 0.25);
action.text = ES.Text(action, 15, { 1, 1, 1 });
action.text:SetPoint("CENTER");
action:SetScript("OnEnter", function(self) if self:IsEnabled() == 1 then ES.Tint(self.edge, ES.GOLD, 0.8); end end);
action:SetScript("OnLeave", function(self) ES.Tint(self.edge, { 1, 1, 1 }, 0.25); end);

local skip = CreateFrame("Button", nil, frame);
skip:SetSize(70, 24);
skip:SetPoint("LEFT", action, "RIGHT", 14, 0);
skip.text = ES.Text(skip, 11, ES.MUTED);
skip.text:SetPoint("CENTER");
skip.text:SetText("Skip >>");
skip:Hide();

local status = ES.Text(frame, 11, ES.BAD);
status:SetPoint("TOP", action, "BOTTOM", 0, -6);

local contains = ES.Text(frame, 11, ES.MUTED);
contains:SetPoint("TOP", action, "BOTTOM", 0, -24);
contains:SetText(spaced("CONTAINS ONE OF THE FOLLOWING"));

local grid = CreateFrame("Frame", nil, frame);
grid:SetSize(W - 40, 290);
grid:SetPoint("TOP", contains, "BOTTOM", 0, -10);
local gridCards = {};

local odds = ES.Text(frame, 10, ES.MUTED);
odds:SetPoint("BOTTOMLEFT", 20, 14);
local gold = ES.Text(frame, 11, ES.TEXT);
gold:SetPoint("BOTTOMRIGHT", -20, 14);

local function checkbox(label, key, anchor)
    local box = CreateFrame("CheckButton", nil, frame, "UICheckButtonTemplate");
    box:SetSize(22, 22);
    box:SetPoint("LEFT", anchor, "RIGHT", 14, 0);
    local text = ES.Text(box, 10, ES.MUTED);
    text:SetPoint("LEFT", box, "RIGHT", 2, 0);
    text:SetText(label);
    box:SetScript("OnShow", function(self) self:SetChecked(db[key]); end);
    box:SetScript("OnClick", function(self) db[key] = self:GetChecked() and true or false; ES.reduceMotion = db.reduceMotion; end);
    return text;
end
local motionLabel = checkbox("Reduce motion", "reduceMotion", odds);
checkbox("Mute", "mute", motionLabel);

-------------------------------------------------------------------------------- reveal

local reveal = CreateFrame("Frame", nil, frame);
reveal:SetAllPoints();
reveal:SetFrameLevel(frame:GetFrameLevel() + 20);
reveal:EnableMouse(true);
reveal:Hide();
ES.Solid(reveal, "BACKGROUND", { 0, 0, 0 }, 0.86):SetAllPoints();
local rays = reveal:CreateTexture(nil, "BORDER");
rays:SetTexture("Interface\\Cooldown\\starburst");
rays:SetBlendMode("ADD");
rays:SetSize(420, 420);
rays:SetPoint("CENTER", 0, 110);
local rays2 = reveal:CreateTexture(nil, "BORDER");
rays2:SetTexture("Interface\\Cooldown\\star4");
rays2:SetBlendMode("ADD");
rays2:SetSize(300, 300);
rays2:SetPoint("CENTER", rays);
local unboxed = ES.Text(reveal, 11, ES.MUTED);
unboxed:SetPoint("TOP", 0, -60);
unboxed:SetText(spaced("YOU UNBOXED"));
local prizeFrame = CreateFrame("Frame", nil, reveal);
prizeFrame:SetSize(84, 84);
prizeFrame:SetPoint("CENTER", rays);
local prizeIcon = prizeFrame:CreateTexture(nil, "ARTWORK");
prizeIcon:SetAllPoints();
local prizeEdge = ES.Border(prizeFrame, ES.GOLD, 1);
local prizeGrade = ES.Text(reveal, 24, ES.GOLD);
prizeGrade:SetPoint("TOP", prizeFrame, "BOTTOM", 0, -70);
local prizeName = ES.Text(reveal, 20, ES.TEXT);
prizeName:SetPoint("TOP", prizeGrade, "BOTTOM", 0, -14);
local prizeFrom = ES.Text(reveal, 11, ES.MUTED);
prizeFrom:SetPoint("TOP", prizeName, "BOTTOM", 0, -8);
local prizeNote = ES.Text(reveal, 11, ES.GOLD);
prizeNote:SetPoint("TOP", prizeFrom, "BOTTOM", 0, -6);

local function revealButton(color, width)
    local b = CreateFrame("Button", nil, reveal);
    b:SetSize(width, 36);
    ES.Solid(b, "BACKGROUND", color, 1):SetAllPoints();
    b.edge = ES.Border(b, { 1, 1, 1 }, 0.25);
    b.text = ES.Text(b, 12, { 1, 1, 1 });
    b.text:SetPoint("CENTER");
    return b;
end
local again = revealButton(GREEN, 230);
again:SetPoint("TOPRIGHT", prizeNote, "BOTTOM", -6, -22);
local done = revealButton(GREY, 110);
done:SetPoint("LEFT", again, "RIGHT", 12, 0);
done.text:SetText(spaced("CLOSE"));

local spinAngle = 0;
reveal:SetScript("OnUpdate", function(_, elapsed)
    if db.reduceMotion then return; end
    spinAngle = spinAngle + elapsed * 0.35;
    rays:SetRotation(spinAngle);
    rays2:SetRotation(-spinAngle * 1.6);
end);

-------------------------------------------------------------------------------- state -> screen

local function canOpen() return state.crates > 0 and state.keys > 0; end

local function refreshAction()
    local enabled, color, text = true, GREEN, spaced("UNLOCK");
    if phase == "pending" or phase == "spin" then
        enabled, color, text = false, GREY, spaced("UNLOCKING") .. "...";
    elseif state.crates < 1 then
        enabled, color, text = false, GREY, spaced("NO CRATES LEFT");
    elseif state.keys < 1 then
        color, text = { 0.45, 0.33, 0.08 }, "Buy " .. (state.keyName or "Key") .. "  " .. GetCoinTextureString(state.price);
        if GetMoney() < state.price then
            enabled = false;
            status:SetText("You need " .. GetCoinTextureString(state.price) .. " for a key.");
        end
    end
    action.text:SetText(text);
    action.bg:SetVertexColor(color[1], color[2], color[3], 1);
    if enabled then action:Enable(); action.text:SetAlpha(1); else action:Disable(); action.text:SetAlpha(0.55); end
end

local function render()
    if not state.crate or not state.total then return; end
    waiting = false;
    local name, icon = itemInfo(state.crate);
    local keyName, keyIcon = itemInfo(state.key);
    state.crateName, state.keyName = name, keyName;
    title:SetText(name);
    crateIcon:SetTexture(icon);
    bagCount:SetText(state.crates .. " in your bags");
    keyCount:SetText("|T" .. keyIcon .. ":14|t " .. keyName .. ": " .. state.keys);
    odds:SetText(string.format("Exact odds: weight / %d.  Pool %d.", state.total, state.pool));
    gold:SetText(GetCoinTextureString(GetMoney()));

    local n = #state.rewards;
    for i = 1, n do
        local card = gridCards[i] or makeCard(grid, GRID_W, GRID_H, 44);
        gridCards[i] = card;
        local row, col = math.floor((i - 1) / PER_ROW), (i - 1) % PER_ROW;
        local inRow = math.min(PER_ROW, n - row * PER_ROW);
        card:ClearAllPoints();
        card:SetPoint("TOPLEFT", grid, "TOP", (col - inRow / 2) * GRID_STEP + 4, -row * (GRID_H + 10));
        fillCard(card, state.rewards[i], true);
    end
    for i = n + 1, #gridCards do gridCards[i]:Hide(); end
    if phase == "idle" and not state.reelFilled then
        fillReel();
        reel:SetHorizontalScroll(offsetFor(WIN_AT - 3));
        state.reelFilled = true;
    end
    refreshAction();
end

-------------------------------------------------------------------------------- opening

local spin = { t = 0 };

local function showReveal()
    phase = "reveal";
    skip:Hide();
    local w = state.won;
    local name, icon = itemInfo(w.item);
    local c = rarityColor(w.rarity);
    rays:SetVertexColor(c[1], c[2], c[3], w.rarity >= 4 and 0.95 or 0.5);
    rays2:SetVertexColor(c[1], c[2], c[3], w.rarity >= 5 and 0.9 or 0.35);
    if w.rarity >= 4 then rays2:Show(); else rays2:Hide(); end
    prizeIcon:SetTexture(icon);
    ES.Tint(prizeEdge, c, 1);
    prizeGrade:SetText(spaced(string.upper(rarityName(w.rarity))));
    prizeGrade:SetTextColor(c[1], c[2], c[3]);
    prizeName:SetText(w.count > 1 and (name .. " x" .. w.count) or name);
    prizeName:SetTextColor(c[1], c[2], c[3]);
    prizeFrom:SetText("from |cffff9933" .. (state.crateName or "") .. "|r");
    prizeNote:SetText(w.mailed and "Your bags were full: it is in your mailbox." or "");
    again.text:SetText(spaced("OPEN ANOTHER") .. "  (" .. state.crates .. " left)");
    if canOpen() then again:Enable(); again:SetAlpha(1); else again:Disable(); again:SetAlpha(0.45); end
    reveal:Show();
    ES.Tween("crate-reveal", 0.35, 0, function(p)
        reveal:SetAlpha(p);
        prizeFrame:SetScale(0.6 + 0.4 * p);
    end);
    if w.rarity >= 5 then sound("LEVELUPSOUND");
    elseif w.rarity >= 4 then sound("igQuestListComplete");
    else sound("igMainMenuOptionCheckBoxOn"); end
end

local function finishSpin()
    reel:SetScript("OnUpdate", nil);
    reel:SetHorizontalScroll(spin.to);
    for i = 1, REEL_LEN do if i ~= WIN_AT then reelCards[i]:SetAlpha(0.35); end end
    ES.Tween("crate-settle", 0.6, 0, function(p) if p >= 1 and phase == "spin" then showReveal(); end end);
end

local function spinUpdate(_, elapsed)
    spin.t = spin.t + elapsed;
    local p = math.min(1, spin.t / spin.d);
    local eased = 1 - (1 - p) ^ 4;
    local x = spin.from + (spin.to - spin.from) * eased;
    reel:SetHorizontalScroll(x);
    local under = math.floor((x + REEL_W / 2 - 4) / STEP);
    if under ~= spin.last then
        spin.last = under;
        sound("igMainMenuOptionCheckBoxOn");
    end
    if p >= 1 then finishSpin(); end
end

local function startSpin(won)
    phase = "spin";
    state.won = won;
    fillReel(won);
    spin.t, spin.last = 0, nil;
    spin.d = db.reduceMotion and 0.4 or SPIN_TIME;
    spin.from = offsetFor(4);
    spin.to = offsetFor(WIN_AT, math.random(-CARD_W / 2 + 14, CARD_W / 2 - 14));
    reel:SetHorizontalScroll(spin.from);
    skip:Show();
    reel:SetScript("OnUpdate", spinUpdate);
    refreshAction();
end

local function request(kind, body)
    local req = nextRequest();
    pendingReq, pendingAt, pendingKind = req, GetTime(), kind;
    send(body .. ":" .. req);
end

local function open()
    if phase ~= "idle" or not canOpen() then return; end
    phase = "pending";
    reveal:Hide();
    status:SetText("");
    refreshAction();
    request("open", "O:" .. state.crate);
end

StaticPopupDialogs["ESTERIA_CRATES_BUY_KEY"] = {
    text = "Buy one %s for %s?\n\nThis gold is removed from the game.",
    button1 = YES,
    button2 = NO,
    OnAccept = function() status:SetText(""); request("buy", "B:" .. state.crate .. ":1:" .. state.price); end,
    timeout = 0,
    whileDead = 1,
    hideOnEscape = 1,
};

action:SetScript("OnClick", function()
    if phase ~= "idle" then return; end
    if state.keys < 1 then
        StaticPopup_Show("ESTERIA_CRATES_BUY_KEY", state.keyName or "key", GetCoinTextureString(state.price));
    else
        open();
    end
end);
skip:SetScript("OnClick", function() if phase == "spin" then spin.t = spin.d; end end);
again:SetScript("OnClick", function()
    reveal:Hide();
    phase = "idle";
    open();
end);
done:SetScript("OnClick", function() frame:Hide(); end);

frame:SetScript("OnHide", function()
    -- The reward is already delivered; closing only stops the show.
    reel:SetScript("OnUpdate", nil);
    reveal:Hide();
    skip:Hide();
    if phase ~= "pending" then phase = "idle"; end
    state.reelFilled = false;
end);

local ERRORS = {
    nocrate = "You have no crate of this kind left.",
    nokey = "You need a matching key.",
    state = "You can't do that right now (combat, dead, or inventory busy).",
    money = "You don't have enough gold.",
    bags = "Your bags are full.",
    price = "The key price changed. Check the new price and try again.",
    quantity = "You can't buy that many keys at once.",
    busy = "Too fast. Try again.",
};

-------------------------------------------------------------------------------- messages

local handlers = {};
function handlers.OPEN(crate)
    if state.crate ~= tonumber(crate) then state.reelFilled = false; end
    state.crate = tonumber(crate);
    phase = "idle";
    frame:Show();
end
function handlers.H(crate, key, price, crates, keys, pool, total, n, maxBuy)
    if tonumber(crate) ~= state.crate then return; end
    state.key, state.price = tonumber(key), tonumber(price);
    state.crates, state.keys = tonumber(crates), tonumber(keys);
    state.pool, state.total, state.n, state.maxBuy = tonumber(pool), tonumber(total), tonumber(n), tonumber(maxBuy);
    state.incoming = {};
end
function handlers.R(crate, idx, item, count, weight, rarity, known)
    if tonumber(crate) ~= state.crate or not state.incoming then return; end
    state.incoming[tonumber(idx) + 1] = { item = tonumber(item), count = tonumber(count), weight = tonumber(weight),
        rarity = tonumber(rarity), known = known == "1" };
end
function handlers.RE(crate)
    if tonumber(crate) ~= state.crate or not state.incoming then return; end
    state.rewards, state.incoming = state.incoming, nil;
    render();
end
function handlers.W(req, crate, idx, item, count, mailed)
    if req ~= pendingReq then return; end
    pendingReq = nil;
    local won = {};
    for k, v in pairs(state.rewards[tonumber(idx) + 1] or { item = tonumber(item), count = tonumber(count),
        weight = 0, rarity = 4 }) do won[k] = v; end
    won.mailed = mailed == "1";
    startSpin(won);
end
function handlers.K(req, crate, qty)
    if req ~= pendingReq then return; end
    pendingReq = nil;
    sound("LOOTWINDOWCOINSOUND");
    status:SetTextColor(ES.GOOD[1], ES.GOOD[2], ES.GOOD[3]);
    status:SetText("Bought " .. qty .. " key" .. (qty == "1" and "." or "s."));
end
function handlers.E(req, code)
    if req ~= pendingReq then return; end
    pendingReq = nil;
    if phase == "pending" then phase = "idle"; end
    refreshAction();
    status:SetTextColor(ES.BAD[1], ES.BAD[2], ES.BAD[3]);
    status:SetText(ERRORS[code] or ("Refused: " .. code));
end

local driver = CreateFrame("Frame");
driver:RegisterEvent("PLAYER_LOGIN");
driver:RegisterEvent("CHAT_MSG_ADDON");
driver:RegisterEvent("PLAYER_MONEY");
driver:SetScript("OnEvent", function(_, event, prefix, message, _, sender)
    if event == "PLAYER_LOGIN" then
        EsteriaCratesDB = EsteriaCratesDB or {};
        db = EsteriaCratesDB;
        ES.reduceMotion = db.reduceMotion;
        send("HI");
    elseif event == "PLAYER_MONEY" then
        if frame:IsShown() then render(); end
    elseif prefix == PFX_IN and sender == UnitName("player") and message then
        local args = {};
        for part in (message .. ":"):gmatch("([^:]*):") do args[#args + 1] = part; end
        local handler = handlers[table.remove(args, 1)];
        if handler then handler(unpack(args)); end
    end
end);
driver:SetScript("OnUpdate", function(_, elapsed)
    if pendingReq and GetTime() - pendingAt > ANSWER_TIMEOUT then
        pendingReq = nil;
        if phase == "pending" then phase = "idle"; end
        refreshAction();
        status:SetTextColor(ES.BAD[1], ES.BAD[2], ES.BAD[3]);
        status:SetText("No answer from the server. Your bags show what happened.");
        if state.crate then send("D:" .. state.crate); end
    end
    if waiting and frame:IsShown() then
        waitTimer = waitTimer + elapsed;
        if waitTimer > 0.5 then waitTimer = 0; render(); end
    end
end);
