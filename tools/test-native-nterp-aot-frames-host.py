#!/usr/bin/env python3
"""AOT exact named source owner and independent Nterp register-array proof."""
import os
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp';out=root/'build/evidence/host/nterp-aot-frames';out.mkdir(parents=True,exist_ok=True)
java=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip());platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
common=[os.environ.get('CXX','clang++'),'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fno-rtti','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(java/'include'),'-I',str(java/'include'/platform)]
for name in ('aot_frame_recovery_proof','native_nterp_frame_reader','native_nterp_frame_proof'):
 exe=out/name
 subprocess.run(common+[str(root/'tests/host'/(name+'_test.cc')),'-o',str(exe)],check=True)
 r=subprocess.run([str(exe)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
 (out/(name+'.log')).write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())

source=(root/'tests/host/native_source_waiting_scope_test.cc').read_text().split('int main(int argc,char**argv){',1)[0].replace('namespace {','namespace waiting_fixture {')
(out/'waiting_scope_fixture.inc').write_text(source)
for name in ('native_nterp_waiting_inspection','native_nterp_physical_waiting'):
 exe=out/name
 units=[root/'tests/host'/(name+'_test.cc')]+[cpp/'platform/common'/(n+'.cc') for n in ('native_source_stop','native_source_class_loading','method_identity_calibration')]
 subprocess.run(common+['-I',str(out),'-I',str(root/'tests/host')]+list(map(str,units))+['-o',str(exe)],check=True)
 r=subprocess.run([str(exe)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
 (out/(name+'.log')).write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
