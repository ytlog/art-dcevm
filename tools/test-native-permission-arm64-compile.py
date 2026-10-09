#!/usr/bin/env python3
"""Compile actual permission consumers with the project's configured ARM64 ABI flags."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--commands', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
entry = next(item for item in json.loads(args.commands.read_text())
             if item['file'].endswith('/native_final_field_service.cc'))
old_root = entry['file'].removesuffix('/artdcevm/src/main/cpp/platform/common/native_final_field_service.cc')
base = [item.replace(old_root, str(root)) for item in shlex.split(entry['command'])]
out = root / 'build/evidence/permission-arm64'
out.mkdir(parents=True, exist_ok=True)
for name in ('native_final_field_service', 'native_final_field_calibration',
             'native_final_write_listener', 'native_final_write_guard'):
    command = base[:]
    command[command.index('-c') + 1] = str(cpp / 'platform/common' / (name + '.cc'))
    command[command.index('-o') + 1] = str(out / (name + '.o'))
    result = subprocess.run(command, cwd=root, capture_output=True, text=True)
    (out / (name + '.log')).write_text(result.stdout + result.stderr)
    result.check_returncode()
    print(name, 'actual ARM64 TU PASS')
