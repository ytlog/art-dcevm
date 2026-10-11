#!/usr/bin/env python3
"""Synthetic metadata guards; actual upstream Android compile is separate."""
import os, subprocess
from pathlib import Path
root = Path(__file__).resolve().parents[1]
out = root / "build/evidence/host/named-symbol-extent";out.mkdir(parents=True,exist_ok=True)
cmd = [os.environ.get("CC", "clang"), "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I", str(root / "artdcevm/src/main/cpp"), str(root / "tests/host/named_symbol_extent_test.c"), "-o", str(out / "test")]
r = subprocess.run(cmd,capture_output=True,text=True);(out / "compile.log").write_text(r.stdout+r.stderr);r.check_returncode()
r = subprocess.run([str(out / "test")],capture_output=True,text=True);(out / "result.log").write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
