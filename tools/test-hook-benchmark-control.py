#!/usr/bin/env python3
"""Exercise benchmark control payload isolation and reject ambiguous APKs."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
import warnings
import zipfile

spec = importlib.util.spec_from_file_location('benchmark_control',
    Path(__file__).with_name('prepare-hook-benchmark-control.py'))
control = importlib.util.module_from_spec(spec)
spec.loader.exec_module(control)


class BenchmarkControlTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def apk(self, name, engine, **extra):
        target = self.root / name
        entries = {'AndroidManifest.xml': b'manifest', 'classes.dex': b'harness-dex',
            'classes2.dex': b'second-dex', 'assets/fixture.dex': b'fixture',
            'lib/arm64-v8a/wrap.sh': b'harness-jni-mode', control.ENGINE: engine,
            'META-INF/services/probe.Service': b'service', 'META-INF/build-info.txt': b'build',
            'META-INF/MANIFEST.MF': b'signature', 'META-INF/CERT.SF': b'signature',
            'META-INF/CERT.RSA': b'certificate', **extra}
        with zipfile.ZipFile(target, 'w') as archive:
            for path, data in entries.items():
                info = zipfile.ZipInfo(path)
                info.external_attr = 0o100755 << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                archive.writestr(info, data)
        return target

    def sources(self):
        return (self.apk('harness.apk', b'old-engine'),
                self.apk('donor.apk', b'new-engine', **{'classes.dex': b'wrong-dex',
                    'lib/arm64-v8a/wrap.sh': b'wrong-jni-mode', 'assets/donor-only': b'extra'}))

    def test_only_the_requested_engine_comes_from_the_donor(self):
        harness, donor = self.sources()
        target = self.root / 'control.apk'
        expected = control.replace_engine(harness, donor, target)
        control.verify_payload(target, expected)
        with zipfile.ZipFile(harness) as old, zipfile.ZipFile(target) as new:
            changed = [name for name in expected if old.read(name) != new.read(name)]
            self.assertEqual(changed, [control.ENGINE])
            self.assertEqual(new.read(control.ENGINE), b'new-engine')
            self.assertNotIn('assets/donor-only', new.namelist())
            self.assertNotIn('META-INF/CERT.RSA', new.namelist())
            self.assertNotIn('META-INF/CERT.SF', new.namelist())
            self.assertNotIn('META-INF/MANIFEST.MF', new.namelist())
            for name in expected:
                self.assertEqual(new.getinfo(name).compress_type, old.getinfo(name).compress_type)
                self.assertEqual(new.getinfo(name).external_attr, old.getinfo(name).external_attr)

    def test_reverse_control_preserves_the_other_harness(self):
        harness, donor = self.sources()
        target = self.root / 'reverse.apk'
        control.verify_payload(target, control.replace_engine(donor, harness, target))
        with zipfile.ZipFile(target) as archive:
            self.assertEqual(archive.read('classes.dex'), b'wrong-dex')
            self.assertEqual(archive.read('lib/arm64-v8a/wrap.sh'), b'wrong-jni-mode')
            self.assertEqual(archive.read(control.ENGINE), b'old-engine')

    def test_identical_engine_is_not_a_comparison(self):
        harness, _ = self.sources()
        with self.assertRaisesRegex(ValueError, 'identical'):
            control.replace_engine(harness, harness, self.root / 'same.apk')

    def test_duplicates_are_rejected_in_each_input_and_signed_output(self):
        for lane in ('harness', 'donor', 'output'):
            with self.subTest(lane=lane):
                harness, donor = self.sources()
                target = self.root / 'control.apk'
                expected = control.replace_engine(harness, donor, target)
                path = {'harness': harness, 'donor': donor, 'output': target}[lane]
                with warnings.catch_warnings():
                    warnings.simplefilter('ignore', UserWarning)
                    with zipfile.ZipFile(path, 'a') as archive:archive.writestr('classes.dex', b'ambiguous')
                with self.assertRaisesRegex(ValueError, 'duplicate'):
                    if lane == 'output':control.verify_payload(target, expected)
                    else:control.replace_engine(harness, donor, self.root / 'bad.apk')

    def test_output_inventory_and_every_payload_byte_are_verified(self):
        harness, donor = self.sources()
        target = self.root / 'control.apk'
        expected = control.replace_engine(harness, donor, target)
        with zipfile.ZipFile(target) as archive:
            entries = {name: archive.read(name) for name in archive.namelist()}
        for name in expected:
            with self.subTest(name=name):
                with zipfile.ZipFile(target, 'w') as archive:
                    for key, value in entries.items():archive.writestr(key, value + b'changed' if key == name else value)
                with self.assertRaisesRegex(ValueError, 'outside'):control.verify_payload(target, expected)
        for mutation in ('added', 'removed'):
            with self.subTest(mutation=mutation):
                with zipfile.ZipFile(target, 'w') as archive:
                    for key, value in entries.items():
                        if mutation != 'removed' or key != 'classes2.dex':archive.writestr(key, value)
                    if mutation == 'added':archive.writestr('extra', b'new')
                with self.assertRaisesRegex(ValueError, 'outside'):control.verify_payload(target, expected)

    def test_engine_and_application_dex_are_required(self):
        for missing in (control.ENGINE, 'application-dex'):
            target = self.root / 'missing.apk'
            with zipfile.ZipFile(target, 'w') as archive:
                if missing != control.ENGINE:archive.writestr(control.ENGINE, b'engine')
                if missing != 'application-dex':archive.writestr('classes.dex', b'dex')
            with zipfile.ZipFile(target) as archive:
                with self.assertRaisesRegex(ValueError, 'lacks'):control.payload(archive)

    def test_certificate_parser_rejects_missing_or_multiple_signers(self):
        line = 'Signer #1 certificate SHA-256 digest: ' + 'a' * 64
        self.assertEqual(control.certificate_digest(line), 'a' * 64)
        for text in ('', line + '\n' + line.replace('#1', '#2')):
            with self.assertRaises(ValueError):control.certificate_digest(text)


if __name__ == '__main__':
    unittest.main()
