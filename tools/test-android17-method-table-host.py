#!/usr/bin/env python3
"""Verify Android17 method table against independent owned member identities."""
import os
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
jdk = Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'], text=True).strip())
platform = 'darwin' if os.uname().sysname == 'Darwin' else 'linux'
out = root / 'build/evidence/host/method-identity-calibration'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'android17_method_table_test'
# Link the actual shared mapping and metadata providers; only unused Android
# JNI entry bridges are stripped from this host executable.
subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-ffunction-sections', '-fdata-sections', '-I', str(cpp),
                '-I', str(jdk / 'include'), '-I', str(jdk / 'include' / platform),
                str(root / 'tests/host/android17_method_table_test.cc'),
                str(cpp / 'platform/common/method_identity_calibration.cc'), str(cpp / 'platform/android17/method_table.cc'), str(cpp / 'platform/common/modern_method_services.cc'),
                str(cpp / 'platform/common/modern_field_metadata.cc'),
                str(cpp / 'platform/common/native_interface_graph.cc'),
                str(cpp / 'evolution/dex_schema.cc'),
                '-Wl,-dead_strip' if platform == 'darwin' else '-Wl,--gc-sections', '-o', str(binary)], check=True)
result = subprocess.run([str(binary)], capture_output=True, text=True,
                        env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
(out / 'result.log').write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('Android17 method table: ASan/UBSan passed')
