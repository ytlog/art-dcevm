#!/usr/bin/env python3
"""Check owned Runtime services and named JNI ID decoding."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
jdk = Path(os.environ.get('JAVA_HOME') or subprocess.check_output(
    ['/usr/libexec/java_home'], text=True).strip())
platform = 'darwin' if os.uname().sysname == 'Darwin' else 'linux'
out = root / 'build/evidence/host/runtime-access'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'runtime_access_test'
command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1',
           '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
           '-fno-omit-frame-pointer', '-Wl,-export_dynamic', '-I', str(cpp),
           '-I', str(jdk / 'include'), '-I', str(jdk / 'include' / platform),
           str(root / 'tests/host/runtime_access_test.cc'),
           str(cpp / 'platform/android13/runtime_access.cc'), str(cpp / 'platform/android13/device_profile.cc'), '-o', str(binary)]
subprocess.run(command, check=True)
result = subprocess.run([str(binary)], capture_output=True, text=True,
                        env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1',
                                 UBSAN_OPTIONS='halt_on_error=1'))
(out / 'runtime-access-test.log').write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('Runtime access: passed (ASan/UBSan)')
