"""Pack this addon (its .toc and the files it lists) into a client patch MPQ: python build_mpq.py <output.MPQ>"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "Tools" / "EclipseImport"))
import eclipse_data as ed  # noqa: E402
import eclipse_install as ei  # noqa: E402

if __name__ == "__main__":
    toc = HERE / f"{HERE.name}.toc"
    files = [toc.name] + [line.strip() for line in toc.read_text(encoding="utf-8").splitlines()
                          if line.strip() and not line.startswith("#")]
    # Art the Lua loads by path (dressing room backgrounds) rides along too.
    files += [str(p.relative_to(HERE)).replace("/", "\\") for p in sorted((HERE / "Rooms").glob("*.blp"))]
    extra = {"Interface\\AddOns\\" + HERE.name + "\\" + name: (HERE / name).read_bytes() for name in files}
    ei.pack_assets(ed.storm(), [], [], Path(sys.argv[1]), extra)
    print(f"Wrote {sys.argv[1]} ({len(extra)} files)")
