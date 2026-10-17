#!/usr/bin/env python3
"""Check actual wrapper argument forwarding and unchanged ZIP payloads offline."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location('collector_apk', Path(__file__).with_name('prepare-collector-apk.py'))
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


class CollectorApkTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def invoke_wrapper(self, collector, mode, arguments):
        wrapper = self.root / 'wrap.sh'
        wrapper.write_bytes(prepare.wrapper_script(collector, mode))
        process = self.root / 'fake process'
        process.write_text('#!' + sys.executable + '\nimport json,os,sys\n'
                           'with open(os.environ["ART_TEST_WRAPPER_RESULT"],"w") as f:json.dump(sys.argv[1:],f)\n')
        process.chmod(0o755)
        result = self.root / 'arguments.json'
        env = dict(os.environ, ART_TEST_WRAPPER_RESULT=str(result))
        subprocess.run(['/bin/sh', str(wrapper), str(process), *arguments], env=env,
                       capture_output=True, text=True, check=True)
        return json.loads(result.read_text())

    def test_wrapper_controls_gc_and_jni_ids_without_erasing_other_arguments(self):
        original = ['/system/bin', '--application', '--nice-name=test-process',
                    '-Xgc:CMC', '-Xopaque-jni-ids:pointer', 'com.android.internal.os.WrapperInit',
                    '', 'two words', 'quote"and\'single', '$(must-not-execute)',
                    '`must-not-execute`', '中文', 'line\nbreak', '--nice-name', '--']
        remaining = [arg for arg in original if not arg.startswith(
            ('--nice-name=', '-Xgc:', '-Xopaque-jni-ids:'))]
        for collector, mode in [('CC', 'indices'), ('CMC', 'swapable'), ('CC', 'pointer')]:
            with self.subTest(collector=collector, mode=mode):
                forwarded = self.invoke_wrapper(collector, mode, original)
                self.assertEqual(forwarded, ['-Xgc:' + collector, '-Xopaque-jni-ids:' + mode,
                    '-Xcompiler-option', '--debuggable', '-XjdwpProvider:adbconnection',
                    '-XjdwpOptions:suspend=n,server=y', *remaining])

    def test_empty_argument_list_and_empty_arguments_are_distinct(self):
        base = self.invoke_wrapper('CC', 'indices', [])
        self.assertEqual(self.invoke_wrapper('CC', 'indices', ['', '']), base + ['', ''])

    def test_default_uses_indices_and_rejects_unknown_vm_choices(self):
        self.assertIn(b'-Xopaque-jni-ids:indices ', prepare.wrapper_script('CC'))
        for collector, mode in [('garbage', 'indices'), ('CC', 'default'), ('CC', 'pointer;exit')]:
            with self.assertRaises(ValueError):prepare.wrapper_script(collector, mode)

    def test_manifest_requires_debugging_and_native_extraction(self):
        for value in ['true', '0xffffffff', '(type 0x12)0xffffffff']:
            manifest='android:debuggable(0x0101000f)='+value+'\nandroid:extractNativeLibs(0x010104ea)='+value
            prepare.validate_manifest(manifest)
        for manifest in ['', 'debuggable=true\nextractNativeLibs=false',
                         'debuggable=false\nextractNativeLibs=true']:
            with self.assertRaises(ValueError):prepare.validate_manifest(manifest)

    def source_apk(self, extra=None):
        source = self.root / 'source.apk'
        with zipfile.ZipFile(source, 'w') as apk:
            for name, value in {'AndroidManifest.xml': b'manifest', 'classes.dex': b'dex',
                    'lib/arm64-v8a/libartdcevm.so': b'native',
                    'META-INF/services/test.Service': b'service',
                    'META-INF/MANIFEST.MF': b'old-signature', 'META-INF/CERT.RSA': b'old-cert',
                    **(extra or {})}.items():apk.writestr(name, value)
        return source

    def test_repacking_preserves_all_payload_and_service_metadata(self):
        source, target = self.source_apk(), self.root / 'target.apk'
        wrapper = prepare.wrapper_script('CC')
        payload = prepare.write_unsigned(source, target, wrapper)
        prepare.verify_payload(target, payload, wrapper)
        with zipfile.ZipFile(target) as apk:
            self.assertEqual(apk.read('META-INF/services/test.Service'), b'service')
            self.assertNotIn('META-INF/CERT.RSA', apk.namelist())
            info=apk.getinfo(prepare.WRAP_PATH)
            self.assertEqual(info.compress_type, zipfile.ZIP_STORED)
            self.assertEqual(info.external_attr >> 16, 0o100755)
        with zipfile.ZipFile(target,'a') as apk:apk.writestr('extra.bin',b'changed')
        with self.assertRaisesRegex(ValueError,'inventory'):prepare.verify_payload(target,payload,wrapper)

    def test_existing_wrapper_is_rejected(self):
        source=self.source_apk({prepare.WRAP_PATH:b'existing'})
        with self.assertRaisesRegex(ValueError,'existing ARM64 wrapper'):
            prepare.write_unsigned(source,self.root/'target.apk',prepare.wrapper_script('CC'))

    def test_signer_parser_requires_one_certificate(self):
        line='Signer #1 certificate SHA-256 digest: '+'a'*64
        self.assertEqual(prepare.certificate_digest(line),'a'*64)
        for value in ['',line+'\n'+line.replace('#1','#2')]:
            with self.assertRaises(ValueError):prepare.certificate_digest(value)


if __name__ == '__main__':unittest.main()
