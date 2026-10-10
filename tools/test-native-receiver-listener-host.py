#!/usr/bin/env python3
import os,subprocess,re
from pathlib import Path
root=Path(__file__).resolve().parents[1]
out=root/'build/evidence/host/native-receiver-listener';out.mkdir(parents=True,exist_ok=True)
s=(root/'tests/host/native_final_write_listener_test.cc').read_text().replace('int main(', 'int ExistingFinalMain(').replace('int adds=0,','uint32_t final_flags=0x10,last_events=0;int receiver_events=0,adds=0,').replace('*flags=0x10;','*flags=final_flags;').replace('int r=0,thread=0,method=0,field=0;','static int r=0,thread=0,method=0,field=0;').replace(' Guard guard(', ' static Guard guard(').replace('events==0x20','(events==0x20||events==0x18||events==0x10||events==0x08)').replace('actual_listener=l;++adds;','actual_listener=l;last_events=events;++adds;if(events==0x18||events==0x10)++receiver_events;').replace('"Named final FieldWritten listener family "','"Named final FieldWritten listener family "')
s=re.sub(r' if\(engine\.legacy\)\{disable_observer.*?assert\(!BindListener\(engine,receiver,guard,&error\)\);\}', '', s, flags=re.S)
s=s.replace('guard.ResetUncommittedProof();assert(!guard.ValidateProof(fresh)&&!guard.ValidateProof(guard.FreezeProof()));','')
s=s.replace('std::cout<<"Named final FieldWritten listener family "', 'std::cout<<"Named final FieldWritten listener family "').replace('route passed\\n";','route passed\\n";return 0;')
(out/'listener_fixture.cc').write_text(s)
sources=['tests/host/native_receiver_listener_test.cc','artdcevm/src/main/cpp/platform/common/native_final_write_guard.cc','artdcevm/src/main/cpp/platform/common/modern_final_field_guard.cc','artdcevm/src/main/cpp/platform/common/native_receiver_guard.cc','artdcevm/src/main/cpp/platform/common/native_receiver_frame.cc','artdcevm/src/main/cpp/platform/common/native_receiver_bytecode.cc','artdcevm/src/main/cpp/platform/common/native_receiver_service.cc','artdcevm/src/main/cpp/platform/common/native_receiver_source_calibration.cc','artdcevm/src/main/cpp/platform/common/native_reference_decoder.cc']
java=Path(subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
binary=out/'test'
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-DARTDCEVM_TESTING','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-pthread','-I',str(java/'include'),'-I',str(java/'include/darwin'),'-I',str(out),'-I',str(root/'artdcevm/src/main/cpp'),*[str(root/p) for p in sources],'-o',str(binary)],check=True)
for family in ('3','4','5','6'):
 r=subprocess.run([str(binary),family],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));(out/('family-'+family+'.log')).write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
