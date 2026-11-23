"""Packs the client's loose Retail/HotS originals into one patch MPQ that the client reads before the loose files.

  Data\\Retail\\Cache\\Retail\\<product>-<build>\\decoded\\<locale>\\strict\\id-<FDID>.bin
      -> EsteriaRetail\\<product>-<build>\\<locale>\\<FDID>.bin   (CascSource::PackedName, same keys as pack_cache.py)
  Data\\HotS\\originals\\<path>
      -> EsteriaHotS\\<product>-<build>\\<path>

Files already served by a patch MPQ are not packed twice. With --delete, every loose file whose key a patch MPQ now
serves with identical bytes is removed (the decoded cache only holds copies; HotS originals are rebuilt by
Tools/HotSMounts/client_mounts.py). The decoded 'tables' cache and CascLib's raw 'data' cache stay: the client only
reads packed copies for strict FileDataID reads.

  py -I pack_loose.py --client "G:\\3.3.5a - Dev" [--delete]
"""
import argparse, struct, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "MpqMerge"))
import mpq_merge as m  # noqa: E402  (StormLib bindings + client patch order)

ARCHIVE = "Data\\patch-RetailLoose.MPQ"
MAGIC = 0x31434457  # WDC1 decoded cache header (Engine/Source/Retail/DecodedCache.cpp)


def ini(path):
    return dict(line.split("=", 1) for line in path.read_text().splitlines() if "=" in line)


def sources(client):
    """key -> (loose file, wrapped in a decoded-cache header)"""
    out = {}
    retail = client / "Data" / "Retail"
    cfg = ini(retail / "RetailCasc.ini")
    scope = f"{cfg['product']}-{cfg['build']}"
    decoded = retail / cfg["path"] / scope / "decoded"
    for locale in decoded.iterdir() if decoded.is_dir() else []:
        for p in (locale / "strict").glob("id-*.bin"):
            out[f"EsteriaRetail\\{scope}\\{locale.name}\\{p.stem[3:]}.bin"] = (p, True)
    hots = client / "Data" / "HotS"
    if (hots / "HotSCasc.ini").is_file():
        cfg = ini(hots / "HotSCasc.ini")
        root = hots / cfg["path"]
        for p in root.rglob("*"):
            if p.is_file():
                out[f"EsteriaHotS\\{cfg['product']}-{cfg['build']}\\" + "\\".join(p.relative_to(root).parts)] = (p, False)
    return out


def payload(path, wrapped):
    data = path.read_bytes()
    if not wrapped:
        return data
    tag, count, _checksum = struct.unpack_from("<IIQ", data)
    body = data[16:]
    # shortcut: the FNV-1a checksum isn't re-verified (pure Python would take hours over 4 GB); the client already
    # rejected corrupt entries when it read them, and tag + length catch truncation.
    if tag != MAGIC or count != len(body):
        raise SystemExit(f"corrupt decoded cache file: {path}")
    return body


def served(client, keys):
    """key -> archive (relative) that the client would serve it from, for keys any patch MPQ holds."""
    found = {}
    for rel in m.client_order(client):
        a = m.Archive(client / rel)
        names = {m.key(n) for n, _, _ in a.files()}
        a.close()
        for k in keys:
            if k not in found and m.key(k) in names:
                found[k] = rel
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--client", type=Path, default=m.CLIENT)
    ap.add_argument("--delete", action="store_true", help="remove loose files a patch MPQ now serves byte-identically")
    args = ap.parse_args()
    client = args.client
    src = sources(client)
    longest = max((len(k) for k in src), default=0)
    if longest >= 260:
        raise SystemExit(f"a key is {longest} characters; MPQ names must stay under 260")
    have = served(client, src)
    new = {k: v for k, v in src.items() if k not in have}
    print(f"{len(src)} loose originals, {len(have)} already in patch MPQs, {len(new)} to pack")
    target = client / ARCHIVE
    if new:
        if target.exists():
            raise SystemExit(f"{target} exists; pack into a new name or fold it in with Tools/MpqMerge first")
        tmp = target.with_suffix(".partial")
        tmp.unlink(missing_ok=True)
        h = m.H()
        if not m.storm.SFileCreateArchive(str(tmp), 0x00100000 | 0x01000000, len(new) + 16, m.c.byref(h)):
            raise SystemExit(f"create {tmp}: {m.c.get_last_error()}")
        try:
            for i, (k, (p, wrapped)) in enumerate(sorted(new.items()), 1):
                data = payload(p, wrapped)
                fh = m.H()
                if not m.storm.SFileCreateFile(h, k.encode("latin1"), 0, len(data), 0, 0x200, m.c.byref(fh)):
                    raise SystemExit(f"add {k}: {m.c.get_last_error()}")
                if data and not m.storm.SFileWriteFile(fh, data, len(data), 0x02):
                    raise SystemExit(f"write {k}: {m.c.get_last_error()}")
                if not m.storm.SFileFinishFile(fh):
                    raise SystemExit(f"finish {k}: {m.c.get_last_error()}")
                if i % 5000 == 0:
                    print(f"  packed {i}/{len(new)}", flush=True)
        finally:
            m.storm.SFileCloseArchive(h)
        tmp.rename(target)
        have.update({k: ARCHIVE for k in new})
        print(f"wrote {target} ({target.stat().st_size / 2**30:.2f} GB)")
    # Byte check of every key against the archive that serves it; only checked files may be deleted.
    by_archive = {}
    for k, rel in have.items():
        by_archive.setdefault(rel, []).append(k)
    removable, mismatched = [], []
    for rel, keys in by_archive.items():
        a = m.Archive(client / rel)
        for k in keys:
            p, wrapped = src[k]
            (removable if a.read(k) == payload(p, wrapped) else mismatched).append(p)
        a.close()
    print(f"verified {len(removable)} loose files match their packed copy; {len(mismatched)} differ")
    for p in mismatched[:20]:
        print("  differs (kept):", p)
    if args.delete:
        freed = 0
        for p in removable:
            freed += p.stat().st_size
            p.unlink()
        print(f"deleted {len(removable)} loose files, {freed / 2**30:.2f} GB")


if __name__ == "__main__":
    main()
