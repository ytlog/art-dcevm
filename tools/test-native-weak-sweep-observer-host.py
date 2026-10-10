#!/usr/bin/env python3
import os, pathlib, subprocess
root = pathlib.Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
out = root / 'build/evidence/host/native-weak-sweep-observer'
out.mkdir(parents=True, exist_ok=True)
subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I', str(cpp), str(root / 'tests/host/native_weak_sweep_observer_test.cc'), str(cpp / 'platform/common/native_weak_sweep_observer.cc'), '-o', str(out / 'test')], check=True)
r = subprocess.run([str(out / 'test')], capture_output=True, text=True, env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
(out / 'result.log').write_text(r.stdout + r.stderr)
r.check_returncode()
print(r.stdout.strip())
