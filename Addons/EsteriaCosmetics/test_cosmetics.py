"""Focused Lua behavior check using UI mocks; does not claim native rendering acceptance."""
from pathlib import Path
from lupa.lua51 import LuaRuntime

HERE = Path(__file__).resolve().parent


def main():
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute(r'''
unpack=unpack or table.unpack; tinsert=table.insert
SlashCmdList={}; UISpecialFrames={}; sent={}; regions={}; combat=false
BOOKTYPE_SPELL="spell"
function InCombatLockdown() return combat end
function UnitName() return "Tester" end
function SendAddonMessage(prefix,body) table.insert(sent,{prefix,body}) end
function GetNumSpellTabs() return 1 end
function GetSpellTabInfo() return "Cosmetics", "icon",0,1 end
function GetSpellLink() return "|Hspell:970340|h[Wings]|h" end
function GetSpellInfo() return "Cosmetic Wings" end
function UnitBuff() return nil end
function PlaySound() end
function GetCursorPosition() return 0,0 end
function GetBindingAction() return "taken" end
function strsplit(sep,s)
 local p=s:find(sep,1,true); if not p then return s end
 return s:sub(1,p-1),s:sub(p+1)
end
local methods={}
local function new(kind,name,parent)
 local f=setmetatable({kind=kind,name=name,parent=parent,scripts={},events={},attributes={},shown=true,enabled=1,text=""},{__index=function(_,k) if k=="dragX" or k=="spin" or k=="waiting" or k=="row" or k=="playerPreview" or k=="emote" then return nil end; return methods[k] or function() end end})
 regions[#regions+1]=f
 if name then _G[name]=f end
 return f
end
function CreateFrame(kind,name,parent)
 local f=new(kind,name,parent)
 if kind=="Slider" and name then for _,suffix in ipairs({"Low","High","Text"}) do _G[name..suffix]=new("FontString") end end
 return f
end
function methods:GetName() return self.name end
function methods:GetValue() return self.value or 0 end
function methods:SetValue(value) self.value=value; if self.scripts.OnValueChanged then self.scripts.OnValueChanged(self,value) end end
function methods:CreateTexture() return new("Texture",nil,self) end
function methods:CreateFontString() return new("FontString",nil,self) end
function methods:SetScript(key,fn) self.scripts[key]=fn end
function methods:RegisterEvent(key) self.events=self.events or {}; self.events[key]=true end
function methods:Show() local old=self.shown; self.shown=true; if not old and self.scripts.OnShow then self.scripts.OnShow(self) end end
function methods:Hide() local old=self.shown; self.shown=false; if old and self.scripts.OnHide then self.scripts.OnHide(self) end end
function methods:IsShown() if rawget(self,"parent") and not self.parent:IsShown() then return false end; return self.shown end
function methods:SetParent(parent) self.parent=parent end
function methods:GetParent() return self.parent end
function methods:SetText(value) self.text=value; if self.scripts.OnTextChanged then self.scripts.OnTextChanged(self) end end
function methods:GetText() return self.text end
function methods:Enable() self.enabled=1 end
function methods:Disable() self.enabled=0 end
function methods:IsEnabled() return self.enabled end
function methods:SetAttribute(k,v) assert(not combat,"Protected mutation in combat"); self.attributes[k]=v end
function methods:SetAlpha(value) self.alpha=value end
function methods:SetSize(w,h) self.width=w;self.height=h end
function methods:GetWidth() return self.width or 100 end
function methods:GetHeight() return self.height or 100 end
function methods:GetEffectiveScale() return 1 end
function methods:SetSheathed(value) self.sheathed=value end
function methods:SetUnit(value) self.previewUnit=value;self.modelPath="player.m2" end
function methods:ClearModel() self.modelPath=nil end
function methods:SetModel(value) self.modelPath=value end
function methods:GetModel() return rawget(self,"modelPath") end
function methods:GetFrameLevel() return 1 end
function methods:GetPoint() return "CENTER",UIParent,"CENTER",0,0 end
UIParent=new("Frame","UIParent")
GameTooltip=new("Tooltip","GameTooltip")
UIErrorsFrame=new("Frame","UIErrorsFrame")
DEFAULT_CHAT_FRAME=new("Frame","DEFAULT_CHAT_FRAME")
EsteriaCosmeticsData={{id=100,item=200,display=300,name="Shield",model="shield.m2"},{id=101,item=201,display=301,name="Pack",model="pack.m2"}}
EsteriaCosmeticsWings={{spell=970340,name="Wings",icon="wing",model="wing.m2"}}
previewsReady=false
function EsteriaCosmeticsFit() return previewsReady end
function RetailModelFit() return true end -- An available fallback must not bypass the native loading gate.
function EsteriaCosmeticsWingPreview() return previewsReady end
function EsteriaCosmeticsPreview() return previewsReady end
testCompanions={MOUNT={},CRITTER={}}
function GetNumCompanions(kind)return #testCompanions[kind] end
function GetCompanionInfo(kind,i)return unpack(testCompanions[kind][i]) end
function PickupCompanion(kind,i)pickedCompanion={kind,i} end
function PickupMacro(id)pickedMacro=id end
coFavorites={MOUNT={},CRITTER={}}
DragonUI={Collections={
 IsFavorite=function(kind,id)return coFavorites[kind][id] or false end,
 ToggleFavorite=function(kind,id)coFavorites[kind][id]=not coFavorites[kind][id] or nil end,
 MountCategory=function(spell)return spell~=1010,spell==1010,false end,
 EnsureRandomMacro=function(kind)macroKind=kind;return 7 end,
 SummonRandomFavorite=function(kind)randomKind=kind end,
}}
EsteriaEmotesData={{900,"Dance","dance","dance",3}}
function dispatch(event,...)
 for _,f in ipairs(regions) do if f.events and f.events[event] then f.scripts.OnEvent(f,event,...) end end
end
''')
    lua.execute((HERE.parent / 'EsteriaEmotes/esteria_style.lua').read_text(encoding='utf-8'))
    lua.execute((HERE.parent / 'EsteriaEmotes/EsteriaEmotes.lua').read_text(encoding='utf-8'))
    lua.execute((HERE/'Companions.lua').read_text())
    lua.execute((HERE / 'EsteriaCosmetics.lua').read_text(encoding='utf-8'))
    lua.execute(r'''
dispatch("PLAYER_LOGIN")
EsteriaCosmetics_Toggle("Backpacks")
assert(EsteriaCosmeticsFrame:IsShown())
local function hasText(text)
 for _,f in ipairs(regions) do if f.text==text then return true end end
 return false
end
assert(hasText("0 of 2 collected"))
dispatch("CHAT_MSG_ADDON","ESTCOS","reset","WHISPER","Tester")
dispatch("CHAT_MSG_ADDON","ESTCOS","known\t101","WHISPER","Intruder")
dispatch("CHAT_MSG_ADDON","ESTCOS","known\t100","WHISPER","Tester")
dispatch("CHAT_MSG_ADDON","ESTCOS","selected\t100","WHISPER","Tester")
assert(hasText("0 of 2 collected")) -- Ownership stays unchanged until done.
dispatch("CHAT_MSG_ADDON","ESTCOS","done","WHISPER","Tester")
assert(hasText("1 of 2 collected"))
for _,f in ipairs(regions) do
 if f.kind=="Button" and f.row and f.row.id==100 and f.scripts.OnClick then f.scripts.OnClick(f); break end
end
for _,f in ipairs(regions) do
 if rawget(f,"label") and f.label.text=="Apply" and f.scripts.PostClick then f.scripts.PostClick(f) end
end
assert(sent[#sent][1]=="ESTCOS" and sent[#sent][2]=="apply\t100")
for _,f in ipairs(regions) do
 if rawget(f,"label") and f.label.text=="Remove" and f.scripts.PostClick then f.scripts.PostClick(f) end
end
assert(sent[#sent][1]=="ESTCOS" and sent[#sent][2]=="apply\t0")
EsteriaCosmetics_Toggle("Wings")
local cardsOnly,playerStage=0,0
for _,f in ipairs(regions) do
 if f.kind=="PlayerModel" and f.row and f.row.spell then
  assert(f.alpha==0,"Loading geometry must not flash at its default size")
  f.scripts.OnUpdate(f,0.016)
  assert(f.alpha==0)
  if f.playerPreview then playerStage=playerStage+1 else cardsOnly=cardsOnly+1 end
 end
end
assert(cardsOnly==1 and playerStage==1,"Only the selected wing stage may include the player")
previewsReady=true
for _,f in ipairs(regions) do
 if f.kind=="PlayerModel" and f.row and f.row.spell then f.scripts.OnUpdate(f,0.016); assert(f.alpha==1) end
end
local spellButton
for _,f in ipairs(regions) do if f.attributes.type=="macro" and f.attributes.macrotext:find("/cast",1,true) then spellButton=f end end
assert(spellButton and spellButton.attributes.macrotext:find("/stopmacro [combat]",1,true))
combat=true
EsteriaCosmetics_Toggle("Backpacks")
dispatch("UNIT_AURA","player")
combat=false
dispatch("PLAYER_REGEN_ENABLED")
EsteriaEmotes_Toggle()
assert(EsteriaEmotesFrame and EsteriaEmotesFrame:IsShown())
assert(EsteriaEmotesFrame:GetParent():GetParent()==EsteriaCosmeticsFrame)
EsteriaEmotes.Play(900)
assert(sent[#sent][1]=="ESTEMO" and sent[#sent][2]=="play\t900")
EsteriaCosmetics_Toggle("Backpacks")
assert(not EsteriaEmotesFrame:IsShown())
EsteriaEmotes_Toggle()
assert(EsteriaEmotesFrame:IsShown())
EsteriaEmotesFrame.cosmeticsClose.scripts.OnClick()
assert(not EsteriaCosmeticsFrame:IsShown())
'''.replace('\\t', '\t'))
    prices = (HERE.parent / 'Manastorm_Transmog/Manastorm_Transmog.lua').read_text(encoding='utf-8')
    prices = 'local function PendingCost()' + prices.split('local function PendingCost()', 1)[1].split("-- The screen's real pixel size", 1)[0]
    lua.execute('''S={pending={[14]=3400014},cost={[14]=25000},info={hiddenFree=true}}; HIDDEN=1
        EsteriaCosmeticsData={{id=3400014}}; function IsChanged() return true end
    ''' + prices + '''
        local money,count=PendingCost(); assert(money==0 and count==1)
        S.pending[14]=123; money,count=PendingCost(); assert(money==25000 and count==1)
        S.pending[14]=0; money,count=PendingCost(); assert(money==0 and count==1)
        S.pending[14]=1; money,count=PendingCost(); assert(money==0 and count==1)
    ''')
    lua.execute('''
        EsteriaCollectionsMounts={{kind="mount",spell=1010,name="Account Mount",model="mount.m2",creatureDisplay=123,mountType="Flying"},{kind="mount",spell=1011,name="Locked Mount",model="locked.m2",creatureDisplay=124}}
        EsteriaCollectionsWeapons={{kind="weapon",id=3430084,display=3430084,name="Fire Sword",model="sword.m2",slot=15},{kind="weapon",id=3430085,display=3430085,name="Offhand Sword",model="offhand.m2",slot=16}}
        EsteriaCollectionsHearths={{kind="hearth",id=1002337,item=1002337,name="Dark Portal",spell=302827,model="portal.m2"},{kind="hearth",id=1002338,item=1002338,name="Lunar Hearthstone",spell=302828}}
        function RetailModelBindDisplay(model,display) model.boundDisplay=display;return true end
        function GetBindLocation()return "Stormwind" end
        local function snapshot()
            dispatch("CHAT_MSG_ADDON","ESTCOS","reset","WHISPER","Tester")
            dispatch("CHAT_MSG_ADDON","ESTCOS","weapons\\t3430084","WHISPER","Tester")
            dispatch("CHAT_MSG_ADDON","ESTCOS","mounts\\t1010","WHISPER","Tester")
            dispatch("CHAT_MSG_ADDON","ESTCOS","hearth\\t1002337\\t555\\t0","WHISPER","Tester")
            dispatch("CHAT_MSG_ADDON","ESTCOS","hearth\\t1002338\\t556\\t1","WHISPER","Tester")
            dispatch("CHAT_MSG_ADDON","ESTCOS","done","WHISPER","Tester")
        end
        snapshot()
        local function chooseCollection(value)
            for _,f in ipairs(regions) do if f.kind=="Button" and f.row and (f.row.spell==value or f.row.id==value) and f.scripts.OnClick then f.scripts.OnClick(f);return end end
        end
        local function action()
            for _,f in ipairs(regions) do if rawget(f,"label") and f.label.text~="" and f.scripts.PostClick and f.label.text~="Remove" and f.label.text~="Dismount" and f.label.text~="Dismiss" then return f end end
        end
        EsteriaCosmetics_Toggle("Mounts");chooseCollection(1010)
        assert(action().attributes.type=="macro" and action().attributes.macrotext:find("CastSpellByID(1010)",1,true))
        local bound=false
        for _,f in ipairs(regions)do if f.kind=="PlayerModel" and f.row and f.row.spell==1010 then assert(not f.playerPreview);bound=bound or f.boundDisplay==123 end end
        assert(bound,"Mount previews must bind the selected creature display")
        chooseCollection(1011);assert(action().enabled==0 and action().attributes.type==nil,"Locked mounts cannot be summoned")
        EsteriaCosmetics_Toggle("Weapons");chooseCollection(3430084);action().scripts.PostClick(action())
        assert(sent[#sent][1]=="TMOGC" and sent[#sent][2]=="A:15:3430084")
        local requests=#sent;chooseCollection(3430085);action().scripts.PostClick(action());assert(#sent==requests,"Uncollected weapons cannot be applied")
        hearthReady=false
        function EsteriaCosmeticsHearthPreview(model,spell,clock)
            model.previewSpell=spell;model.previewClock=clock;return hearthReady
        end
        local beforePreview=#sent
        EsteriaCosmetics_Toggle("Hearthstones");chooseCollection(1002337)
        local hearthModels=0
        for _,f in ipairs(regions)do
            if f.kind=="PlayerModel" and f.row and f.row.kind=="hearth" then
                hearthModels=hearthModels+1
                assert(f.playerPreview and f.previewUnit=="player","Hearth cards and stage must include the player, even without a standalone model")
                f.scripts.OnUpdate(f,.25);assert(f.alpha==0,"Wait for the complete casting kit")
                hearthReady=true;f.scripts.OnUpdate(f,.25)
                assert(f.alpha==1 and f.previewSpell==f.row.spell and f.previewClock==500)
                f.scripts.OnUpdate(f,.25);assert(f.previewClock==750,"Casting must keep animating after the loading gate opens")
                hearthReady=false
            end
        end
        assert(hearthModels==3 and #sent==beforePreview,"Previewing must not use a hearthstone or send a cast request")
        hearthReady=true
        assert(action().attributes.macrotext:find("/use item:1002337",1,true))
        EsteriaInventory={Request=function(_,kind,row,_,_,_,scope)digitalUse={kind,row,scope} end}
        chooseCollection(1002338);assert(action().attributes.type==nil);action().scripts.PostClick(action())
        for _,f in ipairs(regions)do
            if f.kind=="PlayerModel" and f.playerPreview and f.row and f.row.item==1002338 and f.parent.kind=="Frame" then
                assert(f.castClock==0,"Selecting another hearthstone resets the stage casting clock")
            end
        end
        assert(digitalUse[1]==9 and digitalUse[2].guid==556 and digitalUse[3]==0,"Digital hearth use must reuse carried inventory validators")
        local danced=false
        for _,f in ipairs(regions)do if f.kind=="DressUpModel" and f.sheathed then danced=true end end
        assert(danced,"Emote previews must dress weapons in their sheathed positions")
        EsteriaCosmeticsFrame:Hide()
    ''')
    lua.execute('''fitCalls={};fitDisplay=300;exportedFits=nil
        EsteriaCosmeticsDB.backpackRotationDefaults=1
        EsteriaCosmeticsDB.backpackFits={legacy={display=999,body="retail/legacy.m2",scale=1.2,x=.1,y=.2,z=.3,pitch=-7.5,yaw=12,roll=7.5},custom={display=998,body="retail/custom.m2",scale=1,x=0,y=0,z=0,pitch=22,yaw=0,roll=33}}
        EsteriaCosmeticsData={{id=100,display=300,name="Shield",fitScale=1.1,fitX=0.02},{id=101,display=301,name="Pack"}}
        function GetTime() return 0 end
        function EsteriaBackpackFitInfo() return fitDisplay,"retail/test-male.m2" end
        function EsteriaBackpackExportFits(text) exportedFits=text;return true,"WTF/Esteria/BackpackFits/BackpackFits-test.txt" end
        function EsteriaBackpackSetFit(display,body,scale,x,y,z,pitch,yaw,roll)
            fitCalls[#fitCalls+1]={display=display,body=body,scale=scale,x=x,y=y,z=z,pitch=pitch,yaw=yaw,roll=roll};return true
        end
    ''')
    lua.execute((HERE/'BackpackFitter.lua').read_text(encoding='utf-8'))
    lua.execute('''
        EsteriaBackpackFitter_Toggle()
        local legacy=EsteriaCosmeticsDB.backpackFits.legacy
        assert(legacy.pitch==0 and legacy.roll==0 and legacy.yaw==12 and legacy.scale==1.2,"Undo global rotations without changing other values")
        assert(EsteriaCosmeticsDB.backpackFits.custom.pitch==22 and EsteriaCosmeticsDB.backpackFits.custom.roll==33,"Preserve distinct custom angles")
        legacy.pitch=22;legacy.roll=33
        assert(EsteriaBackpackFitter:IsShown())
        assert(fitCalls[#fitCalls].scale==1.1 and fitCalls[#fitCalls].x==0.02,"Opening the fitter must preserve catalog defaults")
        EsteriaBackpackFit_scale:SetValue(1.25)
        EsteriaBackpackFit_x:SetValue(-0.1)
        EsteriaBackpackFit_pitch:SetValue(15);EsteriaBackpackFit_yaw:SetValue(-30);EsteriaBackpackFit_roll:SetValue(90)
        assert(fitCalls[#fitCalls].pitch==15 and fitCalls[#fitCalls].yaw==-30 and fitCalls[#fitCalls].roll==90)
        assert(fitCalls[#fitCalls].scale==1.25 and fitCalls[#fitCalls].x==-0.1)
        for _,f in ipairs(regions) do if f.text=="Save" and f.scripts.OnClick then f.scripts.OnClick(f) end end
        local saved=EsteriaCosmeticsDB.backpackFits["300;retail/test-male.m2"]
        assert(saved.scale==1.25 and saved.x==-0.1)
        assert(legacy.pitch==22 and legacy.roll==33,"Migration must not overwrite subsequent user edits")
        for _,f in ipairs(regions) do if f.text=="Reset" and f.scripts.OnClick then f.scripts.OnClick(f) end end
        assert(fitCalls[#fitCalls].scale==1.1 and fitCalls[#fitCalls].x==0.02)
        assert(saved.scale==1.25,"Reset must not silently overwrite saved profiles")
        dispatch("PLAYER_LOGIN")
        assert(fitCalls[#fitCalls].scale==1.25 and fitCalls[#fitCalls].body=="retail/test-male.m2")
        for _,f in ipairs(regions) do if f.text=="Next" and f.scripts.OnClick then f.scripts.OnClick(f) end end
        assert(sent[#sent][1]=="ESTCOS" and sent[#sent][2]=="apply\t101")
        assert(saved.scale==1.25 and saved.x==-0.1)
        fitDisplay=301;EsteriaBackpackFitter.scripts.OnUpdate(EsteriaBackpackFitter,0.1)
        EsteriaBackpackFit_scale:SetValue(0.8)
        for _,f in ipairs(regions) do if f.text=="Save" and f.scripts.OnClick then f.scripts.OnClick(f) end end
        for _,f in ipairs(regions) do if f.text=="Export" and f.scripts.OnClick then f.scripts.OnClick(f) end end
        assert(exportedFits:find("300;retail/test-male.m2;1.250000;-0.100000;0.000000;0.000000",1,true))
        assert(exportedFits:find(";15.000000;-30.000000;90.000000",1,true))
        assert(exportedFits:find("301;retail/test-male.m2;0.800000;0.000000;0.000000;0.000000",1,true))
        assert(not EsteriaBackpackFitExport,"Export must use the native text file writer instead of a copy window")
        for _,f in ipairs(regions) do if f.text=="Next" and f.scripts.OnClick then f.scripts.OnClick(f) end end
        assert(sent[#sent][2]=="apply\t100","Cycling must wrap to the first backpack")
    ''')
    lua.execute('''pickaxeCalls={};pickaxeExport=nil
        testPickaxeDisplay=3430001
        function EsteriaPickaxeFitInfo()return testPickaxeDisplay,"retail/test-male.m2" end
        function EsteriaPickaxeSetFit(display,body,scale,x,y,z,pitch,yaw,roll,sheathed)
            assert(type(pitch)=="number" and type(yaw)=="number" and type(roll)=="number")
            pickaxeCalls[#pickaxeCalls+1]={display=display,body=body,scale=scale,x=x,y=y,z=z,pitch=pitch,yaw=yaw,roll=roll,sheathed=sheathed};return true
        end
        function EsteriaPickaxeExportFits(text)pickaxeExport=text;return true,"PickaxeFits-test.txt" end
    ''')
    lua.execute((HERE/'EsteriaPickaxesData.lua').read_text(encoding='utf-8'))
    lua.execute((HERE/'PickaxeFitter.lua').read_text(encoding='utf-8'))
    lua.execute('''
        EsteriaPickaxeFitter_Toggle();assert(EsteriaPickaxeFitter:IsShown())
        EsteriaPickaxeFit_pitch:SetValue(15);EsteriaPickaxeFit_yaw:SetValue(-30)
        EsteriaPickaxeFit_x:SetValue(.12)
        assert(pickaxeCalls[#pickaxeCalls].pitch==15 and pickaxeCalls[#pickaxeCalls].yaw==-30)
        for _,f in ipairs(regions) do if f.parent==EsteriaPickaxeFitter and f.text=="Save" and f.scripts.OnClick then f.scripts.OnClick(f) end end
        assert(EsteriaCosmeticsDB.pickaxeFits["3430001;retail/test-male.m2;drawn"].x==.12)
        for _,f in ipairs(regions) do if f.parent==EsteriaPickaxeFitter and f.text=="Sheathed" then f.scripts.OnClick(f) end end
        assert(pickaxeCalls[#pickaxeCalls].sheathed and pickaxeCalls[#pickaxeCalls].x==.29)
        EsteriaPickaxeFit_z:SetValue(.3)
        for _,f in ipairs(regions) do if f.parent==EsteriaPickaxeFitter and f.text=="Save" and f.scripts.OnClick then f.scripts.OnClick(f) end end
        assert(EsteriaCosmeticsDB.pickaxeFits["3430001;retail/test-male.m2;sheathed"].z==.3)
        assert(EsteriaCosmeticsDB.pickaxeFits["3430001;retail/test-male.m2;drawn"].x==.12)
        for _,f in ipairs(regions) do if f.parent==EsteriaPickaxeFitter and f.text=="Export" and f.scripts.OnClick then f.scripts.OnClick(f) end end
        assert(pickaxeExport:find("0.120000;0.035000;-0.085000;15.000000;-30.000000;0.000000",1,true))
        assert(pickaxeExport:find(";sheathed",1,true) and pickaxeExport:find(";drawn",1,true))
        dispatch("PLAYER_LOGIN")
        local drawn,sheathed=false,false
        for _,p in ipairs(pickaxeCalls)do if p.sheathed and p.z==.3 then sheathed=true elseif not p.sheathed and p.x==.12 then drawn=true end end
        assert(drawn and sheathed)
        testPickaxeDisplay=3430083
        EsteriaPickaxeFitter_Toggle();EsteriaPickaxeFitter_Toggle()
        EsteriaPickaxeFit_x:SetValue(-.2)
        for _,f in ipairs(regions) do if f.parent==EsteriaPickaxeFitter and f.text=="Save" and f.scripts.OnClick then f.scripts.OnClick(f) end end
        assert(EsteriaCosmeticsDB.pickaxeFits["3430083;retail/test-male.m2;sheathed"].x==-.2)
        assert(EsteriaCosmeticsDB.pickaxeFits["3430001;retail/test-male.m2;sheathed"].z==.3)
        local function clickPickaxe(label)
            for _,f in ipairs(regions) do if f.parent==EsteriaPickaxeFitter and f.text==label and f.scripts.OnClick then f.scripts.OnClick(f) end end
        end
        local lastPickaxe=EsteriaPickaxesData[#EsteriaPickaxesData]
        testPickaxeDisplay=lastPickaxe.display
        EsteriaPickaxeFitter_Toggle();EsteriaPickaxeFitter_Toggle()
        EsteriaPickaxeFit_x:SetValue(-.27)
        clickPickaxe("Next")
        local first=EsteriaPickaxesData[1].display
        assert(sent[#sent][1]=="TMOGC" and sent[#sent][2]=="A:15:"..first,"Next must wrap and apply the main-hand weapon")
        assert(EsteriaCosmeticsDB.pickaxeFits[lastPickaxe.display..";retail/test-male.m2;sheathed"].x==-.27,"Cycling must save current state")
        local requests=#sent;clickPickaxe("Next");assert(#sent==requests,"Only one switch may be pending")
        dispatch("CHAT_MSG_ADDON","TMOGS","R:15:7","WHISPER","SomeoneElse")
        clickPickaxe("Next");assert(#sent==requests,"Ignore foreign results")
        dispatch("CHAT_MSG_ADDON","TMOGS","R:15:7","WHISPER","Tester")
        clickPickaxe("Next");assert(#sent==requests+1,"Server failure must release pending switch")
        testPickaxeDisplay=first;EsteriaPickaxeFitter.scripts.OnUpdate()
        assert(pickaxeCalls[#pickaxeCalls].sheathed,"Cycling must retain the selected fit tab")
        clickPickaxe("Previous")
        assert(sent[#sent][2]=="A:"..(lastPickaxe.slot or 15)..":"..lastPickaxe.display,"Previous must wrap back to the final weapon")
        testPickaxeDisplay=lastPickaxe.display;EsteriaPickaxeFitter.scripts.OnUpdate()
        assert(EsteriaPickaxeFit_x:GetValue()==-.27,"Cycling back must restore the saved fit")
        requests=#sent;combat=true;clickPickaxe("Next");combat=false
        assert(#sent==requests,"Cycling must not apply transmogs in combat")
        local typePicker
        for _,f in ipairs(regions) do if f.parent==EsteriaPickaxeFitter and f.text=="Type: All" then typePicker=f end end
        assert(typePicker)
        for i=1,20 do if typePicker.text=="Type: Fist (Left)" then break end;typePicker.scripts.OnClick(typePicker) end
        assert(typePicker.text=="Type: Fist (Left)")
        clickPickaxe("Next")
        assert(sent[#sent][2]=="A:16:3430017","Left fist must apply to off hand")
        dispatch("CHAT_MSG_ADDON","TMOGS","R:15:7","WHISPER","Tester")
        requests=#sent;clickPickaxe("Next");assert(#sent==requests,"Other-hand results must not clear the pending request")
        dispatch("CHAT_MSG_ADDON","TMOGS","R:16:7","WHISPER","Tester")
        clickPickaxe("Next");assert(#sent==requests+1)
    ''')
    lua.execute('''
        testCompanions.CRITTER={{901,"Rabbit",5010,"rabbit",true}}
        testCompanions.MOUNT={{902,"Account Mount",1010,"mount",false}}
        EsteriaCollectionsPets={{kind="pet",spell=5010,name="Rabbit",model="rabbit.m2"},{kind="pet",spell=5011,name="Locked Pet",model="locked.m2"}}
        EsteriaCollections_RefreshCompanions()
        EsteriaCosmetics_Show("Pets")
        local summon,dismiss,random,favorite
        for _,f in ipairs(regions)do
            if rawget(f,"label") then
                if f.label.text=="Summon" and f.scripts.PostClick then summon=f end
                if f.label.text=="Dismiss" then dismiss=f end
                if f.label.text=="Random Favorite Pet" then random=f end
                if f.label.text=="Favorite" and f.scripts.OnClick and not f.scripts.PostClick and f:IsShown() then favorite=f end
            end
        end
        assert(summon.attributes.macrotext:find('CallCompanion("CRITTER",1)',1,true) and dismiss.attributes.macrotext:find("CRITTER",1,true))
        assert(random)
        random.scripts.OnClick(random);assert(randomKind=="CRITTER")
        random.scripts.OnDragStart(random);assert(pickedMacro==7 and macroKind=="CRITTER")
        for _,f in ipairs(regions)do
            if f.kind=="Button" and f.row and f.row.spell==5010 then f.scripts.OnDragStart(f) end
        end
        assert(pickedCompanion[1]=="CRITTER" and pickedCompanion[2]==1)
        assert(favorite);favorite.scripts.OnClick(favorite)
        assert(coFavorites.CRITTER[901] and EsteriaCosmeticsDB.favorites["pet:5010"])
        local petModel
        for _,f in ipairs(regions)do if f.kind=="PlayerModel" and f.row and f.row.spell==5010 then petModel=f;break end end
        petModel:ClearModel();EsteriaCosmetics_Show("Mounts");EsteriaCosmetics_Show("Pets")
        assert(petModel:GetModel()=="rabbit.m2","Returning to a tab must reload models cleared by native HideThis")
        for _,f in ipairs(regions)do
            if f.kind=="Button" and f.row and f.row.spell==5011 then f.scripts.OnClick(f) end
        end
        assert(summon.enabled==0 and summon.attributes.type==nil,"Locked companions cannot be summoned")
        combat=true;pickedMacro=nil;random.scripts.OnDragStart(random);assert(pickedMacro==nil);combat=false
        EsteriaCosmetics_Show("Mounts")
        -- Mount types are rail rows (MOUNT TYPE section), one click each.
        local typeRow={}
        for _,f in ipairs(regions)do if rawget(f,"label") and f.Paint and f:IsShown() then typeRow[f.label.text]=f end end
        assert(typeRow["All mounts"] and typeRow.Ground and typeRow.Flying and typeRow.Aquatic,"Mounts show the type section")
        typeRow.Ground.scripts.OnClick(typeRow.Ground)
        assert(typeRow.Ground.selected and not typeRow["All mounts"].selected)
        for _,f in ipairs(regions)do
            if f.kind=="Button" and f:IsShown() and f.row then assert(f.row.spell~=1010,"Flying mounts must be filtered out") end
        end
        typeRow.Flying.scripts.OnClick(typeRow.Flying);assert(typeRow.Flying.selected)
        -- Customize opens the mount customizer for an owned mount the client has options for.
        local opened
        EsteriaMountCustomize_GetDisplay=function(creature) return creature==902 and 77 or nil end
        EsteriaMountCustomize_GetOptions=function(display) return display==77 and {{}} or nil end
        EsteriaMountCustomize_GetLook=function() return {} end
        EsteriaMountCustomize_Preview=function() end
        EsteriaMountCustomize_Open=function(creature) opened=creature end
        typeRow["All mounts"].scripts.OnClick(typeRow["All mounts"])
        for _,f in ipairs(regions)do if f.kind=="Button" and f.row and f.row.spell==1010 then f.scripts.OnClick(f) end end
        local customize
        for _,f in ipairs(regions)do if rawget(f,"label") and f.label.text=="Customize" then customize=f end end
        assert(customize and customize:IsShown() and customize.enabled~=0,"Customize is offered for an owned customizable mount")
        customize.scripts.OnClick(customize);assert(opened==902,"Customize opens that mount's creature")
        EsteriaCosmeticsDB.favorites["mount:1010"]=true
        EsteriaCollections_MigrateFavorites(EsteriaCosmeticsDB.favorites)
        assert(coFavorites.MOUNT[902],"Existing mount favorites must reach the random-favorite service")
        EsteriaIncarnationsData={{id=1,kind="form",name="Bear",icon="bear",looks={{101,"Brown Bear","bear.m2"}}}}
    ''')
    lua.execute((HERE.parent/'EsteriaIncarnations/EsteriaIncarnations.lua').read_text())
    lua.execute('''
        EsteriaCosmetics_Show("Incarnations")
        assert(EsteriaIncarnationsFrame:IsShown() and EsteriaIncarnationsFrame:GetParent():GetParent()==EsteriaCosmeticsFrame)
        EsteriaCosmetics_Show("Pets");assert(not EsteriaIncarnationsFrame:IsShown())
        EsteriaIncarnations_Toggle();assert(EsteriaIncarnationsFrame:IsShown())
        EsteriaIncarnationsFrame.cosmeticsClose.scripts.OnClick();assert(not EsteriaCosmeticsFrame:IsShown())
    ''')
    # Execute the production transmog embedding/close entry points without its renderer.
    transmog=(HERE.parent/'Manastorm_Transmog/Manastorm_Transmog.lua').read_text()
    entrypoints=transmog.split('function ManastormTransmog_Show()',1)[1].split('-- character panel:',1)[0]
    lua.execute('''local win; local pending=false
        local function BuildWindow()
            win=CreateFrame("Frame","ManastormTransmogFrame",UIParent);win:SetSize(1400,800);win:Hide()
            win.cosmeticsClose=CreateFrame("Button",nil,win)
            local hide=win.Hide;win.Hide=function(self)if not pending then hide(self) end end
        end
        function testPending(value)pending=value end
        function ManastormTransmog_Show()'''+entrypoints)
    lua.execute('''
        ManastormTransmog_Show()
        assert(ManastormTransmogFrame:IsShown() and ManastormTransmogFrame:GetParent():GetParent()==EsteriaCosmeticsFrame)
        testPending(true);EsteriaCosmetics_Show("Pets")
        assert(ManastormTransmogFrame:IsShown(),"Tab changes must respect transmog's unsaved-work guard")
        EsteriaCosmeticsFrame:Hide();assert(EsteriaCosmeticsFrame:IsShown(),"Closing Collections must respect unsaved transmog work")
        testPending(false);EsteriaCosmetics_Show("Pets");assert(not ManastormTransmogFrame:IsShown())
        ManastormTransmog_Toggle();assert(ManastormTransmogFrame:IsShown())
        EsteriaCosmeticsFrame:Hide();assert(not EsteriaCosmeticsFrame:IsShown())
    ''')
    print('Cosmetics Lua: previews, pets, mount filters, shared favorites, random macro dragging, Incarnations, transmog close guards and fitter regressions passed')


if __name__ == '__main__':
    main()
