#!/usr/bin/env python3
"""Actual fresh metadata lookup/lazy factory/shared Store composition and counts."""
import os,subprocess,json,re
from pathlib import Path
root=Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp'
out=root/'build/evidence/host/native-declared-field-lookup';out.mkdir(parents=True,exist_ok=True)
java=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
fixture=(root/'tests/host/native_source_commit_test.cc').read_text().split('int main(){')[0].replace('std::array<unsigned char,200>','std::array<unsigned char,512>')
(out/'native_commit_fixture.inc').write_text(fixture)
provider=(root/'tests/host/native_member_provider_test.cc').read_text().split('int main(int argc,char** argv){')[0]
(out/'provider_fixture.inc').write_text(provider)
units=['native_declared_field_lookup','native_lazy_field_mirror','rooted_field_identities','modern_field_metadata','native_interface_graph','method_identity_calibration','source_handle_roots','modern_field_storage']
cmd=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(out),'-I',str(root/'tests/host'),'-I',str(java/'include'),'-I',str(java/'include'/platform),str(root/'tests/host/native_declared_field_lookup_test.cc')]+[str(cpp/'platform/common'/(name+'.cc')) for name in units]
cmd+=[str(cpp/'evolution/dex_schema.cc')]
cmd+=['-Wl,-dead_strip','-Wl,-pagezero_size,0x10000'] if platform=='darwin' else ['-Wl,--gc-sections']
cmd+=['-o',str(out/'test')]
r=subprocess.run(cmd,text=True,capture_output=True);(out/'compile.log').write_text(r.stdout+r.stderr);r.check_returncode()
r=subprocess.run([str(out/'test')],text=True,capture_output=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode()
match=re.search(r'Actual lazy\+Store Stage fields=(\d+) declaration-scans=(\d+) inspected-selected-fields=(\d+) mapping-snapshot-queries=(\d+) total-mapping-queries=(\d+) public-JNI-calls=(\d+) factory-calls=(\d+) native-read-JNI=(\d+)',r.stdout)
assert match
keys=['staged_fields','declaration_scans','inspected_selected_fields','mapping_snapshot_queries','total_mapping_queries','public_jni_calls','original_false_factory_calls','jni_within_native_reads']
counts=dict(zip(keys,map(int,match.groups())))
assert counts['staged_fields']==8 and counts['declaration_scans']==16 and counts['inspected_selected_fields']==40 and counts['original_false_factory_calls']==8 and not counts['jni_within_native_reads']
(out/'observation.json').write_text(json.dumps({'platform':platform,'scope':'actual metadata + lazy service + shared Store with named ART/JNI leaf doubles; not device timing', 'counts':counts,'moved_class_reread':True,'opaque_id_collision_namespaces':True},indent=2)+'\n')
print(r.stdout.strip())
