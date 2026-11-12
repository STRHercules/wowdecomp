"""Prepare and install the native HotS client assets and matched additive table registry."""
import argparse
import json
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'RetailAssets'))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'EclipseImport'))
import native_import as n
import eclipse_install as ei
from import_mounts import inventory, virtual, fetch
from catalog import audit_catalog


def prepare(args):
    args.output.mkdir(parents=True, exist_ok=True)
    report = json.loads((args.source / 'mount-inventory.json').read_text())
    ids = {m['hots_id']: m for m in json.loads((args.server / 'mounts.json').read_text())}
    model_rows,_=n.wdbc((args.server/'tables/creaturemodeldata.dbc').read_bytes(),28)
    display_rows,_=n.wdbc((args.server/'tables/creaturedisplayinfo.dbc').read_bytes(),16)
    model_heights={row[0]:struct.unpack('<f',struct.pack('<I',row[16]))[0] for row in model_rows}
    display_heights={row[0]:model_heights[row[1]] for row in display_rows}
    _, resolve = inventory(Path(report['catalog']['path']), Path(report['sourceIndex']['path']))
    catalog=audit_catalog(report['catalogFiles'],args.source/'originals',set(ids),resolve)
    declared={m['source']:m['requiredAnims'] for m in catalog['mounts'] if m['source'] and m['requiredAnims']}
    all_paths = {}; requests = set(); riders = []; checks = []; unresolved = []
    for model in report['models']:
        original = args.source / 'originals' / model['source']
        result = subprocess.run([str(args.inspect), str(original)], capture_output=True, text=True)
        proof = json.loads(result.stdout)['files'][0]
        if not proof.get('ok'):
            raise ValueError(f'{model["name"]}: {proof.get("error")}')
        layers = {virtual(l['bitmap']): l.get('role', '') for l in proof['nativeLayers'] if l['bitmap']}
        point = next((p for p in proof['attachmentPoints'] if p['wowId'] == 29), None)
        seat = model.get('referenceRider')
        seat_method='source-attachment'
        if point:
            x,y,z=point['pivot'];seat=[-y,x,z]
        elif not seat:
            # Hero additions lack a passenger point; use an explicitly authored root seat and record the approximation.
            geometry=subprocess.run([str(args.inspect),str(original),'--geometry'],capture_output=True,text=True,check=True)
            positions=[v['position'] for v in json.loads(geometry.stdout)['files'][0]['vertexData']]
            if positions:
                seat=[-(min(p[1] for p in positions)+max(p[1] for p in positions))/2,
                      (min(p[0] for p in positions)+max(p[0] for p in positions))/2,max(p[2] for p in positions)]
                seat_method='authored-root-above-mesh'
            else:
                seat=[0,0,display_heights[ids[model['displays'][0]['hots_id']]['display']]]
                seat_method='authored-root-at-target-mount-height'
        bank = None; scene = None; bank_errors = []
        for candidate in dict.fromkeys([declared.get(model['source']),*model['banks'],None]):
            command = [str(args.scene_inspect), str(original), '-1', '0']
            if candidate:
                command.append(str(args.source / 'originals' / candidate))
            command.append('--summary')
            output = subprocess.run(command, capture_output=True, text=True)
            trial = json.loads(output.stdout)
            if trial.get('ok') and any(g >= 0 for g in trial['sequenceGroups']):
                scene, bank = trial, candidate
                break
            bank_errors.append(dict(bank=candidate, error=trial.get('error', 'No playable source sequences')))
        if scene is None:
            unresolved.append(dict(model=model['name'], animationErrors=bank_errors))
            # Keep static models visible while reporting missing locomotion explicitly.
            scene = dict(textures=list(layers), animations=[])
        for name in scene['textures']:
            if name:
                layers.setdefault(virtual(name), '')
        checks.append(dict(model=model['name'], source=model['source'], parser=True, bank=bank,
                           animationCount=len(scene['animations']), animationErrors=bank_errors,riderMethod=seat_method,rider=seat))
        for display in model['displays']:
            ident = display['hots_id'].lower()
            path = f'hots/mounts/{ident}.m3'
            all_paths[path] = model['source']
            if bank:
                all_paths[f'hots/mounts/{ident}_requiredanims.m3a'] = bank
            if seat:
                riders.append(f'{path};{seat[0]};{seat[1]};{seat[2]}')
            textures = display.get('nativeTextures', [])
            for bitmap, role in layers.items():
                dependency = resolve(bitmap, model['source'])
                if not dependency:
                    unresolved.append(dict(mount=ident, texture=bitmap)); continue
                # Exact source suffixes identify the authored replaceable body maps in the seed catalog.
                for donor in model['displays']:
                    for old,new in zip(donor.get('nativeTextures',[]),textures):
                        if Path(bitmap).name==old['name'].lower() and new['source']:
                            dependency=new['source']
                all_paths[f'hots/variants/{ident}/{bitmap}'] = dependency
                requests.add(dependency)
    acquired = fetch(args.tool, args.config, requests, args.source, 'client-native-dependencies')
    if not acquired:
        raise ValueError('Native dependency acquisition incomplete')
    (args.output / 'hots-paths.csv').write_text(''.join(f'{k};{v}\n' for k,v in sorted(all_paths.items())), encoding='utf-8')
    (args.output / 'MountRiders.csv').write_text('\n'.join(riders) + '\n', encoding='utf-8')
    (args.output / 'HotSCasc.ini').write_text((args.source / 'HotSCasc.ini').read_text(), encoding='utf-8')
    n.save_json(args.output / 'validation.json', dict(models=checks, unresolved=unresolved,
                mappings=len(all_paths), mounts=len(ids), source=str(args.source.resolve())))
    print(json.dumps(dict(models=len(checks), mappings=len(all_paths), unresolved=len(unresolved))))


RECLINED_MOUNT = 484  # WotLK chopper pose: fallback for riders without the retail one
WIDE_SEAT = 806  # retail wide motorcycle seat (Incognitro, the Indecipherable Felcycle)


WIDE_STANCE = ('bike', 'hovercycle', 'vulture')  # motorcycle-style seats, by mount name or HotS ID
MORPHS = ('morphform', 'batform')  # the mount is the player's body


def pose_rows(report, ids):
    """EsteriaMounts skin rows: display;r;anim;fallback for wide-stance seats, display;h;0;0 to hide morph riders."""
    rows = []
    for m in report['models']:
        for d in m['displays']:
            mount = ids.get(d['hots_id'])
            if not mount:
                continue
            text = (d['hots_id'] + ' ' + mount['name']).lower()
            if any(k in text for k in MORPHS):
                rows.append(f"{mount['display']};h;0;0")
            elif m['category'] == 'Ridebike' or any(k in text for k in WIDE_STANCE):
                rows.append(f"{mount['display']};r;{WIDE_SEAT};{RECLINED_MOUNT}")
    return sorted(rows)


SEAT_BACK = 0.35  # wide-stance riders sat too far forward on Incognitro's pose; native +X is forward


def shift_seats(text, paths, back):
    """MountRiders.csv rows (path;x;y;z) with the given mounts' seats moved back along native X."""
    out = []
    for line in text.splitlines():
        path, x, y, z = line.split(';')
        out.append(f'{path};{float(x) - back:.9g};{y};{z}' if path in paths else line)
    return ''.join(l + '\n' for l in out)


def poses(args):
    report = json.loads((args.source / 'mount-inventory.json').read_text())
    ids = {m['hots_id']: m for m in json.loads((args.server / 'mounts.json').read_text())}
    rows = pose_rows(report, ids)
    (args.output / 'MountSkins.csv').write_text(''.join(row + '\n' for row in rows), encoding='utf-8')
    shutil.copy2(args.output / 'MountSkins.csv', args.client / 'Data/HotS/MountSkins.csv')
    wide = {ids[h]['display'] for h in ids if any(r.startswith(f"{ids[h]['display']};r;") for r in rows)}
    paths = {f"hots/mounts/{h.lower()}.m3" for h in ids if ids[h]['display'] in wide}
    seats = shift_seats(args.riders.read_text(), paths, SEAT_BACK)
    (args.client / 'Data/HotS/MountRiders.csv').write_text(seats, encoding='utf-8')
    print(json.dumps(dict(wideStance=sum(';r;' in r for r in rows), hiddenRiders=sum(';h;' in r for r in rows))))


def install(args):
    validation = json.loads((args.output / 'validation.json').read_text())
    if len(validation['models']) != 199 or not all(m['parser'] for m in validation['models']):
        raise ValueError('Require the complete model parser audit')
    root = args.client / 'Data/Retail'
    config_path = root / 'RetailCasc.ini'
    config_bytes = config_path.read_bytes()
    config = dict(line.split('=',1) for line in config_bytes.decode().splitlines() if '=' in line and not line.startswith('#'))
    current = root / config['tables']
    backup = args.client / '_backups' / ('hots-native-' + time.strftime('%Y%m%d-%H%M%S'))
    backup.mkdir(parents=True)
    shutil.copy2(config_path, backup / 'RetailCasc.ini')
    for name in ('Wow.exe',):
        shutil.copy2(args.client / name, backup / name)
    revision = n.digest(config_bytes + (args.server / 'stage-receipt.json').read_bytes())[:16]
    registry = root / 'Registries' / ('hots-' + revision)
    if registry.exists():
        raise ValueError('Registry already exists; verify existing receipt before repeating installation')
    shutil.copytree(current, registry)
    # combine_with needs only a directory containing tables/, not the Retail-only pack manifest.
    for table in n.TABLES:
        target = registry / (table.lower()+'.dbc')
        data = ei.combine_with(target if target.exists() else None, [args.server], table)
        target.write_bytes(data)
    hots = args.client / 'Data/HotS'
    if hots.exists():
        raise ValueError('HotS destination already exists; inspect before replacing it')
    hots.mkdir()
    originals = hots / 'originals'
    shutil.copytree(args.source / 'originals', originals)
    for name in ('HotSCasc.ini','hots-paths.csv','MountRiders.csv','validation.json'):
        shutil.copy2(args.output/name, hots/name)
    for line in (hots/'hots-paths.csv').read_text().splitlines():
        _, source = line.split(';')
        old = args.source/'originals'/source; new = originals/source
        if n.digest(old.read_bytes()) != n.digest(new.read_bytes()):
            raise ValueError(f'Installed source hash mismatch: {source}')
    lines=[]
    for line in config_bytes.decode().splitlines():
        if line.startswith('tables='):
            line=f'tables=Registries/{registry.name}'
        elif line.startswith('aliases=') and line.split('=',1)[1].startswith(config['tables']+'/'):
            line=f'aliases=Registries/{registry.name}/aliases.csv'
        lines.append(line)
    if config_path.read_bytes()!=config_bytes:
        raise ValueError('Live config changed while installing; new registry not activated')
    temporary=config_path.with_suffix('.ini.tmp');temporary.write_text('\n'.join(lines)+'\n',encoding='utf-8');temporary.replace(config_path)
    # Install the executable last, after matched metadata and original source bytes are present.
    shutil.copy2(args.exe, args.client/'Wow.exe')
    if n.digest(args.exe.read_bytes())!=n.digest((args.client/'Wow.exe').read_bytes()):
        raise ValueError('Client executable readback mismatch')
    n.save_json(args.output/'install-receipt.json',dict(state='client-installed-awaiting-live-test',backup=str(backup),
                registry=str(registry),exeSha256=n.digest(args.exe.read_bytes()),mounts=585,validation=validation))
    print(json.dumps(dict(backup=str(backup),registry=str(registry),mounts=585)))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('command',choices=['prepare','install','poses'])
    p.add_argument('--output',type=Path,default=Path('build/hots-mounts/client-playable'))
    p.add_argument('--source',type=Path,default=Path('build/hots-mounts/stage'))
    p.add_argument('--server',type=Path,default=Path('build/hots-mounts/server-install-20261112-v3'))
    p.add_argument('--client',type=Path,default=Path(r'G:\3.3.5a - Dev'))
    p.add_argument('--inspect',type=Path,default=Path('build/retail-assets/bin/Release/ASECM3Inspect.exe'))
    p.add_argument('--scene-inspect',type=Path,default=Path('build/retail-assets/bin/Release/ASECM3SceneInspect.exe'))
    p.add_argument('--tool',type=Path,default=Path('build/retail-assets/Release/HotSAssetTool.exe'))
    p.add_argument('--config',type=Path,default=Path('build/hots-mounts/HotSCasc.ini'))
    p.add_argument('--riders',type=Path,default=Path('build/hots-mounts/client-playable-final/MountRiders.csv'))
    p.add_argument('--exe',type=Path,default=Path('bin/Win/WowClientRetail.exe'))
    a=p.parse_args();{'prepare':prepare,'install':install,'poses':poses}[a.command](a)
