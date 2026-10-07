#!/usr/bin/env python3
"""Exercise field transfer scheduling with typed atomic stores and exact commit ordering."""
import os
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
out = root / 'build/evidence/host/field-copy'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'field_copy_test'
command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
           '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I', str(cpp),
           str(root / 'tests/host/field_copy_test.cc'), str(cpp / 'platform/android13/field_copy.cc'), '-o', str(binary)]
if os.uname().sysname != 'Darwin':
    command.append('-ldl')
subprocess.run(command, check=True)
result = subprocess.run([str(binary)], capture_output=True, text=True,
                        env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
(out / 'field-copy-test.log').write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('field copying: passed (ASan/UBSan)')
