#!/usr/bin/env python3
"""Verify native Context ownership and ARM64 consumer buffer publication."""
import os
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
out = root / 'build/evidence/host/owned-context-consumer'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'owned_context_consumer_test'
subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1',
                '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                '-fno-omit-frame-pointer', '-I', str(root / 'artdcevm/src/main/cpp'),
                str(root / 'tests/host/owned_context_consumer_test.cc'),
                str(root / 'artdcevm/src/main/cpp/platform/common/owned_context_consumer.cc'), '-o', str(binary)], check=True)
result = subprocess.run([str(binary)], capture_output=True, text=True,
                        env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1',
                                 UBSAN_OPTIONS='halt_on_error=1'))
(out / 'result.txt').write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('Owned Context consumer: passed (ASan/UBSan)')
