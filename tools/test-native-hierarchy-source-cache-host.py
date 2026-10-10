#!/usr/bin/env python3
"""Exercise actual incoming cache/DEX mapping, calibrated metadata/root/card services; native resolver and final runtime leaves are explicit fixtures."""
import argparse
import os
from pathlib import Path
import subprocess
from host_tu_cache import compile_cached
parser=argparse.ArgumentParser();parser.add_argument('--compile-only',action='store_true');args=parser.parse_args()
root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
jdk = Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'], text=True).strip())
platform = 'darwin' if os.uname().sysname == 'Darwin' else 'linux'
out = root / 'build/evidence/host/native-hierarchy-source-cache'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'native_hierarchy_source_cache_test'
fixture=(root/'tests/host/native_source_commit_test.cc').read_text().split('int main(){')[0].replace('std::array<unsigned char,200>', 'std::array<unsigned char,512>')
(out/'native_commit_fixture.inc').write_text(fixture)
setup=(root/'tests/host/source_handle_roots_test.cc').read_text()
(out/'source_cache_walker_fixture.inc').write_text(setup.split('int main(){')[0].replace('class RootHooks final:', 'class RootHooks:'))
(out/'source_cache_calibration_main.inc').write_text('int main(){'+setup.split('int main(){')[1].split(' struct Use{')[0].replace('RootHooks hooks;','SourceHooks hooks;'))
# Link the actual Ledger and its source-stop dependencies, including allocator
# discard authority; no fabricated Ledger implementation in this fixture.
sources = ['native_hierarchy_graph', 'native_hierarchy_allocator', 'rooted_hierarchy_allocator', 'native_hierarchy_cache', 'native_cache_semantic', 'native_hierarchy_source_cache', 'source_handle_roots', 'source_allocation_transaction', 'native_source_commit', 'native_initialized_transfer', 'modern_source_commit', 'source_commit', 'modern_field_metadata', 'native_interface_graph', 'modern_field_storage', 'field_copy', 'prepared_field_copy', 'exclusive_owned_roots', 'native_cache_service', 'modern_method_services', 'structural_methods', 'method_identity_calibration', 'object_replacement_map', 'native_hierarchy_private_ledger', 'native_source_dex_registry', 'native_source_class_loading', 'native_source_stop']
command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
           '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I', str(cpp), '-I', str(out),
           '-I', str(jdk / 'include'), '-I', str(jdk / 'include' / platform)]
units = [root / 'tests/host/native_hierarchy_source_cache_test.cc']
units += [cpp / 'platform/common' / (s + '.cc') for s in sources]
units += [cpp / 'evolution/dex_schema.cc']
# Compressed-root fixtures need a real heap below 4 GiB. Mach-O otherwise
# reserves that entire address range as __PAGEZERO. Keep the null guard intact.
fixture_path=Path(os.environ['SOURCE_DEX_FIXTURE']) if 'SOURCE_DEX_FIXTURE' in os.environ else root/'app/build/generated/patchAssets/metadata/com/github/ytlog/artdcevm/fixture/SchemaSubject.dex'
if not fixture_path.is_file(): raise SystemExit('Set SOURCE_DEX_FIXTURE to an existing verified SchemaSubject.dex fixture')
command += ['-DSOURCE_DEX_FIXTURE='+chr(34)+str(fixture_path)+chr(34)]
link_flags = ['-Wl,-pagezero_size,0x10000'] if platform == 'darwin' else []
objects = compile_cached(command, units, out/'objects')
subprocess.run(command + objects + link_flags + ['-o', str(binary)], check=True)
if args.compile_only:
    print('Source handle roots link PASS; execution pending');raise SystemExit(0)
for layout in ('legacy','sized'):
    fixture_env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
    if layout=='sized':fixture_env['SOURCE_SCOPE_SIZED']='1'
    else:fixture_env.pop('SOURCE_SCOPE_SIZED',None)
    result=subprocess.run([str(binary)],capture_output=True,text=True,env=fixture_env,timeout=30)
    (out/('result-'+layout+'.log')).write_text(result.stdout+result.stderr)
    if layout=='legacy':(out/'result.log').write_text(result.stdout+result.stderr)
    result.check_returncode();print(layout+': '+result.stdout.strip())
print('Source cache native adapter: ASan/UBSan passed')
