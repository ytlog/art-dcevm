#!/usr/bin/env python3
"""Verify DexCache method pair identity and atomic collision handling."""
import os
from pathlib import Path
import platform
import subprocess

root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
out = root / 'build/evidence/host/dex-cache-method-lookup'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'dex_cache_method_lookup_test'
command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1',
           '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
           '-fno-omit-frame-pointer', '-pthread', '-I', str(cpp)]
if platform.machine().lower() in ('x86_64', 'amd64'):
    command += ['-mcx16']
command += [str(root / 'tests/host/dex_cache_method_lookup_test.cc'),
            str(cpp / 'platform/android13/dex_cache_method_lookup.cc'), '-o', str(binary)]
subprocess.run(command, check=True)
result = subprocess.run([str(binary)], capture_output=True, text=True,
                        env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1',
                                 UBSAN_OPTIONS='halt_on_error=1'))
(out / 'result.txt').write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('DexCache method lookup: passed (ASan/UBSan)')
