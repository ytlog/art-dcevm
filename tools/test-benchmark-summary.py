#!/usr/bin/env python3
"""Keep incomplete and invalid benchmark runs out of recomputed summaries."""
import contextlib
import copy
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('benchmark', Path(__file__).with_name('run-hook-overhead-benchmark.py'))
benchmark = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark)


class SummaryEvidenceTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.report = {'completed': True, 'mode': 'jit', 'state': 'engine', 'checkJni': 'false',
                       'schemaVersion': 6, 'cpuAffinity': -1,
                       'workloads': {'privateDirect': {'medianNs': 3, 'cpuAffinity': -1}},
                       'once': {}, 'setup': {'methodHandleChecks': True, 'arrayAssignabilityChecks': True}}
        self.meta = {'passed': True, 'rounds': 1, 'modes': ['jit'], 'states': ['engine'],
                     'cpuAffinity': None, 'requireStableJni': False, 'clockUnits': [],
                     'checkJni': 'false', 'foreground': False, 'runs': [
                         {'label': 'r0-jit-engine', 'completed': True, 'instrumentationOk': True}]}

    def save(self):
        (self.directory / 'run.json').write_text(json.dumps(self.meta))
        (self.directory / 'r0-jit-engine.json').write_text(json.dumps(self.report))

    def run_summary(self, *directories):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return benchmark.main(['--serial', 'test-device', '--summarize',
                                   *map(str, directories or [self.directory])])

    def assert_rejected(self):
        with self.assertRaises(SystemExit) as error:
            self.run_summary()
        self.assertEqual(error.exception.code, 2)

    def test_complete_run_can_be_recomputed(self):
        self.save()
        self.assertEqual(self.run_summary(), 0)

    def test_failed_process_cannot_disappear_from_recomputed_summary(self):
        self.meta['rounds'] = 2
        self.meta['passed'] = False
        self.meta['runs'].append({'label': 'r1-jit-engine', 'completed': False, 'instrumentationOk': False})
        self.save()
        failed = copy.deepcopy(self.report); failed['completed'] = False
        (self.directory / 'r1-jit-engine.json').write_text(json.dumps(failed))
        self.assert_rejected()

    def test_incomplete_matrix_cannot_claim_success(self):
        self.meta['rounds'] = 2
        self.save()
        self.assert_rejected()

    def test_missing_manifest_or_report_is_rejected(self):
        for filename in ('run.json', 'r0-jit-engine.json'):
            self.save(); (self.directory / filename).unlink()
            with self.subTest(filename=filename): self.assert_rejected()

    def test_manifest_cannot_override_failed_instrumentation(self):
        for key in ('completed', 'instrumentationOk'):
            self.meta['runs'][0][key] = False
            self.save()
            with self.subTest(key=key): self.assert_rejected()
            self.meta['runs'][0][key] = True

    def test_raw_report_is_revalidated(self):
        for field, value in (('completed', False), ('mode', 'interp'), ('state', 'none'),
                             ('checkJni', 'true'), ('cpuAffinity', 7)):
            original = copy.deepcopy(self.report)
            self.report[field] = value; self.save()
            with self.subTest(field=field): self.assert_rejected()
            self.report = original

    def test_result_checks_cannot_be_lost_during_recomputation(self):
        for key in ('methodHandleChecks', 'arrayAssignabilityChecks'):
            self.report['setup'][key] = False; self.save()
            with self.subTest(key=key): self.assert_rejected()
            self.report['setup'][key] = True

    def test_conditions_cannot_be_relaxed_for_recomputation(self):
        for key, value in (('cpuAffinity', 7), ('foreground', True),
                           ('requireStableJni', True), ('clockUnits', ['methodHandleBound'])):
            original = copy.deepcopy(self.meta)
            self.meta[key] = value; self.save()
            with self.subTest(key=key): self.assert_rejected()
            self.meta = original

    def test_duplicate_manifest_cell_is_rejected(self):
        self.meta['runs'].append(copy.deepcopy(self.meta['runs'][0]))
        self.save()
        self.assert_rejected()

    def test_unlisted_report_cannot_add_a_round(self):
        self.save()
        (self.directory / 'r1-jit-engine.json').write_text(json.dumps(self.report))
        self.assert_rejected()

    def test_same_directory_cannot_be_weighted_twice(self):
        self.save()
        with self.assertRaises(SystemExit) as error:
            self.run_summary(self.directory, self.directory)
        self.assertEqual(error.exception.code, 2)


if __name__ == '__main__':
    unittest.main()
