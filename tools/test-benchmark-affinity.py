#!/usr/bin/env python3
"""Reject missing or inconsistent CPU proof in benchmark reports."""
import copy
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('benchmark', Path(__file__).with_name('run-hook-overhead-benchmark.py'))
benchmark = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark)


class AffinityProofTest(unittest.TestCase):
    def setUp(self):
        self.report = {'cpuAffinity': 7, 'workloads': {
            'reflectNewInstance': {'cpuAffinity': 7},
            'jniIsAssignableFromArray': {'cpuAffinity': 7}}}

    def test_complete_proof(self):
        self.assertTrue(benchmark.affinity_matches(self.report, 7))

    def test_legacy_unbound(self):
        self.assertTrue(benchmark.affinity_matches({}, None))
        self.assertFalse(benchmark.affinity_matches({}, 7))

    def test_bound_report_is_not_unbound(self):
        self.assertFalse(benchmark.affinity_matches(self.report, None))
        changed = copy.deepcopy(self.report)
        changed['cpuAffinity'] = -1
        for metric in changed['workloads'].values():
            metric['cpuAffinity'] = -1
        self.assertTrue(benchmark.affinity_matches(changed, None))

    def test_missing_or_wrong_metric(self):
        for metric in ({}, {'cpuAffinity': 6}, {'cpuAffinity': None}, None, {'cpuAffinity': True}):
            with self.subTest(metric=metric):
                changed = copy.deepcopy(self.report)
                changed['workloads']['reflectNewInstance'] = metric
                self.assertFalse(benchmark.affinity_matches(changed, 7))

    def test_empty_or_malformed_workloads(self):
        for workloads in ({}, [], None):
            self.assertFalse(benchmark.affinity_matches({'cpuAffinity': 7, 'workloads': workloads}, 7))

    def test_wrong_top_level(self):
        for cpu in (None, '7', 6, True):
            changed = copy.deepcopy(self.report)
            changed['cpuAffinity'] = cpu
            self.assertFalse(benchmark.affinity_matches(changed, 7))
        boolean_cpu = {'cpuAffinity': True, 'workloads': {'sample': {'cpuAffinity': True}}}
        self.assertFalse(benchmark.affinity_matches(boolean_cpu, 1))


if __name__ == '__main__':
    unittest.main()
