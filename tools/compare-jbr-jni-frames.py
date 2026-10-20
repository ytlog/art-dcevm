#!/usr/bin/env python3
"""Pin old Java frames and retained JNI locals/IDs against the fixed JBR.

The index/pointer distinction is ART-specific and is checked by the device
runner. JBR checks both default VM execution and -Xint in fresh processes.
Invalid CheckJNI calls and failed batches are recorded separately from calls.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import platform
import subprocess

from evidence_paths import new_evidence_directory

ROOT = Path(__file__).resolve().parents[1]
REVISION = "cf1f197170a0f04c6559feb873e362583be021d1"
FIXTURES = ROOT / "app/src/debug/java/com/github/ytlog/artdcevm/fixture"


def expected(own, interface, nonvirtual, native_hold, checked):
    baseline = 7 if nonvirtual and interface else 11 if own else 7
    values = {"threadPolicy": "true", "before": str(baseline)}
    diagnostic = checked and (interface or not own)
    for round_ in range(2):
        values[f"r{round_}held"] = "4"
        for phase in range(4):
            p = f"r{round_}p{phase}."
            changed = phase in (1, 2)
            # This fixed JBR reports InternalError for the malformed peer's
            # classfile. ART instead injects a mapping-plan fault before commit.
            if phase % 2 == 0: values[p + "failure"] = "java.lang.InternalError"
            values.update({p + "interface": str(not changed).lower(),
                p + "peer": str(13 if changed else 3), p + "identity": "true", p + "self": "true",
                p + "kept": str(23 if own else 19), p + "gcIdentity": "true", p + "gcSelf": "true",
                p + "gcInterface": str(not changed).lower()})
            current = ("java.lang.IncompatibleClassChangeError" if changed and not nonvirtual
                       and not (own and not interface) else str(baseline))
            if diagnostic and changed and native_hold: return True, values
            values[p + "frame"] = current if native_hold else str(baseline)
            if diagnostic and changed: return True, values
            values[p + "followup"] = current
    return False, values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--java-home", type=Path, required=True)
    args = parser.parse_args()
    home = args.java_home.resolve()
    java = home / "bin"
    out = new_evidence_directory(ROOT / "build/evidence/jbr-jni-frames")
    work = out / "work"
    work.mkdir()

    def run(name, command):
        result = subprocess.run(list(map(str, command)), cwd=work, capture_output=True, timeout=120)
        (out / (name + ".log")).write_bytes(result.stdout + result.stderr)
        return result

    def require(name, command):
        result = run(name, command)
        if result.returncode: raise RuntimeError(name + " failed; inspect ignored evidence")
        return result

    require("version", [java / "java", "-version"])
    if "JBR-21.0.11+10-1163.116" not in (out / "version.log").read_text():
        raise RuntimeError("Requires fixed JBR 21.0.11+10-b1163.116")
    old, new = work / "original", work / "drop"
    old.mkdir(); new.mkdir()
    sources = [FIXTURES / ("JniFrame" + name + ".java") for name in ("Fixture", "Gate", "Probe")]
    harness = ROOT / "tools/oracles/JniFramesOracle.java"
    native = ROOT / "app/src/debug/cpp/testing/jni_frame_probe.cc"
    drop = ROOT / "fixtures/jni-frames-drop/com/github/ytlog/artdcevm/fixture/JniFrameFixture.java"
    require("compile", [java / "javac", "--release", "11", "-d", old, *sources, harness])
    require("compile-drop", [java / "javac", "--release", "11", "-cp", old, "-d", new, drop])
    (work / "agent.mf").write_text("Premain-Class: JniFramesOracle\nCan-Redefine-Classes: true\n\n")
    require("jar", [java / "jar", "cfm", work / "agent.jar", work / "agent.mf",
                    "-C", old, "JniFramesOracle.class"])
    darwin = platform.system() == "Darwin"
    library = work / ("frames.dylib" if darwin else "frames.so")
    require("native", ["clang++", "-std=c++17", "-dynamiclib" if darwin else "-shared", "-fPIC",
        "-Wall", "-Wextra", "-Werror", "-I" + str(home / "include"),
        "-I" + str(home / "include" / ("darwin" if darwin else "linux")), native, "-o", library])
    rows = []
    for mode, own, interface, nonvirtual, form, native_hold, checked in itertools.product(
            ("default", "interp"), (False, True), (False, True), (False, True), range(3),
            (False, True), (False, True)):
        case = dict(mode=mode, own=own, interfaceOwner=interface, nonvirtual=nonvirtual,
                    form=form, nativeHold=native_hold, checkjni=checked)
        name = f"{mode}-own{int(own)}-interface{int(interface)}-nv{int(nonvirtual)}-form{form}-native{int(native_hold)}-check{int(checked)}"
        result = run(name, [java / "java", *(["-Xint"] if mode == "interp" else []),
            *(["-Xcheck:jni"] if checked else []), "-XX:+AllowEnhancedClassRedefinition",
            "-XX:ErrorFile=" + str(work / "hs-error-%p.log"), "-javaagent:" + str(work / "agent.jar"),
            "-cp", old, "JniFramesOracle", library, new, old, str(own).lower(), str(interface).lower(),
            str(nonvirtual).lower(), form, str(native_hold).lower()])
        lines = [line.split("=", 1) for line in result.stdout.decode().splitlines()
                 if line.startswith(("threadPolicy=", "before=", "r0", "r1"))]
        observed = dict(lines)
        diagnostic, wanted = expected(own, interface, nonvirtual, native_hold, checked)
        log = (result.stdout + result.stderr).decode(errors="replace")
        termination = (result.returncode == -6 and "Wrong object class or methodID passed to JNI call" in log
                       if diagnostic else result.returncode == 0)
        passed = len(observed) == len(lines) and observed == wanted and termination
        rows.append(dict(case=case, passed=passed, outcome="checkjni-diagnostic" if diagnostic else "call-aligned",
                         observations=observed, expected=wanted, exitCode=result.returncode))
        print(name, "PASS" if passed else "FAIL", flush=True)
    report = dict(passed=all(row["passed"] for row in rows), fixedJbrSource=REVISION,
        scope="Default VM and interpreter; no claim of compiled active Java frames", rows=rows,
        sources={str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                 for p in [*sources, harness, native, drop, Path(__file__)]})
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print("JBR JNI frames", sum(row["passed"] for row in rows), "/", len(rows))
    print("Evidence:", out.relative_to(ROOT))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
