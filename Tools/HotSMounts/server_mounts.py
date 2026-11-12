"""Stage and install additive HotS server mount records, preserving existing content."""
import argparse
import io
import json
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'RetailAssets'))
import native_import as n
import native_mount as nm
from server import KEYS, query, snapshot


def batch_sql(manifests):
    statements = [n.server_sql(m) for m in manifests]
    header = statements[0].split('START TRANSACTION;')[0]
    bodies = [s.split('START TRANSACTION;\n', 1)[1].split('\nCOMMIT;', 1)[0] for s in statements]
    return header + 'START TRANSACTION;\n' + '\n'.join(bodies) + (
        '\nCOMMIT;\nDO RELEASE_LOCK(\'esteria_native_retail_import\');\nDROP TEMPORARY TABLE `_native_retail_guard`;\n')


def stage(args):
    if args.output.exists():
        raise ValueError('Choose a new output directory')
    source = json.loads((args.source / 'mount-inventory.json').read_text())
    inspection = {m['source']: m for m in json.loads((args.source / 'inspection.json').read_text())}
    client, providers = n.client_ids(args.client, args.server_root)
    server_ids, server_providers = n.server_table_ids()
    schema = snapshot(KEYS)
    used = {t: client[t] | server_ids[t] for t in n.TABLES}
    for table, sql_table in nm.SQL_DBC.items():
        used[table] |= {int(v) for v in query(f'SELECT ID FROM `{sql_table}`;').splitlines()}
    used['Creature'] = set(schema['creature_template']['ids']) | set(schema['creature_template_model']['ids'])
    used['CreatureDisplayInfo'] |= set(schema['creature_model_info']['ids'])
    item_used = used['Item'] | set(schema['item_template']['ids']) | set(schema['item_dbc']['ids'])
    display_used = used['ItemDisplayInfo'] | set(schema['itemdisplayinfo_dbc']['ids'])
    if query('SELECT COUNT(*) FROM spell_dbc WHERE ID IN (23229,61451);') != '0':
        raise ValueError('Resolve the ground-mount donor SQL override before staging')
    spells, _ = n.wdbc(nm.docker_bytes('cat', f'{nm.SERVER_DATA}/dbc/Spell.dbc'), 234)
    donor = next(row for row in spells if row[0] == 23229)
    surf_visual = next(row for row in spells if row[0] == 61451)[131]
    if donor[95] != 78 or donor[71] != 6:
        raise ValueError('Donor is not a mounted aura')
    args.output.mkdir(parents=True)
    allocation = args.client / 'Data/Retail/allocations.sqlite'
    shutil.copy2(allocation, args.output / 'allocations-before.sqlite')
    rows = {t: [] for t in n.TABLES}
    texts = {t: {} for t in n.TABLES}
    manifests = []
    paths = []
    commands = []
    build = dict(line.split('=', 1) for line in (args.source / 'HotSCasc.ini').read_text().splitlines() if '=' in line)['build']
    for model in source['models']:
        for display in model['displays']:
            hots_id = display['hots_id']
            if not hots_id.isascii() or not hots_id.replace('_', '').isalnum():
                raise ValueError(f'Unsafe HotS ID: {hots_id}')
            key = f'hero:{build}:mount:{hots_id}'
            item, item_display = n.allocate(allocation, key, item_used, display_used)
            ids = nm.allocate_mount(allocation, key, used)
            for kind, value in ids.items():
                used[kind].add(value)
            item_used.add(item)
            display_used.add(item_display)
            name = display['name']
            virtual = f'hots/mounts/{hots_id.lower()}.m3'
            paths.append(f'{virtual};{model["source"]}')
            for bank in model['banks']:
                paths.append(f'hots/mounts/{hots_id.lower()}_requiredanims.m3a;{bank}')
                break
            spell = donor.copy()
            spell[0], spell[110], spell[133] = ids['Spell'], ids['Creature'], ids['SpellIcon']
            spell[131], spell[132] = (surf_visual if model['category'] == 'Ridesurf' else 15809), 0
            for field in n.TABLES['Spell'][1]:
                spell[field] = 0
            spell[152], spell[169], spell[186], spell[203] = 16712190, 0, 16712190, 16712188
            bounds = model.get('referenceBounds') or {'minimum': [-1, -1, 0], 'maximum': [1, 1, 2]}
            rider = model.get('referenceRider') or [0, 0, 1.5]
            creature_model = [ids['CreatureModelData'], 1027, 0, 0, nm.f32(1), 0, 0, 0, 0, nm.f32(1), 0, 0, 0, 0,
                              nm.f32(1), nm.f32(2), nm.f32(rider[2]),
                              *(nm.f32(v) for v in bounds['minimum'] + bounds['maximum']), nm.f32(1), nm.f32(1), 0, 0, 0]
            icon = 'Ability_Mount_RidingHorse'
            tables = {
                'Item': ([[item, 15, 5, 0xffffffff, 4, item_display, 0, 0]], {}),
                'ItemDisplayInfo': ([[item_display] + [0] * 24], {(0, 5): icon}),
                'CreatureModelData': ([creature_model], {(0, 2): virtual.replace('/', '\\')}),
                'CreatureDisplayInfo': ([[ids['CreatureDisplayInfo'], ids['CreatureModelData'], 0, 0, nm.f32(1), 255] + [0] * 10], {}),
                'SpellIcon': ([[ids['SpellIcon'], 0]], {(0, 1): f'Interface\\Icons\\{icon}'}),
                'SkillLineAbility': ([[ids['SkillLineAbility'], 777, ids['Spell'], 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0]], {}),
                'Spell': ([spell], {(0, 136): name, (0, 137): name, (0, 170): 'Summons and dismisses this HotS mount.',
                                    (0, 171): 'Summons and dismisses this HotS mount.', (0, 187): 'Increases speed by $s2%.'}),
            }
            records = {
                'item_template': {'entry': item, 'class': 15, 'subclass': 5, 'SoundOverrideSubclass': -1,
                    'name': 'Reins of the ' + name, 'displayid': item_display, 'Quality': 4, 'ItemLevel': 1, 'RequiredLevel': 1,
                    'AllowableClass': -1, 'AllowableRace': -1, 'maxcount': 1, 'stackable': 1, 'bonding': 1, 'Material': 4,
                    'BagFamily': 128, 'description': 'Teaches you how to summon the ' + name + '.',
                    'spellid_1': 483, 'spelltrigger_1': 0, 'spellcharges_1': -1, 'spellcooldown_1': -1,
                    'spellcategory_1': 330, 'spellcategorycooldown_1': 3000, 'spellid_2': ids['Spell'], 'spelltrigger_2': 6},
                'item_dbc': dict(zip(('ID', 'ClassID', 'SubclassID', 'Sound_Override_Subclassid', 'Material', 'DisplayInfoID', 'InventoryType', 'SheatheType'),
                                     [item, 15, 5, -1, 4, item_display, 0, 0])),
                'itemdisplayinfo_dbc': {'ID': item_display, 'InventoryIcon_1': icon},
                'creature_template': {'entry': ids['Creature'], 'name': name, 'minlevel': 1, 'maxlevel': 1, 'faction': 35,
                    'unit_class': 1, 'type': 1, 'unit_flags2': 2048, 'HoverHeight': 1, 'flags_extra': 2, 'movementId': 140},
                'creature_template_model': {'CreatureID': ids['Creature'], 'Idx': 0, 'CreatureDisplayID': ids['CreatureDisplayInfo'],
                    'DisplayScale': 1, 'Probability': 1, 'VerifiedBuild': 12340},
                'creature_model_info': {'DisplayID': ids['CreatureDisplayInfo'], 'BoundingRadius': 1, 'CombatReach': 2,
                    'Gender': 2, 'DisplayID_Other_Gender': 0, 'VerifiedBuild': 12340},
            }
            for table, (new_rows, strings) in tables.items():
                offset = len(rows[table])
                rows[table].extend(new_rows)
                texts[table].update({(offset + i, field): v for (i, field), v in strings.items()})
            manifest = dict(source_key=key, source=dict(product='hero', build=build, hots_id=hots_id, model=model['source']),
                            target=dict(kind='mount', name=name, item_id=item, display_id=item_display, ids=ids, records=records))
            manifest['definition_sha256'] = n.digest(json.dumps({'source': manifest['source'], 'target': manifest['target']}, sort_keys=True).encode())
            manifests.append(manifest)
            commands.append(dict(name=name, hots_id=hots_id, spell=ids['Spell'], item=item, display=ids['CreatureDisplayInfo'],
                                 parserAccepted=bool(inspection.get(model['source'], {}).get('ok')),
                                 clientReady=False, source=model['source']))
    (args.output / 'tables').mkdir()
    (args.output / 'server').mkdir()
    for table in n.TABLES:
        data = n.make_wdbc(n.TABLES[table][0], rows[table], texts[table])
        n.wdbc(data, n.TABLES[table][0])
        (args.output / 'tables' / (table.lower() + '.dbc')).write_bytes(data)
        if table in nm.SQL_DBC:
            (args.output / 'server' / f'{table}.dbc1-native-hots-mounts').write_bytes(data)
    (args.output / 'server' / 'hots-mounts.sql').write_text(batch_sql(manifests), encoding='utf-8')
    (args.output / 'hots-mount-paths.csv').write_text('\n'.join(paths) + '\n')
    n.save_json(args.output / 'manifests.json', manifests)
    n.save_json(args.output / 'mounts.json', commands)
    n.save_json(args.output / 'baselines.json', dict(client=providers, server=server_providers,
                server_schema={t: s['columns'] for t, s in schema.items()}))
    lines = ['# HotS server mount commands', '', 'Server registrations only. Client mount rendering and variant support remain pending.', '',
             '| Mount | Learn | Item | Parser |', '|---|---|---|---|']
    lines += [f'| {m["name"]} | `.learn {m["spell"]}` | `.additem {m["item"]}` | {"Pass" if m["parserAccepted"] else "Blocked PHCL v2"} |' for m in commands]
    (args.output / 'learn-commands.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    n.save_json(args.output / 'stage-receipt.json', dict(state='server-integration-staged', mounts=len(commands),
                files={p.relative_to(args.output).as_posix(): n.digest(p.read_bytes()) for p in args.output.rglob('*') if p.is_file()}))
    print(json.dumps(dict(mounts=len(commands), parserAccepted=sum(m['parserAccepted'] for m in commands), output=str(args.output))))


def copied_bytes(path):
    archive = subprocess.run(['docker', 'cp', f'ac-worldserver:{path}', '-'], check=True, capture_output=True).stdout
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        files = [f for f in tar.getmembers() if f.isfile()]
        if len(files) != 1:
            raise ValueError('Expected one server file')
        return tar.extractfile(files[0]).read()


def install(args):
    receipt = json.loads((args.output / 'stage-receipt.json').read_text())
    for path, digest in receipt['files'].items():
        if n.digest((args.output / path).read_bytes()) != digest:
            raise ValueError(f'Staged file changed: {path}')
    manifests = json.loads((args.output / 'manifests.json').read_text())
    if query('SELECT COUNT(*) FROM characters WHERE online<>0;', database='acore_characters') != '0':
        raise ValueError('Players are online; a server restart would interrupt them')
    for local in sorted((args.output / 'server').glob('*.dbc1-*')):
        destination = f'{nm.SERVER_DATA}/dbc-continuations/{local.name}'
        existing = subprocess.run(['docker', 'cp', f'ac-worldserver:{destination}', '-'], capture_output=True)
        if existing.returncode == 0 and copied_bytes(destination) != local.read_bytes():
            raise ValueError('Existing HotS continuation differs; no server rows were changed')
    # Scope is additive. Each content insert and ownership guard shares one SQL transaction.
    subprocess.run(['docker', 'stop', '--time', '60', 'ac-worldserver'], check=True)
    try:
        query((args.output / 'server/hots-mounts.sql').read_text(encoding='utf-8'), timeout=600)
        for local in sorted((args.output / 'server').glob('*.dbc1-*')):
            destination = f'{nm.SERVER_DATA}/dbc-continuations/{local.name}'
            # Recheck after stopping, immediately before publication.
            existing = subprocess.run(['docker', 'cp', f'ac-worldserver:{destination}', '-'], capture_output=True)
            if existing.returncode == 0 and copied_bytes(destination) != local.read_bytes():
                raise ValueError('Existing HotS continuation differs')
            subprocess.run(['docker', 'cp', str(local.resolve()), f'ac-worldserver:{destination}'], check=True, capture_output=True)
            if copied_bytes(destination) != local.read_bytes():
                raise ValueError('Server continuation readback differs')
        for table, primary in KEYS.items():
            records = [m['target']['records'][table] for m in manifests]
            columns = list(records[0])
            ids = ','.join(str(r[primary]) for r in records)
            values = query(f'SELECT {",".join("`" + c + "`" for c in columns)} FROM `{table}` WHERE `{primary}` IN ({ids}) ORDER BY `{primary}`;')
            actual = [line.split('\t') for line in values.splitlines()]
            expected = [[str(r[c]) for c in columns] for r in sorted(records, key=lambda r: r[primary])]
            if actual != expected:
                raise ValueError(f'Server readback mismatch: {table}')
        n.save_json(args.output / 'install-receipt.json', dict(state='server-installed-awaiting-startup-check',
                    mounts=len(manifests), serverReadback=True, clientInstalled=False))
    finally:
        subprocess.run(['docker', 'start', 'ac-worldserver'], check=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['stage', 'install-server'])
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--source', type=Path, default=Path('build/hots-mounts/stage'))
    parser.add_argument('--client', type=Path, default=Path(r'G:\3.3.5a - Dev'))
    parser.add_argument('--server-root', type=Path, default=Path(r'R:\Users\Zach\Documents\GitHub\EsteriaWoW'))
    options = parser.parse_args()
    {'stage': stage, 'install-server': install}[options.command](options)
