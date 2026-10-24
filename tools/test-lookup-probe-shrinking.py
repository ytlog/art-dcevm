#!/usr/bin/env python3
"""Run the actual lookup probe after R8 shrinking with the library consumer rules."""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import subprocess

from evidence_paths import new_evidence_directory

ROOT = Path(__file__).resolve().parents[1]
MODES = ('virtual', 'static', 'inherited-static', 'nonpublic-static', 'bridge', 'enumeration', 'static-enumeration', 'bootstrap-static-enumeration', 'bootstrap-override-enumeration', 'bootstrap-instance-enumeration', 'bootstrap-root-static-enumeration')
RUNNER = '''public class Runner {
  public static void main(String[] args) throws Throwable {
    if (args[0].equals("virtual"))
      com.github.ytlog.artdcevm.internal.MethodHandleLookupProbe.run();
    else if (args[0].equals("static"))
      com.github.ytlog.artdcevm.internal.MethodHandleLookupProbe.runStatic();
    else if (args[0].equals("inherited-static"))
      com.github.ytlog.artdcevm.internal.MethodHandleLookupProbe.runInheritedStatic();
    else if (args[0].equals("nonpublic-static"))
      com.github.ytlog.artdcevm.internal.MethodHandleLookupProbe.runNonpublicStatic();
    else if (args[0].equals("bridge"))
      com.github.ytlog.artdcevm.internal.MethodHandleLookupProbe.runBridge();
    else if (args[0].equals("enumeration"))
      com.github.ytlog.artdcevm.internal.MethodEnumerationProbe.run();
    else if (args[0].equals("static-enumeration"))
      com.github.ytlog.artdcevm.internal.MethodEnumerationProbe.runStatic();
    else if (args[0].equals("bootstrap-static-enumeration"))
      com.github.ytlog.artdcevm.internal.MethodEnumerationProbe.runBootstrapStatic();
    else if (args[0].equals("bootstrap-override-enumeration"))
      com.github.ytlog.artdcevm.internal.MethodEnumerationProbe.runBootstrapOverride();
    else if (args[0].equals("bootstrap-instance-enumeration"))
      com.github.ytlog.artdcevm.internal.MethodEnumerationProbe.runBootstrapInstance();
    else
      com.github.ytlog.artdcevm.internal.MethodEnumerationProbe.runBootstrapRootStatic();
    System.out.println("Lookup probe " + args[0] + " PASS");
  }
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--java-home', type=Path)
    args = parser.parse_args()
    helper = runpy.run_path(str(ROOT / 'tools/test-structural-policy-host.py'))
    java = helper['java_home'](args.java_home)
    properties = dict(line.split('=', 1) for line in (ROOT / 'local.properties').read_text().splitlines()
                      if '=' in line and not line.lstrip().startswith('#'))
    r8 = Path(properties['sdk.dir']) / 'build-tools/35.0.0/lib/d8.jar'
    source = ROOT / 'artdcevm/src/main/java/com/github/ytlog/artdcevm/internal/MethodHandleLookupProbe.java'
    enumeration = source.with_name('MethodEnumerationProbe.java')
    rules = ROOT / 'artdcevm/consumer-rules.pro'
    out = new_evidence_directory(ROOT / 'build/evidence/host/lookup-probe-shrinking')
    original, shrunk = out / 'original', out / 'shrunk'
    original.mkdir()
    shrunk.mkdir()
    (out / 'Runner.java').write_text(RUNNER)
    (out / 'keep.pro').write_text(rules.read_text() + '\n-keep class Runner { public static void main(java.lang.String[]); }\n')

    def run(label, command):
        result = subprocess.run(list(map(str, command)), capture_output=True, timeout=120)
        (out / (label + '.log')).write_bytes(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(label + ' failed; inspect ignored evidence')
        return result.stdout.decode()

    run('javac', [java / 'bin/javac', '--release', '11', '-d', original, out / 'Runner.java', source, enumeration])
    run('jar', [java / 'bin/jar', 'cf', out / 'input.jar', '-C', original, '.'])
    run('r8', [java / 'bin/java', '-cp', r8, 'com.android.tools.r8.R8', '--classfile', '--release',
               '--lib', java, '--pg-conf', out / 'keep.pro', '--output', shrunk, out / 'input.jar'])
    for mode in MODES:
        output = run(mode, [java / 'bin/java', '-cp', shrunk, 'Runner', mode])
        if 'Lookup probe ' + mode + ' PASS' not in output:
            raise RuntimeError('Missing actual shrunk probe call result')
    manifest = {'passed': True, 'modes': list(MODES),
                'scope': 'Shrunk Java probe behavior; not ART live-frame or device proof',
                'inputs': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                           for p in (source, enumeration, rules, Path(__file__).resolve())},
                'r8Sha256': hashlib.sha256(r8.read_bytes()).hexdigest()}
    (out / 'summary.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('R8 lookup probe PASS: ' + ', '.join(MODES) + ' real calls')
    print('Evidence: ' + str(out.relative_to(ROOT)))


if __name__ == '__main__':
    main()
