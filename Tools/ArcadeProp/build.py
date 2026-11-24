"""Esteria props (arcade cabinets, casino tables, a stool): Meshy .glb at full detail -> static M2/SKIN/BLP, raw vmap
input, client display row, SQL. One entry per model in PROPS.

    python build.py <slug> <source.glb> <hull.json> <out_dir>

The model goes in untouched: Meshy's own vertices, UVs and texture (1-2M triangles). Its skin is "wide" (more than
65535 vertices, Engine/Source/Model2/M2Data.h M2SkinIsWide): sections of at most 21845 triangles, their vertices back to
back, their indices section-relative, drawn with a base vertex. hull.json is export_blender.py's dump; only its
collision hull is used (client collision, server vmap, mouse picking).

Writes into <out_dir> only. Installing (MPQ, registry rows, vmap merge, SQL) is done by install.py.
"""
import io
import os
import json
import math
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "Tools/FortniteCosmetics"))
from convert_backpacks import texture_bytes  # noqa: E402
from journey_shield import append_array  # noqa: E402
import numpy as np  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402

SCREEN_TYPE = 11  # a creature-skin replaceable slot; nothing in the client fills it for gameobjects but the Arcade extension
SCREEN_ASPECT = 1.56  # (y span) / (z span along the leaning plane); the client letterboxes every game to this
MAX_TEXTURE = int(os.environ.get("PROP_MAX_TEXTURE", 4096))  # Meshy ships 4096; tested in game 2026-11-23 (Viking)
MENU_ID, TEXT_ID = 3610001, 3610001  # the Lionheart cabinet's gossip text, which every cabinet shows

# Screens: the opening's plane x = a + b*y + c*z (WoW model axes, yards, front +X), the quad's y/z extents and how far
# in front of the plane it floats. Fitted from front-facing faces inside the opening. `flatten` (yards in front of the
# plane) pushes a curved or faceted screen's own glass behind the picture so it can't poke through (flatten_screen).
# Cabinet looks (skin > 0) are other models for the same cabinet gameobjects: a house owner unlocks them with gold
# (esteria_arcade_skin) and switches a placed cabinet's display. Their paths must end in "Arcade.m2" (the client's
# Arcade extension shows the attract screen on those). Tables and the stool are their own gameobjects and decor.
PROPS = {
    "LionheartArcade": dict(display=3610001, name="Lionheart Arcade Cabinet", height=2.2, entry=9610001,
                            screen={"plane": [0.3048, 0.0052, -0.2186], "y": [-0.28, 0.43], "z": [1.11, 1.555], "lift": 0.008}),
    "FrostboundArcade": dict(display=3610002, name="Frostbound", height=2.2, skin=1, price=2500000, 
                            screen={"plane": [0.42149, -0.0011, -0.23423], "y": [-0.3378, 0.3662], "z": [1.1278, 1.5672], "lift": 0.013}),
    "DragonfireArcade": dict(display=3610003, name="Dragonfire", height=2.2, skin=2, price=2500000, 
                            screen={"plane": [0.4527, -0.00053, -0.30891], "y": [-0.3607, 0.3847], "z": [1.078, 1.5345], "lift": 0.011}),
    "EmberleafArcade": dict(display=3610004, name="Emberleaf", height=2.2, skin=3, price=2500000,
                            screen={"plane": [0.47899, 0.02682, -0.29555], "y": [-0.3697, 0.364], "z": [1.144, 1.595],
                                    "lift": 0.008, "flatten": 0.06}),
    "PandaPawArcade": dict(display=3610005, name="Panda Paw", height=2.2, skin=4, price=2500000, 
                            screen={"plane": [0.49875, -0.00285, -0.29196], "y": [-0.3139, 0.428], "z": [1.0945, 1.551], "lift": 0.016}),
    "HordeArcade": dict(display=3610006, name="Horde War Machine", height=2.2, skin=5, price=2500000,
                        screen={"plane": [0.35697, 0.00023, -0.2928], "y": [-0.3376, 0.3389], "z": [1.1202, 1.5363], "lift": 0.008}),
    "RoyalRocketArcade": dict(display=3610007, name="Royal Rocket", height=2.2, skin=6, price=2500000,
                              screen={"plane": [0.64171, -0.06441, -0.34085], "y": [-0.3236, 0.3914], "z": [1.1251, 1.5589],
                                      "lift": 0.008, "flatten": 0.25}),
    # A game's own cabinet: pack.py's GAME_DISPLAYS gives that game's gameobject and decor this display (model_id 8)
    "GalacticBattleArcade": dict(display=3610014, name="Galactic Battle Arcade", height=2.2, game_cabinet=True,
                                 screen={"plane": [0.375, 0.0, -0.222], "y": [-0.4245, 0.3745], "z": [1.12, 1.62], "lift": 0.008}),
    "WarfrontArcade": dict(display=3610015, name="Warfront Arcade", height=2.2, game_cabinet=True,  # WarCraft: Orcs & Humans
                           screen={"plane": [0.514, 0.0, -0.325], "y": [-0.43, 0.33], "z": [1.153, 1.617], "lift": 0.008}),
    "WarcraftIIArcade": dict(display=3610016, name="Warcraft II Arcade", height=2.2, game_cabinet=True,
                             screen={"plane": [0.555, 0.0, -0.367], "y": [-0.37, 0.37], "z": [1.2075, 1.6525], "lift": 0.008}),
    "VikingArcade": dict(display=3610017, name="Viking Arcade", height=2.2, game_cabinet=True,  # The Lost Vikings
                         screen={"plane": [0.411, 0.0, -0.27], "y": [-0.32, 0.32], "z": [1.152, 1.548], "lift": 0.015}),
    "NitroSkullArcade": dict(display=3610018, name="Nitro Skull Arcade", height=2.2, game_cabinet=True,  # Rock n' Roll Racing
                             screen={"plane": [0.274, 0.0, -0.225], "y": [-0.33, 0.33], "z": [1.0935, 1.5065], "lift": 0.008}),
    "WarcraftClanArcade": dict(display=3610019, name="Warcraft Clan Arcade", height=2.2, game_cabinet=True,  # Lord of the Clans
                               screen={"plane": [0.651, 0.0, -0.422], "y": [-0.37, 0.32], "z": [1.226, 1.634], "lift": 0.008}),
    "GildedDiceTable": dict(display=3610008, name="Gilded Dice Table", height=1.05, entry=9610501, decor=3620001, model_id=2,
                            script="go_esteria_casino_dice"),
    "LionheartChessTable": dict(display=3610009, name="Lionheart Chess Table", height=1.25, entry=9610502, decor=3620002, model_id=3,
                                script="go_esteria_casino_board"),
    "LionheartBlackjackTable": dict(display=3610010, name="Lionheart Blackjack Table", height=1.15, entry=9610503, decor=3620003,
                                    model_id=4, script="go_esteria_casino_blackjack"),
    "GoldenGryphonCardTable": dict(display=3610011, name="Golden Gryphon Card Table", height=1.25, entry=9610504, decor=3620004,
                                   model_id=5, script="go_esteria_casino_poker"),
    "LionheartJackpot": dict(display=3610012, name="Lionheart Jackpot", height=1.9, entry=9610505, decor=3620005, model_id=6,
                             script="go_esteria_casino_slots"),
    "LionheartStool": dict(display=3610013, name="Lionheart Stool", height=0.6, entry=9610506, decor=3620006, model_id=7,
                           chair=1, collide=False),  # chair: GO data1, 0 low / 1 medium / 2 high seat
}


def paths(slug):
    folder = "Arcade" if slug.endswith("Arcade") else "Casino"
    return f"World\\Esteria\\{folder}\\{slug}.m2", f"World\\Esteria\\{folder}\\{slug}.blp"


def to_wow(points, scale=1.0, offset=(0.0, 0.0, 0.0)):
    # Blender front is -Y; WoW model front is +X: rotate +90 degrees about Z.
    return [((-y) * scale - offset[0], x * scale - offset[1], z * scale - offset[2]) for x, y, z in points]


def plane_x(spec, y, z):
    a, b, c = spec["plane"]
    return a + b * y + c * z


def screen_quad(spec):
    _, b, c = spec["plane"]
    n = (1.0, -b, -c)
    length = math.sqrt(sum(k * k for k in n))
    n = tuple(k / length for k in n)
    corner = lambda y, z: (plane_x(spec, y, z) + spec["lift"] / n[0], y, z)
    (y0, y1), (z0, z1) = spec["y"], spec["z"]
    # viewer's left is -Y; uv (0,0) is the picture's top-left
    return [corner(y0, z0), corner(y1, z0), corner(y1, z1), corner(y0, z1)], n


def flatten_screen(spec, V):
    """Push the model's own glass that bulges in front of the picture back behind it. Only vertices move, so the glass
    stays one piece with its bezel: nothing opens up around the picture from any angle (cutting the glass out did)."""
    (y0, y1), (z0, z1) = spec["y"], spec["z"]
    a, b, cz = spec["plane"]
    behind = a + b * V[:, 1] + cz * V[:, 2] + spec["lift"] - 0.006  # just behind the picture
    over = (V[:, 1] > y0) & (V[:, 1] < y1) & (V[:, 2] > z0) & (V[:, 2] < z1)
    depth = V[:, 0] - (a + b * V[:, 1] + cz * V[:, 2])
    move = over & (depth > -0.15) & (depth < spec["flatten"]) & (V[:, 0] > behind)
    V = V.copy()
    V[move, 0] = behind[move]
    return V


def collision(model, vertices, indices):
    tris = [indices[i:i + 3] for i in range(0, len(indices), 3)]
    normals = []
    for a, b, c in tris:
        u = [vertices[b][k] - vertices[a][k] for k in range(3)]
        v = [vertices[c][k] - vertices[a][k] for k in range(3)]
        n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        length = math.sqrt(sum(x * x for x in n)) or 1.0
        normals.append(tuple(x / length for x in n))
    model = bytearray(model)
    append_array(model, 216, len(indices), struct.pack(f"<{len(indices)}H", *indices))
    append_array(model, 224, len(vertices), b"".join(struct.pack("<3f", *v) for v in vertices))
    append_array(model, 232, len(normals), b"".join(struct.pack("<3f", *n) for n in normals))
    return bytes(model)


def raw_vmap(vertices, indices):
    """Same bytes vmap4_extractor's Model::ConvertToVMAPModel writes for an M2's collision."""
    swapped = list(indices)
    for i in range(1, len(swapped) - 1, 3):
        swapped[i], swapped[i + 1] = swapped[i + 1], swapped[i]
    out = bytearray(b"VMAP048\0")
    out += struct.pack("<II", len(vertices), 1) + bytes(12) + bytes(24) + bytes(4)
    out += b"GRP " + struct.pack("<iII", 8, 1, len(indices))
    out += b"INDX" + struct.pack("<iI", 4 + 2 * len(indices), len(indices)) + struct.pack(f"<{len(indices)}H", *swapped)
    out += b"VERT" + struct.pack("<iI", 4 + 12 * len(vertices), len(vertices))
    out += b"".join(struct.pack("<3f", *v) for v in vertices)  # extractor's two axis swaps cancel out for M2s
    return bytes(out)


def display_row(display, model, low, high):
    name = model.encode() + b"\0"
    row = struct.pack("<2I10I6fI", display, 1, *([0] * 10), *low, *high, 0)
    return b"WDBC" + struct.pack("<4I", 1, 19, 76, 1 + len(name)) + row + b"\0" + name


def q(s):
    return "'" + s.replace("\\", "\\\\").replace("'", "\\'") + "'"


def sql(slug, low, high):
    p = PROPS[slug]
    model = paths(slug)[0]
    out = [f"-- {p['name']} (wowdecomp Tools/ArcadeProp/build.py)",
           f"DELETE FROM `gameobjectdisplayinfo_dbc` WHERE `ID` = {p['display']};",
           f"INSERT INTO `gameobjectdisplayinfo_dbc` (`ID`, `ModelName`, `GeoBoxMinX`, `GeoBoxMinY`, `GeoBoxMinZ`, `GeoBoxMaxX`, "
           f"`GeoBoxMaxY`, `GeoBoxMaxZ`) VALUES ({p['display']}, {q(model)}, {low[0]:.4f}, {low[1]:.4f}, {low[2]:.4f}, "
           f"{high[0]:.4f}, {high[1]:.4f}, {high[2]:.4f});"]
    if slug == "LionheartArcade":  # the original prop: DB gossip until pack.py scripts it (type 2 questgiver, Data3 = menu)
        out += [f"DELETE FROM `gameobject_template` WHERE `entry` = {p['entry']};",
                f"INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `IconName`, `size`, `Data3`) "
                f"VALUES ({p['entry']}, 2, {p['display']}, {q(p['name'])}, 'Speak', 1, {MENU_ID});",
                f"DELETE FROM `npc_text` WHERE `ID` IN ({TEXT_ID}, {TEXT_ID + 1});",
                f"INSERT INTO `npc_text` (`ID`, `text0_0`, `text0_1`, `Probability0`) VALUES "
                f"({TEXT_ID}, {q('The cabinet hums. A golden lion roars across the screen above a blinking prompt:$B$BINSERT COIN TO PLAY')}, '', 1), "
                f"({TEXT_ID + 1}, {q('The coin rattles down into the machine. The lion lets out a pixelated roar...$B$B...and the screen flashes: OUT OF ORDER. Come back soon!')}, '', 1);",
                f"DELETE FROM `gossip_menu` WHERE `MenuID` IN ({MENU_ID}, {MENU_ID + 1});",
                f"INSERT INTO `gossip_menu` (`MenuID`, `TextID`) VALUES ({MENU_ID}, {TEXT_ID}), ({MENU_ID + 1}, {TEXT_ID + 1});",
                f"DELETE FROM `gossip_menu_option` WHERE `MenuID` IN ({MENU_ID}, {MENU_ID + 1});",
                f"INSERT INTO `gossip_menu_option` (`MenuID`, `OptionID`, `OptionIcon`, `OptionText`, `OptionType`, `OptionNpcFlag`, "
                f"`ActionMenuID`) VALUES ({MENU_ID}, 0, 0, 'Insert a coin.', 1, 1, {MENU_ID + 1}), "
                f"({MENU_ID + 1}, 0, 0, 'Try again.', 1, 1, {MENU_ID});"]
    elif p.get("game_cabinet"):
        pass  # gameobject, cabinet row and decor come from Tools/Arcade/pack.py
    elif "skin" in p:
        out += [f"DELETE FROM `esteria_arcade_skin` WHERE `id` = {p['skin']};",
                f"INSERT INTO `esteria_arcade_skin` (`id`, `display_id`, `price`, `name`) "
                f"VALUES ({p['skin']}, {p['display']}, {p['price']}, {q(p['name'])});"]
    else:
        # a casino table (type 2, scripted gossip) or the stool (type 7 chair: data0 slots, data1 seat height)
        kind, data = ("7", "`Data0`, `Data1`") if "chair" in p else ("2", "`Data3`")
        values = f"1, {p['chair']}" if "chair" in p else "0"
        out += [f"DELETE FROM `gameobject_template` WHERE `entry` = {p['entry']};",
                f"INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `IconName`, `size`, {data}, `ScriptName`) "
                f"VALUES ({p['entry']}, {kind}, {p['display']}, {q(p['name'])}, {q('' if 'chair' in p else 'Speak')}, 1, {values}, "
                f"{q(p.get('script', ''))});",
                # housing decor; model_fdid 0x40000000 | n = Extensions/Housing's custom model n; category 10 = Arcade tab
                f"DELETE FROM `housing_decor_catalog` WHERE `decor_id` = {p['decor']};",
                f"INSERT INTO `housing_decor_catalog` (`decor_id`, `model_fdid`, `display_id`, `gameobject_entry`, `name`, `category`, "
                f"`surface`, `budget_cost`, `min_scale`, `max_scale`, `origin`, `enabled`) VALUES ({p['decor']}, "
                f"{0x40000000 | p['model_id']}, {p['display']}, {p['entry']}, {q(p['name'])}, 10, 0, 1, 0.5, 1.5, 'custom', 1);"]
    return "\n".join(out) + "\n"


def read_glb(path):
    """Meshy .glb -> Blender-axis (as export_blender.py's dump) positions, normals, top-down UVs, triangles, texture."""
    data = path.read_bytes()
    size = struct.unpack_from("<I", data, 12)[0]
    doc = json.loads(data[20:20 + size])
    blob = data[20 + size + 8:]
    assert len(doc["meshes"]) == 1 and len(doc["meshes"][0]["primitives"]) == 1
    assert all(n.get("matrix", [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]) == [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
               and not {"rotation", "scale", "translation"} & n.keys() for n in doc["nodes"])
    kinds = {5126: np.float32, 5125: np.uint32, 5123: np.uint16}
    widths = {"SCALAR": 1, "VEC2": 2, "VEC3": 3}

    def accessor(i):
        a = doc["accessors"][i]
        view = doc["bufferViews"][a["bufferView"]]
        dtype, width = kinds[a["componentType"]], widths[a["type"]]
        assert view.get("byteStride") in (None, width * np.dtype(dtype).itemsize)
        at = view.get("byteOffset", 0) + a.get("byteOffset", 0)
        return np.frombuffer(blob, dtype, a["count"] * width, at).reshape(a["count"], width) if width > 1 else \
            np.frombuffer(blob, dtype, a["count"], at)

    prim = doc["meshes"][0]["primitives"][0]
    gl = accessor(prim["attributes"]["POSITION"]).astype(np.float64)
    gn = accessor(prim["attributes"]["NORMAL"]).astype(np.float64)
    uv = accessor(prim["attributes"]["TEXCOORD_0"]).astype(np.float32)  # glTF v already runs top-down, as BLP
    tris = accessor(prim["indices"]).astype(np.int64).reshape(-1, 3)
    image = doc["images"][doc["textures"][doc["materials"][prim["material"]]["pbrMetallicRoughness"]["baseColorTexture"]["index"]]["source"]]
    view = doc["bufferViews"][image["bufferView"]]
    texture = Image.open(io.BytesIO(blob[view.get("byteOffset", 0):view.get("byteOffset", 0) + view["byteLength"]])).convert("RGB")
    blender = lambda a: np.stack([a[:, 0], -a[:, 2], a[:, 1]], 1)  # glTF Y-up -> Blender Z-up
    return blender(gl), blender(gn), uv, tris, texture


def wow_axes(a):
    """to_wow for arrays: Blender front -Y -> WoW +X."""
    return np.stack([-a[:, 1], a[:, 0], a[:, 2]], 1)


def morton_order(V, tris):
    """Triangles ordered along a Z curve through space, so each section's block of them shares most of its vertices."""
    c = V[tris].mean(1)
    q = ((c - c.min(0)) / max((c.max(0) - c.min(0)).max(), 1e-9) * 1023).astype(np.uint64)

    def spread(x):
        x = (x | (x << 16)) & 0x030000FF
        x = (x | (x << 8)) & 0x0300F00F
        x = (x | (x << 4)) & 0x030C30C3
        return (x | (x << 2)) & 0x09249249

    return tris[np.argsort(spread(q[:, 0]) | (spread(q[:, 1]) << 1) | (spread(q[:, 2]) << 2), kind="stable")]


SECTION_TRIS = 65535 // 3  # a section's index count is 16-bit, so each section also has at most 65535 vertices


def write_wide_model(name, V, N, UV, groups, textures):
    """groups: (texture index, triangles) in draw order. Returns the M2 and its skin; the skin is wide when it has more
    than 65535 vertices (Engine M2Data.h M2SkinIsWide), else the classic layout (sections still back to back)."""
    sections = []  # (texture, global vertex ids, local indices)
    for texture, tris in groups:
        for at in range(0, len(tris), SECTION_TRIS):
            block = tris[at:at + SECTION_TRIS]
            ids, local = np.unique(block.ravel(), return_inverse=True)
            sections.append((texture, ids, local.astype(np.uint16)))
    order = np.concatenate([ids for _, ids, _ in sections])
    total = len(order)
    wide = total > 0xFFFF

    vertex = np.dtype([("pos", "<f4", 3), ("weights", "u1", 4), ("bones", "u1", 4), ("normal", "<f4", 3),
                       ("uv", "<f4", 2), ("uv2", "<f4", 2)])
    assert vertex.itemsize == 48
    buf = np.zeros(total, vertex)
    buf["pos"], buf["normal"], buf["uv"], buf["uv2"] = V[order], N[order], UV[order], UV[order]
    buf["weights"][:, 0] = 255

    low, high = V[order].min(0), V[order].max(0)
    radius = float(np.sqrt((V[order] ** 2).sum(1)).max())
    bounds = struct.pack("<7f", *low, *high, radius)
    count = len(textures)
    model, skin = bytearray(304), bytearray(48)
    struct.pack_into("<4sI", model, 0, b"MD20", 264)
    label = name.encode() + b"\0"
    append_array(model, 8, len(label), label)
    model[160:188] = bounds
    model[188:216] = bounds
    sequence = bytearray(64)
    struct.pack_into("<HHIfII", sequence, 0, 0, 0, 1000, 0, 0x20, 32767)
    sequence[32:60] = bounds
    struct.pack_into("<HH", sequence, 60, 65535, 0)
    append_array(model, 28, 1, sequence)
    append_array(model, 36, 1, struct.pack("<H", 0))
    bone = bytearray(88)
    struct.pack_into("<iIHHI", bone, 0, -1, 0, 65535, 0, 0)
    for offset in (16, 36, 56):
        struct.pack_into("<HH", bone, offset, 0, 65535)
    append_array(model, 44, 1, bone)
    append_array(model, 60, total, buf.tobytes())
    struct.pack_into("<I", model, 68, 1)
    append_array(model, 80, count, b"".join(struct.pack("<II8x", kind, 0) for kind, _ in textures))
    table = struct.unpack_from("<I", model, 84)[0]
    for i, (_, path) in enumerate(textures):
        value = path.encode() + b"\0"
        append_array(model, table + 16 * i + 8, len(value), value)
    append_array(model, 88, 1, struct.pack("<HH4I", 0, 65535, 0, 0, 0, 0))
    track = struct.unpack_from("<I", model, 92)[0]
    for field, fmt, value in ((4, "I", 0), (12, "h", 32767)):
        append_array(model, track + field, 1, bytes(8))
        outer = struct.unpack_from("<I", model, track + field + 4)[0]
        append_array(model, outer, 1, struct.pack("<" + fmt, value))
    # materials: the screen's (texture type SCREEN_TYPE) is unlit and two-sided, the rest opaque defaults
    append_array(model, 112, count, b"".join(struct.pack("<HH", 0x05 if kind == SCREEN_TYPE else 0, 0) for kind, _ in textures))
    for header, values in ((120, [0]), (128, list(range(count))), (136, [0] * count), (144, [0] * count), (152, [65535] * count)):
        append_array(model, header, len(values), struct.pack("<" + "H" * len(values), *values))

    skin[:4] = b"SKIN"
    append_array(skin, 4, total, (np.arange(total) & 0xFFFF).astype("<u2").tobytes())  # identity (ignored when wide)
    indices = np.concatenate([local + (0 if wide else first) for (_, _, local), first in
                              zip(sections, np.cumsum([0] + [len(ids) for _, ids, _ in sections[:-1]]))])
    assert indices.max() <= 0xFFFF
    append_array(skin, 12, len(indices), indices.astype("<u2").tobytes())
    append_array(skin, 20, total, bytes(4 * total))
    records, batches = [], []
    first_vertex = first_index = 0
    for i, (texture, ids, local) in enumerate(sections):
        pts = V[ids]
        centre = (pts.min(0) + pts.max(0)) / 2
        sort_radius = float(np.sqrt(((pts - centre) ** 2).sum(1)).max())
        # id's high word: the index start's high bits (M2WideIndexStart); vertex start keeps its low 16 bits
        records.append(struct.pack("<I8H7f", 1502 | (first_index >> 16 << 16), first_vertex & 0xFFFF if wide else first_vertex,
                                   len(ids), first_index & 0xFFFF, len(local), 1, 0, 1, 0, *centre, *centre, sort_radius))
        batches.append(struct.pack("<BB11H", 0, 0, 0, i, i, 65535, texture, 0, 1, texture, texture, texture, texture))
        first_vertex += len(ids)
        first_index += len(local)
    append_array(skin, 28, len(records), b"".join(records))
    append_array(skin, 36, len(batches), b"".join(batches))
    struct.pack_into("<I", skin, 44, 1)
    return bytes(model), bytes(skin), len(sections), wide


def main():
    slug, source, hull_path, out = sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4])
    p = PROPS[slug]
    assert "screen" not in p or p["screen"], f"{slug}: screen not fitted yet"
    model_path, texture_name = paths(slug)
    Vb, Nb, UV, tris, image = read_glb(source)
    hull_dump = json.loads(hull_path.read_text())

    raw = wow_axes(Vb)
    low, high = raw.min(0), raw.max(0)
    scale = p["height"] / (high[2] - low[2])
    offset = np.array([(low[0] + high[0]) / 2 * scale, (low[1] + high[1]) / 2 * scale, low[2] * scale])
    V = raw * scale - offset
    N = wow_axes(Nb)
    N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-9)
    hull = [tuple(v) for v in (wow_axes(np.array(hull_dump["collision_vertices"])) * scale - offset)]
    hull_indices = hull_dump["collision_indices"]
    assert len(hull) < 65536 and max(hull_indices) < len(hull)

    spec = p.get("screen")
    if spec and "flatten" in spec:
        V = flatten_screen(spec, V)
    groups = [(0, morton_order(V, tris))]
    textures = [(0, texture_name)]
    if spec:
        # The in-world screen: one quad over the opening, its own replaceable texture (type SCREEN_TYPE) that the
        # client's Arcade extension points at the live game (or an attract screen), drawn unlit and two-sided.
        corners, quad_normal = screen_quad(spec)
        base = len(V)
        V = np.vstack([V, corners])
        N = np.vstack([N, [quad_normal] * 4])
        UV = np.vstack([UV, [(0, 1), (1, 1), (1, 0), (0, 0)]]).astype(np.float32)
        groups.append((1, np.array([[base, base + 1, base + 2], [base, base + 2, base + 3]])))
        textures.append((SCREEN_TYPE, texture_name[:-4] + "Screen.blp"))  # path unused for a replaceable slot
    model, skin, sections, wide = write_wide_model(slug, V, N, UV, groups, textures)
    if p.get("collide", True):
        model = collision(model, hull, hull_indices)

    out.mkdir(parents=True, exist_ok=True)
    if max(image.size) > MAX_TEXTURE:  # anything larger than the client has been shown to load
        image = image.resize((min(image.width, MAX_TEXTURE), min(image.height, MAX_TEXTURE)), Image.LANCZOS)
    png = out / "texture.png"
    image.save(png)
    used = np.unique(np.concatenate([g.ravel() for _, g in groups]))
    low, high = V[used].min(0).tolist(), V[used].max(0).tolist()
    files = {model_path: model, model_path[:-3] + "00.skin": skin, texture_name: texture_bytes(png)}
    (out / "files.json").write_text(json.dumps({k: (out.resolve() / "patch-root" / k.replace("\\", "/")).as_posix() for k in files}))
    for name, data in files.items():
        dest = out / "patch-root" / name.replace("\\", "/")
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
    if p.get("collide", True):
        vmap = out / "vmap-src"
        vmap.mkdir(exist_ok=True)
        (vmap / "dir_bin").write_bytes(b"")
        plain = f"{slug}.m2".encode()
        (vmap / "temp_gameobject_models").write_bytes(b"VMAP048\0" + struct.pack("<IBI", p["display"], 0, len(plain)) + plain)
        (vmap / plain.decode()).write_bytes(raw_vmap(hull, hull_indices))
    (out / "gameobjectdisplayinfo-row.dbc").write_bytes(display_row(p["display"], model_path, low, high))
    (out / "prop.sql").write_text(sql(slug, low, high))
    print(json.dumps({"slug": slug, "verts": int(struct.unpack_from("<I", model, 60)[0]), "tris": int(sum(len(g) for _, g in groups)),
                      "sections": sections, "wide": wide, "collision_tris": len(hull_indices) // 3,
                      "size": [round(high[k] - low[k], 3) for k in range(3)], "model_mb": round(len(model) / 2**20, 1),
                      "skin_mb": round(len(skin) / 2**20, 1)}))


if __name__ == "__main__":
    main()
