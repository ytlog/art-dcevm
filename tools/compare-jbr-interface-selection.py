#!/usr/bin/env python3
"""Compare non-public interface selection with the fixed JBR oracle."""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import subprocess
import tempfile

from evidence_paths import new_evidence_directory

ROOT = Path(__file__).resolve().parents[1]
REVISION = 'cf1f197170a0f04c6559feb873e362583be021d1'
SOURCE = r'''
import java.lang.instrument.*;
import java.lang.reflect.*;
import java.lang.invoke.*;
import java.net.*;
import java.nio.file.*;
import com.github.ytlog.artdcevm.fixture.InterfaceEvolutionFixture;
public class InterfaceSelectionOracle {
  static Instrumentation inst;
  static int checks;
  static Path patches;
  static final String BASE = "com.github.ytlog.artdcevm.fixture.";
  public static void premain(String args, Instrumentation value) { inst = value; }
  static void eq(Object expected, Object actual) {
    ++checks;
    if (!expected.equals(actual)) throw new AssertionError("expected " + expected + ", got " + actual);
  }
  interface Action { void run() throws Throwable; }
  static void error(Class<?> expected, Action action) throws Throwable {
    try { action.run(); }
    catch (Throwable actual) {
      if (actual instanceof InvocationTargetException) actual = actual.getCause();
      eq(expected, actual.getClass()); return;
    }
    throw new AssertionError("missing " + expected.getName());
  }
  static ClassDefinition definition(Path dir, Class<?> type) throws Exception {
    return new ClassDefinition(type, Files.readAllBytes(dir.resolve(type.getName().replace('.', '/') + ".class")));
  }
  static void change(String variant, Class<?>... types) throws Exception {
    ClassDefinition[] definitions = new ClassDefinition[types.length];
    for (int n = 0; n < types.length; ++n) definitions[n] = definition(patches.resolve(variant), types[n]);
    inst.redefineClasses(definitions); ++checks;
  }
  public static void main(String[] args) throws Throwable {
    patches = Paths.get(args[0]); Path old = Paths.get(args[1]);
    String[] kinds = {"static", "private", "protected", "package", "other-package"};
    String[] names = {"Static", "Private", "Protected", "Package", "OtherPackage"};
    for (int n = 0; n < kinds.length; ++n) {
      Class<?> type = Class.forName(BASE + "InterfaceEvolutionFixture$" + names[n] + "Shadow");
      Object receiver = Class.forName(BASE + "InterfaceEvolutionFixture$" + names[n] + "Impl").getConstructor().newInstance();
      Method stable = type.getMethod("stable");
      Object plain = n == 2 ? new InterfaceEvolutionFixture.ProtectedPlain() : null;
      Method oldCall = n == 0 ? InterfaceEvolutionFixture.StaticBase.class.getMethod("shadow")
          : receiver.getClass().getMethod("oldCall");
      for (int round = 0; round < 2; ++round) {
        System.out.println("selection " + kinds[n] + " round=" + round);
        change("interface-shadow-" + kinds[n], type);
        Method shadow = type.getMethod("shadow"), caller = type.getMethod("call", type);
        Method hot = type.getMethod("hot", type, int.class);
        MethodHandle handle = MethodHandles.publicLookup().unreflect(shadow);
        Object late = Class.forName(BASE + "InterfaceEvolutionFixture$" + names[n] + "Late").getConstructor().newInstance();
        if (n >= 2) {
          error(IllegalAccessError.class, () -> shadow.invoke(receiver));
          error(IllegalAccessError.class, () -> caller.invoke(null, receiver));
          error(IllegalAccessError.class, () -> handle.invokeWithArguments(receiver));
          error(IllegalAccessError.class, () -> caller.invoke(null, late));
          eq(30000, hot.invoke(null, receiver, 30000));
        } else {
          eq(1, shadow.invoke(receiver)); eq(1, caller.invoke(null, receiver));
          eq(1, handle.invokeWithArguments(receiver));
          if (n == 1) error(AbstractMethodError.class, () -> caller.invoke(null, late));
          else eq(1, caller.invoke(null, late));
          eq(0, hot.invoke(null, receiver, 30000));
        }
        if (n == 2) {
          Object override = Class.forName(BASE + "InterfaceEvolutionFixture$ProtectedPublic").getConstructor().newInstance();
          eq(103, caller.invoke(null, override));
        }
        eq(0, stable.invoke(receiver));
        eq(52 + 10 * n, oldCall.invoke(n == 0 ? null : receiver));
        System.gc(); eq(0, stable.invoke(receiver));
        if (n == 2) {
          change("interface-shadow-abstract", type);
          Method abstractShadow = type.getMethod("shadow");
          error(IllegalAccessError.class, () -> abstractShadow.invoke(receiver));
          error(AbstractMethodError.class, () -> abstractShadow.invoke(plain));
          change("interface-shadow-protected", type);
          eq(1, type.getMethod("shadow").invoke(plain));
          Class.forName(BASE + "InterfaceEvolutionFixture$ShadowParent");
          change("interface-shadow-parent", type);
          Method parentShadow = type.getMethod("shadow");
          error(IllegalAccessError.class, () -> parentShadow.invoke(receiver));
          eq(7, parentShadow.invoke(plain));
        }
        inst.redefineClasses(definition(old, type)); ++checks;
        eq(51 + 10 * n, stable.invoke(receiver));
        eq(52 + 10 * n, oldCall.invoke(n == 0 ? null : receiver));
      }
    }
    for (String kind : new String[]{"private", "package", "protected"}) {
      try (URLClassLoader loader = new URLClassLoader(new URL[]{Paths.get(args[2]).toUri().toURL()}, ClassLoader.getPlatformClassLoader())) {
        Class<?> contract = loader.loadClass(BASE + "abstractaddition.Contract");
        Class<?> impl = loader.loadClass(BASE + "abstractaddition.Implementation");
        Object receiver = impl.getConstructor().newInstance();
        inst.redefineClasses(definition(patches.resolve("interface-abstract-add"), contract),
            definition(patches.resolve("interface-abstract-" + kind), impl)); ++checks;
        Class<?> expected = kind.equals("private") ? AbstractMethodError.class : IllegalAccessError.class;
        Method required = contract.getMethod("required");
        MethodHandle handle = MethodHandles.publicLookup().unreflect(required);
        error(expected, () -> required.invoke(receiver));
        error(expected, () -> handle.invokeWithArguments(receiver));
        System.gc(); error(expected, () -> required.invoke(receiver));
        eq(12, contract.getMethod("oldValue").invoke(receiver));
        inst.redefineClasses(definition(patches.resolve("interface-abstract-add"), impl)); ++checks;
        eq("new", required.invoke(receiver));
      }
    }
    System.out.println("JBR interface selection oracle PASS checks=" + checks);
  }
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--java-home', type=Path, required=True)
    args = parser.parse_args()
    jbr = args.java_home.resolve()
    evidence = new_evidence_directory(ROOT / 'build/evidence/jbr-interface-selection')
    manifest = {'sourceOracleRevision': REVISION, 'fixtureSha256': {},
                'harnessSha256': hashlib.sha256(SOURCE.encode()).hexdigest()}
    builder = runpy.run_path(str(ROOT / 'tools/build-fixtures.py'))
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

        def compile_sources(label, sources, destination):
            for source in sources:
                manifest['fixtureSha256'][str(source.relative_to(ROOT))] = hashlib.sha256(source.read_bytes()).hexdigest()
            run('compile-' + label, [str(jbr / 'bin/javac'), '--release', '9', '-d', str(destination), *map(str, sources)])

        old = work / 'old'
        source_root = ROOT / 'app/src/debug/java/com/github/ytlog/artdcevm/fixture'
        compile_sources('old', [source_root / 'InterfaceEvolutionFixture.java', source_root / 'other/OtherPackageBase.java'], old)
        patches = work / 'patches'
        variants = ['interface-shadow-' + kind for kind in ('static', 'private', 'protected', 'package', 'other-package')]
        variants += ['interface-shadow-abstract', 'interface-shadow-parent', 'interface-abstract-original', 'interface-abstract-add', 'interface-abstract-private',
                     'interface-abstract-package', 'interface-abstract-protected']
        for variant in variants:
            directory = ROOT / 'fixtures' / variant
            out = patches / variant
            compile_sources(variant, sorted(directory.rglob('*.java')), out)
            removals_path, access_path = directory / 'remove-methods.json', directory / 'method-access.json'
            removals = json.loads(removals_path.read_text()) if removals_path.exists() else {}
            changes = json.loads(access_path.read_text()) if access_path.exists() else {}
            for recipe in (removals_path, access_path):
                if recipe.exists(): manifest['fixtureSha256'][str(recipe.relative_to(ROOT))] = hashlib.sha256(recipe.read_bytes()).hexdigest()
            for relative in removals.keys() | changes.keys():
                path = out / relative
                path.write_bytes(builder['strip_class_methods'](path.read_bytes(), removals.get(relative, []), changes.get(relative, [])))
        source = evidence / 'InterfaceSelectionOracle.java'
        source.write_text(SOURCE)
        harness = work / 'harness'
        run('compile-oracle', [str(jbr / 'bin/javac'), '--release', '11', '-cp', str(old), '-d', str(harness), str(source)])
        mf = work / 'manifest.mf'
        mf.write_text('Premain-Class: InterfaceSelectionOracle\nCan-Redefine-Classes: true\n\n')
        agent = work / 'agent.jar'
        run('agent', [str(jbr / 'bin/jar'), 'cfm', str(agent), str(mf), '-C', str(harness), 'InterfaceSelectionOracle.class'])
        output = run('oracle', [str(jbr / 'bin/java'), '-XX:+AllowEnhancedClassRedefinition', '-javaagent:' + str(agent),
                               '-cp', str(harness) + ':' + str(old), 'InterfaceSelectionOracle', str(patches), str(old),
                               str(patches / 'interface-abstract-original')])
        manifest['passed'] = True
        manifest['checks'] = int(output.split('PASS checks=')[-1].strip())
        (evidence / 'summary.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print('JBR interface selection oracle PASS checks=' + str(manifest['checks']))
        print('Evidence: ' + str(evidence.relative_to(ROOT)))


if __name__ == '__main__':
    main()
