from datetime import datetime
from pathlib import Path
import hashlib
import json
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[2]
OUT=Path(__file__).resolve().parent
CLIENT=Path('G:/3.3.5a - Dev')
source=ROOT/'bin/Win/WowClientRetailInventory.exe'
destination=CLIENT/'WowCollectionsPreview.exe'

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

running=subprocess.check_output(['powershell','-NoProfile','-Command',
    "Get-Process Wow* -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Path"],text=True).splitlines()
backup=None
if str(destination).lower() in [p.strip().lower() for p in running]:
    destination=CLIENT/'WowCollectionsPlaybackFix.exe'
    if destination.exists():raise RuntimeError('Separate playback fix executable already exists')
elif destination.exists():
    backup=OUT/('WowCollectionsPreview-before-'+datetime.now().strftime('%Y%m%d-%H%M%S')+'.exe')
    shutil.copy2(destination,backup)
    assert sha(backup)==sha(destination)
preserved={name:sha(CLIENT/name) for name in ('Wow.exe','WowCosmetics.exe','WowMountPreviewFix.exe')}
shutil.copy2(source,destination)
assert sha(source)==sha(destination)
assert all(sha(CLIENT/name)==digest for name,digest in preserved.items())
receipt=dict(executable=str(destination),sha256=sha(destination),backup=str(backup) if backup else None,
    existing_clients_preserved=preserved,includes=['Retail dress-up model selection fix','emote autoplay sequence reset fix','hearthstone player previews','M3 mount framing fix'],
    validation=['Release WowClient build passed','production dress-up model-selection and sequence/reset native harness passed','collections Lua tests passed'],
    live_visual_acceptance=False,activation='Restart with this executable; no addon package change required')
(OUT/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt,indent=2))
