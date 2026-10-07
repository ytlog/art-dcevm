#!/usr/bin/env python3
"""Inject parser allocation failures into a generated, valid patch DEX."""
import argparse
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default=os.environ.get('CXX', 'clang++'))
parser.add_argument('--dex', type=Path, default=root / 'app/build/generated/patchAssets/method-signature/com/github/ytlog/artdcevm/fixture/Evolving.dex')
args = parser.parse_args()
if not args.dex.is_file():
    parser.error(f'generated patch fixture missing: {args.dex}; generate patchAssets first')
cpp = root / 'artdcevm/src/main/cpp'
out = root / 'build/evidence/host/dex-schema-oom'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'dex_schema_oom_test'
command = [args.cxx, '-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
           '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I', str(cpp),
           str(root / 'tests/host/dex_schema_oom_test.cc'),
           str(cpp / 'evolution/dex_schema.cc'), '-o', str(binary)]
subprocess.run(command, check=True)
result = subprocess.run([str(binary), str(args.dex)], capture_output=True, text=True,
                        env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1',
                                 UBSAN_OPTIONS='halt_on_error=1'))
(out / 'parser-oom-test.log').write_text(result.stdout + result.stderr)
if result.returncode:
    print(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('DEX parser OOM: passed (ASan/UBSan)')
