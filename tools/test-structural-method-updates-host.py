#!/usr/bin/env python3
"""Verify owned structural method updates and JNI canonicalization with sanitizer fixtures."""
import os
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
java_home = Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'], text=True).strip())
platform = 'darwin' if os.uname().sysname == 'Darwin' else 'linux'
out = root / 'build/evidence/host/structural-method-updates'
out.mkdir(parents=True, exist_ok=True)
binary = out / ('structural_method_updates_test_'+os.environ.get('ARTDCEVM_HOST_SANITIZER','address,undefined').replace(',','-'))
command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
           '-fsanitize='+os.environ.get('ARTDCEVM_HOST_SANITIZER','address,undefined'), '-fno-omit-frame-pointer', '-pthread', '-I', str(cpp),
           '-I', str(java_home / 'include'), '-I', str(java_home / 'include' / platform),
           str(root / 'tests/host/structural_method_updates_test.cc'),
           str(cpp / 'platform/android13/structural_method_updates.cc'), str(cpp / 'platform/android13/jni_method_targets.cc'), str(cpp / 'platform/android13/transaction_metadata.cc'), str(cpp / 'platform/android13/device_profile.cc'), '-o', str(binary)]
if platform == 'darwin':
    command.extend(['-Wl,-pagezero_size,0x10000'])
else:
    command.extend(['-rdynamic', '-ldl'])
subprocess.run(command, check=True)
result = subprocess.run([str(binary)], capture_output=True, text=True,
                        env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
(out / ('structural-method-test-'+os.environ.get('ARTDCEVM_HOST_SANITIZER','address,undefined').replace(',','-')+'.log')).write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('structural method updates: passed ('+os.environ.get('ARTDCEVM_HOST_SANITIZER','address,undefined')+')')
