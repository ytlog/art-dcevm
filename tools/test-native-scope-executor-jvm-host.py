#!/usr/bin/env python3
"""Real host JVM ordinary JNI route; ART state evidence remains an explicit fixture double."""
import os
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
jdk=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
out=root/'build/evidence/host/native-scope-executor-jvm';out.mkdir(parents=True,exist_ok=True)
java=out/'NativeScopeExecutorHostProbe.java'
java.write_text('''public final class NativeScopeExecutorHostProbe {
 private static native boolean run();
 private static native void pending();
 public static void main(String[] args) {
  System.load(args[0]);
  if (!run()) throw new AssertionError("registered route");
  try { pending(); throw new AssertionError("missing original exception"); }
  catch (IllegalStateException expected) {
   if (!"original exception survives executor".equals(expected.getMessage())) throw expected;
  }
  System.out.println("Real host JVM preserved original Java exception PASS");
 }
}''')
subprocess.run([str(jdk/'bin/javac'),'-d',str(out),str(java),str(root/'artdcevm/src/main/java/com/github/ytlog/artdcevm/internal/NativeScopeBridge.java')],check=True)
lib=out/('executor.dylib' if platform=='darwin' else 'executor.so')
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-Wall','-Wextra','-Werror','-dynamiclib' if platform=='darwin' else '-shared','-fPIC','-I',str(root/'artdcevm/src/main/cpp'),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform),str(root/'tests/host/native_scope_executor_jvm_test.cc'),str(root/'artdcevm/src/main/cpp/platform/common/native_scope_executor.cc'),'-o',str(lib)],check=True)
r=subprocess.run([str(jdk/'bin/java'),'-Xcheck:jni','-cp',str(out),'NativeScopeExecutorHostProbe',str(lib)],capture_output=True,text=True);(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
