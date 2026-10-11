#!/usr/bin/env python3
"""Closed production registration/execution service in a real CheckJNI JVM."""
import os
import pathlib
import subprocess
root = pathlib.Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
jdk = pathlib.Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'], text=True).strip())
platform = 'darwin' if os.uname().sysname == 'Darwin' else 'linux'
out = root / 'build/evidence/host/native-jni-exception-calibration'
out.mkdir(parents=True, exist_ok=True)
lib = out / ('calibration.dylib' if platform == 'darwin' else 'calibration.so')
cmd = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror', '-pthread', '-dynamiclib' if platform == 'darwin' else '-shared', '-fPIC', '-I', str(cpp), '-I', str(jdk / 'include'), '-I', str(jdk / 'include' / platform), str(root / 'tests/host/native_jni_exception_calibration_jvm.cc'), str(cpp / 'platform/common/native_jni_exception_calibration.cc'), '-o', str(lib)]
subprocess.run(cmd, check=True)
base = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror', '-pthread', '-I', str(cpp), '-I', str(jdk / 'include'), '-I', str(jdk / 'include' / platform)]
subprocess.run(base + ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(root / 'tests/host/native_jni_exception_calibration_test.cc'), str(cpp / 'platform/common/native_jni_exception_calibration.cc'), '-o', str(out / 'test')], check=True)
r = subprocess.run([str(out / 'test')], capture_output=True, text=True)
(out / 'native-result.log').write_text(r.stdout + r.stderr)
r.check_returncode()
print(r.stdout.strip())
classes = out / 'classes'
classes.mkdir(exist_ok=True)
subprocess.run([str(jdk / 'bin/javac'), '--release', '8', '-Xlint:-options', '-d', str(classes), str(root / 'artdcevm/src/main/java/com/github/ytlog/artdcevm/internal/JniExceptionUpcallFixture.java'), str(root / 'tests/host/java/JniExceptionCalibrationHost.java')], check=True)
r = subprocess.run([str(jdk / 'bin/java'), '-Xcheck:jni', '-cp', str(classes), 'JniExceptionCalibrationHost', str(lib)], capture_output=True, text=True)
(out / 'jvm-result.log').write_text(r.stdout + r.stderr)
r.check_returncode()
if 'WARNING' in r.stdout or 'WARNING' in r.stderr:
    raise RuntimeError('CheckJNI warning: ' + r.stdout + r.stderr)
print(r.stdout.strip())
