#!/usr/bin/env python3
"""Exercise hierarchy validation with moving-GC root updates and named ART boundaries."""
import os
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
java_home = Path(os.environ.get('JAVA_HOME') or subprocess.check_output(
    ['/usr/libexec/java_home'], text=True).strip())
platform = 'darwin' if os.uname().sysname == 'Darwin' else 'linux'
out = root / 'build/evidence/host/hierarchy-validation'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'hierarchy_validation_test'
command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1',
           '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
           '-fno-omit-frame-pointer', '-Wl,-export_dynamic', '-I', str(cpp),
           '-I', str(java_home / 'include'), '-I', str(java_home / 'include' / platform),
           str(root / 'tests/host/hierarchy_validation_test.cc'),
           str(cpp / 'platform/android13/hierarchy_validation.cc'),
           str(cpp / 'platform/android13/handle_root_scope.cc'),
           str(cpp / 'platform/android13/loaded_class_guards.cc'), str(cpp / 'platform/android13/device_profile.cc'), '-o', str(binary)]
if platform == 'darwin':
    command.extend(['-Wl,-pagezero_size,0x10000'])
subprocess.run(command, check=True)
result = subprocess.run([str(binary)], capture_output=True, text=True,
                        env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
(out / 'hierarchy-validation-test.log').write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('hierarchy validation: passed (ASan/UBSan)')
