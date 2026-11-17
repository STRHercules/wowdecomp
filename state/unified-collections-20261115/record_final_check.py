from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parents[2]
path=ROOT/'state/unified-collections-final-20261115-042136/receipt.json'
receipt=json.loads(path.read_text())
assert hashlib.sha256(Path(receipt['installed_archive']).read_bytes()).hexdigest()==receipt['archive_sha256']
receipt['live_acceptance']=True
receipt['live_acceptance_scope']=[
    'Black Tabby Cat summoned using the Collections Summon button; state changed to Summoned and Dismiss enabled',
    'Dismiss button returned the pet state to Collected',
    '0CN-DRFT D.R.I.V.E Kart summoned through Collections; state changed to Summoned',
    'Dismount button cleared the mount and returned the state to Collected',
    'Mount type filter changed shown results from 3408 all to 3185 ground and 221 flying using existing metadata',
    'All six pet cards and the selected model rendered after switching back from Mounts',
    'Full Transmog panel filled its Collections content area with character, appearance grid, slots and Apply controls',
]
receipt['remaining_checks']=['Paid transmog Apply transaction','Every catalog model and mount-type classification','Every character race and body form']
path.write_text(json.dumps(receipt,indent=2)+'\n')
print('Final installed archive hash verified; named live checks recorded:',path)
