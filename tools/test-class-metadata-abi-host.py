#!/usr/bin/env python3
"""Verify versioned Class identity fields and concurrent access flags."""
import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sanitizer', choices=('address,undefined', 'thread'), default='address,undefined')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
out = root / 'build/evidence/host/class-metadata-abi'
out.mkdir(parents=True, exist_ok=True)
binary = out / ('class_metadata_abi_test' if args.sanitizer != 'thread' else 'tsan-test')
command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1',
           '-Wall', '-Wextra', '-Werror', '-fsanitize=' + args.sanitizer,
           '-fno-omit-frame-pointer', '-pthread', '-I', str(cpp)]
command += [str(root / 'tests/host/class_metadata_abi_test.cc'),
            '-o', str(binary)]
subprocess.run(command, check=True)
result = subprocess.run([str(binary)], capture_output=True, text=True,
                        env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1',
                                 UBSAN_OPTIONS='halt_on_error=1', TSAN_OPTIONS='halt_on_error=1'))
(out / ('tsan-result.txt' if args.sanitizer == 'thread' else 'result.txt')).write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('Class metadata: passed (' + ('TSan' if args.sanitizer == 'thread' else 'ASan/UBSan') + ')')
