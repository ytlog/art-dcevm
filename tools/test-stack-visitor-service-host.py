#!/usr/bin/env python3
"""Verify complete callback carriers and the separately compiled native bridge."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
sanitizer = os.environ.get('ARTDCEVM_HOST_SANITIZER', 'address,undefined')
out = root / 'build/evidence/host/stack-visitor-service'
out.mkdir(parents=True, exist_ok=True)
binary = out / ('stack_visitor_service_test_' + sanitizer.replace(',', '-'))
subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-O1', '-g',
                '-Wall', '-Wextra', '-Werror', '-fsanitize=' + sanitizer,
                '-fno-omit-frame-pointer', '-I', str(cpp),
                str(root / 'tests/host/stack_visitor_service_test.cc'),
                str(root / 'tests/host/stack_visitor_native_fixture.cc'), '-o', str(binary)], check=True)
result = subprocess.run([str(binary)], capture_output=True, text=True,
                        env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
(out / ('stack-visitor-service-test-' + sanitizer.replace(',', '-') + '.log')).write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('stack visitor service: passed (' + sanitizer + ')')
