#!/usr/bin/env python3
"""Verify versioned ARM64 decoded CodeInfo objects and borrowed table views."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
out = root / 'build/evidence/host/code-info-view'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'code_info_view_test'
command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1',
           '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
           '-fno-omit-frame-pointer', '-I', str(cpp),
           str(root / 'tests/host/code_info_view_test.cc'), '-o', str(binary)]
subprocess.run(command, check=True)
result = subprocess.run([str(binary)], capture_output=True, text=True,
                        env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1',
                                 UBSAN_OPTIONS='halt_on_error=1'))
(out / 'result.txt').write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('CodeInfo view: passed (ASan/UBSan)')
