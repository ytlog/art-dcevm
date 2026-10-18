#!/usr/bin/env python3
"""Compare static/instance call errors with the fixed enhanced-redefinition JBR."""
import argparse
import hashlib
import json
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
    patches = Path.of(args[0]); Subject subject = new Subject(); Child child = new Child();
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
                'harnessSha256': hashlib.sha256(SOURCE.encode()).hexdigest()}
    with tempfile.TemporaryDirectory(prefix='oracle-', dir=evidence) as temp:
        work = Path(temp)

        def run(name, command):
            result = subprocess.run(command, capture_output=True, text=True)
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
        run('compile-old', [str(jbr / 'bin/javac'), '--release', '11', '-d', str(old), str(source), str(interface_source)])
        patches = work / 'patches'
        for variant in ('invoke-kind-static', 'invoke-kind-instance', 'invoke-kind-original', 'invoke-kind-caller-rebuild', 'interface-kind-static', 'interface-kind-static-restore'):
            source = ROOT / 'fixtures' / variant / ('com/github/ytlog/artdcevm/fixture/InterfaceInvokeKindFixture.java' if variant.startswith('interface-') else 'com/github/ytlog/artdcevm/fixture/InvokeKindFixture.java')
            manifest['fixtureSha256'][variant] = hashlib.sha256(source.read_bytes()).hexdigest()
            run('compile-' + variant, [str(jbr / 'bin/javac'), '--release', '11',
                                      '-d', str(patches / variant), str(source)])
        source = evidence / 'InvokeKindOracle.java'
        source.write_text(SOURCE)
        harness = work / 'harness'
        run('compile-oracle', [str(jbr / 'bin/javac'), '--release', '11', '-cp', str(old),
                               '-d', str(harness), str(source)])
        mf = work / 'manifest.mf'
        mf.write_text('Premain-Class: InvokeKindOracle\nCan-Redefine-Classes: true\n\n')
        agent = work / 'agent.jar'
        run('agent', [str(jbr / 'bin/jar'), 'cfm', str(agent), str(mf), '-C', str(harness), 'InvokeKindOracle.class'])
        output = run('oracle', [str(jbr / 'bin/java'), '-XX:+AllowEnhancedClassRedefinition',
                               '-javaagent:' + str(agent), '-cp', str(harness) + ':' + str(old),
                               'InvokeKindOracle', str(patches)])
        manifest['passed'] = True
        manifest['checks'] = int(output.split('PASS checks=')[-1].strip())
        (evidence / 'summary.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print('JBR invoke kind oracle PASS checks=' + str(manifest['checks']))
        print('Evidence: ' + str(evidence.relative_to(ROOT)))


if __name__ == '__main__':
    main()
