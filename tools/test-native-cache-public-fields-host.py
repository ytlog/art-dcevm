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
fixture=(root/'tests/host/native_source_commit_test.cc').read_text().split('int main(){')[0]
fixture=fixture.replace('std::array<unsigned char,200>', 'std::array<unsigned char,512>')
fixture=fixture.replace('JNIEnv env{&table};',
 'struct EnvOwner {JNIEnv env;void* self;void* vm;};EnvOwner env_owner{{&table},nullptr,nullptr};JNIEnv& env=env_owner.env;')
# Model the real executor-specific TLS restriction, independently from the
# actual source root service's calibrated Runnable/current/Env/VM proof.
fixture=fixture.replace('bool DecodeStrongGlobal(const runnable_jni::Context&,jobject p,void** out){if(!p||!out)',
 'bool host_executor_active=false;\nconst char* Route(){return "host-original-boundary";}\nbool DecodeStrongGlobal(const runnable_jni::Context&,jobject p,void** out){if(!host_executor_active||!p||!out)')
fixture=fixture.replace('bool ok=fn({current,e,&runtime},data);',
 'host_executor_active=true;bool ok=fn({current,e,&runtime},data);host_executor_active=false;')
(out/'native_commit_fixture.inc').write_text(fixture)
cpp=root/'artdcevm/src/main/cpp'
cmd=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(out),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform),str(root/'tests/host/native_cache_public_fields_test.cc')]
cmd += [str(cpp/'platform/common'/(s+'.cc')) for s in ['source_handle_roots','modern_field_storage','exclusive_owned_roots','native_cache_service','object_replacement_map','field_copy','prepared_field_copy','native_cache_public_fields','modern_field_metadata','method_identity_calibration','native_interface_graph','native_schema_policy','native_method_evolution_policy','native_abstract_instances','native_schema_binding','native_method_table']]
cmd += [str(cpp/'evolution/dex_schema.cc'),'-Wl,-dead_strip' if platform=='darwin' else '-Wl,--gc-sections','-o',str(out/'test')]
if platform=='darwin':cmd+=['-Wl,-pagezero_size,0x10000']
subprocess.run(cmd,check=True)
r=subprocess.run([str(out/'test')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
