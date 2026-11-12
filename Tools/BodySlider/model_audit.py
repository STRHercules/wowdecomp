"""Read-only audit of original WotLK skeletons; writes no game files."""

import argparse
import ctypes
import hashlib
import json
import math
from pathlib import Path
import struct


RACES = ("Human", "Orc", "Dwarf", "NightElf", "Scourge", "Tauren", "Gnome", "Troll", "BloodElf", "Draenei")
ARCHIVES = ("patch-3.MPQ", "patch-2.MPQ", "patch.MPQ", "lichking.MPQ", "expansion.MPQ", "common-2.MPQ", "common.MPQ")


def parse_model(data):
    if len(data) < 68 or data[:4] != b"MD20" or struct.unpack_from("<I", data, 4)[0] != 264:
        raise ValueError("Expected a WotLK version-264 MD20 model")
    count, offset, key_count, key_offset = struct.unpack_from("<IIII", data, 44)
    if not 1 <= count <= 4096 or not 27 <= key_count <= 4096 or offset + count * 88 > len(data) or key_offset + key_count * 2 > len(data):
        raise ValueError("Invalid bone/key table bounds")
    keys = list(struct.unpack_from(f"<{key_count}h", data, key_offset))
    bones = []
    for i in range(count):
        base = offset + i * 88
        bone_id, flags, parent, submesh, name_crc = struct.unpack_from("<iIhHI", data, base)
        pivot = struct.unpack_from("<3f", data, base + 76)
        if parent < -1 or parent >= count or parent == i or any(not math.isfinite(value) or abs(value) > 10000 for value in pivot):
            raise ValueError("Invalid bone topology or pivot")
        bones.append(dict(index=i, key_id=bone_id, flags=flags, parent=parent,
                          name_crc=name_crc, pivot=[round(value, 7) for value in pivot], weighted_vertices=0))
    if any(index < -1 or index >= count for index in keys):
        raise ValueError("Invalid key-bone index")
    vertex_count, vertex_offset = struct.unpack_from("<II", data, 60)
    if vertex_offset + vertex_count * 48 > len(data):
        raise ValueError("Invalid vertex table bounds")
    for i in range(vertex_count):
        base = vertex_offset + i * 48
        for weight, bone in zip(data[base + 12:base + 16], data[base + 16:base + 20]):
            if weight:
                if bone >= count:
                    raise ValueError("Vertex references a bone outside the skeleton")
                bones[bone]["weighted_vertices"] += 1
    return dict(sha256=hashlib.sha256(data).hexdigest(), bone_count=count, key_bones=keys, bones=bones)


def select_rig(model):
    """Select limb chains from audited CRC landmarks and verify their topology."""
    bones = model["bones"]
    keys = model["key_bones"]

    def landmark(crc):
        matches = [b["index"] for b in bones if b["name_crc"] == crc]
        if len(matches) != 1:
            raise ValueError(f"Expected one bone with CRC {crc}, got {matches}")
        return matches[0]

    def distinct_parent(index):
        pivot = bones[index]["pivot"]
        parent = bones[index]["parent"]
        while parent >= 0 and math.dist(pivot, bones[parent]["pivot"]) < 0.0001:
            parent = bones[parent]["parent"]
        if parent < 0:
            raise ValueError("Limb chain ends before its proximal joint")
        return parent

    def axis(parent, child):
        delta = [bones[child]["pivot"][i] - bones[parent]["pivot"][i] for i in range(3)]
        if max(abs(value) for value in delta) < 0.001:
            raise ValueError("Cannot determine a length axis from coincident joints")
        return max(range(3), key=lambda i: abs(delta[i]))

    def ancestors(index):
        chain = []
        while index >= 0:
            if index in chain:
                raise ValueError("Bone parent cycle")
            chain.append(index)
            index = bones[index]["parent"]
        return chain

    hands = [landmark(crc) for crc in (294203841, 3951430818)]
    lowerarms = [distinct_parent(index) for index in hands]
    arms = [distinct_parent(index) for index in lowerarms]
    if model["path"].lower().endswith("bloodelfmale.m2"):
        # This rig places a separate weighted elbow before its forearm; key 0/1 are absent.
        arms = [keys[2], keys[3]]
        lowerarms = [landmark(crc) for crc in (2133193007, 2234174540)]
    shoulders = [bones[index]["parent"] for index in arms]
    # Draenei females use a different digitigrade foot naming family.
    foot_crcs = ((3735651477, 614906358) if model["path"].lower().endswith("draeneifemale.m2")
                 else (90603765, 4285119894))
    feet = [landmark(crc) for crc in foot_crcs]
    lowerlegs = [distinct_parent(index) for index in feet]
    upperlegs = [distinct_parent(index) for index in lowerlegs]
    shoulder_chains = [ancestors(index) for index in shoulders]
    chest = next(index for index in shoulder_chains[0] if index in shoulder_chains[1])
    roots = sorted({ancestors(b["index"])[-1] for b in bones if b["weighted_vertices"]})
    if not 1 <= len(roots) <= 4 or keys[6] < 0 or keys[4] < 0 or keys[5] < 0:
        raise ValueError("Unsupported stock central skeleton")
    # Each thigh must lead back through the pelvis; each arm through the chest.
    if any(keys[5] not in ancestors(index) for index in upperlegs):
        raise ValueError("Selected leg does not descend from pelvis")
    if any(chest not in ancestors(index) for index in arms):
        raise ValueError("Selected arm does not descend from chest")
    return dict(roots=roots, head=keys[6], neck=bones[keys[6]]["parent"],
                shoulders=shoulders, arms=arms, lowerarms=lowerarms, hands=hands,
                chest=chest, waist=keys[4], hips=keys[5], upperlegs=upperlegs,
                lowerlegs=lowerlegs, feet=feet,
                armAxis=[axis(parent, child) for parent, child in zip(arms, lowerarms)],
                legAxis=[axis(parent, child) for parent, child in zip(upperlegs, lowerlegs)],
                evidence="CRC hand/foot landmarks, parent chains with coincident helper pivots skipped, dominant bind-pose axis; visual calibration pending")


def generate_header(profiles):
    lines = ["// Generated by model_audit.py from read-only original WotLK model data.",
             "// Topology audited; visual calibration remains a live-client acceptance gate.",
             "#pragma once", "#include <cstdint>", "namespace Stock {",
             "struct Rig { const char* path; uint32_t count; int roots[4]; int head, neck;",
             "    int shoulders[2], arms[2], chest, waist, hips;",
             "    int upperlegs[2], lowerlegs[2], armAxis[2], legAxis[2];",
             "    const uint16_t* keys; const uint16_t* parents; const float (*pivots)[3]; };"]

    def array(values):
        return "{" + ", ".join(str(value) for value in values) + "}"

    for name, model in profiles["models"].items():
        lines.append(f"inline constexpr uint16_t kKeys{name}[] = " + array([v & 0xffff for v in model["key_bones"]]) + ";")
        lines.append(f"inline constexpr uint16_t kParents{name}[] = " + array([v["parent"] & 0xffff for v in model["bones"]]) + ";")
        lines.append(f"inline constexpr float kPivots{name}[][3] = {{")
        for bone in model["bones"]:
            lines.append("    {" + ", ".join(f"{v:.7f}f" for v in bone["pivot"]) + "},")
        lines.append("};")
    lines.append("inline constexpr Rig kRigs[] = {")
    for name, model in profiles["models"].items():
        rig = model["rig"]
        path = model["path"].replace("\\", "/").lower()
        fields = [json.dumps(path), str(model["bone_count"]),
                  array(rig["roots"] + [-1] * (4 - len(rig["roots"]))),
                  str(rig["head"]), str(rig["neck"]), array(rig["shoulders"]), array(rig["arms"]),
                  str(rig["chest"]), str(rig["waist"]), str(rig["hips"])]
        fields.extend(array(rig[name]) for name in ("upperlegs", "lowerlegs", "armAxis", "legAxis"))
        fields.extend((f"kKeys{name}", f"kParents{name}", f"kPivots{name}"))
        lines.append("    {" + ", ".join(fields) + "},")
    return "\n".join(lines + ["};", "} // namespace Stock", ""])


class Archives:
    def __init__(self, client, dll):
        self.dll = ctypes.WinDLL(str(dll), use_last_error=True)
        handle = ctypes.c_void_p
        u32 = ctypes.c_uint32
        for name, args, result in (
            ("SFileOpenArchive", [ctypes.c_wchar_p, u32, u32, ctypes.POINTER(handle)], ctypes.c_bool),
            ("SFileCloseArchive", [handle], ctypes.c_bool),
            ("SFileOpenFileEx", [handle, ctypes.c_char_p, u32, ctypes.POINTER(handle)], ctypes.c_bool),
            ("SFileGetFileSize", [handle, ctypes.POINTER(u32)], u32),
            ("SFileReadFile", [handle, handle, u32, ctypes.POINTER(u32), handle], ctypes.c_bool),
            ("SFileCloseFile", [handle], ctypes.c_bool),
        ):
            function = getattr(self.dll, name)
            function.argtypes = args
            function.restype = result
        self.handles = []
        try:
            for name in ARCHIVES:
                path = client / "Data" / name
                if not path.exists():
                    continue
                opened = handle()
                if not self.dll.SFileOpenArchive(str(path), 0, 0x100, ctypes.byref(opened)):
                    raise OSError(f"Cannot open {path} read-only: {ctypes.get_last_error()}")
                self.handles.append((path, opened))
        except Exception:
            self.close()
            raise

    def close(self):
        for _, handle in self.handles:
            self.dll.SFileCloseArchive(handle)
        self.handles.clear()

    def read(self, name):
        for archive, handle in self.handles:
            opened = ctypes.c_void_p()
            if not self.dll.SFileOpenFileEx(handle, name.encode("ascii"), 0, ctypes.byref(opened)):
                continue
            try:
                high = ctypes.c_uint32()
                size = self.dll.SFileGetFileSize(opened, ctypes.byref(high))
                if high.value or size > 64 * 1024 * 1024:
                    raise ValueError(f"Unexpected model size: {name}")
                buffer = ctypes.create_string_buffer(size)
                read = ctypes.c_uint32()
                if not self.dll.SFileReadFile(opened, buffer, size, ctypes.byref(read), None) or read.value != size:
                    raise OSError(f"Cannot read {name}: {ctypes.get_last_error()}")
                return archive, buffer.raw
            finally:
                self.dll.SFileCloseFile(opened)
        raise FileNotFoundError(name)


def audit(client, dll):
    archives = Archives(client, dll)
    try:
        profiles = {}
        for race in RACES:
            for sex in ("Male", "Female"):
                path = f"Character\\{race}\\{sex}\\{race}{sex}.m2"
                archive, data = archives.read(path)
                model = dict(path=path, archive=str(archive), **parse_model(data))
                model["rig"] = select_rig(model)
                profiles[f"{race}{sex}"] = model
        return dict(client=str(client), evidence="Read-only M2 topology and pivots; no live visual acceptance", models=profiles)
    finally:
        archives.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("client", type=Path)
    parser.add_argument("--stormlib", type=Path,
                        default=Path(__file__).resolve().parents[1] / "MountStudio/runtime/backend/StormLib.dll")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--header", type=Path, help="create an audited StockProfiles.h")
    args = parser.parse_args()
    profiles = audit(args.client, args.stormlib)
    result = json.dumps(profiles, indent=2)
    if args.output:
        with args.output.open("x", encoding="utf-8") as stream:
            stream.write(result + "\n")
    else:
        print(result)
    if args.header:
        with args.header.open("x", encoding="utf-8") as stream:
            stream.write(generate_header(profiles))


if __name__ == "__main__":
    main()
