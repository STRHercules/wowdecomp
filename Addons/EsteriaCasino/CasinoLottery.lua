-- Esteria Casino: the lottery (server: CasinoLottery.cpp). Pick four numbers from 1 to 20 (or a quick pick), buy a
-- ticket, and watch the draw: the server draws, pays three- and four-number matches from the pool, and rolls over.
local C = EsteriaCasinoCore;
local ES, ART = C.ES, C.ART;

local panel = C.panel("lottery", "lottery");
local BALL, PICK = 54, 4;

local cfg = { cost = 0, three = 0, max = 10, pick = 4, highest = 20 };
local data = { draw = 0, pool = 0, seconds = 0, count = 0, tickets = {}, pending = {}, picks = {}, last = nil,
               mine = {}, pendingMine = {}, shownDraw = nil };

local function ballCoords(n)
    local c, r = (n - 1) % 8, math.floor((n - 1) / 8);
    return c / 8, (c + 1) / 8, r / 4, (r + 1) / 4; -- balls.blp is 1024x512 of 128px cells
end

local function ballTexture(parent, size, layer)
    local tex = parent:CreateTexture(nil, layer or "ARTWORK");
    tex:SetTexture(ART .. "balls");
    tex:SetSize(size, size);
    return tex;
end

local function label(size, color, x, y)
    local fs = ES.Text(panel, size, color);
    fs:SetPoint("TOPLEFT", panel, "TOPLEFT", x, y);
    return fs;
end

-- The number grid --------------------------------------------------------------------------------------------------------
local balls = {};
local pickedText = label(13, ES.TEXT, 24, -300);

local function picked(n)
    for i, v in ipairs(data.picks) do if v == n then return i; end end
end

local function drawPicks()
    for n = 1, cfg.highest do
        local b = balls[n];
        if not b then break; end
        local on = picked(n) ~= nil;
        b.tex:SetAlpha(on and 1 or 0.55);
        b.tex:SetSize(on and BALL + 6 or BALL, on and BALL + 6 or BALL);
        if on then b.ring:Show(); else b.ring:Hide(); end
    end
    table.sort(data.picks);
    pickedText:SetText(#data.picks > 0 and ("Your numbers: " .. table.concat(data.picks, "  ")) or
        "Pick four numbers, or take a quick pick.");
end

for n = 1, 20 do
    local b = CreateFrame("Button", "EsteriaCasinoBall" .. n, panel);
    b:SetSize(BALL, BALL);
    b:SetPoint("CENTER", panel, "TOPLEFT", 52 + ((n - 1) % 5) * 62, -66 - math.floor((n - 1) / 5) * 60);
    b.ring = ES.Solid(b, "BACKGROUND", ES.GOLD, 0.45);
    b.ring:SetPoint("CENTER");
    b.ring:SetSize(BALL + 10, BALL + 10);
    b.ring:Hide();
    b.tex = ballTexture(b, BALL);
    b.tex:SetPoint("CENTER");
    b.tex:SetTexCoord(ballCoords(n));
    b:SetScript("OnClick", function()
        local at = picked(n);
        if at then
            table.remove(data.picks, at);
        elseif #data.picks < PICK then
            data.picks[#data.picks + 1] = n;
            PlaySound("igMainMenuOptionCheckBoxOn");
        end
        drawPicks();
    end);
    balls[n] = b;
end

local function blank(text, onClick, w)
    local b = C.atlasButton(nil, panel, "Blank", onClick, text);
    b:SetSize(w or 140, 44);
    return b;
end
local quick = blank("QUICK PICK", function() C.send("LB:Q"); end);
quick:SetPoint("TOPLEFT", panel, "TOPLEFT", 20, -326);
local clearPicks = blank("CLEAR", function() data.picks = {}; drawPicks(); end, 100);
clearPicks:SetPoint("LEFT", quick, "RIGHT", 8, 0);
local buy = blank("BUY TICKET", function()
    if #data.picks == PICK then C.send("LB:" .. table.concat(data.picks, ",")); end
end, 180);
buy:SetPoint("TOPLEFT", panel, "TOPLEFT", 20, -376);

-- Right side: the draw, your tickets, the last result --------------------------------------------------------------------
local RIGHT = 380;
local drawTitle = label(18, ES.GOLD, RIGHT, -40);
local poolText = label(22, ES.GOLD, RIGHT, -66);
local timerText = label(13, ES.TEXT, RIGHT, -96);
local prizeText = label(11, ES.MUTED, RIGHT, -116);
local ticketsTitle = label(13, ES.GOLD, RIGHT, -142);

local ticketRows = {};
for i = 1, 10 do
    local row = { balls = {} };
    for k = 1, PICK do
        row.balls[k] = ballTexture(panel, 20);
        row.balls[k]:SetPoint("TOPLEFT", panel, "TOPLEFT", RIGHT + ((i - 1) % 2) * 110 + (k - 1) * 22,
            -162 - math.floor((i - 1) / 2) * 24);
        row.balls[k]:Hide();
    end
    ticketRows[i] = row;
end

local lastTitle = label(13, ES.GOLD, RIGHT, -290);
local lastBalls = {};
for k = 1, PICK do
    lastBalls[k] = ballTexture(panel, 42, "OVERLAY");
    lastBalls[k]:SetPoint("TOPLEFT", panel, "TOPLEFT", RIGHT + (k - 1) * 48, -310);
    lastBalls[k]:Hide();
end
local lastText = label(11, ES.TEXT, RIGHT, -360);
lastText:SetWidth(380);
lastText:SetJustifyH("LEFT");
local mineText = label(11, ES.GOOD, RIGHT, -392);
mineText:SetWidth(380);
mineText:SetJustifyH("LEFT");

local function clockText(seconds)
    seconds = math.max(0, math.floor(seconds));
    if seconds >= 3600 then return string.format("%dh %02dm", math.floor(seconds / 3600), math.floor(seconds % 3600 / 60)); end
    return string.format("%d:%02d", math.floor(seconds / 60), seconds % 60);
end

C.addTask(function(dt)
    if panel:IsShown() and data.seconds > 0 then
        data.seconds = math.max(0, data.seconds - dt);
        timerText:SetText("Next draw in " .. clockText(data.seconds));
    end
    return false;
end);

local function drawTickets()
    ticketsTitle:SetText(string.format("Your tickets for draw #%d  (%d / %d)", data.draw, #data.tickets, cfg.max));
    for i, row in ipairs(ticketRows) do
        local numbers = data.tickets[i] and C.split(data.tickets[i], ",") or {};
        for k = 1, PICK do
            local n = tonumber(numbers[k]);
            if n then
                row.balls[k]:SetTexCoord(ballCoords(n));
                row.balls[k]:Show();
            else
                row.balls[k]:Hide();
            end
        end
    end
    C.enable(buy, #data.picks == PICK and #data.tickets < cfg.max);
    C.enable(quick, #data.tickets < cfg.max);
end

-- The last draw's balls drop in one at a time the first time this window sees that draw.
local function drawLast(animate)
    local last = data.last;
    if not last then
        lastTitle:SetText("No draw yet");
        lastText:SetText("");
        return;
    end
    lastTitle:SetText(string.format("Draw #%d", last.draw));
    local numbers = C.split(last.numbers, ",");
    for k = 1, PICK do
        local tex = lastBalls[k];
        tex:SetTexCoord(ballCoords(tonumber(numbers[k]) or 1));
        tex:Show();
        if animate then
            C.tween(0.45, 0.35 * (k - 1), C.backOut, function(p)
                tex:SetAlpha(math.min(1, p * 2));
                tex:SetSize(C.lerp(80, 42, p), C.lerp(80, 42, p));
            end, function() PlaySound("igMainMenuOptionCheckBoxOn"); end);
        else
            tex:SetAlpha(1);
            tex:SetSize(42, 42);
        end
    end
    local jackpot = last.jackpotWinners > 0
        and string.format("Jackpot: %d winner(s), %s each.", last.jackpotWinners, C.money(last.jackpotEach))
        or string.format("No jackpot winner: %s rolled over.", C.money(last.rollover));
    local three = last.threeWinners > 0
        and string.format("  Three numbers: %d winner(s), %s each.", last.threeWinners, C.money(last.threeEach)) or "";
    lastText:SetText(jackpot .. three);
end

local function drawMine()
    local lines = {};
    for _, t in ipairs(data.mine) do
        lines[#lines + 1] = string.format("%s: %d matched%s", t.numbers, t.matched,
            t.prize > 0 and ("  +" .. C.money(t.prize)) or "");
    end
    mineText:SetText(table.concat(lines, "\n"));
end

-- Once the last ball has dropped: a title for the best of your tickets in that draw.
local function celebrateMine()
    local best = 0;
    for _, t in ipairs(data.mine) do best = math.max(best, t.prize > 0 and t.matched or 0); end
    if best == 0 then return; end
    C.celebrate(best == PICK and "jackpot" or "youwin", best == PICK and "fireworks" or "coin", 210, -330);
    PlaySound(best == PICK and "LEVELUPSOUND" or "LOOTWINDOWCOINSOUND");
end

-- Messages -------------------------------------------------------------------------------------------------------------
local H = C.handlers;

C.modules.lottery = {
    open = function(f)
        cfg.cost, cfg.three, cfg.max = tonumber(f[3]) or 0, tonumber(f[4]) or 0, tonumber(f[5]) or 10;
        cfg.pick, cfg.highest = tonumber(f[6]) or 4, tonumber(f[7]) or 20;
        C.title:SetText("Esteria Lottery");
        C.rules:SetText(string.format("Ticket %s  -  match 3 wins %s  -  match all 4 shares the jackpot  -  "
            .. "odds 1 in 4,845", C.money(cfg.cost), C.money(cfg.three)));
        buy.label:SetText("BUY TICKET  " .. C.money(cfg.cost));
        prizeText:SetText("Prizes come from the pool. No jackpot winner? It rolls over.");
        data.picks = {};
        drawPicks();
    end,
    close = function() C.send("LQ"); end,
};

function H.LS(f)
    data.draw, data.pool, data.seconds = tonumber(f[2]) or 0, tonumber(f[3]) or 0, tonumber(f[4]) or 0;
    data.tickets = {};
    data.mine = {};
    drawTitle:SetText(string.format("Draw #%d", data.draw));
    poolText:SetText("Jackpot pool  " .. C.money(data.pool));
    timerText:SetText("Next draw in " .. clockText(data.seconds));
    drawTickets();
    -- LT, LR and LP follow; they are drawn as they arrive.
end

function H.LT(f)
    data.tickets[#data.tickets + 1] = f[2];
    drawTickets();
end

function H.LR(f)
    data.last = { draw = tonumber(f[2]) or 0, numbers = f[3] or "", jackpotWinners = tonumber(f[4]) or 0,
                  jackpotEach = tonumber(f[5]) or 0, threeWinners = tonumber(f[6]) or 0,
                  threeEach = tonumber(f[7]) or 0, rollover = tonumber(f[8]) or 0 };
    local fresh = data.shownDraw ~= nil and data.shownDraw ~= data.last.draw;
    data.shownDraw = data.last.draw;
    drawLast(fresh);
    if fresh then C.after(1.6, celebrateMine); end -- the LP lines arrive right after this one
end

function H.LP(f)
    data.mine[#data.mine + 1] = { numbers = f[2], matched = tonumber(f[3]) or 0, prize = tonumber(f[4]) or 0 };
    drawMine();
end

function H.LK(f)
    data.picks = {};
    drawPicks();
    C.setBanner("Ticket bought: " .. (f[2] or ""));
    PlaySound("LOOTWINDOWCOINSOUND");
end

-- Read-only view for /dump and the Lua test.
EsteriaCasinoLotteryDebug = { data = data, buy = buy, quick = quick, tickets = ticketRows, last = lastBalls };
