#!/usr/bin/env python3
"""Actual Catalog/Rooted/SourcePool consumer, with platform snapshot failure injection."""
import os,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp'
out=root/'build/evidence/host/boot-root-import';out.mkdir(parents=True,exist_ok=True)
java=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
fixture=(root/'tests/host/native_source_commit_test.cc').read_text().split('int main(){')[0].replace('std::array<unsigned char,200>','std::array<unsigned char,512>')
old='abi::arm64::android11::ReleaseObjectPointer Decode(const void*,jobject p){return {uintptr_t(p)};}'
assert fixture.count(old)==1
fixture=fixture.replace(old,'void(*import_decoder_observer)()=nullptr;\nabi::arm64::android11::ReleaseObjectPointer Decode(const void*,jobject p){if(import_decoder_observer)import_decoder_observer();return {uintptr_t(p)};}')
(out/'native_commit_fixture.inc').write_text(fixture)
source=(root/'tests/host/source_handle_roots_test.cc').read_text()
old='if(source_probe)assert(source_probe(context,metadata,s,*data.fields));'
assert source.count(old)==1
source=source.replace(old,'if(source_probe)assert(source_probe(pair_context,metadata,*data.pair,*data.fields));')
(out/'import_source_roots.inc').write_text(source)
# Fault only the ordinary full-snapshot OS capture boundary. All successful
# reads, exact source checks, native scopes and collector leaves use production.
mapping=(cpp/'platform/common/method_identity_calibration.cc').read_text()
old='ScopedReadableMappings::ScopedReadableMappings() noexcept:previous_(active_mappings){'
assert mapping.count(old)==1
mapping='extern bool import_snapshot_failure;\n'+mapping.replace(old,old+'\n if(::import_snapshot_failure){active_mappings=this;return;}')
(out/'mapping_failure_boundary.cc').write_text(mapping)
sources=['source_handle_roots','source_allocation_transaction','native_source_commit','native_initialized_transfer','modern_source_commit','source_commit','modern_field_metadata','native_interface_graph','modern_field_storage','field_copy','prepared_field_copy','exclusive_owned_roots','native_cache_service','modern_method_services','structural_methods','object_replacement_map','native_boot_method_catalog']
args=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(out),'-I',str(java/'include'),'-I',str(java/'include'/platform)]
args+=[str(root/'tests/host/native_boot_root_import_test.cc'),str(out/'mapping_failure_boundary.cc')]+[str(cpp/'platform/common'/(s+'.cc')) for s in sources]+[str(cpp/'evolution/dex_schema.cc')]
if platform=='darwin':args+=['-Wl,-pagezero_size,0x10000']
exe=out/'import';args+=['-o',str(exe)]
r=subprocess.run(args,capture_output=True,text=True);(out/'compile.log').write_text(r.stdout+r.stderr);r.check_returncode()
for layout in ['legacy','sized']:
 env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
 if layout=='sized':env['SOURCE_SCOPE_SIZED']='1'
 else:env.pop('SOURCE_SCOPE_SIZED',None)
 r=subprocess.run([str(exe)],capture_output=True,text=True,env=env);(out/(layout+'.log')).write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
