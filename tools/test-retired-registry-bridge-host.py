#!/usr/bin/env python3
"""Check minimal source registry callbacks against the enhancement-owned state."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
jdk = Path(os.environ.get('JAVA_HOME') or subprocess.check_output(
    ['/usr/libexec/java_home'], text=True).strip())
platform = 'darwin' if os.uname().sysname == 'Darwin' else 'linux'
out = root / 'build/evidence/host/retired-registry-bridge'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'retired_registry_bridge_test'
command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1',
           '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
           '-fno-omit-frame-pointer', '-Wl,-export_dynamic', '-I', str(cpp),
           '-I', str(jdk / 'include'), '-I', str(jdk / 'include' / platform),
           str(root / 'tests/host/retired_registry_bridge_test.cc'),
           str(cpp / 'platform/android13/retired_method_registry_bridge.cc'),
           str(cpp / 'platform/android13/retired_field_registry_bridge.cc'),
           str(cpp / 'platform/android13/retired_method_registry.cc'),
           str(cpp / 'platform/android13/retired_field_registry.cc'), '-o', str(binary)]
if platform == 'darwin':
    command.extend(['-Wl,-pagezero_size,0x10000'])
subprocess.run(command, check=True)
result = subprocess.run([str(binary)], capture_output=True, text=True,
                        env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1',
                                 UBSAN_OPTIONS='halt_on_error=1'))
(out / 'retired-registry-bridge-test.log').write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('Retired registry bridge: passed (ASan/UBSan)')
