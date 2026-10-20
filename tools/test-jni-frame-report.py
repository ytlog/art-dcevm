#!/usr/bin/env python3
"""Reject softened receiver errors, invented frame returns and incomplete proofs."""
import copy
from collections import Counter
import unittest
from jni_frame_report import expected_checks, matrix, validate_report

EXIT = "INSTRUMENTATION_CODE: -1"


def fixture(case):
    outcome, checks = expected_checks(case)
    report = dict(schemaVersion=1, failures=0, own=case["receiver"] == "own",
        interfaceOwner=case["owner"] == "interface", nonvirtual=case["call"] == "nonvirtual",
        cacheBeforeAttach=case["cache"] == "before", checkJni=case["checkjni"] == "true",
        form=int(case["form"]), frame=case["frame"],
        idTag=0 if case.get("promote") == "true" and case["cache"] == "before" else 1,
        completed=outcome != "checkjni-diagnostic",
        stage=("r0apply.resumeBeforeCall" if case["frame"] == "native" else "r0apply.followupBeforeCall")
            if outcome == "checkjni-diagnostic" else "finished",
        checks=[dict(name=name, expected=value, actual=value, passed=True) for name, value in checks])
    receiver = "Own" if case["receiver"] == "own" else "DefaultOnly"
    diagnostic = ("JNI DETECTED ERROR IN APPLICATION: can't call <OBSOLETE> int JniFrameFixture$Contract.value() "
                  "on instance of JniFrameFixture$" + receiver + "\nFatal signal6 (SIGABRT)")
    if case["owner"] == "class" and case["call"] == "nonvirtual":
        diagnostic = ("JNI DETECTED ERROR IN APPLICATION: can't call nonvirtual int JniFrameFixture$Contract.value() "
                      "with class java.lang.Class<JniFrameFixture$DefaultOnly>\nFatal signal6 (SIGABRT)")
    return report, diagnostic if outcome == "checkjni-diagnostic" else ""


class Reports(unittest.TestCase):
    def test_exact_matrix_outcomes_and_full_checks(self):
        for mode, counts in (("index", {"call-aligned": 120, "checkjni-diagnostic": 72}),
                ("mixed", {"call-aligned": 96, "checkjni-diagnostic": 72, "admission-rejected-79": 24})):
            cases = matrix(mode)
            self.assertEqual(len(cases), 192)
            self.assertEqual(len({tuple(case.items()) for case in cases}), 192)
            self.assertEqual(Counter(expected_checks(c)[0] for c in cases), counts)
            for case in cases:
                report, crash = fixture(case)
                outcome = validate_report(report, case, crash, EXIT)
                count = (73 if outcome == "admission-rejected-79" else
                         (26 if case["frame"] == "native" else 27) + (mode == "mixed")
                         if outcome == "checkjni-diagnostic" else 101 + (mode == "mixed"))
                self.assertEqual(len(report["checks"]), count)

    def test_native_and_java_frames_have_different_post_commit_results(self):
        for kind, wrong in (("native", 7), ("body", "java.lang.IncompatibleClassChangeError")):
            case = next(c for c in matrix("index") if c["frame"] == kind and c["checkjni"] == "false"
                        and c["receiver"] == "default" and c["call"] == "virtual")
            report, crash = fixture(case)
            check = next(c for c in report["checks"] if c["name"] == "r0apply.frame")
            check.update(expected=wrong, actual=wrong, passed=True)
            with self.subTest(frame=kind), self.assertRaises(ValueError):
                validate_report(report, case, crash, EXIT)

    def test_missing_gc_identity_frame_and_policy_evidence(self):
        case = next(c for c in matrix("index") if c["frame"] == "body" and c["checkjni"] == "false")
        for key in ("threadPolicy", "r0.held", "r0apply.gcSelf", "r1restoreAbort.frame", "r1restore.jni"):
            report, crash = fixture(case)
            report["checks"] = [c for c in report["checks"] if c["name"] != key]
            with self.subTest(missing=key), self.assertRaises(ValueError):
                validate_report(report, case, crash, EXIT)
        for field, value in (("idTag", True), ("schemaVersion", True), ("failures", False),
                             ("frame", "native"), ("completed", False)):
            report, crash = fixture(case);report[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_report(report, case, crash, EXIT)

    def test_body_must_return_before_the_original_checkjni_diagnostic(self):
        case = next(c for c in matrix("index") if c["frame"] == "body" and c["checkjni"] == "true"
                    and c["receiver"] == "default")
        report, crash = fixture(case)
        self.assertEqual(validate_report(report, case, crash, EXIT), "checkjni-diagnostic")
        for stage in ("r0apply.resumeBeforeCall", "baseline.followupBeforeCall", "finished"):
            altered = copy.deepcopy(report);altered["stage"] = stage
            with self.subTest(stage=stage), self.assertRaises(ValueError):
                validate_report(altered, case, crash, EXIT)
        for bad in ("", "Fatal signal11 SIGSEGV", crash.replace("SIGABRT", "SIGSEGV"),
                    crash.replace("$DefaultOnly", "$Own"), crash + "This non-structurally obsolete method",
                    crash + "\nFatal signal 11 (SIGSEGV)"):
            with self.assertRaises(ValueError): validate_report(report, case, bad, EXIT)
        altered = copy.deepcopy(report)
        altered["checks"] = [c for c in altered["checks"] if c["name"] != "r0apply.frame"]
        with self.assertRaises(ValueError): validate_report(altered, case, crash, EXIT)

    def test_79_remains_a_separate_non_alignment_outcome(self):
        case = next(c for c in matrix("mixed") if c.get("admission"))
        report, crash = fixture(case)
        self.assertEqual(validate_report(report, case, crash, EXIT), "admission-rejected-79")
        check = next(c for c in report["checks"] if c["name"] == "r0reject.code")
        check.update(expected=0, actual=0)
        with self.assertRaises(ValueError): validate_report(report, case, crash, EXIT)


if __name__ == "__main__": unittest.main()
