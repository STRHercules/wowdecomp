"""Pack this addon (its .toc, the files it lists and Art/*.blp) into a client patch MPQ:
python build_mpq.py <output.MPQ>. Run make_art.py first."""
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
    art = sorted((HERE / "Art").glob("*.blp"))
    if not (HERE / "Art" / "table.blp").exists():
        sys.exit("Art/ is empty: run make_art.py first.")
    files += [f"Art\\{path.name}" for path in art]
    extra = {"Interface\\AddOns\\" + HERE.name + "\\" + name: (HERE / name.replace("\\", "/")).read_bytes()
             for name in files}
    ei.pack_assets(ed.storm(), [], [], Path(sys.argv[1]), extra)
    print(f"Wrote {sys.argv[1]} ({len(extra)} files)")
