r"""WarCraft: Orcs & Humans (CD, 1.22h) -> a DOSBox Pure zip with the game installed, its sound set up, starting itself.

    python dos_warcraft.py <folder with the .cue/.bin> <out.zip>

What the CD's INSTALL + SETUP would do, without running them: the files come off the data track (ISO 9660, raw
2352-byte sectors), the LHA-packed .EXA programs are unpacked with 7-Zip, and CONFIG.WAR is the file SETUP.EXE wrote
under DOSBox Pure (2026-11-23) for its emulated Sound Blaster 16 (A220 I7 D1 H5): digital SB16, music SB Pro II/SB16.
The whole CD goes in too (CD\WAR.CUE + T1..T8.BIN, 8.3 names), mounted as D: by DOSBOX.BAT with IMGMOUNT: DOSBox
Pure does not mount images in a subfolder by itself. PATH.WAR is what the installer writes. Measured 2026-11-23: the
intro, menus and first mission sound the same with or without the disc (the music comes from the sound card), so its
audio tracks may never play; they ship because the owner wanted the full disc.
DOSBox Pure runs DOSBOX.BAT from the zip's root by itself.
"""
import re
import struct
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

SEVEN_ZIP = Path("C:/Program Files/7-Zip/7z.exe")
CONFIG_WAR = bytes.fromhex("0000020020020700010005000000ffff0000040020020700 0100ffffffffffff".replace(" ", ""))
assert len(CONFIG_WAR) == 32


def iso_files(track):
    """{path: bytes} of an ISO 9660 data track stored as raw MODE1/2352 sectors."""
    data = track.read_bytes()
    sector = lambda n: data[n * 2352 + 16:n * 2352 + 16 + 2048]
    read = lambda lba, size: b"".join(sector(lba + i) for i in range((size + 2047) // 2048))[:size]
    assert sector(16)[1:6] == b"CD001", "not an ISO 9660 data track"
    out = {}

    def walk(lba, size, prefix):
        block, i = read(lba, size), 0
        while i < len(block):
            length = block[i]
            if not length:
                i = (i // 2048 + 1) * 2048
                continue
            extent, bytes_ = struct.unpack_from("<I", block, i + 2)[0], struct.unpack_from("<I", block, i + 10)[0]
            name = block[i + 33:i + 33 + block[i + 32]]
            if name not in (b"\0", b"\1"):
                name = name.decode("ascii").split(";")[0]
                if block[i + 25] & 2:
                    walk(extent, bytes_, prefix + name + "/")
                else:
                    out[prefix + name] = read(extent, bytes_)
            i += length

    root = sector(16)[156:190]
    walk(struct.unpack_from("<I", root, 2)[0], struct.unpack_from("<I", root, 10)[0], "")
    return out


def main():
    source, out = Path(sys.argv[1]), Path(sys.argv[2])
    track = next(source.glob("*(Track 1).bin"))
    files = iso_files(track)
    with tempfile.TemporaryDirectory() as tmp, zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for packed in ("WAR.EXA", "SETUP.EXA", "DOS4GW.EXA"):
            (Path(tmp) / packed).write_bytes(files[packed])
            subprocess.run([str(SEVEN_ZIP), "e", "-y", f"-o{tmp}", str(Path(tmp) / packed)], check=True, capture_output=True)
        for exe in ("war.exe", "setup.exe", "dos4gw.exe"):
            z.write(Path(tmp) / exe, "WAR/" + exe.upper())
        for name, data in sorted(files.items()):
            if name.startswith(("DATA/", "DRIVERS/")):
                z.writestr("WAR/" + name, data)
        z.writestr("WAR/CONFIG.WAR", CONFIG_WAR)
        z.writestr("WAR/PATH.WAR", b"D:\\\r\n\x1a")  # the installer's: where the CD is
        cue = next(source.glob("*.cue")).read_text()
        bins = sorted(source.glob("*.bin"), key=lambda b: int(re.search(r"Track (\d+)", b.name).group(1)))
        for i, b in enumerate(bins, 1):
            cue = cue.replace(f'"{b.name}"', f'"T{i}.BIN"')
            z.write(b, f"CD/T{i}.BIN", compress_type=zipfile.ZIP_STORED)
        z.writestr("CD/WAR.CUE", cue)
        z.writestr("DOSBOX.BAT", "@echo off\r\nIMGMOUNT D C:\\CD\\WAR.CUE -t iso\r\nC:\r\ncd \\WAR\r\nWAR\r\n")
    print(out, out.stat().st_size >> 20, "MB")


if __name__ == "__main__":
    main()
