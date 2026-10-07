#!/usr/bin/env python3
"""Verify the shared checked method resolution policy against source-defined failure ordering."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
out = root / 'build/evidence/host/checked-method-resolution'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'checked_method_resolution_test'
command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1',
           '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
           '-fno-omit-frame-pointer', '-I', str(cpp),
           str(root / 'tests/host/checked_method_resolution_test.cc'),
           str(cpp / 'evolution/checked_method_resolution.cc'), '-o', str(binary)]
subprocess.run(command, check=True)
result = subprocess.run([str(binary)], capture_output=True, text=True,
                        env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1',
                                 UBSAN_OPTIONS='halt_on_error=1'))
(out / 'result.txt').write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('Checked method resolution: passed (ASan/UBSan)')
