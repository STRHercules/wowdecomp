"""Apply movement.csv to the installed HotS mount spells: flying mounts get flight speed, removed mounts lose their spell.

Patches the client's active registry DBCs and the server's HotS continuation files in place (same records, same
string block), so it can run again after any reinstall.
"""
import argparse
import json
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'RetailAssets'))
import native_mount as nm
from server import query
from server_mounts import copied_bytes
from client_mounts import pose_rows

WIDE_VISUAL = 14418  # SpellVisual whose kit seats the rider in ReclinedMount (484); Eclipse bikes use it
FLYING_DONOR = 61309  # the flying donor Esteria's other imported mounts use
EFFECT_ARRAYS = range(71, 122, 3)  # Effect .. PointsPerComboPoint, three effect slots each
ATTRIBUTES = range(4, 11)
SPELL_FIELDS, SKILL_FIELDS = 234, 14


def split(data, fields):
    magic, count, width, size, strings = struct.unpack_from('<4s4I', data)
    if magic != b'WDBC' or width != fields or size != fields * 4 or len(data) != 20 + count * size + strings:
        raise ValueError('Unexpected DBC layout')
    rows = [list(struct.unpack_from(f'<{fields}I', data, 20 + i * size)) for i in range(count)]
    return rows, data[20 + count * size:]


def join(rows, fields, strings):
    return struct.pack('<4s4I', b'WDBC', len(rows), fields, fields * 4, len(strings)) + b''.join(
        struct.pack(f'<{fields}I', *row) for row in rows) + strings


def load_kinds(path):
    kinds = {}
    for line in path.read_text().splitlines():
        if line.strip() and not line.startswith('#'):
            hots_id, kind = line.strip().split(';')
            if kind not in ('Flying', 'Remove') or kinds.setdefault(hots_id, kind) != kind:
                raise ValueError(f'Bad movement row: {line}')
    return kinds


def patch_spells(data, kinds, donor, wide=frozenset()):
    """kinds: spell id -> Flying|Remove. Flying copies the donor's speed effects (slots 2-3) and attributes.
    Spells in `wide` get the motorcycle seat visual."""
    rows, strings = split(data, SPELL_FIELDS)
    out = []
    for row in rows:
        kind = kinds.get(row[0])
        if kind == 'Remove':
            continue
        if row[0] in wide:
            row = row.copy()
            row[131] = WIDE_VISUAL
        if kind == 'Flying':
            row = row.copy()
            for base in EFFECT_ARRAYS:
                row[base + 1], row[base + 2] = donor[base + 1], donor[base + 2]
            for field in ATTRIBUTES:
                row[field] = donor[field]
        out.append(row)
    return join(out, SPELL_FIELDS, strings)


def patch_skills(data, removed):
    rows, strings = split(data, SKILL_FIELDS)
    return join([r for r in rows if r[2] not in removed], SKILL_FIELDS, strings)


def main(args):
    kinds = load_kinds(args.movement)
    mounts = {m['hots_id']: m for m in json.loads((args.server / 'mounts.json').read_text())}
    unknown = set(kinds) - set(mounts)
    if unknown:
        raise ValueError(f'Unknown HotS IDs: {sorted(unknown)}')
    by_spell = {mounts[h]['spell']: k for h, k in kinds.items()}
    removed = {s for s, k in by_spell.items() if k == 'Remove'}
    report = json.loads((args.source / 'mount-inventory.json').read_text())
    by_display = {m['display']: m['spell'] for m in mounts.values()}
    wide = frozenset(by_display[int(r.split(';')[0])] for r in pose_rows(report, mounts) if ';r;' in r)
    server_rows, _ = split(nm.docker_bytes('cat', f'{nm.SERVER_DATA}/dbc/Spell.dbc'), SPELL_FIELDS)
    donor = next(r for r in server_rows if r[0] == FLYING_DONOR)
    if donor[95:98] != [78, 32, 207]:
        raise ValueError('Flying donor is not a mount with ground and flight speed')

    config = dict(l.split('=', 1) for l in (args.client / 'Data/Retail/RetailCasc.ini').read_text().splitlines() if '=' in l)
    registry = args.client / 'Data/Retail' / config['tables']
    remote = f'{nm.SERVER_DATA}/dbc-continuations'
    files = {
        registry / 'spell.dbc': lambda d: patch_spells(d, by_spell, donor, wide),
        registry / 'skilllineability.dbc': lambda d: patch_skills(d, removed),
        f'{remote}/Spell.dbc1-native-hots-mounts': lambda d: patch_spells(d, by_spell, donor, wide),
        f'{remote}/SkillLineAbility.dbc1-native-hots-mounts': lambda d: patch_skills(d, removed),
    }
    results = {}
    for target, patch in files.items():
        before = target.read_bytes() if isinstance(target, Path) else copied_bytes(target)
        results[target] = (before, patch(before))
    print(json.dumps({str(t): dict(before=len(b), after=len(a), changed=a != b) for t, (b, a) in results.items()}, indent=1))
    if not args.apply:
        return
    backup = args.client / '_backups' / ('hots-movement-' + time.strftime('%Y%m%d-%H%M%S'))
    backup.mkdir(parents=True)
    for target, (before, after) in results.items():
        name = target.name if isinstance(target, Path) else 'server-' + target.rsplit('/', 1)[1]
        (backup / name).write_bytes(before)
    for target, (before, after) in results.items():
        if isinstance(target, Path):
            target.write_bytes(after)
    online = query('SELECT COUNT(*) FROM characters WHERE online<>0;', database='acore_characters')
    if online != '0' and not args.force_restart:
        print(f'Client patched; server not restarted: {online} character(s) online. Rerun with --apply --force-restart.')
        return
    subprocess.run(['docker', 'stop', '--time', '60', 'ac-worldserver'], check=True)
    try:
        staging = backup / 'staging'
        staging.mkdir()
        for target, (before, after) in results.items():
            if isinstance(target, str):
                local = staging / target.rsplit('/', 1)[1]
                local.write_bytes(after)
                subprocess.run(['docker', 'cp', str(local.resolve()), f'ac-worldserver:{target}'], check=True, capture_output=True)
                if copied_bytes(target) != after:
                    raise ValueError(f'Server readback differs: {target}')
    finally:
        subprocess.run(['docker', 'start', 'ac-worldserver'], check=True)
    print(json.dumps(dict(backup=str(backup), flying=sum(k == 'Flying' for k in kinds.values()), removed=len(removed))))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--movement', type=Path, default=Path(__file__).with_name('movement.csv'))
    p.add_argument('--server', type=Path, default=Path('build/hots-mounts/server-install-20261112-v3'))
    p.add_argument('--source', type=Path, default=Path('build/hots-mounts/stage'))
    p.add_argument('--client', type=Path, default=Path(r'G:\3.3.5a - Dev'))
    p.add_argument('--apply', action='store_true')
    p.add_argument('--force-restart', action='store_true', help='restart the worldserver even with characters online')
    main(p.parse_args())
