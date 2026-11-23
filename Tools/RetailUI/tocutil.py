"""Retail TOC lines as the mainline in-game client reads them."""
import re

_CONDITION = re.compile(r"\[(AllowLoad\w*)\s+([^\]]*)\]")


def toc_entry(line):
    """The file a TOC line loads in the mainline game (not glue) client, or None.

    [AllowLoad Glue|Game|Both] picks the login screens or the world; [AllowLoadGameType mainline, ...] picks the
    game flavour. [Family], [Game] and [TextLocale] are variables the client substitutes.
    """
    line = line.strip()
    if not line or line.startswith("#"):
        return None
    for kind, value in _CONDITION.findall(line):
        value = value.lower()
        if kind.lower() == "allowload" and "game" not in value and "both" not in value:
            return None
        if kind.lower() == "allowloadgametype" and not re.search(r"mainline|standard", value):
            return None
    line = line.replace("[Family]", "Mainline").replace("[Game]", "Standard").replace("[TextLocale]", "enUS")
    line = _CONDITION.sub("", line).strip()
    return line.split()[0] if line else None


def _selftest():
    assert toc_entry(r"Shared\GameFonts.xml			[AllowLoad Game]") == r"Shared\GameFonts.xml"
    assert toc_entry(r"Shared\GlueFonts.xml			[AllowLoad Glue]") is None
    assert toc_entry(r"[Family]\MenuVariants.lua     [AllowLoadGameType mainline]") == r"Mainline\MenuVariants.lua"
    assert toc_entry(r"Vanilla\MenuVariants.lua      [AllowLoadGameType vanilla, tbc, wrath]") is None
    assert toc_entry(r"[Family]\GlueFonts.xml [AllowLoad Glue][AllowLoadGameType classic]") is None
    assert toc_entry("## Title: x") is None and toc_entry("Menu.lua") == "Menu.lua"
    print("tocutil self-test passed")


if __name__ == "__main__":
    _selftest()
