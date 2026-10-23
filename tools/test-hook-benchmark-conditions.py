#!/usr/bin/env python3
"""Negative evidence tests for the independent JNI clock control."""
import copy
import unittest

from hook_benchmark_conditions import qualify, qualify_units, compare, qualify_foreground


def valid():
    return {"cpuAffinity": 7, "workloads": {"jniCallIntMethod": {
        "callsPerBatch": 64, "checksum": 64 * 63 // 2 * 21}}, "steadyConditions": {
        "cpuSamplesNs": [80.] * 21, "referenceBeforeNs": [270000] * 21,
        "referenceAfterNs": [270000] * 21,
        "frequencyBefore": "2803200 100\n2918400 200\n",
        "frequencyAfter": "2803200 100\n2918400 400\n",
        "gcBefore": "12", "gcAfter": "12"}}


class ConditionsTest(unittest.TestCase):
    def test_foreground_requires_observations_and_matches_requested_conditions(self):
        qualify_foreground({}, False)
        report = {"foreground": True, "foregroundReady": True, "foregroundCpuset": "/top-app",
                  "foregroundEpoch": 0,
                  "workloads": {"unit": {"foregroundEpoch": 0, "foregroundCpuset": "/top-app"}}}
        qualify_foreground(report, True)
        with self.assertRaises(ValueError): qualify_foreground(report, False)
        with self.assertRaises(ValueError): qualify_foreground({}, True)
        for field, value in (("foregroundReady", False), ("foregroundCpuset", "/foreground"),
                             ("workloads", {})):
            changed = copy.deepcopy(report); changed[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError): qualify_foreground(changed, True)
        for field, value in (("foregroundEpoch", True), ("foregroundEpoch", -1),
                             ("foregroundCpuset", "/foreground")):
            changed = copy.deepcopy(report); changed["workloads"]["unit"][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError): qualify_foreground(changed, True)

    def test_foreground_cannot_resume_with_a_new_epoch_between_units(self):
        report = {"foreground": True, "foregroundReady": True, "foregroundCpuset": "/top-app",
                  "foregroundEpoch": 2, "workloads": {
                      "first": {"foregroundEpoch": 2, "foregroundCpuset": "/top-app"},
                      "second": {"foregroundEpoch": 3, "foregroundCpuset": "/top-app"}}}
        with self.assertRaises(ValueError): qualify_foreground(report, True)
        report["workloads"]["second"]["foregroundEpoch"] = 2
        qualify_foreground(report, True)
        for epoch in (None, True, -1):
            changed = copy.deepcopy(report); changed["foregroundEpoch"] = epoch
            with self.subTest(epoch=epoch), self.assertRaises(ValueError): qualify_foreground(changed, True)

    def test_stable_cell_and_pair(self):
        result = qualify(valid())
        self.assertEqual(result["cpuMedianNs"], 80)
        self.assertEqual(result["frequencyKHz"], 2918400)
        self.assertEqual(result["dominantFraction"], 1)
        self.assertEqual(compare(result, result), 0)

    def test_missing_unbound_and_invalid_samples(self):
        for cpu in (-1, None, True):
            report = valid(); report["cpuAffinity"] = cpu
            with self.subTest(cpu=cpu), self.assertRaises(ValueError): qualify(report)
        for field in ("cpuSamplesNs", "referenceBeforeNs", "referenceAfterNs"):
            for value in ([], [1] * 20, [0] * 21, [True] * 21, [float("nan")] * 21,
                          [float("inf")] * 21, ["80"] * 21):
                report = valid(); report["steadyConditions"][field] = value
                with self.subTest(field=field, value=value[0] if value else None), self.assertRaises(ValueError):
                    qualify(report)
        with self.assertRaises(ValueError): qualify({"cpuAffinity": 7})

    def test_outlier_is_never_dropped(self):
        for field in ("cpuSamplesNs", "referenceBeforeNs", "referenceAfterNs"):
            report = valid(); report["steadyConditions"][field][0] *= 1.03
            with self.subTest(field=field), self.assertRaises(ValueError): qualify(report)


    def test_real_jni_results_are_independent_of_clock_diagnostics(self):
        for count, checksum in ((0, 0), (True, 0), (64, 0), (64, True), (64, None)):
            report = valid()
            report["workloads"]["jniCallIntMethod"].update(callsPerBatch=count, checksum=checksum)
            with self.subTest(count=count, checksum=checksum), self.assertRaises(ValueError): qualify(report)

    def test_central_clock_drift_is_rejected(self):
        report = valid()
        report["steadyConditions"]["referenceAfterNs"] = [273000] * 21
        with self.assertRaises(ValueError): qualify(report)

    def test_frequency_coverage_reset_and_missing_service(self):
        for value in ("", "bad", "2918400 400\n2918400 400", "2918400 -1",
                      "2803200 100\n2918400 200", "2803200 100\n2918400 199",
                      "2803200 110\n2918400 400"):
            report = valid(); report["steadyConditions"]["frequencyAfter"] = value
            with self.subTest(value=value), self.assertRaises(ValueError): qualify(report)

    def test_gc_counters_cannot_be_missing_or_diagnostics(self):
        for value in (None, True, 12.5, -1, "unavailable", "13"):
            report = valid(); report["steadyConditions"]["gcAfter"] = value
            with self.subTest(value=value), self.assertRaises(ValueError): qualify(report)

    def test_pair_cannot_mix_clocks_or_adjust_raw_timings(self):
        control = qualify(valid())
        for key, value in (("frequencyKHz", 2803200), ("referenceMedianNs", 273001)):
            candidate = copy.deepcopy(control); candidate[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): compare(control, candidate)
        candidate = copy.deepcopy(control); candidate["referenceMedianNs"] += 1000
        self.assertAlmostEqual(compare(control, candidate), 1000 / 270000)
        self.assertEqual(control["cpuMedianNs"], 80)

    def test_original_reference_and_residency_boundaries_are_preserved(self):
        report = valid()
        report["steadyConditions"]["referenceBeforeNs"][0] = 275400  # Exactly 2% full range.
        report["steadyConditions"]["frequencyAfter"] = "2803200 104\n2918400 396\n"  # Exactly 98%.
        self.assertEqual(qualify(report)["dominantFraction"], .98)
        changed = copy.deepcopy(report)
        changed["steadyConditions"]["referenceBeforeNs"][0] += 1
        with self.assertRaises(ValueError): qualify(changed)
        changed = copy.deepcopy(report)
        changed["steadyConditions"]["frequencyAfter"] = "2803200 105\n2918400 395\n"
        with self.assertRaises(ValueError): qualify(changed)
        control = qualify(valid())
        candidate = copy.deepcopy(control); candidate["referenceMedianNs"] = 272700  # Exactly 1%.
        self.assertEqual(compare(control, candidate), .01)
        candidate["referenceMedianNs"] += 1
        with self.assertRaises(ValueError): compare(control, candidate)

    def test_allocating_units_retain_gc_and_cpu_variation(self):
        report = valid(); unit = "reflectNewInstance"; report["clockUnits"] = unit
        data = copy.deepcopy(report["steadyConditions"])
        data["gcAfter"] = "15"
        data["cpuSamplesNs"][0] *= 2
        report["workloads"][unit] = {"clockConditions": data, "callsPerBatch": 64, "checksum": 3 * 64 * 21}
        result = qualify_units(report, [unit])[unit]
        self.assertEqual(result["gcCycles"], 3)
        self.assertEqual(result["referenceMedianNs"], 270000)
        for field, value in (("referenceAfterNs", [280000] * 21),
                             ("cpuSamplesNs", []), ("gcAfter", "11")):
            changed = copy.deepcopy(report)
            changed["workloads"][unit]["clockConditions"][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError): qualify_units(changed, [unit])
        with self.assertRaises(ValueError): qualify_units(report, ["methodHandleBound"])
        changed = copy.deepcopy(report); changed["workloads"][unit]["checksum"] = 0
        with self.assertRaises(ValueError): qualify_units(changed, [unit])
        del report["workloads"][unit]["clockConditions"]
        with self.assertRaises(ValueError): qualify_units(report, [unit])


if __name__ == "__main__": unittest.main()
