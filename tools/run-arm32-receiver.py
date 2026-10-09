#!/usr/bin/env python3
"""Verify the installed frozen ARM32 main APK and run its self instrumentation."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import shutil
import subprocess
import uuid
from evidence_paths import new_evidence_directory
from apk_verification import base_apk_path, verified_apk_sha256
from arm32_receiver_report import COMPONENT, SCOPE, parse_instrumentation, strict_json

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--apk', required=True, type=Path, help='Frozen main APK; never uploads/installs')
    parser.add_argument('--expected-apk-sha256', required=True)
    parser.add_argument('--skip-install', required=True, action='store_true')
    parser.add_argument('--adb', default=shutil.which('adb'))
    args = parser.parse_args()
    if not args.adb:
        parser.error('adb not found; pass --adb')
    run_id = uuid.uuid4().hex
    out = new_evidence_directory(Path('build/evidence/arm32-receiver'))
    metadata = dict(runId=run_id, serial=args.serial, component=COMPONENT, scope=SCOPE, passed=False)
    def adb(label, *command, timeout=90):
        try:
            result = subprocess.run([args.adb, '-s', args.serial, *command], capture_output=True, text=True, timeout=timeout)
        except subprocess.TimeoutExpired as error:
            def decoded(value):
                return value.decode(errors='replace') if isinstance(value, bytes) else value or ''
            (out/(label+'.txt')).write_text(decoded(error.stdout)+decoded(error.stderr)+'\n'+str(error))
            raise
        (out/(label+'.txt')).write_text(result.stdout+result.stderr)
        result.check_returncode()
        return result.stdout
    try:
        sha = hashlib.sha256(args.apk.read_bytes()).hexdigest()
        if sha != args.expected_apk_sha256:
            raise ValueError('Frozen APK SHA-256 does not match expected hash')
        metadata['apkSha256'] = sha
        abi32 = adb('abi32', 'shell', 'getprop', 'ro.product.cpu.abilist32').strip()
        if 'armeabi-v7a' not in abi32.split(','):
            raise ValueError('Device does not advertise ARM32 runtime support')
        metadata['deviceAbi32'] = abi32
        installed = base_apk_path(adb('package-path', 'shell', 'pm', 'path', 'dev.artdcevm.lab'))
        metadata['installedApkSha256'] = verified_apk_sha256(adb('installed-hash', 'shell', 'sha256sum', installed), sha)
        adb('stop', 'shell', 'am', 'force-stop', 'dev.artdcevm.lab')
        output = adb('instrumentation', 'shell', 'am', 'instrument', '--abi', 'armeabi-v7a', '-w', '-r',
                     '-e', 'proofRunId', run_id, '-e', 'proofApkSha256', sha, COMPONENT)
        payloads = re.findall(r'^INSTRUMENTATION_RESULT: stream=(.*)$', output, re.M)
        if len(payloads) == 1:
            # Preserve failed native diagnostics too; saving is not validation.
            (out/'probe.json').write_text(json.dumps(strict_json(payloads[0]), indent=2)+'\n')
        report = parse_instrumentation(output, run_id, sha)
        (out/'probe.json').write_text(json.dumps(report, indent=2)+'\n')
        metadata['passed'] = True
    except Exception as error:
        metadata['error'] = str(error)
    metadata['evidenceDirectory'] = str(out.resolve())
    (out/'run.json').write_text(json.dumps(metadata, indent=2)+'\n')
    print(json.dumps(metadata, indent=2))
    return 0 if metadata['passed'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
