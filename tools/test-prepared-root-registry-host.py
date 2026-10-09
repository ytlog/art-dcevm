#!/usr/bin/env python3
import os,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp';out=root/'build/evidence/host/prepared-root-registry';out.mkdir(parents=True,exist_ok=True)
jdk=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip());platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
command=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform),str(root/'tests/host/prepared_root_registry_test.cc'),'-o',str(out/'test')]
subprocess.run(command,check=True);r=subprocess.run([str(out/'test')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
