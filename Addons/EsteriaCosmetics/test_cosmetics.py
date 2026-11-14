"""Focused Lua behavior check using UI mocks; does not claim native rendering acceptance."""
from pathlib import Path
from lupa import LuaRuntime

HERE = Path(__file__).resolve().parent


def main():
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute(r'''
unpack=table.unpack; tinsert=table.insert
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
EsteriaEmotesData={{900,"Dance","dance","dance",3}}
function dispatch(event,...)
 for _,f in ipairs(regions) do if f.events and f.events[event] then f.scripts.OnEvent(f,event,...) end end
end
''')
    lua.execute((HERE.parent / 'EsteriaEmotes/esteria_style.lua').read_text(encoding='utf-8'))
    lua.execute((HERE.parent / 'EsteriaEmotes/EsteriaEmotes.lua').read_text(encoding='utf-8'))
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
    lua.execute('''fitCalls={};fitDisplay=300;exportedFits=nil
        EsteriaCosmeticsData={{id=100,display=300,name="Shield",fitScale=1.1,fitX=0.02},{id=101,display=301,name="Pack"}}
        function GetTime() return 0 end
        function EsteriaBackpackFitInfo() return fitDisplay,"retail/test-male.m2" end
        function EsteriaBackpackExportFits(text) exportedFits=text;return true,"WTF/Esteria/BackpackFits/BackpackFits-test.txt" end
        function EsteriaBackpackSetFit(display,body,scale,x,y,z)
            fitCalls[#fitCalls+1]={display=display,body=body,scale=scale,x=x,y=y,z=z};return true
        end
    ''')
    lua.execute((HERE/'BackpackFitter.lua').read_text(encoding='utf-8'))
    lua.execute('''
        EsteriaBackpackFitter_Toggle()
        assert(EsteriaBackpackFitter:IsShown())
        assert(fitCalls[#fitCalls].scale==1.1 and fitCalls[#fitCalls].x==0.02,"Opening the fitter must preserve catalog defaults")
        EsteriaBackpackFit_scale:SetValue(1.25)
        EsteriaBackpackFit_x:SetValue(-0.1)
        assert(fitCalls[#fitCalls].scale==1.25 and fitCalls[#fitCalls].x==-0.1)
        for _,f in ipairs(regions) do if f.text=="Save" and f.scripts.OnClick then f.scripts.OnClick(f) end end
        local saved=EsteriaCosmeticsDB.backpackFits["300;retail/test-male.m2"]
        assert(saved.scale==1.25 and saved.x==-0.1)
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
        assert(exportedFits:find("301;retail/test-male.m2;0.800000;0.000000;0.000000;0.000000",1,true))
        assert(not EsteriaBackpackFitExport,"Export must use the native text file writer instead of a copy window")
        for _,f in ipairs(regions) do if f.text=="Next" and f.scripts.OnClick then f.scripts.OnClick(f) end end
        assert(sent[#sent][2]=="apply\t100","Cycling must wrap to the first backpack")
    ''')
    print('Cosmetics Lua: regressions, fitter save/reset/reload, cycling/wrap and export of all saved profiles passed')


if __name__ == '__main__':
    main()
