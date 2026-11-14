"""Build the supplied Journey Mentor Shield as a rigid Wrath cloak cosmetic.

This deliberately accepts only that mesh: Unreal materials and arbitrary skeletal assets need their own conversion.
Raw game assets and output stay outside Git. Nothing is installed by this script.
"""
import argparse
import hashlib
import io
import json
import math
from pathlib import Path
import struct
import sys

from PIL import Image
import zstandard

REPO = Path(__file__).resolve().parents[2]
SOURCE = Path("G:/FortnitePorter/Exported/BRCosmetics/Accessories/FORT_Backpacks/Backpack_F_MED_Journey_Mentor_Shield")
MODEL_PATH = "custom\\fortnite\\journey_mentor_shield.m2"
TEXTURE_PATH = "custom\\fortnite\\journey_mentor_shield.blp"


class Reader:
    def __init__(self, data):
        self.data = io.BytesIO(data)

    def read(self, size):
        if size < 0:
            raise ValueError("Negative size")
        data = self.data.read(size)
        if len(data) != size:
            raise ValueError("Truncated UEFORMAT payload")
        return data

    def unpack(self, fmt):
        return struct.unpack("<" + fmt, self.read(struct.calcsize("<" + fmt)))

    def integer(self):
        return self.unpack("i")[0]

    def string(self):
        return self.read(self.integer()).rstrip(b"\0").decode("utf-8")

    def attributes(self):
        result = {}
        count = self.integer()
        if not 0 <= count <= 100:
            raise ValueError("Invalid attribute count")
        for _ in range(count):
            name = self.string()
            if name in result:
                raise ValueError("Duplicate attribute")
            result[name] = Reader(self.read(self.integer()))
        return result


def source_mesh(data):
    r = Reader(data)
    if r.read(8) != b"UEFORMAT" or r.string() != "UEMODEL" or r.unpack("B")[0] != 10:
        raise ValueError("Expected UEFORMAT UEMODEL version 10")
    if r.string() != "F_MED_Journey_Mentor_Shield_Pack":
        raise ValueError("This trial supports only Journey Mentor Shield")
    source_path = r.string()
    if r.unpack("B")[0] != 1 or r.string() != "ZSTD":
        raise ValueError("Expected ZSTD compression")
    size, packed = r.unpack("ii")
    if not 0 < size <= 16 * 1024 * 1024:
        raise ValueError("Invalid decompressed size")
    raw = zstandard.ZstdDecompressor().decompress(r.read(packed), max_output_size=size)
    if len(raw) != size or r.data.read(1):
        raise ValueError("Invalid payload size")
    attrs = Reader(raw).attributes()
    lods = attrs["LODS"]
    if lods.integer() != 4 or lods.string() != "LOD0":
        raise ValueError("Unexpected detail levels")
    lod = lods.attributes()
    def vectors(name, width):
        ar = lod[name]
        count = ar.integer()
        if not 0 < count <= 65535:
            raise ValueError("Invalid vertex count")
        return [ar.unpack("f" * width) for _ in range(count)]
    vertices, normals = vectors("VERTICES", 3), vectors("NORMALS", 4)
    uv = lod["TEXCOORDS"]
    if uv.integer() < 1:
        raise ValueError("Missing UVs")
    uv.string()
    if uv.integer() != len(vertices):
        raise ValueError("UV count mismatch")
    uvs = [uv.unpack("ff") for _ in vertices]
    indices = lod["INDICES"]
    count = indices.integer()
    if not 0 < count <= 65535 or count % 3:
        raise ValueError("Invalid triangle array")
    triangles = [indices.unpack("III") for _ in range(count // 3)]
    if max(max(t) for t in triangles) >= len(vertices) or len(normals) != len(vertices):
        raise ValueError("Geometry indices/counts mismatch")
    weights = lod["WEIGHTS"]
    if weights.integer() != len(vertices):
        raise ValueError("Expected rigid per-vertex weights")
    seen = set()
    for _ in vertices:
        bone, vertex, weight = weights.unpack("Hif")
        if bone != 1 or weight != 1 or not 0 <= vertex < len(vertices) or vertex in seen:
            raise ValueError("Expected all vertices rigidly weighted to pack bone")
        seen.add(vertex)
    materials = lod["MATERIALS"]
    if materials.integer() != 1:
        raise ValueError("Expected one material")
    material, material_path = materials.string(), materials.string()
    if materials.unpack("ii") != (0, len(triangles)):
        raise ValueError("Material range mismatch")
    if not all(math.isfinite(x) for row in vertices + normals + uvs for x in row):
        raise ValueError("Non-finite geometry")
    # UE left-handed X lateral/Y depth -> WoW right-handed X forward/Y lateral.
    # Put the top of the rigid shield at the cloak's upper edge. UVs stay in texture-file space.
    top = max(v[2] for v in vertices)
    vertices = [(v[1] / 100, v[0] / 100, (v[2] - top) / 100) for v in vertices]
    normals = [(n[2], n[1], n[3]) for n in normals]
    # This export already carries reverse winding in UE space; the axis swap makes it match WoW.
    for a, b, c in triangles:
        ab = [vertices[b][k] - vertices[a][k] for k in range(3)]
        ac = [vertices[c][k] - vertices[a][k] for k in range(3)]
        cross = (ab[1]*ac[2]-ab[2]*ac[1], ab[2]*ac[0]-ab[0]*ac[2], ab[0]*ac[1]-ab[1]*ac[0])
        if sum(cross[k]*sum(normals[i][k] for i in (a,b,c)) for k in range(3)) <= 0:
            raise ValueError("Triangle winding disagrees with the exported normals")
    return vertices, normals, uvs, triangles, {"source_path": source_path, "material": material, "material_path": material_path}


def append_array(buf, header, count, payload):
    buf.extend(b"\0" * (-len(buf) % 16))
    struct.pack_into("<II", buf, header, count, len(buf))
    buf.extend(payload)


def model_bytes(vertices, normals, uvs, triangles):
    model, skin = bytearray(304), bytearray(48)
    struct.pack_into("<4sI", model, 0, b"MD20", 264)
    append_array(model, 8, 20, b"JourneyMentorShield\0\0")
    low = [min(v[k] for v in vertices) for k in range(3)]
    high = [max(v[k] for v in vertices) for k in range(3)]
    radius = max(math.sqrt(sum(x*x for x in v)) for v in vertices)
    bounds = struct.pack("<7f", *low, *high, radius)
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
    records = b"".join(struct.pack("<3f4B4B3f4f", *v, 255,0,0,0, 0,0,0,0, *n, *uv, *uv)
                       for v, n, uv in zip(vertices, normals, uvs))
    append_array(model, 60, len(vertices), records)
    struct.pack_into("<I", model, 68, 1)
    texture = TEXTURE_PATH.encode() + b"\0"
    append_array(model, 80, 1, bytes(16))
    texture_header = struct.unpack_from("<I", model, 84)[0]
    append_array(model, texture_header + 8, len(texture), texture)
    track = struct.pack("<HH4I", 0, 65535, 0, 0, 0, 0)
    append_array(model, 88, 1, track)
    track_offset = struct.unpack_from("<I", model, 92)[0]
    append_array(model, track_offset + 4, 1, bytes(8))
    times = struct.unpack_from("<I", model, track_offset + 8)[0]
    append_array(model, times, 1, struct.pack("<I", 0))
    append_array(model, track_offset + 12, 1, bytes(8))
    values = struct.unpack_from("<I", model, track_offset + 16)[0]
    append_array(model, values, 1, struct.pack("<h", 32767))
    append_array(model, 112, 1, struct.pack("<HH", 0, 0))  # lit, opaque
    for header, value in ((120, 0), (128, 0), (136, 0), (144, 0), (152, 65535)):
        append_array(model, header, 1, struct.pack("<H", value))
    skin[:4] = b"SKIN"
    append_array(skin, 4, len(vertices), struct.pack("<" + "H"*len(vertices), *range(len(vertices))))
    flat = [i for triangle in triangles for i in triangle]
    append_array(skin, 12, len(flat), struct.pack("<" + "H"*len(flat), *flat))
    append_array(skin, 20, len(vertices), bytes(4 * len(vertices)))
    section = struct.pack("<I8H7f", 1502, 0, len(vertices), 0, len(flat), 1, 0, 1, 0,
                          *((low[k]+high[k])/2 for k in range(3)),
                          *((low[k]+high[k])/2 for k in range(3)), radius)
    append_array(skin, 28, 1, section)
    batch = struct.pack("<BB11H", 0, 0, 0, 0, 0, 65535, 0, 0, 1, 0, 0, 0, 0)
    append_array(skin, 36, 1, batch)
    struct.pack_into("<I", skin, 44, 1)
    return bytes(model), bytes(skin)


def texture_bytes(path):
    image = Image.open(path).convert("RGBA")
    # Fortnite's albedo alpha is material data, not cutout coverage, for this opaque shield.
    image.putalpha(255)
    blob = bytearray(1172)
    struct.pack_into("<4sI4BII", blob, 0, b"BLP2", 1, 3, 8, 8, 1, *image.size)
    for mip in range(16):
        pixels = image.tobytes("raw", "BGRA")
        struct.pack_into("<I", blob, 20 + 4*mip, len(blob))
        struct.pack_into("<I", blob, 84 + 4*mip, len(pixels))
        blob.extend(pixels)
        if image.size == (1, 1):
            break
        image = image.resize((max(1, image.width//2), max(1, image.height//2)), Image.Resampling.BOX)
    return bytes(blob)


def verify(model, skin, texture):
    def array(data, header, stride):
        count, offset = struct.unpack_from("<II", data, header)
        assert offset + count*stride <= len(data)
        return count, offset
    assert struct.unpack_from("<4sI", model) == (b"MD20", 264)
    nv, vo = array(model, 60, 48)
    assert nv == 2209
    for header, stride in ((8,1),(28,64),(36,2),(44,88),(80,16),(88,20),(112,4),(120,2),(128,2),(136,2),(144,2),(152,2)):
        array(model, header, stride)
    alpha_count, alpha_offset = array(model, 88, 20)
    _, combo_offset = array(model, 144, 2)
    assert struct.unpack_from("<H", model, combo_offset)[0] < alpha_count
    for field, stride in ((4,4),(12,2)):
        count, outer = array(model, alpha_offset+field, 8)
        assert count == 1
        assert array(model, outer, stride)[0] == 1
    assert skin[:4] == b"SKIN"
    assert array(skin, 4, 2)[0] == nv and array(skin, 20, 4)[0] == nv
    ni, io = array(skin, 12, 2)
    assert ni == 3504*3
    assert max(struct.unpack_from("<"+"H"*ni, skin, io)) < nv
    array(skin, 28, 48)
    _, batch_offset = array(skin, 36, 24)
    assert struct.unpack_from("<BB11H", skin, batch_offset) == (0,0,0,0,0,65535,0,0,1,0,0,0,0)
    for i in range(nv):
        assert sum(model[vo+48*i+12:vo+48*i+16]) == 255
    assert texture[:4] == b"BLP2" and struct.unpack_from("<II", texture, 12) == (1024,1024)
    for mip in range(11):
        offset = struct.unpack_from("<I", texture, 20+4*mip)[0]
        size = struct.unpack_from("<I", texture, 84+4*mip)[0]
        assert size == max(1,1024>>mip)**2*4 and offset+size <= len(texture)
    # A malformed source must fail before generating any assets.
    try:
        source_mesh(b"UEFORMAT")
    except ValueError:
        pass
    else:
        raise AssertionError("Truncated source accepted")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=Path("G:/FortniteTrial/JourneyMentorShield"))
    args = parser.parse_args()
    source = SOURCE / "Meshes/F_MED_Journey_Mentor_Shield_Pack.uemodel"
    albedo = SOURCE / "Texture/T_F_MED_Journey_Mentor_Shield_Backpack_D.png"
    vertices, normals, uvs, triangles, receipt = source_mesh(source.read_bytes())
    model, skin = model_bytes(vertices, normals, uvs, triangles)
    texture = texture_bytes(albedo)
    verify(model, skin, texture)
    entries = {MODEL_PATH:model, MODEL_PATH[:-3]+"00.skin":skin, TEXTURE_PATH:texture}
    args.out.mkdir(parents=True, exist_ok=True)
    for name, data in entries.items():
        path = args.out / "patch-root" / Path(name.replace("\\", "/"))
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    sys.path.insert(0, str(REPO.parent / "EsteriaWoW/tools"))
    from cars_mount_pack import Storm
    storm = Storm(REPO / "Tools/MountStudio/runtime/backend/StormLib.dll")
    archive = args.out / "Patch-ZzzzzzzzzzzJourneyShield.MPQ"
    if archive.exists():
        raise FileExistsError(archive)
    storm.create_archive(archive, entries)
    handle = storm.open_archive(archive)
    try:
        for name, data in entries.items():
            assert storm.read(handle, name) == data
    finally:
        storm.dll.SFileCloseArchive(handle)
    receipt.update(vertices=len(vertices), triangles=len(triangles),
        source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
        inputs={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in SOURCE.rglob('*') if f.is_file()},
        entries={name:hashlib.sha256(data).hexdigest() for name,data in entries.items()},
        losses=["Unreal normal/specular material response is not reproduced by the lit albedo trial"],
        stage="art converted and MPQ readback verified; native rendering pending")
    (args.out / "receipt.json").write_text(json.dumps(receipt, indent=2)+"\n")
    print(json.dumps({"archive":str(archive), "vertices":len(vertices), "triangles":len(triangles), "checks":"passed"}))


if __name__ == "__main__":
    main()
