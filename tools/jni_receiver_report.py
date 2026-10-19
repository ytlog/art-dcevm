"""Independent expectations for retained JNI IDs after interface loss."""
from itertools import product
import re


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
    def require(condition, message):
        if not condition: raise ValueError(message)
    require(report.get("schemaVersion") == 1, "Missing JNI report schema")
    require(not report.get("fatalError") and report.get("failures") == 0, "JNI probe failed")
    fields = dict(own=case["receiver"] == "own", interfaceOwner=case["owner"] == "interface",
                  nonvirtual=case["call"] == "nonvirtual", cacheBeforeAttach=case["cache"] == "before",
                  checkJni=case["checkjni"] == "true", form=int(case["form"]))
    for name, value in fields.items():
        require(type(report.get(name)) is type(value) and report[name] == value, "Wrong case field: " + name)
    tag = 0 if case.get("promote") == "true" and case["cache"] == "before" else 1
    require(report.get("idTag") == tag, "Requested pointer/index ID was not observed")
    outcome, expected = expected_checks(case)
    checks = report.get("checks", [])
    require(len(checks) == len(expected), "Missing or extra independent observations")
    for check, (name, value) in zip(checks, expected):
        require(check.get("name") == name, "Wrong observation order")
        for key in ("expected", "actual"):
            require(type(check.get(key)) is type(value) and check[key] == value,
                    "Wrong " + key + " for " + name)
        require(check.get("passed") is True, "Observation did not pass: " + name)
    has_diagnostic = "JNI DETECTED ERROR IN APPLICATION" in crash_log
    if outcome == "checkjni-diagnostic":
        require(not report.get("completed") and report.get("stage") == "r0apply.beforeCall",
                "CheckJNI did not stop at the invalid receiver")
        receiver = "Own" if case["receiver"] == "own" else "DefaultOnly"
        # Nonvirtual class-owned copied IDs fail the original class-argument
        # check first; interface-owned calls reach the receiver check.
        if case["owner"] == "class" and case["call"] == "nonvirtual":
            pattern = (r"can't call nonvirtual (?:<OBSOLETE> )?int .*ReceiverLossFixture\$Contract\.value\(\) "
                       r"with class java\.lang\.Class<.*ReceiverLossFixture\$DefaultOnly>")
        else:
            pattern = (r"can't call (?:<OBSOLETE> )?int .*ReceiverLossFixture\$Contract\.value\(\) on instance of "
                       r".*ReceiverLossFixture\$" + receiver + r"\b")
        require(has_diagnostic and re.search(pattern, crash_log),
                "Missing original CheckJNI receiver diagnostic")
        require("SIGABRT" in crash_log, "Missing diagnostic abort")
    else:
        require(report.get("completed") is True and report.get("stage") == "finished",
                "JNI probe did not complete")
        require("INSTRUMENTATION_CODE: -1" in instrumentation_log, "Missing successful instrumentation exit")
        require(not has_diagnostic and "Fatal signal" not in crash_log, "Unexpected native crash")
    require("This non-structurally obsolete method" not in crash_log, "Copied DEX cache assertion")
    return outcome
