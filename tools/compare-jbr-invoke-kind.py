#!/usr/bin/env python3
"""Compare invocation errors after static/instance/private changes with the fixed enhanced-redefinition JBR."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import subprocess
import tempfile

from evidence_paths import new_evidence_directory

ROOT = Path(__file__).resolve().parents[1]
SOURCE_REVISION = 'cf1f197170a0f04c6559feb873e362583be021d1'
SOURCE = r'''
import java.lang.instrument.*;
import java.nio.file.*;
import java.util.concurrent.*;
import com.github.ytlog.artdcevm.fixture.InvokeKindFixture.*;
import com.github.ytlog.artdcevm.fixture.InterfaceInvokeKindFixture;
import com.github.ytlog.artdcevm.fixture.AccessSubject;
import com.github.ytlog.artdcevm.fixture.PrivateSelfFixture;
import com.github.ytlog.artdcevm.fixture.AccessCaller;
import com.github.ytlog.artdcevm.fixture.AccessCallerBase;
public class InvokeKindOracle {
  public static Instrumentation agent;
  public static void premain(String args, Instrumentation value) { agent = value; }
  static int checks;
  static void eq(String name, Object expected, Object actual) {
    if (!expected.equals(actual)) throw new AssertionError(name + ": " + actual);
    checks++;
  }
  static void icce(String name, Runnable action) {
    try { action.run(); } catch (IncompatibleClassChangeError expected) {
      if (expected.getClass() != IncompatibleClassChangeError.class)
        throw new AssertionError(name + " threw " + expected.getClass().getName(), expected);
      checks++; return;
    }
    throw new AssertionError(name + " did not throw IncompatibleClassChangeError");
  }
  static void error(String name, Class<? extends Throwable> type, Runnable action) {
    try { action.run(); } catch (Throwable actual) {
      if (actual.getClass() != type) throw new AssertionError(name + ": " + actual, actual);
      checks++; return;
    }
    throw new AssertionError(name + " did not throw " + type.getName());
  }
  static void privateSelf() throws Exception {
    for (int round = 0; round < 2; round++) {
      for (boolean iface : new boolean[]{false, true}) {
        var subject = new PrivateSelfFixture.Subject(); var child = new PrivateSelfFixture.Child();
        var impl = new PrivateSelfFixture.Impl(); var over = new PrivateSelfFixture.Override();
        Class<?> target = iface ? PrivateSelfFixture.Contract.class : PrivateSelfFixture.Subject.class;
        CountDownLatch open = new CountDownLatch(0);
        for (int i = 0; i < 30000; i++) {
          if ((iface ? impl.await(open, open, 0) : subject.await(open, open, 0)) != (iface ? 11 : 7) ||
              (iface ? over.await(open, open, 0) : child.await(open, open, 0)) != (iface ? 49 : 47) ||
              (iface ? impl.await(open, open, 1) : subject.await(open, open, 1)) != (iface ? 6 : 11))
            throw new AssertionError("own warmup");
        }
        CountDownLatch ready = new CountDownLatch(4), resume = new CountDownLatch(1);
        Thread[] workers = new Thread[4]; Object[] results = new Object[4];
        for (int i = 0; i < workers.length; i++) {
          final int slot = i;
          workers[i] = new Thread(() -> {
            try {
              int mode = slot == 2 ? 1 : slot == 3 ? 2 : 0;
              results[slot] = iface ? (slot == 1 ? over : impl).await(ready, resume, mode)
                  : (slot == 1 ? child : subject).await(ready, resume, mode);
            } catch (Throwable error) { results[slot] = error.getClass().getName(); }
          }); workers[i].start();
        }
        try {
          if (!ready.await(10, TimeUnit.SECONDS)) throw new AssertionError("own not ready");
          if (round == 1) {
            change("method-private-self-retained", target); System.gc();
            eq("own new body", iface ? 1011 : 1007, iface ? impl.await(open, open, 0) : subject.await(open, open, 0));
          }
          change("method-private-self", target); System.gc();
        } finally { resume.countDown(); for (Thread worker : workers) worker.join(10000); }
        for (Thread worker : workers) eq("own completed", false, worker.isAlive());
        eq("own old frame", iface ? 31 : 25, results[0]);
        eq("own ignores override", iface ? 31 : 25, results[1]);
        eq("own range", iface ? 36 : 41, results[2]);
        eq("own null", NullPointerException.class.getName(), results[3]);
        eq("independent override", iface ? 49 : 47, iface ? over.target() : child.target());
        if (!iface) { eq("own entered", 3, PrivateSelfFixture.Subject.entered.get()); eq("own field", 5, subject.value); }
        change("method-private-self-original", target); PrivateSelfFixture.Subject.entered.set(0); System.gc();
        eq("own restore", iface ? 11 : 7, iface ? impl.await(open, open, 0) : subject.await(open, open, 0));
        eq("own override restore", iface ? 49 : 47, iface ? over.await(open, open, 0) : child.await(open, open, 0));
      }
    }
  }
  static void privateCalls() throws Exception {
    AccessSubject subject = new AccessSubject();
    AccessCaller.Child child = new AccessCaller.Child();
    AccessCaller.Override override = new AccessCaller.Override();
    // JBR can directly select the new private target from an obsolete self frame.
    // ART currently retains a 71 boundary for active target frames.
    CountDownLatch selfReady = new CountDownLatch(1), selfResume = new CountDownLatch(1);
    Object[] selfResult = new Object[1];
    Thread own = new Thread(() -> {
      try { selfResult[0] = subject.await(selfReady, selfResume); }
      catch (Throwable t) { selfResult[0] = t.getClass().getName(); }
    });
    own.start();
    try {
      if (!selfReady.await(10, TimeUnit.SECONDS)) throw new AssertionError("self not ready");
      change("method-private", AccessSubject.class);
    } finally { selfResume.countDown(); own.join(10000); }
    eq("obsolete self completed", false, own.isAlive());
    eq("obsolete self uses private", 25, selfResult[0]);
    change("method-private-original", AccessSubject.class); AccessSubject.entered = 0;
    for (int round = 0; round < 2; round++) {
      for (int i = 0; i < 30000; i++) {
        if (AccessCaller.callTarget(subject) != 7 || AccessCaller.inherited(child) != 7 ||
            child.throughSuper() != 7 || AccessCaller.range(subject) != 11 || AccessCaller.overridden(override) != 47)
          throw new AssertionError("private warmup");
      }
      CountDownLatch ready = new CountDownLatch(1), resume = new CountDownLatch(1);
      Throwable[] failure = new Throwable[1];
      Thread active = new Thread(() -> {
        try { AccessCaller.await(subject, ready, resume); } catch (Throwable t) { failure[0] = t; }
      });
      active.start();
      try {
        if (!ready.await(10, TimeUnit.SECONDS)) throw new AssertionError("external not ready");
        change("method-private", AccessSubject.class);
      } finally { resume.countDown(); active.join(10000); }
      eq("private active completed", false, active.isAlive());
      eq("private active error", true, failure[0] != null && failure[0].getClass() == IllegalAccessError.class);
      eq("private hot failures", 30000, AccessCaller.hotFailures(subject, 30000));
      for (int i = 0; i < 32; i++) {
        error("private virtual", IllegalAccessError.class, () -> AccessCaller.callTarget(subject));
        error("private inherited", IllegalAccessError.class, () -> AccessCaller.inherited(child));
        error("private super", IllegalAccessError.class, child::throughSuper);
        error("private range", IllegalAccessError.class, () -> AccessCaller.range(subject));
        error("private base overriding receiver", IllegalAccessError.class, () -> AccessCaller.callTarget(override));
      }
      error("private null receiver", IllegalAccessError.class, () -> AccessCaller.callTarget(null));
      eq("private body not entered", 0, AccessSubject.entered);
      eq("own call", 25, subject.callTarget());
      eq("own call ignores override", 25, override.callTarget());
      eq("independent override", 47, AccessCaller.overridden(override));
      var target = AccessSubject.class.getDeclaredMethod("target"); target.setAccessible(true);
      var range = AccessSubject.class.getDeclaredMethod("range", long.class, long.class, long.class); range.setAccessible(true);
      eq("new private", 25, target.invoke(subject)); eq("new private range", 41, range.invoke(subject,1L,2L,3L));
      System.gc(); error("private after GC", IllegalAccessError.class, () -> AccessCaller.callTarget(subject));
      eq("private entered", 4, AccessSubject.entered);
      change("method-private-caller-rebuild", AccessCallerBase.class);
      eq("private implicit hot failures", 30000, AccessCaller.hotFailures(subject, 30000));
      eq("private implicit entered", 4, AccessSubject.entered);
      change("method-private-delete", AccessSubject.class);
      error("private deleted", NoSuchMethodError.class, () -> AccessCaller.callTarget(subject));
      change("method-private", AccessSubject.class); AccessSubject.entered = 0;
      error("private readded", IllegalAccessError.class, () -> AccessCaller.callTarget(subject));
      eq("readded hot failures", 30000, AccessCaller.hotFailures(subject, 30000));
      eq("readded body not entered", 0, AccessSubject.entered);
      change("method-private-original", AccessSubject.class, AccessCallerBase.class);
      eq("private restored", 7, AccessCaller.callTarget(subject));
      eq("private restored range", 11, AccessCaller.range(subject));
      eq("private restored super", 7, child.throughSuper());
    }
    change("interface-kind-original", InterfaceInvokeKindFixture.Contract.class);
    var impl = new InterfaceInvokeKindFixture.Impl();
    var over = new AccessCaller.InterfaceOverride();
    for (int i = 0; i < 30000; i++) {
      if (AccessCaller.interfaceCall(impl) != 11 || AccessCaller.interfaceVirtual(impl) != 11 ||
          AccessCaller.interfaceCall(over) != 49) throw new AssertionError("private interface warmup");
    }
    change("interface-kind-private", InterfaceInvokeKindFixture.Contract.class);
    for (int i = 0; i < 32; i++) {
      error("private interface", IllegalAccessError.class, () -> AccessCaller.interfaceCall(impl));
      error("private interface overriding receiver", IllegalAccessError.class, () -> AccessCaller.interfaceCall(over));
      error("private default not inherited", NoSuchMethodError.class, () -> AccessCaller.interfaceVirtual(impl));
    }
    eq("private interface own call", 31, InterfaceInvokeKindFixture.Contract.class.getMethod("selfValue").invoke(impl));
    eq("private interface own call ignores override", 31, InterfaceInvokeKindFixture.Contract.class.getMethod("selfValue").invoke(over));
    eq("private interface independent override", 49, AccessCaller.interfaceOverride(over));
    System.gc(); error("private interface after GC", IllegalAccessError.class, () -> AccessCaller.interfaceCall(impl));
    change("interface-kind-original", InterfaceInvokeKindFixture.Contract.class);
    eq("private interface restored", 11, AccessCaller.interfaceCall(impl));
  }
  static Path patches;
  static void change(String variant, Class<?>... types) throws Exception {
    ClassDefinition[] definitions = new ClassDefinition[types.length];
    for (int i = 0; i < types.length; i++) definitions[i] = new ClassDefinition(types[i],
        Files.readAllBytes(patches.resolve(variant).resolve(types[i].getName().replace('.', '/') + ".class")));
    agent.redefineClasses(definitions);
  }
  static void mismatch(String variant, Subject subject, Child child, boolean makeStatic) throws Exception {
    CountDownLatch ready = new CountDownLatch(2), resume = new CountDownLatch(1);
    Throwable[] failure = new Throwable[2];
    Thread active = new Thread(() -> {
      try { Caller.await(subject, ready, resume); } catch (Throwable error) { failure[0] = error; }
    });
    Thread activeTarget = new Thread(() -> {
      try { Caller.awaitTarget(subject, ready, resume); } catch (Throwable error) { failure[1] = error; }
    });
    active.start(); activeTarget.start();
    if (!ready.await(10, TimeUnit.SECONDS)) throw new AssertionError("caller not ready");
    try { change(variant, Subject.class); }
    finally { resume.countDown(); active.join(10000); activeTarget.join(10000); }
    eq("active completed", false, active.isAlive() || activeTarget.isAlive());
    eq("active error", true, failure[0] != null && failure[0].getClass() == IncompatibleClassChangeError.class);
    eq("active target error", true, failure[1] != null && failure[1].getClass() == IncompatibleClassChangeError.class);
    eq("hot failures", 30000, Caller.hotFailures(subject, 30000));
    for (int i = 0; i < 32; i++) {
      icce("virtual", () -> Caller.call(subject));
      icce("inherited", () -> Caller.inherited(child));
      icce("range", () -> Caller.range(subject));
      icce("super", child::throughSuper);
    }
    eq("no incompatible entry", 0, Subject.entered);
    eq("new call", makeStatic ? 101 : 8, Subject.class.getMethod("convert", int.class).invoke(makeStatic ? null : subject, 1));
    eq("new range", makeStatic ? 106 : 13, Subject.class.getMethod("range", long.class, long.class, long.class).invoke(makeStatic ? null : subject, 1L, 2L, 3L));
    System.gc(); icce("after gc", () -> Caller.call(subject)); icce("range after gc", () -> Caller.range(subject));
    eq("entered", 2, Subject.entered); eq("marker", 7, subject.marker);
  }
  public static void main(String[] args) throws Exception {
    patches = Path.of(args[0]);
    if (args.length > 1) { privateSelf(); System.out.println("JBR invoke kind oracle PASS checks=" + checks); return; }
    Subject subject = new Subject(); Child child = new Child();
    try {
      String name = Subject.class.getName().replace('.', '/') + ".class";
      agent.redefineClasses(new ClassDefinition(Subject.class,
          Files.readAllBytes(patches.resolve("invoke-kind-static").resolve(name))),
          new ClassDefinition(Child.class, new byte[]{0, 1}));
      throw new AssertionError("invalid batch accepted");
    } catch (ClassFormatError | InternalError expected) { checks++; }
    eq("abort preserves virtual", 8, Caller.call(subject));
    eq("abort preserves range", 13, Caller.range(subject));
    for (int round = 0; round < 2; round++) {
      for (int i = 0; i < 30000; i++) {
        if (Caller.call(subject) != 8 || Caller.inherited(child) != 8 || Caller.range(subject) != 13 || child.throughSuper() != 8)
          throw new AssertionError("warmup");
      }
      mismatch("invoke-kind-static", subject, child, true);
      change("invoke-kind-caller-rebuild", CallerBase.class);
      eq("implicit rebuild hot failures", 30000, Caller.hotFailures(subject, 30000));
      icce("implicit rebuild old caller", () -> Caller.call(subject));
      eq("implicit rebuild entered", 2, Subject.entered);
      change("invoke-kind-static", Caller.class, Child.class);
      for (int i = 0; i < 30000; i++) {
        if (Caller.call(subject) != 101 || Caller.inherited(child) != 101 || Caller.range(subject) != 106 || child.throughSuper() != 101)
          throw new AssertionError("static warmup");
      }
      Subject.entered = 0;
      try {
        String name = Subject.class.getName().replace('.', '/') + ".class";
        agent.redefineClasses(new ClassDefinition(Subject.class,
            Files.readAllBytes(patches.resolve("invoke-kind-instance").resolve(name))),
            new ClassDefinition(Child.class, new byte[]{0, 1}));
        throw new AssertionError("reverse invalid batch accepted");
      } catch (ClassFormatError | InternalError expected) { checks++; }
      eq("reverse abort preserves static", 101, Caller.call(subject));
      Subject.entered = 0;
      mismatch("invoke-kind-instance", subject, child, false);
      change("invoke-kind-original", CallerBase.class);
      eq("static implicit rebuild hot failures", 30000, Caller.hotFailures(subject, 30000));
      icce("static implicit rebuild old caller", () -> Caller.call(subject));
      eq("static implicit rebuild entered", 2, Subject.entered);
      change("invoke-kind-original", Subject.class, Caller.class, Child.class, CallerBase.class);
      eq("restored virtual", 8, Caller.call(subject)); eq("restored range", 13, Caller.range(subject));
      eq("restored inherited", 8, Caller.inherited(child)); eq("restored super", 8, child.throughSuper());
      Subject.entered = 0;
    }
    var impl = new InterfaceInvokeKindFixture.Impl();
    var type = InterfaceInvokeKindFixture.Contract.class;
    eq("interface virtual before", 11, InterfaceInvokeKindFixture.Caller.call(impl));
    eq("interface before", 11, InterfaceInvokeKindFixture.Caller.interfaceCall(impl));
    change("interface-kind-static", type);
    for (int i = 0; i < 32; i++) {
      try { InterfaceInvokeKindFixture.Caller.call(impl); throw new AssertionError("old inherited default still found"); }
      catch (NoSuchMethodError expected) { checks++; }
      icce("old interface", () -> InterfaceInvokeKindFixture.Caller.interfaceCall(impl));
    }
    eq("interface new static", 21, type.getMethod("value").invoke(null));
    System.gc();
    try { InterfaceInvokeKindFixture.Caller.call(impl); throw new AssertionError("old default after GC"); }
    catch (NoSuchMethodError expected) { checks++; }
    icce("old interface after GC", () -> InterfaceInvokeKindFixture.Caller.interfaceCall(impl));
    change("interface-kind-static", type, InterfaceInvokeKindFixture.Caller.class);
    eq("interface static callers", 22, InterfaceInvokeKindFixture.Caller.call(impl));
    for (int i = 0; i < 30000; i++) {
      if (InterfaceInvokeKindFixture.Caller.call(impl) != 22 || InterfaceInvokeKindFixture.Caller.interfaceCall(impl) != 21)
        throw new AssertionError("interface static warmup");
    }
    change("interface-kind-static-restore", type);
    for (int i = 0; i < 32; i++) {
      icce("interface old static", () -> InterfaceInvokeKindFixture.Caller.call(impl));
      icce("interface old static direct", () -> InterfaceInvokeKindFixture.Caller.interfaceCall(impl));
    }
    eq("interface new default", 13, type.getMethod("value").invoke(impl));
    System.gc(); icce("interface old static after GC", () -> InterfaceInvokeKindFixture.Caller.call(impl));
    change("interface-kind-static-restore", InterfaceInvokeKindFixture.Caller.class);
    eq("interface restored virtual", 13, InterfaceInvokeKindFixture.Caller.call(impl));
    eq("interface restored", 13, InterfaceInvokeKindFixture.Caller.interfaceCall(impl));
    privateCalls(); privateSelf();
    System.out.println("JBR invoke kind oracle PASS checks=" + checks);
  }
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--java-home', type=Path, required=True)
    args = parser.parse_args()
    jbr = args.java_home.resolve()
    evidence = new_evidence_directory(ROOT / 'build/evidence/jbr-invoke-kind')
    manifest = {'sourceOracleRevision': SOURCE_REVISION, 'fixtureSha256': {},
                'harnessSha256': hashlib.sha256(SOURCE.encode()).hexdigest(),
                'toolSha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), 'runs': []}
    with tempfile.TemporaryDirectory(prefix='oracle-', dir=evidence) as temp:
        work = Path(temp)

        def run(name, command):
            try:
                result = subprocess.run(command, capture_output=True, text=True, timeout=180)
            except subprocess.TimeoutExpired as error:
                (evidence / (name + ".log")).write_bytes((error.stdout or b"") + (error.stderr or b""))
                raise RuntimeError(name + " timed out; inspect ignored evidence") from None
            (evidence / (name + '.log')).write_text(result.stdout + result.stderr)
            if result.returncode:
                raise RuntimeError(name + ' failed; inspect ignored evidence')
            return result.stdout + result.stderr

        manifest['runtimeVersion'] = run('version', [str(jbr / 'bin/java'), '-version']).strip()
        if 'JBR-21.0.11+10-1163.116' not in manifest['runtimeVersion']:
            raise RuntimeError('This oracle requires fixed JBR 21.0.11+10-b1163.116')
        manifest['javaExecutableSha256'] = hashlib.sha256((jbr / 'bin/java').read_bytes()).hexdigest()
        old = work / 'old'
        source = ROOT / 'app/src/debug/java/com/github/ytlog/artdcevm/fixture/InvokeKindFixture.java'
        manifest['fixtureSha256']['original'] = hashlib.sha256(source.read_bytes()).hexdigest()
        interface_source = source.with_name('InterfaceInvokeKindFixture.java')
        manifest['fixtureSha256']['original-interface'] = hashlib.sha256(interface_source.read_bytes()).hexdigest()
        run('compile-old', [str(jbr / 'bin/javac'), '--release', '11', '-d', str(old), str(source), str(interface_source), *map(str, (ROOT / 'app/src/debug/java/com/github/ytlog/artdcevm/fixture').glob('Access*.java')), str(ROOT / 'app/src/debug/java/com/github/ytlog/artdcevm/fixture/PrivateSelfFixture.java')])
        for source in (ROOT / 'app/src/debug/java/com/github/ytlog/artdcevm/fixture').glob('Access*.java'):
            manifest['fixtureSha256']['original-' + source.name] = hashlib.sha256(source.read_bytes()).hexdigest()
        manifest['fixtureSha256']['PrivateSelfFixture.java'] = hashlib.sha256((ROOT / 'app/src/debug/java/com/github/ytlog/artdcevm/fixture/PrivateSelfFixture.java').read_bytes()).hexdigest()
        patches = work / 'patches'
        for variant in ('invoke-kind-static', 'invoke-kind-instance', 'invoke-kind-original', 'invoke-kind-caller-rebuild', 'interface-kind-static', 'interface-kind-static-restore'):
            source = ROOT / 'fixtures' / variant / ('com/github/ytlog/artdcevm/fixture/InterfaceInvokeKindFixture.java' if variant.startswith('interface-') else 'com/github/ytlog/artdcevm/fixture/InvokeKindFixture.java')
            manifest['fixtureSha256'][variant] = hashlib.sha256(source.read_bytes()).hexdigest()
            run('compile-' + variant, [str(jbr / 'bin/javac'), '--release', '11',
                                      '-d', str(patches / variant), str(source)])
        for variant in ('method-private', 'method-private-original', 'method-private-delete',
                        'method-private-caller-rebuild', 'interface-kind-private', 'interface-kind-original',
                        'method-private-self', 'method-private-self-original', 'method-private-self-retained'):
            sources = sorted((ROOT / 'fixtures' / variant).rglob('*.java'))
            for source in sources:
                manifest['fixtureSha256'][variant + '/' + source.name] = hashlib.sha256(source.read_bytes()).hexdigest()
            run('compile-' + variant, [str(jbr / 'bin/javac'), '--release', '11',
                                      '-d', str(patches / variant), *map(str, sources)])
        source = evidence / 'InvokeKindOracle.java'
        source.write_text(SOURCE)
        harness = work / 'harness'
        run('compile-oracle', [str(jbr / 'bin/javac'), '--release', '11', '-cp', str(old),
                               '-d', str(harness), str(source)])
        mf = work / 'manifest.mf'
        mf.write_text('Premain-Class: InvokeKindOracle\nCan-Redefine-Classes: true\n\n')
        agent = work / 'agent.jar'
        run('agent', [str(jbr / 'bin/jar'), 'cfm', str(agent), str(mf), '-C', str(harness), 'InvokeKindOracle.class'])
        for mode, flags in (('default', []), ('self-interpreter', ['-Xint']),
                            ('self-compiled', ['-Xbatch', '-XX:-TieredCompilation', '-XX:+PrintCompilation',
                             '-XX:CompileCommand=compileonly,com.github.ytlog.artdcevm.fixture.PrivateSelfFixture*::*'])):
            output = run('oracle-' + mode, [str(jbr / 'bin/java'), *flags, '-XX:+AllowEnhancedClassRedefinition',
                         '-javaagent:' + str(agent), '-cp', str(harness) + ':' + str(old),
                         'InvokeKindOracle', str(patches), *([] if mode == 'default' else ['self'])])
            if mode == 'self-compiled' and not any(re.match(r'^\s*\d+\s+\d+\s+.*PrivateSelfFixture.*::await', line)
                                                  and 'made not compilable' not in line for line in output.splitlines()):
                raise RuntimeError('No compiled self caller observed; inspect ignored evidence')
            checks = int(output.split('PASS checks=')[-1].splitlines()[0].strip())
            manifest['runs'].append({'mode': mode, 'checks': checks, 'passed': True})
        manifest['passed'] = True
        manifest['checks'] = sum(item['checks'] for item in manifest['runs'])
        (evidence / 'summary.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print('JBR invoke kind oracle PASS checks=' + str(manifest['checks']))
        print('Evidence: ' + str(evidence.relative_to(ROOT)))


if __name__ == '__main__':
    main()
