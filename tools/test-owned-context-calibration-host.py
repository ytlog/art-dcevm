#!/usr/bin/env python3
"""Verify owned Context register calibration and native destructor lifetimes."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
out = root / 'build/evidence/host/owned-context-calibration'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'owned_context_calibration_test'
command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1',
           '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
           '-fno-omit-frame-pointer', '-I', str(cpp),
           str(root / 'tests/host/owned_context_calibration_test.cc'),
           str(cpp / 'platform/common/owned_context_calibration.cc'), '-pthread', '-o', str(binary)]
subprocess.run(command, check=True)
result = subprocess.run([str(binary)], capture_output=True, text=True,
                        env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1',
                                 UBSAN_OPTIONS='halt_on_error=1'))
(out / 'owned-context-calibration-test.log').write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('Owned Context calibration: passed (ASan/UBSan)')
