#!/usr/bin/env python3
"""Resolve the project version and whether this main run needs publication."""

import os
from pathlib import Path
import re
import subprocess


def read_version(properties, *, allow_missing=False):
    values = []
    for line in properties.splitlines():
        match = re.fullmatch(
            r"[ \t]*publishVersion(?:[ \t]*[=:][ \t]*|[ \t]+|$)(.*)", line,
        )
        if match:
            values.append(match.group(1).strip())
    if not values and allow_missing:
        return None
    if len(values) != 1:
        raise ValueError("Expected one publishVersion property")
    version = values[0]
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?", version):
        raise ValueError("Invalid project version")
    if "SNAPSHOT" in version.upper():
        raise ValueError("Use an immutable release version")
    return version


def main():
    version = read_version(Path("gradle.properties").read_text())
    event = os.environ["GITHUB_EVENT_NAME"]
    if event == "workflow_dispatch":
        should_publish = True
    elif event == "push":
        before = os.environ["BEFORE_SHA"]
        if not re.fullmatch(r"[0-9a-f]{40}", before):
            raise ValueError("Invalid previous revision")
        previous = None
        if before != "0" * 40:
            result = subprocess.run(
                ["git", "show", f"{before}:gradle.properties"],
                capture_output=True, text=True, check=False,
            )
            if result.returncode:
                raise ValueError("Cannot read the previous project's version")
            previous = read_version(result.stdout, allow_missing=True)
        should_publish = version != previous
    else:
        raise ValueError("Unsupported workflow event")

    with Path(os.environ["GITHUB_OUTPUT"]).open("a") as output:
        output.write(f"version={version}\n")
        output.write(f"publish={str(should_publish).lower()}\n")
    print(f"Project version: {version}; publication requested: {should_publish}")


if __name__ == "__main__":
    try:
        main()
    except (KeyError, OSError, ValueError) as error:
        raise SystemExit(f"::error::{error}")
