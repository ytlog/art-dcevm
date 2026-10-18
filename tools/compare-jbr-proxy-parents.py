#!/usr/bin/env python3
"""Compare Proxy parent evolution with the fixed enhanced-redefinition JBR."""

import argparse
import hashlib
import json
import re
from pathlib import Path
import subprocess
import tempfile

from evidence_paths import new_evidence_directory

ROOT = Path(__file__).resolve().parents[1]
REVISION = 'cf1f197170a0f04c6559feb873e362583be021d1'
SOURCE = r'''
import java.lang.instrument.*;
import java.lang.reflect.*;
import java.nio.file.*;
import com.github.ytlog.artdcevm.fixture.ProxyParentsFixture;
public class ProxyParentOracle {
  static Instrumentation inst; static Path patches; static int checks;
  static java.util.IdentityHashMap<Object,Integer> identities=new java.util.IdentityHashMap<>();
  public static void premain(String args, Instrumentation value){inst=value;}
  static void eq(String label,Object expected,Object actual){
    checks++;if(!expected.equals(actual))throw new AssertionError(label+": "+actual);
  }
  interface Action {void run() throws Throwable;}
  static void error(Class<?> expected,Action action) throws Throwable {
    try{action.run();}catch(Throwable actual){
      if(actual instanceof InvocationTargetException)actual=actual.getCause();
      eq("exception",expected,actual.getClass());return;
    }throw new AssertionError("missing "+expected.getName());
  }
  static ClassDefinition[] definitions(String variant,Class<?>[] types,boolean invalid) throws Exception {
    ClassDefinition[] result=new ClassDefinition[types.length];
    for(int n=0;n<types.length;n++)result[n]=new ClassDefinition(types[n],
      invalid&&types[n]==ProxyParentsFixture.Root.class?new byte[]{0,1}:
      Files.readAllBytes(patches.resolve(variant).resolve(types[n].getName().replace('.','/')+".class")));
    return result;
  }
  static void redefine(String variant,Class<?>... types) throws Exception {
    inst.redefineClasses(definitions(variant,types,false));checks++;
  }
  static void invalid(String variant,Class<?>[] types) throws Throwable {
    error(InternalError.class,()->inst.redefineClasses(definitions(variant,types,true)));
  }
  static String order(Class<?>[] types){
    StringBuilder out=new StringBuilder();
    for(Class<?> type:types){if(out.length()!=0)out.append(',');out.append(type.getSimpleName());}
    return out.toString();
  }
  static void identity(Object proxy,Object alias,Class<?> type,ProxyParentsFixture.RecordingHandler handler,
      Constructor<?> constructor,Method cachedA) throws Throwable {
    eq("identity",true,proxy==alias);eq("identity hash",identities.get(proxy),System.identityHashCode(proxy));
    eq("class",true,proxy.getClass()==type);
    eq("handler",true,Proxy.getInvocationHandler(proxy)==handler);
    eq("cached A",11,cachedA.invoke(proxy));
    eq("handler method",true,handler.last.equals(ProxyParentsFixture.A.class.getMethod("a")));
    var freshHandler=new ProxyParentsFixture.RecordingHandler();Object fresh=constructor.newInstance(freshHandler);
    eq("cached constructor",true,fresh!=proxy&&fresh.getClass()==type&&Proxy.getInvocationHandler(fresh)==freshHandler);
    eq("fresh cached A",11,cachedA.invoke(fresh));
    eq("fresh handler method",true,freshHandler.last.equals(ProxyParentsFixture.A.class.getMethod("a")));
  }
  static void added(Object proxy,ProxyParentsFixture.RecordingHandler handler,Method cachedRoot) throws Throwable {
    eq("added parent",true,proxy instanceof ProxyParentsFixture.Added);
    eq("root parent",true,proxy instanceof ProxyParentsFixture.Root);
    int calls=handler.calls;
    eq("default",43,ProxyParentsFixture.added((ProxyParentsFixture.Added)proxy));
    eq("extra",44,ProxyParentsFixture.Added.class.getMethod("extra").invoke(proxy));
    eq("cached root",47,cachedRoot.invoke(proxy));
    error(AbstractMethodError.class,()->((ProxyParentsFixture.Added)proxy).missing());
    eq("hot added",43*30000,ProxyParentsFixture.addedSum((ProxyParentsFixture.Added)proxy,30000));
    eq("hot root",47*30000,ProxyParentsFixture.rootSum((ProxyParentsFixture.Root)proxy,30000));
    eq("default bypasses handler",calls,handler.calls);
  }
  public static void main(String[] args) throws Throwable {
    patches=Paths.get(args[0]);
    var handler=new ProxyParentsFixture.RecordingHandler();Object proxy=ProxyParentsFixture.create(handler),alias=proxy;
    Class<?> proxyClass=proxy.getClass();Constructor<?> constructor=proxyClass.getConstructor(InvocationHandler.class);
    var onlyHandler=new ProxyParentsFixture.RecordingHandler();Object only=ProxyParentsFixture.createA(onlyHandler),onlyAlias=only;
    Class<?> onlyClass=only.getClass();Constructor<?> onlyConstructor=onlyClass.getConstructor(InvocationHandler.class);
    identities.put(proxy,System.identityHashCode(proxy));identities.put(only,System.identityHashCode(only));
    Method cachedA=ProxyParentsFixture.A.class.getMethod("a"),cachedShared=ProxyParentsFixture.Shared.class.getMethod("shared");
    Method proxyShared=proxyClass.getMethod("shared"),cachedRoot=ProxyParentsFixture.Root.class.getMethod("root");
    ProxyParentsFixture.Shared held=(ProxyParentsFixture.Shared)proxy,onlyHeld=(ProxyParentsFixture.Shared)only;
    var holder=new ProxyParentsFixture.Holder();var onlyHolder=new ProxyParentsFixture.Holder();
    holder.value=held;holder.object=proxy;onlyHolder.value=onlyHeld;onlyHolder.object=only;
    ProxyParentsFixture.Holder.staticValue=held;
    Class<?> A=ProxyParentsFixture.A.class,B=ProxyParentsFixture.B.class,Added=ProxyParentsFixture.Added.class,Root=ProxyParentsFixture.Root.class;
    ClassDefinition[] cycle={new ClassDefinition(A,Files.readAllBytes(patches.resolve("proxy-parents-cycle-a").resolve(A.getName().replace('.','/')+".class"))),
      new ClassDefinition(B,Files.readAllBytes(patches.resolve("proxy-parents-cycle-b").resolve(B.getName().replace('.','/')+".class")))};
    for(boolean reverse:new boolean[]{false,true}){
      error(ClassCircularityError.class,()->inst.redefineClasses(reverse?new ClassDefinition[]{cycle[1],cycle[0]}:cycle));
      eq("cycle kept",71,ProxyParentsFixture.shared(held));
      identity(proxy,alias,proxyClass,handler,constructor,cachedA);
      System.gc();eq("cycle GC",71,ProxyParentsFixture.shared(held));
    }
    for(int round=0;round<2;round++){
      Class<?>[] batch=round==0?new Class<?>[]{A,Added,Root,B}:new Class<?>[]{B,Root,Added,A};
      Class<?>[] restore=round==0?new Class<?>[]{B,Root,Added,A}:new Class<?>[]{A,Added,Root,B};
      eq("batch order",round==0?"A,Added,Root,B":"B,Root,Added,A",order(batch));
      System.out.println("batchOrder"+round+"="+order(batch));
      redefine("proxy-parents-a-drop",A);
      eq("shared kept",true,proxy instanceof ProxyParentsFixture.Shared);
      eq("shared call",71,ProxyParentsFixture.shared(held));
      eq("hot shared retained",0,ProxyParentsFixture.sharedFailures(held,30000));
      eq("indirect field cleared",true,holder.value==null);
      eq("static kept",true,ProxyParentsFixture.Holder.staticValue==proxy);
      eq("onlyA lost shared",false,only instanceof ProxyParentsFixture.Shared);
      eq("onlyA field cleared",true,onlyHolder.value==null);
      error(IncompatibleClassChangeError.class,()->ProxyParentsFixture.shared(onlyHeld));
      identity(proxy,alias,proxyClass,handler,constructor,cachedA);
      identity(only,onlyAlias,onlyClass,onlyHandler,onlyConstructor,cachedA);
      redefine("proxy-parents-drop",B);
      eq("dropped",false,proxy instanceof ProxyParentsFixture.Shared);
      eq("hot shared removed",30000,ProxyParentsFixture.sharedFailures(held,30000));
      error(IncompatibleClassChangeError.class,()->ProxyParentsFixture.shared(held));
      error(IncompatibleClassChangeError.class,()->ProxyParentsFixture.shared(ProxyParentsFixture.Holder.staticValue));
      error(IllegalArgumentException.class,()->cachedShared.invoke(proxy));
      eq("proxy own method",71,proxyShared.invoke(proxy));
      eq("proxy own method owner",true,handler.last.equals(ProxyParentsFixture.Shared.class.getMethod("shared")));
      eq("b",12,((ProxyParentsFixture.B)proxy).b());
      eq("object reference",true,holder.object==proxy&&onlyHolder.object==only);
      System.gc();eq("fresh dropped",false,constructor.newInstance(new ProxyParentsFixture.RecordingHandler()) instanceof ProxyParentsFixture.Shared);
      identity(proxy,alias,proxyClass,handler,constructor,cachedA);
      redefine("proxy-parents-original",round==0?new Class<?>[]{A,B}:new Class<?>[]{B,A});
      eq("restored shared",71,ProxyParentsFixture.shared(held));
      eq("cleared field stays cleared",true,holder.value==null&&onlyHolder.value==null);
      holder.value=held;onlyHolder.value=onlyHeld;
      invalid("proxy-parents-add",batch);
      eq("aborted add",false,proxy instanceof ProxyParentsFixture.Added);
      eq("aborted root",false,proxy instanceof ProxyParentsFixture.Root);
      eq("aborted fields",true,holder.value==proxy&&onlyHolder.value==only);
      identity(proxy,alias,proxyClass,handler,constructor,cachedA);
      redefine("proxy-parents-add",batch);
      added(proxy,handler,cachedRoot);added(only,onlyHandler,cachedRoot);
      holder.added=(ProxyParentsFixture.Added)proxy;holder.root=(ProxyParentsFixture.Root)proxy;
      invalid("proxy-parents-original",restore);
      added(proxy,handler,cachedRoot);
      eq("aborted removal fields",true,holder.added==proxy&&holder.root==proxy);
      identity(proxy,alias,proxyClass,handler,constructor,cachedA);
      Object fresh=constructor.newInstance(new ProxyParentsFixture.RecordingHandler());
      eq("cached proxy class",true,ProxyParentsFixture.create().getClass()==proxyClass&&fresh.getClass()==proxyClass);
      added(fresh,(ProxyParentsFixture.RecordingHandler)Proxy.getInvocationHandler(fresh),cachedRoot);
      System.gc();added(proxy,handler,cachedRoot);
      redefine("proxy-parents-added-b",A);
      added(proxy,handler,cachedRoot);
      eq("only lost added",false,only instanceof ProxyParentsFixture.Added||only instanceof ProxyParentsFixture.Root);
      eq("shared added field cleared",true,holder.added==null);
      eq("shared root field cleared",true,holder.root==null);
      eq("shared still shared",71,ProxyParentsFixture.shared(held));
      eq("shared field kept",true,holder.value==proxy);
      identity(proxy,alias,proxyClass,handler,constructor,cachedA);
      identity(only,onlyAlias,onlyClass,onlyHandler,onlyConstructor,cachedA);
      redefine("proxy-parents-original",restore);
      eq("added removed",false,proxy instanceof ProxyParentsFixture.Added||proxy instanceof ProxyParentsFixture.Root);
      eq("shared final",71,ProxyParentsFixture.shared(held));
      eq("field cleared final",true,holder.added==null&&holder.root==null);
      eq("interfaces",2,proxyClass.getInterfaces().length);eq("serializable",true,proxy instanceof java.io.Serializable);
      System.gc();eq("fresh final",false,constructor.newInstance(new ProxyParentsFixture.RecordingHandler()) instanceof ProxyParentsFixture.Added);
      identity(proxy,alias,proxyClass,handler,constructor,cachedA);
      holder.value=held;onlyHolder.value=onlyHeld;
    }
    System.out.println("JBR Proxy parent oracle PASS checks="+checks);
  }
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--java-home', type=Path, required=True)
    args = parser.parse_args()
    jbr = args.java_home.resolve()
    evidence = new_evidence_directory(ROOT / 'build/evidence/jbr-proxy-parents')
    manifest = {'sourceOracleRevision': REVISION, 'fixtureSha256': {},
                'harnessSha256': hashlib.sha256(SOURCE.encode()).hexdigest(),
                'toolSha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    with tempfile.TemporaryDirectory(prefix='oracle-', dir=evidence) as temp:
        work = Path(temp)

        def run(name, command):
            try:
                result = subprocess.run(command, capture_output=True, text=True, timeout=120)
            except subprocess.TimeoutExpired as error:
                def decoded(value):
                    return value.decode(errors='replace') if isinstance(value, bytes) else value or ''
                (evidence / (name + '.log')).write_text(decoded(error.stdout) + decoded(error.stderr))
                raise RuntimeError(name + ' timed out; inspect ignored evidence') from None
            (evidence / (name + '.log')).write_text(result.stdout + result.stderr)
            if result.returncode:
                raise RuntimeError(name + ' failed; inspect ignored evidence')
            return result.stdout + result.stderr

        manifest['runtimeVersion'] = run('version', [str(jbr / 'bin/java'), '-version']).strip()
        if 'JBR-21.0.11+10-1163.116' not in manifest['runtimeVersion']:
            raise RuntimeError('This oracle requires fixed JBR 21.0.11+10-b1163.116')
        manifest['javaExecutableSha256'] = hashlib.sha256((jbr / 'bin/java').read_bytes()).hexdigest()
        jvm = next((p for p in (jbr / 'lib/server').glob('libjvm.*') if p.is_file()), None)
        if jvm is None:
            raise RuntimeError('Fixed JBR server library is unavailable')
        manifest['jvmLibrarySha256'] = hashlib.sha256(jvm.read_bytes()).hexdigest()
        source = ROOT / 'app/src/debug/java/com/github/ytlog/artdcevm/fixture/ProxyParentsFixture.java'
        old = work / 'old'
        manifest['fixtureSha256']['original'] = hashlib.sha256(source.read_bytes()).hexdigest()
        run('compile-old', [str(jbr / 'bin/javac'), '--release', '11', '-d', str(old), str(source)])
        patches = work / 'patches'
        for variant in ('proxy-parents-original', 'proxy-parents-a-drop', 'proxy-parents-drop',
                        'proxy-parents-add', 'proxy-parents-added-b', 'proxy-parents-cycle-a', 'proxy-parents-cycle-b'):
            source = ROOT / 'fixtures' / variant / 'com/github/ytlog/artdcevm/fixture/ProxyParentsFixture.java'
            manifest['fixtureSha256'][variant] = hashlib.sha256(source.read_bytes()).hexdigest()
            run('compile-' + variant, [str(jbr / 'bin/javac'), '--release', '11',
                                      '-d', str(patches / variant), str(source)])
        source = evidence / 'ProxyParentOracle.java'
        source.write_text(SOURCE)
        harness = work / 'harness'
        run('compile-oracle', [str(jbr / 'bin/javac'), '--release', '11', '-cp', str(old),
                               '-d', str(harness), str(source)])
        mf = work / 'manifest.mf'
        mf.write_text('Premain-Class: ProxyParentOracle\nCan-Redefine-Classes: true\n\n')
        agent = work / 'agent.jar'
        run('agent', [str(jbr / 'bin/jar'), 'cfm', str(agent), str(mf), '-C', str(harness), 'ProxyParentOracle.class'])
        manifest['runs'] = []
        for mode, options in (('interpreter', ['-Xint']), ('jit', ['-Xbatch', '-XX:+PrintCompilation'])):
            output = run('oracle-' + mode, [str(jbr / 'bin/java'), '-XX:+AllowEnhancedClassRedefinition',
                *options, '-javaagent:' + str(agent), '-cp', str(harness) + ':' + str(old),
                'ProxyParentOracle', str(patches)])
            match = re.search(r'JBR Proxy parent oracle PASS checks=(\d+)', output)
            if not match:
                raise RuntimeError(mode + ' did not complete its Proxy oracle')
            orders = dict(line.split('=', 1) for line in output.splitlines() if line.startswith('batchOrder'))
            if orders != {'batchOrder0': 'A,Added,Root,B', 'batchOrder1': 'B,Root,Added,A'}:
                raise RuntimeError('Missing both reverse batch order observations')
            if mode == 'jit' and not all('ProxyParentsFixture::' + name in output
                    for name in ('sharedFailures', 'addedSum', 'rootSum')):
                raise RuntimeError('Missing hot Proxy dispatch compilation evidence')
            manifest['runs'].append({'mode': mode, 'checks': int(match.group(1)), 'batchOrders': orders})
        manifest['passed'] = True
        manifest['checks'] = sum(item['checks'] for item in manifest['runs'])
        (evidence / 'summary.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print('JBR Proxy parent oracle PASS checks=' + str(manifest['checks']) + ' modes=2')
        print('Evidence: ' + str(evidence.relative_to(ROOT)))


if __name__ == '__main__':
    main()
