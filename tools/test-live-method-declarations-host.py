#!/usr/bin/env python3
"""Verify method declaration flags and JVMTI allocation ownership under ASan/UBSan."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
java_home = Path(os.environ.get('JAVA_HOME') or subprocess.check_output(
    ['/usr/libexec/java_home'], text=True).strip())
platform = 'darwin' if os.uname().sysname == 'Darwin' else 'linux'
out = root / 'build/evidence/host/live-method-declarations'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'live_method_declarations_test'
command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1',
           '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
           '-fno-omit-frame-pointer', '-I', str(cpp), '-I', str(java_home / 'include'),
           '-I', str(java_home / 'include' / platform),
           str(root / 'tests/host/live_method_declarations_test.cc'),
           str(cpp / 'platform/android13/schema_snapshot.cc'), '-o', str(binary)]
subprocess.run(command, check=True)
result = subprocess.run([str(binary)], capture_output=True, text=True,
                        env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1',
                                 UBSAN_OPTIONS='halt_on_error=1'))
(out / 'declarations-test.log').write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('live method declarations: passed (ASan/UBSan)')
