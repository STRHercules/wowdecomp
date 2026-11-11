"""Write EsteriaMounts/kits.csv: retail state visual kits on installed mount displays, as far as the client shows them.

Usage: mount_kits.py <Data/Retail/Packs> <csv-root> <default-build> <output.csv> <models.txt>
  <csv-root>/<build>/<Table>.csv are wago.tools exports of CreatureDisplayInfo, SpellVisualKitEffect,
  SpellVisualKitModelAttach, SpellVisualEffectName and GradientEffect. Packs imported from another build's export
  (their recipe has fourth_texture) use their metadata_build; the rest use <default-build> (display IDs carry over).
Rows: display;m;attachment;model;scale;x;y;z (SpellVisualKitModelAttach; attachment -2 = a positioner path, centred
on the body) and display;e;r;g;b;alpha (GradientEffect: an additive wash of its middle colour at its first alpha,
over a body drawn at that alpha). models.txt lists the effect models to ship.
shortcut: positioner paths are centred but not animated (no orbit); animation kits, sounds and other effect types are
skipped.
"""
import csv
import json
import sys
from collections import defaultdict
from pathlib import Path

MODEL_ATTACH, GRADIENT = 2, 16


def load(root, build):
    rows = lambda name: list(csv.DictReader(open(root / build / f"{name}.csv", encoding="utf-8")))
    effects = defaultdict(list)
    for row in rows("SpellVisualKitEffect"):
        effects[row["ParentSpellVisualKitID"]].append((int(row["EffectType"]), row["Effect"]))
    return {"displays": {row["ID"]: row for row in rows("CreatureDisplayInfo")}, "effects": effects,
            "attach": {row["ID"]: row for row in rows("SpellVisualKitModelAttach")},
            "names": {row["ID"]: row for row in rows("SpellVisualEffectName")},
            "gradients": {row["ID"]: row for row in rows("GradientEffect")}}


def kit_rows(data, display, target):
    row = data["displays"].get(str(display))
    kit = row and row.get("StateSpellVisualKitID", "0")
    out, models = [], set()
    for kind, effect in data["effects"].get(kit, []) if kit and kit != "0" else []:
        if kind == MODEL_ATTACH and effect in data["attach"]:
            attach = data["attach"][effect]
            name = data["names"].get(attach["SpellVisualEffectNameID"])
            model = int(name["ModelFileDataID"]) if name else 0
            if model:
                scale = float(attach["Scale"] or 1) * float(name["Scale"] or 1)
                offset = [float(attach[f"Offset_{i}"] or 0) for i in range(3)]
                attachment = int(attach["AttachmentID"])
                if attachment == -1 and attach.get("PositionerID", "0") not in ("", "0"):
                    attachment = -2  # a positioner path around the body: the client centres it on the bounds
                out.append(f"{target};m;{attachment};{model};{scale:g};" + ";".join(f"{v:g}" for v in offset))
                models.add(model)
        elif kind == GRADIENT and effect in data["gradients"]:
            gradient = data["gradients"][effect]
            alpha = float(gradient["Alpha1"] or 1)
            colour = [min(1.0, float(gradient[f"Colors1_{c}"]) * alpha) for c in "rgb"]
            # the gradient's first alpha is how opaque retail draws the body under it
            out.append(f"{target};e;" + ";".join(f"{v:.3f}" for v in colour) + f";{max(0.05, min(1.0, alpha)):.3f}")
    return out, models


def main():
    packs, root, default, output, model_list = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3], Path(sys.argv[4]), Path(sys.argv[5])
    builds, lines, models = {}, [], set()
    for manifest_path in sorted(packs.glob("*/manifest.json")):
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        source, target = manifest["source"], manifest["target"]
        if target.get("kind") != "mount":
            continue
        build = source["metadata_build"] if "fourth_texture" in source else default
        if build not in builds:
            builds[build] = load(root, build)
        rows, used = kit_rows(builds[build], source["retail_display_id"], target["ids"]["CreatureDisplayInfo"])
        lines += rows
        models |= used
    assert all(";m;" in line or ";e;" in line for line in lines)
    output.write_text("# display;m;attachment;model;scale;x;y;z | display;e;r;g;b\n" + "\n".join(lines) + "\n", encoding="ascii")
    model_list.write_text("\n".join(map(str, sorted(models))) + "\n", encoding="ascii")
    print("kit rows", len(lines), "displays", len({line.split(";")[0] for line in lines}), "models", len(models))


if __name__ == "__main__":
    main()
