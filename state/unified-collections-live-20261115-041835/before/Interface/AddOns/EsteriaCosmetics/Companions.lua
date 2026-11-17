-- Keep DragonUI's companion services and existing action-bar macros after retiring its journal.
local companions={MOUNT={},CRITTER={}}
local function service() return DragonUI and DragonUI.Collections end
local function companionKind(row) return row.kind=="pet" and "CRITTER" or "MOUNT" end

function EsteriaCollections_RefreshCompanions()
    companions={MOUNT={},CRITTER={}}
    for kind,entries in pairs(companions) do
        for i=1,(GetNumCompanions and GetNumCompanions(kind) or 0) do
            local creature,name,spell,icon,active=GetCompanionInfo(kind,i)
            if spell then entries[spell]={index=i,creatureID=creature,name=name,spellID=spell,icon=icon,active=active} end
        end
    end
end

function EsteriaCollections_Companion(row)
    return row and companions[companionKind(row)][row.spell]
end

function EsteriaCollections_Pets()
    local rows,seen={},{}
    for _,row in ipairs(EsteriaCollectionsPets or {}) do rows[#rows+1]=row;seen[row.spell]=true end
    for spell,entry in pairs(companions.CRITTER) do
        if not seen[spell] then
            rows[#rows+1]={kind="pet",spell=spell,name=entry.name or GetSpellInfo(spell) or "Companion",
                icon=entry.icon,creature=entry.creatureID}
        end
    end
    return rows
end

function EsteriaCollections_IsFavorite(row,favorites)
    local entry,co=EsteriaCollections_Companion(row),service()
    if entry and co then return co.IsFavorite(companionKind(row),entry.creatureID) end
    return favorites[(row.kind or "mount")..":"..row.spell]
end

function EsteriaCollections_SetFavorite(row,on,favorites)
    favorites[row.kind..":"..row.spell]=on or nil
    local entry,co=EsteriaCollections_Companion(row),service()
    if entry and co and co.IsFavorite(companionKind(row),entry.creatureID)~=on then
        co.ToggleFavorite(companionKind(row),entry.creatureID)
    end
end

function EsteriaCollections_MigrateFavorites(favorites)
    for _,row in ipairs(EsteriaCollectionsMounts or {}) do
        if favorites["mount:"..row.spell] then EsteriaCollections_SetFavorite(row,true,favorites) end
    end
end

function EsteriaCollections_MountType(row,value)
    if value=="All types" then return true end
    local co=service()
    if co and not (row.model and row.model:lower():find("hots",1,true)) then
        local ground,flying,aquatic=co.MountCategory(row.spell)
        if value=="Ground" then return ground end
        if value=="Flying" then return flying end
        if value=="Aquatic" then return aquatic end
    end
    return row.mountType==value
end

function EsteriaCollections_Random(kind,drag)
    if InCombatLockdown() then return end
    local co=service()
    if not co then UIErrorsFrame:AddMessage("DragonUI's companion service is unavailable.",1,0.1,0.1);return end
    if drag then
        local macro=co.EnsureRandomMacro(kind)
        if macro then PickupMacro(macro) end
    else co.SummonRandomFavorite(kind) end
end

local function retireJournal()
    local co=service()
    if not co then return end
    local function open(kind) EsteriaCosmetics_Show(kind=="CRITTER" and "Pets" or "Mounts") end
    co.Open=open
    co.Toggle=function(kind) EsteriaCosmetics_Toggle(kind=="CRITTER" and "Pets" or "Mounts") end
    DragonUI.ToggleCollections=co.Toggle
    local old=_G.DragonUICollectionsFrame
    if old then old:SetScript("OnShow",old.Hide);old:Hide() end
    local b=_G.CollectionsMicroButton
    if b then b.dragonUISuppressed=true;b:SetScript("OnShow",b.Hide);b:Hide() end
    if DragonUI.RefreshMicromenu then DragonUI.RefreshMicromenu() end
end

local events=CreateFrame("Frame")
events:RegisterEvent("PLAYER_LOGIN")
events:RegisterEvent("ADDON_LOADED")
events:SetScript("OnEvent",function(_,event,name)
    if event=="PLAYER_LOGIN" or name=="DragonUI" then
        retireJournal()
        if DragonUI and DragonUI.ApplyCollectionsSystem and not events.hooked then
            events.hooked=true;hooksecurefunc(DragonUI,"ApplyCollectionsSystem",retireJournal)
        end
    end
end)
