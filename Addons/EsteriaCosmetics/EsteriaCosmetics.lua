-- Cosmetics shares the collection layout with Incarnations and embeds the existing Emotes window intact.
local ES = EsteriaStyle
local PREFIX = "ESTCOS"
local W, H, CONTENT_H = 1180, 722, 680
local frame, content, pane, grid, stage, search, totalText, countText, pageText, apply, remove, favorite
local tabs, cards, rail = {}, {}, {}
local tab, filter, page, focus = "Backpacks", "all", 1, nil
local known, selected, syncing, db = {}, 0, nil, nil
local filtered = {}
local refresh, selectTab
local filters = {{"all", "All appearances"}, {"known", "Collected"}, {"favorites", "Favorites"}, {"locked", "Not collected"}}

local function send(body) SendAddonMessage(PREFIX, body, "WHISPER", UnitName("player")) end
local function shown(region, value) if value then region:Show() else region:Hide() end end
local function list() return tab == "Wings" and (EsteriaCosmeticsWings or {}) or (EsteriaCosmeticsData or {}) end
local function id(row) return row and (row.spell or row.id) end
local function favoriteKey(row) return (row.spell and "wing:" or "backpack:") .. id(row) end
local function spellbook()
    local result = {}
    for t = 1, GetNumSpellTabs() do
        local _, _, offset, count = GetSpellTabInfo(t)
        for slot = offset + 1, offset + count do
            local link = GetSpellLink(slot, BOOKTYPE_SPELL or "spell")
            local value = link and tonumber(link:match("spell:(%d+)"))
            if value then result[value] = true end
        end
    end
    return result
end
local learnedSpells = {}
local function owned(row) return row and (row.spell and learnedSpells[row.spell] or not row.spell and known[row.id]) end
local function wingActive(row)
    if not row or not row.spell then return false end
    for i = 1, 40 do
        local name, _, _, _, _, _, _, _, _, _, spell = UnitBuff("player", i)
        if not name then break end
        if spell == row.spell then return true end
    end
    return false
end
local function button(parent, width, text, secure)
    local b = CreateFrame("Button", nil, parent, secure and "SecureActionButtonTemplate" or nil)
    b:SetSize(width, 28)
    b.bg = ES.Solid(b, "BACKGROUND", ES.INK, 0.95); b.bg:SetAllPoints()
    b.lines = ES.Border(b, ES.GOLD_DIM, 0.9)
    b.label = ES.Text(b, 12, ES.TEXT); b.label:SetPoint("CENTER"); b.label:SetText(text)
    b:SetScript("OnEnter", function(self) if self:IsEnabled() == 1 then ES.Tint(self.lines, ES.GOLD) end end)
    b:SetScript("OnLeave", function(self) ES.Tint(self.lines, ES.GOLD_DIM, 0.9) end)
    b.Active = function(self, on)
        if on then self:Enable() else self:Disable() end
        self.label:SetAlpha(on and 1 or 0.35)
    end
    return b
end

local function preview(parent)
    local model = CreateFrame("PlayerModel", nil, parent)
    model.facing = 3.14
    model:SetScript("OnUpdate", function(self, elapsed)
        if self.waiting then
            local ready
            if self.playerPreview then
                if self.row.spell then ready = EsteriaCosmeticsWingPreview(self, self.row.spell)
                else ready = EsteriaCosmeticsPreview(self, self.row.display) end
                if ready and EsteriaCosmeticsFit then ready = EsteriaCosmeticsFit(self,self.facing,0.9) end
            else
                if EsteriaCosmeticsFit then ready = EsteriaCosmeticsFit(self,self.facing,0.9)
                elseif RetailModelFit then ready = RetailModelFit(self,0.65)
                elseif RetailModelReportLoaded then ready = RetailModelReportLoaded(self) end
            end
            if ready then self.waiting = nil; self:SetFacing(self.facing); self:SetAlpha(1) end
        end
        if self.dragX then
            local x = GetCursorPosition()
            self.facing = self.facing + (x - self.dragX) * 0.012
            self.dragX = x; self:SetFacing(self.facing)
            if EsteriaCosmeticsFit then EsteriaCosmeticsFit(self,self.facing,0.9) end
        elseif self.spin and not self.waiting then
            self.facing = self.facing + elapsed * self.spin; self:SetFacing(self.facing)
            if EsteriaCosmeticsFit then EsteriaCosmeticsFit(self,self.facing,0.9) end
        end
    end)
    model.ShowRow = function(self, row, player)
        if self.row == row and self.playerPreview == player then return end
        self.row, self.playerPreview, self.waiting = row, player, nil
        self:SetAlpha(0)
        self:ClearModel()
        if not row then self:Hide(); return end
        if player then self:SetUnit("player")
        elseif row.model then self:SetModel(row.model)
        else self:Hide(); return end
        self.waiting = true; self:Show()
    end
    return model
end

local function choose(row)
    if InCombatLockdown() then UIErrorsFrame:AddMessage("Leave combat to change cosmetic selection.",1,0.1,0.1); return end
    focus = row; refresh()
end

function EsteriaCosmetics_RefitPreview()
    if stage and stage.model and stage.model.row and stage.model.playerPreview then stage.model.waiting=true end
end
local function buildPane()
    pane = CreateFrame("Frame", nil, content); pane:SetAllPoints()
    ES.Solid(pane, "BACKGROUND", ES.INK, 0.97):SetAllPoints()
    local title = ES.Text(pane, 22, ES.GOLD); title:SetPoint("TOPLEFT", 20, -12)
    pane.title = title
    totalText = ES.Text(pane, 12, ES.TEXT); totalText:SetPoint("TOPRIGHT", -20, -20)
    local sidebar = CreateFrame("Frame", nil, pane)
    sidebar:SetPoint("TOPLEFT", 14, -64); sidebar:SetPoint("BOTTOMLEFT", 14, 16); sidebar:SetWidth(236)
    ES.Solid(sidebar, "BACKGROUND", ES.INK, 1):SetAllPoints(); ES.Border(sidebar, ES.GOLD_DIM)
    for i, option in ipairs(filters) do
        local row = button(sidebar, 224, option[2]); row:SetPoint("TOPLEFT", 6, -16 - (i - 1) * 38)
        row:SetScript("OnClick", function() filter = option[1]; page = 1; refresh() end)
        rail[i] = row
    end
    local note = ES.Text(sidebar, 11, ES.MUTED)
    note:SetPoint("BOTTOMLEFT", 14, 14); note:SetPoint("RIGHT", -14, 0); note:SetJustifyH("LEFT")
    pane.note = note
    grid = CreateFrame("Frame", nil, pane); grid:SetPoint("TOPLEFT", 268, -104); grid:SetSize(534, 508)
    for i = 1, 6 do
        local card = CreateFrame("Button", nil, grid)
        card:SetSize(170, 248); card:SetPoint("TOPLEFT", ((i - 1) % 3) * 182, -math.floor((i - 1) / 3) * 260)
        ES.Solid(card, "BACKGROUND", {0.05,0.05,0.07}, 1):SetAllPoints()
        card.lines = ES.Border(card, ES.GOLD_DIM, 0.7)
        card.model = preview(card); card.model:SetPoint("TOPLEFT", 2, -2); card.model:SetPoint("BOTTOMRIGHT", -2, 46)
        card.icon = card:CreateTexture(nil, "ARTWORK"); card.icon:SetSize(70,70); card.icon:SetPoint("CENTER", 0, 22)
        local plate = ES.Solid(card, "ARTWORK", ES.INK, 0.96)
        plate:SetPoint("BOTTOMLEFT", 1, 1); plate:SetPoint("BOTTOMRIGHT", -1, 1); plate:SetHeight(45)
        card.name = ES.Text(card, 11, ES.TEXT); card.name:SetPoint("BOTTOMLEFT", 8, 24); card.name:SetPoint("RIGHT", -8, 0)
        card.name:SetJustifyH("LEFT")
        card.state = ES.Text(card, 10, ES.MUTED); card.state:SetPoint("BOTTOMLEFT", 8, 8)
        card:SetScript("OnClick", function(self) choose(self.row) end)
        card:SetScript("OnEnter", function(self)
            if not self.row then return end
            GameTooltip:SetOwner(self, "ANCHOR_RIGHT"); GameTooltip:SetText(self.row.name, 1, 0.82, 0.1)
            GameTooltip:AddLine(owned(self.row) and "Collected" or "Not collected", 1,1,1)
            if self.row.item then GameTooltip:AddLine("Learned from item " .. self.row.item, 0.6,0.59,0.55) end
            GameTooltip:Show(); self.model.spin = 0.25
        end)
        card:SetScript("OnLeave", function(self) GameTooltip:Hide(); self.model.spin = nil end)
        card:EnableMouseWheel(true)
        card:SetScript("OnMouseWheel", function(_, delta) page = page - delta; refresh() end)
        cards[i] = card
    end
    search = CreateFrame("EditBox", "EsteriaCosmeticsSearch", pane, "InputBoxTemplate")
    search:SetSize(220,20); search:SetPoint("BOTTOMLEFT", grid, "TOPLEFT", 6,12); search:SetAutoFocus(false)
    search:SetScript("OnEscapePressed", function(self) self:ClearFocus() end)
    search:SetScript("OnTextChanged", function() page=1; refresh() end)
    local searchLabel = ES.Text(pane, 11, ES.MUTED); searchLabel:SetPoint("BOTTOMLEFT", search, "TOPLEFT", -2, 4); searchLabel:SetText("Search appearances")
    countText=ES.Text(pane,11,ES.MUTED); countText:SetPoint("BOTTOMRIGHT", grid, "TOPRIGHT", 0,16)
    local prev=button(pane,36,"<"); prev:SetPoint("TOPLEFT",grid,"BOTTOMLEFT",0,-14)
    prev:SetScript("OnClick", function() page=page-1; refresh() end)
    local next=button(pane,36,">"); next:SetPoint("TOPRIGHT",grid,"BOTTOMRIGHT",0,-14)
    next:SetScript("OnClick", function() page=page+1; refresh() end)
    pageText=ES.Text(pane,12,ES.TEXT); pageText:SetPoint("TOP",grid,"BOTTOM",0,-20)
    pane.prev, pane.next = prev,next
    stage=CreateFrame("Frame",nil,pane); stage:SetPoint("TOPRIGHT",-14,-64); stage:SetPoint("BOTTOMRIGHT",-14,16); stage:SetWidth(346)
    ES.Solid(stage,"BACKGROUND",{0.05,0.05,0.07},1):SetAllPoints(); ES.Border(stage,ES.GOLD,0.8)
    stage.model=preview(stage); stage.model:SetPoint("TOPLEFT",4,-4); stage.model:SetPoint("BOTTOMRIGHT",-4,170)
    stage.model:EnableMouse(true)
    stage.model:SetScript("OnMouseDown",function(self) self.dragX=GetCursorPosition() end)
    stage.model:SetScript("OnMouseUp",function(self) self.dragX=nil end)
    stage.icon=stage:CreateTexture(nil,"ARTWORK"); stage.icon:SetSize(100,100); stage.icon:SetPoint("CENTER",stage.model,"CENTER")
    stage.name=ES.Text(stage,17,ES.GOLD); stage.name:SetPoint("BOTTOMLEFT",16,136); stage.name:SetPoint("RIGHT",-16,0); stage.name:SetJustifyH("LEFT")
    stage.state=ES.Text(stage,11,ES.MUTED); stage.state:SetPoint("TOPLEFT",stage.name,"BOTTOMLEFT",0,-8)
    apply=button(stage,96,"Apply",true); apply:SetPoint("BOTTOMRIGHT",-14,14)
    apply:SetScript("PostClick",function() if tab=="Backpacks" and focus and owned(focus) and not InCombatLockdown() then send("apply\t"..focus.id) end end)
    remove=button(stage,96,"Remove",true); remove:SetPoint("RIGHT",apply,"LEFT",-8,0)
    remove:SetScript("PostClick",function() if tab=="Backpacks" and not InCombatLockdown() then send("apply\t0") end end)
    favorite=button(stage,110,"Favorite"); favorite:SetPoint("BOTTOMLEFT",14,52)
    favorite:SetScript("OnClick",function() if focus and db then local key=favoriteKey(focus); db.favorites[key]=not db.favorites[key] or nil; refresh() end end)
end

refresh=function()
    if not frame or not frame:IsShown() or tab=="Emotes" then return end
    filtered={}
    local query=search:GetText():lower()
    local collected=0
    for _,row in ipairs(list()) do
        local has=owned(row)
        if has then collected=collected+1 end
        local keep=filter=="all" or filter=="known" and has or filter=="locked" and not has or filter=="favorites" and db and db.favorites[favoriteKey(row)]
        if keep and (query=="" or row.name:lower():find(query,1,true)) then filtered[#filtered+1]=row end
    end
    table.sort(filtered,function(a,b) local ka,kb=owned(a) and 1 or 0,owned(b) and 1 or 0; if ka~=kb then return ka>kb end; return a.name<b.name end)
    local pages=math.max(1,math.ceil(#filtered/6)); page=math.max(1,math.min(page,pages))
    if not focus then focus=filtered[1] end
    pane.title:SetText(tab)
    pane.note:SetText(tab=="Wings" and "Wings you know from your spellbook appear here. Apply a wing to wear it." or "Use a backpack's appearance item to collect it, then apply it to your cloak.")
    totalText:SetText(("%d of %d collected"):format(collected,#list()))
    countText:SetText(("%d shown"):format(#filtered)); pageText:SetText(("Page %d / %d"):format(page,pages))
    pane.prev:Active(page>1); pane.next:Active(page<pages)
    for i,row in ipairs(rail) do ES.Tint(row.lines,filters[i][1]==filter and ES.GOLD or ES.GOLD_DIM) end
    for i,card in ipairs(cards) do
        local row=filtered[(page-1)*6+i]; card.row=row
        if row then
            card.name:SetText(row.name)
            card.state:SetText(owned(row) and (row.spell and wingActive(row) or not row.spell and selected==row.id) and "Applied" or owned(row) and "Collected" or "Not collected")
            card.model:ShowRow(row,false)
            card.icon:SetTexture(row.icon or "Interface\\Icons\\INV_Misc_Bag_10")
            shown(card.icon,not row.model)
            ES.Tint(card.lines,row==focus and ES.GOLD or ES.GOLD_DIM,row==focus and 1 or 0.7)
            card:Show()
        else card.model:ShowRow(nil,false); card:Hide() end
    end
    local playerPreview=focus and (focus.spell and type(EsteriaCosmeticsWingPreview)=="function"
        or not focus.spell and type(EsteriaCosmeticsPreview)=="function") or false
    stage.model:ShowRow(focus,playerPreview)
    stage.icon:SetTexture(focus and focus.icon or nil); shown(stage.icon,focus and not focus.model and not playerPreview)
    stage.name:SetText(focus and focus.name or "No appearances")
    stage.state:SetText(not focus and "" or not owned(focus) and "Not collected" or tab=="Wings" and wingActive(focus) and "Applied" or tab=="Backpacks" and selected==focus.id and "Applied to cloak" or "Collected")
    favorite:Active(focus~=nil); favorite.label:SetText(focus and db and db.favorites[favoriteKey(focus)] and "Unfavorite" or "Favorite")
    -- Secure action attributes cannot change in combat; the server also validates ownership and combat state.
    if not InCombatLockdown() then
        apply:SetAttribute("type",nil); remove:SetAttribute("type",nil)
        apply:SetAttribute("spell",nil); remove:SetAttribute("macrotext",nil)
        if tab=="Wings" and focus and owned(focus) then
            local name=GetSpellInfo(focus.spell)
            if name then
                apply:SetAttribute("type","macro"); apply:SetAttribute("macrotext","/stopmacro [combat]\n/cast [@player] "..name)
                remove:SetAttribute("type","macro"); remove:SetAttribute("macrotext","/stopmacro [combat]\n/cancelaura "..name)
            end
        end
        apply:Active(focus and owned(focus)); remove:Active(tab=="Wings" and focus and wingActive(focus) or tab=="Backpacks" and selected~=0)
    end
end

local function build()
    frame=CreateFrame("Frame","EsteriaCosmeticsFrame",UIParent); frame:SetSize(W,H); frame:SetPoint("CENTER")
    frame:SetFrameStrata("HIGH"); frame:SetToplevel(true); frame:EnableMouse(true); frame:SetMovable(true); frame:SetClampedToScreen(true)
    frame:RegisterForDrag("LeftButton"); frame:SetScript("OnDragStart",frame.StartMoving); frame:SetScript("OnDragStop",frame.StopMovingOrSizing)
    frame:Hide(); tinsert(UISpecialFrames,"EsteriaCosmeticsFrame")
    ES.Solid(frame,"BACKGROUND",ES.INK,0.98):SetAllPoints(); ES.Border(frame,ES.GOLD)
    local title=ES.Text(frame,18,ES.GOLD); title:SetPoint("TOPLEFT",20,-11); title:SetText("Cosmetics")
    for i,name in ipairs({"Wings","Backpacks","Emotes"}) do
        local b=button(frame,110,name); b:SetPoint("TOPLEFT",164+(i-1)*118,-7); b:SetScript("OnClick",function() selectTab(name) end); tabs[name]=b
    end
    local close=CreateFrame("Button",nil,frame,"UIPanelCloseButton"); close:SetPoint("TOPRIGHT",2,2)
    content=CreateFrame("Frame",nil,frame); content:SetSize(W,CONTENT_H); content:SetPoint("BOTTOMLEFT")
    buildPane()
    frame:SetScript("OnShow",function() learnedSpells=spellbook(); send("sync"); selectTab(tab) end)
    frame:SetScript("OnHide",function() if EsteriaEmotesFrame then EsteriaEmotesFrame:Hide() end; GameTooltip:Hide() end)
end

selectTab=function(name)
    if InCombatLockdown() then UIErrorsFrame:AddMessage("Leave combat to change cosmetic selection.",1,0.1,0.1); return end
    tab=name; page=1; focus=nil
    for label,b in pairs(tabs) do ES.Tint(b.lines,label==tab and ES.GOLD or ES.GOLD_DIM) end
    if EsteriaEmotesFrame then EsteriaEmotesFrame:Hide() end
    shown(pane,tab~="Emotes")
    if tab=="Emotes" then
        if EsteriaEmotes_Embed then EsteriaEmotes_Embed(content) end
    else search:SetText(""); refresh() end
end

function EsteriaCosmetics_Toggle(requested)
    if InCombatLockdown() then UIErrorsFrame:AddMessage("Leave combat to open Cosmetics.",1,0.1,0.1); return end
    if not frame then build() end
    if requested then
        if frame:IsShown() and tab==requested then frame:Hide(); return end
        tab=requested
        if not frame:IsShown() then frame:Show() else selectTab(requested) end
    elseif frame:IsShown() then frame:Hide() else frame:Show() end
end
SLASH_ESTERIACOSMETICS1="/cosmetics"
SlashCmdList.ESTERIACOSMETICS=function() EsteriaCosmetics_Toggle() end

-- DragonUI supplies its own strip button. Keep the same entry point on the stock micro bar.
if not _G.CosmeticsMicroButton and MainMenuBarArtFrame then
    local anchor=_G.IncarnationMicroButton or _G.NeighborhoodMicroButton or HelpMicroButton
    local b=CreateFrame("Button","CosmeticsMicroButton",MainMenuBarArtFrame,"MainMenuBarMicroButton")
    b:SetPoint("BOTTOMLEFT",anchor,"BOTTOMRIGHT",-3,0)
    b:SetNormalTexture("Interface\\Buttons\\UI-MicroButton-Spellbook-Up")
    b:SetPushedTexture("Interface\\Buttons\\UI-MicroButton-Spellbook-Down")
    b:SetDisabledTexture("Interface\\Buttons\\UI-MicroButton-Spellbook-Disabled")
    b:SetHighlightTexture("Interface\\Buttons\\UI-MicroButton-Hilight")
    b:SetScript("OnClick",function() EsteriaCosmetics_Toggle() end)
    b:SetScript("OnEnter",function(self)
        GameTooltip:SetOwner(self,"ANCHOR_LEFT"); GameTooltip:SetText("Cosmetics",1,0.82,0.1)
        GameTooltip:AddLine("Your wings, backpacks and emotes.",1,1,1,true); GameTooltip:Show()
    end)
    b:SetScript("OnLeave",function() GameTooltip:Hide() end)
    hooksecurefunc("VehicleMenuBar_MoveMicroButtons",function() b:SetParent(anchor:GetParent()) end)
end

local events=CreateFrame("Frame")
for _,event in ipairs({"PLAYER_LOGIN","CHAT_MSG_ADDON","SPELLS_CHANGED","UNIT_AURA","PLAYER_REGEN_ENABLED","PLAYER_EQUIPMENT_CHANGED"}) do events:RegisterEvent(event) end
events:SetScript("OnEvent",function(_,event,prefix,message,_,sender)
    if event=="PLAYER_LOGIN" then
        EsteriaCosmeticsDB=EsteriaCosmeticsDB or {}; db=EsteriaCosmeticsDB; db.favorites=db.favorites or {}
        learnedSpells=spellbook(); send("sync"); return
    elseif event=="SPELLS_CHANGED" then learnedSpells=spellbook(); refresh(); return
    elseif event=="UNIT_AURA" then if prefix=="player" then refresh() end; return
    elseif event=="PLAYER_REGEN_ENABLED" then refresh(); return end
    if event=="PLAYER_EQUIPMENT_CHANGED" then
        if frame and frame:IsShown() and tab=="Backpacks" then
            stage.model.row=nil; refresh(); send("sync")
        end
        return
    end
    if prefix~=PREFIX or sender~=UnitName("player") then return end
    local kind,a=strsplit("\t",message)
    if kind=="reset" then syncing={known={},selected=selected}
    elseif kind=="known" and syncing then for value in (a or ""):gmatch("%d+") do syncing.known[tonumber(value)]=true end
    elseif kind=="selected" then if syncing then syncing.selected=tonumber(a) or 0 else selected=tonumber(a) or 0 end
    elseif kind=="done" and syncing then known,selected=syncing.known,syncing.selected; syncing=nil
    elseif kind=="err" then UIErrorsFrame:AddMessage(a or "That cosmetic is unavailable.",1,0.1,0.1) end
    refresh()
end)
