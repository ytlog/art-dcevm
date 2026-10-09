#!/usr/bin/env python3
"""Host regressions for evidence integrity and multi-device orchestration; no ADB."""
import argparse
import concurrent.futures
import importlib.util
import json
import pathlib
import subprocess
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('matrix', pathlib.Path(__file__).with_name('run-production-evolution-matrix.py'))
matrix = importlib.util.module_from_spec(spec)
spec.loader.exec_module(matrix)


class MatrixTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name).resolve()
        self.artifacts = {'app': {'path': '/app.apk', 'sha256': 'a' * 64},
                          'test': {'path': '/test.apk', 'sha256': 'b' * 64}}
        self.run = '1' * 32
        self.directory = self.root / 'build/evidence/production-schema' / self.run
        self.directory.mkdir(parents=True)
        self.result = {'runId': self.run, 'serial': 'remote', 'case': 'field-types', 'scope': 'common-owned',
                       'runner': 'junit', 'appSha256': 'a' * 64, 'testSha256': 'b' * 64,
                       'reportFileCaptured': True, 'passed': True}
        self.required = {'schema': ['field-types'], 'permissions': ['final-fields']}

    def save(self):
        (self.directory / 'result.json').write_text(json.dumps(self.result))
        return str(self.directory) + '\n'

    def accept(self, stdout=None, seen=None, started=0):
        return matrix.accepted_evidence(self.save() if stdout is None else stdout, 'production-schema', started,
                                       set() if seen is None else seen, 'remote', self.artifacts,
                                       'field-types', 'common-owned', root=self.root)

    def test_required_set_not_vacuous_or_partial(self):
        self.assertFalse(matrix.complete(self.required, [], []))
        one = {'family': 'schema', 'case': 'field-types', 'passed': True}
        two = {'family': 'permissions', 'case': 'final-fields', 'passed': True}
        self.assertFalse(matrix.complete(self.required, [one], []))
        self.assertFalse(matrix.complete(self.required, [one, one, two], []))
        self.assertFalse(matrix.complete(self.required, [one, two], ['exception after bootstrap']))
        self.assertTrue(matrix.complete(self.required, [one, two], []))
        self.assertFalse(matrix.complete({}, [], []))

    def test_dynamic_cases_and_duplicate_rejection(self):
        available = dict(matrix.SCHEMA_CASES, **{'new-hierarchy': 'newMethod'})
        self.assertIn('new-hierarchy', matrix.selection(None, available))
        for selected in ([], ['field-types', 'field-types'], ['unknown']):
            with self.assertRaises(ValueError):
                matrix.selection(selected, available)

    def test_current_exact_hash_identity_only(self):
        self.assertEqual(self.accept()['runId'], self.run)
        for key, bad in [('serial', 'other'), ('case', 'native-java'), ('scope', 'full'),
                         ('runId', 'bad'), ('testSha256', 'c' * 64), ('passed', 1), ('reportFileCaptured', False)]:
            original = self.result[key]
            self.result[key] = bad
            with self.assertRaises(ValueError, msg=key):
                self.accept()
            self.result[key] = original
        with self.assertRaises(ValueError):
            self.accept(seen={self.run})
        with self.assertRaises(ValueError):
            self.accept(started=time.time_ns() + 10**9)

    def test_path_escape_and_symlink_rejected(self):
        self.save()
        with self.assertRaises(ValueError):
            self.accept(stdout=str(self.directory / '..' / self.run) + '\n')
        outside = self.root / 'outside'
        outside.mkdir()
        with self.assertRaises(ValueError):
            self.accept(stdout=str(outside) + '\n')
        link = self.directory.parent / ('2' * 32)
        link.symlink_to(self.directory, target_is_directory=True)
        with self.assertRaises(ValueError):
            self.accept(stdout=str(link) + '\n')
        file = self.directory / 'result.json'
        stored = outside / 'result.json'
        file.rename(stored)
        file.symlink_to(stored)
        with self.assertRaises(ValueError):
            self.accept(stdout=str(self.directory) + '\n')

    def test_freeze_before_device_operations(self):
        source = self.root / 'source.apk'
        source.write_bytes(b'app-v1')
        frozen = self.root / 'batch'
        manifest = matrix.freeze(source, None, frozen, [(31, 'remote')], ['field-types'], ['final-fields'])
        source.write_bytes(b'app-v2')
        self.assertEqual(pathlib.Path(manifest['artifacts']['app']['path']).read_bytes(), b'app-v1')
        self.assertEqual(matrix.digest(manifest['artifacts']['app']['path']), manifest['artifacts']['app']['sha256'])
        self.assertTrue((frozen / 'manifest.json').is_file())

    def test_actual_device_sdk_and_primary_arm64(self):
        def fake(abi='arm64-v8a', sdk='31'):
            return lambda command, **kw: subprocess.CompletedProcess(command, 0, sdk if command[-1].endswith('sdk') else abi, '')
        self.assertEqual(matrix.validate_device('adb', 'remote', 31, fake())['ro.build.version.sdk'], '31')
        self.assertEqual(matrix.validate_device('adb', 'remote', 31, fake(abi='arm64-v8a,armeabi-v7a,armeabi'))['ro.product.cpu.abilist'], 'arm64-v8a,armeabi-v7a,armeabi')
        for execute in [fake(sdk='32'), fake(abi='x86_64,arm64-v8a'), fake(abi='x86_64')]:
            with self.assertRaises(ValueError):
                matrix.validate_device('adb', 'remote', 31, execute)
        with self.assertRaises(argparse.ArgumentTypeError):
            matrix.device('32=remote')

    def test_nonzero_child_and_current_timeout_preserved(self):
        self.save()
        output = self.root / 'logs'
        output.mkdir()
        with patch.object(matrix, 'ROOT', self.root), patch.object(matrix.time, 'time_ns', return_value=0), patch.object(
                matrix.subprocess, 'run', return_value=subprocess.CompletedProcess([], 1, str(self.directory) + '\n', '')):
            # accepted_evidence defaults to the real ROOT, pass a wrapper for the test root.
            original = matrix.accepted_evidence
            with patch.object(matrix, 'accepted_evidence', side_effect=lambda *a, **k: original(*a, **k, root=self.root)):
                record = matrix.run_child([], 'production-schema', output, 'one', set(), 'remote', self.artifacts,
                                          'field-types', 'common-owned')
                self.assertFalse(record['passed'])
                self.result.update(passed=False, instrumentationTimedOut=True, timeoutDiagnostics={'thread': 'live'})
                self.save()
                record = matrix.run_child([], 'production-schema', output, 'two', set(), 'remote', self.artifacts,
                                          'field-types', 'common-owned')
                self.assertFalse(record['passed'])
                self.assertTrue(record['result']['instrumentationTimedOut'])
                self.assertEqual(record['result']['timeoutDiagnostics'], {'thread': 'live'})

    def test_devices_parallel_cases_sequential_and_usb_never_installs(self):
        output = self.root / 'parallel'
        output.mkdir()
        apk = self.root / 'app.apk'
        test = self.root / 'test.apk'
        apk.write_bytes(b'a'); test.write_bytes(b'b')
        artifacts = {'app': {'path': str(apk), 'sha256': matrix.digest(apk)},
                     'test': {'path': str(test), 'sha256': matrix.digest(test)}}
        args = argparse.Namespace(adb='unused', install=True, timeout=10, other_scope='common-owned',
                                  self_instrumentation_serial=[matrix.USB_SELF_ONLY])
        barrier = threading.Barrier(2)
        calls = {'remote': [], matrix.USB_SELF_ONLY: []}
        def child(command, family, directory, label, seen, serial, artifacts, *rest):
            calls[serial].append((family, command))
            if family == 'production-schema':
                barrier.wait(timeout=5)  # Cannot succeed if devices were serialized.
            return {'passed': True, 'runId': str(len(calls[serial])), 'result': {}}
        manifest = {'artifacts': artifacts, 'required': self.required}
        with patch.object(matrix, 'validate_device', return_value={}), patch.object(matrix, 'run_child', side_effect=child):
            with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
                futures = [pool.submit(matrix.run_device, api, serial, args, manifest, output)
                           for api, serial in [(31, 'remote'), (33, matrix.USB_SELF_ONLY)]]
                reports = [future.result(timeout=10) for future in futures]
        self.assertTrue(all(r['passed'] for r in reports))
        self.assertEqual([family for family, _ in calls['remote']], ['production-bootstrap', 'production-schema', 'production-permissions'])
        self.assertEqual([family for family, _ in calls[matrix.USB_SELF_ONLY]], ['production-schema', 'production-permissions'])
        self.assertTrue(all('--self-instrumentation' in command and '--test-apk' not in command
                            for _, command in calls[matrix.USB_SELF_ONLY]))

    def test_timeout_stops_device_queue_without_retry(self):
        output = self.root / 'timed'
        output.mkdir()
        args = argparse.Namespace(adb='unused', install=False, timeout=10, other_scope='common-owned', self_instrumentation_serial=[])
        with patch.object(matrix, 'validate_device', return_value={}), patch.object(matrix, 'run_child', return_value={
                'passed': False, 'error': 'failed', 'result': {'instrumentationTimedOut': True}}) as child:
            report = matrix.run_device(31, 'remote', args, {'required': self.required, 'artifacts': self.artifacts}, output)
        self.assertFalse(report['passed'])
        self.assertEqual(child.call_count, 1)
        self.assertEqual(len(report['completed']), 1)


if __name__ == '__main__':
    unittest.main()
