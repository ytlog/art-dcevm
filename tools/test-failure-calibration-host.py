#!/usr/bin/env python3
"""Verify owned owned redefinition failure calibration using a real host libc++ oracle with sanitizer fixtures."""
import os
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
java_home = Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'], text=True).strip())
platform = 'darwin' if os.uname().sysname == 'Darwin' else 'linux'
out = root / 'build/evidence/host/failure-calibration'
out.mkdir(parents=True, exist_ok=True)
binary = out / ('failure_calibration_test_'+os.environ.get('ARTDCEVM_HOST_SANITIZER','address,undefined').replace(',','-'))
# Require real libc++ ABI v1; the oracle owns assignment and allocation.
# The fixture independently verifies its string and Driver layouts.
command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-stdlib=libc++', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
           '-fsanitize='+os.environ.get('ARTDCEVM_HOST_SANITIZER','address,undefined'), '-fno-omit-frame-pointer', '-pthread', '-I', str(cpp),
           '-I', str(java_home / 'include'), '-I', str(java_home / 'include' / platform),
           str(root / 'tests/host/failure_calibration_test.cc'),
           str(cpp / 'platform/android13/failure_calibration.cc'), str(cpp / 'platform/android13/redefinition_failure.cc'), str(cpp / 'platform/android13/device_profile.cc'), str(cpp / 'platform/android13/field_validation.cc'), str(cpp / 'platform/android13/field_evolution_policy.cc'), '-o', str(binary)]
if platform == 'darwin':
    command.extend(['-Wl,-pagezero_size,0x10000'])
else:
    command.extend(['-rdynamic', '-ldl'])
subprocess.run(command, check=True)
result = subprocess.run([str(binary)], capture_output=True, text=True,
                        env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
(out / ('failure-calibration-test-'+os.environ.get('ARTDCEVM_HOST_SANITIZER','address,undefined').replace(',','-')+'.log')).write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('failure calibration: passed ('+os.environ.get('ARTDCEVM_HOST_SANITIZER','address,undefined')+')')
