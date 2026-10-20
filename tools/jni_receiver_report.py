"""Independent expectations for retained JNI IDs after interface loss."""
from itertools import product
from jni_probe_report import validate_observations, validate_termination


def matrix(id_mode):
    cases = []
    for cache, receiver, owner, call, form, checked in product(
            ("before", "after"), ("default", "own"), ("interface", "class"),
            ("virtual", "nonvirtual"), ("0", "1", "2"), ("false", "true")):
        case = dict(cache=cache, receiver=receiver, owner=owner, call=call,
                    form=form, checkjni=checked)
        if id_mode == "mixed":
            case["promote"] = "true"
            if cache == "before" and receiver == "own" and owner == "class":
                case["admission"] = "pointer-reject"
        cases.append(case)
    return cases


def expected_checks(case):
    result = []
    def add(name, value): result.append((name, value))
    own, interface = case["receiver"] == "own", case["owner"] == "interface"
    nonvirtual = case["call"] == "nonvirtual"
    baseline = 7 if nonvirtual and interface else 11 if own else 7
    if case.get("promote") == "true": add("promoted", 0)
    add("idAvailable", True)
    add("baseline.jni", baseline)
    if case.get("admission") == "pointer-reject":
        add("pointerId", 0)
        for round_ in range(2):
            label = f"reject{round_}"
            for name, value in (("code", 79), ("interface", True), ("peer", 3),
                    ("identity", True), ("self", True), ("kept", 23),
                    ("jni", baseline), ("gc.jni", baseline)):
                add(label + "." + name, value)
            for phase, peer in (("followApply", 13), ("followRestore", 3)):
                for name, value in (("code", 0), ("peer", peer), ("jni", baseline)):
                    add(label + "." + phase + "." + name, value)
        return "admission-rejected-79", result
    diagnostic = case["checkjni"] == "true" and (interface or not own)
    for round_ in range(2):
        for phase, suffix in enumerate(("abort", "apply", "restoreAbort", "restore")):
            label, failed, changed = f"r{round_}{suffix}", phase % 2 == 0, phase in (1, 2)
            if failed: add(label + ".armed", 0)
            add(label + ".code", 110 if failed else 0)
            if failed: add(label + ".faultHits", 1)
            for name, value in (("interface", not changed), ("peer", 13 if changed else 3),
                    ("identity", True), ("self", True), ("kept", 23 if own else 19)):
                add(label + "." + name, value)
            if diagnostic and changed: return "checkjni-diagnostic", result
            value = "java.lang.IncompatibleClassChangeError" if (
                changed and not nonvirtual and not (own and not interface)) else baseline
            add(label + ".jni", value)
            add(label + ".gc.jni", value)
    return "call-aligned", result


def validate_report(report, case, crash_log, instrumentation_log):
    fields = dict(own=case["receiver"] == "own", interfaceOwner=case["owner"] == "interface",
                  nonvirtual=case["call"] == "nonvirtual", cacheBeforeAttach=case["cache"] == "before",
                  checkJni=case["checkjni"] == "true", form=int(case["form"]))
    tag = 0 if case.get("promote") == "true" and case["cache"] == "before" else 1
    outcome, expected = expected_checks(case)
    validate_observations(report, fields, tag, expected)
    validate_termination(report, case, outcome, crash_log, instrumentation_log,
                         "ReceiverLossFixture", "r0apply.beforeCall")
    return outcome
