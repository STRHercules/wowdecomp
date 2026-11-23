"""Lionheart Arcade prop: Blender mesh dump -> static M2/SKIN/BLP, patch MPQ, raw vmap input, client display row, SQL.

    python build.py <mesh.json> <texture.png> <out_dir>

Writes into <out_dir> only. Installing (MPQ, registry row, vmap merge, SQL) is done by install.py.
"""
import json
import math
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "Tools/FortniteCosmetics"))
from convert_backpacks import texture_bytes, verify, write_model  # noqa: E402
from journey_shield import append_array  # noqa: E402
import numpy as np  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402

DISPLAY_ID, GO_ENTRY, MENU_ID, TEXT_ID = 3610001, 9610001, 3610001, 3610001
NAME = "Lionheart Arcade Cabinet"
MODEL = "World\\Esteria\\Arcade\\LionheartArcade.m2"
TEXTURE = "World\\Esteria\\Arcade\\LionheartArcade.blp"
HEIGHT = 2.2  # yards; a human character is about 2


def to_wow(points, scale=1.0, offset=(0.0, 0.0, 0.0)):
    # Blender front is -Y; WoW model front is +X: rotate +90 degrees about Z.
    return [((-y) * scale - offset[0], x * scale - offset[1], z * scale - offset[2]) for x, y, z in points]


SCREEN_TYPE = 11  # a creature-skin replaceable slot; nothing in the client fills it for gameobjects but the Arcade extension
SCREEN_TEXTURE = "World\\Esteria\\Arcade\\LionheartArcadeScreen.blp"  # unused for a replaceable slot, kept for tools
# Measured on the 96.8k-triangle build (front-facing faces whose texels are black, inside the bezel): the opening's
# plane is x = 0.3048 + 0.0052 y - 0.2186 z (leans back ~12 degrees). Inset so no bezel vertex pokes through.
SCREEN_Y, SCREEN_Z, SCREEN_LIFT = (-0.28, 0.43), (1.11, 1.555), 0.008
SCREEN_ASPECT = 1.56  # (y span) / (z span along the leaning plane); the client letterboxes games to this


def screen_quad():
    plane = lambda y, z: 0.3048 + 0.0052 * y - 0.2186 * z
    n = (1.0, -0.0052, 0.2186)
    length = math.sqrt(sum(c * c for c in n))
    n = tuple(c / length for c in n)
    corner = lambda y, z: (plane(y, z) + SCREEN_LIFT / n[0], y, z)
    (y0, y1), (z0, z1) = SCREEN_Y, SCREEN_Z
    # viewer's left is -Y; uv (0,0) is the picture's top-left
    return [corner(y0, z0), corner(y1, z0), corner(y1, z1), corner(y0, z1)], n


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


def pad_islands(image, uvs, indices, passes=64):
    """Grow each UV island's colour into the dark gutters so mip levels never sample the gap colour."""
    size = image.width
    mask = Image.new("L", image.size, 0)
    draw = ImageDraw.Draw(mask)
    for i in range(0, len(indices), 3):
        draw.polygon([(uvs[j][0] * size, uvs[j][1] * size) for j in indices[i:i + 3]], fill=255, outline=255)
    color = np.asarray(image, dtype=np.float32)
    known = np.asarray(mask) > 0
    covered = known.mean()
    for _ in range(passes):
        if known.all():
            break
        acc, hits = np.zeros_like(color), np.zeros(known.shape, np.float32)
        for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (-1, 1), (1, -1), (1, 1)):
            k = np.roll(known, (dy, dx), (0, 1))
            acc += np.roll(color, (dy, dx), (0, 1)) * k[..., None]
            hits += k
        grow = ~known & (hits > 0)
        color[grow] = acc[grow] / hits[grow][:, None]
        known |= grow
    return Image.fromarray(color.clip(0, 255).astype(np.uint8)), covered


def display_row(low, high):
    name = MODEL.encode() + b"\0"
    row = struct.pack("<2I10I6fI", DISPLAY_ID, 1, *([0] * 10), *low, *high, 0)
    return b"WDBC" + struct.pack("<4I", 1, 19, 76, 1 + len(name)) + row + b"\0" + name


def sql(low, high):
    q = lambda s: "'" + s.replace("\\", "\\\\").replace("'", "\\'") + "'"
    return f"""-- {NAME}: DB-only gossip prop (type 2 questgiver, Data3 = gossip menu).
DELETE FROM `gameobjectdisplayinfo_dbc` WHERE `ID` = {DISPLAY_ID};
INSERT INTO `gameobjectdisplayinfo_dbc` (`ID`, `ModelName`, `GeoBoxMinX`, `GeoBoxMinY`, `GeoBoxMinZ`, `GeoBoxMaxX`, `GeoBoxMaxY`, `GeoBoxMaxZ`)
VALUES ({DISPLAY_ID}, {q(MODEL)}, {low[0]:.4f}, {low[1]:.4f}, {low[2]:.4f}, {high[0]:.4f}, {high[1]:.4f}, {high[2]:.4f});
DELETE FROM `gameobject_template` WHERE `entry` = {GO_ENTRY};
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `IconName`, `size`, `Data3`)
VALUES ({GO_ENTRY}, 2, {DISPLAY_ID}, {q(NAME)}, 'Speak', 1, {MENU_ID});
DELETE FROM `npc_text` WHERE `ID` IN ({TEXT_ID}, {TEXT_ID + 1});
INSERT INTO `npc_text` (`ID`, `text0_0`, `text0_1`, `Probability0`) VALUES
({TEXT_ID}, {q("The cabinet hums. A golden lion roars across the screen above a blinking prompt:$B$BINSERT COIN TO PLAY")}, '', 1),
({TEXT_ID + 1}, {q("The coin rattles down into the machine. The lion lets out a pixelated roar...$B$B...and the screen flashes: OUT OF ORDER. Come back soon!")}, '', 1);
DELETE FROM `gossip_menu` WHERE `MenuID` IN ({MENU_ID}, {MENU_ID + 1});
INSERT INTO `gossip_menu` (`MenuID`, `TextID`) VALUES ({MENU_ID}, {TEXT_ID}), ({MENU_ID + 1}, {TEXT_ID + 1});
DELETE FROM `gossip_menu_option` WHERE `MenuID` IN ({MENU_ID}, {MENU_ID + 1});
INSERT INTO `gossip_menu_option` (`MenuID`, `OptionID`, `OptionIcon`, `OptionText`, `OptionType`, `OptionNpcFlag`, `ActionMenuID`) VALUES
({MENU_ID}, 0, 0, 'Insert a coin.', 1, 1, {MENU_ID + 1}),
({MENU_ID + 1}, 0, 0, 'Try again.', 1, 1, {MENU_ID});
"""


def main():
    mesh_path, texture_path, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    mesh = json.loads(mesh_path.read_text())
    raw = to_wow(mesh["vertices"])
    low = [min(v[k] for v in raw) for k in range(3)]
    high = [max(v[k] for v in raw) for k in range(3)]
    scale = HEIGHT / (high[2] - low[2])
    offset = ((low[0] + high[0]) / 2 * scale, (low[1] + high[1]) / 2 * scale, low[2] * scale)
    vertices = to_wow(mesh["vertices"], scale, offset)
    normals = to_wow(mesh["normals"])
    hull = to_wow(mesh["collision_vertices"], scale, offset)
    indices = mesh["indices"]
    assert len(vertices) < 65536 and max(indices) < len(vertices)
    assert len(hull) < 65536 and max(mesh["collision_indices"]) < len(hull)

    per = 65535 // 3  # one skin section draws at most 65535 indices; wide index starts chain them
    sections = [{"first_index": f * 3, "faces": min(per, len(indices) // 3 - f)} for f in range(0, len(indices) // 3, per)]
    # The in-world screen: one quad over the black opening, its own replaceable texture (type SCREEN_TYPE) that the
    # client's Arcade extension points at the live game (or an attract screen), drawn unlit and two-sided.
    base = len(vertices)
    quad, quad_normal = screen_quad()
    model, skin, size = write_model({"vertices": vertices + quad, "normals": normals + [quad_normal] * 4,
                                     "uvs": mesh["uvs"] + [(0, 1), (1, 1), (1, 0), (0, 0)],
                                     "indices": indices + [base, base + 1, base + 2, base, base + 2, base + 3],
                                     "materials": sections + [{"first_index": len(indices), "faces": 2}],
                                     "source_name": "LionheartArcade"}, [TEXTURE] * len(sections) + [SCREEN_TEXTURE])
    model = bytearray(model)
    textures = struct.unpack_from("<I", model, 84)[0]
    struct.pack_into("<II", model, textures + 16 * len(sections), SCREEN_TYPE, 0)
    materials = struct.unpack_from("<I", model, 116)[0]
    struct.pack_into("<HH", model, materials + 4 * len(sections), 0x01 | 0x04, 0)  # unlit, two-sided, opaque
    model = bytes(model)
    verify(model, skin)
    model = collision(model, hull, mesh["collision_indices"])
    verify(model, skin)

    image, covered = pad_islands(Image.open(texture_path).convert("RGB"), mesh["uvs"], indices)
    png = out / "texture-padded.png"
    out.mkdir(parents=True, exist_ok=True)
    image.save(png)

    low = [min(v[k] for v in vertices) for k in range(3)]
    high = [max(v[k] for v in vertices) for k in range(3)]
    files = {MODEL: model, MODEL[:-3] + "00.skin": skin, TEXTURE: texture_bytes(png)}
    (out / "files.json").write_text(json.dumps({k: (out / "patch-root" / k.replace("\\", "/")).as_posix() for k in files}))
    for name, data in files.items():
        dest = out / "patch-root" / name.replace("\\", "/")
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
    vmap = out / "vmap-src"
    vmap.mkdir(exist_ok=True)
    (vmap / "dir_bin").write_bytes(b"")
    plain = b"LionheartArcade.m2"
    (vmap / "temp_gameobject_models").write_bytes(b"VMAP048\0" + struct.pack("<IBI", DISPLAY_ID, 0, len(plain)) + plain)
    (vmap / plain.decode()).write_bytes(raw_vmap(hull, mesh["collision_indices"]))
    (out / "gameobjectdisplayinfo-row.dbc").write_bytes(display_row(low, high))
    (out / "arcade.sql").write_text(sql(low, high))
    print(json.dumps({"verts": len(vertices), "tris": len(indices) // 3, "collision_tris": len(mesh["collision_indices"]) // 3,
                      "sections": len(sections), "uv_coverage": round(float(covered), 3), "size": [round(high[k] - low[k], 3) for k in range(3)], "model_bytes": len(model)}))


if __name__ == "__main__":
    main()
