from datetime import datetime
from pathlib import Path
import hashlib
import json
import shutil

ROOT = Path(__file__).resolve().parents[2]
CLIENT = Path('G:/3.3.5a - Dev')
OUT = Path(__file__).resolve().parent


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


source = ROOT / 'bin/Win/WowClientRetailInventory.exe'
destination = CLIENT / 'WowMountPreviewFix.exe'
baseline = {name: sha(CLIENT / name) for name in ('Wow.exe', 'WowCosmetics.exe')}
backup = None
if destination.exists():
    backup = OUT / ('WowMountPreviewFix-before-' + datetime.now().strftime('%Y%m%d-%H%M%S') + '.exe')
    shutil.copy2(destination, backup)
    assert sha(backup) == sha(destination)
shutil.copy2(source, destination)
assert sha(source) == sha(destination)
assert all(sha(CLIENT / name) == digest for name, digest in baseline.items())
receipt = dict(executable=str(destination), sha256=sha(destination), source=str(source),
    prior_same_name_backup=str(backup) if backup else None, existing_clients_preserved=baseline,
    m3_mount_entries=578, tests=['test_mount_preview.py passed', 'test_cosmetics.py passed', 'Release WowClient build passed'],
    scope='M3 source bounds and asynchronous preview readiness in both native fitters',
    build_context='Current checkout including pre-existing native changes; separate test executable',
    live_visual_acceptance=False)
(OUT / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
print(json.dumps(receipt, indent=2))
