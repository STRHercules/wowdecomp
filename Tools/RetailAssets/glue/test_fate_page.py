"""Runs fate_page.lua (with the style kit and the creator skin) against the skin test's fake glue layer and checks
the FATE step: when it opens, what trait it sends, Back, a new character, paid customization and the tweens."""
import re
from pathlib import Path
from lupa.lua51 import LuaRuntime

HERE = Path(__file__).parent
# shortcut: reuses the skin test's harness by slicing its source; move HARNESS to a shared module if a third test needs it.
HARNESS = re.search(r"HARNESS = r'''(.*?)'''", (HERE / "test_creator_skin.py").read_text(encoding="utf-8"), re.S).group(1)
NATIVES = r'''
sent, okays, backs = {}, 0, 0
function SetCreateTrait(id)
    if id < 0 or id > 8 then return false end
    sent[#sent + 1] = id; trait = id; return true
end
function CharacterCreate_Okay() okays = okays + 1; okayTrait = trait end
function CharacterCreate_Back() backs = backs + 1 end
function CharacterCreate_TogglePersonalization() end
function CharacterCreate_OnShow() end
CharCreateOkayButton:SetText("Accept")
'''

L = LuaRuntime()
L.execute(HARNESS)
L.execute(NATIVES)
for name in ("esteria_style.lua", "creator_skin.lua", "fate_page.lua"):
    L.execute((HERE / name).read_text(encoding="utf-8"))
L.execute(r'''
local function check(cond, msg) if not cond then error("FAIL: " .. msg, 2) end print("ok  " .. msg) end
local function fire(n) for _ = 1, n or 1 do
    for _, o in ipairs(created) do local f = o._scripts.OnUpdate if f then f(o, 1) end end
end end
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
check(FatePageTrait1._alpha == 1 and FatePageTrait7._alpha == 1, "rows finished their entrance")
local p = FatePageTrait1._points[1]
check(p[2] == CharacterCreateFrame and p[4] == 55 and p[5] == -140, "first row sits on the left panel's row line")

check(FatePageTrait9 and FatePageTrait7.trait.name == "Folk Hero" and FatePageTrait8.trait.name == "Outlaw Heart",
    "all nine traits listed, Folk Hero and Outlaw Heart included")
FatePageTrait7._scripts.OnClick(FatePageTrait7)
check(sent[#sent] == 6 and FatePageDetail ~= nil, "Folk Hero can be picked")
-- Picking Glass Cannon sends it; Accept creates with it.
FatePageTrait3._scripts.OnClick(FatePageTrait3)
check(sent[#sent] == 2 and FatePageTrait3.mark:IsShown() and not FatePageTrait1.mark:IsShown(), "Glass Cannon chosen")
check(FatePageDetail ~= nil and FatePagePanel:IsShown(), "detail panel up")
CharacterCreate_Okay()
check(okays == 1 and okayTrait == 2, "Accept on Fate creates with trait 2")
-- the native clears its pick after the packet; a retry (name taken) sends it again
trait = 0; CharacterCreate_Okay()
check(okays == 2 and okayTrait == 2, "retried Accept resends the trait")

-- Back leaves Fate first, then goes to the stock handler.
CharacterCreate_Back()
check(backs == 0 and not FatePage_Open(), "Back closes Fate")
fire()
check(not FatePagePanel:IsShown(), "page hidden after Back")
CharacterCreate_Back()
check(backs == 1, "second Back reaches the stock handler")

-- A new character starts Unmarked.
CharacterCreate_Okay(); FatePageTrait5._scripts.OnClick(FatePageTrait5)
CharacterCreate_OnShow()
check(sent[#sent] == 0 and not FatePage_Open(), "new character resets to Unmarked and closes Fate")

-- Paid customization never offers the step.
PAID_SERVICE_TYPE = 1; fire()
check(not FatePage_Follows() and not FatePage_Offered(), "not offered for paid customization")
local before = okays; CharacterCreate_Okay()
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
