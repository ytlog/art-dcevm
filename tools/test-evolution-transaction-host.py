#!/usr/bin/env python3
"""Verify owned transaction ownership and compiled precommit scheduling with sanitizer fixtures."""
import os
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
java_home = Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'], text=True).strip())
platform = 'darwin' if os.uname().sysname == 'Darwin' else 'linux'
out = root / 'build/evidence/host/evolution-transaction'
out.mkdir(parents=True, exist_ok=True)
binary = out / ('evolution_transaction_test_'+os.environ.get('ARTDCEVM_HOST_SANITIZER','address,undefined').replace(',','-'))
# Fixed virtual ABI proxies intentionally use ART's foreign vtables; RTTI identities differ.
# Disable RTTI/vptr checks while retaining all remaining ASan/UBSan checks.
command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
           '-fsanitize='+os.environ.get('ARTDCEVM_HOST_SANITIZER','address,undefined'), '-fno-omit-frame-pointer', '-fno-rtti', '-pthread', '-I', str(cpp),
           '-I', str(java_home / 'include'), '-I', str(java_home / 'include' / platform),
           str(root / 'tests/host/evolution_transaction_test.cc'),
           str(cpp / 'platform/android13/stack_access.cc'), str(cpp / 'platform/android13/evolution_transaction.cc'), str(cpp / 'platform/android13/device_dex_input.cc'), str(cpp / 'platform/android13/thread_suspend_scope.cc'), '-o', str(binary)]
if platform == 'darwin':
    command.extend(['-Wl,-pagezero_size,0x10000'])
else:
    command.extend(['-rdynamic', '-ldl'])
subprocess.run(command, check=True)
result = subprocess.run([str(binary)], capture_output=True, text=True,
                        env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
(out / ('evolution-transaction-test-'+os.environ.get('ARTDCEVM_HOST_SANITIZER','address,undefined').replace(',','-')+'.log')).write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('evolution transaction: passed ('+os.environ.get('ARTDCEVM_HOST_SANITIZER','address,undefined')+')')
