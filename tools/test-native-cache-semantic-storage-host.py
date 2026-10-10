#!/usr/bin/env python3
"""Read-only DexCache oracle with actual metadata and original JNI/JVMTI doubles.
Host coverage is not evidence of device hidden-field availability.
"""
import os,subprocess,json,hashlib,sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
root=Path(__file__).resolve().parents[1]
jdk=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
declaring=sys.argv[1:]==['--field-declaring']
if sys.argv[1:] and not declaring:raise SystemExit('usage: test-native-cache-semantic-storage-host.py [--field-declaring]')
case='field-declaring-metadata' if declaring else 'native-cache-semantic-storage'
test='field_declaring_metadata_test.cc' if declaring else 'native_cache_semantic_storage_test.cc'
out=root/'build/evidence/host'/case;out.mkdir(parents=True,exist_ok=True)
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
cmd=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(out),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform),str(root/'tests/host'/test)]
cmd += [str(cpp/'platform/common'/(s+'.cc')) for s in ['contextual_field_id','retired_field_registry','retired_method_registry','source_handle_roots','modern_field_storage','exclusive_owned_roots','native_cache_service','object_replacement_map','field_copy','prepared_field_copy','native_cache_public_fields','native_cache_semantic','native_cache_semantic_storage','modern_field_metadata','method_identity_calibration','native_interface_graph','native_schema_policy','native_method_evolution_policy','native_abstract_instances','native_schema_binding','native_method_table']]
cmd += [str(cpp/'evolution/dex_schema.cc'),'-Wl,-dead_strip' if platform=='darwin' else '-Wl,--gc-sections','-o',str(out/'test')]
if platform=='darwin':cmd+=['-Wl,-pagezero_size,0x10000']
# Cache independent host TUs by exact flags/source/header contents; this keeps
# iterative callback tests fast without accepting stale production objects.
headers=hashlib.sha256()
for path in sorted(cpp.rglob('*.h')):headers.update(str(path).encode());headers.update(path.read_bytes())
headers.update((out/'native_commit_fixture.inc').read_bytes())
first=next(i for i,arg in enumerate(cmd) if arg.endswith('.cc'))
base=cmd[:first];sources=[Path(arg) for arg in cmd[first:] if arg.endswith('.cc')]
objects=out/'objects';objects.mkdir(exist_ok=True)
def compile_one(source):
    key=hashlib.sha256(headers.digest()+json.dumps(base).encode()+source.read_bytes()).hexdigest()
    target=objects/(source.stem+'-'+key+'.o')
    if not target.exists():
        command=base+['-c',str(source),'-o',str(target)]
        result=subprocess.run(command,capture_output=True,text=True)
        (objects/(source.stem+'.log')).write_text(result.stdout+result.stderr)
        if result.returncode:print(result.stderr)
        result.check_returncode()
    return str(target)
with ThreadPoolExecutor(max_workers=2) as pool:compiled=list(pool.map(compile_one,sources))
link=base+compiled+['-Wl,-dead_strip' if platform=='darwin' else '-Wl,--gc-sections','-o',str(out/'test')]
if platform=='darwin':link+=['-Wl,-pagezero_size,0x10000']
(out/'link-command.json').write_text(json.dumps(link,indent=2))
subprocess.run(link,check=True)
r=subprocess.run([str(out/'test')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
