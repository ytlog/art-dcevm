#!/usr/bin/env python3
"""Verify owned Baker reference reads and concurrent stores under sanitizers."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
out = root / 'build/evidence/host/heap-reference'
out.mkdir(parents=True, exist_ok=True)
for sanitizer in ('address,undefined', 'thread'):
    name = sanitizer.replace(',', '-')
    binary = out / f'heap_reference_{name}_test'
    command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1',
               '-Wall', '-Wextra', '-Werror', '-pthread',
               f'-fsanitize={sanitizer}', '-fno-omit-frame-pointer', '-I', str(cpp),
               str(root / 'tests/host/heap_reference_test.cc'),
               str(cpp / 'platform/android13/heap_reference.cc'), '-o', str(binary)]
    subprocess.run(command, check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True,
                            env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1',
                                     UBSAN_OPTIONS='halt_on_error=1',
                                     TSAN_OPTIONS='halt_on_error=1'))
    (out / f'{name}-test.log').write_text(result.stdout + result.stderr)
    result.check_returncode()
    print(result.stdout.strip())
    print(f'Heap-reference reader: passed ({sanitizer})')
