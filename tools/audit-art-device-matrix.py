#!/usr/bin/env python3
"""Verify collected ELF hashes and inventory Android 13 named-symbol dependencies."""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifests', nargs='+', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    evidence = (ROOT / 'build/evidence').resolve()
    if not args.output.resolve().is_relative_to(evidence):
        parser.error('Raw symbol audit must remain under build/evidence/')
    dependencies = {}
    for path in sorted((ROOT / 'artdcevm/src/main/cpp/platform/android13').rglob('*')):
        if path.suffix not in ('.h', '.cc'):
            continue
        for symbol in set(re.findall(r'"(_Z[^"\s]+)"', path.read_text())):
            dependencies.setdefault(symbol, []).append(str(path.relative_to(ROOT)))
    devices = []
    errors = []
    identities = {}
    for manifest in args.manifests:
        try:
            data = json.loads(manifest.read_text())
            if not data['complete']:
                raise ValueError('Incomplete device collection')
            libraries = []
            available = {}
            for library in data['libraries']:
                local = (manifest.parent / library['file']).resolve()
                if not local.is_relative_to(evidence):
                    raise ValueError('Device library is outside evidence directory')
                digest = hashlib.sha256(local.read_bytes()).hexdigest()
                if digest != library['sha256']:
                    raise ValueError('Library hash mismatch: ' + library['path'])
                inventory = (local.parent / 'defined-symbols.json').read_bytes()
                if hashlib.sha256(inventory).hexdigest() != library['symbolInventorySha256']:
                    raise ValueError('Symbol inventory hash mismatch: ' + library['path'])
                names = json.loads(inventory)
                for name in names:
                    available.setdefault(name.split('@', 1)[0], []).append(library['path'])
                identity = (library['name'], library['buildId'], digest)
                identities.setdefault(identity, []).append(str(manifest))
                libraries.append({k: library[k] for k in ('name', 'path', 'sha256', 'buildId')})
            symbols = [{'symbol': symbol, 'sourceFiles': files,
                        'presentIn': sorted(set(available.get(symbol, [])))}
                       for symbol, files in sorted(dependencies.items())]
            devices.append(dict(manifest=str(manifest), origin=data['origin'], device=data['device'],
                                libraries=libraries, symbols=symbols,
                                presentCount=sum(bool(x['presentIn']) for x in symbols),
                                absentCount=sum(not x['presentIn'] for x in symbols)))
        except (OSError, ValueError, KeyError, TypeError) as error:
            errors.append(f'{manifest}: {error}')
    firebase = [x for x in devices if x['origin'] == 'firebase']
    covered = sorted(set(x['device']['sdk'] for x in firebase))
    report = dict(scope='Named-symbol inventory; optional dependencies included; presence does not prove ABI compatibility',
                  devices=devices, firebaseApis=covered,
                  missingFirebaseApis=sorted(set(range(30, 38)) - set(covered)),
                  firebaseManufacturers=sorted(set(x['device']['manufacturer'] for x in firebase)),
                  artifacts=[dict(name=k[0], buildId=k[1], sha256=k[2], manifests=sorted(set(v)))
                             for k, v in sorted(identities.items())], errors=errors)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: report[k] for k in ('firebaseApis', 'missingFirebaseApis',
                                           'firebaseManufacturers', 'errors')}, indent=2))
    for device in devices:
        print(f"{device['device']['manufacturer']} API {device['device']['sdk']}: "
              f"{device['presentCount']} present / {device['absentCount']} absent baseline symbol literals")
    return 1 if errors else 0


if __name__ == '__main__':
    raise SystemExit(main())
