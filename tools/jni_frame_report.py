"""Independent held-frame JNI expectations; rejection and diagnostics are separate."""
from jni_probe_report import validate_observations, validate_termination
from jni_receiver_report import matrix as receiver_matrix


def matrix(id_mode):
    return [dict(case, frame=frame) for case in receiver_matrix(id_mode) for frame in ("native", "body")]


def expected_checks(case):
    checks = []
    def add(name, value): checks.append((name, value))
    own, interface = case["receiver"] == "own", case["owner"] == "interface"
    nonvirtual, hold_native = case["call"] == "nonvirtual", case["frame"] == "native"
    baseline = 7 if nonvirtual and interface else 11 if own else 7
    if case.get("promote") == "true": add("promoted", 0)
    add("idAvailable", True)
    add("threadPolicy", True)
    add("baseline.jni", baseline)

    def state(label, compatible, peer):
        for name, value in (("interface", compatible), ("peer", peer), ("identity", True),
                ("self", True), ("kept", 23 if own else 19), ("gcIdentity", True),
                ("gcSelf", True), ("gcInterface", compatible)):
            add(label + "." + name, value)

    if case.get("admission") == "pointer-reject":
        add("pointerId", 0)
        for round_ in range(2):
            add(f"r{round_}.held", 3)
            for phase, code, peer in (("reject", 79, 3), ("peerApply", 0, 13), ("peerRestore", 0, 3)):
                label = f"r{round_}{phase}"
                add(label + ".code", code)
                state(label, True, peer)
                add(label + ".frame", baseline)
                add(label + ".jni", baseline)
        return "admission-rejected-79", checks
    diagnostic = case["checkjni"] == "true" and (interface or not own)
    for round_ in range(2):
        add(f"r{round_}.held", 4)
        for phase, name in enumerate(("abort", "apply", "restoreAbort", "restore")):
            label, fail, changed = f"r{round_}{name}", phase % 2 == 0, phase in (1, 2)
            if fail: add(label + ".armed", 0)
            add(label + ".code", 110 if fail else 0)
            if fail: add(label + ".faultHits", 1)
            state(label, not changed, 13 if changed else 3)
            boundary = "java.lang.IncompatibleClassChangeError" if (
                changed and not nonvirtual and not (own and not interface)) else baseline
            if diagnostic and changed and hold_native: return "checkjni-diagnostic", checks
            add(label + ".frame", boundary if hold_native else baseline)
            if diagnostic and changed: return "checkjni-diagnostic", checks
            add(label + ".jni", boundary)
    return "call-aligned", checks


def validate_report(report, case, crash_log, instrumentation_log):
    fields = dict(own=case["receiver"] == "own", interfaceOwner=case["owner"] == "interface",
                  nonvirtual=case["call"] == "nonvirtual", cacheBeforeAttach=case["cache"] == "before",
                  checkJni=case["checkjni"] == "true", form=int(case["form"]), frame=case["frame"])
    tag = 0 if case.get("promote") == "true" and case["cache"] == "before" else 1
    outcome, expected = expected_checks(case)
    validate_observations(report, fields, tag, expected)
    stage = "r0apply." + ("resumeBeforeCall" if case["frame"] == "native" else "followupBeforeCall")
    validate_termination(report, case, outcome, crash_log, instrumentation_log, "JniFrameFixture", stage)
    return outcome
