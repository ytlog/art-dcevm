#!/usr/bin/env python3
import os
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
jdk = Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'], text=True).strip())
out = root / 'build/evidence/host/jni-receiver-calibration'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'jni_receiver_calibration_test'
subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1',
                '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                '-fno-omit-frame-pointer', '-I', str(cpp), '-I', str(jdk/'include'),
                '-I', str(jdk/'include'/('darwin' if os.uname().sysname=='Darwin' else 'linux')),
                str(root/'tests/host/jni_receiver_calibration_test.cc'), '-o', str(binary)], check=True)
r = subprocess.run([str(binary)], capture_output=True, text=True,
                   env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
(out/'host.log').write_text(r.stdout+r.stderr)
r.check_returncode()
print(r.stdout.strip())
