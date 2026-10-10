#!/usr/bin/env python3
"""Exercise native JIT receiver observations, owned operations and fail-closed graph publication."""
import os
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
jdk = Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'], text=True).strip())
platform = 'darwin' if os.uname().sysname == 'Darwin' else 'linux'
out = root / 'build/evidence/host/method-identity-calibration'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'owned_jit_receivers_test'
subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I', str(cpp),
                '-I', str(jdk / 'include'), '-I', str(jdk / 'include' / platform),
                str(root / 'tests/host/owned_jit_receivers_test.cc'),
                str(cpp / 'platform/common/method_identity_calibration.cc'), str(cpp / 'platform/common/owned_jit_receivers.cc'), str(cpp / 'platform/common/heap_collect_observer.cc'), '-o', str(binary)], check=True)
outputs=[]
for mode in ['success','source-mismatch','legacy11','legacy12','missing','wrong-identity','wrong-graph','cache-missing','cleanup-error','gc-error','additive-error']:
    result=subprocess.run([str(binary),mode],capture_output=True,text=True,
                         env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
    outputs.append(result.stdout+result.stderr)
    result.check_returncode()
(out/'result.log').write_text(''.join(outputs))
print(''.join(outputs).strip())
print('Owned JIT receivers: ASan/UBSan passed')
