#!/usr/bin/env python3
"""Compare shared Constructor copies and accessor history with the fixed JBR."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from evidence_paths import new_evidence_directory

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / 'app/src/debug/java/com/github/ytlog/artdcevm/fixture'
EXPECTATIONS = ROOT / 'app/src/debug/assets/constructor-history-expectations.json'
REVISION = 'cf1f197170a0f04c6559feb873e362583be021d1'
HARNESS = r'''
import java.lang.instrument.*;
import java.nio.file.*;
import java.util.*;
import com.github.ytlog.artdcevm.fixture.*;
public class ConstructorHistoryOracle {
  static Instrumentation inst;
  public static void premain(String a, Instrumentation i) { inst=i; }
  static void change(Path directory, Class<?>[] types) throws Exception {
    ClassDefinition[] defs=new ClassDefinition[types.length];
    for(int i=0;i<types.length;i++) defs[i]=new ClassDefinition(types[i],
      Files.readAllBytes(directory.resolve(types[i].getName().replace('.','/')+".class")));
    inst.redefineClasses(defs);
  }
  static void observations(String prefix, ConstructorHistoryProbe[] probes, String[] modes,
                           boolean changed, boolean abstractClass) throws Exception {
    for(int i=0;i<probes.length;i++) for(var e:probes[i].observations(changed,abstractClass).entrySet())
      System.out.println(prefix+"."+modes[i]+"."+e.getKey()+"="+e.getValue());
  }
  public static void main(String[] a) throws Exception {
    String[] modes={"Cold","Warm","Arity","Type"};
    Class<?>[] types=new Class<?>[modes.length];
    ConstructorHistoryProbe[] probes=new ConstructorHistoryProbe[modes.length];
    for(int i=0;i<modes.length;i++) {
      types[i]=Class.forName("com.github.ytlog.artdcevm.fixture.ConstructorHistoryFixture$"+modes[i]);
      probes[i]=new ConstructorHistoryProbe(types[i],modes[i]);
      System.out.println("warm."+modes[i]+"="+probes[i].warmResult.split(":")[0]);
      System.out.println("warmDeclared."+modes[i]+"="+probes[i].declaredWarmResult.split(":")[0]);
    }
    for(int round=0;round<2;round++) {
      System.gc();System.gc();
      change(Path.of(a[0]),types);
      observations(round+".apply",probes,modes,true,a[2].equals("abstract"));
      System.gc();
      observations(round+".retained",probes,modes,true,a[2].equals("abstract"));
      change(Path.of(a[1]),types);
      observations(round+".restore",probes,modes,false,a[2].equals("abstract"));
    }
  }
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--java-home', type=Path, required=True)
    args = parser.parse_args()
    java = args.java_home.resolve() / 'bin'
    out = new_evidence_directory(ROOT / 'build/evidence/jbr-constructor-history')
    work = out / 'work'
    work.mkdir()
    def run(name, command):
        result = subprocess.run(list(map(str, command)), cwd=work, capture_output=True, timeout=90)
        (out / (name + '.log')).write_bytes(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(name + ' failed; inspect ignored evidence')
        return result.stdout.decode()
    version = run('version', [java / 'java', '-version'])
    # java -version writes stderr; inspect the captured log.
    if 'JBR-21.0.11+10-1163.116' not in (out / 'version.log').read_text():
        raise RuntimeError('Requires fixed JBR 21.0.11+10-b1163.116')
    del version
    old = work / 'original'
    old.mkdir()
    sources = [FIXTURES / 'ConstructorHistoryFixture.java', FIXTURES / 'ConstructorHistoryProbe.java']
    run('compile-original', [java / 'javac', '--release', '11', '-d', old, *sources])
    (work / 'ConstructorHistoryOracle.java').write_text(HARNESS)
    run('compile-agent', [java / 'javac', '--release', '11', '-cp', old, '-d', work, work / 'ConstructorHistoryOracle.java'])
    (work / 'agent.mf').write_text('Premain-Class: ConstructorHistoryOracle\nCan-Redefine-Classes: true\n\n')
    run('agent', [java / 'jar', 'cfm', work / 'agent.jar', work / 'agent.mf', '-C', work, 'ConstructorHistoryOracle.class'])
    expected = json.loads(EXPECTATIONS.read_text())
    rows = []
    for kind in ('abstract', 'interface', 'annotation'):
        destination = work / kind
        destination.mkdir()
        source = ROOT / f'fixtures/constructor-history-{kind}/com/github/ytlog/artdcevm/fixture/ConstructorHistoryFixture.java'
        run('compile-' + kind, [java / 'javac', '--release', '11', '-d', destination, source])
        for mode in ('interpreter', 'default'):
            name = kind + '-' + mode
            output = run(name, [java / 'java', *(['-Xint'] if mode == 'interpreter' else []),
                '-XX:+AllowEnhancedClassRedefinition', '-javaagent:' + str(work / 'agent.jar'),
                '-cp', str(work) + ':' + str(old), 'ConstructorHistoryOracle', destination, old, kind])
            observations = dict(line.split('=', 1) for line in output.splitlines() if '=' in line)
            wanted = {'warm.' + m: 'SKIPPED' if m == 'Cold' else 'OK' if m == 'Warm' else 'java.lang.IllegalArgumentException' for m in expected}
            wanted.update({'warmDeclared.' + m: 'SKIPPED' if m == 'Warm' else 'OK' if m == 'Cold' else 'java.lang.IllegalArgumentException' for m in expected})
            for round_number in range(2):
                for phase in ('apply', 'retained', 'restore'):
                    key = ('back-' if phase == 'restore' else '') + ('abstract' if kind == 'abstract' else 'removed')
                    for constructor_mode, phases in expected.items():
                        wanted.update({f'{round_number}.{phase}.{constructor_mode}.{k}': v for k, v in phases[key].items()})
            if observations != wanted:
                mismatch = {k: {'expected': wanted.get(k), 'actual': observations.get(k)} for k in wanted.keys() | observations.keys() if wanted.get(k) != observations.get(k)}
                (out / (name + '-mismatch.json')).write_text(json.dumps(mismatch, indent=2) + '\n')
                raise RuntimeError(name + ' differs from pinned accessor-history semantics')
            rows.append({'kind': kind, 'mode': mode, 'observations': observations, 'passed': True})
    summary = {'sourceOracleRevision': REVISION, 'runs': rows,
        'observations': sum(len(row['observations']) for row in rows),
        'toolSha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'expectationsSha256': hashlib.sha256(EXPECTATIONS.read_bytes()).hexdigest(),
        'probeSha256': hashlib.sha256(sources[1].read_bytes()).hexdigest(),
        'harnessSha256': hashlib.sha256(HARNESS.encode()).hexdigest(),
        'fixtureSha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in [*sources, EXPECTATIONS, *sorted((ROOT / 'fixtures').glob('constructor-history-*/**/*.java'))]}}
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print('JBR constructor history PASS processes=' + str(len(rows)) + ' observations=' + str(summary['observations']))
    print('Evidence: ' + str(out.relative_to(ROOT)))


if __name__ == '__main__':
    main()
