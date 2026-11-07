"""Founder's Point plots and every house exterior style, for mod-esteria-housing.

    python -I make_plots.py <tables.json> <out dir> [<door geometry dir>]

tables.json comes from ../phase2/dump.py and must hold NeighborhoodPlot, ExteriorComponent, ExteriorComponentHook and
HouseExteriorWmoData. Writes <out dir>/plots.tsv (one row per plot of NeighborhoodMap 1) and <out dir>/exterior.tsv:
one recipe per style and size (style = HouseExteriorWmoData.ID * 10 + base Size; Human House small is 92), each the
default (IsDefaultFixture) pieces at their hooks.

Retail's exterior is a component graph: a base (type 9) with hooks for doors (11) and windows (12); a roof (10) sits on
the base's origin and has hooks for dormers (14), towers (15) and chimneys (16). Hook OffsetRot turns clockwise, as
RoomComponent's does. The door goes on one of the base's door hooks; the module picks the one facing the cornerstone,
so every candidate is written as a "door_hook" row.

<door geometry dir> holds each door WMO's first group as g<fdid>.wmo (RetailAssetProbe --extract); its porch (see
porch()) becomes the door rows' mat_x, mat_z, where the clickable Front Door mat lies. Without it, or for a door with no
porch near its centre line, the mat lies 1.5 yd out on the ground.

Also writes the owner's choices: <out dir>/fixtures.tsv, every base, roof, door, window and chimney of every style
(colour variants name their shape as parent; a door's mat comes from its shape's geometry), and fixture_hooks.tsv,
the door, window and chimney hooks of each base and roof shape.
"""
import csv
import json
import math
import os
import struct
import sys

BASE, ROOF, DOOR, WINDOW, CHIMNEY = 9, 10, 11, 12, 16
SIZES = {2: "Small", 3: "Medium", 4: "Large"}
GROUND_MAT = (1.5, 0.0)
FAR_HOUSE = 55  # yd: past this from its cornerstone, a plot's HousePosition is not used (see main)


def porch(path):
    """Where the Front Door mat lies, in the door piece's own coordinates (x out of the door, z up): the door models
    differ in origin and porch depth, so this is the largest flat surface near the door's centre line (|y| < 3 yd,
    below 5 yd), binned by height; its area-weighted mean x and its height. None when there is no such surface."""
    w = open(path, "rb").read()
    i, ch = 0, {}
    while i + 8 <= len(w):
        tag = w[i:i + 4][::-1].decode("latin-1"); n = struct.unpack_from("<I", w, i + 4)[0]
        if tag == "MOGP":
            j = i + 8 + 0x44
            while j + 8 <= i + 8 + n:
                t2 = w[j:j + 4][::-1].decode("latin-1"); m = struct.unpack_from("<I", w, j + 4)[0]
                ch.setdefault(t2, w[j + 8:j + 8 + m]); j += 8 + m
        i += 8 + n
    if "MOVT" not in ch:
        return None
    v = [struct.unpack_from("<3f", ch["MOVT"], k) for k in range(0, len(ch["MOVT"]), 12)]
    key = "MOVI" if "MOVI" in ch else "MOVX"
    size = 2 if key == "MOVI" else 4
    idx = [struct.unpack_from("<H" if size == 2 else "<I", ch[key], k)[0] for k in range(0, len(ch[key]), size)]
    bins = {}  # height (0.25 yd) -> [area, area * x]
    for t in range(0, len(idx) - 2, 3):
        a, b, c = v[idx[t]], v[idx[t + 1]], v[idx[t + 2]]
        u = [b[k] - a[k] for k in range(3)]; q = [c[k] - a[k] for k in range(3)]
        n = (u[1] * q[2] - u[2] * q[1], u[2] * q[0] - u[0] * q[2], u[0] * q[1] - u[1] * q[0])
        area = sum(x * x for x in n) ** .5 / 2
        m = [(a[k] + b[k] + c[k]) / 3 for k in range(3)]
        if area > 0 and n[2] / (2 * area) > 0.9 and abs(m[1]) < 3 and m[2] < 5:
            acc = bins.setdefault(round(m[2] * 4) / 4, [0.0, 0.0])
            acc[0] += area
            acc[1] += area * m[0]
    if not bins:
        return None
    z, (area, ax) = max(bins.items(), key=lambda kv: kv[1][0])
    return ax / area, z


def main(tables, out, doors=None):
    t = json.load(open(tables))
    plots = sorted((p for p in t["NeighborhoodPlot"].values() if p["NeighborhoodMapID"] == 1), key=lambda p: p["PlotIndex"])
    with open(os.path.join(out, "plots.tsv"), "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, delimiter="\t", lineterminator="\n")
        w.writerow(["plot", "house_x", "house_y", "house_z", "house_yaw", "corner_x", "corner_y", "corner_z", "corner_o",
                    "exit_x", "exit_y", "exit_z"])
        for p in plots:
            # In this build the DBD's HousePosition/HouseRotation arrive as Field_12_0_0_63534_004/005 (radians).
            house, yaw = list(p["Field_12_0_0_63534_004"]), p["Field_12_0_0_63534_005"][2]
            corner, facing = p["CornerstonePosition"], p["CornerstoneRotation"][2]
            if math.hypot(house[0] - corner[0], house[1] - corner[1]) > FAR_HOUSE:
                # A few plots' HousePosition lies 60 to 1700 yd from their cornerstone (unfinished retail data): put the
                # house where most plots have it, 32 yd out along the cornerstone's facing, at its height.
                house = [corner[0] + 32 * math.cos(facing), corner[1] + 32 * math.sin(facing), corner[2]]
                yaw = facing + math.pi
            w.writerow([p["PlotIndex"], *[round(v, 3) for v in house], round(yaw, 4), *[round(v, 3) for v in corner],
                        round(facing, 4), *[round(v, 3) for v in p["TeleportPosition"]]])

    hooks_of = lambda owner, kind: sorted((h for h in t["ExteriorComponentHook"].values()  # noqa: E731
                                           if h["ExteriorComponentID"] == owner["ID"] and h["ComponentType"] == kind),
                                          key=lambda h: h["ID"])
    rows, made = [], []
    for wmo_id, wmo in sorted(((int(k), v) for k, v in t["HouseExteriorWmoData"].items())):
        comps = sorted((c for c in t["ExteriorComponent"].values() if c["HouseExteriorWmoDataID"] == wmo_id),
                       key=lambda c: c["ID"])
        defaults = lambda kind: [c for c in comps if c["Type"] == kind and c["Flags"] & 1]  # noqa: E731
        door, window, chimney = (next(iter(defaults(k)), None) for k in (DOOR, WINDOW, CHIMNEY))
        roofs = {c["Size"]: c for c in defaults(ROOF)}
        for base in defaults(BASE):
            size = base["Size"]
            roof = roofs.get(size)
            if not roof or not door:
                continue
            style = wmo_id * 10 + size
            name = f"{wmo['Name_lang']} ({SIZES.get(size, size)})"
            mat = GROUND_MAT
            if doors and os.path.exists(os.path.join(doors, f"g{door['ModelFileDataID']}.wmo")):
                mat = porch(os.path.join(doors, f"g{door['ModelFileDataID']}.wmo")) or mat
            recipe = [("base", base["ModelFileDataID"], 0, 0, 0, 0), ("roof", roof["ModelFileDataID"], 0, 0, 0, 0)]
            if window:
                recipe += [("window", window["ModelFileDataID"], *h["OffsetPos"], h["OffsetRot"][2]) for h in hooks_of(base, WINDOW)]
            chimneys = hooks_of(roof, CHIMNEY)
            if chimney and chimneys:
                recipe.append(("chimney", chimney["ModelFileDataID"], *chimneys[0]["OffsetPos"], chimneys[0]["OffsetRot"][2]))
            recipe += [("door_hook", door["ModelFileDataID"], *h["OffsetPos"], h["OffsetRot"][2]) for h in hooks_of(base, DOOR)]
            if not any(r[0] == "door_hook" for r in recipe):
                continue  # no door, no way in
            rows += [(style, name, r[0], r[1], *[round(v, 3) for v in r[2:]],
                      *([round(mat[0], 3), round(mat[1], 3)] if r[0] == "door_hook" else [0, 0])) for r in recipe]
            made.append(f"{style} {name}")
    with open(os.path.join(out, "exterior.tsv"), "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, delimiter="\t", lineterminator="\n")
        w.writerow(["style", "name", "role", "fdid", "x", "y", "z", "rot_deg", "mat_x", "mat_z"])
        w.writerows(rows)
    print(f"{len(plots)} plots; {len(made)} exterior styles: {', '.join(made)}")

    comps = {c["ID"]: c for c in t["ExteriorComponent"].values()
             if c["Type"] in (BASE, ROOF, DOOR, WINDOW, CHIMNEY) and c["ModelFileDataID"]}
    with open(os.path.join(out, "fixtures.tsv"), "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, delimiter="\t", lineterminator="\n")
        w.writerow(["component", "theme", "type", "size", "parent", "is_default", "fdid", "name", "mat_x", "mat_z"])
        for c in sorted(comps.values(), key=lambda c: c["ID"]):
            mat = (0, 0)
            if c["Type"] == DOOR:
                shape = comps.get(c["ParentComponentID"], c)
                g = os.path.join(doors, f"g{shape['ModelFileDataID']}.wmo") if doors else ""
                mat = (porch(g) if g and os.path.exists(g) else None) or GROUND_MAT
            w.writerow([c["ID"], c["HouseExteriorWmoDataID"], c["Type"], c["Size"], c["ParentComponentID"], c["Flags"] & 1,
                        c["ModelFileDataID"], c["Name_lang"], round(mat[0], 3), round(mat[1], 3)])
    hooks = sorted((h for h in t["ExteriorComponentHook"].values() if h["ExteriorComponentID"] in comps
                    and h["ComponentType"] in (DOOR, WINDOW, CHIMNEY)), key=lambda h: h["ID"])
    with open(os.path.join(out, "fixture_hooks.tsv"), "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, delimiter="\t", lineterminator="\n")
        w.writerow(["component", "type", "x", "y", "z", "rot_deg"])
        w.writerows([h["ExteriorComponentID"], h["ComponentType"], *[round(v, 3) for v in h["OffsetPos"]],
                     round(h["OffsetRot"][2], 3)] for h in hooks)
    print(f"{len(comps)} fixtures, {len(hooks)} fixture hooks")


if __name__ == "__main__":
    main(*sys.argv[1:4])
