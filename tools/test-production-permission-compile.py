#!/usr/bin/env python3
"""Compile production permission scenarios against actual built APIs and ARM64 JNI configuration."""
import argparse,json,pathlib,shlex,subprocess,zipfile
p=argparse.ArgumentParser()
for key in ('android-jar','app-classes','runtime-classes','junit-jar','monitor-aar','commands'):p.add_argument('--'+key,type=pathlib.Path,required=True)
a=p.parse_args();root=pathlib.Path(__file__).resolve().parents[1];out=root/'build/evidence/production-permission-host';out.mkdir(parents=True,exist_ok=True)
jdk=pathlib.Path(subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
with zipfile.ZipFile(a.monitor_aar) as z:(out/'monitor.jar').write_bytes(z.read('classes.jar'))
classpath=':'.join(map(str,[a.android_jar,a.app_classes,a.runtime_classes,a.junit_jar,out/'monitor.jar']))
java=list((root/'app/src/debug/java/com/github/ytlog/artdcevm/testing').glob('ProductionPermission*.java'))+[root/'app/src/androidTest/java/com/github/ytlog/artdcevm/testing/ProductionFinalPermissionTest.java']
java += [root/'app/src/debug/java/com/github/ytlog/artdcevm/testing'/name for name in ('TestForegroundScope.java','RuntimeAccessForegroundActivity.java','TestPhaseLog.java')]
cmd=[str(jdk/'bin/javac'),'-source','11','-target','11','-cp',classpath,'-d',str(out/'classes'),*map(str,java)]
r=subprocess.run(cmd,capture_output=True,text=True);(out/'javac.log').write_text(r.stdout+r.stderr);r.check_returncode();print('Actual Java/API/JUnit compile PASS')
e=next(e for e in json.loads(a.commands.read_text()) if e['file'].endswith('/native_test_bridge.cc'))
old=e['file'].split('/app/src/debug/cpp/')[0];cmd=[item.replace(old,str(root)) for item in shlex.split(e['command'])]
cmd[cmd.index('-c')+1]=str(root/'app/src/debug/cpp/testing/production_permission_probe.cc');cmd[cmd.index('-o')+1]=str(out/'probe.o')
r=subprocess.run(cmd,capture_output=True,text=True);(out/'arm64.log').write_text(r.stdout+r.stderr);r.check_returncode();print('Actual ARM64 debug JNI TU PASS')
