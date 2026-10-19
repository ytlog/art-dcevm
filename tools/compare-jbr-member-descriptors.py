#!/usr/bin/env python3
"""Verify descriptive member flags against the pinned enhanced-redefinition JBR."""
import argparse
import hashlib
import json
import platform
from pathlib import Path
import runpy
import subprocess

from evidence_paths import new_evidence_directory

ROOT = Path(__file__).resolve().parents[1]
REVISION = 'cf1f197170a0f04c6559feb873e362583be021d1'
SOURCE = r'''
import java.lang.instrument.*;
import java.nio.file.*;
import com.github.ytlog.artdcevm.fixture.MemberDescriptorFixture;
import com.github.ytlog.artdcevm.fixture.MemberDescriptorState;
public class MemberDescriptorOracle {
  static Instrumentation inst;
  static int checks;
  public static void premain(String args, Instrumentation value) { inst = value; }
  static void eq(String name, Object expected, Object actual) {
    if (!expected.equals(actual)) throw new AssertionError(name + ": expected " + expected + ", got " + actual);
    ++checks;
  }
  static ClassDefinition definition(Class<?> type, Path directory) throws Exception {
    return new ClassDefinition(type, Files.readAllBytes(directory.resolve(type.getName().replace('.', '/') + ".class")));
  }
  static void change(Path directory, boolean fail) throws Exception {
    inst.redefineClasses(definition(MemberDescriptorFixture.Contract.class, directory),
      fail ? new ClassDefinition(MemberDescriptorFixture.Peer.class, new byte[]{0, 1})
           : definition(MemberDescriptorFixture.Peer.class, directory));
  }
  public static void main(String[] args) throws Exception {
    MemberDescriptorState state = new MemberDescriptorState();
    for (int i = 0; i < 30000; ++i) eq("warmup", 52, MemberDescriptorFixture.Calls.call(state.receiver));
    for (int round = 0; round < 2; ++round) {
      for (String kind : new String[]{"synthetic", "bridge", "varargs", "strict", "field", "all"}) {
        Path patch = Paths.get(args[0], "descriptor-" + kind), original = Paths.get(args[0], "descriptor-original");
        for (int phase = 0; phase < 4; ++phase) {
          boolean restoring = phase >= 2, fail = phase % 2 == 0;
          try { change(restoring ? original : patch, fail); if (fail) throw new AssertionError("batch accepted"); }
          catch (InternalError expected) { if (!fail || expected.getClass() != InternalError.class) throw expected; ++checks; }
          boolean changed = phase == 1 || phase == 2;
          state.verify(MemberDescriptorOracle::eq, "", kind, changed);
          System.gc();
          state.verify(MemberDescriptorOracle::eq, "gc", kind, changed);
          System.out.println("state " + round + "/" + kind + "/" + phase + " PASS");
        }
      }
    }
    System.out.println("PASS checks=" + checks);
  }
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--java-home', type=Path, required=True)
    args = parser.parse_args()
    java = args.java_home.resolve()
    evidence = new_evidence_directory(ROOT / 'build/evidence/jbr-member-descriptors')
    manifest = {'sourceOracleRevision': REVISION, 'inputs': {}, 'runs': []}
    def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
    def run(name, command):
        result = subprocess.run(list(map(str, command)), capture_output=True, text=True, cwd=evidence, timeout=120)
        (evidence / (name + '.log')).write_text(result.stdout + result.stderr)
        if result.returncode: raise RuntimeError(name + ' failed; inspect ignored evidence')
        return result.stdout + result.stderr
    version = run('version', [java / 'bin/java', '-version'])
    if 'JBR-21.0.11+10-1163.116' not in version: raise RuntimeError('Requires pinned JBR 21.0.11+10-b1163.116')
    manifest['javaSha256'] = sha(java / 'bin/java')
    manifest['jvmSha256'] = sha(java / 'lib/server' / ('libjvm.dylib' if platform.system() == 'Darwin' else 'libjvm.so'))
    manifest['toolSha256'] = sha(Path(__file__))
    manifest['inputs']['tools/build-fixtures.py'] = sha(ROOT / 'tools/build-fixtures.py')
    manifest['harnessSha256'] = hashlib.sha256(SOURCE.encode()).hexdigest()
    builder = runpy.run_path(str(ROOT / 'tools/build-fixtures.py'))
    fixture = ROOT / 'app/src/debug/java/com/github/ytlog/artdcevm/fixture'
    old = evidence / 'old'; patches = evidence / 'patches'
    sources = [fixture / (name + '.java') for name in ('MemberDescriptorFixture', 'MemberDescriptorState')]
    for path in sources: manifest['inputs'][str(path.relative_to(ROOT))] = sha(path)
    run('compile-old', [java / 'bin/javac', '--release', '9', '-d', old, *sources])
    for kind in ('synthetic', 'bridge', 'varargs', 'strict', 'field', 'all', 'original'):
        variant = 'descriptor-' + kind; directory = ROOT / 'fixtures' / variant; output = patches / variant
        sources = sorted(directory.rglob('*.java'))
        run('compile-' + variant, [java / 'bin/javac', '--release', '9', '-d', output, *sources])
        for path in sources: manifest['inputs'][str(path.relative_to(ROOT))] = sha(path)
        for recipe, keyword in (('class-method-access.json', 'access_changes'), ('class-field-access.json', 'field_access_changes')):
            path = directory / recipe
            if not path.exists(): continue
            manifest['inputs'][str(path.relative_to(ROOT))] = sha(path)
            for relative, changes in json.loads(path.read_text()).items():
                klass = output / relative
                klass.write_bytes(builder['strip_class_methods'](klass.read_bytes(), [], **{keyword: changes}))
    source = evidence / 'MemberDescriptorOracle.java'; source.write_text(SOURCE)
    classes = evidence / 'harness'
    run('compile-oracle', [java / 'bin/javac', '--release', '9', '-cp', old, '-d', classes, source])
    mf = evidence / 'manifest.mf'; mf.write_text('Premain-Class: MemberDescriptorOracle\nCan-Redefine-Classes: true\n\n')
    agent = evidence / 'agent.jar'
    run('agent', [java / 'bin/jar', 'cfm', agent, mf, '-C', classes, 'MemberDescriptorOracle.class'])
    for name, flags in (('interpreter', ['-Xint']), ('jit', ['-Xbatch', '-XX:+PrintCompilation',
            '-XX:CompileCommand=compileonly,com.github.ytlog.artdcevm.fixture.MemberDescriptorFixture$Calls::call'])):
        output = run(name, [java / 'bin/java', *flags, '-XX:+AllowEnhancedClassRedefinition', '-javaagent:' + str(agent),
                           '-cp', str(classes) + ':' + str(old), 'MemberDescriptorOracle', patches])
        if output.count(' PASS\n') != 48: raise RuntimeError('Incomplete descriptor matrix')
        if name == 'jit' and not any('MemberDescriptorFixture$Calls::call' in line and 'CompileCommand:' not in line
                                    for line in output.splitlines()): raise RuntimeError('Missing compiled old caller evidence')
        checks = int(output.split('PASS checks=')[-1].strip())
        manifest['runs'].append({'mode': name, 'states': 48, 'checks': checks, 'passed': True})
    manifest['passed'] = True
    (evidence / 'summary.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Pinned JBR descriptors PASS: ' + ', '.join(str(row['checks']) + ' checks / ' + row['mode'] for row in manifest['runs']))
    print('Evidence: ' + str(evidence.relative_to(ROOT)))


if __name__ == '__main__': main()
