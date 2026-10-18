#!/usr/bin/env python3
"""Compare interface-narrowing regressions with an enhanced-redefinition JBR."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
from evidence_paths import new_evidence_directory

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
import java.lang.instrument.*;
import java.nio.file.*;
import com.github.ytlog.artdcevm.fixture.*;
public class RemovalOracle {
  public static Instrumentation instrumentation;
  public static void premain(String args, Instrumentation value) { instrumentation = value; }
  static Path patches;
  static final int TIMES = 200000;
  static int checks;
  static void eq(String name, Object expected, Object actual) {
    if (!expected.equals(actual)) throw new AssertionError(name + ": expected=" + expected + " actual=" + actual);
    checks++;
  }
  static void icce(String name, Runnable action) {
    try { action.run(); } catch (IncompatibleClassChangeError expected) { checks++; return; }
    throw new AssertionError(name + " did not throw IncompatibleClassChangeError");
  }
  static byte[] patch(String variant, Class<?> type) throws Exception {
    return Files.readAllBytes(patches.resolve(variant).resolve(type.getName().replace('.', '/') + ".class"));
  }
  static void redefine(String variant, Class<?>... types) throws Exception {
    ClassDefinition[] definitions = new ClassDefinition[types.length];
    for (int i = 0; i < types.length; ++i) definitions[i] = new ClassDefinition(types[i], patch(variant, types[i]));
    instrumentation.redefineClasses(definitions);
  }
  public static void main(String[] args) throws Exception {
    patches = Path.of(args[0]);
    var subject = new InterfaceRemovalFixture.Subject();
    var child = new InterfaceRemovalFixture.Child();
    var task = new InterfaceRemovalFixture.Task();
    var thread = new Thread(task);
    eq("warm", 0, InterfaceRemovalFixture.failures(subject, TIMES));
    InterfaceRemovalFixture.Subject.self = subject;
    InterfaceRemovalFixture.Subject.keptSelf = subject;
    redefine("interface-remove", InterfaceRemovalFixture.Subject.class, InterfaceRemovalFixture.Task.class);
    eq("static-cleared", true, InterfaceRemovalFixture.Subject.class.getField("self").get(null) == null);
    eq("static-kept", true, InterfaceRemovalFixture.Subject.keptSelf == subject);
    eq("removed", TIMES, InterfaceRemovalFixture.failures(subject, TIMES));
    thread.run();
    eq("old-thread-cleared-runs", 0, task.runs);
    eq("old-thread-cleared-spins", 0, task.spins);
    try { InterfaceRemovalFixture.callDefault(subject); throw new AssertionError("default still present"); }
    catch (NoSuchMethodError expected) { checks++; }
    var late = (InterfaceRemovalFixture.Subject) Class.forName(
        "com.github.ytlog.artdcevm.fixture.InterfaceRemovalFixture$LateChild").getConstructor().newInstance();
    eq("late-kept", 22, InterfaceRemovalFixture.kept(late));
    var override = (InterfaceRemovalFixture.Subject) Class.forName(
        "com.github.ytlog.artdcevm.fixture.InterfaceRemovalFixture$LateOverride").getConstructor().newInstance();
    eq("override", 44, InterfaceRemovalFixture.kept(override));
    eq("parent-cache", 22, InterfaceRemovalFixture.kept(late));
    eq("override-removed", TIMES, InterfaceRemovalFixture.failures(override, TIMES));
    redefine("interface-remove-again", InterfaceRemovalFixture.Subject.class);
    eq("added", 0L, InterfaceRemovalFixture.Subject.class.getField("added").getLong(subject));
    eq("repeat", TIMES, InterfaceRemovalFixture.failures(subject, TIMES));
    eq("repeat-child", TIMES, InterfaceRemovalFixture.failures(child, TIMES));
    redefine("interface-remove", InterfaceRemovalFixture.Kept.class);
    eq("interface-group", TIMES, InterfaceRemovalFixture.failures(subject, TIMES));
    redefine("interface-remove-original", InterfaceRemovalFixture.Subject.class, InterfaceRemovalFixture.Task.class);
    eq("restore", 0, InterfaceRemovalFixture.failures(subject, TIMES));
    eq("late-restore", 0, InterfaceRemovalFixture.failures(late, TIMES));
    var existing = new AbstractRemovalFixture.Existing();
    var existingTask = new AbstractRemovalFixture.ExistingTask();
    var before = new Thread(existingTask);
    before.run();
    eq("abstract-warm", 0, AbstractRemovalFixture.failures(existing, TIMES));
    try {
      instrumentation.redefineClasses(
          new ClassDefinition(AbstractRemovalFixture.Subject.class, patch("abstract-remove", AbstractRemovalFixture.Subject.class)),
          new ClassDefinition(AbstractRemovalFixture.Task.class, new byte[]{0, 1}));
      throw new AssertionError("invalid batch accepted");
    } catch (ClassFormatError | InternalError expected) {
      System.out.println("JBR invalid batch rejection=" + expected.getClass().getSimpleName());
      checks++;
    }
    eq("abort-keeps", 0, AbstractRemovalFixture.failures(existing, TIMES));
    redefine("abstract-remove", AbstractRemovalFixture.Subject.class, AbstractRemovalFixture.Task.class);
    eq("abstract-existing", TIMES, AbstractRemovalFixture.failures(existing, TIMES));
    var indirect = (AbstractRemovalFixture.Subject) Class.forName(
        "com.github.ytlog.artdcevm.fixture.AbstractRemovalFixture$LateChild").getConstructor().newInstance();
    var direct = (AbstractRemovalFixture.Subject) Class.forName(
        "com.github.ytlog.artdcevm.fixture.AbstractRemovalFixture$LateDirect").getConstructor().newInstance();
    eq("abstract-indirect", TIMES, AbstractRemovalFixture.failures(indirect, TIMES));
    eq("abstract-direct", TIMES, AbstractRemovalFixture.failures(direct, TIMES));
    eq("abstract-direct-kept", 55, AbstractRemovalFixture.kept(direct));
    eq("abstract-parent-cache", 22, AbstractRemovalFixture.kept(existing));
    var lateTask = (AbstractRemovalFixture.Task) Class.forName(
        "com.github.ytlog.artdcevm.fixture.AbstractRemovalFixture$LateTask").getConstructor().newInstance();
    var after = AbstractRemovalFixture.thread(lateTask);
    icce("abstract-late-boot", after::run);
    eq("abstract-late-spins", 0, lateTask.spins);
    before.run();
    eq("abstract-existing-runs", 1, existingTask.runs);
    redefine("abstract-remove-again", AbstractRemovalFixture.Subject.class);
    eq("abstract-added", 0L, AbstractRemovalFixture.Subject.class.getField("added").getLong(existing));
    eq("abstract-repeat", TIMES, AbstractRemovalFixture.failures(indirect, TIMES));
    redefine("abstract-remove", AbstractRemovalFixture.Kept.class);
    eq("abstract-group", TIMES, AbstractRemovalFixture.failures(indirect, TIMES));
    System.gc();
    eq("abstract-gc-marker", 5, indirect.marker);
    redefine("abstract-remove-original", AbstractRemovalFixture.Subject.class, AbstractRemovalFixture.Task.class);
    eq("abstract-restored", 0, AbstractRemovalFixture.failures(indirect, TIMES));
    System.out.println("JBR interface removal oracle PASS checks=" + checks);
  }
}
'''


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--java-home', type=Path, required=True)
    args = p.parse_args()
    evidence = new_evidence_directory(ROOT / 'build/evidence/jbr-interface-removal')
    jbr = args.java_home.resolve()
    manifest = {'scenarios': ['repeated-structure', 'interface-group', 'late-override',
                             'abstract-direct-indirect', 'abstract-boot', 'abort-retry', 'restore'],
                'sourceOracleRevision': 'cf1f197170a0', 'fixtureSha256': {}}
    with tempfile.TemporaryDirectory(prefix='oracle-', dir=evidence) as temp:
        work = Path(temp)
        def run(name, cmd):
            result = subprocess.run(cmd, capture_output=True, text=True)
            (evidence / (name + '.log')).write_text(result.stdout + result.stderr)
            if result.returncode:
                raise RuntimeError(name + ' failed; inspect ignored evidence')
            return result.stdout + result.stderr
        manifest['runtimeVersion'] = run('version', [str(jbr / 'bin/java'), '-version']).strip()
        manifest['sourceMatchVerified'] = 'JBR-21.0.11+10-1163.116' in manifest['runtimeVersion']
        old = work / 'old'; old.mkdir()
        fixtures = ROOT / 'app/src/debug/java/com/github/ytlog/artdcevm/fixture'
        names = ['InterfaceRemovalFixture.java', 'AbstractRemovalFixture.java']
        run('compile-old', [str(jbr / 'bin/javac'), '--release', '11', '-d', str(old),
                            *[str(fixtures / name) for name in names]])
        patches = work / 'patches'; patches.mkdir()
        for variant in ['interface-remove', 'interface-remove-again', 'interface-remove-original',
                        'abstract-remove', 'abstract-remove-again', 'abstract-remove-original']:
            source = next((ROOT / 'fixtures' / variant).rglob('*.java'))
            manifest['fixtureSha256'][variant] = hashlib.sha256(source.read_bytes()).hexdigest()
            run('compile-' + variant, [str(jbr / 'bin/javac'), '--release', '11', '-d',
                                       str(patches / variant), str(source)])
        source = work / 'RemovalOracle.java'; source.write_text(SOURCE)
        (evidence / 'RemovalOracle.java').write_text(SOURCE)
        harness = work / 'harness'; harness.mkdir()
        run('compile-oracle', [str(jbr / 'bin/javac'), '--release', '11', '-cp', str(old),
                              '-d', str(harness), str(source)])
        mf = work / 'manifest.mf'; mf.write_text('Premain-Class: RemovalOracle\nCan-Redefine-Classes: true\n\n')
        agent = work / 'agent.jar'
        run('agent', [str(jbr / 'bin/jar'), 'cfm', str(agent), str(mf), '-C', str(harness), 'RemovalOracle.class'])
        output = run('oracle', [str(jbr / 'bin/java'), '-XX:+AllowEnhancedClassRedefinition',
                               '-javaagent:' + str(agent), '-cp', str(harness) + ':' + str(old),
                               'RemovalOracle', str(patches)])
        manifest['passed'] = True
        (evidence / 'summary.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print(next(line for line in output.splitlines() if line.startswith('JBR interface removal oracle PASS')))
        print('Evidence: ' + str(evidence.relative_to(ROOT)))


if __name__ == '__main__':
    main()
