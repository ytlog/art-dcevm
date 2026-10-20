"""Shared strict JNI observation and original CheckJNI termination checks."""
import re


def validate_observations(report, fields, id_tag, expected):
    def require(condition, message):
        if not condition: raise ValueError(message)
    require(type(report.get("schemaVersion")) is int and report["schemaVersion"] == 1, "Missing JNI report schema")
    require(not report.get("fatalError") and type(report.get("failures")) is int and report["failures"] == 0, "JNI probe failed")
    for name, value in fields.items():
        require(type(report.get(name)) is type(value) and report[name] == value, "Wrong case field: " + name)
    require(type(report.get("idTag")) is int and report["idTag"] == id_tag, "Requested pointer/index ID was not observed")
    checks = report.get("checks", [])
    require(len(checks) == len(expected), "Missing or extra independent observations")
    for check, (name, value) in zip(checks, expected):
        require(check.get("name") == name, "Wrong observation order")
        for key in ("expected", "actual"):
            require(type(check.get(key)) is type(value) and check[key] == value,
                    "Wrong " + key + " for " + name)
        require(check.get("passed") is True, "Observation did not pass: " + name)


def validate_termination(report, case, outcome, crash_log, instrumentation_log, fixture, diagnostic_stage):
    def require(condition, message):
        if not condition: raise ValueError(message)
    require("SIGSEGV" not in crash_log and "SIGBUS" not in crash_log, "Unexpected native memory fault")
    has_diagnostic = "JNI DETECTED ERROR IN APPLICATION" in crash_log
    if outcome == "checkjni-diagnostic":
        require(not report.get("completed") and report.get("stage") == diagnostic_stage,
                "CheckJNI did not stop at the intended invalid receiver")
        receiver = "Own" if case["receiver"] == "own" else "DefaultOnly"
        prefix = re.escape(fixture)
        if case["owner"] == "class" and case["call"] == "nonvirtual":
            pattern = (r"can't call nonvirtual (?:<OBSOLETE> )?int .*" + prefix + r"\$Contract\.value\(\) "
                       r"with class java\.lang\.Class<.*" + prefix + r"\$DefaultOnly>")
        else:
            pattern = (r"can't call (?:<OBSOLETE> )?int .*" + prefix + r"\$Contract\.value\(\) on instance of "
                       r".*" + prefix + r"\$" + receiver + r"\b")
        require(has_diagnostic and re.search(pattern, crash_log), "Missing original CheckJNI receiver diagnostic")
        require("SIGABRT" in crash_log, "Missing diagnostic abort")
    else:
        require(report.get("completed") is True and report.get("stage") == "finished", "JNI probe did not complete")
        require("INSTRUMENTATION_CODE: -1" in instrumentation_log, "Missing successful instrumentation exit")
        require(not has_diagnostic and "Fatal signal" not in crash_log, "Unexpected native crash")
    require("This non-structurally obsolete method" not in crash_log, "Copied DEX cache assertion")
