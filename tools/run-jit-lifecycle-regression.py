#!/usr/bin/env python3
"""Run 32 mixed structural/body updates in one process, with or without the engine."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import runpy
import re
import subprocess
import zipfile
from evidence_paths import new_evidence_directory

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = 'dev.artdcevm.lab'
COMPONENT = PACKAGE + '/com.github.ytlog.artdcevm.testing.JitMixedInvalidationDiagnostic'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', default=os.environ.get('ANDROID_SERIAL'), required=False)
    parser.add_argument('--device-id', required=True)
    parser.add_argument('--apk', type=Path, default=ROOT/'app/build/outputs/apk/debug/app-debug.apk')
    parser.add_argument('--engine', choices=['on', 'off'], default='on')
    parser.add_argument('--fixtures', type=Path, help='Reuse previously generated h0..h32/v0..v32 DEX files')
    args = parser.parse_args()
    if not args.serial or args.device_id == args.serial or not re.fullmatch(r'[a-z][a-z0-9-]{0,63}', args.device_id):
        parser.error('Select a device and supply a distinct anonymous device-id')
    out = new_evidence_directory(ROOT/'build/evidence/jit-lifecycle')
    print('Evidence: '+str(out.relative_to(ROOT)), flush=True)
    def run(command, timeout=120):
        r = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
        with (out/'commands.log').open('a') as log:
            log.write(r.stdout+r.stderr)
        if r.returncode:
            raise RuntimeError('Command failed; inspect ignored evidence')
        return r.stdout
    def adb(*command, timeout=120):
        return run(['adb', '-s', args.serial, *command], timeout)
    fixtures = args.fixtures or out/'fixtures'
    if not args.fixtures:
        java = runpy.run_path(str(ROOT/'tools/test-structural-policy-host.py'))['java_home'](None)
        sdk = os.environ.get('ANDROID_HOME') or os.environ.get('ANDROID_SDK_ROOT')
        if not sdk:
            for line in (ROOT/'local.properties').read_text().splitlines():
                if line.startswith('sdk.dir='):
                    sdk = line.split('=', 1)[1]
        if not sdk:
            raise RuntimeError('Android SDK unavailable')
        d8 = sorted(Path(sdk).glob('build-tools/*/d8'))[-1]
        for prefix, name in [('h', 'Hot'), ('v', 'Target')]:
            for i in range(33):
                folder = fixtures/(prefix+str(i)); folder.mkdir(parents=True)
                fields = ''.join('public int field'+str(n)+';' for n in range(i)) if prefix == 'v' else ''
                src = folder/(name+'.java')
                src.write_text('package baseline; public class '+name+' {public int value(){return '+str(i)+';}'+fields+'}')
                run([str(java/'bin/javac'), '--release', '8', '-d', str(folder), str(src)])
                run([str(d8), '--min-api', '30', '--output', str(folder), str(folder/'baseline'/(name+'.class'))])
    hashes = {prefix+str(i): hashlib.sha256((fixtures/(prefix+str(i))/'classes.dex').read_bytes()).hexdigest()
              for prefix in ('h', 'v') for i in range(33)}
    apk_hash = hashlib.sha256(args.apk.read_bytes()).hexdigest()
    with zipfile.ZipFile(args.apk) as archive:
        engine_hash = hashlib.sha256(archive.read('lib/arm64-v8a/libartdcevm.so')).hexdigest()
    receipt = {'device': args.device_id, 'apkSha256': apk_hash, 'engineSha256': engine_hash,
               'engineRequested': args.engine == 'on', 'fixtureHashes': hashes}
    (out/'provenance.json').write_text(json.dumps(receipt, indent=2)+'\n')
    adb('install', '-r', str(args.apk))
    installed = adb('shell', 'pm', 'path', PACKAGE).strip().removeprefix('package:')
    assert adb('shell', 'sha256sum', installed).split()[0] == apk_hash
    adb('shell', 'run-as', PACKAGE, 'mkdir', '-p', 'files/jit-mixed')
    adb('shell', 'run-as', PACKAGE, 'rm', '-f', 'files/jit-mixed/report.json')
    for name in hashes:
        adb('push', str(fixtures/name/'classes.dex'), '/data/local/tmp/jit-lifecycle.dex')
        adb('shell', 'run-as', PACKAGE, 'cp', '/data/local/tmp/jit-lifecycle.dex', 'files/jit-mixed/'+name+'.dex')
    try:
        adb('shell', 'am', 'instrument', '-w', '-e', 'engine', str(args.engine == 'on').lower(), COMPONENT, timeout=300)
    except subprocess.TimeoutExpired:
        (out/'timeout.txt').write_text('Instrumentation exceeded fixed 300-second limit\n')
    raw = adb('shell', 'run-as', PACKAGE, 'cat', 'files/jit-mixed/report.json')
    (out/'report.json').write_text(raw)
    report = json.loads(raw)
    (out/'runtime.logcat').write_text(adb('logcat', '-d', '--pid='+str(report['pid'])))
    assert report['engineLoaded'] == receipt['engineRequested']
    assert report.get('complete') and report.get('result') == 'PASS', 'Incomplete/failed lifecycle regression'
    assert len(report['rounds']) == 32
    for i, row in enumerate(report['rounds'], 1):
        assert row == {'round': i, 'builderChecksum': 700000, 'hotChecksum': 100000*(i-1), 'value': i}
    print(args.device_id+': PASS 32/32; engine='+args.engine)


if __name__ == '__main__':
    main()
