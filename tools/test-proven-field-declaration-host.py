#!/usr/bin/env python3
"""Consume genuinely calibrated FieldMetadata against arbitrary target counts."""
import os,pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp'
jdk=pathlib.Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip());platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
out=root/'build/evidence/host/proven-field-declaration';out.mkdir(parents=True,exist_ok=True);binary=out/'test'
command=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform),str(root/'tests/host/proven_field_declaration_test.cc'),str(cpp/'platform/common/modern_field_metadata.cc'),str(cpp/'platform/common/method_identity_calibration.cc'),str(cpp/'evolution/dex_schema.cc'),'-Wl,-dead_strip' if platform=='darwin' else '-Wl,--gc-sections','-o',str(binary)]
if platform=='darwin':command+=['-Wl,-pagezero_size,0x10000']
subprocess.run(command,check=True)
r=subprocess.run([str(binary)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
