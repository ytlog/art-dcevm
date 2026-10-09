#!/usr/bin/env python3
"""Run strict formal and owned-runtime suites sequentially per device, in parallel across devices."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import math
import os
from pathlib import Path
import signal
import subprocess
import sys
from evidence_paths import new_evidence_directory

SDKS = {'11': {'30'}, '12': {'31', '32'}, '13': {'33'}, '14': {'34'},
        '15': {'35'}, '16': {'36'}, '17': {'37', '37.1'}}
FLAGS = ['--runtime-services', '--exclusive-roots', '--native-cache',
         '--jit-receivers', '--method-guards', '--field-storage']

def device(value):
    version, sep, serial = value.partition('=')
    if not sep or version not in SDKS or not serial.strip():
        raise argparse.ArgumentTypeError('Expected VERSION=SERIAL with VERSION 11 through 17')
    return version, serial.strip()

def invoke(command, directory, label, timeout):
    stdout_path = directory / (label + '.stdout.log')
    stderr_path = directory / (label + '.stderr.log')
    result = {'command': command, 'stdout': str(stdout_path), 'stderr': str(stderr_path)}
    try:
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   text=True, start_new_session=True)
        try:
            stdout, stderr = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            # Kill this entire host runner/ADB group before the next suite starts.
            try: os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError: pass
            stdout, stderr = process.communicate()
            result['error'] = 'Suite command timed out after %s seconds' % timeout
            result['timedOut'] = True
        result['exitCode'] = process.returncode
    except OSError as error:
        stdout, stderr = '', str(error)
        result['exitCode'] = None
        result['error'] = str(error)
    stdout_path.write_text(stdout)
    stderr_path.write_text(stderr)
    (directory / (label + '.command.json')).write_text(json.dumps(result, indent=2) + '\n')
    return result, stdout

def suite(command, directory, label, timeout, root):
    result, stdout = invoke(command, directory, label, timeout)
    result['passed'] = False
    try:
        lines = stdout.strip().splitlines()
        if not lines: raise ValueError('Runner did not publish an evidence path')
        evidence = Path(lines[-1])
        if not evidence.is_absolute(): evidence = root / evidence
        evidence = evidence.resolve()
        evidence.relative_to((root / 'build/evidence').resolve())
        report_path = evidence / ('report.json' if label == 'production' else 'run.json')
        report = json.loads(report_path.read_text())
        result['evidencePath'] = str(evidence)
        result['reportPath'] = str(report_path)
        result['report'] = report
        result['passed'] = result['exitCode'] == 0 and report.get('passed') is True and not result.get('timedOut')
    except (OSError, ValueError, IndexError) as error:
        result['evidenceError'] = str(error)
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', type=device, action='append', required=True, metavar='VERSION=SERIAL')
    parser.add_argument('--apk', type=Path, required=True)
    parser.add_argument('--test-apk', type=Path, required=True)
    parser.add_argument('--adb', required=True)
    parser.add_argument('--foreground-version', choices=SDKS, action='append',
                        help='Foreground carrier versions; defaults to 12 when omitted')
    parser.add_argument('--active-frames',action='store_true',help='Require independent owned active-frame proof in each runtime suite')
    parser.add_argument('--max-workers', type=int, default=7)
    parser.add_argument('--suite-timeout', type=float, default=900,
                        help='Outer host runner deadline per suite (seconds); original runner limits remain unchanged')
    args = parser.parse_args()
    if not 1 <= args.max_workers <= 7: parser.error('--max-workers must be 1 through 7')
    if not math.isfinite(args.suite_timeout) or args.suite_timeout <= 0: parser.error('--suite-timeout must be positive')
    if len({serial for _, serial in args.device}) != len(args.device):
        parser.error('Each serial may appear only once; a device cannot run concurrently with itself')
    root = Path(__file__).resolve().parents[1]
    args.apk = args.apk.resolve(); args.test_apk = args.test_apk.resolve()
    if Path(args.adb).exists(): args.adb = str(Path(args.adb).resolve())
    try:
        artifacts = {name: {'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
                     for name, path in [('application', args.apk), ('instrumentation', args.test_apk)]}
    except OSError as error: parser.error(str(error))
    foreground = set(args.foreground_version or ['12'])
    out = new_evidence_directory(root / 'build/evidence/device-compatibility-matrix')
    summary = {'passed': False, 'artifacts': artifacts, 'foregroundVersions': sorted(foreground),
               'activeFramesRequested': args.active_frames, 'devices': [], 'scope': 'strict formal and owned method/field removal; full evolution is not established'}

    if args.active_frames:
        summary['scope'] += '; requires independent owned compiled carrier/two-inlinee proof'

    def run(index, version, serial):
        directory = out / ('%02d-android-%s' % (index, version)); directory.mkdir()
        result = {'version': version, 'serial': serial, 'foreground': version in foreground,
                  'passed': False, 'evidencePath': str(directory)}
        sdk, text = invoke([args.adb, '-s', serial, 'shell', 'getprop', 'ro.build.version.sdk'], directory, 'sdk', 20)
        actual = text.strip(); result['sdk'] = {**sdk, 'actual': actual, 'expected': sorted(SDKS[version])}
        # VERSION is a reporting label only, never a runtime capability selector.
        if sdk['exitCode'] != 0 or actual not in SDKS[version]:
            result['error'] = 'Requested Android %s label disagrees with verified SDK %r' % (version, actual)
            result['production'] = {'skipped': True}; result['runtime'] = {'skipped': True}
        else:
            common = ['--serial', serial, '--adb', args.adb, '--apk', str(args.apk)]
            production = [sys.executable, str(root / 'tools/run-production-bootstrap.py'), *common,
                          '--test-apk', str(args.test_apk), '--require-fields']
            runtime = [sys.executable, str(root / 'tools/run-runtime-access.py'), *common,
                       '--skip-install', *FLAGS]
            if args.active_frames:runtime.append('--active-frames')
            if version in foreground:
                production.append('--foreground');runtime.append('--foreground')
            # A formal refusal is still followed by an independently recorded runtime suite.
            for label, command in [('production', production), ('runtime', runtime)]:
                result[label] = suite(command, directory, label, args.suite_timeout, root)
                if result[label].get('timedOut'):
                    cleanup, _ = invoke([args.adb, '-s', serial, 'shell', 'am', 'force-stop', 'dev.artdcevm.lab'],
                                        directory, label + '-timeout-cleanup', 15)
                    result[label]['timeoutCleanup'] = cleanup
            result['passed'] = result['production']['passed'] and result['runtime']['passed']
        (directory / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
        return index, result

    previous = Path.cwd()
    try:
        os.chdir(root)  # Both original runners retain their own strict evidence behavior.
        with ThreadPoolExecutor(max_workers=min(args.max_workers, len(args.device))) as pool:
            futures = {pool.submit(run, n, version, serial): (n, version, serial)
                       for n, (version, serial) in enumerate(args.device)}
            ordered = {}
            for future in as_completed(futures):
                index, version, serial = futures[future]
                try: index, result = future.result()
                except Exception as error:
                    result = {'version': version, 'serial': serial, 'passed': False, 'error': str(error)}
                ordered[index] = result
                print('Android %s %s: %s' % (version, serial, 'passed' if result['passed'] else 'failed'), flush=True)
            summary['devices'] = [ordered[n] for n in range(len(args.device))]
    finally: os.chdir(previous)
    summary['passed'] = all(result['passed'] for result in summary['devices'])
    (out / 'matrix.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2)); print(out)
    return 0 if summary['passed'] else 1

if __name__ == '__main__': sys.exit(main())
