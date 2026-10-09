#!/usr/bin/env python3
"""Verify actual native source observer setup ordering under sanitizers."""
import os
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
jdk = Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'], text=True).strip())
platform = 'darwin' if os.uname().sysname == 'Darwin' else 'linux'
out = root / 'build/evidence/host/native-source-observation-owner'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'owner-test'
command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
           '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I', str(cpp),
           '-I', str(jdk / 'include'), '-I', str(jdk / 'include' / platform),
           str(root / 'tests/host/native_source_observation_owner_test.cc'), '-o', str(binary)]
subprocess.run(command, check=True)
subprocess.run([str(binary)], check=True)
