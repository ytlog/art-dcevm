#!/usr/bin/env python3
"""Verify persistent final-field guard publication on concurrent threads."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
out = root / 'build/evidence/host/final-field-guard'
out.mkdir(parents=True, exist_ok=True)
for sanitizer in ('address,undefined', 'thread'):
    name = sanitizer.replace(',', '-')
    binary = out / f'final_field_guard_state_{name}_test'
    command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1',
               '-Wall', '-Wextra', '-Werror', '-pthread',
               f'-fsanitize={sanitizer}', '-fno-omit-frame-pointer', '-I', str(cpp),
               str(root / 'tests/host/final_field_guard_state_test.cc'),
               str(cpp / 'platform/common/modern_final_field_guard.cc'),
               '-o', str(binary)]
    subprocess.run(command, check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True,
                            env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1',
                                     UBSAN_OPTIONS='halt_on_error=1',
                                     TSAN_OPTIONS='halt_on_error=1'))
    (out / f'{name}-test.log').write_text(result.stdout + result.stderr)
    result.check_returncode()
    print(f'Final field guard: passed ({sanitizer})')
