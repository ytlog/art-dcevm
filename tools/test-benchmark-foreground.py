#!/usr/bin/env python3
"""Exercise the host's explicit foreground launch and bounded failure paths."""
import importlib.util
import json
from pathlib import Path
import subprocess
import unittest
from unittest.mock import Mock, patch

spec = importlib.util.spec_from_file_location('benchmark', Path(__file__).with_name('run-hook-overhead-benchmark.py'))
benchmark = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark)


class ForegroundLaunchTest(unittest.TestCase):
    def setUp(self):
        self.launches = []
        self.commands = []
        self.reports = [json.dumps({'foregroundLaunchRequested': True})]
        self.launch = subprocess.CompletedProcess([], 0, 'Status: ok\n', '')

    def adb(self, *command, **kwargs):
        self.commands.append(command)
        if command[:3] == ('shell', 'am', 'start'):
            return self.launch
        return subprocess.CompletedProcess([], 0, self.reports.pop(0), '')

    def run_launch(self, finished=lambda: False):
        with patch.object(benchmark.time, 'sleep'):
            benchmark.launch_foreground(self.adb, finished, self.launches.append)

    def test_waits_for_monitor_then_launches_exactly_once(self):
        self.reports[:0] = ['', '{}']
        self.run_launch()
        self.assertEqual(len(self.launches), 1)
        self.assertEqual(self.commands[-1], ('shell', 'am', 'start', '-W', '-n', benchmark.ACTIVITY))
        self.assertEqual(sum(c[:3] == ('shell', 'am', 'start') for c in self.commands), 1)

    def test_failed_or_incomplete_launch_is_rejected_and_saved(self):
        for rc, output in ((0, 'Error: launch denied\n'), (1, 'Status: ok\n'), (0, 'Status: timeout\n')):
            with self.subTest(rc=rc, output=output):
                self.setUp()
                self.launch = subprocess.CompletedProcess([], rc, output, '')
                with self.assertRaisesRegex(ValueError, 'launch failed'):
                    self.run_launch()
                self.assertEqual(self.launches, [output])

    def test_finished_process_does_not_launch(self):
        with self.assertRaises(ValueError):
            self.run_launch(lambda: True)
        self.assertEqual(self.commands, [])

    def test_completed_report_is_never_a_launch_request(self):
        for completed in (True, False):
            self.setUp()
            self.reports = [json.dumps({'foregroundLaunchRequested': True, 'completed': completed})]
            with self.subTest(completed=completed), self.assertRaises(ValueError):
                self.run_launch()
            self.assertEqual(self.launches, [])

    def test_initial_failure_is_preserved_without_launch(self):
        self.reports = [json.dumps({'completed': False, 'fatalError': 'Device locked'})]
        with self.assertRaises(ValueError):
            self.run_launch()
        self.assertEqual(self.launches, [])

    def test_no_ready_marker_has_a_deadline(self):
        self.reports = ['[]']
        with patch.object(benchmark.time, 'monotonic', side_effect=[0, 0, 31]):
            with self.assertRaisesRegex(ValueError, 'deadline'):
                self.run_launch()
        self.assertEqual(self.launches, [])

    def test_command_timeout_is_not_retried(self):
        adb = Mock(side_effect=subprocess.TimeoutExpired('adb', 5))
        with self.assertRaises(subprocess.TimeoutExpired):
            benchmark.launch_foreground(adb, lambda: False, self.launches.append)
        self.assertEqual(adb.call_count, 1)

    def test_sanitized_real_foreground_observations_remain_verifiable(self):
        raw = {'foreground': True, 'foregroundReady': True, 'foregroundCpuset': '/top-app',
               'foregroundEpoch': 0, 'workloads': {'unit': {
                   'foregroundEpoch': 0, 'foregroundCpuset': '/top-app'}}}
        saved = benchmark.safe_benchmark_report(raw, 'test-device')
        benchmark.qualify_foreground(saved, True)
        self.assertEqual(saved, raw)

    def test_allowlist_does_not_preserve_arbitrary_paths_or_relax_proof(self):
        for name in ('/foreground', '/top-app/private', '/private/example', 'test-device'):
            raw = {'foreground': True, 'foregroundReady': True, 'foregroundCpuset': name,
                   'foregroundEpoch': 0, 'workloads': {'unit': {
                       'foregroundEpoch': 0, 'foregroundCpuset': name}}}
            saved = benchmark.safe_benchmark_report(raw, 'test-device')
            self.assertNotEqual(saved['foregroundCpuset'], name)
            with self.assertRaises(ValueError):
                benchmark.qualify_foreground(saved, True)
        saved = benchmark.safe_benchmark_report({'unrelatedPath': '/top-app'}, 'test-device')
        self.assertEqual(saved['unrelatedPath'], '<local-path>')


if __name__ == '__main__':
    unittest.main()
