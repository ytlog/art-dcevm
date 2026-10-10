#!/usr/bin/env python3
"""Independent StandardDex body identity and native empty-root map bounds."""
import os,sys,subprocess,struct
from pathlib import Path
root=Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp';out=root/'build/evidence/host/boot-aot-body';out.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(root/'tests/host'))
from schema_run_state_dex_fixture import write_fixture
path=write_fixture(out/'dex',True);data=path.read_bytes();pattern=struct.pack('<HHHHIIHH',1,0,0,0,0,2,0x0012,0x000f);offset=data.index(pattern)
java=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip());platform='darwin' if os.uname().sysname=='Darwin' else 'linux';exe=out/'body'
args=[os.environ.get('CXX','clang++'),'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(java/'include'),'-I',str(java/'include'/platform)]
subprocess.run(args+[str(root/'tests/host/native_boot_aot_body_test.cc'),str(cpp/'platform/common/native_boot_method_catalog.cc'),str(cpp/'evolution/dex_schema.cc'),'-o',str(exe)],check=True)
r=subprocess.run([str(exe),str(path),str(offset)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
