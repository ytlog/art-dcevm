#!/usr/bin/env python3
"""Check the bounded DEX code scanner against D8 output.

The engine uses the scanner to prove that no loaded class outside a batch still
invokes a method whose invoke kind changes. Instruction widths and payloads
must stay in step, so the sample puts switch and array payloads, wide
constants and every invoke kind ahead of the references it looks for.
"""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

JAVA = {
    'sample/Callee.java': '''package sample;
public class Callee {
    public static int twice(int value) { return value * 2; }
    public int value() { return 7; }
    public static int unused(int value) { return value; }
}
''',
    'sample/Base.java': '''package sample;
public class Base extends Callee {}
''',
    'sample/Caller.java': '''package sample;
public class Caller {
    static long wide = 0x123456789abcdefL;
    static final int[] TABLE = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    public int call(Base base, int key) {
        int total = 0;
        switch (key) { case 0: total = 3; break; case 1: total = 4; break; case 2: total = 5; break; default: total = 6; }
        switch (key) { case 10: total += 1; break; case 1000: total += 2; break; case 100000: total += 3; break; }
        long[] values = {wide, 2L, 3L};
        total += (int) values[key % 3] + TABLE[key % 9];
        total += String.valueOf(total).length();
        return total + Callee.twice(key) + base.value();
    }
}
''',
    'sample/Bystander.java': '''package sample;
public class Bystander {
    public int other(Callee callee) { return callee.hashCode() + Callee.unused(1) * 0; }
}
''',
}

SOURCE = r'''
#include "evolution/class_schema.h"
#include "platform/common/method_identity.h"
#include <cstring>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
using namespace artdcevm::evolution;
static int failures = 0;
static void Require(bool ok, const char* what) { if (!ok) { std::printf("FAIL %s\n", what); ++failures; } }
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  std::ifstream file(argv[1], std::ios::binary);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  DexView view{bytes.data(), bytes.size(), bytes.data(), bytes.size()};
  std::string error;
  const std::set<std::string> callee{"Lsample/Callee;", "Lsample/Base;"};
  std::vector<uint32_t> twice, value, unused, absent;
  Require(FindMethodIds(view, callee, "twice", "(I)I", &twice, &error) && twice.size() == 1, "static method id");
  // The declaration's own id and the call site's id through the subclass.
  Require(FindMethodIds(view, callee, "value", "()I", &value, &error) && value.size() == 2,
          "virtual method ids of the class and its subclass");
  Require(FindMethodIds(view, callee, "unused", "(I)I", &unused, &error) && unused.size() == 1, "unused id");
  Require(FindMethodIds(view, callee, "twice", "(J)I", &absent, &error) && absent.empty(), "prototype must match");
  // The DEX symbolic owner can be a subclass rather than the resolved
  // declaration. Identity must read each method's own MethodId, and the
  // defining loader remains part of the result.
  std::set<std::string> owners;
  for (uint32_t id : value) {
    std::string owner;
    Require(artdcevm::method_identity::DeclaringDescriptor(view, id, &owner), "method owner from real D8 DEX");
    Require(callee.count(owner), "unexpected symbolic owner"); owners.insert(owner);
    Require(artdcevm::method_identity::Matches(owner, 1, owner.c_str(), 1) &&
            !artdcevm::method_identity::Matches(owner, 1, owner.c_str(), 2), "same descriptor in different defining loader");
  }
  Require(owners == callee, "collapsed distinct symbolic owners");
  std::string owner;
  Require(!artdcevm::method_identity::DeclaringDescriptor(view, ~uint32_t{0}, &owner), "invalid method id accepted");
  auto corrupt = bytes; uint32_t offset = ~uint32_t{0};
  std::memcpy(corrupt.data()+92, &offset, 4);
  DexView bad{corrupt.data(),corrupt.size(),corrupt.data(),corrupt.size()};
  Require(!artdcevm::method_identity::DeclaringDescriptor(bad, 0, &owner), "out of range owner table accepted");
  corrupt = bytes; bad = {corrupt.data(),corrupt.size(),corrupt.data(),corrupt.size()};
  std::memcpy(&offset, corrupt.data()+92, 4);
  uint16_t invalid = 0xffff; std::memcpy(corrupt.data()+offset, &invalid, 2);
  Require(!artdcevm::method_identity::DeclaringDescriptor(bad, 0, &owner), "invalid owner type accepted");
  uint32_t caller = 0, bystander = 0, callee_def = 0;
  Require(FindClassDef(view, "Lsample/Caller;", &caller, &error), "Caller class def");
  Require(FindClassDef(view, "Lsample/Bystander;", &bystander, &error), "Bystander class def");
  Require(FindClassDef(view, "Lsample/Callee;", &callee_def, &error), "Callee class def");
  uint32_t missing = 0;
  Require(!FindClassDef(view, "Lsample/Missing;", &missing, &error) && error.empty(), "absent class def");
  bool found = false;
  Require(CodeReferencesMethods(view, caller, twice, &found, &error) && found, "invoke-static after payloads");
  Require(CodeReferencesMethods(view, caller, value, &found, &error) && found, "invoke-virtual through subclass");
  Require(CodeReferencesMethods(view, caller, unused, &found, &error) && !found, "unreferenced method");
  Require(CodeReferencesMethods(view, bystander, twice, &found, &error) && !found, "other class code");
  Require(CodeReferencesMethods(view, bystander, unused, &found, &error) && found, "bystander reference");
  Require(CodeReferencesMethods(view, callee_def, twice, &found, &error) && !found, "declaring class code");
  std::vector<uint8_t> truncated(bytes.begin(), bytes.begin() + bytes.size() / 2);
  DexView cut{truncated.data(), bytes.size(), truncated.data(), truncated.size()};
  Require(!CodeReferencesMethods(cut, caller, twice, &found, &error) && !error.empty(), "truncated DEX fails");
  std::printf(failures ? "DEX code scanner FAIL\n" : "DEX code scanner PASS\n");
  return failures ? 1 : 0;
}
'''


def sdk_dir():
    for line in (ROOT / 'local.properties').read_text().splitlines():
        if line.startswith('sdk.dir='):
            return Path(line.split('=', 1)[1].strip())
    raise RuntimeError('local.properties has no sdk.dir')


def main():
    sdk = sdk_dir()
    d8 = sdk / 'build-tools/35.0.0/d8'
    evidence = ROOT / 'build/evidence/host/dex-code'
    evidence.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='cases-', dir=evidence) as directory:
        work = Path(directory)
        for name, text in JAVA.items():
            path = work / 'src' / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        classes = work / 'classes'
        subprocess.run(['javac', '--release', '9', '-d', str(classes),
                        *[str(work / 'src' / name) for name in JAVA]], check=True)
        out = work / 'dex'
        out.mkdir()
        subprocess.run([str(d8), '--min-api', '30', '--output', str(out),
                        *[str(p) for p in sorted(classes.rglob('*.class'))]], check=True)
        source = work / 'test.cc'
        source.write_text(SOURCE)
        binary = work / 'test'
        native = ROOT / 'artdcevm/src/main/cpp'
        build = subprocess.run([shutil.which('clang++') or shutil.which('c++'), '-std=c++17', '-Wall', '-Wextra',
                                '-Werror', '-I' + str(native), str(source),
                                str(native / 'evolution/dex_schema.cc'), '-o', str(binary)],
                               capture_output=True, text=True)
        (evidence / 'compile.log').write_text(build.stdout + build.stderr)
        if build.returncode:
            raise RuntimeError('Host compilation failed; see build/evidence/host/dex-code/compile.log')
        test = subprocess.run([str(binary), str(out / 'classes.dex')], capture_output=True, text=True)
        (evidence / 'test.log').write_text(test.stdout + test.stderr)
        if test.returncode:
            raise RuntimeError('Host assertions failed; see build/evidence/host/dex-code/test.log')
        print(test.stdout.strip())


if __name__ == '__main__':
    main()
