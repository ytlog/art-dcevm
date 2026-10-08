#!/usr/bin/env python3
"""Verify class-loader registry pruning across immediate and batch cleanup."""
import os
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
out = root / 'build/evidence/host/class-loader-cleanup'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'class_loader_cleanup_test'
subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1',
                '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                '-fno-omit-frame-pointer', '-I', str(root / 'artdcevm/src/main/cpp'),
                str(root / 'tests/host/class_loader_cleanup_test.cc'), '-o', str(binary)], check=True)
result = subprocess.run([str(binary)], capture_output=True, text=True,
                        env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1',
                                 UBSAN_OPTIONS='halt_on_error=1'))
(out / 'result.txt').write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('Class-loader cleanup: passed (ASan/UBSan)')
