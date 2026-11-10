"""Write EsteriaMounts/skins.csv rows for customizable retail mounts from decoded DB2 tables.

Usage: mount_skins.py <reader> <definitions> <tables-dir> <output.csv> <retailDisplay:esteriaDisplay> ...
tables-dir holds <Name>.db2 for every table in TABLES.
"""
import sys
from pathlib import Path

TABLES = ("CreatureDisplayInfo", "CreatureDisplayInfoExtra", "CreatureDisplayInfoOption", "TextureFileData",
          "ChrCustomizationElement", "ChrCustomizationMaterial", "ChrCustomizationGeoset", "ChrModelTextureLayer",
          "ChrCustomizationOption", "ChrModel")


def skin_rows(tables, retail_display, target_display):
    """One texture per M2 type (the display's bake wins for skin) plus each chosen geoset."""
    files = {}
    for row in tables["TextureFileData"].values():
        files.setdefault(row["MaterialResourcesID"], row["FileDataID"])
    extra_id = tables["CreatureDisplayInfo"][retail_display]["ExtendedDisplayInfoID"]
    extra = tables["CreatureDisplayInfoExtra"][extra_id]
    options = [r for r in tables["CreatureDisplayInfoOption"].values() if r["CreatureDisplayInfoExtraID"] == extra_id]
    chosen = {r["ChrCustomizationChoiceID"] for r in options}
    # A material target means different texture types per layout, so scope it to this display's model.
    model = tables["ChrCustomizationOption"][options[0]["ChrCustomizationOptionID"]]["ChrModelID"]
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
        if geoset and geoset["GeosetType"]:
            geosets[geoset["GeosetType"]] = geoset["GeosetID"]
    bake = files.get(extra["BakeMaterialResourcesID"])
    if bake:
        textures[1] = bake
    return ([f"{target_display};t;{kind};{fid}" for kind, fid in sorted(textures.items())]
            + [f"{target_display};g;{group};{value}" for group, value in sorted(geosets.items())])


def main():
    reader, definitions, source, output, *pairs = sys.argv[1:]
    sys.path.insert(0, reader)
    from wotlkconv.db.db2 import parse_db2
    from wotlkconv.db.dbd import DbdIndex
    index = DbdIndex(definitions)
    tables = {name: parse_db2((Path(source) / f"{name}.db2").read_bytes(), name + ".db2", index).rows for name in TABLES}
    lines = []
    for pair in pairs:
        retail, target = map(int, pair.split(":"))
        lines += skin_rows(tables, retail, target)
    Path(output).write_text("# display;t;textureType;fileDataId | display;g;geosetGroup;geosetValue\n"
                            + "\n".join(lines) + "\n", encoding="ascii")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
