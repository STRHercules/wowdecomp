"""Write EsteriaMounts/skins.csv rows (skins, geosets, rider poses) for installed native retail mounts.

Usage: mount_skins.py <reader> <definitions> <tables-dir> <output.csv> <Data/Retail/Packs>
tables-dir holds <Name>.db2 for every table in TABLES.
"""
import json
import struct
import sys
from collections import defaultdict
from pathlib import Path

TABLES = ("CreatureDisplayInfo", "CreatureDisplayInfoExtra", "CreatureDisplayInfoOption", "TextureFileData",
          "ChrCustomizationElement", "ChrCustomizationMaterial", "ChrCustomizationGeoset", "ChrModelTextureLayer",
          "ChrCustomizationOption", "ChrCustomizationChoice", "ChrModel", "CreatureDisplayInfoGeosetData", "SpellXSpellVisual",
          "SpellVisualEvent", "SpellVisualKitEffect", "SpellVisualAnim")
CLIENT_ANIMS = 0x1FA
MOUNT = 91
# Retail-only rider poses, for riders whose model lacks them: motorcycle and wide saddles sit, discs stand.
FALLBACK = {896: 0}


def model_meshes(originals, model_file):
    """{submesh ID: the M2 texture types its batches draw} for the model's first skin profile."""
    data = (originals / f"{model_file}.bin").read_bytes()
    chunks, offset = {}, 0
    while offset + 8 <= len(data):
        size = struct.unpack_from("<I", data, offset + 4)[0]
        chunks[data[offset:offset + 4]] = (offset + 8, size)
        offset += 8 + size
    model = data[chunks[b"MD21"][0]:]
    skin = (originals / f"{struct.unpack_from('<I', data, chunks[b'SFID'][0])[0]}.bin").read_bytes()
    word = lambda blob, at: struct.unpack_from("<I", blob, at)[0]
    half = lambda blob, at: struct.unpack_from("<H", blob, at)[0]
    count, start = struct.unpack_from("<II", skin, 0x1C)
    meshes = {half(skin, start + 48 * i): set() for i in range(count)}
    types = [word(model, word(model, 0x54) + 16 * i) for i in range(word(model, 0x50))]
    lookup = [half(model, word(model, 0x84) + 2 * i) for i in range(word(model, 0x80))]
    for i in range(word(skin, 0x24)):
        batch = word(skin, 0x28) + 24 * i
        section, textures, first = half(skin, batch + 4), half(skin, batch + 14), half(skin, batch + 16)
        if section < count:
            meshes[half(skin, start + 48 * section)] |= {types[lookup[k]] for k in range(first, first + textures)
                                                          if k < len(lookup) and lookup[k] < len(types)}
    return meshes


def empty_slots(tables, retail_display):
    """M2 texture types the display leaves without a texture: 11-13 for variations 0-2, 5 for variation 3."""
    slots = (tables["CreatureDisplayInfo"][retail_display].get("TextureVariationFileDataID") or []) + [0] * 4
    return {kind for kind, fid in zip((11, 12, 13, 5), slots) if not fid}


def geoset_rows(tables, retail_display, target, meshes, chosen, empty=frozenset()):
    """Retail's per-display choices; a group that draws a texture the display leaves empty is hidden (retail cannot
    show it either: Dapple Gray's banner is slot 13 and the display names two textures); any other group with
    variants shows its first one, as retail does."""
    explicit = {row["GeosetIndex"] + 1: row["GeosetValue"] for row in tables["CreatureDisplayInfoGeosetData"].values()
                if row["CreatureDisplayInfoID"] == retail_display}
    groups = defaultdict(set)
    for mesh in meshes:
        if mesh >= 100:
            groups[mesh // 100].add(mesh % 100)
    rows = []
    for group in sorted(set(groups) | set(explicit)):
        if group in chosen or group >= 100:
            continue
        if group in explicit:
            rows.append(f"{target};g;{group};{explicit[group]}")
        elif any(meshes.get(group * 100 + value, set()) & empty for value in groups[group]):
            rows.append(f"{target};g;{group};0")
        elif len(groups[group]) > 1 and 1 in groups[group]:
            rows.append(f"{target};g;{group};1")
    return rows


def rider_pose(index, spell):
    """The looping animation retail's mount aura puts on its rider, with a client-safe fallback."""
    for visual in index["visuals"].get(spell, ()):
        for event in index["events"].get(visual, ()):
            if event["StartEvent"] != 7 or event["TargetType"] != 2:
                continue
            for effect in index["effects"].get(event["SpellVisualKitID"], ()):
                anim = index["anims"].get(effect["Effect"]) if effect["EffectType"] == 6 else None
                if anim and anim["LoopAnimID"] not in (-1, MOUNT):
                    loop = anim["LoopAnimID"]
                    return loop, loop if loop < CLIENT_ANIMS else FALLBACK.get(loop, MOUNT)
    return None


def customization(tables, retail_display):
    """(extra, chosen choices, ChrModel) for a display: its extra's options, or, for a customizable mount shown
    without an extra (Steel Prototype Panther), each of its ChrModel's options at their first choice."""
    extra_id = tables["CreatureDisplayInfo"][retail_display].get("ExtendedDisplayInfoID")
    if extra_id:
        options = [r for r in tables["CreatureDisplayInfoOption"].values() if r["CreatureDisplayInfoExtraID"] == extra_id]
        if options:
            model = tables["ChrCustomizationOption"][options[0]["ChrCustomizationOptionID"]]["ChrModelID"]
            return extra_id, {r["ChrCustomizationChoiceID"] for r in options}, model
    model = next((m["ID"] for m in tables["ChrModel"].values() if m["DisplayID"] == retail_display), None)
    if model is None:
        return None
    first = {}
    for choice in tables["ChrCustomizationChoice"].values():
        option = tables["ChrCustomizationOption"].get(choice["ChrCustomizationOptionID"])
        if option and option["ChrModelID"] == model:
            key = (choice["OrderIndex"], choice["ID"])
            if option["ID"] not in first or key < first[option["ID"]]:
                first[option["ID"]] = key
    return 0, {choice for _, choice in first.values()}, model


def slot_rows(tables, retail_display, target_display):
    """Creature textures behind an empty slot: the import packed them leftward, so restore their real type."""
    slots = tables["CreatureDisplayInfo"][retail_display].get("TextureVariationFileDataID") or []
    packed = [fid for fid in slots[:3] if fid]
    rows = [f"{target_display};t;{11 + i};{fid}" for i, fid in enumerate(slots[:3]) if fid and packed.index(fid) != i]
    # The fourth variation fills texture type 5 (Coldflame Cormaera's glow map, the Arboons' and Petalwings' wing cards).
    if len(slots) > 3 and slots[3]:
        rows.append(f"{target_display};t;5;{slots[3]}")
    return rows


def skin_rows(tables, target_display, extra_id, chosen, model):
    """One texture per M2 type (the display's bake wins for skin) plus each chosen geoset."""
    files = {}
    for row in tables["TextureFileData"].values():
        files.setdefault(row["MaterialResourcesID"], row["FileDataID"])
    # A material target means different texture types per layout, so scope it to this display's model.
    layout = tables["ChrModel"][model]["CharComponentTextureLayoutID"]
    types = {}
    for row in tables["ChrModelTextureLayer"].values():
        if row["CharComponentTextureLayoutsID"] == layout:
            for target in row["ChrModelTextureTargetID"]:
                if target:
                    types.setdefault(target, set()).add(row["TextureType"])
    textures, geosets = {}, {}
    for element in tables["ChrCustomizationElement"].values():
        if element["ChrCustomizationChoiceID"] not in chosen:
            continue
        if element["RelatedChrCustomizationChoiceID"] not in (0, *chosen):
            continue
        material = tables["ChrCustomizationMaterial"].get(element["ChrCustomizationMaterialID"])
        if material and material["ChrModelTextureTargetID"] in types and material["MaterialResourcesID"] in files:
            for kind in types[material["ChrModelTextureTargetID"]]:
                textures.setdefault(kind, files[material["MaterialResourcesID"]])
        geoset = tables["ChrCustomizationGeoset"].get(element["ChrCustomizationGeosetID"])
        if geoset:  # type 0 picks among alternative bodies (meshes 1..99), e.g. the Delve vehicles
            geosets[geoset["GeosetType"]] = geoset["GeosetID"]
    bake = files.get(tables["CreatureDisplayInfoExtra"][extra_id]["BakeMaterialResourcesID"]) if extra_id else None
    if bake:
        textures[1] = bake
    return ([f"{target_display};t;{kind};{fid}" for kind, fid in sorted(textures.items())]
            + [f"{target_display};g;{group};{value}" for group, value in sorted(geosets.items())])


def main():
    reader, definitions, source, output, packs = sys.argv[1:]
    sys.path.insert(0, reader)
    from wotlkconv.db.db2 import parse_db2
    from wotlkconv.db.dbd import DbdIndex
    index = DbdIndex(definitions)
    tables = {name: parse_db2((Path(source) / f"{name}.db2").read_bytes(), name + ".db2", index).rows for name in TABLES}
    lookup = {"visuals": defaultdict(list), "events": defaultdict(list), "effects": defaultdict(list),
              "anims": tables["SpellVisualAnim"]}
    for row in tables["SpellXSpellVisual"].values():
        lookup["visuals"][row["SpellID"]].append(row["SpellVisualID"])
    for row in tables["SpellVisualEvent"].values():
        lookup["events"][row["SpellVisualID"]].append(row)
    for row in tables["SpellVisualKitEffect"].values():
        lookup["effects"][row["ParentSpellVisualKitID"]].append(row)
    lines = []
    for manifest_path in sorted(Path(packs).glob("*/manifest.json")):
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        source_info, target = manifest["source"], manifest["target"]
        if target.get("kind") != "mount":
            continue
        display, esteria = source_info["retail_display_id"], target["ids"]["CreatureDisplayInfo"]
        custom = customization(tables, display)
        skin = slot_rows(tables, display, esteria) + (skin_rows(tables, esteria, *custom) if custom else [])
        chosen = {int(row.split(";")[2]) for row in skin if ";g;" in row}
        meshes = model_meshes(manifest_path.parent / "originals", source_info["model_file_id"])
        filled = {int(row.split(";")[2]) for row in skin if ";t;" in row}  # customization can fill a display slot
        lines += skin + geoset_rows(tables, display, esteria, meshes, chosen, empty_slots(tables, display) - filled)
        pose = rider_pose(lookup, source_info["retail_spell_id"])
        if pose:
            lines.append(f"{esteria};r;{pose[0]};{pose[1]}")
    Path(output).write_text("# display;t;textureType;fileDataId | display;g;geosetGroup;geosetValue\n"
                            + "\n".join(lines) + "\n", encoding="ascii")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
