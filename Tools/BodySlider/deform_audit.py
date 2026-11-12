"""Read-only, geometry-based deformation profiles for original WotLK meshes.

These are bind-pose landmark estimates, not live visual calibration. No game
archive or model is modified. Only explicitly selected output artifacts are written.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

from model_audit import Archives, RACES, parse_model, select_rig

FACE_NAMES = ["JawSize", "JawLength", "FaceWidth", "FaceLength", "BrowHeight", "BrowDepth",
              "EyeSize", "EyeSpacing", "EyeHeight", "NoseSize", "NoseWidth", "NoseLength",
              "NoseHeight", "MouthWidth", "LipFullness", "MouthHeight", "ChinLength", "ChinDepth",
              "CheekWidth", "CheekHeight", "JawWidth", "EarSize", "EarHeight"]
REGIONS = ["breast_left", "breast_right", "butt_left", "butt_right", "face", "jaw",
           "brow_left", "brow_right", "eye_left", "eye_right", "nose", "mouth", "chin",
           "cheek_left", "cheek_right", "jaw_left", "jaw_right", "ear_left", "ear_right"]


def fingerprint(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return value


def read_vertices(data):
    count, offset = struct.unpack_from("<II", data, 60)
    if not 1 <= count <= 1000000 or offset + count * 48 > len(data):
        raise ValueError("Invalid M2 vertex table")
    raw = data[offset:offset + count * 48]
    for position in range(0, len(raw), 48):
        numbers = struct.unpack_from("<3f8x7f", raw, position)
        if any(not math.isfinite(v) or abs(v) > 10000 for v in numbers):
            raise ValueError("Invalid vertex coordinate, normal or UV")
    return raw


def read_skin(data, vertex_count):
    if len(data) < 48 or data[:4] != b"SKIN":
        raise ValueError("Expected WotLK SKIN")
    count, offset = struct.unpack_from("<II", data, 4)
    bone_count, bone_offset, section_count, section_offset = struct.unpack_from("<IIII", data, 20)
    if (count > 65535 or bone_count != count or offset + count * 2 > len(data)
            or bone_offset + count * 4 > len(data) or section_offset + section_count * 48 > len(data)):
        raise ValueError("Invalid SKIN bounds")
    indices = struct.unpack_from(f"<{count}H", data, offset)
    if any(index >= vertex_count for index in indices):
        raise ValueError("SKIN index outside vertex table")
    sections = []
    for i in range(section_count):
        identifier, start, length, _, _, _, combo, influences = struct.unpack_from("<I7H", data, section_offset+i*48)
        if start + length > count or influences > 4:
            raise ValueError("Invalid SKIN section")
        sections.append(dict(id=identifier, start=start, count=length, combo=combo, influences=influences))
    return dict(indices=indices, bones=data[bone_offset:bone_offset+count*4], sections=sections)


def remapped_vertices(raw, skin, combos):
    result = bytearray(len(skin["indices"]) * 48)
    for section in skin["sections"]:
        for j in range(section["start"], section["start"]+section["count"]):
            source = skin["indices"][j]*48
            result[j*48:(j+1)*48] = raw[source:source+48]
            for k in range(section["influences"]):
                combo = section["combo"] + skin["bones"][j*4+k]
                if combo >= len(combos) or combos[combo] > 255:
                    raise ValueError(f"Invalid bone combo {combo}/{len(combos)} value {combos[combo] if combo < len(combos) else None}")
                result[j*48+16+k] = combos[combo]
    return bytes(result)


def percentile(values, fraction):
    ordered = sorted(values)
    if not ordered:
        raise ValueError("No landmark candidates")
    return ordered[round((len(ordered)-1)*fraction)]


def mean(points):
    if not points:
        raise ValueError("No landmark candidates")
    return [sum(p[a] for p in points)/len(points) for a in range(3)]


def region(center, radius):
    if any(not math.isfinite(v) for v in center+radius) or min(radius) <= 0:
        raise ValueError("Invalid deformation volume")
    return dict(center=[round(v, 7) for v in center], radius=[round(v, 7) for v in radius])


def profile(data, skin, path):
    raw = read_vertices(data)
    model = dict(path=path, **parse_model(data))
    rig = select_rig(model)
    bones = model["bones"]

    def descends(index, ancestor):
        for _ in range(len(bones)):
            if index == ancestor:
                return True
            index = bones[index]["parent"]
            if index < 0:
                return False
        raise ValueError("Bone cycle")

    flags = []
    for index in range(len(bones)):
        head = descends(index, rig["head"])
        arm = any(descends(index, parent) for parent in rig["arms"])
        torso = not head and not arm and any(descends(index, parent) for parent in rig["roots"])
        flags.append(int(head) | (int(torso) << 1))

    vertices = [struct.unpack_from("<3f", raw, i) for i in range(0, len(raw), 48)]

    def influence(index, bit):
        offset = index*48
        return sum(raw[offset+12+k] for k in range(4) if raw[offset+12+k] and flags[raw[offset+16+k]] & bit)/255

    main = sorted({skin["indices"][j] for section in skin["sections"] if section["id"] == 0
                   for j in range(section["start"], section["start"]+section["count"])})
    heads = [vertices[i] for i in main if influence(i, 1) >= .5]
    torso = [vertices[i] for i in main if influence(i, 2) >= .5]
    if len(heads) < 20 or len(torso) < 20:
        raise ValueError("Insufficient weighted body/head surface")
    head = bones[rig["head"]]["pivot"]
    neck = bones[rig["neck"]]["pivot"]
    chest = bones[rig["chest"]]["pivot"]
    hips = bones[rig["hips"]]["pivot"]
    height = head[2]
    jaw_index = model["key_bones"][7]
    jaw = bones[jaw_index]["pivot"] if jaw_index >= 0 else [head[0]+.06*height, 0, head[2]-.04*height]
    face_height = max(.08*height, percentile([p[2] for p in heads], .92)-jaw[2]+.02*height)
    face_width = max(.035*height, percentile([abs(p[1]) for p in heads], .65))
    front = percentile([p[0] for p in heads], .85)
    eye_bones = [next((b["pivot"] for b in bones if b["name_crc"] == crc), None)
                 for crc in (0xaa5cfbf4, 0x5053c697)]
    eye_source = "paired original eye-quad bone CRC landmarks"
    if any(p is None for p in eye_bones):
        eye_source = "main-body head mesh quantiles (no paired eye-quad bone landmarks)"
        eye_z = jaw[2]+.52*face_height
        eye_bones = [[front, sign*.35*face_width, eye_z] for sign in (1, -1)]
    eye_z = sum(p[2] for p in eye_bones)/2
    candidates = [p for p in heads if abs(p[1]) < face_width*.35 and jaw[2]-.1*face_height <= p[2] <= eye_z+.1*face_height]
    if not candidates:
        raise ValueError("No nose surface candidates")
    x_cut = percentile([p[0] for p in candidates], .65)
    nose = mean([p for p in candidates if p[0] >= x_cut])
    mouth = [nose[0]-.05*face_height, 0, jaw[2]+.12*face_height]
    mouth_bones = [b["pivot"] for b in bones if b["name_crc"] in (0xc43124bb, 0x3e3e19d8)]
    if len(mouth_bones) == 2 and abs(mouth_bones[0][2]-jaw[2]) < face_height*.5:
        mouth = mean(mouth_bones)
    chin = [mouth[0]-.1*face_height, 0, jaw[2]-.2*face_height]
    zones = {}
    zones["face"] = region([head[0]+.25*(front-head[0]), 0, eye_z-.12*face_height],
                           [max(.07*height, front-head[0]+.03*height), face_width*1.8, face_height*1.4])
    zones["jaw"] = region([jaw[0]+.3*(mouth[0]-jaw[0]), 0, jaw[2]],
                          [max(.06*height, abs(mouth[0]-jaw[0])+.03*height), face_width*1.25, face_height*.65])
    zones["nose"] = region(nose, [max(.025*height, (nose[0]-head[0])*.6), face_width*.5, face_height*.45])
    zones["mouth"] = region(mouth, [max(.025*height, (mouth[0]-head[0])*.7), face_width*.9, face_height*.3])
    zones["chin"] = region(chin, [max(.03*height, (chin[0]-head[0])*.8), face_width*.85, face_height*.4])
    for side, label in enumerate(("left", "right")):
        sign = 1 if side == 0 else -1
        eye = eye_bones[side]
        zones[f"eye_{label}"] = region(eye, [.035*height, max(.018*height, abs(eye[1])*.9), face_height*.28])
        zones[f"brow_{label}"] = region([eye[0], eye[1], eye[2]+.11*face_height], [.04*height, face_width*.6, face_height*.3])
        zones[f"cheek_{label}"] = region([nose[0]-.05*face_height, sign*face_width*.55, eye_z-.25*face_height],
                                         [.045*height, face_width*.65, face_height*.4])
        zones[f"jaw_{label}"] = region([jaw[0], sign*face_width*.65, jaw[2]], [.05*height, face_width*.7, face_height*.5])
        ear_candidates = [p for p in heads if p[1]*sign > face_width*.8 and abs(p[2]-eye_z) < face_height*.75]
        if ear_candidates:
            cut = percentile([abs(p[1]) for p in ear_candidates], .6)
            ear = mean([p for p in ear_candidates if abs(p[1]) >= cut])
        else:
            ear = [head[0], sign*face_width*1.2, eye_z]
        zones[f"ear_{label}"] = region(ear, [.055*height, max(.025*height, abs(ear[1])-face_width*.6), face_height*.75])

        breast_candidates = [p for p in torso if p[1]*sign >= .01*height and p[1]*sign < .16*height
                             and chest[2]-.035*height < p[2] < neck[2]-.07*height]
        if not breast_candidates:
            raise ValueError("No torso-front surface candidates")
        cut = percentile([p[0] for p in breast_candidates], .7)
        breast = mean([p for p in breast_candidates if p[0] >= cut])
        zones[f"breast_{label}"] = region(breast, [.085*height, .075*height, .095*height])
        butt_candidates = [p for p in torso if p[1]*sign >= .01*height and p[1]*sign < .17*height
                           and hips[2]-.13*height < p[2] < hips[2]+.04*height]
        if not butt_candidates:
            raise ValueError("No pelvis-rear surface candidates")
        cut = percentile([p[0] for p in butt_candidates], .3)
        butt = mean([p for p in butt_candidates if p[0] <= cut])
        zones[f"butt_{label}"] = region(butt, [.095*height, .09*height, .12*height])

    # Expand sparse low-polygon feature volumes only until they contain surface vertices.
    counts = {}
    for name in REGIONS:
        zone = zones[name]
        source = torso if name.startswith(("breast", "butt")) else heads
        for _ in range(5):
            count = sum(sum(((p[a]-zone["center"][a])/zone["radius"][a])**2 for a in range(3)) < 1 for p in source)
            if count >= 3:
                break
            zone["radius"] = [round(v*1.15, 7) for v in zone["radius"]]
        counts[name] = count
    return dict(path=path.replace("\\", "/").lower(), model_sha256=hashlib.sha256(data).hexdigest(),
                raw_vertex_count=len(raw)//48, height=round(height, 7), female="/female/" in path.replace("\\", "/").lower(),
                head_landmark_source=eye_source, bone_flags=flags, regions=zones, surface_vertex_counts=counts,
                evidence="Main geoset-0 surface, original head/jaw skeleton, eye CRC landmarks where present; analytic smooth local deformation, visual calibration pending")


def audit(client, dll):
    archives = Archives(client, dll)
    try:
        models = {}
        for race in RACES:
            for sex in ("Male", "Female"):
                path = f"Character\\{race}\\{sex}\\{race}{sex}.m2"
                archive, data = archives.read(path)
                raw = read_vertices(data)
                _, skin_data = archives.read(path[:-3]+"00.skin")
                first_skin = read_skin(skin_data, len(raw)//48)
                result = profile(data, first_skin, path)
                result["archive"] = str(archive)
                result["variants"] = [dict(kind="raw", count=len(raw)//48, fingerprint=f"{fingerprint(raw):016x}")]
                combo_count, combo_offset = struct.unpack_from("<II", data, 120)
                if combo_offset+combo_count*2 > len(data):
                    raise ValueError("Invalid bone-combo table")
                combos = struct.unpack_from(f"<{combo_count}H", data, combo_offset)
                for lod in range(4):
                    try:
                        _, content = archives.read(path[:-3]+f"{lod:02}.skin")
                    except FileNotFoundError:
                        continue
                    skin = read_skin(content, len(raw)//48)
                    try:
                        remapped = remapped_vertices(raw, skin, combos)
                    except ValueError as exc:
                        result.setdefault("unsupported_variants", []).append(dict(kind=f"software-lod{lod}", reason=str(exc)))
                        continue
                    result["variants"].append(dict(kind=f"software-lod{lod}", count=len(remapped)//48,
                                                    fingerprint=f"{fingerprint(remapped):016x}"))
                models[f"{race}{sex}"] = result
        return dict(schema=1, regions=REGIONS, facial_sliders=FACE_NAMES,
                    evidence="Read-only bind geometry audit; no live visual acceptance", models=models)
    finally:
        archives.close()


def generate_header(profiles):
    lines = ["// Generated by deform_audit.py; bind geometry evidence, not live visual acceptance.",
             "#pragma once", "#include <cstdint>", "namespace Deform {",
             "struct Zone { float center[3], radius[3]; };",
             "struct Variant { uint32_t count; uint64_t hash; };",
             "struct Profile { const char* path; float height; bool female; const uint8_t* boneFlags; uint32_t boneCount;",
             "    Zone zones[19]; const Variant* variants; uint32_t variantCount; uint32_t support; };"]
    for name, model in profiles["models"].items():
        lines.append(f"inline constexpr uint8_t kBoneFlags{name}[] = {{"+", ".join(map(str, model["bone_flags"]))+"};")
        lines.append(f"inline constexpr Variant kVariants{name}[] = {{")
        for variant in model["variants"]:
            lines.append(f"    {{{variant['count']}, 0x{variant['fingerprint']}ull}},")
        lines.append("};")
    lines.append("inline constexpr Profile kProfiles[] = {")
    for name, model in profiles["models"].items():
        mask = 0
        mappings = [[4], [5], [4], [4], [6, 7], [6, 7], [8, 9], [8, 9], [8, 9], [10], [10], [10], [10],
                    [11], [11], [11], [12], [12], [13, 14], [13, 14], [15, 16], [17, 18], [17, 18]]
        for index, regions in enumerate(mappings):
            if all(model["surface_vertex_counts"][REGIONS[r]] >= 3 for r in regions):
                mask |= 1 << index
        lines.append("    {"+json.dumps(model["path"])+f", {model['height']:.7f}f, "+str(model["female"]).lower()+
                     f", kBoneFlags{name}, {len(model['bone_flags'])}, {{")
        for region_name in REGIONS:
            zone = model["regions"][region_name]
            values = ["{"+", ".join(f"{v:.7f}f" for v in zone[field])+"}" for field in ("center", "radius")]
            lines.append("        {"+", ".join(values)+"},")
        lines.append(f"    }}, kVariants{name}, {len(model['variants'])}, 0x{mask:08x}u}},")
    return "\n".join(lines+["};", "} // namespace Deform", ""])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("client", type=Path)
    parser.add_argument("--stormlib", type=Path, default=Path(__file__).resolve().parents[1]/"MountStudio/runtime/backend/StormLib.dll")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    args = parser.parse_args()
    profiles = audit(args.client, args.stormlib)
    args.output.write_text(json.dumps(profiles, indent=2)+"\n", encoding="utf-8")
    args.header.write_text(generate_header(profiles), encoding="utf-8")


if __name__ == "__main__":
    main()
