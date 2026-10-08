#!/usr/bin/env python3
"""Reproduce equal-timestamp parallel device runs without sharing evidence."""
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path
import json
from evidence_paths import new_evidence_directory


def worker(task):
    base, index = task
    directory = new_evidence_directory(Path(base), 'same-device-run-timestamp')
    (directory / 'probe.json').write_text(json.dumps({'run': index}))
    return str(directory), index


if __name__ == '__main__':
    root = new_evidence_directory(Path('build/evidence/host/evidence-paths'))
    with ProcessPoolExecutor(max_workers=8) as pool:
        outputs = list(pool.map(worker, [(str(root), i) for i in range(64)]))
    assert len({directory for directory, _ in outputs}) == 64
    for directory, index in outputs:
        assert json.loads((Path(directory) / 'probe.json').read_text()) == {'run': index}
    (root / 'result.json').write_text(json.dumps({'parallelProcesses': 8, 'runs': 64,
                                                'identicalTimestamp': True, 'passed': True}))
    print('64 equal-timestamp runs in 8 processes retain distinct directories and reports')
