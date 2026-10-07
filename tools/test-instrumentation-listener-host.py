#!/usr/bin/env python3
"""Verify listener ABI, receiver checks, type-cache reads and code-item decoding."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
jdk = Path(os.environ.get('JAVA_HOME') or subprocess.check_output(
    ['/usr/libexec/java_home'], text=True).strip())
platform = 'darwin' if os.uname().sysname == 'Darwin' else 'linux'
out = root / 'build/evidence/host/instrumentation-listener'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'instrumentation_listener_test'
command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1',
           '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
           '-fno-omit-frame-pointer', '-Wl,-export_dynamic', '-I', str(cpp),
           '-I', str(jdk / 'include'), '-I', str(jdk / 'include' / platform),
           str(root / 'tests/host/instrumentation_listener_test.cc'),
           str(cpp / 'platform/android13/instrumentation_listener.cc'),
           '-o', str(binary)]
if platform == 'darwin':
    command.extend(['-Wl,-pagezero_size,0x10000'])
subprocess.run(command, check=True)
result = subprocess.run([str(binary)], capture_output=True, text=True,
                        env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1',
                                 UBSAN_OPTIONS='halt_on_error=1'))
(out / 'instrumentation-listener-test.log').write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('Instrumentation listener: passed (ASan/UBSan)')
