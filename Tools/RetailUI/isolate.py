"""Give every global the retail files define a "Retail" prefix, consistently across their Lua and XML.

Retail's framework reuses many 3.3.5a names with different behaviour (PanelTemplates_TabResize, GameFontNormal,
UIPanelButtonTemplate, FriendsFrame ...). Loaded as shipped, it would replace the stock versions the rest of the UI
relies on, and which name wins would depend on the other installed addons. Renamed, the retail windows use only their
own copies and nothing outside them changes. Names the retail files use but do not define (C_* APIs, Enum,
Constants, events, global strings of the engine) are left alone; Compat/ provides those.

    defined = definitions(texts)          # {path: text} of every retail .lua/.xml
    text = rename_lua(text, defined)       # or rename_xml
"""
import re

PREFIX = "Retail"
# Widget methods that addons (ClassicAPI) replace on the shared widget metatables with incompatible versions; the
# retail files call Retail<Method>, which the engine (RetailSetAtlas/RetailGetAtlas) or Compat/Widgets.lua provide.
METHODS = {"SetAtlas", "GetAtlas", "SetRotation", "GetRotation", "SetNormalAtlas", "SetPushedAtlas", "SetHighlightAtlas",
           "SetDisabledAtlas", "SetCheckedAtlas", "SetDisabledCheckedAtlas", "SetStatusBarAtlas", "GetStatusBarAtlas", "ClearText",
           "CopyFontObject"}
# Shared with the engine or provided by Compat/, never renamed even if a retail file assigns them.
EXEMPT = {"Enum", "Constants", "_G", "self", "SlashCmdList", "StaticPopupDialogs", "UISpecialFrames", "UIPanelWindows"}

_LUA_DEFS = [
    re.compile(r"^function\s+([A-Za-z_]\w*)\s*[(.:]", re.M),
    re.compile(r"^([A-Za-z_]\w*)\s*=(?!=)", re.M),
    re.compile(r"^([A-Za-z_]\w*)\s*,\s*[A-Za-z_][\w\s,]*=(?!=)", re.M),
]
# Texture templates have names like "_UI-Frame-Top" and "!HelpPlateBox-Shadow-Left"; those only appear in XML.
_XML_NAME = re.compile(r'\bname="([A-Za-z_!][\w!-]*)"')
_LUA_TOKEN = re.compile(
    r'--\[(=*)\[.*?\]\1\]'            # long comment
    r'|--[^\n]*'                      # line comment
    r'|\[(=*)\[.*?\]\2\]'              # long string (left untouched)
    r'|"((?:\\.|[^"\\\n])*)"'          # "string"
    r"|'((?:\\.|[^'\\\n])*)'"          # 'string'
    r'|([A-Za-z_]\w*)', re.S)
_XML_ATTRS = ("name", "inherits", "mixin", "secureMixin", "function", "parent", "relativeTo", "text", "font", "style",
              "value", "intrinsic-base")
_XML_ATTR = re.compile(r'\b(%s)="([^"]*)"' % "|".join(_XML_ATTRS))
_XML_TAG = re.compile(r"(</?)([A-Za-z_]\w*)")
# A handler with a body; self-closing <OnLoad method="OnLoad"/> has none and stays XML.
_XML_SCRIPT = re.compile(r"(<(On\w+|Script)\b[^>]*(?<!/)>)(.*?)(</\2>)", re.S)


def definitions(texts):
    names = set()
    for path, text in texts.items():
        if path.endswith(".lua"):
            for pattern in _LUA_DEFS:
                names.update(pattern.findall(text))
        elif path.endswith(".xml"):
            names.update(_XML_NAME.findall(text))
            for _, _, body, _ in _XML_SCRIPT.findall(text):
                for pattern in _LUA_DEFS:
                    names.update(pattern.findall(body))
    keywords = {"and", "break", "do", "else", "elseif", "end", "false", "for", "function", "if", "in", "local",
                "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"}
    return {n for n in names if n not in EXEMPT and n not in keywords and not n.startswith("C_")}


def _string(value, defined, prefixes, built):
    if value in defined:
        return PREFIX + value
    # _G["FriendsFrameTab" .. i] or format("FriendsFrameTab%d", i): a name built from a renamed frame name. Other
    # strings that merely start with one (table keys such as "ButtonFrameTemplateNoPortrait") are left alone.
    if built:
        for p in prefixes:
            if value.startswith(p) and len(value) > len(p):
                return PREFIX + value
    return value


def rename_lua(text, defined, prefixes=()):
    # Child frames named "$parentRankSettingsFrame" become globals like GuildControlUIRankSettingsFrame, which code
    # then names directly: identifiers that start with a renamed frame name are renamed with it.
    child = re.compile("(?:%s)\\w" % "|".join(map(re.escape, prefixes))) if prefixes else None

    def sub(m):
        if m.group(5):
            name = m.group(5)
            before = text[max(0, m.start() - 8):m.start()].rstrip(" \t")
            member = before.endswith((".", ":")) and not before.endswith("..")
            if member:
                # Widget methods other addons replace (ClassicAPI's SetAtlas ...) get Retail-named equivalents.
                return PREFIX + name if before.endswith(":") and name in METHODS else name
            if name.startswith("C_"):
                return PREFIX + name  # private RetailC_* tables: our providers, untouched by other addons' C_*
            if re.match(r"BN[A-Z][a-z]", name):  # BNGetInfo, not BNET_LAST_ONLINE_TIME
                return PREFIX + name  # retail BN* API; 3.3.5a's own BN* functions keep their stock signatures
            # ALL_CAPS names are global strings (BNET_LAST_ONLINE_TIME), never child frames.
            derived = child and name not in defined and not name.isupper() and child.match(name)
            if derived and re.match(r"\s*=(?!=)", text[m.end():m.end() + 4]):
                return name  # "ButtonFrameTemplateNoPortrait = {" is a table key, not a child frame
            if name in defined or derived:
                return PREFIX + name
            return name
        value = m.group(3) if m.group(3) is not None else m.group(4)
        if value is not None:
            built = "%" in value or text[m.end():m.end() + 8].lstrip(" \t").startswith("..")
            quote = '"' if m.group(3) is not None else "'"
            return quote + _string(value, defined, prefixes, built) + quote
        return m.group(0)
    return _LUA_TOKEN.sub(sub, text)


def rename_xml(text, defined, prefixes=()):
    def attr(m):
        values = [v.strip() for v in m.group(2).split(",")]
        if m.group(1) == "text" or m.group(1) == "value":
            out = m.group(2) if m.group(2) not in defined else PREFIX + m.group(2)
        else:
            out = ", ".join(PREFIX + v if v in defined else v for v in values) if len(values) > 1 else \
                (PREFIX + m.group(2) if m.group(2) in defined else m.group(2))
        return f'{m.group(1)}="{out}"'

    parts = re.split(r"(<(?:On\w+|Script)\b[^>]*(?<!/)>.*?</(?:On\w+|Script)>)", text, flags=re.S)
    out = []
    for i, part in enumerate(parts):
        if i % 2:
            # the handler's attributes (function=, method=) are XML; its body is Lua
            out.append(_XML_SCRIPT.sub(lambda m: _XML_ATTR.sub(attr, m.group(1)) + rename_lua(m.group(3), defined, prefixes) + m.group(4), part))
        else:
            part = _XML_ATTR.sub(attr, part)
            part = _XML_TAG.sub(lambda m: m.group(1) + (PREFIX + m.group(2) if m.group(2) in defined else m.group(2)), part)
            out.append(part)
    return "".join(out)


def _selftest():
    lua = 'function FriendsFrame_OnLoad(self) self:FriendsFrame_OnLoad() t.FriendsFrame_OnLoad = 1 ' \
          'local x = _G["FriendsFrameTab" .. 1] print("FriendsFrame_OnLoad", \'other\') end -- FriendsFrame_OnLoad\n' \
          'FriendsFrame = nil a = b .. FriendsFrame'
    defined = definitions({"a.lua": lua, "b.xml": '<Frame name="FriendsFrame"/>'})
    assert defined == {"FriendsFrame_OnLoad", "FriendsFrame"}, defined
    out = rename_lua(lua, defined, ["FriendsFrame"])
    assert out.startswith("function RetailFriendsFrame_OnLoad(self) self:FriendsFrame_OnLoad() t.FriendsFrame_OnLoad"), out
    assert '_G["RetailFriendsFrameTab"' in out and 'print("RetailFriendsFrame_OnLoad", \'other\')' in out, out
    assert out.endswith("-- FriendsFrame_OnLoad\nRetailFriendsFrame = nil a = b .. RetailFriendsFrame"), out
    assert rename_lua("t:SetAtlas(a) t.SetAtlas = 1 C_Club.Get() x.C_Club = 2", defined) == \
        "t:RetailSetAtlas(a) t.SetAtlas = 1 RetailC_Club.Get() x.C_Club = 2"
    assert rename_lua("BNET_LAST_ONLINE_TIME BNGetInfo()", {"BNET"}, ["BNET"]) == "BNET_LAST_ONLINE_TIME RetailBNGetInfo()"
    assert rename_lua("FriendsFrameTabX:Show() x.FriendsFrameTabX = 1", defined, ["FriendsFrame"]) == \
        "RetailFriendsFrameTabX:Show() x.FriendsFrameTabX = 1"
    # A self-closing handler before a template must not swallow the XML that follows it.
    out = rename_xml('<OnShow method="OnShow"/><Button name="FriendsFrame"/><OnHide>x()</OnHide>', defined)
    assert out == '<OnShow method="OnShow"/><Button name="RetailFriendsFrame"/><OnHide>x()</OnHide>', out
    xml = '<Frame name="FriendsFrame" inherits="A, FriendsFrame" parentKey="FriendsFrame"><Scripts>' \
          '<OnLoad function="FriendsFrame_OnLoad">FriendsFrame_OnLoad(self)</OnLoad></Scripts></Frame><FriendsFrame/>'
    out = rename_xml(xml, defined, ["FriendsFrame"])
    assert out == '<Frame name="RetailFriendsFrame" inherits="A, RetailFriendsFrame" parentKey="FriendsFrame"><Scripts>' \
                  '<OnLoad function="RetailFriendsFrame_OnLoad">RetailFriendsFrame_OnLoad(self)</OnLoad></Scripts>' \
                  '</Frame><RetailFriendsFrame/>', out
    print("isolate self-test passed")


if __name__ == "__main__":
    _selftest()
