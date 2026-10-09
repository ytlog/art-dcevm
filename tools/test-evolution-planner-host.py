#!/usr/bin/env python3
"""Exercise the common batch planner, DEX parser and public schema snapshot."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--assets', type=Path, default=root / 'app/build/generated/patchAssets')
a = p.parse_args()
jdk = Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'], text=True).strip())
cpp = root / 'artdcevm/src/main/cpp'
out = root / 'build/evidence/host/evolution-planner'
out.mkdir(parents=True, exist_ok=True)
command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
           '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I', str(cpp), '-I', str(jdk / 'include'),
           '-I', str(jdk / 'include' / ('darwin' if sys.platform == 'darwin' else 'linux')),
           str(root / 'tests/host/evolution_planner_test.cc'), str(cpp / 'platform/common/evolution_planner.cc'),
           str(cpp / 'platform/common/schema_snapshot.cc'), str(cpp / 'evolution/dex_schema.cc'),
           str(cpp / 'evolution/migration_plan.cc'), '-o', str(out / 'test')]
subprocess.run(command, check=True)
result = subprocess.run([str(out / 'test'), str(a.assets)], text=True, capture_output=True,
                        env=dict(os.environ, UBSAN_OPTIONS='halt_on_error=1', ASAN_OPTIONS='halt_on_error=1'))
(out / 'result.log').write_text(result.stdout + result.stderr)
print(result.stdout + result.stderr, end='')
raise SystemExit(result.returncode)
