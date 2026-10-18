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
spec = importlib.util.spec_from_file_location("abort_isolation", ROOT / "tools/check-interface-abort-isolation.py")
isolation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(isolation)


def complete_report(api=36, self_selection=None):
    if self_selection is None:
        self_selection = api >= 36
    if type(self_selection) is bool:
        self_selection = {"class": self_selection, "interface": self_selection}
    def rejection_check(name, code):
        if name == "tool-retransform-invalid-batch":
            return {"name": "nativeFallbackReason", "expected": True, "actual": True, "passed": True}
        if code == 0:
            return {"name": "lastRejection", "expected": "", "actual": "", "passed": True}
        prefix = "Lcom/github/ytlog/artdcevm/fixture/Evolving;: "
        return {"name": "rejectedByPolicy", "expected": prefix, "actual": prefix, "passed": True}
    return {
        "schemaVersion": 1, "completed": True, "failures": 0, "uid": 10123,
        "device": {"model": "Mock Android", "api": api, "release": str(api - 20)},
        "initialization": {"passed": True, "runtimeInitialized": True, "clientAttached": True,
                           "capabilities": {
            "installed": True, "standardRedefineRouted": True, "retransformRouted": 30 <= api <= 37,
            "memberAddition": True, "memberModifierChange": True, "classModifierChange": True,
            "interfaceAddition": True, "superclassWidening": True,
            "interfaceRedefinition": False, "interfaceRuntime": "unverified: interface runtime declarations",
            "methodDeletion": True, "methodSignatureChange": True,
            "fieldDeletion": True, "fieldTypeChange": True,
            "memberDeletion": True, "memberTypeChange": True,
            "hierarchyChange": False, "fullDcevmComplete": False,
            "privateSelfSelection": all(self_selection.values()),
            **{"privateSelfSelection" + kind.title(): value for kind, value in self_selection.items()},
            **{"privateSelfSelection" + kind.title() + "Reason": "" if value else "named selector unavailable"
               for kind, value in self_selection.items()}, "interfaceMethodSelection": True}},
        "scenarios": [{"name": name, "variants": variants, "expectedCode": code,
                       "actualCode": code, "completed": True, "passed": True,
                       "capabilities": {"interfaceMethodSelection": True, "retransformRouted": 30 <= api <= 37, "memberMapping": "verified: android16 class layout",
                                        "interfaceRedefinition": True,
                                        "interfaceRuntime": "verified: android16 Class/Thread/HandleScope/holder probes"},
                       "threadSingleStepCallbacks": 1 if name.startswith("methods-retire") else 0,
                       "interfaceTiming": {"types": 207, "snapshotNanos": 1000, "allocationNanos": 2000}
                           if name == "interface-stress" else None,
                       "checks": [{"name": "returnCode", "expected": code, "actual": code,
                                   "passed": True},
                                  *[{"name": key, "expected": value, "actual": value, "passed": True}
                                    for key, value in required.items()],
                                  *[{"name": key, "expected": "captured baseline", "actual": "captured baseline",
                                     "passed": True} for key in runner.UNCHANGED_CHECKS.get(name, ())],
                                  rejection_check(name, code)]}
                      for name, variants, base_code in runner.scenario_sequence("full", api, self_selection)
                      for code, required in [runner.release_expectation(
                          name, base_code, runner.REQUIRED_CHECKS[name], api, self_selection)]],
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
    print("not JSON" if mode == "bad_json" else open(os.environ["APK_TEST_REPORT_FILE"]).read())
    if mode == "report_read_exit":
        sys.exit(1)
else:
    sys.exit(2)
'''


class ApkVerificationTests(unittest.TestCase):
    def test_interface_selection_requires_transport_access_and_loader_evidence(self):
        report = complete_report(36)
        report['scenarios'] = [s for s in report['scenarios'] if s['name'].startswith('interface-selection-')]
        runner.validate_report(report, scenario_group='interface-selection')
        for name, key in (('interface-selection-local-default-0-add', 'PrivateChild.cachedHandle'),
                          ('interface-selection-local-default-0-add', 'OwnOnPlain.oldSite'),
                          ('interface-selection-split-abstract-0-swap', 'PackagePublic.classBytecode'),
                          ('interface-selection-local-parent-1-back', 'OwnPackage.access'),
                          ('interface-selection-split-parent-0-add', 'splitRuntimePackage'),
                          ('interface-selection-local-abstract-0-abort', 'failureHits')):
            broken = copy.deepcopy(report)
            scenario = next(s for s in broken['scenarios'] if s['name'] == name)
            scenario['checks'] = [c for c in scenario['checks'] if c['name'] != key]
            with self.subTest(scenario=name, missing=key), self.assertRaises(runner.RunFailure):
                runner.validate_report(broken, scenario_group='interface-selection')
        scenario = next(s for s in report['scenarios'] if s['name'] == 'interface-selection-local-default-0-add')
        check = next(c for c in scenario['checks'] if c['name'] == 'PrivateChild.cachedJni')
        check['expected'] = check['actual'] = 'java.lang.AbstractMethodError'
        with self.assertRaises(runner.RunFailure):
            runner.validate_report(report, scenario_group='interface-selection')

    def test_class_kinds_require_precise_calls_types_and_retained_members(self):
        report = complete_report(36)
        report['scenarios'] = [s for s in report['scenarios'] if s['name'].startswith('class-kind-')]
        runner.validate_report(report, scenario_group='class-kinds')
        for name, key in (('class-kind-c-i-0', 'oldNullCall'),
                          ('class-kind-a-c-1', 'castOther'),
                          ('class-kind-live-annotation-0', 'jni'),
                          ('class-kind-enum-class-0', 'marker'),
                          ('class-kind-abort', 'constantKept'),
                          ('class-kind-instance-reject', 'afterGc')):
            broken = copy.deepcopy(report)
            scenario = next(s for s in broken['scenarios'] if s['name'] == name)
            scenario['checks'] = [c for c in scenario['checks'] if c['name'] != key]
            with self.subTest(scenario=name, missing=key), self.assertRaises(runner.RunFailure):
                runner.validate_report(broken, scenario_group='class-kinds')
        broken = copy.deepcopy(report)
        scenario = next(s for s in broken['scenarios'] if s['name'] == 'class-kind-c-i-0')
        call = next(c for c in scenario['checks'] if c['name'] == 'oldNullCall')
        call['expected'] = call['actual'] = 'java.lang.NullPointerException'
        with self.assertRaises(runner.RunFailure):
            runner.validate_report(broken, scenario_group='class-kinds')

    def test_proxy_parent_graph_requires_type_reference_and_call_evidence(self):
        report = complete_report(36)
        report["scenarios"] = [s for s in report["scenarios"] if s["name"].startswith("proxy-interface")]
        runner.validate_report(report, scenario_group="proxy-interface")
        for name, check in (("proxy-interface-parent-drop", "parentReflection"),
                            ("proxy-interface-parents-drop", "holderCleared"),
                            ("proxy-interface-parents-drop-again", "retainedCall"),
                            ("proxy-interface-parents-batch", "extra"),
                            ("proxy-interface-parents-batch-shared-again", "added"),
                            ("proxy-interface-parents-shared", "holderCleared"),
                            ("proxy-interface-parents-batch-again", "batchOrder"),
                            ("proxy-interface-parents-batch", "root"),
                            ("proxy-interface-parents-batch", "proxyCachedConstructor"),
                            ("proxy-interface-parents-batch", "proxyMethod"),
                            ("proxy-interface-parents-batch", "onlyisRoot"),
                            ("proxy-interface-parents-drop-abort", "failureHits"),
                            ("proxy-interface-parents-batch-abort-again", "fieldsKept"),
                            ("proxy-interface-parents-remove-abort", "rootSum")):
            broken = copy.deepcopy(report)
            scenario = next(s for s in broken["scenarios"] if s["name"] == name)
            scenario["checks"] = [c for c in scenario["checks"] if c["name"] != check]
            with self.subTest(scenario=name, missing=check), self.assertRaises(runner.RunFailure):
                runner.validate_report(broken, scenario_group="proxy-interface")

    def test_proxy_parent_batch_orders_require_equal_behavior(self):
        report = complete_report(36)
        report["scenarios"] = [s for s in report["scenarios"] if s["name"].startswith("proxy-interface")]
        for scenario in report["scenarios"]:
            if scenario["name"] in ("proxy-interface-parents-shared", "proxy-interface-parents-shared-again"):
                value = not scenario["name"].endswith("-again")
                scenario["checks"].append({"name": "orderWitness", "expected": value, "actual": value, "passed": True})
        with self.assertRaisesRegex(runner.RunFailure, "outcomes differ"):
            runner.validate_report(report, scenario_group="proxy-interface")
        report = complete_report(36)
        report["scenarios"] = [s for s in report["scenarios"] if s["name"].startswith("proxy-interface")]
        changed = next(s for s in report["scenarios"] if s["name"] == "proxy-interface-parents-batch-again")
        check = next(c for c in changed["checks"] if c["name"] == "batchOrder")
        check["expected"] = check["actual"] = "A,Added,Root,B"
        with self.assertRaises(runner.RunFailure):
            runner.validate_report(report, scenario_group="proxy-interface")

    def test_interface_shadow_requires_jbr_dispatch_on_every_release(self):
        for api in (30, 33, 34, 35, 36, 37):
            report = complete_report(api)
            report["scenarios"] = [s for s in report["scenarios"] if s["name"].startswith("interface-shadow-")]
            self.assertEqual(len(report["scenarios"]), 19)
            runner.validate_report(report, scenario_group="interface-shadow")
            for name in ("interface-shadow-protected", "interface-shadow-package", "interface-shadow-other-package"):
                scenario = next(s for s in report["scenarios"] if s["name"] == name)
                self.assertEqual(scenario["actualCode"], 0)
                for check in ("interfaceShadow", "bytecode", "handle", "jni", "late", "lateJni", "hotFailures", "oldShadow"):
                    broken = copy.deepcopy(report)
                    target = next(s for s in broken["scenarios"] if s["name"] == name)
                    target["checks"] = [c for c in target["checks"] if c["name"] != check]
                    with self.subTest(api=api, scenario=name, check=check), self.assertRaises(runner.RunFailure):
                        runner.validate_report(broken, scenario_group="interface-shadow")

    def test_non_public_interface_calls_require_jbr_errors(self):
        for api in (33, 34, 35, 36):
            report = complete_report(api)
            runner.validate_report(report)
            for kind, wrong in (("package", "java.lang.AbstractMethodError"),
                                ("protected", "java.lang.AbstractMethodError"),
                                ("private", "java.lang.IllegalAccessError")):
                broken = copy.deepcopy(report)
                target = next(s for s in broken["scenarios"]
                              if s["name"] == "interface-abstract-unimplemented-" + kind)
                self.assertEqual(target["expectedCode"], 0)
                call = next(c for c in target["checks"] if c["name"] == "interfaceCall")
                call.update(expected=wrong, actual=wrong)
                with self.subTest(api=api, kind=kind), self.assertRaises(runner.RunFailure):
                    runner.validate_report(broken)

    def test_ordinary_group_requires_retirement_without_interface_admission(self):
        report = complete_report()
        names = {item[0] for item in runner.scenario_sequence("ordinary")}
        self.assertTrue({"methods-retire", "field-retire", "field-types", "interface-add"} <= names)
        self.assertFalse({"interface-method-bodies", "interface-abstract-batch-forward"} & names)
        report["scenarios"] = [item for item in report["scenarios"] if item["name"] in names]
        capabilities = report["initialization"]["capabilities"]
        capabilities["interfaceRuntime"] = "disabled: interface runtime not device-verified"
        for item in report["scenarios"]:
            item["capabilities"].update(interfaceRedefinition=False,
                                        interfaceRuntime=capabilities["interfaceRuntime"])
        runner.validate_report(report, scenario_group="ordinary")
        with self.assertRaises(runner.RunFailure):
            runner.validate_report(report)
        retired = next(item for item in report["scenarios"] if item["name"] == "methods-retire")
        retired["capabilities"]["memberMapping"] = "unverified"
        with self.assertRaises(runner.RunFailure):
            runner.validate_report(report, scenario_group="ordinary")
        retired["capabilities"]["memberMapping"] = "verified: android16 class layout"
        report["scenarios"].pop()
        with self.assertRaises(runner.RunFailure):
            runner.validate_report(report, scenario_group="ordinary")

    def test_invoke_kind_groups_require_both_directions(self):
        for group, reverse in (("invoke-kind", "invoke-kind-instance"),
                               ("invoke-interface-kind", "interface-kind-instance-legacy"),
                               ("method-private", "method-private")):
            with self.subTest(group=group):
                names = {item[0] for item in runner.scenario_sequence(group)}
                self.assertIn(reverse, names)
                report = complete_report()
                report["scenarios"] = [item for item in report["scenarios"] if item["name"] in names]
                runner.validate_report(report, scenario_group=group)
                changed = next(item for item in report["scenarios"] if item["name"] == reverse)
                error = next(check for check in changed["checks"]
                             if check["actual"] in ("java.lang.IncompatibleClassChangeError", "java.lang.IllegalAccessError"))
                error.update(expected="java.lang.NoSuchMethodError", actual="java.lang.NoSuchMethodError")
                with self.assertRaises(runner.RunFailure):
                    runner.validate_report(report, scenario_group=group)
                report["scenarios"].remove(changed)
                with self.assertRaises(runner.RunFailure):
                    runner.validate_report(report, scenario_group=group)

    def test_obsolete_self_calls_follow_reported_selection_services(self):
        names = {item[0] for item in runner.scenario_sequence("method-private")}
        for api, selection in ((33, False), (35, True), (35, False), (36, True),
                               (34, {"class": False, "interface": True}),
                               (37, {"class": True, "interface": False})):
            per_kind = {"class": selection, "interface": selection} if type(selection) is bool else selection
            report = complete_report(api, selection)
            report["scenarios"] = [item for item in report["scenarios"] if item["name"] in names]
            runner.validate_report(report, scenario_group="method-private")
            for kind in ("class", "interface"):
                changed = next(item for item in report["scenarios"] if item["name"] == "method-private-self-" + kind)
                self.assertEqual(changed["actualCode"], 0 if per_kind[kind] else 71)
                override = next(item for item in changed["checks"] if item["name"] == "overrideReceiver")
                saved = dict(override)
                override.update(expected=49, actual=49)
                if per_kind[kind] or kind == "class":
                    with self.assertRaises(runner.RunFailure):
                        runner.validate_report(report, scenario_group="method-private")
                override.update(saved)
                null = next(item for item in changed["checks"] if item["name"] == "nullReceiver")
                null.update(expected="java.lang.IllegalAccessError", actual="java.lang.IllegalAccessError")
                with self.assertRaises(runner.RunFailure):
                    runner.validate_report(report, scenario_group="method-private")
                null.update(expected="java.lang.NullPointerException", actual="java.lang.NullPointerException")

    def test_per_kind_selection_capabilities_and_masks(self):
        for kind in ("class", "interface"):
            for suffix in ("", "Reason"):
                report = complete_report()
                del report["initialization"]["capabilities"]["privateSelfSelection" + kind.title() + suffix]
                with self.assertRaises(runner.RunFailure): runner.validate_report(report)
        for selection, mode in (({"class": False, "interface": True}, "interface-only"),
                                ({"class": True, "interface": False}, "class-only"),
                                ({"class": False, "interface": False}, "disabled")):
            report = complete_report(36, selection)
            runner.validate_report(report, private_self_mode=mode)
            report["initialization"]["capabilities"]["privateSelfSelection"] = True
            with self.assertRaises(runner.RunFailure): runner.validate_report(report)
        for mode in ("class-only", "interface-only", "disabled"):
            with self.assertRaises(runner.RunFailure): runner.validate_report(complete_report(), private_self_mode=mode)
        report = complete_report()
        report["initialization"]["capabilities"]["privateSelfSelectionClassReason"] = "unavailable"
        with self.assertRaises(runner.RunFailure): runner.validate_report(report)

    def test_self_selection_capability_is_required(self):
        report = complete_report(35, True)
        del report["initialization"]["capabilities"]["privateSelfSelection"]
        with self.assertRaises(runner.RunFailure):
            runner.validate_report(report)

    def test_optimized_lab_flags_keep_symbols_assertions_and_faults(self):
        def command(extra="", flags=None):
            arguments = flags or ["clang++", "-g", "-O2", "-DARTDCEVM_INTERFACE_ABORT_TESTS",
                                  "-o", "test-engine/CMakeFiles/artdcevm.dir/agent.cc.o"]
            return [{"command": " ".join(arguments) + " " + extra}]
        self.assertEqual(isolation.verify_test_compile_flags(command()), 1)
        for extra in ("-O0", "-g0", "-DNDEBUG", "-DNDEBUG=0"):
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                isolation.verify_test_compile_flags(command(extra))
        with self.assertRaises(ValueError):
            isolation.verify_test_compile_flags(command(flags=["clang++", "-g", "-O2", "-o",
                                                               "test-engine/CMakeFiles/artdcevm.dir/agent.cc.o"]))
        with self.assertRaises(ValueError):
            isolation.verify_test_compile_flags([])

    def test_performance_summary_requires_complete_consistent_timings(self):
        report = complete_report()
        self.assertIsNone(runner.report_performance(report))
        report.update(reportNanos=50, reportWrites=2)
        for index, scenario in enumerate(report["scenarios"]):
            scenario.update(elapsedNanos=100 + index, jvmtiNanos=50)
        summary = runner.report_performance(report)
        self.assertEqual(summary["slowestScenarios"][0]["name"], report["scenarios"][-1]["name"])
        for elapsed, native in ((-1, 0), (1, 2), (True, 0), (1, None)):
            broken = copy.deepcopy(report)
            broken["scenarios"][0].update(elapsedNanos=elapsed, jvmtiNanos=native)
            with self.subTest(elapsed=elapsed, native=native), self.assertRaises(runner.RunFailure):
                runner.report_performance(broken)
        del report["scenarios"][0]["jvmtiNanos"]
        with self.assertRaises(runner.RunFailure):
            runner.report_performance(report)

    def test_tool_group_requires_append_and_later_redefinition(self):
        names = {item[0] for item in runner.scenario_sequence("tool")}
        self.assertEqual(names, {"tool-add-classes", "tool-redefine-added", "tool-retransform-structural",
                                 "tool-retransform-restore", "tool-retransform-add-again",
                                 "tool-retransform-no-replacement", "tool-retransform-add-for-retire",
                                 "tool-retransform-invalid-batch", "tool-retransform-abort", "tool-retransform-retire",
                                 "tool-retransform-restore-retired", "tool-retransform-nested",
                                 "tool-retransform-nested-restore"})
        report = complete_report()
        report["scenarios"] = [item for item in report["scenarios"] if item["name"] in names]
        runner.validate_report(report, scenario_group="tool")
        report["scenarios"].pop()
        with self.assertRaises(runner.RunFailure):
            runner.validate_report(report, scenario_group="tool")

    def test_retransform_requires_behavior_and_installed_capability(self):
        report = complete_report()
        names = {item[0] for item in runner.scenario_sequence("tool")}
        report["scenarios"] = [item for item in report["scenarios"] if item["name"] in names]
        for name in ("tool-retransform-structural", "tool-retransform-restore"):
            scenario = next(item for item in report["scenarios"] if item["name"] == name)
            for key in runner.REQUIRED_CHECKS[name]:
                broken = copy.deepcopy(report)
                target = next(item for item in broken["scenarios"] if item["name"] == name)
                target["checks"] = [item for item in target["checks"] if item["name"] != key]
                with self.subTest(name=name, missing=key), self.assertRaises(runner.RunFailure):
                    runner.validate_report(broken, scenario_group="tool")
            scenario["capabilities"]["retransformRouted"] = False
            with self.assertRaises(runner.RunFailure):
                runner.validate_report(report, scenario_group="tool")
            scenario["capabilities"]["retransformRouted"] = True
        del report["initialization"]["capabilities"]["retransformRouted"]
        with self.assertRaises(runner.RunFailure):
            runner.validate_report(report, scenario_group="tool")
        for api in (30, 31, 32):
            legacy = complete_report(api)
            legacy["scenarios"] = [item for item in legacy["scenarios"] if item["name"].startswith("tool-")]
            self.assertEqual(len(legacy["scenarios"]), 13)
            runner.validate_report(legacy, scenario_group="tool")

    def test_legacy_retransform_requires_enhanced_success(self):
        for api in (30, 31, 32, 33, 36, 37):
            report = complete_report(api)
            names = {item[0] for item in runner.scenario_sequence("retransform")}
            report["scenarios"] = [item for item in report["scenarios"] if item["name"] in names]
            self.assertEqual(len(report["scenarios"]), 11)
            runner.validate_report(report, scenario_group="retransform")
            for name in names:
                item = next(x for x in report["scenarios"] if x["name"] == name)
                for key in ("hookCalls", "hookReplacements", "retransformRouted"):
                    broken = copy.deepcopy(report)
                    target = next(x for x in broken["scenarios"] if x["name"] == name)
                    target["checks"] = [x for x in target["checks"] if x["name"] != key]
                    with self.assertRaises(runner.RunFailure): runner.validate_report(broken, scenario_group="retransform")
                if item["actualCode"] == 0:
                    saved = item["actualCode"]; item["actualCode"] = 64
                    with self.assertRaises(runner.RunFailure): runner.validate_report(report, scenario_group="retransform")
                    item["actualCode"] = saved
            invalid = next(x for x in report["scenarios"] if x["name"] == "tool-retransform-invalid-batch")
            invalid["checks"] = [x for x in invalid["checks"] if x["name"] != "nativeFallbackReason"]
            with self.assertRaises(runner.RunFailure): runner.validate_report(report, scenario_group="retransform")

    def test_batch_combinations_require_zero_parent_fields_and_abort_recovery(self):
        report = complete_report()
        runner.validate_report(report)
        for number in (1, 3, 4, 5, 6):
            name = f"batch-combination-s{number}"
            for key in ("newParentZero", "own", "afterGc", "newAdded", "newParentZeroAfterGc"):
                changed = copy.deepcopy(report)
                scenario = next(s for s in changed["scenarios"] if s["name"] == name)
                scenario["checks"] = [c for c in scenario["checks"] if c["name"] != key]
                with self.assertRaises(runner.RunFailure):
                    runner.validate_report(changed)
            changed = copy.deepcopy(report)
            abort = next(s for s in changed["scenarios"] if s["name"] == name + "-abort")
            abort["checks"] = [c for c in abort["checks"] if c["name"] != "unchangedDefinition0"]
            with self.assertRaises(runner.RunFailure):
                runner.validate_report(changed)
        for number in (1, 4):
            changed = copy.deepcopy(report)
            scenario = next(s for s in changed["scenarios"] if s["name"] == f"batch-combination-s{number}")
            check = next(c for c in scenario["checks"] if c["name"] == "newParentFieldZero")
            check.update(expected=3, actual=3, passed=True)
            with self.assertRaises(runner.RunFailure):
                runner.validate_report(changed)

    def test_abstract_group_requires_all_focused_scenarios(self):
        report = complete_report()
        report["scenarios"] = [item for item in report["scenarios"]
                               if item["name"].startswith("interface-abstract-")]
        runner.validate_report(report, scenario_group="interface-abstract")
        with self.assertRaises(runner.RunFailure):
            runner.validate_report(report)
        with self.assertRaises(runner.RunFailure):
            runner.validate_report(report, scenario_group="unknown")
        report["scenarios"].pop()
        with self.assertRaises(runner.RunFailure):
            runner.validate_report(report, scenario_group="interface-abstract")

    def test_stress_requires_complete_type_count_and_positive_phase_measurements(self):
        runner.validate_report(complete_report())
        for key, value in (("types", 206), ("snapshotNanos", 0), ("allocationNanos", -1),
                           ("snapshotNanos", True), ("allocationNanos", "unknown")):
            with self.subTest(key=key, value=value):
                report = complete_report()
                scenario = next(item for item in report["scenarios"] if item["name"] == "interface-stress")
                scenario["interfaceTiming"][key] = value
                with self.assertRaises(runner.RunFailure):
                    runner.validate_report(report)

    def test_interface_capability_requires_probe_evidence(self):
        for state in ("unavailable", "unverified: interface runtime declarations", "disabled: probe mismatch"):
            report = complete_report()
            report["scenarios"][0]["capabilities"].update(interfaceRuntime=state, interfaceRedefinition=True)
            with self.subTest(state=state), self.assertRaises(runner.RunFailure):
                runner.validate_report(report)
        report = complete_report()
        report["initialization"]["capabilities"]["interfaceRedefinition"] = True
        with self.assertRaises(runner.RunFailure):
            runner.validate_report(report)
        report = complete_report()
        report["scenarios"][0]["capabilities"]["interfaceRedefinition"] = False
        with self.assertRaises(runner.RunFailure):
            runner.validate_report(report)

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
                                 "incomplete_scenario", "wrong_expected_behavior",
                                 "missing_policy_rejection", "stale_policy_rejection",
                                 "missing_cached_jni", "wrong_reflection_exception",
                                 "hierarchy_claimed", "missing_method_capability",
                                 "missing_field_capability", "wrong_field_exception",
                                 "missing_interface_capability", "interface_preloaded",
                                 "missing_single_step_count", "single_step_no_callbacks", "matching"]
        evidence = ROOT / "build/evidence"
        evidence.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="apk-verification-", dir=evidence) as temporary:
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
                        report["scenarios"][2]["actualCode"] = 67
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
                    elif mode == "missing_method_capability":
                        del report["initialization"]["capabilities"]["methodDeletion"]
                    elif mode == "hierarchy_claimed":
                        report["initialization"]["capabilities"]["hierarchyChange"] = True
                    elif mode == "missing_field_capability":
                        del report["initialization"]["capabilities"]["fieldDeletion"]
                    elif mode == "missing_interface_capability":
                        del report["initialization"]["capabilities"]["interfaceAddition"]
                    elif mode == "interface_preloaded":
                        scenario = next(item for item in report["scenarios"]
                                        if item["name"] == "interface-add")
                        check = next(item for item in scenario["checks"]
                                     if item["name"] == "contractLoadedBefore")
                        check["expected"] = check["actual"] = True
                    elif mode == "wrong_field_exception":
                        scenario = next(item for item in report["scenarios"]
                                        if item["name"] == "field-retire")
                        check = next(item for item in scenario["checks"] if item["name"] == "jni0")
                        check["expected"] = check["actual"] = "java.lang.IllegalArgumentException"
                    elif mode in ("missing_cached_jni", "wrong_reflection_exception"):
                        scenario = next(item for item in report["scenarios"]
                                        if item["name"] == "methods-retire")
                        if mode == "missing_cached_jni":
                            scenario["checks"] = [item for item in scenario["checks"]
                                                  if not item["name"].startswith("cachedJni")]
                        else:
                            check = next(item for item in scenario["checks"]
                                         if item["name"] == "cachedReflection0")
                            check["expected"] = check["actual"] = "java.lang.NoSuchMethodError"
                    elif mode in ("missing_single_step_count", "single_step_no_callbacks"):
                        scenario = next(item for item in report["scenarios"]
                                        if item["name"] == "methods-retire")
                        if mode == "missing_single_step_count":
                            del scenario["threadSingleStepCallbacks"]
                        else:
                            scenario["threadSingleStepCallbacks"] = 0
                    elif mode == "boolean_return_code":
                        report["scenarios"][0]["actualCode"] = False
                    elif mode == "code_only":
                        report["scenarios"][0]["checks"] = report["scenarios"][0]["checks"][:1]
                    elif mode == "incomplete_scenario":
                        report["scenarios"][0]["completed"] = False
                    elif mode == "missing_policy_rejection":
                        report["scenarios"][2]["checks"].pop()
                    elif mode == "stale_policy_rejection":
                        check = report["scenarios"][0]["checks"][-1]
                        check["expected"] = check["actual"] = "Lcom/github/ytlog/artdcevm/fixture/FieldSubject;: stale"
                    elif mode == "wrong_expected_behavior":
                        check = report["scenarios"][0]["checks"][1]
                        check["expected"] = check["actual"] = 99
                    report_file = work / "mock-report.json"
                    report_file.write_text(json.dumps(report))
                    environment = dict(os.environ, APK_TEST_TRACE=str(trace), APK_TEST_MODE=mode,
                                       APK_TEST_SHA=digest, APK_TEST_REPORT_FILE=str(report_file))
                    command = [sys.executable, str(RUNNER), "--serial", "private-device-selector",
                               "--device-id", "android-test-device", "--adb", str(fake_adb),
                               "--apk", str(apk), "--timeout", "1",
                               "--evidence-root", str(work / "build/evidence/runs")]
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
                        self.assertEqual(metadata["scenarioCount"], len(runner.SCENARIOS))
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
