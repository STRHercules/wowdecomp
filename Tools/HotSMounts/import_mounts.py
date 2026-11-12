"""Inventory and stage original HotS mounts; never install assets or modify live tables."""
import argparse
import hashlib
import json
import subprocess
from collections import Counter, defaultdict
from pathlib import Path, PurePosixPath
from catalog import audit_catalog


def virtual(path):
    path = path.replace('\\', '/').lower()
    value = PurePosixPath(path)
    if not path or value.is_absolute() or ':' in path or '..' in value.parts or '\n' in path or ';' in path:
        raise ValueError(f'Unsafe source path: {path!r}')
    return path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inventory(catalog, index):
    mounts = json.loads(catalog.read_text(encoding='utf-8'))
    by_name = defaultdict(list)
    metadata = []
    with index.open(encoding='utf-8') as stream:
        for line in stream:
            path = virtual(line.strip())
            if path.endswith(('.m3', '.m3a', '.dds')):
                by_name[PurePosixPath(path).name].append(path)
            if path.startswith(('mods/core.stormmod/', 'mods/heroesdata.stormmod/')) and '/gamedata/' in path:
                if path.endswith('/mountdata.xml') or '/gamedata/mounts/' in path and path.endswith('.xml'):
                    metadata.append(path)

    def resolve(name, like=None):
        candidates = sorted(set(by_name.get(PurePosixPath(virtual(name)).name, [])))
        if like:
            same = [p for p in candidates if p.rsplit('/', 1)[0] == like.rsplit('/', 1)[0]]
            if len(same) == 1:
                return same[0]
        main = [p for p in candidates if p.startswith('mods/heroes.stormmod/base.stormassets/')]
        if len(main) == 1:
            return main[0]
        return candidates[0] if len(candidates) == 1 else None

    result = []
    identifiers = set()
    requests = set(metadata)
    for model in mounts:
        source = resolve(model['source'])
        banks = []
        stem = PurePosixPath(virtual(model['source'])).stem
        while stem:
            bank = resolve(stem + '_requiredanims.m3a', source)
            if bank:
                banks.append(bank)
            if '_' not in stem:
                break
            stem = stem.rsplit('_', 1)[0]
        displays = []
        for display in model['displays']:
            if display['hots_id'] in identifiers:
                raise ValueError(f"Duplicate HotS mount ID: {display['hots_id']}")
            identifiers.add(display['hots_id'])
            textures = []
            for texture in display['textures']:
                if not texture:
                    continue
                name = texture if texture.lower().endswith('.dds') else texture + '.dds'
                resolved = resolve(name, source)
                textures.append(dict(name=name, source=resolved))
                if resolved:
                    requests.add(resolved)
            displays.append(dict(display, nativeTextures=textures))
        for path in [source, *banks]:
            if path:
                requests.add(path)
        result.append(dict(name=model['name'], source=source, sourceHint=model['source'],
                           banks=sorted(set(banks)), category=model['category'],
                           referenceRider=model['rider'], referenceBounds=model['bounds'],
                           referenceAnimations=model['animations'], displays=displays))
    return dict(stage='inventory', completeRosterVerified=False,
                catalog=dict(path=str(catalog.resolve()), sha256=digest(catalog)),
                sourceIndex=dict(path=str(index.resolve()), sha256=digest(index)),
                counts=dict(models=len(result), variants=len(identifiers),
                            resolvedModels=sum(bool(m['source']) for m in result),
                            categories=dict(Counter(m['category'] for m in result))),
                models=result, catalogFiles=sorted(set(metadata)), requestedFiles=sorted(requests)), resolve


def fetch(tool, config, requests, output, label):
    listing = output / (label + '-requests.txt')
    listing.write_text(''.join('hots/' + p + '\n' for p in sorted(set(requests))), encoding='utf-8')
    with (output / (label + '-fetch.log')).open('w', encoding='utf-8') as log:
        status = subprocess.run([str(tool), str(config), 'fetch', str(listing), str(output / 'originals')],
                                stdout=log, stderr=subprocess.STDOUT)
    return status.returncode == 0


def stage(report, resolve, tool, config, inspector, output):
    acquired = fetch(tool, config, report['requestedFiles'], output, 'models-and-variants')
    dependencies = set()
    inspected = []
    paths = {}
    imported_ids = {d['hots_id'] for m in report['models'] for d in m['displays']}
    catalog = audit_catalog(report['catalogFiles'], output / 'originals', imported_ids, resolve)
    (output / 'catalog-audit.json').write_text(json.dumps(catalog, indent=2), encoding='utf-8')
    by_source = {m['source']: m for m in report['models'] if m['source']}
    for mount in catalog['mounts']:
        for dependency in (mount['source'], mount['requiredAnims']):
            if dependency:
                dependencies.add(dependency)
        if mount['source'] and not mount['inSuppliedCatalog']:
            if mount['source'] not in by_source:
                model = dict(name=mount['model'], source=mount['source'], category=mount['category'], banks=[], displays=[])
                report['models'].append(model)
                by_source[mount['source']] = model
            by_source[mount['source']]['displays'].append(dict(hots_id=mount['hots_id'], name=mount['hots_id'],
                                                              nativeTextures=[], needsCatalogMaterialResolution=True))
        if mount['source'] in by_source and mount['requiredAnims']:
            banks = by_source[mount['source']]['banks']
            if mount['requiredAnims'] not in banks:
                banks.append(mount['requiredAnims'])
    acquired = fetch(tool, config, dependencies, output, 'catalog-models') and acquired
    report['counts'].update(sourceMountRecords=catalog['concreteMounts'], stagedModels=len(report['models']),
                            stagedVariants=sum(len(m['displays']) for m in report['models']),
                            additionalSourceIds=len(catalog['additionalSourceIds']))
    for model in report['models']:
        if not model['source']:
            continue
        original = output / 'originals' / model['source']
        if not original.exists():
            inspected.append(dict(source=model['source'], ok=False, error='Source not acquired'))
            continue
        result = subprocess.run([str(inspector), str(original)], capture_output=True, text=True)
        try:
            proof = json.loads(result.stdout)['files'][0]
        except (ValueError, KeyError, IndexError):
            proof = dict(ok=False, error=result.stderr or result.stdout)
        proof['source'] = model['source']
        inspected.append(proof)
        for layer in proof.get('textureLayers', []):
            bitmap = layer.get('bitmap')
            if not bitmap:
                continue
            dependency = resolve(bitmap, model['source'])
            if dependency:
                name = 'hots/' + virtual(bitmap)
                if name in paths and paths[name] != dependency:
                    raise ValueError(f'Conflicting dependency mapping: {name}')
                paths[name] = dependency
                dependencies.add(dependency)
            else:
                model.setdefault('unresolvedDependencies', []).append(bitmap)
    acquired = fetch(tool, config, dependencies, output, 'dependencies') and acquired
    (output / 'hots-paths.csv').write_text(''.join(f'{k};{v}\n' for k, v in sorted(paths.items())), encoding='utf-8')
    (output / 'inspection.json').write_text(json.dumps(inspected, indent=2), encoding='utf-8')
    files = [dict(path=p.relative_to(output / 'originals').as_posix(), bytes=p.stat().st_size, sha256=digest(p))
             for p in sorted((output / 'originals').rglob('*')) if p.is_file() and not p.name.endswith('.tmp')]
    report.update(stage='source-staged' if acquired else 'source-staging-incomplete', allRequestsAcquired=acquired,
                  inspectedModels=sum(bool(p.get('ok')) for p in inspected), files=files,
                  config=dict(path=str(config.resolve()), sha256=digest(config), text=config.read_text()),
                  pending=['Source mount catalog reconciliation outside gamedata/mounts',
                           'Resolve parser failures recorded in inspection.json', 'Variant material overrides',
                           'Esteria additive IDs, tables, spells and collection',
                           'Rider and animation validation', 'Live gameplay and VFX acceptance'])
    settings = dict(line.split('=', 1) for line in config.read_text().splitlines()
                    if '=' in line and not line.lstrip().startswith(('#', ';')))
    (output / 'HotSCasc.ini').write_text('mode=files\npath=originals\nproduct=hero\n'
                                        f"build={settings['build']}\nlocale={settings.get('locale', '2')}\npaths=hots-paths.csv\n", encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['inventory', 'stage'])
    parser.add_argument('--catalog', type=Path, required=True)
    parser.add_argument('--index', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--tool', type=Path)
    parser.add_argument('--config', type=Path)
    parser.add_argument('--inspect', type=Path)
    args = parser.parse_args()
    if args.command == 'stage' and not all((args.tool, args.config, args.inspect)):
        parser.error('stage requires --tool, --config and --inspect')
    report, resolve = inventory(args.catalog, args.index)
    args.output.mkdir(parents=True, exist_ok=True)
    if args.command == 'stage':
        stage(report, resolve, args.tool, args.config, args.inspect, args.output)
    (args.output / 'mount-inventory.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(dict(stage=report['stage'], **report['counts'],
                         sourceCatalogs=len(report['catalogFiles']), requests=len(report['requestedFiles']))))
    return 1 if report.get('allRequestsAcquired') is False else 0


if __name__ == '__main__':
    raise SystemExit(main())
