#!/usr/bin/env python3
"""Run HookOverheadBenchmark over interleaved states and summarize ns per call.

Every (round, mode, state) runs in a fresh process; the state order rotates
per round so drift and thermal effects spread across states. Raw reports and
the summary stay under ignored build/evidence.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import statistics
import subprocess
import sys

from apk_verification import base_apk_path, require_install_success, verified_apk_sha256
from evidence_paths import new_evidence_directory

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = "dev.artdcevm.lab"
COMPONENT = PACKAGE + "/com.github.ytlog.artdcevm.testing.HookOverheadBenchmark"
REPORT_PATH = "files/hook-overhead.json"
STATES = ("none", "agent", "engine", "engine-client", "added", "removed")
# Opt-in controls: redefinitions without the engine's transaction (--states).
CONTROLS = ("art-body", "art-added", "engine-body")
# Debug-only ART 13 observation and restoration experiments, opt-in.
PROBES = tuple(state + "-" + probe for state in ("art-added", "added")
               for probe in ("observe", "restore", "jvmti"))
MODES = ("jit", "interp")
# (label, compared state, baseline state)
DELTAS = (("agent-none", "agent", "none"), ("engine-agent", "engine", "agent"),
          ("engine-none", "engine", "none"), ("added-engineClient", "added", "engine-client"),
          ("removed-added", "removed", "added"), ("removed-engineClient", "removed", "engine-client"),
          ("artBody-engineClient", "art-body", "engine-client"), ("artAdded-engineClient", "art-added", "engine-client"),
          ("engineBody-engineClient", "engine-body", "engine-client"))

_runner = importlib.util.spec_from_file_location("run_device_tests", ROOT / "tools/run-device-tests.py")
_tests = importlib.util.module_from_spec(_runner)
_runner.loader.exec_module(_tests)
safe_console, safe_report, find_adb = _tests.safe_console, _tests.safe_report, _tests.find_adb


def summarize(reports):
    """Median over processes of each process's median batch, per mode/state/metric.

    Deltas also carry pairedPercent: the median over rounds of the ratio
    between two states measured in the same round, which cancels drift
    between rounds (thermal state, background load).
    """
    table, rounds = {}, {}
    for report in reports:
        by_round = rounds.setdefault(report["mode"], {}).setdefault(report["round"], {})
        by_round[report["state"]] = {**{name: value["medianNs"] for name, value in report["workloads"].items()},
                                     **{"once." + name: value for name, value in report["once"].items()}}
        cell = table.setdefault(report["mode"], {}).setdefault(report["state"], {})
        for name, value in report["workloads"].items():
            cell.setdefault(name, []).append(value["medianNs"])
        for name, value in report["once"].items():
            if name != "checksum": cell.setdefault("once." + name, []).append(value)
        for name in ("initializeNanos", "redefineNanos"):
            if name in report["setup"]: cell.setdefault("setup." + name, []).append(report["setup"][name])
    summary = {}
    for mode, states in table.items():
        for state, metrics in states.items():
            for name, values in metrics.items():
                summary.setdefault(mode, {}).setdefault(state, {})[name] = {
                    "median": statistics.median(values), "min": min(values), "max": max(values), "n": len(values)}
    for mode, states in summary.items():
        deltas = {}
        for label, compared, baseline in DELTAS:
            if compared not in states or baseline not in states: continue
            for name, value in states[compared].items():
                base = states[baseline].get(name)
                if not base or name.startswith("setup."): continue
                ratios = [cells[compared][name] / cells[baseline][name]
                          for cells in rounds[mode].values()
                          if name in cells.get(compared, {}) and cells.get(baseline, {}).get(name)]
                deltas.setdefault(label, {})[name] = {
                    "delta": value["median"] - base["median"],
                    "percent": 100.0 * (value["median"] / base["median"] - 1.0),
                    "pairedPercent": 100.0 * (statistics.median(ratios) - 1.0) if ratios else None,
                    "pairedRounds": len(ratios)}
        states["deltas"] = deltas
    return summary


def markdown(summary):
    lines = []
    for mode, states in summary.items():
        present = [state for state in STATES + CONTROLS + PROBES if state in states]
        names = sorted({name for state in present for name in states[state]}, key=lambda n: (n.startswith(("once.", "setup.")), n))
        labels = [label for label, _, _ in DELTAS if label in states["deltas"]]
        lines += ["", "mode=" + mode + " (median ns over processes; deltas are paired same-round medians)", "",
                  "| metric | " + " | ".join(present) + " | " + " | ".join(labels) + " |",
                  "|---|" + "---:|" * (len(present) + len(labels))]
        for name in names:
            cells = ["%.1f" % states[s][name]["median"] if name in states[s] else "" for s in present]
            for label in labels:
                delta = states["deltas"][label].get(name)
                paired = delta and delta["pairedPercent"]
                cells.append("%+.1f%%" % paired if paired is not None else "")
            lines.append("| " + name + " | " + " | ".join(cells) + " |")
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True, help="ADB device selector (kept only in raw evidence)")
    parser.add_argument("--adb", default=find_adb())
    parser.add_argument("--apk", type=Path, default=ROOT / "app/build/outputs/apk/debug/app-debug.apk")
    parser.add_argument("--device-id", default="android-device-1", help="Anonymous label for the record")
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--modes", default=",".join(MODES))
    parser.add_argument("--states", default=",".join(STATES))
    parser.add_argument("--checkjni", choices=("true", "false"), default="false",
                        help="JNI checking mode selected before any agent (Debug apps default to true)")
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--probe-unwind", action="store_true",
                        help="Probe states only: let the original instrumentation thread exit before redefining")
    parser.add_argument("--evidence-root", type=Path, default=ROOT / "build/evidence/hook-overhead/runs")
    parser.add_argument("--summarize", type=Path, nargs="+", metavar="DIR",
                        help="Only recompute the summary from the reports in these evidence directories")
    args = parser.parse_args(argv)
    if args.summarize:
        reports = []
        for directory in args.summarize:
            for path in sorted(directory.glob("r*-*.json")):
                report = json.loads(path.read_text())
                if report.get("completed") is True:
                    reports.append({**report, "round": directory.name + "/" + path.name.split("-")[0]})
        print(markdown(summarize(reports)))
        return 0
    modes, states = args.modes.split(","), args.states.split(",")
    if not args.adb: parser.error("Use --adb or set the SDK path in local.properties")
    if not args.apk.is_file(): parser.error("The requested APK is missing; build :app:assembleDebug first")
    if not re.fullmatch(r"[a-z][a-z0-9-]{0,63}", args.device_id) or args.device_id == args.serial:
        parser.error("Use an anonymous --device-id such as android-device-1")
    if not set(modes) <= set(MODES) or not set(states) <= set(STATES + CONTROLS + PROBES) or args.rounds <= 0:
        parser.error("Unsupported modes, states or rounds")
    if not args.evidence_root.resolve().is_relative_to((ROOT / "build/evidence").resolve()):
        parser.error("Evidence must remain under ignored build/evidence")
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    out = new_evidence_directory(args.evidence_root.resolve(), timestamp)

    def adb(*command, timeout=120):
        result = subprocess.run([args.adb, "-s", args.serial, *command], capture_output=True, text=True,
                                timeout=timeout)
        return result

    meta = {"deviceId": args.device_id, "apkSha256": hashlib.sha256(args.apk.read_bytes()).hexdigest(),
            "startedUtc": timestamp, "rounds": args.rounds, "modes": modes, "states": states,
            "checkJni": args.checkjni, "probeUnwind": args.probe_unwind, "runs": []}
    installed = adb("install", "-r", str(args.apk))
    require_install_success(installed.returncode, installed.stdout, installed.stderr)
    path = base_apk_path(adb("shell", "pm", "path", PACKAGE).stdout)
    meta["installedApkSha256"] = verified_apk_sha256(adb("shell", "sha256sum", path).stdout, meta["apkSha256"])
    reports = []
    for round_index in range(args.rounds):
        for mode in modes:
            shift = round_index % len(states)
            for state in states[shift:] + states[:shift]:
                label = "r%d-%s-%s" % (round_index, mode, state)
                adb("shell", "am", "force-stop", PACKAGE)
                adb("shell", "run-as", PACKAGE, "rm", "-f", REPORT_PATH)
                test = adb("shell", "am", "instrument", "-w", "-r", "-e", "state", state, "-e", "mode", mode,
                           "-e", "probeunwind", str(args.probe_unwind).lower(),
                           "-e", "checkjni", args.checkjni, COMPONENT, timeout=args.timeout)
                (out / (label + ".txt")).write_text(safe_console(test.stdout + test.stderr, args.serial))
                raw = adb("exec-out", "run-as", PACKAGE, "cat", REPORT_PATH).stdout
                run = {"label": label, "instrumentationOk": "INSTRUMENTATION_CODE: -1" in test.stdout}
                try:
                    report = safe_report(json.loads(raw), args.serial)
                except ValueError:
                    report = None
                if report is not None:
                    (out / (label + ".json")).write_text(json.dumps(report, indent=2))
                    run["completed"] = report.get("completed") is True
                    if run["completed"] and run["instrumentationOk"]: reports.append({**report, "round": round_index})
                    else: run["error"] = report.get("fatalError")
                    probe = report.get("instrumentationProbe", {})
                    if "restorationSucceeded" in probe:
                        run["restoration"] = "restored" if probe["restorationSucceeded"] else "blocked"
                meta["runs"].append(run)
                print(label, "ok" if run.get("completed") and run["instrumentationOk"] else "FAILED",
                      ("restore=" + run["restoration"]) if "restoration" in run else "", flush=True)
    if reports: meta["device"] = reports[0]["device"]
    summary = summarize(reports)
    meta["passed"] = len(reports) == len(meta["runs"])
    (out / "run.json").write_text(json.dumps(meta, indent=2))
    (out / "summary.json").write_text(json.dumps(summary, indent=2))
    table = markdown(summary)
    (out / "summary.md").write_text(table + "\n")
    print(table)
    print("Evidence: " + str(out.relative_to(ROOT)))
    return 0 if meta["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
