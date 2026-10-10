#!/usr/bin/env python3
"""Exercise calibrated native commit adapter and dependencies as one transaction."""
import argparse
import os
import hashlib
import struct
import zlib
from pathlib import Path
import subprocess
parser=argparse.ArgumentParser();parser.add_argument('--syntax-only',action='store_true');parser.add_argument('--compile-only',action='store_true');parser.add_argument('--method-policy-root',type=Path);args=parser.parse_args()
root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
jdk = Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'], text=True).strip())
platform = 'darwin' if os.uname().sysname == 'Darwin' else 'linux'
out = root / 'build/evidence/host/native-receiver-class-window'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'native_receiver_class_window_test'
fixture = (root / 'tests/host/native_source_commit_test.cc').read_text()
assert fixture.count('int main(){') == 1
(out / 'native_commit_fixture.inc').write_text(fixture.split('int main(){')[0])
provider=(root/'tests/host/native_member_provider_test.cc').read_text().split('int main(int argc,char** argv){')[0]
(out/'provider_fixture.inc').write_text(provider)
sources = ['native_member_provider', 'native_method_table', 'native_structural_reflection', 'resolution_invalidation_plan', 'native_source_commit', 'native_initialized_transfer', 'modern_source_commit', 'source_commit',
           'modern_field_metadata', 'native_interface_graph', 'native_receiver_class_source', 'native_receiver_relations', 'modern_field_storage', 'field_copy', 'prepared_field_copy',
           'native_schema_policy','native_schema_binding','native_method_evolution_policy','source_handle_roots',
           'exclusive_owned_roots', 'native_cache_service', 'modern_method_services',
           'structural_methods', 'method_identity_calibration', 'object_replacement_map']
command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
           '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I', str(cpp), '-I', str(out),
           '-I',str(root/'tests/host'), '-I', str(jdk / 'include'), '-I', str(jdk / 'include' / platform),
           str(root / 'tests/host/native_receiver_class_window_test.cc')]
command += ['-I',str(args.method_policy_root)] if args.method_policy_root else []
command += [str((args.method_policy_root if args.method_policy_root and s=='native_method_evolution_policy' else cpp) / 'platform/common' / (s + '.cc')) for s in sources]
command += [str(cpp / s) for s in ['evolution/dex_schema.cc','evolution/migration_plan.cc']]
# Compressed-root fixtures need a real heap below 4 GiB. Mach-O otherwise
# reserves that entire address range as __PAGEZERO. Keep the null guard intact.
if platform == 'darwin' and not args.syntax_only:
    command += ['-Wl,-pagezero_size,0x10000']
command += ['-fsyntax-only'] if args.syntax_only else ['-o', str(binary)]
subprocess.run(command, check=True)
if args.syntax_only or args.compile_only:
    print('Native member production TU '+('syntax PASS' if args.syntax_only else 'link PASS; execution pending'));raise SystemExit(0)
result = subprocess.run([str(binary)], capture_output=True,text=True,
 env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
(out / 'result.log').write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('Actual ClassSource bounded read owner: ASan/UBSan passed')
