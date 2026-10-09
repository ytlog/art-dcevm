#!/usr/bin/env python3
"""Actual named startup mapper/gate TU; OS loader/VM edges are boundary doubles."""
import os,pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp'
jdk=pathlib.Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
out=root/'build/evidence/host/startup-plugin-mapping';out.mkdir(parents=True,exist_ok=True)
subprocess.run(['clang++','-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread','-I',str(cpp),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform),str(root/'tests/host/startup_plugin_mapping_test.cc'),str(cpp/'platform/common/method_identity_calibration.cc'),'-o',str(out/'test')],check=True)
for mode in ['', 'old','race']:
 r=subprocess.run([str(out/'test'),mode],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));(out/(mode or 'startup')).with_suffix('.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
