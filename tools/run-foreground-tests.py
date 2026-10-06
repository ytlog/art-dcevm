#!/usr/bin/env python3
"""Run conformance with the real Activity/Choreographer alive in the same process."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import re
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--serial', required=True)
parser.add_argument('--stress', choices=['stock', 'source'])
parser.add_argument('--rounds', type=int, default=48)
parser.add_argument('--callbacks-only', action='store_true', help='Diagnostic only: does not prove continuous foreground drawing')
parser.add_argument('--virtual-display', action='store_true', help='Use an app-owned virtual display; does not establish physical-screen foreground stability')
parser.add_argument('--startup-race', action='store_true', help='Start the first commit without waiting for the first draw')
parser.add_argument('--no-deopt-tracing', action='store_true', help='Diagnostic: keep the deopt hook installed but disable its shadow-frame trace reads')
args = parser.parse_args()
if not 1 <= args.rounds <= 2000:
    parser.error('--rounds must be between 1 and 2000')
if args.virtual_display and (not args.stress or args.callbacks_only):
    parser.error('--virtual-display requires --stress and cannot use --callbacks-only')
if args.startup_race and (not args.stress or args.callbacks_only):
    parser.error('--startup-race requires drawing stress')
if args.no_deopt_tracing and args.stress != 'source':
    parser.error('--no-deopt-tracing requires --stress source')
report_name = 'foreground-stress.json' if args.stress else 'conformance.json'
sdk = next(line.split('=', 1)[1] for line in (ROOT / 'local.properties').read_text().splitlines() if line.startswith('sdk.dir='))
adb = str(Path(sdk) / 'platform-tools/adb')
out = ROOT / 'evidence/ui' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
out.mkdir(parents=True)
meta = {'serial': args.serial, 'passed': False, 'fullDcevmComplete': False,
        'localApkSha256': hashlib.sha256((ROOT / 'app/build/outputs/apk/debug/app-debug.apk').read_bytes()).hexdigest(),
        'startupRace': args.startup_race, 'virtualDisplay': args.virtual_display,
        'deoptTracing': not args.no_deopt_tracing,
        'scope': 'activity launch conformance' if not args.stress else 'callback-only diagnostic' if args.callbacks_only else 'virtual display drawing conformance' if args.virtual_display else 'foreground drawing conformance'}

def command(*parts):
    return subprocess.run([adb, '-s', args.serial, *parts], capture_output=True, text=True, timeout=30)

try:
    packages = command('shell', 'pm', 'path', 'dev.artdcevm.lab')
    packages.check_returncode()
    apk_path = next(line.removeprefix('package:') for line in packages.stdout.splitlines() if line.endswith('/base.apk'))
    installed = command('shell', 'sha256sum', apk_path)
    installed.check_returncode()
    meta['installedApkSha256'] = installed.stdout.split()[0]
    if meta['installedApkSha256'] != meta['localApkSha256']:
        raise ValueError('Installed APK differs from local build; install the current APK before testing')
    command('shell', 'am', 'force-stop', 'dev.artdcevm.lab').check_returncode()
    command('shell', 'run-as', 'dev.artdcevm.lab', 'rm', '-f', 'files/' + report_name).check_returncode()
    activity = 'dev.artdcevm.testing.ForegroundStressActivity' if args.stress else 'dev.artdcevm.MainActivity'
    extras = ['--ez', 'source', str(args.stress == 'source').lower(), '--ei', 'rounds', str(args.rounds), '--ez', 'callbacksOnly', str(args.callbacks_only).lower()] if args.stress else []
    if args.virtual_display:
        extras += ['--ez', 'virtualDisplay', 'true']
    if args.startup_race:
        extras += ['--ez', 'startupRace', 'true']
    if args.no_deopt_tracing:
        extras += ['--ez', 'deoptTracing', 'false']
    launch = command('shell', 'am', 'start', '-W', '-n', 'dev.artdcevm.lab/' + activity, *extras)
    meta['stress'] = args.stress
    (out / 'launch.txt').write_text(launch.stdout + launch.stderr)
    launch.check_returncode()
    deadline = time.monotonic() + (max(90, args.rounds * 0.5 + 30) if args.stress else 30)
    completed_at = None
    data = {}
    while time.monotonic() < deadline:
        pid = command('shell', 'pidof', 'dev.artdcevm.lab').stdout.strip()
        if not pid:
            meta['error'] = 'Activity process exited before the foreground stability check completed'
            break
        meta['pid'] = pid
        result = command('exec-out', 'run-as', 'dev.artdcevm.lab', 'cat', 'files/' + report_name)
        try:
            data = json.loads(result.stdout)
            (out / report_name).write_text(result.stdout)
        except ValueError:
            pass  # The app checkpoints with a short truncate/write window.
        if data.get('completed'):
            if (args.stress and data.get('passed') is not True) or (not args.stress and data.get('failures') != 0):
                meta['error'] = 'Foreground conformance reported failures'
                break
            if completed_at is None:
                completed_at = time.monotonic()
            if time.monotonic() - completed_at >= 5:
                meta['passed'] = True
                meta['checks'] = len(data.get('checks', []))
                break
        time.sleep(1)
    if not meta['passed'] and 'error' not in meta:
        meta['error'] = 'Foreground conformance did not complete before the deadline'
    result = command('exec-out', 'run-as', 'dev.artdcevm.lab', 'cat', 'files/' + report_name)
    if result.stdout:
        (out / report_name).write_text(result.stdout)
        try:
            final_report = json.loads(result.stdout)
            for key in ('frames', 'draws', 'renderedBuffers', 'everFocused', 'windowFocused', 'focusLosses'):
                if key in final_report:
                    meta[key] = final_report[key]
            if final_report.get('fatal'):
                meta['reportFatal'] = final_report['fatal']
        except ValueError:
            pass
    if 'pid' in meta:
        logs = command('logcat', '-d', '-b', 'crash', '-t', '2500')
        # Tombstone lines belong to debuggerd's PID, not the app's PID.
        blocks = re.split(r'(?=^.*F DEBUG\s*: \*{3}.*$)', logs.stdout, flags=re.MULTILINE)
        relevant = [block for block in blocks if 'pid: ' + meta['pid'] + ',' in block]
        signal = command('logcat', '-d', '-b', 'crash', '--pid=' + meta['pid'])
        (out / 'crash.txt').write_text(signal.stdout + ''.join(relevant) + logs.stderr)
        diagnostics = command('logcat', '-d', '--pid=' + meta['pid'], '-s', 'ArtDcevmDeopt:I')
        (out / 'deopt.txt').write_text(diagnostics.stdout)
    elif meta.get('error') == 'Activity process exited before the foreground stability check completed':
        # A launch-time crash can precede the first pidof poll. Preserve the
        # unfiltered buffer so debuggerd's PID and the app PID remain visible.
        logs = command('logcat', '-d', '-b', 'crash', '-t', '2500')
        (out / 'crash.txt').write_text(logs.stdout + logs.stderr)
except (subprocess.SubprocessError, ValueError, StopIteration, IndexError) as error:
    meta['error'] = str(error)
(out / 'run.json').write_text(json.dumps(meta, indent=2) + '\n')
print(json.dumps(meta, indent=2))
print(out)
raise SystemExit(0 if meta['passed'] else 1)
