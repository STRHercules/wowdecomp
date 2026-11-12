"""Read mount definitions and inherited model references from staged HotS XML."""
import xml.etree.ElementTree as ET


def audit_catalog(files, originals, imported_ids, resolve):
    records = {'CMount': {}, 'CModel': {}}
    missing = []
    for path in files:
        source = originals / path
        if not source.exists():
            missing.append(path)
            continue
        for node in ET.parse(source).getroot():
            if node.tag not in records:
                continue
            ident = node.get('id', '')
            record = records[node.tag].setdefault(ident, dict(parent='', default=False, values={}, files=[]))
            record['parent'] = node.get('parent', record['parent'])
            record['default'] = node.get('default', str(int(record['default']))) == '1'
            record['files'].append(path)
            for child in node:
                if child.tag=='RequiredAnimsEx' and child.get('FilePath'):
                    record['values'][('RequiredAnims','')]=child.get('FilePath')
                if 'value' in child.attrib:
                    record['values'][(child.tag, child.get('index', ''))] = child.get('value')

    def value(kind, ident, field, index='', leaf=None, chain=()):
        if ident in chain:
            raise ValueError(f'HotS catalog inheritance cycle: {chain + (ident,)}')
        record = records[kind].get(ident)
        if record is None:
            return None
        leaf = ident if leaf is None else leaf
        direct = record['values'].get((field, index))
        if direct is not None:
            return direct.replace('##id##', leaf)
        if ident:
            return value(kind, record['parent'], field, index, leaf, chain + (ident,))
        return None

    mounts = []
    for ident, record in sorted(records['CMount'].items()):
        if not ident or record['default']:
            continue
        model = value('CMount', ident, 'Model')
        path = value('CModel', model, 'Model') if model else None
        bank = value('CModel', model, 'RequiredAnims') if model else None
        mounts.append(dict(hots_id=ident, parent=record['parent'], model=model, modelPath=path,
                           source=resolve(path) if path else None,
                           requiredAnims=resolve(bank) if bank else None,
                           category=value('CMount', ident, 'MountCategory'),
                           hidden=value('CMount', ident, 'Flags', 'Hidden'),
                           inSuppliedCatalog=ident in imported_ids, declarations=sorted(set(record['files']))))
    return dict(scope='Core and HeroesData mountdata.xml plus gamedata/mounts XML; other catalog directories not yet audited',
                completeRosterVerified=False, missingCatalogFiles=missing, concreteMounts=len(mounts),
                suppliedIdsAbsentFromSource=sorted(imported_ids - {m['hots_id'] for m in mounts}),
                additionalSourceIds=[m['hots_id'] for m in mounts if not m['inSuppliedCatalog']], mounts=mounts)
