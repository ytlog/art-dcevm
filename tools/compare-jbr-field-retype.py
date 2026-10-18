#!/usr/bin/env python3
"""Compare instance reference retyping with the fixed enhanced-redefinition JBR."""
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
import java.lang.reflect.Proxy;
import java.nio.file.*;
import com.github.ytlog.artdcevm.fixture.RetypeFixture;
public class RetypeOracle {
  public static Instrumentation instrumentation;
  public static void premain(String args, Instrumentation value) { instrumentation = value; }
  static int checks;
  static void eq(String name, Object expected, Object actual) {
    if (!expected.equals(actual)) throw new AssertionError(name + ": expected=" + expected + " actual=" + actual);
    checks++;
  }
  static Object get(String field, Object instance) throws Exception {
    return RetypeFixture.Subject.class.getField(field).get(instance);
  }
  static void redefine(Path patches, String variant) throws Exception {
    String name = RetypeFixture.Subject.class.getName().replace('.', '/') + ".class";
    instrumentation.redefineClasses(new ClassDefinition(RetypeFixture.Subject.class,
        Files.readAllBytes(patches.resolve(variant).resolve(name))));
  }
  public static void main(String[] args) throws Exception {
    var subject = new RetypeFixture.Subject();
    var child = new RetypeFixture.Child();
    Object text = subject.text, number = subject.number, task = subject.task, inherited = subject.inherited;
    Object childTask = child.task;
    Object proxy = Proxy.newProxyInstance(RetypeFixture.class.getClassLoader(),
        new Class<?>[]{Runnable.class}, (receiver, method, values) -> null);
    Object indirect = Proxy.newProxyInstance(RetypeFixture.class.getClassLoader(),
        new Class<?>[]{RetypeFixture.SubTask.class}, (receiver, method, values) -> null);
    subject.proxy = child.proxy = proxy;
    subject.indirectProxy = child.indirectProxy = indirect;
    subject.proxyBase = child.proxyBase = proxy;
    eq("direct-proxy", true, Proxy.isProxyClass(proxy.getClass()));
    eq("indirect-proxy", true, Proxy.isProxyClass(indirect.getClass()));
    Path patches = Path.of(args[0]);
    for (int round = 0; round < 2; round++) {
      redefine(patches, "field-retype");
      eq("text-type", CharSequence.class, RetypeFixture.Subject.class.getField("text").getType());
      eq("text", true, get("text", subject) == (round == 0 ? text : null));
      eq("number", true, get("number", subject) == number);
      eq("mismatch", true, get("mismatch", subject) == null);
      eq("direct-interface", true, get("task", subject) == task);
      eq("indirect-interface", true, get("deep", subject) == null);
      eq("inherited-interface", true, get("inherited", subject) == inherited);
      eq("static", true, get("label", null) == null);
      eq("static-mismatch", true, get("staticMismatch", null) == null);
      for (Object holder : new Object[]{subject, child}) {
        eq("proxy", true, get("proxy", holder) == proxy);
        eq("indirect-proxy", true, get("indirectProxy", holder) == null);
        eq("proxy-base", true, get("proxyBase", holder) == proxy);
      }
      eq("child-text", true, get("text", child) == text);
      eq("child-task", true, get("task", child) == childTask);
      eq("keep", 41, subject.keep);
      eq("child-own", 17, child.own);
      System.gc();
      eq("proxy-after-gc", true, get("proxy", subject) == proxy);
      eq("child-proxy-after-gc", true, get("proxy", child) == proxy);
      RetypeFixture.Subject.class.getField("text").set(subject, new StringBuilder("builder"));
      redefine(patches, "field-retype-restore");
      eq("text-restored-type", String.class, RetypeFixture.Subject.class.getField("text").getType());
      eq("builder-cleared", true, subject.text == null);
      eq("child-text-restored", true, child.text == text);
      eq("number-restored", true, subject.number == number);
      eq("interface-restored", true, subject.task == task);
      for (RetypeFixture.Subject holder : new RetypeFixture.Subject[]{subject, child}) {
        eq("proxy-restored", true, holder.proxy == proxy);
        eq("proxy-base-restored", true, holder.proxyBase == proxy);
        eq("indirect-proxy-restored", true, holder.indirectProxy == null);
      }
    }
    System.out.println("JBR field retype oracle PASS checks=" + checks);
  }
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--java-home', type=Path, required=True)
    args = parser.parse_args()
    jbr = args.java_home.resolve()
    evidence = new_evidence_directory(ROOT / 'build/evidence/jbr-field-retype')
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
        source = ROOT / 'app/src/debug/java/com/github/ytlog/artdcevm/fixture/RetypeFixture.java'
        manifest['fixtureSha256']['original'] = hashlib.sha256(source.read_bytes()).hexdigest()
        run('compile-old', [str(jbr / 'bin/javac'), '--release', '11', '-d', str(old), str(source)])
        patches = work / 'patches'
        for variant in ('field-retype', 'field-retype-restore'):
            source = ROOT / 'fixtures' / variant / 'com/github/ytlog/artdcevm/fixture/RetypeFixture.java'
            manifest['fixtureSha256'][variant] = hashlib.sha256(source.read_bytes()).hexdigest()
            run('compile-' + variant, [str(jbr / 'bin/javac'), '--release', '11',
                                      '-d', str(patches / variant), str(source)])
        source = evidence / 'RetypeOracle.java'
        source.write_text(SOURCE)
        harness = work / 'harness'
        run('compile-oracle', [str(jbr / 'bin/javac'), '--release', '11', '-cp', str(old),
                               '-d', str(harness), str(source)])
        mf = work / 'manifest.mf'
        mf.write_text('Premain-Class: RetypeOracle\nCan-Redefine-Classes: true\n\n')
        agent = work / 'agent.jar'
        run('agent', [str(jbr / 'bin/jar'), 'cfm', str(agent), str(mf), '-C', str(harness), 'RetypeOracle.class'])
        output = run('oracle', [str(jbr / 'bin/java'), '-XX:+AllowEnhancedClassRedefinition',
                               '-javaagent:' + str(agent), '-cp', str(harness) + ':' + str(old),
                               'RetypeOracle', str(patches)])
        manifest['passed'] = True
        manifest['checks'] = int(output.split('PASS checks=')[-1].strip())
        (evidence / 'summary.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print('JBR field retype oracle PASS checks=' + str(manifest['checks']))
        print('Evidence: ' + str(evidence.relative_to(ROOT)))


if __name__ == '__main__':
    main()
