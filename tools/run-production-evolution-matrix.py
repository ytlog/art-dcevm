#!/usr/bin/env python3
"""Freeze artifacts and run required evolution cases sequentially per device, in parallel.

No installation by default. This matrix establishes only the selected cases and
initialization scopes; it does not claim compatibility with every Android device.
"""
import argparse
import concurrent.futures
import datetime
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import sys
import time
import uuid

from production_foreground_report import validate_foreground
from production_schema_report import CASE_METHODS as SCHEMA_CASES
from production_permission_report import CASE_METHODS as PERMISSION_CASES

ROOT = pathlib.Path(__file__).resolve().parents[1]
SUPPORTED_APIS = {30, 31, 33, 34, 35, 36, 37}
USB_SELF_ONLY = 'DEVICE_001'


def digest(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def device(value):
    try:
        api, serial = value.split('=', 1)
        api = int(api)
    except ValueError as error:
        raise argparse.ArgumentTypeError('Expected API=serial') from error
    if api not in SUPPORTED_APIS or not serial or any(c.isspace() for c in serial):
        raise argparse.ArgumentTypeError('Supported API 30/31/33/34/35/36/37 and nonempty serial required')
    return api, serial


def selection(selected, available):
    cases = list(available) if selected is None else list(selected)
    if not cases or len(cases) != len(set(cases)) or any(c not in available for c in cases):
        raise ValueError('Missing, duplicate or unknown required case')
    return cases


def freeze(apk, test_apk, output, devices, schema, permissions):
    output.mkdir(parents=True, exist_ok=False)
    artifacts = {}
    for key, source in [('app', apk), ('test', test_apk)]:
        if source is None:
            continue
        source = pathlib.Path(source).resolve(strict=True)
        before = digest(source)
        target = output / (key + '.apk')
        shutil.copyfile(source, target)
        if digest(source) != before or digest(target) != before:
            raise ValueError('Artifact changed while freezing: ' + key)
        target.chmod(0o444)
        artifacts[key] = {'path': str(target.resolve()), 'source': str(source), 'sha256': before}
    manifest = {'matrixId': output.name, 'startedUtc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
                'devices': [{'api': api, 'serial': serial} for api, serial in devices],
                'required': {'schema': schema, 'permissions': permissions}, 'artifacts': artifacts}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest


def validate_device(adb, serial, api, execute=subprocess.run):
    properties = {}
    for name in ('ro.build.version.sdk', 'ro.product.cpu.abilist'):
        result = execute([adb, '-s', serial, 'shell', 'getprop', name], capture_output=True, text=True,
                         check=True, timeout=20)
        properties[name] = result.stdout.strip()
    if properties['ro.build.version.sdk'] != str(api):
        raise ValueError('Connected SDK does not match requested API')
    # ARM64 execution does not require the phone to remove its optional 32-bit
    # ABI. The frozen application contains ARM64 code; accept a native ARM64
    # primary ABI even when the same phone can also run unrelated 32-bit apps.
    if properties['ro.product.cpu.abilist'].split(',')[0] != 'arm64-v8a':
        raise ValueError('Connected device must advertise primary arm64-v8a')
    return properties


class EvidenceError(ValueError):
    def __init__(self, message, record):
        super().__init__(message)
        self.record = record


def accepted_evidence(stdout, family, started_ns, seen, serial, artifacts, case=None, scope=None, self_route=False,
                      root=ROOT, require_foreground=False):
    """Only the fresh directory emitted by this child can establish success."""
    lines = stdout.splitlines()
    if not lines:
        raise ValueError('Child emitted no evidence directory')
    raw = pathlib.Path(lines[-1])
    if not raw.is_absolute():
        raise ValueError('Evidence path must be absolute')
    directory = raw.resolve(strict=True)
    allowed = (root / 'build/evidence' / family).resolve()
    if directory.parent != allowed or raw != directory:
        raise ValueError('Invalid or escaped evidence path')
    filename = 'report.json' if family == 'production-bootstrap' else 'result.json'
    path = directory / filename
    if path.is_symlink() or path.resolve(strict=True).parent != directory or path.stat().st_mtime_ns < started_ns:
        raise ValueError('Missing, escaped or stale current evidence')
    result = json.loads(path.read_text())
    if not isinstance(result, dict):
        raise ValueError('Result must be an object')
    run_id = result.get('runId')
    if not isinstance(run_id, str) or not re.fullmatch('[0-9a-f]{32}', run_id) or run_id in seen:
        raise ValueError('Missing or duplicate current run identity')
    if family != 'production-bootstrap' and directory.name != run_id:
        raise ValueError('Run identity differs from evidence directory')
    seen.add(run_id)
    current = {'runId': run_id, 'evidence': str(directory), 'result': result}
    expected = [('serial', serial)]
    if family == 'production-bootstrap':
        expected += [('applicationSha256', artifacts['app']['sha256']),
                     ('testSha256', artifacts['test']['sha256'])]
    else:
        expected += [('case', case), ('scope', scope), ('runner', 'self' if self_route else 'junit'),
                     ('appSha256', artifacts['app']['sha256'])]
        if not self_route:
            expected.append(('testSha256', artifacts['test']['sha256']))
    for key, value in expected:
        if result.get(key) != value:
            raise EvidenceError('Current result identity/hash mismatch: ' + key, current)
    if result.get('passed') is not True:
        raise EvidenceError('Individual runner failed', current)
    if family != 'production-bootstrap' and result.get('reportFileCaptured') is not True:
        raise EvidenceError('Missing current persisted report', current)
    if require_foreground:
        if result.get('foregroundRequested') is not True:
            raise EvidenceError('Child did not request foreground', current)
        payload = result.get('transaction') if family == 'production-bootstrap' else None
        if family != 'production-bootstrap':
            proof_path = directory / 'report-file.json'
            if not proof_path.is_file() or proof_path.is_symlink() or proof_path.resolve(strict=True).parent != directory or proof_path.stat().st_mtime_ns < started_ns:
                raise EvidenceError('Missing fresh foreground report', current)
            payload = json.loads(proof_path.read_text())
        if not isinstance(payload, dict) or payload.get('runId') != run_id:
            raise EvidenceError('Foreground report run identity mismatch', current)
        errors = validate_foreground(payload, required=True)
        if errors:
            raise EvidenceError('; '.join(errors), current)
    return {'runId': run_id, 'evidence': str(directory), 'result': result}


def complete(required, completed, errors):
    # Never let all([]) turn a bootstrap-only or interrupted run into success.
    expected = {(family, case) for family, cases in required.items() for case in cases}
    actual = [(entry['family'], entry['case']) for entry in completed]
    return bool(expected) and not errors and len(actual) == len(set(actual)) and set(actual) == expected and all(
        entry.get('passed') is True for entry in completed)


def run_child(command, family, output, label, seen, serial, artifacts, case=None, scope=None, self_route=False, require_foreground=False):
    started = time.time_ns()
    # The individual runner owns its timeout and current-run diagnostics. No outer
    # timeout terminates/restarts its live device workflow or retries installation.
    child = subprocess.run(command, capture_output=True, text=True)
    (output / (label + '.log')).write_text(child.stdout + child.stderr)
    # Parse evidence even for nonzero exit, retaining a current failure diagnostic.
    record = {'exitCode': child.returncode, 'log': str(output / (label + '.log')), 'command': command, 'passed': False}
    try:
        record.update(accepted_evidence(child.stdout, family, started, seen, serial, artifacts,
                                       case, scope, self_route, require_foreground=require_foreground))
        if child.returncode != 0:
            raise ValueError('Individual runner exited nonzero')
        record['passed'] = True
    except Exception as error:
        if isinstance(error, EvidenceError):
            record.update(error.record)
        record['error'] = str(error)
        # Failed results remain diagnosis, never a successful completed case.
    return record


def run_device(api, serial, args, manifest, output):
    directory = output / ('api' + str(api) + '-' + hashlib.sha256(serial.encode()).hexdigest()[:12])
    directory.mkdir()
    artifacts = manifest['artifacts']
    self_route = serial in args.self_instrumentation_serial
    scope = args.scope or ('full' if api == 33 else args.other_scope)
    required = manifest['required']
    report = {'api': api, 'serial': serial, 'scope': scope, 'required': required,
              'completed': [], 'errors': [], 'passed': False, 'artifacts': artifacts,
              'foregroundRequested': getattr(args, 'foreground', False)}
    seen = set()
    try:
        report['properties'] = validate_device(args.adb, serial, api)
        base = ['--adb', args.adb, '--serial', serial, '--apk', artifacts['app']['path']]
        foreground = getattr(args, 'foreground', False)
        if foreground:
            base.append('--foreground')
        if args.install and not self_route:
            bootstrap = run_child([sys.executable, str(ROOT / 'tools/run-production-bootstrap.py'),
                                   *base, '--test-apk', artifacts['test']['path'], '--require-fields'],
                                  'production-bootstrap', directory, 'bootstrap', seen, serial, artifacts, require_foreground=foreground)
            report['bootstrap'] = bootstrap
            if not bootstrap['passed']:
                raise ValueError('Required installation/bootstrap failed')
        for family, runner, evidence in [('schema', 'run-production-schema.py', 'production-schema'),
                                          ('permissions', 'run-production-permissions.py', 'production-permissions')]:
            for case in required[family]:
                route = ['--self-instrumentation'] if self_route else ['--test-apk', artifacts['test']['path']]
                record = run_child([sys.executable, str(ROOT / 'tools' / runner), *base, *route,
                                    '--scope', scope, '--case', case, '--timeout', str(args.timeout)],
                                   evidence, directory, family + '-' + case, seen, serial, artifacts,
                                   case, scope, self_route, require_foreground=foreground)
                report['completed'].append({'family': family, 'case': case, **record})
                if not record['passed']:
                    report['errors'].append(family + '/' + case + ': ' + record.get('error', 'failed'))
                    # Timeout is observation failure, not proof the device operation
                    # stopped. Do not start another case that would force-stop it.
                    current = record.get('result', {})
                    if current.get('instrumentationTimedOut') is True or current.get('timeoutDiagnostics') or current.get('installTimeoutDiagnostics'):
                        raise ValueError('Current device workflow timed out; inspect individual diagnostics before another run')
        for artifact in artifacts.values():
            if digest(artifact['path']) != artifact['sha256']:
                raise ValueError('Frozen artifact changed during device workflow')
    except Exception as error:
        report['errors'].append(str(error))
    report['passed'] = complete(required, report['completed'], report['errors'])
    (directory / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--device', type=device, action='append', required=True, metavar='API=serial')
    p.add_argument('--adb', required=True)
    p.add_argument('--apk', required=True)
    p.add_argument('--test-apk')
    p.add_argument('--install', action='store_true', help='Explicitly install both artifacts and require field bootstrap')
    p.add_argument('--self-instrumentation-serial', action='append', default=[])
    p.add_argument('--foreground', action='store_true', help='Keep each formal suite in its actual instrumentation foreground process')
    p.add_argument('--schema-case', choices=SCHEMA_CASES, action='append')
    p.add_argument('--permission-case', choices=PERMISSION_CASES, action='append')
    p.add_argument('--other-scope', choices=('full', 'common-owned'), default='common-owned')
    p.add_argument('--scope', choices=('full', 'common-owned'),
                   help='Override every device scope, including API 33, to exercise the same runtime route')
    p.add_argument('--timeout', type=int, default=300)
    return p


def main():
    p = parser()
    args = p.parse_args()
    try:
        schema = selection(args.schema_case, SCHEMA_CASES)
        permissions = selection(args.permission_case, PERMISSION_CASES)
        serials = [serial for _, serial in args.device]
        if len(serials) != len(set(serials)):
            raise ValueError('Duplicate device serial')
        if len(args.self_instrumentation_serial) != len(set(args.self_instrumentation_serial)) or any(
                serial not in serials for serial in args.self_instrumentation_serial):
            raise ValueError('Self instrumentation serial must identify a unique selected device')
        if USB_SELF_ONLY in serials and USB_SELF_ONLY not in args.self_instrumentation_serial:
            raise ValueError('DEVICE_001 requires --self-instrumentation-serial DEVICE_001')
        if len(args.self_instrumentation_serial) != len(serials) and not args.test_apk:
            raise ValueError('--test-apk required for JUnit/installation')
        if args.timeout <= 0:
            raise ValueError('Positive individual-runner timeout required')
    except ValueError as error:
        p.error(str(error))
    output = ROOT / 'build/evidence/production-evolution-matrix' / uuid.uuid4().hex
    manifest = freeze(args.apk, args.test_apk, output, args.device, schema, permissions)
    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=len(args.device)) as pool:
        futures = {pool.submit(run_device, api, serial, args, manifest, output): (api, serial) for api, serial in args.device}
        for future in concurrent.futures.as_completed(futures):
            api, serial = futures[future]
            try:
                results.append(future.result())
            except Exception as error:
                results.append({'api': api, 'serial': serial, 'required': manifest['required'],
                                'completed': [], 'errors': [str(error)], 'passed': False})
    summary = {'matrixId': manifest['matrixId'], 'manifest': str(output / 'manifest.json'),
               'scope': 'Selected production cases on listed devices and initialization scopes; no claim of complete compatibility',
               'allDefaultCasesSelected': args.schema_case is None and args.permission_case is None,
               'devices': sorted(results, key=lambda result: result['api']),
               'passed': len(results) == len(args.device) and bool(results) and all(r['passed'] is True for r in results)}
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2))
    print(output)
    return 0 if summary['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
