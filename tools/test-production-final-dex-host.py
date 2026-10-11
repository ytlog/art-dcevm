#!/usr/bin/env python3
"""Actual public Direct/final vector/weak provenance composition."""
from pathlib import Path
import os, subprocess
root=Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp'
jdk=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
out=root/'build/evidence/host/production-final-dex';out.mkdir(parents=True,exist_ok=True)
command=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-ffunction-sections','-fdata-sections','-pthread','-I',str(cpp),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform),str(root/'tests/host/production_final_dex_test.cc')]
command += [str(cpp/'platform/common'/(name+'.cc')) for name in ('production_final_dex','native_transformed_definitions','method_identity_calibration')]
command += ['-Wl,-dead_strip' if platform=='darwin' else '-Wl,--gc-sections','-o',str(out/'test')]
steps=[('compile',command)]+[(mode,[str(out/'test'),mode]) for mode in ('success','oom','unwind','history','process','failed-history','process-history','unbound')]
for name,args in steps:
 result=subprocess.run(args,capture_output=True,text=True);(out/(name+'.log')).write_text(result.stdout+result.stderr);result.check_returncode()
print('Actual public final DEX observer host protocol PASS')
