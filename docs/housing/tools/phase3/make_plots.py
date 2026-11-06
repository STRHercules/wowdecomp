"""Founder's Point plots and the default Human House exterior, for mod-esteria-housing.

    python -I make_plots.py <tables.json> <out dir>

tables.json comes from ../phase2/dump.py and must hold NeighborhoodPlot, ExteriorComponent, ExteriorComponentHook.
Writes <out dir>/plots.tsv (one row per plot of NeighborhoodMap 1) and <out dir>/exterior.tsv (the default small
Human House: HouseExteriorWmoData 9, the size-2 pieces flagged IsDefaultFixture, placed at their hooks).

Retail's exterior is a component graph: a base (type 9) with hooks for doors (11) and windows (12); a roof (10) sits on
the base's origin and has hooks for dormers (14), towers (15) and chimneys (16). Hook OffsetRot turns clockwise, as
RoomComponent's does. The door goes on one of the base's door hooks; the module picks the one facing the cornerstone,
so every candidate is written as a "door_hook" row.
"""
import csv
import json
import os
import sys

HUMAN_HOUSE, SIZE = 9, 2
BASE, ROOF, DOOR, WINDOW, CHIMNEY = 9, 10, 11, 12, 16


def main(tables, out):
    t = json.load(open(tables))
    plots = sorted((p for p in t["NeighborhoodPlot"].values() if p["NeighborhoodMapID"] == 1), key=lambda p: p["PlotIndex"])
    with open(os.path.join(out, "plots.tsv"), "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, delimiter="\t", lineterminator="\n")
        w.writerow(["plot", "house_x", "house_y", "house_z", "house_yaw", "corner_x", "corner_y", "corner_z", "corner_o",
                    "exit_x", "exit_y", "exit_z"])
        for p in plots:
            # In this build the DBD's HousePosition/HouseRotation arrive as Field_12_0_0_63534_004/005 (radians).
            w.writerow([p["PlotIndex"], *[round(v, 3) for v in p["Field_12_0_0_63534_004"]],
                        round(p["Field_12_0_0_63534_005"][2], 4), *[round(v, 3) for v in p["CornerstonePosition"]],
                        round(p["CornerstoneRotation"][2], 4), *[round(v, 3) for v in p["TeleportPosition"]]])

    comps = {int(k): c for k, c in t["ExteriorComponent"].items() if c["HouseExteriorWmoDataID"] == HUMAN_HOUSE}
    default = lambda kind, size=None: next(c for c in sorted(comps.values(), key=lambda c: c["ID"])  # noqa: E731
                                           if c["Type"] == kind and c["Flags"] & 1 and (size is None or c["Size"] == size))
    base, roof = default(BASE, SIZE), default(ROOF, SIZE)
    door, window, chimney = default(DOOR), default(WINDOW), default(CHIMNEY)
    hooks = lambda owner, kind: sorted((h for h in t["ExteriorComponentHook"].values()  # noqa: E731
                                        if h["ExteriorComponentID"] == owner["ID"] and h["ComponentType"] == kind),
                                       key=lambda h: h["ID"])
    rows = [("base", base["ModelFileDataID"], 0, 0, 0, 0), ("roof", roof["ModelFileDataID"], 0, 0, 0, 0)]
    rows += [("window", window["ModelFileDataID"], *h["OffsetPos"], h["OffsetRot"][2]) for h in hooks(base, WINDOW)]
    chimneys = hooks(roof, CHIMNEY)
    if chimneys:
        rows.append(("chimney", chimney["ModelFileDataID"], *chimneys[0]["OffsetPos"], chimneys[0]["OffsetRot"][2]))
    rows += [("door_hook", door["ModelFileDataID"], *h["OffsetPos"], h["OffsetRot"][2]) for h in hooks(base, DOOR)]
    with open(os.path.join(out, "exterior.tsv"), "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, delimiter="\t", lineterminator="\n")
        w.writerow(["role", "fdid", "x", "y", "z", "rot_deg"])
        w.writerows([(r[0], r[1], *[round(v, 3) for v in r[2:]]) for r in rows])
    print(f"{len(plots)} plots; exterior: base {base['ID']}, roof {roof['ID']}, door {door['ID']}, "
          f"window {window['ID']} x{sum(1 for r in rows if r[0] == 'window')}, door hooks {sum(1 for r in rows if r[0] == 'door_hook')}")


if __name__ == "__main__":
    main(*sys.argv[1:3])
