#!/usr/bin/env python3
"""Check complete immutable source DEX roles under ASan/UBSan, including real D8 IDs."""
import argparse
import os
import pathlib
import subprocess

root = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--dex', type=pathlib.Path)
a = p.parse_args()
out = root / 'build/evidence/host/source-cache-role-catalog'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'catalog-test'
command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
           '-g', '-O1', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
           '-I', str(root / 'artdcevm/src/main/cpp'),
           str(root / 'tests/host/source_cache_role_catalog_test.cc'),
           str(root / 'artdcevm/src/main/cpp/platform/common/source_cache_role_catalog.cc'),
           '-o', str(binary)]
compiled = subprocess.run(command, capture_output=True, text=True)
(out / 'compile.log').write_text(compiled.stdout + compiled.stderr)
compiled.check_returncode()
args = [str(binary)]
if a.dex:
    args.append(str(a.dex.resolve()))
result = subprocess.run(args, capture_output=True, text=True, timeout=60,
                        env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
(out / 'result.log').write_text(result.stdout + result.stderr)
print(result.stdout + result.stderr, end='')
result.check_returncode()
