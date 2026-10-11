#!/usr/bin/env python3
"""Actual Boot AOT frame and concrete dependency owner through a matching full transaction."""
from pathlib import Path
import sys
import subprocess
baseline=Path(__file__).with_name('test-native-schema-run-state-host.py').resolve()
# Reuse the verified ART/JNI leaf fixture composition; execute only its ordinary
# generation prelude, then compile the separate aggregate success scenario.
namespace={'__file__':str(baseline)}
exec(compile(baseline.read_text().split('fixture=Path',1)[0],str(baseline),'exec'),namespace)
root=namespace['root'];cpp=namespace['cpp'];out=namespace['out'];java=namespace['java'];platform=namespace['platform'];sources=namespace['sources']
for name in ['native_managed_stack_source','native_jni_exception_calibration','native_exception_entry_ownership','modern_method_guards','modern_field_guards','modern_method_handle_guards','contextual_field_id','modern_final_field_guard','native_receiver_guard','retired_field_registry','retired_method_registry','native_receiver_class_source','native_receiver_entry_source']:
 if name not in sources:sources.append(name)
sys.path.insert(0,str(root/'tests/host'))
from schema_run_state_dex_fixture import write_fixture,write_graph_fixture
from schema_run_state_boot_dex_fixture import write_boot_fixture
import struct,hashlib,zlib
before=write_fixture(out/'matched-dex',True);after=write_fixture(out/'matched-dex',False);graph_dex=write_graph_fixture(out/'matched-dex',typed_probe=True)
# Same original superclass: this is member schema evolution, not prefix-changing hierarchy.
b=bytearray(after.read_bytes());types_at=struct.unpack_from('<I',b,68)[0];strings_at=struct.unpack_from('<I',b,60)[0]
base_index=None
for i in range(struct.unpack_from('<I',b,64)[0]):
 si=struct.unpack_from('<I',b,types_at+4*i)[0];at=struct.unpack_from('<I',b,strings_at+4*si)[0]+1;end=b.index(0,at)
 if b[at:end].decode().endswith('HierarchyBase;'):base_index=i
assert base_index is not None
struct.pack_into('<I',b,struct.unpack_from('<I',b,100)[0]+8,base_index);b[12:32]=hashlib.sha1(b[32:]).digest();struct.pack_into('<I',b,8,zlib.adler32(b[12:]));after.write_bytes(b)
boot_dex=write_boot_fixture(out/'matched-boot')
from managed_upcall_inline_dex_fixture import write as write_inline_fixture
write_inline_fixture(out/'matched-inline')
setup=(root/'tests/host/native_schema_run_state_test.cc').read_text().split('  // Additional actual owners',1)[1].split('  if(!cache_role_fixture.Prepare(',1)[0]
(out/'aggregate_owner_setup.inc').write_text(('  // Additional actual owners'+setup).replace('OwnerHooks owner_hooks;','BootHooks owner_hooks;').replace('whole_process_frames::Service scanner;','boot_frame_binding=true;whole_process_frames::Service scanner;').replace('BindGeometryOnly(code_info_view::SourceFamily::kAndroid13','Bind(code_info_view::SourceFamily::kAndroid14'))
# A moving-GC leaf must also update the actual Field/Method GcRoot<Class>
# members, not just the native TLS handles. This is the named lookup boundary.
prefix=(out/'pipeline_fixture.inc').read_text().replace('OriginalRoots(self,&mover,0);','OriginalRoots(self,&mover,0);for(auto* field:new_fields)field->declaring_class=uint32_t(uintptr_t(relocated_new));for(size_t n=0;n<3;++n)reinterpret_cast<abi::arm64::android12::Method12*>(uintptr_t(new_class->methods)+8+32*n)->declaring_class=uint32_t(uintptr_t(relocated_new));')
prefix=prefix.replace('HierarchyInserted;','HierarchyBase;')
(out/'pipeline_fixture.inc').write_text(prefix)
# HandleScopeVisitRoots is also an original GC visitor over a Native thread.
# Preserve actual TLS/root traversal and accept the fixture's independently
# calibrated Native state while StartGC waits for the preceding collector.
walker=out/'source_cache_walker_fixture.inc'
walker.write_text(walker.read_text().replace('assert(self==thread.data()&&flags==0&&Read32(self)==0);','assert(self==thread.data()&&flags==0&&(Read32(self)==0||Read32(self)==92u<<24));'))

native=(out/'native_commit_fixture.inc').read_text();native=native.replace('void ForEach(void* receiver,','void(*boot_enumeration)(void*,void(*)(void*,void*),void*)=nullptr;\nvoid ForEach(void* receiver,').replace('assert(receiver==&thread_list&&stopped&&locked);action(thread.data(),data);', 'assert(receiver==&thread_list&&stopped&&locked);if(boot_enumeration){boot_enumeration(receiver,action,data);return;}action(thread.data(),data);')
(out/'native_commit_fixture.inc').write_text(native)
(out/'pipeline_setup.inc').write_text((out/'pipeline_setup.inc').read_text().replace('d.schema->superclass==pkg+"HierarchyInserted;"','d.schema->superclass==pkg+"HierarchyBase;"'))
import importlib.util,os
helper=Path(__file__).with_name('prepare-native-upcall-elf-fixture.py')
spec=importlib.util.spec_from_file_location('upcall_real_elf',helper);elfhelper=importlib.util.module_from_spec(spec);spec.loader.exec_module(elfhelper)
elf=elfhelper.prepare(out/'upcall-real-elf');elfobj=out/'upcall-real-elf'/'mapped-elf.o'
subprocess.run([os.environ.get('CC','clang'),'-x','c','-std=c11','-g','-O0','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(root/'tests/host'),'-c',str(root/'tests/host/native_upcall_elf_extent_fixture.cc'),'-o',str(elfobj)],check=True)
(out/'shadowhook.h').write_text('extern "C" void* shadowhook_dlopen(const char*);\nextern "C" void* shadowhook_dlsym(void*,const char*);\nextern "C" void shadowhook_dlclose(void*);\n')
common=[namespace['os'].environ.get('CXX','clang++'),'-std=c++17','-g','-O0','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(out),'-I',str(root/'tests/host'),'-I',str(java/'include'),'-I',str(java/'include'/platform),'-DRUN_STATE_BOOT_DEX_FIXTURE=\"'+str(boot_dex)+'\"','-DPIPELINE_DEX_FIXTURE="'+str(after)+'"','-DRUN_STATE_BEFORE_DEX_FIXTURE="'+str(before)+'"','-DRUN_STATE_GRAPH_DEX_FIXTURE="'+str(graph_dex)+'"']
if os.environ.get('ART_DCEVM_INLINE_UPCALL')=='1':common+=['-DWHOLE_INLINE_FIXTURE','-DINLINE_DEX_DIR="'+str(out/'matched-inline')+'"']
if os.environ.get('ART_DCEVM_WHOLE_UPCALL')=='1':common+=['-DWHOLE_UPCALL_FIXTURE','-DUPCALL_ELF_FIXTURE="'+str(elf)+'"']
units=[root/'tests/host/native_schema_run_state_boot_test.cc']+[cpp/'platform/common'/(s+'.cc') for s in sources]+[cpp/'evolution/dex_schema.cc',cpp/'evolution/migration_plan.cc',cpp/'platform/common/schema_snapshot.cc']
units.append(cpp/'support/named_symbol_extent.cc')
# Keep compiled modules cached while the separate real boundary scenario evolves.
cache=baseline.read_text().split("headers=b''.join",1)[1].split("exe=out/'",1)[0]
cache=cache.replace("source.read_bytes()+headers+", "source.read_bytes()+(headers if source.is_relative_to(root/'tests') else b''.join(p.read_bytes() for p in sorted(cpp.rglob('*.h'))))+")
exec(compile(("headers=b''.join"+cache).replace("objects=out", "headers+=b''.join(p.read_bytes() for p in (root/'tests/host').glob('*fixture.h'))\nobjects=out"),str(baseline),'exec'),dict(namespace,common=common,units=units))
# The generated cache code holds its own namespace; collect explicit object paths.
compiled=[str(out/'objects'/(p.stem+'.o')) for p in units]
exe=out/'native_schema_run_state_boot_test';link=common+compiled+[str(elfobj)]
if platform=='darwin':link+=['-Wl,-pagezero_size,0x10000']
subprocess.run(link+['-o',str(exe)],check=True)
reports=[]
for case in ['', 'pc', 'value', 'mask', 'cursor', 'body', 'prefix', 'declaring']:
 r=subprocess.run([str(exe)]+([case] if case else []),text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 reports.append(r.stdout+r.stderr)
 (out/'boot-result.log').write_text('\n'.join(reports))
 r.check_returncode();print(r.stdout.strip())
