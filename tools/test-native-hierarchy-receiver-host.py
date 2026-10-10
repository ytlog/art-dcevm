#!/usr/bin/env python3
"""Test additive receiver proof with explicitly mocked native metadata leaves."""
import os,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp';out=root/'build/evidence/host/native-hierarchy-receiver';out.mkdir(parents=True,exist_ok=True)
# Reuse the graph fixture's Class bytes and explicit metadata/IfTable leaf stubs.
fixture=(root/'tests/host/native_hierarchy_graph_test.cc').read_text().split('// Fake JNI implements',1)[0]
(out/'native_hierarchy_receiver_fixture.h').write_text(fixture)
jdk=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
command=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(out),'-I',str(jdk/'include'),'-I',str(jdk/'include'/('darwin' if os.uname().sysname=='Darwin' else 'linux')),str(root/'tests/host/native_hierarchy_receiver_test.cc'),'-o',str(out/'test')]
r=subprocess.run(command,capture_output=True,text=True);(out/'compile.log').write_text(r.stdout+r.stderr);r.check_returncode()
r=subprocess.run([str(out/'test')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
