#!/usr/bin/env python3
"""Actual RunState and public guard proof with matching independent StandardDex."""
from pathlib import Path
import sys
import subprocess
baseline=Path(__file__).with_name('test-native-schema-run-state-host.py').resolve()
# Reuse the verified ART/JNI leaf fixture composition; execute only its ordinary
# generation prelude, then compile the separate aggregate success scenario.
namespace={'__file__':str(baseline)}
exec(compile(baseline.read_text().split('fixture=Path',1)[0],str(baseline),'exec'),namespace)
root=namespace['root'];cpp=namespace['cpp'];out=namespace['out'];java=namespace['java'];platform=namespace['platform'];sources=namespace['sources']
sys.path.insert(0,str(root/'tests/host'))
from schema_run_state_dex_fixture import write_fixture
before=write_fixture(out/'matched-dex',True);after=write_fixture(out/'matched-dex',False)
setup=(root/'tests/host/native_schema_run_state_test.cc').read_text().split('  // Additional actual owners',1)[1].split('  auto begin=',1)[0]
(out/'aggregate_owner_setup.inc').write_text(('  // Additional actual owners'+setup).replace('OwnerHooks owner_hooks;','MatchedHooks owner_hooks;'))
# A moving-GC leaf must also update the actual Field/Method GcRoot<Class>
# members, not just the native TLS handles. This is the named lookup boundary.
prefix=(out/'pipeline_fixture.inc').read_text().replace('OriginalRoots(self,&mover,0);','OriginalRoots(self,&mover,0);for(auto* field:new_fields)field->declaring_class=uint32_t(uintptr_t(relocated_new));for(size_t n=0;n<3;++n)reinterpret_cast<abi::arm64::android12::Method12*>(uintptr_t(new_class->methods)+8+32*n)->declaring_class=uint32_t(uintptr_t(relocated_new));')
(out/'pipeline_fixture.inc').write_text(prefix)
common=[namespace['os'].environ.get('CXX','clang++'),'-std=c++17','-g','-O0','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(out),'-I',str(root/'tests/host'),'-I',str(java/'include'),'-I',str(java/'include'/platform),'-DPIPELINE_DEX_FIXTURE="'+str(after)+'"','-DRUN_STATE_BEFORE_DEX_FIXTURE="'+str(before)+'"']
units=[root/'tests/host/native_schema_run_state_success_test.cc']+[cpp/'platform/common'/(s+'.cc') for s in sources]+[cpp/'evolution/dex_schema.cc',cpp/'evolution/migration_plan.cc',cpp/'platform/common/schema_snapshot.cc']
# Keep compiled modules cached while the separate real boundary scenario evolves.
cache=baseline.read_text().split("headers=b''.join",1)[1].split("exe=out/'",1)[0]
exec(compile(("headers=b''.join"+cache).replace("objects=out", "headers+=(root/'tests/host/native_schema_run_state_public_fixture.h').read_bytes()\nobjects=out"),str(baseline),'exec'),dict(namespace,common=common,units=units))
# The generated cache code holds its own namespace; collect explicit object paths.
compiled=[str(out/'objects'/(p.stem+'.o')) for p in units]
exe=out/'native_schema_run_state_success_test';link=common+compiled
if platform=='darwin':link+=['-Wl,-pagezero_size,0x10000']
subprocess.run(link+['-o',str(exe)],check=True)
r=subprocess.run([str(exe)],text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
(out/'success-result.log').write_text(r.stdout+r.stderr)
r.check_returncode();print(r.stdout.strip())
