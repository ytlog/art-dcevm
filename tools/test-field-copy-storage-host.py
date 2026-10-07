#!/usr/bin/env python3
"""Verify owned Java field storage, monitor transfer and card barriers."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
out = root / 'build/evidence/host/field-copy-storage'
out.mkdir(parents=True, exist_ok=True)
for sanitizer in ('address,undefined', 'thread'):
    name = sanitizer.replace(',', '-')
    binary = out / f'field_copy_storage_{name}_test'
    command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1',
               '-Wall', '-Wextra', '-Werror', '-pthread', f'-fsanitize={sanitizer}',
               '-fno-omit-frame-pointer', '-I', str(cpp),
               str(root / 'tests/host/field_copy_storage_test.cc'),
               str(cpp / 'platform/android13/field_copy_storage.cc'),
               str(cpp / 'platform/android13/heap_reference.cc'), '-o', str(binary)]
    subprocess.run(command, check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True,
                            env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1',
                                     UBSAN_OPTIONS='halt_on_error=1', TSAN_OPTIONS='halt_on_error=1'))
    (out / f'{name}-test.log').write_text(result.stdout + result.stderr)
    result.check_returncode()
    print(result.stdout.strip())
    print(f'Field-copy storage: passed ({sanitizer})')
