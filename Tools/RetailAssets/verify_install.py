"""Read back installed pack bytes and server records, and record runtime evidence."""
import argparse
import json
import re
import subprocess

from native_import import Path, digest, load_pack, packs_dir, save_json, verify_server


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, required=True)
    parser.add_argument("--slug", required=True)
    args = parser.parse_args()
    if not re.fullmatch(r"[a-z][a-z0-9-]{0,63}", args.slug):
        raise ValueError("Invalid pack slug")
    root = args.client / "Data/Retail"
    pack = packs_dir(root) / args.slug
    manifest = load_pack(pack)
    verify_server(manifest)
    originals = root / "Assets" / f"{manifest['source']['product']}-{manifest['source']['build']}"
    for original in (pack / "originals").glob("*.bin"):
        if digest((originals / original.name).read_bytes()) != digest(original.read_bytes()):
            raise ValueError("Installed original asset differs")
    started = subprocess.run(["docker", "inspect", "--format", "{{.State.StartedAt}}", "ac-worldserver"],
                             capture_output=True, text=True, check=True).stdout.strip()
    logs = subprocess.run(["docker", "logs", "--since", started, "ac-worldserver"],
                          capture_output=True, text=True, encoding="utf-8", errors="replace", check=True)
    server_log = re.sub(r"\x1b\[[0-9;?]*[a-zA-Z]", "", logs.stdout + logs.stderr)
    # Use the actual target IDs; do not confuse unrelated world-content warnings with this pack.
    ids = tuple(str(value) for value in (manifest["target"]["item_id"], manifest["target"]["display_id"],
                *manifest["target"].get("ids", {}).values()))
    errors = [line for line in server_log.splitlines() if any(re.search(r"(?<!\d)" + value + r"(?!\d)", line) for value in ids) and
              any(word in line.lower() for word in ("error", "invalid", "does not exist", "not found"))]
    if errors:
        raise ValueError("Server rejected imported content: " + "\n".join(errors))
    ready = "AC>" in server_log or bool(re.search(r"World initialized|server is ready|worldserver.*ready", server_log, re.I))
    client_log = args.client / "Logs/RetailAssets.log"
    current = client_log.read_text(errors="replace").split("# System started at")[-1] if client_log.exists() else ""
    required_tables = ["Item", "ItemDisplayInfo"]
    continuation_evidence = {}
    if manifest["target"].get("kind") == "mount":
        from native_mount import SQL_DBC, SERVER_DATA, docker_bytes
        required_tables += ["CreatureModelData", "CreatureDisplayInfo", "Spell", "SpellIcon", "SkillLineAbility"]
        for table in SQL_DBC:
            filename = f"{table}.dbc1-native-retail-{args.slug}"
            expected = (pack / "tables" / f"{table.lower()}.dbc").read_bytes()
            if docker_bytes("cat", f"{SERVER_DATA}/dbc-continuations/{filename}") != expected:
                raise ValueError("Server mount continuation differs: " + filename)
            continuation_evidence[filename] = f"{filename} -> 1 row(s) into {table}.dbc" in server_log
        if not all(continuation_evidence.values()): raise ValueError("Server has not loaded every mount continuation")
    tables = all(f"DBFilesClient\\{table}.dbc: additive Retail records mounted" in current for table in required_tables)
    receipt = json.loads((pack / "receipt.json").read_text())
    receipt.update(server_started_at=started, server_ready=ready, client_tables_mounted=tables,
                   originals_verified=True, live_acceptance=False, server_continuations=continuation_evidence,
                   required_client_tables=required_tables)
    if ready and tables:
        receipt["state"] = "runtime-registered-awaiting-live-acceptance"
    save_json(pack / "receipt.json", receipt)
    print(json.dumps({"state": receipt["state"], "server_ready": ready, "client_tables_mounted": tables,
                      "originals_verified": True, "item_id": manifest["target"]["item_id"],
                      "display_id": manifest["target"]["display_id"]}, indent=2))


if __name__ == "__main__":
    main()
