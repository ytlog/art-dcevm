#!/usr/bin/env python3
"""Verify pending-thread ownership and the suspend proxy without ART layouts."""
import os
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
out = root / 'build/evidence/host/suspend-deopt'
out.mkdir(parents=True, exist_ok=True)
flags = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1',
         '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
         '-fno-omit-frame-pointer', '-pthread', '-I', str(cpp)]
for name in ('pending_suspend_deopt', 'suspend_deopt_hooks'):
    command = [*flags, str(root / f'tests/host/{name}_test.cc'),
               str(cpp / 'platform/android13/pending_suspend_deopt.cc')]
    if name == 'suspend_deopt_hooks':
        command += [str(cpp / 'platform/android13/suspend_deopt_hooks.cc')]
        command += (['-Wl,-export_dynamic'] if sys.platform == 'darwin'
                    else ['-rdynamic', '-ldl'])
    binary = out / name
    subprocess.run([*command, '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True,
                            env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1',
                                     UBSAN_OPTIONS='halt_on_error=1'))
    (out / f'{name}.log').write_text(result.stdout + result.stderr)
    result.check_returncode()
    print(f'{name}: passed (ASan/UBSan)')
