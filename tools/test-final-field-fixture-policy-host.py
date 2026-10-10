#!/usr/bin/env python3
"""Compile real field fixtures, audit DEX reads and exercise actual C++ policy."""
import os
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
sdk = Path(os.environ.get('ANDROID_HOME', str(Path.home() / 'Library/Android/sdk')))
jdk = Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'], text=True).strip())
out = root / 'build/evidence/host/final-field-fixture-policy'
out.mkdir(parents=True, exist_ok=True)
source = Path('com/github/ytlog/artdcevm/fixture/FinalFieldSubject.java')
folded = out / 'folded-source' / source
folded.parent.mkdir(parents=True, exist_ok=True)
folded.write_text((root / 'fixtures/modifier-field-original' / source).read_text().replace('public int value', 'public final int value').replace('public static int staticValue', 'public static final int staticValue'))
inputs = [root / 'app/src/debug/java' / source, root / 'fixtures/modifier-field-final' / source, folded]
files = []
for index, java in enumerate(inputs):
    work = out / str(index); classes = work / 'classes'; dex = work / 'dex'
    classes.mkdir(parents=True, exist_ok=True); dex.mkdir(exist_ok=True)
    subprocess.run([str(jdk / 'bin/javac'), '--release', '8', '-encoding', 'UTF-8', '-g', '-d', str(classes), str(java)], check=True)
    subprocess.run([str(sdk / 'build-tools/35.0.0/d8'), '--min-api', '30', '--lib', str(sdk / 'platforms/android-35/android.jar'), '--output', str(dex), str(classes / source.with_suffix('.class'))], check=True)
    files.append(dex / 'classes.dex')
    dump = subprocess.check_output([str(sdk / 'build-tools/35.0.0/dexdump'), '-d', str(files[-1])], text=True)
    (work / 'dexdump.txt').write_text(dump)
    if index == 1:
        assert "name          : '<clinit>'" in dump
        assert 'iget ' in dump and 'sget ' in dump
cpp = root / 'artdcevm/src/main/cpp'
binary = out / 'test'
subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I', str(cpp), str(root / 'tests/host/final_field_fixture_policy_test.cc'), str(cpp / 'platform/common/native_method_evolution_policy.cc'), str(cpp / 'evolution/dex_schema.cc'), '-o', str(binary)], check=True)
result = subprocess.run([str(binary), *map(str, files)], capture_output=True, text=True, env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
(out / 'result.log').write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())

java_out = out / 'read-check'; java_out.mkdir(exist_ok=True)
subprocess.run([str(jdk / 'bin/javac'), '--release', '8', '-d', str(java_out), str(root / 'tests/host/FinalFieldFixtureReadsTest.java')], check=True)
result = subprocess.run([str(jdk / 'bin/java'), '-cp', str(java_out), 'FinalFieldFixtureReadsTest', str(out / '1/classes')], capture_output=True, text=True)
(out / 'real-java-reads.log').write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
