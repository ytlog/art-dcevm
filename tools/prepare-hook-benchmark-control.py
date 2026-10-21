#!/usr/bin/env python3
"""Build a debug benchmark control with identical harness and a different engine.

Only lib/arm64-v8a/libartdcevm.so is replaced. This does not install or run an
APK; the control and its payload proof remain under ignored build/evidence.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import traceback
import zipfile

from evidence_paths import new_evidence_directory

ROOT = Path(__file__).resolve().parents[1]
ENGINE = 'lib/arm64-v8a/libartdcevm.so'
SIGNATURE = re.compile(r'META-INF/(?:MANIFEST\.MF|[^/]+\.(?:SF|RSA|DSA|EC))', re.I)
DEX = re.compile(r'classes\d*\.dex')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def payload(archive):
    names = archive.namelist()
    if len(names) != len(set(names)):
        raise ValueError('APK has duplicate ZIP entries')
    result = {name: sha(archive.read(name)) for name in names if not SIGNATURE.fullmatch(name)}
    if ENGINE not in result or not any(DEX.fullmatch(name) for name in result):
        raise ValueError('APK lacks the ARM64 engine or application DEX')
    return result


def replace_engine(harness, donor, destination):
    with zipfile.ZipFile(harness) as original, zipfile.ZipFile(donor) as source:
        expected = payload(original)
        payload(source)
        engine = source.read(ENGINE)
        if expected[ENGINE] == sha(engine):
            raise ValueError('Control engine is identical to the harness engine')
        with zipfile.ZipFile(destination, 'w') as output:
            for info in original.infolist():
                if not SIGNATURE.fullmatch(info.filename):
                    output.writestr(info, engine if info.filename == ENGINE else original.read(info.filename))
        expected[ENGINE] = sha(engine)
        return expected


def verify_payload(target, expected):
    with zipfile.ZipFile(target) as archive:
        if payload(archive) != expected:
            raise ValueError('Control changed payload outside the requested engine replacement')


def certificate_digest(text):
    matches = re.findall(r'^Signer #\d+ certificate SHA-256 digest: ([a-f0-9]{64})$', text, re.M)
    if len(matches) != 1:
        raise ValueError('Expected exactly one APK signing certificate')
    return matches[0]


def prepare(args, out):
    sdk = args.sdk
    if sdk is None:
        properties = dict(line.split('=', 1) for line in (ROOT / 'local.properties').read_text().splitlines()
                          if '=' in line and not line.startswith('#'))
        sdk = Path(properties['sdk.dir'])
    candidates = sorted((sdk / 'build-tools').iterdir(),
                        key=lambda p: tuple(int(x) for x in re.findall(r'\d+', p.name)))
    tools = next(p for p in reversed(candidates)
                 if all((p / name).is_file() for name in ('aapt2', 'zipalign', 'apksigner')))

    def run(label, command):
        result = subprocess.run([str(x) for x in command], capture_output=True, text=True, timeout=120)
        (out / (label + '-raw.log')).write_text(result.stdout + result.stderr)
        if result.returncode:
            raise ValueError(label + ' failed')
        return result.stdout

    manifest = run('manifest', [tools / 'aapt2', 'dump', 'xmltree', '--file', 'AndroidManifest.xml', args.harness_apk])
    if not re.search(r'debuggable[^\n]*=(?:\(type 0x12\))?(?:true|0xffffffff)\b', manifest):
        raise ValueError('Benchmark controls require a debuggable harness APK')
    original_cert = certificate_digest(run('harness-certificate', [tools / 'apksigner', 'verify', '--print-certs', args.harness_apk]))
    run('donor-signature', [tools / 'apksigner', 'verify', args.engine_apk])
    unsigned, aligned, target = out / 'unsigned.apk', out / 'aligned.apk', out / 'control.apk'
    expected = replace_engine(args.harness_apk, args.engine_apk, unsigned)
    run('align', [tools / 'zipalign', '-f', '-P', '16', '4', unsigned, aligned])
    run('sign', [tools / 'apksigner', 'sign', '--ks', args.keystore, '--ks-key-alias', 'androiddebugkey',
                 '--ks-pass', 'pass:android', '--key-pass', 'pass:android', '--v4-signing-enabled', 'false',
                 '--out', target, aligned])
    actual_cert = certificate_digest(run('control-certificate', [tools / 'apksigner', 'verify', '--print-certs', target]))
    if actual_cert != original_cert:
        raise ValueError('Control signing certificate differs from the harness')
    run('alignment-check', [tools / 'zipalign', '-c', '-P', '16', '4', target])
    verify_payload(target, expected)
    unsigned.unlink()
    aligned.unlink()
    return {'passed': True, 'harnessApkSha256': sha(args.harness_apk.read_bytes()),
            'engineApkSha256': sha(args.engine_apk.read_bytes()), 'controlApkSha256': sha(target.read_bytes()),
            'replacedEntries': [ENGINE], 'engineSha256': expected[ENGINE],
            'identicalHarnessEntries': {name: digest for name, digest in expected.items() if name != ENGINE},
            'signingCertificateMatched': True, 'deviceOperationsPerformed': False, 'apk': 'control.apk'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--harness-apk', type=Path, required=True)
    parser.add_argument('--engine-apk', type=Path, required=True)
    parser.add_argument('--sdk', type=Path)
    parser.add_argument('--keystore', type=Path, default=Path.home() / '.android/debug.keystore')
    parser.add_argument('--evidence-root', type=Path, default=ROOT / 'build/evidence/hook-benchmark-control')
    args = parser.parse_args()
    if not args.evidence_root.resolve().is_relative_to(ROOT / 'build/evidence'):
        parser.error('Evidence must stay under ignored build/evidence')
    out = new_evidence_directory(args.evidence_root.resolve())
    try:
        report = prepare(args, out)
    except (ValueError, OSError, KeyError, StopIteration, subprocess.TimeoutExpired, zipfile.BadZipFile):
        (out / 'exception-raw.txt').write_text(traceback.format_exc())
        print('Unable to prepare benchmark control; inspect ' + str(out.relative_to(ROOT)))
        return 1
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print('Benchmark control prepared; payload proof: ' + str((out / 'report.json').relative_to(ROOT)))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
