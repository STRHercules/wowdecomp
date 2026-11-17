"""Behavior check for the mount customizer with UI mocks; does not prove native rendering."""
import sys
from pathlib import Path
from lupa.lua51 import LuaRuntime

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "EsteriaCrates"))
from test_crates import MOCKS  # noqa: E402  shared frame/texture mocks

lua = LuaRuntime(unpack_returned_tuples=True)
lua.execute(MOCKS)
lua.execute('ACCEPT, CANCEL, RESET = "Accept", "Cancel", "Reset"')
for name in ("esteria_style.lua", "EsteriaMountCustomize.lua"):
    lua.execute((HERE / name).read_text(encoding="utf-8"))
lua.execute('''
saved, refreshed = nil, false
EsteriaMountCustomize_GetDisplay = function(creature) return creature == 902 and 77 or nil end
EsteriaMountCustomize_GetOptions = function(display)
    return { { name = "Color", choices = { { id = 1, name = "Red", swatch = 16711680 }, { id = 2, name = "Blue", swatch = 255 } } } }
end
EsteriaMountCustomize_GetLook = function() return { 1 } end
EsteriaMountCustomize_Preview = function() end
EsteriaMountCustomize_Save = function(display, look) saved = look; return true end
EsteriaCosmetics_MountCustomized = function() refreshed = true end
EsteriaMountCustomize_Open(902, "Test Drake")
''')
g = lua.globals()
assert g.EsteriaMountCustomizeFrame.shown, "the customizer opens"
# step the option once with its right arrow, then accept
lua.execute('''
for _, f in ipairs(frames) do
    if rawget(f, "scripts") and f.scripts.OnClick and f.scripts.OnMouseWheel then f.scripts.OnClick(f, "LeftButton") end
end
''')
# the Accept button is the only 130x30 button
lua.execute('''
local accept
for i, f in ipairs(frames) do
    if f.scripts.OnClick and f.w == 130 and f.h == 30 then accept = f end
end
assert(accept, "Accept button exists")
accept.scripts.OnClick(accept)
''')
assert g.saved is not None and g.saved[1] == 2, "the stepped choice is saved"
assert g.refreshed, "Collections is told to re-dress its preview"
assert not g.EsteriaMountCustomizeFrame.shown, "accept closes the window"
print("EsteriaMountCustomize: all checks passed")
