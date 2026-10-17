#!/usr/bin/env python3
"""Prepare a signed ARM64 debug APK for a standalone ART collector test.

Does not install an APK, connect to a device, or reserve cloud resources.
The original DEX/native payload is preserved; evidence stays under build/evidence.
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
SIGNATURE = re.compile(r'META-INF/(?:MANIFEST\.MF|[^/]+\.(?:SF|RSA|DSA|EC))', re.I)
WRAP_PATH = 'lib/arm64-v8a/wrap.sh'


def wrapper_script(collector, jni_ids='indices'):
    if collector not in ('CC', 'CMC') or jni_ids not in ('indices', 'swapable', 'pointer'):
        raise ValueError('Unsupported collector or JNI ID mode')
    # app_main sets --nice-name before loading ART. AndroidRuntime::setArgv0
    # zeros argv, hiding -Xgc from ART's early userfaultfd selection. Filter it,
    # and remove conflicting VM flags so early and parsed GC choices agree.
    return ('''#!/system/bin/sh
process="$1"
shift
remaining="$#"
while [ "$remaining" -gt 0 ]; do
  argument="$1"
  shift
  case "$argument" in
    --nice-name=*|-Xgc:*|-Xopaque-jni-ids:*) ;;
    *) set -- "$@" "$argument" ;;
  esac
  remaining=$((remaining - 1))
done
exec "$process" -Xgc:''' + collector + ' -Xopaque-jni-ids:' + jni_ids + ''' -Xcompiler-option --debuggable -XjdwpProvider:adbconnection -XjdwpOptions:suspend=n,server=y "$@"
''').encode()


def validate_manifest(manifest):
    for attribute in ('debuggable', 'extractNativeLibs'):
        if not re.search(attribute + r'[^\n]*=(?:\(type 0x12\))?(?:true|0xffffffff)\b', manifest):
            raise ValueError('APK must enable ' + attribute)


def write_unsigned(source, destination, wrapper):
    payload = {}
    with zipfile.ZipFile(source) as original, zipfile.ZipFile(destination, 'w') as output:
        names = original.namelist()
        if len(names) != len(set(names)) or WRAP_PATH in names:
            raise ValueError('APK has duplicate entries or an existing ARM64 wrapper')
        if 'lib/arm64-v8a/libartdcevm.so' not in names:
            raise ValueError('APK lacks the ARM64 engine')
        for info in original.infolist():
            if SIGNATURE.fullmatch(info.filename):
                continue
            data = original.read(info.filename)
            output.writestr(info, data)
            payload[info.filename] = hashlib.sha256(data).hexdigest()
        info = zipfile.ZipInfo(WRAP_PATH)
        info.external_attr = 0o100755 << 16
        info.compress_type = zipfile.ZIP_STORED
        output.writestr(info, wrapper)
    return payload


def verify_payload(target, payload, wrapper):
    with zipfile.ZipFile(target) as output:
        actual = {name for name in output.namelist() if not SIGNATURE.fullmatch(name)}
        if actual != set(payload) | {WRAP_PATH} or output.read(WRAP_PATH) != wrapper:
            raise ValueError('APK payload inventory changed')
        if any(hashlib.sha256(output.read(name)).hexdigest() != sha for name, sha in payload.items()):
            raise ValueError('Original APK payload changed')


def certificate_digest(text):
    matches = re.findall(r'^Signer #\d+ certificate SHA-256 digest: ([a-f0-9]{64})$', text, re.M)
    if len(matches) != 1:
        raise ValueError('APK must have one debug signing certificate')
    return matches[0]


def prepare(args, out):
    source = args.apk.resolve()
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

    validate_manifest(run('manifest', [tools / 'aapt2', 'dump', 'xmltree', '--file', 'AndroidManifest.xml', source]))
    source_cert = certificate_digest(run('source-certificate', [tools / 'apksigner', 'verify', '--print-certs', source]))
    wrapper = wrapper_script(args.collector, args.jni_id_mode)
    (out / 'wrap.sh').write_bytes(wrapper)
    unsigned, aligned, target = out / 'unsigned.apk', out / 'aligned.apk', out / 'app.apk'
    payload = write_unsigned(source, unsigned, wrapper)
    run('align', [tools / 'zipalign', '-f', '-P', '16', '4', unsigned, aligned])
    run('sign', [tools / 'apksigner', 'sign', '--ks', args.keystore, '--ks-key-alias', 'androiddebugkey',
                 '--ks-pass', 'pass:android', '--key-pass', 'pass:android', '--v4-signing-enabled', 'false',
                 '--out', target, aligned])
    target_cert = certificate_digest(run('target-certificate', [tools / 'apksigner', 'verify', '--print-certs', target]))
    if target_cert != source_cert:
        raise ValueError('APK signing certificate changed')
    verify_payload(target, payload, wrapper)
    unsigned.unlink()
    aligned.unlink()
    return {'passed': True, 'collector': args.collector, 'jniIdMode': args.jni_id_mode,
            'sourceApkSha256': hashlib.sha256(source.read_bytes()).hexdigest(),
            'apkSha256': hashlib.sha256(target.read_bytes()).hexdigest(),
            'wrapperSha256': hashlib.sha256(wrapper).hexdigest(),
            'originalPayloadEntriesUnchanged': len(payload), 'signingCertificateMatched': True,
            'deviceOperationsPerformed': False, 'apk': 'app.apk'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apk', type=Path, required=True)
    parser.add_argument('--collector', choices=('CC', 'CMC'), required=True)
    parser.add_argument('--jni-id-mode', choices=('indices', 'swapable', 'pointer'), default='indices',
                        help='Use pointer only for explicit rejection tests')
    parser.add_argument('--sdk', type=Path)
    parser.add_argument('--keystore', type=Path, default=Path.home() / '.android/debug.keystore')
    parser.add_argument('--evidence-root', type=Path, default=ROOT / 'build/evidence/collector-apk')
    args = parser.parse_args()
    root = args.evidence_root.resolve()
    if not root.is_relative_to(ROOT / 'build/evidence'):
        parser.error('Evidence must stay under ignored build/evidence')
    out = new_evidence_directory(root)
    try:
        report = prepare(args, out)
    except (ValueError, OSError, KeyError, StopIteration, subprocess.TimeoutExpired, zipfile.BadZipFile):
        (out / 'exception-raw.txt').write_text(traceback.format_exc())
        print('Unable to prepare collector APK; inspect ' + str(out.relative_to(ROOT)))
        return 1
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    print('Evidence: ' + str(out.relative_to(ROOT)))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
