#!/usr/bin/env python3
"""Compare Proxy parent evolution with the fixed enhanced-redefinition JBR."""

import argparse
import hashlib
import json
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
  static Instrumentation inst;
  static Path patches;
  static int checks;
  public static void premain(String args, Instrumentation value) { inst = value; }
  static void eq(String label, Object expected, Object actual) {
    checks++;
    if (!expected.equals(actual)) throw new AssertionError(label + ": expected " + expected + ", got " + actual);
  }
  interface Action { void run() throws Throwable; }
  static void error(Class<?> expected, Action action) throws Throwable {
    try { action.run(); }
    catch (Throwable actual) {
      if (actual instanceof InvocationTargetException) actual = actual.getCause();
      eq("exception", expected, actual.getClass()); return;
    }
    throw new AssertionError("missing " + expected.getName());
  }
  static void redefine(String variant, Class<?>... types) throws Exception {
    ClassDefinition[] definitions = new ClassDefinition[types.length];
    for (int i = 0; i < types.length; ++i) {
      byte[] bytes = Files.readAllBytes(patches.resolve(variant).resolve(types[i].getName().replace('.', '/') + ".class"));
      definitions[i] = new ClassDefinition(types[i], bytes);
    }
    inst.redefineClasses(definitions);
    checks++;
  }
  public static void main(String[] args) throws Throwable {
    patches = Paths.get(args[0]);
    Object proxy = ProxyParentsFixture.create();
    ProxyParentsFixture.Shared held = (ProxyParentsFixture.Shared) proxy;
    ProxyParentsFixture.Holder holder = new ProxyParentsFixture.Holder();
    holder.value = held;
    ProxyParentsFixture.Holder.staticValue = held;
    Class<?> A = ProxyParentsFixture.A.class, B = ProxyParentsFixture.B.class;
    Class<?> Added = ProxyParentsFixture.Added.class;
    ClassDefinition[] cycle = new ClassDefinition[] {
      new ClassDefinition(A, Files.readAllBytes(patches.resolve("proxy-parents-cycle-a").resolve(A.getName().replace('.', '/') + ".class"))),
      new ClassDefinition(B, Files.readAllBytes(patches.resolve("proxy-parents-cycle-b").resolve(B.getName().replace('.', '/') + ".class")))};
    error(ClassCircularityError.class, () -> inst.redefineClasses(cycle));
    eq("cycle-kept", 71, ProxyParentsFixture.shared(held));
    System.gc();
    eq("cycle-after-gc", 71, ProxyParentsFixture.shared(held));
    for (int round = 0; round < 2; ++round) {
      redefine("proxy-parents-a-drop", A);
      eq("shared", 71, ProxyParentsFixture.shared(held));
      eq("shared-parent", true, proxy instanceof ProxyParentsFixture.Shared);
      eq("cached-proxy", true, ProxyParentsFixture.create().getClass() == proxy.getClass());
      redefine("proxy-parents-drop", B);
      eq("dropped-parent", false, proxy instanceof ProxyParentsFixture.Shared);
      eq("cleared-reference", true, holder.value == null);
      error(IncompatibleClassChangeError.class, () -> ProxyParentsFixture.shared(held));
      error(IllegalArgumentException.class, () -> ProxyParentsFixture.Shared.class.getMethod("shared").invoke(proxy));
      eq("a", 11, ((ProxyParentsFixture.A) proxy).a());
      eq("b", 12, ((ProxyParentsFixture.B) proxy).b());
      System.gc();
      eq("fresh-dropped", false, ProxyParentsFixture.create() instanceof ProxyParentsFixture.Shared);
      redefine("proxy-parents-original", A, B);
      eq("restored", 71, ProxyParentsFixture.shared(held));
      eq("not-restored-reference", true, holder.value == null);
      eq("direct-interfaces", 2, proxy.getClass().getInterfaces().length);
      eq("serializable", true, proxy instanceof java.io.Serializable);
      redefine("proxy-parents-add", B, Added, A);
      eq("added-parent", true, proxy instanceof ProxyParentsFixture.Added);
      eq("added-body", 43, ProxyParentsFixture.added((ProxyParentsFixture.Added) proxy));
      eq("added-member", 44, Added.getMethod("extra").invoke(proxy));
      error(AbstractMethodError.class, () -> ((ProxyParentsFixture.Added) proxy).missing());
      eq("kept-handler", 71, ProxyParentsFixture.shared(held));
      Object fresh = ProxyParentsFixture.create();
      eq("same-class", true, fresh.getClass() == proxy.getClass());
      System.gc();
      eq("fresh-body", 43, ProxyParentsFixture.added((ProxyParentsFixture.Added) fresh));
      redefine("proxy-parents-added-b", A);
      eq("shared-added-parent", 43, ProxyParentsFixture.added((ProxyParentsFixture.Added) proxy));
      eq("kept-shared", 71, ProxyParentsFixture.shared(held));
      redefine("proxy-parents-original", A, B, Added);
      eq("removed-added-parent", false, proxy instanceof ProxyParentsFixture.Added);
      eq("shared-final", 71, ProxyParentsFixture.shared(held));
      System.gc();
      eq("fresh-final", false, ProxyParentsFixture.create() instanceof ProxyParentsFixture.Added);
      holder.value = held;
    }
    System.out.println("JBR Proxy parent oracle PASS checks=" + checks);
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
        output = run('oracle', [str(jbr / 'bin/java'), '-XX:+AllowEnhancedClassRedefinition',
                               '-javaagent:' + str(agent), '-cp', str(harness) + ':' + str(old),
                               'ProxyParentOracle', str(patches)])
        manifest['passed'] = True
        manifest['checks'] = int(output.split('PASS checks=')[-1].strip())
        (evidence / 'summary.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print('JBR Proxy parent oracle PASS checks=' + str(manifest['checks']))
        print('Evidence: ' + str(evidence.relative_to(ROOT)))


if __name__ == '__main__':
    main()
