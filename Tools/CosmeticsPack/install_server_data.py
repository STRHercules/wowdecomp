"""Apply the isolated-schema-verified, additive Cosmetics world data. Does not restart services."""
from datetime import datetime
import hashlib
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "ManastormPort"))
from manastorm_port import mysql


def main(stage):
    verify=json.loads((stage/"sql-verified.json").read_text())
    sql=(stage/"world.sql").read_bytes()
    schema_path=HERE.parents[1].parent/"EsteriaWoW/data/sql/updates/pending_db_world/rev_20261114120000000_cosmetics_backpacks.sql"
    schema=schema_path.read_bytes()
    if hashlib.sha256(sql).hexdigest()!=verify["world_sql_sha256"] or hashlib.sha256(schema).hexdigest()!=verify["schema_sql_sha256"]:
        raise RuntimeError("SQL changed after isolated verification")
    before={
        "items":mysql("SELECT COUNT(*) FROM item_template WHERE entry BETWEEN 3400001 AND 3400099 OR entry BETWEEN 3410001 AND 3410099","acore_world"),
        "spell":mysql("SELECT COUNT(*) FROM spell_dbc WHERE ID=3420001","acore_world"),
        "schema":mysql("SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='acore_world' AND table_name='esteria_backpack_appearances'")
    }
    if before["items"]!="0" or before["spell"]!="0":
        raise RuntimeError("Additive IDs already occupied; inspect the existing installation before updating")
    if before["schema"]!="0" and mysql("SELECT COUNT(*) FROM esteria_backpack_appearances","acore_world")!="0":
        raise RuntimeError("Cosmetics catalog already populated")
    report={"before":before,"world_sql_sha256":verify["world_sql_sha256"],"schema_sql_sha256":verify["schema_sql_sha256"],
            "time":datetime.now().isoformat(),"state":"baseline recorded before additive migration"}
    path=stage/"server-data-installed.json"
    if path.exists(): raise FileExistsError(path)
    path.write_text(json.dumps(report,indent=2)+"\n")
    mysql(schema,"acore_world")
    mysql(sql,"acore_world")
    counts=mysql("SELECT (SELECT COUNT(*) FROM item_template WHERE entry BETWEEN 3400001 AND 3400099 OR entry BETWEEN 3410001 AND 3410099),"
                 "(SELECT COUNT(*) FROM esteria_backpack_appearances),(SELECT COUNT(*) FROM spell_dbc WHERE ID=3420001)","acore_world")
    assert counts=="198\t99\t1",counts
    report.update(state="world data installed and read back; changed server binary requires restart",counts=[198,99,1])
    path.write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps(report))


if __name__=="__main__": main(Path(sys.argv[1]))
