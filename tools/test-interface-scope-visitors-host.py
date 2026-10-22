#!/usr/bin/env python3
"""Call both fixed HandleScope visitor prototypes and reject missing services."""
from pathlib import Path
import shutil
import subprocess
import tempfile

from evidence_paths import new_evidence_directory

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "platform/abi/arm64/common/handle_scope_visitor.h"
#include <iostream>
#include <stdexcept>
using namespace artdcevm::abi::arm64;
void* thread = reinterpret_cast<void*>(0x1000);
RootVisitor* roots = reinterpret_cast<RootVisitor*>(0x2000);
int signed_calls = 0, unsigned_calls = 0;
void Require(bool condition) { if (!condition) throw std::runtime_error("Scope visitor ABI mismatch"); }
void Signed(void* self, RootVisitor* visitor, int32_t tid) {
  Require(self == thread && visitor == roots && tid == -2); ++signed_calls;
}
void Unsigned(void* self, RootVisitor* visitor, uint32_t tid) {
  Require(self == thread && visitor == roots && tid == 0xfffffffeu); ++unsigned_calls;
}
int main() {
  try {
    HandleScopeVisitors service;
    Require(!service.Available() && !service.Visit(true, thread, roots, 0xfffffffeu) &&
            !service.Visit(false, thread, roots, 0xfffffffeu));
    service.signed_ids = Signed;
    Require(service.Available() && service.Visit(true, thread, roots, 0xfffffffeu) &&
            !service.Visit(false, thread, roots, 0xfffffffeu));
    service.unsigned_ids = Unsigned;
    Require(service.Visit(false, thread, roots, 0xfffffffeu));
    service.signed_ids = nullptr;
    Require(!service.Visit(true, thread, roots, 0xfffffffeu) && signed_calls == 1 && unsigned_calls == 1);
    std::cout << "Exact signed/unsigned scope visitors PASS\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
'''


def main():
    out = new_evidence_directory(ROOT / 'build/evidence/host/interface-scope-visitors')
    with tempfile.TemporaryDirectory(prefix='cases-', dir=out) as directory:
        work = Path(directory)
        source, binary = work / 'test.cc', work / 'test'
        source.write_text(SOURCE)
        r = subprocess.run([shutil.which('clang++') or shutil.which('c++'), '-std=c++17',
            '-Wall', '-Wextra', '-Werror', '-I' + str(ROOT / 'artdcevm/src/main/cpp'),
            str(source), '-o', str(binary)], capture_output=True, text=True)
        (out / 'compiler-raw.log').write_text(r.stdout + r.stderr)
        if r.returncode:raise RuntimeError('Scope visitor compilation failed; inspect ignored evidence')
        r = subprocess.run([str(binary)], capture_output=True, text=True)
        (out / 'cases.log').write_text(r.stdout + r.stderr)
        if r.returncode:raise RuntimeError('Scope visitor cases failed; inspect ignored evidence')
        print(r.stdout.strip())
    print('Evidence: ' + str(out.relative_to(ROOT)))


if __name__ == '__main__':main()
