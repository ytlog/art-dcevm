#!/usr/bin/env python3
"""Compose actual waiting stop and compiled frame proof, only ART leaves doubled."""
import os
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp'
out=root/'build/evidence/host/compiled-frame-waiting';out.mkdir(parents=True,exist_ok=True)
source=(root/'tests/host/native_source_waiting_scope_test.cc').read_text().split('int main(int argc,char**argv){',1)[0]
# Keep the actual source-stop fixture's native leaf state separate from the
# physical compiled-frame fixture; neither production ownership API is replaced.
source=source.replace('namespace {','namespace waiting_fixture {')
(out/'waiting_scope_fixture.inc').write_text(source)
active=(root/'tests/host/active_frame_recovery_proof_test.cc').read_text()
# Native ART's <=16 register decoder uses inline storage. Keep the host leaf
# equivalent instead of the older fixture's allocating std::vector oracle.
active=active.replace('std::vector<decoded_frame_recovery::Location> locations(map->count);', 'std::array<decoded_frame_recovery::Location,16> locations{};assert(map->count<=locations.size());')
(out/'active_compiled_fixture.inc').write_text(active)
proof=(root/'tests/host/frame_family_calibration_test.cc').read_text().split('int main(){',1)[0].replace('#include "active_frame_recovery_proof_test.cc"','#include "active_compiled_fixture.inc"')
(out/'compiled_frame_proof_fixture.inc').write_text(proof)
java=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
exe=out/'compiled-frame-waiting'
common=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-rtti','-I',str(cpp),'-I',str(out),'-I',str(root/'tests/host'),'-I',str(java/'include'),'-I',str(java/'include'/platform)]
units=[root/'tests/host/compiled_frame_waiting_inspection_test.cc']+[cpp/'platform/common'/(n+'.cc') for n in ('native_source_stop','native_source_class_loading','method_identity_calibration')]
subprocess.run(common+list(map(str,units))+['-o',str(exe)],check=True)
r=subprocess.run([str(exe)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
