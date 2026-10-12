#!/usr/bin/env python3
"""Run the ordered structural-redefinition checks on a verified debug APK."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

from apk_verification import base_apk_path, require_install_success, verified_apk_sha256
from evidence_paths import new_evidence_directory


ROOT = Path(__file__).resolve().parents[1]
PACKAGE = "dev.artdcevm.lab"
COMPONENT = PACKAGE + "/com.github.ytlog.artdcevm.testing.StructuralRedefineInstrumentation"
REPORT_PATH = "files/structural-redefine.json"
SCENARIOS = (
    ("body", ["body"], 0),
    ("add", ["add"], 0),
    ("method-delete", ["method-delete"], 67),
    ("hierarchy", ["hierarchy"], 66),
    ("atomic-modifier-public-field-delete", ["modifier-public", "field-delete"], 64),
    ("modifier-public", ["modifier-public"], 0),
    ("modifier-final", ["modifier-final"], 0),
    ("modifier-sync", ["modifier-sync"], 0),
    ("modifier-field-final", ["modifier-field-final"], 0),
    ("field-delete", ["field-delete"], 64),
    ("field-types", ["field-types"], 64),
    ("class-final", ["class-final"], 70),
)
EVOLVING_CHECKS = {"oldCompute": 13, "newCompute": 13, "addedType": "long",
                   "oldAdded": 0, "newAdded": 77, "oldAddedMethod": 110,
                   "newAddedMethod": 187, "oldValue": 10, "staticCounter": 7}
FIELD_CHECKS = {"oldCompute": 41, "newCompute": 41, "instanceReferencePreserved": True,
               "staticReferencePreserved": True}
MODIFIER_CHECKS = {"valueFlags": 1, "packageValueFlags": 1, "computePublic": True,
                  "packageComputeFlags": 1, "modifierThroughPublic": 43,
                  "passiveThroughPublic": 43, "packageCompute": 13,
                  "passiveSuperclass": "com.github.ytlog.artdcevm.fixture.ModifierSubject",
                  "newPassiveThroughPublic": 43}
REQUIRED_CHECKS = {
    "body": {"computeBefore": 11, "oldCompute": 12, "newCompute": 12},
    "add": EVOLVING_CHECKS,
    "method-delete": EVOLVING_CHECKS,
    "hierarchy": {**EVOLVING_CHECKS, "superclass": "java.lang.Object", "interfaceCount": 0},
    "atomic-modifier-public-field-delete": {
        **FIELD_CHECKS, "modifierBefore": 42, "passiveBefore": 42,
        "modifierThroughPublic": 42, "passiveThroughPublic": 42,
        "modifierFinalizable": 29, "passiveFinalizable": 29},
    "modifier-public": {**MODIFIER_CHECKS, "computeFlags": 1, "finalizableFlags": 1,
                        "modifierFinalizable": 29, "passiveFinalizable": 29},
    "modifier-final": {**MODIFIER_CHECKS, "computeFlags": 1, "finalizableFlags": 17,
                       "modifierFinalizable": 30, "passiveFinalizable": 30},
    "modifier-sync": {**MODIFIER_CHECKS, "computeFlags": 33, "finalizableFlags": 1,
                      "modifierFinalizable": 29, "passiveFinalizable": 29},
    "modifier-field-final": {"oldValueBefore": 61, "staticValueBefore": 72,
                             "valueFlags": 17, "staticValueFlags": 25, "oldValue": 61,
                             "oldRead": 61, "staticValue": 72, "staticRead": 72,
                             "newConstructorValue": 41},
    "field-delete": FIELD_CHECKS,
    "field-types": FIELD_CHECKS,
    "class-final": {"classFlags": 1, "oldValue": 29, "newValue": 29},
}
UNCHANGED_CHECKS = {
    "method-delete": ("unchangedMembers",),
    "hierarchy": ("unchangedMembers",),
    "atomic-modifier-public-field-delete": ("unchangedModifierMembers", "unchangedMembers", "unchangedValues"),
    "field-delete": ("unchangedMembers", "unchangedValues"),
    "field-types": ("unchangedMembers", "unchangedValues"),
    "class-final": ("unchangedMembers",),
}


class RunFailure(RuntimeError):
    """A safe failure message; raw command details stay in local evidence."""


def validate_report(report):
    """Require every requested return code and an actual behavior observation."""
    if (not isinstance(report, dict) or type(report.get("schemaVersion")) is not int
            or report["schemaVersion"] != 1):
        raise RunFailure("The device report has an unsupported schema")
    if (report.get("completed") is not True or type(report.get("failures")) is not int
            or report["failures"] != 0):
        raise RunFailure("The device report is incomplete or reports failures")
    if type(report.get("uid")) is not int or report["uid"] < 10000:
        raise RunFailure("Instrumentation did not run under an application UID")
    if report.get("fatalError"):
        raise RunFailure("Instrumentation reported a fatal error")
    device = report.get("device")
    if (not isinstance(device, dict) or type(device.get("api")) is not int
            or device["api"] < 30 or not isinstance(device.get("model"), str)
            or not device["model"] or not isinstance(device.get("release"), str)
            or not device["release"]):
        raise RunFailure("The device report is missing device identity metadata")
    initialization = report.get("initialization")
    if (not isinstance(initialization, dict) or initialization.get("passed") is not True
            or initialization.get("runtimeInitialized") is not True
            or initialization.get("clientAttached") is not True):
        raise RunFailure("The structural engine did not initialize")
    capabilities = initialization.get("capabilities")
    if not isinstance(capabilities, dict):
        raise RunFailure("The initialization report is missing engine capabilities")
    for name in ("installed", "standardRedefineRouted", "memberAddition", "memberModifierChange"):
        if capabilities.get(name) is not True:
            raise RunFailure("The initialization report is missing a required MVP capability")
    for name in ("memberDeletion", "memberTypeChange", "hierarchyChange", "fullDcevmComplete"):
        if capabilities.get(name) is not False:
            raise RunFailure("The engine capabilities do not describe the requested MVP")
    scenarios = report.get("scenarios")
    if not isinstance(scenarios, list) or len(scenarios) != len(SCENARIOS):
        raise RunFailure("The device report does not contain the complete scenario sequence")
    for scenario, (name, variants, code) in zip(scenarios, SCENARIOS):
        if not isinstance(scenario, dict) or scenario.get("name") != name:
            raise RunFailure("The device report has an unexpected scenario order")
        if scenario.get("variants") != variants:
            raise RunFailure("Scenario " + name + " did not submit the requested variants")
        if (type(scenario.get("expectedCode")) is not int or scenario["expectedCode"] != code
                or type(scenario.get("actualCode")) is not int or scenario["actualCode"] != code):
            raise RunFailure("Scenario " + name + " returned an unexpected JVMTI code")
        if (scenario.get("completed") is not True or scenario.get("passed") is not True
                or scenario.get("error")):
            raise RunFailure("Scenario " + name + " failed")
        checks = scenario.get("checks")
        if not isinstance(checks, list) or not checks:
            raise RunFailure("Scenario " + name + " is missing behavior checks")
        names = set()
        for index, check in enumerate(checks):
            if not isinstance(check, dict) or not isinstance(check.get("name"), str) or not check["name"]:
                raise RunFailure("Scenario " + name + " has a malformed behavior check")
            if check["name"] in names:
                raise RunFailure("Scenario " + name + " repeats a behavior check")
            names.add(check["name"])
            if (check.get("passed") is not True or "expected" not in check or "actual" not in check
                    or type(check["expected"]) is not type(check["actual"])
                    or check["expected"] != check["actual"]):
                raise RunFailure("Scenario " + name + " behavior check " + str(index + 1) + " failed")
        observed = {check["name"]: check for check in checks}
        for check_name, expected in {"returnCode": code, **REQUIRED_CHECKS[name]}.items():
            check = observed.get(check_name)
            if (check is None or type(check["expected"]) is not type(expected)
                    or check["expected"] != expected):
                raise RunFailure("Scenario " + name + " is missing a required behavior observation")
        for check_name in UNCHANGED_CHECKS.get(name, ()):
            check = observed.get(check_name)
            if check is None or not isinstance(check["expected"], str) or not check["expected"]:
                raise RunFailure("Scenario " + name + " is missing an unchanged-definition observation")
    return report


def require_instrumentation_success(result):
    if result.returncode != 0:
        raise RunFailure("The instrumentation command returned a nonzero exit code")
    output = result.stdout + "\n" + result.stderr
    codes = re.findall(r"^INSTRUMENTATION_CODE:\s*(-?\d+)\s*$", output, re.MULTILINE)
    if codes != ["-1"] or re.search(r"^INSTRUMENTATION_(?:FAILED|ABORTED):", output, re.MULTILINE):
        raise RunFailure("Android did not report successful instrumentation completion")


def find_adb():
    executable = shutil.which("adb")
    if executable:
        return executable
    sdk = os.environ.get("ANDROID_SDK_ROOT") or os.environ.get("ANDROID_HOME")
    properties = ROOT / "local.properties"
    if not sdk and properties.exists():
        sdk = next((line.split("=", 1)[1] for line in properties.read_text().splitlines()
                    if line.startswith("sdk.dir=")), None)
    return str(Path(sdk) / "platform-tools/adb") if sdk else None


def raw_text(value):
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return value or ""


def safe_console(text, serial):
    text = text.replace(serial, "<device>")
    text = re.sub(r"(?<![\w.])/(?:[^\s\"'<>]+)", "<local-path>", text)
    text = re.sub(r"\b(?:10(?:\.\d{1,3}){3}|192\.168(?:\.\d{1,3}){2}|"
                  r"172\.(?:1[6-9]|2\d|3[01])(?:\.\d{1,3}){2})(?::\d+)?\b", "<private-address>", text)
    return text


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True, help="ADB device selector (kept only in raw evidence)")
    parser.add_argument("--adb", default=find_adb())
    parser.add_argument("--apk", type=Path, default=ROOT / "app/build/outputs/apk/debug/app-debug.apk")
    parser.add_argument("--device-id", default="android-device-1", help="Anonymous label for the verification record")
    parser.add_argument("--timeout", type=int, default=180, help="Instrumentation timeout in seconds")
    args = parser.parse_args(argv)
    if not args.adb:
        parser.error("Use --adb or set the SDK path in local.properties")
    if not args.apk.is_file():
        parser.error("The requested APK is missing; build :app:assembleDebug first")
    if not re.fullmatch(r"[a-z][a-z0-9-]{0,63}", args.device_id) or args.device_id == args.serial:
        parser.error("Use an anonymous --device-id such as android-device-1")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    out = new_evidence_directory(Path.cwd() / "build/evidence/runs", timestamp)
    meta = {"deviceId": args.device_id, "apkSha256": hashlib.sha256(args.apk.read_bytes()).hexdigest(),
            "startedUtc": timestamp, "passed": False}
    # Device selectors and local command paths never leave ignored evidence.
    (out / "selection.json").write_text(json.dumps({"serial": args.serial, "apk": str(args.apk.resolve())}, indent=2))

    def adb(label, *command, timeout=120):
        try:
            result = subprocess.run([args.adb, "-s", args.serial, *command], capture_output=True,
                                    text=True, timeout=timeout)
        except subprocess.TimeoutExpired as error:
            (out / (label + ".txt")).write_text(raw_text(error.stdout) + raw_text(error.stderr))
            (out / (label + ".json")).write_text(json.dumps({"timedOut": True, "timeout": timeout}))
            raise RunFailure("The " + label + " command timed out") from error
        except OSError as error:
            (out / (label + ".txt")).write_text(str(error))
            raise RunFailure("The " + label + " command could not start") from error
        (out / (label + ".txt")).write_text(result.stdout + result.stderr)
        (out / (label + ".json")).write_text(json.dumps({"returncode": result.returncode}))
        return result

    try:
        installed = adb("install", "install", "-r", str(args.apk))
        try:
            require_install_success(installed.returncode, installed.stdout, installed.stderr)
        except RuntimeError as error:
            raise RunFailure("APK installation did not report Success") from error
        paths = adb("package-path", "shell", "pm", "path", PACKAGE)
        if paths.returncode:
            raise RunFailure("The installed APK path command failed")
        try:
            path = base_apk_path(paths.stdout)
        except RuntimeError as error:
            raise RunFailure("Android did not report exactly one installed base APK") from error
        hashed = adb("installed-hash", "shell", "sha256sum", path)
        if hashed.returncode:
            raise RunFailure("The installed APK hash command failed")
        try:
            meta["installedApkSha256"] = verified_apk_sha256(hashed.stdout, meta["apkSha256"])
        except RuntimeError as error:
            raise RunFailure("The installed APK SHA-256 does not match the requested artifact") from error
        stopped = adb("force-stop", "shell", "am", "force-stop", PACKAGE)
        if stopped.returncode:
            raise RunFailure("Android could not stop the previous application process")
        removed = adb("remove-report", "shell", "run-as", PACKAGE, "rm", "-f", REPORT_PATH)
        if removed.returncode:
            raise RunFailure("The previous device report could not be removed")
        instrumentation_error = None
        try:
            test = adb("instrumentation", "shell", "am", "instrument", "-w", "-r", COMPONENT,
                       timeout=args.timeout)
            meta["instrumentationExitCode"] = test.returncode
        except RunFailure as error:
            test = None
            instrumentation_error = error
            meta["instrumentationTimedOut"] = isinstance(error.__cause__, subprocess.TimeoutExpired)
        # Read the report even when Android reports failure so diagnostics are retained.
        try:
            report = adb("report-read", "exec-out", "run-as", PACKAGE, "cat", REPORT_PATH)
            (out / "structural-redefine.json").write_text(report.stdout)
            meta["reportReadExitCode"] = report.returncode
        except RunFailure:
            if instrumentation_error:
                raise instrumentation_error
            raise
        if instrumentation_error:
            raise instrumentation_error
        require_instrumentation_success(test)
        if report.returncode:
            raise RunFailure("The application-private device report could not be read")
        try:
            result = json.loads(report.stdout)
        except (ValueError, TypeError) as error:
            raise RunFailure("The device report is not valid JSON") from error
        validate_report(result)
        meta["device"] = result["device"]
        meta["scenarioCount"] = len(result["scenarios"])
        meta["passed"] = True
        meta["status"] = "structural_redefinition_passed"
    except RunFailure as error:
        meta["status"] = "failed_or_incomplete"
        meta["error"] = str(error)
    (out / "run.json").write_text(json.dumps(meta, ensure_ascii=False, indent=2))
    print(safe_console(json.dumps(meta, ensure_ascii=False, indent=2), args.serial))
    print("Evidence: " + str(out.relative_to(Path.cwd())))
    return 0 if meta["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
