#!/usr/bin/env python3
"""Verify request completion and real-PC eligibility with fixed ART stack fixtures."""
import os
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
java_home = Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'], text=True).strip())
platform = 'darwin' if sys.platform == 'darwin' else 'linux'
out = root / 'build/evidence/host/suspend-deopt'
out.mkdir(parents=True, exist_ok=True)
sanitizer = os.environ.get('ARTDCEVM_HOST_SANITIZER','address,undefined')
suffix = '' if sanitizer == 'address,undefined' else '-'+sanitizer.replace(',','-')
flags = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1',
         '-Wall', '-Wextra', '-Werror', '-fsanitize='+sanitizer,
         '-fno-omit-frame-pointer', '-fno-rtti', '-pthread', '-I', str(cpp),
         '-I', str(java_home / 'include'), '-I', str(java_home / 'include' / platform)]
for name in ('pending_suspend_deopt', 'suspend_deopt_hooks'):
    command = [*flags, str(root / f'tests/host/{name}_test.cc'),
               str(cpp / 'platform/android13/pending_suspend_deopt.cc')]
    if name == 'suspend_deopt_hooks':
        command += [str(cpp / 'platform/android13/suspend_deopt_hooks.cc'), str(cpp / 'platform/android13/stack_access.cc'), str(cpp / 'platform/android13/device_profile.cc')]
        command += (['-Wl,-export_dynamic'] if sys.platform == 'darwin'
                    else ['-rdynamic', '-ldl'])
    binary = out / (name+suffix)
    subprocess.run([*command, '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True,
                            env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1',
                                     UBSAN_OPTIONS='halt_on_error=1'))
    (out / f'{name}{suffix}.log').write_text(result.stdout + result.stderr)
    result.check_returncode()
    print(f'{name}: passed ({sanitizer})')
