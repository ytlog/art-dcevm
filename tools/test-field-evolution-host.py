#!/usr/bin/env python3
"""Check the enhanced field-validation policy under ASan and UBSan."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
out = root / 'build/evidence/host/field-evolution'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'field_evolution_policy_test'
command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1',
           '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
           '-fno-omit-frame-pointer', '-I', str(cpp),
           str(root / 'tests/host/field_evolution_policy_test.cc'),
           str(cpp / 'platform/common/native_schema_policy.cc'), str(cpp / 'platform/common/native_method_evolution_policy.cc'), str(cpp / 'evolution/migration_plan.cc'), str(cpp / 'evolution/dex_schema.cc'), '-o', str(binary)]
subprocess.run(command, check=True)
result = subprocess.run([str(binary)], capture_output=True, text=True,
                        env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1',
                                 UBSAN_OPTIONS='halt_on_error=1'))
(out / 'policy-test.log').write_text(result.stdout + result.stderr)
result.check_returncode()
print('field evolution policy: passed (ASan/UBSan)')
