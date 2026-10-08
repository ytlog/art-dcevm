#!/usr/bin/env python3
"""Run OUR owned Context/Thread fixture on ARM64 hardware; not ART calibration."""
import argparse
import datetime
import hashlib
import json
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--serial', required=True)
parser.add_argument('--adb', default='adb')
parser.add_argument('--ndk-bin', required=True, type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
out = root / 'build/evidence/device/owned-context-consumer' / stamp
out.mkdir(parents=True)
binary = out / 'owned-context-arm64'
cpp = root / 'artdcevm/src/main/cpp'
command = [str(args.ndk_bin / 'aarch64-linux-android30-clang++'), '-std=c++17',
           '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-fno-rtti',
           '-static-libstdc++', '-I', str(cpp),
           str(root / 'tests/device/owned_context_arm64_test.cc'),
           str(root / 'tests/device/owned_context_arm64_test.S'),
           str(cpp / 'platform/common/owned_context_consumer.cc'),
           str(cpp / 'platform/arch/arm64/owned_context_long_jump.S'), '-o', str(binary)]
build = subprocess.run(command, capture_output=True, text=True)
(out / 'build.log').write_text(build.stdout + build.stderr)
build.check_returncode()
adb = [args.adb, '-s', args.serial]
def device(*arguments):
    return subprocess.run(adb + list(arguments), capture_output=True, text=True, check=True)
properties = {name: device('shell', 'getprop', name).stdout.strip() for name in (
    'ro.build.fingerprint', 'ro.build.version.sdk', 'ro.product.cpu.abi')}
if properties['ro.product.cpu.abi'] != 'arm64-v8a':
    raise RuntimeError('This consumer test requires ARM64 hardware')
remote = '/data/local/tmp/art-dcevm-context-' + stamp
try:
    device('push', str(binary), remote)
    device('shell', 'chmod', '700', remote)
    result = subprocess.run(adb + ['shell', remote], capture_output=True, text=True,
                            timeout=30)
    (out / 'result.txt').write_text(result.stdout + result.stderr)
    passed = result.returncode == 0 and '1000 reserved/unreserved jumps passed' in result.stdout
    (out / 'run.json').write_text(json.dumps({
        'serial': args.serial, 'properties': properties,
        'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
        'compile_command': command, 'exit_code': result.returncode, 'passed': passed,
        'scope': 'Self-owned Context/Thread fixture; CPU/ownership proof only, no ART calibration',
    }, indent=2) + '\n')
    print(result.stdout.strip())
    print(out)
    result.check_returncode()
    if not passed:
        raise RuntimeError('Hardware consumer proof did not report success')
finally:
    device('shell', 'rm', '-f', remote)
