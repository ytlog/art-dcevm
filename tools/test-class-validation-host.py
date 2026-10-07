#!/usr/bin/env python3
"""Exercise the enhancement CheckClass proxy using real DEX and instrumented ART boundaries."""
import os
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
java_home = Path(os.environ.get('JAVA_HOME') or subprocess.check_output(
    ['/usr/libexec/java_home'], text=True).strip())
platform = 'darwin' if os.uname().sysname == 'Darwin' else 'linux'
out = root / 'build/evidence/host/class-validation'
out.mkdir(parents=True, exist_ok=True)
fixtures = root / 'app/build/generated/patchAssets'
if not fixtures.exists():
    raise SystemExit("Missing generated patch assets; run the app fixture generation task first")
binary = out / 'class_validation_test'
command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1',
           '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
           '-fno-omit-frame-pointer', '-Wl,-export_dynamic', '-I', str(cpp),
           '-I', str(java_home / 'include'), '-I', str(java_home / 'include' / platform),
           str(root / 'tests/host/class_validation_test.cc'),
           str(cpp / 'platform/android13/class_validation.cc'),
           str(cpp / 'platform/android13/redefinition_input.cc'),
           str(cpp / 'platform/android13/schema_snapshot.cc'),
           str(cpp / 'platform/android13/modifier_evolution_policy.cc'),
           str(cpp / 'evolution/dex_schema.cc'), str(cpp / 'platform/android13/device_profile.cc'), '-o', str(binary)]
subprocess.run(command, check=True)
fixture = fixtures / 'body/com/github/ytlog/artdcevm/fixture/Evolving.dex'
result = subprocess.run([str(binary), str(fixture)], capture_output=True, text=True,
                        env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
(out / 'class-validation-test.log').write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('class validation: passed (ASan/UBSan)')
