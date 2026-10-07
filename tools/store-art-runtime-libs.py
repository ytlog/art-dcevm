#!/usr/bin/env python3
"""Store verified device ELF collections in the project's runtime library assets."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil

ROOT = Path(__file__).resolve().parents[1]
DESTINATION = ROOT / 'assets/runtime_lib'


def slug(value):
    return re.sub(r'[^a-z0-9._-]+', '-', value.lower()).strip('-') or 'unknown'


def store(manifest):
    data = json.loads(manifest.read_text())
    if not data['complete']:
        raise ValueError('Incomplete collection: ' + str(manifest))
    device = data['device']
    if device['abi'] != 'arm64-v8a' or device['sdk'] < 30:
        raise ValueError('Expected an Android 11+ ARM64 collection')
    libraries = []
    for library in data['libraries']:
        source = (manifest.parent / library['file']).resolve()
        if not source.is_relative_to((ROOT / 'build/evidence').resolve()):
            raise ValueError('Source library is outside build/evidence/')
        if hashlib.sha256(source.read_bytes()).hexdigest() != library['sha256']:
            raise ValueError('Source ELF hash mismatch: ' + str(source))
        relative = Path(library['path'].lstrip('/'))
        if '..' in relative.parts or relative.parts[0] not in ('apex', 'system'):
            raise ValueError('Unexpected remote library path')
        libraries.append((library, source, relative))
    # Include the entire library set so Mainline updates under the same fingerprint coexist.
    identity = '\n'.join(sorted(x['path'] + ':' + x['sha256'] for x, _, _ in libraries))
    collection_id = hashlib.sha256(identity.encode()).hexdigest()[:12]
    parts = device['fingerprint'].split('/')
    build = parts[3] if len(parts) > 3 else 'unknown-build'
    target = (DESTINATION / f"api-{device['sdk']}" / slug(device['manufacturer']) /
              slug(device['model'] + '-' + device['product']) / (slug(build) + '-' + collection_id))
    target.mkdir(parents=True, exist_ok=True)
    stored = []
    for library, source, relative in libraries:
        destination = target / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination.exists():
            if hashlib.sha256(destination.read_bytes()).hexdigest() != library['sha256']:
                raise ValueError('Existing asset hash mismatch: ' + str(destination))
        else:
            shutil.copyfile(source, destination)
        if hashlib.sha256(destination.read_bytes()).hexdigest() != library['sha256']:
            raise ValueError('Copied asset hash mismatch: ' + str(destination))
        stored.append({k: library[k] for k in ('name', 'path', 'size', 'sha256', 'buildId')})
        stored[-1]['file'] = str(relative)
    record = {k: data[k] for k in ('origin', 'project', 'reservation', 'device', 'startedUtc')}
    record.update(collectionId=collection_id, libraries=stored,
                  evidenceManifest=str(manifest.resolve().relative_to(ROOT)),
                  scope='Device ELF reference assets; not runtime calibration')
    (target / 'manifest.json').write_text(json.dumps(record, indent=2) + '\n')
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifests', type=Path, nargs='+')
    args = parser.parse_args()
    try:
        for manifest in args.manifests:
            print(store(manifest).relative_to(ROOT))
        index = []
        for path in sorted(DESTINATION.glob('api-*/*/*/*/manifest.json')):
            data = json.loads(path.read_text())
            index.append(dict(directory=str(path.parent.relative_to(DESTINATION)),
                              origin=data['origin'], device=data['device'],
                              collectionId=data['collectionId'], libraryCount=len(data['libraries'])))
        (DESTINATION / 'index.json').write_text(json.dumps(index, indent=2) + '\n')
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
