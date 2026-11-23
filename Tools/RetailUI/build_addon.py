"""Assemble retail Blizzard addons (as extracted by extract_ui.py) into one 3.3.5a addon.

    python -I build_addon.py <extracted-root> <addon-src> <out-addons-dir> [--only addon,addon]

<addon-src> is wowdecomp/Addons/EsteriaRetailUI: its EsteriaRetailUI.toc header and the files it lists before the
"#@retail" line load first (the compatibility layer), the retail files are inserted at that line, and the files after
it load last (our providers and glue). Retail files are copied under retail/, keep their own relative <Include>/<Script>
paths, and have the globals they define renamed with a Retail prefix (isolate.py). The retail TOCs' mainline file
lists are used, in ORDER below.
"""
import os
import re
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # -I leaves the script's directory off the path
import isolate  # noqa: E402
import tocutil  # noqa: E402

# Retail's own load order: the shared framework the game loads before any addon, then the social addons.
ORDER = [
    "blizzard_sharedxmlbase", "blizzard_colors", "blizzard_menu", "blizzard_projectconstants", "blizzard_fonts_shared",
    "blizzard_printhandler", "blizzard_helpplate", "blizzard_narration", "blizzard_sharedxml", "blizzard_sharedxmlgame",
    "blizzard_objectapi", "blizzard_framexmlbase", "blizzard_framexmlutil", "blizzard_uipaneltemplates",
    "blizzard_accessibilitytemplates", "blizzard_timerunningutil",
    "blizzard_addfriend", "blizzard_socialuishared", "blizzard_recentallies", "blizzard_unitpopupshared",
    "blizzard_friendsframe", "blizzard_quickjoin", "blizzard_recruitafriend", "blizzard_communities", "blizzard_communitiessecure",
    "blizzard_guildcontrolui",
]


# Retail files whose job the stock client already does; loading them would replace working 3.3.5a globals.
SKIP = {
    r"retail\blizzard_sharedxmlbase\compat.lua",  # Lua 4 aliases (tinsert, wipe, ...) the stock client defines
}


def toc_path_of(root, addon):
    base = os.path.join(root, "interface", "addons", addon)
    for name in (f"{addon}_mainline.toc", f"{addon}.toc"):
        path = os.path.join(base, name)
        if os.path.exists(path):
            return path
    raise SystemExit(f"no toc for {addon}")


def toc_files(root, addon):
    base = os.path.join(root, "interface", "addons", addon)
    path = toc_path_of(root, addon)
    files = []
    for line in open(path, encoding="utf-8", errors="replace"):
        line = tocutil.toc_entry(line)
        if not line:
            continue
        line = line.replace("/", "\\")
        if os.path.exists(os.path.join(base, line.replace("\\", os.sep).lower())):
            entry = f"retail\\{addon}\\{line.lower()}"
            if entry not in SKIP:
                files.append(entry)
    return files


def main(root, src, out, *args):
    only = None
    if args[:1] == ("--only",):
        only = [a.strip().lower() for a in args[1].split(",")]
    addons = [a for a in ORDER if only is None or a in only]

    dest = os.path.join(out, "EsteriaRetailUI")
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    shutil.copytree(src, dest)

    # Only the files that actually load: the TOC entries and the XML they include, followed recursively.
    retail = [f for a in addons for f in toc_files(root, a)]
    texts, queue = {}, [f.replace("\\", "/") for f in retail]
    while queue:
        rel = queue.pop()
        if rel in texts:
            continue
        source = os.path.join(root, "interface", "addons", rel[len("retail/"):].replace("/", os.sep))
        if not os.path.exists(source):
            print(f"missing {rel}")
            continue
        texts[rel] = open(source, encoding="utf-8", errors="replace").read()
        if rel.endswith(".xml"):
            base = rel.rsplit("/", 1)[0]
            for ref in re.findall(r'<(?:Include|Script)\s[^>]*file="([^"]+)"', texts[rel]):
                queue.append(os.path.normpath(base + "/" + ref.replace("\\", "/")).replace("\\", "/").lower())

    # Isolate the retail globals from the stock UI and other addons (see isolate.py).
    defined = isolate.definitions(texts)
    # Stand-ins in Compat/*.xml for retail templates from addons not extracted (RetailActionButtonTemplate): the
    # retail files' references to the unprefixed name are renamed to reach them.
    for name in os.listdir(os.path.join(src, "Compat")):
        if name.endswith(".xml"):
            text = open(os.path.join(src, "Compat", name), encoding="utf-8").read()
            defined.update(n[len(isolate.PREFIX):] for n in isolate._XML_NAME.findall(text) if n.startswith(isolate.PREFIX))
    frames = sorted({n for k, t in texts.items() if k.endswith(".xml")
                     for n in isolate._XML_NAME.findall(t) if n in defined and len(n) >= 8}, key=len, reverse=True)
    for rel, text in texts.items():
        text = isolate.rename_lua(text, defined, frames) if rel.endswith(".lua") else isolate.rename_xml(text, defined, frames)
        path = os.path.join(dest, rel.replace("/", os.sep))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        open(path, "w", encoding="utf-8", newline="\n").write(text)
    open(os.path.join(dest, "renamed.txt"), "w", encoding="utf-8").write("\n".join(sorted(defined)))
    for table in ("RetailAtlas.txt", "RetailTextures.txt"):  # build/retail-ui, from make_atlas_table/make_texture_map
        path = os.path.join(os.path.dirname(os.path.abspath(root)), table)
        if os.path.exists(path):
            shutil.copy(path, os.path.join(dest, table))
    toc_path = os.path.join(dest, "EsteriaRetailUI.toc")
    lines = open(toc_path, encoding="utf-8").read().splitlines()
    at = lines.index("#@retail")
    lines[at:at + 1] = retail
    # The retail addons' saved variables, under their renamed names, become ours (header lines go before any file).
    saved = {}
    for a in addons:
        for line in open(toc_path_of(root, a), encoding="utf-8", errors="replace"):
            key, _, names = line.strip().partition(":")
            if key.lower() in ("## savedvariables", "## savedvariablespercharacter"):
                saved.setdefault(key[3:], []).extend(
                    (isolate.PREFIX + n if n in defined else n) for n in (x.strip() for x in names.split(",")) if n)
    lines[1:1] = [f"## {key}: {', '.join(names)}" for key, names in saved.items()]
    open(toc_path, "w", encoding="utf-8", newline="\r\n").write("\n".join(lines) + "\n")
    print(f"{len(addons)} retail addons, {len(retail)} files -> {dest}")


if __name__ == "__main__":
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    main(*sys.argv[1:])
