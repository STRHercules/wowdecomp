-- Esteria Casino: the table window for blackjack, slots and Goblin Dice. Server half: modules/mod-esteria-casino
-- (EsteriaWoW). The server deals, rolls, spins and pays; this file only draws what it is told, animates it, and sends
-- button presses. Every animation is cosmetic: results and balances are already committed when they start.
-- Wire format is documented at the top of EsteriaCasino.cpp.
local ES = EsteriaStyle;
local PFX_OUT, PFX_IN = "ECAS", "ECAR";
local ART = "Interface\\AddOns\\EsteriaCasino\\Art\\";
local GOLD = 10000;
local RANKS, SUITS = "A23456789TJQK", "CDHS";
local CHIPS = { 1, 5, 10, 25, 100, 500, 1000, 5000 }; -- gold; order matches chips.blp
local BUTTON_ROWS = 16; -- buttons.blp is 16 cells tall
local BUTTON_ROW = { HIT = 0, STAND = 1, DOUBLE = 2, SPLIT = 3, DEAL = 4, CLEAR = 5, REBET = 6, MAX_BET = 7, SPIN = 8,
                     Blank = 9 };
-- Slot symbols in the server's order (Slots.h): copper, silver, gold, ruby, crown, red dragon.
local SYMBOLS = { "INV_Misc_Coin_05", "INV_Misc_Coin_03", "INV_Misc_Coin_01", "INV_Misc_Gem_Ruby_02", "INV_Crown_02",
                  "INV_Misc_Head_Dragon_Red" };
local SYMBOL_NAMES = { "Copper", "Silver", "Gold", "Ruby", "Crown", "Dragon" };
local CALLS = { "UNDER 7", "SEVEN", "OVER 7", "DOUBLES" };
local CUP_FRAMES, TITLE_FRAMES, FX_FRAMES = 24, 19, 24;
local ERRORS = {
    closed = "The tables are closed.",
    playing = "Finish the current hand first.",
    range = "Move closer to the dealer to bet.",
    state = "Reconnect before betting.",
    excluded = "You have excluded yourself from the casino.",
    min = "That bet is below the table minimum.",
    max = "That bet is above the table limit right now.",
    money = "You don't have enough gold.",
    house = "The house can't cover that bet right now.",
    daily = "That would pass your daily wager limit.",
    noround = "No hand is in play.",
    action = "You can't do that now.",
    noseat = "Talk to a dealer to sit down.",
    amount = "That isn't a valid amount.",
    gone = "That challenge or match is gone.",
    account = "You can't play against your own account.",
    host = "The challenger can't cover the stake right now.",
    turn = "It isn't your turn.",
    move = "That move isn't legal.",
    taken = "That seat is taken.",
    tickets = "You have bought the most tickets allowed for this draw.",
    numbers = "Pick four different numbers from 1 to 20.",
    seated = "You're seated at the Hold'em table: stand up there first.",
    declined = "The House declines the draw.",
};
local STATUS = { W = "Win", J = "Blackjack!", U = "Push", L = "Lose", B = "Bust" };
local RETURN_OF = { W = 2, J = 2.5, U = 1 }; -- per hand, for the payout animation only

local state = { game = "bj", min = 0, max = 0, bet = 0, lastBet = 0, playing = false, busy = false, seated = false };

local function send(body) SendAddonMessage(PFX_OUT, body, "WHISPER", UnitName("player")); end

-- Splits on a plain separator and keeps empty fields.
local function split(s, sep)
    local out, start = {}, 1;
    while true do
        local i = string.find(s, sep, start, true);
        if not i then out[#out + 1] = string.sub(s, start); return out; end
        out[#out + 1] = string.sub(s, start, i - 1);
        start = i + 1;
    end
end

local function money(copper) return GetCoinTextureString(copper); end
local function percent(bp) return string.format("%.2f%%", (tonumber(bp) or 0) / 100); end
local function multiple(bp) return string.format("%gx", (tonumber(bp) or 0) / 10000); end
local function lerp(a, b, p) return a + (b - a) * p; end

-- Animation core -----------------------------------------------------------------------------------------------------
-- Tasks run every frame until they return true. A failing task is dropped instead of erroring every frame.
local clock, tasks = 0, {};
local taskDriver = CreateFrame("Frame");
taskDriver:SetScript("OnUpdate", function(_, elapsed)
    clock = clock + elapsed;
    for i = #tasks, 1, -1 do
        local task = tasks[i];
        local ok, finished = pcall(task, elapsed);
        if not ok or finished then table.remove(tasks, i); end
    end
end);
local function addTask(fn) tasks[#tasks + 1] = fn; end

local function after(delay, fn)
    local t = 0;
    addTask(function(dt)
        t = t + dt;
        if t < delay then return false; end
        fn();
        return true;
    end);
end

local function easeOut(p) return 1 - (1 - p) ^ 3; end
local function easeInOut(p) return p < 0.5 and 4 * p ^ 3 or 1 - (-2 * p + 2) ^ 3 / 2; end
local function backOut(p) local c = 1.70158; return 1 + (c + 1) * (p - 1) ^ 3 + c * (p - 1) ^ 2; end

-- apply(p) gets p eased from 0 to 1 over `duration` after `delay`; apply(0) runs at once so the start state shows.
local function tween(duration, delay, ease, apply, done)
    local t = -(delay or 0);
    apply(0);
    addTask(function(dt)
        t = t + dt;
        if t < 0 then return false; end
        local p = math.min(1, t / duration);
        apply((ease or easeOut)(p));
        if p < 1 then return false; end
        if done then done(); end
        return true;
    end);
end

-- Steps a texture through a sprite atlas of cols x rows cells, holds the last frame, then fades it out.
local function playSheet(tex, file, frames, cols, rows, frameTime, hold, done)
    tex:SetTexture(ART .. file);
    tex:SetAlpha(1);
    tex:Show();
    local t = 0;
    addTask(function(dt)
        t = t + dt;
        local i = math.floor(t / frameTime);
        if i >= frames then
            local over = t - frames * frameTime;
            if over >= hold then
                tex:Hide();
                if done then done(); end
                return true;
            end
            tex:SetAlpha(math.min(1, (hold - over) / math.max(hold * 0.35, 0.01)));
            i = frames - 1;
        end
        local c, r = i % cols, math.floor(i / cols);
        tex:SetTexCoord(c / cols, (c + 1) / cols, r / rows, (r + 1) / rows);
        return false;
    end);
end

-- Window -------------------------------------------------------------------------------------------------------------
local frame = CreateFrame("Frame", "EsteriaCasinoFrame", UIParent);
frame:SetSize(800, 560);
frame:SetPoint("CENTER");
frame:SetFrameStrata("DIALOG");
frame:EnableMouse(true);
frame:SetMovable(true);
frame:RegisterForDrag("LeftButton");
frame:SetScript("OnDragStart", frame.StartMoving);
frame:SetScript("OnDragStop", frame.StopMovingOrSizing);
frame:Hide();
tinsert(UISpecialFrames, "EsteriaCasinoFrame");

local bg = ES.Solid(frame, "BACKGROUND", ES.INK, 0.96);
bg:SetAllPoints();
ES.Border(frame, ES.GOLD_DIM, 1);

-- One felt-sized panel per game; only the open game's panel shows. Positions inside a panel are measured from its
-- top centre; PANEL_TOP converts them to the frame's bottom-left space, where flying chips and titles live.
local FELT_W, FELT_H = 784, 441; -- 16:9: every backdrop atlas is squashed to 1024x512
local PANEL_TOP_X, PANEL_TOP_Y = 400, 560 - 36;
local function toFrame(x, y) return PANEL_TOP_X + x, PANEL_TOP_Y + y; end

local panels = {};
local function panel(game, texture)
    local p = CreateFrame("Frame", nil, frame);
    p:SetPoint("TOPLEFT", 8, -36);
    p:SetSize(FELT_W, FELT_H);
    local back = p:CreateTexture(nil, "BACKGROUND");
    back:SetAllPoints();
    back:SetTexture(ART .. texture);
    p.back = back;
    p:Hide();
    panels[game] = p;
    return p;
end
local bjPanel, slotPanel, dicePanel = panel("bj", "table"), panel("slots", "slots"), panel("dice", "dicetable");

-- Everything that flies or celebrates is drawn on this layer, above the felt and its buttons.
local overlay = CreateFrame("Frame", nil, frame);
overlay:SetAllPoints();
overlay:SetFrameLevel(frame:GetFrameLevel() + 30);

local title = ES.Text(frame, 16, ES.GOLD);
title:SetPoint("TOPLEFT", 14, -10);

local rules = ES.Text(frame, 11, ES.MUTED);
rules:SetPoint("LEFT", title, "RIGHT", 14, -1);

local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton");
close:SetPoint("TOPRIGHT", 2, 2);

local titleTex = overlay:CreateTexture(nil, "OVERLAY");
titleTex:SetSize(400, 100);
titleTex:SetPoint("CENTER", overlay, "BOTTOMLEFT", toFrame(0, -170));
titleTex:Hide();
local fxTex = overlay:CreateTexture(nil, "ARTWORK");
fxTex:SetSize(240, 240);
fxTex:Hide();

-- Result banner below the title, on a dark strip so it reads over the printed table text.
local bannerFrame = CreateFrame("Frame", nil, overlay);
bannerFrame:SetSize(440, 36);
bannerFrame:SetPoint("CENTER", overlay, "BOTTOMLEFT", toFrame(0, -232));
local bannerBack = ES.Solid(bannerFrame, "BACKGROUND", ES.INK, 0.72);
bannerBack:SetAllPoints();
local banner = ES.Text(bannerFrame, 20, ES.GOLD);
banner:SetPoint("CENTER");
bannerFrame:Hide();
local function setBanner(text)
    banner:SetText(text or "");
    if text and text ~= "" then
        bannerFrame:Show();
        tween(0.3, 0, nil, function(p) bannerFrame:SetAlpha(p); end);
    else
        bannerFrame:Hide();
    end
end

-- titleName: you-win art file suffix ("youwin", "blackjack", ...); fxName: "coin", "confetti", "fireworks", "glow".
local function celebrate(titleName, fxName, x, y)
    if titleName then playSheet(titleTex, "title_" .. titleName, TITLE_FRAMES, 4, 8, 0.11, 1.6); end
    if fxName then
        fxTex:ClearAllPoints();
        fxTex:SetPoint("CENTER", overlay, "BOTTOMLEFT", toFrame(x or 0, y or -170));
        playSheet(fxTex, "fx_" .. fxName, FX_FRAMES, 8, 4, 0.06, 0.2);
    end
end

local notice = ES.Text(overlay, 12, ES.BAD);
notice:SetPoint("BOTTOM", overlay, "BOTTOMLEFT", toFrame(0, -FELT_H + 8));

-- Buttons ------------------------------------------------------------------------------------------------------------
local function atlasButton(name, parent, label, onClick, text)
    local b = CreateFrame("Button", name, parent);
    b:SetSize(112, 56);
    local row = BUTTON_ROW[label];
    for col, setter in ipairs({ "SetNormalTexture", "SetPushedTexture", "SetDisabledTexture" }) do
        b[setter](b, ART .. "buttons");
        local tex = (col == 1 and b:GetNormalTexture()) or (col == 2 and b:GetPushedTexture()) or b:GetDisabledTexture();
        if tex then tex:SetTexCoord((col - 1) * 0.25, col * 0.25, row / BUTTON_ROWS, (row + 1) / BUTTON_ROWS); end
    end
    if text then
        b.label = ES.Text(b, 13, { 1, 1, 1 });
        b.label:SetPoint("CENTER", 0, 2);
        b.label:SetText(text);
    end
    b:SetScript("OnClick", onClick);
    return b;
end

local function enable(button, on)
    if on then button:Enable(); else button:Disable(); end
    if button.label then button.label:SetAlpha(on and 1 or 0.45); end
end

-- Chips and stacks ---------------------------------------------------------------------------------------------------
local function chipCoords(index)
    local c, r = (index - 1) % 4, math.floor((index - 1) / 4);
    return c * 0.25, (c + 1) * 0.25, r * 0.5, (r + 1) * 0.5;
end

-- Chips that make up a stake, largest first (the bottom of the stack), at most 8.
local function breakdown(copper)
    local out, gold = {}, math.floor(copper / GOLD);
    for index = #CHIPS, 1, -1 do
        while gold >= CHIPS[index] and #out < 8 do
            out[#out + 1] = index;
            gold = gold - CHIPS[index];
        end
    end
    if #out == 0 and copper > 0 then out[1] = 1; end
    return out;
end

local CHIP_STEP = 4;
local function newStack(parent, x, y, size)
    return { parent = parent, homeX = x, homeY = y, x = x, y = y, size = size, tex = {}, copper = 0 };
end

local function layoutStack(st)
    for k, tex in ipairs(st.tex) do
        tex:ClearAllPoints();
        tex:SetPoint("CENTER", st.parent, "TOP", st.x, st.y + (k - 1) * CHIP_STEP);
    end
end

local function drawStack(st, copper)
    st.copper = copper;
    local chips = breakdown(copper);
    for k, index in ipairs(chips) do
        local tex = st.tex[k];
        if not tex then
            tex = st.parent:CreateTexture(nil, "BORDER");
            tex:SetTexture(ART .. "chips");
            st.tex[k] = tex;
        end
        tex:SetSize(st.size, st.size);
        tex:SetTexCoord(chipCoords(index));
        tex:SetAlpha(1);
        tex:Show();
    end
    for k = #chips + 1, #st.tex do st.tex[k]:Hide(); end
    layoutStack(st);
end

-- The top chip's centre in frame space, where a thrown chip lands.
local function stackTop(st)
    local x, y = toFrame(st.x, st.y);
    return x, y + math.max(0, #breakdown(st.copper) - 1) * CHIP_STEP;
end

-- Slides a whole stack to (x, y) in panel space while fading it, then puts it back home, empty.
local function slideStack(st, x, y, delay, done)
    local fromX, fromY = st.x, st.y;
    tween(0.45, delay, easeInOut, function(p)
        st.x, st.y = lerp(fromX, x, p), lerp(fromY, y, p);
        layoutStack(st);
        for _, tex in ipairs(st.tex) do tex:SetAlpha(1 - p * p); end
    end, function()
        st.x, st.y = st.homeX, st.homeY;
        drawStack(st, 0);
        if done then done(); end
    end);
end

local flyPool = {};
local function flyChip(index, fromX, fromY, toX, toY, delay, fade, done)
    local tex;
    for _, t in ipairs(flyPool) do if not t.busy then tex = t; break; end end
    if not tex then
        tex = overlay:CreateTexture(nil, "OVERLAY");
        tex:SetTexture(ART .. "chips");
        flyPool[#flyPool + 1] = tex;
    end
    tex.busy = true;
    tex:SetSize(36, 36);
    tex:SetTexCoord(chipCoords(index));
    tex:Hide();
    tween(0.3, delay, easeOut, function(p)
        tex:ClearAllPoints();
        -- A small arc: the chip rises a little mid-flight.
        tex:SetPoint("CENTER", overlay, "BOTTOMLEFT", lerp(fromX, toX, p), lerp(fromY, toY, p) + math.sin(p * math.pi) * 24);
        tex:SetAlpha(fade and (1 - p) or 1);
        if p > 0 then tex:Show(); end
    end, function()
        tex:Hide();
        tex.busy = false;
        if done then done(); end
    end);
end

local function rackPoint(index) return 36 + (index - 1) * 48, 42; end

-- Betting bar: chips, bet, clear, rebet. Each game adds its own play button(s).
local betText = ES.Text(frame, 14, ES.GOLD);
betText:SetPoint("BOTTOMLEFT", frame, "BOTTOMLEFT", 16, 82);
local limitText = ES.Text(frame, 11, ES.MUTED);
limitText:SetPoint("LEFT", betText, "RIGHT", 12, 0);

local playButtons = {}; -- every button that places a bet; enabled together when the bet is valid
local deal, spin, clear, rebet;
local chipButtons = {};

-- Where each game shows the stake on its felt, and where the house pays from.
local stacks = {
    bj = { newStack(bjPanel, 0, -339, 36), newStack(bjPanel, 62, -339, 32) },
    slots = { newStack(slotPanel, -336, -280, 36) },
    dice = { newStack(dicePanel, -250, -165, 36) },
};
local HOUSE = { bj = { 0, -95 }, slots = { -70, -20 }, dice = { 0, -40 } };
local function betStack() return stacks[state.game][1]; end

local function betValid()
    return not state.busy and state.bet >= state.min and state.bet <= state.max and state.bet <= GetMoney();
end

local function refreshBet()
    betText:SetText("Bet: " .. money(state.bet));
    limitText:SetText(string.format("Table %s - %s", money(state.min), money(state.max)));
    for _, b in ipairs(playButtons) do enable(b, betValid()); end
    enable(rebet, not state.busy and state.lastBet > 0);
    enable(clear, not state.busy);
    for _, chip in ipairs(chipButtons) do enable(chip, not state.busy); end
end

-- Sets the pending bet and shows it on the felt: added chips are thrown from the rack, removed ones fly back.
local function setBet(copper, thrownFrom)
    local before = state.bet;
    state.bet = math.max(0, math.min(copper, state.max, GetMoney()));
    refreshBet();
    if state.playing then return; end
    local st = betStack();
    if state.bet > before then
        local chips = breakdown(state.bet - before);
        for k = 1, thrownFrom and 1 or math.min(#chips, 3) do
            local index = thrownFrom or chips[k];
            local fx, fy = rackPoint(index);
            local tx, ty = stackTop(st);
            flyChip(index, fx, fy, tx, ty + CHIP_STEP, (k - 1) * 0.06, false, function()
                if not state.playing then drawStack(st, state.bet); end
                PlaySound("igMainMenuOptionCheckBoxOn");
            end);
        end
    elseif state.bet < before then
        local chips = breakdown(before - state.bet);
        local fx, fy = stackTop(st);
        drawStack(st, state.bet);
        for k = 1, math.min(#chips, 4) do
            local tx, ty = rackPoint(chips[k]);
            flyChip(chips[k], fx, fy, tx, ty, (k - 1) * 0.05, false);
        end
    else
        drawStack(st, state.bet);
    end
end

-- lock: slots and dice stay busy until their animation and payout finish.
local function placeBet(body, lock)
    if not betValid() then return; end
    notice:SetText("");
    setBanner("");
    state.lastBet = state.bet;
    send(body);
    if lock then
        state.busy = true;
        refreshBet();
    end
end

deal = atlasButton("EsteriaCasinoDeal", frame, "DEAL", function() placeBet("B:" .. state.bet); end);
deal:SetPoint("BOTTOMRIGHT", frame, "BOTTOMRIGHT", -14, 14);
spin = atlasButton("EsteriaCasinoSpin", frame, "SPIN", function() placeBet("SP:" .. state.bet, true); end);
spin:SetPoint("BOTTOMRIGHT", frame, "BOTTOMRIGHT", -14, 14);
playButtons[#playButtons + 1] = deal;
playButtons[#playButtons + 1] = spin;
clear = atlasButton("EsteriaCasinoClear", frame, "CLEAR", function() setBet(0); end);
clear:SetPoint("RIGHT", deal, "LEFT", -8, 0);
rebet = atlasButton("EsteriaCasinoRebet", frame, "REBET", function() setBet(state.lastBet); end);
rebet:SetPoint("RIGHT", clear, "LEFT", -8, 0);

for i, gold in ipairs(CHIPS) do
    local chip = CreateFrame("Button", "EsteriaCasinoChip" .. i, frame);
    chip:SetSize(44, 44);
    chip:SetPoint("BOTTOMLEFT", frame, "BOTTOMLEFT", 14 + (i - 1) * 48, 20);
    chip:RegisterForClicks("LeftButtonUp", "RightButtonUp");
    local tex = chip:CreateTexture(nil, "ARTWORK");
    tex:SetAllPoints();
    tex:SetTexture(ART .. "chips");
    tex:SetTexCoord(chipCoords(i));
    chip:SetScript("OnClick", function(_, button)
        notice:SetText("");
        if button == "RightButton" then setBet(state.bet - gold * GOLD); else setBet(state.bet + gold * GOLD, i); end
    end);
    chip:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_TOP");
        GameTooltip:SetText(gold .. " gold");
        GameTooltip:AddLine("Left-click to add, right-click to remove.", 1, 1, 1);
        GameTooltip:Show();
    end);
    chip:SetScript("OnLeave", function() GameTooltip:Hide(); end);
    chipButtons[#chipButtons + 1] = chip;
end

local bettingBar = { deal, spin, clear, rebet, betText, limitText };

-- The stake's journey after a result: winnings fly in from the house, then the stack is collected toward the
-- player's rack; a lost stack slides to the house. `done` runs when the felt is clear again.
local function payout(st, stake, paid, delay, done)
    local house = HOUSE[state.game];
    local hx, hy = toFrame(house[1], house[2]);
    if paid > stake then
        local chips = breakdown(paid - stake);
        local count = math.min(#chips, 4);
        for k = 1, count do
            after(delay, function()
                local tx, ty = stackTop(st);
                flyChip(chips[k], hx, hy, tx, ty + CHIP_STEP, (k - 1) * 0.08, false, function()
                    drawStack(st, stake + math.floor((paid - stake) * k / count));
                end);
            end);
        end
        after(delay + 0.5, function() drawStack(st, paid); PlaySound("LOOTWINDOWCOINSOUND"); end);
        slideStack(st, st.homeX, -FELT_H - 20, delay + 1.6, done);
    elseif paid > 0 then
        slideStack(st, st.homeX, -FELT_H - 20, delay + 1.0, done);
    else
        slideStack(st, house[1], house[2], delay + 0.3, done);
    end
end

-- Blackjack ----------------------------------------------------------------------------------------------------------
-- Table positions come from the layout (Layout_Blackjack_Arc.svg scaled by 784/2048): the dealer's dashed boxes and
-- the one betting circle, measured from the top centre of the felt. Cards are placed by their centres.
local CARD_W, CARD_H = 50, 70;
local DEALER_Y, DEALER_STEP = -95, 58; -- two cards sit exactly in the dashed boxes
local SEAT_Y, HAND_Y, HAND_FAN, SPLIT_X = -339, -286, 16, 62;
local SHOE_X, SHOE_Y, DISCARD_X, DISCARD_Y = 330, -40, -330, -40;
local DEAL_GAP = 0.2;

local function cardCoords(code)
    local col, row = 0, 4;
    if code ~= "??" then
        col = (string.find(RANKS, string.sub(code, 1, 1), 1, true) or 1) - 1;
        row = (string.find(SUITS, string.sub(code, 2, 2), 1, true) or 1) - 1;
    end
    local l, t = col * 128 / 2048, row * 180 / 1024;
    return l, l + 128 / 2048, t, t + 180 / 1024;
end

local cards = {};      -- every card texture; alive ones are on the felt
local animEnd = 0;     -- clock time when the last queued card animation lands
local currentRound = nil;

local function placeCard(e, x, y)
    e.x, e.y = x, y;
    e.frame:ClearAllPoints();
    e.frame:SetPoint("CENTER", bjPanel, "TOP", x, y);
end

-- Each card is its own frame: overlapping textures in one layer have no reliable order in this client, but frame
-- levels do, so a card's place in its row sets its level and later cards cover earlier ones.
local function newCard()
    for _, e in ipairs(cards) do if not e.alive and not e.leaving then return e; end end
    local e = { frame = CreateFrame("Frame", nil, bjPanel) };
    e.frame:SetSize(CARD_W, CARD_H);
    e.tex = e.frame:CreateTexture(nil, "ARTWORK");
    e.tex:SetTexture(ART .. "cards");
    e.tex:SetSize(CARD_W, CARD_H);
    e.tex:SetPoint("CENTER");
    cards[#cards + 1] = e;
    return e;
end

-- Turns a card over: it narrows to an edge, changes face, and widens again.
local function flip(e, code, delay)
    tween(0.09, delay, nil, function(p) e.tex:SetWidth(math.max(1, CARD_W * (1 - p))); end, function()
        e.tex:SetTexCoord(cardCoords(code));
        tween(0.09, 0, nil, function(p) e.tex:SetWidth(math.max(1, CARD_W * p)); end);
    end);
    e.code = code;
end

local function moveCard(e, x, y, delay)
    local fromX, fromY = e.x, e.y;
    tween(0.25, delay, easeInOut, function(p) placeCard(e, lerp(fromX, x, p), lerp(fromY, y, p)); end);
end

-- A new card leaves the shoe face down, lands, and turns face up unless it's the dealer's hole card.
local function dealCard(e, code, x, y, delay)
    e.alive, e.code = true, code;
    e.tex:SetTexCoord(cardCoords("??"));
    e.tex:SetWidth(CARD_W);
    e.tex:SetAlpha(1);
    e.tex:Hide();
    tween(0.28, delay, easeOut, function(p)
        placeCard(e, lerp(SHOE_X, x, p), lerp(SHOE_Y, y, p));
        if p > 0 then e.tex:Show(); end
    end, function()
        PlaySound("igMainMenuOptionCheckBoxOn");
        if code ~= "??" then e.code = "??"; flip(e, code, 0); end
    end);
end

-- Sweeps every card on the felt into the discard pile.
local function sweepCards()
    for _, e in ipairs(cards) do
        if e.alive then
            e.alive, e.leaving = false, true;
            local fromX, fromY = e.x, e.y;
            tween(0.3, 0, easeInOut, function(p)
                placeCard(e, lerp(fromX, DISCARD_X, p), lerp(fromY, DISCARD_Y, p));
                e.tex:SetAlpha(1 - p);
            end, function() e.tex:Hide(); e.leaving = false; end);
        end
    end
end

local dealerTotal = ES.Text(bjPanel, 14, ES.TEXT);
local handLabels = {};
local function handLabel(i)
    if not handLabels[i] then handLabels[i] = ES.Text(bjPanel, 13, ES.TEXT); end
    return handLabels[i];
end

local function clearLabels()
    for _, label in ipairs(handLabels) do label:SetText(""); end
    dealerTotal:SetText("");
end

local actionButtons = {};
for i, spec in ipairs({ { "HIT", "H" }, { "STAND", "S" }, { "DOUBLE", "D" }, { "SPLIT", "P" } }) do
    local b = atlasButton("EsteriaCasino" .. spec[1], frame, spec[1], function() send(spec[2]); end);
    b:SetPoint("BOTTOM", frame, "BOTTOM", (i - 2.5) * 124, 14);
    b.code = spec[2];
    b:Hide();
    actionButtons[#actionButtons + 1] = b;
end

-- Card centres for a row of `n` around cx.
local function rowX(n, cx, step, i) return cx - step * (n - 1) / 2 + (i - 1) * step; end

local lastHands = {};

-- Brings the felt to the server's table: keeps cards that are already in place, slides moved ones (a split),
-- turns the hole card, and deals the rest from the shoe one after another. Labels update once the cards land.
local function renderTable(f)
    local id = f[2];
    local fresh = id ~= currentRound;
    if fresh then
        sweepCards();
        clearLabels();
        currentRound = id;
        lastHands = {};
        animEnd = clock + 0.3;
    end
    local finished = f[3] == "2";
    local active = (tonumber(f[4]) or 0) + 1;
    local dealer = split(f[5], ",");
    local hands = {};
    for i, spec in ipairs(split(f[7], ";")) do hands[i] = split(spec, "|"); end

    -- Wanted slots, in the order a dealer would deal them.
    local slots = {};
    local function add(key, code, x, y)
        slots[#slots + 1] = { key = key, code = code, x = x, y = y, k = tonumber(string.match(key, "(%d+)$")) };
    end
    local function handCards(i)
        local h = hands[i];
        local list = split(h[1], ",");
        local cx = #hands == 1 and 0 or (i == 1 and -SPLIT_X or SPLIT_X);
        return list, cx;
    end
    if fresh and #hands == 1 then
        local list = handCards(1);
        local n = #split(hands[1][1], ",");
        add("h1_1", list[1], rowX(n, 0, HAND_FAN, 1), HAND_Y);
        add("d1", dealer[1], rowX(#dealer, 0, DEALER_STEP, 1), DEALER_Y);
        for k = 2, n do add("h1_" .. k, list[k], rowX(n, 0, HAND_FAN, k), HAND_Y); end
        for k = 2, #dealer do add("d" .. k, dealer[k], rowX(#dealer, 0, DEALER_STEP, k), DEALER_Y); end
    else
        for i = 1, #hands do
            local list, cx = handCards(i);
            for k, code in ipairs(list) do add("h" .. i .. "_" .. k, code, rowX(#list, cx, HAND_FAN, k), HAND_Y); end
        end
        for k, code in ipairs(dealer) do add("d" .. k, code, rowX(#dealer, 0, DEALER_STEP, k), DEALER_Y); end
    end

    local old = {};
    for _, e in ipairs(cards) do if e.alive then old[#old + 1] = e; e.claimed = false; end end
    local function claim(slot, e) e.claimed, e.key, slot.e = true, slot.key, e; end
    for _, slot in ipairs(slots) do -- same place, same card
        for _, e in ipairs(old) do
            if not e.claimed and e.key == slot.key and e.code == slot.code then claim(slot, e); break; end
        end
    end
    for _, slot in ipairs(slots) do -- a card that moved (split)
        if not slot.e then
            for _, e in ipairs(old) do
                if not e.claimed and e.code == slot.code and slot.code ~= "??" then claim(slot, e); break; end
            end
        end
    end
    for _, slot in ipairs(slots) do -- the hole card turned
        if not slot.e then
            for _, e in ipairs(old) do
                if not e.claimed and e.key == slot.key and e.code == "??" then claim(slot, e); slot.turn = true; break; end
            end
        end
    end

    local t = math.max(0, animEnd - clock);
    for _, slot in ipairs(slots) do
        local e = slot.e;
        if not e then
            e = newCard();
            e.key = slot.key;
            slot.e = e;
            dealCard(e, slot.code, slot.x, slot.y, t);
            t = t + DEAL_GAP;
        elseif slot.turn then
            flip(e, slot.code, t);
            t = t + DEAL_GAP + 0.15;
        elseif e.x ~= slot.x or e.y ~= slot.y then
            moveCard(e, slot.x, slot.y, t);
        end
    end
    for _, e in ipairs(old) do if not e.claimed then e.alive = false; e.tex:Hide(); end end
    for _, slot in ipairs(slots) do slot.e.frame:SetFrameLevel(bjPanel:GetFrameLevel() + slot.k); end
    animEnd = clock + t + 0.3;

    -- Stakes: a raised stake (double) or a new hand (split) gets chips thrown from the rack.
    local st = stacks.bj;
    for i = 1, 2 do
        local h = hands[i];
        local stake = h and tonumber(h[4]) or 0;
        local cx = (#hands == 1) and 0 or (i == 1 and -SPLIT_X or SPLIT_X);
        st[i].homeX, st[i].x = cx, cx;
        local shown = lastHands[i] or (i == 1 and st[1].copper or 0);
        if stake > shown and shown > 0 or (i == 2 and stake > 0 and not lastHands[2]) then
            local index = breakdown(stake - shown)[1] or 1;
            local fx, fy = rackPoint(index);
            local tx, ty = stackTop(st[i]);
            flyChip(index, fx, fy, tx, ty + CHIP_STEP, 0, false, function() drawStack(st[i], stake); end);
        else
            drawStack(st[i], stake);
        end
        lastHands[i] = stake;
    end

    -- Totals once the cards have landed.
    local labelAt = animEnd;
    after(labelAt - clock, function()
        if currentRound ~= id then return; end
        dealerTotal:ClearAllPoints();
        dealerTotal:SetPoint("LEFT", bjPanel, "TOP", rowX(#dealer, 0, DEALER_STEP, #dealer) + CARD_W / 2 + 8, DEALER_Y);
        dealerTotal:SetText(f[6] or "");
        for i, h in ipairs(hands) do
            local label = handLabel(i);
            local cx = #hands == 1 and 0 or (i == 1 and -SPLIT_X or SPLIT_X);
            label:ClearAllPoints();
            label:SetPoint("TOP", bjPanel, "TOP", cx, SEAT_Y - 34);
            local text = (h[3] == "1" and "Soft " or "") .. h[2];
            if STATUS[h[5]] then text = text .. "  " .. STATUS[h[5]]; end
            label:SetText(text);
            local color = (not finished and i == active) and ES.GOLD or ES.TEXT;
            label:SetTextColor(color[1], color[2], color[3]);
        end
    end);
    state.lastTable = hands;
    return finished;
end

-- Slots --------------------------------------------------------------------------------------------------------------
-- Each reel is a clipping scroll frame over four icons; the strip scrolls down and lands with a bounce.
local REEL_X, ICON, ROW = { -220, -70, 80 }, 84, 100;
local reels = {};
local function randomSymbol() return math.random(#SYMBOLS); end

local function layoutReel(reel)
    for k = 0, 3 do
        local y = -ROW / 2 - (k - 1) * ROW + reel.offset;
        local icon = reel.icons[k];
        icon:ClearAllPoints();
        icon:SetPoint("CENTER", reel.child, "TOP", 0, y);
        icon:SetTexture("Interface\\Icons\\" .. SYMBOLS[reel.syms[k]]);
        icon:SetAlpha(1 - math.min(1, math.abs(y + 150) / ROW) * 0.55);
    end
end

for r = 1, 3 do
    local reel = CreateFrame("ScrollFrame", nil, slotPanel);
    reel:SetSize(120, 300);
    reel:SetPoint("CENTER", slotPanel, "TOP", REEL_X[r], -200);
    local back = ES.Solid(reel, "BACKGROUND", ES.INK, 0.9);
    back:SetAllPoints();
    ES.Border(reel, ES.GOLD, 0.9);
    reel.child = CreateFrame("Frame", nil, reel);
    reel.child:SetSize(120, 300);
    reel:SetScrollChild(reel.child);
    reel.icons, reel.syms, reel.offset = {}, {}, 0;
    for k = 0, 3 do
        local icon = reel.child:CreateTexture(nil, "ARTWORK");
        icon:SetSize(ICON, ICON);
        icon:SetTexCoord(0.07, 0.93, 0.07, 0.93);
        reel.icons[k] = icon;
        reel.syms[k] = randomSymbol();
    end
    reel.syms[2] = r + 2;
    layoutReel(reel);
    reels[r] = reel;
end
local payline = ES.Solid(slotPanel, "OVERLAY", ES.GOLD, 0.8);
payline:SetSize(470, 2);
payline:SetPoint("CENTER", slotPanel, "TOP", -70, -200);

-- Marquee lights around the reels: a slow twinkle at rest, a chase while spinning, all flashing on a win.
local lights, lightMode = {}, "idle";
do
    local left, right, top, bottom = -300, 160, -40, -360;
    local function light(x, y)
        local dot = ES.Solid(slotPanel, "ARTWORK", { 1, 0.85, 0.4 }, 1);
        dot:SetSize(7, 7);
        dot:SetPoint("CENTER", slotPanel, "TOP", x, y);
        lights[#lights + 1] = dot;
    end
    for i = 0, 13 do light(lerp(left, right, i / 13), top); end
    for i = 1, 8 do light(right, lerp(top, bottom, i / 9)); end
    for i = 13, 0, -1 do light(lerp(left, right, i / 13), bottom); end
    for i = 8, 1, -1 do light(left, lerp(top, bottom, i / 9)); end
end
addTask(function()
    if not slotPanel:IsShown() then return false; end
    local chase = math.floor(clock * 16);
    for i, dot in ipairs(lights) do
        local a;
        if lightMode == "spin" then
            a = ((i + chase) % 4 == 0) and 1 or 0.18;
        elseif lightMode == "win" then
            a = (math.floor(clock * 7) % 2 == 0) and 1 or 0.25;
        else
            a = 0.35 + 0.3 * math.sin(clock * 2 + i * 0.7);
        end
        dot:SetAlpha(a);
    end
    return false;
end);

local payTitle = ES.Text(slotPanel, 13, ES.GOLD);
payTitle:SetPoint("TOPLEFT", slotPanel, "TOPLEFT", 584, -52);
payTitle:SetText("Pays (stake included)");
local payRows = {};
for i = 1, #SYMBOLS do
    local row = {};
    for k = 1, 3 do
        row[k] = slotPanel:CreateTexture(nil, "ARTWORK");
        row[k]:SetSize(22, 22);
        row[k]:SetPoint("TOPLEFT", slotPanel, "TOPLEFT", 584 + (k - 1) * 24, -54 - i * 30);
        row[k]:SetTexture("Interface\\Icons\\" .. SYMBOLS[#SYMBOLS + 1 - i]);
    end
    row.text = ES.Text(slotPanel, 13, ES.TEXT);
    row.text:SetPoint("LEFT", row[3], "RIGHT", 10, 0);
    payRows[i] = row;
end
local mixedText = ES.Text(slotPanel, 12, ES.TEXT);
mixedText:SetPoint("TOPLEFT", slotPanel, "TOPLEFT", 584, -54 - 7 * 30);
local rtpText = ES.Text(slotPanel, 11, ES.MUTED);
rtpText:SetPoint("TOPLEFT", mixedText, "BOTTOMLEFT", 0, -8);
local slotPays = {};

-- Spins one reel until `stopAt`, then lands `result` on the payline with a bounce.
local function spinReel(reel, stopAt, result, done)
    local t, speed = 0, 0;
    addTask(function(dt)
        t = t + dt;
        if t < stopAt then
            speed = math.min(1600, speed + dt * 6000);
            reel.offset = reel.offset + speed * dt;
            while reel.offset >= ROW do
                reel.offset = reel.offset - ROW;
                for k = 3, 1, -1 do reel.syms[k] = reel.syms[k - 1]; end
                reel.syms[0] = randomSymbol();
            end
            layoutReel(reel);
            return false;
        end
        -- Land: the result enters at the top row and settles one row down, on the line.
        reel.syms[3], reel.syms[2], reel.syms[1], reel.syms[0] = reel.syms[2], reel.syms[1], result, randomSymbol();
        reel.offset = 0;
        tween(0.5, 0, backOut, function(p)
            reel.offset = ROW * p;
            layoutReel(reel);
        end, function()
            reel.offset = 0;
            for k = 3, 1, -1 do reel.syms[k] = reel.syms[k - 1]; end
            reel.syms[0] = randomSymbol();
            layoutReel(reel);
            PlaySound("igMainMenuOptionCheckBoxOn");
            done();
        end);
        return true;
    end);
end

local function pulseWinners()
    tween(1.6, 0, function(p) return p; end, function(p)
        local size = ICON + 16 * math.abs(math.sin(p * math.pi * 4)) * (1 - p);
        for _, reel in ipairs(reels) do reel.icons[2]:SetSize(size, size); end
        payline:SetAlpha(0.5 + 0.5 * math.abs(math.sin(p * math.pi * 6)));
    end, function()
        for _, reel in ipairs(reels) do reel.icons[2]:SetSize(ICON, ICON); end
        payline:SetAlpha(0.8);
    end);
end

-- Dice ---------------------------------------------------------------------------------------------------------------
local cup = dicePanel:CreateTexture(nil, "ARTWORK");
cup:SetTexture(ART .. "cup");
cup:SetSize(180, 180);
cup:SetPoint("CENTER", dicePanel, "TOP", 0, -150);
cup:Hide();
local function cupFrame(i)
    local c, r = i % 8, math.floor(i / 8);
    cup:SetTexCoord(c / 8, (c + 1) / 8, r / 4, (r + 1) / 4); -- 1024x512 atlas of 128px cells
end

local dieTex = {};
for i = 1, 2 do
    dieTex[i] = dicePanel:CreateTexture(nil, "ARTWORK");
    dieTex[i]:SetTexture(ART .. "dice");
    dieTex[i]:SetSize(96, 96);
    dieTex[i]:SetPoint("CENTER", dicePanel, "TOP", (i - 1.5) * 110, -150);
end
local function showDie(i, face)
    dieTex[i]:SetTexCoord((face - 1) * 128 / 1024, face * 128 / 1024, 0, 1);
    dieTex[i]:Show();
end
showDie(1, 6); showDie(2, 1);
local diceSum = ES.Text(dicePanel, 22, ES.GOLD);
diceSum:SetPoint("LEFT", dicePanel, "TOP", 130, -150);

local callButtons = {};
for i, label in ipairs(CALLS) do
    local b = atlasButton("EsteriaCasinoCall" .. i, dicePanel, "Blank", function()
        placeBet("DR:" .. state.bet .. ":" .. (i - 1), true);
    end, label);
    b:SetSize(140, 64);
    b:SetPoint("TOP", dicePanel, "TOP", (i - 2.5) * 156, -300);
    b.odds = ES.Text(dicePanel, 12, ES.TEXT);
    b.odds:SetPoint("TOP", b, "BOTTOM", 0, -2);
    callButtons[i] = b;
    playButtons[#playButtons + 1] = b;
end

-- Results ------------------------------------------------------------------------------------------------------------
local function announce(stake, paid, mailed, titleName, fxName, fxX, fxY)
    local net = paid - stake;
    if net > 0 then
        celebrate(titleName or "youwin", fxName or "coin", fxX, fxY);
        setBanner("+" .. money(net));
    elseif net == 0 and paid > 0 then
        celebrate(titleName == "draw" and "draw" or nil, nil);
        setBanner("Stake returned");
    else
        setBanner("You lose " .. money(-net));
    end
    if mailed then notice:SetText("Your gold is full: the winnings were sent by mail."); end
end

-- Puts blackjack's first stack back on the betting circle (a split moves it aside).
local function homeBetCircle()
    local st = stacks.bj[1];
    st.homeX, st.x = 0, 0;
end

-- Puts the pending bet back on the felt once a result has been collected.
local function settleDone()
    state.busy = false;
    homeBetCircle();
    drawStack(stacks.bj[2], 0);
    drawStack(betStack(), state.bet);
    refreshBet();
end

-- Modes --------------------------------------------------------------------------------------------------------------
local function showMode(playing, actions)
    state.playing = playing;
    local bj = state.game == "bj";
    for _, b in ipairs(actionButtons) do
        if bj and playing then b:Show(); else b:Hide(); end
        enable(b, playing and string.find(actions or "", b.code, 1, true) ~= nil);
    end
    for _, w in ipairs(bettingBar) do if playing then w:Hide(); else w:Show(); end end
    for _, chip in ipairs(chipButtons) do if playing then chip:Hide(); else chip:Show(); end end
    if not playing then
        if bj then deal:Show(); else deal:Hide(); end
        if state.game == "slots" then spin:Show(); else spin:Hide(); end
        refreshBet();
    end
end

-- Messages -----------------------------------------------------------------------------------------------------------
-- Chess, checkers, poker and the lottery live in their own files and register here (EsteriaCasinoCore below):
-- each brings a panel, open(f) for its O message, close() for when the window hides, and its own handlers.
local modules = {};
local handlers = {};

local function openModule(f)
    local module = modules[f[2]];
    state.game, state.busy, state.playing = f[2], false, false;
    for game, p in pairs(panels) do if game == state.game then p:Show(); else p:Hide(); end end
    for _, w in ipairs(bettingBar) do w:Hide(); end
    for _, chip in ipairs(chipButtons) do chip:Hide(); end
    for _, b in ipairs(actionButtons) do b:Hide(); end
    setBanner("");
    notice:SetText("");
    state.seated = true;
    frame:Show();
    module.open(f);
end

function handlers.O(f)
    if modules[f[2]] then return openModule(f); end
    state.game, state.min, state.max = f[2], tonumber(f[3]) or 0, tonumber(f[4]) or 0;
    state.busy = false;
    for game, p in pairs(panels) do if game == state.game then p:Show(); else p:Hide(); end end
    if state.game == "slots" then
        title:SetText("Slots");
        slotPays = split(f[6] or "", ",");
        for i, row in ipairs(payRows) do
            local symbol = #SYMBOLS + 1 - i;
            row.text:SetText(string.format("%s  x%s", SYMBOL_NAMES[symbol], slotPays[symbol] or "?"));
        end
        mixedText:SetText(string.format("Any 3 coins  x%s", f[7] or "?"));
        rtpText:SetText("Returns " .. percent(f[8]) .. " of stakes over time");
        rules:SetText("Three reels, one payline. Line up three of a kind on the gold line.");
    elseif state.game == "dice" then
        title:SetText("Goblin Dice");
        local returns, rtps = split(f[5] or "", ","), split(f[6] or "", ",");
        for i, b in ipairs(callButtons) do
            b.odds:SetText(string.format("pays %s  (%s)", multiple(returns[i]), percent(rtps[i])));
        end
        cup:Hide();
        diceSum:SetText("");
        rules:SetText("Pick your call, then roll two dice. Payouts include the stake.");
    else
        title:SetText("Blackjack");
        rules:SetText(string.format("Blackjack pays 3 to 2  -  Dealer %s soft 17  -  Double any two  -  Split once",
            f[5] == "1" and "hits" or "stands on"));
    end
    setBanner("");
    notice:SetText("");
    if state.bet == 0 then state.bet = math.min(math.max(state.min, 10 * GOLD), state.max); end
    state.seated = true;
    frame:Show();
    showMode(false);
    homeBetCircle();
    drawStack(betStack(), state.bet);
end

function handlers.T(f)
    if #f <= 2 then
        sweepCards();
        clearLabels();
        currentRound = nil;
        drawStack(stacks.bj[2], 0);
        showMode(false);
        homeBetCircle();
        drawStack(stacks.bj[1], state.bet);
        return;
    end
    setBanner("");
    local finished = renderTable(f);
    showMode(not finished, f[8]);
    if not finished then
        -- Hold the buttons until the cards have landed.
        for _, b in ipairs(actionButtons) do b:SetAlpha(0.6); end
        after(math.max(0, animEnd - clock), function() for _, b in ipairs(actionButtons) do b:SetAlpha(1); end end);
    end
end

function handlers.R(f)
    local stake, paid, mailed = tonumber(f[3]) or 0, tonumber(f[4]) or 0, f[5] == "1";
    state.busy = true;
    showMode(false);
    local hands = state.lastTable or {};
    local blackjack, push = false, false;
    for _, h in ipairs(hands) do
        blackjack = blackjack or h[5] == "J";
        push = push or h[5] == "U";
    end
    after(math.max(0, animEnd - clock) + 0.3, function()
        local pending = #hands;
        local function handDone()
            pending = pending - 1;
            if pending <= 0 then settleDone(); end
        end
        for i, h in ipairs(hands) do
            local handStake = tonumber(h[4]) or 0;
            payout(stacks.bj[i], handStake, math.floor(handStake * (RETURN_OF[h[5]] or 0)), 0.1 * (i - 1), handDone);
        end
        if pending == 0 then settleDone(); end
        local st = stacks.bj[1];
        announce(stake, paid, mailed, blackjack and "blackjack" or (push and "draw") or nil,
            blackjack and "confetti" or "coin", st.homeX, SEAT_Y + 40);
    end);
end

function handlers.SR(f)
    local result = { (tonumber(f[3]) or 0) + 1, (tonumber(f[4]) or 0) + 1, (tonumber(f[5]) or 0) + 1 };
    local stake, paid, mailed = tonumber(f[6]) or 0, tonumber(f[7]) or 0, f[8] == "1";
    state.busy = true;
    refreshBet();
    lightMode = "spin";
    local stopped = 0;
    for r = 1, 3 do
        spinReel(reels[r], 0.8 + 0.45 * (r - 1), result[r], function()
            stopped = stopped + 1;
            if stopped < 3 then return; end
            if paid > 0 then pulseWinners(); end
            lightMode = paid > stake and "win" or "idle";
            after(2.2, function() lightMode = "idle"; end);
            local jackpot = stake > 0 and paid / stake >= 100;
            announce(stake, paid, mailed, jackpot and "jackpot" or nil, jackpot and "fireworks" or "coin", -70, -200);
            if jackpot then PlaySound("LEVELUPSOUND"); end
            payout(stacks.slots[1], stake, paid, 0.2, settleDone);
        end);
    end
end

function handlers.DR(f)
    local result = { tonumber(f[3]) or 1, tonumber(f[4]) or 1 };
    local call = tonumber(f[5]) or 0;
    local stake, paid, mailed = tonumber(f[6]) or 0, tonumber(f[7]) or 0, f[8] == "1";
    state.busy = true;
    refreshBet();
    dieTex[1]:Hide();
    dieTex[2]:Hide();
    diceSum:SetText("");
    cup:SetAlpha(1);
    cup:Show();
    PlaySound("igMainMenuOptionCheckBoxOn");
    local t = 0;
    addTask(function(dt)
        t = t + dt;
        if t < 1.2 then
            cupFrame(math.floor(t * 20) % CUP_FRAMES);
            return false;
        end
        -- The cup lifts away and the dice drop onto the felt with a bounce.
        tween(0.2, 0, nil, function(p) cup:SetAlpha(1 - p); end, function() cup:Hide(); end);
        for i = 1, 2 do
            showDie(i, result[i]);
            tween(0.45, 0.05 * i, backOut, function(p)
                local size = lerp(150, 96, p);
                dieTex[i]:SetSize(size, size);
                dieTex[i]:SetAlpha(math.min(1, p * 2));
                dieTex[i]:ClearAllPoints();
                dieTex[i]:SetPoint("CENTER", dicePanel, "TOP", (i - 1.5) * lerp(40, 110, p), lerp(-110, -150, p));
            end);
        end
        after(0.5, function()
            diceSum:SetText(result[1] + result[2]);
            tween(0.3, 0, nil, function(p) diceSum:SetAlpha(p); end);
            local lucky = paid > stake and (call == 1 or call == 3);
            announce(stake, paid, mailed, lucky and "luckyroll" or nil, lucky and "confetti" or "coin", 0, -150);
            payout(stacks.dice[1], stake, paid, 0.3, settleDone);
        end);
        return true;
    end);
end

function handlers.M(f)
    state.max = tonumber(f[2]) or state.max;
    if state.bet > state.max then setBet(state.max); else refreshBet(); end
end

function handlers.E(f)
    state.busy = false;
    notice:SetText(ERRORS[f[2]] or ("Refused: " .. tostring(f[2])));
    if not state.playing then refreshBet(); end
end

function handlers.W() notice:SetText("Your blackjack hand is still waiting at the dealer."); end

function handlers.C() state.seated = false; frame:Hide(); end

local driver = CreateFrame("Frame");
driver:RegisterEvent("PLAYER_LOGIN");
driver:RegisterEvent("CHAT_MSG_ADDON");
driver:RegisterEvent("PLAYER_MONEY");
driver:SetScript("OnEvent", function(_, event, prefix, body)
    if event == "PLAYER_LOGIN" then
        send("HI");
    elseif event == "PLAYER_MONEY" then
        if frame:IsShown() and not state.playing then refreshBet(); end
    elseif prefix == PFX_IN then
        local f = split(body, ":");
        local handler = handlers[f[1]];
        if handler then handler(f); end
    end
end);

-- Closing mid-hand only hides the window: the dealer keeps the hand, and talking to them again reopens it.
frame:SetScript("OnHide", function()
    local module = modules[state.game];
    if module then
        if state.seated then
            state.seated = false;
            module.close();
        end
        return;
    end
    if state.seated and not state.playing then
        state.seated = false;
        send("L");
    end
end);

-- What the game modules build on.
EsteriaCasinoCore = {
    ES = ES, ART = ART, GOLD = GOLD, frame = frame, overlay = overlay, title = title, rules = rules, state = state,
    handlers = handlers, errors = ERRORS, modules = modules, notice = notice,
    panel = panel, send = send, split = split, money = money, lerp = lerp, toFrame = toFrame,
    addTask = addTask, after = after, tween = tween, easeOut = easeOut, easeInOut = easeInOut, backOut = backOut,
    playSheet = playSheet, celebrate = celebrate, setBanner = setBanner, atlasButton = atlasButton, enable = enable,
    cardCoords = cardCoords, chipCoords = chipCoords, breakdown = breakdown, flyChip = flyChip,
    clock = function() return clock; end,
};

-- Read-only view for /dump and the Lua test.
EsteriaCasinoDebug = { state = state, stacks = stacks, cards = cards, title = titleTex, reels = reels };
