#!/usr/bin/env python3
"""Verify versioned ARM64 decoded CodeInfo objects and borrowed table views."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
out = root / 'build/evidence/host/active-frame-recovery-proof'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'active_frame_recovery_proof_test'
command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1',
           '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
           '-fno-omit-frame-pointer', '-fno-rtti', '-I', str(cpp),
           str(root / 'tests/host/active_frame_recovery_proof_test.cc'), '-o', str(binary)]
subprocess.run(command, check=True)
result = subprocess.run([str(binary)], capture_output=True, text=True,
                        env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1',
                                 UBSAN_OPTIONS='halt_on_error=1'))
(out / 'result.txt').write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('Active frame recovery proof: passed (ASan/UBSan)')
