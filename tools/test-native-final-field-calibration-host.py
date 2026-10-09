#!/usr/bin/env python3
"""Actual calibration TU lifecycle test; fake JNI does not claim ART/device readiness."""
import os,pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[1]
peer=pathlib.Path(os.environ.get('ARTDCEVM_SCHEMA_INCLUDE',str(root/'artdcevm/src/main/cpp')))
java=pathlib.Path(subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
out=root/'build/evidence/host/native-final-field-calibration';out.mkdir(parents=True,exist_ok=True)
subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(java/'include'),'-I'+str(java/'include/darwin'),'-I'+str(peer),'-I'+str(root/'artdcevm/src/main/cpp'),'-I'+str(root/'third_party/jvmti'),str(root/'tests/host/native_final_field_calibration_test.cc'),str(root/'artdcevm/src/main/cpp/platform/common/native_final_field_calibration.cc'),str(root/'artdcevm/src/main/cpp/platform/common/native_final_write_guard.cc'),str(root/'artdcevm/src/main/cpp/platform/common/modern_final_field_guard.cc'),'-o',str(out/'probe')],check=True)
for mode in range(6):
 result=subprocess.run([str(out/'probe'),str(mode)],capture_output=True,text=True);(out/('mode-'+str(mode)+'.log')).write_text(result.stdout+result.stderr);result.check_returncode();print(result.stdout.strip())
subprocess.run(['javac','-d',str(out/'java'),str(root/'artdcevm/src/main/java/com/github/ytlog/artdcevm/internal/FinalPermissionOracle.java'),str(root/'tests/host/FinalPermissionOracleHostTest.java')],check=True)
subprocess.run(['java','-cp',str(out/'java'),'com.github.ytlog.artdcevm.internal.FinalPermissionOracleHostTest'],check=True)
bytecode=subprocess.check_output(['javap','-c','-p','-classpath',str(out/'java'),'com.github.ytlog.artdcevm.internal.FinalPermissionOracle'],text=True)
for instruction in ['putfield','putstatic','MethodHandle.invokeExact','VarHandle.set','Field.setAccessible','Field.setInt']:assert instruction in bytecode,instruction
(out/'javap.txt').write_text(bytecode)
