-- Esteria Casino: chess and checkers (server: CasinoBoardGames.cpp). The lobby lists challenges and matches; a match
-- shows the board, both clocks and the moves the server says are legal. The server checks every move again.
local C = EsteriaCasinoCore;
local ES, ART, GOLD = C.ES, C.ART, C.GOLD;

local panel = C.panel("board", "chessbg");
-- Board grids measured on the Parlour art (1920x1080 scaled by 784/1920): left, top, square size.
local GRID = { c = { 219.7, 48.2, 43.07, "chessbg" }, k = { 221.3, 49.8, 42.67, "checkersbg" } };
-- Cells in pieces.blp.
local CHESS_PIECE = { P = 0, N = 1, B = 2, R = 3, Q = 4, K = 5, p = 6, n = 7, b = 8, r = 9, q = 10, k = 11 };
local CHECKERS_PIECE = { w = 12, W = 13, b = 14, B = 15 };

local view = { kind = "c", match = nil, side = -1, names = {}, stake = 0, board = nil, toMove = 0, clocks = { 0, 0 },
               legal = {}, pendingLegal = {}, selected = nil, maxStake = 0, cutBp = 0, minutes = 10 };
local lobby = { challenges = {}, matches = {}, pendingC = {}, pendingM = {} };

-- Left column: players, clocks, stake, status ------------------------------------------------------------------------
local function label(size, color, x, y, anchor)
    local fs = ES.Text(panel, size, color);
    fs:SetPoint(anchor or "TOPLEFT", panel, "TOPLEFT", x, y);
    return fs;
end
local topName, topClock = label(15, ES.TEXT, 16, -60), label(22, ES.GOLD, 16, -82);
local bottomName, bottomClock = label(15, ES.TEXT, 16, -330), label(22, ES.GOLD, 16, -352);
local stakeText = label(13, ES.MUTED, 16, -200);
local statusText = label(13, ES.GOOD, 16, -224);
local lastText = label(12, ES.MUTED, 16, -248);

-- Board: squares to click, highlights, pieces --------------------------------------------------------------------------
local squares, highlights, pieces = {}, {}, {};
local function flipped()
    return (view.kind == "c" and view.side == 1) or (view.kind == "k" and view.side == 0);
end

local function squareCenter(sq)
    local g = GRID[view.kind];
    local file, rank = sq % 8, math.floor(sq / 8);
    if flipped() then file, rank = 7 - file, 7 - rank; end
    return g[1] + (file + 0.5) * g[3], -(g[2] + (7 - rank + 0.5) * g[3]);
end

local function placeAt(region, sq)
    local x, y = squareCenter(sq);
    region:ClearAllPoints();
    region:SetPoint("CENTER", panel, "TOPLEFT", x, y);
end

local onSquare; -- set below
for sq = 0, 63 do
    local hl = ES.Solid(panel, "ARTWORK", ES.GOLD, 0.35);
    hl:Hide();
    highlights[sq] = hl;
    local button = CreateFrame("Button", nil, panel);
    button.square = sq;
    button:SetScript("OnClick", function(self) onSquare(self.square); end);
    squares[sq] = button;
end

local function layoutSquares()
    local size = GRID[view.kind][3];
    for sq = 0, 63 do
        squares[sq]:SetSize(size, size);
        placeAt(squares[sq], sq);
        highlights[sq]:SetSize(size, size);
        placeAt(highlights[sq], sq);
    end
end

local function pieceCoords(letter)
    local cell = (view.kind == "c" and CHESS_PIECE or CHECKERS_PIECE)[letter] or 0;
    local c, r = cell % 8, math.floor(cell / 8);
    return c / 8, (c + 1) / 8, r / 2, (r + 1) / 2;
end

local function pieceTexture(i)
    if not pieces[i] then
        pieces[i] = panel:CreateTexture(nil, "OVERLAY");
        pieces[i]:SetTexture(ART .. "pieces");
    end
    return pieces[i];
end

-- The square a move ends on, and the square it starts from: "e2e4" / "e7e8q" or "40-31-22".
local function sqOf(name) return (string.byte(name, 2) - 49) * 8 + (string.byte(name, 1) - 97); end
local function moveEnds(move)
    if view.kind == "c" then return sqOf(string.sub(move, 1, 2)), sqOf(string.sub(move, 3, 4)); end
    local parts = C.split(move, "-");
    return tonumber(parts[1]), tonumber(parts[#parts]);
end

local function clearHighlights()
    for sq = 0, 63 do highlights[sq]:Hide(); end
end

local function drawBoard(animateLast)
    clearHighlights();
    local board = view.board;
    local used = 0;
    if board then
        local size = GRID[view.kind][3];
        local fromSq, toSq;
        if animateLast and view.last and view.last ~= "" then fromSq, toSq = moveEnds(view.last); end
        for sq = 0, 63 do
            local letter = string.sub(board, sq + 1, sq + 1);
            if letter ~= "." then
                used = used + 1;
                local tex = pieceTexture(used);
                tex:SetSize(size * 0.92, size * 0.92);
                tex:SetTexCoord(pieceCoords(letter));
                tex:Show();
                if sq == toSq and fromSq then
                    local x0, y0 = squareCenter(fromSq);
                    local x1, y1 = squareCenter(toSq);
                    C.tween(0.25, 0, C.easeInOut, function(p)
                        tex:ClearAllPoints();
                        tex:SetPoint("CENTER", panel, "TOPLEFT", C.lerp(x0, x1, p), C.lerp(y0, y1, p));
                    end);
                else
                    placeAt(tex, sq);
                end
            end
        end
        if fromSq then highlights[fromSq]:SetAlpha(0.2); highlights[fromSq]:Show(); highlights[toSq]:Show(); end
    end
    for i = used + 1, #pieces do pieces[i]:Hide(); end
end

local function showTargets(from)
    clearHighlights();
    highlights[from]:SetAlpha(0.5);
    highlights[from]:Show();
    for _, move in ipairs(view.legal) do
        local a, b = moveEnds(move);
        if a == from then
            highlights[b]:SetAlpha(0.3);
            highlights[b]:Show();
        end
    end
end

onSquare = function(sq)
    if not view.match or view.side ~= view.toMove or #view.legal == 0 then return; end
    if view.selected then
        local chosen;
        for _, move in ipairs(view.legal) do
            local a, b = moveEnds(move);
            if a == view.selected and b == sq then
                -- Promotions list four moves to the same square: take the queen.
                if not chosen or string.sub(move, 5, 5) == "q" then chosen = move; end
            end
        end
        if chosen then
            view.selected = nil;
            clearHighlights();
            C.send("GM:" .. chosen);
            return;
        end
    end
    for _, move in ipairs(view.legal) do
        if moveEnds(move) == sq then
            view.selected = sq;
            showTargets(sq);
            return;
        end
    end
    view.selected = nil;
    drawBoard(false);
end

-- Right column: lobby, or the match buttons -----------------------------------------------------------------------------
local RIGHT = 580;
local function blank(text, onClick, w, h)
    local b = C.atlasButton(nil, panel, "Blank", onClick, text);
    b:SetSize(w or 92, h or 40);
    return b;
end

local lobbyWidgets = {};
local matchWidgets = {};

local gameButtons = {};
for i, spec in ipairs({ { "c", "CHESS" }, { "k", "CHECKERS" } }) do
    local b = blank(spec[2], function() view.newKind = spec[1]; end, 96, 38);
    b:SetPoint("TOPLEFT", panel, "TOPLEFT", RIGHT + (i - 1) * 100, -52);
    gameButtons[i] = b;
    lobbyWidgets[#lobbyWidgets + 1] = b;
end
view.newKind = "c";

local stakeLabel = label(12, ES.TEXT, RIGHT, -100);
stakeLabel:SetText("Stake (gold, 0 = friendly)");
lobbyWidgets[#lobbyWidgets + 1] = stakeLabel;
local stakeBox = CreateFrame("EditBox", "EsteriaCasinoBoardStake", panel, "InputBoxTemplate");
stakeBox:SetSize(70, 22);
stakeBox:SetPoint("TOPLEFT", panel, "TOPLEFT", RIGHT + 6, -116);
stakeBox:SetAutoFocus(false);
stakeBox:SetNumeric(true);
stakeBox:SetText("0");
lobbyWidgets[#lobbyWidgets + 1] = stakeBox;

local MINUTES = { 3, 5, 10, 15, 30 };
local minutesButton;
minutesButton = blank("10 MIN", function()
    local next = 1;
    for i, m in ipairs(MINUTES) do if m == view.minutes then next = i % #MINUTES + 1; end end
    view.minutes = MINUTES[next];
    minutesButton.label:SetText(view.minutes .. " MIN");
end, 96, 34);
minutesButton:SetPoint("TOPLEFT", panel, "TOPLEFT", RIGHT + 90, -110);
lobbyWidgets[#lobbyWidgets + 1] = minutesButton;

local postButton = blank("POST CHALLENGE", function()
    local gold = tonumber(stakeBox:GetText()) or 0;
    C.send(string.format("GC:%s:%d:%d", view.newKind, gold * GOLD, view.minutes));
end, 190, 40);
postButton:SetPoint("TOPLEFT", panel, "TOPLEFT", RIGHT, -150);
lobbyWidgets[#lobbyWidgets + 1] = postButton;

-- Free practice against the house, at three levels.
local LEVELS = { "EASY", "NORMAL", "HARD" };
view.level = 2;
local levelButton;
levelButton = blank("NORMAL", function()
    view.level = view.level % #LEVELS + 1;
    levelButton.label:SetText(LEVELS[view.level]);
end, 84, 36);
levelButton:SetPoint("TOPLEFT", panel, "TOPLEFT", RIGHT + 112, -198);
lobbyWidgets[#lobbyWidgets + 1] = levelButton;
local houseButton = blank("PLAY THE HOUSE", function()
    C.send(string.format("GH:%s:%d:%d", view.newKind, view.level, view.minutes));
end, 108, 40);
houseButton:SetPoint("TOPLEFT", panel, "TOPLEFT", RIGHT, -196);
lobbyWidgets[#lobbyWidgets + 1] = houseButton;
local houseNote = label(10, ES.MUTED, RIGHT, -238);
houseNote:SetText("Practice games against the house are free.");
lobbyWidgets[#lobbyWidgets + 1] = houseNote;

local listTitle = label(13, ES.GOLD, RIGHT, -256);
listTitle:SetText("Open challenges");
lobbyWidgets[#lobbyWidgets + 1] = listTitle;

local rows = {};
for i = 1, 5 do
    local text = label(11, ES.TEXT, RIGHT, -276 - (i - 1) * 32);
    text:SetWidth(120);
    text:SetJustifyH("LEFT");
    local b = blank("ACCEPT", nil, 70, 28);
    b:SetPoint("TOPLEFT", panel, "TOPLEFT", RIGHT + 124, -270 - (i - 1) * 32);
    rows[i] = { text = text, button = b };
    lobbyWidgets[#lobbyWidgets + 1] = text;
    lobbyWidgets[#lobbyWidgets + 1] = b;
end

local function stakeName(copper) return copper > 0 and C.money(copper) or "friendly"; end
local function gameName(kind) return kind == "c" and "Chess" or "Checkers"; end

local function drawLobbyRows()
    local entries = {};
    for _, ch in ipairs(lobby.challenges) do entries[#entries + 1] = { kind = "challenge", data = ch }; end
    for _, m in ipairs(lobby.matches) do entries[#entries + 1] = { kind = "match", data = m }; end
    for i, row in ipairs(rows) do
        local e = entries[i];
        if not e then
            row.text:SetText("");
            row.button:Hide();
        elseif e.kind == "challenge" then
            local ch = e.data;
            row.text:SetText(string.format("%s  %s  %dm\n%s", gameName(ch.kind), stakeName(ch.stake), ch.minutes,
                ch.host));
            row.button.label:SetText(ch.mine and "CANCEL" or "ACCEPT");
            row.button:SetScript("OnClick", function() C.send(ch.mine and "GX" or ("GA:" .. ch.id)); end);
            row.button:Show();
        else
            local m = e.data;
            row.text:SetText(string.format("%s  %s\n%s vs %s", gameName(m.kind), stakeName(m.stake), m.a, m.b));
            row.button.label:SetText("WATCH");
            row.button:SetScript("OnClick", function() C.send("GW:" .. m.id); end);
            row.button:Show();
        end
    end
end

local resign = blank("RESIGN", function() C.send("GR"); end, 120, 38);
resign:SetPoint("TOPLEFT", panel, "TOPLEFT", RIGHT + 20, -120);
local drawButton = blank("OFFER DRAW", function() C.send("GD"); end, 120, 38);
drawButton:SetPoint("TOPLEFT", panel, "TOPLEFT", RIGHT + 20, -164);
local decline = blank("DECLINE", function() C.send("GN"); end, 120, 38);
decline:SetPoint("TOPLEFT", panel, "TOPLEFT", RIGHT + 20, -208);
local leaveWatch = blank("BACK TO LOBBY", function() C.send("GQ"); C.send("GL"); end, 150, 38);
leaveWatch:SetPoint("TOPLEFT", panel, "TOPLEFT", RIGHT + 6, -120);
for _, w in ipairs({ resign, drawButton, decline, leaveWatch }) do matchWidgets[#matchWidgets + 1] = w; end

local function showLobby(on)
    for _, w in ipairs(lobbyWidgets) do if on then w:Show(); else w:Hide(); end end
    for _, w in ipairs(matchWidgets) do w:Hide(); end
    if on then drawLobbyRows(); end
end

local function clockText(seconds)
    seconds = math.max(0, math.floor(seconds));
    return string.format("%d:%02d", math.floor(seconds / 60), seconds % 60);
end

local function sideNames()
    -- Your side at the bottom; spectators see side 0 at the bottom.
    local bottom = view.side >= 0 and view.side or 0;
    return 1 - bottom, bottom;
end

local function drawPlayers()
    local top, bottom = sideNames();
    local colour = function(side)
        if view.kind == "c" then return side == 0 and "White" or "Black"; end
        return side == 0 and "Black" or "White";
    end
    topName:SetText(string.format("%s  (%s)", view.names[top] or "", colour(top)));
    bottomName:SetText(string.format("%s  (%s)%s", view.names[bottom] or "", colour(bottom),
        view.side == bottom and "  - you" or ""));
    topClock:SetText(clockText(view.clocks[top + 1]));
    bottomClock:SetText(clockText(view.clocks[bottom + 1]));
    local turn = view.toMove;
    topClock:SetTextColor(unpack(turn == top and ES.GOLD or ES.MUTED));
    bottomClock:SetTextColor(unpack(turn == bottom and ES.GOLD or ES.MUTED));
end

-- Clocks tick down locally between server updates.
C.addTask(function(dt)
    if view.match and not view.over and panel:IsShown() then
        local i = view.toMove + 1;
        view.clocks[i] = math.max(0, view.clocks[i] - dt);
        drawPlayers();
    end
    return false;
end);

local function setKind(kind)
    view.kind = kind;
    panel.back:SetTexture(ART .. GRID[kind][4]);
    layoutSquares();
end

-- Messages -------------------------------------------------------------------------------------------------------------
local H = C.handlers;

local function enterLobby()
    view.match, view.side, view.board, view.legal, view.over = nil, -1, nil, {}, false;
    C.title:SetText("Chess & Checkers");
    C.rules:SetText(string.format("Winner takes both stakes minus %s%% of the winnings. Draws return both stakes.",
        view.cutBp / 100));
    for _, fs in ipairs({ topName, topClock, bottomName, bottomClock, stakeText, statusText, lastText }) do
        fs:SetText("");
    end
    setKind("c");
    drawBoard(false);
    showLobby(true);
end

C.modules.board = {
    open = function(f)
        view.maxStake, view.cutBp, view.minutes = tonumber(f[3]) or 0, tonumber(f[4]) or 0, tonumber(f[5]) or 10;
        minutesButton.label:SetText(view.minutes .. " MIN");
        enterLobby();
    end,
    close = function() C.send("GQ"); end,
};

function H.GL(f)
    lobby.pendingC[#lobby.pendingC + 1] = { id = f[2], kind = f[3], host = f[4], stake = tonumber(f[5]) or 0,
                                            minutes = tonumber(f[6]) or 0, mine = f[7] == "1" };
end
function H.GLE()
    lobby.challenges, lobby.pendingC = lobby.pendingC, {};
    if not view.match then drawLobbyRows(); end
end
function H.GG(f)
    lobby.pendingM[#lobby.pendingM + 1] = { id = f[2], kind = f[3], a = f[4], b = f[5], stake = tonumber(f[6]) or 0 };
end
function H.GGE()
    lobby.matches, lobby.pendingM = lobby.pendingM, {};
    if not view.match then drawLobbyRows(); end
end

function H.GS(f)
    view.match, view.side, view.over = f[2], tonumber(f[7]) or -1, false;
    view.names = { [0] = f[4], [1] = f[5] };
    view.stake = tonumber(f[6]) or 0;
    setKind(f[3]);
    C.title:SetText(gameName(view.kind));
    C.setBanner("");
    stakeText:SetText("Stake: " .. stakeName(view.stake));
    showLobby(false);
    local playing = view.side >= 0;
    for _, w in ipairs({ resign, drawButton, decline }) do if playing then w:Show(); else w:Hide(); end end
    if playing then leaveWatch:Hide(); else leaveWatch:Show(); end
    decline:Hide();
end

function H.GB(f)
    if f[2] ~= view.match then return; end
    local moved = view.board ~= nil and view.last ~= f[5];
    view.board, view.toMove, view.last = f[3], tonumber(f[4]) or 0, f[5];
    view.clocks = { tonumber(f[6]) or 0, tonumber(f[7]) or 0 };
    view.legal, view.selected = {}, nil;
    local status = f[8] or "";
    local words = {};
    if string.find(status, "check", 1, true) then words[#words + 1] = "Check!"; end
    local offer = string.match(status, "offer(%d)");
    if offer then
        words[#words + 1] = (tonumber(offer) == view.side) and "You offered a draw." or "Draw offered.";
    end
    for side in string.gmatch(status, "away(%d)") do
        words[#words + 1] = (view.names[tonumber(side)] or "?") .. " disconnected.";
    end
    statusText:SetText(table.concat(words, "  "));
    lastText:SetText(view.last ~= "" and ("Last move: " .. view.last) or "");
    if view.side >= 0 then
        local theirOffer = offer and tonumber(offer) ~= view.side;
        drawButton.label:SetText(theirOffer and "ACCEPT DRAW" or "OFFER DRAW");
        if theirOffer then decline:Show(); else decline:Hide(); end
    end
    drawBoard(moved);
    drawPlayers();
    if moved then PlaySound("igMainMenuOptionCheckBoxOn"); end
end

function H.GV(f)
    for _, move in ipairs(C.split(f[2] or "", ",")) do
        if move ~= "" then view.pendingLegal[#view.pendingLegal + 1] = move; end
    end
end
function H.GVE()
    view.legal, view.pendingLegal = view.pendingLegal, {};
end

local REASONS = { checkmate = "Checkmate", stalemate = "Stalemate", fifty = "Fifty-move rule",
                  repetition = "Repetition", material = "Insufficient material", resign = "Resignation", time = "Out of time",
                  forfeit = "Forfeit", agreement = "Agreed draw", nomoves = "No moves left", quiet = "Draw" };

function H.GE(f)
    if f[2] ~= view.match then return; end
    view.over = true;
    view.legal = {};
    local winner, reason, paid = tonumber(f[3]) or 2, f[4] or "", tonumber(f[5]) or 0;
    local text = REASONS[reason] or reason;
    if winner == 2 then
        C.celebrate("draw", nil);
        C.setBanner(text .. (view.stake > 0 and "  -  stakes returned" or ""));
    elseif view.side < 0 then
        C.celebrate(reason == "checkmate" and "checkmate" or "gameover", "confetti", -100, -200);
        C.setBanner((view.names[winner] or "?") .. " wins  -  " .. text);
    elseif winner == view.side then
        C.celebrate(reason == "checkmate" and "checkmate" or "youwin", "confetti", -100, -200);
        C.setBanner(text .. (paid > 0 and ("  +" .. C.money(paid - view.stake)) or ""));
        PlaySound("LOOTWINDOWCOINSOUND");
    else
        C.celebrate("youlose", nil);
        C.setBanner(text .. (view.stake > 0 and ("  -" .. C.money(view.stake)) or ""));
    end
    for _, w in ipairs({ resign, drawButton, decline }) do w:Hide(); end
    C.after(5, function()
        if view.over and C.state.game == "board" then
            enterLobby();
            C.send("GL");
        end
    end);
end

-- Read-only view for /dump and the Lua test.
EsteriaCasinoBoardsDebug = { view = view, lobby = lobby, squares = squares, highlights = highlights, rows = rows,
                             post = postButton, stake = stakeBox, resign = resign, draw = drawButton,
                             house = houseButton, level = levelButton };
