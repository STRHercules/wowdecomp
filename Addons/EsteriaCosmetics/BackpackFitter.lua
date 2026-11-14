-- Local authoring profiles: offsets are relative to the automatic cloak fit, scale is a multiplier.
local ES=EsteriaStyle
local frame,title,status,bodyLabel
local controls={}
local current,body,loading
local pending,pendingAt
local fields={{"scale","Scale multiplier",0.1,4,0.01,1},
    {"x","Depth: forward / back",-3,3,0.005,0},
    {"y","Side: left / right",-3,3,0.005,0},{"z","Height: up / down",-3,3,0.005,0}}
local function defaults()
    for _,r in ipairs(EsteriaCosmeticsData or {}) do
        if r.display==current then return {scale=r.fitScale or 1,x=r.fitX or 0,y=r.fitY or 0,z=r.fitZ or 0} end
    end
    return {scale=1,x=0,y=0,z=0}
end
local function key(display,model) return display..";"..model end
local function profiles()
    EsteriaCosmeticsDB=EsteriaCosmeticsDB or {}
    EsteriaCosmeticsDB.backpackFits=EsteriaCosmeticsDB.backpackFits or {}
    return EsteriaCosmeticsDB.backpackFits
end
local function valid(p)
    if type(p)~="table" or type(p.display)~="number" or type(p.body)~="string" then return false end
    for _,f in ipairs(fields) do local v=p[f[1]]; if type(v)~="number" or v~=v or v<f[3] or v>f[4] then return false end end
    return true
end
local function values()
    local result={}
    for _,f in ipairs(fields) do result[f[1]]=controls[f[1]].slider:GetValue() end
    return result
end
local function apply(p)
    if not current or not body or not EsteriaBackpackSetFit then return false end
    local ok=EsteriaBackpackSetFit(current,body,p.scale,p.x,p.y,p.z)
    if ok and EsteriaCosmetics_RefitPreview then EsteriaCosmetics_RefitPreview() end
    return ok
end
local function changed()
    if loading then return end
    local p=values()
    for _,f in ipairs(fields) do controls[f[1]].edit:SetText(("%.3f"):format(p[f[1]])) end
    if apply(p) then status:SetText("Live preview — unsaved changes")
    else status:SetText("Equip a cosmetic backpack to adjust its fit.") end
end
local function loadValues(p)
    loading=true
    for _,f in ipairs(fields) do
        controls[f[1]].slider:SetValue(p[f[1]])
        controls[f[1]].edit:SetText(("%.3f"):format(p[f[1]]))
    end
    loading=nil
end
local function refresh()
    current,body=nil,nil
    if EsteriaBackpackFitInfo then current,body=EsteriaBackpackFitInfo() end
    if not current then
        title:SetText("No backpack equipped"); bodyLabel:SetText("")
        status:SetText("Equip a cosmetic backpack, then reopen this panel.")
        return
    end
    local name="Backpack "..current
    for _,r in ipairs(EsteriaCosmeticsData or {}) do if r.display==current then name=r.name; break end end
    title:SetText(name); bodyLabel:SetText(body)
    local saved=profiles()[key(current,body)]
    loadValues(valid(saved) and saved or defaults())
    apply(values()); status:SetText(saved and "Loaded saved local fit" or "Default fit — adjust the controls below")
end
local function makeButton(parent,label,x,fn)
    local b=CreateFrame("Button",nil,parent,"UIPanelButtonTemplate")
    b:SetSize(100,25); b:SetPoint("BOTTOMLEFT",x,18); b:SetText(label); b:SetScript("OnClick",fn)
    return b
end
local function saveCurrent()
    if not current then return false end
    local p=values();p.display=current;p.body=body
    if not apply(p) then return false end
    profiles()[key(current,body)]=p
    return true
end
local function cycle(direction)
    if pending then return end
    if InCombatLockdown() then status:SetText("Leave combat before cycling backpacks.");return end
    local rows=EsteriaCosmeticsData or {}
    if #rows==0 then return end
    saveCurrent()
    local index=direction>0 and 0 or 1
    for i,row in ipairs(rows) do if row.display==current then index=i;break end end
    local row=rows[((index-1+direction)%#rows)+1]
    pending,pendingAt=row.display,GetTime()
    SendAddonMessage("ESTCOS","apply\t"..row.id,"WHISPER",UnitName("player"))
    status:SetText("Applying "..row.name.." — current fit saved")
end
local function export()
    local rows={"# display;body_model;scale_multiplier;x;y;z"}
    local keys={}; for k,p in pairs(profiles()) do if valid(p) then keys[#keys+1]=k end end; table.sort(keys)
    for _,k in ipairs(keys) do local p=profiles()[k]; rows[#rows+1]=("%d;%s;%.6f;%.6f;%.6f;%.6f"):format(p.display,p.body,p.scale,p.x,p.y,p.z) end
    if not EsteriaBackpackExportFits then status:SetText("Restart with the updated client to export files.");return end
    local ok,path=EsteriaBackpackExportFits(table.concat(rows,"\n").."\n")
    if ok then
        status:SetText(("Exported %d saved fits to .txt"):format(#keys))
        DEFAULT_CHAT_FRAME:AddMessage("Backpack Fitter: "..path)
    else status:SetText("Export failed: "..tostring(path)) end
end
local function build()
    frame=CreateFrame("Frame","EsteriaBackpackFitter",UIParent)
    frame:SetSize(500,445);frame:SetPoint("CENTER");frame:SetFrameStrata("DIALOG");frame:EnableMouse(true)
    frame:SetMovable(true);frame:SetClampedToScreen(true);frame:RegisterForDrag("LeftButton")
    frame:SetScript("OnDragStart",frame.StartMoving);frame:SetScript("OnDragStop",frame.StopMovingOrSizing)
    frame:Hide();tinsert(UISpecialFrames,"EsteriaBackpackFitter")
    ES.Solid(frame,"BACKGROUND",ES.INK,0.98):SetAllPoints();ES.Border(frame,ES.GOLD)
    local heading=ES.Text(frame,20,ES.GOLD);heading:SetPoint("TOPLEFT",20,-14);heading:SetText("Backpack Fitter")
    local close=CreateFrame("Button",nil,frame,"UIPanelCloseButton");close:SetPoint("TOPRIGHT",2,2)
    title=ES.Text(frame,14,ES.TEXT);title:SetPoint("TOPLEFT",20,-50)
    title:SetWidth(235);title:SetJustifyH("LEFT")
    local previous=makeButton(frame,"Previous",0,function()cycle(-1)end)
    previous:ClearAllPoints();previous:SetPoint("TOPLEFT",264,-46)
    local next=makeButton(frame,"Next",0,function()cycle(1)end)
    next:ClearAllPoints();next:SetPoint("TOPLEFT",376,-46)
    bodyLabel=ES.Text(frame,10,ES.MUTED);bodyLabel:SetPoint("TOPLEFT",20,-72);bodyLabel:SetWidth(460);bodyLabel:SetJustifyH("LEFT")
    for i,f in ipairs(fields) do
        local label=ES.Text(frame,12,ES.TEXT);label:SetPoint("TOPLEFT",20,-99-(i-1)*57);label:SetText(f[2])
        local s=CreateFrame("Slider","EsteriaBackpackFit_"..f[1],frame,"OptionsSliderTemplate")
        s:SetSize(320,16);s:SetPoint("TOPLEFT",22,-120-(i-1)*57);s:SetMinMaxValues(f[3],f[4]);s:SetValueStep(f[5])
        _G[s:GetName().."Low"]:SetText(f[3]);_G[s:GetName().."High"]:SetText(f[4]);_G[s:GetName().."Text"]:SetText("")
        local e=CreateFrame("EditBox",nil,frame,"InputBoxTemplate");e:SetSize(95,22);e:SetAutoFocus(false);e:SetPoint("LEFT",s,"RIGHT",24,0)
        e:SetScript("OnEnterPressed",function(self)
            local v=tonumber(self:GetText())
            if v and v==v and v>=f[3] and v<=f[4] then s:SetValue(v) else self:SetText(("%.3f"):format(s:GetValue())) end
            self:ClearFocus()
        end)
        e:SetScript("OnEscapePressed",function(self)self:ClearFocus();self:SetText(("%.3f"):format(s:GetValue())) end)
        controls[f[1]]={slider=s,edit=e};s:SetScript("OnValueChanged",changed)
    end
    status=ES.Text(frame,11,ES.MUTED);status:SetPoint("TOPLEFT",20,-342);status:SetWidth(460);status:SetJustifyH("LEFT")
    local note=ES.Text(frame,10,ES.MUTED);note:SetPoint("TOPLEFT",20,-364);note:SetWidth(460);note:SetJustifyH("LEFT")
    note:SetText("Previous/Next saves the current fit. Export writes all saved fits to WTF/Esteria/BackpackFits.")
    makeButton(frame,"Save",20,function()
        if saveCurrent() then status:SetText("Saved local fit") end
    end)
    makeButton(frame,"Revert saved",132,function() refresh() end)
    makeButton(frame,"Reset",244,function() loadValues(defaults());changed() end)
    makeButton(frame,"Export",356,export)
    frame:SetScript("OnShow",refresh)
    frame:SetScript("OnUpdate",function()
        if not pending then return end
        local display=EsteriaBackpackFitInfo and EsteriaBackpackFitInfo()
        if display==pending then pending=nil;refresh()
        elseif GetTime()-pendingAt>5 then pending=nil;status:SetText("Backpack did not change. Check your equipped cloak and collection.") end
    end)
end
function EsteriaBackpackFitter_Toggle()
    if not frame then build() end
    if frame:IsShown() then frame:Hide() else frame:Show() end
end
SLASH_ESTERIABACKPACKFIT1="/backpackfit"
SlashCmdList.ESTERIABACKPACKFIT=EsteriaBackpackFitter_Toggle
local events=CreateFrame("Frame")
for _,event in ipairs({"PLAYER_LOGIN","PLAYER_ENTERING_WORLD","PLAYER_EQUIPMENT_CHANGED","CHAT_MSG_ADDON"}) do events:RegisterEvent(event) end
events:SetScript("OnEvent",function(_,event,prefix,message,_,sender)
    if event=="CHAT_MSG_ADDON" then
        if prefix=="ESTCOS" and sender==UnitName("player") and pending then
            local kind,text=strsplit("\t",message)
            if kind=="err" then pending=nil;status:SetText(text or "Backpack could not be applied.") end
        end
        return
    end
    if event=="PLAYER_LOGIN" and EsteriaBackpackSetFit then
        for _,p in pairs(profiles()) do if valid(p) then EsteriaBackpackSetFit(p.display,p.body,p.scale,p.x,p.y,p.z) end end
    end
    if frame and frame:IsShown() then refresh() end
end)
