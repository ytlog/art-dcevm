"""Require successful installation and the exact installed artifact before tests."""
import re

def require_install_success(returncode, stdout, stderr=""):
    output=stdout+"\n"+stderr
    if returncode or re.search(r"\bFailure\s*\[",output) or not re.search(r"^Success\s*$",output,re.M):
        raise RuntimeError("APK installation did not report Success: "+output.strip())

def base_apk_path(output):
    paths=[line[len("package:"):] for line in output.splitlines()
           if line.startswith("package:") and line.endswith("/base.apk")]
    if len(paths)!=1:
        raise RuntimeError("Expected exactly one installed base APK")
    return paths[0]

def verified_apk_sha256(output, expected):
    match=re.match(r"^([0-9a-fA-F]{64})\s",output)
    if not match:
        raise RuntimeError("No valid SHA-256 for installed APK")
    actual=match.group(1).lower()
    if actual!=expected.lower():
        raise RuntimeError("Installed APK hash differs from the requested frozen artifact")
    return actual
