#!/usr/bin/env python3
"""Run isolated normal/CheckJNI receiver-loss JNI calls on one device.

index: ordinary debug APK, ID tag 1 before and after attachment.
mixed: debug APK built with -PjniPointerProbe=true; retains tag-0 IDs before
explicit promotion, obtains tag-1 IDs after it. Code 79 is a separate outcome.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

from apk_verification import base_apk_path, require_install_success, verified_apk_sha256
from evidence_paths import new_evidence_directory
from jni_receiver_report import matrix, validate_report

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = "dev.artdcevm.lab"


def audit_evidence(out, expected, id_mode):
    """Replay raw observations without trusting a previous validator's verdict."""
    original = json.loads((out / "run.json").read_text())
    cases = matrix(id_mode)
    if (original.get("idMode") != id_mode or original.get("apkSha256") != expected or
            original.get("installedApkSha256") != expected or
            [row.get("case") for row in original.get("cases", [])] != cases):
        raise ValueError("Evidence does not contain the exact frozen-artifact matrix")
    rows = []
    for index, case in enumerate(cases):
        work = out / str(index)
        report = json.loads((work / "report.json").read_text())
        outcome = validate_report(report, case, (work / "crash.log").read_text(),
                                  (work / "instrumentation.log").read_text())
        rows.append(dict(case=case, outcome=outcome, checks=len(report["checks"]),
                         idTag=report["idTag"], device=report["device"]))
    return dict(passed=True, apkSha256=expected, installedApkSha256=expected, idMode=id_mode,
                deviceId=original["deviceId"], cases=rows,
                outcomes=dict(Counter(row["outcome"] for row in rows)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--serial")
    parser.add_argument("--device-id", default="android-device-1", help="Anonymous evidence label")
    parser.add_argument("--apk", required=True, type=Path)
    parser.add_argument("--id-mode", choices=("index", "mixed"), required=True)
    parser.add_argument("--replay", type=Path, help="Audit a complete raw matrix under build/evidence")
    args = parser.parse_args()
    if not re.fullmatch(r"[a-zA-Z0-9_-]{1,48}", args.device_id):
        parser.error("Use a short anonymous device label")
    expected = hashlib.sha256(args.apk.read_bytes()).hexdigest()
    if args.replay:
        out = args.replay.resolve()
        if not out.is_relative_to((ROOT / "build/evidence").resolve()):
            parser.error("Keep audit evidence under build/evidence")
        try:
            audit = audit_evidence(out, expected, args.id_mode)
        except (OSError, ValueError, KeyError):
            print("JNI matrix replay FAIL; inspect raw evidence")
            return 1
        (out / "audit.json").write_text(json.dumps(audit, indent=2) + "\n")
        print("JNI matrix replay PASS", audit["outcomes"])
        print("Evidence:", out.relative_to(ROOT))
        return 0
    if not args.serial: parser.error("--serial is required for device execution")
    out = new_evidence_directory(ROOT / "build/evidence/jni-receiver" / args.device_id)
    def adb(*command, timeout=180):
        return subprocess.run([args.adb, "-s", args.serial, *command],
                              capture_output=True, text=True, timeout=timeout)
    run = dict(apkSha256=expected, idMode=args.id_mode, deviceId=args.device_id, cases=[], passed=False)
    try:
        result = adb("install", "-r", str(args.apk))
        (out / "install.log").write_text(result.stdout + result.stderr)
        require_install_success(result.returncode, result.stdout, result.stderr)
        installed = base_apk_path(adb("shell", "pm", "path", PACKAGE).stdout)
        run["installedApkSha256"] = verified_apk_sha256(adb("shell", "sha256sum", installed).stdout, expected)
        for index, case in enumerate(matrix(args.id_mode)):
            work = out / str(index)
            work.mkdir()
            since = adb("shell", "date", "+%s.%N").stdout.strip()
            if not re.fullmatch(r"[0-9]{10}\.[0-9]{1,9}", since):
                raise ValueError("Missing device capture start time")
            (work / "capture.json").write_text(json.dumps(dict(deviceStartEpoch=since)) + "\n")
            adb("shell", "am", "force-stop", PACKAGE)
            adb("shell", "run-as", PACKAGE, "rm", "-f", "files/jni-receiver-loss.json")
            options = [value for pair in case.items() for value in ("-e", *pair)]
            result = adb("shell", "am", "instrument", "-w", "-r", *options,
                         PACKAGE + "/com.github.ytlog.artdcevm.testing.JniReceiverLossInstrumentation", timeout=600)
            output = result.stdout + result.stderr
            (work / "instrumentation.log").write_text(output)
            raw = adb("shell", "run-as", PACKAGE, "cat", "files/jni-receiver-loss.json").stdout
            (work / "report.json").write_text(raw)
            row = dict(case=case, passed=False)
            try:
                report = json.loads(raw)
                pid = report.get("pid")
                if not isinstance(pid, int) or pid <= 0: raise ValueError("Missing process identity")
                captured = adb("logcat", "-b", "all", "-d", "-T", since, "--pid=" + str(pid))
                crash = captured.stdout + captured.stderr
                (work / "crash.log").write_text(crash)
                if captured.returncode: raise ValueError("Process log capture failed")
                row["outcome"] = validate_report(report, case, crash, output)
                row["checks"] = len(report["checks"])
                row["idTag"] = report["idTag"]
                row["device"] = report["device"]
                row["passed"] = True
            except (ValueError, KeyError):
                row["error"] = "Report failed independent validation; inspect raw evidence"
            run["cases"].append(row)
            (out / "run.json").write_text(json.dumps(run, indent=2) + "\n")
            print(args.device_id, index + 1, "PASS" if row["passed"] else "FAIL", row.get("outcome", "unverified"), flush=True)
        run["passed"] = all(row["passed"] for row in run["cases"]) and len(run["cases"]) == 96
        run["outcomes"] = dict(Counter(row.get("outcome", "failed") for row in run["cases"]))
    except (OSError, RuntimeError, subprocess.SubprocessError, ValueError):
        run["error"] = "JNI matrix execution failed; inspect raw evidence"
    (out / "run.json").write_text(json.dumps(run, indent=2) + "\n")
    print("JNI matrix", "PASS" if run["passed"] else "FAIL", run.get("outcomes", {}))
    print("Evidence:", out.relative_to(ROOT))
    return 0 if run["passed"] else 1


if __name__ == "__main__": sys.exit(main())
