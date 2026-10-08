#!/usr/bin/env python3
"""Install and test only our debug application on the explicitly selected device."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from apk_verification import require_install_success,base_apk_path,verified_apk_sha256
from evidence_paths import new_evidence_directory

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = 'dev.artdcevm.lab'
p = argparse.ArgumentParser()
p.add_argument('--serial', required=True)
p.add_argument('--adb', default=shutil.which('adb'))
p.add_argument('--skip-install', action='store_true')
args = p.parse_args()
if not args.adb:
    sdk = os.environ.get('ANDROID_SDK_ROOT') or os.environ.get('ANDROID_HOME')
    props = ROOT / 'local.properties'
    if not sdk and props.exists():
        sdk = next((line.split('=', 1)[1] for line in props.read_text().splitlines() if line.startswith('sdk.dir=')), None)
    if sdk:
        args.adb = str(Path(sdk) / 'platform-tools/adb')
if not args.adb:
    p.error('Use --adb or set the SDK path in local.properties')
apk = ROOT / 'app/build/outputs/apk/debug/app-debug.apk'
if not apk.exists():
    p.error('Build :app:assembleDebug first')
timestamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
out = new_evidence_directory(ROOT / 'build/evidence/runs', timestamp)

def adb(*command, timeout=120):
    return subprocess.run([args.adb, '-s', args.serial, *command], capture_output=True, text=True, timeout=timeout)

meta = {'serial': args.serial, 'apkSha256': hashlib.sha256(apk.read_bytes()).hexdigest(), 'startedUtc': timestamp}
try:
    if not args.skip_install:
        installed = adb('install', '-r', str(apk))
        (out / 'install.txt').write_text(installed.stdout + installed.stderr)
        require_install_success(installed.returncode,installed.stdout,installed.stderr)
    paths=adb('shell','pm','path',PACKAGE);paths.check_returncode()
    (out/'package-path.txt').write_text(paths.stdout+paths.stderr)
    installed=adb('shell','sha256sum',base_apk_path(paths.stdout));installed.check_returncode()
    (out/'installed-hash.txt').write_text(installed.stdout+installed.stderr)
    meta['installedApkSha256']=verified_apk_sha256(installed.stdout,meta['apkSha256'])
except (RuntimeError,subprocess.CalledProcessError,subprocess.TimeoutExpired) as error:
    meta['status']='install_failed_or_hash_mismatch';meta['error']=str(error)
    (out/'run.json').write_text(json.dumps(meta,indent=2))
    print(error,file=sys.stderr);print(out);sys.exit(2)
stopped = adb('shell', 'am', 'force-stop', PACKAGE)
if stopped.returncode:
    sys.exit(stopped.returncode)
adb('shell', 'run-as', PACKAGE, 'rm', '-f', 'files/conformance.json')
try:
    test = adb('shell', 'am', 'instrument', '-w', '-r', PACKAGE + '/com.github.ytlog.artdcevm.ConformanceInstrumentation')
    (out / 'instrumentation.txt').write_text(test.stdout + test.stderr)
    meta['instrumentationExitCode'] = test.returncode
except subprocess.TimeoutExpired:
    meta['status'] = 'timeout'
report = adb('exec-out', 'run-as', PACKAGE, 'cat', 'files/conformance.json')
(out / 'conformance.json').write_text(report.stdout)
passed = False
try:
    result = json.loads(report.stdout)
    passed = (meta.get('status') != 'timeout' and meta.get('instrumentationExitCode') == 0 and
              result.get('completed') is True and result.get('failures') == 0 and result.get('uid', 0) >= 10000)
    meta['status'] = 'baseline_checks_passed' if passed else 'failed_or_incomplete'
except ValueError:
    meta['status'] = 'no_valid_report'
(out / 'run.json').write_text(json.dumps(meta, indent=2))
print(json.dumps(meta, ensure_ascii=False, indent=2))
print(out)
sys.exit(0 if passed else 1)
