#!/usr/bin/env python3
"""Compile real Java caller wiring; execute its independent frame-failure boundary."""
import os,pathlib,subprocess,tempfile
root=pathlib.Path(__file__).resolve().parents[1]
src=root/'app/src/debug/java/com/github/ytlog/artdcevm/testing'
suite=(src/'RuntimeAccessSuite.java').read_text()
start=suite.index('            if(activeFrames) {')
assert suite.index('String jit=probeJitServices(')<start<suite.index('String error=probeOwnedMethodDeletion(owner,')
brace=suite.index('{',start);end=brace+1;depth=1
while depth:
 if suite[end]=='{':depth+=1
 elif suite[end]=='}':depth-=1
 end+=1
block=suite[start:end]
cmake=(root/'artdcevm/src/main/cpp/CMakeLists.txt').read_text()
assert cmake.count('    platform/common/owned_active_frame_probe.cc\n')==1
sdk=pathlib.Path(os.environ.get('ANDROID_HOME',str(pathlib.Path.home()/'Library/Android/sdk')))
jars=sorted(sdk.glob('platforms/*/android.jar'))
if not jars:raise RuntimeError('Android SDK platform android.jar is needed for caller syntax verification')
with tempfile.TemporaryDirectory() as temp:
 p=pathlib.Path(temp)
 api=p/'com/github/ytlog/artdcevm/api/DcevmRuntime.java';api.parent.mkdir(parents=True)
 api.write_text('package com.github.ytlog.artdcevm.api;public class DcevmRuntime {public static void attachPublicJvmti(android.content.Context c){}}')
 activity=p/'com/github/ytlog/artdcevm/testing/RuntimeAccessForegroundActivity.java';activity.parent.mkdir(parents=True)
 activity.write_text('package com.github.ytlog.artdcevm.testing;public class RuntimeAccessForegroundActivity extends android.app.Activity {public boolean carrierResumed(){return true;}public int carrierPauses(){return 0;}public int carrierPid(){return 1;}public boolean awaitDestroyed(){return true;}}')
 names=['TestPhaseLog.java','RuntimeAccessSuite.java','RuntimeAccessInstrumentation.java','MethodDexOracle.java','MethodIdentityFixture.java','RetiredEntryFixture.java','ActiveFrameDexOracle.java','OwnedActiveFrameFixture.java','OwnedActiveFrameCalibration.java']
 fixtures=root/'app/src/debug/java/com/github/ytlog/artdcevm/fixture'
 result=subprocess.run(['javac','-cp',str(jars[-1]),'-d',str(p/'compiled'),*[str(src/name) for name in names],str(fixtures/'Evolving.java'),str(fixtures/'FieldSubject.java'),str(api),str(activity)],capture_output=True,text=True)
 if result.returncode:raise RuntimeError(result.stdout+result.stderr)
 print('Real RuntimeAccessSuite/Instrumentation and actual frame fixture: Java caller signatures compile')
 log=p/'android/util/Log.java';log.parent.mkdir(parents=True);log.write_text('package android.util;public class Log {public static String getStackTraceString(Throwable t){return t.toString();}}')
 test=p/'ActiveFrameCallerBoundary.java'
 test.write_text('''import java.util.HashMap;
public class ActiveFrameCallerBoundary {
 static final class JSONObject extends HashMap<String,Object>{public JSONObject put(String k,Object v){super.put(k,v);return this;}}
 static final class OwnedActiveFrameCalibration {static boolean fail;static int calls;
  static JSONObject run(String path){++calls;if(fail)throw new AssertionError("Missing actual inlinees");return new JSONObject().put("passed",true);}}
 static final class Phases {final java.util.List<String> seen=new java.util.ArrayList<>();void mark(String phase){seen.add(phase);}}
 static Phases phases;
 static JSONObject run(boolean activeFrames){phases=new Phases();String apkPath="independent fixture";JSONObject report=new JSONObject();
 report.put("passed",true);
'''+block+'''
 report.put("ownedMethodDeletion",true).put("ownedFieldDeletion",true);return report;}
 public static void main(String[] args){
  for(boolean active:new boolean[]{false,true})for(boolean failed:new boolean[]{false,true}){
   OwnedActiveFrameCalibration.fail=failed;OwnedActiveFrameCalibration.calls=0;JSONObject report=run(active);
   if(!Boolean.TRUE.equals(report.get("passed"))||!Boolean.TRUE.equals(report.get("ownedMethodDeletion"))||!Boolean.TRUE.equals(report.get("ownedFieldDeletion")))throw new AssertionError("Frame failure discarded independent ordinary proofs");
   if(!phases.seen.equals(active?java.util.Arrays.asList("active-frames.before","active-frames.after"):java.util.Collections.emptyList()))throw new AssertionError("Frame phases lost at optional failure boundary");
   if(OwnedActiveFrameCalibration.calls!=(active?1:0))throw new AssertionError("Optional fixture executed incorrectly");
   if(active){JSONObject proof=(JSONObject)report.get("ownedActiveFrameProof");
    if(!Boolean.valueOf(!failed).equals(proof.get("passed")))throw new AssertionError("Missing actual frame failure marker");
    if(failed&&!proof.get("error").toString().contains("Missing actual inlinees"))throw new AssertionError("Missing actual earliest diagnostic");
   }else if(report.containsKey("ownedActiveFrameProof"))throw new AssertionError("Unrequested frame result fabricated");
  }
  System.out.println("Actual optional caller block: frame failure stays independent, preserves method/field evidence and diagnostic; disabled scope makes no calls");
 }
}''')
 subprocess.run(['javac','-d',str(p/'boundary'),str(test),str(log)],check=True)
 subprocess.run(['java','-cp',str(p/'boundary'),'ActiveFrameCallerBoundary'],check=True)
