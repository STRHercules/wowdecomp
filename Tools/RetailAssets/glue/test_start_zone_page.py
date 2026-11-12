"""Runs start_zone_page.lua (after the style kit, skin and Fate page) against the skin test's fake glue layer and
checks the ZONE step: it follows Fate, lists the faction's zones, sends the pick, Back, new characters, Death Knights."""
import re
from pathlib import Path
from lupa.lua51 import LuaRuntime

HERE = Path(__file__).parent
# shortcut: reuses the skin test's harness by slicing its source, like test_fate_page.py.
HARNESS = re.search(r"HARNESS = r'''(.*?)'''", (HERE / "test_creator_skin.py").read_text(encoding="utf-8"), re.S).group(1)
NATIVES = r'''
zones, okays, backs = {}, 0, 0
function SetCreateTraitList(...) return true end
function SetCreateStartZone(id)
    if id < 0 or id > 8 then return false end
    zones[#zones + 1] = id; zone = id; return true
end
function CharacterCreate_Okay() okays = okays + 1; okayZone = zone end
function CharacterCreate_Back() backs = backs + 1 end
function CharacterCreate_TogglePersonalization() end
function CharacterCreate_OnShow() end
classFile = "WARRIOR"
function GetSelectedClass() return "Warrior", classFile, 1 end
CharacterCreate.selectedRaceID = 1   -- Human
function GetFactionForRaceID(id) return (id == 2 or id == 5 or id == 6 or id == 8 or id == 10) and "Horde" or "Alliance" end
CharCreateOkayButton:SetText("Accept")
'''

L = LuaRuntime()
L.execute(HARNESS)
L.execute(NATIVES)
for name in ("esteria_style.lua", "creator_skin.lua", "fate_traits.lua", "fate_picker.lua", "fate_page.lua",
             "start_zone_page.lua"):
    L.execute((HERE / name).read_text(encoding="utf-8"))
L.execute(r'''
local function check(cond, msg) if not cond then error("FAIL: " .. msg, 2) end print("ok  " .. msg) end
local function fire(n) for _ = 1, n or 1 do
    for _, o in ipairs(created) do local f = o._scripts.OnUpdate if f then f(o, 1) end end
end end
local function shownRows() local n = 0 for i = 1, 8 do if _G["StartZonePageRow" .. i]:IsShown() then n = n + 1 end end return n end
CharacterCreateEnumerateRaces()
fire(3)
check(StartZonePagePanel ~= nil and not StartZonePage_Open(), "page built, closed on the race page")
CharacterCreate.personalizationMode = true; CharCreateOkayButton:Show(); fire()

-- Appearance -> Fate -> Zone. Accept never creates before the last page.
CharacterCreate_Okay()
check(FatePage_Open() and not StartZonePage_Open() and okays == 0, "Accept on Appearance opens Fate")
fire()
check(CharCreateOkayButton:GetText() == "Next", "Accept reads Next on Fate while the zone page follows")
CharacterCreate_Okay()
check(StartZonePage_Open() and okays == 0, "Accept on Fate opens the zone page, does not create")
fire()
check(StartZonePagePanel:IsShown() and not FatePagePanel:IsShown(), "zone page shown over Fate")
check(CharCreateOkayButton:GetText() == "Accept", "Accept labelled Accept on the last page")
check(shownRows() == 8, "Human sees all eight zones, Horde ones included")
check(StartZonePageRow1.name:GetText() == "Northshire Valley" and StartZonePageRow8.name:GetText() == "Sunstrider Isle",
    "zones in fixed order, no Homeland row")
check(zones[#zones] == 1 and StartZonePageRow1.mark:IsShown(), "opens on the race's own zone, chosen")

StartZonePageRow3._scripts.OnClick(StartZonePageRow3)
check(zones[#zones] == 3 and StartZonePageRow3.mark:IsShown() and not StartZonePageRow1.mark:IsShown(), "Shadowglen chosen")
check(StartZonePageDetail ~= nil and StartZonePagePanel:IsShown(), "detail up")
CharacterCreate_Okay()
check(okays == 1 and okayZone == 3, "Accept creates with zone 3")
zone = 0; CharacterCreate_Okay()
check(okays == 2 and okayZone == 3, "a retried Accept resends the zone")

CharacterCreate_Back()
check(backs == 0 and not StartZonePage_Open() and FatePage_Open(), "Back closes the zone page and returns to Fate")
fire()
check(not StartZonePagePanel:IsShown() and FatePagePanel:IsShown(), "Fate shown again")

-- Any race sees every zone; an unmapped (custom) race has no Homeland art.
CharacterCreate_Okay()
CharacterCreate.selectedRaceID = 2; CharacterCreate_Back(); CharacterCreate_Okay()
check(shownRows() == 8 and StartZonePageRow1.name:GetText() == "Northshire Valley" and StartZonePageRow5.mark:IsShown() and zones[#zones] == 5,
    "Orc sees the same list with Valley of Trials chosen")
CharacterCreate_Back()
CharacterCreate.selectedRaceID = 99; CharacterCreate_Okay()
check(shownRows() == 8 and zones[#zones] == 0 and not StartZonePageRow1.mark:IsShown(), "custom race: every zone, none chosen, server default")
CharacterCreate_Back()

-- Death Knights always start in Acherus.
classFile = "DEATHKNIGHT"; fire()
check(not StartZonePage_Offered(), "not offered to a Death Knight")
CharacterCreate_Okay()
check(okays == 3, "a Death Knight's Accept on Fate creates")
classFile = "WARRIOR"

-- A new character starts in its race's own zone.
CharacterCreate_Okay(); CharacterCreate_OnShow()
check(zones[#zones] == 0 and not StartZonePage_Open(), "new character resets to the race's own zone (id 0)")
print("PASS")
''')
