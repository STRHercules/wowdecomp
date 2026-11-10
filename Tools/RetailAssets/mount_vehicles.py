"""Append passenger vehicles for retail utility mounts to a Vehicle.dbc / VehicleSeat.dbc pair.

Usage: mount_vehicles.py <Vehicle.dbc> <VehicleSeat.dbc> <out-dir>
Seats sit on the retail model's own seat attachments (40+). NPC seats copy the Traveler's Tundra Mammoth's vendor
seat (2764); player seats copy the X-53 Touring Rocket's passenger seat (7770). The same IDs go to client and server.
"""
import struct
import sys
from pathlib import Path

NPC_SEAT, PLAYER_SEAT, TEMPLATE_VEHICLE = 2764, 7770, 312
# VehicleSeat.AttachmentID indexes the client's seat table (VehiclePassenger_C.cpp s_seatAttachments), not the model's
# attachment IDs; model attachments 37-46 are indexes 11-20.
SEAT_INDEX = {-1: -1, 20: 0, 34: 1, 19: 2, 21: 3, 22: 4, 17: 5, 23: 6, 24: 7, 25: 8, 15: 9, 16: 10,
              **{model: model - 26 for model in range(37, 47)}}
ORIGIN, MOUNT, STAND = -1, 91, 0  # attachment -1 seats the passenger on the mount's origin (whole-mount overlays)
# vehicle id: [(seat id, template seat, model attachment, offset xyz, ride animation)]; index 0 stays empty.
VEHICLES = {
    # Grand Expedition Yak: retail's sidecar seats (travelersyakseat1/2) on 40/41, goods stalls
    # (travelersyakvendor1/2) on 42/43, and the two vendors sitting in the sidecars.
    910001: [(910001, NPC_SEAT, 40, (0, 0, 0), STAND), (910002, NPC_SEAT, 41, (0, 0, 0), STAND),
             (910010, NPC_SEAT, 42, (0, 0, 0), STAND), (910011, NPC_SEAT, 43, (0, 0, 0), STAND),
             (910012, NPC_SEAT, 40, (0, 0, 0.2), MOUNT), (910013, NPC_SEAT, 41, (0, 0, 0.2), MOUNT)],
    910002: [(910003, NPC_SEAT, 40, (0, 0, 0), STAND)],                                       # Mighty Caravan Brutosaur
    910003: [(910004, NPC_SEAT, 40, (0, 0, 0), STAND)],                                       # Trader's Gilded Brutosaur
    910004: [(910005, NPC_SEAT, 40, (0, 0, 0), STAND)],                                       # Sporebearer Fungal Strider
    # Grizzly Hills Packmaster: retail's anvil and bag models are drawn in the bear's own space.
    910005: [(910006, NPC_SEAT, ORIGIN, (0, 0, 0), STAND), (910007, NPC_SEAT, ORIGIN, (0, 0, 0), STAND)],
    910006: [(910008, NPC_SEAT, 40, (0, 0, 0), MOUNT), (910009, PLAYER_SEAT, 42, (0, 0, 0), MOUNT)],  # Hearthkeeper
}


def read(path):
    data = Path(path).read_bytes()
    if data[:4] != b"WDBC":
        raise SystemExit(f"{path}: not a DBC")
    count, fields, size, strings = struct.unpack_from("<4I", data, 4)
    rows = [bytearray(data[20 + i * size:20 + (i + 1) * size]) for i in range(count)]
    return rows, fields, size, data[20 + count * size:]


def write(path, rows, fields, size, strings):
    Path(path).write_bytes(b"WDBC" + struct.pack("<4I", len(rows), fields, size, len(strings)) + b"".join(rows) + strings)


def main():
    vehicle_path, seat_path, out = sys.argv[1], sys.argv[2], Path(sys.argv[3])
    vehicles, vfields, vsize, vstrings = read(vehicle_path)
    seats, sfields, ssize, sstrings = read(seat_path)
    by_vehicle = {struct.unpack_from("<I", r)[0]: r for r in vehicles}
    by_seat = {struct.unpack_from("<I", r)[0]: r for r in seats}
    taken = set(by_vehicle) & set(VEHICLES) | set(by_seat) & {s[0] for v in VEHICLES.values() for s in v}
    if taken:
        raise SystemExit(f"IDs already present: {sorted(taken)}")
    for vehicle_id, seat_list in VEHICLES.items():
        row = bytearray(by_vehicle[TEMPLATE_VEHICLE])
        struct.pack_into("<I", row, 0, vehicle_id)
        struct.pack_into("<8I", row, 24, 0, *[s[0] for s in seat_list], *[0] * (7 - len(seat_list)))
        vehicles.append(row)
        for seat_id, template, attachment, offset, ride in seat_list:
            seat = bytearray(by_seat[template])
            struct.pack_into("<I", seat, 0, seat_id)
            struct.pack_into("<i3f", seat, 8, SEAT_INDEX[attachment], *offset)
            struct.pack_into("<i", seat, 0x40, ride)  # RideAnimLoop
            seats.append(seat)
    out.mkdir(parents=True, exist_ok=True)
    write(out / "Vehicle.dbc", vehicles, vfields, vsize, vstrings)
    write(out / "VehicleSeat.dbc", seats, sfields, ssize, sstrings)
    check, *_ = read(out / "VehicleSeat.dbc")
    assert struct.unpack_from("<I", check[-1], 0)[0] == 910009 and struct.unpack_from("<i", check[-1], 8)[0] == 16
    assert SEAT_INDEX[40] == 14 and SEAT_INDEX[25] == 8  # s_seatAttachments[14] == 40, [8] == 25
    print("vehicles", len(vehicles), "seats", len(seats))


if __name__ == "__main__":
    main()
