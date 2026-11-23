"""Extract retail Blizzard addons (and the addons they depend on) from CASC, as the original files.

    python -I extract_ui.py <RetailAssetProbe.exe> <RetailCasc.ini> <community-listfile.csv> <out-dir> Blizzard_FriendsFrame [...]

Follows each addon's TOC (mainline variant first), the <Include>/<Script> files its XML names, and its
Dependencies/RequiredDeps. OptionalDeps are followed only when also named on the command line. Files keep their
retail paths under <out-dir>, lowercased as the listfile has them. Already extracted files are skipped, so reruns
are cheap. The ini can be a local install (mode=local, path=<retail install>) or the CDN build.
"""
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # -I leaves the script's directory off the path
import tocutil  # noqa: E402

TOC_ORDER = ("{a}_mainline.toc", "{a}.toc")


def main(probe, ini, listfile, out, *wanted):
    ids = {}
    for line in open(listfile, encoding="utf-8", errors="replace"):
        i, _, p = line.rstrip().partition(";")
        if p.startswith("interface/"):
            ids[p] = i
    root_addons = {w.lower() for w in wanted}

    staging = os.path.join(out, "_batch")

    def fetch(paths):
        # One probe run per batch: opening CASC costs seconds, reading a file does not.
        todo = [p for p in paths if not os.path.exists(os.path.join(out, p)) and p in ids]
        if todo:
            os.makedirs(staging, exist_ok=True)
            names = [f"retail/{ids[p]}.{p.rsplit('.', 1)[-1]}" for p in todo]
            listing = os.path.join(staging, "list.txt")
            open(listing, "w").write("\n".join(names) + "\n")
            subprocess.run([probe, ini, "--extract-batch", listing, staging], cwd=os.path.dirname(ini), capture_output=True)
            for p, n in zip(todo, names):
                src = os.path.join(staging, n.split("/", 1)[1])
                if os.path.exists(src):
                    os.makedirs(os.path.dirname(os.path.join(out, p)), exist_ok=True)
                    os.replace(src, os.path.join(out, p))
        return [read(p) for p in paths]

    def read(path):
        dest = os.path.join(out, path)
        return open(dest, "rb").read().decode("utf-8", "replace") if os.path.exists(dest) else None

    def extract(path):
        return fetch([path])[0]

    seen_addons, seen_files, missing = set(), set(), []

    def walk_files(paths):
        # Breadth-first so each level of XML includes is one batch.
        while paths:
            paths = [p for p in dict.fromkeys(paths) if p not in seen_files]
            seen_files.update(paths)
            nxt = []
            for p, text in zip(paths, fetch(paths)):
                if text is None:
                    missing.append(p)
                elif p.endswith(".xml"):
                    base = p.rsplit("/", 1)[0]
                    for ref in re.findall(r'<(?:Include|Script)\s[^>]*file="([^"]+)"', text):
                        nxt.append(norm(base, ref))
            paths = nxt

    queue = list(root_addons)
    while queue:
        addon = queue.pop(0)
        if addon in seen_addons:
            continue
        seen_addons.add(addon)
        base = f"interface/addons/{addon}"
        toc = text = None
        for t in TOC_ORDER:  # a listed variant can be absent from the installed build
            toc = f"{base}/{t.format(a=addon)}"
            text = extract(toc)
            if text:
                break
        if not text:
            missing.append(base + "/<toc>")
            continue
        seen_files.add(toc)
        files = []
        for line in text.splitlines():
            line = line.strip()
            m = re.match(r"##\s*(Dependencies|RequiredDeps|Dep\w*|OptionalDeps)\s*:\s*(.*)", line, re.I)
            if m:
                for dep in re.split(r"[,\s]+", re.sub(r"\[[^\]]*\]", "", m.group(2)).lower()):
                    if dep and (m.group(1).lower() != "optionaldeps" or dep in root_addons):
                        queue.append(dep)
            elif tocutil.toc_entry(line):
                files.append(norm(base, tocutil.toc_entry(line)))
        walk_files(files)
    print(f"{len(seen_addons)} addons, {len(seen_files) - len(missing)} files -> {out}")
    for m in missing:
        print("missing", m)


def norm(base, ref):
    parts = (base + "/" + ref.replace("\\", "/")).lower().split("/")
    stack = []
    for p in parts:
        if p == "..":
            stack.pop()
        elif p and p != ".":
            stack.append(p)
    return "/".join(stack)


if __name__ == "__main__":
    if len(sys.argv) < 6:
        sys.exit(__doc__)
    main(*sys.argv[1:])
