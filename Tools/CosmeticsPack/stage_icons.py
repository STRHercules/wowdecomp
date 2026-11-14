"""Stage matched icons into existing cosmetics display rows, preserving all item identities and other data."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct

import backpack_icons
import stage


def patch_displays(data, mappings):
    records,pool=stage.native.wdbc(data,25)
    result=bytearray(data[:20+len(records)*100])
    strings=bytearray(pool)
    changed=set()
    for index,row in enumerate(records):
        if row[0] not in mappings: continue
        name=mappings[row[0]].rsplit("\\",1)[-1].encode()+b"\0"
        struct.pack_into("<I",result,20+index*100+5*4,len(strings))
        strings.extend(name)
        changed.add(row[0])
    if changed!=set(mappings): raise ValueError("Missing expected display rows")
    struct.pack_into("<I",result,16,len(strings))
    result.extend(strings)
    after,after_pool=stage.native.wdbc(result,25)
    assert after_pool[:len(pool)]==pool
    for before,current in zip(records,after):
        assert before[:5]==current[:5] and before[6:]==current[6:]
        if before[0] not in mappings: assert before==current
    return bytes(result)


def main(args):
    if args.out.exists(): raise FileExistsError(args.out)
    base=json.loads((args.base/"receipt.json").read_text())
    assets=Path(base["asset_source"])
    rows=json.loads((assets/"catalog.json").read_text())["rows"]
    ids=base["ids"]
    rows.sort(key=lambda row:ids[row["slug"]]["index"])
    config_path=args.client/"Data/Retail/RetailCasc.ini"
    config=config_path.read_text()
    values=dict(line.split("=",1) for line in config.splitlines() if "=" in line and not line.startswith("#"))
    registry=args.client/"Data/Retail"/values["tables"]
    mappings={ids[row["slug"]]["display"]:backpack_icons.icon_path(ids[row["slug"]]["display"])
              for row in rows if backpack_icons.resolve(row)[0]}
    before=stage.file_manifest(registry)
    payload=patch_displays((registry/"itemdisplayinfo.dbc").read_bytes(),mappings)
    revision="cosmetics-icons-"+hashlib.sha256(payload).hexdigest()[:12]
    args.out.mkdir(parents=True)
    shutil.copytree(registry,args.out/"registry")
    (args.out/"registry/itemdisplayinfo.dbc").write_bytes(payload)
    after=stage.file_manifest(args.out/"registry")
    assert all(after[name]==digest for name,digest in before.items() if name!="itemdisplayinfo.dbc")
    for folder in ("additions",):
        if (args.base/folder).exists(): shutil.copytree(args.base/folder,args.out/folder)
    if (args.base/"world.sql").exists(): shutil.copy2(args.base/"world.sql",args.out/"world.sql")
    lines=[]
    for line in config.splitlines():
        if line.startswith("tables="): line="tables=Registries/"+revision
        elif line=="aliases="+values["tables"]+"/aliases.csv": line="aliases=Registries/"+revision+"/aliases.csv"
        lines.append(line)
    new_config="\n".join(lines)+"\n"
    (args.out/"RetailCasc.ini").write_text(new_config,encoding="utf-8")
    # Regenerate only backpack Lua icons; the existing wing graph remains unchanged.
    wing_text=(stage.REPO/"Addons/EsteriaCosmetics/EsteriaCosmeticsWings.lua").read_text()
    data,_=stage.generated_lua(rows,ids,[])
    (stage.REPO/"Addons/EsteriaCosmetics/EsteriaCosmeticsData.lua").write_text(data,encoding="utf-8")
    assert (stage.REPO/"Addons/EsteriaCosmetics/EsteriaCosmeticsWings.lua").read_text()==wing_text
    from eclipse_data import storm
    archive,entries=stage.package(args.out,assets,rows,ids,args.client,storm())
    base.update(revision=revision,archive=str(archive),archive_sha256=stage.sha(archive.read_bytes()),
                entries={name:stage.sha(data) for name,data in entries.items()},
                registry_before={"path":str(registry),"files":before},registry_after=after,
                config_before_sha256=stage.sha(config_path.read_bytes()),
                config_after_sha256=stage.sha((args.out/"RetailCasc.ini").read_bytes()),
                icons_matched=len(mappings),icons_missing=[row["name"] for row in rows if not backpack_icons.resolve(row)[0]],
                state="icon update staged, data preserved and MPQ readback verified; not installed")
    (args.out/"receipt.json").write_text(json.dumps(base,indent=2)+"\n")
    print(json.dumps({k:base[k] for k in ("archive","revision","icons_matched","icons_missing","state")},indent=2))


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base",type=Path,default=Path("G:/FortniteCosmetics/integration-v6"))
    parser.add_argument("--client",type=Path,default=Path("G:/3.3.5a - Dev"))
    parser.add_argument("--out",type=Path,required=True)
    main(parser.parse_args())
