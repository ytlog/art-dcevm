#!/usr/bin/env python3
"""Reject incomplete matrices, self-consistent wrong values and false aborts."""
import copy
from collections import Counter
import unittest
from jni_receiver_report import expected_checks, matrix, validate_report

EXIT = "INSTRUMENTATION_CODE: -1"
DIAGNOSTIC = ("JNI DETECTED ERROR IN APPLICATION: can't call int ReceiverLossFixture$Contract.value() "
              "on instance of ReceiverLossFixture$DefaultOnly\nFatal signal 6 (SIGABRT)")


def fixture(case):
    outcome, values = expected_checks(case)
    report = dict(schemaVersion=1, failures=0, own=case["receiver"] == "own",
        interfaceOwner=case["owner"] == "interface", nonvirtual=case["call"] == "nonvirtual",
        cacheBeforeAttach=case["cache"] == "before", checkJni=case["checkjni"] == "true",
        form=int(case["form"]), idTag=0 if case.get("promote") == "true" and case["cache"] == "before" else 1,
        completed=outcome != "checkjni-diagnostic",
        stage="r0apply.beforeCall" if outcome == "checkjni-diagnostic" else "finished",
        checks=[dict(name=name, expected=value, actual=value, passed=True) for name, value in values])
    diagnostic = DIAGNOSTIC.replace("$DefaultOnly", "$Own") if case["receiver"] == "own" else DIAGNOSTIC
    if case["owner"] == "class" and case["call"] == "nonvirtual":
        diagnostic = ("JNI DETECTED ERROR IN APPLICATION: can't call nonvirtual <OBSOLETE> int "
                      "ReceiverLossFixture$Contract.value() with class "
                      "java.lang.Class<ReceiverLossFixture$DefaultOnly>\nFatal signal 6 (SIGABRT)")
    return report, diagnostic if outcome == "checkjni-diagnostic" else ""


class Reports(unittest.TestCase):
    def test_matrix(self):
        for mode, counts in (("index", {"call-aligned": 60, "checkjni-diagnostic": 36}),
                ("mixed", {"call-aligned": 48, "checkjni-diagnostic": 36, "admission-rejected-79": 12})):
            cases = matrix(mode)
            self.assertEqual(len(cases), 96)
            self.assertEqual(len({tuple(case.items()) for case in cases}), 96)
            self.assertEqual(Counter(expected_checks(case)[0] for case in cases), counts)
            for case in cases:
                report, crash = fixture(case)
                self.assertEqual(validate_report(report, case, crash, EXIT), expected_checks(case)[0])
                self.assertEqual(len(report["checks"]),
                    32 if case.get("admission") else (19 if mode == "mixed" else 18)
                    if crash else 75 if mode == "mixed" else 74)

    def test_wrong_values_cannot_validate_themselves(self):
        case = matrix("mixed")[18]  # Old copied default, nonvirtual, plain JNI.
        report, crash = fixture(case)
        for check in report["checks"]:
            if check["name"] == "r0apply.jni":
                self.assertEqual(check["expected"], 7)
                check.update(expected="java.lang.InternalError", actual="java.lang.InternalError", passed=True)
        with self.assertRaises(ValueError): validate_report(report, case, crash, EXIT)

    def test_missing_or_altered_evidence(self):
        case = matrix("mixed")[18]
        for mutation in (lambda r: r.update(idTag=1), lambda r: r.update(completed=False),
                lambda r: r.update(failures=1), lambda r: r.update(nonvirtual=False),
                lambda r: r["checks"].pop(), lambda r: r["checks"][0].update(actual=False)):
            report, crash = fixture(case)
            mutation(report)
            with self.assertRaises(ValueError): validate_report(report, case, crash, EXIT)

    def test_original_receiver_diagnostic_is_required(self):
        case = matrix("mixed")[1]
        report, _ = fixture(case)
        self.assertEqual(validate_report(report, case, DIAGNOSTIC.replace("call int", "call <OBSOLETE> int"), EXIT),
                         "checkjni-diagnostic")
        for crash in ("", "Fatal signal 11 (SIGSEGV)", "JNI DETECTED ERROR IN APPLICATION SIGABRT",
                DIAGNOSTIC.replace("SIGABRT", "SIGSEGV"),
                DIAGNOSTIC + "This non-structurally obsolete method"):
            with self.assertRaises(ValueError): validate_report(report, case, crash, EXIT)
        altered = copy.deepcopy(report)
        altered["stage"] = "baseline.beforeCall"
        with self.assertRaises(ValueError): validate_report(altered, case, DIAGNOSTIC, EXIT)

    def test_79_does_not_count_as_a_call(self):
        case = next(c for c in matrix("mixed") if c.get("admission"))
        report, crash = fixture(case)
        self.assertEqual(validate_report(report, case, crash, EXIT), "admission-rejected-79")
        report["checks"][4].update(expected=0, actual=0)
        with self.assertRaises(ValueError): validate_report(report, case, crash, EXIT)


if __name__ == "__main__": unittest.main()
