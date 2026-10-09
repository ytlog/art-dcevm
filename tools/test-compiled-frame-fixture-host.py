#!/usr/bin/env python3
"""Actual bounded DEX oracle, native gate and Java/JNI fixture lifecycle."""
import os
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
out = root / 'build/evidence/host/compiled-frame-fixture'
out.mkdir(parents=True, exist_ok=True)
java = Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'], text=True).strip())
platform = 'darwin' if os.uname().sysname == 'Darwin' else 'linux'
common = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-fno-rtti', '-I', str(cpp)]
exe = out / 'dex-oracle'
subprocess.run(common + ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(root / 'tests/host/compiled_frame_dex_oracle_test.cc'), '-o', str(exe)], check=True)
r = subprocess.run([str(exe)], capture_output=True, text=True, env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
(out / 'dex-oracle.log').write_text(r.stdout + r.stderr); r.check_returncode(); print(r.stdout.strip())
shared = out / ('fixture.dylib' if platform == 'darwin' else 'fixture.so')
subprocess.run(common + ['-shared', '-fPIC', '-pthread', '-I', str(java / 'include'), '-I', str(java / 'include' / platform), str(root / 'tests/host/compiled_frame_fixture_bridge_test.cc'), str(cpp / 'platform/common/compiled_frame_fixture.cc'), '-o', str(shared)], check=True)
subprocess.run([str(java / 'bin/javac'), '--release', '11', '-d', str(out), str(root / 'artdcevm/src/main/java/com/github/ytlog/artdcevm/internal/CompiledFrameFixture.java'), str(root / 'tests/host/CompiledFrameFixtureHostTest.java')], check=True)
r = subprocess.run([str(java / 'bin/java'), '-cp', str(out), 'com.github.ytlog.artdcevm.internal.CompiledFrameFixtureHostTest', str(shared)], capture_output=True, text=True, timeout=90)
(out / 'java-jni-lifecycle.log').write_text(r.stdout + r.stderr); r.check_returncode(); print(r.stdout.strip())
subprocess.run(common + ['-I', str(java / 'include'), '-I', str(java / 'include' / platform), '-I', str(root / 'third_party/jvmti'), '-fsyntax-only', str(cpp / 'platform/common/compiled_frame_owner.cc')], check=True)
print('Compiled frame typed owner: strict host syntax passed; real ART compiled geometry/CodeInfo needs device proof')

sdk = Path(os.environ.get('ANDROID_HOME') or str(Path.home() / 'Library/Android/sdk'))
d8 = sdk / 'build-tools/36.0.0/lib/d8.jar'
android = sdk / 'platforms/android-35/android.jar'
if not d8.exists() or not android.exists():
    raise RuntimeError('Actual fixture DEX check requires Android SDK build-tools 36 and platform 35')
dexout = out / 'actual-d8'; dexout.mkdir(exist_ok=True)
classes = sorted((out / 'com/github/ytlog/artdcevm/internal').glob('CompiledFrameFixture*.class'))
classes = [p for p in classes if 'HostTest' not in p.name]
subprocess.run([str(java / 'bin/java'), '-cp', str(d8), 'com.android.tools.r8.D8', '--min-api', '30', '--lib', str(android), '--output', str(dexout), *map(str, classes)], check=True)
actual = out / 'actual-installed-dex-oracle'
subprocess.run(common + ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(root / 'tests/host/compiled_frame_installed_dex_test.cc'), str(cpp / 'platform/common/bootstrap_dex_oracle.cc'), str(cpp / 'evolution/dex_schema.cc'), '-o', str(actual)], check=True)
r = subprocess.run([str(actual), str(dexout / 'classes.dex')], capture_output=True, text=True, env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
(out / 'actual-d8-codeitems.log').write_text(r.stdout + r.stderr); r.check_returncode(); print(r.stdout.strip())
