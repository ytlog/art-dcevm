#!/usr/bin/env python3
"""Build and run declaration-policy regressions on the host with -Werror."""
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
sdk = os.environ.get('ANDROID_SDK_ROOT') or os.environ.get('ANDROID_HOME')
if not sdk:
    sdk = next((line.split('=', 1)[1] for line in (ROOT / 'local.properties').read_text().splitlines()
                if line.startswith('sdk.dir=')), None)
if not sdk:
    raise SystemExit('Configure an Android SDK to provide the JNI declarations')
headers = sorted(Path(sdk).glob('ndk/*/toolchains/llvm/prebuilt/*/sysroot/usr/include/jni.h'))
if not headers:
    raise SystemExit('No NDK JNI declarations found')
compiler = shutil.which('clang++') or shutil.which('c++')
if not compiler:
    raise SystemExit('No host C++ compiler found')
out = ROOT / 'build/evidence/policy-host'
out.mkdir(parents=True, exist_ok=True)
include = out / 'include'
include.mkdir(exist_ok=True)
# Use only JNI declarations with the host C/C++ standard library, never the
# Android sysroot headers as a host system include directory.
shutil.copyfile(headers[-1], include / 'jni.h')
binary = out / 'policy-test'
command = [compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
           '-I' + str(include), '-I' + str(ROOT / 'third_party/jvmti'),
           '-I' + str(ROOT / 'artdcevm/src/main/cpp'), str(ROOT / 'tools/test-structural-policy.cc'),
           str(ROOT / 'artdcevm/src/main/cpp/platform/common/structural_policy.cc'), '-o', str(binary)]
with (out / 'build.log').open('w') as log:
    result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
if result.returncode:
    raise SystemExit('Policy build failed; see build/evidence/policy-host/build.log')
result = subprocess.run([str(binary)], capture_output=True, text=True)
(out / 'test.log').write_text(result.stdout + result.stderr)
print(result.stdout.strip())
raise SystemExit(result.returncode)
