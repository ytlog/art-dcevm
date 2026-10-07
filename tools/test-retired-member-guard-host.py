#!/usr/bin/env python3
"""Verify retired-member guard semantics with real registries and sanitizers."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
out = root / 'build/evidence/host/retired-member-guard'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'retired_member_guard_test'
command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1',
           '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
           '-fno-omit-frame-pointer', '-I', str(cpp),
           str(root / 'tests/host/retired_member_guard_test.cc')]
command += [str(cpp / ('platform/android13/' + name + '.cc')) for name in (
    'retired_member_guard', 'retired_method_registry', 'retired_field_registry')]
subprocess.run(command + ['-o', str(binary)], check=True)
result = subprocess.run([str(binary)], capture_output=True, text=True,
                        env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1',
                                 UBSAN_OPTIONS='halt_on_error=1'))
(out / 'guard-test.log').write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('retired-member guard: passed (ASan/UBSan)')
