#!/usr/bin/env python3
"""Run independent JNI/JVMTI bootstrap orchestration and real storage regressions."""
import os
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
java = Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'], text=True).strip())
platform = 'darwin' if os.uname().sysname == 'Darwin' else 'linux'
out = root / 'build/evidence/host/owned-calibration'
out.mkdir(parents=True, exist_ok=True)
san = os.environ.get('ARTDCEVM_HOST_SANITIZER', 'address,undefined')
binary = out / ('owned_calibration_test_' + san.replace(',', '-'))
cmd = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror', '-fsanitize='+san, '-fno-omit-frame-pointer', '-I', str(cpp), '-I', str(java/'include'), '-I', str(java/'include'/platform), str(root/'tests/host/owned_calibration_test.cc')]
cmd += [str(cpp/'platform/android13'/name) for name in ['owned_calibration.cc', 'device_profile.cc', 'field_copy_storage.cc', 'handle_root_scope.cc']]
cmd += ['-o', str(binary)]
if platform == 'darwin': cmd += ['-Wl,-pagezero_size,0x10000']
subprocess.run(cmd, check=True)
result = subprocess.run([str(binary)], capture_output=True, text=True, env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
(out/('owned-calibration-'+san.replace(',', '-')+'.log')).write_text(result.stdout+result.stderr)
result.check_returncode()
print(result.stdout.strip())
