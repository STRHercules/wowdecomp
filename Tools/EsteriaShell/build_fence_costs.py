"""Write the Fence prices into a copy of the active additive ItemExtendedCost.dbc."""

import argparse
from pathlib import Path
import struct


# Keep in sync with rev_20261118235000000_world_pvp_fence_vendor.sql.
FENCE_COSTS = {951071: 1, 951072: 3}
EAR_TROPHY = 9510710
HEADER = struct.Struct("<4s4I")
ROW = struct.Struct("<16I")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base", type=Path, help="Current additive registry's ItemExtendedCost.dbc")
    parser.add_argument("output", type=Path, help="New registry's ItemExtendedCost.dbc")
    args = parser.parse_args()
    if args.base.resolve() == args.output.resolve():
        parser.error("Write to a new registry so the previous data stays available.")

    data = args.base.read_bytes()
    tag, count, fields, stride, string_size = HEADER.unpack_from(data)
    pool_start = HEADER.size + count * stride
    if (tag, fields, stride) != (b"WDBC", 16, ROW.size) or len(data) != pool_start + string_size:
        raise ValueError("Unexpected ItemExtendedCost.dbc format")

    rows = list(ROW.iter_unpack(data[HEADER.size:pool_start]))
    existing = {row[0]: row for row in rows}
    for entry, amount in FENCE_COSTS.items():
        row = (entry, 0, 0, 0, EAR_TROPHY, 0, 0, 0, 0, amount, 0, 0, 0, 0, 0, 0)
        if entry not in existing:
            rows.append(row)
        elif existing[entry] != row:
            raise ValueError(f"Extended cost {entry} is already assigned")

    with args.output.open("xb") as output:
        output.write(HEADER.pack(tag, len(rows), fields, stride, string_size))
        for row in sorted(rows, key=lambda entry: entry[0]):
            output.write(ROW.pack(*row))
        output.write(data[pool_start:])


if __name__ == "__main__":
    main()
