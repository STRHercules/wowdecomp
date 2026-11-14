"""Install a verified Cosmetics stage with backups, or check its SQL in an isolated schema."""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SERVER = ROOT.parent / "EsteriaWoW"
sys.path.insert(0, str(ROOT / "Tools/ManastormPort"))
from manastorm_port import mysql


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def verify_sql(stage):
    schema = "cosmetics_verify_" + datetime.now().strftime("%Y%m%d_%H%M%S")
    mysql("CREATE DATABASE `" + schema + "` CHARACTER SET utf8mb4")
    for table in ("item_template", "spell_dbc"):
        mysql(f"CREATE TABLE `{schema}`.`{table}` LIKE `acore_world`.`{table}`")
    schema_sql = SERVER / "data/sql/updates/pending_db_world/rev_20261114120000000_cosmetics_backpacks.sql"
    mysql(schema_sql.read_bytes(), schema)
    mysql((stage / "world.sql").read_bytes(), schema)
    result = mysql("SELECT (SELECT COUNT(*) FROM item_template),"
                   "(SELECT COUNT(*) FROM esteria_backpack_appearances),"
                   "(SELECT COUNT(*) FROM spell_dbc)", schema)
    if result != "198\t99\t1":
        raise AssertionError(result)
    mysql((stage / "world.sql").read_bytes(), schema)
    assert mysql("SELECT COUNT(*) FROM item_template", schema) == "198"
    report = {"schema":schema,"items":198,"backpacks":99,"spells":1,"idempotent":True,
              "world_sql_sha256":sha(stage / "world.sql"),"schema_sql_sha256":sha(schema_sql)}
    (stage / "sql-verified.json").write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps(report))


def install_client(stage, client):
    receipt = json.loads((stage / "receipt.json").read_text())
    processes = subprocess.check_output(["powershell", "-NoProfile", "-Command",
        "Get-Process | Where-Object { $_.Path -like '" + str(client).replace("'","''") +
        "\\*.exe' } | Select-Object -ExpandProperty Path"], text=True).strip()
    if processes:
        raise RuntimeError("Close client executables before installing: " + processes)
    config = client / "Data/Retail/RetailCasc.ini"
    if sha(config) != receipt["config_before_sha256"]:
        raise RuntimeError("Client configuration changed after staging")
    baseline = Path(receipt["registry_before"]["path"])
    for relative,digest in receipt["registry_before"]["files"].items():
        if sha(baseline / relative) != digest:
            raise RuntimeError("Registry changed after staging: " + relative)
    for relative,digest in receipt["registry_after"].items():
        if sha(stage / "registry" / relative) != digest:
            raise RuntimeError("Staged registry changed: " + relative)
    archive = Path(receipt["archive"])
    if sha(archive) != receipt["archive_sha256"]:
        raise RuntimeError("Staged archive changed")
    backup = stage / ("backup-" + datetime.now().strftime("%Y%m%d-%H%M%S"))
    backup.mkdir()
    executable = ROOT / "bin/Win/WowClientRetailInventory.exe"
    destinations = [(archive,client / "Data" / archive.name),
                    (stage / "RetailCasc.ini",config),(executable,client / "WowCosmetics.exe")]
    saved = []
    for source,target in destinations:
        prior = None
        if target.exists():
            copy = backup / target.relative_to(client)
            copy.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(target,copy)
            assert sha(copy) == sha(target)
            prior = {"path":str(copy),"sha256":sha(copy)}
        saved.append({"target":str(target),"source":str(source),"before":prior,"after_sha256":sha(source)})
    registry = client / "Data/Retail/Registries" / receipt["revision"]
    if registry.exists():
        raise FileExistsError(registry)
    shutil.copytree(stage / "registry",registry)
    for relative,digest in receipt["registry_after"].items():
        assert sha(registry / relative) == digest
    (backup / "before.json").write_text(json.dumps(saved,indent=2)+"\n")
    for source,target in destinations:
        shutil.copy2(source,target)
        assert sha(source) == sha(target)
    report = {"stage":str(stage),"backup":str(backup),"registry":str(registry),"files":saved,
              "state":"client installed; live acceptance pending"}
    (stage / "client-installed.json").write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps({"backup":str(backup),"exe":str(client/'WowCosmetics.exe'),"archive":str(client/'Data'/archive.name)}))


def update_client(stage, client):
    running=subprocess.run(["powershell","-NoProfile","-Command",
        "Get-Process -Name Wow,WowCosmetics,WowJourneyShieldTrial -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id"],
        capture_output=True,text=True)
    if running.stdout.strip(): raise RuntimeError("Close client processes before updating")
    receipt=json.loads((stage/"receipt.json").read_text())
    if sha(client/"Data/Retail/RetailCasc.ini")!=receipt["config_after_sha256"]:
        raise RuntimeError("Installed registry does not match this preview package")
    archive=Path(receipt["archive"])
    if sha(archive)!=receipt["archive_sha256"]: raise RuntimeError("Archive changed after verification")
    exe=ROOT/"bin/Win/WowClientRetailInventory.exe"
    backup=stage/("backup-preview-"+datetime.now().strftime("%Y%m%d-%H%M%S"))
    backup.mkdir()
    targets=[(exe,client/"Wow.exe"),(exe,client/"WowCosmetics.exe"),(archive,client/"Data"/archive.name)]
    records=[]
    for source,target in targets:
        saved=backup/target.relative_to(client)
        saved.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(target,saved)
        assert sha(target)==sha(saved)
        records.append({"target":str(target),"source":str(source),"before":str(saved),
                        "before_sha256":sha(saved),"after_sha256":sha(source)})
    (backup/"before.json").write_text(json.dumps(records,indent=2)+"\n")
    for source,target in targets:
        shutil.copy2(source,target)
        assert sha(source)==sha(target)
    sys.path.insert(0,str(SERVER/"tools"))
    from cars_mount_pack import Storm
    storm=Storm(ROOT/"Tools/MountStudio/runtime/backend/StormLib.dll")
    handle=storm.open_archive(client/"Data"/archive.name)
    try:
        for name,digest in receipt["entries"].items():
            assert hashlib.sha256(storm.read(handle,name)).hexdigest()==digest,name
    finally: storm.dll.SFileCloseArchive(handle)
    report={"backup":str(backup),"files":records,"entries_verified":len(receipt["entries"]),
            "state":"preview update installed; native visual review pending"}
    (stage/"preview-installed.json").write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps({"backup":str(backup),"entries_verified":len(receipt["entries"])}))


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action",choices=("verify-sql","install-client","update-client"))
    parser.add_argument("--stage",type=Path,required=True)
    parser.add_argument("--client",type=Path,default=Path("G:/3.3.5a - Dev"))
    args=parser.parse_args()
    if args.action=="verify-sql": verify_sql(args.stage)
    elif args.action=="update-client": update_client(args.stage,args.client)
    else: install_client(args.stage,args.client)
