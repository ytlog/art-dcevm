#!/usr/bin/env python3
"""Pin live concrete-to-abstract behavior on the fixed enhanced-redefinition JBR.

This is an independent JVM/JNI oracle, not an ART support claim. Every run uses
the same compiled fixtures and records raw output only under build/evidence.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import subprocess

from evidence_paths import new_evidence_directory

ROOT = Path(__file__).resolve().parents[1]
REVISION = 'cf1f197170a0f04c6559feb873e362583be021d1'
SUBJECT = r'''
import java.util.concurrent.CountDownLatch;
public ABSTRACT class LiveAbstractSubject implements LiveAbstractContract {
  public int number=17;
  public long wide=23;
  public Object reference;
  public LiveAbstractSubject self;
  public static Object staticReference;
  public static int staticNumber;
  public LiveAbstractSubject(int number){this.number=number;}
  public int value(){return number+DELTA;}
  MISSING
  public int hold(CountDownLatch ready, CountDownLatch resume, boolean missing) throws Exception {
    ready.countDown();resume.await();return missing?missing():value();
  }
}
'''
CALLS = r'''
public class LiveAbstractCalls {
  public static int value(LiveAbstractSubject receiver){return receiver.value();}
  public static int missing(LiveAbstractSubject receiver){return receiver.missing();}
  public static int contract(LiveAbstractContract receiver){return receiver.missing();}
  public static Object construct(){return new LiveAbstractSubject(9);}
  public static int hotValue(LiveAbstractSubject receiver){return receiver.value();}
  public static int hotMissing(LiveAbstractSubject receiver){return receiver.missing();}
  public static int hotContract(LiveAbstractContract receiver){return receiver.missing();}
  public static int hotAwait(LiveAbstractSubject receiver,java.util.concurrent.CountDownLatch ready,
      java.util.concurrent.CountDownLatch resume,boolean missing) throws Exception {
    return receiver.hold(ready,resume,missing);
  }
  public static class Inherits extends LiveAbstractSubject {public Inherits(){super(41);}}
  public static class Overrides extends LiveAbstractSubject {
    public Overrides(){super(43);}
    public int value(){return 71;}
    public int missing(){return 73;}
  }
}
'''
NATIVE_JAVA = r'''
public class LiveAbstractNative {
  static native void cache();
  static native int value(Object receiver);
  static native int missing(Object receiver);
  static native int contract(Object receiver);
  static native int nonvirtual(Object receiver);
  static native Object construct();
  static native Object allocate();
}
'''
NATIVE = r'''
#include <jni.h>
static jclass type;
static jmethodID value_id,missing_id,contract_id,constructor_id;
JNIEXPORT void JNICALL Java_LiveAbstractNative_cache(JNIEnv* env,jclass ignored){
  (void)ignored;
  jclass local=(*env)->FindClass(env,"LiveAbstractSubject");
  type=(*env)->NewGlobalRef(env,local);(*env)->DeleteLocalRef(env,local);
  value_id=(*env)->GetMethodID(env,type,"value","()I");
  missing_id=(*env)->GetMethodID(env,type,"missing","()I");
  constructor_id=(*env)->GetMethodID(env,type,"<init>","(I)V");
  local=(*env)->FindClass(env,"LiveAbstractContract");
  contract_id=(*env)->GetMethodID(env,local,"missing","()I");
  (*env)->DeleteLocalRef(env,local);
}
JNIEXPORT jint JNICALL Java_LiveAbstractNative_value(JNIEnv* e,jclass c,jobject o){
  (void)c;return (*e)->CallIntMethod(e,o,value_id);
}
JNIEXPORT jint JNICALL Java_LiveAbstractNative_missing(JNIEnv* e,jclass c,jobject o){
  (void)c;return (*e)->CallIntMethod(e,o,missing_id);
}
JNIEXPORT jint JNICALL Java_LiveAbstractNative_contract(JNIEnv* e,jclass c,jobject o){
  (void)c;return (*e)->CallIntMethod(e,o,contract_id);
}
JNIEXPORT jint JNICALL Java_LiveAbstractNative_nonvirtual(JNIEnv* e,jclass c,jobject o){
  (void)c;return (*e)->CallNonvirtualIntMethod(e,o,type,missing_id);
}
JNIEXPORT jobject JNICALL Java_LiveAbstractNative_construct(JNIEnv* e,jclass c){
  (void)c;return (*e)->NewObject(e,type,constructor_id,9);
}
JNIEXPORT jobject JNICALL Java_LiveAbstractNative_allocate(JNIEnv* e,jclass c){
  (void)c;return (*e)->AllocObject(e,type);
}
'''
HARNESS = r'''
import java.lang.instrument.*;
import java.lang.invoke.*;
import java.lang.ref.WeakReference;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.concurrent.*;
public class LiveAbstractOracle {
  static Instrumentation inst;
  static int checks;
  static Path patches;
  static final Class<?> cachedType=LiveAbstractSubject.class;
  static Class<?> type(){return LiveAbstractSubject.class;}
  public static void premain(String args,Instrumentation value){inst=value;}
  interface Op {Object run() throws Throwable;}
  static void eq(String name,Object expected,Object actual){
    if(!expected.equals(actual))throw new AssertionError(name+": "+actual);
    checks++;
  }
  static void error(String name,Class<?> expected,Op op) throws Throwable {
    try{op.run();}catch(Throwable actual){
      if(actual instanceof InvocationTargetException)actual=actual.getCause();
      eq(name,expected,actual.getClass());return;
    }
    throw new AssertionError(name+": no exception");
  }
  static void observe(String name,Op op){
    try{System.out.println(name+"="+op.run());}
    catch(Throwable error){
      String chain=error.getClass().getName();
      if(error.getCause()!=null)chain+=":"+error.getCause().getClass().getName();
      System.out.println(name+"=ERROR:"+chain);
    }
  }
  static void change(String variant) throws Exception {
    inst.redefineClasses(new ClassDefinition(type(),Files.readAllBytes(patches.resolve(variant+"/LiveAbstractSubject.class"))));
    checks++;
  }
  static void failedBatch(LiveAbstractSubject receiver,boolean abstractKind) throws Throwable {
    try{
      inst.redefineClasses(new ClassDefinition(type(),Files.readAllBytes(patches.resolve(
        (abstractKind?"concrete":"abstract")+"/LiveAbstractSubject.class"))),
        new ClassDefinition(LiveAbstractPeer.class,new byte[]{0,1}));
    }catch(Throwable error){
      eq("batch error",InternalError.class,error.getClass());
      eq("batch class",abstractKind,Modifier.isAbstract(type().getModifiers()));
      eq("batch value",receiver.number+(abstractKind?3:1),receiver.value());
      eq("batch peer",47,LiveAbstractPeer.value());return;
    }
    throw new AssertionError("batch unexpectedly succeeded");
  }
  static Object[] active(LiveAbstractSubject receiver,String variant) throws Throwable {
    CountDownLatch ready=new CountDownLatch(2),resume=new CountDownLatch(1);
    Object[] results=new Object[2];Thread[] workers=new Thread[2];
    for(int n=0;n<2;n++){
      final int index=n;
      workers[n]=new Thread(()->{
        try{results[index]=LiveAbstractCalls.hotAwait(receiver,ready,resume,index==1);}
        catch(Throwable error){results[index]=error.getClass().getName();}
      });workers[n].start();
    }
    try{
      if(!ready.await(10,TimeUnit.SECONDS))throw new AssertionError("active frame not ready");
      change(variant);System.gc();
    }finally{
      resume.countDown();for(Thread worker:workers)worker.join(10000);
    }
    for(Thread worker:workers)eq("active completed",false,worker.isAlive());
    return results;
  }
  public static void main(String[] args) throws Throwable {
    patches=Path.of(args[0]);System.load(args[1]);LiveAbstractNative.cache();
    LiveAbstractSubject receiver=new LiveAbstractSubject(61);
    LiveAbstractCalls.Inherits inherited=new LiveAbstractCalls.Inherits();
    LiveAbstractCalls.Overrides override=new LiveAbstractCalls.Overrides();
    Object[] rooted={receiver};receiver.self=receiver;receiver.wide=0x100000017L;
    receiver.reference=new String("instance-root");
    WeakReference<Object> instanceRoot=new WeakReference<>(receiver.reference);
    LiveAbstractSubject.staticReference=new String("static-root");
    WeakReference<Object> staticRoot=new WeakReference<>(LiveAbstractSubject.staticReference);
    LiveAbstractSubject.staticNumber=67;
    Method value=type().getMethod("value"),missing=type().getMethod("missing");
    Method contract=LiveAbstractContract.class.getMethod("missing");
    Field number=type().getField("number"),reference=type().getField("reference");
    MethodHandle valueHandle=MethodHandles.publicLookup().unreflect(value);
    MethodHandle missingHandle=MethodHandles.publicLookup().unreflect(missing);
    MethodHandle contractHandle=MethodHandles.publicLookup().unreflect(contract);
    Constructor<?> warm=type().getConstructor(int.class),shared=type().getConstructor(int.class);
    warm.newInstance(1);
    MethodHandle constructor=MethodHandles.publicLookup().unreflectConstructor(warm);
    CountDownLatch open=new CountDownLatch(0);
    for(int round=0;round<2;round++){
      for(int n=0;n<30000;n++){
        if(LiveAbstractCalls.hotValue(receiver)!=62||LiveAbstractCalls.hotMissing(receiver)!=29||
           LiveAbstractCalls.hotContract(receiver)!=29||
           LiveAbstractCalls.hotAwait(receiver,open,open,false)!=62)throw new AssertionError("warmup");
      }
      failedBatch(receiver,false);
      Object[] result=active(receiver,"abstract");
      eq("active value",64,result[0]);
      eq("active missing",AbstractMethodError.class.getName(),result[1]);
      observe("cachedClassModifiers"+round,()->cachedType.getModifiers());
      eq("abstract",true,Modifier.isAbstract(type().getModifiers()));
      eq("identity",true,receiver.getClass()==type()&&rooted[0]==receiver&&receiver.self==receiver);
      eq("instanceof",true,receiver instanceof LiveAbstractSubject&&receiver instanceof LiveAbstractContract);
      eq("cast",receiver,type().cast(receiver));
      eq("number",61,number.getInt(receiver));eq("wide",0x100000017L,receiver.wide);
      eq("reference",true,reference.get(receiver)==instanceRoot.get());
      eq("static number",67,LiveAbstractSubject.staticNumber);
      eq("static reference",true,LiveAbstractSubject.staticReference==staticRoot.get());
      eq("cached reflection",64,value.invoke(receiver));
      eq("cached handle",64,valueHandle.invoke(receiver));
      eq("JNI value",64,LiveAbstractNative.value(receiver));
      eq("override virtual",71,LiveAbstractCalls.value(override));
      eq("override interface",73,LiveAbstractCalls.contract(override));
      eq("inherited concrete",44,inherited.value());
      System.out.println("abstractDispatchRound"+round);
      for(int n=0;n<30000;n++){
        if(LiveAbstractCalls.hotValue(receiver)!=64)throw new AssertionError("abstract hot value");
        if(LiveAbstractCalls.hotAwait(receiver,open,open,false)!=64)throw new AssertionError("abstract hot frame");
        error("hot virtual",AbstractMethodError.class,()->LiveAbstractCalls.hotMissing(receiver));
        error("hot interface",AbstractMethodError.class,()->LiveAbstractCalls.hotContract(receiver));
      }
      System.out.println("abstractDispatchEnd"+round);
      error("old virtual",AbstractMethodError.class,()->LiveAbstractCalls.missing(receiver));
      error("old interface",AbstractMethodError.class,()->LiveAbstractCalls.contract(receiver));
      error("cached reflection missing",AbstractMethodError.class,()->missing.invoke(receiver));
      error("fresh reflection missing",AbstractMethodError.class,()->type().getMethod("missing").invoke(receiver));
      error("cached interface reflection",AbstractMethodError.class,()->contract.invoke(receiver));
      error("cached virtual handle",AbstractMethodError.class,()->missingHandle.invoke(receiver));
      error("cached interface handle",AbstractMethodError.class,()->contractHandle.invoke(receiver));
      error("fresh virtual handle",AbstractMethodError.class,()->MethodHandles.publicLookup().findVirtual(type(),"missing",MethodType.methodType(int.class)).invoke(receiver));
      error("JNI virtual",AbstractMethodError.class,()->LiveAbstractNative.missing(receiver));
      error("JNI interface",AbstractMethodError.class,()->LiveAbstractNative.contract(receiver));
      error("JNI nonvirtual",AbstractMethodError.class,()->LiveAbstractNative.nonvirtual(receiver));
      error("inherited missing",AbstractMethodError.class,()->LiveAbstractCalls.missing(inherited));
      error("new",InstantiationError.class,()->LiveAbstractCalls.construct());
      error("new JNI",InstantiationException.class,()->LiveAbstractNative.construct());
      error("allocate JNI",InstantiationException.class,()->LiveAbstractNative.allocate());
      error("constructor handle",InstantiationException.class,()->constructor.invoke(9));
      Constructor<?> cold=type().getConstructor(int.class);
      error("fresh reflection constructor",InstantiationException.class,()->cold.newInstance(9));
      observe("warmConstructor"+round,()->warm.newInstance(9));
      observe("sharedConstructor"+round,()->shared.newInstance(9));
      observe("coldConstructor"+round,()->cold.newInstance(9));
      observe("cachedReflectionOwner"+round,()->value.getDeclaringClass()==type());
      System.gc();
      eq("GC instance root",true,receiver.reference==instanceRoot.get()&&instanceRoot.get()!=null);
      eq("GC static root",true,LiveAbstractSubject.staticReference==staticRoot.get()&&staticRoot.get()!=null);
      error("after GC",AbstractMethodError.class,()->LiveAbstractCalls.contract(receiver));
      failedBatch(receiver,true);
      result=active(receiver,"concrete");
      eq("restored active value",62,result[0]);eq("restored active missing",29,result[1]);
      eq("concrete",false,Modifier.isAbstract(type().getModifiers()));
      eq("restored old value",62,receiver.value());eq("restored missing",29,missingHandle.invoke(receiver));
      eq("restored JNI",29,LiveAbstractNative.missing(receiver));
      eq("restored new",true,LiveAbstractCalls.construct().getClass()==type());
      eq("restored JNI new",true,LiveAbstractNative.construct().getClass()==type());
      eq("restored allocate",true,LiveAbstractNative.allocate().getClass()==type());
      eq("restored constructor handle",true,constructor.invoke(9).getClass()==type());
      observe("restoredWarmConstructor"+round,()->warm.newInstance(9).getClass()==type());
      observe("restoredSharedConstructor"+round,()->shared.newInstance(9).getClass()==type());
      observe("restoredColdConstructor"+round,()->cold.newInstance(9).getClass()==type());
      System.gc();eq("restored fields",true,receiver.self==receiver&&receiver.reference==instanceRoot.get());
    }
    System.out.println("JBR live abstract oracle PASS checks="+checks);
  }
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--java-home', required=True, type=Path)
    args = parser.parse_args()
    home = args.java_home.resolve()
    evidence = new_evidence_directory(ROOT / 'build/evidence/jbr-live-abstract')
    work = evidence / 'work'
    work.mkdir()
    manifest = {'sourceOracleRevision': REVISION, 'fixtures': {},
                'toolSha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), 'runs': []}

    def run(name, command, timeout=120):
        try:
            result = subprocess.run(list(map(str, command)), capture_output=True, cwd=work, timeout=timeout)
        except subprocess.TimeoutExpired as error:
            (evidence / (name + '.log')).write_bytes((error.stdout or b'') + (error.stderr or b''))
            raise RuntimeError(name + ' timed out; inspect ignored evidence') from None
        output = result.stdout + result.stderr
        (evidence / (name + '.log')).write_bytes(output)
        if result.returncode:
            raise RuntimeError(name + ' failed; inspect ignored evidence')
        return output.decode(errors='replace')

    manifest['runtimeVersion'] = run('version', [home / 'bin/java', '-version']).strip()
    if 'JBR-21.0.11+10-1163.116' not in manifest['runtimeVersion']:
        raise RuntimeError('Requires fixed JBR 21.0.11+10-b1163.116')
    manifest['javaExecutableSha256'] = hashlib.sha256((home / 'bin/java').read_bytes()).hexdigest()
    common = work / 'common'
    common.mkdir()
    for name, source in {'LiveAbstractContract.java': 'public interface LiveAbstractContract {int missing();}',
                         'LiveAbstractPeer.java': 'public class LiveAbstractPeer {public static int value(){return 47;}}',
                         'LiveAbstractNative.java': NATIVE_JAVA}.items():
        (common / name).write_text(source)
    run('common', [home / 'bin/javac', '--release', '11', '-d', common, *sorted(common.glob('*.java'))])
    for variant, abstract, missing, delta in [('concrete', '', 'public int missing(){return 29;}', '1'),
                                            ('abstract', 'abstract', 'public abstract int missing();', '3')]:
        directory = work / variant
        directory.mkdir()
        source = SUBJECT.replace('ABSTRACT', abstract).replace('MISSING', missing).replace('DELTA', delta)
        (directory / 'LiveAbstractSubject.java').write_text(source)
        run('compile-' + variant, [home / 'bin/javac', '--release', '11', '-cp', common,
                                  '-d', directory, directory / 'LiveAbstractSubject.java'])
        manifest['fixtures'][variant] = hashlib.sha256((directory / 'LiveAbstractSubject.class').read_bytes()).hexdigest()
    (work / 'LiveAbstractCalls.java').write_text(CALLS)
    (work / 'LiveAbstractOracle.java').write_text(HARNESS)
    cp = str(common) + ':' + str(work / 'concrete')
    run('harness', [home / 'bin/javac', '--release', '11', '-cp', cp, '-d', common,
                    work / 'LiveAbstractCalls.java', work / 'LiveAbstractOracle.java'])
    (work / 'agent.mf').write_text('Premain-Class: LiveAbstractOracle\nCan-Redefine-Classes: true\n\n')
    agent = work / 'agent.jar'
    run('agent', [home / 'bin/jar', 'cfm', agent, work / 'agent.mf', '-C', common, 'LiveAbstractOracle.class'])
    system = platform.system()
    if system not in ('Darwin', 'Linux'):
        raise RuntimeError('JNI oracle build supports Darwin and Linux')
    compiler = shutil.which('cc')
    if not compiler:
        raise RuntimeError('JNI oracle requires a host C compiler')
    (work / 'native.c').write_text(NATIVE)
    library = work / ('libliveabstract.dylib' if system == 'Darwin' else 'libliveabstract.so')
    run('native', [compiler, '-dynamiclib' if system == 'Darwin' else '-shared', '-fPIC',
                   '-I' + str(home / 'include'), '-I' + str(home / 'include' / system.lower()),
                   '-o', library, work / 'native.c'])
    manifest['jniLibrarySha256'] = hashlib.sha256(library.read_bytes()).hexdigest()
    for mode, options in [('interpreter', ['-Xint']), ('jit', ['-Xbatch', '-XX:CompileThreshold=1000',
                           '-XX:CompileCommand=compileonly,LiveAbstractCalls::hot*', '-XX:+PrintCompilation'])]:
        output = run(mode, [home / 'bin/java', '-XX:+AllowEnhancedClassRedefinition', *options,
                            '-javaagent:' + str(agent), '-cp', cp, 'LiveAbstractOracle', work, library])
        match = re.search(r'JBR live abstract oracle PASS checks=(\d+)', output)
        if not match:
            raise RuntimeError(mode + ' did not finish the oracle')
        observations = dict(line.split('=', 1) for line in output.splitlines()
                            if re.match(r'(?:cached(?:ClassModifiers|ReflectionOwner)|'
                                        r'(?:restored)?(?:Warm|Cold|Shared|warm|cold|shared)Constructor)\d=', line))
        expected = {}
        for round_number in range(2):
            for name, value in {
                'cachedClassModifiers': '1', 'cachedReflectionOwner': 'true',
                'warmConstructor': 'ERROR:java.lang.reflect.InvocationTargetException:java.lang.InstantiationException',
                'sharedConstructor': 'ERROR:java.lang.reflect.InvocationTargetException:java.lang.InstantiationException',
                'coldConstructor': 'ERROR:java.lang.InstantiationException',
                'restoredWarmConstructor': 'true', 'restoredSharedConstructor': 'true',
                'restoredColdConstructor': 'ERROR:java.lang.InstantiationException',
            }.items():
                expected[name + str(round_number)] = value
        if observations != expected:
            raise RuntimeError(mode + ' differs from pinned reflection-cache behavior')
        if mode == 'jit':
            for round_number in range(2):
                region = output.split('abstractDispatchRound' + str(round_number), 1)[1].split(
                    'abstractDispatchEnd' + str(round_number), 1)[0]
                for name in ('hotValue', 'hotMissing', 'hotContract', 'hotAwait'):
                    if not any(re.search(r'LiveAbstractCalls::' + name + r'\b', line) and not
                               any(text in line for text in ('made not', 'excluded', 'not compilable'))
                               for line in region.splitlines()):
                        raise RuntimeError('Missing post-transition compilation evidence for ' + name)
        manifest['runs'].append({'mode': mode, 'checks': int(match.group(1)), 'observations': observations,
                                 'compiledDispatchCallers': mode == 'jit'})
    manifest['passed'] = True
    (evidence / 'summary.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('JBR live abstract oracle PASS modes=2')
    print('Evidence: ' + str(evidence.relative_to(ROOT)))


if __name__ == '__main__':
    main()
