#!/usr/bin/env python3
"""Independent adversarial native tests and real JVM -Xcheck:jni upcall witness."""
import os
import pathlib
import subprocess

root = pathlib.Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
jdk = pathlib.Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'], text=True).strip())
platform = 'darwin' if os.uname().sysname == 'Darwin' else 'linux'
out = root / 'build/evidence/host/native-jni-exception-upcall'
out.mkdir(parents=True, exist_ok=True)
base = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror', '-pthread', '-I', str(cpp), '-I', str(jdk / 'include'), '-I', str(jdk / 'include' / platform)]
subprocess.run(base + ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(root / 'tests/host/native_jni_exception_upcall_test.cc'), '-o', str(out / 'test')], check=True)
r = subprocess.run([str(out / 'test')], capture_output=True, text=True)
(out / 'native-result.log').write_text(r.stdout + r.stderr)
r.check_returncode()
print(r.stdout.strip())
lib = out / ('probe.dylib' if platform == 'darwin' else 'probe.so')
subprocess.run(base + ['-dynamiclib' if platform == 'darwin' else '-shared', '-fPIC', str(root / 'tests/host/native_jni_exception_upcall_jvm.cc'), '-o', str(lib)], check=True)
classes = out / 'classes'
classes.mkdir(exist_ok=True)
subprocess.run([str(jdk / 'bin/javac'), '--release', '8', '-Xlint:-options', '-d', str(classes), str(root / 'artdcevm/src/main/java/com/github/ytlog/artdcevm/internal/JniExceptionUpcallFixture.java'), str(root / 'tests/host/java/JniExceptionUpcallHost.java')], check=True)
r = subprocess.run([str(jdk / 'bin/java'), '-Xcheck:jni', '-cp', str(classes), 'JniExceptionUpcallHost', str(lib)], capture_output=True, text=True)
(out / 'jvm-result.log').write_text(r.stdout + r.stderr)
r.check_returncode()
if 'WARNING' in r.stdout or 'WARNING' in r.stderr:
    raise RuntimeError('CheckJNI warning: ' + r.stdout + r.stderr)
print(r.stdout.strip())
