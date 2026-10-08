#!/usr/bin/env python3
"""Verify calibrated void DeoptimizeIfNeeded call ABI and rejection boundaries."""
import argparse
import os
from pathlib import Path
import subprocess
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--arm64-cxx', help='NDK clang++ for AAPCS64 libc++ return ABI compilation')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
out = root / 'build/evidence/host/deoptimize-if-needed'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'deoptimize_if_needed_service_test'
subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1',
                '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                '-fno-omit-frame-pointer', '-I', str(root / 'artdcevm/src/main/cpp'),
                str(root / 'tests/host/deoptimize_if_needed_service_test.cc'), '-o', str(binary)], check=True)
result = subprocess.run([str(binary)], capture_output=True, text=True,
                        env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1',
                                 UBSAN_OPTIONS='halt_on_error=1'))
(out / 'result.txt').write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('DeoptimizeIfNeeded: passed (ASan/UBSan)')

if args.arm64_cxx:
    ir = out / 'arm64-abi.ll'
    source = root / 'tests/host/deoptimize_if_needed_arm64_abi.cc'
    command = [args.arm64_cxx, '--target=aarch64-linux-android30', '-std=c++17',
               '-Wall', '-Wextra', '-Werror', '-O2', '-S', '-emit-llvm',
               '-I', str(root / 'artdcevm/src/main/cpp'), str(source), '-o', str(ir)]
    subprocess.run(command, check=True)
    lines = ir.read_text().splitlines()
    owned = next(line for line in lines if line.startswith('define ') and 'ArtOwnedDeoptCall' in line)
    void = next(line for line in lines if line.startswith('define ') and '@ArtVoidDeoptCall(' in line)
    assert 'sret(' in owned and 'unique_ptr' in owned
    assert 'sret(' not in void
    assert any('call void %' in line and 'sret(' in line for line in lines)
    (out / 'arm64-abi-result.txt').write_text('Canonical libc++ owned Context uses ARM64 sret; void declaration does not.\n')
    print('ARM64 return ABI: passed (NDK compile / LLVM IR; no device calibration implied)')
