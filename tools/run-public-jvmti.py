#!/usr/bin/env python3
"""Run the portable JVMTI instrumentation on one local/emulated Android device."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument('--serial', required=True)
parser.add_argument('--apk', default='app/build/outputs/apk/debug/app-debug.apk')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
apk = (root / args.apk).resolve()
if not apk.is_file():
    parser.error(f'APK does not exist: {apk}')

def adb(*parts, timeout=180):
    return subprocess.run(['adb', '-s', args.serial, *parts], capture_output=True,
                          text=True, timeout=timeout, check=True)

try:
    adb('install', '-r', str(apk))
    adb('shell', 'am', 'force-stop', 'dev.artdcevm.lab')
    output = adb('shell', 'am', 'instrument', '-w', '-r',
                 'dev.artdcevm.lab/dev.artdcevm.testing.PublicJvmtiInstrumentation').stdout
    match = re.search(r'^INSTRUMENTATION_RESULT: stream=(.*)$', output, re.MULTILINE)
    report = json.loads(match.group(1)) if match else {'passed': False, 'error': output}
    report['instrumentationCode'] = re.search(r'^INSTRUMENTATION_CODE: (.*)$', output, re.MULTILINE).group(1)
    report['apkSha256'] = hashlib.sha256(apk.read_bytes()).hexdigest()
    print(json.dumps(report, ensure_ascii=False, indent=2))
    sys.exit(0 if report.get('passed') is True and report['instrumentationCode'] == '-1' else 1)
except (subprocess.CalledProcessError, subprocess.TimeoutExpired, AttributeError) as error:
    detail = error.stderr if isinstance(error, subprocess.CalledProcessError) else str(error)
    print(f'Public JVMTI test failed: {detail}', file=sys.stderr)
    sys.exit(1)
