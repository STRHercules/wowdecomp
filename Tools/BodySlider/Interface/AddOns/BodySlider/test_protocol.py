"""Small Lua 5.1 behavioral checks; run with Python and lupa installed."""
from pathlib import Path
import unittest

from lupa.lua51 import LuaRuntime


class ProtocolTests(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute(r'''
            now, calls, cleared, sent, frames = 100, {}, {}, {}, {}
            targetName, targetGuid = "Alice", "0x0000000000000002"
            function CreateFrame()
                local f = { scripts = {} }
                function f:SetScript(name, fn) self.scripts[name] = fn end
                function f:RegisterEvent() end
                frames[#frames + 1] = f
                return f
            end
            function GetTime() return now end
            function UnitName(unit) if unit == "target" then return targetName end return "Self" end
            function UnitGUID(unit) if unit == "target" then return targetGuid end return "0x0000000000000001" end
            function UnitIsPlayer(unit) return unit == "target" end
            function GetNumRaidMembers() return 0 end
            function IsIgnored() return false end
            function BodySlider_Set(guid, name, value) calls[#calls + 1] = { guid, name, value }; return true end
            function BodySlider_Clear(guid) cleared[#cleared + 1] = guid or "all" end
            function SendAddonMessage(prefix, body, dist, target) sent[#sent + 1] = { prefix, body, dist, target } end
            function message(prefix, body, sender)
                frames[1].scripts.OnEvent(frames[1], "CHAT_MSG_ADDON", prefix, body, "WHISPER", sender or "Alice")
            end
            function tick(seconds) now = now + seconds; frames[1].scripts.OnUpdate(frames[1], seconds) end
        ''')
        self.lua.execute((Path(__file__).parent / "Protocol.lua").read_text())
        self.lua.execute('store = { values = {}, transport = "whisper" }; BodySlider.Start(store)')

    def run_lua(self, code):
        self.lua.execute(code)

    def test_validated_round_trip_and_native_sender_binding(self):
        self.run_lua('''
            local encoded = BodySlider.Encode({ Height = .25, LegLength = -.5 })
            assert(BodySlider.Decode(encoded).Height == .25)
            message("MSP", "BS7=" .. encoded)
            assert(#calls == 37)
            assert(calls[1][1] == targetGuid and calls[1][3] == .25)
            assert(calls[11][3] == -.5)
            assert(msp_RPAddOn == nil and msp == nil)
        ''')

    def test_malformed_payloads_never_partially_apply(self):
        self.run_lua('''
            for _, value in ipairs({
                "BodySlider/1:1,0,0,0,0,0,0,0,0,0,0,101",
                "BodySlider/1:1,0,0,0,0,0,0,0,0,0,0,nan",
                "BodySlider/1:1,0,0,0,0,0,0,0,0,0,0,0,",
                "BodySlider/1:1,0,0,0,0,0,0,0,0,0,0,0,0",
                "BodySlider/1:1,,0,0,0,0,0,0,0,0,0,0,0",
                "BodySlider/2:1,0,0,0,0,0,0,0,0,0,0,0",
                "BodySlider/1:1,0,0,0,0,0,0,0,0,0,0,0x0000000000000003",
                "error('must never execute')",
            }) do
                message("MSP", "BS1=" .. value)
                assert(#calls == 0)
            end
            message("MSP", "BS1=" .. BodySlider.Encode({}), "Stranger")
            assert(#calls == 0)
        ''')

    def test_msp_requests_versioning_and_rp_fields(self):
        self.run_lua('''
            tick(.2) -- initial ?BS discovery request
            assert(sent[1][1] == "MSP" and sent[1][2] == "?BS")
            message("MSP", "?BS")
            tick(.3)
            assert(sent[2][2] == "BS1=" .. store.payload)
            tick(3)
            message("MSP", "?BS1")
            tick(.3)
            assert(sent[#sent][2] == "!BS1")
            local count = #sent
            message("MSP", "?DE" .. string.char(1) .. "?NA")
            tick(.3)
            assert(#sent == count)
            store.values.Height = .1
            BodySlider.Publish()
            assert(store.version == 2)
        ''')

    def test_fragment_reassembly_limits_and_expiry(self):
        self.run_lua('''
            local value = "BS7=" .. BodySlider.Encode({ Height = .5 })
            message("MSP" .. string.char(1), value:sub(1, 20))
            message("MSP" .. string.char(3), value:sub(21))
            assert(#calls == 37 and calls[1][3] == .5)
            calls = {}
            message("MSP" .. string.char(1), value:sub(1, 20))
            now = now + 11
            message("MSP" .. string.char(3), value:sub(21))
            assert(#calls == 0)
            message("MSP" .. string.char(1), string.rep("a", 255))
            for i = 1, 17 do message("MSP" .. string.char(2), string.rep("a", 255)) end
            message("MSP" .. string.char(3), value)
            assert(#calls == 0)
        ''')

    def test_shared_library_and_private_trp3_empty_response(self):
        self.run_lua('''
            msp = { my = { DE = "Do not change", NA = "RP Name" }, Update = function(self) self.updated = true end }
            BodySlider.Publish()
            assert(msp.my.BS == store.payload and msp.updated)
            assert(msp.my.DE == "Do not change" and msp.my.NA == "RP Name")
            tick(.2)
            local count = #sent
            message("MSP", "?BS")
            tick(.3)
            assert(#sent == count) -- shared LibMSP responds, addon does not duplicate
            msp = nil -- TRP3's MSP object is private
            message("MSP", "BS") -- its unknown-field response must not reset a body
            message("MSP", "BS1=" .. BodySlider.Encode({ Height = .1 }))
            assert(#calls == 37)
            message("MSP", "BS")
            assert(#calls == 37)
        ''')

    def test_request_throttling_and_unknown_prefix(self):
        self.run_lua('''
            tick(.2)
            for i = 1, 100 do message("MSP", "?BS") end
            tick(.3)
            tick(.3)
            assert(#sent == 2)
            message("UNRELATED", "BS1=" .. BodySlider.Encode({}))
            assert(#calls == 0)
        ''')

    def test_native_visible_player_enumeration_binds_the_sender(self):
        self.run_lua('''
            function BodySlider_Units()
                return {
                    { name = "Bob", guid = "0x0000000000000003" },
                    { name = "Invalid", guid = "0x123" },
                    { name = "Self", guid = "0x0000000000000001" },
                }
            end
            tick(2)
            message("MSP", "BS1=" .. BodySlider.Encode({ HeadSize = .3 }), "Bob")
            assert(#calls == 37)
            assert(calls[2][1] == "0x0000000000000003" and calls[2][3] == .3)
            message("MSP", "BS1=" .. BodySlider.Encode({}), "Invalid")
            message("MSP", "BS1=" .. BodySlider.Encode({}), "Self")
            assert(#calls == 37)
        ''')

    def test_ui_is_login_only_and_saved_settings_are_sanitized(self):
        self.run_lua('''
            local function object(name)
                local o = { scripts = {}, name = name, text = "", shown = true }
                function o:SetScript(event, fn) self.scripts[event] = fn end
                function o:GetName() return self.name end
                function o:SetValue(value)
                    self.value = value
                    if self.scripts.OnValueChanged then self.scripts.OnValueChanged(self, value) end
                end
                function o:GetValue() return self.value end
                function o:Hide() self.shown = false end
                function o:Show()
                    local was = self.shown
                    self.shown = true
                    if not was and self.scripts.OnShow then self.scripts.OnShow(self) end
                end
                function o:IsShown() return self.shown end
                function o:Enable() self.enabled = true end
                function o:Disable() self.enabled = false end
                function o:IsEnabled() return self.enabled ~= false and 1 or nil end
                function o:SetText(text) self.text = text end
                function o:GetText() return self.text end
                function o:HasFocus() return false end
                function o:Click() self.scripts.OnClick(self, "LeftButton") end
                function o:SetMinMaxValues(minimum, maximum) self.minimum, self.maximum = minimum, maximum end
                function o:SetUnit(unit) self.unit = unit end
                function o:CreateFontString() return object() end
                function o:CreateTexture() return object() end
                function o:GetThumbTexture() return object() end
                -- Frame methods are capitalized; any other missing field is nil, as on a real frame.
                return setmetatable(o, { __index = function(_, k) if k:match("^%u") then return function() end end end })
            end
            function CreateFrame(_, name)
                local frame = object(name)
                frames[#frames + 1] = frame
                if name then _G[name] = frame end
                return frame
            end
            function GetRealmName() return "Test realm" end
            function UnitRace() return "Human", "Human" end
            function UnitSex() return 3 end
            function PlaySound() end
            function GetCursorPosition() return 0 end
            function strtrim(s) return (s:gsub("^%s+", ""):gsub("%s+$", "")) end
            GameTooltip = object()
            function BodySlider_Supported() return true end
            function BodySlider_Names() return BodySlider.names end
            function BodySlider_HasSlider(name) return name ~= "ShoulderWidth" end
            local realSet = BodySlider_Set
            function BodySlider_Set(a, ...) if type(a) == "table" then previewed = a; return true end return realSet(a, ...) end
            SlashCmdList, UISpecialFrames, tinsert = {}, {}, table.insert
            BodySliderDB = { ["Test realm:Self"] = { values = { Height = 4, ArmGirth = .25, BreastSize = -.5 }, version = 0/0 },
                presets = { ["Human:Female"] = { { name = "Kept", code = BodySlider.Encode({ LegLength = .5 }) },
                    { name = "Broken", code = "junk" }, "not a preset", { name = ("x"):rep(40), code = BodySlider.Encode({}) } } } }
        ''')
        for part in ("Style.lua", "BodySlider.lua"):  # TOC order after Protocol.lua
            self.lua.execute((Path(__file__).parent / part).read_text(encoding="utf-8"))
        self.run_lua('''
            assert(BodySliderPanel == nil and SlashCmdList.BODYSLIDER == nil)
            frames[#frames].scripts.OnEvent()
            assert(BodySliderPanel and not BodySliderPanel:IsShown())
            assert(cleared[#cleared] == "all")
            local values = BodySliderDB["Test realm:Self"].values
            assert(values.Height == 0 and values.ArmGirth == .25 and values.BreastSize == 0)
            assert(BodySlider_BreastSize.minimum == 0 and BodySlider_ButtSize.minimum == 0)
            assert(BodySlider_Height.minimum == -1 and BodySlider_JawSize.minimum == -1)
            local saved = BodySliderDB.presets["Human:Female"]
            assert(#saved == 1 and saved[1].name == "Kept")

            SlashCmdList.BODYSLIDER()
            assert(BodySliderPanel:IsShown() and BodySliderPreview.unit == "player")
            assert(BodySlider_HeadSize.enabled and not BodySlider_ShoulderWidth.enabled)
            assert(BodySlider_ShoulderWidth.row.value.text == "n/a")
            assert(BodySlider_Height.row:IsShown() and BodySlider_ButtSize.row:IsShown())
            assert(not BodySlider_JawSize.row:IsShown())
            BodySliderPage2.scripts.OnClick()
            assert(not BodySlider_Height.row:IsShown() and BodySlider_JawSize.row:IsShown())
            assert(BodySlider_NoseLength.row:IsShown() and not BodySlider_NoseHeight.row:IsShown())
            BodySliderPage3.scripts.OnClick()
            assert(BodySlider_EarHeight.row:IsShown())
            BodySlider_EarHeight:SetValue(.4)
            assert(values.EarHeight == .4 and calls[#calls][2] == "EarHeight")
            BodySlider_EarHeight.scripts.OnMouseUp(BodySlider_EarHeight, "RightButton")
            assert(values.EarHeight == 0)

            -- The preview model asks the DLL to mirror the player's shape every frame.
            BodySliderPreview.scripts.OnUpdate(BodySliderPreview, .016)
            assert(previewed == BodySliderPreview)

            -- Presets: save, load, two-click delete that survives the periodic refresh.
            BodySliderPage4.scripts.OnClick()
            assert(not BodySlider_EarHeight.row:IsShown() and BodySliderCode.text == BodySlider.Encode(values))
            BodySlider_Height:SetValue(.3)
            BodySliderPresetName:SetText("  Tall  ")
            BodySliderPresetName.scripts.OnEnterPressed(BodySliderPresetName)
            assert(#saved == 2 and saved[2].name == "Tall" and BodySlider.Decode(saved[2].code).Height == .3)
            BodySliderPresetName.scripts.OnEnterPressed(BodySliderPresetName)
            assert(#saved == 2)
            BodySlider_Height:SetValue(-.2)
            BodySliderPreset2:Click()
            assert(values.Height == .3 and BodySlider_Height.value == .3)
            BodySliderPreset1:Click()
            assert(values.LegLength == .5 and values.Height == 0)
            BodySliderPreset1.delete:Click()
            BodySliderPanel.scripts.OnUpdate(BodySliderPanel, 1)
            assert(#saved == 2)
            BodySliderPreset1.delete:Click()
            assert(#saved == 1 and saved[1].name == "Tall")

            -- Share codes: a valid code applies everything; a bad one changes nothing.
            BodySliderCode:SetText(BodySlider.Encode({ Height = .5, NoseSize = -.25 }))
            BodySliderCode.scripts.OnEnterPressed(BodySliderCode)
            assert(values.Height == .5 and values.NoseSize == -.25 and values.LegLength == 0)
            BodySliderCode:SetText("BodySlider/2:999")
            BodySliderCode.scripts.OnEnterPressed(BodySliderCode)
            assert(values.Height == .5)

            BodySlider_HasSlider = function() return false end
            BodySliderPage1.scripts.OnClick()
            assert(not BodySlider_HeadSize.enabled)
            BodySlider_HasSlider = function() return true end
            BodySliderPanel.scripts.OnUpdate(BodySliderPanel, 1)
            assert(BodySlider_ShoulderWidth.enabled)
        ''')

    def test_v2_new_controls_and_maximum_single_packet(self):
        self.run_lua('''
            local values = {}
            assert(#BodySlider.names == 37)
            for _, name in ipairs(BodySlider.names) do values[name] = -1 end
            values.BreastSize, values.ButtSize = 1, .5
            local encoded = BodySlider.Encode(values)
            assert(encoded:sub(1, 13) == "BodySlider/2:")
            assert(#("BS1000000000=" .. encoded) <= 250)
            local decoded = BodySlider.Decode(encoded)
            assert(decoded.BreastSize == 1 and decoded.ButtSize == .5)
            assert(decoded.JawSize == -1 and decoded.EarHeight == -1)
            message("MSP", "BS4=" .. encoded)
            assert(#calls == 37 and calls[13][3] == 1 and calls[14][3] == .5)
            assert(calls[37][2] == "EarHeight" and calls[37][3] == -1)
            calls = {}
            local bad = encoded:gsub("100,50", "-1,50", 1)
            assert(BodySlider.Decode(bad) == nil)
            message("MSP", "BS5=" .. bad)
            assert(#calls == 0)
            assert(BodySlider.Decode(encoded .. ",0") == nil)
            assert(BodySlider.Decode(encoded:gsub("BodySlider/2:", "BodySlider/3:")) == nil)
        ''')

    def test_legacy_schema_migrates_and_clears_added_controls(self):
        self.run_lua('''
            message("MSP", "BS1=" .. BodySlider.Encode({ BreastSize = 1, EarHeight = .5 }))
            calls = {}
            local legacy = "BodySlider/1:25,0,0,0,0,0,0,0,0,0,-50,0"
            local decoded = BodySlider.Decode(legacy)
            assert(decoded.Height == .25 and decoded.LegLength == -.5)
            assert(decoded.BreastSize == 0 and decoded.ButtSize == 0 and decoded.EarHeight == 0)
            message("MSP", "BS2=" .. legacy)
            assert(#calls == 37 and calls[1][3] == .25 and calls[11][3] == -.5)
            for i = 13, 37 do assert(calls[i][3] == 0) end
            assert(BodySlider.Decode("BodySlider/1:" .. string.rep("0,", 36) .. "0") == nil)
            assert(BodySlider.Decode("BodySlider/2:" .. string.rep("0,", 11) .. "0") == nil)
        ''')

    def transport_client(self, name, other, guid, other_guid, mode=None, observed=None):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.globals().selfName, lua.globals().otherName = name, other
        lua.globals().selfGuid, lua.globals().otherGuid = guid, other_guid
        lua.execute('''
            now, frames, calls, sent, chats, notices, joins, filters = 100, {}, {}, {}, {}, {}, {}, {}
            channel, joinSucceeds, raid, party, observedName = 0, true, 0, 1, otherName
            UNKNOWN, UNKNOWNOBJECT = "Unknown", "Unknown object"
            ERR_CHAT_PLAYER_NOT_FOUND_S = "No player named '%s' is currently playing."
            DEFAULT_CHAT_FRAME = { AddMessage = function(_, value) notices[#notices+1] = value end }
            function CreateFrame()
                local frame = {scripts={}}
                function frame:SetScript(name, fn) self.scripts[name] = fn end
                function frame:RegisterEvent() end
                frames[#frames+1] = frame
                return frame
            end
            function GetTime() return now end
            function UnitName() return selfName end
            function UnitGUID() return selfGuid end
            function UnitIsPlayer() return false end
            function GetNumRaidMembers() return raid end
            function GetNumPartyMembers() return party end
            function BodySlider_Units() return {{ name=observedName, guid=otherGuid }} end
            function BodySlider_Set(guid, name, value) calls[#calls+1] = {guid,name,value}; return true end
            function BodySlider_Clear() end
            function SendAddonMessage(prefix,body,dist,target) sent[#sent+1]={prefix,body,dist,target} end
            function SendChatMessage(body,dist,language,id) chats[#chats+1]={body,dist,id} end
            function GetChannelName(name) assert(name == "BodySliderMSP"); return channel, name end
            function JoinChannelByName(name) joins[#joins+1] = name; if joinSucceeds then channel=8 end end
            function ChatFrame_AddMessageEventFilter(event, fn) filters[event] = fn end
            function tick(delta) now=now+delta; frames[1].scripts.OnUpdate(frames[1],delta) end
            function event(name,...) frames[1].scripts.OnEvent(frames[1],name,...) end
            function chat(body,sender,index,base)
                event("CHAT_MSG_CHANNEL",body,sender,"", "8. " .. (base or "BodySliderMSP"),"","",0,index or 8,base or "BodySliderMSP")
            end
            function addonMessage(body,sender,dist)
                event("CHAT_MSG_ADDON","MSP",body,dist or "PARTY",sender)
            end
        ''')
        lua.execute((Path(__file__).parent / "Protocol.lua").read_text())
        lua.globals().transport = mode
        if observed is not None:
            lua.globals().observedName = observed
        lua.execute('store = { values = {}, transport = transport }; BodySlider.Start(store)')
        return lua

    def test_channel_two_clients_debounce_heartbeat_and_sender_binding(self):
        alice = self.transport_client("Alice", "Bob", "0x0000000000000001", "0x0000000000000002")
        bob = self.transport_client("Bob", "Alice", "0x0000000000000002", "0x0000000000000001")
        alice.execute('store.values.BreastSize=.6; store.values.EarHeight=.3; tick(.2)')
        bob.execute('store.values.Height=.4; tick(.2)')
        alice.globals().chat(bob.globals().chats[1][1], "Bob", 8, "BodySliderMSP")
        bob.globals().chat(alice.globals().chats[1][1], "Alice", 8, "BodySliderMSP")
        alice.execute('assert(#calls==37 and calls[1][1]==otherGuid and calls[1][3]==.4); assert(#sent==0 and #joins==1)')
        bob.execute('assert(#calls==37 and calls[13][3]==.6 and calls[37][3]==.3); assert(#sent==0 and #joins==1)')
        alice.execute('''
            assert(BodySlider.GetTransport()=="channel" and store.transport=="channel")
            assert(BodySlider.Status():find("joined",1,true))
            store.values.Height=.2; tick(1); store.values.Height=.5; tick(1)
            assert(#chats==1); tick(1.1); assert(#chats==2)
            assert(#chats[2][1]<=254)
            tick(5); assert(#chats==2); tick(25.1); assert(#chats==3)
            assert(#sent==0)
        ''')

    def test_group_two_clients_deliver_without_joining_or_whispering(self):
        alice = self.transport_client("Alice", "Bob", "0x0000000000000001", "0x0000000000000002", "group")
        bob = self.transport_client("Bob", "Alice", "0x0000000000000002", "0x0000000000000001", "group")
        alice.execute('store.values.ButtSize=.8; tick(.2)')
        bob.execute('store.values.NoseLength=-.3; tick(.2)')
        alice.globals().addonMessage(bob.globals().sent[1][2], "Bob", "PARTY")
        bob.globals().addonMessage(alice.globals().sent[1][2], "Alice", "PARTY")
        alice.execute('assert(#calls==37 and calls[26][3]==-.3); assert(sent[1][3]=="PARTY" and sent[1][4]==nil)')
        bob.execute('''
            assert(#calls==37 and calls[14][3]==.8)
            assert(#joins==0 and #chats==0)
            calls={}; addonMessage("BS1="..store.payload,otherName,"WHISPER"); assert(#calls==0)
            raid=5; tick(31); assert(sent[#sent][3]=="RAID")
            party=0; raid=0; store.values.Height=.1; tick(31)
            assert(BodySlider.Status():find("waiting",1,true))
        ''')

    def test_channel_validation_filter_and_unknown_name_cache(self):
        lua = self.transport_client("Alice", "Bob", "0x0000000000000001", "0x0000000000000002")
        lua.execute('''
            msp={my={DE="Keep",BS="Other addon"},Update=function() error("must not publish LibMSP in channel") end}
            tick(.2)
            local body="[BodySliderMSP1]BS4="..BodySlider.Encode({Height=.5})
            chat(body,"Bob",7,"OtherChannel"); assert(#calls==0)
            chat(body,"Bob",8,"OtherChannel"); assert(#calls==0)
            chat(body,"Mallory",8,"BodySliderMSP"); assert(#calls==0)
            chat(body,"Unknown",8,"BodySliderMSP"); assert(#calls==0)
            chat(body,selfName,8,"BodySliderMSP"); assert(#calls==0)
            chat("plain chat","Bob",8,"BodySliderMSP"); assert(#calls==0)
            local filter=filters.CHAT_MSG_CHANNEL
            assert(filter(nil,nil,body,"Bob","","","","",0,8,"BodySliderMSP"))
            assert(not filter(nil,nil,body,"Bob","","","","",0,8,"OtherChannel"))
            assert(not filter(nil,nil,"hello","Bob","","","","",0,8,"BodySliderMSP"))
            assert(not filter(nil,nil,"[BodySliderMSP1]BS4=garbage","Bob","","","","",0,8,"BodySliderMSP"))
            assert(msp.my.BS=="Other addon" and msp.my.DE=="Keep")
            chat(body,"Bob",8,"BodySliderMSP"); assert(#calls==37 and calls[1][1]==otherGuid)
            BodySlider.SetTransport("whisper")
            msp=nil
            observedName=UNKNOWNOBJECT; tick(20); assert(#sent==0)
            observedName=""; tick(20); assert(#sent==0)
            observedName=UNKNOWN; tick(20); assert(#sent==0)
            observedName="UNKNOWN"; tick(20); assert(#sent==0)
        ''')
        # Fresh contexts prove rejected native placeholder names never enter the peer cache.
        for invalid in ("", "UNKNOWN", "Unknown", "Unknown object"):
            client = self.transport_client("Alice", "Bob", "0x0000000000000001", "0x0000000000000002", "whisper", invalid)
            client.execute('tick(20); tick(20); assert(#sent==0)')

    def test_rejections_stop_only_recent_transport_and_join_retries_are_bounded(self):
        lua = self.transport_client("Alice", "Bob", "0x0000000000000001", "0x0000000000000002")
        lua.execute('''
            tick(.2)
            event("UI_ERROR_MESSAGE",17,"You cannot speak in channels until level 10.")
            assert(BodySlider.Status():find("blocked",1,true) and #notices==1)
            for i=1,12 do store.values.Height=i/20; tick(10) end
            assert(#chats==1 and #sent==0 and #joins==1 and #notices==1)
            assert(BodySlider.SetTransport("group"))
            tick(.2); assert(#sent==1 and sent[1][3]=="PARTY")
            event("CHAT_MSG_SYSTEM","You cannot whisper until level 10.")
            assert(not BodySlider.Status():find("blocked",1,true))
            tick(5); event("CHAT_MSG_SYSTEM","You cannot whisper until level 10.")
            assert(not BodySlider.Status():find("blocked",1,true))
            assert(not BodySlider.SetTransport("invalid") and BodySlider.GetTransport()=="group")
            assert(BodySlider.SetTransport("channel")); channel=0; joinSucceeds=false
            for i=1,20 do tick(1) end
            assert(#joins==3) -- original successful join plus two attempts in this mode
            for i=1,80 do tick(1) end
            assert(#joins==4 and #sent==1)
            assert(BodySlider.Status():find("3 join attempts",1,true))
        ''')

    def test_owned_libmsp_field_unpublishes_on_switch_and_block(self):
        lua = self.transport_client("Alice", "Bob", "0x0000000000000001", "0x0000000000000002", "whisper")
        lua.execute('''
            msp={my={DE="Keep",NA="Keep"},updates=0,Update=function(self) self.updates=self.updates+1 end}
            BodySlider.Publish(); assert(msp.my.BS==store.payload and msp.updates==1)
            BodySlider.SetTransport("group")
            assert(msp.my.BS==nil and msp.updates==2 and msp.my.DE=="Keep")
            tick(.2); store.values.Height=.4; tick(4)
            assert(msp.my.BS==nil and msp.updates==2 and #sent==2)
            BodySlider.SetTransport("whisper"); tick(.2)
            assert(msp.my.BS==store.payload and msp.updates==3)
            event("CHAT_MSG_SYSTEM","You cannot whisper until level 10.")
            assert(msp.my.BS==nil and msp.updates==4 and #notices==1)
            BodySlider.SetTransport("whisper"); tick(.2)
            msp.my.BS="Foreign field"
            BodySlider.SetTransport("channel")
            assert(msp.my.BS=="Foreign field" and msp.my.DE=="Keep" and msp.my.NA=="Keep")
        ''')

    def test_channel_wire_filter_remains_narrow_after_transport_switch(self):
        lua = self.transport_client("Alice", "Bob", "0x0000000000000001", "0x0000000000000002")
        lua.execute('''
            tick(.2)
            local body="[BodySliderMSP1]BS4="..BodySlider.Encode({Height=.5})
            local filter=filters.CHAT_MSG_CHANNEL
            BodySlider.SetTransport("group")
            assert(filter(nil,nil,body,"Bob","","","","",0,8,"BodySliderMSP"))
            assert(not filter(nil,nil,"hello","Bob","","","","",0,8,"BodySliderMSP"))
            assert(not filter(nil,nil,body,"Bob","","","","",0,8,"Unrelated"))
            chat(body,"Bob",8,"BodySliderMSP"); assert(#calls==0)
            BodySlider.SetTransport("whisper")
            assert(filter(nil,nil,body,"Bob","","","","",0,8,"BodySliderMSP"))
            chat(body,"Bob",8,"BodySliderMSP"); assert(#calls==0)
        ''')

    def test_whisper_missing_player_backoff_does_not_hide_or_repeat_errors(self):
        lua = self.transport_client("Alice", "Bob", "0x0000000000000001", "0x0000000000000002", "whisper")
        lua.execute('''
            tick(.2); assert(#sent==1 and sent[1][4]=="Bob")
            event("CHAT_MSG_SYSTEM",string.format(ERR_CHAT_PLAYER_NOT_FOUND_S,"Bob"))
            for i=1,10 do tick(20) end
            assert(#sent==1 and #notices==0)
            assert(filters.CHAT_MSG_SYSTEM==nil)
            BodySlider.SetTransport("whisper"); tick(.2)
            event("CHAT_MSG_SYSTEM","You need to be level 10 to whisper other players.")
            tick(60); assert(#sent==2 and #notices==1 and BodySlider.Status():find("blocked",1,true))
        ''')


if __name__ == "__main__":
    unittest.main()
