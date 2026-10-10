#!/usr/bin/env python3
"""Read-only DexCache oracle with actual metadata and original JNI/JVMTI doubles.
Host coverage is not evidence of device hidden-field availability.
"""
import os,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
jdk=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
out=root/'build/evidence/host/native-cache-public-fields';out.mkdir(parents=True,exist_ok=True)
fixture=(root/'tests/host/native_source_commit_test.cc').read_text();(out/'native_commit_fixture.inc').write_text(fixture.split('int main(){')[0])
cpp=root/'artdcevm/src/main/cpp'
cmd=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(out),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform),str(root/'tests/host/native_cache_public_fields_test.cc')]
cmd += [str(cpp/'platform/common'/(s+'.cc')) for s in ['native_cache_public_fields','modern_field_metadata','method_identity_calibration','native_interface_graph','native_schema_policy','native_method_evolution_policy','native_abstract_instances','native_schema_binding','native_method_table']]
cmd += [str(cpp/'evolution/dex_schema.cc'),'-Wl,-dead_strip' if platform=='darwin' else '-Wl,--gc-sections','-o',str(out/'test')]
if platform=='darwin':cmd+=['-Wl,-pagezero_size,0x10000']
subprocess.run(cmd,check=True)
r=subprocess.run([str(out/'test')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
