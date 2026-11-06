"""Stage verified decoded originals as size-capped MPQs. Never changes the client tree."""
import argparse
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "MountStudio"))
from worker import Storm, file_hash, save_json, MAX_ASSET, MAX_ARCHIVE


def package(client, output, dll, audit):
    client, output = Path(client).resolve(), Path(output).resolve()
    if output.exists():
        raise ValueError("Use a new staging directory")
    if any((client / "Data").glob("patch-Retail-*.MPQ")) or any((client / "Data").glob("patch-Forever-*.MPQ")):
        raise ValueError("Base Retail MPQs already installed; future exports need distinct patch names, not base replacement")
    cache = client / "Data/Retail/Cache"
    subprocess.run([str(audit), str(cache)], check=True)
    sources = {}
    for path in sorted(cache.rglob("id-*.bin")):
        relative = path.relative_to(cache)
        parts = relative.parts
        if parts[-2] != "strict":
            continue
        scope = next((part for part in parts if re.fullmatch(r"[a-z_]+-[a-f0-9]{32}", part)), None)
        locale = parts[-3]
        if not scope or not locale.isdecimal():
            raise ValueError(f"Unrecognized decoded cache path: {path}")
        file_id = path.stem.removeprefix("id-")
        if not file_id.isdecimal():
            raise ValueError(f"Invalid FileDataID: {path}")
        key = f"EsteriaRetail\\{scope}\\{locale}\\{file_id}.bin"
        if key in sources:
            raise ValueError(f"Duplicate packed key: {key}")
        sources[key] = (path, True)
    # Installed enUS offline originals may not have been requested through the online cache yet.
    for folder in (client / "Data/Retail/Assets").glob("wow-*"):
        if not re.fullmatch(r"wow-[a-f0-9]{32}", folder.name):
            continue
        for path in folder.glob("*.bin"):
            if path.stem.isdecimal():
                sources.setdefault(f"EsteriaRetail\\{folder.name}\\2\\{path.name}", (path, False))
    if not sources:
        raise ValueError("No decoded originals to pack")
    output.mkdir(parents=True)
    raw = output / "originals"
    raw.mkdir()
    entries, manifest = [], []
    for index, (key, (path, wrapped)) in enumerate(sorted(sources.items()), 1):
        before = path.stat()
        payload = path.read_bytes()
        if wrapped:
            magic, size, _checksum = struct.unpack_from("<IIQ", payload)
            if magic != 0x31434457 or size > MAX_ASSET or len(payload) != size + 16:
                raise ValueError(f"Invalid decoded original: {path}")
            payload = payload[16:]
        if len(payload) > MAX_ASSET:
            raise ValueError(f"Oversized original: {path}")
        target = raw / key.replace("\\", "/")
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
        expected = file_hash(target)
        after = path.stat()
        if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
            raise ValueError(f"Source changed while staging: {path}")
        entries.append((key, target, expected))
        manifest.append({"name": key, "source": str(path), "source_sha256": file_hash(path),
                         "sha256": expected, "bytes": len(payload), "wrapped": wrapped})
        if index % 2000 == 0:
            print(f"Staged {index}/{len(sources)} originals", flush=True)
    groups = []
    for product in ("wow", "wow_classic_beta"):
        group, total = [], 0
        for entry in entries:
            if not entry[0].split("\\")[1].startswith(product + "-"):
                continue
            size = entry[1].stat().st_size
            if group and total + size > MAX_ARCHIVE:
                groups.append((product, group)); group, total = [], 0
            group.append(entry); total += size
        if group:
            groups.append((product, group))
    if sum(len(group) for _, group in groups) != len(entries):
        raise ValueError("An original belongs to an unsupported product")
    storm, archives, serial = Storm(dll), [], {}
    for product, group in groups:
        serial[product] = serial.get(product, 0) + 1
        label = "Retail" if product == "wow" else "Forever"
        target = output / f"patch-{label}-{serial[product]:03}.MPQ"
        print(f"Compressing and fully verifying {target.name}: {len(group)} originals", flush=True)
        storm.pack(target, group)
        archives.append({"name": target.name, "sha256": file_hash(target), "bytes": target.stat().st_size,
                         "entries": len(group)})
        save_json(output / "archives.json", archives)
    save_json(output / "originals.json", manifest)
    save_json(output / "receipt.json", {"client": str(client), "archives": archives,
              "originals": len(manifest), "raw_bytes": sum(row["bytes"] for row in manifest),
              "all_entries_sha256_readback": True, "installed": False})
    print(json.dumps({"archives": archives, "originals": len(manifest)}), flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--stormlib", type=Path, required=True)
    parser.add_argument("--audit", type=Path, required=True, help="CdnCacheTrim.exe integrity checker")
    args = parser.parse_args()
    package(args.client, args.out, args.stormlib, args.audit)
