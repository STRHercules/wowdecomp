from datetime import datetime
import hashlib,json,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
CLIENT=Path('G:/3.3.5a - Dev')
running=subprocess.check_output(['powershell','-NoProfile','-Command',
    "Get-Process Wow* -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Path"],text=True).strip()
assert not running,running
old=json.loads((ROOT/'state/unified-collections-live-20261115-041835/receipt.json').read_text())
for name,digest in old['installed_loose'].items():
    path=Path(name).resolve()
    assert path.is_relative_to((CLIENT/'Interface/AddOns').resolve())
    assert old['loose_rollback'][name] is None
    assert hashlib.sha256(path.read_bytes()).hexdigest()==digest
    path.unlink()
sys.path.insert(0,str(ROOT/'Tools/CosmeticsPack'))
from package_unified_collections import package,install
out=ROOT/'state'/('unified-collections-final-'+datetime.now().strftime('%Y%m%d-%H%M%S'))
receipt=package(CLIENT,out)
install(receipt,CLIENT)
