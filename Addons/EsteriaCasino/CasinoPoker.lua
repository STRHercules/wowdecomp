-- Esteria Casino: fixed-limit Texas Hold'em (server: CasinoPoker.cpp). Six seats around the table; the server deals,
-- runs the betting and the clock, and sends each player only their own hole cards until the showdown.
local C = EsteriaCasinoCore;
local ES, ART, GOLD = C.ES, C.ART, C.GOLD;

local panel = C.panel("poker", "poker");
local CARD_W, CARD_H, SMALL_W, SMALL_H = 44, 62, 26, 36;
-- Seat anchors on the oval (from the top centre of the felt): bottom, bottom left, top left, top, top right,
-- bottom right.
local SEAT_POS = { [0] = { 0, -392 }, { -296, -326 }, { -296, -112 }, { 0, -50 }, { 296, -112 }, { 296, -326 } };
local BOARD_Y, POT_Y = -214, -166;

local cfg = { small = 0, big = 0, minBuy = 0, maxBuy = 0, rakeBp = 0, rakeCap = 0, seconds = 30 };
local table_ = { hand = nil, street = 0, toAct = -1, legal = "", toCall = 0, betSize = 0, seconds = 0, you = nil,
                 board = {}, seats = {} };

local function cardTex(parent, w, h, layer)
    local tex = parent:CreateTexture(nil, layer or "ARTWORK");
    tex:SetTexture(ART .. "cards");
    tex:SetSize(w, h);
    tex:SetTexCoord(C.cardCoords("??"));
    return tex;
end

-- Flips a card face up: it narrows to an edge, changes face and widens again.
local function flipTo(tex, code, w, delay)
    C.tween(0.1, delay or 0, nil, function(p) tex:SetWidth(math.max(1, w * (1 - p))); end, function()
        tex:SetTexCoord(C.cardCoords(code));
        C.tween(0.1, 0, nil, function(p) tex:SetWidth(math.max(1, w * p)); end);
    end);
end

-- Seats ---------------------------------------------------------------------------------------------------------------
local seats = {};
for i = 0, 5 do
    local pos = SEAT_POS[i];
    local s = CreateFrame("Frame", nil, panel);
    s:SetSize(150, 46);
    s:SetPoint("CENTER", panel, "TOP", pos[1], pos[2]);
    s.back = ES.Solid(s, "BACKGROUND", ES.INK, 0.78);
    s.back:SetAllPoints();
    s.border = ES.Border(s, ES.GOLD_DIM, 0.8);
    s.name = ES.Text(s, 13, ES.TEXT);
    s.name:SetPoint("TOPLEFT", 8, -6);
    s.stack = ES.Text(s, 12, ES.GOLD);
    s.stack:SetPoint("BOTTOMLEFT", 8, 6);
    s.timer = ES.Text(s, 12, ES.BAD);
    s.timer:SetPoint("BOTTOMRIGHT", -8, 6);
    s.result = ES.Text(panel, 12, ES.GOOD);
    s.result:SetPoint("BOTTOM", s, "TOP", 0, 40);
    s.dealer = panel:CreateTexture(nil, "OVERLAY");
    s.dealer:SetTexture(ART .. "chips");
    s.dealer:SetTexCoord(C.chipCoords(1));
    s.dealer:SetSize(22, 22);
    s.dealer:SetPoint("LEFT", s, "RIGHT", 4, 0);
    s.dealerText = ES.Text(panel, 10, { 0.1, 0.1, 0.1 }, "OVERLAY");
    s.dealerText:SetPoint("CENTER", s.dealer, "CENTER", 0, 0);
    s.dealerText:SetText("D");
    -- Bets sit between the seat and the middle of the table.
    local bx, by = pos[1] * 0.62, pos[2] + (-214 - pos[2]) * 0.38;
    s.betChip = panel:CreateTexture(nil, "ARTWORK");
    s.betChip:SetTexture(ART .. "chips");
    s.betChip:SetSize(24, 24);
    s.betChip:SetPoint("CENTER", panel, "TOP", bx - 18, by);
    s.betText = ES.Text(panel, 12, ES.TEXT);
    s.betText:SetPoint("LEFT", s.betChip, "RIGHT", 4, 0);
    s.cards = { cardTex(panel, SMALL_W, SMALL_H, "OVERLAY"), cardTex(panel, SMALL_W, SMALL_H, "OVERLAY") };
    s.cards[1]:SetPoint("BOTTOM", s, "TOP", -14, 2);
    s.cards[2]:SetPoint("BOTTOM", s, "TOP", 14, 2);
    s.sit = C.atlasButton(nil, s, "Blank", nil, "SIT");
    s.sit:SetSize(80, 34);
    s.sit:SetPoint("CENTER");
    s.index = i;
    seats[i] = s;
end

-- Board and pot --------------------------------------------------------------------------------------------------------
local boardCards = {};
for k = 1, 5 do
    boardCards[k] = cardTex(panel, CARD_W, CARD_H);
    boardCards[k]:SetPoint("CENTER", panel, "TOP", (k - 3) * 52, BOARD_Y);
    boardCards[k]:Hide();
end
local potText = ES.Text(panel, 15, ES.GOLD);
potText:SetPoint("CENTER", panel, "TOP", 0, POT_Y);

-- Bottom bar: your hand, actions, buy-in ---------------------------------------------------------------------------------
local hand = { cardTex(panel, CARD_W, CARD_H, "OVERLAY"), cardTex(panel, CARD_W, CARD_H, "OVERLAY") };
hand[1]:SetPoint("BOTTOMLEFT", C.frame, "BOTTOMLEFT", 16, 12);
hand[2]:SetPoint("LEFT", hand[1], "RIGHT", 6, 0);
local yourHand = ES.Text(panel, 11, ES.MUTED);
yourHand:SetPoint("BOTTOMLEFT", hand[1], "TOPLEFT", 0, 2);

local function blank(text, onClick, w)
    local b = C.atlasButton(nil, panel, "Blank", onClick, text);
    b:SetSize(w or 116, 46);
    return b;
end
local fold = blank("FOLD", function() C.send("PA:f"); end);
local call = blank("CHECK", function() C.send(string.find(table_.legal, "k", 1, true) and "PA:k" or "PA:c"); end, 140);
local raise = blank("BET", function() C.send(string.find(table_.legal, "r", 1, true) and "PA:r" or "PA:b"); end, 140);
local leave = blank("LEAVE", function() C.send("PL"); end, 96);
fold:SetPoint("BOTTOMLEFT", C.frame, "BOTTOMLEFT", 130, 14);
call:SetPoint("LEFT", fold, "RIGHT", 8, 0);
raise:SetPoint("LEFT", call, "RIGHT", 8, 0);
leave:SetPoint("BOTTOMRIGHT", C.frame, "BOTTOMRIGHT", -14, 14);

local buyRow = CreateFrame("Frame", nil, panel);
buyRow:SetSize(420, 46);
buyRow:SetPoint("BOTTOMLEFT", C.frame, "BOTTOMLEFT", 130, 14);
local buyLabel = ES.Text(buyRow, 12, ES.TEXT);
buyLabel:SetPoint("LEFT", 0, 0);
local buyBox = CreateFrame("EditBox", "EsteriaCasinoPokerBuyIn", buyRow, "InputBoxTemplate");
buyBox:SetSize(70, 22);
buyBox:SetPoint("LEFT", buyLabel, "RIGHT", 10, 0);
buyBox:SetAutoFocus(false);
buyBox:SetNumeric(true);
local buy = C.atlasButton(nil, buyRow, "Blank", function()
    local gold = tonumber(buyBox:GetText()) or 0;
    C.send(string.format("PJ:%d:%d", table_.pendingSeat or 0, gold * GOLD));
    buyRow:Hide();
end, "BUY IN");
buy:SetSize(100, 40);
buy:SetPoint("LEFT", buyBox, "RIGHT", 10, 0);
local cancel = C.atlasButton(nil, buyRow, "Blank", function() buyRow:Hide(); end, "CANCEL");
cancel:SetSize(90, 40);
cancel:SetPoint("LEFT", buy, "RIGHT", 6, 0);
buyRow:Hide();

for i = 0, 5 do
    seats[i].sit:SetScript("OnClick", function()
        table_.pendingSeat = i;
        buyLabel:SetText(string.format("Buy in at seat %d (%s - %s):", i + 1, C.money(cfg.minBuy), C.money(cfg.maxBuy)));
        buyBox:SetText(tostring(math.floor(math.min(cfg.maxBuy, cfg.big * 50) / GOLD)));
        buyRow:Show();
    end);
end

-- Drawing --------------------------------------------------------------------------------------------------------------
local function refreshActions()
    local seated = table_.you ~= nil;
    local legal = table_.legal;
    for _, b in ipairs({ fold, call, raise }) do if seated then b:Show(); else b:Hide(); end end
    if seated then leave:Show(); else leave:Hide(); end
    C.enable(fold, string.find(legal, "f", 1, true) ~= nil);
    local canCheck = string.find(legal, "k", 1, true) ~= nil;
    call.label:SetText(canCheck and "CHECK" or ("CALL " .. C.money(table_.toCall)));
    C.enable(call, canCheck or string.find(legal, "c", 1, true) ~= nil);
    local canRaise = string.find(legal, "r", 1, true) ~= nil;
    raise.label:SetText((canRaise and "RAISE " or "BET ") .. C.money(table_.betSize));
    C.enable(raise, canRaise or string.find(legal, "b", 1, true) ~= nil);
end

local function drawSeat(i)
    local s, data = seats[i], table_.seats[i];
    local empty = not data or data.name == "";
    if empty then
        s.name:SetText("");
        s.stack:SetText("");
        s.timer:SetText("");
        s.sit:Show();
        if table_.you then s.sit:Hide(); end
        s.back:SetAlpha(0.45);
        s.dealer:Hide(); s.dealerText:Hide(); s.betChip:Hide(); s.betText:SetText("");
        s.cards[1]:Hide(); s.cards[2]:Hide();
        return;
    end
    s.sit:Hide();
    s.back:SetAlpha(data.flags.f and 0.4 or 0.82);
    s.name:SetText(data.name .. (data.flags.s and "  (you)" or "") .. (data.flags.w and "  - leaving" or ""));
    local nameColor = data.flags.h and ES.GOLD_DIM or ES.TEXT; -- house players read a shade apart
    s.name:SetTextColor(nameColor[1], nameColor[2], nameColor[3]);
    s.stack:SetText(C.money(data.stack) .. (data.flags.a and "  ALL IN" or ""));
    ES.Tint(s.border, data.flags.t and ES.GOLD or ES.GOLD_DIM, data.flags.t and 1 or 0.8);
    if data.flags.d then s.dealer:Show(); s.dealerText:Show(); else s.dealer:Hide(); s.dealerText:Hide(); end
    if data.bet > 0 then
        s.betChip:SetTexCoord(C.chipCoords(C.breakdown(data.bet)[1] or 1));
        s.betChip:Show();
        s.betText:SetText(C.money(data.bet));
    else
        s.betChip:Hide();
        s.betText:SetText("");
    end
    local showBacks = data.flags.i and not data.flags.f and not data.flags.s and not data.shown;
    for k = 1, 2 do
        if showBacks or data.shown then s.cards[k]:Show(); else s.cards[k]:Hide(); end
    end
end

-- New board cards turn over one after another.
local function drawBoard(codes)
    for k = 1, 5 do
        local code = codes[k];
        if code then
            if table_.board[k] ~= code then
                boardCards[k]:SetTexCoord(C.cardCoords("??"));
                boardCards[k]:Show();
                flipTo(boardCards[k], code, CARD_W, 0.15 * (k - #table_.board - 1));
                PlaySound("igMainMenuOptionCheckBoxOn");
            end
        else
            boardCards[k]:Hide();
        end
    end
    table_.board = codes;
end

local function newHand(number)
    table_.hand = number;
    table_.board = {};
    for k = 1, 5 do boardCards[k]:Hide(); end
    for i = 0, 5 do
        seats[i].result:SetText("");
        if table_.seats[i] then table_.seats[i].shown = false; end
        for k = 1, 2 do
            seats[i].cards[k]:SetTexCoord(C.cardCoords("??"));
            seats[i].cards[k]:SetWidth(SMALL_W);
        end
    end
    hand[1]:Hide(); hand[2]:Hide();
    yourHand:SetText("");
    C.setBanner("");
end

-- The seat on the clock counts down locally between updates.
C.addTask(function(dt)
    if panel:IsShown() and table_.street > 0 and table_.toAct >= 0 then
        table_.seconds = math.max(0, table_.seconds - dt);
        for i = 0, 5 do seats[i].timer:SetText(i == table_.toAct and string.format("%ds", math.ceil(table_.seconds)) or ""); end
    end
    return false;
end);

-- Messages -------------------------------------------------------------------------------------------------------------
local H = C.handlers;

C.modules.poker = {
    open = function(f)
        cfg.small, cfg.big = tonumber(f[3]) or 0, tonumber(f[4]) or 0;
        cfg.minBuy, cfg.maxBuy = tonumber(f[5]) or 0, tonumber(f[6]) or 0;
        cfg.rakeBp, cfg.rakeCap, cfg.seconds = tonumber(f[7]) or 0, tonumber(f[8]) or 0, tonumber(f[9]) or 30;
        C.title:SetText("Texas Hold'em");
        C.rules:SetText(string.format("Fixed limit %s/%s  -  blinds %s/%s  -  rake %s%% up to %s, none without a flop",
            C.money(cfg.big), C.money(cfg.big * 2), C.money(cfg.small), C.money(cfg.big), cfg.rakeBp / 100,
            C.money(cfg.rakeCap)));
        buyRow:Hide();
    end,
    close = function() C.send("PQ"); end,
};

function H.PS(f)
    local i = tonumber(f[2]);
    if not i then return; end
    local flags = {};
    for letter in string.gmatch(f[6] or "", ".") do flags[letter] = true; end
    local old = table_.seats[i];
    table_.seats[i] = { name = f[3] or "", stack = tonumber(f[4]) or 0, bet = tonumber(f[5]) or 0, flags = flags,
                        shown = old and old.shown and flags.i };
    if flags.s then table_.you = i; elseif table_.you == i then table_.you = nil; end
    if f[3] == "" and table_.you == i then table_.you = nil; end
    drawSeat(i);
    refreshActions();
end

function H.PT(f)
    local number = f[2];
    if number ~= table_.hand then newHand(number); end
    table_.street = tonumber(f[3]) or 0;
    local pot = tonumber(f[4]) or 0;
    potText:SetText(pot > 0 and ("Pot  " .. C.money(pot)) or "");
    drawBoard(f[5] ~= "" and C.split(f[5], ",") or {});
    table_.toAct = tonumber(f[6]) or -1;
    table_.legal = f[8] or "";
    table_.seconds = tonumber(f[9]) or 0;
    table_.toCall = tonumber(f[10]) or 0;
    table_.betSize = tonumber(f[11]) or 0;
    local stillYou = false;
    for i = 0, 5 do
        local data = table_.seats[i];
        if data and data.flags.s and data.name ~= "" then stillYou = true; end
    end
    if not stillYou then table_.you = nil; end
    refreshActions();
    for i = 0, 5 do drawSeat(i); end
end

function H.PH(f)
    local codes = C.split(f[2] or "", ",");
    for k = 1, 2 do
        if hand[k].code ~= codes[k] or not hand[k]:IsShown() then
            hand[k].code = codes[k];
            hand[k]:SetTexCoord(C.cardCoords("??"));
            hand[k]:Show();
            flipTo(hand[k], codes[k], CARD_W, 0.15 * (k - 1));
        end
    end
    yourHand:SetText("Your hand");
end

function H.PW(f)
    local i, won = tonumber(f[3]), tonumber(f[4]) or 0;
    if not i then return; end
    local s = seats[i];
    local codes = f[5] ~= "" and C.split(f[5], ",") or nil;
    if codes then
        if table_.seats[i] then table_.seats[i].shown = true; end
        for k = 1, 2 do
            s.cards[k]:Show();
            flipTo(s.cards[k], codes[k], SMALL_W, 0.1 * k);
        end
    end
    local text = won > 0 and ("+" .. C.money(won)) or "";
    if f[6] and f[6] ~= "" then text = text .. (text ~= "" and "  " or "") .. f[6]; end
    s.result:SetText(text);
    if won > 0 and i == table_.you then
        C.celebrate("youwin", "coin", SEAT_POS[i][1], SEAT_POS[i][2] + 40);
        PlaySound("LOOTWINDOWCOINSOUND");
    end
end

function H.PR(f)
    local rake = tonumber(f[4]) or 0;
    if rake > 0 then potText:SetText("Rake " .. C.money(rake)); end
end

-- Read-only view for /dump and the Lua test.
EsteriaCasinoPokerDebug = { table = table_, seats = seats, board = boardCards, hand = hand, fold = fold, call = call,
                            raise = raise, leave = leave, buyRow = buyRow, buyBox = buyBox, buy = buy };
