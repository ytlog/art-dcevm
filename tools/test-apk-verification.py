#!/usr/bin/env python3
"""Exercise artifact guards and complete behavioral device-report validation."""

import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from apk_verification import base_apk_path, require_install_success, verified_apk_sha256

ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "tools/run-device-tests.py"
spec = importlib.util.spec_from_file_location("device_runner", RUNNER)
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


def complete_report():
    return {
        "schemaVersion": 1, "completed": True, "failures": 0, "uid": 10123,
        "device": {"model": "Mock Android", "api": 36, "release": "16"},
        "initialization": {"passed": True, "runtimeInitialized": True, "clientAttached": True,
                           "capabilities": {
            "installed": True, "standardRedefineRouted": True,
            "memberAddition": True, "memberModifierChange": True,
            "memberDeletion": False, "memberTypeChange": False,
            "hierarchyChange": False, "fullDcevmComplete": False}},
        "scenarios": [{"name": name, "variants": variants, "expectedCode": code,
                       "actualCode": code, "completed": True, "passed": True,
                       "checks": [{"name": "returnCode", "expected": code, "actual": code,
                                   "passed": True},
                                  *[{"name": key, "expected": value, "actual": value, "passed": True}
                                    for key, value in runner.REQUIRED_CHECKS[name].items()],
                                  *[{"name": key, "expected": "captured baseline", "actual": "captured baseline",
                                     "passed": True} for key in runner.UNCHANGED_CHECKS.get(name, ())]]}
                      for name, variants, code in runner.SCENARIOS],
    }


FAKE_ADB = r'''#!/usr/bin/env python3
import json
import os
import sys
import time

parts = sys.argv[3:]
with open(os.environ["APK_TEST_TRACE"], "a") as trace:
    trace.write(json.dumps(parts) + "\n")
mode = os.environ["APK_TEST_MODE"]
if parts[0] == "install":
    if mode == "install_rejected":
        private_host = ".".join(("192", "168", "56", "2")) + ":5555"
        print(f"Failure [INSTALL_FAILED_ABORTED: private-device-selector /home/<user>/private {private_host}]")
    elif mode == "stderr_rejected":
        print("Success")
        print("Failure [INSTALL_FAILED_ABORTED]", file=sys.stderr)
    elif mode == "install_no_success":
        print("Performing Streamed Install")
    else:
        print("Success")
    if mode == "install_exit":
        sys.exit(1)
elif "path" in parts:
    print("package:/data/app/mock/split.apk" if mode == "missing_path" else "package:/data/app/mock/base.apk")
    if mode == "path_exit":
        sys.exit(1)
elif "sha256sum" in parts:
    digest = "0" * 64 if mode == "stale_hash" else "invalid" if mode == "malformed_hash" else os.environ["APK_TEST_SHA"]
    print(digest + "  /data/app/mock/base.apk")
    if mode == "hash_exit":
        sys.exit(1)
elif "force-stop" in parts:
    if mode == "force_stop_exit":
        sys.exit(1)
elif "rm" in parts:
    if mode == "remove_report_exit":
        sys.exit(1)
elif "instrument" in parts:
    if mode == "instrument_timeout":
        print("INSTRUMENTATION_STATUS: stream=partial output", flush=True)
        time.sleep(3)
    if mode != "instrument_missing_code":
        print("INSTRUMENTATION_CODE: " + ("0" if mode == "instrument_bad_code" else "-1"))
    if mode == "instrument_failed":
        print("INSTRUMENTATION_FAILED: Process crashed")
    if mode == "instrument_exit":
        sys.exit(1)
elif "cat" in parts:
    print("not JSON" if mode == "bad_json" else os.environ["APK_TEST_REPORT"])
    if mode == "report_read_exit":
        sys.exit(1)
else:
    sys.exit(2)
'''


class ApkVerificationTests(unittest.TestCase):
    def test_install_and_artifact_helpers(self):
        for code, out, err in [(0, "Failure [INSTALL_FAILED_ABORTED]", ""),
                               (0, "Success", "Failure [INSTALL_FAILED_ABORTED]"),
                               (0, "Performing Streamed Install", ""), (1, "Success", "")]:
            with self.subTest(code=code, out=out, err=err), self.assertRaises(RuntimeError):
                require_install_success(code, out, err)
        require_install_success(0, "Performing Streamed Install\nSuccess\n", "")
        self.assertEqual(base_apk_path("package:/data/app/mock/base.apk\npackage:/data/app/mock/split.apk"),
                         "/data/app/mock/base.apk")
        for output in ["package:/data/app/mock/split.apk",
                       "package:/data/app/mock/base.apk\npackage:/data/app/other/base.apk"]:
            with self.subTest(output=output), self.assertRaises(RuntimeError):
                base_apk_path(output)
        self.assertEqual(verified_apk_sha256("A" * 64 + "  /data/app/mock/base.apk", "a" * 64), "a" * 64)

    def test_fake_adb(self):
        guarded_modes = ["install_rejected", "stderr_rejected", "install_no_success", "install_exit",
                         "missing_path", "path_exit", "stale_hash", "malformed_hash", "hash_exit",
                         "force_stop_exit", "remove_report_exit"]
        modes = guarded_modes + ["instrument_exit", "instrument_bad_code", "instrument_missing_code",
                                 "instrument_failed", "instrument_timeout", "report_read_exit", "bad_json",
                                 "incomplete", "behavior_failed", "lying_behavior", "wrong_return_code",
                                 "missing_scenario", "wrong_order", "missing_checks", "initialization_failed",
                                 "wrong_uid", "missing_capability", "boolean_return_code", "code_only",
                                 "incomplete_scenario", "wrong_expected_behavior", "matching"]
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            apk = directory / "fixture.apk"
            apk.write_bytes(b"frozen artifact for runner regression checks")
            digest = hashlib.sha256(apk.read_bytes()).hexdigest()
            fake_adb = directory / "adb"
            fake_adb.write_text(FAKE_ADB)
            fake_adb.chmod(0o755)
            for mode in modes:
                with self.subTest(mode=mode):
                    work = directory / mode
                    work.mkdir()
                    trace = work / "trace.jsonl"
                    report = copy.deepcopy(complete_report())
                    if mode == "incomplete":
                        report["completed"] = False
                    elif mode == "behavior_failed":
                        report["scenarios"][1]["checks"][1]["passed"] = False
                    elif mode == "lying_behavior":
                        report["scenarios"][1]["checks"][1]["actual"] = False
                    elif mode == "wrong_return_code":
                        report["scenarios"][2]["actualCode"] = 0
                    elif mode == "missing_scenario":
                        report["scenarios"].pop()
                    elif mode == "wrong_order":
                        report["scenarios"].reverse()
                    elif mode == "missing_checks":
                        report["scenarios"][0]["checks"] = []
                    elif mode == "initialization_failed":
                        report["initialization"]["passed"] = False
                    elif mode == "wrong_uid":
                        report["uid"] = 2000
                    elif mode == "missing_capability":
                        del report["initialization"]["capabilities"]["standardRedefineRouted"]
                    elif mode == "boolean_return_code":
                        report["scenarios"][0]["actualCode"] = False
                    elif mode == "code_only":
                        report["scenarios"][0]["checks"] = report["scenarios"][0]["checks"][:1]
                    elif mode == "incomplete_scenario":
                        report["scenarios"][0]["completed"] = False
                    elif mode == "wrong_expected_behavior":
                        check = report["scenarios"][0]["checks"][1]
                        check["expected"] = check["actual"] = 99
                    environment = dict(os.environ, APK_TEST_TRACE=str(trace), APK_TEST_MODE=mode,
                                       APK_TEST_SHA=digest, APK_TEST_REPORT=json.dumps(report))
                    command = [sys.executable, str(RUNNER), "--serial", "private-device-selector",
                               "--device-id", "android-test-device", "--adb", str(fake_adb),
                               "--apk", str(apk), "--timeout", "1"]
                    result = subprocess.run(command, cwd=work, env=environment,
                                            capture_output=True, text=True, timeout=15)
                    positive = mode == "matching"
                    self.assertEqual(result.returncode == 0, positive, result.stdout + result.stderr)
                    calls = [json.loads(line) for line in trace.read_text().splitlines()]
                    reached_instrumentation = any("instrument" in call for call in calls)
                    self.assertEqual(reached_instrumentation, mode not in guarded_modes)
                    runs = list((work / "build/evidence/runs").glob("*/run.json"))
                    self.assertEqual(len(runs), 1)
                    metadata = json.loads(runs[0].read_text())
                    self.assertEqual(metadata["passed"], positive)
                    if positive:
                        self.assertEqual(metadata["installedApkSha256"], digest)
                        self.assertEqual(metadata["apkSha256"], digest)
                        self.assertEqual(metadata["scenarioCount"], 12)
                        self.assertTrue(any(runner.COMPONENT in call for call in calls))
                        self.assertTrue(any(call[-1] == runner.REPORT_PATH and "cat" in call for call in calls))
                    if mode in guarded_modes[:9]:
                        self.assertFalse(any("force-stop" in call for call in calls))
                    if mode == "instrument_timeout":
                        self.assertIn("partial output", (runs[0].parent / "instrumentation.txt").read_text())
                        self.assertTrue(any("cat" in call for call in calls))
                        self.assertEqual(json.loads((runs[0].parent / "structural-redefine.json").read_text()), report)
                    private_host = ".".join(("192", "168", "56", "2")) + ":5555"
                    for secret in ("private-device-selector", str(directory), "/home/<user>/private", private_host):
                        self.assertNotIn(secret, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
