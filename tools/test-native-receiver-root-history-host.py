#!/usr/bin/env python3
"""Concrete typed root bounds with actual calibrated EntrySource and FieldMetadata."""
import os, pathlib, subprocess
root=pathlib.Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp'
jdk=pathlib.Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip());platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
out=root/'build/evidence/host/native-receiver-root-history';out.mkdir(parents=True,exist_ok=True)
(out/'receiver_entry_metadata_fixture.inc').write_text((root/'tests/host/proven_field_declaration_test.cc').read_text().split('int main(){')[0].replace('if(c.self!=&self||c.env!=&env||c.vm!=&vm||!root||!out)return false;*out=root;return true;', 'return HistoryStrong(c,root,out);').replace('namespace artdcevm::native_interface_graph {\nbool Calibrate(const runnable_jni::Context&,jobject,const modern_field_metadata::FieldMetadata&,std::string*){std::abort();}\n}', ''))
cmd=[os.environ.get('CXX','clang++'),'-std=c++17','-DARTDCEVM_TESTING','-g','-O1','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(out),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform),str(root/'tests/host/native_receiver_root_history_test.cc')]
cmd += [str(cpp/'platform/common'/f'{x}.cc') for x in ['native_receiver_entry_source','native_receiver_class_source','native_receiver_relations','native_receiver_guard','native_receiver_frame','native_receiver_bytecode','retired_method_registry','modern_field_metadata','native_interface_graph','method_identity_calibration']]
cmd += [str(cpp/'evolution/dex_schema.cc'),'-Wl,-dead_strip' if platform=='darwin' else '-Wl,--gc-sections']
if platform=='darwin':cmd += ['-Wl,-pagezero_size,0x10000']
cmd += ['-o',str(out/'test')];subprocess.run(cmd,check=True)
for family,args in [('named-thread',[]),('inline-modern',['inline-modern'])]:
 r=subprocess.run([str(out/'test'),*args],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));(out/f'result-{family}.log').write_text(r.stdout+r.stderr);r.check_returncode();print(f'{family}: {r.stdout.strip()}')
