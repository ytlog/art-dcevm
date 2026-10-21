#!/usr/bin/env python3
"""Compare self-array kind conversions with an unchanged Java probe on fixed JBR."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from evidence_paths import new_evidence_directory

ROOT = Path(__file__).resolve().parents[1]
PROBE = ROOT / 'app/src/debug/java/com/github/ytlog/artdcevm/testing/SelfArrayProbe.java'
EXPECTATIONS = ROOT / 'app/src/debug/assets/self-array-expectations.json'
TARGET = 'com.github.ytlog.artdcevm.fixture.SelfArrayTarget'
REVISION = 'cf1f197170a0f04c6559feb873e362583be021d1'
HARNESS = r'''
import java.lang.instrument.*;
import java.net.*;
import java.nio.file.*;
import java.util.*;
import com.github.ytlog.artdcevm.testing.SelfArrayProbe;
public class SelfArrayOracle {
  static Instrumentation inst;
  public static void premain(String args, Instrumentation value) { inst=value; }
  static void observe(String prefix, SelfArrayProbe probe, SelfArrayProbe other) throws Exception {
    for (Map.Entry<String,Object> entry : probe.observe(other).entrySet())
      System.out.println(prefix+entry.getKey()+"="+entry.getValue());
  }
  static void change(String phase, Class<?> type, byte[] bytes) throws Exception {
    inst.redefineClasses(new ClassDefinition(type,bytes));
    System.out.println(phase+".status=OK");
  }
  public static void main(String[] args) throws Exception {
    Path original=Path.of(args[0]), changed=Path.of(args[1]);
    String name="com.github.ytlog.artdcevm.fixture.SelfArrayTarget";
    String file=name.replace('.','/')+".class";
    URL[] urls={original.toUri().toURL()};
    Class<?> type=Class.forName(name,true,new URLClassLoader(urls,null));
    Class<?> twin=Class.forName(name,true,new URLClassLoader(urls,null));
    SelfArrayProbe other=new SelfArrayProbe(twin);
    SelfArrayProbe probe=args[2].equals("warm") ? new SelfArrayProbe(type) : null;
    byte[] patch=Files.readAllBytes(changed.resolve(file));
    byte[] before=Files.readAllBytes(original.resolve(file));
    for(int round=0;round<2;++round) {
      String phase=round+".apply";
      change(phase,type,patch);
      if(probe==null) probe=new SelfArrayProbe(type);
      observe(phase+".",probe,other); observe(phase+".twin.",other,probe);
      System.gc(); observe(phase+".gc.",probe,other);
      phase=round+".restore";
      change(phase,type,before); observe(phase+".",probe,other);
      System.gc(); observe(phase+".gc.",probe,other);
    }
    try {
      inst.redefineClasses(new ClassDefinition(type,patch),
        new ClassDefinition(twin,Files.readAllBytes(Path.of(args[3]))));
      System.out.println("format-abort.status=UNEXPECTED_SUCCESS");
    } catch(Throwable error) { System.out.println("format-abort.status="+error.getClass().getName()); }
    observe("format-abort.",probe,other); observe("format-abort.twin.",other,probe);
    change("recovery",type,patch); observe("recovery.",probe,other);
    change("recovery-restore",type,before); observe("recovery-restore.",probe,other);
  }
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--java-home', required=True, type=Path)
    args = parser.parse_args()
    java = args.java_home.resolve() / 'bin'
    out = new_evidence_directory(ROOT / 'build/evidence/jbr-self-arrays')
    work = out / 'work'
    work.mkdir()

    def run(name, command):
        result = subprocess.run(list(map(str, command)), capture_output=True, timeout=90, cwd=work)
        (out / (name + '.log')).write_bytes(result.stdout + result.stderr)
        if result.returncode: raise RuntimeError(name + ' failed; inspect ignored evidence')
        return result.stdout.decode()

    version = subprocess.run([str(java / 'java'), '-version'], capture_output=True, text=True, check=True)
    if 'JBR-21.0.11+10-1163.116' not in version.stderr:
        raise RuntimeError('Requires fixed JBR 21.0.11+10-b1163.116')
    manifest = {'sourceOracleRevision': REVISION, 'runtimeVersion': version.stderr.strip(),
                'probeSha256': hashlib.sha256(PROBE.read_bytes()).hexdigest(),
                'expectationsSha256': hashlib.sha256(EXPECTATIONS.read_bytes()).hexdigest(),
                'toolSha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), 'cells': []}
    for kind in ('class', 'interface'):
        directory = work / kind
        directory.mkdir()
        source = ROOT / ('fixtures/self-array-' + kind) / (TARGET.replace('.', '/') + '.java')
        run('compile-' + kind, [java / 'javac', '--release', '9', '-d', directory, source])
        data = (directory / (TARGET.replace('.', '/') + '.class')).read_bytes()
        (work / (kind + '-invalid.class')).write_bytes(bytes([0, 1]))
        manifest[kind + 'Sha256'] = hashlib.sha256(data).hexdigest()
    (work / 'SelfArrayOracle.java').write_text(HARNESS)
    run('compile-harness', [java / 'javac', '--release', '11', '-d', work, PROBE, work / 'SelfArrayOracle.java'])
    (work / 'manifest.mf').write_text('Premain-Class: SelfArrayOracle\nCan-Redefine-Classes: true\n\n')
    agent = work / 'agent.jar'
    run('agent', [java / 'jar', 'cfm', agent, work / 'manifest.mf', '-C', work, 'SelfArrayOracle.class'])
    common = json.loads(EXPECTATIONS.read_text())
    for kind in ('class', 'interface'):
        changed = 'interface' if kind == 'class' else 'class'
        iface = kind == 'interface'
        for warm in ('cold', 'warm'):
            name = kind + '-' + warm
            raw = run(name, [java / 'java', '-XX:+AllowEnhancedClassRedefinition', '-javaagent:' + str(agent),
                            '-cp', work, 'SelfArrayOracle', work / kind, work / changed, warm,
                            work / (changed + '-invalid.class')])
            actual = dict(line.split('=', 1) for line in raw.splitlines() if '=' in line)
            expected = {}

            def expect(phase, is_interface, twin=False, gc=False, status='OK'):
                expected[phase + '.status'] = status
                for prefix, current in [('', is_interface)] + ([('twin.', iface)] if twin else []) + ([('gc.', is_interface)] if gc else []):
                    expected.update({phase + '.' + prefix + key: str(value).lower() if isinstance(value, bool) else str(value)
                                     for key, value in dict(common, interface=current).items()})

            for round_number in range(2):
                expect(str(round_number) + '.apply', not iface, twin=True, gc=True)
                expect(str(round_number) + '.restore', iface, gc=True)
            expect('format-abort', iface, twin=True, status='java.lang.InternalError')
            expect('recovery', not iface)
            expect('recovery-restore', iface)
            # Fixed JBR also changes array constant-pool resolution in the
            # untouched loader of this same binary name. Record that boundary
            # explicitly; Android must retain loader isolation, so these cells
            # are not counted as matching the Android contract.
            boundary = {}
            for phase in ('0.apply', '1.apply', 'format-abort'):
                for key, value in {'arrayLiteral': 'false', 'matrix': 'java.lang.ArrayStoreException',
                                   'cube': 'java.lang.ArrayStoreException', 'cast': 'java.lang.ClassCastException',
                                   'otherCast': 'returned'}.items():
                    full = phase + '.twin.' + key
                    boundary[full] = {'androidContract': expected[full], 'fixedJbr': value}
                    expected[full] = value
            # A failed same-name/different-loader batch also contaminates
            # the next successful conversion on this fixed JBR. Keep the
            # observed bug separate from the Android rollback contract.
            for key, value in {'arrayLiteral': 'false', 'freshField': 'false', 'cachedPack': 'false',
                               'freshPack': 'java.lang.NoSuchMethodException', 'handlePack': 'false',
                               'matrix': 'java.lang.ArrayStoreException', 'cube': 'java.lang.ArrayStoreException',
                               'cast': 'java.lang.ClassCastException', 'widen': 'java.lang.NoSuchMethodException',
                               'otherCast': 'returned'}.items():
                full = 'recovery.' + key
                boundary[full] = {'androidContract': expected[full], 'fixedJbr': value}
                expected[full] = value
            mismatches = {key: {'expected': value, 'actual': actual.get(key)}
                          for key, value in expected.items() if actual.get(key) != value}
            manifest['cells'].append({'name': name, 'observations': actual, 'expected': expected,
                                      'mismatches': mismatches, 'knownOracleDifferences': boundary, 'passed': not mismatches})
    manifest['matchingProbeObservations'] = sum(len(cell['expected']) - len(cell['knownOracleDifferences']) -
        sum(key.endswith('.status') for key in cell['expected']) for cell in manifest['cells'])
    manifest['knownOracleDifferenceObservations'] = sum(len(cell['knownOracleDifferences']) for cell in manifest['cells'])
    manifest['passed'] = all(cell['passed'] for cell in manifest['cells'])
    (out / 'report.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps({'passed': manifest['passed'], 'cells': len(manifest['cells']),
                      'observations': sum(len(cell['expected']) for cell in manifest['cells']),
                      'matchingProbeObservations': manifest['matchingProbeObservations'],
                      'knownOracleDifferenceObservations': manifest['knownOracleDifferenceObservations']}))
    return 0 if manifest['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
