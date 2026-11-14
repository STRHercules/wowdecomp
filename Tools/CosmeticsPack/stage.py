"""Stage backpack unlock items, additive client registry and the dedicated Cosmetics MPQ.

Read-only against the installed client/database. Installation is a separate parent-owned operation.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import shutil
import struct
import sys

REPO = Path(__file__).resolve().parents[2]
SERVER = REPO.parent / "EsteriaWoW"
sys.path[:0] = [str(REPO / "Tools/ManastormPort"), str(REPO / "Tools/EclipseImport"),
               str(REPO / "Tools/RetailAssets")]
import manastorm_client as mc
import manastorm_port as db
import native_import as native
import backpack_icons

USE_SPELL = 3420001
ICON = "Interface\\Icons\\INV_Misc_Cape_19"
DATA_SQL = SERVER / "data/sql/updates/pending_db_world/rev_20261114120100000_cosmetics_backpack_items.sql"
ID_MANIFEST = Path(__file__).with_name("ids.json")
DEFAULT_FITS = Path(__file__).with_name("backpack-default-fits.txt")
STRING_FIELDS = {
    "Item": (), "ItemDisplayInfo": tuple(range(1, 7)) + tuple(range(15, 23)),
    "Spell": tuple(range(136, 152)) + tuple(range(153, 169)) + tuple(range(170, 186)) + tuple(range(187, 203)),
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def lua(value):
    return json.dumps(value, ensure_ascii=True)


def quote(value):
    return "'" + str(value).replace("\\", "\\\\").replace("'", "''") + "'"


def row_icon(row, display):
    return backpack_icons.icon_path(display) if row.get("source") and backpack_icons.resolve(row)[0] else ICON


def allocate(rows, existing):
    """Keep old assignments forever, including a missing/temporarily unconvertible source."""
    result = dict(existing)
    indexes = [entry["index"] for entry in result.values()]
    if len(indexes) != len(set(indexes)) or any(i < 1 or i >= 10000 for i in indexes):
        raise ValueError("Invalid stable ID manifest")
    next_index = max(indexes, default=0) + 1
    for slug in sorted({row["slug"] for row in rows} - result.keys()):
        result[slug] = {"index": next_index, "source": 3400000 + next_index,
                        "token": 3410000 + next_index, "display": 3400000 + next_index}
        next_index += 1
    for entry in result.values():
        i = entry["index"]
        if entry != {"index": i, "source": 3400000+i, "token": 3410000+i, "display": 3400000+i}:
            raise ValueError("Manifest ID formula changed")
    return result


def merge_registry(base, additions, fields, string_fields):
    """Append records/pool while preserving every existing record byte and old pool offset."""
    old_rows, old_pool = native.wdbc(base, fields)
    rows, pool = native.wdbc(additions, fields)
    if {r[0] for r in rows} & {r[0] for r in old_rows}:
        raise ValueError("ID already present in supplemental registry")
    start = 20 + len(old_rows)*fields*4
    added = bytearray(additions[20:20+len(rows)*fields*4])
    for i, row in enumerate(rows):
        for field in string_fields:
            if row[field]:
                if row[field] >= len(pool) or b"\0" not in pool[row[field]:]:
                    raise ValueError("Invalid generated string reference")
                struct.pack_into("<I", added, (i*fields+field)*4, row[field]+len(old_pool))
    merged = (struct.pack("<4s4I", b"WDBC", len(old_rows)+len(rows), fields, fields*4, len(old_pool)+len(pool))
              + base[20:start] + added + old_pool + pool)
    checked, checked_pool = native.wdbc(merged, fields)
    assert checked[:len(old_rows)] == old_rows and checked_pool[:len(old_pool)] == old_pool
    assert merged[20:start] == base[20:start]
    return merged


def spell_row():
    row = [0]*234
    row[0], row[28], row[46], row[68], row[71], row[86], row[133], row[224] = (
        USE_SPELL, 1, 1, 0xffffffff, 3, 1, 1, 1)
    return row


def build_tables(rows, ids, cape):
    items, displays = [], []
    display_text = {}
    for row in rows:
        ids_for_row = ids[row["slug"]]
        source, token, display = (ids_for_row[k] for k in ("source", "token", "display"))
        items += [[source, 4, 1, 0xffffffff, 0, display, 16, 0],
                  [token, 0, 8, 0xffffffff, 0, display, 0, 0]]
        record = [0]*25
        record[0], record[7] = display, 1
        index = len(displays)
        displays.append(record)
        display_text[index, 3] = cape
        display_text[index, 5] = row_icon(row, display).rsplit("\\", 1)[-1]
    spell_text = {(0,136): "Learn Backpack Appearance", (0,170): "Unlocks an appearance for cloak transmogrification."}
    return {
        "Item": native.make_wdbc(8, items),
        "ItemDisplayInfo": native.make_wdbc(25, displays, display_text),
        "Spell": native.make_wdbc(234, [spell_row()], spell_text),
    }


def world_sql(rows, ids):
    item_rows, mappings = [], []
    for row in rows:
        assigned = ids[row["slug"]]
        source, token, display = (assigned[k] for k in ("source", "token", "display"))
        name = row["name"]
        item_rows.append((source,4,1,name,display,4,16,0,0,"", "Cosmetic cloak appearance.", 0))
        item_rows.append((token,0,8,"Appearance: "+name,display,4,0,USE_SPELL,0,
                          "item_esteria_backpack_appearance", "Use: Unlocks the "+name+" cloak appearance for your account.",1))
        mappings.append((source,token))
    all_items = sorted(value for pair in mappings for value in pair)
    source_ids = sorted(source for source,_ in mappings)
    lines = ["-- Fortnite backpacks: unique account appearance tokens and their cloak transmog sources.",
             "-- Apply rev_20261114120000000_cosmetics_backpacks.sql first; restart the changed worldserver afterward.",
             "START TRANSACTION;", f"DELETE FROM `spell_dbc` WHERE `ID` = {USE_SPELL};",
             "INSERT INTO `spell_dbc` (`ID`, `CastingTimeIndex`, `RangeIndex`, `EquippedItemClass`, `Effect_1`, "
             "`ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Description_Lang_enUS`, `SchoolMask`) VALUES",
             f"({USE_SPELL},1,1,-1,3,1,1,'Learn Backpack Appearance','Unlocks an appearance for cloak transmogrification.',1);",
             "DELETE FROM `item_template` WHERE `entry` IN ("+",".join(map(str,all_items))+");",
             "INSERT INTO `item_template` (`entry`, `class`, `subclass`, `name`, `displayid`, `Quality`, "
             "`InventoryType`, `AllowableClass`, `AllowableRace`, `ItemLevel`, `RequiredLevel`, `stackable`, "
             "`maxcount`, `spellid_1`, `spelltrigger_1`, `spellcharges_1`, `ScriptName`, `description`, `bonding`) VALUES"]
    values=[]
    for entry,cls,sub,name,display,quality,inventory,spell,trigger,script,description,bonding in item_rows:
        values.append(f"({entry},{cls},{sub},{quote(name)},{display},{quality},{inventory},-1,-1,1,0,1,1,"
                      f"{spell},{trigger},0,{quote(script)},{quote(description)},{bonding})")
    lines.append(",\n".join(values)+";")
    lines += ["DELETE FROM `esteria_backpack_appearances` WHERE `source_item` IN ("+",".join(map(str,source_ids))+");",
              "INSERT INTO `esteria_backpack_appearances` (`source_item`, `token_item`) VALUES",
              ",\n".join(f"({source},{token})" for source,token in mappings)+";", "COMMIT;"]
    return "\n".join(lines)+"\n"


def wings(client):
    group = [int(x) for x in db.mysql("SELECT spell_id FROM spell_group WHERE id=9100 ORDER BY spell_id", "acore_world").split()]
    spells = client.table("Spell").rows
    visuals = client.table("SpellVisual").rows
    effects = client.table("SpellVisualEffectName").rows
    attachments = client.table("SpellVisualKitModelAttach").rows
    icons = client.table("SpellIcon").rows
    result=[]
    for spell in group:
        record,text = spells[spell]
        visual = visuals[record[131]][0]
        choices = [row for row,_ in attachments.values() if row[1] == visual[4] and row[3] == 16]
        if len(choices) != 1:
            raise ValueError(f"Wing {spell}: expected one state-kit attachment, found {len(choices)}")
        effect,effect_text = effects[choices[0][2]]
        model = effect_text.get(2, "")
        if not model:
            raise ValueError(f"Wing {spell}: missing model")
        if model.lower().endswith(".mdx"):
            model = model[:-4]+".m2"
        icon = icons.get(record[133], (None,{}))[1].get(1, ICON)
        result.append({"spell":spell,"name":text.get(136,"Wing "+str(spell)),"icon":icon,
                       "model":model,"scale":struct.unpack("<f",struct.pack("<I",effect[4]))[0]})
    if not result:
        raise ValueError("No exclusive cosmetic wings found")
    return result


def default_fits():
    fits = {}
    known = {entry["display"] for entry in json.loads(ID_MANIFEST.read_text()).values()}
    with DEFAULT_FITS.open(encoding="utf-8-sig", newline="") as source:
        for row in csv.reader((line for line in source if line.strip() and not line.startswith("#")), delimiter=";"):
            if len(row) != 6:
                raise ValueError("Invalid default backpack fit row")
            display = int(row[0])
            fit = tuple(map(float, row[2:]))
            if display in fits or display not in known:
                raise ValueError("Unknown or duplicate default backpack display")
            if not all(math.isfinite(v) for v in fit) or not 0.1 <= fit[0] <= 4 or any(abs(v) > 3 for v in fit[1:]):
                raise ValueError("Default backpack fit outside runtime limits")
            fits[display] = fit
    if fits.keys() != known:
        raise ValueError("Default fits must cover every backpack display")
    return fits


def generated_lua(rows, ids, wing_rows):
    fits = default_fits()
    lines=["-- Generated by Tools/CosmeticsPack/stage.py; stable IDs live in ids.json.","EsteriaCosmeticsData = {"]
    for row in rows:
        assigned=ids[row["slug"]]
        fields={"id":assigned["source"],"item":assigned["token"],"display":assigned["display"],
                "name":row["name"],"model":row["model"],"width":row["width"],"height":row["height"],
                "depth":row["depth"],"icon":row_icon(row,assigned["display"])}
        fields.update(zip(("fitScale", "fitX", "fitY", "fitZ"), fits.get(assigned["display"], (1, 0, 0, 0))))
        lines.append("    {"+", ".join(k+"="+(lua(v) if isinstance(v,str) else str(v)) for k,v in fields.items())+"},")
    lines.append("}")
    wing_lines=["-- Generated from the installed exclusive spell group 9100 and effective client visual graph.","EsteriaCosmeticsWings = {"]
    for fields in wing_rows:
        wing_lines.append("    {"+", ".join(k+"="+(lua(v) if isinstance(v,str) else str(v)) for k,v in fields.items())+"},")
    wing_lines.append("}")
    return "\n".join(lines)+"\n", "\n".join(wing_lines)+"\n"


def file_manifest(root):
    return {p.relative_to(root).as_posix():sha(p.read_bytes()) for p in sorted(root.rglob("*")) if p.is_file()}


def package(out, assets, rows, ids, client_root, storm):
    fits = default_fits()
    runtime="".join(f"{ids[r['slug']]['display']};{r['model']};{r['width']:.9g};{r['height']:.9g};{r['depth']:.9g};"
                    + ";".join(f"{v:.6f}" for v in fits.get(ids[r['slug']]['display'], (1, 0, 0, 0))) + "\n" for r in rows)
    entries={p.relative_to(assets/"patch-root").as_posix().replace("/","\\"):p.read_bytes()
             for p in (assets/"patch-root").rglob("*") if p.is_file()}
    entries["EsteriaCosmetics\\Backpacks.csv"]=runtime.encode()
    addon_data, _ = generated_lua(rows, ids, [])
    icon_entries,icon_report=backpack_icons.entries(rows,ids)
    entries.update(icon_entries)
    (out/"icons.json").write_text(json.dumps(icon_report,indent=2)+"\n")
    for addon_name in ("EsteriaCosmetics","EsteriaEmotes"):
        for p in (REPO/"Addons"/addon_name).rglob("*"):
            if p.is_file() and p.suffix.lower() in (".lua",".toc",".xml"):
                entries["Interface\\AddOns\\"+addon_name+"\\"+p.relative_to(REPO/"Addons"/addon_name).as_posix().replace("/","\\")]=p.read_bytes()
    entries["Interface\\AddOns\\EsteriaCosmetics\\EsteriaCosmeticsData.lua"]=addon_data.encode()
    sys.path.insert(0,str(REPO/"Tools/EsteriaShell"))
    import build_shell_mpq as shell
    entries.update(shell.patched_dragonui(str(client_root)))
    entries[shell.DUI+"modules\\esteria_microbuttons.lua"]=(REPO/"Addons/EsteriaShell/esteria_microbuttons.lua").read_bytes()
    entries["Interface\\AddOns\\Manastorm_Transmog\\Manastorm_Transmog.lua"]=(REPO/"Addons/Manastorm_Transmog/Manastorm_Transmog.lua").read_bytes()
    archive=out/"Patch-ZzzzzzzzzzzFortniteBackpacks.MPQ"
    if archive.exists(): raise FileExistsError(archive)
    storm.create_archive(archive,entries)
    handle=storm.open_archive(archive)
    try:
        for name,data in entries.items():
            if storm.read(handle,name)!=data: raise AssertionError("MPQ readback mismatch: "+name)
    finally: storm.dll.SFileCloseArchive(handle)
    return archive,entries


def repack(args):
    if args.out.exists(): raise FileExistsError(args.out)
    original=json.loads((args.repack_from/"receipt.json").read_text())
    assets=Path(original["asset_source"])
    rows=json.loads((assets/"catalog.json").read_text())["rows"]
    ids=original["ids"]
    rows=sorted(rows,key=lambda r:ids[r["slug"]]["index"])
    args.out.mkdir(parents=True)
    shutil.copytree(args.repack_from/"registry",args.out/"registry")
    shutil.copytree(args.repack_from/"additions",args.out/"additions")
    for name in ("RetailCasc.ini","world.sql"):
        shutil.copy2(args.repack_from/name,args.out/name)
    from eclipse_data import storm
    archive,entries=package(args.out,assets,rows,ids,args.client,storm())
    original.update(archive=str(archive),archive_sha256=sha(archive.read_bytes()),entries={k:sha(v) for k,v in entries.items()},
                    repacked_from=str(args.repack_from))
    (args.out/"receipt.json").write_text(json.dumps(original,indent=2)+"\n")
    print(json.dumps({"archive":str(archive),"entries":len(entries),"sha256":original["archive_sha256"]},indent=2))


def stage(args):
    catalog=json.loads((args.assets/"catalog.json").read_text())
    all_rows=catalog["rows"]
    existing=json.loads(ID_MANIFEST.read_text()) if ID_MANIFEST.exists() else {}
    ids=allocate(all_rows,existing)
    rows=sorted((r for r in all_rows if r["status"] == "converted"),key=lambda r:ids[r["slug"]]["index"])
    if len(rows)!=len(all_rows):
        raise ValueError("Incomplete asset conversion: refusing a partial collection")
    if args.out.exists():
        raise FileExistsError("Use a new integration output directory")
    client=mc.Client(args.client,True)
    try:
        wanted={"Item":{ids[r["slug"]][k] for r in rows for k in ("source","token")},
                "ItemDisplayInfo":{ids[r["slug"]]["display"] for r in rows},"Spell":{USE_SPELL}}
        for name,values in wanted.items():
            conflicts=values & client.ids(name)
            if conflicts: raise ValueError(f"{name} IDs already in effective client: {sorted(conflicts)}")
        item_ids=sorted(wanted["Item"])
        if db.mysql("SELECT entry FROM item_template WHERE entry IN ("+",".join(map(str,item_ids))+")", "acore_world"):
            raise ValueError("Generated item ID already used by server")
        if db.mysql(f"SELECT ID FROM spell_dbc WHERE ID={USE_SPELL}","acore_world"):
            raise ValueError("Generated spell ID already used by server")
        # Use an actual cape texture as the temporary cloth fallback while the backpack mesh loads.
        cape=next(text[3] for row,text in client.table("ItemDisplayInfo").rows.values()
                  if row[7] == 1 and text.get(3) and not text.get(1) and not text.get(2))
        additions=build_tables(rows,ids,cape)
        wing_rows=wings(client)
        registry_before=file_manifest(client.registry)
        args.out.mkdir(parents=True)
        registry=args.out/"registry"
        shutil.copytree(client.registry,registry)
        for name,add in additions.items():
            fields=mc.LAYOUT[name][0]
            target=registry/(name.lower()+".dbc")
            base=target.read_bytes() if target.exists() else native.make_wdbc(fields,[])
            target.write_bytes(merge_registry(base,add,fields,STRING_FIELDS[name]))
            delta=args.out/"additions"/(name.lower()+".dbc")
            delta.parent.mkdir(exist_ok=True)
            delta.write_bytes(add)
        registry_after=file_manifest(registry)
        for path,digest in registry_before.items():
            if path not in {n.lower()+".dbc" for n in additions} and registry_after[path]!=digest:
                raise AssertionError(f"Unrelated registry file changed: {path}")
        revision="cosmetics-"+sha(b"".join(additions[n] for n in sorted(additions)))[:12]
        config_path=args.client/"Data/Retail/RetailCasc.ini"
        original_config=config_path.read_text()
        lines=[]
        old_registry=client.registry.relative_to(args.client/"Data/Retail").as_posix()
        for line in original_config.splitlines():
            if line.startswith("tables="): line="tables=Registries/"+revision
            elif line.strip()=="aliases="+old_registry+"/aliases.csv": line="aliases=Registries/"+revision+"/aliases.csv"
            lines.append(line)
        (args.out/"RetailCasc.ini").write_text("\n".join(lines)+"\n",encoding="utf-8")
        addon_data,wing_data=generated_lua(rows,ids,wing_rows)
        addon=REPO/"Addons/EsteriaCosmetics"
        addon.mkdir(exist_ok=True)
        (addon/"EsteriaCosmeticsData.lua").write_text(addon_data,encoding="utf-8")
        (addon/"EsteriaCosmeticsWings.lua").write_text(wing_data,encoding="utf-8")
        ID_MANIFEST.write_text(json.dumps(ids,indent=2,sort_keys=True)+"\n")
        sql=world_sql(rows,ids)
        DATA_SQL.write_text(sql,encoding="utf-8")
        (args.out/"world.sql").write_text(sql,encoding="utf-8")
        archive,entries=package(args.out,args.assets,rows,ids,args.client,client.archives.storm)
        report={"backpacks":len(rows),"wings":len(wing_rows),"asset_source":str(args.assets),"revision":revision,
                "archive":str(archive),"archive_sha256":sha(archive.read_bytes()),"entries":{k:sha(v) for k,v in entries.items()},
                "registry_before":{"path":str(client.registry),"files":registry_before},"registry_after":registry_after,
                "config_before_sha256":sha(config_path.read_bytes()),"config_after_sha256":sha((args.out/"RetailCasc.ini").read_bytes()),
                "sql":str(DATA_SQL),"sql_sha256":sha(sql.encode()),"ids":ids,"use_spell":USE_SPELL,
                "losses":catalog.get("losses",[]),"state":"integration staged and MPQ readback verified; not installed"}
        (args.out/"receipt.json").write_text(json.dumps(report,indent=2)+"\n")
        print(json.dumps({k:report[k] for k in ("backpacks","wings","revision","archive","state")},indent=2))
    finally: client.archives.close()


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets",type=Path,default=Path("G:/FortniteCosmetics/stage-v1"))
    parser.add_argument("--client",type=Path,default=Path("G:/3.3.5a - Dev"))
    parser.add_argument("--out",type=Path,required=True)
    parser.add_argument("--repack-from",type=Path)
    args=parser.parse_args()
    repack(args) if args.repack_from else stage(args)
