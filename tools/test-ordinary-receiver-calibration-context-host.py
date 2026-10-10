#!/usr/bin/env python3
"""Test the actual process gate and ordinary receiver borrowed-context lifetime."""
import os,pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp'
jdk=pathlib.Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
out=root/'build/evidence/host/ordinary-receiver-calibration-context';out.mkdir(parents=True,exist_ok=True)
binary=out/'test'
subprocess.run(['clang++','-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread','-I',str(cpp),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform),str(root/'tests/host/ordinary_receiver_calibration_context_test.cc'),str(cpp/'platform/common/method_identity_calibration.cc'),'-o',str(binary)],check=True)
r=subprocess.run([str(binary)],capture_output=True,text=True);(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
