-- Upright undead toggle (wowdecomp)
-- Undead only (the native side decides), on the Appearance page: switches between the hunched and the upright body
-- (native EA_UPRIGHT). The option rows follow by themselves: the upright body uses the five stock selectors, and
-- EA_REFRESH re-lays the rows whenever EA_RETAIL changes. Hidden when the client has no upright package (EA_UPRIGHT
-- answers nil).
(function()
local UPRIGHT_LABEL, HUNCHED_LABEL = "Posture: Upright", "Posture: Hunched";
local button, lastReason;

local function Note(reason)
    -- Logged once per change (Logs\BodyShape.log), so a missing button can be traced to its cause.
    if reason ~= lastReason then
        lastReason = reason;
        if BodyShapeLog then BodyShapeLog("upright: " .. reason); end
    end
end

local function State()
    -- No race test here: CharacterCreate.selectedRaceID is a creator list id, not the ChrRaces id. EA_UPRIGHT answers
    -- nil for anything but undead.
    if not CharacterCreate.personalizationMode or not CharCreateOkayButton:IsShown() then return Note("not on Appearance"); end
    if (BodyShapePage_Active and BodyShapePage_Active()) or (FatePage_Open and FatePage_Open())
        or (StartZonePage_Open and StartZonePage_Open()) then
        return Note("another page open");
    end
    local state = CycleCharCustomization("EA_UPRIGHT");
    Note("state " .. tostring(state));
    return state;
end

-- Built on first use: this chunk can run before the creator's XML frames exist. Layered and placed with the
-- Male / Female buttons so it draws above the model view.
local function Build()
    local male = CharacterCreateGenderButtonMale;
    if not male then return nil; end
    button = CreateFrame("Button", "CharacterCreateUprightButton", male:GetParent(), "GlueButtonTemplate");
    button:SetSize(170, 32);
    button:SetFrameStrata(male:GetFrameStrata());
    button:SetFrameLevel(male:GetFrameLevel() + 2);
    button:SetPoint("BOTTOM", male, "TOP", 60, 8); -- Male sits 60px left of centre on the Appearance page
    button:SetScript("OnClick", function()
        local state = State();
        if state == nil then return; end
        PlaySound("gsCharacterCreationLook");
        CharacterCustomization_CloseChoices();
        CycleCharCustomization("EA_UPRIGHT", state == 1 and 0 or 1);
        CharacterCreate_UpdateHairCustomization();
    end);
    button:Hide();
    return button;
end

local driver = CreateFrame("Frame");
driver:SetScript("OnUpdate", function(self, elapsed)
    self.elapsed = (self.elapsed or 0) + elapsed;
    if self.elapsed < 0.2 then return; end
    self.elapsed = 0;
    local state = State();
    if not button and (state == nil or not Build()) then return; end
    if state == nil then button:Hide(); return; end
    button:SetText(state == 1 and UPRIGHT_LABEL or HUNCHED_LABEL);
    button:Show();
    local shown = "shown " .. tostring(button:IsVisible()) .. " at " .. tostring(button:GetLeft()) .. "," .. tostring(button:GetBottom());
    if shown ~= self.shown then self.shown = shown; if BodyShapeLog then BodyShapeLog("upright: " .. shown); end end
end);
end)();
