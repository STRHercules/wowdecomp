local EsteriaPickaxesData=EsteriaPickaxesData or {{display=3430001,name="Rainbow Smash"}}
-- Local authoring profiles: offsets are relative to the automatic cloak fit, scale is a multiplier.
local ES=EsteriaStyle
local frame,title,status,bodyLabel
local controls={}
local current,body,loading
local mode="drawn"
local selectedType="All"
local typeButton
local stateTabs={}
local pending,pendingAt
local fields={{"scale","Scale multiplier",0.1,4,0.01,1},
    {"x","Depth: forward / back",-3,3,0.005,0},
    {"y","Side: left / right",-3,3,0.005,0},{"z","Height: up / down",-3,3,0.005,0},
    {"pitch","Rotation X (degrees)",-180,180,0.5,0},{"yaw","Rotation Y (degrees)",-180,180,0.5,0},{"roll","Rotation Z (degrees)",-180,180,0.5,0}}
local function defaults()
    if EsteriaPickaxeDefaultFit then
        local scale,x,y,z,pitch,yaw,roll=EsteriaPickaxeDefaultFit(current,mode=="sheathed")
        return {scale=scale,x=x,y=y,z=z,pitch=pitch,yaw=yaw,roll=roll}
    end
    for _,r in ipairs(EsteriaPickaxesData or {}) do
        if r.display==current then
            local fit=r[mode]
            if fit then return {scale=fit[1],x=fit[2],y=fit[3],z=fit[4],pitch=fit[5],yaw=fit[6],roll=fit[7]} end
        end
    end
    return {scale=1,x=0,y=0,z=0,pitch=0,yaw=0,roll=0}
end
local function key(display,model,state) return display..";"..model..";"..(state or mode) end
local function profiles()
    EsteriaCosmeticsDB=EsteriaCosmeticsDB or {}
    EsteriaCosmeticsDB.pickaxeFits=EsteriaCosmeticsDB.pickaxeFits or {}
    local migrated={}
    for k,p in pairs(EsteriaCosmeticsDB.pickaxeFits) do
        if not p.mode then p.mode="drawn";migrated[key(p.display,p.body,p.mode)]=p;EsteriaCosmeticsDB.pickaxeFits[k]=nil end
    end
    for k,p in pairs(migrated) do EsteriaCosmeticsDB.pickaxeFits[k]=p end
    return EsteriaCosmeticsDB.pickaxeFits
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
    if not current or not body or not EsteriaPickaxeSetFit then return false end
    local ok=EsteriaPickaxeSetFit(current,body,p.scale,p.x,p.y,p.z,p.pitch,p.yaw,p.roll,mode=="sheathed")
    if ok and EsteriaCosmetics_RefitPreview then EsteriaCosmetics_RefitPreview() end
    return ok
end
local function changed()
    if loading then return end
    local p=values()
    for _,f in ipairs(fields) do controls[f[1]].edit:SetText(("%.3f"):format(p[f[1]])) end
    if apply(p) then status:SetText("Live preview â€” unsaved changes")
    else status:SetText("Equip a cosmetic pickaxe to adjust its fit.") end
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
    for state,button in pairs(stateTabs) do button:SetButtonState(state==mode and "PUSHED" or "NORMAL",state==mode) end
    local preferred=pending or current
    current,body=nil,nil
    if EsteriaPickaxeFitInfo then current,body=EsteriaPickaxeFitInfo(preferred) end
    if not current and not pending and EsteriaPickaxeFitInfo then current,body=EsteriaPickaxeFitInfo() end
    if not current then
        title:SetText("No pickaxe equipped"); bodyLabel:SetText("")
        status:SetText("Equip a cosmetic pickaxe, then reopen this panel.")
        return
    end
    local name="Pickaxe "..current
    for _,r in ipairs(EsteriaPickaxesData or {}) do if r.display==current then name=r.name.." ("..(r.weaponType or "2H Axe")..")"; break end end
    title:SetText(name); bodyLabel:SetText(body)
    local saved=profiles()[key(current,body)]
    loadValues(valid(saved) and saved or defaults())
    apply(values()); status:SetText(saved and "Loaded saved local fit" or "Default fit â€” adjust the controls below")
end
local function makeButton(parent,label,x,fn)
    local b=CreateFrame("Button",nil,parent,"UIPanelButtonTemplate")
    b:SetSize(100,25); b:SetPoint("BOTTOMLEFT",x,18); b:SetText(label); b:SetScript("OnClick",fn)
    return b
end
local function saveCurrent()
    if not current then return false end
    local p=values();p.display=current;p.body=body;p.mode=mode
    if not apply(p) then return false end
    profiles()[key(current,body)]=p
    return true
end
local function cycle(direction)
    if pending then return end
    if InCombatLockdown() then status:SetText("Leave combat before cycling pickaxes.");return end
    local rows={}
    for _,row in ipairs(EsteriaPickaxesData) do
        if selectedType=="All" or row.weaponType==selectedType then rows[#rows+1]=row end
    end
    if #rows==0 then return end
    if current and not saveCurrent() then status:SetText("The current fit could not be saved.");return end
    local index=direction>0 and 0 or 1
    for i,row in ipairs(rows) do if row.display==current then index=i;break end end
    local row=rows[((index-1+direction)%#rows)+1]
    pending,pendingAt=row.display,GetTime()
    SendAddonMessage("TMOGC","A:"..(row.slot or 15)..":"..row.display,"WHISPER",UnitName("player"))
    status:SetText("Applying "..row.name.." - current fit saved")
end
local function export()
    local rows={"# display;body_model;scale_multiplier;x;y;z;pitch;yaw;roll;state"}
    local keys={}; for k,p in pairs(profiles()) do if valid(p) then keys[#keys+1]=k end end; table.sort(keys)
    for _,k in ipairs(keys) do local p=profiles()[k]; rows[#rows+1]=("%d;%s;%.6f;%.6f;%.6f;%.6f;%.6f;%.6f;%.6f;%s"):format(p.display,p.body,p.scale,p.x,p.y,p.z,p.pitch,p.yaw,p.roll,p.mode) end
    if not EsteriaPickaxeExportFits then status:SetText("Restart with the updated client to export files.");return end
    local ok,path=EsteriaPickaxeExportFits(table.concat(rows,"\n").."\n")
    if ok then
        status:SetText(("Exported %d saved fits to .txt"):format(#keys))
        DEFAULT_CHAT_FRAME:AddMessage("Pickaxe Fitter: "..path)
    else status:SetText("Export failed: "..tostring(path)) end
end
local function build()
    frame=CreateFrame("Frame","EsteriaPickaxeFitter",UIParent)
    frame:SetSize(500,660);frame:SetPoint("CENTER");frame:SetFrameStrata("DIALOG");frame:EnableMouse(true)
    frame:SetMovable(true);frame:SetClampedToScreen(true);frame:RegisterForDrag("LeftButton")
    frame:SetScript("OnDragStart",frame.StartMoving);frame:SetScript("OnDragStop",frame.StopMovingOrSizing)
    frame:Hide();tinsert(UISpecialFrames,"EsteriaPickaxeFitter")
    ES.Solid(frame,"BACKGROUND",ES.INK,0.98):SetAllPoints();ES.Border(frame,ES.GOLD)
    local heading=ES.Text(frame,20,ES.GOLD);heading:SetPoint("TOPLEFT",20,-14);heading:SetText("Pickaxe Fitter")
    local close=CreateFrame("Button",nil,frame,"UIPanelCloseButton");close:SetPoint("TOPRIGHT",2,2)
    title=ES.Text(frame,14,ES.TEXT);title:SetPoint("TOPLEFT",20,-50)
    title:SetWidth(235);title:SetJustifyH("LEFT")
    local previous=makeButton(frame,"Previous",0,function()cycle(-1)end)
    previous:ClearAllPoints();previous:SetPoint("TOPLEFT",264,-46)
    local next=makeButton(frame,"Next",0,function()cycle(1)end)
    next:ClearAllPoints();next:SetPoint("TOPLEFT",376,-46)
    bodyLabel=ES.Text(frame,10,ES.MUTED);bodyLabel:SetPoint("TOPLEFT",20,-72);bodyLabel:SetWidth(460);bodyLabel:SetJustifyH("LEFT")
    for i,state in ipairs({"drawn","sheathed"}) do
        local b=makeButton(frame,state=="drawn" and "Drawn" or "Sheathed",0,function()
            saveCurrent();mode=state;refresh()
            status:SetText(state=="drawn" and "Editing drawn grip - draw the weapon to preview" or "Editing sheathed mount - sheath the weapon to preview")
        end)
        b:ClearAllPoints();b:SetPoint("TOPLEFT",20+(i-1)*112,-94)
        stateTabs[state]=b
    end
    local types={"All"};local seen={}
    for _,row in ipairs(EsteriaPickaxesData) do
        local kind=row.weaponType or "2H Axe"
        if not seen[kind] then types[#types+1]=kind;seen[kind]=true end
    end
    table.sort(types,function(a,b)if a=="All" then return true elseif b=="All" then return false else return a<b end end)
    typeButton=makeButton(frame,"Type: All",0,function()
        local index=1;for i,kind in ipairs(types)do if kind==selectedType then index=i;break end end
        selectedType=types[index%#types+1];typeButton:SetText("Type: "..selectedType)
    end)
    typeButton:ClearAllPoints();typeButton:SetSize(224,25);typeButton:SetPoint("TOPLEFT",252,-94)
    for i,f in ipairs(fields) do
        local label=ES.Text(frame,12,ES.TEXT);label:SetPoint("TOPLEFT",20,-139-(i-1)*57);label:SetText(f[2])
        local s=CreateFrame("Slider","EsteriaPickaxeFit_"..f[1],frame,"OptionsSliderTemplate")
        s:SetSize(320,16);s:SetPoint("TOPLEFT",22,-160-(i-1)*57);s:SetMinMaxValues(f[3],f[4]);s:SetValueStep(f[5])
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
    status=ES.Text(frame,11,ES.MUTED);status:SetPoint("TOPLEFT",20,-553);status:SetWidth(460);status:SetJustifyH("LEFT")
    local note=ES.Text(frame,10,ES.MUTED);note:SetPoint("TOPLEFT",20,-575);note:SetWidth(460);note:SetJustifyH("LEFT")
    note:SetText("Previous/Next saves this fit and applies a weapon transmog (normal costs apply). Export includes all saved fits.")
    makeButton(frame,"Save",20,function()
        if saveCurrent() then status:SetText("Saved local fit") end
    end)
    makeButton(frame,"Revert saved",132,function() refresh() end)
    makeButton(frame,"Reset",244,function() loadValues(defaults());changed() end)
    makeButton(frame,"Export",356,export)
    frame:SetScript("OnShow",refresh)
    frame:SetScript("OnUpdate",function()
        if not pending then return end
        local display=EsteriaPickaxeFitInfo and EsteriaPickaxeFitInfo(pending)
        if display==pending then current=pending;pending=nil;refresh()
        elseif GetTime()-pendingAt>5 then pending=nil;status:SetText("Pickaxe did not change. Check your equipped weapon and collection.") end
    end)
end
function EsteriaPickaxeFitter_Toggle()
    if not frame then build() end
    if frame:IsShown() then frame:Hide() else frame:Show() end
end
SLASH_ESTERIAPICKAXEFIT1="/pickaxefit"
SlashCmdList.ESTERIAPICKAXEFIT=EsteriaPickaxeFitter_Toggle
local events=CreateFrame("Frame")
for _,event in ipairs({"PLAYER_LOGIN","PLAYER_ENTERING_WORLD","PLAYER_EQUIPMENT_CHANGED","CHAT_MSG_ADDON"}) do events:RegisterEvent(event) end
events:SetScript("OnEvent",function(_,event,prefix,message,_,sender)
    if event=="CHAT_MSG_ADDON" then
        if prefix=="TMOGS" and sender==UnitName("player") and pending then
            local slot,code=message:match("^R:(%d+):(%d+)$")
            code=tonumber(code)
            local expected=15;for _,row in ipairs(EsteriaPickaxesData)do if row.display==pending then expected=row.slot or 15;break end end
            if tonumber(slot)~=expected then return end
            if code and code~=1 then
                local errors={[4]="You have not collected that pickaxe.",[5]="Equip a weapon first.",
                    [6]="That appearance cannot be used on your equipped weapon.",[7]="Not enough gold.",
                    [8]="Not enough tokens.",[101]="You must be next to a transmogrifier."}
                pending=nil;status:SetText(errors[code] or "Pickaxe could not be applied.")
            end
        end
        return
    end
    if event=="PLAYER_LOGIN" and EsteriaPickaxeSetFit then
        for _,p in pairs(profiles()) do if valid(p) then EsteriaPickaxeSetFit(p.display,p.body,p.scale,p.x,p.y,p.z,p.pitch,p.yaw,p.roll,p.mode=="sheathed") end end
    end
    if frame and frame:IsShown() then refresh() end
end)
