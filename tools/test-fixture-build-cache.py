#!/usr/bin/env python3
"""Exercise cache invalidation, atomic failure and optional real dual-package DEX."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('builder', ROOT / 'tools/build-fixtures.py')
builder = importlib.util.module_from_spec(spec); spec.loader.exec_module(builder)


def tree(path):
    return {p.relative_to(path).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(path.rglob('*')) if p.is_file()}


def fake(temp):
    java = temp / 'java'; sdk = temp / 'sdk'; fixtures = temp / 'fixtures'; out = temp / 'out'; log = temp / 'calls'
    (java / 'bin').mkdir(parents=True); (java / 'lib').mkdir(); (java / 'release').write_text('fake compiler 1')
    (java / 'lib/modules').write_bytes(b'compiler modules 1')
    d8 = sdk / 'build-tools/35.0.0'; (d8 / 'lib').mkdir(parents=True)
    jar = sdk / 'platforms/android-35/android.jar'; jar.parent.mkdir(parents=True); jar.write_bytes(b'android lib 1')
    (d8 / 'lib/d8.jar').write_bytes(b'd8 implementation 1')
    script = '''#!/usr/bin/env python3
import hashlib,os,pathlib,sys
args=sys.argv[1:]; name=pathlib.Path(sys.argv[0]).name
with open(os.environ['FIXTURE_TEST_LOG'],'a') as f:f.write(name+'\\n')
failure=pathlib.Path(os.environ['FIXTURE_TEST_FAIL'])
if failure.exists() and failure.read_text() in ('',name):sys.exit(9)
if name=='javac':
 out=pathlib.Path(args[args.index('-d')+1]); payload=b''.join(pathlib.Path(p).read_bytes() for p in args if p.endswith('.java'))
 for p in args:
  if p.endswith('.java'):
   source=pathlib.Path(p); dest=out/source.parent.name/(source.stem+'.class');dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(payload)
else:
 out=pathlib.Path(args[args.index('--output')+1]);(out/'classes.dex').write_bytes(b'dex'+hashlib.sha256(pathlib.Path(args[-1]).read_bytes()).digest())
'''
    for executable in (java / 'bin/javac', d8 / 'd8'):
        executable.write_text(script); executable.chmod(0o755)
    (java / 'bin/java').write_text('fake VM 1')
    for name in ('alpha', 'beta'):
        path = fixtures / name / 'pkg/Subject.java'; path.parent.mkdir(parents=True); path.write_text('package pkg; class Subject {}')
    shared = temp / 'shared/Common.java'; shared.parent.mkdir(); shared.write_text('package shared; class Common {}')
    fail = temp / 'fail'; env = dict(os.environ, FIXTURE_TEST_LOG=str(log), FIXTURE_TEST_FAIL=str(fail), CLASSPATH=str(temp / 'shared'))
    # Classpath root must make the declared package independently reachable.
    env['CLASSPATH'] = str(temp)
    for name in builder.ENV_INPUTS[1:6]: env.pop(name, None)
    def run(ok=True):
        r = subprocess.run(['python3', str(ROOT / 'tools/build-fixtures.py'), '--java', str(java), '--sdk', str(sdk), '--fixtures', str(fixtures), '--out', str(out)], env=env, cwd=temp, capture_output=True, text=True)
        assert (r.returncode == 0) == ok, r.stdout + r.stderr
        return log.read_text().splitlines()
    assert len(run()) == 4; original = tree(out)
    assert len(run()) == 4 and tree(out) == original
    (fixtures / 'alpha/pkg/Subject.java').write_text('package pkg; class Subject { int field; }')
    assert len(run()) == 6 and tree(out)['beta/pkg/Subject.dex'] == original['beta/pkg/Subject.dex']
    missing = out / 'alpha/pkg/Subject.dex'; missing.unlink(); assert len(run()) == 8
    missing.write_bytes(b'corrupt'); assert len(run()) == 10
    (out / 'alpha/stale.dex').write_bytes(b'extra'); assert len(run()) == 12 and not (out / 'alpha/stale.dex').exists()
    shared.write_text('package shared; class Common { int changed; }'); assert len(run()) == 16
    (java / 'lib/modules').write_bytes(b'compiler modules 2'); assert len(run()) == 20
    (d8 / 'lib/d8.jar').write_bytes(b'd8 implementation 2'); assert len(run()) == 24
    jar.write_bytes(b'android lib 2'); assert len(run()) == 28
    (fixtures / 'alpha/pkg/Subject.java').write_text('package pkg; class Subject { int failure; }'); fail.touch()
    run(False); assert not (out / 'alpha').exists() and (out / 'beta').is_dir()
    assert not (temp / '.out-fixture-cache/alpha.json').exists(); fail.unlink(); run()
    # D8 failure after successful javac must revoke the old variant too.
    missing.write_bytes(b'corrupt again'); fail.write_text('d8'); calls = len(log.read_text().splitlines()); run(False)
    assert len(log.read_text().splitlines()) == calls + 2 and not (out / 'alpha').exists()
    assert not (temp / '.out-fixture-cache/alpha.json').exists(); fail.unlink(); run()
    # Adding a new variant must not rebuild unrelated variants from implicit cwd.
    added = fixtures / 'gamma/pkg/Subject.java'; added.parent.mkdir(parents=True); added.write_text('package pkg; class Subject {}')
    calls = len(log.read_text().splitlines()); assert len(run()) == calls + 2
    shutil.rmtree(fixtures / 'gamma'); run(); assert not (out / 'gamma').exists()
    # A recipe is a compiler input even when Java source bytes are unchanged.
    # Empty recipes exercise cache identity without requiring valid fake classfiles.
    recipe = fixtures / 'alpha/remove-methods.json'
    beta_before = tree(out / 'beta')
    calls = len(log.read_text().splitlines()); recipe.write_text('{}')
    assert len(run()) == calls + 2 and tree(out / 'beta') == beta_before
    calls = len(log.read_text().splitlines()); recipe.write_text('{\n}\n')
    assert len(run()) == calls + 2 and tree(out / 'beta') == beta_before
    calls = len(log.read_text().splitlines()); recipe.unlink()
    assert len(run()) == calls + 2 and tree(out / 'beta') == beta_before
    shutil.rmtree(fixtures / 'beta'); run(); assert not (out / 'beta').exists() and not (temp / '.out-fixture-cache/beta.json').exists()
    calls = len(log.read_text().splitlines()); env['JAVA_OPTS'] = '-Dfixture.option=1'; assert len(run()) == calls + 2; assert len(run()) == calls + 4; env.pop('JAVA_OPTS'); run()
    cold = temp / 'cold'
    subprocess.run(['python3', str(ROOT / 'tools/build-fixtures.py'), '--java', str(java), '--sdk', str(sdk), '--fixtures', str(fixtures), '--out', str(cold)], env=env, cwd=temp, check=True, capture_output=True)
    assert tree(out) == tree(cold)
    print('Fake compiler: per-variant/cache corruption/shared classpath/JDK/D8/library/recipe/deletion/failure/retry/cold equality PASS')


def real(temp, sdk, java):
    fixtures = temp / 'fixtures'; fixtures.mkdir()
    for variant in ('private-static-external', 'hier-parent'):
        shutil.copytree(ROOT / 'fixtures' / variant, fixtures / variant)
    env = dict(os.environ); env.pop('CLASSPATH', None)
    for name in builder.ENV_INPUTS[1:6]: env.pop(name, None)
    timings = []
    def run(out):
        begin = time.monotonic()
        result = subprocess.run(['python3', str(ROOT / 'tools/build-fixtures.py'), '--java', str(java), '--sdk', str(sdk), '--fixtures', str(fixtures), '--out', str(out)], env=env, cwd=temp, check=True, capture_output=True, text=True).stdout
        timings.append(time.monotonic() - begin); return result
    first = temp / 'first'; second = temp / 'second'; assert 'BUILT private-static-external' in run(first)
    before = tree(first); warm = run(first); assert 'BUILT' not in warm and tree(first) == before
    run(second); assert tree(second) == before
    assert any('/external/' in path for path in before) and any('/fixture/' in path for path in before)
    assert any(path.startswith('hier-invalid/') for path in before)
    # Independent uncached reference preserves the original compiler arguments
    # and one-D8-invocation-per-class recipe, not the cache implementation.
    reference = temp / 'reference'
    for variant in sorted(fixtures.iterdir()):
        with tempfile.TemporaryDirectory(dir=temp) as directory:
            work = Path(directory); classes = work / 'classes'; classes.mkdir()
            subprocess.run([str(java / 'bin/javac'), '--release', '8', '-encoding', 'UTF-8', '-g', '-d', str(classes), *map(str, sorted(variant.rglob('*.java')))], env=dict(env, JAVA_HOME=str(java)), check=True, capture_output=True)
            for index, klass in enumerate(sorted(classes.rglob('*.class'))):
                dex = work / str(index); dex.mkdir()
                subprocess.run([str(sdk / 'build-tools/35.0.0/d8'), '--min-api', '30', '--lib', str(sdk / 'platforms/android-35/android.jar'), '--output', str(dex), str(klass)], env=dict(env, JAVA_HOME=str(java)), check=True, capture_output=True)
                target = reference / variant.name / klass.relative_to(classes).with_suffix('.dex'); target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes((dex / 'classes.dex').read_bytes())
    relative = 'com/github/ytlog/artdcevm/fixture/HierarchySubject.dex'
    invalid = reference / 'hier-invalid' / relative; invalid.parent.mkdir(parents=True); invalid.write_bytes(builder.invalid_hierarchy(reference / 'hier-parent' / relative))
    assert tree(reference) == before
    print('Actual javac/D8 dual namespaces + derived invalid DEX: cold/warm/independent-cold/original-recipe byte equality PASS; seconds=' + str(timings))
    (ROOT / 'build/evidence/host/fixture-cache/real-result.json').write_text(json.dumps({'seconds': timings, 'outputs': before}, indent=2))


if __name__ == '__main__':
    p = argparse.ArgumentParser(); p.add_argument('--sdk', type=Path); p.add_argument('--java', type=Path); a = p.parse_args()
    evidence = ROOT / 'build/evidence/host/fixture-cache'; evidence.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='fake-', dir=evidence) as temp: fake(Path(temp))
    if a.sdk and a.java:
        with tempfile.TemporaryDirectory(prefix='real-', dir=evidence) as temp: real(Path(temp), a.sdk, a.java)
