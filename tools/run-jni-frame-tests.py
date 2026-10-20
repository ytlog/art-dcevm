#!/usr/bin/env python3
"""Run retained JNI IDs across active native/Java frames, GC and transactions.

index uses ordinary debug APK; mixed requires -PjniPointerProbe=true and proves
retained tag0 IDs before promotion and tag1 IDs after it. Code79 is separate.
"""
from jni_probe_runner import Probe, audit_evidence as audit_probe, main as run_probe
from jni_frame_report import matrix, validate_report

PROBE = Probe("jni-frames", "jni-frames.json", "JniFrameInstrumentation", matrix, validate_report)


def audit_evidence(out, expected, id_mode):
    return audit_probe(out, expected, id_mode, PROBE)


def main():
    return run_probe(PROBE, __doc__)


if __name__ == "__main__":
    raise SystemExit(main())
