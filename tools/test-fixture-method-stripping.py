#!/usr/bin/env python3
"""Verify fixture recipes, missing direct targets and actual D8 lambda bridges."""
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
        # Enum kind is independent of its superclass. Recipes must preserve
        # every method/code byte while changing the ClassFile flag word.
        changed = builder.strip_class_methods(original, [], class_access={'clear': 0, 'set': 0x4000})
        assert changed != original
        assert builder.strip_class_methods(changed, [], class_access={'clear': 0x4000, 'set': 0}) == original
        for flags in ({'set': 0}, {'clear': -1, 'set': 0}, {'clear': 0, 'set': 65536}):
            try: builder.strip_class_methods(original, [], class_access=flags)
            except ValueError: pass
            else: raise AssertionError('Invalid class-kind recipe accepted')
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
        renamed_class = builder.strip_class_methods(original, [], [['directValue', '()I', 0x1002]],
                                                       {'directValue': 'directValuf'})
        renamed_removed = builder.strip_class_methods(renamed_class, [['directValuf', '()I']])
        assert renamed_removed != renamed_class
        for renames in ({'missing': 'missinh'}, {'directValue': 'short'}, {'directValue': 'nativeValue'}):
            try: builder.strip_class_methods(original, [], [], renames)
            except ValueError: pass
            else: raise AssertionError('Invalid classfile rename accepted')
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
            changed = builder.strip_dex_methods(data, [['nativeValue', '()I']],
                                                 [['directValue', '()I', 0x1002],
                                                  ['throughDirect', '()I', 2]])
            decoded = builder.FixtureDex(changed)
            assert decoded.declarations[('directValue', '()I')][1] == 0x1002
            assert decoded.declarations[('throughDirect', '()I')][1] == 2
            assert len(decoded.groups[1]) == 0, 'Access edit did not move method to direct group'
            assert changed[wrapper:end] == data[wrapper:end], 'Access recipe changed caller code'
            renamed = builder.FixtureDex(builder.rename_dex_strings(data, {'nativeValue': 'nativeValuf'}))
            assert ('nativeValuf', '()I') in renamed.declarations
            assert ('nativeValue', '()I') not in renamed.declarations
            for changes in ([['missing', '()I', 9]], [['directValue', '()I', -1]],
                            [['directValue', '()I', 2], ['directValue', '()I', 2]]):
                try: builder.strip_dex_methods(data, [], changes)
                except ValueError: pass
                else: raise AssertionError('Invalid access recipe accepted')
            for renames in ({'missing': 'missinh'}, {'nativeValue': 'short'}, {'nativeValue': 'zzzzzzzzzzz'}):
                try: builder.rename_dex_strings(data, renames)
                except ValueError: pass
                else: raise AssertionError('Invalid rename recipe accepted')
            constructor_removed = builder.FixtureDex(
                    builder.strip_dex_methods(data, [['<init>', '()V']]))
            assert ('<init>', '()V') not in constructor_removed.declarations
            assert ('throughDirect', '()I') in constructor_removed.declarations
            source = directory / 'LambdaProbe.java'
            source.write_text("""public interface LambdaProbe {
                default int seed() { return 5; }
                default int value() {
                    LambdaProbe self = this;
                    java.util.function.IntSupplier f = () -> self.seed();
                    return f.getAsInt();
                }
            }""")
            subprocess.run([javac, '--release', '9', '-d', str(directory), str(source)],
                           check=True, capture_output=True)
            klass = directory / 'LambdaProbe.class'
            klass.write_bytes(builder.strip_class_methods(klass.read_bytes(), [],
                    [['lambda$value$0', '(LLambdaProbe;)I', 10]], {'lambda$value$0': 'value$lambda$0'}))
            output = directory / 'lambda-dex'; output.mkdir()
            subprocess.run([str(args.sdk / 'build-tools/35.0.0/d8'), '--min-api', '30', '--file-per-class',
                            '--lib', str(args.sdk / 'platforms/android-35/android.jar'), '--output',
                            str(output), str(klass)], check=True, capture_output=True,
                           env=dict(os.environ, JAVA_HOME=str(args.java)) if args.java else None)
            decoded = builder.FixtureDex((output / 'LambdaProbe.dex').read_bytes())
            assert decoded.declarations[('value$lambda$0', '(LLambdaProbe;)I')][1] == 10
            bridges = [entry for (name, _), entry in decoded.declarations.items() if name.startswith('$r8$lambda$')]
            assert len(bridges) == 1 and bridges[0][1] == 0x1009
            assert len(list(output.glob('*ExternalSyntheticLambda*.dex'))) == 1
            print('Actual D8: missing direct targets, access groups, Kotlin-shaped helpers, generated bridges and invalid recipes PASS')
    print('Classfile stripping: actual missing direct target, preserved declarations and malformed inputs PASS')


if __name__ == '__main__':
    main()
