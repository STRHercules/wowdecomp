"""Fold runs of patch MPQs in a 3.3.5 client Data folder into single larger archives.

Priority model (WoW/Source/WowServices/PatchFiles.cpp + Client.cpp OpenArchives):
Data\\patch-*.MPQ and Data\\<loc>\\patch-<loc>-*.MPQ are sorted together by full relative path,
case-insensitively, descending; the first in that list wins. patch.MPQ / patch-<loc>.MPQ come last.

Every contiguous run of that order between the KEEP archives becomes one archive named "<lowest member>-All.MPQ",
which sorts directly below the lowest member, so if a tool later recreates any former member, the fresh copy
still beats the stale copy in the merged archive. Round 1 (2026-11-17) used explicit groups named *-Merged.MPQ;
their originals and manifest are in "_backups\\mpq-merge-20261117".

  py -I mpq_merge.py plan
  py -I mpq_merge.py build  --out G:\\mpq-merge-staging
  py -I mpq_merge.py verify --out G:\\mpq-merge-staging     (reads every file twice, compares bytes)
  py -I mpq_merge.py swap   --out G:\\mpq-merge-staging --backup "G:\\3.3.5a - Dev\\_backups\\mpq-merge-<date>"
"""
import argparse, ctypes as c, hashlib, json, re, shutil, sys
from pathlib import Path

CLIENT = Path(r"G:\3.3.5a - Dev")
LOCALE = "enUS"
DLL = CLIENT / "StormLib.dll"  # needs SFileCreateArchive2-era StormLib (v9) for V2 archives

# Never merged: Blizzard's stock patches (delete markers, BugReport checks their sizes), the hub archives tools
# patch in place, and PATCH-V and CoA (entries with no recoverable names; make_slice.py also reads CoA by name).
KEEP = {r.lower() for r in [
    "Data\\patch.MPQ", "Data\\patch-2.MPQ", "Data\\patch-3.MPQ", "Data\\patch-Z.MPQ", "Data\\PATCH-V.mpq",
    "Data\\Patch-ZZZZZZZZZZZZZZZZZZZZZZZZCoA.MPQ",
    f"Data\\{LOCALE}\\patch-{LOCALE}.MPQ", f"Data\\{LOCALE}\\patch-{LOCALE}-2.MPQ",
    f"Data\\{LOCALE}\\patch-{LOCALE}-3.MPQ", f"Data\\{LOCALE}\\patch-{LOCALE}-Z.MPQ"]}
GROUPS = []  # filled by plan(): contiguous runs (highest priority first) of two or more non-KEEP archives
SPECIAL = {"(listfile)", "(attributes)", "(signature)", "(user data)"}
NAMES = []  # extra listfiles (--names, files or folders of *.txt): names an archive's own (listfile) misses

H, U = c.c_void_p, c.c_uint32
class FindData(c.Structure):
    _fields_ = [("cFileName", c.c_char * 260), ("szPlainName", c.c_char_p), ("dwHashIndex", U), ("dwBlockIndex", U),
                ("dwFileSize", U), ("dwFileFlags", U), ("dwCompSize", U), ("dwFileTimeLo", U), ("dwFileTimeHi", U),
                ("lcLocale", U), ("dwCrc32", U), ("dwDataOffset", U)]

storm = c.WinDLL(str(DLL), use_last_error=True)
for name, args, res in [
    ("SFileOpenArchive", [c.c_wchar_p, U, U, c.POINTER(H)], c.c_bool), ("SFileCloseArchive", [H], c.c_bool),
    ("SFileFindFirstFile", [H, c.c_char_p, c.POINTER(FindData), c.c_wchar_p], H),
    ("SFileFindNextFile", [H, c.POINTER(FindData)], c.c_bool), ("SFileFindClose", [H], c.c_bool),
    ("SFileOpenFileEx", [H, c.c_char_p, U, c.POINTER(H)], c.c_bool), ("SFileGetFileSize", [H, c.POINTER(U)], U),
    ("SFileReadFile", [H, c.c_void_p, U, c.POINTER(U), H], c.c_bool), ("SFileCloseFile", [H], c.c_bool),
    ("SFileCreateArchive", [c.c_wchar_p, U, U, c.POINTER(H)], c.c_bool),
    ("SFileCreateFile", [H, c.c_char_p, c.c_uint64, U, U, U, c.POINTER(H)], c.c_bool),
    ("SFileWriteFile", [H, c.c_void_p, U, U], c.c_bool), ("SFileFinishFile", [H], c.c_bool),
    ("SFileFlushArchive", [H], c.c_bool), ("SFileAddListFile", [H, c.c_wchar_p], U)]:
    fn = getattr(storm, name); fn.argtypes = args; fn.restype = res


def fail(msg):
    raise SystemExit(f"ERROR: {msg}")


def key(name):
    return name.replace("/", "\\").upper()


def client_order(root):
    """Patch archives in client priority order (index 0 wins), as paths relative to the client folder."""
    data = root / "Data"
    rel = [f"Data\\{p.name}" for p in data.iterdir() if p.is_file() and re.fullmatch(r"(?i)patch-.*\.mpq", p.name)]
    loc = data / LOCALE
    if loc.is_dir():
        rel += [f"Data\\{LOCALE}\\{p.name}" for p in loc.iterdir()
                if p.is_file() and re.fullmatch(rf"(?i)patch-{LOCALE}-.*\.mpq", p.name)]
    rel.sort(key=str.lower, reverse=True)
    rel += [r for r in ("Data\\patch.MPQ", f"Data\\{LOCALE}\\patch-{LOCALE}.MPQ") if (root / r).is_file()]
    return rel


def merged_name(group):
    stem = re.sub(r"(?i)-merged$", "", group[-1][: -len(".mpq")])
    return stem + "-All.MPQ"


class Archive:
    def __init__(self, path):
        self.path, self.h = Path(path), H()
        if not storm.SFileOpenArchive(str(path), 0, 0x100, c.byref(self.h)):  # MPQ_OPEN_READ_ONLY
            fail(f"open {path}: {c.get_last_error()}")
        self.named = False

    def files(self):
        d, out = FindData(), []
        f = storm.SFileFindFirstFile(self.h, b"*", c.byref(d), None)
        while f:
            out.append((d.cFileName.split(b"\0")[0].decode("latin1"), d.dwFileFlags, d.lcLocale))
            if not storm.SFileFindNextFile(f, c.byref(d)):
                break
        if f:
            storm.SFileFindClose(f)
        if NAMES and not self.named and any(re.fullmatch(r"File\d{8}\..*", n) for n, _, _ in out):
            # recovers entries the archive's own (listfile) leaves out; slow, so only when needed
            self.named = True
            for listfile in NAMES:  # StormLib loses names when fed one huge listfile, so names come in chunks
                storm.SFileAddListFile(self.h, str(listfile))
            return self.files()
        return out

    def read(self, name):
        fh = H()
        if not storm.SFileOpenFileEx(self.h, name.encode("latin1"), 0, c.byref(fh)):
            fail(f"open {name} in {self.path}: {c.get_last_error()}")
        try:
            hi = U()
            size = storm.SFileGetFileSize(fh, c.byref(hi))
            if hi.value:
                fail(f"{name} in {self.path} is over 4 GB")
            buf, got = c.create_string_buffer(size or 1), U()
            if size and (not storm.SFileReadFile(fh, buf, size, c.byref(got), None) or got.value != size):
                fail(f"read {name} in {self.path}: {c.get_last_error()}")
            return buf.raw[:size]
        finally:
            storm.SFileCloseFile(fh)

    def close(self):
        storm.SFileCloseArchive(self.h)


def contents(arch):
    """key -> (stored name, flags) for real files; refuses archives the merge can't reproduce exactly."""
    out = {}
    for name, flags, loc in arch.files():
        if name in SPECIAL:
            continue
        if re.fullmatch(r"File\d{8}\..*", name):
            fail(f"{arch.path} has files with unknown names ({name}); cannot merge it without losing them")
        if loc != 0:
            fail(f"{arch.path}:{name} has locale {loc}; merge only handles neutral files")
        if flags & 0x02000000:
            fail(f"{arch.path}:{name} is a delete marker; merge does not reproduce those")
        out.setdefault(key(name), (name, flags))
    return out


def plan(root):
    order = client_order(root)
    pos = {r.lower(): i for i, r in enumerate(order)}
    if not GROUPS:
        run = []
        for r in order + ["Data\\patch.MPQ"]:  # sentinel KEEP entry closes the last run
            if r.lower() in KEEP:
                if len(run) > 1:
                    GROUPS.append(run)
                run = []
            else:
                run.append(r)
    names = []
    for g in GROUPS:
        idx = []
        for m in g:
            if m.lower() not in pos:
                fail(f"{m} is not in the client's patch list")
            idx.append(pos[m.lower()])
        if idx != list(range(idx[0], idx[0] + len(g))):
            fail(f"group starting {g[0]} is not a contiguous run in client order")
        name = merged_name(g)
        if (root / name).exists():
            fail(f"{name} already exists")
        # Must land right below its lowest member and above the next archive in the same pass-0 sort.
        new = sorted([r for r in order if r not in ("Data\\patch.MPQ", f"Data\\{LOCALE}\\patch-{LOCALE}.MPQ")] + [name],
                     key=str.lower, reverse=True)
        i = new.index(name)
        if new[i - 1].lower() != g[-1].lower():
            fail(f"{name} would not sort directly below {g[-1]} (it sorts below {new[i - 1]})")
        names.append(name)
    return order, names


def build(root, out):
    order, names = plan(root)
    out.mkdir(parents=True, exist_ok=True)
    manifest = {}
    for g, name in zip(GROUPS, names):
        target = out / Path(name).name
        if target.exists():
            print(f"skip {target} (exists)")
            continue
        archives = [Archive(root / m) for m in g]
        winners = {}  # key -> (archive index, stored name, flags), first (highest priority) wins
        for i, a in enumerate(archives):
            for k, (n, fl) in contents(a).items():
                winners.setdefault(k, (i, n, fl))
        tmp = target.with_suffix(".partial")
        tmp.unlink(missing_ok=True)
        h = H()
        # MPQ_CREATE_LISTFILE | MPQ_CREATE_ARCHIVE_V2 (64-bit offsets, same format as common-3.MPQ and patch.MPQ)
        if not storm.SFileCreateArchive(str(tmp), 0x00100000 | 0x01000000, len(winners) + 16, c.byref(h)):
            fail(f"create {tmp}: {c.get_last_error()}")
        try:
            for n_done, (k, (i, n, fl)) in enumerate(sorted(winners.items()), 1):
                data = archives[i].read(n)
                compress = bool(fl & 0x00000300)  # keep stored files stored; compressed/imploded ones get zlib
                fh = H()
                if not storm.SFileCreateFile(h, n.encode("latin1"), 0, len(data), 0, 0x200 if compress else 0, c.byref(fh)):
                    fail(f"add {n}: {c.get_last_error()}")
                if data and not storm.SFileWriteFile(fh, data, len(data), 0x02 if compress else 0):
                    fail(f"write {n}: {c.get_last_error()}")
                if not storm.SFileFinishFile(fh):
                    fail(f"finish {n}: {c.get_last_error()}")
                if n_done % 5000 == 0:
                    print(f"  {name}: {n_done}/{len(winners)}", flush=True)
        finally:
            if not storm.SFileCloseArchive(h):
                fail(f"close {tmp}: {c.get_last_error()}")
        for a in archives:
            a.close()
        tmp.rename(target)
        manifest[name] = {"members": g, "files": len(winners)}
        print(f"built {name}: {len(winners)} files from {len(g)} archives, {target.stat().st_size / 2**30:.2f} GB", flush=True)
    (out / "manifest.json").write_text(json.dumps({"groups": [{"merged": n, "members": g} for g, n in zip(GROUPS, names)]}, indent=1))


def verify(root, out):
    """Resolve every path of every merged member under the old and the new order and compare bytes."""
    order, names = plan(root)
    members = {m.lower(): gi for gi, g in enumerate(GROUPS) for m in g}
    merged = {names[gi].lower(): gi for gi in range(len(GROUPS))}
    new_order = sorted([r for r in order if r.lower() not in members and r not in ("Data\\patch.MPQ", f"Data\\{LOCALE}\\patch-{LOCALE}.MPQ")]
                       + names, key=str.lower, reverse=True) + [r for r in order if r in ("Data\\patch.MPQ", f"Data\\{LOCALE}\\patch-{LOCALE}.MPQ")]
    path_of = lambda r: out / Path(r).name if r.lower() in merged else root / r
    listings = {}
    for r in set(order) | set(new_order):
        a = Archive(path_of(r))
        listings[r.lower()] = {key(n) for n, _, _ in a.files() if n not in SPECIAL}
        a.close()
    for gi, name in enumerate(names):
        expect = set().union(*(listings[m.lower()] for m in GROUPS[gi]))
        if listings[name.lower()] != expect:
            fail(f"{name}: file set differs (missing {len(expect - listings[name.lower()])}, extra {len(listings[name.lower()] - expect)})")
    winner = lambda o, k: next(r for r in o if k in listings[r.lower()])
    check = {}  # (old archive, new archive) -> keys whose bytes must match
    for k in set().union(*(listings[m.lower()] for g in GROUPS for m in g)):
        old, new = winner(order, k), winner(new_order, k)
        if old.lower() in members:
            if merged.get(new.lower()) != members[old.lower()]:
                fail(f"{k}: was served by {old}, would now come from {new}")
            check.setdefault((old, new), []).append(k)
        elif old != new:
            fail(f"{k}: was served by {old}, would now come from {new}")
    total = 0
    for (old, new), keys in sorted(check.items()):
        a, b = Archive(path_of(old)), Archive(path_of(new))
        names_a = {key(n): n for n, _, _ in a.files()}
        names_b = {key(n): n for n, _, _ in b.files()}
        for k in keys:
            if hashlib.sha1(a.read(names_a[k])).digest() != hashlib.sha1(b.read(names_b[k])).digest():
                fail(f"{k}: bytes differ between {old} and {new}")
        a.close(); b.close()
        total += len(keys)
        print(f"  ok {len(keys):6d} files  {old} -> {new}", flush=True)
    print(f"VERIFIED: {total} files resolve to identical bytes; {len(order)} -> {len(new_order)} patch archives")


def record_redirects(root, members, name):
    """Data\\mpq-merged.json: old archive file name (lowercase) -> archive (relative to Data) now holding it. Tools that
    open archives by name follow it (EsteriaWoW tools/cars_mount_pack.py merged_archive); the client ignores it."""
    path = root / "Data" / "mpq-merged.json"
    moved = json.loads(path.read_text(encoding="utf-8")) if path.exists() else {}
    for m in members:
        moved[Path(m).name.lower()] = name[len("Data\\"):]
    path.write_text(json.dumps(moved, indent=1, sort_keys=True), encoding="utf-8")


def swap(root, out, backup):
    order, names = plan(root)
    for name in names:
        if not (out / Path(name).name).is_file():
            fail(f"missing built archive {name}")
    backup.mkdir(parents=True, exist_ok=True)
    for g, name in zip(GROUPS, names):
        if any((backup / m).exists() for m in g):
            fail(f"{backup} already holds members of {name}")
    shutil.copy2(out / "manifest.json", backup / f"manifest-{Path(names[0]).stem}.json")
    for g, name in zip(GROUPS, names):
        moved = []  # every member goes to the backup first, so a locked file rolls the whole group back
        try:
            for m in g:
                dest = backup / m
                dest.parent.mkdir(parents=True, exist_ok=True)
                (root / m).rename(dest)
                moved.append(m)
            (out / Path(name).name).rename(root / name)
        except OSError as e:
            for m in moved:
                (backup / m).rename(root / m)
            fail(f"swap of {name} rolled back: {e}")
        record_redirects(root, g, name)
        for m in g:
            if m.lower().endswith("-merged.mpq"):
                # a round-1 merge output: its originals are already in _backups\mpq-merge-20261117, and verify just
                # proved the new archive serves the same bytes, so keeping a third copy only costs disk space
                (backup / m).unlink()
        print(f"installed {name}; moved {len(g)} originals to {backup}")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["plan", "build", "verify", "swap"])
    ap.add_argument("--client", type=Path, default=CLIENT)
    ap.add_argument("--out", type=Path, default=Path(r"G:\mpq-merge-staging"))
    ap.add_argument("--backup", type=Path)
    ap.add_argument("--names", type=Path, nargs="+", default=[], help="extra listfiles, or folders of *.txt listfiles")
    ap.add_argument("--group", type=int, help="only this group index (as listed by plan), to build/verify/swap one at a time")
    a = ap.parse_args()
    NAMES = [f for p in a.names for f in (sorted(p.glob("*.txt")) if p.is_dir() else [p])]
    if a.group is not None:
        plan(a.client)
        GROUPS[:] = [GROUPS[a.group]]
    if a.cmd == "plan":
        order, names = plan(a.client)
        for g, n in zip(GROUPS, names):
            print(f"{n}  <=  {len(g)} archives")
        print(f"{len(order)} patch archives -> {len(order) - sum(map(len, GROUPS)) + len(GROUPS)}")
    elif a.cmd == "build":
        build(a.client, a.out)
    elif a.cmd == "verify":
        verify(a.client, a.out)
    else:
        if not a.backup:
            fail("--backup is required")
        swap(a.client, a.out, a.backup)
