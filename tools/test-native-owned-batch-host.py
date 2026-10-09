#!/usr/bin/env python3
"""Exercise the actual batch source callbacks and final commit with JNI/ART boundaries."""
import os
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
jdk = Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'], text=True).strip())
platform = 'darwin' if os.uname().sysname == 'Darwin' else 'linux'
out = root / 'build/evidence/host/native-owned-batch'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'native_owned_batch_test'
(out/'android').mkdir(exist_ok=True)
(out/'android/log.h').write_text('#pragma once\n#define ANDROID_LOG_ERROR 6\ninline int __android_log_print(int,const char*,const char*,...){return 0;}\n')
sources = ['native_source_commit', 'modern_source_commit', 'source_commit', 'native_removal_policy',
           'modern_field_metadata', 'native_interface_graph', 'modern_field_storage', 'field_copy',
           'exclusive_owned_roots', 'native_cache_service', 'modern_method_services',
           'structural_methods', 'method_identity_calibration', 'object_replacement_map']
command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
           '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I', str(cpp), '-I', str(out),
           '-I', str(jdk / 'include'), '-I', str(jdk / 'include' / platform),
           str(root / 'tests/host/native_owned_batch_test.cc')]
command += [str(cpp / 'platform/common' / (s + '.cc')) for s in sources]
command += [str(cpp / 'evolution/dex_schema.cc')]
# Compressed-root fixtures need a real heap below 4 GiB. Mach-O otherwise
# reserves that entire address range as __PAGEZERO. Keep the null guard intact.
if platform == 'darwin':
    command += ['-Wl,-pagezero_size,0x10000']
command += ['-o', str(binary)]
subprocess.run(command, check=True)
result = subprocess.run([str(binary),str(root/'artdcevm/build/generated/bootstrapAssets/artdcevm/bootstrap/original.dex'),str(root/'app/build/generated/patchAssets/hier-caller/com/github/ytlog/artdcevm/fixture/HierarchyBase.dex'),str(root/'app/build/generated/patchAssets/hier-caller/com/github/ytlog/artdcevm/fixture/HierarchySubject.dex')], capture_output=True, text=True,
                        env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
(out / 'result.log').write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('Actual native batch entry: ASan/UBSan passed')
