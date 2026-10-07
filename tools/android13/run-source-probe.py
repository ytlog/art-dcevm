#!/usr/bin/env python3
"""Install the opt-in source-engine APK and test ABI, method and field evolution."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--serial', required=True)
p.add_argument('--hook-order', choices=['validation-first', 'source-first'], default='validation-first')
p.add_argument('--suite', choices=['all', 'hierarchy', 'narrow', 'narrow-empty', 'narrow-commit', 'narrow-roundtrip', 'narrow-concurrent', 'narrow-special', 'narrow-swap', 'narrow-swap-batch', 'narrow-swap-batch-reverse', 'narrow-swap-concurrent', 'narrow-swap-concurrent-reverse', 'jit-parent-swap-spin', 'interface-remove', 'interface-roundtrip', 'interface-concurrent', 'interface-batch', 'interface-three-batch', 'interface-transfer', 'interface-mixed-batch', 'interface-default', 'interface-default-checkjni', 'interface-default-shadow', 'interface-default-special', 'loader-baseline', 'loader-evolved', 'loader-retired', 'loader-field-cycles', 'loader-field-concurrent', 'loader-mixed', 'loader-mixed-long', 'loader-mixed-cache', 'aot-loader', 'aot-experiment', 'aot-active-evolve', 'aot-active-frame', 'aot-compiled-caller', 'aot-interface-remove', 'aot-interface-default', 'aot-parent-swap', 'aot-parent-swap-two', 'aot-parent-swap-nondebug', 'aot-suspend-deopt', 'jit-inline', 'jit-inline-optimized', 'jit-inline-active', 'jit-boot-inline-diagnostic', 'field-read', 'jni-receiver', 'modifier', 'volatile', 'private', 'private-visibility'], default='all')
p.add_argument('--skip-install', action='store_true', help='Verify the installed APK hash and retain its dexopt state')
args = p.parse_args()
sdk = next(line.split('=', 1)[1] for line in (ROOT / 'local.properties').read_text().splitlines() if line.startswith('sdk.dir='))
adb = str(Path(sdk) / 'platform-tools/adb')
apk = ROOT / 'app/build/outputs/apk/debug/app-debug.apk'
with zipfile.ZipFile(apk) as archive:
    if 'lib/arm64-v8a/libartdcevm_source13.so' not in archive.namelist():
        p.error('Build the APK with -PsourceEngine13 first')
out = ROOT / 'build/evidence/source-engine' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
out.mkdir(parents=True)
meta = {'serial': args.serial, 'apkSha256': hashlib.sha256(apk.read_bytes()).hexdigest(),
        'fullDcevmComplete': False, 'hookOrder': args.hook_order, 'suite': args.suite,
        'installSkipped': args.skip_install}
instrumentation_timeout = 1200 if args.suite in ('all', 'loader-mixed-long') else 360
meta['instrumentationTimeoutSeconds'] = instrumentation_timeout

def command(*parts, timeout=360):
    return subprocess.run([adb, '-s', args.serial, *parts], capture_output=True, text=True, timeout=timeout)

try:
    if args.skip_install:
        packages = command('shell', 'pm', 'path', 'dev.artdcevm.lab')
        packages.check_returncode()
        apk_path = next((line.removeprefix('package:') for line in packages.stdout.splitlines()
                         if line.endswith('/base.apk')), None)
        if apk_path is None:
            raise ValueError('No installed base.apk found for dev.artdcevm.lab')
        installed = command('shell', 'sha256sum', apk_path)
        installed.check_returncode()
        meta['installedApkSha256'] = installed.stdout.split()[0]
        if meta['installedApkSha256'] != meta['apkSha256']:
            raise ValueError('Installed APK differs from local build; install the current APK before testing')
        (out / 'install.txt').write_text('Skipped install; installed APK SHA-256 verified: '
                                           + meta['installedApkSha256'] + '\n')
    else:
        install = command('install', '-r', str(apk))
        (out / 'install.txt').write_text(install.stdout + install.stderr)
        install.check_returncode()
    command('shell', 'am', 'force-stop', 'dev.artdcevm.lab').check_returncode()
    command('shell', 'run-as', 'dev.artdcevm.lab', 'rm', '-f', 'files/source-engine.json').check_returncode()
    run = None
    try:
        run = command('shell', 'am', 'instrument', '-w', '-r', '-e', 'hookOrder', args.hook_order, '-e', 'suite', args.suite,
                      'dev.artdcevm.lab/com.github.ytlog.artdcevm.platform.android13.SourceEngineInstrumentation',
                      timeout=instrumentation_timeout)
        (out / 'instrumentation.txt').write_text(run.stdout + run.stderr)
    except subprocess.TimeoutExpired as error:
        meta['instrumentationTimedOut'] = True
        meta['error'] = str(error)
        # TimeoutExpired can expose bytes even when subprocess uses text=True.
        def partial_text(value):
            return value.decode('utf-8', errors='replace') if isinstance(value, bytes) else value or ''
        (out / 'instrumentation.txt').write_text(partial_text(error.stdout) + partial_text(error.stderr))
        # Recover the last device checkpoint, but a missing completion from am
        # remains a failed run even if the report already records test success.
    report = command('exec-out', 'run-as', 'dev.artdcevm.lab', 'cat', 'files/source-engine.json')
    (out / 'probe.json').write_text(report.stdout)
    data = json.loads(report.stdout)
    (out / 'probe.json').write_text(json.dumps(data, indent=2) + '\n')
    meta['passed'] = (run is not None and run.returncode == 0 and data.get('completed') is True and
                      data.get('readOnlyProbePassed') is True and data.get('passed') is True and
                      data.get('uid', 0) >= 10000)
except (subprocess.SubprocessError, ValueError) as error:
    meta['passed'] = False
    meta['error'] = str(error)
(out / 'run.json').write_text(json.dumps(meta, indent=2) + '\n')
print(json.dumps(meta, indent=2))
print(out)
raise SystemExit(0 if meta['passed'] else 1)
