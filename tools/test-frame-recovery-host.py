#!/usr/bin/env python3
"""Check fixed ART map counts against real standard/CDEX code-item storage."""
import os
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
cpp=root/'artdcevm/src/main/cpp'
java_home=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
out=root/'build/evidence/host/frame-recovery';out.mkdir(parents=True,exist_ok=True)
sanitizer=os.environ.get('ARTDCEVM_HOST_SANITIZER','address,undefined')
binary=out/('frame-recovery-test-'+sanitizer.replace(',','-'))
command=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fno-rtti','-fsanitize='+sanitizer,'-fno-omit-frame-pointer','-I',str(cpp),'-I',str(java_home/'include'),'-I',str(java_home/'include'/platform),str(root/'tests/host/frame_recovery_test.cc'),str(cpp/'platform/android13/frame_recovery.cc'),str(cpp/'platform/android13/stack_access.cc'),str(cpp/'platform/android13/device_profile.cc'),'-o',str(binary)]
if os.uname().sysname=='Darwin':command+=['-Wl,-pagezero_size,0x10000']
else:command+=['-rdynamic','-ldl']
subprocess.run(command,check=True)
result=subprocess.run([str(binary)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
(out/(binary.name+'.log')).write_text(result.stdout+result.stderr)
result.check_returncode();print(result.stdout.strip());print('frame recovery: passed ('+sanitizer+')')
