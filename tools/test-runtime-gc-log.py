#!/usr/bin/env python3
"""Check transaction-thread identity, process filtering and GC log boundaries."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('gc_log', Path(__file__).with_name('analyze-runtime-gc-log.py'))
gc_log = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gc_log)


def line(pid, tid, tag, message):
    return f'10-17 01:01:01.001 {pid:5d} {tid:5d} I {tag}: {message}\n'


def snapshot(pid=100, tid=201):
    return line(pid, tid, 'ART-DCEVM-SNAPSHOT', 'targets=1 loaded=100 nanos=1000')


def gc(pid=100, tid=201, cause='Explicit', collector='concurrent copying'):
    return line(pid, tid, 'app_process64', f'{cause} {collector} GC freed 1MB, paused 1ms total 10ms')


class GcLogTest(unittest.TestCase):
    def test_instrumentation_thread_is_not_the_process_main_thread(self):
        report = gc_log.analyze(snapshot() + gc() + snapshot() + gc(tid=100))
        row = report['engineProcesses'][0]
        self.assertEqual(row['gcCompletionsOnSnapshotThreads'], 1)
        self.assertEqual(row['gcCompletionsOnOtherThreads'], 1)
        self.assertEqual(row['explicitGcPerSnapshotWindow'], {1: 2})
        self.assertFalse(report['markingOverlapEstablished'])

    def test_unrelated_cmc_process_does_not_reclassify_engine_gc(self):
        row = gc_log.analyze(snapshot() + gc() + gc(pid=200, collector='concurrent mark compact'))['engineProcesses'][0]
        self.assertEqual(row['collectors'], {'concurrent copying': 1})

    def test_same_thread_number_in_another_process_is_not_combined(self):
        rows = gc_log.analyze(snapshot() + snapshot(200, 301) + gc() + gc(200, 201))['engineProcesses']
        self.assertEqual([row['gcCompletionsOnSnapshotThreads'] for row in rows], [1, 0])
        self.assertEqual([row['recognizedGcCompletions'] for row in rows], [1, 1])

    def test_startup_gc_and_final_tail_are_counted_separately(self):
        row = gc_log.analyze(gc(cause='Background') + snapshot() + snapshot() + gc())['engineProcesses'][0]
        self.assertEqual(row['gcCompletionsBeforeFirstSnapshot'], 1)
        self.assertEqual(row['explicitGcPerSnapshotWindow'], {0: 1, 1: 1})
        self.assertEqual(row['gcCauses'], {'Background': 1, 'Explicit': 1})

    def test_cmc_and_young_cc_logs_do_not_establish_marking_overlap(self):
        report = gc_log.analyze(snapshot() + gc(collector='young concurrent copying') +
                                gc(cause='NativeAlloc', collector='young concurrent mark compact'))
        self.assertEqual(report['engineProcesses'][0]['recognizedGcCompletions'], 2)
        self.assertFalse(report['markingOverlapEstablished'])

    def test_absent_gc_and_truncated_lines_do_not_invent_observations(self):
        row = gc_log.analyze(snapshot() + 'truncated GC freed 1MB\n')['engineProcesses'][0]
        self.assertEqual(row['recognizedGcCompletions'], 0)
        self.assertEqual(row['explicitGcPerSnapshotWindow'], {0: 1})

    def test_without_a_snapshot_engine_identity_is_not_guessed(self):
        for text in ('', gc(), line(100, 201, 'another-tag', 'targets=1 loaded=100 nanos=1000')):
            with self.assertRaises(ValueError):
                gc_log.analyze(text)

    def test_unknown_cause_text_and_numeric_process_ids_are_not_published(self):
        report = gc_log.analyze(snapshot(12345, 23456) + gc(12345, 23456, cause='private-sentinel'))
        self.assertEqual(report['engineProcesses'][0]['gcCauses'], {'Other': 1})
        for value in ('private-sentinel', '12345', '23456'):
            self.assertNotIn(value, str(report))

    def test_explicit_gc_skips_are_not_counted_as_completed_collections(self):
        row = gc_log.analyze(snapshot() + line(100, 202, 'app_process64', 'Explicit GC skipped.') +
                             line(300, 302, 'app_process64', 'Explicit GC skipped.'))['engineProcesses'][0]
        self.assertEqual(row['explicitGcSkippedMessages'], 1)
        self.assertEqual(row['recognizedGcCompletions'], 0)


if __name__ == '__main__':
    unittest.main()
