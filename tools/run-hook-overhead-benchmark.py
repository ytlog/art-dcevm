#!/usr/bin/env python3
"""Run HookOverheadBenchmark over interleaved states and summarize ns per call.

Every (round, mode, state) runs in a fresh process; the state order rotates
per round so drift and thermal effects spread across states. Raw reports and
the summary stay under ignored build/evidence.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import statistics
import subprocess
import sys
import time

from apk_verification import base_apk_path, require_install_success, verified_apk_sha256
from evidence_paths import new_evidence_directory
from hook_benchmark_conditions import qualify as qualify_conditions, qualify_units, qualify_foreground

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = "dev.artdcevm.lab"
COMPONENT = PACKAGE + "/com.github.ytlog.artdcevm.testing.HookOverheadBenchmark"
ACTIVITY = PACKAGE + "/com.github.ytlog.artdcevm.testing.HookBenchmarkActivity"
REPORT_PATH = "files/hook-overhead.json"
STATES = ("none", "agent", "engine", "engine-client", "added", "removed")
# Opt-in controls: redefinitions without the engine's transaction (--states).
CONTROLS = ("art-body", "art-added", "engine-body")
# Activates non-public interface selection before loading the late classes.
SELECTION = ("selected",)
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
DELTAS += (("selected-added", "selected", "added"),)

_runner = importlib.util.spec_from_file_location("run_device_tests", ROOT / "tools/run-device-tests.py")
_tests = importlib.util.module_from_spec(_runner)
_runner.loader.exec_module(_tests)
safe_console, safe_report, find_adb = _tests.safe_console, _tests.safe_report, _tests.find_adb


def launch_foreground(adb, finished, save_launch, timeout=30):
    """Launch once, only after this instrumentation installed its ActivityMonitor."""
    deadline = time.monotonic() + timeout
    while not finished() and time.monotonic() < deadline:
        raw = adb("exec-out", "run-as", PACKAGE, "cat", REPORT_PATH, timeout=5)
        try:
            report = json.loads(raw.stdout)
        except ValueError:
            report = None
        if isinstance(report, dict) and report.get("foregroundLaunchRequested") is True:
            if "completed" in report:
                raise ValueError("Foreground instrumentation ended before launch")
            launch = adb("shell", "am", "start", "-W", "-n", ACTIVITY, timeout=10)
            output = launch.stdout + launch.stderr
            save_launch(output)
            if launch.returncode != 0 or not re.search(r"^Status: ok\s*$", output, re.M):
                raise ValueError("Explicit foreground activity launch failed")
            return
        if isinstance(report, dict) and "completed" in report:
            break
        time.sleep(.1)
    raise ValueError("Foreground instrumentation did not request a launch before its deadline")


def safe_benchmark_report(raw, serial):
    """Keep the exact public cpuset name needed to verify foreground evidence."""
    report = safe_report(raw, serial)
    if not isinstance(raw, dict):
        return report
    pairs = [(raw, report)]
    if isinstance(raw.get("workloads"), dict):
        pairs.extend((metric, report["workloads"][name])
                     for name, metric in raw["workloads"].items() if isinstance(metric, dict))
    for original, sanitized in pairs:
        if original.get("foregroundCpuset") == "/top-app":
            sanitized["foregroundCpuset"] = "/top-app"
    return report


def affinity_matches(report, cpu):
    if cpu is None:
        if "cpuAffinity" not in report:
            return True
        cpu = -1
    workloads = report.get("workloads")
    return (type(report.get("cpuAffinity")) is int and report["cpuAffinity"] == cpu
            and isinstance(workloads, dict) and bool(workloads)
            and all(isinstance(metric, dict) and type(metric.get("cpuAffinity")) is int
                    and metric["cpuAffinity"] == cpu for metric in workloads.values()))


def report_conditions(report, *, mode, state, checkjni, cpu, foreground, stable_jni, clock_units):
    """Use the same admission checks for live reports and offline recomputation."""
    if not isinstance(report, dict) or report.get("completed") is not True:
        raise ValueError("Benchmark did not complete")
    if report.get("mode") != mode or report.get("state") != state:
        raise ValueError("Reported benchmark cell mismatch")
    if checkjni is not None and report.get("checkJni") != checkjni:
        raise ValueError("Reported CheckJNI mode mismatch")
    if not affinity_matches(report, cpu):
        raise ValueError("Reported CPU affinity mismatch")
    qualify_foreground(report, foreground)
    result = {}
    if stable_jni:
        result["steadyConditions"] = qualify_conditions(report)
    if clock_units:
        result["unitClockConditions"] = qualify_units(report, clock_units)
    setup = report.get("setup")
    if not isinstance(setup, dict):
        raise ValueError("Missing benchmark setup checks")
    if state == "selected" and setup.get("interfaceSelectionActive") is not True:
        raise ValueError("Interface selection was not activated")
    schema = report.get("schemaVersion", 1)
    if type(schema) is not int or schema < 1:
        raise ValueError("Invalid benchmark schema")
    if schema >= 2 and setup.get("methodHandleChecks") is not True:
        raise ValueError("MethodHandle result checks failed")
    if schema >= 3 and setup.get("arrayAssignabilityChecks") is not True:
        raise ValueError("Array assignability result checks failed")
    return result


def completed_reports(directory, *, cpu, foreground, stable_jni, clock_units):
    """Require the complete original matrix; failed or missing cells cannot vanish."""
    try:
        meta = json.loads((directory / "run.json").read_text())
    except (OSError, ValueError):
        raise ValueError("Missing or invalid benchmark run manifest") from None
    if not isinstance(meta, dict) or meta.get("passed") is not True:
        raise ValueError("Benchmark run manifest records an incomplete or failed run")
    rounds, modes, states, runs = (meta.get(key) for key in ("rounds", "modes", "states", "runs"))
    if (type(rounds) is not int or rounds <= 0 or
            not isinstance(modes, list) or not modes or not all(mode in MODES for mode in modes) or
            len(set(modes)) != len(modes) or not isinstance(states, list) or not states or
            not all(state in STATES + CONTROLS + SELECTION + PROBES for state in states) or
            len(set(states)) != len(states) or not isinstance(runs, list)):
        raise ValueError("Invalid benchmark run matrix")
    if (meta.get("cpuAffinity") != cpu or meta.get("foreground", False) is not foreground or
            meta.get("requireStableJni", False) is not stable_jni or meta.get("clockUnits", []) != clock_units):
        raise ValueError("Summary conditions differ from the original benchmark run")
    expected = {f"r{index}-{mode}-{state}": (mode, state)
                for index in range(rounds) for mode in modes for state in states}
    labels = [run.get("label") if isinstance(run, dict) else None for run in runs]
    if (len(labels) != len(expected) or any(type(label) is not str for label in labels) or
            set(labels) != set(expected) or
            any(run.get("completed") is not True or run.get("instrumentationOk") is not True for run in runs)):
        raise ValueError("Benchmark run has missing, duplicate or failed cells")
    if {path.name for path in directory.glob("r*-*.json")} != {label + ".json" for label in expected}:
        raise ValueError("Raw benchmark reports differ from the run manifest")
    reports = []
    for label, (mode, state) in expected.items():
        try:
            report = json.loads((directory / (label + ".json")).read_text())
        except (OSError, ValueError):
            raise ValueError("Missing or invalid raw benchmark report") from None
        report_conditions(report, mode=mode, state=state, checkjni=meta.get("checkJni"),
                          cpu=cpu, foreground=foreground, stable_jni=stable_jni, clock_units=clock_units)
        reports.append({**report, "round": label.split("-")[0]})
    return reports


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
        present = [state for state in STATES + CONTROLS + SELECTION + PROBES if state in states]
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
    parser.add_argument("--cpu-affinity", type=int, help="Bind only the benchmark thread to this CPU; verify each metric")
    parser.add_argument("--require-stable-jni", action="store_true",
                        help="Require all JNI clock-control samples, frequency residency and GC to be stable")
    parser.add_argument("--clock-units", default="",
                        help="Condition and qualify methodHandleBound,reflectNewInstance with references outside all batches")
    parser.add_argument("--foreground", action="store_true",
                        help="Require an unlocked focused benchmark window and actual top-app worker group")
    parser.add_argument("--probe-unwind", action="store_true",
                        help="Probe states only: let the original instrumentation thread exit before redefining")
    parser.add_argument("--evidence-root", type=Path, default=ROOT / "build/evidence/hook-overhead/runs")
    parser.add_argument("--summarize", type=Path, nargs="+", metavar="DIR",
                        help="Only recompute the summary from the reports in these evidence directories")
    args = parser.parse_args(argv)
    if args.cpu_affinity is not None and not 0 <= args.cpu_affinity < 1024:
        parser.error("CPU affinity must be in 0..1023")
    if args.require_stable_jni and args.cpu_affinity is None:
        parser.error("Stable JNI control requires --cpu-affinity")
    clock_units = args.clock_units.split(",") if args.clock_units else []
    if (len(set(clock_units)) != len(clock_units) or
            not set(clock_units) <= {"methodHandleBound", "reflectNewInstance"}):
        parser.error("Unsupported or repeated clock units")
    if clock_units and args.cpu_affinity is None:
        parser.error("Unit clock controls require --cpu-affinity")
    if args.summarize:
        reports = []
        if len({directory.resolve() for directory in args.summarize}) != len(args.summarize):
            parser.error("A benchmark evidence directory cannot be summarized twice")
        for index, directory in enumerate(args.summarize):
            try:
                cells = completed_reports(directory, cpu=args.cpu_affinity, foreground=args.foreground,
                                          stable_jni=args.require_stable_jni, clock_units=clock_units)
            except ValueError as error:
                parser.error(str(error))
            reports.extend({**report, "round": str(index) + "/" + report["round"]} for report in cells)
        print(markdown(summarize(reports)))
        return 0
    modes, states = args.modes.split(","), args.states.split(",")
    if not args.adb: parser.error("Use --adb or set the SDK path in local.properties")
    if not args.apk.is_file(): parser.error("The requested APK is missing; build :app:assembleDebug first")
    if not re.fullmatch(r"[a-z][a-z0-9-]{0,63}", args.device_id) or args.device_id == args.serial:
        parser.error("Use an anonymous --device-id such as android-device-1")
    if not set(modes) <= set(MODES) or not set(states) <= set(STATES + CONTROLS + SELECTION + PROBES) or args.rounds <= 0:
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
            "checkJni": args.checkjni, "probeUnwind": args.probe_unwind,
            "cpuAffinity": args.cpu_affinity, "requireStableJni": args.require_stable_jni, "runs": []}
    meta["clockUnits"] = clock_units
    meta["foreground"] = args.foreground
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
                command = ("shell", "am", "instrument", "-w", "-r", "-e", "state", state, "-e", "mode", mode,
                           "-e", "probeunwind", str(args.probe_unwind).lower(),
                           "-e", "cpu", "none" if args.cpu_affinity is None else str(args.cpu_affinity),
                           "-e", "steadyConditioning", str(args.require_stable_jni).lower(),
                           *(["-e", "clockUnits", args.clock_units] if clock_units else []),
                           *(["-e", "foreground", "true"] if args.foreground else []),
                           "-e", "checkjni", args.checkjni, COMPONENT)
                launch_error = None
                if args.foreground:
                    with ThreadPoolExecutor(max_workers=1) as executor:
                        pending = executor.submit(adb, *command, timeout=args.timeout)
                        try:
                            launch_foreground(adb, pending.done, lambda output:
                                (out / (label + "-foreground.txt")).write_text(safe_console(output, args.serial)))
                        except (ValueError, subprocess.TimeoutExpired) as error:
                            launch_error = (str(error) if isinstance(error, ValueError)
                                            else "Foreground launch command timed out")
                            adb("shell", "am", "force-stop", PACKAGE)
                        test = pending.result()
                else:
                    test = adb(*command, timeout=args.timeout)
                (out / (label + ".txt")).write_text(safe_console(test.stdout + test.stderr, args.serial))
                raw = adb("exec-out", "run-as", PACKAGE, "cat", REPORT_PATH).stdout
                (out / "raw").mkdir(exist_ok=True)
                (out / "raw" / (label + ".json")).write_text(raw)
                run = {"label": label, "instrumentationOk": launch_error is None and "INSTRUMENTATION_CODE: -1" in test.stdout}
                if launch_error:
                    run["foregroundLaunchError"] = launch_error
                try:
                    report = safe_benchmark_report(json.loads(raw), args.serial)
                    if not isinstance(report, dict):
                        raise ValueError("Benchmark report is not an object")
                except ValueError:
                    report = None
                if report is not None:
                    (out / (label + ".json")).write_text(json.dumps(report, indent=2))
                    run["completed"] = False
                    try:
                        run.update(report_conditions(report, mode=mode, state=state, checkjni=args.checkjni,
                                                     cpu=args.cpu_affinity, foreground=args.foreground,
                                                     stable_jni=args.require_stable_jni, clock_units=clock_units))
                        run["completed"] = True
                    except ValueError as error:
                        run["error"] = report.get("fatalError") or str(error)
                    if run["completed"] and run["instrumentationOk"]: reports.append({**report, "round": round_index})
                    else: run.setdefault("error", report.get("fatalError"))
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
