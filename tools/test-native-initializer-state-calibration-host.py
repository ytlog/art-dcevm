#!/usr/bin/env python3
"""Run actual initialization-state JNI producer with independent boundary declarations."""
import ast
import os
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
cpp=root/'artdcevm/src/main/cpp'
out=root/'build/evidence/host/native-initializer-state';out.mkdir(parents=True,exist_ok=True)
fixture=(root/'tests/host/native_source_commit_test.cc').read_text().split('int main(){')[0]
# Keep only the shared public metadata boundary, not unrelated transaction
# callbacks which this producer test does not execute.
for name in ('MethodLeaf','Retired','PublishRetirement','ReflectiveReady','ReflectiveUpdate'):
    lines=[line for line in fixture.splitlines() if line.startswith(('void* '+name+'(', 'bool '+name+'(', 'void '+name+'('))]
    assert len(lines)==1
    fixture=fixture.replace(lines[0]+'\n','')
fixture=fixture.replace('bool reflection_ready=true;\n','')
(out/'native_initializer_state_fixture.inc').write_text(fixture)
tree=ast.parse((root/'tools/test-native-source-commit-host.py').read_text())
sources=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='sources' for t in n.targets))
sources.append('native_initializer_state_calibration')
jdk=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
binary=out/'native_initializer_state_calibration_test'
command=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(out),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform),str(root/'tests/host/native_initializer_state_calibration_test.cc')]
command += [str(cpp/'platform/common'/(s+'.cc')) for s in sources]
command += [str(cpp/'evolution/dex_schema.cc')]
if platform=='darwin':command += ['-Wl,-pagezero_size,0x10000']
command += ['-o',str(binary)]
subprocess.run(command,check=True)
result=subprocess.run([str(binary)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
(out/'result.log').write_text(result.stdout+result.stderr);result.check_returncode();print(result.stdout.strip())
