"""Runs fate_page.lua (with the style kit, the creator skin, the trait catalog and the picker) against the skin
test's fake glue layer and checks the FATE step: the point budget and its rules, what build it sends, the summary
before creating, Back, a new character, paid customization and the tweens."""
import re
from pathlib import Path
from lupa.lua51 import LuaRuntime

HERE = Path(__file__).parent
# shortcut: reuses the skin test's harness by slicing its source; move HARNESS to a shared module if a third test needs it.
HARNESS = re.search(r"HARNESS = r'''(.*?)'''", (HERE / "test_creator_skin.py").read_text(encoding="utf-8"), re.S).group(1)
NATIVES = r'''
sent, okays, backs = {}, 0, 0
build = ""
-- The native bridge's own checks: whole numbers, ascending, unique, at most eight.
function SetCreateTraitList(...)
    local ids = { ... }
    if #ids > 8 then return false end
    for i, id in ipairs(ids) do
        if id < 1 or (i > 1 and id <= ids[i - 1]) then return false end
    end
    build = table.concat(ids, ","); sent[#sent + 1] = build; return true
end
function CharacterCreate_Okay() okays = okays + 1; okayBuild = build end
function CharacterCreate_Back() backs = backs + 1 end
function CharacterCreate_TogglePersonalization() end
function CharacterCreate_OnShow() end
classFile = "WARRIOR"
function GetSelectedClass() return "Warrior", classFile, 1 end
CharCreateOkayButton:SetText("Accept")
function CharCreateOkayButton:Enable() self.off = false end
function CharCreateOkayButton:Disable() self.off = true end
'''

L = LuaRuntime()
L.execute(HARNESS)
L.execute(NATIVES)
for name in ("esteria_style.lua", "creator_skin.lua", "fate_traits.lua", "fate_picker.lua", "fate_page.lua"):
    L.execute((HERE / name).read_text(encoding="utf-8"))
L.execute(r'''
local function check(cond, msg) if not cond then error("FAIL: " .. msg, 2) end print("ok  " .. msg) end
local function fire(n) for _ = 1, n or 1 do
    for _, o in ipairs(created) do local f = o._scripts.OnUpdate if f then f(o, 1) end end
end end
local P, T = EsteriaFatePicker, EsteriaFateTraits
local function set(...) local s = {} for _, id in ipairs({ ... }) do s[id] = true end return s end

-- ---- the catalog and the advisory rules (the server's are PerkTraitRules.h) ----
check(#T.list == 162 and T.list[1].id == 1 and T.list[162].id == 162, "162 traits, ids 1..162 in order")
local live = 0 for _, t in ipairs(T.list) do if not t.lock then live = live + 1 end end
check(live == 155, "155 traits can be picked, the rest are locked with a reason")
local _, _, left = P.Budget(set())
check(left == 4 and P.Problem(set(), "WARRIOR") == nil, "empty build: 4 points, valid")
check(P.Problem(set(119), "WARRIOR") == nil and select(3, P.Budget(set(119))) == 0, "a 4 point merit spends the base points")
check(string.find(P.Problem(set(119, 126), "WARRIOR"), "Over budget by 3"), "two merits without flaws are over budget")
check(P.Problem(set(119, 126, 130, 145, 147, 149), "WARRIOR") == nil, "3 + 3 + 2 flaw points pay for three merits")
check(string.find(P.Blocked(P.byId[149], set(139, 147), "WARRIOR"), "at most 8"), "a third 3 point flaw would pass the flaw cap")
check(string.find(P.Blocked(P.byId[11], set(1, 9), "WARRIOR"), "At most 2 quirks"), "third quirk refused")
check(string.find(P.Blocked(P.byId[3], set(2), "WARRIOR"), "Glass Cannon"), "Ironbound conflicts with Glass Cannon")
check(string.find(P.Blocked(P.byId[119], set(2), "WARRIOR"), "Glass Cannon"), "conflicts cross kinds: Vigorous and Glass Cannon")
check(P.Blocked(P.byId[161], set(), "WARRIOR") and not P.Blocked(P.byId[161], set(), "MAGE"), "a mana flaw is not free points for a warrior")
check(P.Blocked(P.byId[133], set(), "MAGE") and not P.Blocked(P.byId[133], set(), "HUNTER"), "pet traits need a pet class")
check(P.Blocked(P.byId[111], set(), "WARRIOR") == P.byId[111].lock and P.byId[111].lock ~= nil, "a locked trait says why")
check(P.Points(P.byId[119]) == "-4" and P.Points(P.byId[139]) == "+3" and P.Points(P.byId[9]) == "0", "points read -N, +N and 0")

-- ---- the page ----
CharacterCreateEnumerateRaces()
fire(2)   -- install, then tick
check(FatePagePanel ~= nil, "page built")
check(not FatePage_Open() and not FatePage_Follows(), "not offered on the race page")

-- Appearance page of a race without the Body page: Accept reads Next and opens Fate instead of creating.
CharacterCreate.personalizationMode = true; CharCreateOkayButton:Show(); fire()
check(FatePage_Follows(), "Fate follows Appearance when there is no Body page")
CharacterCreate_Okay()
check(okays == 0 and FatePage_Open(), "Accept on Appearance opens Fate, does not create")
fire()
check(FatePagePanel:IsShown() and CharCreateOkayButton:GetText() == "Accept", "page shown, Accept labelled Accept")
check(not CharacterCustomizationButtonFrame1:IsShown(), "appearance rows hidden")
check(not CharacterCreateGenderButtonsContainer:IsShown(), "gender picks hidden")
check(FatePageList._alpha == 1 and FatePageDetail._alpha == 1, "panels finished their entrance")
local p = FatePageList._points[1]
check(p[2] == CharacterCreateFrame and p[4] == 55 and p[5] == -140, "list sits on the left panel's row line")

-- The list is a window on the catalog, not 162 frames.
check(FatePageRow11 ~= nil and FatePageRow12 == nil, "eleven rows drawn, the rest scrolled to")
check(FatePageRow1.trait.name == "Wanderer" and FatePageRow7.trait.name == "Outlaw Heart", "rows start at the original backgrounds")
FatePageList._scripts.OnMouseWheel(FatePageList, -1)
check(FatePageRow1.trait.id == 4, "the wheel scrolls three rows")
FatePageList._scripts.OnMouseWheel(FatePageList, 1)
FatePageSearch:SetText("glass"); FatePageSearch._scripts.OnTextChanged(FatePageSearch)
check(FatePageRow1.trait.name == "Glass Cannon" and FatePageRow1:IsShown(), "search finds Glass Cannon")
FatePageSearch:SetText(""); FatePageSearch._scripts.OnTextChanged(FatePageSearch)

-- Picking: a quirk, then a merit that needs a flaw to pay for it.
local function click(id)
    for i = 1, 11 do local row = _G["FatePageRow" .. i]
        if row.trait and row.trait.id == id then row._scripts.OnClick(row) return true end end
    error("trait " .. id .. " is not on screen")
end
click(2)
check(FatePageRow2.name._text == "Glass Cannon" and FatePageRow2.mark._text == "CHOSEN", "Glass Cannon chosen")
click(3)
check(FatePageRow3.mark._text == "UNAVAILABLE", "Ironbound cannot join Glass Cannon")
fire()
check(not CharCreateOkayButton.off, "a valid build leaves Accept on")

-- Accept shows the summary first, then creates with exactly the build.
CharacterCreate_Okay()
check(okays == 0 and FatePageReview:IsShown(), "first Accept shows the summary")
check(string.find(FatePageReview.body._text, "Glass Cannon") and string.find(FatePageReview.sum._text, "= 4 unspent"), "summary lists the trait and the points")
CharacterCreate_Okay()
check(okays == 1 and okayBuild == "2", "second Accept creates with the build")
-- the native clears its pick after the packet; a retry (name taken) sends it again without asking twice
build = ""; CharacterCreate_Okay()
check(okays == 2 and okayBuild == "2", "retried Accept resends the build")

-- Over budget: editing is allowed, leaving is not.
local function scrollTo(id) for _ = 1, 60 do
    for i = 1, 11 do local row = _G["FatePageRow" .. i] if row.trait and row.trait.id == id then return end end
    FatePageList._scripts.OnMouseWheel(FatePageList, -1) end error("cannot reach " .. id) end
FatePageList._scripts.OnMouseWheel(FatePageList, 100)
scrollTo(126); click(126); scrollTo(130); click(130)
fire()
check(CharCreateOkayButton.off, "over budget (3 + 2 against 4) turns Accept off")
local before = okays; CharacterCreate_Okay()
check(okays == before and not FatePageReview:IsShown(), "an over-budget build is never created")
scrollTo(145); click(145)   -- Clumsy Climber, +2
fire()
check(not CharCreateOkayButton.off, "a flaw pays the difference")
CharacterCreate_Okay()
check(FatePageReview:IsShown(), "a changed build is shown again before creating")
CharacterCreate_Back()
check(backs == 0 and not FatePageReview:IsShown() and FatePage_Open(), "Back closes the summary first")
CharacterCreate_Okay(); CharacterCreate_Okay()
check(okayBuild == "2,126,130,145", "the build goes out ascending")

-- The class decides what can be picked.
classFile = "MAGE"; fire()
FatePageList._scripts.OnMouseWheel(FatePageList, 100); scrollTo(161)
for i = 1, 11 do local row = _G["FatePageRow" .. i] if row.trait and row.trait.id == 161 then
    check(row.mark._text == "FLAW", "Unfocused Mage is open to a mage") end end
classFile = "WARRIOR"; fire()
for i = 1, 11 do local row = _G["FatePageRow" .. i] if row.trait and row.trait.id == 161 then
    check(row.mark._text == "UNAVAILABLE", "and closed to a warrior") end end

-- Back leaves Fate first, then goes to the stock handler.
CharacterCreate_Back()
check(backs == 0 and not FatePage_Open(), "Back closes Fate")
fire()
check(not FatePagePanel:IsShown() and not CharCreateOkayButton.off, "page hidden after Back, Accept on")
CharacterCreate_Back()
check(backs == 1, "second Back reaches the stock handler")

-- A new character starts Unmarked.
CharacterCreate_Okay()
CharacterCreate_OnShow()
check(sent[#sent] == "" and not FatePage_Open(), "new character resets to no traits and closes Fate")
CharacterCreate_Okay(); fire()
before = okays; CharacterCreate_Okay()
check(okays == before + 1 and okayBuild == "", "an empty build creates at once, no summary")
CharacterCreate_Back()

-- Paid customization never offers the step.
PAID_SERVICE_TYPE = 1; fire()
check(not FatePage_Follows() and not FatePage_Offered(), "not offered for paid customization")
before = okays; CharacterCreate_Okay()
check(okays == before + 1, "Accept goes straight through for paid customization")
PAID_SERVICE_TYPE = nil

-- Reduce motion snaps tweens to their end state.
EsteriaStyle.reduceMotion = true
local got; EsteriaStyle.Tween("t", 1, 1, function(v) got = v end)
check(got == 1, "reduce motion applies the end state at once")
EsteriaStyle.reduceMotion = nil
local seen = {}; EsteriaStyle.Tween("t", 2, 0, function(v) seen[#seen + 1] = v end)
fire(3)
check(seen[1] == 0 and seen[#seen] == 1, "tween starts at 0 and lands on 1")
print("PASS")
''')
