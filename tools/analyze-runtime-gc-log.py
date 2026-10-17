#!/usr/bin/env python3
"""Count GC completion provenance in existing engine logcat, without a device.

Snapshot tags identify engine processes and transaction threads. Completion
threads and log order do not establish a collector's marking interval.
"""
import argparse
from bisect import bisect_right
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import sys

from evidence_paths import new_evidence_directory

ROOT = Path(__file__).resolve().parents[1]
LINE = re.compile(r'^\d\d-\d\d\s+\d\d:\d\d:\d\d\.\d+\s+'
                  r'(\d+)\s+(\d+)\s+[VDIWEF]\s+([^:]+):\s*(.*)$')
GC = re.compile(r'^(.+?)\s+((?:young\s+)?concurrent\s+(?:copying|mark compact))\s+GC freed\b')
SNAPSHOT = re.compile(r'^targets=\d+ loaded=\d+ nanos=\d+$')
CAUSES = {'Alloc', 'Background', 'Explicit', 'NativeAlloc', 'CollectorTransition', 'DisableMovingGc'}


def analyze(text):
    events = []
    processes = {}
    for position, line in enumerate(text.splitlines()):
        match = LINE.match(line)
        if not match:
            continue
        pid, tid, tag, message = match.groups()
        events.append((position, pid, tid, message))
        if tag.strip() == 'ART-DCEVM-SNAPSHOT' and SNAPSHOT.fullmatch(message):
            process = processes.setdefault(pid, {'snapshots': [], 'threads': set()})
            process['snapshots'].append(position)
            process['threads'].add(tid)
    if not processes:
        raise ValueError('No engine snapshots; GC process identity is unproven')
    rows = []
    for number, (pid, process) in enumerate(processes.items(), 1):
        snapshots = process['snapshots']
        causes, collectors = Counter(), Counter()
        on_thread = before = skipped = 0
        explicit_windows = [0] * len(snapshots)
        for position, event_pid, tid, message in events:
            if event_pid == pid and message == 'Explicit GC skipped.':
                skipped += 1
            match = GC.match(message)
            if event_pid != pid or not match:
                continue
            cause, collector = match.groups()
            cause = cause if cause in CAUSES else 'Other'
            causes[cause] += 1
            collectors[collector] += 1
            on_thread += tid in process['threads']
            window = bisect_right(snapshots, position) - 1
            if window < 0:
                before += 1
            elif cause == 'Explicit':
                explicit_windows[window] += 1
        count = sum(causes.values())
        rows.append({
            'processId': f'engine-{number:02d}',
            'snapshots': len(snapshots), 'snapshotThreads': len(process['threads']),
            'recognizedGcCompletions': count,
            'gcCauses': dict(causes), 'collectors': dict(collectors),
            'gcCompletionsOnSnapshotThreads': on_thread,
            'gcCompletionsOnOtherThreads': count - on_thread,
            'gcCompletionsBeforeFirstSnapshot': before,
            'explicitGcSkippedMessages': skipped,
            'explicitGcPerSnapshotWindow': dict(sorted(Counter(explicit_windows).items()))})
    return {'schemaVersion': 1, 'engineProcesses': rows,
            'scope': 'CC/CMC completion logs and file order only; no marking/forwarding proof',
            'snapshotWindows': 'After each snapshot until the next, including the final log tail',
            'markingOverlapEstablished': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--logcat', type=Path, required=True)
    parser.add_argument('--device-id', required=True, help='Anonymous device label')
    parser.add_argument('--evidence-root', type=Path, default=ROOT / 'build/evidence/runtime-gc-log')
    args = parser.parse_args()
    if not re.fullmatch(r'[a-z][a-z0-9-]{0,63}', args.device_id):
        parser.error('Use an anonymous lowercase device label')
    root = args.evidence_root.resolve()
    if not root.is_relative_to(ROOT / 'build/evidence'):
        parser.error('Evidence must stay under ignored build/evidence')
    try:
        data = args.logcat.read_bytes()
        report = analyze(data.decode('utf-8', errors='replace'))
        report.update(deviceId=args.device_id, logcatSha256=hashlib.sha256(data).hexdigest(),
                      analyzerSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
        out = new_evidence_directory(root)
        (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    except (OSError, ValueError):
        print('Unable to analyze GC logs; require a readable log with engine snapshots', file=sys.stderr)
        return 1
    print(json.dumps(report, indent=2))
    print('Evidence: ' + str(out.relative_to(ROOT)))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
