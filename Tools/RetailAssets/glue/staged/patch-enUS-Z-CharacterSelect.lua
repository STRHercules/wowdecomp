-- ============================================================================
-- Autora: Noa
-- ============================================================================
CHARACTER_SELECT_ROTATION_START_X = nil;
CHARACTER_SELECT_INITIAL_FACING = nil;

CHARACTER_ROTATION_CONSTANT = 0.6;

MAX_CHARACTERS_DISPLAYED = 8;
MAX_CHARACTERS_PER_REALM = 100;
CHARACTER_SELECT_ROW_HEIGHT = 67;


function CharacterSelect_OnLoad(self)
    REALM_SIRION = GetServerName();

    self:SetSequence(0);
    self:SetCamera(0);

    self.createIndex = 0;
    self.selectedIndex = 0;
    self.scrollOffset = 0;
    self.scrollMax = 0;
    self.scrollUpdating = false;
    self.skipSelectionUpdate = false;
    self.selectLast = 0;
    self.currentModel = nil;
    self:RegisterEvent("ADDON_LIST_UPDATE");
    self:RegisterEvent("CHARACTER_LIST_UPDATE");
    self:RegisterEvent("UPDATE_SELECTED_CHARACTER");
    self:RegisterEvent("SELECT_LAST_CHARACTER");
    self:RegisterEvent("SELECT_FIRST_CHARACTER");
    self:RegisterEvent("SUGGEST_REALM");
    self:RegisterEvent("FORCE_RENAME_CHARACTER");
    SetCharSelectModelFrame("CharacterSelect");

    CharacterSelectCharacterFrame:SetBackdrop({
        bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true,
        tileSize = 16,
        edgeSize = 16,
        insets = { left = 3, right = 3, top = 3, bottom = 3 }
    });

    CharacterSelectCharacterFrame:SetBackdropColor(0, 0, 0)

    SelectBorroso = 1;
    CharacterSelect_OnEventO();
    if CharSelectTooltip then
        CharSelectTooltipText:SetFont("Fonts\\FRIZQT__.TTF", 14, "OUTLINE")
        CharSelectTooltipText:SetTextColor(1, 1, 1)
        CharSelectTooltip:SetBackdropColor(0, 0, 0, 0.9)
        CharSelectTooltip:SetBackdropBorderColor(0.4, 0.4, 0.4, 1)
    end
end

function CharacterSelect_OnShow()
    -- request account data times from the server (so we know if we should refresh keybindings, etc...)
    ReadyForAccountDataTimes()

    local CurrentModel = CharacterSelect.currentModel;

    if ( CurrentModel ) then


        local ambience = GlueAmbienceTracks[strupper(CurrentModel)] or GlueAmbienceTracks["HUMAN"];


        if ( ambience ) then


            PlayGlueAmbience(ambience, 4.0);


        end
    end

    UpdateAddonButton();

    local serverName, isPVP, isRP = GetServerName();
    local connected = IsConnectedToServer();
    local serverType = "";
    if ( serverName ) then
        if( not connected ) then
            serverName = serverName.."\n("..SERVER_DOWN..")";
        end
        if ( isPVP ) then
            if ( isRP ) then
                serverType = RPPVP_PARENTHESES;
            else
                serverType = PVP_PARENTHESES;
            end
        elseif ( isRP ) then
            serverType = RP_PARENTHESES;
        end
        CharSelectRealmName:SetText(serverName.." "..serverType);
        CharSelectRealmName:Show();
    else
        CharSelectRealmName:Hide();
    end

    if ( connected ) then
        GetCharacterListUpdate();
    else
        UpdateCharacterList();
    end

    -- Gameroom billing stuff (For Korea and China only)
    if ( SHOW_GAMEROOM_BILLING_FRAME ) then
        local paymentPlan, hasFallBackBillingMethod, isGameRoom = GetBillingPlan();
        if ( paymentPlan == 0 ) then
            -- No payment plan
            GameRoomBillingFrame:Hide();
            CharacterSelectRealmSplitButton:ClearAllPoints();
            CharacterSelectRealmSplitButton:SetPoint("TOP", CharacterSelectLogo, "BOTTOM", 0, -5);
        else
            local billingTimeLeft = GetBillingTimeRemaining();
            -- Set default text for the payment plan
            local billingText = _G["BILLING_TEXT"..paymentPlan];
            if ( paymentPlan == 1 ) then
                -- Recurring account
                billingTimeLeft = ceil(billingTimeLeft/(60 * 24));
                if ( billingTimeLeft == 1 ) then
                    billingText = BILLING_TIME_LEFT_LAST_DAY;
                end
            elseif ( paymentPlan == 2 ) then
                -- Free account
                if ( billingTimeLeft < (24 * 60) ) then
                    billingText = format(BILLING_FREE_TIME_EXPIRE, billingTimeLeft.." "..MINUTES_ABBR);
                end
            elseif ( paymentPlan == 3 ) then
                -- Fixed but not recurring
                if ( isGameRoom == 1 ) then
                    if ( billingTimeLeft <= 30 ) then
                        billingText = BILLING_GAMEROOM_EXPIRE;
                    else
                        billingText = format(BILLING_FIXED_IGR, MinutesToTime(billingTimeLeft, 1));
                    end
                else
                    -- personal fixed plan
                    if ( billingTimeLeft < (24 * 60) ) then
                        billingText = BILLING_FIXED_LASTDAY;
                    else
                        billingText = format(billingText, MinutesToTime(billingTimeLeft));
                    end
                end
            elseif ( paymentPlan == 4 ) then
                -- Usage plan
                if ( isGameRoom == 1 ) then
                    -- game room usage plan
                    if ( billingTimeLeft <= 600 ) then
                        billingText = BILLING_GAMEROOM_EXPIRE;
                    else
                        billingText = BILLING_IGR_USAGE;
                    end
                else
                    -- personal usage plan
                    if ( billingTimeLeft <= 30 ) then
                        billingText = BILLING_TIME_LEFT_30_MINS;
                    else
                        billingText = format(billingText, billingTimeLeft);
                    end
                end
            end
            -- If fallback payment method add a note that says so
            if ( hasFallBackBillingMethod == 1 ) then
                billingText = billingText.."\n\n"..BILLING_HAS_FALLBACK_PAYMENT;
            end
            GameRoomBillingFrameText:SetText(billingText);
            GameRoomBillingFrame:SetHeight(GameRoomBillingFrameText:GetHeight() + 26);
            GameRoomBillingFrame:Show();
            CharacterSelectRealmSplitButton:ClearAllPoints();
            CharacterSelectRealmSplitButton:SetPoint("TOP", GameRoomBillingFrame, "BOTTOM", 0, -10);
        end
    end

    if( IsTrialAccount() ) then
        CharacterSelectUpgradeAccountButton:Show();
    else
        CharacterSelectUpgradeAccountButton:Hide();
    end

    -- fadein the character select ui
    GlueFrameFadeIn(CharacterSelectUI, CHARACTER_SELECT_FADE_IN)

    RealmSplitCurrentChoice:Hide();
    RequestRealmSplitInfo();

    --Clear out the addons selected item
    GlueDropDownMenu_SetSelectedValue(AddonCharacterDropDown, ALL);
end

function CharacterSelect_OnHide()
    CharacterDeleteDialog:Hide();
    CharacterRenameDialog:Hide();
    if ( DeclensionFrame ) then
        DeclensionFrame:Hide();
    end
    SERVER_SPLIT_STATE_PENDING = -1;
end

function CharacterSelect_OnUpdate(elapsed)
    if ( SERVER_SPLIT_STATE_PENDING > 0 ) then
        CharacterSelectRealmSplitButton:Show();

        if ( SERVER_SPLIT_CLIENT_STATE > 0 ) then
            RealmSplit_SetChoiceText();
            RealmSplitPending:SetPoint("TOP", RealmSplitCurrentChoice, "BOTTOM", 0, -10);
        else
            RealmSplitPending:SetPoint("TOP", CharacterSelectRealmSplitButton, "BOTTOM", 0, 0);
            RealmSplitCurrentChoice:Hide();
        end

        if ( SERVER_SPLIT_STATE_PENDING > 1 ) then
            CharacterSelectRealmSplitButton:Disable();
            CharacterSelectRealmSplitButtonGlow:Hide();
            RealmSplitPending:SetText( SERVER_SPLIT_PENDING );
        else
            CharacterSelectRealmSplitButton:Enable();
            CharacterSelectRealmSplitButtonGlow:Show();
            local datetext = SERVER_SPLIT_CHOOSE_BY.."\n"..SERVER_SPLIT_DATE;
            RealmSplitPending:SetText( datetext );
        end

        if ( SERVER_SPLIT_SHOW_DIALOG and not GlueDialog:IsShown() ) then
            SERVER_SPLIT_SHOW_DIALOG = false;
            local dialogString = format(SERVER_SPLIT,SERVER_SPLIT_DATE);
            if ( SERVER_SPLIT_CLIENT_STATE > 0 ) then
                local serverChoice = RealmSplit_GetFormatedChoice(SERVER_SPLIT_REALM_CHOICE);
                local stringWithDate = format(SERVER_SPLIT,SERVER_SPLIT_DATE);
                dialogString = stringWithDate.."\n\n"..serverChoice;
                GlueDialog_Show("SERVER_SPLIT_WITH_CHOICE", dialogString);
            else
                GlueDialog_Show("SERVER_SPLIT", dialogString);
            end
        end
    else
        CharacterSelectRealmSplitButton:Hide();
    end

    -- Account Msg stuff
    if ( (ACCOUNT_MSG_NUM_AVAILABLE > 0) and not GlueDialog:IsShown() ) then
        if ( ACCOUNT_MSG_HEADERS_LOADED ) then
            if ( ACCOUNT_MSG_BODY_LOADED ) then
                local dialogString = AccountMsg_GetHeaderSubject( ACCOUNT_MSG_CURRENT_INDEX ).."\n\n"..AccountMsg_GetBody();
                GlueDialog_Show("ACCOUNT_MSG", dialogString);
            end
        end
    end
end

function CharacterSelect_ClampScrollOffset(offset)
    local maxOffset = CharacterSelect.scrollMax or 0;
    offset = tonumber(offset) or 0;
    if ( offset < 0 ) then
        return 0;
    elseif ( offset > maxOffset ) then
        return maxOffset;
    end
    return math.floor(offset + 0.5);
end

function CharacterSelect_ApplyScrollOffset()
    if ( CharacterSelectCharacterScrollFrame and not CharacterSelect.scrollUpdating ) then
        CharacterSelect.scrollUpdating = true;
        CharacterSelectCharacterScrollFrame:SetVerticalScroll((CharacterSelect.scrollOffset or 0) * CHARACTER_SELECT_ROW_HEIGHT);
        local scrollbar = CharacterSelectCharacterScrollFrameScrollBar;
        if scrollbar then
            scrollbar:SetValue((CharacterSelect.scrollOffset or 0) * CHARACTER_SELECT_ROW_HEIGHT);
            GlueScrollFrame_OnVerticalScroll(CharacterSelectCharacterScrollFrame, scrollbar:GetValue());
        end
        CharacterSelect.scrollUpdating = false;
    end
end

function CharacterSelect_AdjustOffsetForSelection()
    local selectedIndex = CharacterSelect.selectedIndex or 0;
    if ( selectedIndex <= 0 or selectedIndex > GetNumCharacters() ) then
        return;
    end

    local offset = CharacterSelect.scrollOffset or 0;
    if ( selectedIndex <= offset ) then
        offset = selectedIndex - 1;
    elseif ( selectedIndex > offset + MAX_CHARACTERS_DISPLAYED ) then
        offset = selectedIndex - MAX_CHARACTERS_DISPLAYED;
    end
    CharacterSelect.scrollOffset = CharacterSelect_ClampScrollOffset(offset);
end

function CharacterSelect_RefreshVisibleList()
    local skipSelectionUpdate = CharacterSelect.skipSelectionUpdate;
    CharacterSelect.skipSelectionUpdate = true;
    UpdateCharacterList();
    CharacterSelect.skipSelectionUpdate = skipSelectionUpdate;
end

function CharacterSelect_SetScrollOffset(offset)
    local clamped = CharacterSelect_ClampScrollOffset(offset);
    if ( clamped == (CharacterSelect.scrollOffset or 0) ) then
        CharacterSelect_ApplyScrollOffset();
        return;
    end

    CharacterSelect.scrollOffset = clamped;
    CharacterSelect_RefreshVisibleList();
end

function CharacterSelect_ScrollBy(delta)
    if ( delta and delta ~= 0 ) then
        CharacterSelect_SetScrollOffset((CharacterSelect.scrollOffset or 0) - delta);
    end
end

function CharacterSelect_OnVerticalScroll(self, offset)
    if ( CharacterSelect.scrollUpdating ) then
        return;
    end
    -- Delegate to the stock glue helper first so it can do its own bookkeeping;
    -- guarded because the global is provided by GlueTemplates.lua and may be
    -- absent in a trimmed load order.
    if ( type(GlueScrollFrame_OnVerticalScroll) == "function" ) then
        GlueScrollFrame_OnVerticalScroll(self, offset);
    end
    CharacterSelect_SetScrollOffset(offset / CHARACTER_SELECT_ROW_HEIGHT);
end

function CharacterSelect_OnKeyDown(self,key)
    if ( key == "ESCAPE" ) then
        CharacterSelect_Exit();
    elseif ( key == "ENTER" ) then
        CharacterSelect_EnterWorld();
    elseif ( key == "PRINTSCREEN" ) then
        Screenshot();
    elseif ( key == "UP" or key == "LEFT" ) then
        local numChars = GetNumCharacters();
        if ( numChars > 1 ) then
            -- ECS walks the VISUAL order, which stops matching server order as
            -- soon as a custom order or a search filter is active. Without ECS
            -- loaded this falls back to the original server-index behaviour.
            --
            -- Both directions must delegate. Only DOWN/RIGHT used to, so with a
            -- custom order or a filter active, DOWN moved to the next row the
            -- user could SEE while UP jumped to an unrelated server index.
            if ( ECS and ECS.Integrate and ECS.Integrate.StepSelection ) then
                ECS.Integrate.StepSelection(-1);
            elseif ( self.selectedIndex > 1 ) then
                CharacterSelect_SelectCharacter(self.selectedIndex - 1);
            else
                CharacterSelect_SelectCharacter(numChars);
            end
        end
    elseif ( key == "DOWN" or key == "RIGHT" ) then
        local numChars = GetNumCharacters();
        if ( numChars > 1 ) then
            -- ECS walks the VISUAL order, which stops matching server order as
            -- soon as a custom order or a search filter is active. Without ECS
            -- loaded this falls back to the original server-index behaviour.
            if ( ECS and ECS.Integrate and ECS.Integrate.StepSelection ) then
                ECS.Integrate.StepSelection(1);
            elseif ( self.selectedIndex < GetNumCharacters() ) then
                CharacterSelect_SelectCharacter(self.selectedIndex + 1);
            else
                CharacterSelect_SelectCharacter(1);
            end
        end
    end
end

function CharacterSelect_OnEventO()
    local numChars = GetNumCharacters();
    local index = 1;
    local coords;

    if ( SelectBorroso == 0 ) then
        CharSelectCharacterName:SetAlpha(0);
        CharacterSelectCharacterFrame:SetAlpha(0);
        CharacterSelectCharacterFrame:SetPoint("TOPRIGHT", 5000, -8);
        CharSelectCharacterName2:SetPoint("CENTER", 330, 100);
        CharSelectCharacterName2:SetAlpha(1);
        CharSelectCharacterName2:SetFont("Fonts\\FRIZQT__.ttf", 43, "OUTLINE");
        CharSelectCharacterName2:Show();
        CharSelectCharacterName3:SetPoint("TOP", "CharSelectCharacterName2", 0, 120);
        CharSelectCharacterName3:SetFont("Fonts\\FRIZQT__.ttf", 83, "OUTLINE");
        CharSelectCharacterName3:Show();
        CharacterSelectRotateLeft:SetAlpha(0);
        CharacterSelectRotateRight:SetAlpha(0);
        CharacterSelectAddonsButton:SetAlpha(0);
        CharSelectEnterWorldButton:SetAlpha(0);
        CharacterSelectBackButton:SetAlpha(0);
        CharacterSelectAddonsButton:Disable();
        CharacterSelectBackButton:Disable();
        DesenfoBoton:SetText(" ");
        DesenfoBoton2:Hide();
        OptionsButton2:Hide();
        CharSelectChangeRealmButton:Hide();
        CharacterSelectDeleteButton:Hide();

    elseif ( SelectBorroso == 1 ) then
        CharSelectCharacterName:SetAlpha(1);
        CharacterSelectCharacterFrame:SetAlpha(1);
        CharacterSelectCharacterFrame:SetPoint("TOPRIGHT", -5, -42);
        CharSelectCharacterName2:SetPoint("CENTER", 330, 100);
        CharSelectCharacterName2:SetFont("Fonts\\FRIZQT__.ttf", 43, "OUTLINE");
        CharSelectCharacterName2:SetAlpha(0);
        CharSelectCharacterName2:Hide();
        CharSelectCharacterName3:SetPoint("TOP", "CharSelectCharacterName2", 0, 120);
        CharSelectCharacterName3:SetFont("Fonts\\FRIZQT__.ttf", 83, "OUTLINE");
        CharSelectCharacterName3:Hide();
        CharacterSelectRotateLeft:SetAlpha(1);
        CharacterSelectRotateRight:SetAlpha(1);
        CharacterSelectAddonsButton:SetAlpha(1);
        CharSelectEnterWorldButton:SetAlpha(1);
        CharacterSelectBackButton:SetAlpha(1);
        CharacterSelectAddonsButton:Enable();
        CharacterSelectBackButton:Enable();
        DesenfoBoton:SetText(" ");
        DesenfoBoton2:Show();
        OptionsButton2:Show();
        CharSelectChangeRealmButton:Show();
        CharacterSelectDeleteButton:Show();
    end
    PlaySound("igMainMenuOptionCheckBoxOn")
end

function CharacterSelect_OnEventO2()
    if (SelectBorroso2 == 0) then
        CharacterSelectCharacterFrame:Hide();
        if CharacterSelectDeleteButton then
            CharacterSelectDeleteButton:Hide();
        end

    elseif (SelectBorroso2 == 1) then
        CharacterSelectCharacterFrame:Show();
        if CharacterSelectDeleteButton then
            CharacterSelectDeleteButton:Show();
        end
    end
    PlaySound("igMainMenuOptionCheckBoxOn");
end

function CharacterSelect_OnEvent(self, event, ...)
    if ( event == "ADDON_LIST_UPDATE" ) then
        UpdateAddonButton();
    elseif ( event == "CHARACTER_LIST_UPDATE" ) then
        UpdateCharacterList();
        CharSelectCharacterName:SetText(GetCharacterInfo(self.selectedIndex));
    elseif ( event == "UPDATE_SELECTED_CHARACTER" ) then
        local index = ...;
        if ( index == 0 ) then
            CharSelectCharacterName:SetText("");
        else
            CharSelectCharacterName:SetText(GetCharacterInfo(index));
            self.selectedIndex = index;
        end
        CharacterSelect_AdjustOffsetForSelection();
        CharacterSelect_RefreshVisibleList();
    elseif ( event == "SELECT_LAST_CHARACTER" ) then
        self.selectLast = 1;
    elseif ( event == "SELECT_FIRST_CHARACTER" ) then
        CharacterSelect_SelectCharacter(1, 1);
    elseif ( event == "SUGGEST_REALM" ) then
        local category, id = ...;
        local name = GetRealmInfo(category, id);
        if ( name ) then
            SetGlueScreen("charselect");
            ChangeRealm(category, id);
        else
            if ( RealmList:IsShown() ) then
                RealmListUpdate();
            else
                RealmList:Show();
            end
        end
    elseif ( event == "FORCE_RENAME_CHARACTER" ) then
        local message = ...;
        CharacterRenameDialog:Show();
        CharacterRenameText1:SetText(_G[message]);
    end
end

function CharacterSelect_UpdateModel(self)
    UpdateSelectionCustomizationScene();
    self:AdvanceTime();
end

function UpdateCharacterSelection(self)
    for i=1, MAX_CHARACTERS_DISPLAYED, 1 do
        _G["CharSelectCharacterButton"..i]:UnlockHighlight();
    end

    local index = (self.selectedIndex or 0) - (CharacterSelect.scrollOffset or 0);
    if ( (index > 0) and (index <= MAX_CHARACTERS_DISPLAYED) )then
        _G["CharSelectCharacterButton"..index]:LockHighlight();
    end
end

function UpdateCharacterList()
    local numChars = GetNumCharacters();
    local index = 1;
    local coords;
    local maxOffset = math.max(numChars - MAX_CHARACTERS_DISPLAYED, 0);

    CharacterSelect.scrollMax = maxOffset;
    if ( numChars == 0 ) then
        CharacterSelect.scrollOffset = 0;
    elseif ( not CharacterSelect.skipSelectionUpdate ) then
        CharacterSelect_AdjustOffsetForSelection();
        CharacterSelect.scrollOffset = CharacterSelect_ClampScrollOffset(CharacterSelect.scrollOffset or 0);
    end

    if ( CharacterSelectCharacterScrollChild and CharacterSelectCharacterScrollFrame ) then
        local viewportHeight = CharacterSelectCharacterScrollFrame:GetHeight();
        if ( viewportHeight <= 0 ) then
            viewportHeight = 580;
        end
        CharacterSelectCharacterScrollChild:SetHeight(viewportHeight + maxOffset * CHARACTER_SELECT_ROW_HEIGHT);
        -- Tell the scroll frame to recompute its range from the new child height.
        -- Without this the scrollbar keeps a stale range after the child grows.
        CharacterSelectCharacterScrollFrame:UpdateScrollChildRect();
        CharacterSelect_ApplyScrollOffset();
    end

    for i = 1, MAX_CHARACTERS_DISPLAYED do
        local button = _G["CharSelectCharacterButton"..i];
        button:Show();
    end

    _G["CharacterSelectCharacterFrame"]:SetHeight(620);

    local GENERAL_BACKGROUND = {
        texture = "Interface\\Glues\\CharacterSelect\\uicharacterselectglues2x",
        coords = {0.656250000, 0.965820313, 0.123046875, 0.217285156}
    };

    for i = 1, MAX_CHARACTERS_DISPLAYED do
        local button = _G["CharSelectCharacterButton"..i];

        local generalBg = _G["CharSelectCharacterButton"..i.."GeneralBackground"];
        if generalBg then generalBg:Hide(); end

        local background = _G["CharSelectCharacterButton"..i.."Background"];
        if background then background:Hide(); end

        local factionIcon = _G["CharSelectCharacterButton"..i.."FactionIcon"];
        if factionIcon then factionIcon:Hide(); end

        local nameText = _G["CharSelectCharacterButton"..i.."ButtonTextName"];
        if nameText then nameText:SetText(""); end

        local levelText = _G["CharSelectCharacterButton"..i.."ButtonTextLevel"];
        if levelText then levelText:SetText(""); end

        local classText = _G["CharSelectCharacterButton"..i.."ButtonTextClass"];
        if classText then classText:SetText(""); end

        local zoneText = _G["CharSelectCharacterButton"..i.."ButtonTextZone"];
        if zoneText then zoneText:SetText(""); end

        local statusText = _G["CharSelectCharacterButton"..i.."ButtonTextStatus"];
        if statusText then statusText:SetText(""); end

        local defaultBg = _G["CharSelectCharacterButton"..i.."DefaultBackground"];
        if defaultBg then defaultBg:Hide(); end
    end

    local CLASS_COLORS = {
        -- Español
        ["GUERRERO"]="|cffC79C6E",["GUERRERA"]="|cffC79C6E",
        ["PALADIN"]="|cffF58CBA",
        ["CAZADOR"]="|cffABD473",["CAZADORA"]="|cffABD473",
        ["PICARO"]="|cffFFF569",["PICARA"]="|cffFFF569",
        ["SACERDOTE"]="|cffFFFFFF",["SACERDOTISA"]="|cffFFFFFF",
        ["CABALLERO DE LA MUERTE"]="|cffC41F3B",
        ["CHAMÃN"]="|cff0070DE",
        ["MAGO"]="|cff69CCF0",["MAGA"]="|cff69CCF0",
        ["BRUJO"]="|cff9482C9",["BRUJA"]="|cff9482C9",
        ["DRUIDA"]="|cffFF7D0A",
        -- English
        ["WARRIOR"]="|cffC79C6E",
        ["PALADIN"]="|cffF58CBA",
        ["HUNTER"]="|cffABD473",
        ["ROGUE"]="|cffFFF569",
        ["PRIEST"]="|cffFFFFFF",
        ["DEATHKNIGHT"]="|cffC41F3B",
        ["SHAMAN"]="|cff0070DE",
        ["MAGE"]="|cff69CCF0",
        ["WARLOCK"]="|cff9482C9",
        ["DRUID"]="|cffFF7D0A"
    };

    local function GetCharacterFaction(race)
        return GetFactionForRaceName(race)
    end

    local FACTION_ICONS = {
        ["Alliance"] = "Interface\\Glues\\CharacterSelect\\AllianceLogo",
        ["Horde"] = "Interface\\Glues\\CharacterSelect\\HordeLogo"
    };

    for actualIndex = (CharacterSelect.scrollOffset or 0) + 1, math.min(numChars, (CharacterSelect.scrollOffset or 0) + MAX_CHARACTERS_DISPLAYED), 1 do
        local name, race, class, level, zone, sex, ghost, PCC, PRC, PFC = GetCharacterInfo(actualIndex);
        local button = _G["CharSelectCharacterButton"..index];
        button:SetID(actualIndex);

        if ( not name ) then
            button:SetText("ERROR - Contact Admin");
        else
            if ( not zone ) then
                zone = "";
            end

            local faction = GetCharacterFaction(race);
            -- Both Darkfallen races share a display name, so take the side from the
            -- client's own per-character race string (the select backdrop key).
            local raceModel = strupper(GetSelectBackgroundModel(actualIndex) or "");
            if ( strfind(raceModel, "HORDE") ) then
                faction = "Horde";
            elseif ( raceModel == "OGREALLIANCE" or raceModel == "OGREHORDE" or raceModel == "DARKFALLEN" or raceModel == "TUSKARR" or raceModel == "VRYKUL" or raceModel == "THINHUMAN" or raceModel == "DRAKONIDALLIANCE" or raceModel == "SLIMEGIANTALLIANCE" or raceModel == "KYRIANALLIANCE" or raceModel == "CENTAURALLIANCE" or raceModel == "CENTAURHORDE" or raceModel == "SLIMEGIANTHORDE" or raceModel == "DRAKKINALLIANCE" or raceModel == "DRAKKINHORDE" or raceModel == "JINYUALLIANCE" or raceModel == "SETHRAKALLIANCE" ) then
                faction = "Alliance";
            end

            local defaultBg = _G["CharSelectCharacterButton"..index.."DefaultBackground"];
            if defaultBg then defaultBg:Hide(); end

            local generalBg = _G["CharSelectCharacterButton"..index.."GeneralBackground"];
            if not generalBg then
                generalBg = button:CreateTexture("CharSelectCharacterButton"..index.."GeneralBackground", "BACKGROUND", nil, 1);
            end
            generalBg:ClearAllPoints();
            generalBg:SetPoint("CENTER", button, "CENTER", -22, 1);
            generalBg:SetSize(243, 62);
            generalBg:SetTexture(GENERAL_BACKGROUND.texture);
            generalBg:SetTexCoord(unpack(GENERAL_BACKGROUND.coords));
            generalBg:Show();

            local background = _G["CharSelectCharacterButton"..index.."Background"];
            if not background then
                background = button:CreateTexture("CharSelectCharacterButton"..index.."Background", "OVERLAY", nil, 2);
            end
            background:ClearAllPoints();
            background:SetPoint("CENTER", button, "CENTER", -35, 0);
            background:SetSize(205, 55);
            background:Show();

            local factionIcon = _G["CharSelectCharacterButton"..index.."FactionIcon"];
            if not factionIcon then
                factionIcon = button:CreateTexture("CharSelectCharacterButton"..index.."FactionIcon", "OVERLAY", nil, 3);
            end
            factionIcon:ClearAllPoints();
            factionIcon:SetPoint("TOPLEFT", button, "TOPLEFT", 170, -5);
            factionIcon:SetSize(60, 60);

            if faction == "Alliance" then
                factionIcon:SetTexture(FACTION_ICONS["Alliance"]);
            else
                factionIcon:SetTexture(FACTION_ICONS["Horde"]);
            end
            factionIcon:Show();

            -- Nombre
            local nameText = _G["CharSelectCharacterButton"..index.."ButtonTextName"];
            if not nameText then
                nameText = button:CreateFontString("CharSelectCharacterButton"..index.."ButtonTextName", "OVERLAY", "GlueFontNormal");
            end
            nameText:ClearAllPoints();
            nameText:SetPoint("TOPLEFT", button, "TOPLEFT", 0, -8);
            nameText:SetText(name);

            -- Nivel
            local levelText = _G["CharSelectCharacterButton"..index.."ButtonTextLevel"];
            if not levelText then
                levelText = button:CreateFontString("CharSelectCharacterButton"..index.."ButtonTextLevel", "OVERLAY", "GlueFontNormalSmall");
            end
            levelText:ClearAllPoints();

            -- Separador entre nivel y clase
            local separatorText = _G["CharSelectCharacterButton"..index.."ButtonTextSeparator"];
            if not separatorText then
                separatorText = button:CreateFontString("CharSelectCharacterButton"..index.."ButtonTextSeparator", "OVERLAY", "GlueFontNormalSmall");
            end
            separatorText:ClearAllPoints();
            separatorText:SetPoint("TOPLEFT", button, "TOPLEFT", 20, -25);
            separatorText:SetText("|cffffffff-|r");

            if level < 10 then
                levelText:SetPoint("TOPLEFT", button, "TOPLEFT", 5, -25);
                levelText:SetText("|cffffffff"..level.."|r");
            else
                levelText:SetPoint("TOPLEFT", button, "TOPLEFT", 0, -25);
                levelText:SetText("|cffffffff"..level.."|r");
            end

            -- Clase
            local classText = _G["CharSelectCharacterButton"..index.."ButtonTextClass"];
            if not classText then
                classText = button:CreateFontString("CharSelectCharacterButton"..index.."ButtonTextClass", "OVERLAY", "GlueFontNormalSmall");
            end
            classText:ClearAllPoints();
            classText:SetPoint("TOPLEFT", button, "TOPLEFT", 30, -25);

            classText:SetWidth(150);
            classText:SetJustifyH("LEFT");
            classText:SetWordWrap(false);

            local classColor = CLASS_COLORS[strupper(class)] or "|cffFFFFFF";
            classText:SetText(classColor..class.."|r");

            -- Zona
            local zoneText = _G["CharSelectCharacterButton"..index.."ButtonTextZone"];
            if not zoneText then
                zoneText = button:CreateFontString("CharSelectCharacterButton"..index.."ButtonTextZone", "OVERLAY", "GlueFontNormalSmall");
            end
            zoneText:ClearAllPoints();
            zoneText:SetPoint("TOPLEFT", button, "TOPLEFT", 0, -40);

            zoneText:SetWidth(180);
            zoneText:SetJustifyH("LEFT");
            zoneText:SetWordWrap(false);

            zoneText:SetText(zone);
            zoneText:SetTextColor(0.3, 0.3, 0.3);

            -- Estado: Vivo/Muerto
            local statusText = _G["CharSelectCharacterButton"..index.."ButtonTextStatus"];
            if not statusText then
                statusText = button:CreateFontString("CharSelectCharacterButton"..index.."ButtonTextStatus", "OVERLAY", "GlueFontNormalSmall");
            end
            statusText:ClearAllPoints();

            if ghost then
                statusText:SetPoint("TOPLEFT", button, "TOPLEFT", 170, -5);
                statusText:SetFont("Fonts\\FRIZQT__.TTF", 12, "OUTLINE");
                statusText:SetTextColor(1, 0.2, 0.2);

                local deadText = _G["DEAD_INF"] or
                                (_G["GlueStrings"] and _G["GlueStrings"]["DEAD_INF"] and
                                 _G["GlueStrings"]["DEAD_INF"][GetLocale()]);
                statusText:SetText(deadText);
            else
                statusText:SetPoint("TOPLEFT", button, "TOPLEFT", 182, -5);
                statusText:SetFont("Fonts\\FRIZQT__.TTF", 12, "OUTLINE");
                statusText:SetTextColor(0.2, 1, 0.2);

                local aliveText = _G["LIFE_INF"] or
                                 (_G["GlueStrings"] and _G["GlueStrings"]["LIFE_INF"] and
                                  _G["GlueStrings"]["LIFE_INF"][GetLocale()]);
                statusText:SetText(aliveText);
            end
        end

        button:Show();

        local customizeButton = _G["CharSelectCharacterCustomize"..index];
        local raceChangeButton = _G["CharSelectRaceChange"..index];
        local factionChangeButton = _G["CharSelectFactionChange"..index];
        customizeButton:SetID(actualIndex);
        raceChangeButton:SetID(actualIndex);
        factionChangeButton:SetID(actualIndex);
        customizeButton:Hide();
        raceChangeButton:Hide();
        factionChangeButton:Hide();
        if (PFC) then
            factionChangeButton:Show();
        elseif (PRC) then
            raceChangeButton:Show();
        elseif (PCC) then
            customizeButton:Show();
        end

        index = index + 1;
        if (index > MAX_CHARACTERS_DISPLAYED) then
            break;
        end
    end

    for i = index, MAX_CHARACTERS_DISPLAYED do
        local button = _G["CharSelectCharacterButton"..i];
        button:SetID((CharacterSelect.scrollOffset or 0) + i);

        local defaultBg = _G["CharSelectCharacterButton"..i.."DefaultBackground"];
        if not defaultBg then
            defaultBg = button:CreateTexture("CharSelectCharacterButton"..i.."DefaultBackground", "BACKGROUND");
        end
        defaultBg:ClearAllPoints();
        defaultBg:SetPoint("CENTER", button, "CENTER", -22, 1);
        defaultBg:SetSize(242, 61);
        defaultBg:SetTexture("Interface\\Glues\\CharacterSelect\\uicharacterselectglues2x");
        defaultBg:SetTexCoord(0.619628906, 0.929199219, 0.242675781, 0.336425781);
        defaultBg:Show();

        local generalBg = _G["CharSelectCharacterButton"..i.."GeneralBackground"];
        if generalBg then generalBg:Hide(); end

        local background = _G["CharSelectCharacterButton"..i.."Background"];
        if background then background:Hide(); end

        local factionIcon = _G["CharSelectCharacterButton"..i.."FactionIcon"];
        if factionIcon then factionIcon:Hide(); end

        local nameText = _G["CharSelectCharacterButton"..i.."ButtonTextName"];
        if nameText then nameText:SetText(""); end

        local levelText = _G["CharSelectCharacterButton"..i.."ButtonTextLevel"];
        if levelText then levelText:SetText(""); end

        local separatorText = _G["CharSelectCharacterButton"..i.."ButtonTextSeparator"];
        if separatorText then separatorText:Hide(); end

        local classText = _G["CharSelectCharacterButton"..i.."ButtonTextClass"];
        if classText then classText:SetText(""); end

        local zoneText = _G["CharSelectCharacterButton"..i.."ButtonTextZone"];
        if zoneText then zoneText:SetText(""); end

        local statusText = _G["CharSelectCharacterButton"..i.."ButtonTextStatus"];
        if statusText then statusText:SetText(""); end

        _G["CharSelectCharacterCustomize"..i]:Hide();
        _G["CharSelectRaceChange"..i]:Hide();
        _G["CharSelectFactionChange"..i]:Hide();

        button:Show();
    end

    if ( numChars == 0 ) then
        CharacterSelect.selectedIndex = 0;
        if ( not CharacterSelect.skipSelectionUpdate ) then
            CharacterSelect_SelectCharacter(CharacterSelect.selectedIndex, 1);
        end
        CharacterSelectDeleteButton:Disable();
        CharSelectEnterWorldButton:Disable();
    else
        CharacterSelectDeleteButton:Enable();
        CharSelectEnterWorldButton:Enable();
    end

    CharacterSelect.createIndex = 0;
    local connected = IsConnectedToServer();

    if (numChars < MAX_CHARACTERS_PER_REALM and connected) then
        CharacterSelect.createIndex = numChars + 1;
        CharSelectCreateCharacterButton:SetID(CharacterSelect.createIndex);
        CharSelectCreateCharacterButton:Show();
    else
        CharSelectCreateCharacterButton:Hide();
    end

    if ( CharacterSelect.selectLast == 1 ) then
        CharacterSelect.selectLast = 0;
        CharacterSelect.scrollOffset = CharacterSelect.scrollMax;
        CharacterSelect_ApplyScrollOffset();
        if ( not CharacterSelect.skipSelectionUpdate ) then
            CharacterSelect_SelectCharacter(numChars, 1);
        end
        return;
    end

    if ( (CharacterSelect.selectedIndex == 0) or (CharacterSelect.selectedIndex > numChars) ) then
        CharacterSelect.selectedIndex = 1;
        CharacterSelect.scrollOffset = 0;
        CharacterSelect_ApplyScrollOffset();
    end
    UpdateCharacterSelection(CharacterSelect);
    if ( not CharacterSelect.skipSelectionUpdate ) then
        CharacterSelect_SelectCharacter(CharacterSelect.selectedIndex, 1);
    end
end

function CharacterSelectButton_OnClick(self)
    local id = self:GetID();
    local numChars = GetNumCharacters();

    if ( id <= numChars and id ~= CharacterSelect.selectedIndex ) then
        CharacterSelect_SelectCharacter(id);
    else
        PlaySound("igMainMenuOptionCheckBoxOn");
    end
end

function CharacterSelectButton_OnDoubleClick(self)
    local id = self:GetID();
    local numChars = GetNumCharacters();

    if ( id <= numChars ) then
        if ( id ~= CharacterSelect.selectedIndex ) then
            CharacterSelect_SelectCharacter(id);
        end
        CharacterSelect_EnterWorld();
    else
        PlaySound("igMainMenuOptionCheckBoxOn");
    end
end

function CharacterSelect_TabResize(self)
    local buttonMiddle = _G[self:GetName().."Middle"];
    local buttonMiddleDisabled = _G[self:GetName().."MiddleDisabled"];
    local width = self:GetTextWidth() - 8;
    local leftWidth = _G[self:GetName().."Left"]:GetWidth();
    buttonMiddle:SetWidth(width);
    buttonMiddleDisabled:SetWidth(width);
    self:SetWidth(width + (2 * leftWidth));
end

function CharacterSelect_SelectCharacter(id, noCreate)
    if ( id == CharacterSelect.createIndex ) then
        if ( not noCreate ) then
            PlaySound("gsCharacterSelectionCreateNew");
            SetGlueScreen("charcreate");
        end
    else
        CharacterSelect.currentModel = GetSelectBackgroundModel(id);
        SetBackgroundModel(CharacterSelect,CharacterSelect.currentModel);

        SelectCharacter(id);
    end
end

function CharacterDeleteDialog_OnShow()
    local name, race, class, level = GetCharacterInfo(CharacterSelect.selectedIndex);
    local faction, isAlliance;

    if name == "You" or name == "Found" or name == "Asecret" or name == "Area" then
        DisconnectFromServer();
        CharacterDeleteDialog:Hide();
        return;
    end

    local factionType = GetFactionForRaceName(race)

    if factionType == "Alliance" then
        faction = ALLIANCE_RACE;
        isAlliance = true;
    else
        faction = HORDE_RACE;
        isAlliance = false;
    end

    CharacterDeleteText1:SetFont("Fonts\\FRIZQT__.TTF", 12)
    CharacterDeleteText2:SetFont("Fonts\\FRIZQT__.TTF", 10)
    CharacterDeleteAlertText:SetFont("Fonts\\FRIZQT__.TTF", 10)
    CharacterDeleteAlertText:SetTextColor(1, 0.2, 0.2)
    CharacterDeleteAlertText:SetText(WARNING_CHAR_INF)

    CharacterDeleteCenterText:SetFont("Fonts\\FRIZQT__.TTF", 11)
    CharacterDeleteCenterText:SetTextColor(1, 0, 0)
    CharacterDeleteCenterText:SetText(WARNING_INF)

    CharacterDeleteText1:SetFormattedText(CONFIRM_CHAR_DELETE, name, level, class, faction);

    CharacterDeleteText1:ClearAllPoints()
    CharacterDeleteText1:SetPoint("CENTER", CharacterDeleteBackground, "CENTER", 0, 50)

    CharacterDeleteText2:ClearAllPoints()
    CharacterDeleteText2:SetPoint("CENTER", CharacterDeleteBackground, "CENTER", 0, -20)

    CharacterDeleteBackground:SetWidth(300);
    CharacterDeleteBackground:SetHeight(250);
    CharacterDeleteButton1:Disable();

    if not CharacterDeleteLeftTexture then
        CharacterDeleteLeftTexture = CharacterDeleteBackground:CreateTexture(nil, "ARTWORK")
        CharacterDeleteLeftTexture:SetSize(42, 42)
        CharacterDeleteLeftTexture:SetPoint("RIGHT", CharacterDeleteText1, "LEFT", 70, 0)
        CharacterDeleteLeftTexture:SetTexture("Interface\\Glues\\CharacterSelect\\PlusManz-Alliance")
    end

    if not CharacterDeleteRightTexture then
        CharacterDeleteRightTexture = CharacterDeleteBackground:CreateTexture(nil, "ARTWORK")
        CharacterDeleteRightTexture:SetSize(42, 42)
        CharacterDeleteRightTexture:SetPoint("LEFT", CharacterDeleteText1, "RIGHT", -70, 0)
        CharacterDeleteRightTexture:SetTexture("Interface\\Glues\\CharacterSelect\\PlusManz-Horde")
    end

    if isAlliance == true then
        CharacterDeleteLeftTexture:SetDesaturated(false)
        CharacterDeleteLeftTexture:SetAlpha(1.0)
        CharacterDeleteRightTexture:SetDesaturated(true)
        CharacterDeleteRightTexture:SetAlpha(0.6)
    else
        CharacterDeleteRightTexture:SetDesaturated(false)
        CharacterDeleteRightTexture:SetAlpha(1.0)
        CharacterDeleteLeftTexture:SetDesaturated(true)
        CharacterDeleteLeftTexture:SetAlpha(0.6)
    end

    CharacterDeleteLeftTexture:Show()
    CharacterDeleteRightTexture:Show()

    if not CharacterDeleteTooltip then
        CharacterDeleteTooltip = CreateFrame("Frame", nil, CharacterDeleteBackground)
        CharacterDeleteTooltip:SetFrameLevel(CharacterDeleteBackground:GetFrameLevel() - 1)

        CharacterDeleteTooltip:SetBackdrop({
            bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
            edgeFile = "Interface\\Tooltips\\ui-tooltip-border-mawBlack",
            tile = true,
            tileSize = 12,
            edgeSize = 12,
            insets = { left = 3, right = 3, top = 3, bottom = 3 }
        })

        CharacterDeleteTooltip:SetBackdropColor(0, 0, 0)
    end

    CharacterDeleteTooltip:ClearAllPoints()
    CharacterDeleteTooltip:SetPoint("LEFT", CharacterDeleteLeftTexture, "LEFT", -10, 0)
    CharacterDeleteTooltip:SetPoint("RIGHT", CharacterDeleteRightTexture, "RIGHT", 10, 0)
    CharacterDeleteTooltip:SetPoint("TOP", CharacterDeleteText1, "TOP", 0, 8)
    CharacterDeleteTooltip:SetPoint("BOTTOM", CharacterDeleteText1, "BOTTOM", 0, -10)

    CharacterDeleteTooltip:Show()

    if not CharacterDeleteCenterTooltip then
        CharacterDeleteCenterTooltip = CreateFrame("Frame", nil, CharacterDeleteBackground)
        CharacterDeleteCenterTooltip:SetFrameLevel(CharacterDeleteBackground:GetFrameLevel() - 1)

        CharacterDeleteCenterTooltip:SetBackdrop({
            bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
            edgeFile = "Interface\\Tooltips\\ui-tooltip-border-mawBlack",
            tile = true,
            tileSize = 12,
            edgeSize = 12,
            insets = { left = 3, right = 3, top = 3, bottom = 3 }
        })

        CharacterDeleteCenterTooltip:SetBackdropColor(0, 0, 0)
    end

    CharacterDeleteCenterTooltip:ClearAllPoints()
    CharacterDeleteCenterTooltip:SetPoint("TOP", CharacterDeleteCenterText, "TOP", 0, 5)
    CharacterDeleteCenterTooltip:SetPoint("BOTTOM", CharacterDeleteCenterText, "BOTTOM", 0, -5)
    CharacterDeleteCenterTooltip:SetPoint("LEFT", CharacterDeleteCenterText, "LEFT", 20, 0)
    CharacterDeleteCenterTooltip:SetPoint("RIGHT", CharacterDeleteCenterText, "RIGHT", -20, 0)

    CharacterDeleteCenterTooltip:Show()

    CharacterDeleteEditBox:SetText("")
    CharacterDeleteEditBox:SetFocus()
end

function CharacterSelect_EnterWorld()
    PlaySound("gsCharacterSelectionEnterWorld");
    StopGlueAmbience();
    EnterWorld();
end

function CharacterSelect_Exit()
    PlaySound("gsCharacterSelectionExit");
    DisconnectFromServer();
    SetGlueScreen("login");
end

function CharacterSelect_AccountOptions()
    PlaySound("gsCharacterSelectionAcctOptions");
end

function CharacterSelect_TechSupport()
    PlaySound("gsCharacterSelectionAcctOptions");
    LaunchURL(TECH_SUPPORT_URL);
end

function CharacterSelect_Delete()
    PlaySound("gsCharacterSelectionDelCharacter");
    if ( CharacterSelect.selectedIndex > 0 ) then
        CharacterDeleteDialog:Show();
    end
end

function CharacterSelect_ChangeRealm()
    PlaySound("gsCharacterSelectionDelCharacter");
    RequestRealmList(1);
end

function CharacterSelectFrame_OnMouseDown(button)
    if ( button == "LeftButton" ) then
        CHARACTER_SELECT_ROTATION_START_X = GetCursorPosition();
        CHARACTER_SELECT_INITIAL_FACING = GetCharacterSelectFacing();
    end
end

function CharacterSelectFrame_OnMouseUp(button)
    if ( button == "LeftButton" ) then
        CHARACTER_SELECT_ROTATION_START_X = nil
    end
end

function CharacterSelectFrame_OnUpdate()
    if ( CHARACTER_SELECT_ROTATION_START_X ) then
        local x = GetCursorPosition();
        local diff = (x - CHARACTER_SELECT_ROTATION_START_X) * CHARACTER_ROTATION_CONSTANT;
        CHARACTER_SELECT_ROTATION_START_X = GetCursorPosition();
        SetCharacterSelectFacing(GetCharacterSelectFacing() + diff);
    end
end

function CharacterSelectRotateRight_OnUpdate(self)
    if ( self:GetButtonState() == "PUSHED" ) then
        SetCharacterSelectFacing(GetCharacterSelectFacing() + CHARACTER_FACING_INCREMENT);
    end
end

function CharacterSelectRotateLeft_OnUpdate(self)
    if ( self:GetButtonState() == "PUSHED" ) then
        SetCharacterSelectFacing(GetCharacterSelectFacing() - CHARACTER_FACING_INCREMENT);
    end
end

function CharacterSelect_ManageAccount()
    PlaySound("gsCharacterSelectionAcctOptions");
    LaunchURL(AUTH_NO_TIME_URL);
end

function RealmSplit_GetFormatedChoice(formatText)
    if ( SERVER_SPLIT_CLIENT_STATE == 1 ) then
        realmChoice = SERVER_SPLIT_SERVER_ONE;
    else
        realmChoice = SERVER_SPLIT_SERVER_TWO;
    end
    return format(formatText, realmChoice);
end

function RealmSplit_SetChoiceText()
    RealmSplitCurrentChoice:SetText( RealmSplit_GetFormatedChoice(SERVER_SPLIT_CURRENT_CHOICE) );
    RealmSplitCurrentChoice:Show();
end

function CharacterSelect_PaidServiceOnClick(self, button, down, service)
    PAID_SERVICE_CHARACTER_ID = self:GetID();
    PAID_SERVICE_TYPE = service;
    PlaySound("gsCharacterSelectionCreateNew");
    SetGlueScreen("charcreate");
end

function CharacterSelect_DeathKnightSwap(self)
    if ( CharacterSelect.currentModel == "DEATHKNIGHT" ) then
        if (self.currentModel ~= "DEATHKNIGHT") then
            self.currentModel = "DEATHKNIGHT";
            self:SetNormalTexture("Interface\\Glues\\Common\\Glue-Panel-Button-Up-Blue");
            self:SetPushedTexture("Interface\\Glues\\Common\\Glue-Panel-Button-Down-Blue");
            self:SetHighlightTexture("Interface\\Glues\\Common\\Glue-Panel-Button-Highlight-Blue");
        end
    else
        if (self.currentModel == "DEATHKNIGHT") then
            self.currentModel = nil;
            self:SetNormalTexture("Interface\\Glues\\Common\\Glue-Panel-Button-Up");
            self:SetPushedTexture("Interface\\Glues\\Common\\Glue-Panel-Button-Down");
            self:SetHighlightTexture("Interface\\Glues\\Common\\Glue-Panel-Button-Highlight");
        end
    end
end

-- Esteria Vrykul preview sizing.
local creaturePreviewUpdate = CharacterSelect_UpdateModel;
function CharacterSelect_UpdateModel(self)
    creaturePreviewUpdate(self);
    local index = self.selectedIndex or 0;
    if index and index > 0 and type(CycleCharCustomization) == "function" then
        CycleCharCustomization("EA_PREVIEW_SCALE", index);
    end
end

-- Character select skin (wowdecomp)
-- Chrome around the Esteria Character Select (ECS_*.lua), which owns the roster, search and favorites. The skin
-- never touches rows or controls ECS positions; it adds: dark edge panels in the creator's style, a realm header, a
-- compact favorites list (ECS's own rows, scaled down, portraits kept), a details panel for the selected character,
-- a name plate, and the scene dip (out to black, swap at the darkest point, fade back in; the native side fades the
-- new model in and plays its salute). Any error turns the skin off for the session.
-- The stock screen is saved in Tools\RetailAssets\glue\backup\charselect-stock (ECS files in ..\ecs).
-- Its own function: keeps the chunk clear of Lua's 200-locals-per-function limit.
(function()
local SK = { ready = false, broken = nil, fade = nil, pending = nil, alpha = 0 };

local GOLD = { 0.91, 0.77, 0.42 };
local GOLD_DIM = { 0.54, 0.43, 0.18 };
local INK = { 0.03, 0.04, 0.055 };
local TEXT = { 0.88, 0.86, 0.80 };
local MUTED = { 0.60, 0.59, 0.55 };
local FONT = "Fonts\\FRIZQT__.TTF";
local WHITE = "Interface\\Buttons\\WHITE8X8";
local PANEL_W, EDGE_X = 300, 14;
-- The logo texture is a 1024 square whose art spans y 240..692, so crop to that band to fill the panel's width.
local LOGO_W, LOGO_H = PANEL_W - 2 * EDGE_X, 122;
local COUNT_Y, RULE_Y, LABEL_Y = LOGO_H + 12, LOGO_H + 34, LOGO_H + 42;
local FAV_SCALE, FAV_TOP = 0.72, LOGO_H + 62; -- favorites keep ECS's rows (portraits and all), drawn at 72%
local GEAR_SLOTS = 19;

-- Scene dip: out to black, swap, back in.
local FADE_PEAK, FADE_OUT, FADE_IN = 0.92, 0.16, 0.55;

local function SK_Log(text)
    if BodyShapeLog then BodyShapeLog("select skin: " .. tostring(text)); end
end

local function Solid(parent, layer, r, g, b, a)
    local tex = parent:CreateTexture(nil, layer or "ARTWORK");
    tex:SetTexture(WHITE);
    tex:SetVertexColor(r, g, b, a or 1);
    return tex;
end

local function Text(parent, size, color, layer)
    local fs = parent:CreateFontString(nil, layer or "OVERLAY", "GlueFontHighlightSmall");
    fs:SetFont(FONT, size);
    fs:SetTextColor(color[1], color[2], color[3]);
    fs:SetShadowOffset(1, -1);
    fs:SetShadowColor(0, 0, 0, 0.8);
    return fs;
end

local function Smooth(t)
    if t <= 0 then return 0; elseif t >= 1 then return 1; end
    return t * t * (3 - 2 * t);
end

local function Hex(colour)
    colour = colour or { 1, 1, 1 };
    return string.format("|cff%02x%02x%02x", math.floor(colour[1] * 255 + 0.5), math.floor(colour[2] * 255 + 0.5),
        math.floor(colour[3] * 255 + 0.5));
end

-- The selected character as ECS sees it (custom race names, faction, note); stock fields when ECS is absent.
local function Current()
    local selected = CharacterSelect.selectedIndex or 0;
    local data = ECS and ECS.Integrate and ECS.Integrate.data;
    if data then
        for _, character in ipairs(data) do
            if character.realIndex == selected then return character; end
        end
    end
    local name, race, class, level, zone, _, ghost = GetCharacterInfo(selected);
    if not name then return nil; end
    return { realIndex = selected, name = name, raceName = race, className = class, level = level, zone = zone, ghost = ghost };
end

local function InfoLine(character)
    return (LEVEL or "Level") .. " " .. tostring(character.level or "?") .. "  " .. (character.raceName or "") .. "  "
        .. Hex(character.classColour) .. (character.className or "") .. "|r";
end

-- ---- static chrome -------------------------------------------------------------------------------------------

local function BuildChrome()
    local chrome = CreateFrame("Frame", "CharSelectSkinChrome", CharacterSelectUI);
    chrome:SetAllPoints(CharacterSelectUI);
    chrome:SetFrameLevel(CharacterSelectUI:GetFrameLevel());
    SK.chrome = chrome;

    for _, side in ipairs({ "LEFT", "RIGHT" }) do
        local left = side == "LEFT";
        local panel = Solid(chrome, "BACKGROUND", INK[1], INK[2], INK[3], 0.84);
        panel:SetPoint("TOP" .. side, chrome, "TOP" .. side, 0, 0);
        panel:SetPoint("BOTTOM" .. side, chrome, "BOTTOM" .. side, 0, 0);
        panel:SetWidth(PANEL_W);
        local fade = chrome:CreateTexture(nil, "BACKGROUND");
        fade:SetTexture(WHITE);
        fade:SetWidth(70);
        fade:SetPoint("TOP" .. (left and "LEFT" or "RIGHT"), panel, "TOP" .. (left and "RIGHT" or "LEFT"), 0, 0);
        fade:SetPoint("BOTTOM" .. (left and "LEFT" or "RIGHT"), panel, "BOTTOM" .. (left and "RIGHT" or "LEFT"), 0, 0);
        if left then
            fade:SetGradientAlpha("HORIZONTAL", INK[1], INK[2], INK[3], 0.5, INK[1], INK[2], INK[3], 0);
        else
            fade:SetGradientAlpha("HORIZONTAL", INK[1], INK[2], INK[3], 0, INK[1], INK[2], INK[3], 0.5);
        end
        local edge = Solid(chrome, "BORDER", GOLD_DIM[1], GOLD_DIM[2], GOLD_DIM[3], 0.9);
        edge:SetWidth(1);
        edge:SetPoint("TOP", panel, "TOP" .. (left and "RIGHT" or "LEFT"), 0, 0);
        edge:SetPoint("BOTTOM", panel, "BOTTOM" .. (left and "RIGHT" or "LEFT"), 0, 0);
    end

    -- Logo across the panel with the character count under it, then the favorites label.
    -- The stock logo belongs to the parent frame and would sit under the panel; draw the same art from the chrome.
    local logo = chrome:CreateTexture(nil, "OVERLAY");
    logo:SetTexture(CharacterSelectLogo:GetTexture());
    logo:SetTexCoord(0, 1, 0.23, 0.68);
    logo:SetSize(LOGO_W, LOGO_H);
    logo:SetPoint("TOPLEFT", chrome, "TOPLEFT", EDGE_X, -8);
    CharacterSelectLogo:SetAlpha(0);
    SK.count = Text(chrome, 12, MUTED);
    SK.count:SetPoint("TOP", chrome, "TOPLEFT", PANEL_W / 2, -COUNT_Y);
    local rule = Solid(chrome, "BORDER", GOLD_DIM[1], GOLD_DIM[2], GOLD_DIM[3], 0.6);
    rule:SetSize(PANEL_W - 2 * EDGE_X, 1);
    rule:SetPoint("TOPLEFT", chrome, "TOPLEFT", EDGE_X, -RULE_Y);
    SK.favLabel = Text(chrome, 13, GOLD);
    SK.favLabel:SetPoint("TOPLEFT", chrome, "TOPLEFT", EDGE_X + 6, -LABEL_Y);
    SK.favLabel:SetText("FAVORITES");

    -- The name and info line under the character are hidden (the details panel and roster already show them).
    -- Tick still fills SK.nameInfo, so keep it, hidden.
    SK.nameInfo = Text(chrome, 14, MUTED);
    SK.nameInfo:Hide();
    CharSelectCharacterName:Hide();

    -- Scene dip, above the 3D view and below every control.
    local overlay = CreateFrame("Frame", "CharSelectSkinFade", CharacterSelect);
    overlay:SetAllPoints(CharacterSelect);
    overlay:SetFrameLevel(CharacterSelectUI:GetFrameLevel());
    local black = overlay:CreateTexture(nil, "BACKGROUND");
    black:SetAllPoints(overlay);
    black:SetTexture(WHITE);
    black:SetVertexColor(0, 0, 0, 1);
    overlay:SetAlpha(0);
    SK.overlay = overlay;
end

-- ---- details panel -------------------------------------------------------------------------------------------

local DETAIL_ROWS = { "Level", "Race", "Class", "Faction", "Zone", "Status", "Gear", "Pet" };
local ROW_PITCH = 20;

local function BuildDetails()
    local panel = CreateFrame("Frame", "CharSelectSkinDetails", SK.chrome);
    panel:SetWidth(PANEL_W - 2 * EDGE_X);
    panel:SetHeight(48 + #DETAIL_ROWS * ROW_PITCH + 40);
    local back = Solid(panel, "BACKGROUND", 1, 1, 1, 0.04);
    back:SetAllPoints(panel);
    local top = Solid(panel, "BORDER", GOLD_DIM[1], GOLD_DIM[2], GOLD_DIM[3], 0.7);
    top:SetHeight(1);
    top:SetPoint("TOPLEFT", panel, "TOPLEFT", 0, 0);
    top:SetPoint("TOPRIGHT", panel, "TOPRIGHT", 0, 0);

    panel.title = Text(panel, 15, GOLD);
    panel.title:SetPoint("TOPLEFT", panel, "TOPLEFT", 12, -12);
    panel.title:SetPoint("RIGHT", panel, "RIGHT", -12, 0);
    panel.title:SetJustifyH("LEFT");
    local sub = Text(panel, 11, MUTED);
    sub:SetPoint("TOPLEFT", panel.title, "BOTTOMLEFT", 0, -3);
    sub:SetText("CHARACTER DETAILS");

    panel.labels, panel.values = {}, {};
    for index, label in ipairs(DETAIL_ROWS) do
        local y = -(44 + (index - 1) * ROW_PITCH);
        local line = Solid(panel, "BACKGROUND", 1, 1, 1, 0.06);
        line:SetHeight(1);
        line:SetPoint("TOPLEFT", panel, "TOPLEFT", 12, y + 3);
        line:SetPoint("TOPRIGHT", panel, "TOPRIGHT", -12, y + 3);
        panel.labels[index] = Text(panel, 12, MUTED);
        panel.labels[index]:SetPoint("TOPLEFT", panel, "TOPLEFT", 12, y - 2);
        panel.labels[index]:SetText(label);
        panel.values[index] = Text(panel, 12, TEXT);
        panel.values[index]:SetPoint("TOPRIGHT", panel, "TOPRIGHT", -12, y - 2);
        panel.values[index]:SetJustifyH("RIGHT");
        panel.values[index]:SetWidth(170);
    end
    panel.note = Text(panel, 11, MUTED);
    panel.note:SetPoint("TOPLEFT", panel, "TOPLEFT", 12, -(48 + #DETAIL_ROWS * ROW_PITCH + 2));
    panel.note:SetWidth(PANEL_W - 2 * EDGE_X - 24);
    panel.note:SetJustifyH("LEFT");
    SK.details = panel;
end

local function UpdateDetails(character)
    local panel = SK.details;
    if not character then panel:Hide(); return; end
    panel:Show();
    panel.title:SetText(character.name or "");
    local faction = character.factionAccent or MUTED;
    local equipped, petLevel = 0, 0;
    if GetCharacterDetails then equipped, petLevel = GetCharacterDetails(character.realIndex); end
    local values = {
        tostring(character.level or "?"),
        character.raceName or "Unknown",
        Hex(character.classColour) .. (character.className or "Hero") .. "|r",
        Hex(faction) .. (character.factionName or "Neutral") .. "|r",
        character.zone or "Unknown Zone",
        character.ghost and "|cffff4040Dead|r" or "|cff5fd35fAlive|r",
        (equipped or 0) .. " / " .. GEAR_SLOTS .. " slots",
        (petLevel and petLevel > 0) and ("Level " .. petLevel) or "None",
    };
    for index, value in ipairs(values) do panel.values[index]:SetText(value); end
    local note = character.note;
    if type(note) == "string" and note ~= "" then
        panel.note:SetText("Note: " .. note);
    else
        panel.note:SetText("");
    end
end

-- ---- bottom-left buttons -------------------------------------------------------------------------------------

-- Back, then Options to its right. AddOns lives inside the Options menu now, so the standalone button goes.
-- ECS re-lays these on every roster refresh, so this runs after its layout and again on each tick.
local function Buttons()
    local back, options, addons = CharacterSelectBackButton, OptionsButton2, CharacterSelectAddonsButton;
    if addons then addons:Hide(); addons:EnableMouse(false); end
    if back and options then
        options:ClearAllPoints();
        options:SetPoint("BOTTOMLEFT", back, "BOTTOMRIGHT", -14, 0);
    end
end

-- ---- per-tick refresh ----------------------------------------------------------------------------------------

local function Tick()
    local numChars = GetNumCharacters();
    SK.count:SetText(numChars .. (numChars == 1 and " character" or " characters"));

    -- Favorites: ECS's own rows, smaller. Re-assert every tick; ECS only sets the anchor once at build.
    Buttons();
    local favorites = ECSFavoritesPanel;
    local below = nil;
    if favorites then
        if favorites:GetScale() ~= FAV_SCALE then favorites:SetScale(FAV_SCALE); end
        favorites:ClearAllPoints();
        favorites:SetPoint("TOPLEFT", SK.chrome, "TOPLEFT", EDGE_X / FAV_SCALE, -FAV_TOP / FAV_SCALE);
        if favorites:IsShown() then below = favorites; end
    end
    if below then SK.favLabel:Show(); else SK.favLabel:Hide(); end
    SK.details:ClearAllPoints();
    if below then
        SK.details:SetPoint("TOPLEFT", below, "BOTTOMLEFT", 0, -10);
    else
        SK.details:SetPoint("TOPLEFT", SK.chrome, "TOPLEFT", EDGE_X, -LABEL_Y);
    end

    local character = Current();
    UpdateDetails(character);
    SK.nameInfo:SetText(character and InfoLine(character) .. ((character.zone and character.zone ~= "") and ("  \194\183  " .. character.zone) or "") or "");
end

-- ---- scene dip -----------------------------------------------------------------------------------------------

local stockSelect = CharacterSelect_SelectCharacter;
function CharacterSelect_SelectCharacter(id, noCreate)
    if SK.broken or not SK.ready or noCreate or id == CharacterSelect.createIndex
        or id == CharacterSelect.selectedIndex or not CharacterSelect:IsVisible() then
        return stockSelect(id, noCreate);
    end
    SK.pending = id;
    if not SK.fade or SK.fade.phase == "in" then
        -- Continue from the current darkness so a quick second pick does not flash.
        SK.fade = { phase = "out", t = SK.alpha / FADE_PEAK * FADE_OUT };
    end
end

local function Fade(elapsed)
    local fade = SK.fade;
    if not fade then return; end
    fade.t = fade.t + elapsed;
    if fade.phase == "out" then
        SK.alpha = FADE_PEAK * Smooth(fade.t / FADE_OUT);
        if fade.t >= FADE_OUT then
            local id = SK.pending;
            SK.pending = nil;
            SK.fade = { phase = "in", t = 0 };
            if id then stockSelect(id); end
        end
    else
        SK.alpha = FADE_PEAK * (1 - Smooth(fade.t / FADE_IN));
        if fade.t >= FADE_IN then SK.alpha = 0; SK.fade = nil; end
    end
    SK.overlay:SetAlpha(SK.alpha);
end

-- ---- install -------------------------------------------------------------------------------------------------

local function SetSkin(on)
    if SK.chrome then if on then SK.chrome:Show(); else SK.chrome:Hide(); end end
    if not on then
        if SK.overlay then SK.overlay:SetAlpha(0); end
        if ECSFavoritesPanel then ECSFavoritesPanel:SetScale(1); end
        if SK.pending then local id = SK.pending; SK.pending = nil; SK.fade = nil; stockSelect(id); end
    end
end

local function Fail(where, err)
    SK.broken = tostring(err);
    SK_Log(where .. ": " .. SK.broken);
    pcall(SetSkin, false);
end

local function SK_Install()
    BuildChrome();
    BuildDetails();
    CharacterSelectUI:EnableMouseWheel(true);
    CharacterSelectUI:SetScript("OnMouseWheel", function(_, delta) CycleCharCustomization("EA_ZOOM", delta); end);
    SK.ready = true;
    if ECS and ECS.UI and type(ECS.UI.Layout) == "function" then
        local layout = ECS.UI.Layout;
        ECS.UI.Layout = function(...)
            layout(...);
            if not SK.broken then pcall(Buttons); end
        end;
    end
    local driver = CreateFrame("Frame");
    driver:SetScript("OnUpdate", function(self, elapsed)
        if not CharacterSelectUI:IsVisible() then return; end
        -- "Hide All" hides the roster frame; the panels go with it.
        if CharacterSelectCharacterFrame:IsShown() then SK.chrome:Show(); else SK.chrome:Hide(); end
        local ok, err = pcall(Fade, elapsed);
        if not ok then Fail("fade error", err); self:SetScript("OnUpdate", nil); return; end
        self.elapsed = (self.elapsed or 0) + elapsed;
        if self.elapsed < 0.1 then return; end
        self.elapsed = 0;
        ok, err = pcall(Tick);
        if not ok then Fail("tick error", err); self:SetScript("OnUpdate", nil); end
    end);
    SK_Log("installed");
end

-- ---- camera zoom and pan -------------------------------------------------------------------------------------

-- Mouse wheel zooms to the face and right-drag pans, as in the creator (the native side handles the camera).
-- Left-drag still turns the character. Right-click without moving resets the pan.
local stockMouseDown, stockMouseUp, stockUpdate = CharacterSelectFrame_OnMouseDown, CharacterSelectFrame_OnMouseUp, CharacterSelectFrame_OnUpdate;
local panX, panY, panMoved;

function CharacterSelectFrame_OnMouseDown(button, ...)
    if button == "RightButton" then
        panX, panY = GetCursorPosition();
        panMoved = false;
    end
    return stockMouseDown(button, ...);
end

function CharacterSelectFrame_OnMouseUp(button, ...)
    if button == "RightButton" and panX then
        if not panMoved then CycleCharCustomization("EA_PAN_RESET", 1); end
        panX = nil;
    end
    return stockMouseUp(button, ...);
end

function CharacterSelectFrame_OnUpdate(...)
    if panX then
        local x, y = GetCursorPosition();
        local dx, dy = x - panX, y - panY;
        if dx ~= 0 or dy ~= 0 then
            panMoved = true;
            CycleCharCustomization("EA_PAN", dx / GetScreenHeight(), dy / GetScreenHeight());
            panX, panY = x, y;
        end
    end
    return stockUpdate(...);
end

local waiter = CreateFrame("Frame");
waiter:SetScript("OnUpdate", function(self)
    if not (CharacterSelect and CharacterSelectUI and CharacterSelectCharacterFrame and CharacterSelectLogo
        and CharSelectCharacterName) then
        return;
    end
    self:SetScript("OnUpdate", nil);
    local ok, err = pcall(SK_Install);
    if not ok then Fail("install failed", err); end
end);
end)();

-- Screen transitions (wowdecomp)
-- Character select <-> create and the create screen's race page <-> customization page dip to black and back
-- instead of snapping. The change itself runs at the darkest point. Any error runs the change at once and turns
-- the fade off for the session.
(function()
local FADE_OUT, FADE_IN = 0.18, 0.35;
local T = { alpha = 0, fade = nil, pending = nil, running = false, broken = nil };
local SCREENS = { charselect = true, charcreate = true };
local WHITE_TEX = "Interface" .. string.char(92) .. "Buttons" .. string.char(92) .. "WHITE8X8";

local function Smooth(t)
    if t <= 0 then return 0; elseif t >= 1 then return 1; end
    return t * t * (3 - 2 * t);
end

local function Run(fn)
    T.running = true;
    local ok, err = pcall(fn);
    T.running = false;
    if not ok then T.broken = tostring(err); T.fade = nil; T.alpha = 0; if T.overlay then T.overlay:SetAlpha(0); end end
end

local function Step(elapsed)
    local fade = T.fade;
    if not fade then return; end
    fade.t = fade.t + elapsed;
    if fade.phase == "out" then
        T.alpha = Smooth(fade.t / FADE_OUT);
        if fade.t >= FADE_OUT then
            local fn = T.pending;
            T.pending = nil;
            T.fade = { phase = "in", t = 0 };
            if fn then Run(fn); end
        end
    else
        T.alpha = 1 - Smooth(fade.t / FADE_IN);
        if fade.t >= FADE_IN then T.alpha = 0; T.fade = nil; end
    end
    T.overlay:SetAlpha(T.alpha);
end

local function Overlay()
    if T.overlay then return T.overlay; end
    if not (GlueParent and CreateFrame) then return nil; end
    local frame = CreateFrame("Frame", "GlueTransitionFade", GlueParent);
    frame:SetAllPoints(GlueParent);
    frame:SetFrameStrata("TOOLTIP");
    local black = frame:CreateTexture(nil, "BACKGROUND");
    black:SetAllPoints(frame);
    black:SetTexture(WHITE_TEX);
    black:SetVertexColor(0, 0, 0, 1);
    frame:SetAlpha(0);
    frame:SetScript("OnUpdate", function(_, elapsed)
        if not T.broken then
            local ok, err = pcall(Step, elapsed);
            if not ok then
                T.broken = tostring(err);
                local fn = T.pending;
                T.pending, T.fade, T.alpha = nil, nil, 0;
                frame:SetAlpha(0);
                if fn then Run(fn); end
            end
        end
    end);
    T.overlay = frame;
    return frame;
end

local function Transition(fn)
    if T.broken or T.running or not Overlay() then return fn(); end
    if T.pending then
        -- A second change while the first waits: do both now and fade in over them.
        local old = T.pending;
        T.pending = nil;
        Run(old);
        Run(fn);
        T.fade = { phase = "in", t = 0 };
        return;
    end
    T.pending = fn;
    -- Continue from the current darkness so a change mid fade-in does not flash.
    T.fade = { phase = "out", t = T.alpha * FADE_OUT };
end

local stockScreen = SetGlueScreen;
function SetGlueScreen(name, ...)
    local from = CURRENT_GLUE_SCREEN;
    if T.broken or T.running or not (SCREENS[name] and SCREENS[from] and name ~= from) then
        return stockScreen(name, ...);
    end
    local args = { ... };
    Transition(function() stockScreen(name, unpack(args)); end);
end

-- The create screen's functions are defined after this file loads, so wrap them once they exist.
local waiter = CreateFrame("Frame");
waiter:SetScript("OnUpdate", function(self)
    local toggle, back = CharacterCreate_TogglePersonalization, CharacterCreate_Back;
    if type(toggle) ~= "function" or type(back) ~= "function" then return; end
    self:SetScript("OnUpdate", nil);
    function CharacterCreate_TogglePersonalization(...)
        if T.broken or T.running then return toggle(...); end
        local args = { ... };
        Transition(function() toggle(unpack(args)); end);
    end
    function CharacterCreate_Back(...)
        -- Back from the customization page toggles it; Back from the race page goes through SetGlueScreen.
        if T.broken or T.running or not (CharacterCreate and CharacterCreate.personalizationMode) then return back(...); end
        local args = { ... };
        Transition(function() back(unpack(args)); end);
    end
end);
end)();

-- AddOns entry in the Options menu (wowdecomp)
-- The character select screen's AddOns button moved into the Options menu. The menu is shared with the login screen,
-- so the entry only shows (and the menu only grows to fit it) while character select is the current screen.
(function()
local EXTRA = 55;
local waiter = CreateFrame("Frame");
waiter:SetScript("OnUpdate", function(self)
    local container = OptionsSelectFrameBackgroundContainer;
    local last = OptionsSelectFrameBackgroundContainerRealmResetOptionsButton;
    if not (OptionsSelectFrame and OptionsSelectFrameBackground and container and last and AddonList) then return; end
    self:SetScript("OnUpdate", nil);
    local ok = pcall(function()
        local button = CreateFrame("Button", "OptionsSelectAddOnsButton", container, "GlueButtonTemplateBlue");
        button:SetSize(220, 45);
        button:SetPoint("TOP", last, "BOTTOM", 0, -10);
        button:SetText(ADDONS or "AddOns");
        button:SetScript("OnClick", function()
            PlaySound("igMainMenuOption");
            OptionsSelectFrame:Hide();
            AddonList:Show();
        end);
        local height = OptionsSelectFrameBackground:GetHeight();
        OptionsSelectFrame:HookScript("OnShow", function()
            local inSelect = CURRENT_GLUE_SCREEN == "charselect";
            if inSelect then button:Show(); else button:Hide(); end
            OptionsSelectFrameBackground:SetHeight(height + (inSelect and EXTRA or 0));
        end);
    end);
    if not ok then return; end
end);
end)();
