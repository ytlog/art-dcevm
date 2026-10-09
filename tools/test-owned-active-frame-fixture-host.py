#!/usr/bin/env python3
"""Native gate/bridge compile and actual Java DEX CodeItem oracle, no APK/device."""
import os,pathlib,subprocess,tempfile
root=pathlib.Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp'
out=root/'build/evidence/host/owned-active-frame-fixture';out.mkdir(parents=True,exist_ok=True)
java=pathlib.Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
compiler=os.environ.get('CXX','clang++');common=[compiler,'-std=c++17','-Wall','-Wextra','-Werror','-fno-rtti','-I',str(cpp)]
binary=out/'gate-test'
subprocess.run(common+['-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread',str(root/'tests/host/owned_active_frame_gate_test.cc'),'-o',str(binary)],check=True)
gate=subprocess.run([str(binary)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));(out/'gate.log').write_text(gate.stdout+gate.stderr);gate.check_returncode();print(gate.stdout.strip())
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
bridge=subprocess.run(common+['-DARTDCEVM_TESTING=1','-I',str(java/'include'),'-I',str(java/'include'/platform),'-I',str(root/'third_party/jvmti'),'-fsyntax-only',str(cpp/'platform/common/owned_active_frame_probe.cc')],capture_output=True,text=True);(out/'bridge-syntax.log').write_text(bridge.stdout+bridge.stderr);bridge.check_returncode();print('Actual JNI bridge translation unit: strict host compilation passed')
with tempfile.TemporaryDirectory() as temp:
 p=pathlib.Path(temp);stub=p/'org/json/JSONObject.java';stub.parent.mkdir(parents=True);stub.write_text('package org.json;public class JSONObject {public JSONObject(String s){}public boolean getBoolean(String k){return false;}public int getInt(String k){return 0;}public JSONObject put(String k,Object v){return this;}}')
 src=root/'app/src/debug/java/com/github/ytlog/artdcevm/testing'
 sources=[src/name for name in ['MethodDexOracle.java','ActiveFrameDexOracle.java','OwnedActiveFrameFixture.java','OwnedActiveFrameCalibration.java']]
 subprocess.run([str(java/'bin/javac'),'-d',str(p),*[str(s) for s in sources],str(stub),str(root/'tests/host/ActiveFrameDexOracleTest.java')],check=True)
 dex=subprocess.run([str(java/'bin/java'),'-cp',str(p),'com.github.ytlog.artdcevm.testing.ActiveFrameDexOracleTest'],capture_output=True,text=True);(out/'dex-codeitem.log').write_text(dex.stdout+dex.stderr);dex.check_returncode();print(dex.stdout.strip())
print('Owned active fixture host validation passed; actual inline/register/root device evidence is still required')
