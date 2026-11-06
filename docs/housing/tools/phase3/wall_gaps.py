"""Find stretches of each room kind's outline that no wall or corner piece covers (the gaps seen between rooms).

    python -I wall_gaps.py <room_kit.tsv> <raw dir>

<raw dir> holds original WMO roots as <fdid>.bin (RetailAssetProbe --extract). Each wall's MOHD bounding box is placed
the way HousingRooms.h places it (retail OffsetRot applied clockwise), then the room's four outline edges (the kind's
bounding box) are walked in 0.05 yd steps; a step no piece's footprint reaches is a gap. Angled (octagon) edges are
skipped: their outline is not the bounding box.
"""
import collections
import csv
import math
import os
import struct
import sys


def box(raw, fdid):
    data = open(os.path.join(raw, f"{fdid}.bin"), "rb").read()
    at = data.find(b"DHOM")
    return struct.unpack_from("<6f", data, at + 8 + 0x24)


def footprint(piece, b):
    yaw = -math.radians(float(piece["rot_deg"]))
    c, s = math.cos(yaw), math.sin(yaw)
    xs, ys = [], []
    for x in (b[0], b[3]):
        for y in (b[1], b[4]):
            xs.append(float(piece["x"]) + x * c - y * s)
            ys.append(float(piece["y"]) + x * s + y * c)
    return min(xs), min(ys), max(xs), max(ys)


def main(kit, raw):
    kinds = collections.defaultdict(list)
    for r in csv.DictReader(open(kit, encoding="utf-8"), delimiter="\t"):
        kinds[(int(r["kind"]), r["name"])].append(r)
    for (kind, name), pieces in sorted(kinds.items()):
        p0 = pieces[0]
        lo = (float(p0["min_x"]), float(p0["min_y"]))
        hi = (float(p0["max_x"]), float(p0["max_y"]))
        prints = [footprint(p, box(raw, p["solid"])) for p in pieces if p["type"] == "1"]
        if "Octagon" in name:
            continue
        gaps = []
        # The outline sits on the wall slabs: half a yard outside the floor box.
        edges = [("south", (lo[0] - 0.5, None)), ("north", (hi[0] + 0.5, None)),
                 ("east", (None, lo[1] - 0.5)), ("west", (None, hi[1] + 0.5))]
        for label, (fx, fy) in edges:
            run, start = 0, None
            span = (lo[1] - 0.5, hi[1] + 0.5) if fx is not None else (lo[0] - 0.5, hi[0] + 0.5)
            steps = int((span[1] - span[0]) / 0.05)
            for k in range(steps + 1):
                t = span[0] + k * 0.05
                x, y = (fx, t) if fx is not None else (t, fy)
                covered = any(a - 0.05 <= x <= c + 0.05 and b - 0.05 <= y <= d + 0.05 for a, b, c, d in prints)
                if not covered and start is None:
                    start = t
                if covered and start is not None:
                    if t - start > 0.1:
                        gaps.append((label, round(start, 2), round(t, 2)))
                    start = None
            if start is not None and span[1] - start > 0.1:
                gaps.append((label, round(start, 2), round(span[1], 2)))
        print(kind, name, "OK" if not gaps else gaps)


if __name__ == "__main__":
    main(*sys.argv[1:3])
