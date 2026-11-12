"""Phase 4: Manastorm on the dev realm only (housing_dev_* databases, worldserver 8099, authserver 3725).
Never writes acore_* (they are only read, --single-transaction) and never touches ac-worldserver.

    python -I deploy_dev.py db            # back up housing_dev_*, re-clone them from acore_*, apply the stage, verify
    python -I deploy_dev.py image <tag>   # ninja install in housing-build, package FROM the live worldserver image
    python -I deploy_dev.py start <tag>   # dev realm + Manastorm continuations, Manastorm.dbc and config
"""
import gzip
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import manastorm_port as mp  # noqa: E402

sys.path.insert(0, str(mp.ESTERIA / "modules/mod-esteria-housing/tools"))
import stage  # noqa: E402  (the housing dev realm: clones, config, containers)

DEV = mp.WORK / "dev-data"
COA_DBC = mp.WORK / "coa-client/patch-M.MPQ/DBFilesClient/Manastorm.dbc"
DATA = "/azerothcore/env/dist/data"


def sh(command, **kw):
    return subprocess.run(["docker", "exec", "ac-database", "bash", "-o", "pipefail", "-c", command], **kw)


def db():
    backup = mp.WORK / time.strftime("deploy-backup-%Y%m%d-%H%M%S")
    backup.mkdir()
    for target in sorted(stage.databases() & set(stage.CLONE.values())):
        result = sh(f'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" mysqldump -uroot --single-transaction --quick --routines '
                    f"--events --triggers --set-gtid-purged=OFF {target}", capture_output=True)
        if result.returncode or b"Dump completed" not in result.stdout[-200:]:
            raise SystemExit(f"backup {target}: {result.stderr.decode(errors='replace')[:2000]}")
        with gzip.open(backup / f"{target}.sql.gz", "wb", compresslevel=1) as out:
            out.write(result.stdout)
        print("backed up", target, flush=True)
    for source, target in stage.CLONE.items():
        assert target.startswith("housing_dev_")
        mp.mysql(f"DROP DATABASE IF EXISTS `{target}`; CREATE DATABASE `{target}` CHARACTER SET utf8mb4;")
        sh(f'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" mysqldump -uroot --single-transaction --skip-lock-tables --routines '
           f'--triggers --set-gtid-purged=OFF {source} | MYSQL_PWD="$MYSQL_ROOT_PASSWORD" mysql -uroot {target}', check=True)
        print("cloned", source, "->", target, flush=True)
    mp.mysql((mp.STAGE / "world.sql").read_bytes(), stage.CLONE["acore_world"], timeout=3600)
    mp.mysql((mp.STAGE / "characters.sql").read_bytes(), stage.CLONE["acore_characters"], timeout=3600)
    mp.mysql(stage.REALM_SQL, stage.CLONE["acore_auth"])
    print(mp.mysql((HERE / "verify.sql").read_text(encoding="utf-8"), stage.CLONE["acore_world"]))
    print("backups in", backup)


def image(tag):
    container, binary = "housing-build", "/azerothcore/env/dist/bin/worldserver"
    subprocess.run(["docker", "exec", container, "sh", "-c",
                    "cd /azerothcore/build && ninja -j12 worldserver && ninja install > /dev/null"], check=True)
    runtime = mp.WORK / ("runtime-" + tag.split(":")[-1])
    runtime.mkdir()
    subprocess.run(["docker", "cp", f"{container}:{binary}", str(runtime / "worldserver")], check=True)
    subprocess.run(["docker", "cp", f"{container}:/azerothcore/env/dist/lib/liblua52.so", str(runtime)], check=True)
    assert b"Manastorm.NativeDBCPath" in (runtime / "worldserver").read_bytes(), "module not linked"
    base = subprocess.check_output(["docker", "inspect", "-f", "{{.Config.Image}}", "ac-worldserver"], text=True).strip()
    (runtime / "Dockerfile").write_text(
        f"FROM {base}\n"
        "COPY --chown=acore:acore --chmod=755 worldserver /azerothcore/env/dist/bin/worldserver\n"
        "COPY --chown=acore:acore liblua52.so /azerothcore/env/dist/lib/liblua52.so\n", encoding="utf-8", newline="\n")
    subprocess.run(["docker", "build", "--no-cache", "-t", tag, str(runtime)], check=True, capture_output=True)
    print("built", tag, "FROM", base)


def start(tag):
    # The dev realm mounts the live data volume read-only, so new continuation files can't be bound into it:
    # bind a merged copy of the whole folder (live's files + Manastorm's) over it instead.
    continuations = DEV / "dbc-continuations"
    if continuations.exists():
        shutil.rmtree(continuations)
    DEV.mkdir(exist_ok=True)
    subprocess.run(["docker", "cp", f"ac-worldserver:{DATA}/dbc-continuations/.", str(continuations)], check=True)
    for path in (mp.STAGE / "server").glob("*.dbc1-manastorm"):
        shutil.copy2(path, continuations)
    # The build registered no module config file (housing-build has no conf/ for the module), so the keys go in
    # worldserver.conf, which stage.start() rewrites from the live etc each time: add them after its prepare().
    prepare = stage.prepare

    def prepare_with_manastorm():
        prepare()
        world = stage.OUT / "stage/etc/worldserver.conf"
        text = world.read_text(encoding="utf-8")
        for key, value in (("Manastorm.Enable", "1"), ("Manastorm.ClientMode", "2"),
                           ("Manastorm.NativeDBCPath", '"/manastorm/Manastorm.dbc"')):
            text = stage.set_option(text, key, value)
        world.write_text(text, encoding="utf-8", newline="\n")

    stage.prepare = prepare_with_manastorm
    stage.stop()
    stage.start(tag, ["-v", f"{continuations}:{DATA}/dbc-continuations:ro",
                      "-v", f"{COA_DBC}:/manastorm/Manastorm.dbc:ro"])


if __name__ == "__main__":
    action = sys.argv[1] if len(sys.argv) > 1 else ""
    {"db": db, "image": lambda: image(sys.argv[2]), "start": lambda: start(sys.argv[2])}.get(action, lambda: print(__doc__))()
