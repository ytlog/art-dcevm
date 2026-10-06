#!/usr/bin/env python3
"""Exercise the standard JVMTI API through an independent Agent in both load orders."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--serial', required=True)
args = parser.parse_args()
sdk = next(line.split('=', 1)[1] for line in (root / 'local.properties').read_text().splitlines()
           if line.startswith('sdk.dir='))
adb = str(Path(sdk) / 'platform-tools/adb')
apk = root / 'app/build/outputs/apk/debug/app-debug.apk'
output = root / 'evidence/standard-jvmti' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
output.mkdir(parents=True)

def command(*parts):
    return subprocess.run([adb, '-s', args.serial, *parts], capture_output=True, text=True, timeout=180)

install = command('install', '-r', str(apk))
(output / 'install.txt').write_text(install.stdout + install.stderr)
install.check_returncode()
result = {'apkSha256': hashlib.sha256(apk.read_bytes()).hexdigest(), 'serial': args.serial}
for first in (False, True):
    command('shell', 'am', 'force-stop', 'dev.artdcevm.lab').check_returncode()
    run = command('shell', 'am', 'instrument', '-w', '-r', '-e', 'clientFirst', str(first).lower(),
                  'dev.artdcevm.lab/dev.artdcevm.testing.StandardJvmtiInstrumentation')
    (output / ('client-first.txt' if first else 'enhancer-first.txt')).write_text(run.stdout + run.stderr)
    result['clientFirst' if first else 'enhancerFirst'] = (
        run.returncode == 0 and '"passed":true' in run.stdout and
        ('"clientFirst":true' if first else '"clientFirst":false') in run.stdout and
        'INSTRUMENTATION_CODE: -1' in run.stdout)
(output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
print(output)
raise SystemExit(0 if result['clientFirst'] and result['enhancerFirst'] else 1)
