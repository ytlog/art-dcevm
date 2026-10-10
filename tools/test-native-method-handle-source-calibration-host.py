#!/usr/bin/env python3
import os, subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
java=Path(subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
out=root/'build/evidence/host/native-method-handle-source-calibration';out.mkdir(parents=True,exist_ok=True)
fixture=(root/'tests/host/modern_method_handle_guards_test.cc').read_text().split('int main(){')[0]
fixture=fixture.replace('runnable_jni::Result With(', 'runnable_jni::Result FixtureWith(')
(out/'native_receiver_handle_fixture.inc').write_text(fixture)
(out/'native_receiver_source_fixture.inc').write_text((root/'tests/host/native_method_handle_receiver_source_test.cc').read_text().split('int main(){')[0])
sources=['native_method_handle_source_calibration.cc','native_receiver_guard.cc','native_receiver_frame.cc','native_receiver_bytecode.cc','retired_method_registry.cc','retired_field_registry.cc','modern_final_field_guard.cc']
binary=out/'calibration_test'
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(root/'artdcevm/src/main/cpp'),'-I',str(out),'-I',str(java/'include'),'-I',str(java/'include/darwin'),str(root/'tests/host/native_method_handle_source_calibration_test.cc'),*[str(root/'artdcevm/src/main/cpp/platform/common'/s) for s in sources],'-o',str(binary)],check=True)
r=subprocess.run([str(binary)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
subprocess.run([str(java/'bin/javac'),'-d',str(out),str(root/'artdcevm/src/main/java/com/github/ytlog/artdcevm/internal/MethodHandleReceiverOracle.java')],check=True)

probe=out/'PublicSamplesProbe.java'
probe.write_text("""import com.github.ytlog.artdcevm.internal.MethodHandleReceiverOracle;
public class PublicSamplesProbe {
 public static void main(String[] args) throws Throwable {
  var samples=MethodHandleReceiverOracle.samples();
  int[] kinds={0,1,2,3,4,8,9,10,11};
  int[] counts={2,2,2,1,2,1,2,0,1};
  var base=new MethodHandleReceiverOracle.Base();
  var child=new MethodHandleReceiverOracle.Child();
  for(int i=0;i<9;i++)if(samples[i].kind!=kinds[i]||samples[i].handle.type().parameterCount()!=counts[i])throw new AssertionError();
  if(samples[1].handle.type().parameterType(0)!=MethodHandleReceiverOracle.Child.class||samples[1].member.getDeclaringClass()!=MethodHandleReceiverOracle.Base.class)throw new AssertionError();
  Object[] receivers={base,child,base,null,base};
  int[] results={6,6,7,8,6};
  for(int i=0;i<5;i++){
   Object result=i==3?samples[i].handle.invokeWithArguments(5):samples[i].handle.invokeWithArguments(receivers[i],5);
   if(!result.equals(results[i]))throw new AssertionError();
  }
  samples[6].handle.invokeWithArguments(base,7);
  if(!samples[5].handle.invokeWithArguments(base).equals(7))throw new AssertionError();
  samples[8].handle.invokeWithArguments(9);
  if(!samples[7].handle.invokeWithArguments().equals(9))throw new AssertionError();
  System.out.println("Real public Java factory: nine construction/type/member oracles and actual calls PASS (host JVM, not ART source-kind calibration)");
 }
}
""")
subprocess.run([str(java/'bin/javac'),'-cp',str(out),'-d',str(out),str(probe)],check=True)
subprocess.run([str(java/'bin/java'),'-cp',str(out),'PublicSamplesProbe'],check=True)
