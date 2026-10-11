#!/usr/bin/env python3
"""Test strict declaration lookup using unchanged same-version official resolver."""
import os,subprocess
from pathlib import Path
import runpy
root=Path(__file__).resolve().parents[1]
prepare=runpy.run_path(str(root/"tools/prepare-shadowhook-metadata.py"))["prepare"]

out=root/"build/evidence/host/shadow-declared-symbol";out.mkdir(parents=True,exist_ok=True)
source=out/"official";prepare(source,out/"licenses")
xdl=(source/"third_party/xdl/xdl.c").read_text()
start=xdl.index("typedef struct {\n  unsigned long size;")
end=xdl.index("static void xdl_ensure_dynsym_loaded(",start)
(out/"official_resolver.inc").write_text(xdl[start:end])
flags=["-O1","-g","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-omit-frame-pointer"]
cpp=root/"artdcevm/src/main/cpp"
commands=[[os.environ.get("CC","clang"),"-std=c11",*flags,"-I",str(cpp),"-I",str(out),str(root/"tests/host/shadow_declared_symbol_test.c"),"-o",str(out/"test")],
 [os.environ.get("CXX","clang++"),"-std=c++17",*flags,"-I",str(cpp),"-I",str(root/"tests/host/shadowhook_stub"),str(root/"tests/host/shadow_unique_symbol_test.cc"),str(cpp/"hooks/shadow_hook_engine.cc"),"-o",str(out/"engine-test")]]
for name,command in zip(("test","engine-test"),commands):
 result=subprocess.run(command,capture_output=True,text=True);(out/(name+"-compile.log")).write_text(result.stdout+result.stderr);result.check_returncode()
 result=subprocess.run([str(out/name)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS="halt_on_error=1",UBSAN_OPTIONS="halt_on_error=1"));(out/(name+"-result.log")).write_text(result.stdout+result.stderr);result.check_returncode();print(result.stdout.strip())
# Production must own exactly one decoder, inside official ShadowHook xDL.
assert not (cpp/"support/elf_symbol_names.cc").exists()
assert not (cpp/"third_party/xz_embedded").exists()
assert "elf_symbol_names" not in (cpp/"hooks/shadow_hook_engine.cc").read_text()
