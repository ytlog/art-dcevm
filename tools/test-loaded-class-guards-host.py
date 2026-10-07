#!/usr/bin/env python3
"""Verify loaded-subclass guards using fixed ART methods and real DEX signatures."""
import os
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
out = root / 'build/evidence/host/loaded-class-guards'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'loaded_class_guards_test'
command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
           '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I', str(cpp),
           str(root / 'tests/host/loaded_class_guards_test.cc'),
           str(cpp / 'platform/android13/loaded_class_guards.cc'), str(cpp / 'evolution/dex_schema.cc'), '-o', str(binary)]
subprocess.run(command, check=True)
fixture = root / 'app/build/generated/patchAssets/metadata/com/github/ytlog/artdcevm/fixture/SchemaSubject.dex'
result = subprocess.run([str(binary), str(fixture)], capture_output=True, text=True,
                        env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
(out / 'guards-test.log').write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('loaded-class guards: passed (ASan/UBSan)')
