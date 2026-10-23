"""Qualify the allocation-free JNI clock control without adjusting timings."""
import math
import statistics


def _samples(values, count):
    if (not isinstance(values, list) or len(values) != count or
            any(type(v) not in (int, float) or not math.isfinite(v) or v <= 0 for v in values)):
        raise ValueError("Missing or invalid clock-control samples")
    return sorted(values)


def _spread(values, label):
    median = statistics.median(values)
    central = values[len(values) * 9 // 10] - values[len(values) // 10]
    extent = values[-1] - values[0]
    if central > .01 * median or extent > .02 * median:
        raise ValueError(label + " clock-control samples are not stable")
    return median, central / median, extent / median


def _residency(text):
    try:
        rows = [tuple(map(int, row.split())) for row in text.splitlines()]
        if (not rows or any(len(row) != 2 or row[0] <= 0 or row[1] < 0 for row in rows)
                or len(dict(rows)) != len(rows)):
            raise ValueError()
        return dict(rows)
    except (AttributeError, TypeError, ValueError):
        raise ValueError("Missing or invalid frequency residency") from None


def _clock(data):
    refs = _samples(data.get("referenceBeforeNs"), 21) + _samples(data.get("referenceAfterNs"), 21)
    reference, reference_spread, reference_range = _spread(sorted(refs), "Reference")
    before, after = _residency(data.get("frequencyBefore")), _residency(data.get("frequencyAfter"))
    if before.keys() != after.keys() or any(after[f] < n for f, n in before.items()):
        raise ValueError("Frequency residency changed shape or reset")
    delta = {f: after[f] - n for f, n in before.items()}
    total = sum(delta.values())
    frequency = max(delta, key=delta.get)
    if total <= 0 or delta[frequency] < .98 * total:
        raise ValueError("Frequency residency is not stable")
    return {"referenceMedianNs": reference, "referenceP90P10": reference_spread,
            "referenceRange": reference_range, "frequencyKHz": frequency,
            "dominantFraction": delta[frequency] / total}


def _gc(data):
    try:
        if any(type(data[key]) not in (str, int) or type(data[key]) is bool
               or (type(data[key]) is str and not data[key].isdecimal())
               for key in ("gcBefore", "gcAfter")):
            raise ValueError()
        first, last = int(data["gcBefore"]), int(data["gcAfter"])
    except (KeyError, ValueError, TypeError):
        raise ValueError("Missing GC clock-control counters") from None
    if first < 0 or last < first:
        raise ValueError("GC counters reset")
    return last - first


def _bound(report):
    if type(report.get("cpuAffinity")) is not int or report["cpuAffinity"] < 0:
        raise ValueError("Clock control requires a bound worker")


def qualify(report):
    """Reject the whole cell on unstable conditions; never drop samples."""
    _bound(report)
    data = report.get("steadyConditions")
    if not isinstance(data, dict):
        raise ValueError("Missing steady-state conditions")
    metric = report.get("workloads", {}).get("jniCallIntMethod", {})
    count = metric.get("callsPerBatch")
    if (type(count) is not int or not 0 < count <= 1 << 26 or
            type(metric.get("checksum")) is not int or
            metric["checksum"] != count * (count - 1) // 2 * 21):
        raise ValueError("JNI clock control is missing real-call results")
    cpu, cpu_spread, cpu_range = _spread(_samples(data.get("cpuSamplesNs"), 21), "JNI CPU")
    clock = _clock(data)
    if _gc(data):
        raise ValueError("GC changed during allocation-free JNI clock control")
    return {"cpuMedianNs": cpu, "cpuP90P10": cpu_spread, "cpuRange": cpu_range,
            **clock}


def qualify_units(report, units):
    """Check references around allocating units, retaining their GC costs."""
    _bound(report)
    if report.get("clockUnits") != ",".join(units):
        raise ValueError("Reported clock units differ")
    result = {}
    for unit in units:
        metric = report.get("workloads", {}).get(unit, {})
        count = metric.get("callsPerBatch")
        # Both fixed E0 fixtures return 3: area(3), or the constructor's field.
        if (unit not in ("methodHandleBound", "reflectNewInstance") or
                type(count) is not int or not 0 < count <= 1 << 26 or
                type(metric.get("checksum")) is not int or metric["checksum"] != 3 * count * 21):
            raise ValueError("Unit clock control is missing real-call results")
        data = metric.get("clockConditions")
        if not isinstance(data, dict):
            raise ValueError("Missing unit clock conditions: " + unit)
        cpu = _samples(data.get("cpuSamplesNs"), 21)
        result[unit] = {**_clock(data), "gcCycles": _gc(data),
                        "cpuMedianBatchNs": statistics.median(cpu)}
    return result


def compare(control, candidate):
    """Check both engines ran at equivalent clocks, keeping raw ns unchanged."""
    reference = control["referenceMedianNs"]
    difference = candidate["referenceMedianNs"] - reference
    if control["frequencyKHz"] != candidate["frequencyKHz"] or abs(difference) > .01 * reference:
        raise ValueError("Control and candidate clock conditions differ")
    return difference / reference


def qualify_foreground(report, required):
    """Require observed window and worker-group conditions, not a request flag."""
    if report.get("foreground", False) is not required:
        raise ValueError("Reported foreground condition differs")
    if not required:
        return
    if report.get("foregroundReady") is not True or report.get("foregroundCpuset") != "/top-app":
        raise ValueError("Foreground window or top-app scheduling group was not proved")
    epoch = report.get("foregroundEpoch")
    if type(epoch) is not int or epoch < 0:
        raise ValueError("Missing foreground process continuity observation")
    workloads = report.get("workloads")
    if not isinstance(workloads, dict) or not workloads:
        raise ValueError("Missing foreground workload observations")
    for metric in workloads.values():
        if (not isinstance(metric, dict) or type(metric.get("foregroundEpoch")) is not int or
                metric["foregroundEpoch"] != epoch or metric.get("foregroundCpuset") != "/top-app"):
            raise ValueError("Missing foreground unit continuity observations")
