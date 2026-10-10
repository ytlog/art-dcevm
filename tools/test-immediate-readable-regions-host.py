#!/usr/bin/env python3
"""Exercise real platform VMA queries and bounded immediate native read views."""
import json,os,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp';out=root/'build/evidence/host/immediate-readable-regions';out.mkdir(parents=True,exist_ok=True)
jdk=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip());platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
command=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform),str(root/'tests/host/immediate_readable_regions_test.cc'),str(cpp/'platform/common/method_identity_calibration.cc'),'-o',str(out/'test')]
r=subprocess.run(command,capture_output=True,text=True);(out/'compile.log').write_text(r.stdout+r.stderr);r.check_returncode()
r=subprocess.run([str(out/'test')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));(out/'result.log').write_text(r.stdout+r.stderr)
(out/'manifest.json').write_text(json.dumps({'compile':command,'run':[str(out/'test')],'exit':r.returncode,'platform':platform},indent=2));r.check_returncode();print(r.stdout.strip())
