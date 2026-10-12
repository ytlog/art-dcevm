#!/usr/bin/env python3
"""Verify missing direct targets without changing JVM or D8 invocation instructions."""
import argparse
import hashlib
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('builder', ROOT / 'tools/build-fixtures.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--java', type=Path, help='JDK directory, or use javac/java on PATH')
    parser.add_argument('--sdk', type=Path, help='Android SDK for the actual D8 invocation regression')
    args = parser.parse_args()
    javac = str(args.java / 'bin/javac') if args.java else 'javac'
    java = str(args.java / 'bin/java') if args.java else 'java'
    evidence = ROOT / 'build/evidence/host/fixture-method-stripping'
    evidence.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=evidence) as temporary:
        directory = Path(temporary)
        (directory / 'Subject.java').write_text('''public class Subject {
    private int directValue() { return 7; }
    public int throughDirect() { return directValue(); }
    public native int nativeValue();
}''')
        (directory / 'Probe.java').write_text('''public class Probe {
    public static void main(String[] ignored) throws Exception {
        try { new Subject().throughDirect(); throw new AssertionError("direct target survived"); }
        catch (NoSuchMethodError expected) { }
        try { Subject.class.getDeclaredMethod("directValue"); throw new AssertionError("declaration survived"); }
        catch (NoSuchMethodException expected) { }
        Subject.class.getDeclaredMethod("throughDirect");
        Subject.class.getDeclaredMethod("nativeValue");
    }
}''')
        subprocess.run([javac, '--release', '8', '-d', str(directory),
                        str(directory / 'Subject.java'), str(directory / 'Probe.java')],
                       check=True, capture_output=True)
        klass = directory / 'Subject.class'
        original = klass.read_bytes()
        assert builder.strip_class_methods(original, []) == original
        removed = builder.strip_class_methods(original, [['directValue', '()I']])
        assert removed != original
        for data, removals in (
                (original[:7], [['directValue', '()I']]),
                (original + b'trailing', [['directValue', '()I']]),
                (original, [['missing', '()I']]),
                (original, [['directValue', '(I)I']]),
                (original, [['directValue', '()I'], ['directValue', '()I']])):
            try: builder.strip_class_methods(data, removals)
            except ValueError: pass
            else: raise AssertionError('Invalid method stripping input was accepted')
        klass.write_bytes(removed)
        subprocess.run([java, '-cp', str(directory), 'Probe'], check=True, capture_output=True)
        if args.sdk:
            # D8 needs the intact private declaration to choose invoke-direct.
            klass.write_bytes(original)
            dexdir = directory / 'dex'; dexdir.mkdir()
            subprocess.run([str(args.sdk / 'build-tools/35.0.0/d8'), '--min-api', '30', '--lib',
                            str(args.sdk / 'platforms/android-35/android.jar'), '--output',
                            str(dexdir), str(klass)], check=True, capture_output=True,
                           env=dict(os.environ, JAVA_HOME=str(args.java)) if args.java else None)
            data = (dexdir / 'classes.dex').read_bytes()
            before = builder.FixtureDex(data)
            removed_dex = builder.strip_dex_methods(data, [['directValue', '()I']])
            after = builder.FixtureDex(removed_dex)
            assert ('directValue', '()I') not in after.declarations
            assert set(after.declarations) == set(before.declarations) - {('directValue', '()I')}
            direct_index = before.declarations[('directValue', '()I')][0]
            wrapper = before.declarations[('throughDirect', '()I')][2]
            assert wrapper == after.declarations[('throughDirect', '()I')][2]
            assert data[wrapper + 16] in (0x70, 0x76), 'D8 did not emit invoke-direct'
            assert before.u16(wrapper + 18) == direct_index
            end = wrapper + 16 + 2 * before.u32(wrapper + 12)
            assert removed_dex[wrapper:end] == data[wrapper:end], 'Caller code changed during removal'
            assert removed_dex[12:32] == hashlib.sha1(removed_dex[32:]).digest()
            assert after.u32(8) == zlib.adler32(removed_dex[12:]) & 0xffffffff
            for bad_data, identities in (
                    (data[:40], [['directValue', '()I']]),
                    (data, [['missing', '()I']]),
                    (data, [['directValue', '(I)I']]),
                    (data, [['directValue', '()I'], ['directValue', '()I']])):
                try: builder.strip_dex_methods(bad_data, identities)
                except ValueError: pass
                else: raise AssertionError('Invalid DEX removal recipe was accepted')
            constructor_removed = builder.FixtureDex(
                    builder.strip_dex_methods(data, [['<init>', '()V']]))
            assert ('<init>', '()V') not in constructor_removed.declarations
            assert ('throughDirect', '()I') in constructor_removed.declarations
            print('Actual D8: removed definition, original invoke-direct target/code, constructor recipe and invalid inputs PASS')
    print('Classfile stripping: actual missing direct target, preserved declarations and malformed inputs PASS')


if __name__ == '__main__':
    main()
