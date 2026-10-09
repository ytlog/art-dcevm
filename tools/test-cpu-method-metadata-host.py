#!/usr/bin/env python3
"""Verify width-specific fixed ART declarations without device capability claims."""
import os
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
jdk = Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'], text=True).strip())
out = root / 'build/evidence/host/cpu-method-metadata'
out.mkdir(parents=True, exist_ok=True)
source = root / 'tests/host/cpu_method_metadata_test.cc'
common = ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(cpp)]
binary = out / 'cpu_method_metadata_test'
subprocess.run([os.environ.get('CXX', 'clang++'), *common, '-g', '-O1',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                '-I', str(jdk / 'include'), '-I', str(jdk / 'include' / ('darwin' if os.uname().sysname == 'Darwin' else 'linux')),
                str(source), '-o', str(binary)], check=True)
r = subprocess.run([str(binary)], capture_output=True, text=True,
                   env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
(out / 'host.log').write_text(r.stdout + r.stderr)
r.check_returncode()
print(r.stdout.strip())
# Explicit opt-in path makes NDK compilation repeatable on other developer hosts.
ndk = os.environ.get('ANDROID_NDK_HOME')
if ndk:
    toolchain = Path(ndk) / 'toolchains/llvm/prebuilt'
    prebuilt = next(toolchain.iterdir())
    for target in ('armv7a-linux-androideabi30', 'aarch64-linux-android30', 'x86_64-linux-android30'):
        compiler = prebuilt / 'bin' / (target + '-clang++')
        obj = out / (target + '.o')
        subprocess.run([str(compiler), *common, '-c', str(source), '-o', str(obj)], check=True)
        print(target, 'strict C++ compile passed (no device execution)')
else:
    print('NDK compile not requested; set ANDROID_NDK_HOME to include it')
