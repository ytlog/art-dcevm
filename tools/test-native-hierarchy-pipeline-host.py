#!/usr/bin/env python3
"""Exercise actual incoming cache/DEX mapping, calibrated metadata/root/card services; native resolver and final runtime leaves are explicit fixtures."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import struct
parser=argparse.ArgumentParser();parser.add_argument('--compile-only',action='store_true');args=parser.parse_args()
root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
jdk = Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'], text=True).strip())
platform = 'darwin' if os.uname().sysname == 'Darwin' else 'linux'
out = root / 'build/evidence/host/native-hierarchy-pipeline'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'native_hierarchy_pipeline_test'
fixture=(root/'tests/host/native_source_commit_test.cc').read_text().split('int main(){')[0].replace('std::array<unsigned char,200>', 'std::array<unsigned char,512>')
(out/'native_commit_fixture.inc').write_text(fixture)
setup=(root/'tests/host/source_handle_roots_test.cc').read_text()
(out/'source_cache_walker_fixture.inc').write_text(setup.split('int main(){')[0].replace('class RootHooks final:', 'class RootHooks:'))
(out/'source_cache_calibration_main.inc').write_text('int main(){'+setup.split('int main(){')[1].split(' struct Use{')[0].replace('RootHooks hooks;','SourceHooks hooks;'))
sources = ['native_hierarchy_private_ledger', 'native_source_dex_registry', 'native_source_class_loading', 'native_hierarchy_session', 'native_class_lookup', 'native_hierarchy_source_checks', 'native_hierarchy_graph', 'native_hierarchy_allocator', 'native_hierarchy_receiver', 'rooted_hierarchy_allocator', 'native_hierarchy_cache','native_cache_semantic', 'native_hierarchy_source_cache', 'source_handle_roots', 'source_allocation_transaction', 'native_source_commit', 'native_initialized_transfer', 'modern_source_commit', 'source_commit',
           'modern_field_metadata', 'native_interface_graph', 'modern_field_storage', 'field_copy', 'prepared_field_copy',
           'exclusive_owned_roots', 'native_cache_service', 'modern_method_services',
           'structural_methods', 'method_identity_calibration', 'object_replacement_map']
command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O0', '-Wall', '-Wextra', '-Werror',
           '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I', str(cpp), '-I', str(out),
           '-I', str(jdk / 'include'), '-I', str(jdk / 'include' / platform),
           str(root / 'tests/host/native_hierarchy_pipeline_test.cc'), str(root / 'tests/host/native_hierarchy_pipeline_stop_boundary.cc')]
command += [str(cpp / 'platform/common' / (s + '.cc')) for s in sources]
command += [str(cpp / 'evolution/dex_schema.cc')]
# Compressed-root fixtures need a real heap below 4 GiB. Mach-O otherwise
# reserves that entire address range as __PAGEZERO. Keep the null guard intact.
if platform == 'darwin':
    command += ['-Wl,-pagezero_size,0x10000']
fixture_path=Path(os.environ['SOURCE_DEX_FIXTURE']) if 'SOURCE_DEX_FIXTURE' in os.environ else root/'app/build/generated/patchAssets/hier-parent/com/github/ytlog/artdcevm/fixture/HierarchySubject.dex'
if not fixture_path.is_file(): raise SystemExit('Set SOURCE_DEX_FIXTURE to an existing verified SchemaSubject.dex fixture')
# Independent immutable DEX for every public parent/interface/boot Class.
sys.path.insert(0,str(root/'tests/host'))
from schema_run_state_dex_fixture import write_graph_fixture
raw=fixture_path.read_bytes();word=lambda at:struct.unpack_from('<I',raw,at)[0]
type_index=word(word(100));string_index=word(word(68)+4*type_index);offset=word(word(60)+4*string_index)
while raw[offset]&128:offset+=1
offset+=1;descriptor=raw[offset:raw.index(0,offset)].decode();assert descriptor.startswith('L') and '/' in descriptor
graph_fixture=write_graph_fixture(out/'defining-graph',descriptor[:descriptor.rfind('/')+1])
command += ['-DPIPELINE_GRAPH_DEX_FIXTURE='+chr(34)+str(graph_fixture)+chr(34)]
command += ['-DPIPELINE_DEX_FIXTURE='+chr(34)+str(fixture_path)+chr(34),'-o',str(binary)]
subprocess.run(command, check=True)
if args.compile_only:
    print('Source handle roots link PASS; execution pending');raise SystemExit(0)
# Each execution covers both independently bound plain and length FindClass
# leaves; vary the original source handle-scope declaration as well.
for layout in ('legacy','sized'):
    fixture_env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
    if layout=='sized': fixture_env['SOURCE_SCOPE_SIZED']='1'
    else: fixture_env.pop('SOURCE_SCOPE_SIZED',None)
    result=subprocess.run([str(binary)],capture_output=True,text=True,env=fixture_env,timeout=30)
    (out/('result-'+layout+'.log')).write_text(result.stdout+result.stderr)
    if layout=='legacy': (out/'result.log').write_text(result.stdout+result.stderr)
    result.check_returncode()
    print(layout+' (plain + length FindClass): '+result.stdout.strip())
print('Native DEX registry owner: ASan/UBSan passed' if os.environ.get('SOURCE_REGISTRY_ONLY') else 'Source cache native adapter: ASan/UBSan passed')
