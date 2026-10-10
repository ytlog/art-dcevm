#!/usr/bin/env python3
"""Verify portable JVMTI behavior, preserving device identity and failure evidence."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import time
from evidence_paths import new_evidence_directory
from apk_verification import require_install_success,base_apk_path,verified_apk_sha256

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = 'dev.artdcevm.lab'


def validate_mvp_body_report(report):
    expected = {'suite': 'mvp-body-evolution', 'firstCode': 0, 'schemaCode': 98,
                'hierarchyCode': 98, 'recoveryCode': 0, 'evolvedValue': 43,
                'recoveredValue': 48, 'jniValue': 48, 'reflectionValue': 48,
                'retainedFieldValue': 41, 'staticValue': 113, 'checks': 4}
    if report.get('passed') is not True or any(type(report.get(key)) is not type(value)
            or report.get(key) != value for key, value in expected.items()):
        raise RuntimeError('mvp-body-evolution: missing or incorrect actual behavior proof')
    for key in ('capabilities', 'capabilitiesAfter'):
        caps = report.get(key)
        if not isinstance(caps, dict) or caps.get('methodBody') is not True or caps.get('evolutionAvailable') is not False:
            raise RuntimeError('mvp-body-evolution: exact independent capability report is required')
        if caps.get('privateHooksSupported', False) is not False:
            raise RuntimeError('mvp-body-evolution: private owner was initialized')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--apk', default='app/build/outputs/apk/debug/app-debug.apk')
    parser.add_argument('--adb', default=shutil.which('adb'))
    parser.add_argument('--expected-sdk', type=int)
    parser.add_argument('--expected-abi', choices=('arm64-v8a',), default='arm64-v8a')
    parser.add_argument('--expected-page-size', type=int, choices=(4096, 16384))
    parser.add_argument('--skip-install', action='store_true')
    parser.add_argument('--mvp-body', action='store_true',
                        help='Also verify EVOLVE_SCHEMA body routing before private initialization')
    parser.add_argument('--standard', action='store_true',
                        help='Also run both Agent load orders and transformation callbacks')
    parser.add_argument('--boot-timeout', type=int, default=180)
    parser.add_argument('--output', type=Path,
                        help='Evidence directory; defaults to build/evidence/compatibility/<UTC timestamp>')
    args = parser.parse_args()
    apk = (ROOT / args.apk).resolve()
    if not apk.is_file():
        parser.error(f'APK does not exist: {apk}')
    if not args.adb:
        parser.error('adb not found; pass --adb')
    if args.boot_timeout <= 0:
        parser.error('--boot-timeout must be positive')
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    out = args.output
    if out is None:
        out = new_evidence_directory(ROOT / 'build/evidence/compatibility', stamp)
    else:
        out.mkdir(parents=True, exist_ok=False)
    report = {'serial': args.serial, 'apkSha256': hashlib.sha256(apk.read_bytes()).hexdigest(),
              'startedUtc': stamp, 'scope': 'public-jvmti', 'passed': False, 'tests': {}}

    def adb(label, *parts, timeout=180):
        try:
            run = subprocess.run([args.adb, '-s', args.serial, *parts], capture_output=True,
                                 text=True, timeout=timeout)
        except subprocess.TimeoutExpired as error:
            def decode(value):
                return value.decode(errors='replace') if isinstance(value, bytes) else value or ''
            (out / f'{label}.txt').write_text(decode(error.stdout) + decode(error.stderr))
            raise RuntimeError(f'{label} timed out after {timeout}s') from error
        (out / f'{label}.txt').write_text(run.stdout + run.stderr)
        if label=='install':require_install_success(run.returncode,run.stdout,run.stderr)
        if run.returncode:
            raise RuntimeError(f'{label} failed ({run.returncode}): {run.stderr or run.stdout}')
        return run.stdout.strip()

    def instrumentation(name, runner, *options):
        adb(f'{name}-stop', 'shell', 'am', 'force-stop', PACKAGE)
        raw = adb(name, 'shell', 'am', 'instrument', '-w', '-r', *options,
                  f'{PACKAGE}/com.github.ytlog.artdcevm.testing.{runner}')
        stream = re.search(r'^INSTRUMENTATION_RESULT: stream=(.*)$', raw, re.MULTILINE)
        code = re.search(r'^INSTRUMENTATION_CODE: (.*)$', raw, re.MULTILINE)
        if not stream or not code:
            raise RuntimeError(f'{name}: no complete instrumentation report; see {out / (name + ".txt")}')
        result = json.loads(stream.group(1))
        if not isinstance(result, dict):
            raise RuntimeError(f'{name}: instrumentation report is not an object')
        result['instrumentationCode'] = code.group(1).strip()
        report['tests'][name] = result
        if result.get('passed') is not True or result['instrumentationCode'] != '-1':
            raise RuntimeError(f'{name}: {result.get("error", "instrumentation failed")}')
        return result

    try:
        # ADB can accept commands before system_server's package manager is ready.
        adb("wait-for-device", "wait-for-device", timeout=args.boot_timeout)
        deadline = time.monotonic() + args.boot_timeout
        while adb('boot', 'shell', 'getprop', 'sys.boot_completed', timeout=10) != '1':
            if time.monotonic() >= deadline:
                raise RuntimeError('Android boot did not complete')
            time.sleep(1)
        device = {}
        for key, prop in (('sdk', 'ro.build.version.sdk'), ('abi', 'ro.product.cpu.abi'),
                          ('fingerprint', 'ro.build.fingerprint'), ('release', 'ro.build.version.release'),
                          ('sdkFull', 'ro.build.version.sdk_full')):
            device[key] = adb(key, 'shell', 'getprop', prop, timeout=10)
        device['sdk'] = int(device['sdk'])
        device['pageSize'] = int(adb('page-size', 'shell', 'getconf', 'PAGESIZE', timeout=10))
        report['device'] = device
        if args.expected_sdk is not None and device['sdk'] != args.expected_sdk:
            raise RuntimeError(f'Expected SDK {args.expected_sdk}, connected SDK {device["sdk"]}')
        if args.expected_abi is not None and device['abi'] != args.expected_abi:
            raise RuntimeError(f'Expected ABI {args.expected_abi}, connected ABI {device["abi"]}')
        if args.expected_page_size is not None and device['pageSize'] != args.expected_page_size:
            raise RuntimeError(f'Expected page size {args.expected_page_size}, connected page size {device["pageSize"]}')
        if not args.skip_install:
            adb('install', 'install', '-r', str(apk))
        installed_path = adb('installed-path', 'shell', 'pm', 'path', PACKAGE)
        base = base_apk_path(installed_path)
        hash_output = adb('installed-hash', 'shell', 'sha256sum', base)
        report['installedApkSha256'] = verified_apk_sha256(hash_output,report['apkSha256'])
        result = instrumentation('portable', 'PublicJvmtiInstrumentation')
        if any(result.get(key) != device[key] for key in ('sdk', 'abi', 'pageSize')):
            raise RuntimeError('Runtime report does not match the connected device')
        if args.standard:
            for first in (False, True):
                for transform in (False, True):
                    name = ('client-first' if first else 'enhancer-first') + (
                        '-transform' if transform else '')
                    result = instrumentation(name, 'StandardJvmtiInstrumentation',
                        '-e', 'publicOnly', 'true', '-e', 'clientFirst', str(first).lower(),
                        '-e', 'transformCase', str(transform).lower())
                    if result.get('publicOnly') is not True or result.get('clientFirst') != first:
                        raise RuntimeError(f'{name}: incorrect test mode')
        if args.mvp_body:
            mvp = instrumentation('mvp-body-evolution', 'PublicJvmtiInstrumentation', '-e', 'suite', 'mvp-body')
            validate_mvp_body_report(mvp)
            if any(mvp.get(key) != device[key] for key in ('sdk', 'abi', 'pageSize')):
                raise RuntimeError('MVP runtime report does not match the connected device')
        report['passed'] = True
    except (RuntimeError, ValueError, OSError) as error:
        report['error'] = str(error)
    finally:
        (out / 'result.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(report, ensure_ascii=False, indent=2))
    print(f'Evidence: {out}')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
