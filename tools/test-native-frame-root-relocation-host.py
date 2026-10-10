#!/usr/bin/env python3
"""Actual SourceStop + Runtime-root observer; explicit unit ART leaf boundaries."""
import os
import subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp'
jdk=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
out=root/'build/evidence/host/native-frame-root-relocation';out.mkdir(parents=True,exist_ok=True)
command=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-ffunction-sections','-fdata-sections','-I',str(cpp),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform),str(root/'tests/host/native_frame_root_relocation_test.cc')]
# Exercise RootProvenance with the real Session implementation. The unrelated
# field/array Service in the same TU is exercised by its dedicated existing test.
command += [str(cpp/'platform/common'/(name+'.cc')) for name in ('native_frame_root_relocation','native_typed_heap_walk','method_identity_calibration','native_source_stop','native_source_class_loading','native_cache_service')]
command += ['-Wl,-dead_strip' if platform=='darwin' else '-Wl,--gc-sections','-o',str(out/'test')]
r=subprocess.run(command,capture_output=True,text=True);(out/'compile.log').write_text(r.stdout+r.stderr);r.check_returncode()
r=subprocess.run([str(out/'test')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
# Closed interfaces must not be forgeable by copying a real base subobject
# into a caller-owned implementation that merely reports success.
negative=out/'forge.cc'
negative.write_text('''#include "platform/common/native_frame_root_binding.h"
#include "platform/common/native_frame_root_relocation.h"
using namespace artdcevm;
struct Fake : whole_process_frames::RecaptureInterface {
 Fake(const whole_process_frames::RecaptureInterface& real):RecaptureInterface(real){}
 bool Recapture(bool)const noexcept override{return true;}
};
struct FakeRoots : native_frame_root_relocation::Interface {
 FakeRoots(const native_frame_root_relocation::Interface& real):Interface(real){}
};
''')
check=command[:command.index(str(root/'tests/host/native_frame_root_relocation_test.cc'))]+['-fsyntax-only',str(negative)]
r=subprocess.run(check,capture_output=True,text=True);(out/'forge-rejected.log').write_text(r.stdout+r.stderr)
assert r.returncode and r.stderr.count('deleted constructor')>=2, r.stderr
print('Copied-base recovery/recapture interface forgery rejected by actual compiler')
