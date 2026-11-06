"""Build the room-kit table mod-esteria-housing spawns rooms from, using the retail housing DB2s.

    python -I make_kit.py <tables.json> <community-listfile.csv> <raw dir> <out.tsv>

tables.json comes from ../phase2/dump.py (HouseRoom, RoomWmoData, RoomComponent, RoomComponentOption).
<raw dir> holds original WMO roots as <fdid>.bin (RetailAssetProbe --extract); each solid wall's MOHD bounding
box gives its width. Only the Generic theme (HouseTheme 3) is used.

How retail's tables fit together (docs/housing/maps-recon.md, "Phase 3a" findings):
- A component's MeshStyleFilterID selects RoomComponentOption rows; Type 0 is the solid piece, Type 1 SubType 1
  a wall with a 3 yd door, Type 1 SubType 2 one with a ~11 yd opening (measured from the group geometry), and Type 2
  SubType 1/2 the matching door and wide frames that sit centred on the joint (wall_2_1/wall_2_2_multitheme05).
- A wall with ConnectionType 1 that has a door option is a socket. Corners (style 46) are connectable but have none.
"""
import collections
import csv
import json
import os
import struct
import sys

KINDS = [1, 2, 3, 6, 7, 8, 9, 11, 12, 13, 14, 15]  # generic single-floor rooms: no stairwells, prefabs or plot
THEME = 3


def wall_width(raw, fdid):
    data = open(os.path.join(raw, f"{fdid}.bin"), "rb").read()
    at = data.find(b"DHOM")
    assert at >= 0, f"{fdid}: no MOHD"
    box = struct.unpack_from("<6f", data, at + 8 + 0x24)
    return round(box[4] - box[1], 2)  # walls run along local y


def main(tables, listfile, raw, out):
    t = json.load(open(tables))
    rooms, wmo, comps, options = t["HouseRoom"], t["RoomWmoData"], t["RoomComponent"], t["RoomComponentOption"]
    names = {}
    for line in open(listfile, encoding="utf-8", errors="replace"):
        i, _, p = line.rstrip().partition(";")
        names[int(i)] = p
    option = collections.defaultdict(dict)
    for o in options.values():
        if o["Theme"] == THEME and o["ModelFileDataID"]:
            option[o["MeshStyleFilterID"]].setdefault((o["Type"], o["SubType"]), o["ModelFileDataID"])

    rows, skipped = [], []
    for kind in KINDS:
        room = rooms[str(kind)]
        box = wmo[str(room["RoomWmoDataID"])]["BoundingBox"]
        for c in sorted((c for c in comps.values() if c["RoomWmoDataID"] == room["RoomWmoDataID"]), key=lambda c: c["ID"]):
            styles = option[c["MeshStyleFilterID"]]
            solid = c["ModelFileDataID"] or styles.get((0, 0), 0)
            if not solid:
                skipped.append((kind, c["ID"], c["MeshStyleFilterID"]))
                continue
            socket = c["Type"] == 1 and c["ConnectionType"] == 1 and (1, 1) in styles
            door, wide = (styles.get((1, 1), 0), styles.get((1, 2), 0)) if socket else (0, 0)
            # Type 2 frames sit centred on a joint and cover the floor strip between two rooms' floors.
            door_frame, wide_frame = (styles.get((2, 1), 0), styles.get((2, 2), 0)) if socket else (0, 0)
            rows.append([kind, room["Name_lang"], *[round(v, 3) for v in box], c["ID"], c["Type"],
                         *[round(v, 4) for v in c["OffsetPos"]], round(c["OffsetRot"][2], 3), solid, door, wide, door_frame, wide_frame,
                         wall_width(raw, solid) if socket else 0, names.get(solid, "")])
    with open(out, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, delimiter="\t", lineterminator="\n")
        w.writerow(["kind", "name", "min_x", "min_y", "min_z", "max_x", "max_y", "max_z", "piece", "type",
                    "x", "y", "z", "rot_deg", "solid", "door", "wide", "door_frame", "wide_frame", "width", "solid_path"])
        w.writerows(rows)
    print(f"{len(rows)} pieces in {len(KINDS)} kinds; {sum(1 for r in rows if r[15])} sockets; skipped {skipped}")


if __name__ == "__main__":
    main(*sys.argv[1:5])
