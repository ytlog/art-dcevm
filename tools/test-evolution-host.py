#!/usr/bin/env python3
"""Run the platform-independent DEX/schema migration code with ASan and UBSan."""
import argparse
import json
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--cxx', default=os.environ.get('CXX', 'clang++'))
p.add_argument('--assets', type=Path, default=root / 'app/build/generated/patchAssets')
args = p.parse_args()
out = root / 'build/host-evolution'
out.mkdir(parents=True, exist_ok=True)
cpp = root / 'app/src/main/cpp'
command = [args.cxx, '-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
           '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I', str(cpp),
           str(root / 'tests/host/evolution_test.cc'), str(cpp / 'evolution/dex_schema.cc'),
           str(cpp / 'evolution/migration_plan.cc'), '-o', str(out / 'evolution_test')]
subprocess.run(command, check=True)
result = subprocess.run([str(out / 'evolution_test'), str(args.assets)], capture_output=True, text=True,
                        env=dict(os.environ, UBSAN_OPTIONS='halt_on_error=1', ASAN_OPTIONS='halt_on_error=1'))
(out / 'stdout.json').write_text(result.stdout)
(out / 'stderr.txt').write_text(result.stderr)
if result.returncode:
    print(result.stdout + result.stderr)
    raise SystemExit(result.returncode)
report = json.loads(result.stdout)
report['sanitizers'] = ['AddressSanitizer', 'UndefinedBehaviorSanitizer']
report['scope'] = 'host schema parser and migration planner; not Android runtime mutation'
evidence = root / 'build/evidence/host'
evidence.mkdir(parents=True, exist_ok=True)
(evidence / 'evolution-tests.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
print(json.dumps({key: value for key, value in report.items() if key != 'plans'}, ensure_ascii=False))
