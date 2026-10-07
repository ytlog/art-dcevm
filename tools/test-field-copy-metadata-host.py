#!/usr/bin/env python3
"""Verify owned Class/ArtField copy metadata using real DEX and named accessor mocks."""
import os
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
java_home = Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'], text=True).strip())
platform = 'darwin' if os.uname().sysname == 'Darwin' else 'linux'
out = root / 'build/evidence/host/field-copy-metadata'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'field_copy_metadata_test'
command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
           '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I', str(cpp),
           '-I', str(java_home / 'include'), '-I', str(java_home / 'include' / platform),
           str(root / 'tests/host/field_copy_metadata_test.cc'),
           str(cpp / 'platform/android13/field_copy_metadata.cc'), '-o', str(binary)]
if platform == 'darwin':
    # ART compressed heap references require a low-address cache fixture.
    command.extend(['-Wl,-pagezero_size,0x10000'])
subprocess.run(command, check=True)
fixture = root / 'app/build/generated/patchAssets/metadata/com/github/ytlog/artdcevm/fixture/SchemaSubject.dex'
result = subprocess.run([str(binary), str(fixture)], capture_output=True, text=True,
                        env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
(out / 'metadata-test.log').write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('field-copy metadata: passed (ASan/UBSan)')
