#!/usr/bin/env python3
import os
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
out = root / 'build/evidence/host/jni-receiver-metadata'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'jni_receiver_metadata_test'
subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1',
                '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                '-fno-omit-frame-pointer', '-I', str(root / 'artdcevm/src/main/cpp'),
                str(root / 'tests/host/jni_receiver_metadata_test.cc'), '-o', str(binary)], check=True)
r = subprocess.run([str(binary)], capture_output=True, text=True,
                   env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
(out / 'host.log').write_text(r.stdout + r.stderr)
r.check_returncode()
print(r.stdout.strip())
