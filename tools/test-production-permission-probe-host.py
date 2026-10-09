#!/usr/bin/env python3
"""Actual debug JNI cache TU: function-table identity, VM isolation and all root rollback paths."""
import pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[1]
jdk=pathlib.Path(subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
out=root/'build/evidence/production-permission-host';out.mkdir(parents=True,exist_ok=True)
command=['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(jdk/'include'),'-I'+str(jdk/'include/darwin'),str(root/'tests/host/production_permission_probe_test.cc'),'-o',str(out/'probe-test')]
for name,cmd in [('compile',command),('run',[str(out/'probe-test')])]:
 result=subprocess.run(cmd,capture_output=True,text=True);(out/(name+'.log')).write_text(result.stdout+result.stderr);result.check_returncode()
print('Actual production public JNI cache ASan/UBSan PASS')
