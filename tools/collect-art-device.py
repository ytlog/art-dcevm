#!/usr/bin/env python3
"""Pull ART ELF libraries from a connected device without installing an APK."""
import argparse
from datetime import datetime, timezone
import gzip
import hashlib
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
LIBRARIES = ('libart.so', 'libartd.so', 'libopenjdkjvmti.so', 'libopenjdkjvmtid.so',
             'libdexfile.so', 'libdexfiled.so', 'libart-compiler.so',
             'libartd-compiler.so', 'libartbase.so', 'libc++.so')
DIRECTORIES = ('/apex/com.android.art/lib64', '/apex/com.android.runtime/lib64',
               '/apex/com.android.runtime/lib64/bionic', '/system/lib64')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--adb', default=shutil.which('adb'))
    parser.add_argument('--readelf', default=shutil.which('llvm-readelf'))
    parser.add_argument('--origin', required=True, choices=('firebase', 'local'))
    parser.add_argument('--project')
    parser.add_argument('--reservation')
    parser.add_argument('--expected-sdk', type=int)
    parser.add_argument('--pull-timeout', type=int, default=300,
                        help='Per-library deadline for slower remote ADB links')
    parser.add_argument('--compress', action='store_true',
                        help='Use device gzip over adb exec-out to reduce remote transfer time')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if not args.adb or not args.readelf:
        parser.error('Pass --adb and --readelf or make both tools available on PATH')
    if args.origin == 'firebase' and not (args.project and args.reservation):
        parser.error('Firebase provenance requires --project and --reservation')
    if args.pull_timeout <= 0:
        parser.error('--pull-timeout must be positive')
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    out = (args.output or ROOT / 'build/evidence/art-devices' / stamp).resolve()
    evidence = (ROOT / 'build/evidence').resolve()
    if not out.is_relative_to(evidence):
        parser.error('Device libraries and raw reports must remain under build/evidence/')
    if out.exists() and any(out.iterdir()):
        parser.error('Use a new output directory; existing evidence is never overwritten')
    out.mkdir(parents=True, exist_ok=True)
    report = dict(origin=args.origin, project=args.project, reservation=args.reservation,
                  serial=args.serial, startedUtc=stamp, complete=False, libraries=[], errors=[])

    def run(label, command, required=True, timeout=60):
        try:
            result = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
        except subprocess.TimeoutExpired as error:
            def decode(value):
                return value.decode(errors='replace') if isinstance(value, bytes) else value or ''
            (out / (label + '.txt')).write_text(decode(error.stdout) + decode(error.stderr))
            raise RuntimeError(f'{label} timed out after {timeout}s') from error
        (out / (label + '.txt')).write_text(result.stdout + result.stderr)
        if required and result.returncode:
            raise RuntimeError(f'{label} failed ({result.returncode}); see raw log')
        return result

    def adb(label, *parts, required=True, timeout=60):
        return run(label, [args.adb, '-s', args.serial, *parts], required, timeout).stdout.strip()

    try:
        raw_props = adb('properties', 'shell', 'getprop')
        props = dict(re.findall(r'^\[([^]]+)\]: \[(.*)\]$', raw_props, re.MULTILINE))
        report['device'] = {key: props.get(prop, '') for key, prop in (
            ('manufacturer', 'ro.product.manufacturer'), ('brand', 'ro.product.brand'),
            ('model', 'ro.product.model'), ('product', 'ro.product.device'),
            ('fingerprint', 'ro.build.fingerprint'), ('sdk', 'ro.build.version.sdk'),
            ('sdkFull', 'ro.build.version.sdk_full'), ('release', 'ro.build.version.release'),
            ('securityPatch', 'ro.build.version.security_patch'),
            ('abi', 'ro.product.cpu.abi'), ('abilist', 'ro.product.cpu.abilist'))}
        device = report['device']
        device['sdk'] = int(device['sdk'])
        device['pageSize'] = int(adb('page-size', 'shell', 'getconf', 'PAGESIZE'))
        if device['sdk'] < 30 or (args.expected_sdk and device['sdk'] != args.expected_sdk):
            raise RuntimeError('Device API does not match requested Android 11+ collection')
        if device['abi'] != 'arm64-v8a':
            raise RuntimeError('Private ART collection currently requires arm64-v8a')
        if args.origin == 'firebase' and props.get('ro.kernel.qemu') == '1':
            raise RuntimeError('Firebase collection requires a physical device')
        adb('apex-packages', 'shell', 'dumpsys', 'apexservice', required=False)
        adb('mounts', 'shell', 'cat', '/proc/mounts', required=False)
        # Versioned APEX mount aliases are deduplicated using the canonical file path.
        candidates = ' '.join(shlex.quote(d + '/' + n) for d in DIRECTORIES for n in LIBRARIES)
        paths = adb('library-paths', 'shell',
                    f'for f in {candidates}; do if [ -f "$f" ]; then readlink -f "$f"; fi; done')
        paths = sorted(set(paths.splitlines()))
        hashes = adb('remote-library-sha256', 'shell',
                     'sha256sum ' + ' '.join(shlex.quote(path) for path in paths)) if paths else ''
        remote_hashes = {path: digest for digest, path in
                         re.findall(r'^([0-9a-f]{64})\s+\*?(.+)$', hashes, re.MULTILINE)}
        cache = {}
        asset_root = (ROOT / 'assets/runtime_lib').resolve()
        for stored_manifest in asset_root.glob('api-*/*/*/*/manifest.json'):
            try:
                stored = json.loads(stored_manifest.read_text())
                for library in stored['libraries']:
                    source = (stored_manifest.parent / library['file']).resolve()
                    if source.is_relative_to(asset_root):
                        cache.setdefault(library['sha256'], source)
            except (OSError, ValueError, KeyError, TypeError):
                continue
        for index, path in enumerate(paths):
            name = Path(path).name
            if name not in LIBRARIES or not path.startswith(('/apex/', '/system/')):
                raise RuntimeError(f'Unexpected device library path: {path}')
            folder = out / 'libraries' / f'{index:02d}-{name}'
            folder.mkdir(parents=True)
            local = folder / name
            entry = dict(path=path, name=name, file=str(local.relative_to(out)), complete=False)
            report['libraries'].append(entry)
            try:
                expected_hash = remote_hashes.get(path)
                if not expected_hash:
                    raise RuntimeError('Device did not report a SHA-256 for this library')
                entry['deviceSha256'] = expected_hash
                cached_file = cache.get(expected_hash)
                reusable = (cached_file and cached_file.is_file() and
                            hashlib.sha256(cached_file.read_bytes()).hexdigest() == expected_hash)
                if reusable:
                    shutil.copyfile(cached_file, local)
                    entry['reusedAsset'] = str(cached_file.relative_to(ROOT))
                    (out / f'pull-{index}.txt').write_text(
                        f'Verified local device asset reused; remote SHA-256={expected_hash}\n')
                elif args.compress:
                    transfer = subprocess.run(
                        [args.adb, '-s', args.serial, 'exec-out',
                         'gzip -c ' + shlex.quote(path)], capture_output=True, timeout=args.pull_timeout)
                    (out / f'pull-{index}.txt').write_text(
                        f'gzip transfer: exit={transfer.returncode}, bytes={len(transfer.stdout)}\n' +
                        transfer.stderr.decode(errors='replace'))
                    if transfer.returncode:
                        raise RuntimeError('Device gzip transfer failed')
                    local.write_bytes(gzip.decompress(transfer.stdout))
                else:
                    adb(f'pull-{index}', 'pull', path, str(local), timeout=args.pull_timeout)
                data = local.read_bytes()
                if data[:4] != b'\x7fELF':
                    raise RuntimeError('Pulled library is not ELF')
                if hashlib.sha256(data).hexdigest() != expected_hash:
                    raise RuntimeError('ELF content differs from device-reported SHA-256')
                entry.update(sha256=hashlib.sha256(data).hexdigest(), size=len(data))
                notes = run(f'notes-{index}', [args.readelf, '--notes', '--file-header', str(local)]).stdout
                match = re.search(r'Build ID:\s*([0-9a-fA-F]+)', notes)
                entry['buildId'] = match.group(1).lower() if match else None
                if 'AArch64' not in notes:
                    raise RuntimeError('ELF machine is not AArch64')
                symbols = run(f'symbols-{index}', [args.readelf, '--symbols', '--wide', str(local)]).stdout
                run(f'symbols-demangled-{index}',
                    [args.readelf, '--symbols', '--wide', '--demangle', str(local)])
                names = sorted(set(re.findall(
                    r'^\s*\d+:\s+\S+\s+\d+\s+\S+\s+\S+\s+\S+\s+(?!UND\b)\S+\s+(.+)$',
                    symbols, re.MULTILINE)))
                symbol_file = folder / 'defined-symbols.json'
                symbol_file.write_text(json.dumps(names, indent=2) + '\n')
                entry.update(definedSymbolCount=len(names),
                             symbolInventorySha256=hashlib.sha256(symbol_file.read_bytes()).hexdigest(),
                             complete=True)
            except (OSError, EOFError, RuntimeError, subprocess.SubprocessError) as error:
                entry['error'] = str(error)
                report['errors'].append(f'{path}: {error}')
        # Collection inventories actual ELF packaging; a separately loaded compiler is not
        # universal. AOSP 15+ links the compiler into libart. Runtime calibration is separate.
        report['standaloneCompilerCollected'] = any(
            x['name'] == 'libart-compiler.so' and x['complete'] for x in report['libraries'])
        if not report['standaloneCompilerCollected']:
            report['notes'] = ['No standalone libart-compiler.so found; check compiler packaging in pinned AOSP sources']
        for required in ('libart.so', 'libopenjdkjvmti.so', 'libdexfile.so', 'libc++.so'):
            if not any(x['name'] == required and x['complete'] for x in report['libraries']):
                report['errors'].append('Missing required library: ' + required)
        report['complete'] = not report['errors']
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        report['errors'].append(str(error))
    report['finishedUtc'] = datetime.now(timezone.utc).isoformat()
    report['scope'] = 'Device ELF and named symbols only; not runtime calibration or proof of loaded library'
    manifest = out / 'manifest.json'
    manifest.write_text(json.dumps(report, indent=2) + '\n')
    if report['complete']:
        report['assetStorageComplete'] = False
        try:
            storage = run('runtime-assets', [sys.executable, str(ROOT / 'tools/store-art-runtime-libs.py'),
                                            str(manifest)], required=False)
            if storage.returncode:
                raise RuntimeError('Runtime asset storage failed; see runtime-assets.txt')
            report['assetStorageComplete'] = True
            report['runtimeAssetDirectory'] = storage.stdout.strip()
        except (OSError, RuntimeError, subprocess.SubprocessError) as error:
            report['errors'].append(str(error))
        manifest.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return 0 if report['complete'] and not report['errors'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
