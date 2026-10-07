#!/usr/bin/env python3
"""Verify collected ELF hashes and inventory Android 13 named-symbol dependencies."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def evolution_dependencies(cxx, directory, manifests):
    """Enumerate the production gate, without duplicating its generated names."""
    cpp = ROOT / 'artdcevm/src/main/cpp'
    with tempfile.TemporaryDirectory(prefix='symbol-inventory-', dir=directory) as temporary:
        binary = Path(temporary) / 'inventory'
        subprocess.run([cxx, '-std=c++17', '-I', str(cpp),
                        str(ROOT / 'tools/art-symbol-dependencies.cc'),
                        str(cpp / 'platform/symbols/art_symbol_compat.cc'),
                        str(cpp / 'support/elf_symbol_names.cc'),
                        '-o', str(binary)], check=True, capture_output=True, text=True)
        output = subprocess.run([str(binary)], check=True, capture_output=True, text=True).stdout
        aliases = {}
        evidence = (ROOT / 'build/evidence').resolve()
        for manifest in manifests:
            # Invalid collections are reported by the normal audit below. Only
            # independently hashed files under evidence enter the name reader.
            try:
                data = json.loads(manifest.read_text())
                if not data['complete']:
                    continue
                for library in data['libraries']:
                    path = (manifest.parent / library['file']).resolve()
                    if not path.is_relative_to(evidence):
                        continue
                    digest = hashlib.sha256(path.read_bytes()).hexdigest()
                    if digest != library['sha256'] or digest in aliases:
                        continue
                    names = subprocess.run([str(binary), str(path)], check=True,
                                           capture_output=True, text=True).stdout
                    aliases[digest] = dict(line.split('\t') for line in names.splitlines())
            except (OSError, ValueError, KeyError, TypeError):
                continue
    required = {}
    for line in output.splitlines():
        library, symbol = line.split('\t')
        required.setdefault(symbol, set()).add(library)
    if not required:
        raise ValueError('Production evolution symbol inventory is empty')
    return required, aliases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifests', nargs='+', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--cxx', default='c++', help='Host C++17 compiler for the production symbol enumerator')
    args = parser.parse_args()
    evidence = (ROOT / 'build/evidence').resolve()
    if not args.output.resolve().is_relative_to(evidence):
        parser.error('Raw symbol audit must remain under build/evidence/')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    try:
        required, aliases = evolution_dependencies(args.cxx, args.output.parent, args.manifests)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        detail = error.stderr if isinstance(error, subprocess.CalledProcessError) else str(error)
        parser.error('Cannot enumerate production evolution dependencies: ' + detail)
    dependencies = {}
    for path in sorted((ROOT / 'artdcevm/src/main/cpp/platform/android13').rglob('*')):
        if path.suffix not in ('.h', '.cc'):
            continue
        for symbol in set(re.findall(r'"(_Z[^"\s]+)"', path.read_text())):
            dependencies.setdefault(symbol, []).append(str(path.relative_to(ROOT)))
    for symbol in required:
        dependencies.setdefault(symbol, []).append('artdcevm/src/main/cpp/platform/symbols/art_symbol_compat.cc')
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
            unique_names = {}
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
                names = set(json.loads(inventory))
                for base, full in aliases.get(digest, {}).items():
                    if full not in names:
                        raise ValueError('Unique name missing from verified symbol inventory: ' + full)
                    unique_names.setdefault(base, []).append(dict(path=library['path'], symbol=full))
                for name in names:
                    available.setdefault(name.split('@', 1)[0], []).append(library['path'])
                identity = (library['name'], library['buildId'], digest)
                identities.setdefault(identity, []).append(str(manifest))
                libraries.append({k: library[k] for k in ('name', 'path', 'sha256', 'buildId')})
            def paths(symbol):
                return set(available.get(symbol, [])) | {name['path'] for name in unique_names.get(symbol, [])}

            symbols = [{'symbol': symbol, 'sourceFiles': sorted(set(files)),
                        'exactPresentIn': sorted(set(available.get(symbol, []))),
                        'uniqueNameMatches': sorted(unique_names.get(symbol, []), key=lambda name: name['path']),
                        'presentIn': sorted(paths(symbol)),
                        'requiredLibraries': sorted(required.get(symbol, set())),
                        'missingRequiredLibraries': sorted(
                            library for library in required.get(symbol, set())
                            if not any(Path(path).name == library for path in paths(symbol)))}
                       for symbol, files in sorted(dependencies.items())]
            devices.append(dict(manifest=str(manifest), origin=data['origin'], device=data['device'],
                                libraries=libraries, symbols=symbols,
                                presentCount=sum(bool(x['presentIn']) for x in symbols),
                                absentCount=sum(not x['presentIn'] for x in symbols),
                                evolutionRequiredCount=len(required),
                                evolutionMissingCount=sum(bool(x['missingRequiredLibraries']) for x in symbols)))
        except (OSError, ValueError, KeyError, TypeError) as error:
            errors.append(f'{manifest}: {error}')
    firebase = [x for x in devices if x['origin'] == 'firebase']
    covered = sorted(set(x['device']['sdk'] for x in firebase))
    report = dict(scope='Production evolution gate plus Android 13 literal dependencies; exact and unique local names from production ELF reader; optional dependencies included; presence does not prove ABI or loader namespace compatibility',
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
              f"{device['presentCount']} present / {device['absentCount']} absent named dependencies; "
              f"{device['evolutionMissingCount']} / {device['evolutionRequiredCount']} evolution dependencies missing from required library")
    return 1 if errors else 0


if __name__ == '__main__':
    raise SystemExit(main())
