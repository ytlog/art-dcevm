#!/usr/bin/env python3
"""Exercise fixed-source handle roots against calibrated storage and named source traversal."""
import argparse
import os
from pathlib import Path
import subprocess
parser=argparse.ArgumentParser();parser.add_argument('--compile-only',action='store_true');args=parser.parse_args()
root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
jdk = Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'], text=True).strip())
platform = 'darwin' if os.uname().sysname == 'Darwin' else 'linux'
out = root / 'build/evidence/host/native-typed-heap-walk'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'native_typed_heap_walk_test'
fixture=(root/'tests/host/native_source_commit_test.cc').read_text().split('int main(){')[0].replace('std::array<unsigned char,200>', 'std::array<unsigned char,512>')
(out/'native_commit_fixture.inc').write_text(fixture)
sources = ['native_typed_heap_walk','native_source_stop','native_source_class_loading','source_handle_roots', 'source_allocation_transaction', 'native_source_commit', 'native_initialized_transfer', 'modern_source_commit', 'source_commit',
           'modern_field_metadata', 'native_interface_graph', 'modern_field_storage', 'field_copy', 'prepared_field_copy',
           'exclusive_owned_roots', 'native_cache_service', 'modern_method_services',
           'structural_methods', 'method_identity_calibration', 'object_replacement_map']
command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
           '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I', str(cpp), '-I', str(out),
           '-I', str(jdk / 'include'), '-I', str(jdk / 'include' / platform),
           str(root / 'tests/host/native_typed_heap_walk_test.cc')]
command += [str(cpp / 'platform/common' / (s + '.cc')) for s in sources]
command += [str(cpp / 'evolution/dex_schema.cc')]
# Compressed-root fixtures need a real heap below 4 GiB. Mach-O otherwise
# reserves that entire address range as __PAGEZERO. Keep the null guard intact.
if platform == 'darwin':
    command += ['-Wl,-pagezero_size,0x10000']
command += ['-o', str(binary)]
subprocess.run(command, check=True)
if args.compile_only:
    print('Source handle roots link PASS; execution pending');raise SystemExit(0)
for layout in ('actual',):
    fixture_env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
    if layout == 'sized': fixture_env['SOURCE_SCOPE_SIZED']='1'
    else: fixture_env.pop('SOURCE_SCOPE_SIZED',None)
    result=subprocess.run([str(binary)],capture_output=True,text=True,env=fixture_env)
    (out / ('result-'+layout+'.log')).write_text(result.stdout+result.stderr)
    result.check_returncode()
    print(layout+': '+result.stdout.strip())
print('Named typed heap walk: ASan/UBSan passed')
